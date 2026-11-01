package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"reflect"
	"regexp"
	"strings"
)

var propertyKey = regexp.MustCompile(`^[A-Za-z_][A-Za-z0-9_.-]{0,63}$`)
var typeColor = regexp.MustCompile(`^#[0-9A-Fa-f]{6}$`)
var typeIcon = regexp.MustCompile(`^[A-Za-z0-9_.-]{1,100}$`)

func propertyDefinitionKey(raw any) string {
	switch value := raw.(type) {
	case string:
		return value
	case map[string]any:
		return schemaKey(value)
	}
	return ""
}

// ensureTemplateProperty makes is_template a reserved, canonical definition on
// every type. A client may send its own definition (or a legacy name-only key);
// the server replaces it with the shared boolean so clients always see the same
// schema and cannot redefine the flag.
func ensureTemplateProperty(properties []any) []any {
	out := make([]any, 0, len(properties)+1)
	inserted := false
	for _, raw := range properties {
		if propertyDefinitionKey(raw) == "is_template" {
			if !inserted {
				out = append(out, cloneJSONValue(isTemplateProperty))
				inserted = true
			}
			continue
		}
		out = append(out, raw)
	}
	if !inserted {
		out = append(out, cloneJSONValue(isTemplateProperty))
	}
	return out
}

func validateType(typ *ObjectType) error {
	typ.Name = strings.TrimSpace(typ.Name)
	if typ.Name == "" || len([]rune(typ.Name)) > 100 {
		return errors.New("A type needs a name of up to 100 characters")
	}
	if !typeColor.MatchString(typ.Color) {
		return errors.New("Type color must be #RRGGBB")
	}
	if !typeIcon.MatchString(typ.Icon) {
		return errors.New("Type icon must be a theme icon name of up to 100 characters")
	}
	if typ.Properties == nil {
		typ.Properties = []any{}
	}
	// The reserved is_template definition does not count toward the 64-property
	// limit, so existing schemas at the limit keep working after the upgrade.
	typ.Properties = ensureTemplateProperty(typ.Properties)
	customProperties := 0
	for _, raw := range typ.Properties {
		if propertyDefinitionKey(raw) != "is_template" {
			customProperties++
		}
	}
	if customProperties > 64 {
		return errors.New("A type supports up to 64 properties")
	}
	seen := map[string]bool{}
	for _, raw := range typ.Properties {
		var key string
		switch schema := raw.(type) {
		case string: // compatibility with author/status/date in existing types
			key = schema
		case map[string]any:
			name, _ := schema["name"].(string)
			key = schemaKey(schema)
			if len([]rune(name)) > 100 {
				return errors.New("Property labels support up to 100 characters")
			}
			kind, _ := schema["type"].(string)
			switch kind {
			case "text", "number", "date", "boolean", "select", "object":
			default:
				return errors.New("Property type must be text, number, date, boolean, select or object")
			}
			if rawOptions, exists := schema["options"]; exists {
				options, ok := rawOptions.([]any)
				if !ok || len(options) > 100 {
					return errors.New("Property options must be an array of up to 100 strings")
				}
				optionSeen := map[string]bool{}
				for _, rawOption := range options {
					option, ok := rawOption.(string)
					if !ok || strings.TrimSpace(option) == "" || len([]rune(option)) > 100 || optionSeen[option] {
						return errors.New("Property options must be distinct nonempty strings of up to 100 characters")
					}
					optionSeen[option] = true
				}
			}
		default:
			return errors.New("Properties must be names or schema objects")
		}
		if !propertyKey.MatchString(key) || seen[key] {
			return fmt.Errorf("Property key %q must be unique and use letters, numbers, underscore, dot or dash (up to 64 characters)", key)
		}
		seen[key] = true
	}
	if raw, err := json.Marshal(typ); err != nil || len(raw) > 32*1024 {
		return errors.New("Type schema exceeds 32 KB")
	}
	return nil
}

func (a *App) updateType(w http.ResponseWriter, r *http.Request) {
	var patch map[string]json.RawMessage
	if !decode(w, r, &patch) {
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	var typ ObjectType
	if err := a.store.Get("type", r.PathValue("id"), &typ); err != nil {
		jsonError(w, 404, "Object type not found")
		return
	}
	for key, raw := range patch {
		var target any
		switch key {
		case "name":
			target = &typ.Name
		case "color":
			target = &typ.Color
		case "icon":
			target = &typ.Icon
		case "properties":
			target = &typ.Properties
		case "default_template_id":
			target = &typ.DefaultTemplateID
		default:
			jsonError(w, 400, "Only name, color, icon, properties and default_template_id can be changed")
			return
		}
		if err := json.Unmarshal(raw, target); err != nil {
			jsonError(w, 400, "Invalid type field: "+key)
			return
		}
	}
	// JSON null is a common way to clear an optional reference. Unmarshalling
	// null into a string is a no-op, so handle it explicitly.
	if raw, ok := patch["default_template_id"]; ok && strings.TrimSpace(string(raw)) == "null" {
		typ.DefaultTemplateID = ""
	}
	typ.DefaultTemplateID = strings.TrimSpace(typ.DefaultTemplateID)
	if err := validateType(&typ); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if typ.ID == "collection" {
		if err := validateCollectionTypeSchema(typ); err != nil {
			jsonError(w, 400, err.Error())
			return
		}
	}
	if err := a.validateTypeReferences(typ); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if err := a.validateDefaultTemplate(typ); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	// Editing a schema never removes notes or rewrites their stored properties.
	// Relations are derived state, so objects of this type are updated in the
	// same transaction when the set of object properties changes.
	records := []Record{{Kind: "type", ID: typ.ID, Value: typ}}
	objects, err := a.store.ObjectsByType(typ.ID)
	if err != nil {
		jsonError(w, 500, "Could not read objects for the type")
		return
	}
	for _, o := range objects {
		if o.TypeID != typ.ID {
			continue
		}
		before := append([]string{}, o.PropertyLinks...)
		a.recomputePropertyLinks(&o, typ)
		if reflect.DeepEqual(before, o.PropertyLinks) {
			continue
		}
		o.UpdatedAt = now()
		records = append(records, a.prepareSave(&o)...)
	}
	if err := a.store.PutBatch(records, nil); err != nil {
		jsonError(w, 500, "Could not save object type")
		return
	}
	jsonResponse(w, 200, typ)
}
