package main

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
	deletes := [][2]string{{"trash", id}, {"versions", id}}
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
		if linksChanged || propertiesChanged {
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
	if len(assetIDs) == 0 || o.Properties == nil {
		return false
	}
	changed := false
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
