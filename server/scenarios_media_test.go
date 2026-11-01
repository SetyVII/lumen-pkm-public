package main

// User journey: attach a photo to a page, delete the page by mistake, restore
// it, and export the space. This combines asset upload/promote, object links,
// trash, backlinks and the full export.

import (
	"bytes"
	"encoding/base64"
	"strings"
	"testing"
)

// scenarioPNG is a valid 1x1 PNG. The bytes are compared after the export, so
// the fixture must be stable.
const scenarioPNG = "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j0ioAAAAASUVORK5CYII="

func TestScenarioMediaAttachDeleteRestoreExportJourney(t *testing.T) {
	app, _, _ := makeApp(t)
	png, err := base64.StdEncoding.DecodeString(scenarioPNG)
	if err != nil {
		t.Fatal(err)
	}

	page := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Trip plan", "content": "Pack the camera",
	}))

	// Step 1: upload an image on the page. The upload creates the reusable
	// image object and links it to its owner.
	asset := uploadFixture(t, app, page.ID, "cover.png", png)
	if asset.MediaObjectID == "" || asset.MIME != "image/png" {
		t.Fatalf("upload did not create an image object: %+v", asset)
	}
	media := scenarioGet(t, app, asset.MediaObjectID)
	if media.TypeID != "image" || media.Properties["asset_id"] != asset.ID || media.Properties["image_asset_id"] != asset.ID {
		t.Fatalf("image object is wrong: %+v", media)
	}
	if media.Properties["source_object_id"] != page.ID {
		t.Fatalf("image object lost its source: %#v", media.Properties)
	}

	// Step 2: promote is the explicit "make this a reusable image" action. It
	// is idempotent for an upload that already created the media object.
	promoted := decodeBody[map[string]any](t, api(t, app, "POST", "/v1/assets/"+asset.ID+"/promote", nil))
	if promoted["media_object_id"] != asset.MediaObjectID {
		t.Fatalf("promote returned a different media object: %#v", promoted)
	}

	// Step 3: the page links the image and embeds it in a block.
	page = scenarioGet(t, app, page.ID)
	if !scenarioContainsID(page.Links, media.ID) {
		t.Fatalf("owner did not link the uploaded image: %v", page.Links)
	}
	page = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+page.ID, map[string]any{
		"properties": map[string]any{
			"blocks": []any{
				map[string]any{"id": "trip-intro", "type": "paragraph", "text": "Pack the camera"},
				map[string]any{
					"id": "trip-photo", "type": "paragraph", "text": "",
					"runs": []any{map[string]any{
						"text": "", "type": "image", "image_name": "lumen://image/" + media.ID,
					}},
				},
			},
		},
	}))
	if !scenarioContainsID(page.Links, media.ID) {
		t.Fatal("editing the page dropped the image link")
	}
	assets, ok := page.Properties["assets"].([]any)
	if !ok || len(assets) != 1 || assets[0].(map[string]any)["id"] != asset.ID {
		t.Fatalf("editing the page dropped the managed asset metadata: %#v", page.Properties["assets"])
	}

	// Step 4: delete the page. The asset is kept encrypted while the page is in
	// the trash, so a restore does not lose the photo.
	scenarioStatus(t, api(t, app, "DELETE", "/v1/objects/"+page.ID, nil), 200)
	download := api(t, app, "GET", "/v1/assets/"+asset.ID+"/content", nil)
	scenarioStatus(t, download, 200)
	if !bytes.Equal(download.Body.Bytes(), png) {
		t.Fatal("trashing the page changed the asset bytes")
	}
	if !scenarioContainsID(scenarioTrashIDs(t, app), page.ID) {
		t.Fatal("deleted page is not in the trash")
	}
	if backlinks := scenarioBacklinks(t, app, media.ID); len(backlinks) != 0 {
		t.Fatalf("trashed page still advertises the image backlink: %+v", backlinks)
	}

	// Step 5: restore the page. The link and the backlink come back.
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+page.ID+"/restore", nil))
	if !scenarioContainsID(restored.Links, media.ID) {
		t.Fatalf("restore lost the image link: %v", restored.Links)
	}
	backlinks := scenarioBacklinks(t, app, media.ID)
	if len(backlinks) != 1 || backlinks[0].ID != page.ID {
		t.Fatalf("restore did not bring the image backlink back: %+v", backlinks)
	}

	// Step 6: export the space. The asset bytes are identical and the page's
	// Markdown points at the exported asset file.
	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	assetEntries := scenarioExportAssets(t, entries)
	exported, ok := assetEntries[asset.ID]
	if !ok {
		t.Fatalf("export omitted the asset; manifest assets: %+v", assetEntries)
	}
	if !bytes.Equal(mustEntry(t, entries, exported.Path), png) {
		t.Fatal("exported asset bytes differ from the uploaded bytes")
	}
	_, body := frontMatterBody(t, objectEntry(t, entries, app, restored))
	if !strings.Contains(body, "![](../"+exported.Path+")") {
		t.Fatalf("page Markdown does not reference the exported asset:\n%s", body)
	}
}
