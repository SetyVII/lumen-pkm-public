package main

import "encoding/json"

// Attachment metadata is owned by upload/promote, not by a document snapshot.
// A late autosave can replace editable properties while retaining the latest
// committed attachments and cover unless the cover is explicitly updated/cleared.
// The caller holds writeMu and has already loaded the current object.
func (a *App) preserveManagedAssets(o *Object, previous map[string]any, rawProperties json.RawMessage) {
	if o.Properties == nil {
		o.Properties = map[string]any{}
	}
	if value, exists := previous["assets"]; exists {
		o.Properties["assets"] = value
	} else {
		delete(o.Properties, "assets")
	}
	var explicit map[string]json.RawMessage
	_ = json.Unmarshal(rawProperties, &explicit)
	if rawCover, hasCoverKey := explicit["image_asset_id"]; hasCoverKey {
		var coverID *string
		if json.Unmarshal(rawCover, &coverID) == nil {
			if coverID == nil || *coverID == "" {
				delete(o.Properties, "image_asset_id")
			} else {
				var asset Asset
				if a.store.Get("asset", *coverID, &asset) == nil {
					o.Properties["image_asset_id"] = *coverID
				} else if prev, ok := previous["image_asset_id"]; ok {
					o.Properties["image_asset_id"] = prev
				} else {
					delete(o.Properties, "image_asset_id")
				}
			}
		}
	} else if value, exists := previous["image_asset_id"]; exists {
		o.Properties["image_asset_id"] = value
	} else {
		delete(o.Properties, "image_asset_id")
	}
	seen := map[string]bool{}
	for _, id := range o.Links {
		seen[id] = true
	}
	if assets, ok := o.Properties["assets"].([]any); ok {
		for _, raw := range assets {
			asset, ok := raw.(map[string]any)
			if !ok {
				continue
			}
			id, _ := asset["media_object_id"].(string)
			if id == "" || id == o.ID || seen[id] {
				continue
			}
			if _, err := a.store.Object(id); err != nil {
				continue // explicitly deleted media must never be resurrected
			}
			o.Links = append(o.Links, id)
			seen[id] = true
		}
	}
}
