package main

import (
	"lumen/server/internal/document"
	"strings"
	"testing"
)

func TestToggleBodyAndStateAcrossDesktopAPIAndMobileSync(t *testing.T) {
	app, _, _ := makeApp(t)
	tree := document.Document{SchemaVersion: 2, Blocks: []document.Node{
		{ID: "toggle", Type: "toggle", Attrs: map[string]any{"expanded": true}, Content: []document.Inline{{Type: "text", Text: "Esto es un toggle"}}, Children: []document.Node{
			{ID: "child", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Original"}}},
			{ID: "nested", Type: "toggle", Attrs: map[string]any{"expanded": false}, Content: []document.Inline{{Type: "text", Text: "Toggle anidado"}}, Children: []document.Node{{ID: "nested-child", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Texto anidado editable"}}}}},
		}},
		{ID: "outside", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Fuera del toggle"}}},
	}}
	created := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Toggles", "document": tree}))
	created.Document.Blocks[0].Attrs["expanded"] = false
	created.Document.Blocks[0].Children[0].Content[0].Text = "Editado en Android"
	result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "toggle-mobile", "operations": []any{map[string]any{"op_id": "close-toggle", "action": "put", "base_revision": created.Revision, "object": created}}}))
	if len(result.Errors) != 0 || len(result.Applied) != 1 {
		t.Fatalf("push failed: %+v", result)
	}
	desktop := objectFrom(t, api(t, app, "GET", "/v1/objects/"+created.ID, nil))
	if desktop.Document.Blocks[0].Attrs["expanded"] != false || !strings.Contains(desktop.Content, "Editado en Android") || strings.Contains(desktop.Content, "▶") {
		t.Fatal("toggle state leaked glyphs into text or lost child edit")
	}
	head, _ := app.store.HeadSeq()
	desktop.Document.Blocks[0].Attrs["expanded"] = true
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+created.ID, documentPatchPayload(t, app, created.ID, map[string]any{"document": desktop.Document})))
	pull := decodeBody[syncPullResponse](t, api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": head}))
	found := false
	for _, obj := range pull.Objects {
		if obj.ID == created.ID {
			toggle := obj.Document.Blocks[0]
			if toggle.Attrs["expanded"] != true || len(toggle.Children) != 2 || toggle.Children[1].Attrs["expanded"] != false || toggle.Children[0].Content[0].Text != "Editado en Android" {
				t.Fatal("pull lost independent nested toggle state/body")
			}
			found = true
		}
	}
	if !found {
		t.Fatal("pull missing toggle")
	}
}
