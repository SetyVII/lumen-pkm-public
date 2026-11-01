package main

import (
	"net/http"
	"strings"
)

// registerWeblinkReaderRoutes keeps this stage additive: the shared router
// gains one registration call instead of an edited route block.
func (a *App) registerWeblinkReaderRoutes(mux *http.ServeMux) {
	mux.HandleFunc("POST /v1/objects/{id}/refresh-reader", a.refreshReader)
}

// refreshReader re-fetches a saved weblink and replaces its extracted article.
// The fetch happens before writeMu so a slow public site cannot block unrelated
// writes; the object is re-read under the lock to detect edits made meanwhile.
func (a *App) refreshReader(w http.ResponseWriter, r *http.Request) {
	id := r.PathValue("id")
	o, err := a.store.Object(id)
	if err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	if o.TypeID != "web" {
		jsonError(w, 404, "Object is not a web link")
		return
	}
	rawURL, _ := o.Properties["url"].(string)
	rawURL = strings.TrimSpace(rawURL)
	if rawURL == "" {
		jsonError(w, 400, "Web link has no URL")
		return
	}
	article, err := a.fetcher.Reader(r.Context(), rawURL)
	if err != nil {
		jsonError(w, 502, "Could not fetch the page; the saved reader content was kept")
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	current, err := a.store.Object(id)
	if err != nil {
		jsonError(w, 404, "Object not found")
		return
	}
	if current.TypeID != "web" {
		jsonError(w, 404, "Object is not a web link")
		return
	}
	currentURL, _ := current.Properties["url"].(string)
	if strings.TrimSpace(currentURL) != rawURL {
		jsonResponse(w, 409, map[string]any{"error": "conflict", "conflict": true, "current": current})
		return
	}
	if current.Properties == nil {
		current.Properties = map[string]any{}
	}
	current.Properties["reader_content"] = article.Content
	if article.Favicon != "" {
		current.Properties["favicon"] = article.Favicon
	}
	current.UpdatedAt = now()
	if err := a.saveObject(r.Context(), &current); err != nil {
		jsonError(w, 500, "Could not persist reader content")
		return
	}
	jsonResponse(w, 200, current)
}
