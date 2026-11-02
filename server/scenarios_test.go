package main

// End-to-end user-journey scenarios (backend parity stage 14).
//
// Each TestScenario* function walks one realistic story through the HTTP API,
// combining the features of several earlier stages and asserting the state
// after every step. The helpers here are intentionally small: they only remove
// JSON boilerplate, never business logic, so a failure points at the scenario
// step that broke instead of at a helper.

import (
	"encoding/json"
	"net/http/httptest"
	"strings"
	"testing"
)

// scenarioGet fetches one live object and fails the test when it is missing.
func scenarioGet(t *testing.T, app *App, id string) Object {
	t.Helper()
	return objectFrom(t, api(t, app, "GET", "/v1/objects/"+id, nil))
}

// scenarioStatus asserts an exact HTTP status and prints the body on failure.
func scenarioStatus(t *testing.T, rr *httptest.ResponseRecorder, want int) {
	t.Helper()
	if rr.Code != want {
		t.Fatalf("HTTP %d, want %d: %s", rr.Code, want, rr.Body.String())
	}
}

// scenarioContainsID reports whether ids contains id.
func scenarioContainsID(ids []string, id string) bool {
	for _, candidate := range ids {
		if candidate == id {
			return true
		}
	}
	return false
}

// scenarioBacklinks decodes the backlinks endpoint.
func scenarioBacklinks(t *testing.T, app *App, id string) []Object {
	t.Helper()
	return decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+id+"/backlinks", nil))
}

// scenarioMarkdownBlocks converts Markdown through the stage-4 utility
// endpoint. The api-v2 object-from-markdown endpoint is not part of this
// branch, so scenarios compose the converter with the normal object API.
func scenarioMarkdownBlocks(t *testing.T, app *App, markdown string) []Block {
	t.Helper()
	rr := api(t, app, "POST", "/v1/markdown/to-blocks", map[string]any{"markdown": markdown})
	scenarioStatus(t, rr, 200)
	var response struct {
		Blocks []Block `json:"blocks"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &response); err != nil {
		t.Fatal(err)
	}
	return response.Blocks
}

// scenarioBlocksMarkdown converts blocks back to canonical Markdown.
func scenarioBlocksMarkdown(t *testing.T, app *App, blocks []Block) string {
	t.Helper()
	rr := api(t, app, "POST", "/v1/markdown/from-blocks", map[string]any{"blocks": blocks})
	scenarioStatus(t, rr, 200)
	var response struct {
		Markdown string `json:"markdown"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &response); err != nil {
		t.Fatal(err)
	}
	return response.Markdown
}

// scenarioCreateFromMarkdown creates an object whose block tree comes from the
// Markdown converter, which is how a user pastes Markdown into a new note.
func scenarioCreateFromMarkdown(t *testing.T, app *App, typeID, title, markdown string) Object {
	t.Helper()
	blocks := scenarioMarkdownBlocks(t, app, markdown)
	doc, err := canonicalDocumentFromBlocks(blocks)
	if err != nil {
		t.Fatal(err)
	}
	return objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": typeID, "title": title, "document": doc}))
}

// scenarioObjectBlocks reads the stored block tree of an object.
func scenarioObjectBlocks(t *testing.T, o Object) []Block {
	t.Helper()
	if o.Document != nil {
		return documentMarkdownBlocks(*o.Document)
	}
	raw, ok := o.Properties["blocks"]
	if !ok {
		t.Fatalf("object %s has no blocks: %#v", o.ID, o.Properties)
	}
	encoded, err := json.Marshal(raw)
	if err != nil {
		t.Fatal(err)
	}
	var blocks []Block
	if err := json.Unmarshal(encoded, &blocks); err != nil {
		t.Fatal(err)
	}
	return blocks
}

// scenarioTag creates a tag object.
func scenarioTag(t *testing.T, app *App, title string) Object {
	t.Helper()
	return objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": title}))
}

// scenarioSearchIDs returns the ids of a search response.
func scenarioSearchIDs(t *testing.T, app *App, query string) []string {
	t.Helper()
	results := searchFor(t, app, query)
	ids := make([]string, 0, len(results))
	for _, result := range results {
		ids = append(ids, result.ID)
	}
	return ids
}

// scenarioTrashIDs returns the ids currently in the trash.
func scenarioTrashIDs(t *testing.T, app *App) []string {
	t.Helper()
	entries := decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash", nil))
	ids := make([]string, 0, len(entries))
	for _, entry := range entries {
		ids = append(ids, entry.ID)
	}
	return ids
}

// scenarioExportAssets indexes the manifest asset table by asset id.
func scenarioExportAssets(t *testing.T, entries map[string][]byte) map[string]exportAssetEntry {
	t.Helper()
	manifest := manifestFrom(t, entries)
	out := make(map[string]exportAssetEntry, len(manifest.Assets))
	for _, asset := range manifest.Assets {
		out[asset.ID] = asset
	}
	return out
}

// scenarioCSVValue returns the value of a named column in a CSV row.
func scenarioCSVValue(t *testing.T, rows [][]string, row []string, column string) string {
	t.Helper()
	index := csvColumn(t, rows, column)
	if index >= len(row) {
		t.Fatalf("row %v has no column %q", row, column)
	}
	return row[index]
}

// scenarioCompactJSON is used only in failure messages.
func scenarioCompactJSON(t *testing.T, value any) string {
	t.Helper()
	raw, err := json.Marshal(value)
	if err != nil {
		t.Fatal(err)
	}
	return string(raw)
}

// scenarioAssertContains fails when haystack does not contain needle. It keeps
// the assertions in the scenarios short and readable.
func scenarioAssertContains(t *testing.T, haystack, needle, context string) {
	t.Helper()
	if !strings.Contains(haystack, needle) {
		t.Fatalf("%s: %q is missing from %q", context, needle, haystack)
	}
}
