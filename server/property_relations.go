package main

import (
	"errors"
	"fmt"
	"math"
	"strings"
)

func schemaKey(schema map[string]any) string {
	for _, field := range []string{"key", "id", "name"} {
		if value, ok := schema[field].(string); ok && value != "" {
			return value
		}
	}
	return ""
}

func (a *App) validateTypeReferences(typ ObjectType) error {
	for _, raw := range typ.Properties {
		schema, ok := raw.(map[string]any)
		if !ok {
			continue
		}
		if value, exists := schema["target_type"]; exists {
			targetID, ok := value.(string)
			if !ok {
				return errors.New("Property target_type must be an object type ID")
			}
			if targetID == "" || targetID == typ.ID {
				continue
			}
			if _, err := a.store.Type(targetID); err != nil {
				return errors.New("Property target_type does not exist")
			}
		}
	}
	return nil
}

func (a *App) validatePropertyValues(o *Object, typ ObjectType) error {
	return a.validatePropertyValuesReferences(o, typ, false)
}

func (a *App) validatePropertyValuesReferences(o *Object, typ ObjectType, allowPending bool) error {
	// is_template is reserved on every type, including legacy schemas that have
	// not been normalized yet. Reject non-booleans even when the schema lacks
	// the definition, so the flag cannot silently become a truthy string.
	if value, exists := o.Properties["is_template"]; exists && value != nil {
		if _, ok := value.(bool); !ok {
			return errors.New("Property is_template must be a boolean")
		}
	}
	refs := []string{}
	seen := map[string]bool{}
	for _, raw := range typ.Properties {
		schema, ok := raw.(map[string]any)
		if !ok {
			continue
		} // pre-existing name-only properties are untyped
		key := schemaKey(schema)
		if reservedPropertyKey(key) {
			// Default properties are validated by the server (aliases.go), not
			// by the type schema that happens to declare the same key.
			continue
		}
		value, exists := o.Properties[key]
		if !exists || value == nil || value == "" {
			continue
		}
		kind, _ := schema["type"].(string)
		invalid := func() error { return fmt.Errorf("Property %s must contain a valid %s value", key, kind) }
		switch kind {
		case "text":
			if _, ok := value.(string); !ok {
				return invalid()
			}
		case "number":
			number, ok := value.(float64)
			if !ok || math.IsNaN(number) || math.IsInf(number, 0) {
				return invalid()
			}
			if min, ok := schema["min"].(float64); ok && number < min {
				return invalid()
			}
			if max, ok := schema["max"].(float64); ok && number > max {
				return invalid()
			}
		case "boolean":
			if _, ok := value.(bool); !ok {
				return invalid()
			}
		case "date":
			// A template stores the placeholder until it is applied. The new
			// object is validated again after substitution, so an unknown or
			// non-ISO format still fails at creation time.
			if o != nil && isTemplateObject(*o) {
				if s, ok := value.(string); ok && strings.Contains(s, "{date") {
					break
				}
			}
			// Legacy plain strings and the extended {start,end} range are both
			// valid; see contracts/SYNC.md.
			if validateDatePropertyValue(value) != nil {
				return invalid()
			}
		case "select":
			selected, ok := value.(string)
			if !ok {
				return invalid()
			}
			if options, ok := schema["options"].([]any); ok && len(options) > 0 {
				found := false
				for _, option := range options {
					if option == selected {
						found = true
						break
					}
				}
				if !found {
					return fmt.Errorf("Property %s must use a configured option", key)
				}
			}
		case "object":
			ids := []string{}
			switch v := value.(type) {
			case string:
				ids = append(ids, v)
			case []any:
				for _, entry := range v {
					id, ok := entry.(string)
					if !ok {
						return invalid()
					}
					ids = append(ids, id)
				}
			default:
				return invalid()
			}
			for _, id := range ids {
				if !validObjectReferenceID(id) {
					return invalid()
				}
				target := *o
				targetExists := true
				if id != o.ID {
					var err error
					target, targetExists, err = a.referenceTarget(id)
					if err != nil {
						return fmt.Errorf("Could not read property %s reference: %w", key, err)
					}
					if !targetExists && !allowPending {
						return fmt.Errorf("Property %s references an object that does not exist", key)
					}
				}
				if targetType, _ := schema["target_type"].(string); targetExists && targetType != "" && target.TypeID != targetType {
					return fmt.Errorf("Property %s requires objects of type %s", key, targetType)
				}
				if !seen[id] && id != o.ID {
					refs = append(refs, id)
					seen[id] = true
				}
			}
		}
	}
	o.PropertyLinks = refs
	return nil
}

// recomputePropertyLinks derives the relation list from the current type
// schema without validating user values. It is used when a schema changes:
// an object may keep a value that is no longer a valid relation, but the
// derived backlink list must stop advertising it.
func (a *App) recomputePropertyLinks(o *Object, typ ObjectType) {
	refs := []string{}
	seen := map[string]bool{}
	for _, raw := range typ.Properties {
		schema, ok := raw.(map[string]any)
		if !ok || schema["type"] != "object" {
			continue
		}
		key := schemaKey(schema)
		if reservedPropertyKey(key) {
			continue
		}
		value, exists := o.Properties[key]
		if !exists || value == nil || value == "" {
			continue
		}
		ids := []string{}
		switch v := value.(type) {
		case string:
			ids = append(ids, v)
		case []any:
			for _, entry := range v {
				if id, ok := entry.(string); ok {
					ids = append(ids, id)
				}
			}
		}
		targetType, _ := schema["target_type"].(string)
		for _, id := range ids {
			if id == "" || id == o.ID || seen[id] {
				continue
			}
			target, err := a.store.Object(id)
			if err != nil {
				trashed, trashErr := a.store.TrashedObject(id)
				if trashErr != nil {
					continue
				}
				target = trashed.Object
			}
			if targetType != "" && target.TypeID != targetType {
				continue
			}
			seen[id] = true
			refs = append(refs, id)
		}
	}
	o.PropertyLinks = refs
}

func allObjectLinks(o Object) []string {
	out := make([]string, 0, len(o.Links)+len(o.PropertyLinks))
	seen := map[string]bool{}
	for _, group := range [][]string{o.Links, o.PropertyLinks, o.DocumentLinks} {
		for _, id := range group {
			if id != "" && id != o.ID && !seen[id] {
				out = append(out, id)
				seen[id] = true
			}
		}
	}
	return out
}

func (a *App) removePropertyReference(o *Object, id string) bool {
	changed := false
	clean := make([]string, 0, len(o.PropertyLinks))
	for _, link := range o.PropertyLinks {
		if link == id {
			changed = true
		} else {
			clean = append(clean, link)
		}
	}
	o.PropertyLinks = clean
	typ, err := a.store.Type(o.TypeID)
	if err != nil {
		return changed
	}
	for _, raw := range typ.Properties {
		schema, ok := raw.(map[string]any)
		if !ok || schema["type"] != "object" {
			continue
		}
		key := schemaKey(schema)
		if reservedPropertyKey(key) {
			continue
		}
		switch value := o.Properties[key].(type) {
		case string:
			if value == id {
				o.Properties[key] = nil
				changed = true
			}
		case []any:
			values := make([]any, 0, len(value))
			for _, entry := range value {
				if entry == id {
					changed = true
				} else {
					values = append(values, entry)
				}
			}
			o.Properties[key] = values
		}
	}
	return changed
}
