package main

import "lumen/server/internal/document"

// purgeObjectData builds the permanent deletion transaction for a trashed
// object. The tombstone stays as the anti-resurrection marker; the trash
// payload and the live row disappear, inbound references are cleaned through
// the reverse index, and attachment ownership and blobs are resolved exactly as
// the old hard delete did.
func (a *App) purgeObjectData(t trashedObject) ([]Record, [][2]string, error) {
	id := t.Object.ID
	records := []Record{}
	var deleted tombstone
	if err := a.store.Get("tombstone", id, &deleted); err != nil {
		// Defensive: a trash entry should always have its tombstone. If it does
		// not, purge still leaves an anti-resurrection marker behind.
		records = append(records, Record{Kind: "tombstone", ID: id, Value: tombstone{
			ID: id, Revision: t.Object.Revision + 1, DeletedAt: t.DeletedAt,
		}})
	}
	deletes := [][2]string{{"trash", id}, {"versions", id}, {"receipts", id}}
	// Purging a template must also clear every default_template_id that points
	// at it, or the dangling reference would outlive the type registry check.
	if isTemplateObject(t.Object) {
		records = append(records, a.clearDefaultTemplateRecords(id)...)
	}

	assetIDs := map[string]bool{}
	assets, err := a.store.Assets()
	if err != nil {
		return nil, nil, err
	}
	isMedia := t.Object.TypeID == "image" || t.Object.TypeID == "pdf" || t.Object.TypeID == "audio" || t.Object.TypeID == "video" || t.Object.TypeID == "file"
	for _, asset := range assets {
		remove := false
		if asset.MediaObjectID == id || (isMedia && asset.ObjectID == id && asset.MediaObjectID == "") {
			remove = true
		}
		if !isMedia && asset.ObjectID == id && asset.MediaObjectID == "" {
			remove = true
		}
		if remove {
			assetIDs[asset.ID] = true
			deletes = append(deletes, [2]string{"asset", asset.ID}, [2]string{"blob", asset.ID})
			continue
		}
		// A promoted medium survives purge of the note that first attached it.
		// Reassign the asset metadata to the reusable media object.
		if !isMedia && asset.ObjectID == id && asset.MediaObjectID != "" {
			asset.ObjectID = asset.MediaObjectID
			records = append(records, Record{Kind: "asset", ID: asset.ID, Value: asset})
		}
	}

	changed := map[string]Object{}
	for _, otherID := range a.store.BacklinkIDs(id) {
		other, err := a.store.Object(otherID)
		if err != nil {
			continue
		}
		clean := make([]string, 0, len(other.Links))
		for _, link := range other.Links {
			if link != id {
				clean = append(clean, link)
			}
		}
		linksChanged := len(clean) != len(other.Links)
		other.Links = clean
		propertiesChanged := a.removePropertyReference(&other, id)
		documentChanged := removeCanonicalReferences(&other, map[string]string{id: t.Object.Title}, nil)
		if linksChanged || propertiesChanged || documentChanged {
			other.UpdatedAt = now()
			changed[other.ID] = other
		}
	}
	// Asset references are not object links, so they need a separate pass. It
	// only runs when this purge actually removes an attachment.
	if len(assetIDs) > 0 {
		objects, err := a.store.Objects()
		if err != nil {
			return nil, nil, err
		}
		for _, other := range objects {
			if other.ID == id {
				continue
			}
			if existing, ok := changed[other.ID]; ok {
				other = existing
			}
			if removeAssetReferences(&other, assetIDs) {
				other.UpdatedAt = now()
				changed[other.ID] = other
			}
		}
	}
	for _, other := range changed {
		records = append(records, a.prepareSave(&other)...)
	}
	return records, deletes, nil
}

func removeAssetReferences(o *Object, assetIDs map[string]bool) bool {
	if len(assetIDs) == 0 {
		return false
	}
	changed := removeCanonicalReferences(o, nil, assetIDs)
	for _, key := range []string{"image_asset_id", "asset_id"} {
		if id, ok := o.Properties[key].(string); ok && assetIDs[id] {
			delete(o.Properties, key)
			changed = true
		}
	}
	if raw, ok := o.Properties["assets"].([]any); ok {
		clean := make([]any, 0, len(raw))
		for _, item := range raw {
			if entry, ok := item.(map[string]any); ok {
				if id, _ := entry["id"].(string); assetIDs[id] {
					changed = true
					continue
				}
			}
			clean = append(clean, item)
		}
		o.Properties["assets"] = clean
	}
	return changed
}

// removeCanonicalReferences preserves readable labels and node identities while
// removing references to permanently deleted objects or media. Version snapshots
// remain immutable; callers sanitize a snapshot again when explicitly restoring.
func removeCanonicalReferences(o *Object, objectIDs map[string]string, assetIDs map[string]bool) bool {
	if o.Document == nil {
		return false
	}
	d := cloneDocument(o.Document)
	changed := false
	labelFor := func(id, label string) string {
		if label != "" {
			return label
		}
		if objectIDs[id] != "" {
			return objectIDs[id]
		}
		return "Objeto eliminado"
	}
	var visit func([]document.Node)
	visit = func(nodes []document.Node) {
		for i := range nodes {
			n := &nodes[i]
			for j, in := range n.Content {
				if _, ok := objectIDs[in.ObjectID]; in.ObjectID != "" && ok {
					n.Content[j] = document.Inline{Type: "text", Text: labelFor(in.ObjectID, in.Label), Marks: in.Marks}
					changed = true
				}
			}
			objectID, _ := n.Attrs["object_id"].(string)
			assetID, _ := n.Attrs["asset_id"].(string)
			if _, ok := objectIDs[objectID]; objectID != "" && ok {
				if n.Type == "object_embed" {
					*n = document.Node{ID: n.ID, Type: "paragraph", Content: []document.Inline{{Type: "text", Text: labelFor(objectID, "")}}, Children: n.Children}
				} else {
					delete(n.Attrs, "object_id")
				}
				changed = true
			}
			if assetIDs[assetID] && n.Type == "image" {
				caption, _ := n.Attrs["caption"].(string)
				*n = document.Node{ID: n.ID, Type: "paragraph", Children: n.Children}
				if caption != "" {
					n.Content = []document.Inline{{Type: "text", Text: caption}}
				}
				changed = true
			}
			visit(n.Children)
		}
	}
	visit(d.Blocks)
	if changed {
		o.Document = d
		o.Content = document.PlainText(*d)
	}
	return changed
}
