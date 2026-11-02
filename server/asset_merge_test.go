package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"mime/multipart"
	"net/http/httptest"
	"testing"
)

func uploadFixture(t *testing.T, app *App, ownerID, name string, data []byte) Asset {
	t.Helper()
	body := bytes.NewBuffer(nil)
	writer := multipart.NewWriter(body)
	_ = writer.WriteField("object_id", ownerID)
	part, err := writer.CreateFormFile("file", name)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = part.Write(data); err != nil {
		t.Fatal(err)
	}
	if err = writer.Close(); err != nil {
		t.Fatal(err)
	}
	req := httptest.NewRequest("POST", "/v1/assets", body)
	req.Header.Set("Authorization", "Bearer "+testToken)
	req.Header.Set("X-Lumen-Document-Version", "2")
	req.Header.Set("Content-Type", writer.FormDataContentType())
	rr := httptest.NewRecorder()
	app.Handler().ServeHTTP(rr, req)
	if rr.Code != 201 {
		t.Fatal(rr.Body.String())
	}
	var asset Asset
	if err = json.Unmarshal(rr.Body.Bytes(), &asset); err != nil {
		t.Fatal(err)
	}
	return asset
}

func TestLateAutosaveCannotOverwriteNewUploadMetadataOrMediaRelation(t *testing.T) {
	app, _, _ := makeApp(t)
	before := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Upload while editing", "document": documentFixture("Before"), "properties": map[string]any{"author": "Before"}}))
	png, err := base64.StdEncoding.DecodeString("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j0ioAAAAASUVORK5CYII=")
	if err != nil {
		t.Fatal(err)
	}
	asset := uploadFixture(t, app, before.ID, "cover.png", png)
	staleProperties := before.Properties
	staleProperties["author"] = "New author from editor"
	stale := api(t, app, "PATCH", "/v1/objects/"+before.ID, documentPatchPayload(t, app, before.ID, map[string]any{
		"base_revision": before.Revision, "document": documentFixture("Unrebased editor snapshot"), "properties": staleProperties, "links": before.Links,
	}))
	if stale.Code != 409 {
		t.Fatalf("late pre-upload document was not rejected: %d %s", stale.Code, stale.Body.String())
	}
	committed := objectFrom(t, api(t, app, "GET", "/v1/objects/"+before.ID, nil))
	if committed.Content != "Before" || committed.Properties["image_asset_id"] != asset.ID || len(committed.Links) != 1 {
		t.Fatal("rejected stale snapshot mutated the committed body or attachment")
	}
	// After reading the latest revision, explicitly retry the local document;
	// managed media metadata must survive even if the property snapshot is old.
	got := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, documentPatchPayload(t, app, before.ID, map[string]any{"document": documentFixture("Fresh document text"), "properties": staleProperties, "links": before.Links})))
	assets, _ := got.Properties["assets"].([]any)
	if len(assets) != 1 || assets[0].(map[string]any)["id"] != asset.ID || got.Properties["image_asset_id"] != asset.ID || len(got.Links) != 1 || got.Links[0] != asset.MediaObjectID {
		t.Fatal("late document snapshot removed committed attachment, cover or media relation")
	}
	if got.Content != "Fresh document text" || got.Properties["author"] != "New author from editor" {
		t.Fatal("preserving attachment overwrote editable text or typed properties")
	}
	// Attempting to clear server-owned arrays by a stale document snapshot also
	// cannot remove an attachment that still exists in the encrypted store, or replace cover with a non-existent asset ID.
	got = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, map[string]any{"properties": map[string]any{"assets": []any{}, "image_asset_id": "stale-other-id", "author": "Latest author"}, "links": []string{}}))
	if len(got.Properties["assets"].([]any)) != 1 || got.Properties["image_asset_id"] != asset.ID || len(got.Links) != 1 || got.Properties["author"] != "Latest author" {
		t.Fatal("late clear defeated managed metadata merge")
	}
	// Explicitly choosing a valid existing asset as cover or clearing cover (empty string) must persist.
	asset2 := uploadFixture(t, app, "", "second-cover.png", png)
	got = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, map[string]any{"properties": map[string]any{"image_asset_id": asset2.ID, "author": "Latest author"}}))
	if got.Properties["image_asset_id"] != asset2.ID {
		t.Fatalf("explicit cover selection was discarded: got %v, want %v", got.Properties["image_asset_id"], asset2.ID)
	}
	got = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, map[string]any{"properties": map[string]any{"image_asset_id": "", "author": "Latest author"}}))
	if _, hasCover := got.Properties["image_asset_id"]; hasCover {
		t.Fatalf("explicit cover removal was ignored: %v", got.Properties["image_asset_id"])
	}
}

func TestPromotionSurvivesStaleAutosaveAndDeletedMediaDoesNotReappear(t *testing.T) {
	app, _, _ := makeApp(t)
	before := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Promote while editing"}))
	// Legacy previews may have metadata without an assets property. Promotion
	// must establish that canonical list before late client saves can arrive.
	asset, err := app.saveAsset(before.ID, "legacy.pdf", "application/pdf", []byte("%PDF-1.4\nlegacy\n"))
	if err != nil {
		t.Fatal(err)
	}
	rr := api(t, app, "POST", "/v1/assets/"+asset.ID+"/promote", nil)
	if rr.Code != 201 {
		t.Fatal(rr.Body.String())
	}
	var promoted struct {
		MediaObjectID string `json:"media_object_id"`
	}
	if err = json.Unmarshal(rr.Body.Bytes(), &promoted); err != nil {
		t.Fatal(err)
	}
	got := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, documentPatchPayload(t, app, before.ID, map[string]any{"document": documentFixture("Fresh body"), "properties": before.Properties, "links": before.Links})))
	assets, _ := got.Properties["assets"].([]any)
	if len(assets) != 1 || assets[0].(map[string]any)["media_object_id"] != promoted.MediaObjectID || len(got.Links) != 1 || got.Links[0] != promoted.MediaObjectID {
		t.Fatal("stale autosave removed promoted medium")
	}
	if rr = api(t, app, "DELETE", "/v1/objects/"+promoted.MediaObjectID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	got = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, documentPatchPayload(t, app, before.ID, map[string]any{"document": documentFixture("After explicit media delete"), "properties": before.Properties, "links": []string{}})))
	if len(got.Links) != 0 {
		t.Fatal("managed asset relation resurrected deleted media")
	}
}

func TestUploadAfterAutosavePreservesLatestEditableProperties(t *testing.T) {
	app, _, _ := makeApp(t)
	before := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Autosave before upload", "document": documentFixture("Before")}))
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, documentPatchPayload(t, app, before.ID, map[string]any{"document": documentFixture("Latest typed text"), "properties": map[string]any{"author": "Latest author"}})))
	asset := uploadFixture(t, app, before.ID, "source.pdf", []byte("%PDF-1.4\nfixture\n"))
	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+before.ID, nil))
	if got.Content != "Latest typed text" || got.Properties["author"] != "Latest author" || len(got.Links) != 1 || got.Links[0] != asset.MediaObjectID {
		t.Fatal("attachment write replaced latest editor content")
	}
}
