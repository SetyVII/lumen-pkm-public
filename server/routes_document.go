package main

import (
	"errors"
	"lumen/server/internal/document"
	"net/http"
)

func (a *App) registerDocumentRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /v1/objects/{id}/document", a.readDocument)
	mux.HandleFunc("PUT /v1/objects/{id}/document", a.writeDocument)
	mux.HandleFunc("POST /v1/objects/{id}/document/commands", a.writeDocument)
}

func (a *App) readDocument(w http.ResponseWriter, r *http.Request) {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, err := a.store.Object(r.PathValue("id"))
	if err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	if o.Document == nil {
		jsonError(w, 409, "Object has no canonical document")
		return
	}
	jsonResponse(w, 200, map[string]any{"object_id": o.ID, "revision": o.Revision, "document": o.Document})
}

func (a *App) writeDocument(w http.ResponseWriter, r *http.Request) {
	var req struct {
		BaseRevision uint64             `json:"base_revision"`
		OpID         string             `json:"op_id"`
		Document     *document.Document `json:"document"`
		Commands     []document.Command `json:"commands"`
	}
	if !decodeLimit(w, r, &req, document.MaxBytes+(1<<20)) {
		return
	}
	if r.Method == http.MethodPut && req.Document == nil {
		jsonError(w, 400, "document is required")
		return
	}
	if r.Method == http.MethodPost && (req.Document != nil || len(req.Commands) == 0) {
		jsonError(w, 400, "Provide a nonempty commands array only")
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, err := a.commitDocument(r.PathValue("id"), req.BaseRevision, req.OpID, req.Document, req.Commands)
	if err != nil {
		var httpErr *httpError
		if errors.As(err, &httpErr) && httpErr.status == 409 && o.ID != "" {
			jsonResponse(w, 409, map[string]any{"error": "conflict", "current": o})
			return
		}
		writeCoreError(w, err)
		return
	}
	jsonResponse(w, 200, o)
}
