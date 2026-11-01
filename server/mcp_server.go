package main

// MCP over HTTP for external AI agents (backend parity stage mcp-server).
//
// Capacities exposes an MCP server (docs.capacities.io/developer/
// model-context-protocol) so AI tools can search, read and edit a user's
// space. This stage gives Lumen the same capability on the official Go SDK
// (github.com/modelcontextprotocol/go-sdk, MIT): a streamable HTTP endpoint
// mounted at /mcp on the existing mux, behind the existing bearer token, with
// thin tools over the internal functions of the earlier parity stages.
//
// Design rules:
//
//   - No business logic lives here. Every tool delegates to the function the
//     REST endpoint uses (searchObjects' matcher, the api-v2 Markdown cores,
//     the collections helper, the store), so MCP and REST cannot drift.
//   - Auth deviates from Capacities' OAuth 2.1 + PKCE on purpose: Capacities
//     is a multi-tenant SaaS mediating many users, apps and spaces, while
//     Lumen is single-user and self-hosted with one deployment token. The
//     endpoint reuses the bearer token middleware; scopes apply per tool
//     (api:read for read tools, api:write for mutating tools, an unscoped
//     token keeps full access), see scopes.go for the /mcp exemption.
//   - The sync endpoints, token management and trash purge-all are never
//     exposed as tools. Read tools exclude trashed objects and templates,
//     exactly like the REST API.
//   - A single tool call may create at most mcpMaxObjectsPerCall objects (the
//     main object plus get-or-created tag objects), a simplified version of
//     Capacities' per-user creation quota.

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/modelcontextprotocol/go-sdk/mcp"
)

// mcpMaxObjectsPerCall caps how many objects one tool call may create (the
// object itself plus any tag objects resolved get-or-create). Capacities
// enforces a per-user quota; for a single-user self-hosted server a per-call
// cap keeps a runaway agent loop bounded without extra state.
const mcpMaxObjectsPerCall = 20

// mcpEndpointPath is where the streamable handler is mounted. The discovery
// document (GET /v1/api) lists it in apiCatalog.
const mcpEndpointPath = "/mcp"

// mcpHandler lazily builds the streamable HTTP handler: one MCP server per
// App, shared by every session, exactly like the SDK examples' getServer
// callback allows.
func (a *App) mcpHTTPHandler() http.Handler {
	a.mcpOnce.Do(func() { a.mcpHandler = newMCPHandler(a) })
	return a.mcpHandler
}

// newMCPHandler builds the MCP server and wraps it in the streamable HTTP
// transport. Sessions, notifications and the standalone SSE stream follow
// the SDK defaults (stateful sessions with Mcp-Session-Id). The body limit
// matches the 8 MB the Markdown REST endpoints accept.
func newMCPHandler(a *App) http.Handler {
	server := mcp.NewServer(&mcp.Implementation{
		Name:        "lumen",
		Title:       "Lumen",
		Version:     apiVersion,
		Description: "Lumen personal knowledge server: typed objects, daily notes and tags",
	}, nil)
	a.registerMCPTools(server)
	return mcp.NewStreamableHTTPHandler(func(*http.Request) *mcp.Server { return server }, &mcp.StreamableHTTPOptions{
		MaxRequestBodyBytes: 8 << 20,
	})
}

// requireMCPWriteScope is the per-tool scope check for mutating tools. Read
// tools answer for any valid token (api:read is the least configured scope).
func (a *App) requireMCPWriteScope() error {
	if a.scopes.fullAccess() || a.scopes.write {
		return nil
	}
	return errReadOnlyToken
}

// requireMCPCreationBudget refuses a call whose inputs could create more
// objects than mcpMaxObjectsPerCall: the main object plus every tag name that
// might not exist yet. It over-approximates (existing tags are not created),
// which keeps the rule simple and predictable.
func requireMCPCreationBudget(tagCount int) error {
	if 1+tagCount > mcpMaxObjectsPerCall {
		return fmt.Errorf("A single call may create at most %d objects; the object plus %d tag names exceeds that. Send fewer tags per call", mcpMaxObjectsPerCall, tagCount)
	}
	return nil
}

// mcpCheckType verifies a type id before a tool reads or writes it, so a typo
// reports a clear error instead of an empty result.
func (a *App) mcpCheckType(typeID string) error {
	if _, err := a.store.Type(typeID); err != nil {
		return errors.New("Unknown object type " + typeID + "; list them with get_type_shape or GET /v1/types")
	}
	return nil
}

// mcpLoadObject reads a live object. Trashed objects are not live cards: the
// payload moved to the trash record, so they answer "not found" here too,
// exactly like GET /v1/objects/{id}.
func (a *App) mcpLoadObject(id string) (Object, error) {
	var o Object
	if err := a.store.Get("object", id, &o); err != nil {
		return Object{}, errNotFoundHTTP("Object not found")
	}
	return o, nil
}

// ---------------------------------------------------------------------------
// Tool registration
// ---------------------------------------------------------------------------

// registerMCPTools wires the tool catalog. Every handler is a thin wrapper:
// the table in docs/implementation/backend/mcp-server.md maps each tool to
// the internal function it delegates to and the scope it requires.
func (a *App) registerMCPTools(server *mcp.Server) {
	readOnly := &mcp.ToolAnnotations{ReadOnlyHint: true}
	write := func(destructive bool) *mcp.ToolAnnotations {
		return &mcp.ToolAnnotations{ReadOnlyHint: false, DestructiveHint: &destructive}
	}

	// Read tools (api:read).
	mcp.AddTool(server, &mcp.Tool{
		Name:        "search",
		Title:       "Search objects",
		Annotations: readOnly,
		Description: "Search the workspace by keyword. Case-insensitive substring over titles, body text, weblink reader articles and aliases. Returns id, type, title, an optional matched alias and a text snippet; use read_object for the full payload. Trashed objects and templates are excluded.",
	}, a.mcpSearch)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "read_object",
		Title:       "Read an object",
		Annotations: readOnly,
		Description: "Read one object in full: type, title, properties (including aliases and weblink reader_content), links and body, the same assembly as GET /v1/objects/{id}. Also returns the body as Markdown when the object uses blocks, otherwise as plain text. Trashed objects are not readable.",
	}, a.mcpReadObject)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "read_object_blocks",
		Title:       "Read an object's blocks",
		Annotations: readOnly,
		Description: "Read the block array of an object's body (the editor's structured representation, per the block schema) plus the same content rendered as Markdown. Use this when add_block, update_block or delete_block need exact block ids.",
	}, a.mcpReadObjectBlocks)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "list_by_type",
		Title:       "List objects by type",
		Annotations: readOnly,
		Description: "List the objects of one type, most recently updated first, with limit/cursor pagination (the cursor is the same keyset cursor the REST API returns in X-Lumen-Next-Cursor). Trashed objects and templates are excluded.",
	}, a.mcpListByType)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "list_by_tag",
		Title:       "List objects by tag",
		Annotations: readOnly,
		Description: "List every object tagged with a tag name (tags are objects of type tag; resolve the names with list_by_type type_id=tag), most recently updated first. Trashed objects and templates are excluded.",
	}, a.mcpListByTag)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "list_by_collection",
		Title:       "List a collection's objects",
		Annotations: readOnly,
		Description: "List the objects of a collection in its stored, curated order. The id is an object of type collection; ids that no longer resolve or are trashed are skipped, like the REST collections endpoint.",
	}, a.mcpListByCollection)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "get_type_shape",
		Title:       "Describe an object type",
		Annotations: readOnly,
		Description: "Return one object type's property definitions (key, name, type, options, target type, multiple flag) so new objects can be filled correctly. Call this before create_object or create_task.",
	}, a.mcpTypeShape)

	// Write tools (api:write).
	mcp.AddTool(server, &mcp.Tool{
		Name:        "create_object",
		Title:       "Create an object",
		Annotations: write(false),
		Description: "Create an object of any type with its body from Markdown (headings, lists, tasks, quotes, code, tables, [[wikilinks]]). Properties are validated against the type shape (call get_type_shape first); blocks, rich_text and tags inside properties are managed by the server and rejected. tags is a list of tag names resolved get-or-create. Collections and templates cannot be created this way. The call creates at most 20 objects (the object plus new tags).",
	}, a.mcpCreateObject)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "append_md_to_object",
		Title:       "Append Markdown to an object",
		Annotations: write(false),
		Description: "Append Markdown to an object's body. An object that uses blocks gets the new blocks appended or inserted at position (0-based); an object without any text gains the block representation; a plain-text object stays plain-text (position is ignored). The write bumps the revision, snapshots the previous version and reaches mobile sync.",
	}, a.mcpAppendMarkdown)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "append_to_daily_note",
		Title:       "Append to the daily note",
		Annotations: write(false),
		Description: "Append Markdown to the daily note of a date, creating the note when it does not exist yet. The date defaults to today.",
	}, a.mcpAppendDailyNote)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "add_block",
		Title:       "Add a block",
		Annotations: write(false),
		Description: "Add one editor block to an object's body at position (0-based; default: append). Prefer append_md_to_object for content; use this only when a single structured block must be placed precisely. A plain-text object rejects blocks (use append_md_to_object); an object without text gains the block representation. The block id is assigned by the server.",
	}, a.mcpAddBlock)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "update_block",
		Title:       "Replace a block",
		Annotations: write(false),
		Description: "Replace one block by id with a new block. The stored block id is kept stable. The previous object state is snapshotted in the server-side version history, like every object write.",
	}, a.mcpUpdateBlock)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "delete_block",
		Title:       "Delete a block",
		Annotations: write(true),
		Description: "Remove one block by id from an object's body. Deleting the last block empties the body. The previous object state is snapshotted in the server-side version history, like every object write.",
	}, a.mcpDeleteBlock)
	mcp.AddTool(server, &mcp.Tool{
		Name:        "create_task",
		Title:       "Create a task",
		Annotations: write(false),
		Description: "Create a task with a title and an optional Markdown body. due_date is YYYY-MM-DD, priority is free text, context_object_id must reference an existing object, recurrence repeats the task when completed, tags are names resolved get-or-create. The status starts at todo; a recurring task requires a due date. The call creates at most 20 objects (the task plus new tags).",
	}, a.mcpCreateTask)
}

// ---------------------------------------------------------------------------
// Shared input/output shapes
// ---------------------------------------------------------------------------

// mcpObjectListOutput is the common listing answer: full objects in display
// order plus the continuation cursor when a page is not the last one.
type mcpObjectListOutput struct {
	Objects    []Object `json:"objects"`
	NextCursor string   `json:"next_cursor,omitempty"`
}

// mcpMutateOutput returns the updated object after a write tool.
type mcpMutateOutput struct {
	Object Object `json:"object"`
}

// ---------------------------------------------------------------------------
// Read tools
// ---------------------------------------------------------------------------

type mcpSearchInput struct {
	Query  string `json:"query" jsonschema:"Keywords to find. Case-insensitive substring over titles, body text, weblink reader articles and aliases."`
	TypeID string `json:"type_id,omitempty" jsonschema:"Restrict the search to one object type id (e.g. book, note). Omit to search every type."`
	Limit  int    `json:"limit,omitempty" jsonschema:"Maximum number of results, 1 to 200. Defaults to 50."`
}

type mcpSearchHit struct {
	ID           string `json:"id"`
	TypeID       string `json:"type_id"`
	Title        string `json:"title"`
	UpdatedAt    string `json:"updated_at"`
	MatchedAlias string `json:"matched_alias,omitempty"`
	Snippet      string `json:"snippet,omitempty"`
}

type mcpSearchOutput struct {
	Results []mcpSearchHit `json:"results"`
}

// mcpSnippet is a bounded plain-text preview of an object, so search answers
// stay small enough for a model context.
const mcpSnippetLength = 280

func mcpSnippet(text string) string {
	text = strings.TrimSpace(text)
	if len([]rune(text)) <= mcpSnippetLength {
		return text
	}
	runes := []rune(text)
	return strings.TrimSpace(string(runes[:mcpSnippetLength])) + "…"
}

// mcpSearch is the Capacities `search` tool over the GET /v1/search matcher
// (matchesSearchQuery): same fields, same ranking (display order), minus
// pagination (agents narrow with type_id or read specific objects).
func (a *App) mcpSearch(_ context.Context, _ *mcp.CallToolRequest, in mcpSearchInput) (*mcp.CallToolResult, mcpSearchOutput, error) {
	query := strings.ToLower(strings.TrimSpace(in.Query))
	if query == "" {
		return nil, mcpSearchOutput{}, errors.New("Provide a search query")
	}
	if in.TypeID != "" {
		if err := a.mcpCheckType(in.TypeID); err != nil {
			return nil, mcpSearchOutput{}, err
		}
	}
	limit := in.Limit
	if limit == 0 {
		limit = defaultPageLimit
	}
	if limit < 1 || limit > maxPageLimit {
		return nil, mcpSearchOutput{}, errors.New("limit must be between 1 and " + strconv.Itoa(maxPageLimit))
	}
	var objects []Object
	var err error
	if in.TypeID != "" {
		objects, err = a.store.ObjectsByType(in.TypeID)
	} else {
		objects, err = a.store.Objects()
	}
	if err != nil {
		return nil, mcpSearchOutput{}, errors.New("Could not read objects")
	}
	out := mcpSearchOutput{Results: []mcpSearchHit{}}
	for _, o := range objects {
		if isTemplateObject(o) {
			continue
		}
		if matched, alias := matchesSearchQuery(o, query); matched {
			out.Results = append(out.Results, mcpSearchHit{
				ID:           o.ID,
				TypeID:       o.TypeID,
				Title:        o.Title,
				UpdatedAt:    o.UpdatedAt,
				MatchedAlias: alias,
				Snippet:      mcpSnippet(o.Content),
			})
			if len(out.Results) >= limit {
				break
			}
		}
	}
	return nil, out, nil
}

type mcpObjectIDInput struct {
	ObjectID string `json:"object_id" jsonschema:"Id of the object, as returned by search or the list tools."`
}

type mcpReadObjectOutput struct {
	Object   Object `json:"object"`
	Markdown string `json:"markdown,omitempty"`
}

// mcpReadBlocksOutput is returned as `any`: Block is a recursive type
// (columns blocks contain blocks) and the SDK's JSON-schema inference
// rejects cycles, so the tool ships no output schema. The JSON shape is the
// same as the struct definition here.
type mcpReadBlocksOutput struct {
	ObjectID string  `json:"object_id"`
	Blocks   []Block `json:"blocks"`
	Markdown string  `json:"markdown"`
}

// mcpReadObject is the Capacities `getObjectContent` tool: the same assembly
// as GET /v1/objects/{id} (properties, aliases, reader_content included),
// plus a Markdown rendering of the body for convenience.
func (a *App) mcpReadObject(_ context.Context, _ *mcp.CallToolRequest, in mcpObjectIDInput) (*mcp.CallToolResult, mcpReadObjectOutput, error) {
	o, err := a.mcpLoadObject(in.ObjectID)
	if err != nil {
		return nil, mcpReadObjectOutput{}, err
	}
	out := mcpReadObjectOutput{Object: o}
	if blocks, blockErr := objectBlocks(o); blockErr == nil && len(blocks) > 0 {
		out.Markdown = BlocksToMarkdown(blocks)
	} else {
		out.Markdown = o.Content
	}
	return nil, out, nil
}

// mcpReadObjectBlocks is the Capacities `readObjectBlocks` tool: the stored
// block array plus its Markdown rendering, so an agent can pick block ids
// for the block tools.
func (a *App) mcpReadObjectBlocks(_ context.Context, _ *mcp.CallToolRequest, in mcpObjectIDInput) (*mcp.CallToolResult, any, error) {
	o, err := a.mcpLoadObject(in.ObjectID)
	if err != nil {
		return nil, nil, err
	}
	blocks, err := objectBlocks(o)
	if err != nil {
		return nil, nil, errPersistHTTP("Could not read the stored blocks")
	}
	markdown := BlocksToMarkdown(blocks)
	if strings.TrimSpace(markdown) == "" {
		markdown = o.Content
	}
	return nil, mcpReadBlocksOutput{ObjectID: o.ID, Blocks: blocks, Markdown: markdown}, nil
}

type mcpListByTypeInput struct {
	TypeID string `json:"type_id" jsonschema:"The object type id to list, e.g. book or note."`
	Limit  int    `json:"limit,omitempty" jsonschema:"Page size, 1 to 200. Defaults to 50."`
	Cursor string `json:"cursor,omitempty" jsonschema:"Continuation cursor from a previous answer's next_cursor; omit for the first page."`
}

// mcpListByType is the Capacities `listObjectsByObjectType` tool over
// store.ObjectsByType plus the shared keyset pagination, with the REST API's
// template exclusion.
func (a *App) mcpListByType(_ context.Context, _ *mcp.CallToolRequest, in mcpListByTypeInput) (*mcp.CallToolResult, mcpObjectListOutput, error) {
	if err := a.mcpCheckType(in.TypeID); err != nil {
		return nil, mcpObjectListOutput{}, err
	}
	limit := in.Limit
	if limit == 0 {
		limit = defaultPageLimit
	}
	if limit < 1 || limit > maxPageLimit {
		return nil, mcpObjectListOutput{}, errors.New("limit must be between 1 and " + strconv.Itoa(maxPageLimit))
	}
	objects, err := a.store.ObjectsByType(in.TypeID)
	if err != nil {
		return nil, mcpObjectListOutput{}, errors.New("Could not read objects")
	}
	ordered := make([]Object, 0, len(objects))
	for _, o := range objects {
		if isTemplateObject(o) {
			continue
		}
		ordered = append(ordered, o)
	}
	start, end, nextCursor, err := pageWindowParams(strconv.Itoa(limit), in.Cursor, ordered)
	if err != nil {
		return nil, mcpObjectListOutput{}, err
	}
	return nil, mcpObjectListOutput{Objects: ordered[start:end], NextCursor: nextCursor}, nil
}

type mcpListByTagInput struct {
	TagName string `json:"tag_name" jsonschema:"The exact title of a tag object (list them with list_by_type type_id=tag)."`
}

// mcpListByTag is the Capacities `listObjectsByTag` tool. The name was chosen
// over the id because agents and users refer to tags by name; the server
// resolves it through the same unique-title index the REST API uses.
func (a *App) mcpListByTag(_ context.Context, _ *mcp.CallToolRequest, in mcpListByTagInput) (*mcp.CallToolResult, mcpObjectListOutput, error) {
	name := strings.TrimSpace(in.TagName)
	if name == "" {
		return nil, mcpObjectListOutput{}, errors.New("Provide a tag name")
	}
	owner := a.store.TitleOwner(name)
	if owner != "" {
		var tag Object
		if err := a.store.Get("object", owner, &tag); err == nil && tag.TypeID == "tag" && !isTemplateObject(tag) {
			return nil, a.objectsTaggedWith(owner), nil
		}
		return nil, mcpObjectListOutput{}, errors.New("An object with the title \"" + name + "\" exists but is not a tag; list the tags with list_by_type type_id=tag")
	}
	return nil, mcpObjectListOutput{}, errors.New("No tag named \"" + name + "\" exists; list the tags with list_by_type type_id=tag")
}

// objectsTaggedWith scans the live objects in display order for one tag id.
func (a *App) objectsTaggedWith(tagID string) mcpObjectListOutput {
	out := mcpObjectListOutput{Objects: []Object{}}
	objects, err := a.store.Objects()
	if err != nil {
		return out
	}
	for _, o := range objects {
		if isTemplateObject(o) {
			continue
		}
		for _, id := range stringList(o.Properties["tags"]) {
			if id == tagID {
				out.Objects = append(out.Objects, o)
				break
			}
		}
	}
	return out
}

type mcpCollectionIDInput struct {
	CollectionID string `json:"collection_id" jsonschema:"Id of an object of type collection."`
}

// mcpListByCollection is the Capacities `listObjectsByCollection` tool over
// collectionItemIDs (the same validation as the `collection` query filter)
// and the same resolution rules as GET /v1/collections/{id}/items: stored
// order, ids that no longer resolve or are trashed are skipped.
func (a *App) mcpListByCollection(_ context.Context, _ *mcp.CallToolRequest, in mcpCollectionIDInput) (*mcp.CallToolResult, mcpObjectListOutput, error) {
	ids, err := a.collectionItemIDs(in.CollectionID)
	if err != nil {
		return nil, mcpObjectListOutput{}, err
	}
	out := mcpObjectListOutput{Objects: []Object{}}
	for _, id := range ids {
		item, err := a.store.Object(id)
		if err != nil || a.isDeleted(id) {
			continue
		}
		out.Objects = append(out.Objects, item)
	}
	return nil, out, nil
}

type mcpTypeIDInput struct {
	TypeID string `json:"type_id" jsonschema:"The object type id to describe."`
}

// mcpTypeShape is the Capacities `getObjectTypeShape` tool: the type registry
// entry (GET /v1/types) so an agent can fill properties correctly.
func (a *App) mcpTypeShape(_ context.Context, _ *mcp.CallToolRequest, in mcpTypeIDInput) (*mcp.CallToolResult, ObjectType, error) {
	if err := a.mcpCheckType(in.TypeID); err != nil {
		return nil, ObjectType{}, err
	}
	t, err := a.store.Type(in.TypeID)
	if err != nil {
		return nil, ObjectType{}, err
	}
	return nil, t, nil
}

// ---------------------------------------------------------------------------
// Write tools
// ---------------------------------------------------------------------------

type mcpCreateObjectInput struct {
	TypeID     string         `json:"type_id" jsonschema:"The object type id (call get_type_shape to see its properties)."`
	Title      string         `json:"title,omitempty" jsonschema:"Object title. Titles of normal objects are unique in the workspace."`
	Markdown   string         `json:"markdown,omitempty" jsonschema:"The object body as Markdown: headings, lists, task checkboxes, quotes, code fences, tables, [[wikilinks]]."`
	Tags       []string       `json:"tags,omitempty" jsonschema:"Tag names to attach, resolved get-or-create (at most 19 so the call stays within the 20-object creation limit)."`
	Properties map[string]any `json:"properties,omitempty" jsonschema:"Typed properties per the type shape. blocks, rich_text and tags are managed by the server and rejected here."`
}

// mcpCreateObject is the Capacities `createObjectViaMd`/`createPage` tool: the
// exact createObjectFromMarkdownCore that POST /v1/objects/markdown uses
// (same validation, same tag resolution, same transactional save).
func (a *App) mcpCreateObject(ctx context.Context, _ *mcp.CallToolRequest, in mcpCreateObjectInput) (*mcp.CallToolResult, mcpMutateOutput, error) {
	if err := a.requireMCPWriteScope(); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	if err := requireMCPCreationBudget(len(in.Tags)); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	markdown := in.Markdown
	o, err := a.createObjectFromMarkdownCore(ctx, objectFromMarkdownRequest{
		TypeID:     in.TypeID,
		Title:      in.Title,
		Markdown:   &markdown,
		Tags:       in.Tags,
		Properties: in.Properties,
	})
	if err != nil {
		return nil, mcpMutateOutput{}, err
	}
	return nil, mcpMutateOutput{Object: o}, nil
}

type mcpAppendMarkdownInput struct {
	ObjectID string `json:"object_id" jsonschema:"Id of the object to append to."`
	Markdown string `json:"markdown" jsonschema:"The Markdown to append to the object's body."`
	Position *int   `json:"position,omitempty" jsonschema:"0-based insertion index among the object's blocks; omit or send the block count to append at the end. Ignored for plain-text objects."`
}

// mcpAppendMarkdown is the Capacities `appendMdToObject` tool over
// appendObjectMarkdownCore (POST /v1/objects/{id}/append-markdown).
func (a *App) mcpAppendMarkdown(ctx context.Context, _ *mcp.CallToolRequest, in mcpAppendMarkdownInput) (*mcp.CallToolResult, mcpMutateOutput, error) {
	if err := a.requireMCPWriteScope(); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	markdown := in.Markdown
	o, err := a.appendObjectMarkdownCore(ctx, in.ObjectID, appendMarkdownRequest{Markdown: &markdown, Position: in.Position})
	if err != nil {
		return nil, mcpMutateOutput{}, err
	}
	return nil, mcpMutateOutput{Object: o}, nil
}

type mcpDailyNoteInput struct {
	Markdown string `json:"markdown" jsonschema:"The Markdown to append to the daily note."`
	Date     string `json:"date,omitempty" jsonschema:"The journal date, YYYY-MM-DD. Defaults to today."`
}

// mcpAppendDailyNote is the Capacities `saveToDailyNote` tool over
// appendJournalMarkdownCore (POST /v1/journal/{date}/append), with the date
// defaulting to today.
func (a *App) mcpAppendDailyNote(ctx context.Context, _ *mcp.CallToolRequest, in mcpDailyNoteInput) (*mcp.CallToolResult, mcpMutateOutput, error) {
	if err := a.requireMCPWriteScope(); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	date := strings.TrimSpace(in.Date)
	if date == "" {
		date = todayJournalDate()
	}
	o, err := a.appendJournalMarkdownCore(ctx, date, in.Markdown, "")
	if err != nil {
		return nil, mcpMutateOutput{}, err
	}
	return nil, mcpMutateOutput{Object: o}, nil
}

// mcpLeafBlockInput is mcpBlockInput without the columns field: the columns
// of a columns block accept non-container blocks, which keeps the tool input
// schema acyclic (the SDK's JSON-schema inference rejects recursive types).
// Deeper columns layouts remain available through the Markdown converter
// (::: columns in create_object or append_md_to_object), just not through
// the block tools.
type mcpLeafBlockInput struct {
	Type     string        `json:"type" jsonschema:"Block type: paragraph, small, heading, bullet_list, numbered_list, alphabetic_list, quote, todo, toggle, group, code, math, divider, embed or table."`
	Text     string        `json:"text,omitempty" jsonschema:"Visible text of the block; when runs are omitted, the whole text becomes one run."`
	Level    int           `json:"level,omitempty" jsonschema:"Heading level 1-4 (heading blocks only)."`
	Indent   int           `json:"indent,omitempty" jsonschema:"List nesting 1-8, 1 = top level (list and todo blocks only)."`
	Language string        `json:"language,omitempty" jsonschema:"Code fence language (code blocks only)."`
	Runs     []Run         `json:"runs,omitempty" jsonschema:"Inline spans; text is recomputed as the concatenation of the run texts."`
	Align    []string      `json:"align,omitempty" jsonschema:"Table column alignment: left, center, right or empty (table blocks only)."`
	Rows     [][]BlockCell `json:"rows,omitempty" jsonschema:"Table cells; the first row is the header (table blocks only)."`
}

func (b mcpLeafBlockInput) toBlock() (Block, error) {
	return (mcpBlockInput{
		Type:     b.Type,
		Text:     b.Text,
		Level:    b.Level,
		Indent:   b.Indent,
		Language: b.Language,
		Runs:     b.Runs,
		Align:    b.Align,
		Rows:     b.Rows,
	}).toBlock()
}

type mcpBlockInput struct {
	Type     string                `json:"type" jsonschema:"Block type: paragraph, small, heading, bullet_list, numbered_list, alphabetic_list, quote, todo, toggle, group, code, math, divider, embed, table or columns."`
	Text     string                `json:"text,omitempty" jsonschema:"Visible text of the block; when runs are omitted, the whole text becomes one run."`
	Level    int                   `json:"level,omitempty" jsonschema:"Heading level 1-4 (heading blocks only)."`
	Indent   int                   `json:"indent,omitempty" jsonschema:"List nesting 1-8, 1 = top level (list and todo blocks only)."`
	Language string                `json:"language,omitempty" jsonschema:"Code fence language (code blocks only)."`
	Runs     []Run                 `json:"runs,omitempty" jsonschema:"Inline spans; text is recomputed as the concatenation of the run texts."`
	Align    []string              `json:"align,omitempty" jsonschema:"Table column alignment: left, center, right or empty (table blocks only)."`
	Rows     [][]BlockCell         `json:"rows,omitempty" jsonschema:"Table cells; the first row is the header (table blocks only)."`
	Columns  [][]mcpLeafBlockInput `json:"columns,omitempty" jsonschema:"The columns of a two-column layout, each a list of non-container blocks (columns blocks only)."`
}

// toBlock validates the agent-supplied block against the block schema of
// contracts/SYNC.md and normalizes it to the editor's stored shape:
//
//   - the id is always assigned by the server;
//   - text is the concatenation of the run texts (the stored invariant), with
//     runs synthesized from text when the agent sent none;
//   - todo blocks carry the ○/✔ marker and toggle blocks the ▶/▼ marker, so
//     the editor keeps the block type on reload (same glyphs as the
//     Markdown converter);
//   - headings are clamped to levels 1-4 and list nesting to 1-8;
//   - dividers are canonicalized to the placeholder shape, tables need rows
//     and columns need at least one column.
func (b mcpBlockInput) toBlock() (Block, error) {
	block := Block{
		Type:     strings.TrimSpace(b.Type),
		Text:     b.Text,
		Level:    b.Level,
		Indent:   b.Indent,
		Language: b.Language,
		Align:    b.Align,
		Rows:     b.Rows,
	}
	switch block.Type {
	case "":
		return Block{}, errors.New("A block needs a type")
	case blockParagraph, blockSmall, blockHeading, blockBullet, blockNumbered, blockAlphabetic,
		blockQuote, blockTodo, blockToggle, blockGroup, blockCode, blockMath, blockEmbed:
	case blockDivider:
		return newDividerBlock(), nil
	case blockTable:
		if len(b.Rows) == 0 {
			return Block{}, errors.New("A table block needs rows (the first row is the header)")
		}
		if len(b.Align) > 0 && len(b.Align) != len(b.Rows[0]) {
			return Block{}, errors.New("A table block's align needs one entry per column")
		}
		return Block{ID: newID(), Type: blockTable, Runs: []Run{}, Align: b.Align, Rows: b.Rows}, nil
	case blockColumns:
		if len(b.Columns) == 0 {
			return Block{}, errors.New("A columns block needs at least one column of blocks")
		}
		columns := make([][]Block, 0, len(b.Columns))
		for i, column := range b.Columns {
			blocks := make([]Block, 0, len(column))
			for j, entry := range column {
				nested, err := entry.toBlock()
				if err != nil {
					return Block{}, fmt.Errorf("column %d, block %d: %w", i+1, j+1, err)
				}
				blocks = append(blocks, nested)
			}
			columns = append(columns, blocks)
		}
		return Block{ID: newID(), Type: blockColumns, Runs: []Run{}, Columns: columns}, nil
	default:
		return Block{}, errors.New("Unknown block type \"" + block.Type + "\"")
	}
	runs := b.Runs
	switch block.Type {
	case blockMath:
		if len(runs) == 0 {
			latex := strings.TrimSpace(b.Text)
			if latex == "" {
				return Block{}, errors.New("A math block needs LaTeX in text or a math run")
			}
			return newMathBlock(latex), nil
		}
	case blockCode:
		if len(runs) == 0 {
			if strings.TrimSpace(b.Text) == "" {
				return Block{}, errors.New("A code block needs text or runs")
			}
			body := strings.ReplaceAll(b.Text, "\n", lineSeparator)
			return Block{ID: newID(), Type: blockCode, Text: body, Language: b.Language, Runs: []Run{{Text: body, Code: true}}}, nil
		}
	}
	if len(runs) == 0 {
		if strings.TrimSpace(b.Text) == "" {
			return Block{}, errors.New("A block needs text or runs")
		}
		runs = []Run{{Text: b.Text}}
	}
	// The stored invariant: text equals the concatenation of the runs.
	block.Text = runsText(runs)
	switch block.Type {
	case blockTodo:
		if !strings.HasPrefix(runs[0].Text, todoDone) && !strings.HasPrefix(runs[0].Text, todoOpen) {
			runs = append([]Run{{Text: todoOpen}}, runs...)
			block.Text = runsText(runs)
		}
	case blockToggle:
		if !strings.HasPrefix(runs[0].Text, toggleClosed) && !strings.HasPrefix(runs[0].Text, toggleOpen) {
			runs = append([]Run{{Text: toggleOpen}}, runs...)
			block.Text = runsText(runs)
		}
	case blockHeading:
		if block.Level < 1 {
			block.Level = 1
		}
		if block.Level > 4 {
			block.Level = 4
		}
	}
	if isListFamily(block.Type) {
		if block.Indent < 1 {
			block.Indent = 1
		}
		if block.Indent > 8 {
			block.Indent = 8
		}
	}
	block.Runs = runs
	block.ID = newID()
	return block, nil
}

type mcpAddBlockInput struct {
	ObjectID string        `json:"object_id" jsonschema:"Id of the object to add the block to."`
	Block    mcpBlockInput `json:"block" jsonschema:"The block to add, per the block schema."`
	Position *int          `json:"position,omitempty" jsonschema:"0-based index among the object's blocks; omit to append at the end."`
}

// mcpAddBlock is the Capacities `addBlock` tool. It follows the convert-merge
// representation rules: an object with blocks gets the block inserted, an
// object without any text gains the block representation, and a plain-text
// object refuses blocks (a block array is authoritative in the editor and
// would hide the plain body).
func (a *App) mcpAddBlock(ctx context.Context, _ *mcp.CallToolRequest, in mcpAddBlockInput) (*mcp.CallToolResult, mcpMutateOutput, error) {
	if err := a.requireMCPWriteScope(); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	block, err := in.Block.toBlock()
	if err != nil {
		return nil, mcpMutateOutput{}, err
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, err := a.mcpLoadObject(in.ObjectID)
	if err != nil {
		return nil, mcpMutateOutput{}, err
	}
	blocks, err := objectBlocks(o)
	if err != nil {
		return nil, mcpMutateOutput{}, errPersistHTTP("Could not read the stored blocks")
	}
	mirrored := contentMirrorsBlocks(o, blocks)
	switch {
	case len(blocks) > 0:
		insert := len(blocks)
		if in.Position != nil {
			if *in.Position < 0 || *in.Position > len(blocks) {
				return nil, mcpMutateOutput{}, errors.New("position must be between 0 and the number of blocks")
			}
			insert = *in.Position
		}
		merged := make([]Block, 0, len(blocks)+1)
		merged = append(merged, blocks[:insert]...)
		merged = append(merged, block)
		merged = append(merged, blocks[insert:]...)
		blocks = merged
		if mirrored {
			o.Content = blocksPlainText(blocks)
		} else if insert == len(blocks)-1 {
			// Appending at the end extends the canonical plain body without
			// recomputing it, like appendMarkdownBlocks does.
			if text := blockPlainText(block); text != "" {
				appendPlainText(&o, text)
			}
		}
	case !objectHasText(o):
		if in.Position != nil && *in.Position != 0 {
			return nil, mcpMutateOutput{}, errors.New("position must be 0 for an object without blocks")
		}
		blocks = []Block{block}
		o.Content = blocksPlainText(blocks)
	default:
		return nil, mcpMutateOutput{}, errors.New("This object renders from plain text; a block array would hide its body. Use append_md_to_object instead")
	}
	if err := a.saveMCPBlocks(ctx, &o, blocks); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	return nil, mcpMutateOutput{Object: o}, nil
}

type mcpUpdateBlockInput struct {
	ObjectID string        `json:"object_id" jsonschema:"Id of the object that holds the block."`
	BlockID  string        `json:"block_id" jsonschema:"Id of the stored block to replace."`
	Block    mcpBlockInput `json:"block" jsonschema:"The replacement block, per the block schema. The stored block id is kept."`
}

// mcpUpdateBlock is the Capacities `updateBlock` tool: it replaces one block
// by id, keeping the stored id stable so other references survive.
func (a *App) mcpUpdateBlock(ctx context.Context, _ *mcp.CallToolRequest, in mcpUpdateBlockInput) (*mcp.CallToolResult, mcpMutateOutput, error) {
	if err := a.requireMCPWriteScope(); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	block, err := in.Block.toBlock()
	if err != nil {
		return nil, mcpMutateOutput{}, err
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, err := a.mcpLoadObject(in.ObjectID)
	if err != nil {
		return nil, mcpMutateOutput{}, err
	}
	blocks, err := objectBlocks(o)
	if err != nil {
		return nil, mcpMutateOutput{}, errPersistHTTP("Could not read the stored blocks")
	}
	if len(blocks) == 0 {
		return nil, mcpMutateOutput{}, errors.New("This object has no block representation; use append_md_to_object or add_block instead")
	}
	index := -1
	for i, candidate := range blocks {
		if candidate.ID == in.BlockID {
			index = i
			break
		}
	}
	if index < 0 {
		return nil, mcpMutateOutput{}, errors.New("Block not found in this object")
	}
	mirrored := contentMirrorsBlocks(o, blocks)
	block.ID = blocks[index].ID
	blocks[index] = block
	if mirrored {
		o.Content = blocksPlainText(blocks)
	}
	if err := a.saveMCPBlocks(ctx, &o, blocks); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	return nil, mcpMutateOutput{Object: o}, nil
}

type mcpDeleteBlockInput struct {
	ObjectID string `json:"object_id" jsonschema:"Id of the object that holds the block."`
	BlockID  string `json:"block_id" jsonschema:"Id of the stored block to remove."`
}

// mcpDeleteBlock is the Capacities `deleteBlock` tool. Deleting the last block
// empties the body; the previous state is snapshotted by the central save path
// (the server-side version history every write produces).
func (a *App) mcpDeleteBlock(ctx context.Context, _ *mcp.CallToolRequest, in mcpDeleteBlockInput) (*mcp.CallToolResult, mcpMutateOutput, error) {
	if err := a.requireMCPWriteScope(); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, err := a.mcpLoadObject(in.ObjectID)
	if err != nil {
		return nil, mcpMutateOutput{}, err
	}
	blocks, err := objectBlocks(o)
	if err != nil {
		return nil, mcpMutateOutput{}, errPersistHTTP("Could not read the stored blocks")
	}
	if len(blocks) == 0 {
		return nil, mcpMutateOutput{}, errors.New("This object has no block representation")
	}
	index := -1
	for i, candidate := range blocks {
		if candidate.ID == in.BlockID {
			index = i
			break
		}
	}
	if index < 0 {
		return nil, mcpMutateOutput{}, errors.New("Block not found in this object")
	}
	mirrored := contentMirrorsBlocks(o, blocks)
	blocks = append(blocks[:index], blocks[index+1:]...)
	if mirrored {
		o.Content = blocksPlainText(blocks)
	}
	if err := a.saveMCPBlocks(ctx, &o, blocks); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	return nil, mcpMutateOutput{Object: o}, nil
}

// saveMCPBlocks persists a new block array. The caller holds writeMu; the
// write is a normal object save, so the revision is bumped, the previous
// state is snapshotted and the mobile sync journal is written in one
// transaction.
func (a *App) saveMCPBlocks(ctx context.Context, o *Object, blocks []Block) error {
	if len(blocks) == 0 {
		delete(o.Properties, "blocks")
	} else {
		o.Properties["blocks"] = blockListFromBlocks(blocks)
	}
	delete(o.Properties, "rich_text")
	o.UpdatedAt = now()
	if err := a.validateObject(o); err != nil {
		return err
	}
	if err := a.saveObject(ctx, o); err != nil {
		return errPersistHTTP("Could not save the block change")
	}
	return nil
}

type mcpRecurrenceInput struct {
	Mode     string `json:"mode" jsonschema:"Recurrence mode: daily, weekly, monthly, every_n_days, yearly, weekdays, last_day or last_weekday."`
	N        *int   `json:"n,omitempty" jsonschema:"Interval in days; only for every_n_days (1-3660)."`
	Weekdays []int  `json:"weekdays,omitempty" jsonschema:"ISO weekdays the weekly recurrence lands on: 1=Monday .. 7=Sunday; only for weekly."`
}

func (r mcpRecurrenceInput) toProperties() map[string]any {
	rule := map[string]any{"mode": r.Mode}
	if r.N != nil {
		rule["n"] = *r.N
	}
	if len(r.Weekdays) > 0 {
		rule["weekdays"] = r.Weekdays
	}
	return rule
}

type mcpCreateTaskInput struct {
	Title           string              `json:"title" jsonschema:"The task title."`
	Markdown        string              `json:"markdown,omitempty" jsonschema:"The task body as Markdown, e.g. a checklist."`
	DueDate         string              `json:"due_date,omitempty" jsonschema:"Due date, YYYY-MM-DD. Required when recurrence is set."`
	Priority        string              `json:"priority,omitempty" jsonschema:"Free-text priority, e.g. high, medium, low."`
	ContextObjectID string              `json:"context_object_id,omitempty" jsonschema:"Id of an existing object this task belongs to (stored as the task's context)."`
	Recurrence      *mcpRecurrenceInput `json:"recurrence,omitempty" jsonschema:"Recurrence rule; when set, completing the task creates the next occurrence."`
	Tags            []string            `json:"tags,omitempty" jsonschema:"Tag names to attach, resolved get-or-create (at most 19 so the call stays within the 20-object creation limit)."`
}

// mcpCreateTask is the Capacities `createTask` tool: it builds a task through
// createObjectFromMarkdownCore, so the recurring-task validation
// (validateTaskRecurrence via validateObject) and every other object rule
// apply unchanged. The status starts at todo.
func (a *App) mcpCreateTask(ctx context.Context, _ *mcp.CallToolRequest, in mcpCreateTaskInput) (*mcp.CallToolResult, mcpMutateOutput, error) {
	if err := a.requireMCPWriteScope(); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	if strings.TrimSpace(in.Title) == "" {
		return nil, mcpMutateOutput{}, errors.New("Provide a task title")
	}
	if err := requireMCPCreationBudget(len(in.Tags)); err != nil {
		return nil, mcpMutateOutput{}, err
	}
	if in.DueDate != "" {
		if parsed, err := time.Parse("2006-01-02", in.DueDate); err != nil || parsed.Format("2006-01-02") != in.DueDate {
			return nil, mcpMutateOutput{}, errors.New("due_date must be YYYY-MM-DD")
		}
	}
	properties := map[string]any{"status": "todo"}
	if in.DueDate != "" {
		properties["due_date"] = in.DueDate
	}
	if strings.TrimSpace(in.Priority) != "" {
		properties["priority"] = in.Priority
	}
	if in.ContextObjectID != "" {
		if _, err := a.store.Object(in.ContextObjectID); err != nil {
			return nil, mcpMutateOutput{}, errors.New("context_object_id does not reference an existing object")
		}
		properties["context_id"] = in.ContextObjectID
	}
	if in.Recurrence != nil {
		if strings.TrimSpace(in.Recurrence.Mode) == "" {
			return nil, mcpMutateOutput{}, errors.New("recurrence.mode must be daily, weekly, monthly or every_n_days")
		}
		properties["recurrence"] = in.Recurrence.toProperties()
	}
	markdown := in.Markdown
	o, err := a.createObjectFromMarkdownCore(ctx, objectFromMarkdownRequest{
		TypeID:     "task",
		Title:      in.Title,
		Markdown:   &markdown,
		Tags:       in.Tags,
		Properties: properties,
	})
	if err != nil {
		return nil, mcpMutateOutput{}, err
	}
	return nil, mcpMutateOutput{Object: o}, nil
}

// ---------------------------------------------------------------------------
// Block helpers
// ---------------------------------------------------------------------------

// objectBlocks decodes the stored properties.blocks array into typed blocks.
func objectBlocks(o Object) ([]Block, error) {
	raw, _ := o.Properties["blocks"].([]any)
	if len(raw) == 0 {
		return []Block{}, nil
	}
	data, err := json.Marshal(raw)
	if err != nil {
		return nil, err
	}
	var blocks []Block
	if err := json.Unmarshal(data, &blocks); err != nil {
		return nil, err
	}
	return blocks, nil
}

// contentMirrorsBlocks reports whether the plain-text content is exactly the
// mirror of the block array — the invariant the create/append Markdown paths
// maintain. Only then may a block edit recompute content; otherwise the
// content holds text the blocks do not represent and must be preserved.
func contentMirrorsBlocks(o Object, blocks []Block) bool {
	if len(blocks) == 0 {
		return false
	}
	return strings.TrimSpace(blocksPlainText(blocks)) == strings.TrimSpace(o.Content)
}

// blockPlainText is one block's contribution to the plain-text mirror, with
// the same rules blocksPlainText applies to a single block (placeholder-only
// blocks contribute nothing).
func blockPlainText(block Block) string {
	text := strings.ReplaceAll(block.Text, lineSeparator, "\n")
	if strings.TrimSpace(text) == "" || text == objectReplacement {
		return ""
	}
	return text
}
