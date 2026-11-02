package main

import (
	"errors"
	"fmt"
	"strings"
)

// Collections are ordinary objects of the built-in `collection` type. Their
// curated list lives in properties.items, which is an ordered object relation:
// the array order is authoritative, while PropertyLinks/backlinks expose the
// same items as a derived set. Keeping the list in a normal property means
// mobile sync, deletion cleanup and the encrypted store need no special case.

func collectionItemType(o *Object) string {
	if o == nil || o.Properties == nil {
		return ""
	}
	value, _ := o.Properties["item_type_id"].(string)
	return strings.TrimSpace(value)
}

// collectionItemIDs normalizes the stored items value. A single string is
// accepted for symmetry with every other object property, but collections
// always write back a JSON array so clients can rely on the shape.
func collectionItemIDs(value any) ([]string, error) {
	switch v := value.(type) {
	case nil:
		return []string{}, nil
	case string:
		id := strings.TrimSpace(v)
		if id == "" {
			return []string{}, nil
		}
		return []string{id}, nil
	case []string:
		out := make([]string, 0, len(v))
		for _, entry := range v {
			id := strings.TrimSpace(entry)
			if id == "" {
				return nil, errors.New("Collection items must be object IDs")
			}
			out = append(out, id)
		}
		return out, nil
	case []any:
		out := make([]string, 0, len(v))
		for _, entry := range v {
			id, ok := entry.(string)
			if !ok {
				return nil, errors.New("Collection items must be object IDs")
			}
			id = strings.TrimSpace(id)
			if id == "" {
				return nil, errors.New("Collection items must be object IDs")
			}
			out = append(out, id)
		}
		return out, nil
	default:
		return nil, errors.New("Collection items must be an array of object IDs")
	}
}

func collectionIDsValue(ids []string) []any {
	out := make([]any, len(ids))
	for i, id := range ids {
		out[i] = id
	}
	return out
}

// validateCollectionTypeSchema protects the two structural properties of the
// built-in collection type. Renaming labels, colors or the tags property stays
// allowed, but removing item_type_id/items would break validation, backlinks
// and deletion cleanup for every existing collection.
func validateCollectionTypeSchema(typ ObjectType) error {
	hasItemType, hasItems := false, false
	for _, raw := range typ.Properties {
		schema, ok := raw.(map[string]any)
		if !ok {
			continue
		}
		switch schemaKey(schema) {
		case "item_type_id":
			hasItemType = schema["type"] == "text"
		case "items":
			hasItems = schema["type"] == "object" && schema["multiple"] == true
		}
	}
	if !hasItemType || !hasItems {
		return errors.New("The collection type must keep item_type_id (text) and items (object, multiple)")
	}
	return nil
}

// validateCollectionItem enforces the cross-object invariants of one item.
// Trashed objects are checked explicitly because the trash stage keeps a
// tombstone for anti-resurrection even when the object row is gone.
func (a *App) validateCollectionItem(itemTypeID, id string) error {
	item, err := a.store.Object(id)
	if err != nil {
		return errors.New("Collection item does not exist")
	}
	if a.isDeleted(id) {
		return errors.New("Collection items cannot be trashed")
	}
	if item.TypeID != itemTypeID {
		return fmt.Errorf("Collection items must be of type %s", itemTypeID)
	}
	return nil
}

func (a *App) validateCollection(o *Object) error {
	itemTypeID := collectionItemType(o)
	if itemTypeID == "" {
		return errors.New("A collection needs item_type_id")
	}
	if itemTypeID == "collection" {
		return errors.New("A collection cannot contain collections")
	}
	if _, err := a.store.Type(itemTypeID); err != nil {
		return errors.New("Collection item_type_id must reference an existing type")
	}
	if o.ID != "" {
		var previous Object
		if err := a.store.Get("object", o.ID, &previous); err == nil && previous.TypeID == "collection" {
			if stored := collectionItemType(&previous); stored != "" && stored != itemTypeID {
				return errors.New("Collection item_type_id cannot change after creation")
			}
		}
	}
	o.Properties["item_type_id"] = itemTypeID
	ids, err := collectionItemIDs(o.Properties["items"])
	if err != nil {
		return err
	}
	seen := make(map[string]bool, len(ids))
	for _, id := range ids {
		if seen[id] {
			return errors.New("A collection cannot contain the same object twice")
		}
		seen[id] = true
		if id == o.ID {
			return errors.New("A collection cannot contain itself")
		}
		if err := a.validateCollectionItem(itemTypeID, id); err != nil {
			return err
		}
	}
	o.Properties["items"] = collectionIDsValue(ids)
	return nil
}
