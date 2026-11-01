package main

import (
	"net/http"
	"strings"
)

// registerConvertMergeRoutes mounts type conversion and object merge. Keeping
// these routes in their own file keeps the stage diff on main.go additive.
func (a *App) registerConvertMergeRoutes(mux *http.ServeMux) {
	mux.HandleFunc("POST /v1/objects/{id}/convert", a.convertObject)
	mux.HandleFunc("POST /v1/objects/{id}/merge", a.mergeObject)
}

// convertObject changes an object's type. The object keeps its id, title,
// content and links; only type_id and the property document change. Values that
// cannot move are appended to the content instead of being dropped.
func (a *App) convertObject(w http.ResponseWriter, r *http.Request) {
	var req convertRequest
	if !decode(w, r, &req) {
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	id := r.PathValue("id")
	o, err := a.store.Object(id)
	if err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	if req.BaseRevision > 0 && req.BaseRevision != o.Revision {
		jsonResponse(w, 409, map[string]any{"error": "conflict", "conflict": true, "current": o})
		return
	}
	converted, err := a.convertObjectData(o, req)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	// A collection holds exactly one type. Removing the object from collections
	// whose item_type_id no longer matches keeps the curated list truthful and
	// editable; leaving a stale item would make every later collection write
	// fail validation. The cleanup commits with the conversion.
	records := a.prepareSave(&converted)
	records = append(records, a.convertCollectionCleanup(converted)...)
	// A type must never keep pointing at an object that stopped being a
	// template of that type. Conversion is a type change, so it runs the same
	// cleanup as PATCH and sync push.
	records = append(records, a.templateDefaultCleanup(o, converted)...)
	if err := a.store.PutBatch(records, saveCleanup(converted.ID)); err != nil {
		jsonError(w, 500, "Could not persist encrypted object")
		return
	}
	jsonResponse(w, 200, converted)
}

// mergeObject merges the object in the URL (the source) into target_id (the
// target). The source moves to the trash, so the operation stays reversible.
func (a *App) mergeObject(w http.ResponseWriter, r *http.Request) {
	var req mergeRequest
	if !decode(w, r, &req) {
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	sourceID := r.PathValue("id")
	if a.store.Has("trash", sourceID) {
		jsonError(w, 400, "Object is in the trash and cannot be merged")
		return
	}
	source, err := a.store.Object(sourceID)
	if err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	targetID := strings.TrimSpace(req.TargetID)
	if targetID == "" {
		jsonError(w, 400, "target_id is required")
		return
	}
	if targetID == sourceID {
		jsonError(w, 400, "An object cannot be merged into itself")
		return
	}
	if a.store.Has("trash", targetID) {
		jsonError(w, 400, "Target object is in the trash")
		return
	}
	target, err := a.store.Object(targetID)
	if err != nil {
		jsonError(w, 400, "Target object not found")
		return
	}
	if source.TypeID != target.TypeID {
		jsonError(w, 400, "Both objects must have the same type")
		return
	}
	// base_revision guards the object in the URL (the source), exactly like
	// PATCH on the same path. target_base_revision optionally guards the object
	// that survives and is returned.
	if req.BaseRevision > 0 && req.BaseRevision != source.Revision {
		jsonResponse(w, 409, map[string]any{"error": "conflict", "conflict": true, "current": source})
		return
	}
	if req.TargetBaseRevision > 0 && req.TargetBaseRevision != target.Revision {
		jsonResponse(w, 409, map[string]any{"error": "conflict", "conflict": true, "current": target})
		return
	}
	merged, records, deletes, err := a.mergeObjectData(source, target)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if err := a.store.PutBatch(records, deletes); err != nil {
		jsonError(w, 500, "Could not persist merge")
		return
	}
	jsonResponse(w, 200, merged)
}
