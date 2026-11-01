package main

import (
	"database/sql"
	"errors"
	"net/http"
	"strings"
)

// registerTrashRoutes mounts the trash API. Keeping these routes in their own
// file keeps the stage diff on main.go additive.
func (a *App) registerTrashRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /v1/trash", a.listTrash)
	mux.HandleFunc("DELETE /v1/trash/{id}", a.purgeTrash)
	mux.HandleFunc("POST /v1/trash/empty", a.emptyTrash)
	mux.HandleFunc("POST /v1/objects/{id}/restore", a.restoreObject)
}

func (a *App) listTrash(w http.ResponseWriter, r *http.Request) {
	entries, err := a.trashList(r.URL.Query().Get("q"), r.URL.Query().Get("type_id"))
	if err != nil {
		jsonError(w, 500, "Could not read the trash")
		return
	}
	jsonResponse(w, 200, entries)
}

func (a *App) restoreObject(w http.ResponseWriter, r *http.Request) {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, records, deletes, err := a.restoreObjectData(r.PathValue("id"))
	if errors.Is(err, sql.ErrNoRows) {
		jsonError(w, 404, "Object is not in the trash")
		return
	}
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if err = a.store.PutBatch(records, deletes); err != nil {
		jsonError(w, 500, "Could not restore object")
		return
	}
	jsonResponse(w, 200, o)
}

func (a *App) purgeTrash(w http.ResponseWriter, r *http.Request) {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	id := r.PathValue("id")
	var trashed trashedObject
	if err := a.store.Get("trash", id, &trashed); err != nil {
		jsonError(w, 404, "Object is not in the trash")
		return
	}
	records, deletes, err := a.purgeObjectData(trashed)
	if err != nil {
		jsonError(w, 500, "Could not read object relations")
		return
	}
	if err = a.store.PutBatch(records, deletes); err != nil {
		jsonError(w, 500, "Could not purge object")
		return
	}
	jsonResponse(w, 200, map[string]any{"purged": true, "id": id})
}

// emptyTrash purges one entry per transaction. A crash halfway leaves the rest
// recoverable in the trash, and a retry finishes the job; atomicity across the
// whole trash is not worth holding every payload in one commit.
func (a *App) emptyTrash(w http.ResponseWriter, r *http.Request) {
	typeID := strings.TrimSpace(r.URL.Query().Get("type_id"))
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	entries, err := a.store.TrashedObjects()
	if err != nil {
		jsonError(w, 500, "Could not read the trash")
		return
	}
	purged := 0
	for _, trashed := range entries {
		if typeID != "" && trashed.Object.TypeID != typeID {
			continue
		}
		records, deletes, err := a.purgeObjectData(trashed)
		if err != nil {
			jsonError(w, 500, "Could not read object relations")
			return
		}
		if err = a.store.PutBatch(records, deletes); err != nil {
			jsonError(w, 500, "Could not empty the trash")
			return
		}
		purged++
	}
	jsonResponse(w, 200, map[string]any{"purged": purged})
}
