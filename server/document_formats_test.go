package main

import (
	"encoding/json"
	"lumen/server/internal/document"
	"os"
	"reflect"
	"testing"
)

func formattedFixture(t *testing.T, app *App) Object {
	t.Helper()
	return objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Formatted fixture", "document": documentFixture("Old content"),
		"properties": map[string]any{"custom": "remove deliberately", "date": "2098-11-16"},
	}))
}

func TestPlainContentPatchCannotInvalidateCanonicalFormats(t *testing.T) {
	for _, properties := range []any{nil, map[string]any{"date": "2098-11-17"}, map[string]any{"rich_text": "<p>New content</p>"}, map[string]any{"blocks": []any{}, "rich_text": "<p>New content</p>"}} {
		app, _, _ := makeApp(t)
		before := formattedFixture(t, app)
		patch := map[string]any{"content": "New content", "base_revision": before.Revision, "op_id": newID()}
		if properties != nil {
			patch["properties"] = properties
		}
		rr := api(t, app, "PATCH", "/v1/objects/"+before.ID, patch)
		if rr.Code != 400 {
			t.Fatalf("text projection was allowed to replace canonical data: %d %s", rr.Code, rr.Body.String())
		}
		got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+before.ID, nil))
		if got.Revision != before.Revision || !reflect.DeepEqual(got.Document, before.Document) || !reflect.DeepEqual(got.Properties, before.Properties) {
			t.Fatal("rejected legacy edit mutated document or metadata")
		}
	}
}

func TestPropertiesReplacementRemovesOmittedEditableKeysWithoutChangingDocument(t *testing.T) {
	app, _, _ := makeApp(t)
	before := formattedFixture(t, app)
	got := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, map[string]any{"properties": map[string]any{"date": "2098-11-18"}}))
	if len(got.Properties) != 1 || got.Properties["date"] != "2098-11-18" || !reflect.DeepEqual(got.Document, before.Document) {
		t.Fatal("metadata replacement retained omitted fields or changed rich document")
	}
	got = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+before.ID, map[string]any{"title": "Renamed formatted fixture"}))
	if !reflect.DeepEqual(got.Document, before.Document) {
		t.Fatal("title-only patch discarded rich marks")
	}
}

func TestMobileCanonicalDocumentControlsTextProjection(t *testing.T) {
	app, _, _ := makeApp(t)
	before := formattedFixture(t, app)
	incoming := before
	next := documentFixture("Old content\nTest desde el movil")
	incoming.Document = &next
	incoming.Content = "A projection from a buggy client cannot win"
	incoming.Properties = map[string]any{"date": "2098-11-16"}
	result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "canonical-text-test", "operations": []any{map[string]any{"op_id": "canonical-text-edit", "action": "put", "base_revision": before.Revision, "object": incoming}}}))
	if len(result.Errors) != 0 || len(result.Applied) != 1 {
		t.Fatalf("canonical mobile edit rejected: %+v", result)
	}
	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+before.ID, nil))
	if got.Content != "Old content\nTest desde el movil" || !reflect.DeepEqual(got.Document, &next) || got.Properties["date"] != "2098-11-16" {
		t.Fatal("canonical source, marks or unrelated properties lost")
	}
}

func TestMobileCanonicalEditorAllTypesRoundTripWithoutLosingFormatting(t *testing.T) {
	app, _, _ := makeApp(t)
	data, err := os.ReadFile("../contracts/fixtures/document-v2/rich.document.json")
	if err != nil {
		t.Fatal(err)
	}
	var rich document.Document
	if err = json.Unmarshal(data, &rich); err != nil {
		t.Fatal(err)
	}
	// Instantiate the shared contract vector in a real library: media must be
	// uploaded, and semantic references must name actual target objects/blocks.
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Reference target", "document": documentFixture("Referenced paragraph")}))
	asset := uploadFixture(t, app, "", "fixture.png", []byte{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 0})
	var bind func([]document.Node)
	bind = func(nodes []document.Node) {
		for i := range nodes {
			node := &nodes[i]
			if node.Type == "image" {
				node.Attrs["asset_id"] = asset.ID
				node.Attrs["object_id"] = asset.MediaObjectID
			}
			if node.Type == "object_embed" {
				node.Attrs["object_id"] = target.ID
				if _, ok := node.Attrs["block_id"]; ok {
					node.Attrs["block_id"] = "paragraph-1"
				}
			}
			for j := range node.Content {
				item := &node.Content[j]
				if item.Type == "object_ref" || item.Type == "block_ref" {
					item.ObjectID = target.ID
					if item.Type == "block_ref" {
						item.BlockID = "paragraph-1"
					}
				}
			}
			bind(node.Children)
		}
	}
	bind(rich.Blocks)
	obj := Object{ID: "native-all-types", TypeID: "note", Title: "All canonical types", Document: &rich, Properties: map[string]any{}, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
	result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "native-editor-test", "operations": []any{map[string]any{"op_id": "native-all-types-put", "action": "put", "object": obj}}}))
	if len(result.Errors) != 0 || len(result.Applied) != 1 {
		t.Fatalf("canonical tree rejected: %+v", result)
	}
	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+obj.ID, nil))
	if !reflect.DeepEqual(got.Document, &rich) || got.Content != document.PlainText(rich) {
		t.Fatal("sync changed canonical type, marks, image dimensions, temporal value or reference")
	}
	if len(got.Links) != 0 || !scenarioContainsID(got.DocumentLinks, target.ID) || !scenarioContainsID(got.DocumentLinks, asset.MediaObjectID) {
		t.Fatal("document references were lost or silently promoted to manual links")
	}
}

func TestMobileJournalAppendKeepsCanonicalImagesAndIdempotency(t *testing.T) {
	app, _, _ := makeApp(t)
	initial := documentFixture("Original")
	initial.Blocks[0].Attrs = map[string]any{"custom": "keep"}
	journal := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "journal", "title": "Native journal append", "document": initial, "properties": map[string]any{"date": "2098-12-01"}}))
	for n, id := range []string{"native-image-a", "native-image-b"} {
		uploaded := uploadFixture(t, app, "", id+".png", []byte{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 0})
		image := document.Node{ID: id, Type: "image", Attrs: map[string]any{"asset_id": uploaded.ID, "object_id": uploaded.MediaObjectID, "width": float64(400), "height": float64(100), "alignment": "center", "caption": "Atomic image"}}
		delta := journal
		addition := document.Document{SchemaVersion: 2, Blocks: []document.Node{image}}
		delta.Document = &addition
		delta.Content = ""
		op := map[string]any{"op_id": id + "-operation", "action": "put", "append": true, "base_revision": journal.Revision, "object": delta}
		for retry := 0; retry < 2; retry++ {
			result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "native-append-test", "operations": []any{op}}))
			if len(result.Errors) != 0 || len(result.Applied) != 1 {
				t.Fatalf("canonical append rejected: %+v", result)
			}
		}
		got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+journal.ID, nil))
		if len(got.Document.Blocks) != n+2 {
			t.Fatalf("missing or duplicated blocks: %+v", got.Document)
		}
		if !reflect.DeepEqual(got.Document.Blocks[0], journal.Document.Blocks[0]) {
			t.Fatal("existing rich block changed")
		}
		if !reflect.DeepEqual(got.Document.Blocks[len(got.Document.Blocks)-1], image) {
			t.Fatal("image metadata changed")
		}
	}
}

func TestMobileObjectSchemasKeepTextAndTypedRelationsSeparate(t *testing.T) {
	app, _, _ := makeApp(t)
	data, err := os.ReadFile("../contracts/fixtures/mobile-object-schemas.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture struct {
		Types   []ObjectType `json:"types"`
		Objects []Object     `json:"objects"`
	}
	if err = json.Unmarshal(data, &fixture); err != nil {
		t.Fatal(err)
	}
	for _, typ := range fixture.Types {
		rr := api(t, app, "POST", "/v1/types", typ)
		if rr.Code != 201 && rr.Code != 200 {
			t.Fatalf("schema rejected: %s", rr.Body.String())
		}
	}
	var operations []any
	for _, obj := range fixture.Objects {
		doc := documentFixture(obj.Content)
		obj.Document = &doc
		operations = append(operations, map[string]any{"op_id": "schema-test-" + obj.ID, "action": "put", "object": obj})
	}
	result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "schema-client", "operations": operations}))
	if len(result.Errors) > 0 {
		t.Fatalf("schema sync failed: %+v", result)
	}
	legacy := objectFrom(t, api(t, app, "GET", "/v1/objects/fixture-book-legacy", nil))
	modern := objectFrom(t, api(t, app, "GET", "/v1/objects/fixture-book-modern-object", nil))
	if legacy.Properties["author"] != "Nombre de autor" || len(legacy.PropertyLinks) != 0 {
		t.Fatal("text author became guessed relation")
	}
	if len(modern.PropertyLinks) != 1 || modern.PropertyLinks[0] != "fixture-author" {
		t.Fatal("typed relation lost")
	}
	if !reflect.DeepEqual(modern.Properties["reading_line"], fixture.Objects[2].Properties["reading_line"]) {
		t.Fatal("reading interval changed")
	}
}
