package main

import (
	"database/sql"
	"errors"
	"lumen/server/internal/document"
	"sort"
	"strconv"
	"strings"
)

// trashedObject is the encrypted side record that preserves a full object while
// it sits in the trash. The tombstone stays small because it is part of the
// mobile sync payload; the payload itself is never synced and is decrypted only
// when the trash list or a restore needs it.
type trashedObject struct {
	Object    Object `json:"object"`
	DeletedAt string `json:"deleted_at"`
}

// trashEntry is the public, metadata-only view of a trashed object.
type trashEntry struct {
	ID        string `json:"id"`
	TypeID    string `json:"type_id"`
	Title     string `json:"title"`
	DeletedAt string `json:"deleted_at"`
}

// trashObjectData moves a live object to the trash. The live row disappears so
// every existing listing, search, picker and backlink path stops seeing it
// without extra filters. The tombstone keeps the anti-resurrection guarantee
// and the mobile deletion signal; the side record keeps the payload for
// restore. Inbound references are intentionally left untouched: the target is
// simply not resolvable until the object is restored.
func (a *App) trashObjectData(id string) ([]Record, [][2]string, tombstone, error) {
	o, err := a.store.Object(id)
	if err != nil {
		return nil, nil, tombstone{}, err
	}
	deletedAt := now()
	deleted := tombstone{ID: id, Revision: o.Revision + 1, DeletedAt: deletedAt}
	records := []Record{
		{Kind: "tombstone", ID: id, Value: deleted},
		{Kind: "trash", ID: id, Value: trashedObject{Object: o, DeletedAt: deletedAt}},
	}
	// Trashing a template must also clear every default_template_id that
	// points at it: a default may not reference a deleted object. Restoring
	// the template does not re-set the reference (documented degraded case).
	if isTemplateObject(o) {
		records = append(records, a.clearDefaultTemplateRecords(id)...)
	}
	return records, [][2]string{{"object", id}}, deleted, nil
}

// restoreObjectData rebuilds the live object from its trash payload. The
// tombstone revision is the floor for the new revision, so a restored object is
// always newer than the deletion and a stale mobile push cannot take it over
// through the tombstone path. The original incarnation is preserved: restore
// brings back the same object, it is not a recreation.
func (a *App) restoreObjectData(id string) (Object, []Record, [][2]string, error) {
	var trashed trashedObject
	if err := a.store.Get("trash", id, &trashed); err != nil {
		return Object{}, nil, nil, err
	}
	o := trashed.Object
	o.ID = id
	if err := a.prepareRestoredObject(&o); err != nil {
		return Object{}, nil, nil, err
	}
	var deleted tombstone
	if err := a.store.Get("tombstone", id, &deleted); err == nil && deleted.Revision > o.Revision {
		o.Revision = deleted.Revision
	}
	o.Revision++
	o.UpdatedAt = now()
	o.SyncStatus, o.SyncError = "synced", ""
	records := []Record{{Kind: "object", ID: id, Value: o}}
	deletes := [][2]string{{"trash", id}, {"tombstone", id}}
	return o, records, deletes, nil
}

// prepareRestoredObject makes a stored trash payload safe to publish again. It
// drops references to objects that were permanently purged while this object
// was in the trash, removes attachments that no longer exist, resolves title
// collisions and rebuilds derived relations. It is deliberately lenient about
// unrelated scalar properties: schema edits do not silently rewrite them.
// Object relations retain pending IDs and validate every known target type.
func (a *App) prepareRestoredObject(o *Object) error {
	typ, err := a.store.Type(o.TypeID)
	if err != nil {
		return errors.New("Unknown object type")
	}
	o.Title = strings.TrimSpace(o.Title)
	if o.Title == "" {
		o.Title = "Untitled"
	}
	o.Title = a.uniqueRestoreTitle(o.Title, o.ID)
	if o.Properties == nil {
		o.Properties = map[string]any{}
	}
	if err := a.filterPurgedReferences(o); err != nil {
		return err
	}
	if err := a.sanitizeRestoredAssets(o); err != nil {
		return err
	}
	if err := a.validateMediaCreation(o, true); err != nil {
		return err
	}
	if err := a.normalizeObjectDocument(o); err != nil {
		return err
	}
	return a.validateRestoredRelations(o, typ)
}

// referenceExists reports whether a relation target can still be resolved,
// either because it is live or because it is in the trash and can be restored.
func (a *App) referenceExists(id string) bool {
	return a.store.Has("object", id) || a.store.Has("trash", id)
}

// sanitizeRestoredAssets removes attachment metadata whose blob was purged
// while the owner was in the trash. A dangling cover would otherwise survive
// the restore and fail on the next download.
func (a *App) sanitizeRestoredAssets(o *Object) error {
	cache := map[string]bool{}
	missing := func(id string) (bool, error) {
		if id == "" {
			return false, nil
		}
		if value, ok := cache[id]; ok {
			return value, nil
		}
		var asset Asset
		err := a.store.Get("asset", id, &asset)
		if err != nil && !errors.Is(err, sql.ErrNoRows) {
			return false, err
		}
		absent := errors.Is(err, sql.ErrNoRows)
		cache[id] = absent
		return absent, nil
	}
	if o.Document != nil {
		missingAssets := map[string]bool{}
		for _, ref := range document.References(*o.Document) {
			if ref.AssetID == "" {
				continue
			}
			absent, err := missing(ref.AssetID)
			if err != nil {
				return err
			}
			if absent {
				missingAssets[ref.AssetID] = true
			}
		}
		removeCanonicalReferences(o, nil, missingAssets)
	}
	for _, key := range []string{"image_asset_id", "asset_id"} {
		id, _ := o.Properties[key].(string)
		if o.Properties["media_state"] == "upload_pending" && id != "" && id == mediaSourceID(*o) {
			continue
		}
		absent, err := missing(id)
		if err != nil {
			return err
		}
		if absent {
			delete(o.Properties, key)
		}
	}
	raw, ok := o.Properties["assets"].([]any)
	if !ok {
		return nil
	}
	clean := make([]any, 0, len(raw))
	for _, item := range raw {
		if entry, ok := item.(map[string]any); ok {
			id, _ := entry["id"].(string)
			absent, err := missing(id)
			if err != nil {
				return err
			}
			if absent {
				continue
			}
		}
		clean = append(clean, item)
	}
	o.Properties["assets"] = clean
	return nil
}

// uniqueRestoreTitle keeps workspace titles unique. A trashed title is free
// while the object is in the trash, so another object may have taken it; the
// restored object gets a numbered suffix instead of failing the restore.
func (a *App) uniqueRestoreTitle(title, id string) string {
	if owner := a.store.TitleOwner(title); owner == "" || owner == id {
		return title
	}
	base := title
	if runes := []rune(base); len(runes) > 990 {
		base = string(runes[:990])
	}
	for suffix := 2; ; suffix++ {
		candidate := base + " (" + strconv.Itoa(suffix) + ")"
		if owner := a.store.TitleOwner(candidate); owner == "" || owner == id {
			return candidate
		}
	}
}

// trashList returns the public trash view, filtered and ordered by deletion
// time descending. Trash is expected to be small; payloads are decrypted on
// demand and never enter the in-memory index.
func (a *App) trashList(q, typeID string) ([]trashEntry, error) {
	entries, err := a.store.TrashedObjects()
	if err != nil {
		return nil, err
	}
	q = strings.ToLower(strings.TrimSpace(q))
	typeID = strings.TrimSpace(typeID)
	out := make([]trashEntry, 0, len(entries))
	for _, trashed := range entries {
		if typeID != "" && trashed.Object.TypeID != typeID {
			continue
		}
		if q != "" && !strings.Contains(strings.ToLower(trashed.Object.Title+" "+trashed.Object.Content), q) {
			continue
		}
		out = append(out, trashEntry{
			ID: trashed.Object.ID, TypeID: trashed.Object.TypeID,
			Title: trashed.Object.Title, DeletedAt: trashed.DeletedAt,
		})
	}
	sort.SliceStable(out, func(i, j int) bool {
		if out[i].DeletedAt != out[j].DeletedAt {
			return out[i].DeletedAt > out[j].DeletedAt
		}
		return out[i].ID < out[j].ID
	})
	return out, nil
}

// saveCleanup returns the deletion markers that a successful save supersedes.
// Recreating an object (for example a fixed-id journal) must not leave a stale
// trash payload behind.
func saveCleanup(id string) [][2]string {
	return [][2]string{{"tombstone", id}, {"trash", id}}
}
