package main

import (
	"context"
	"encoding/json"
	"errors"
	"lumen/server/internal/document"
	"net/http"
	"strings"
	"time"
)


// apiVersion is echoed in the X-Lumen-Api-Version header on every response.
// The request header is accepted for forward compatibility: an absent or
// unknown value never yields 400, the server always answers with the version
// it actually speaks.
const apiVersion = "1.0.0"

// httpError carries the HTTP status a core (handler-independent) error maps
// to. The api-v2 handlers and the MCP tools (backend stage mcp-server) call
// the same core functions; the MCP surface shows the message to the agent
// in-band, while the HTTP handlers still answer with the exact status codes
// they always had. A plain error from a core is a client error (400).
type httpError struct {
	status  int
	message string
}

func (e *httpError) Error() string { return e.message }

func errNotFoundHTTP(message string) error { return &httpError{status: 404, message: message} }
func errPersistHTTP(message string) error  { return &httpError{status: 500, message: message} }

// writeCoreError maps a core error onto the HTTP status the pre-extraction
// handlers used: 404 for a missing object, 500 for a persist failure and 400
// for every validation problem.
func writeCoreError(w http.ResponseWriter, err error) {
	var he *httpError
	if errors.As(err, &he) {
		jsonError(w, he.status, he.message)
		return
	}
	jsonError(w, 400, err.Error())
}

func (a *App) registerAPIv2Routes(mux *http.ServeMux) {
	mux.HandleFunc("GET /v1/api", a.apiDiscovery)
	mux.HandleFunc("GET /v1/catalog", a.listCatalog)
	mux.HandleFunc("POST /v1/objects/from-url", a.createObjectFromURL)
	mux.HandleFunc("POST /v1/objects/markdown", a.createObjectFromMarkdown)
	mux.HandleFunc("POST /v1/objects/{id}/append-markdown", a.appendObjectMarkdown)
	mux.HandleFunc("POST /v1/journal/{date}/append", a.appendJournalMarkdown)
	mux.HandleFunc("POST /v1/assets/upload/init", a.startChunkedUpload)
	mux.HandleFunc("POST /v1/assets/upload/{upload_id}/part", a.uploadChunkedPart)
	mux.HandleFunc("POST /v1/assets/upload/{upload_id}/complete", a.completeChunkedUpload)
	mux.HandleFunc("POST /v1/assets/upload/{upload_id}/abort", a.abortChunkedUpload)
}

// ---------------------------------------------------------------------------
// Discovery
// ---------------------------------------------------------------------------

type apiEndpointInfo struct {
	Name   string `json:"name"`
	Method string `json:"method"`
	Path   string `json:"path"`
	Scope  string `json:"scopes"`
}

// apiCatalog is the endpoint catalog of the developer API. Scope values:
// "public" needs no token, "api:read"/"api:write" name the scope that grants
// access (api:write implies api:read), and "full" marks the first-party sync
// protocol, which is deliberately not governed by scopes.
var apiCatalog = []apiEndpointInfo{
	{Name: "health", Method: "GET", Path: "/health", Scope: "public"},
	{Name: "api_discovery", Method: "GET", Path: "/v1/api", Scope: "api:read"},

	{Name: "list_types", Method: "GET", Path: "/v1/types", Scope: "api:read"},
	{Name: "create_type", Method: "POST", Path: "/v1/types", Scope: "api:write"},
	{Name: "update_type", Method: "PATCH", Path: "/v1/types/{id}", Scope: "api:write"},

	{Name: "list_objects", Method: "GET", Path: "/v1/objects", Scope: "api:read"},
	{Name: "create_object", Method: "POST", Path: "/v1/objects", Scope: "api:write"},
	{Name: "get_object", Method: "GET", Path: "/v1/objects/{id}", Scope: "api:read"},
	{Name: "update_object", Method: "PATCH", Path: "/v1/objects/{id}", Scope: "api:write"},
	{Name: "delete_object", Method: "DELETE", Path: "/v1/objects/{id}", Scope: "api:write"},
	{Name: "list_backlinks", Method: "GET", Path: "/v1/objects/{id}/backlinks", Scope: "api:read"},
	{Name: "create_object_from_template", Method: "POST", Path: "/v1/objects/from-template", Scope: "api:write"},
	{Name: "create_object_from_url", Method: "POST", Path: "/v1/objects/from-url", Scope: "api:write"},
	{Name: "create_object_from_markdown", Method: "POST", Path: "/v1/objects/markdown", Scope: "api:write"},
	{Name: "append_markdown_to_object", Method: "POST", Path: "/v1/objects/{id}/append-markdown", Scope: "api:write"},
	{Name: "convert_object", Method: "POST", Path: "/v1/objects/{id}/convert", Scope: "api:write"},
	{Name: "merge_objects", Method: "POST", Path: "/v1/objects/{id}/merge", Scope: "api:write"},
	{Name: "restore_object_from_trash", Method: "POST", Path: "/v1/objects/{id}/restore", Scope: "api:write"},
	{Name: "refresh_reader", Method: "POST", Path: "/v1/objects/{id}/refresh-reader", Scope: "api:write"},
	{Name: "list_object_versions", Method: "GET", Path: "/v1/objects/{id}/versions", Scope: "api:read"},
	{Name: "get_object_version", Method: "GET", Path: "/v1/objects/{id}/versions/{revision}", Scope: "api:read"},
	{Name: "restore_object_version", Method: "POST", Path: "/v1/objects/{id}/restore/{revision}", Scope: "api:write"},

	{Name: "get_journal", Method: "GET", Path: "/v1/journal/{date}", Scope: "api:read"},
	{Name: "list_catalog", Method: "GET", Path: "/v1/catalog", Scope: "api:read"},
	{Name: "append_to_journal", Method: "POST", Path: "/v1/journal/{date}/append", Scope: "api:write"},
	{Name: "search_objects", Method: "GET", Path: "/v1/search", Scope: "api:read"},
	{Name: "capture", Method: "POST", Path: "/v1/capture", Scope: "api:write"},
	{Name: "complete_task", Method: "POST", Path: "/v1/tasks/{id}/complete", Scope: "api:write"},

	{Name: "list_trash", Method: "GET", Path: "/v1/trash", Scope: "api:read"},
	{Name: "purge_from_trash", Method: "DELETE", Path: "/v1/trash/{id}", Scope: "api:write"},
	{Name: "empty_trash", Method: "POST", Path: "/v1/trash/empty", Scope: "api:write"},

	{Name: "list_collection_items", Method: "GET", Path: "/v1/collections/{id}/items", Scope: "api:read"},
	{Name: "append_collection_item", Method: "POST", Path: "/v1/collections/{id}/items", Scope: "api:write"},
	{Name: "replace_collection_items", Method: "PUT", Path: "/v1/collections/{id}/items", Scope: "api:write"},
	{Name: "remove_collection_item", Method: "DELETE", Path: "/v1/collections/{id}/items/{object_id}", Scope: "api:write"},

	{Name: "markdown_to_blocks", Method: "POST", Path: "/v1/markdown/to-blocks", Scope: "api:read"},
	{Name: "markdown_from_blocks", Method: "POST", Path: "/v1/markdown/from-blocks", Scope: "api:read"},

	{Name: "upload_asset", Method: "POST", Path: "/v1/assets", Scope: "api:write"},
	{Name: "promote_asset", Method: "POST", Path: "/v1/assets/{id}/promote", Scope: "api:write"},
	{Name: "list_assets", Method: "GET", Path: "/v1/assets", Scope: "api:read"},
	{Name: "get_asset_content", Method: "GET", Path: "/v1/assets/{id}/content", Scope: "api:read"},
	{Name: "start_chunked_upload", Method: "POST", Path: "/v1/assets/upload/init", Scope: "api:write"},
	{Name: "upload_chunk_part", Method: "POST", Path: "/v1/assets/upload/{upload_id}/part", Scope: "api:write"},
	{Name: "complete_chunked_upload", Method: "POST", Path: "/v1/assets/upload/{upload_id}/complete", Scope: "api:write"},
	{Name: "abort_chunked_upload", Method: "POST", Path: "/v1/assets/upload/{upload_id}/abort", Scope: "api:write"},

	{Name: "export_space", Method: "GET", Path: "/v1/export", Scope: "api:read"},
	{Name: "export_space_options", Method: "POST", Path: "/v1/export", Scope: "api:read"},

	{Name: "search_blocks", Method: "GET", Path: "/v1/blocks", Scope: "api:read"},
	{Name: "resolve_block", Method: "GET", Path: "/v1/blocks/{block_id}", Scope: "api:read"},
	{Name: "block_references", Method: "GET", Path: "/v1/blocks/{block_id}/references", Scope: "api:read"},
	{Name: "read_blocks", Method: "GET", Path: "/v1/objects/{id}/blocks", Scope: "api:read"},
	{Name: "add_block", Method: "POST", Path: "/v1/objects/{id}/blocks", Scope: "api:write"},
	{Name: "update_block", Method: "PATCH", Path: "/v1/objects/{id}/blocks/{block_id}", Scope: "api:write"},
	{Name: "delete_block", Method: "DELETE", Path: "/v1/objects/{id}/blocks/{block_id}", Scope: "api:write"},
	{Name: "skip_task", Method: "POST", Path: "/v1/tasks/{id}/skip", Scope: "api:write"},
	{Name: "excuse_task", Method: "POST", Path: "/v1/tasks/{id}/excuse", Scope: "api:write"},
	{Name: "habit_stats", Method: "GET", Path: "/v1/habits/{id}/stats", Scope: "api:read"},
	{Name: "set_habit_entry", Method: "PUT", Path: "/v1/habits/{id}/entries/{date}", Scope: "api:write"},
	{Name: "delete_habit_entry", Method: "DELETE", Path: "/v1/habits/{id}/entries/{date}", Scope: "api:write"},

	{Name: "sync_status", Method: "GET", Path: "/v1/sync/status", Scope: "full"},
	{Name: "sync_pull", Method: "POST", Path: "/v1/sync/pull", Scope: "full"},
	{Name: "sync_push", Method: "POST", Path: "/v1/sync/push", Scope: "full"},

	// The MCP endpoint (backend stage mcp-server) hosts read AND write tools
	// behind one URL: its scope is the one the full toolset needs, and a
	// token restricted to api:read can still initialize and call the read
	// tools (the per-tool check lives in the MCP layer).
	{Name: "mcp_server", Method: "POST", Path: "/mcp", Scope: "api:write"},
}

// apiDiscovery serves GET /v1/api: the API version, the effective scopes of
// the current token, the operational limits and the endpoint catalog. The
// MCP stage (backend/api-v2's successor) consumes this document to build its
// tool surface.
func (a *App) apiDiscovery(w http.ResponseWriter, r *http.Request) {
	jsonResponse(w, 200, map[string]any{
		"version":                 apiVersion,
		"document_schema_version": 2,
		"document_protocol":       "lumen-document/2",
		"token_scopes":            a.scopeNames,
		"limits": map[string]int{
			"max_small_asset_bytes":    50 << 20,
			"max_chunked_asset_bytes":  chunkedUploadMaxSize,
			"min_chunk_part_bytes":     chunkedUploadMinPart,
			"max_chunk_part_bytes":     chunkedUploadMaxPart,
			"upload_session_ttl_hours": int(chunkedUploadTTL / time.Hour),
			"default_page_limit":       defaultPageLimit,
			"max_page_limit":           maxPageLimit,
			"mcp_max_objects_per_call": mcpMaxObjectsPerCall,
		},
		"endpoints": apiCatalog,
	})
}

// ---------------------------------------------------------------------------
// Create from URL
// ---------------------------------------------------------------------------

type fromURLRequest struct {
	URL           string   `json:"url"`
	Title         string   `json:"title"`
	Tags          []string `json:"tags"`
	NotesMarkdown string   `json:"notes_markdown"`
	JournalLink   *bool    `json:"journal_link"`
}

func (a *App) createObjectFromURL(w http.ResponseWriter, r *http.Request) {
	var req fromURLRequest
	if !decodeLimit(w, r, &req, 12<<20) {
		return
	}
	if strings.TrimSpace(req.URL) == "" {
		jsonError(w, 400, "Provide a URL")
		return
	}
	cleaned, err := a.normalizeCaptureURL(r.Context(), req.URL)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	// Same dedupe as capture: the cleaned URL is the identity of a web object.
	if existing, found := a.findWebObjectByURL(cleaned); found {
		a.writeMu.Lock()
		current, readErr := a.store.Object(existing.ID)
		if readErr != nil {
			// A concurrent delete raced the scan; fall through and create.
			a.writeMu.Unlock()
		} else {
			defer a.writeMu.Unlock()
			a.respondExistingFromURL(w, r, current, req)
			return
		}
	}
	// Fetch before taking the write lock, exactly like capture: a slow public
	// site must not block unrelated writes. Same fetch, same guards.
	o := Object{ID: newID(), TypeID: "web", Title: req.Title, Properties: map[string]any{"url": cleaned, "source": "api"}, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
	o.Properties["status"] = "Para revisar"
	var previewAsset *Asset
	var previewBlob []byte
	preview, err := a.capturePreview(r.Context(), cleaned)
	if err != nil {
		o.Properties["preview_status"] = "unavailable"
		o.Properties["preview_error"] = "The public preview could not be fetched; the link is saved."
		o.Properties["reader_content"] = ""
	} else {
		o.Properties["preview_status"] = "ready"
		o.Properties["og_title"] = preview.Title
		o.Properties["og_description"] = preview.Description
		o.Properties["summary"] = preview.Description
		o.Properties["og_site_name"] = preview.SiteName
		o.Properties["reader_content"] = preview.ReaderContent
		if preview.Favicon != "" {
			o.Properties["favicon"] = preview.Favicon
		}
		if o.Title == "" {
			o.Title = preview.Title
		}
		if len(preview.ImageData) > 0 {
			asset := Asset{ID: newID(), ObjectID: o.ID, Name: "preview.webp", MIME: preview.ImageMIME, Size: len(preview.ImageData), CreatedAt: now()}
			previewAsset, previewBlob = &asset, preview.ImageData
			o.Properties["image_asset_id"] = asset.ID
		}
	}
	if strings.TrimSpace(o.Title) == "" {
		if u, safeErr := safeURL(cleaned); safeErr == nil {
			o.Title = u.Hostname()
		} else {
			o.Title = "Texto guardado"
		}
	}
	if err := a.finishFromURL(w, r, &o, req, previewAsset, previewBlob); err != nil {
		jsonError(w, 500, err.Error())
	}
}

// respondExistingFromURL handles the duplicate case: notes_markdown and tags
// are applied to the existing object and today's journal is linked on
// request. The caller holds writeMu. Notes, tags and the object update commit
// in one transaction, so a failure cannot leave orphan tags.
func (a *App) respondExistingFromURL(w http.ResponseWriter, r *http.Request, current Object, req fromURLRequest) {
	records := []Record{}
	changed := false
	if len(req.Tags) > 0 {
		ids, tagRecords, err := a.resolveTagNames(req.Tags)
		if err != nil {
			jsonError(w, 400, err.Error())
			return
		}
		records = append(records, tagRecords...)
		if current.Properties == nil {
			current.Properties = map[string]any{}
		}
		union := mergeLinks(stringList(current.Properties["tags"]), ids)
		list := make([]any, 0, len(union))
		for _, id := range union {
			list = append(list, id)
		}
		current.Properties["tags"] = list
		changed = true
	}
	if strings.TrimSpace(req.NotesMarkdown) != "" {
		if err := appendMarkdownBlocks(&current, req.NotesMarkdown, nil); err != nil {
			jsonError(w, 400, err.Error())
			return
		}
		changed = true
	}
	if changed {
		current.UpdatedAt = now()
		records = append(records, a.prepareSave(&current)...)
		if err := a.store.PutBatch(records, saveCleanup(current.ID)); err != nil {
			jsonError(w, 500, "Could not append the notes")
			return
		}
	}
	journalID := ""
	if req.JournalLink != nil && *req.JournalLink {
		var err error
		if journalID, err = a.linkJournalObject(r.Context(), todayJournalDate(), current.ID); err != nil {
			jsonError(w, 500, "The link was saved, but could not link the journal")
			return
		}
	}
	raw, _ := json.Marshal(current)
	response := map[string]any{}
	_ = json.Unmarshal(raw, &response)
	response["existing"] = true
	if journalID != "" {
		response["journal_id"] = journalID
	}
	jsonResponse(w, 200, response)
}

// finishFromURL validates and commits a newly built web object (plus its
// preview asset, notes and tags) and links today's journal on request.
func (a *App) finishFromURL(w http.ResponseWriter, r *http.Request, o *Object, req fromURLRequest, previewAsset *Asset, previewBlob []byte) error {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	if strings.TrimSpace(req.NotesMarkdown) != "" {
		blocks, err := MarkdownToBlocks(req.NotesMarkdown)
		if err != nil {
			jsonError(w, 400, err.Error())
			return nil
		}
		if len(blocks) > 0 {
			d, e := canonicalDocumentFromBlocks(blocks)
			if e != nil {
				return e
			}
			if e = setCanonicalDocument(o, d); e != nil {
				return e
			}
		}
	}
	if err := a.validateObject(o); err != nil {
		jsonError(w, 400, err.Error())
		return nil
	}
	// Tags are attached after validation: the tag objects are created in the
	// same transaction, so they do not exist as store rows when the relation
	// check would run. resolveTagNames guarantees they are live tag objects.
	tagRecords := []Record{}
	if len(req.Tags) > 0 {
		ids, records, err := a.resolveTagNames(req.Tags)
		if err != nil {
			jsonError(w, 400, err.Error())
			return nil
		}
		tagRecords = records
		list := make([]any, 0, len(ids))
		for _, id := range ids {
			list = append(list, id)
		}
		o.Properties["tags"] = list
	}
	records := append(a.prepareSave(o), tagRecords...)
	var blobs []BlobRecord
	if previewAsset != nil {
		records = append(records, Record{Kind: "asset", ID: previewAsset.ID, Value: *previewAsset})
		blobs = append(blobs, BlobRecord{ID: previewAsset.ID, Data: previewBlob})
	}
	if err := a.store.PutBatchBlobs(records, saveCleanup(o.ID), blobs); err != nil {
		return errors.New("Could not save the link")
	}
	journalID := ""
	if req.JournalLink != nil && *req.JournalLink {
		var err error
		if journalID, err = a.linkJournalObject(r.Context(), todayJournalDate(), o.ID); err != nil {
			jsonError(w, 500, "The link was saved, but could not link the journal")
			return nil
		}
	}
	raw, _ := json.Marshal(*o)
	response := map[string]any{}
	_ = json.Unmarshal(raw, &response)
	response["existing"] = false
	if journalID != "" {
		response["journal_id"] = journalID
	}
	jsonResponse(w, 201, response)
	return nil
}

// stringList extracts a string array from a property value of unknown shape.
func stringList(value any) []string {
	switch v := value.(type) {
	case []string:
		return v
	case []any:
		out := make([]string, 0, len(v))
		for _, entry := range v {
			if id, ok := entry.(string); ok {
				out = append(out, id)
			}
		}
		return out
	}
	return nil
}

// resolveTagNames maps tag names to tag object IDs, creating the missing tag
// objects, and returns the records of the created ones so the caller commits
// them together with its own object. The caller holds writeMu.
func (a *App) resolveTagNames(names []string) ([]string, []Record, error) {
	ids := make([]string, 0, len(names))
	records := []Record{}
	seen := map[string]bool{}
	for _, name := range names {
		name = strings.TrimSpace(name)
		if name == "" {
			return nil, nil, errors.New("Tag names must not be empty")
		}
		if len([]rune(name)) > 1000 {
			return nil, nil, errors.New("Tag names support up to 1000 characters")
		}
		key := strings.ToLower(name)
		if seen[key] {
			continue
		}
		seen[key] = true
		if owner := a.store.TitleOwner(name); owner != "" {
			var existing Object
			if err := a.store.Get("object", owner, &existing); err == nil {
				if existing.TypeID == "tag" && !isTemplateObject(existing) {
					ids = append(ids, owner)
					continue
				}
				return nil, nil, errors.New("An object with the title \"" + name + "\" already exists; tags need a free title")
			}
			// The title index resolves to a row that no longer reads; the
			// uniqueness check in validateObject has the final word.
		}
		tag := Object{ID: newID(), TypeID: "tag", Title: name, Properties: map[string]any{}, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
		if err := a.validateObject(&tag); err != nil {
			return nil, nil, err
		}
		records = append(records, a.prepareSave(&tag)...)
		ids = append(ids, tag.ID)
	}
	return ids, records, nil
}

// ---------------------------------------------------------------------------
// Create / append via Markdown
// ---------------------------------------------------------------------------

type objectFromMarkdownRequest struct {
	TypeID     string         `json:"type_id"`
	Title      string         `json:"title"`
	Markdown   *string        `json:"markdown"`
	Tags       []string       `json:"tags"`
	Properties map[string]any `json:"properties"`
}

// createObjectFromMarkdown serves POST /v1/objects/markdown: create an object
// whose body comes from MarkdownToBlocks (stage markdown-converter).
// Collections are rejected (their items are managed by the collections
// endpoints) and so are templates (created like any other object through
// POST /v1/objects).
func (a *App) createObjectFromMarkdown(w http.ResponseWriter, r *http.Request) {
	var req objectFromMarkdownRequest
	if !decodeLimit(w, r, &req, 8<<20) {
		return
	}
	o, err := a.createObjectFromMarkdownCore(r.Context(), req)
	if err != nil {
		writeCoreError(w, err)
		return
	}
	jsonResponse(w, 201, o)
}

// createObjectFromMarkdownCore is the create-from-Markdown path shared by the
// HTTP endpoint and the MCP create_object tool (backend stage mcp-server):
// the same validation, the same block conversion, the same tag resolution and
// the same transactional save. The caller does NOT hold writeMu.
func (a *App) createObjectFromMarkdownCore(ctx context.Context, req objectFromMarkdownRequest) (Object, error) {
	if strings.TrimSpace(req.TypeID) == "" {
		return Object{}, errors.New("Provide type_id")
	}
	if req.TypeID == "collection" {
		return Object{}, errors.New("Collections are created with POST /v1/objects and managed through the collections endpoints")
	}
	if req.Markdown == nil {
		return Object{}, errors.New("Provide markdown")
	}
	if req.Properties == nil {
		req.Properties = map[string]any{}
	}
	if isTemplate, ok := req.Properties["is_template"].(bool); ok && isTemplate {
		return Object{}, errors.New("Templates are created with POST /v1/objects and properties.is_template")
	}
	for _, key := range []string{"blocks", "rich_text", "tags"} {
		if _, exists := req.Properties[key]; exists {
			return Object{}, errors.New("properties." + key + " is managed by this endpoint; blocks come from the markdown field and tags from the top-level tags array")
		}
	}
	blocks, err := MarkdownToBlocks(*req.Markdown)
	if err != nil {
		return Object{}, err
	}
	o := Object{ID: newID(), TypeID: req.TypeID, Title: req.Title, Properties: req.Properties, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
	d, e := canonicalDocumentFromBlocks(blocks)
	if e != nil {
		return Object{}, e
	}
	if e = setCanonicalDocument(&o, d); e != nil {
		return Object{}, e
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	if o.TypeID == "web" {
		preserveReaderMetadata(&o, nil)
	}
	if err := a.validateObject(&o); err != nil {
		return Object{}, err
	}
	// Tags are attached after validation: the tag objects are created in the
	// same transaction, so they do not exist as store rows when the relation
	// check would run. resolveTagNames guarantees they are live tag objects.
	tagRecords := []Record{}
	if len(req.Tags) > 0 {
		ids, records, tagErr := a.resolveTagNames(req.Tags)
		if tagErr != nil {
			return Object{}, tagErr
		}
		tagRecords = records
		list := make([]any, 0, len(ids))
		for _, id := range ids {
			list = append(list, id)
		}
		o.Properties["tags"] = list
	}
	records := append(a.prepareSave(&o), tagRecords...)
	if err := a.store.PutBatch(records, saveCleanup(o.ID)); err != nil {
		return Object{}, errPersistHTTP("Could not persist encrypted object")
	}
	return o, nil
}

type appendMarkdownRequest struct {
	BaseRevision uint64  `json:"base_revision"`
	OperationID  string  `json:"op_id"`
	Markdown     *string `json:"markdown"`
	Position     *int    `json:"position"`
}

// appendObjectMarkdown serves POST /v1/objects/{id}/append-markdown: it
// converts the Markdown to blocks and appends them following the
// convert-merge representation rules (see appendMarkdownBlocks). The save is
// a normal object write: revision bump, version snapshot and a mobile journal
// entry, all in one transaction.
func (a *App) appendObjectMarkdown(w http.ResponseWriter, r *http.Request) {
	var req appendMarkdownRequest
	if !decodeLimit(w, r, &req, 8<<20) {
		return
	}
	o, err := a.appendObjectMarkdownCore(r.Context(), r.PathValue("id"), req)
	if err != nil {
		writeCoreError(w, err)
		return
	}
	jsonResponse(w, 200, o)
}

// appendObjectMarkdownCore is the append path shared by the HTTP endpoint and
// the MCP append_md_to_object tool (backend stage mcp-server). The caller
// does NOT hold writeMu.
func (a *App) appendObjectMarkdownCore(ctx context.Context, objectID string, req appendMarkdownRequest) (Object, error) {
	commands, e := canonicalAppendCommands(stringValue(req.Markdown), req.OperationID, req.Position, "")
	if e != nil {
		return Object{}, e
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	return a.commitDocument(objectID, req.BaseRevision, req.OperationID, nil, commands)
}

func stringValue(value *string) string {
	if value == nil {
		return ""
	}
	return *value
}

// appendMarkdownBlocks appends Markdown to an object's body following the
// representation rules of the convert-merge stage:
//
//   - an object that carries a non-empty properties.blocks gets the new
//     blocks appended (or inserted at position) and loses rich_text, so no
//     client renders a stale formatted body;
//   - an object with no text anywhere gains the block representation: the
//     block array is authoritative in the editor, but an empty note has
//     nothing to hide;
//   - a plain-text object (content or rich_text, no blocks) never gains
//     blocks: promoting the block array would hide the existing plain body,
//     so only content is appended and rich_text is dropped.
//
// The plain-text content is always appended and stays canonical. position
// (0-based) applies only to the block representation; for plain-text objects
// it is ignored. The caller holds writeMu.
func appendMarkdownBlocks(o *Object, markdown string, position *int) error {
	if o.Document != nil {
		return appendCanonicalMarkdown(o, markdown, position)
	}
	if strings.TrimSpace(markdown) == "" {
		return errors.New("Provide markdown")
	}
	if position != nil && *position < 0 {
		return errors.New("position must be a non-negative integer")
	}
	blocks, err := MarkdownToBlocks(markdown)
	if err != nil {
		return err
	}
	if len(blocks) == 0 {
		return errors.New("The markdown produced no content")
	}
	existing, _ := o.Properties["blocks"].([]any)
	if len(existing) > 0 {
		insert := len(existing)
		if position != nil {
			if *position > len(existing) {
				return errors.New("position must be between 0 and the number of blocks")
			}
			insert = *position
		}
		merged := make([]any, 0, len(existing)+len(blocks))
		merged = append(merged, existing[:insert]...)
		merged = append(merged, blockListFromBlocks(blocks)...)
		merged = append(merged, existing[insert:]...)
		o.Properties["blocks"] = merged
	} else if !objectHasText(*o) {
		if position != nil && *position > 0 {
			return errors.New("position must be 0 for an object without blocks")
		}
		o.Properties["blocks"] = blockListFromBlocks(blocks)
	} else {
		// Plain-text object: a block array would hide its body.
		delete(o.Properties, "blocks")
	}
	delete(o.Properties, "rich_text")
	appendPlainText(o, blocksPlainText(blocks))
	return nil
}

// appendPlainText extends the canonical plain-text body.
func appendPlainText(o *Object, text string) {
	if o.Document != nil {
		if text != "" {
			o.Document.Blocks = append(o.Document.Blocks, document.Node{ID: newID(), Type: "paragraph", Content: []document.Inline{{Type: "text", Text: text}}})
			o.Content = document.PlainText(*o.Document)
		}
		return
	}
	if text == "" {
		return
	}
	if strings.TrimSpace(o.Content) == "" {
		o.Content = text
		return
	}
	o.Content = strings.TrimRight(o.Content, "\n") + "\n\n" + text
}

// blocksPlainText renders the plain-text mirror of a block array: every
// block's text (the concatenation of its runs) with in-block line breaks
// normalized, joined by a blank line. Placeholder-only blocks (dividers,
// math) contribute nothing.
func blocksPlainText(blocks []Block) string {
	parts := make([]string, 0, len(blocks))
	for _, block := range blocks {
		text := strings.ReplaceAll(block.Text, lineSeparator, "\n")
		if strings.TrimSpace(text) != "" && text != objectReplacement {
			parts = append(parts, text)
		}
		if children := blocksPlainText(block.Children); children != "" {
			parts = append(parts, children)
		}
		for _, column := range block.Columns {
			if text := blocksPlainText(column); text != "" {
				parts = append(parts, text)
			}
		}
		for _, row := range block.Rows {
			cells := []string{}
			for _, cell := range row {
				cells = append(cells, runsText(cell.Runs))
			}
			if text := strings.Join(cells, "\t"); strings.TrimSpace(text) != "" {
				parts = append(parts, text)
			}
		}
	}
	return strings.Join(parts, "\n\n")
}

// blockListFromBlocks converts typed blocks into the generic []any shape the
// editor persists, through JSON, so field names and omitempty stay canonical.
func blockListFromBlocks(blocks []Block) []any {
	raw, err := json.Marshal(blocks)
	if err != nil {
		return []any{}
	}
	var out []any
	if json.Unmarshal(raw, &out) != nil {
		return []any{}
	}
	return out
}

// ---------------------------------------------------------------------------
// Journal append
// ---------------------------------------------------------------------------

type journalAppendRequest struct {
	BaseRevision uint64 `json:"base_revision"`
	OperationID  string `json:"op_id"`
	Markdown     string `json:"markdown"`
	Section      string `json:"section"`
}

// appendJournalMarkdown updates an existing daily note with a revisioned,
// replayable body append. GET the journal first to obtain its current revision.
func (a *App) appendJournalMarkdown(w http.ResponseWriter, r *http.Request) {
	var req journalAppendRequest
	if !decodeLimit(w, r, &req, 8<<20) {
		return
	}
	o, err := a.appendJournalMarkdownCore(r.Context(), r.PathValue("date"), req.Markdown, req.Section, req.BaseRevision, req.OperationID)
	if err != nil {
		writeCoreError(w, err)
		return
	}
	jsonResponse(w, 200, o)
}

// appendJournalMarkdownCore is the daily-note append shared by the HTTP
// endpoint and the MCP append_to_daily_note tool (backend stage mcp-server).
// The caller does NOT hold writeMu.
func (a *App) appendJournalMarkdownCore(ctx context.Context, date, markdown, section string, base uint64, opID string) (Object, error) {
	commands, e := canonicalAppendCommands(markdown, opID, nil, strings.TrimSpace(section))
	if e != nil {
		return Object{}, e
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	if parsed, err := time.Parse("2006-01-02", date); err != nil || parsed.Format("2006-01-02") != date {
		return Object{}, errors.New("Date must be YYYY-MM-DD")
	}
	return a.commitDocument("journal-"+date, base, opID, nil, commands)
}

// appendJournalSection appends Markdown under a section heading. In the block
// representation the section is a level-2 heading block and the content is
// inserted before the next heading of the same or a higher level; in a
// plain-text daily note the section is a "## <name>" line and the content is
// inserted before the next line starting with "#".
func appendJournalSection(o *Object, section, markdown string) error {
	if o.Document != nil {
		addition, e := MarkdownToBlocks(markdown)
		if e != nil {
			return e
		}
		nodes, e := canonicalDocumentFromBlocks(addition)
		if e != nil {
			return e
		}
		d, e := document.Normalize(*o.Document)
		if e != nil {
			return e
		}
		insert := len(d.Blocks)
		found := false
		level := 0
		for i, n := range d.Blocks {
			if n.Type != "heading" {
				continue
			}
			v, _ := n.Attrs["level"].(float64)
			if found && int(v) <= level {
				insert = i
				break
			}
			title := document.PlainText(document.Document{SchemaVersion: 2, Blocks: []document.Node{n}})
			if title == section {
				found = true
				level = int(v)
			}
		}
		if !found {
			nodes.Blocks = append([]document.Node{{ID: newID(), Type: "heading", Attrs: map[string]any{"level": 2}, Content: []document.Inline{{Type: "text", Text: section}}}}, nodes.Blocks...)
		}
		out := append([]document.Node{}, d.Blocks[:insert]...)
		out = append(out, nodes.Blocks...)
		out = append(out, d.Blocks[insert:]...)
		d.Blocks = out
		return setCanonicalDocument(o, d)
	}
	blocks, err := MarkdownToBlocks(markdown)
	if err != nil {
		return err
	}
	if len(blocks) == 0 {
		return errors.New("The markdown produced no content")
	}
	text := blocksPlainText(blocks)
	existing, _ := o.Properties["blocks"].([]any)
	if len(existing) > 0 {
		insert, found := sectionBlockIndex(existing, section)
		toInsert := blockListFromBlocks(blocks)
		if !found {
			toInsert = append(blockListFromBlocks([]Block{sectionHeadingBlock(section)}), toInsert...)
		}
		merged := make([]any, 0, len(existing)+len(toInsert))
		merged = append(merged, existing[:insert]...)
		merged = append(merged, toInsert...)
		merged = append(merged, existing[insert:]...)
		o.Properties["blocks"] = merged
		delete(o.Properties, "rich_text")
		appendPlainText(o, text)
		return nil
	}
	if !objectHasText(*o) {
		all := append([]Block{sectionHeadingBlock(section)}, blocks...)
		o.Properties["blocks"] = blockListFromBlocks(all)
		delete(o.Properties, "rich_text")
		o.Content = blocksPlainText(all)
		return nil
	}
	insertSectionText(o, section, text)
	delete(o.Properties, "rich_text")
	return nil
}

// sectionHeadingBlock builds the level-2 heading that names a section.
func sectionHeadingBlock(section string) Block {
	return Block{ID: newID(), Type: blockHeading, Text: section, Level: 2, Runs: []Run{{Text: section}}}
}

// sectionBlockIndex locates the insertion point for new content inside a
// section of a block array: the index right after the last block of the last
// heading whose text matches section, or the end of the array when the
// section does not exist yet.
func sectionBlockIndex(blocks []any, section string) (int, bool) {
	headingLevel := 0
	lastHeading := -1
	insert := len(blocks)
	for i, raw := range blocks {
		block, ok := raw.(map[string]any)
		if !ok {
			continue
		}
		if block["type"] != blockHeading {
			continue
		}
		title, _ := block["text"].(string)
		level := 2
		if rawLevel, ok := block["level"].(float64); ok && rawLevel > 0 {
			level = int(rawLevel)
		}
		if lastHeading >= 0 && level <= headingLevel {
			insert = i
			break
		}
		if strings.EqualFold(strings.TrimSpace(title), section) {
			lastHeading, headingLevel, insert = i, level, len(blocks)
		}
	}
	if lastHeading < 0 {
		return len(blocks), false
	}
	return insert, true
}

// insertSectionText is the plain-text variant: it inserts the content after
// the last "## <section>" line, creating the heading when missing. The next
// line starting with "#" ends the section.
func insertSectionText(o *Object, section, text string) {
	lines := strings.Split(o.Content, "\n")
	headingLine := -1
	for i, line := range lines {
		if strings.EqualFold(strings.TrimSpace(line), "## "+section) {
			headingLine = i
		}
	}
	if headingLine < 0 {
		if strings.TrimSpace(o.Content) == "" {
			o.Content = "## " + section + "\n\n" + text
			return
		}
		o.Content = strings.TrimRight(o.Content, "\n") + "\n\n## " + section + "\n\n" + text
		return
	}
	end := len(lines)
	for i := headingLine + 1; i < len(lines); i++ {
		if strings.HasPrefix(strings.TrimSpace(lines[i]), "#") {
			end = i
			break
		}
	}
	merged := make([]string, 0, len(lines)+3)
	merged = append(merged, lines[:end]...)
	if len(merged) > 0 && strings.TrimSpace(merged[len(merged)-1]) != "" {
		merged = append(merged, "")
	}
	merged = append(merged, strings.Split(text, "\n")...)
	if end < len(lines) && strings.TrimSpace(lines[end]) != "" {
		merged = append(merged, "")
	}
	merged = append(merged, lines[end:]...)
	o.Content = strings.Join(merged, "\n")
}
