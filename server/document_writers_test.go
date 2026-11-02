package main

import (
	"archive/zip"
	"bytes"
	"encoding/json"
	"io"
	"net/http/httptest"
	"reflect"
	"strings"
	"testing"
	"time"

	"lumen/server/internal/document"
)

func canonicalTestDoc() document.Document {
	return document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "writer-group", Type: "group", Attrs: map[string]any{"background": "#223344", "custom": true}, Children: []document.Node{{ID: "writer-text", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Preserved 😀", Marks: map[string]any{"bold": true}}}}}}}}
}
func TestCanonicalMarkdownAppendPreservesRichTree(t *testing.T) {
	app, _, _ := makeApp(t)
	d := canonicalTestDoc()
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Canonical rich append", "document": d}))
	updated := objectFrom(t, apiDocumentAppend(t, app, "POST", "/v1/objects/"+o.ID+"/append-markdown", map[string]any{"markdown": "## Added\n\n- child"}))
	if updated.Document == nil || len(updated.Document.Blocks) != 3 {
		t.Fatalf("append lost document: %+v", updated.Document)
	}
	if !reflect.DeepEqual(updated.Document.Blocks[0], o.Document.Blocks[0]) {
		t.Fatal("append rewrote existing rich node")
	}
	if updated.Properties["blocks"] != nil || updated.Properties["rich_text"] != nil {
		t.Fatal("persisted obsolete rich representations")
	}
	if !strings.Contains(updated.Content, "Added") {
		t.Fatal("projection not derived")
	}
}
func TestCanonicalImportHierarchyAndTemporalProjection(t *testing.T) {
	o := Object{Content: "1. Parent\n   1. Nested\n\nNext"}
	d, e := canonicalDocumentFromLegacy(o)
	if e != nil {
		t.Fatal(e)
	}
	if len(d.Blocks) != 2 || len(d.Blocks[0].Children) != 1 {
		t.Fatalf("lost imported hierarchy: %+v", d)
	}
	if d.Blocks[0].Type != "list_item" || d.Blocks[0].Children[0].Type != "list_item" {
		t.Fatal("not semantic list items")
	}
	o = Object{Content: "Unresolved [[Wiki title]] and @name"}
	d, e = canonicalDocumentFromLegacy(o)
	if e != nil {
		t.Fatal(e)
	}
	if !strings.Contains(document.PlainText(d), "Wiki title") {
		t.Fatal("unresolved title lost")
	}
}
func TestCanonicalTemplateIDsAndFullExport(t *testing.T) {
	app, _, _ := makeApp(t)
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Canonical writer template", "document": canonicalTestDoc(), "properties": map[string]any{"is_template": true}}))
	created := objectFrom(t, api(t, app, "POST", "/v1/objects/from-template", map[string]any{"template_id": tpl.ID, "title": "Applied canonical template"}))
	if created.Document == nil || len(created.Document.Blocks) != 1 {
		t.Fatal("template lacks document")
	}
	if created.Document.Blocks[0].ID == tpl.Document.Blocks[0].ID || created.Document.Blocks[0].Children[0].ID == tpl.Document.Blocks[0].Children[0].ID {
		t.Fatal("template reused subtree identities")
	}
	if created.Document.Blocks[0].Attrs["custom"] != true {
		t.Fatal("template stripped attrs")
	}
	rr := api(t, app, "GET", "/v1/export", nil)
	if rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	zr, e := zip.NewReader(bytes.NewReader(rr.Body.Bytes()), int64(rr.Body.Len()))
	if e != nil {
		t.Fatal(e)
	}
	found := false
	for _, f := range zr.File {
		if !strings.HasSuffix(f.Name, ".document.json") {
			continue
		}
		r, e := f.Open()
		if e != nil {
			t.Fatal(e)
		}
		data, e := io.ReadAll(r)
		r.Close()
		if e != nil {
			t.Fatal(e)
		}
		var obj Object
		if e = json.Unmarshal(data, &obj); e != nil {
			t.Fatal(e)
		}
		if obj.ID == created.ID {
			found = true
			if !reflect.DeepEqual(obj.Document, created.Document) {
				t.Fatal("export not lossless")
			}
		}
	}
	if !found {
		t.Fatal("canonical document absent from archive")
	}
}
func TestCanonicalBlockMutationAndSearch(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Canonical block search", "document": canonicalTestDoc()}))
	rr := api(t, app, "GET", "/v1/blocks?q=Preserved&object_id="+o.ID, nil)
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), "writer-text") {
		t.Fatalf("nested search: %s", rr.Body.String())
	}
	rr = api(t, app, "PATCH", "/v1/objects/"+o.ID+"/blocks/writer-text", map[string]any{"base_revision": o.Revision, "op_id": "canonical-block-edit-001", "block": document.Node{ID: "writer-text", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Updated"}}}})
	if rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	saved := objectFrom(t, api(t, app, "GET", "/v1/objects/"+o.ID, nil))
	if !strings.Contains(saved.Content, "Updated") || saved.Document.Blocks[0].ID != "writer-group" {
		t.Fatal("block write damaged containing document")
	}
}

// Explicitly prepare protocol fields for ingestion tests; generic API tests still
// exercise missing preconditions without this helper.
func apiDocumentAppend(t *testing.T, app *App, method, path string, body any) *httptest.ResponseRecorder {
	t.Helper()
	fields := map[string]any{}
	if original, ok := body.(map[string]any); ok {
		for k, v := range original {
			fields[k] = v
		}
	}
	resource := strings.TrimSuffix(strings.TrimSuffix(path, "/append-markdown"), "/append")
	revision := uint64(1)
	rr := api(t, app, "GET", resource, nil)
	if rr.Code == 200 {
		revision = objectFrom(t, rr).Revision
	}
	if _, ok := fields["base_revision"]; !ok {
		fields["base_revision"] = revision
	}
	if _, ok := fields["op_id"]; !ok {
		fields["op_id"] = newID()
	}
	return api(t, app, method, path, fields)
}

func mcpWriteArguments(t *testing.T, app *App, pairs ...any) map[string]any {
	t.Helper()
	fields := mcpArguments(pairs...)
	resource := ""
	if id, ok := fields["object_id"].(string); ok {
		resource = "/v1/objects/" + id
	} else {
		date, _ := fields["date"].(string)
		if date == "" {
			date = time.Now().Format("2006-01-02")
		}
		resource = "/v1/journal/" + date
	}
	revision := uint64(1)
	rr := api(t, app, "GET", resource, nil)
	if rr.Code == 200 {
		revision = objectFrom(t, rr).Revision
	}
	if _, ok := fields["base_revision"]; !ok {
		fields["base_revision"] = revision
	}
	if _, ok := fields["op_id"]; !ok {
		fields["op_id"] = newID()
	}
	return fields
}

func TestCanonicalAppendProtocolReceipts(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "document": canonicalTestDoc()}))
	path := "/v1/objects/" + o.ID + "/append-markdown"
	if rr := api(t, app, "POST", path, map[string]any{"markdown": "entry"}); rr.Code != 400 {
		t.Fatalf("missing protocol accepted: %d %s", rr.Code, rr.Body.String())
	}
	request := map[string]any{"markdown": "Entry 😀", "base_revision": o.Revision, "op_id": "append-test-operation"}
	first := objectFrom(t, api(t, app, "POST", path, request))
	second := objectFrom(t, api(t, app, "POST", path, request))
	if !reflect.DeepEqual(first, second) {
		t.Fatal("retry did not return same committed snapshot")
	}
	if first.Revision != o.Revision+1 || len(first.Document.Blocks) != 2 {
		t.Fatal("wrong append state")
	}
	stale := api(t, app, "POST", path, map[string]any{"markdown": "stale", "base_revision": o.Revision, "op_id": "different-operation"})
	if stale.Code != 409 {
		t.Fatalf("stale append accepted %d", stale.Code)
	}
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-12-01", nil))
	request = map[string]any{"markdown": "First entry", "section": "Log", "base_revision": journal.Revision, "op_id": "journal-operation"}
	first = objectFrom(t, api(t, app, "POST", "/v1/journal/2026-12-01/append", request))
	second = objectFrom(t, api(t, app, "POST", "/v1/journal/2026-12-01/append", request))
	if !reflect.DeepEqual(first, second) || len(first.Document.Blocks) != 2 {
		t.Fatal("journal section retry duplicated content")
	}
}

func TestCanonicalPurgeRemovesNestedReferences(t *testing.T) {
	app, _, _ := makeApp(t)
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Purge target"}))
	d := document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "container", Type: "group", Children: []document.Node{{ID: "reference", Type: "paragraph", Content: []document.Inline{{Type: "block_ref", ObjectID: target.ID, BlockID: "block-target", Label: "Preserved reference", Marks: map[string]any{"bold": true}}}}, {ID: "embedded", Type: "object_embed", Attrs: map[string]any{"object_id": target.ID, "view": "wide_card"}}}}}}
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "document": d}))
	for _, path := range []string{"/v1/objects/" + target.ID, "/v1/trash/" + target.ID} {
		if rr := api(t, app, "DELETE", path, nil); rr.Code != 200 {
			t.Fatalf("delete %s: %d %s", path, rr.Code, rr.Body.String())
		}
	}
	cleaned := objectFrom(t, api(t, app, "GET", "/v1/objects/"+owner.ID, nil))
	if len(document.References(*cleaned.Document)) != 0 || len(cleaned.DocumentLinks) != 0 {
		t.Fatal("purge retained canonical references")
	}
	nodes := cleaned.Document.Blocks[0].Children
	if nodes[0].ID != "reference" || nodes[0].Content[0].Text != "Preserved reference" || nodes[0].Content[0].Marks["bold"] != true || nodes[1].ID != "embedded" {
		t.Fatal("purge changed stable identity or text formatting")
	}
}

func TestCanonicalAssetRemovalPreservesCaptionAndUndoSource(t *testing.T) {
	d := document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "img", Type: "image", Attrs: map[string]any{"asset_id": "asset-gone", "caption": "Caption retained"}}}}
	o := Object{Document: &d}
	if !removeAssetReferences(&o, map[string]bool{"asset-gone": true}) {
		t.Fatal("did not remove image without properties")
	}
	if err := document.Validate(*o.Document); err != nil {
		t.Fatal(err)
	}
	if o.Document.Blocks[0].ID != "img" || o.Document.Blocks[0].Type != "paragraph" || o.Content != "Caption retained" {
		t.Fatal("image cleanup lost readable text")
	}
	if d.Blocks[0].Type != "image" {
		t.Fatal("cleanup mutated previous snapshot")
	}
}

func TestCanonicalMergeRemapsLocalIDsAndCompoundReferences(t *testing.T) {
	app, _, _ := makeApp(t)
	tree := func(label string) document.Document {
		return document.Document{SchemaVersion: 2, Blocks: []document.Node{
			{ID: "same", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: label}}},
			{ID: "table", Type: "table", Attrs: map[string]any{"columns": []any{map[string]any{"id": "column", "width": 120}}}, Children: []document.Node{{ID: "row", Type: "table_row", Children: []document.Node{{ID: "cell", Type: "table_cell", Children: []document.Node{{ID: "cell-text", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: label + " cell"}}}}}}}}},
		}}
	}
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Canonical merge target", "document": tree("Target")}))
	source := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Canonical merge source", "document": tree("Source")}))
	sourceDoc := *cloneDocument(source.Document)
	sourceDoc.Blocks[0].Content = append(sourceDoc.Blocks[0].Content, document.Inline{Type: "block_ref", ObjectID: source.ID, BlockID: "same", Label: "Source self"}, document.Inline{Type: "block_ref", ObjectID: target.ID, BlockID: "same", Label: "Existing target"})
	source = objectFrom(t, api(t, app, "PUT", "/v1/objects/"+source.ID+"/document", map[string]any{"base_revision": source.Revision, "op_id": "merge-test-source-update", "document": sourceDoc}))
	externalDoc := document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "references", Type: "paragraph", Content: []document.Inline{{Type: "block_ref", ObjectID: source.ID, BlockID: "same", Label: "External source"}, {Type: "block_ref", ObjectID: target.ID, BlockID: "same", Label: "External target"}}}, {ID: "embedding", Type: "object_embed", Attrs: map[string]any{"object_id": source.ID, "block_id": "cell-text", "view": "embed"}}}}
	external := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Canonical merge external", "document": externalDoc}))
	merged := objectFrom(t, api(t, app, "POST", "/v1/objects/"+source.ID+"/merge", map[string]any{"target_id": target.ID}))
	if err := document.Validate(*merged.Document); err != nil {
		t.Fatal(err)
	}
	if len(merged.Document.Blocks) != 4 || merged.Document.Blocks[0].ID != "same" {
		t.Fatal("target identity lost")
	}
	moved := merged.Document.Blocks[2]
	movedTable := merged.Document.Blocks[3]
	if moved.ID == "same" || movedTable.ID == "table" {
		t.Fatal("source collision not renewed")
	}
	cols := movedTable.Attrs["columns"].([]any)
	if cols[0].(map[string]any)["id"] == "column" {
		t.Fatal("table column collision not renewed")
	}
	if moved.Content[1].ObjectID != target.ID || moved.Content[1].BlockID != moved.ID || moved.Content[2].BlockID != "same" {
		t.Fatal("internal compound ref remapped incorrectly")
	}
	external = objectFrom(t, api(t, app, "GET", "/v1/objects/"+external.ID, nil))
	refs := external.Document.Blocks[0].Content
	if refs[0].ObjectID != target.ID || refs[0].BlockID != moved.ID || refs[1].ObjectID != target.ID || refs[1].BlockID != "same" {
		t.Fatal("external source/target identities conflated")
	}
	movedCellText := movedTable.Children[0].Children[0].Children[0].ID
	if external.Document.Blocks[1].Attrs["block_id"] != movedCellText {
		t.Fatal("block embed not remapped")
	}
}

func TestCanonicalCommandRouteRejectsUnknownNestedFieldsAtomically(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "document": canonicalTestDoc()}))
	for _, command := range []any{
		map[string]any{"type": "delete", "node_id": "writer-group", "unknown": true},
		map[string]any{"type": "insert", "node": map[string]any{"id": "p", "type": "paragraph", "unknown": true}},
		map[string]any{"type": "replace_inline", "node_id": "writer-text", "content": []any{map[string]any{"type": "text", "text": "change", "unknown": true}}},
	} {
		rr := api(t, app, "POST", "/v1/objects/"+o.ID+"/document/commands", map[string]any{"base_revision": o.Revision, "op_id": newID(), "commands": []any{command}})
		if rr.Code != 400 {
			t.Fatalf("unknown command structure accepted %d %s", rr.Code, rr.Body.String())
		}
		current, _ := app.store.Object(o.ID)
		if current.Revision != o.Revision || !reflect.DeepEqual(current.Document, o.Document) {
			t.Fatal("rejected command altered durable document")
		}
	}
}
