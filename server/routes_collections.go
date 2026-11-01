package main

import (
	"net/http"
	"strings"
)

// Collection item endpoints. Collections are created and renamed through the
// generic object API; these routes only mutate the curated list so clients do
// not have to resend the whole properties document to add or move one item.

type collectionItemsResponse struct {
	CollectionID string   `json:"collection_id"`
	ItemTypeID   string   `json:"item_type_id"`
	ItemIDs      []string `json:"item_ids"`
	Items        []Object `json:"items"`
}

func (a *App) registerCollectionRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /v1/collections/{id}/items", a.listCollectionItems)
	mux.HandleFunc("POST /v1/collections/{id}/items", a.addCollectionItem)
	mux.HandleFunc("PUT /v1/collections/{id}/items", a.replaceCollectionItems)
	mux.HandleFunc("DELETE /v1/collections/{id}/items/{object_id}", a.removeCollectionItem)
}

// loadCollection writes the HTTP error itself so every handler reports the
// same distinction: missing object (404) versus an object of another type (400).
func (a *App) loadCollection(w http.ResponseWriter, id string) (*Object, bool) {
	o, err := a.store.Object(id)
	if err != nil {
		jsonError(w, 404, "Collection not found")
		return nil, false
	}
	if o.TypeID != "collection" {
		jsonError(w, 400, "Object is not a collection")
		return nil, false
	}
	return &o, true
}

func (a *App) listCollectionItems(w http.ResponseWriter, r *http.Request) {
	c, ok := a.loadCollection(w, r.PathValue("id"))
	if !ok {
		return
	}
	ids, err := collectionItemIDs(c.Properties["items"])
	if err != nil {
		jsonError(w, 500, "Stored collection items are invalid")
		return
	}
	// Deletion cleanup removes items transactionally. Skipping a missing or
	// trashed ID is defense in depth for data written by an older or interrupted
	// version; it must never turn a read into a 500.
	items := make([]Object, 0, len(ids))
	for _, id := range ids {
		item, err := a.store.Object(id)
		if err != nil || a.isDeleted(id) {
			continue
		}
		items = append(items, item)
	}
	jsonResponse(w, 200, collectionItemsResponse{
		CollectionID: c.ID,
		ItemTypeID:   collectionItemType(c),
		ItemIDs:      ids,
		Items:        items,
	})
}

func (a *App) addCollectionItem(w http.ResponseWriter, r *http.Request) {
	var req struct {
		ObjectID string `json:"object_id"`
		Position *int   `json:"position"`
	}
	if !decode(w, r, &req) {
		return
	}
	req.ObjectID = strings.TrimSpace(req.ObjectID)
	if req.ObjectID == "" {
		jsonError(w, 400, "object_id is required")
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	c, ok := a.loadCollection(w, r.PathValue("id"))
	if !ok {
		return
	}
	ids, err := collectionItemIDs(c.Properties["items"])
	if err != nil {
		jsonError(w, 500, "Stored collection items are invalid")
		return
	}
	for _, id := range ids {
		if id == req.ObjectID {
			jsonError(w, 409, "Object is already in this collection")
			return
		}
	}
	position := len(ids)
	if req.Position != nil {
		position = *req.Position
	}
	if position < 0 || position > len(ids) {
		jsonError(w, 400, "position is out of range")
		return
	}
	ids = append(ids, "")
	copy(ids[position+1:], ids[position:])
	ids[position] = req.ObjectID
	c.Properties["items"] = collectionIDsValue(ids)
	c.UpdatedAt = now()
	// validateObject re-derives PropertyLinks, so the new item becomes a
	// backlink in the same transaction as the collection write.
	if err := a.validateObject(c); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if err := a.saveObject(r.Context(), c); err != nil {
		jsonError(w, 500, "Could not persist collection")
		return
	}
	jsonResponse(w, 200, c)
}

func (a *App) replaceCollectionItems(w http.ResponseWriter, r *http.Request) {
	var req struct {
		Items *[]string `json:"items"`
	}
	if !decode(w, r, &req) {
		return
	}
	if req.Items == nil {
		jsonError(w, 400, "items is required")
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	c, ok := a.loadCollection(w, r.PathValue("id"))
	if !ok {
		return
	}
	current, err := collectionItemIDs(c.Properties["items"])
	if err != nil {
		jsonError(w, 500, "Stored collection items are invalid")
		return
	}
	ids := make([]string, 0, len(*req.Items))
	for _, id := range *req.Items {
		ids = append(ids, strings.TrimSpace(id))
	}
	if sameStrings(current, ids) {
		jsonResponse(w, 200, c)
		return
	}
	c.Properties["items"] = collectionIDsValue(ids)
	c.UpdatedAt = now()
	if err := a.validateObject(c); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if err := a.saveObject(r.Context(), c); err != nil {
		jsonError(w, 500, "Could not persist collection")
		return
	}
	jsonResponse(w, 200, c)
}

func (a *App) removeCollectionItem(w http.ResponseWriter, r *http.Request) {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	c, ok := a.loadCollection(w, r.PathValue("id"))
	if !ok {
		return
	}
	ids, err := collectionItemIDs(c.Properties["items"])
	if err != nil {
		jsonError(w, 500, "Stored collection items are invalid")
		return
	}
	target := r.PathValue("object_id")
	position := -1
	for i, id := range ids {
		if id == target {
			position = i
			break
		}
	}
	if position < 0 {
		jsonError(w, 404, "Object is not in this collection")
		return
	}
	ids = append(ids[:position], ids[position+1:]...)
	c.Properties["items"] = collectionIDsValue(ids)
	c.UpdatedAt = now()
	if err := a.validateObject(c); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if err := a.saveObject(r.Context(), c); err != nil {
		jsonError(w, 500, "Could not persist collection")
		return
	}
	jsonResponse(w, 200, c)
}
