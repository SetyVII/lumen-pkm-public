package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"strings"
	"unicode"
	"unicode/utf8"
)

// Field intents let a catalog-backed UI change one property without replacing
// values it never fetched. The caller already holds writeMu and checked CAS.
func (a *App) applyPropertyIntents(o *Object, patch map[string]json.RawMessage) (json.RawMessage, error) {
	updatesRaw, hasUpdates := patch["property_updates"]
	removesRaw, hasRemoves := patch["remove_properties"]
	if !hasUpdates && !hasRemoves {
		return patch["properties"], nil
	}
	if _, full := patch["properties"]; full {
		return nil, errors.New("Use property_updates/remove_properties or properties replacement, not both")
	}
	updates := map[string]any{}
	removes := []string{}
	if hasUpdates && (json.Unmarshal(updatesRaw, &updates) != nil || updates == nil) {
		return nil, errors.New("property_updates must be an object")
	}
	if hasRemoves && (json.Unmarshal(removesRaw, &removes) != nil || removes == nil) {
		return nil, errors.New("remove_properties must be an array of property keys")
	}
	if len(updates)+len(removes) > 256 {
		return nil, errors.New("At most 256 property field intents are allowed")
	}
	validateKey := func(key string) error {
		if key == "" || len(key) > 256 || !utf8.ValidString(key) {
			return errors.New("Invalid property key")
		}
		for _, r := range key {
			if unicode.IsControl(r) {
				return errors.New("Invalid property key")
			}
		}
		switch key {
		case "assets", "reader_content", "favicon":
			return fmt.Errorf("Property %s is managed by the server", key)
		case "blocks", "rich_text":
			return errors.New("Edit the canonical document instead of legacy format properties")
		}
		return nil
	}
	explicit := map[string]any{}
	for key, value := range updates {
		if err := validateKey(key); err != nil {
			return nil, err
		}
		explicit[key] = value
	}
	for _, key := range removes {
		if err := validateKey(key); err != nil {
			return nil, err
		}
		if _, alsoSet := updates[key]; alsoSet {
			return nil, fmt.Errorf("Property %s cannot be set and removed together", key)
		}
		explicit[key] = nil
	}
	if cover, exists := updates["image_asset_id"]; exists && cover != nil {
		id, ok := cover.(string)
		if !ok {
			return nil, errors.New("image_asset_id must be an image asset ID or null")
		}
		if id != "" {
			var asset Asset
			if err := a.store.Get("asset", id, &asset); err != nil || !strings.HasPrefix(asset.MIME, "image/") {
				return nil, errors.New("image_asset_id requires an existing image asset")
			}
		}
	}
	if o.Properties == nil {
		o.Properties = map[string]any{}
	}
	for key, value := range updates {
		o.Properties[key] = value
	}
	for _, key := range removes {
		delete(o.Properties, key)
	}
	return json.Marshal(explicit)
}
