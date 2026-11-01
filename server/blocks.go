package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"net/url"
	"strconv"
	"strings"
)

const blockHrefPrefix = "lumen://block/"

type blockHit struct {
	ObjectID    string         `json:"object_id"`
	ObjectTitle string         `json:"object_title"`
	Revision    uint64         `json:"revision"`
	Block       map[string]any `json:"block"`
}

type blockReference struct {
	ObjectID    string `json:"object_id"`
	ObjectTitle string `json:"object_title"`
	BlockID     string `json:"block_id"`
	Embedded    bool   `json:"embedded"`
}

func (a *App) registerBlockRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /v1/blocks", a.searchBlocks)
	mux.HandleFunc("GET /v1/blocks/{block_id}", a.resolveBlock)
	mux.HandleFunc("GET /v1/blocks/{block_id}/references", a.blockReferences)
	mux.HandleFunc("GET /v1/objects/{id}/blocks", a.readBlocks)
	mux.HandleFunc("POST /v1/objects/{id}/blocks", a.editBlocks)
	mux.HandleFunc("PATCH /v1/objects/{id}/blocks/{block_id}", a.editBlocks)
	mux.HandleFunc("DELETE /v1/objects/{id}/blocks/{block_id}", a.editBlocks)
}

// Walk only block containers, never arbitrary user properties. Payloads are
// decrypted on demand rather than retaining a second plaintext content index.
func walkBlocks(list []any, depth int, visit func(map[string]any)) {
	if depth > 32 {
		return
	}
	for _, entry := range list {
		b, ok := entry.(map[string]any)
		if !ok {
			continue
		}
		visit(b)
		children, _ := b["children"].([]any)
		walkBlocks(children, depth+1, visit)
		columns, _ := b["columns"].([]any)
		for _, column := range columns {
			items, _ := column.([]any)
			walkBlocks(items, depth+1, visit)
		}
	}
}

func rawObjectBlocks(o Object) []any { blocks, _ := o.Properties["blocks"].([]any); return blocks }

func blockSearchText(block map[string]any) string {
	text, _ := block["text"].(string)
	parts := []string{text}
	appendRuns := func(raw any) {
		runs, _ := raw.([]any)
		for _, entry := range runs {
			run, _ := entry.(map[string]any)
			for _, key := range []string{"text", "latex"} {
				if value, ok := run[key].(string); ok {
					parts = append(parts, value)
				}
			}
		}
	}
	appendRuns(block["runs"])
	rows, _ := block["rows"].([]any)
	for _, entry := range rows {
		row, _ := entry.([]any)
		for _, entry := range row {
			cell, _ := entry.(map[string]any)
			appendRuns(cell["runs"])
		}
	}
	return strings.Join(parts, " ")
}

func (a *App) readBlocks(w http.ResponseWriter, r *http.Request) {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, err := a.store.Object(r.PathValue("id"))
	if err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	blocks := rawObjectBlocks(o)
	if blocks == nil {
		blocks = []any{}
	}
	jsonResponse(w, 200, map[string]any{"object_id": o.ID, "revision": o.Revision, "blocks": blocks})
}

func (a *App) findBlock(id, objectID string) (blockHit, error) {
	var hit blockHit
	count := 0
	scan := func(o Object) {
		walkBlocks(rawObjectBlocks(o), 0, func(b map[string]any) {
			if b["id"] == id {
				count++
				hit = blockHit{o.ID, o.Title, o.Revision, b}
			}
		})
	}
	if objectID != "" {
		o, err := a.store.Object(objectID)
		if err != nil {
			return hit, errNotFoundHTTP("Object not found")
		}
		scan(o)
	} else if err := a.store.ObjectBatches(100, func(batch []Object) error {
		for _, o := range batch {
			scan(o)
		}
		return nil
	}); err != nil {
		return hit, errPersistHTTP("Could not read blocks")
	}
	if count == 0 {
		return hit, errNotFoundHTTP("Block not found")
	}
	if count > 1 {
		return hit, &httpError{status: 409, message: "Block ID is ambiguous; specify object_id or repair duplicate IDs"}
	}
	return hit, nil
}

func (a *App) resolveBlock(w http.ResponseWriter, r *http.Request) {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	hit, err := a.findBlock(r.PathValue("block_id"), r.URL.Query().Get("object_id"))
	if err != nil {
		writeCoreError(w, err)
		return
	}
	jsonResponse(w, 200, hit)
}

func (a *App) searchBlocks(w http.ResponseWriter, r *http.Request) {
	limit := 50
	if raw := r.URL.Query().Get("limit"); raw != "" {
		n, err := strconv.Atoi(raw)
		if err != nil || n < 1 || n > 200 {
			jsonError(w, 400, "limit must be 1-200")
			return
		}
		limit = n
	}
	q := strings.ToLower(strings.TrimSpace(r.URL.Query().Get("q")))
	objectID := r.URL.Query().Get("object_id")
	hits := []blockHit{}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	err := a.store.ObjectBatches(100, func(batch []Object) error {
		for _, o := range batch {
			if isTemplateObject(o) || (objectID != "" && o.ID != objectID) {
				continue
			}
			walkBlocks(rawObjectBlocks(o), 0, func(b map[string]any) {
				text := blockSearchText(b)
				if len(hits) < limit && strings.Contains(strings.ToLower(text), q) {
					hits = append(hits, blockHit{o.ID, o.Title, o.Revision, b})
				}
			})
		}
		return nil
	})
	if err != nil {
		jsonError(w, 500, "Could not search blocks")
		return
	}
	jsonResponse(w, 200, map[string]any{"results": hits})
}

// References are live links, including references in table cells. Resolving a
// transclusion returns the source once; it never recursively follows a cycle.
func hasBlockHref(value any, id string) bool {
	switch v := value.(type) {
	case map[string]any:
		if href, ok := v["href"].(string); ok {
			u, err := url.Parse(href)
			if err == nil && u.Scheme == "lumen" && u.Host == "block" && strings.TrimPrefix(u.Path, "/") == id {
				return true
			}
		}
		for key, child := range v {
			if key != "children" && key != "columns" && hasBlockHref(child, id) {
				return true
			}
		}
	case []any:
		for _, child := range v {
			if hasBlockHref(child, id) {
				return true
			}
		}
	}
	return false
}

func (a *App) blockReferences(w http.ResponseWriter, r *http.Request) {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	id := r.PathValue("block_id")
	if _, err := a.findBlock(id, r.URL.Query().Get("object_id")); err != nil {
		writeCoreError(w, err)
		return
	}
	refs := []blockReference{}
	err := a.store.ObjectBatches(100, func(batch []Object) error {
		for _, o := range batch {
			walkBlocks(rawObjectBlocks(o), 0, func(b map[string]any) {
				if hasBlockHref(b, id) {
					bid, _ := b["id"].(string)
					refs = append(refs, blockReference{o.ID, o.Title, bid, b["type"] == "embed"})
				}
			})
		}
		return nil
	})
	if err != nil {
		jsonError(w, 500, "Could not read block references")
		return
	}
	jsonResponse(w, 200, map[string]any{"block_id": id, "count": len(refs), "references": refs})
}

// Normalize through the existing MCP block validator. Preserve extension fields
// and nested containers so REST edits cannot flatten future client formats.
func normalizeBlockJSON(raw map[string]any, fresh bool, depth int) (map[string]any, error) {
	if depth > 32 {
		return nil, errors.New("Blocks support at most 32 nested containers")
	}
	if raw == nil {
		return nil, errors.New("Provide a block")
	}
	out := cloneStringMap(raw)
	data, _ := json.Marshal(raw)
	var input mcpBlockInput
	if err := json.Unmarshal(data, &input); err != nil {
		return nil, errors.New("Invalid block fields")
	}
	var canonical map[string]any
	if input.Type == blockColumns {
		canonical = map[string]any{"id": newID(), "type": blockColumns, "text": "", "runs": []any{}}
		columns, ok := raw["columns"].([]any)
		if !ok || len(columns) == 0 || len(columns) > 8 {
			return nil, errors.New("A columns block needs 1-8 columns")
		}
	} else if (input.Type == blockParagraph || input.Type == blockSmall || input.Type == blockGroup) && input.Text == "" && len(input.Runs) == 0 {
		canonical = map[string]any{"id": newID(), "type": input.Type, "text": "", "runs": []any{}}
	} else {
		b, err := input.toBlock()
		if err != nil {
			return nil, err
		}
		data, _ = json.Marshal(b)
		_ = json.Unmarshal(data, &canonical)
	}
	for key, value := range canonical {
		out[key] = value
	}
	if !fresh {
		if id, ok := raw["id"].(string); ok && id != "" {
			out["id"] = id
		}
	}
	if children, exists := raw["children"]; exists {
		list, ok := children.([]any)
		if !ok {
			return nil, errors.New("children must be a block array")
		}
		normalized, err := normalizeBlockList(list, fresh, depth+1)
		if err != nil {
			return nil, err
		}
		out["children"] = normalized
	}
	if columns, exists := raw["columns"]; exists {
		list, ok := columns.([]any)
		if !ok {
			return nil, errors.New("columns must be arrays of blocks")
		}
		result := []any{}
		for _, col := range list {
			children, ok := col.([]any)
			if !ok {
				return nil, errors.New("Each column must be a block array")
			}
			normalized, err := normalizeBlockList(children, fresh, depth+1)
			if err != nil {
				return nil, err
			}
			result = append(result, normalized)
		}
		out["columns"] = result
	}
	return out, nil
}

func normalizeBlockList(list []any, fresh bool, depth int) ([]any, error) {
	result := []any{}
	for _, entry := range list {
		raw, ok := entry.(map[string]any)
		if !ok {
			return nil, errors.New("Each block must be an object")
		}
		b, err := normalizeBlockJSON(raw, fresh, depth)
		if err != nil {
			return nil, err
		}
		result = append(result, b)
	}
	return result, nil
}

func replaceBlockInTree(list []any, id string, replacement map[string]any, depth int) ([]any, int) {
	if depth > 32 {
		return list, 0
	}
	out := []any{}
	count := 0
	for _, entry := range list {
		b, ok := entry.(map[string]any)
		if !ok {
			out = append(out, entry)
			continue
		}
		if b["id"] == id {
			count++
			if replacement != nil {
				out = append(out, replacement)
			}
			continue
		}
		if children, ok := b["children"].([]any); ok {
			updated, n := replaceBlockInTree(children, id, replacement, depth+1)
			b["children"] = updated
			count += n
		}
		if columns, ok := b["columns"].([]any); ok {
			for i, col := range columns {
				children, _ := col.([]any)
				updated, n := replaceBlockInTree(children, id, replacement, depth+1)
				columns[i] = updated
				count += n
			}
		}
		out = append(out, b)
	}
	return out, count
}

func (a *App) editBlocks(w http.ResponseWriter, r *http.Request) {
	var req struct {
		BaseRevision uint64         `json:"base_revision"`
		Block        map[string]any `json:"block"`
		Position     *int           `json:"position"`
	}
	if !decodeLimit(w, r, &req, 8<<20) {
		return
	}
	if req.BaseRevision == 0 {
		jsonError(w, 400, "base_revision is required")
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, err := a.store.Object(r.PathValue("id"))
	if err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	if o.Revision != req.BaseRevision {
		jsonResponse(w, 409, map[string]any{"error": "conflict", "current": o})
		return
	}
	blocks := rawObjectBlocks(o)
	if len(blocks) == 0 && objectHasText(o) {
		jsonError(w, 409, "This object has a plain-text body; convert it before editing blocks")
		return
	}
	// A partial/stale block representation must not erase text that only
	// exists in the canonical body. Whitespace-only differences are harmless.
	if len(blocks) > 0 {
		typed, err := objectBlocks(o)
		if err != nil {
			jsonError(w, 409, "Could not read the stored block tree")
			return
		}
		body := strings.Join(strings.Fields(strings.ReplaceAll(o.Content, objectReplacement, "")), " ")
		projection := strings.Join(strings.Fields(blocksPlainText(typed)), " ")
		if body != "" && body != projection {
			jsonError(w, 409, "The body contains text outside the block tree; reconcile it before editing blocks")
			return
		}
	}
	var block map[string]any
	if r.Method != http.MethodDelete {
		block, err = normalizeBlockJSON(req.Block, r.Method == http.MethodPost, 0)
		if err != nil {
			jsonError(w, 400, err.Error())
			return
		}
	}
	if r.Method == http.MethodPost {
		pos := len(blocks)
		if req.Position != nil {
			pos = *req.Position
		}
		if pos < 0 || pos > len(blocks) {
			jsonError(w, 400, "position is outside the block list")
			return
		}
		blocks = append(blocks, nil)
		copy(blocks[pos+1:], blocks[pos:])
		blocks[pos] = block
	} else {
		if block != nil {
			block["id"] = r.PathValue("block_id")
		}
		var count int
		blocks, count = replaceBlockInTree(blocks, r.PathValue("block_id"), block, 0)
		if count != 1 {
			status := 404
			if count > 1 {
				status = 409
			}
			jsonError(w, status, "Block not found or ambiguous in this object")
			return
		}
	}
	ids := map[string]bool{}
	duplicate := false
	walkBlocks(blocks, 0, func(b map[string]any) {
		id, _ := b["id"].(string)
		if id == "" || ids[id] {
			duplicate = true
		}
		ids[id] = true
	})
	if duplicate {
		jsonError(w, 400, "Block IDs must be nonempty and unique within an object")
		return
	}
	if o.Properties == nil {
		o.Properties = map[string]any{}
	}
	o.Properties["blocks"] = blocks
	// Content is the derived plain-text projection of this edited block tree.
	data, _ := json.Marshal(blocks)
	var typed []Block
	if err = json.Unmarshal(data, &typed); err != nil {
		jsonError(w, 400, fmt.Sprintf("Invalid block tree: %v", err))
		return
	}
	o.Content = blocksPlainText(typed)
	delete(o.Properties, "rich_text")
	o.UpdatedAt = now()
	if err = a.validateObject(&o); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if err = a.saveObject(r.Context(), &o); err != nil {
		jsonError(w, 500, "Could not save blocks")
		return
	}
	status := 200
	if r.Method == http.MethodPost {
		status = 201
	}
	jsonResponse(w, status, map[string]any{"object": o, "block": block})
}
