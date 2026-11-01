package main

import (
	"encoding/json"
	"os"
	"reflect"
	"testing"
)

func formattedFixture(t *testing.T, app *App) Object {
	t.Helper()
	return objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Formatted fixture", "content": "Old content",
		"properties": map[string]any{"blocks": []any{map[string]any{"id": "stable-block", "text": "Old content"}}, "rich_text": "<p>Old content</p>", "custom": "remove deliberately", "date": "2098-11-16"},
	}))
}

func TestPlainContentPatchInvalidatesOnlyImplicitRichFormats(t *testing.T) {
	for _, variant := range []struct {
		name       string
		properties any
		expected   map[string]bool
	}{
		{"no properties", nil, map[string]bool{"blocks": false, "rich_text": false, "custom": true, "date": true}},
		{"date only", map[string]any{"date": "2098-11-17"}, map[string]bool{"blocks": false, "rich_text": false, "custom": false, "date": true}},
		{"explicit HTML", map[string]any{"rich_text": "<p>New content</p>"}, map[string]bool{"blocks": false, "rich_text": true, "custom": false, "date": false}},
		{"explicit both", map[string]any{"blocks": []any{map[string]any{"id": "stable-block", "text": "New content"}}, "rich_text": "<p>New content</p>"}, map[string]bool{"blocks": true, "rich_text": true, "custom": false, "date": false}},
	} {
		t.Run(variant.name, func(t *testing.T) {
			app, _, _ := makeApp(t)
			before := formattedFixture(t, app)
			patch := map[string]any{"content": "New content"}
			if variant.properties != nil {
				patch["properties"] = variant.properties
			}
			objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, patch))
			got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+before.ID, nil))
			if got.Content != "New content" {
				t.Fatal("plain text did not persist")
			}
			for key, expected := range variant.expected {
				if _, present := got.Properties[key]; present != expected {
					t.Fatalf("%s presence=%v, expected %v", key, present, expected)
				}
			}
		})
	}
}

func TestPropertiesReplacementRemovesOmittedEditableKeysWithoutChangingContent(t *testing.T) {
	app, _, _ := makeApp(t)
	before := formattedFixture(t, app)
	got := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, map[string]any{"properties": map[string]any{"date": "2098-11-18"}}))
	if len(got.Properties) != 1 || got.Properties["date"] != "2098-11-18" || got.Content != before.Content {
		t.Fatal("properties replacement retained omitted fields or changed text")
	}
	// A relation/title-only update has no authority to discard current formats.
	got = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, map[string]any{"properties": before.Properties}))
	got = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, map[string]any{"title": "Renamed formatted fixture"}))
	if got.Properties["blocks"] == nil || got.Properties["rich_text"] == nil {
		t.Fatal("unrelated patch invalidated current formatting")
	}
}

func TestMobilePlainContentCanExplicitlyInvalidateNativeFormats(t *testing.T) {
	app, _, _ := makeApp(t)
	before := formattedFixture(t, app)
	incoming := before
	incoming.Content += "\nTest desde el movil"
	incoming.Properties = map[string]any{"blocks": nil, "rich_text": nil, "date": "2098-11-16"}
	result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{
		"device_id": "plain-text-test", "operations": []any{map[string]any{
			"op_id": "plain-text-edit", "action": "put", "base_revision": before.Revision, "object": incoming,
		}},
	}))
	if len(result.Errors) != 0 || len(result.Applied) != 1 {
		t.Fatalf("mobile edit rejected: %+v", result)
	}
	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+before.ID, nil))
	if got.Content != incoming.Content || got.Properties["blocks"] != nil || got.Properties["rich_text"] != nil {
		t.Fatal("mobile text is hidden by stale native formats")
	}
	if got.Properties["date"] != "2098-11-16" {
		t.Fatal("unrelated properties lost")
	}
}

func TestMobileNativeEditorBlocksRoundTripWithoutLosingFormatting(t *testing.T) {
	app, _, _ := makeApp(t)
	data, err := os.ReadFile("../contracts/fixtures/native-editor.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture struct {
		Objects []Object `json:"objects"`
	}
	if err := json.Unmarshal(data, &fixture); err != nil {
		t.Fatal(err)
	}
	operations := make([]any, 0, len(fixture.Objects))
	for _, object := range fixture.Objects {
		operations = append(operations, map[string]any{"op_id": "native-" + object.ID, "action": "put", "object": object})
	}
	result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{
		"device_id": "native-editor-test", "operations": operations,
	}))
	if len(result.Errors) != 0 || len(result.Applied) != len(operations) {
		t.Fatalf("native blocks rejected: %+v", result)
	}
	want := fixture.Objects[len(fixture.Objects)-1]
	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+want.ID, nil))
	if got.Content != want.Content || !reflect.DeepEqual(got.Properties["blocks"], want.Properties["blocks"]) {
		t.Fatal("native sync altered the body, list/style metadata or image positions")
	}
}

func TestMobileJournalAppendKeepsNativeImagesAndIdempotency(t *testing.T) {
	app, _, _ := makeApp(t)
	journal := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "journal", "title": "Native journal append", "content": "Original",
		"properties": map[string]any{"date": "2098-12-01", "blocks": []any{map[string]any{"id": "original", "type": "paragraph", "text": "Original", "custom": "keep", "runs": []any{map[string]any{"text": "Original", "bold": true}}}}, "rich_text": "<p>Original</p>"},
	}))
	for n, id := range []string{"native-image-a", "native-image-b"} {
		image := map[string]any{"id": id, "type": "embed", "text": "\uFFFC", "runs": []any{map[string]any{"type": "image", "text": "\uFFFC", "image_name": "lumen://image/" + id, "width": 400, "height": 100}}}
		delta := journal
		delta.Content = "\uFFFC"
		delta.Properties = map[string]any{"blocks": []any{image}}
		op := map[string]any{"op_id": id + "-operation", "action": "put", "append": true, "client_time": "2098-12-01T10:30:00Z", "object": delta}
		for retry := 0; retry < 2; retry++ {
			result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "native-append-test", "operations": []any{op}}))
			if len(result.Errors) != 0 {
				t.Fatalf("native append rejected: %+v", result)
			}
		}
		got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+journal.ID, nil))
		blocks := got.Properties["blocks"].([]any)
		if len(blocks) != 1+(n+1)*3 {
			t.Fatalf("missing or duplicated blocks: %d", len(blocks))
		}
		if !reflect.DeepEqual(blocks[0], journal.Properties["blocks"].([]any)[0]) {
			t.Fatal("existing rich block changed")
		}
		if !reflect.DeepEqual(blocks[len(blocks)-1], image) {
			// JSON number types differ from the local integer fixture.
			a, _ := json.Marshal(blocks[len(blocks)-1])
			b, _ := json.Marshal(image)
			if string(a) != string(b) {
				t.Fatalf("image metadata changed: %s vs %s", a, b)
			}
		}
		if got.Properties["rich_text"] != nil {
			t.Fatal("stale HTML survived append")
		}
	}
}

func TestMobileObjectSchemasKeepLegacyAndTypedRelationsSeparate(t *testing.T) {
	app, _, _ := makeApp(t)
	data, err := os.ReadFile("../contracts/fixtures/mobile-object-schemas.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture struct {
		Types   []ObjectType `json:"types"`
		Objects []Object     `json:"objects"`
	}
	if err := json.Unmarshal(data, &fixture); err != nil {
		t.Fatal(err)
	}
	for _, typ := range fixture.Types {
		rr := api(t, app, "POST", "/v1/types", typ)
		if rr.Code != 201 && rr.Code != 200 {
			t.Fatalf("schema rejected: %d %s", rr.Code, rr.Body.String())
		}
	}
	var operations []any
	for _, obj := range fixture.Objects {
		operations = append(operations, map[string]any{"op_id": "schema-test-" + obj.ID, "action": "put", "object": obj})
	}
	result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "schema-client", "operations": operations}))
	if len(result.Errors) > 0 {
		t.Fatalf("object schema sync failed: %+v", result)
	}
	legacy := objectFrom(t, api(t, app, "GET", "/v1/objects/fixture-book-legacy", nil))
	modern := objectFrom(t, api(t, app, "GET", "/v1/objects/fixture-book-modern-object", nil))
	if legacy.Properties["author"] != "Nombre de autor" || len(legacy.PropertyLinks) != 0 {
		t.Fatal("legacy text became a guessed relation")
	}
	if len(modern.PropertyLinks) != 1 || modern.PropertyLinks[0] != "fixture-author" {
		t.Fatalf("typed author relation lost: %v", modern.PropertyLinks)
	}
	if !reflect.DeepEqual(modern.Properties["reading_line"], fixture.Objects[2].Properties["reading_line"]) {
		t.Fatal("reading interval changed")
	}
}
