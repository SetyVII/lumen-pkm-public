package main

import (
	"encoding/json"
	"strings"
	"testing"

	"lumen/server/internal/document"
)

func TestMCPCanonicalOutputSchemaValidatesRecursiveDocuments(t *testing.T) {
	schema := mcpCanonicalOutputSchema[mcpMutateOutput]()
	resolved, err := schema.Resolve(nil)
	if err != nil {
		t.Fatal(err)
	}
	object := Object{ID: "object", TypeID: "note", Title: "Nested", Content: "Preserved 😀", Document: ptrDoc(canonicalTestDoc()), Properties: map[string]any{}, Links: []string{}, CreatedAt: "now", UpdatedAt: "now"}
	raw, _ := json.Marshal(mcpMutateOutput{Object: object})
	var value any
	if err = json.Unmarshal(raw, &value); err != nil {
		t.Fatal(err)
	}
	if err = resolved.Validate(value); err != nil {
		t.Fatal(err)
	}
	object.Document.Blocks[0].Children[0].Type = "unsupported"
	raw, _ = json.Marshal(mcpMutateOutput{Object: object})
	_ = json.Unmarshal(raw, &value)
	if err = resolved.Validate(value); err == nil {
		t.Fatal("MCP schema accepted unsupported nested node")
	}
	object.Document = nil
	raw, _ = json.Marshal(mcpMutateOutput{Object: object})
	_ = json.Unmarshal(raw, &value)
	if err = resolved.Validate(value); err == nil {
		t.Fatal("MCP schema accepted object without an editor document")
	}
	raw, _ = json.Marshal(schema)
	if !strings.Contains(string(raw), "schema_version") || !strings.Contains(string(raw), "$defs") {
		t.Fatal("missing canonical recursive contract")
	}
}

func ptrDoc(d document.Document) *document.Document { return &d }
