package main

import (
	"database/sql"
	"encoding/json"
	"errors"
	"io"
	"lumen/server/internal/document"
	"net/http"
	"strconv"
)

type objectVersionSummary struct {
	Revision  uint64 `json:"revision"`
	Timestamp string `json:"timestamp"`
	WordCount int    `json:"word_count"`
	Bytes     int    `json:"bytes"`
}

type objectVersionResponse struct {
	Document      *document.Document `json:"document"`
	DocumentLinks []string           `json:"document_links"`
	ObjectID      string             `json:"object_id"`
	Revision      uint64             `json:"revision"`
	Timestamp     string             `json:"timestamp"`
	TypeID        string             `json:"type_id"`
	Title         string             `json:"title"`
	Content       string             `json:"content"`
	Properties    map[string]any     `json:"properties"`
	Links         []string           `json:"links"`
	WordCount     int                `json:"word_count"`
	Bytes         int                `json:"bytes"`
}

func (a *App) registerVersionHistoryRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /v1/objects/{id}/versions", a.listObjectVersions)
	mux.HandleFunc("GET /v1/objects/{id}/versions/{revision}", a.getObjectVersion)
	mux.HandleFunc("POST /v1/objects/{id}/restore/{revision}", a.restoreObjectVersion)
}

func (a *App) listObjectVersions(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	if _, err := a.store.Object(id); err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	versions, err := a.store.ListVersions(id)
	if err != nil {
		jsonError(w, 500, "Could not read object versions")
		return
	}
	out := make([]objectVersionSummary, 0, len(versions))
	for _, version := range versions {
		out = append(out, objectVersionSummary{
			Revision:  version.Revision,
			Timestamp: versionTimestamp(version),
			WordCount: versionWordCount(version.Content),
			Bytes:     versionBytes(version.Content),
		})
	}
	jsonResponse(w, 200, out)
}

func (a *App) getObjectVersion(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	revision, ok := parseRevision(w, r.PathValue("revision"))
	if !ok {
		return
	}
	if _, err := a.store.Object(id); err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	version, err := a.store.Version(id, revision)
	if errors.Is(err, sql.ErrNoRows) {
		jsonError(w, 404, "Version not found")
		return
	}
	if err != nil {
		jsonError(w, 500, "Could not read object version")
		return
	}
	properties := version.Properties
	if properties == nil {
		properties = map[string]any{}
	}
	links := version.Links
	if links == nil {
		links = []string{}
	}
	jsonResponse(w, 200, objectVersionResponse{
		ObjectID:      version.ObjectID,
		Document:      cloneDocument(version.Document),
		DocumentLinks: append([]string{}, version.DocumentLinks...),
		Revision:      version.Revision,
		Timestamp:     versionTimestamp(version),
		TypeID:        version.TypeID,
		Title:         version.Title,
		Content:       version.Content,
		Properties:    properties,
		Links:         links,
		WordCount:     versionWordCount(version.Content),
		Bytes:         versionBytes(version.Content),
	})
}

func (a *App) restoreObjectVersion(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	revision, ok := parseRevision(w, r.PathValue("revision"))
	if !ok {
		return
	}
	var request struct {
		BaseRevision uint64 `json:"base_revision"`
	}
	decoder := json.NewDecoder(http.MaxBytesReader(w, r.Body, 1<<20))
	if err := decoder.Decode(&request); err != nil && !errors.Is(err, io.EOF) {
		jsonError(w, 400, "Invalid JSON body")
		return
	} else if err == nil {
		var extra any
		if decoder.Decode(&extra) != io.EOF {
			jsonError(w, 400, "Only one JSON document is allowed")
			return
		}
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	var current Object
	if err := a.store.Get("object", id, &current); err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	// A deleted object (trashed or purged) must be restored through the
	// trash flow, not through version history.
	if a.isDeleted(id) {
		jsonError(w, 404, "Object not found")
		return
	}
	if request.BaseRevision > 0 && request.BaseRevision != current.Revision {
		jsonResponse(w, 409, map[string]any{"error": "conflict", "conflict": true, "current": current})
		return
	}
	version, err := a.store.Version(id, revision)
	if errors.Is(err, sql.ErrNoRows) {
		jsonError(w, 404, "Version not found")
		return
	}
	if err != nil {
		jsonError(w, 500, "Could not read object version")
		return
	}
	restored := cloneObject(current)
	restored.Content = version.Content
	restored.Document = cloneDocument(version.Document)
	restored.DocumentLinks = append([]string(nil), version.DocumentLinks...)
	restored.Properties = cloneStringMap(version.Properties)
	restored.Links = append([]string{}, version.Links...)
	if err := a.filterPurgedReferences(&restored); err != nil {
		jsonError(w, 500, "Could not inspect restored references")
		return
	}
	if err := a.sanitizeRestoredAssets(&restored); err != nil {
		jsonError(w, 500, "Could not inspect restored assets")
		return
	}
	if err := a.validateMediaCreation(&restored, true); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	restored.UpdatedAt = now()
	if err := a.validateObjectReferences(&restored, true); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if err := a.saveObject(r.Context(), &restored); err != nil {
		jsonError(w, 500, "Could not persist restored version")
		return
	}
	jsonResponse(w, 200, restored)
}

func parseRevision(w http.ResponseWriter, raw string) (uint64, bool) {
	revision, err := strconv.ParseUint(raw, 10, 64)
	if err != nil {
		jsonError(w, 400, "Revision must be a non-negative integer")
		return 0, false
	}
	return revision, true
}
