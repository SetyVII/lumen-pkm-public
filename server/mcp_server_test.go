package main

// End-to-end tests for the MCP endpoint (backend parity stage mcp-server):
// a real streamable HTTP client from the official Go SDK talks to the full
// App handler (bearer-token middleware included) over an httptest server,
// exercising the initialize handshake, the tool catalog, every read and write
// tool, the scope enforcement, the trash exclusion and the creation limit.
// State changes are verified through the store and the regular REST API.

import (
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

// mcpBearerTransport injects the Authorization header on every request the
// streamable client transport makes, the same way an MCP client configured
// with a bearer token does.
type mcpBearerTransport struct {
	token string
}

func (t mcpBearerTransport) RoundTrip(r *http.Request) (*http.Response, error) {
	r.Header.Set("Authorization", "Bearer "+t.token)
	return http.DefaultTransport.RoundTrip(r)
}

// mcpConnect starts a test server for the whole handler stack and connects an
// MCP client to /mcp. The standalone SSE stream is disabled: these tests never
// need server-initiated messages.
func mcpConnect(t *testing.T, app *App) (*mcp.ClientSession, *httptest.Server) {
	t.Helper()
	server := httptest.NewServer(app.Handler())
	t.Cleanup(server.Close)
	client := mcp.NewClient(&mcp.Implementation{Name: "lumen-tests", Version: "1.0.0"}, nil)
	transport := &mcp.StreamableClientTransport{
		Endpoint:             server.URL + mcpEndpointPath,
		HTTPClient:           &http.Client{Transport: mcpBearerTransport{token: testToken}},
		DisableStandaloneSSE: true,
	}
	cs, err := client.Connect(context.Background(), transport, nil)
	if err != nil {
		t.Fatalf("MCP initialize handshake failed: %v", err)
	}
	t.Cleanup(func() { _ = cs.Close() })
	return cs, server
}

// mcpCall invokes one tool and fails the test on a protocol-level error.
func mcpCall(t *testing.T, cs *mcp.ClientSession, name string, arguments map[string]any) *mcp.CallToolResult {
	t.Helper()
	res, err := cs.CallTool(context.Background(), &mcp.CallToolParams{Name: name, Arguments: arguments})
	if err != nil {
		t.Fatalf("CallTool %s: %v", name, err)
	}
	return res
}

func mcpResultText(res *mcp.CallToolResult) string {
	if len(res.Content) == 0 {
		return ""
	}
	if text, ok := res.Content[0].(*mcp.TextContent); ok {
		return text.Text
	}
	return ""
}

// mcpDecode asserts a successful tool call and decodes its structured output.
func mcpDecode(t *testing.T, cs *mcp.ClientSession, name string, arguments map[string]any, out any) {
	t.Helper()
	res := mcpCall(t, cs, name, arguments)
	if res.IsError {
		t.Fatalf("tool %s failed: %s", name, mcpResultText(res))
	}
	raw, err := json.Marshal(res.StructuredContent)
	if err != nil {
		t.Fatalf("tool %s: structured content: %v", name, err)
	}
	if err := json.Unmarshal(raw, out); err != nil {
		t.Fatalf("tool %s: decoding output: %v", name, err)
	}
}

// mcpExpectError asserts the tool answered with an in-band error carrying the
// message an agent needs to self-correct.
func mcpExpectError(t *testing.T, cs *mcp.ClientSession, name string, arguments map[string]any, contains string) {
	t.Helper()
	res := mcpCall(t, cs, name, arguments)
	if !res.IsError {
		t.Fatalf("tool %s unexpectedly succeeded", name)
	}
	if !strings.Contains(mcpResultText(res), contains) {
		t.Fatalf("tool %s error %q does not contain %q", name, mcpResultText(res), contains)
	}
}

func mcpArguments(pairs ...any) map[string]any {
	out := map[string]any{}
	for i := 0; i+1 < len(pairs); i += 2 {
		out[pairs[i].(string)] = pairs[i+1]
	}
	return out
}

// ---------------------------------------------------------------------------
// Transport and handshake
// ---------------------------------------------------------------------------

func TestMCPEndpointRequiresBearerToken(t *testing.T) {
	app, _, _ := makeApp(t)
	server := httptest.NewServer(app.Handler())
	t.Cleanup(server.Close)
	body := strings.NewReader(`{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-11-25"}}`)
	for name, headers := range map[string]http.Header{
		"missing":   {},
		"wrong":     {"Authorization": []string{"Bearer definitely-not-the-token-0123456789"}},
		"malformed": {"Authorization": []string{"Basic abc"}},
	} {
		req, _ := http.NewRequest("POST", server.URL+mcpEndpointPath, body)
		for key, values := range headers {
			req.Header[key] = values
		}
		req.Header.Set("Content-Type", "application/json")
		req.Header.Set("Accept", "application/json, text/event-stream")
		resp, err := http.DefaultClient.Do(req)
		if err != nil {
			t.Fatalf("%s token: %v", name, err)
		}
		resp.Body.Close()
		if resp.StatusCode != 401 {
			t.Fatalf("%s token: HTTP %d, want 401", name, resp.StatusCode)
		}
		if resp.Header.Get("X-Lumen-Api-Version") != apiVersion {
			t.Fatalf("%s token: version header missing", name)
		}
	}
}

func TestMCPHandshakeAndToolCatalog(t *testing.T) {
	app, _, _ := makeApp(t)
	cs, _ := mcpConnect(t, app)
	init := cs.InitializeResult()
	if init == nil || init.ServerInfo == nil || init.ServerInfo.Name != "lumen" || init.ServerInfo.Version != apiVersion {
		t.Fatalf("unexpected initialize result: %+v", init)
	}
	if init.ProtocolVersion == "" {
		t.Fatal("initialize result carries no protocol version")
	}
	list, err := cs.ListTools(context.Background(), nil)
	if err != nil {
		t.Fatalf("ListTools: %v", err)
	}
	want := map[string]bool{
		// read tools
		"search": false, "read_object": false, "read_object_blocks": false,
		"list_by_type": false, "list_by_tag": false, "list_by_collection": false,
		"get_type_shape": false,
		// write tools
		"create_object": false, "append_md_to_object": false, "append_to_daily_note": false,
		"add_block": false, "update_block": false, "delete_block": false, "create_task": false,
	}
	readTools := map[string]bool{"search": true, "read_object": true, "read_object_blocks": true,
		"list_by_type": true, "list_by_tag": true, "list_by_collection": true, "get_type_shape": true}
	for _, tool := range list.Tools {
		if _, known := want[tool.Name]; !known {
			t.Fatalf("unexpected tool %q", tool.Name)
		}
		want[tool.Name] = true
		if tool.Description == "" {
			t.Fatalf("tool %q has no description for the model", tool.Name)
		}
		schema, _ := tool.InputSchema.(map[string]any)
		if schema["type"] != "object" {
			t.Fatalf("tool %q input schema is not an object: %v", tool.Name, tool.InputSchema)
		}
		if readTools[tool.Name] && (tool.Annotations == nil || !tool.Annotations.ReadOnlyHint) {
			t.Fatalf("read tool %q lacks the readOnlyHint annotation", tool.Name)
		}
	}
	for name, seen := range want {
		if !seen {
			t.Fatalf("tool %q missing from the catalog", name)
		}
	}
}

// ---------------------------------------------------------------------------
// Read tools
// ---------------------------------------------------------------------------

func mcpReadFixtures(t *testing.T, app *App) (book, deep, note, collection, template, victim Object) {
	t.Helper()
	book = objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "book", "title": "The Happiness Hypothesis",
		"content":    "Highlight: the mind is divided.",
		"properties": map[string]any{"aliases": []string{"THH"}},
	}))
	deep = objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "book", "title": "Deep Work", "content": "Focus on the deep.",
	}))
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "philosophy"}))
	// properties is a replacement document: the aliases must ride along.
	book = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+book.ID, map[string]any{
		"properties": map[string]any{"aliases": []string{"THH"}, "tags": []string{tag.ID}},
	}))
	note = objectFrom(t, api(t, app, "POST", "/v1/objects/markdown", map[string]any{
		"type_id": "note", "title": "Study methods", "markdown": "# Methods\n\n- [ ] review",
	}))
	collection = objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "collection", "title": "Reading list",
		"properties": map[string]any{"item_type_id": "book", "items": []string{book.ID}},
	}))
	template = objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Template note",
		"properties": map[string]any{"is_template": true},
	}))
	victim = objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Trash me"}))
	if rr := api(t, app, "DELETE", "/v1/objects/"+victim.ID, nil); rr.Code != 200 {
		t.Fatalf("trashing the victim: HTTP %d: %s", rr.Code, rr.Body.String())
	}
	return book, deep, note, collection, template, victim
}

func TestMCPReadTools(t *testing.T) {
	app, _, _ := makeApp(t)
	book, deep, note, collection, template, victim := mcpReadFixtures(t, app)
	cs, _ := mcpConnect(t, app)

	var search mcpSearchOutput
	mcpDecode(t, cs, "search", mcpArguments("query", "happiness"), &search)
	if len(search.Results) != 1 || search.Results[0].ID != book.ID {
		t.Fatalf("search: %+v", search.Results)
	}
	if !strings.Contains(search.Results[0].Snippet, "divided") {
		t.Fatalf("search snippet: %+v", search.Results[0])
	}
	mcpDecode(t, cs, "search", mcpArguments("query", "thh"), &search)
	if len(search.Results) != 1 || search.Results[0].MatchedAlias != "THH" {
		t.Fatalf("alias search: %+v", search.Results)
	}
	mcpDecode(t, cs, "search", mcpArguments("query", "work", "type_id", "book"), &search)
	if len(search.Results) != 1 || search.Results[0].ID != deep.ID {
		t.Fatalf("typed search: %+v", search.Results)
	}
	// Templates and trashed objects stay invisible to read tools.
	mcpDecode(t, cs, "search", mcpArguments("query", "template note"), &search)
	if len(search.Results) != 0 {
		t.Fatalf("template leaked into search: %+v", search.Results)
	}
	mcpDecode(t, cs, "search", mcpArguments("query", "trash me"), &search)
	if len(search.Results) != 0 {
		t.Fatalf("trashed object leaked into search: %+v", search.Results)
	}
	mcpExpectError(t, cs, "search", mcpArguments("query", ""), "Provide a search query")
	mcpExpectError(t, cs, "search", mcpArguments("query", "x", "type_id", "nope"), "Unknown object type")

	var read mcpReadObjectOutput
	mcpDecode(t, cs, "read_object", mcpArguments("object_id", book.ID), &read)
	if read.Object.ID != book.ID || read.Object.Title != book.Title {
		t.Fatalf("read_object: %+v", read.Object)
	}
	aliases, _ := read.Object.Properties["aliases"].([]any)
	if len(aliases) != 1 || aliases[0] != "THH" {
		t.Fatalf("read_object aliases: %v", read.Object.Properties["aliases"])
	}
	if !strings.Contains(read.Markdown, "divided") {
		t.Fatalf("read_object markdown falls back to content: %q", read.Markdown)
	}

	var blocks mcpReadBlocksOutput
	mcpDecode(t, cs, "read_object_blocks", mcpArguments("object_id", note.ID), &blocks)
	if len(blocks.Blocks) != 2 || blocks.Blocks[0].Type != blockHeading || blocks.Blocks[1].Type != blockTodo {
		t.Fatalf("read_object_blocks: %+v", blocks.Blocks)
	}
	if !strings.Contains(blocks.Markdown, "# Methods") || !strings.Contains(blocks.Markdown, "[ ] review") {
		t.Fatalf("read_object_blocks markdown: %q", blocks.Markdown)
	}
	mcpDecode(t, cs, "read_object_blocks", mcpArguments("object_id", book.ID), &blocks)
	if len(blocks.Blocks) != 0 || blocks.Markdown != book.Content {
		t.Fatalf("blocks for a plain object: %+v", blocks)
	}

	// list_by_type with the cursor walk, plus REST cursor interchangeability.
	var page mcpObjectListOutput
	mcpDecode(t, cs, "list_by_type", mcpArguments("type_id", "book", "limit", 1), &page)
	if len(page.Objects) != 1 || page.NextCursor == "" {
		t.Fatalf("list_by_type page 1: %+v", page)
	}
	var next mcpObjectListOutput
	mcpDecode(t, cs, "list_by_type", mcpArguments("type_id", "book", "cursor", page.NextCursor), &next)
	if len(next.Objects) != 1 || next.NextCursor != "" {
		t.Fatalf("list_by_type page 2: %+v", next)
	}
	if page.Objects[0].ID == next.Objects[0].ID {
		t.Fatal("list_by_type repeated the same object across pages")
	}
	var notes mcpObjectListOutput
	mcpDecode(t, cs, "list_by_type", mcpArguments("type_id", "note"), &notes)
	for _, o := range notes.Objects {
		if o.ID == template.ID || o.ID == victim.ID {
			t.Fatalf("list_by_type leaked %s", o.ID)
		}
	}
	restPage := api(t, app, "GET", "/v1/objects?type_id=book&limit=1", nil)
	restCursor := restPage.Header().Get("X-Lumen-Next-Cursor")
	if restCursor == "" {
		t.Fatal("REST page produced no cursor")
	}
	mcpDecode(t, cs, "list_by_type", mcpArguments("type_id", "book", "cursor", restCursor), &next)
	if len(next.Objects) != 1 {
		t.Fatalf("REST cursor rejected by MCP: %+v", next)
	}
	mcpExpectError(t, cs, "list_by_type", mcpArguments("type_id", "nope"), "Unknown object type")
	mcpExpectError(t, cs, "list_by_type", mcpArguments("type_id", "book", "cursor", "garbage"), "cursor")

	var tagged mcpObjectListOutput
	mcpDecode(t, cs, "list_by_tag", mcpArguments("tag_name", "philosophy"), &tagged)
	if len(tagged.Objects) != 1 || tagged.Objects[0].ID != book.ID {
		t.Fatalf("list_by_tag: %+v", tagged.Objects)
	}
	mcpExpectError(t, cs, "list_by_tag", mcpArguments("tag_name", "missing-tag"), "No tag named")

	var items mcpObjectListOutput
	mcpDecode(t, cs, "list_by_collection", mcpArguments("collection_id", collection.ID), &items)
	if len(items.Objects) != 1 || items.Objects[0].ID != book.ID {
		t.Fatalf("list_by_collection: %+v", items.Objects)
	}
	mcpExpectError(t, cs, "list_by_collection", mcpArguments("collection_id", book.ID), "not a collection")
	mcpExpectError(t, cs, "list_by_collection", mcpArguments("collection_id", "missing-id"), "does not exist")

	var shape ObjectType
	mcpDecode(t, cs, "get_type_shape", mcpArguments("type_id", "book"), &shape)
	if shape.ID != "book" || len(shape.Properties) == 0 {
		t.Fatalf("get_type_shape: %+v", shape)
	}
	var taskShape ObjectType
	mcpDecode(t, cs, "get_type_shape", mcpArguments("type_id", "task"), &taskShape)
	keys := map[string]bool{}
	for _, raw := range taskShape.Properties {
		switch def := raw.(type) {
		case string:
			keys[def] = true
		case map[string]any:
			if key, _ := def["key"].(string); key != "" {
				keys[key] = true
			}
		}
	}
	if !keys["due_date"] || !keys["status"] {
		t.Fatalf("task shape misses task properties: %v", taskShape.Properties)
	}
	mcpExpectError(t, cs, "get_type_shape", mcpArguments("type_id", "nope"), "Unknown object type")

	// Trashed objects are not readable, exactly like GET /v1/objects/{id}.
	mcpExpectError(t, cs, "read_object", mcpArguments("object_id", victim.ID), "Object not found")
	mcpExpectError(t, cs, "read_object_blocks", mcpArguments("object_id", victim.ID), "Object not found")
	mcpExpectError(t, cs, "read_object", mcpArguments("object_id", "missing-id"), "Object not found")
}

// ---------------------------------------------------------------------------
// Write tools
// ---------------------------------------------------------------------------

func TestMCPCreateObjectAndAppendTools(t *testing.T) {
	app, _, _ := makeApp(t)
	cs, _ := mcpConnect(t, app)

	var created mcpMutateOutput
	mcpDecode(t, cs, "create_object", mcpArguments(
		"type_id", "note", "title", "Agent note",
		"markdown", "# Title\n\n- [ ] task", "tags", []string{"agent-tag"},
	), &created)
	stored := objectFrom(t, api(t, app, "GET", "/v1/objects/"+created.Object.ID, nil))
	if stored.Title != "Agent note" {
		t.Fatalf("create_object title: %+v", stored)
	}
	blocks, err := objectBlocks(stored)
	if err != nil || len(blocks) != 2 || blocks[0].Type != blockHeading || blocks[1].Type != blockTodo {
		t.Fatalf("create_object blocks: %+v (%v)", blocks, err)
	}
	if stored.Content != blocksPlainText(blocks) {
		t.Fatalf("create_object content mirror: %q", stored.Content)
	}
	tagIDs := stringList(stored.Properties["tags"])
	if len(tagIDs) != 1 {
		t.Fatalf("create_object tags: %v", stored.Properties["tags"])
	}
	var tags mcpObjectListOutput
	mcpDecode(t, cs, "list_by_type", mcpArguments("type_id", "tag"), &tags)
	found := false
	for _, tag := range tags.Objects {
		if tag.Title == "agent-tag" {
			found = true
		}
	}
	if !found {
		t.Fatal("the tag was not created get-or-create style")
	}

	// Insert at position 0 through append_md_to_object.
	var appended mcpMutateOutput
	mcpDecode(t, cs, "append_md_to_object", mcpArguments(
		"object_id", created.Object.ID, "markdown", "Inserted first", "position", 0,
	), &appended)
	blocks, _ = objectBlocks(objectFrom(t, api(t, app, "GET", "/v1/objects/"+created.Object.ID, nil)))
	if len(blocks) != 3 || blocks[0].Text != "Inserted first" {
		t.Fatalf("append_md_to_object position: %+v", blocks)
	}
	mcpExpectError(t, cs, "append_md_to_object", mcpArguments("object_id", created.Object.ID, "markdown", "x", "position", 99), "position")
	mcpExpectError(t, cs, "append_md_to_object", mcpArguments("object_id", "missing-id", "markdown", "x"), "Object not found")
	mcpExpectError(t, cs, "append_md_to_object", mcpArguments("object_id", created.Object.ID, "markdown", "  "), "Provide markdown")

	// The daily note append creates today's journal lazily.
	var journal mcpMutateOutput
	mcpDecode(t, cs, "append_to_daily_note", mcpArguments("markdown", "- walked the dog"), &journal)
	if journal.Object.ID != "journal-"+todayJournalDate() || journal.Object.TypeID != "journal" {
		t.Fatalf("append_to_daily_note: %+v", journal.Object)
	}
	daily := objectFrom(t, api(t, app, "GET", "/v1/journal/"+todayJournalDate(), nil))
	if !strings.Contains(daily.Content, "walked the dog") {
		t.Fatalf("daily note content: %q", daily.Content)
	}
	mcpDecode(t, cs, "append_to_daily_note", mcpArguments("markdown", "entry", "date", "2026-11-05"), &journal)
	if journal.Object.ID != "journal-2026-11-05" {
		t.Fatalf("append_to_daily_note explicit date: %+v", journal.Object)
	}
	mcpExpectError(t, cs, "append_to_daily_note", mcpArguments("markdown", "x", "date", "2026-13-45"), "Date must be YYYY-MM-DD")

	// The same validation as POST /v1/objects/markdown.
	mcpExpectError(t, cs, "create_object", mcpArguments("type_id", "note", "properties", map[string]any{"blocks": []any{}}), "managed by this endpoint")
	mcpExpectError(t, cs, "create_object", mcpArguments("type_id", "note", "properties", map[string]any{"is_template": true}), "Templates")
	mcpExpectError(t, cs, "create_object", mcpArguments("type_id", "collection"), "Collections")
	mcpExpectError(t, cs, "create_object", mcpArguments("type_id", "nope", "markdown", "x"), "Unknown object type")

	// Creation limit: the object plus tag objects, at most 20 per call.
	many := make([]string, 20)
	for i := range many {
		many[i] = "bulk-tag"
	}
	mcpExpectError(t, cs, "create_object", mcpArguments("type_id", "note", "tags", many), "at most 20 objects")
	nineteen := make([]string, 19)
	for i := range nineteen {
		nineteen[i] = "limit-tag"
	}
	mcpDecode(t, cs, "create_object", mcpArguments("type_id", "note", "title", "Budget", "tags", nineteen), &created)
	budget := objectFrom(t, api(t, app, "GET", "/v1/objects/"+created.Object.ID, nil))
	tagIDs = stringList(budget.Properties["tags"])
	if len(tagIDs) != 1 {
		t.Fatalf("duplicate tag names must deduplicate: %v", budget.Properties["tags"])
	}
}

func TestMCPBlockTools(t *testing.T) {
	app, _, _ := makeApp(t)
	cs, _ := mcpConnect(t, app)

	var created mcpMutateOutput
	mcpDecode(t, cs, "create_object", mcpArguments("type_id", "note", "title", "Block target"), &created)
	objectID := created.Object.ID

	var added mcpMutateOutput
	mcpDecode(t, cs, "add_block", mcpArguments("object_id", objectID, "block", map[string]any{"type": "paragraph", "text": "First paragraph"}), &added)
	if added.Object.Content != "First paragraph" {
		t.Fatalf("add_block on an empty object: %+v", added.Object)
	}
	mcpDecode(t, cs, "add_block", mcpArguments("object_id", objectID, "block", map[string]any{"type": "todo", "text": "Write tests"}, "position", 0), &added)
	mcpDecode(t, cs, "add_block", mcpArguments("object_id", objectID, "block", map[string]any{"type": "heading", "text": "Title", "level": 9}), &added)
	blocks, _ := objectBlocks(objectFrom(t, api(t, app, "GET", "/v1/objects/"+objectID, nil)))
	if len(blocks) != 3 {
		t.Fatalf("add_block count: %+v", blocks)
	}
	if blocks[0].Type != blockTodo || !strings.HasPrefix(blocks[0].Text, todoOpen) {
		t.Fatalf("todo marker missing: %+v", blocks[0])
	}
	if blocks[2].Level != 4 {
		t.Fatalf("heading level not clamped: %+v", blocks[2])
	}

	// A plain-text object refuses blocks: the block array is authoritative in
	// the editor and would hide the plain body.
	plain := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Plain", "content": "Just text"}))
	mcpExpectError(t, cs, "add_block", mcpArguments("object_id", plain.ID, "block", map[string]any{"type": "paragraph", "text": "Nope"}), "plain text")
	mcpExpectError(t, cs, "delete_block", mcpArguments("object_id", plain.ID, "block_id", "x"), "no block representation")
	mcpExpectError(t, cs, "add_block", mcpArguments("object_id", objectID, "block", map[string]any{"type": "wat", "text": "x"}), "Unknown block type")
	mcpExpectError(t, cs, "add_block", mcpArguments("object_id", objectID, "block", map[string]any{"type": "paragraph"}), "text or runs")
	mcpExpectError(t, cs, "add_block", mcpArguments("object_id", objectID, "block", map[string]any{"type": "paragraph", "text": "x"}, "position", 99), "position")

	// update_block keeps the stored id and recomputes the content mirror.
	var update mcpMutateOutput
	mcpDecode(t, cs, "update_block", mcpArguments("object_id", objectID, "block_id", blocks[1].ID,
		"block", map[string]any{"type": "heading", "text": "Renamed", "level": 2}), &update)
	updated, _ := objectBlocks(objectFrom(t, api(t, app, "GET", "/v1/objects/"+objectID, nil)))
	if updated[1].ID != blocks[1].ID || updated[1].Type != blockHeading || updated[1].Level != 2 {
		t.Fatalf("update_block: %+v", updated[1])
	}
	stored := objectFrom(t, api(t, app, "GET", "/v1/objects/"+objectID, nil))
	if stored.Content != blocksPlainText(updated) {
		t.Fatalf("update_block content mirror: %q vs %q", stored.Content, blocksPlainText(updated))
	}
	mcpExpectError(t, cs, "update_block", mcpArguments("object_id", objectID, "block_id", "missing", "block", map[string]any{"type": "paragraph", "text": "x"}), "Block not found")

	// delete_block removes a block; the version history proves the parity
	// with Capacities' "a new version is created automatically".
	var removed mcpMutateOutput
	mcpDecode(t, cs, "delete_block", mcpArguments("object_id", objectID, "block_id", blocks[0].ID), &removed)
	final, _ := objectBlocks(objectFrom(t, api(t, app, "GET", "/v1/objects/"+objectID, nil)))
	if len(final) != 2 || final[0].ID == blocks[0].ID {
		t.Fatalf("delete_block: %+v", final)
	}
	versions := api(t, app, "GET", "/v1/objects/"+objectID+"/versions", nil)
	if versions.Code != 200 || !strings.Contains(versions.Body.String(), "\"revision\"") {
		t.Fatalf("block edits left no version history: HTTP %d: %s", versions.Code, versions.Body.String())
	}

	// Deleting every block empties the body.
	for _, block := range final {
		mcpDecode(t, cs, "delete_block", mcpArguments("object_id", objectID, "block_id", block.ID), &removed)
	}
	emptied := objectFrom(t, api(t, app, "GET", "/v1/objects/"+objectID, nil))
	if _, has := emptied.Properties["blocks"]; has || emptied.Content != "" {
		t.Fatalf("deleting every block must empty the body: %+v", emptied)
	}
}

func TestMCPCreateTask(t *testing.T) {
	app, _, _ := makeApp(t)
	cs, _ := mcpConnect(t, app)

	var created mcpMutateOutput
	mcpDecode(t, cs, "create_task", mcpArguments(
		"title", "Finish MCP docs", "due_date", "2026-11-06", "priority", "high",
		"markdown", "- [ ] outline", "tags", []string{"work"},
	), &created)
	task := objectFrom(t, api(t, app, "GET", "/v1/objects/"+created.Object.ID, nil))
	if task.TypeID != "task" || task.Properties["status"] != "todo" {
		t.Fatalf("create_task defaults: %+v", task)
	}
	if task.Properties["due_date"] != "2026-11-06" || task.Properties["priority"] != "high" {
		t.Fatalf("create_task properties: %+v", task.Properties)
	}
	if !strings.Contains(task.Content, "outline") {
		t.Fatalf("create_task body: %q", task.Content)
	}
	completion := api(t, app, "POST", "/v1/tasks/"+task.ID+"/complete", nil)
	if completion.Code != 200 || !strings.Contains(completion.Body.String(), "\"created\":false") {
		t.Fatalf("completing an MCP task: HTTP %d: %s", completion.Code, completion.Body.String())
	}

	// A recurring task created through MCP spawns its next occurrence through
	// the recurring-tasks engine.
	mcpDecode(t, cs, "create_task", mcpArguments(
		"title", "Weekly review", "due_date", "2026-11-02",
		"recurrence", map[string]any{"mode": "weekly", "weekdays": []int{1, 5}},
	), &created)
	recurring := api(t, app, "POST", "/v1/tasks/"+created.Object.ID+"/complete", nil)
	if !strings.Contains(recurring.Body.String(), "\"created\":true") ||
		!strings.Contains(recurring.Body.String(), "\"next_date\":\"2026-11-06\"") {
		t.Fatalf("recurring MCP task did not spawn the next occurrence: %s", recurring.Body.String())
	}

	project := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Project X"}))
	mcpDecode(t, cs, "create_task", mcpArguments("title", "With context", "context_object_id", project.ID), &created)
	withContext := objectFrom(t, api(t, app, "GET", "/v1/objects/"+created.Object.ID, nil))
	if withContext.Properties["context_id"] != project.ID {
		t.Fatalf("create_task context: %+v", withContext.Properties)
	}

	mcpExpectError(t, cs, "create_task", mcpArguments("title", "Bad due", "due_date", "tomorrow"), "due_date must be YYYY-MM-DD")
	mcpExpectError(t, cs, "create_task", mcpArguments("title", "No due", "recurrence", map[string]any{"mode": "daily"}), "needs a due_date")
	mcpExpectError(t, cs, "create_task", mcpArguments("title", "Bad rule", "due_date", "2026-11-06", "recurrence", map[string]any{"mode": "hourly"}), "mode")
	mcpExpectError(t, cs, "create_task", mcpArguments("title", "Ghost", "context_object_id", "missing-id"), "existing object")
	mcpExpectError(t, cs, "create_task", mcpArguments(), "title")
	many := make([]string, 20)
	for i := range many {
		many[i] = "task-bulk-tag"
	}
	mcpExpectError(t, cs, "create_task", mcpArguments("title", "Budget", "tags", many), "at most 20 objects")
}

func TestMCPScopeEnforcement(t *testing.T) {
	app, _, _ := makeApp(t)
	seed := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Seed note", "content": "keep me"}))
	before := app.store.ObjectCount()

	// A read-only token can initialize and call read tools.
	if err := app.SetScopes("api:read"); err != nil {
		t.Fatal(err)
	}
	cs, _ := mcpConnect(t, app)
	var search mcpSearchOutput
	mcpDecode(t, cs, "search", mcpArguments("query", "seed"), &search)
	if len(search.Results) != 1 || search.Results[0].ID != seed.ID {
		t.Fatalf("read tool with api:read: %+v", search.Results)
	}
	var read mcpReadObjectOutput
	mcpDecode(t, cs, "read_object", mcpArguments("object_id", seed.ID), &read)
	if read.Object.ID != seed.ID {
		t.Fatal("read_object with api:read failed")
	}

	// Every write tool rejects the call in-band with the scope message.
	mcpExpectError(t, cs, "create_object", mcpArguments("type_id", "note", "title", "Nope"), "api:read")
	mcpExpectError(t, cs, "append_md_to_object", mcpArguments("object_id", seed.ID, "markdown", "nope"), "api:read")
	mcpExpectError(t, cs, "append_to_daily_note", mcpArguments("markdown", "nope"), "api:read")
	mcpExpectError(t, cs, "add_block", mcpArguments("object_id", seed.ID, "block", map[string]any{"type": "paragraph", "text": "no"}), "api:read")
	mcpExpectError(t, cs, "update_block", mcpArguments("object_id", seed.ID, "block_id", "x", "block", map[string]any{"type": "paragraph", "text": "no"}), "api:read")
	mcpExpectError(t, cs, "delete_block", mcpArguments("object_id", seed.ID, "block_id", "x"), "api:read")
	mcpExpectError(t, cs, "create_task", mcpArguments("title", "Nope"), "api:read")
	if count := app.store.ObjectCount(); count != before {
		t.Fatalf("a read-only token changed the store: %d -> %d", before, count)
	}

	// An api:write token gets the full surface.
	if err := app.SetScopes("api:write"); err != nil {
		t.Fatal(err)
	}
	var created mcpMutateOutput
	mcpDecode(t, cs, "create_object", mcpArguments("type_id", "note", "title", "Written with scoped token"), &created)
	if created.Object.ID == "" {
		t.Fatal("api:write token could not create through MCP")
	}
}
