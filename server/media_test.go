package main

import (
	"bytes"
	"encoding/json"
	"mime/multipart"
	"net/http/httptest"
	"testing"
)

func TestExistingAttachmentPromotionIsExplicitIdempotentAndReusesBlob(t *testing.T) {
	app, _, _ := makeApp(t)
	source := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Existing attachment owner"}))
	bytes := []byte("%PDF-1.4\npreviously stored bytes\n")
	asset, err := app.saveAsset(source.ID, "old.pdf", "application/pdf", bytes)
	if err != nil {
		t.Fatal(err)
	}
	source = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+source.ID, map[string]any{"properties": map[string]any{"assets": []any{assetProperties(asset)}}}))
	before, _ := app.objects()
	after, _ := app.objects()
	if len(after) != len(before) {
		t.Fatal("ordinary sync silently promoted a legacy attachment")
	}
	first := api(t, app, "POST", "/v1/assets/"+asset.ID+"/promote", map[string]any{})
	if first.Code != 201 {
		t.Fatal(first.Body.String())
	}
	var response struct {
		Asset         Asset  `json:"asset"`
		MediaObjectID string `json:"media_object_id"`
		Object        Object `json:"object"`
	}
	if err = json.Unmarshal(first.Body.Bytes(), &response); err != nil {
		t.Fatal(err)
	}
	if response.MediaObjectID == "" || response.Object.TypeID != "pdf" || response.Object.Properties["asset_id"] != asset.ID || response.Asset.ID != asset.ID {
		t.Fatal("promotion replaced attachment identity or omitted typed medium")
	}
	second := api(t, app, "POST", "/v1/assets/"+asset.ID+"/promote", nil)
	if second.Code != 200 {
		t.Fatal(second.Body.String())
	}
	var duplicate map[string]any
	if err = json.Unmarshal(second.Body.Bytes(), &duplicate); err != nil {
		t.Fatal(err)
	}
	if duplicate["media_object_id"] != response.MediaObjectID {
		t.Fatal("repeat promotion duplicated medium")
	}
	objects, _ := app.objects()
	if len(objects) != len(before)+1 {
		t.Fatal("promotion created extra copies")
	}
	stored, err := app.store.Blob(asset.ID)
	if err != nil || string(stored) != string(bytes) {
		t.Fatal("promotion rewrote encrypted blob bytes")
	}
	owner := objectFrom(t, api(t, app, "GET", "/v1/objects/"+source.ID, nil))
	if len(owner.Links) != 1 || owner.Links[0] != response.MediaObjectID {
		t.Fatal("promotion did not link original owner")
	}
}

func TestMediaNamesAreDistinctAndQueryViewPersists(t *testing.T) {
	app, _, _ := makeApp(t)
	objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "cover.png"}))
	app.writeMu.Lock()
	asset := Asset{ID: newID(), Name: "cover.png", MIME: "image/png"}
	medium, err := app.newMediaObject(&asset)
	app.writeMu.Unlock()
	if err != nil || medium.Title != "cover.png (2)" || medium.TypeID != "image" || medium.Properties["image_asset_id"] != asset.ID {
		t.Fatalf("invalid typed medium: %+v, %v", medium, err)
	}
	view := map[string]any{"representation": "gallery", "scope": "all", "filters": []any{map[string]any{"field": "type_id", "operator": "equals", "value": "book"}}, "sort": "title", "group_by": "status"}
	query := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "query", "title": "Books to read", "properties": map[string]any{"view": view}}))
	reloaded := objectFrom(t, api(t, app, "GET", "/v1/objects/"+query.ID, nil))
	left, _ := json.Marshal(view)
	right, _ := json.Marshal(reloaded.Properties["view"])
	if string(left) != string(right) {
		t.Fatal("saved query did not preserve manual view definition")
	}
}

func TestStandaloneMediaUploadHasNoPlaceholderOriginAndRecognizesFiles(t *testing.T) {
	for _, fixture := range []struct {
		name, data string
		status     int
		typ        string
	}{
		{"standalone.pdf", "%PDF-1.4\nstandalone\n", 201, "pdf"},
		{"plain.txt", "not an image or PDF", 201, "file"},
	} {
		t.Run(fixture.name, func(t *testing.T) {
			app, _, _ := makeApp(t)
			body := bytes.NewBuffer(nil)
			writer := multipart.NewWriter(body)
			_ = writer.WriteField("object_id", "")
			part, err := writer.CreateFormFile("file", fixture.name)
			if err != nil {
				t.Fatal(err)
			}
			_, _ = part.Write([]byte(fixture.data))
			_ = writer.Close()
			req := httptest.NewRequest("POST", "/v1/assets", body)
			req.Header.Set("Authorization", "Bearer "+testToken)
			req.Header.Set("Content-Type", writer.FormDataContentType())
			rr := httptest.NewRecorder()
			app.Handler().ServeHTTP(rr, req)
			if rr.Code != fixture.status {
				t.Fatal(rr.Body.String())
			}
			objects, err := app.objects()
			if err != nil {
				t.Fatal(err)
			}
			if fixture.status == 400 {
				if len(objects) != 0 {
					t.Fatal("invalid standalone upload created placeholder object")
				}
				return
			}
			var asset Asset
			if err = json.Unmarshal(rr.Body.Bytes(), &asset); err != nil {
				t.Fatal(err)
			}
			if len(objects) != 1 || objects[0].TypeID != fixture.typ || objects[0].ID != asset.MediaObjectID || asset.ObjectID != asset.MediaObjectID {
				t.Fatal("standalone media created a phantom source or confused blob/object IDs")
			}
			if asset.MediaObject == nil || asset.MediaObject.ID != asset.MediaObjectID {
				t.Fatal("upload response missing media_object")
			}
			if len(objects[0].Links) != 0 || objects[0].Properties["source_object_id"] != nil {
				t.Fatal("standalone medium invented a source relation")
			}
		})
	}
}
