package main

import (
	"bytes"
	"crypto/sha256"
	"database/sql"
	"encoding/json"
	"fmt"
	"net/http/httptest"
	"os"
	"testing"
	"time"
)

func makeUploadApp(t *testing.T) (*App, string, []byte) {
	t.Helper()
	app, path, key := makeApp(t)
	// Stage the uploads in a per-test temp dir, never the host's default.
	app.uploadDir = t.TempDir()
	return app, path, key
}

func chunkedInit(t *testing.T, app *App, body map[string]any) (map[string]any, *httptest.ResponseRecorder) {
	t.Helper()
	rr := api(t, app, "POST", "/v1/assets/upload/init", body)
	if rr.Code != 201 {
		t.Fatalf("init: HTTP %d: %s", rr.Code, rr.Body.String())
	}
	var response map[string]any
	if err := json.Unmarshal(rr.Body.Bytes(), &response); err != nil {
		t.Fatal(err)
	}
	return response, rr
}

func uploadPart(t *testing.T, app *App, uploadID string, index int, data []byte) *httptest.ResponseRecorder {
	t.Helper()
	req := httptest.NewRequest("POST", "/v1/assets/upload/"+uploadID+"/part?index="+fmt.Sprint(index), bytes.NewReader(data))
	req.Header.Set("Authorization", "Bearer "+testToken)
	req.Header.Set("X-Lumen-Document-Version", "2")
	req.Header.Set("Content-Type", "application/octet-stream")
	rr := httptest.NewRecorder()
	app.Handler().ServeHTTP(rr, req)
	return rr
}

func TestChunkedUploadMultiPartLifecycle(t *testing.T) {
	app, path, key := makeUploadApp(t)
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Chunk owner"}))

	total := int64(5<<20 + 5<<20 + 2<<20 + 1000)
	init, _ := chunkedInit(t, app, map[string]any{
		"filename": "large.bin", "mime": "text/plain", "size": total, "object_id": owner.ID,
	})
	uploadID, _ := init["upload_id"].(string)
	assetID, _ := init["asset_id"].(string)
	if uploadID == "" || assetID == "" {
		t.Fatalf("init response incomplete: %v", init)
	}

	// Out-of-order parts are rejected before anything is staged.
	if rr := uploadPart(t, app, uploadID, 2, bytes.Repeat([]byte{7}, chunkedUploadMinPart)); rr.Code != 400 {
		t.Fatalf("out-of-order part accepted: HTTP %d", rr.Code)
	}
	// A non-final part below the minimum size is rejected.
	if rr := uploadPart(t, app, uploadID, 1, bytes.Repeat([]byte{7}, 1<<20)); rr.Code != 400 {
		t.Fatalf("small non-final part accepted: HTTP %d", rr.Code)
	}

	first := bytes.Repeat([]byte{0x61}, 5<<20)
	second := bytes.Repeat([]byte{0x62}, 5<<20)
	third := bytes.Repeat([]byte{0x63}, 2<<20+1000)
	for index, data := range [][]byte{first, second, third} {
		rr := uploadPart(t, app, uploadID, index+1, data)
		if rr.Code != 200 {
			t.Fatalf("part %d: HTTP %d: %s", index+1, rr.Code, rr.Body.String())
		}
		var progress map[string]any
		if err := json.Unmarshal(rr.Body.Bytes(), &progress); err != nil {
			t.Fatal(err)
		}
		if got, _ := progress["parts"].(float64); int(got) != index+1 {
			t.Fatalf("part counter wrong: %v", progress)
		}
	}

	// complete before all bytes would fail; here everything arrived.
	complete := api(t, app, "POST", "/v1/assets/upload/"+uploadID+"/complete", nil)
	if complete.Code != 201 {
		t.Fatalf("complete: HTTP %d: %s", complete.Code, complete.Body.String())
	}
	var asset Asset
	if err := json.Unmarshal(complete.Body.Bytes(), &asset); err != nil {
		t.Fatal(err)
	}
	if asset.ID != assetID || asset.Size != int(total) || asset.ObjectID != owner.ID || asset.MIME != "text/plain; charset=utf-8" {
		t.Fatalf("asset mismatch: %+v", asset)
	}

	// The owner carries the attachment metadata.
	updated := objectFrom(t, api(t, app, "GET", "/v1/objects/"+owner.ID, nil))
	attachments, _ := updated.Properties["assets"].([]any)
	if len(attachments) != 1 {
		t.Fatalf("owner metadata missing: %v", updated.Properties["assets"])
	}

	// The bytes round-trip exactly through the chunked streaming path.
	expected := append(append(append([]byte{}, first...), second...), third...)
	download := api(t, app, "GET", "/v1/assets/"+asset.ID+"/content", nil)
	if download.Code != 200 {
		t.Fatalf("download: HTTP %d", download.Code)
	}
	if !bytes.Equal(download.Body.Bytes(), expected) {
		t.Fatal("chunked asset bytes changed")
	}
	if download.Header().Get("Content-Length") != fmt.Sprint(total) {
		t.Fatalf("content length header: %q", download.Header().Get("Content-Length"))
	}

	// Storage shape: no whole-blob row, three chunk rows, reassembly via
	// Blob (the path the exporter uses).
	var whole int
	if err := app.store.db.QueryRow("SELECT count(*) FROM blobs WHERE id=?", asset.ID).Scan(&whole); err != nil || whole != 0 {
		t.Fatalf("chunked asset also stored a whole blob: %d %v", whole, err)
	}
	count, err := app.store.BlobChunkCount(asset.ID)
	if err != nil || count != 3 {
		t.Fatalf("chunk count: %d %v", count, err)
	}
	assembled, err := app.store.Blob(asset.ID)
	if err != nil || !bytes.Equal(assembled, expected) {
		t.Fatal("Blob reassembly failed")
	}

	// Staging is gone after complete.
	if _, err := os.Stat(app.uploadSessionDir(uploadID)); !os.IsNotExist(err) {
		t.Fatal("staging directory survived complete")
	}

	// The chunks survive a store restart.
	app.store.Close()
	reopened, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	app.store = reopened
	again := api(t, app, "GET", "/v1/assets/"+asset.ID+"/content", nil)
	if !bytes.Equal(again.Body.Bytes(), expected) {
		t.Fatal("chunked asset did not survive the restart")
	}
	// fsck (run by makeApp cleanup) validates the chunk invariants.
}

func TestChunkedUploadSinglePartAndMimeSniffing(t *testing.T) {
	app, _, _ := makeUploadApp(t)
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Small chunk owner"}))
	init, _ := chunkedInit(t, app, map[string]any{
		"filename": "tiny.bin", "mime": "application/x-custom", "size": 1000, "object_id": owner.ID,
	})
	uploadID, _ := init["upload_id"].(string)
	// Binary content has no known signature: the declared mime survives, as
	// in POST /v1/assets where DetectContentType decides.
	if rr := uploadPart(t, app, uploadID, 1, bytes.Repeat([]byte{0x81, 0x02, 0x7f}, 334)[:1000]); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	complete := api(t, app, "POST", "/v1/assets/upload/"+uploadID+"/complete", nil)
	if complete.Code != 201 {
		t.Fatalf("single-part complete: HTTP %d: %s", complete.Code, complete.Body.String())
	}
	var asset Asset
	if err := json.Unmarshal(complete.Body.Bytes(), &asset); err != nil {
		t.Fatal(err)
	}
	if asset.MIME != "application/x-custom" {
		t.Fatalf("declared mime lost: %+v", asset)
	}
	// Textual content is sniffed like the small upload path does.
	text, _ := chunkedInit(t, app, map[string]any{
		"filename": "tiny.txt", "mime": "application/x-custom", "size": 1000, "object_id": owner.ID,
	})
	textID, _ := text["upload_id"].(string)
	if rr := uploadPart(t, app, textID, 1, bytes.Repeat([]byte("z"), 1000)); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	sniffed := api(t, app, "POST", "/v1/assets/upload/"+textID+"/complete", nil)
	var sniffedAsset Asset
	if err := json.Unmarshal(sniffed.Body.Bytes(), &sniffedAsset); err != nil {
		t.Fatal(err)
	}
	if sniffedAsset.MIME != "text/plain; charset=utf-8" {
		t.Fatalf("sniffed mime lost: %+v", sniffedAsset)
	}

	// A renamed text file becomes a generic file, never an image.
	standalone, _ := chunkedInit(t, app, map[string]any{
		"filename": "lying.png", "mime": "image/png", "size": len("definitely not png"),
	})
	standaloneID, _ := standalone["upload_id"].(string)
	if rr := uploadPart(t, app, standaloneID, 1, []byte("definitely not png")); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	rejected := api(t, app, "POST", "/v1/assets/upload/"+standaloneID+"/complete", nil)
	if rejected.Code != 201 || !bytes.Contains(rejected.Body.Bytes(), []byte(`"type_id":"file"`)) {
		t.Fatalf("sniffed standalone rejection: HTTP %d %s", rejected.Code, rejected.Body.String())
	}

	// A genuine PDF becomes a media object, like the small upload path.
	pdfBytes := []byte("%PDF-1.4\nchunked standalone pdf\n")
	pdf, _ := chunkedInit(t, app, map[string]any{"filename": "chunked.pdf", "mime": "application/pdf", "size": len(pdfBytes)})
	pdfID, _ := pdf["upload_id"].(string)
	if rr := uploadPart(t, app, pdfID, 1, pdfBytes); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	done := api(t, app, "POST", "/v1/assets/upload/"+pdfID+"/complete", nil)
	if done.Code != 201 {
		t.Fatalf("standalone PDF complete: HTTP %d %s", done.Code, done.Body.String())
	}
	var pdfAsset Asset
	if err := json.Unmarshal(done.Body.Bytes(), &pdfAsset); err != nil {
		t.Fatal(err)
	}
	if pdfAsset.MediaObjectID == "" || pdfAsset.MediaObject == nil || pdfAsset.MediaObject.TypeID != "pdf" {
		t.Fatalf("standalone chunked PDF did not create a media object: %+v", pdfAsset)
	}
	media := objectFrom(t, api(t, app, "GET", "/v1/objects/"+pdfAsset.MediaObjectID, nil))
	if media.Properties["asset_id"] != pdfAsset.ID {
		t.Fatal("media object does not reference the chunked asset")
	}
}

func TestChunkedUploadValidationAndAbuse(t *testing.T) {
	app, _, _ := makeUploadApp(t)
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Validation owner"}))

	for name, body := range map[string]map[string]any{
		"no-filename":   {"mime": "text/plain", "size": 10, "object_id": owner.ID},
		"zero-size":     {"filename": "x.bin", "mime": "text/plain", "size": 0, "object_id": owner.ID},
		"negative-size": {"filename": "x.bin", "mime": "text/plain", "size": -5, "object_id": owner.ID},
		"too-large":     {"filename": "x.bin", "mime": "text/plain", "size": chunkedUploadMaxSize + 1, "object_id": owner.ID},
		"no-mime":       {"filename": "x.bin", "size": 10, "object_id": owner.ID},
		"unknown-owner": {"filename": "x.bin", "mime": "text/plain", "size": 10, "object_id": "missing-object"},
	} {
		rr := api(t, app, "POST", "/v1/assets/upload/init", body)
		if rr.Code != 400 && rr.Code != 404 {
			t.Fatalf("%s: HTTP %d: %s", name, rr.Code, rr.Body.String())
		}
	}

	// Unknown uploads answer 404 on every operation.
	if rr := api(t, app, "POST", "/v1/assets/upload/no-such-id/complete", nil); rr.Code != 404 {
		t.Fatalf("unknown complete: HTTP %d", rr.Code)
	}
	if rr := api(t, app, "POST", "/v1/assets/upload/no-such-id/abort", nil); rr.Code != 404 {
		t.Fatalf("unknown abort: HTTP %d", rr.Code)
	}
	if rr := uploadPart(t, app, "no-such-id", 1, []byte("x")); rr.Code != 404 {
		t.Fatalf("unknown part: HTTP %d", rr.Code)
	}

	// Complete before every byte arrived.
	init, _ := chunkedInit(t, app, map[string]any{"filename": "x.bin", "mime": "text/plain", "size": chunkedUploadMinPart + 1<<20, "object_id": owner.ID})
	uploadID, _ := init["upload_id"].(string)
	if rr := uploadPart(t, app, uploadID, 1, bytes.Repeat([]byte{1}, chunkedUploadMinPart)); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "POST", "/v1/assets/upload/"+uploadID+"/complete", nil); rr.Code != 400 {
		t.Fatalf("incomplete complete: HTTP %d", rr.Code)
	}

	// A part may not exceed the declared size or 25 MB.
	if rr := uploadPart(t, app, uploadID, 2, bytes.Repeat([]byte{2}, chunkedUploadMaxPart+1)); rr.Code != 400 {
		t.Fatalf("oversized part: HTTP %d", rr.Code)
	}
	small, _ := chunkedInit(t, app, map[string]any{"filename": "y.bin", "mime": "text/plain", "size": 100, "object_id": owner.ID})
	smallID, _ := small["upload_id"].(string)
	if rr := uploadPart(t, app, smallID, 1, bytes.Repeat([]byte{3}, 200)); rr.Code != 400 {
		t.Fatalf("part beyond declared size: HTTP %d", rr.Code)
	}
	if rr := uploadPart(t, app, smallID, 1, nil); rr.Code != 400 {
		t.Fatalf("empty part: HTTP %d", rr.Code)
	}
	if rr := uploadPart(t, app, smallID, 0, bytes.Repeat([]byte{4}, 100)); rr.Code != 400 {
		t.Fatalf("zero index: HTTP %d", rr.Code)
	}
}

func TestChunkedUploadAbort(t *testing.T) {
	app, _, _ := makeUploadApp(t)
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Abort owner"}))
	init, _ := chunkedInit(t, app, map[string]any{"filename": "abort.bin", "mime": "text/plain", "size": chunkedUploadMinPart + 10, "object_id": owner.ID})
	uploadID, _ := init["upload_id"].(string)
	if rr := uploadPart(t, app, uploadID, 1, bytes.Repeat([]byte{9}, chunkedUploadMinPart)); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	aborted := api(t, app, "POST", "/v1/assets/upload/"+uploadID+"/abort", nil)
	if aborted.Code != 200 {
		t.Fatalf("abort: HTTP %d", aborted.Code)
	}
	if _, err := os.Stat(app.uploadSessionDir(uploadID)); !os.IsNotExist(err) {
		t.Fatal("abort left the staging directory behind")
	}
	if rr := uploadPart(t, app, uploadID, 2, bytes.Repeat([]byte{1}, 10)); rr.Code != 404 {
		t.Fatalf("part after abort: HTTP %d", rr.Code)
	}
	if rr := api(t, app, "POST", "/v1/assets/upload/"+uploadID+"/complete", nil); rr.Code != 404 {
		t.Fatalf("complete after abort: HTTP %d", rr.Code)
	}
}

func TestChunkedUploadTTLJanitorOnAccess(t *testing.T) {
	app, _, _ := makeUploadApp(t)
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "TTL owner"}))
	init, _ := chunkedInit(t, app, map[string]any{"filename": "stale.bin", "mime": "text/plain", "size": chunkedUploadMinPart + 10, "object_id": owner.ID})
	uploadID, _ := init["upload_id"].(string)
	if rr := uploadPart(t, app, uploadID, 1, bytes.Repeat([]byte{5}, chunkedUploadMinPart)); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	// Age the session past the TTL; the next upload request sweeps it.
	app.uploadMu.Lock()
	if session, ok := app.uploads[uploadID]; ok {
		session.UpdatedAt = time.Now().Add(-chunkedUploadTTL - time.Hour)
	}
	app.uploadMu.Unlock()
	fresh, _ := chunkedInit(t, app, map[string]any{"filename": "fresh.bin", "mime": "text/plain", "size": 10, "object_id": owner.ID})
	if _, ok := fresh["upload_id"]; !ok {
		t.Fatal("sweeping init failed")
	}
	if _, err := os.Stat(app.uploadSessionDir(uploadID)); !os.IsNotExist(err) {
		t.Fatal("expired session was not swept")
	}
	if rr := uploadPart(t, app, uploadID, 2, bytes.Repeat([]byte{6}, 10)); rr.Code != 404 {
		t.Fatalf("expired session still accepts parts: HTTP %d", rr.Code)
	}
}

func TestChunkedUploadSessionSurvivesRestart(t *testing.T) {
	app, _, _ := makeUploadApp(t)
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Restart owner"}))
	init, _ := chunkedInit(t, app, map[string]any{
		"filename": "restart.bin", "mime": "text/plain", "size": 2*chunkedUploadMinPart + 16, "object_id": owner.ID,
	})
	uploadID, _ := init["upload_id"].(string)
	if rr := uploadPart(t, app, uploadID, 1, bytes.Repeat([]byte{0x11}, chunkedUploadMinPart)); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	// A new App over the same store recovers the session from the sealed file.
	restarted, err := NewApp(app.store, testToken, nil)
	if err != nil {
		t.Fatal(err)
	}
	restarted.uploadDir = app.uploadDir
	if rr := uploadPart(t, restarted, uploadID, 2, bytes.Repeat([]byte{0x22}, chunkedUploadMinPart)); rr.Code != 200 {
		t.Fatalf("recovered part: HTTP %d: %s", rr.Code, rr.Body.String())
	}
	if rr := uploadPart(t, restarted, uploadID, 3, bytes.Repeat([]byte{0x33}, 16)); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	complete := api(t, restarted, "POST", "/v1/assets/upload/"+uploadID+"/complete", nil)
	if complete.Code != 201 {
		t.Fatalf("recovered complete: HTTP %d: %s", complete.Code, complete.Body.String())
	}
	var asset Asset
	if err := json.Unmarshal(complete.Body.Bytes(), &asset); err != nil {
		t.Fatal(err)
	}
	expected := append(append(append([]byte{},
		bytes.Repeat([]byte{0x11}, chunkedUploadMinPart)...),
		bytes.Repeat([]byte{0x22}, chunkedUploadMinPart)...),
		bytes.Repeat([]byte{0x33}, 16)...)
	download := api(t, app, "GET", "/v1/assets/"+asset.ID+"/content", nil)
	if sha256.Sum256(expected) != sha256.Sum256(download.Body.Bytes()) {
		t.Fatal("recovered upload bytes changed")
	}
}

func TestPurgeDeletesChunkedAttachment(t *testing.T) {
	app, _, _ := makeUploadApp(t)
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Purge owner"}))
	init, _ := chunkedInit(t, app, map[string]any{"filename": "purge.bin", "mime": "text/plain", "size": 512, "object_id": owner.ID})
	uploadID, _ := init["upload_id"].(string)
	assetID, _ := init["asset_id"].(string)
	if rr := uploadPart(t, app, uploadID, 1, bytes.Repeat([]byte("p"), 512)); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "POST", "/v1/assets/upload/"+uploadID+"/complete", nil); rr.Code != 201 {
		t.Fatal(rr.Body.String())
	}
	var count int
	if err := app.store.db.QueryRow("SELECT count(*) FROM blob_chunks WHERE asset_id=?", assetID).Scan(&count); err != nil || count != 1 {
		t.Fatalf("chunk rows missing: %d %v", count, err)
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+owner.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "DELETE", "/v1/trash/"+owner.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	// The new typed file object still owns the shared asset after the source
	// note is purged. Purging its last owner must finally remove the chunks.
	var surviving Asset
	if err := app.store.Get("asset", assetID, &surviving); err != nil {
		t.Fatal(err)
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+surviving.MediaObjectID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "DELETE", "/v1/trash/"+surviving.MediaObjectID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if err := app.store.db.QueryRow("SELECT count(*) FROM blob_chunks WHERE asset_id=?", assetID).Scan(&count); err != nil || count != 0 {
		t.Fatalf("purge left chunk rows: %d %v", count, err)
	}
	var assetRows int
	if err := app.store.db.QueryRow("SELECT count(*) FROM records WHERE kind='asset' AND id=?", assetID).Scan(&assetRows); err != nil || assetRows != 0 {
		t.Fatalf("purge left the asset record: %d %v", assetRows, err)
	}
}

// The whole-blob path is untouched: small uploads and sync assets keep using
// the blobs table, and Store.Blob still serves them.
func TestWholeBlobsStillUseBlobsTable(t *testing.T) {
	app, _, _ := makeApp(t)
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Small blob owner"}))
	asset, err := app.saveAsset(owner.ID, "small.txt", "text/plain", []byte("small bytes"))
	if err != nil {
		t.Fatal(err)
	}
	var whole int
	if err := app.store.db.QueryRow("SELECT count(*) FROM blobs WHERE id=?", asset.ID).Scan(&whole); err != nil || whole != 1 {
		t.Fatalf("small asset not in blobs: %d %v", whole, err)
	}
	count, err := app.store.BlobChunkCount(asset.ID)
	if err != nil || count != 0 {
		t.Fatalf("small asset grew chunks: %d %v", count, err)
	}
	if _, err := app.store.Blob(asset.ID); err != nil {
		t.Fatal(err)
	}
	if _, err := app.store.Blob("definitely-missing"); err == nil || err != sql.ErrNoRows {
		t.Fatalf("missing blob error: %v", err)
	}
}
