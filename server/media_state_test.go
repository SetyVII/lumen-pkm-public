package main

import (
	"encoding/base64"
	"encoding/json"
	"testing"

	"lumen/server/internal/document"
)

func TestOfflineMediaStateRequiresAnActualUploadBeforeDocumentUse(t *testing.T) {
	a, _, _ := makeApp(t)
	d := documentFixture("Photo notes")
	photo := Object{ID: "offline-photo", TypeID: "image", Title: "Offline photo", Document: &d, Properties: map[string]any{"asset_id": "offline-photo-bytes", "image_asset_id": "offline-photo-bytes"}}
	push := func(op syncOperation) syncPushResponse {
		t.Helper()
		r := api(t, a, "POST", "/v1/sync/push", syncPushRequest{DeviceID: "phone", Operations: []syncOperation{op}})
		var result syncPushResponse
		if r.Code != 200 || json.Unmarshal(r.Body.Bytes(), &result) != nil {
			t.Fatalf("sync response: %d %s", r.Code, r.Body.String())
		}
		return result
	}
	if result := push(syncOperation{OpID: "empty-media-forbidden", Action: "put", Object: &photo}); len(result.Errors) != 1 || a.store.Has("object", photo.ID) {
		t.Fatal("implicit missing media source was accepted")
	}
	photo.Properties["media_state"] = "upload_pending"
	result := push(syncOperation{OpID: "explicit-media-pending", Action: "put", Object: &photo})
	if len(result.Errors) != 0 {
		t.Fatalf("pending upload: %+v", result)
	}
	stored, _ := a.store.Object(photo.ID)
	if stored.Properties["media_state"] != "upload_pending" || stored.Document == nil {
		t.Fatal("pending state or notes missing")
	}
	doc := document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "image-node", Type: "image", Attrs: map[string]any{"asset_id": "offline-photo-bytes"}}}}
	r := api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Embedded photo", "document": doc})
	if r.Code != 400 {
		t.Fatal("document committed an image before its source upload")
	}
	// An invalid upload cannot make the owner ready or acknowledge the operation.
	bad := push(syncOperation{OpID: "wrong-media-upload", Action: "asset", Asset: &syncAsset{ID: "offline-photo-bytes", ObjectID: photo.ID, Name: "bad.pdf", DataBase64: base64.StdEncoding.EncodeToString([]byte("%PDF-1.4\n"))}})
	if len(bad.Errors) != 1 || a.store.Has("asset", "offline-photo-bytes") {
		t.Fatal("wrong media type committed")
	}
	// Another editor can change notes while the phone uploads the file.
	remote := objectFrom(t, api(t, a, "PUT", "/v1/objects/"+photo.ID+"/document", map[string]any{"base_revision": stored.Revision, "op_id": "remote-notes-during-upload", "document": documentFixture("Remote photo notes")}))
	png, err := base64.StdEncoding.DecodeString("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jRZkAAAAASUVORK5CYII=")
	if err != nil {
		t.Fatal(err)
	}
	good := push(syncOperation{OpID: "actual-media-upload", Action: "asset", Asset: &syncAsset{ID: "offline-photo-bytes", ObjectID: photo.ID, Name: "photo.png", DataBase64: base64.StdEncoding.EncodeToString(png)}})
	if len(good.Errors) != 0 {
		t.Fatalf("upload: %+v", good)
	}
	if len(good.Receipts) != 1 || good.Receipts[0].PreviousRevision != remote.Revision || good.Receipts[0].Revision != remote.Revision+1 {
		t.Fatal("upload receipt omitted the actual previous revision; a client could rebase over remote notes")
	}
	stored, _ = a.store.Object(photo.ID)
	if stored.Properties["media_state"] != "ready" || stored.Content != "Remote photo notes" {
		t.Fatal("upload did not atomically preserve notes and mark media ready")
	}
	objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Embedded photo", "document": doc}))
	beforeRevision := stored.Revision
	resend := push(syncOperation{OpID: "same-asset-new-operation", Action: "asset", Asset: &syncAsset{ID: "offline-photo-bytes", ObjectID: photo.ID, Name: "photo.png", DataBase64: base64.StdEncoding.EncodeToString(png)}})
	if len(resend.Errors) != 0 {
		t.Fatalf("same asset resend: %+v", resend)
	}
	stored, _ = a.store.Object(photo.ID)
	if stored.Revision != beforeRevision {
		t.Fatal("asset resend created a false object edit")
	}
	changed := append(append([]byte{}, png...), byte(1))
	replace := push(syncOperation{OpID: "replace-existing-asset", Action: "asset", Asset: &syncAsset{ID: "offline-photo-bytes", ObjectID: photo.ID, Name: "photo.png", DataBase64: base64.StdEncoding.EncodeToString(changed)}})
	if len(replace.Errors) != 1 {
		t.Fatal("existing asset identity was reused for different bytes")
	}
	actual, err := a.store.Blob("offline-photo-bytes")
	if err != nil || string(actual) != string(png) {
		t.Fatal("failed asset replacement damaged original bytes")
	}

}

func TestMediaSourceRequirementAppliesToTypeChanges(t *testing.T) {
	a, _, _ := makeApp(t)
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Cannot become an empty image"}))
	if r := api(t, a, "PATCH", "/v1/objects/"+o.ID, map[string]any{"base_revision": o.Revision, "type_id": "image"}); r.Code != 400 {
		t.Fatalf("blank image via type patch: %d %s", r.Code, r.Body.String())
	}
	if _, err := a.convertObjectData(cloneObject(o), convertRequest{TargetTypeID: "image"}); err == nil {
		t.Fatal("conversion created a media object without a source")
	}
	current, _ := a.store.Object(o.ID)
	if current.TypeID != "note" || current.Revision != o.Revision {
		t.Fatal("rejected type change modified original object")
	}
}
