package main

import (
	"encoding/json"
	"fmt"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

// ---------------------------------------------------------------------------
// Token scopes
// ---------------------------------------------------------------------------

func TestParseTokenScopes(t *testing.T) {
	scopes, names, err := parseTokenScopes("")
	if err != nil || !scopes.fullAccess() || len(names) != 1 || names[0] != "full_access" {
		t.Fatalf("empty scopes must mean full access: %+v %v %v", scopes, names, err)
	}
	scopes, names, err = parseTokenScopes(" api:read ")
	if err != nil || scopes.read != true || scopes.write != false || len(names) != 1 || names[0] != scopeAPIRead {
		t.Fatalf("api:read parsing: %+v %v %v", scopes, names, err)
	}
	scopes, names, err = parseTokenScopes("API:Write")
	if err != nil || !scopes.read || !scopes.write || len(names) != 2 || names[0] != scopeAPIRead || names[1] != scopeAPIWrite {
		t.Fatalf("api:write must imply read: %+v %v %v", scopes, names, err)
	}
	scopes, _, err = parseTokenScopes("api:read,api:write")
	if err != nil || !scopes.read || !scopes.write {
		t.Fatalf("combined scopes: %+v %v", scopes, err)
	}
	if _, _, err = parseTokenScopes("api:admin"); err == nil {
		t.Fatal("unknown scope accepted")
	}
	if _, _, err = parseTokenScopes("read"); err == nil {
		t.Fatal("unprefixed scope accepted")
	}
}

func TestReadOnlyTokenScopeMatrix(t *testing.T) {
	app, _, _ := makeApp(t)
	if err := app.SetScopes("api:read"); err != nil {
		t.Fatal(err)
	}
	note := objectFrom(t, api(t, app, "POST", "/v1/sync/push", map[string]any{
		"device_id": "scope-matrix-device", "base_seq": 0,
		"operations": []map[string]any{{
			"op_id": "scope-matrix-0001", "action": "put",
			"object": map[string]any{"type_id": "note", "title": "Seeded by sync", "content": "seed"},
		}},
	}))
	_ = note

	readAllowed := []struct{ method, path string }{
		{"GET", "/v1/objects"},
		{"GET", "/v1/objects?type_id=note"},
		{"GET", "/v1/search?q=seed"},
		{"GET", "/v1/types"},
		{"GET", "/v1/trash"},
		{"GET", "/v1/assets"},
		{"GET", "/v1/api"},
		{"GET", "/v1/journal/2026-10-31"},
		{"GET", "/v1/export"},
		// POST endpoints that never mutate the store.
		{"POST", "/v1/markdown/to-blocks"},
		{"POST", "/v1/markdown/from-blocks"},
		{"POST", "/v1/export"},
	}
	for _, endpoint := range readAllowed {
		body := any(nil)
		switch endpoint.path {
		case "/v1/markdown/to-blocks":
			body = map[string]any{"markdown": "# hi"}
		case "/v1/markdown/from-blocks":
			body = map[string]any{"blocks": []any{}}
		}
		rr := api(t, app, endpoint.method, endpoint.path, body)
		if rr.Code != 200 {
			t.Fatalf("%s %s with api:read returned HTTP %d: %s", endpoint.method, endpoint.path, rr.Code, rr.Body.String())
		}
	}

	writesRejected := []struct{ method, path string }{
		{"POST", "/v1/objects"},
		{"PATCH", "/v1/objects/seeded"},
		{"DELETE", "/v1/objects/seeded"},
		{"POST", "/v1/objects/from-url"},
		{"POST", "/v1/objects/markdown"},
		{"POST", "/v1/objects/seeded/append-markdown"},
		{"POST", "/v1/journal/2026-10-31/append"},
		{"POST", "/v1/capture"},
		{"POST", "/v1/tasks/seeded/complete"},
		{"POST", "/v1/trash/empty"},
		{"POST", "/v1/assets"},
		{"POST", "/v1/assets/upload/init"},
	}
	for _, endpoint := range writesRejected {
		rr := api(t, app, endpoint.method, endpoint.path, map[string]any{})
		if rr.Code != 403 {
			t.Fatalf("%s %s with api:read returned HTTP %d, want 403", endpoint.method, endpoint.path, rr.Code)
		}
		if !strings.Contains(rr.Body.String(), "api:write") {
			t.Fatalf("%s %s: unclear scope error: %s", endpoint.method, endpoint.path, rr.Body.String())
		}
	}

	// The sync endpoints are first-party and full-trust: a scoped token must
	// not break the mobile client.
	pull := api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": 0})
	if pull.Code != 200 {
		t.Fatalf("sync/pull with api:read returned HTTP %d", pull.Code)
	}
	push := api(t, app, "POST", "/v1/sync/push", map[string]any{
		"device_id": "scope-matrix-device", "base_seq": 0,
		"operations": []map[string]any{{
			"op_id": "scope-matrix-0002", "action": "put",
			"object": map[string]any{"type_id": "note", "title": "Sync still writes", "content": "ok"},
		}},
	})
	if push.Code != 200 || !strings.Contains(push.Body.String(), "scope-matrix-0002") {
		t.Fatalf("sync/push with api:read returned HTTP %d: %s", push.Code, push.Body.String())
	}
}

func TestWriteTokenScopeAllowsWritesAndReads(t *testing.T) {
	app, _, _ := makeApp(t)
	if err := app.SetScopes("api:write"); err != nil {
		t.Fatal(err)
	}
	created := api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Write scope note"})
	if created.Code != 201 {
		t.Fatalf("api:write token could not create: HTTP %d: %s", created.Code, created.Body.String())
	}
	o := objectFrom(t, created)
	listed := api(t, app, "GET", "/v1/objects", nil)
	if listed.Code != 200 || !strings.Contains(listed.Body.String(), o.ID) {
		t.Fatal("api:write token could not read")
	}
}

// ---------------------------------------------------------------------------
// Version header and discovery
// ---------------------------------------------------------------------------

func TestAPIVersionHeaderEcho(t *testing.T) {
	app, _, _ := makeApp(t)
	health := httptest.NewRecorder()
	app.Handler().ServeHTTP(health, httptest.NewRequest("GET", "/health", nil))
	if health.Code != 200 || health.Header().Get("X-Lumen-Api-Version") != apiVersion {
		t.Fatalf("health must echo the API version: HTTP %d header %q", health.Code, health.Header().Get("X-Lumen-Api-Version"))
	}
	// An unknown request version is never rejected; the server answers with
	// the version it speaks.
	req := httptest.NewRequest("GET", "/v1/types", nil)
	req.Header.Set("Authorization", "Bearer "+testToken)
	req.Header.Set("X-Lumen-Api-Version", "99.9.9")
	rr := httptest.NewRecorder()
	app.Handler().ServeHTTP(rr, req)
	if rr.Code != 200 || rr.Header().Get("X-Lumen-Api-Version") != apiVersion {
		t.Fatalf("unknown request version must not fail: HTTP %d header %q", rr.Code, rr.Header().Get("X-Lumen-Api-Version"))
	}
	anonymous := httptest.NewRecorder()
	app.Handler().ServeHTTP(anonymous, httptest.NewRequest("GET", "/v1/types", nil))
	if anonymous.Code != 401 || anonymous.Header().Get("X-Lumen-Api-Version") != apiVersion {
		t.Fatal("the version header must be present even on a 401")
	}
}

func TestAPIDiscoveryDocument(t *testing.T) {
	app, _, _ := makeApp(t)
	rr := api(t, app, "GET", "/v1/api", nil)
	if rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	var doc struct {
		Version     string            `json:"version"`
		TokenScopes []string          `json:"token_scopes"`
		Limits      map[string]int    `json:"limits"`
		Endpoints   []apiEndpointInfo `json:"endpoints"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &doc); err != nil {
		t.Fatal(err)
	}
	if doc.Version != apiVersion {
		t.Fatalf("discovery version %q", doc.Version)
	}
	if len(doc.TokenScopes) != 1 || doc.TokenScopes[0] != "full_access" {
		t.Fatalf("unscoped token must report full_access: %v", doc.TokenScopes)
	}
	if doc.Limits["max_page_limit"] != maxPageLimit || doc.Limits["default_page_limit"] != defaultPageLimit {
		t.Fatalf("limits missing pagination: %v", doc.Limits)
	}
	if doc.Limits["max_chunked_asset_bytes"] != chunkedUploadMaxSize || doc.Limits["max_chunk_part_bytes"] != chunkedUploadMaxPart {
		t.Fatalf("limits missing upload constraints: %v", doc.Limits)
	}
	catalog := map[string]string{}
	for _, endpoint := range doc.Endpoints {
		catalog[endpoint.Method+" "+endpoint.Path] = endpoint.Scope
	}
	expected := map[string]string{
		"POST /v1/objects/from-url":                   scopeAPIWrite,
		"POST /v1/objects/markdown":                   scopeAPIWrite,
		"POST /v1/objects/{id}/append-markdown":       scopeAPIWrite,
		"POST /v1/journal/{date}/append":              scopeAPIWrite,
		"POST /v1/assets/upload/init":                 scopeAPIWrite,
		"POST /v1/assets/upload/{upload_id}/complete": scopeAPIWrite,
		"GET /v1/objects":                             scopeAPIRead,
		"POST /v1/markdown/to-blocks":                 scopeAPIRead,
		"GET /v1/api":                                 scopeAPIRead,
		"GET /health":                                 "public",
		"POST /v1/sync/push":                          "full",
	}
	for path, scope := range expected {
		if catalog[path] != scope {
			t.Fatalf("catalog %s has scope %q, want %q", path, catalog[path], scope)
		}
	}
	if err := app.SetScopes("api:read"); err != nil {
		t.Fatal(err)
	}
	scoped := api(t, app, "GET", "/v1/api", nil)
	var scopedDoc struct {
		TokenScopes []string `json:"token_scopes"`
	}
	if err := json.Unmarshal(scoped.Body.Bytes(), &scopedDoc); err != nil {
		t.Fatal(err)
	}
	if len(scopedDoc.TokenScopes) != 1 || scopedDoc.TokenScopes[0] != scopeAPIRead {
		t.Fatalf("scoped token must report its scopes: %v", scopedDoc.TokenScopes)
	}
}

// ---------------------------------------------------------------------------
// Create from URL
// ---------------------------------------------------------------------------

func fromURLApp(t *testing.T) *App {
	t.Helper()
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		if r.URL.Path == "/quiet" {
			_, _ = w.Write([]byte(readerArticlePage("")))
			return
		}
		_, _ = fmt.Fprintf(w, `<!doctype html><html><head>
<meta property="og:title" content="Article %s">
<meta property="og:description" content="Description of %s">
</head><body><article><h1>Article %s</h1><p>Readable body for %s with enough words that the extractor keeps the content.</p></article></body></html>`,
			r.URL.Path, r.URL.Path, r.URL.Path, r.URL.Path)
	}))
	t.Cleanup(server.Close)
	app, _, _ := makeApp(t)
	app.fetcher = readerTestFetcher(server)
	return app
}

func TestCreateObjectFromURLPipeline(t *testing.T) {
	app := fromURLApp(t)
	rr := api(t, app, "POST", "/v1/objects/from-url", map[string]any{
		"url":            "http://example.com/quiet?utm_source=newsletter",
		"tags":           []string{"reading", "web"},
		"notes_markdown": "# Notes\n\n- [ ] summarize the essay",
		"journal_link":   true,
	})
	if rr.Code != 201 {
		t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
	}
	var created struct {
		Object
		Existing  bool   `json:"existing"`
		JournalID string `json:"journal_id"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &created); err != nil {
		t.Fatal(err)
	}
	if created.Existing {
		t.Fatal("first capture must not be a duplicate")
	}
	if created.TypeID != "web" || created.Title != "The Quiet Web" {
		t.Fatalf("preview pipeline not reused: %+v", created.Object)
	}
	if url, _ := created.Properties["url"].(string); url != "http://example.com/quiet" {
		t.Fatalf("URL was not cleaned: %q", url)
	}
	if reader, _ := created.Properties["reader_content"].(string); !strings.Contains(reader, "slow reading") {
		t.Fatal("reader content missing from the from-url pipeline")
	}
	if created.Properties["preview_status"] != "ready" {
		t.Fatalf("preview not fetched: %v", created.Properties["preview_status"])
	}
	blocks := rawObjectBlocks(created.Object)
	if len(blocks) != 2 {
		t.Fatalf("notes_markdown did not produce blocks: %v", created.Properties["blocks"])
	}
	if !strings.Contains(created.Content, "summarize the essay") {
		t.Fatalf("plain-text mirror missing: %q", created.Content)
	}
	// The tag objects were created and linked.
	tags, _ := created.Properties["tags"].([]any)
	if len(tags) != 2 {
		t.Fatalf("tags not resolved: %v", created.Properties["tags"])
	}
	if created.JournalID == "" || created.JournalID != "journal-"+todayJournalDate() {
		t.Fatalf("journal_link did not link today's journal: %q", created.JournalID)
	}
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/"+todayJournalDate(), nil))
	found := false
	for _, id := range journal.Links {
		if id == created.ID {
			found = true
		}
	}
	if !found {
		t.Fatal("journal does not link the captured object")
	}
	// The tag objects were created and linked.
	for _, raw := range tags {
		id, _ := raw.(string)
		tag := objectFrom(t, api(t, app, "GET", "/v1/objects/"+id, nil))
		if tag.TypeID != "tag" {
			t.Fatalf("tag resolution created a %s", tag.TypeID)
		}
	}
}

func TestCreateObjectFromURLDedupeAndNotes(t *testing.T) {
	app := fromURLApp(t)
	first := api(t, app, "POST", "/v1/objects/from-url", map[string]any{"url": "http://example.com/quiet"})
	if first.Code != 201 {
		t.Fatal(first.Body.String())
	}
	o := objectFrom(t, first)
	// The tracking-parameter-free canonical URL is the identity: a different
	// spelling with noise still dedupes.
	second := api(t, app, "POST", "/v1/objects/from-url", map[string]any{
		"url":            "http://example.com/quiet?fbclid=abc123",
		"notes_markdown": "Append on dedupe",
	})
	if second.Code != 200 {
		t.Fatalf("dedupe must return 200: HTTP %d: %s", second.Code, second.Body.String())
	}
	var existing struct {
		Object
		Existing bool `json:"existing"`
	}
	if err := json.Unmarshal(second.Body.Bytes(), &existing); err != nil {
		t.Fatal(err)
	}
	if !existing.Existing || existing.ID != o.ID {
		t.Fatalf("dedupe did not return the existing object: %+v", existing.Object)
	}
	if existing.Revision <= o.Revision {
		t.Fatal("appending notes on the duplicate did not bump the revision")
	}
	if !strings.Contains(existing.Content, "Append on dedupe") {
		t.Fatalf("notes not appended: %q", existing.Content)
	}
	// Tags on a duplicate are unioned into the existing object.
	third := api(t, app, "POST", "/v1/objects/from-url", map[string]any{
		"url":  "http://example.com/quiet",
		"tags": []string{"dedupe-tag"},
	})
	if third.Code != 200 {
		t.Fatal(third.Body.String())
	}
	o = objectFrom(t, api(t, app, "GET", "/v1/objects/"+o.ID, nil))
	tags, _ := o.Properties["tags"].([]any)
	if len(tags) != 1 {
		t.Fatalf("tag not unioned into the duplicate: %v", o.Properties["tags"])
	}
	// A tag name owned by a non-tag object is a clear error.
	objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "owned-title"}))
	conflict := api(t, app, "POST", "/v1/objects/from-url", map[string]any{
		"url": "http://example.com/other-page", "tags": []string{"owned-title"},
	})
	if conflict.Code != 400 || !strings.Contains(conflict.Body.String(), "owned-title") {
		t.Fatalf("tag title conflict must be a 400: HTTP %d %s", conflict.Code, conflict.Body.String())
	}
}

func TestCreateObjectFromURLValidation(t *testing.T) {
	app := fromURLApp(t)
	for name, body := range map[string]map[string]any{
		"missing-url":  {"title": "No URL"},
		"ftp-scheme":   {"url": "ftp://example.com/file"},
		"private-host": {"url": "http://127.0.0.1/admin"},
	} {
		rr := api(t, app, "POST", "/v1/objects/from-url", body)
		if rr.Code != 400 {
			t.Fatalf("%s: HTTP %d: %s", name, rr.Code, rr.Body.String())
		}
	}
}

// ---------------------------------------------------------------------------
// Markdown create and append
// ---------------------------------------------------------------------------

func TestCreateObjectFromMarkdown(t *testing.T) {
	app, _, _ := makeApp(t)
	rr := api(t, app, "POST", "/v1/objects/markdown", map[string]any{
		"type_id":    "note",
		"title":      "From Markdown",
		"markdown":   "# Heading\n\n- [ ] task one\n\nParagraph with **bold**.",
		"tags":       []string{"md-tag"},
		"properties": map[string]any{"priority": "high"},
	})
	if rr.Code != 201 {
		t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
	}
	o := objectFrom(t, rr)
	blocks := rawObjectBlocks(o)
	if len(blocks) != 3 {
		t.Fatalf("markdown did not convert to 3 blocks: %v", o.Properties["blocks"])
	}
	first, _ := blocks[0].(map[string]any)
	if first["type"] != blockHeading || first["attrs"].(map[string]any)["level"] != float64(1) {
		t.Fatalf("first block is not a heading: %v", first)
	}
	if !strings.Contains(o.Content, "task one") || !strings.Contains(o.Content, "Paragraph with bold.") {
		t.Fatalf("plain-text mirror wrong: %q", o.Content)
	}
	if o.Properties["priority"] != "high" {
		t.Fatalf("passthrough properties lost: %v", o.Properties)
	}
	tags, _ := o.Properties["tags"].([]any)
	if len(tags) != 1 {
		t.Fatalf("tags not resolved: %v", o.Properties["tags"])
	}
	// Round-trip: the stored blocks render back to the same Markdown core.
	rendered := api(t, app, "POST", "/v1/markdown/from-blocks", map[string]any{"document": o.Document})
	var out struct {
		Markdown string `json:"markdown"`
	}
	if err := json.Unmarshal(rendered.Body.Bytes(), &out); err != nil {
		t.Fatal(err)
	}
	if !strings.Contains(out.Markdown, "# Heading") || !strings.Contains(out.Markdown, "- [ ] task one") || !strings.Contains(out.Markdown, "**bold**") {
		t.Fatalf("round-trip lost the document: %q", out.Markdown)
	}

	for name, body := range map[string]map[string]any{
		"missing-type":    {"markdown": "# x"},
		"unknown-type":    {"type_id": "no-such-type", "markdown": "# x"},
		"missing-md":      {"type_id": "note"},
		"collection":      {"type_id": "collection", "markdown": "# x"},
		"template":        {"type_id": "note", "markdown": "# x", "properties": map[string]any{"is_template": true}},
		"client-blocks":   {"type_id": "note", "markdown": "# x", "properties": map[string]any{"blocks": []any{}}},
		"client-tags":     {"type_id": "note", "markdown": "# x", "properties": map[string]any{"tags": []any{}}},
		"client-richtext": {"type_id": "note", "markdown": "# x", "properties": map[string]any{"rich_text": "<p>x</p>"}},
	} {
		bad := api(t, app, "POST", "/v1/objects/markdown", body)
		if bad.Code != 400 {
			t.Fatalf("%s: HTTP %d: %s", name, bad.Code, bad.Body.String())
		}
	}
	// A web object created via Markdown loses nothing the server owns.
	web := objectFrom(t, api(t, app, "POST", "/v1/objects/markdown", map[string]any{
		"type_id": "web", "title": "Manual web object", "markdown": "Body",
		"properties": map[string]any{"reader_content": "forged", "url": "https://example.com/manual"},
	}))
	if web.Properties["reader_content"] != nil {
		t.Fatalf("client-supplied reader_content survived: %v", web.Properties["reader_content"])
	}
}

func appendTestObject(t *testing.T, app *App, body map[string]any) Object {
	t.Helper()
	return objectFrom(t, api(t, app, "POST", "/v1/objects", body))
}

func TestAppendMarkdownToBlockObject(t *testing.T) {
	app, _, _ := makeApp(t)
	// The desktop persists blocks plus its rich_text snapshot; build the
	// same shape the editor would have saved.
	converted := api(t, app, "POST", "/v1/markdown/to-blocks", map[string]any{"markdown": "# Start\n\nIntro."})
	var parsed struct {
		Blocks []any `json:"blocks"`
	}
	if err := json.Unmarshal(converted.Body.Bytes(), &parsed); err != nil {
		t.Fatal(err)
	}
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Append target", "content": "Start\n\nIntro.",
		"properties": map[string]any{"blocks": parsed.Blocks, "rich_text": "<p>Intro.</p>"},
	}))
	rr := apiDocumentAppend(t, app, "POST", "/v1/objects/"+o.ID+"/append-markdown", map[string]any{"markdown": "## Section\n\nAdded later."})
	if rr.Code != 200 {
		t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
	}
	updated := objectFrom(t, rr)
	if updated.Revision != o.Revision+1 {
		t.Fatalf("append must bump the revision: %d -> %d", o.Revision, updated.Revision)
	}
	blocks := rawObjectBlocks(updated)
	if len(blocks) != 4 {
		t.Fatalf("blocks not appended: %v", blocks)
	}
	if _, exists := updated.Properties["rich_text"]; exists {
		t.Fatal("stale rich_text survived the append")
	}
	if !strings.Contains(updated.Content, "Added later.") {
		t.Fatalf("content not appended: %q", updated.Content)
	}
	// Insert at position 0.
	rr = apiDocumentAppend(t, app, "POST", "/v1/objects/"+o.ID+"/append-markdown", map[string]any{"markdown": "Top of the note.", "position": 0})
	if rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	updated = objectFrom(t, rr)
	blocks = rawObjectBlocks(updated)
	first, _ := blocks[0].(map[string]any)
	if !strings.Contains(blockSearchText(first), "Top of the note.") {
		t.Fatalf("position insert failed: %v", blocks[0])
	}
	// Out of range.
	if bad := apiDocumentAppend(t, app, "POST", "/v1/objects/"+o.ID+"/append-markdown", map[string]any{"markdown": "x", "position": 99}); bad.Code != 400 {
		t.Fatalf("out-of-range position: HTTP %d", bad.Code)
	}
	if bad := apiDocumentAppend(t, app, "POST", "/v1/objects/"+o.ID+"/append-markdown", map[string]any{"markdown": "x", "position": -1}); bad.Code != 400 {
		t.Fatalf("negative position: HTTP %d", bad.Code)
	}
	// Version history: the pre-append state is snapshot in the same write.
	versions := api(t, app, "GET", "/v1/objects/"+o.ID+"/versions", nil)
	if versions.Code != 200 || !strings.Contains(versions.Body.String(), "\"revision\"") {
		t.Fatalf("versions after append: HTTP %d %s", versions.Code, versions.Body.String())
	}
	// Mobile journal: the append is a normal object put.
	pull := api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": 0})
	if !strings.Contains(pull.Body.String(), o.ID) {
		t.Fatal("append did not reach the sync journal")
	}
	// Errors.
	if bad := apiDocumentAppend(t, app, "POST", "/v1/objects/missing-id/append-markdown", map[string]any{"markdown": "x"}); bad.Code != 404 {
		t.Fatalf("unknown object: HTTP %d", bad.Code)
	}
	for name, body := range map[string]map[string]any{
		"empty":  {"markdown": ""},
		"blank":  {"markdown": "  \n\t "},
		"absent": {},
	} {
		bad := apiDocumentAppend(t, app, "POST", "/v1/objects/"+o.ID+"/append-markdown", body)
		if bad.Code != 400 {
			t.Fatalf("%s markdown: HTTP %d", name, bad.Code)
		}
	}
}

func TestAppendMarkdownToPlainTextObjectNeverInventsBlocks(t *testing.T) {
	app, _, _ := makeApp(t)
	plain := appendTestObject(t, app, map[string]any{
		"type_id": "note", "title": "Plain body", "content": "Existing plain text.",
		"properties": map[string]any{"rich_text": "<p>Existing plain text.</p>"},
	})
	rr := apiDocumentAppend(t, app, "POST", "/v1/objects/"+plain.ID+"/append-markdown", map[string]any{"markdown": "- [ ] item one"})
	if rr.Code != 200 {
		t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
	}
	updated := objectFrom(t, rr)
	if blocks := rawObjectBlocks(updated); len(blocks) < 2 {
		t.Fatalf("blocks must not be invented for a plain-text object: %v", blocks)
	}
	if _, exists := updated.Properties["rich_text"]; exists {
		t.Fatal("stale rich_text survived the content append")
	}
	if !strings.Contains(updated.Content, "Existing plain text.") || !strings.Contains(updated.Content, "item one") {
		t.Fatalf("content append wrong: %q", updated.Content)
	}
	// An object with no text anywhere gains the block representation.
	empty := appendTestObject(t, app, map[string]any{"type_id": "note", "title": "Empty note"})
	rr = apiDocumentAppend(t, app, "POST", "/v1/objects/"+empty.ID+"/append-markdown", map[string]any{"markdown": "First content."})
	if rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	updated = objectFrom(t, rr)
	if blocks := rawObjectBlocks(updated); len(blocks) != 1 {
		t.Fatalf("empty object did not gain blocks: %v", updated.Properties["blocks"])
	}
}

// ---------------------------------------------------------------------------
// Journal append
// ---------------------------------------------------------------------------

func TestJournalAppendMarkdown(t *testing.T) {
	app, _, _ := makeApp(t)
	rr := apiDocumentAppend(t, app, "POST", "/v1/journal/2026-10-31/append", map[string]any{"markdown": "- walked the dog"})
	if rr.Code != 200 {
		t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
	}
	journal := objectFrom(t, rr)
	if journal.ID != "journal-2026-10-31" || journal.TypeID != "journal" {
		t.Fatalf("lazy journal semantics changed: %+v", journal)
	}
	if !strings.Contains(journal.Content, "walked the dog") {
		t.Fatalf("content missing: %q", journal.Content)
	}
	// Same date again: same note, appended.
	again := objectFrom(t, apiDocumentAppend(t, app, "POST", "/v1/journal/2026-10-31/append", map[string]any{"markdown": "- second entry"}))
	if again.ID != journal.ID || !strings.Contains(again.Content, "second entry") {
		t.Fatalf("second append failed: %+v", again)
	}
	// Errors.
	if bad := apiDocumentAppend(t, app, "POST", "/v1/journal/2026-13-45/append", map[string]any{"markdown": "x"}); bad.Code != 400 {
		t.Fatalf("invalid date: HTTP %d", bad.Code)
	}
	if bad := apiDocumentAppend(t, app, "POST", "/v1/journal/2026-12-24/append", map[string]any{"markdown": ""}); bad.Code != 400 {
		t.Fatalf("empty markdown: HTTP %d", bad.Code)
	}
}

func TestJournalAppendSections(t *testing.T) {
	app, _, _ := makeApp(t)
	// A fresh note gains a heading block plus the content.
	first := objectFrom(t, apiDocumentAppend(t, app, "POST", "/v1/journal/2026-11-01/append", map[string]any{"markdown": "entry one", "section": "Log"}))
	blocks := rawObjectBlocks(first)
	if len(blocks) != 2 {
		t.Fatalf("section did not create a heading block: %v", first.Properties["blocks"])
	}
	heading, _ := blocks[0].(map[string]any)
	if heading["type"] != blockHeading || blockSearchText(heading) != " Log" && !strings.Contains(blockSearchText(heading), "Log") || heading["attrs"].(map[string]any)["level"] != float64(2) {
		t.Fatalf("wrong section heading: %v", heading)
	}
	// A second entry to the same section lands inside it.
	second := objectFrom(t, apiDocumentAppend(t, app, "POST", "/v1/journal/2026-11-01/append", map[string]any{"markdown": "entry two", "section": "Log"}))
	blocks = rawObjectBlocks(second)
	if len(blocks) != 3 {
		t.Fatalf("second section entry not appended in place: %v", blocks)
	}
	if entry, _ := blocks[1].(map[string]any); !strings.Contains(blockSearchText(entry), "entry one") {
		t.Fatalf("section order broken: %v", blocks)
	}
	if entry, _ := blocks[2].(map[string]any); !strings.Contains(blockSearchText(entry), "entry two") {
		t.Fatalf("section order broken: %v", blocks)
	}
	// A new section is appended after the existing one.
	third := objectFrom(t, apiDocumentAppend(t, app, "POST", "/v1/journal/2026-11-01/append", map[string]any{"markdown": "plan a", "section": "Plan"}))
	blocks = rawObjectBlocks(third)
	if len(blocks) != 5 {
		t.Fatalf("new section not appended: %v", blocks)
	}
	lastHeading, _ := blocks[3].(map[string]any)
	if !strings.Contains(blockSearchText(lastHeading), "Plan") {
		t.Fatalf("new section heading misplaced: %v", blocks)
	}

	// A plain-text daily note keeps its content representation.
	plain := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-11-02", nil))
	plain = objectFrom(t, api(t, app, "PUT", "/v1/objects/"+plain.ID+"/document", map[string]any{"base_revision": plain.Revision, "op_id": "journal-existing-text-001", "document": map[string]any{"schema_version": 2, "blocks": []any{map[string]any{"id": "mobile-existing", "type": "paragraph", "content": []any{map[string]any{"type": "text", "text": "Existing mobile text."}}}}}}))
	after := objectFrom(t, apiDocumentAppend(t, app, "POST", "/v1/journal/2026-11-02/append", map[string]any{"markdown": "plain entry", "section": "Log"}))
	if blocks := rawObjectBlocks(after); len(blocks) == 0 {
		t.Fatalf("plain-text journal must not gain blocks: %v", blocks)
	}
	if !strings.Contains(after.Content, "Log") || !strings.Contains(after.Content, "plain entry") || !strings.Contains(after.Content, "Existing mobile text.") {
		t.Fatalf("plain section append wrong: %q", after.Content)
	}
	if strings.Count(after.Content, "Log") != 1 {
		t.Fatalf("section heading duplicated: %q", after.Content)
	}
	after = objectFrom(t, apiDocumentAppend(t, app, "POST", "/v1/journal/2026-11-02/append", map[string]any{"markdown": "plain entry two", "section": "Log"}))
	if !strings.Contains(after.Content, "plain entry two") || strings.Count(after.Content, "Log") != 1 {
		t.Fatalf("second plain section entry wrong: %q", after.Content)
	}
	// Section insert before a following heading line stays inside the section.
	after = objectFrom(t, apiDocumentAppend(t, app, "POST", "/v1/journal/2026-11-02/append", map[string]any{"markdown": "other entry", "section": "Other"}))
	after = objectFrom(t, apiDocumentAppend(t, app, "POST", "/v1/journal/2026-11-02/append", map[string]any{"markdown": "log last", "section": "Log"}))
	logPos := strings.Index(after.Content, "Log")
	otherPos := strings.Index(after.Content, "Other")
	lastPos := strings.Index(after.Content, "log last")
	if !(logPos < lastPos && lastPos < otherPos) {
		t.Fatalf("section insert did not stay inside its section: %q", after.Content)
	}
}
