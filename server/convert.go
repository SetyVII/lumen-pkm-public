package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"sort"
	"strconv"
	"strings"
	"time"
)


// propertySchema is the declared shape of one property of a type. A legacy
// property declared as a bare string has an empty Kind: the store does not
// validate it, so conversion falls back to the JSON shape of the value.
type propertySchema struct {
	Key        string
	Label      string
	Kind       string
	TargetType string
	Multiple   bool
	Options    []string
	Min        *float64
	Max        *float64
}

// propertySchemasOf indexes a type schema by property key. Later entries with
// the same key cannot exist (validateType rejects duplicates), so the map is
// unambiguous.
func propertySchemasOf(typ ObjectType) map[string]propertySchema {
	schemas := make(map[string]propertySchema, len(typ.Properties))
	for _, raw := range typ.Properties {
		switch value := raw.(type) {
		case string:
			schemas[value] = propertySchema{Key: value, Label: value}
		case map[string]any:
			key := schemaKey(value)
			if key == "" {
				continue
			}
			schema := propertySchema{Key: key, Label: key}
			if name, ok := value["name"].(string); ok && strings.TrimSpace(name) != "" {
				schema.Label = name
			}
			schema.Kind, _ = value["type"].(string)
			schema.TargetType, _ = value["target_type"].(string)
			schema.Multiple, _ = value["multiple"].(bool)
			if options, ok := value["options"].([]any); ok {
				for _, option := range options {
					if text, ok := option.(string); ok {
						schema.Options = append(schema.Options, text)
					}
				}
			}
			if number, ok := value["min"].(float64); ok {
				schema.Min = &number
			}
			if number, ok := value["max"].(float64); ok {
				schema.Max = &number
			}
			schemas[key] = schema
		}
	}
	return schemas
}

// valueFitsSchema mirrors the write-time validation of validatePropertyValues
// for one target property. Conversion uses it to decide whether a value can
// move; a value that does not fit is written into the content instead of being
// dropped or making the converted object unsavable.
func (a *App) valueFitsSchema(schema propertySchema, value any) bool {
	if value == nil {
		return false
	}
	switch schema.Kind {
	case "":
		// Legacy untyped property: the store accepts any JSON value.
		return true
	case "text":
		_, ok := value.(string)
		return ok
	case "number":
		number, ok := value.(float64)
		if !ok || math.IsNaN(number) || math.IsInf(number, 0) {
			return false
		}
		if schema.Min != nil && number < *schema.Min {
			return false
		}
		if schema.Max != nil && number > *schema.Max {
			return false
		}
		return true
	case "boolean":
		_, ok := value.(bool)
		return ok
	case "date":
		date, ok := value.(string)
		if !ok {
			return false
		}
		parsed, err := time.Parse("2006-01-02", date)
		return err == nil && parsed.Format("2006-01-02") == date
	case "select":
		selected, ok := value.(string)
		if !ok {
			return false
		}
		if len(schema.Options) == 0 {
			return true
		}
		for _, option := range schema.Options {
			if option == selected {
				return true
			}
		}
		return false
	case "object":
		ids, ok := objectIDList(value)
		if !ok {
			return false
		}
		// A list cannot be squeezed into a single-value relation; the caller
		// writes it into the content instead.
		if !schema.Multiple && len(ids) > 1 {
			return false
		}
		for _, id := range ids {
			if id == "" || strings.TrimSpace(id) != id {
				return false
			}
			target, err := a.relationTarget(id)
			if err != nil {
				return false
			}
			if schema.TargetType != "" && target.TypeID != schema.TargetType {
				return false
			}
		}
		return true
	}
	return false
}

// normalizeForSchema adapts the stored shape to the target property so a
// converted object looks like one written through the normal API: a single
// relation stores a string, a multiple relation stores a JSON array.
func normalizeForSchema(schema propertySchema, value any) any {
	if schema.Kind != "object" {
		return value
	}
	ids, ok := objectIDList(value)
	if !ok {
		return value
	}
	if schema.Multiple {
		if _, isString := value.(string); isString {
			return []any{ids[0]}
		}
		if _, isStrings := value.([]string); isStrings {
			out := make([]any, 0, len(ids))
			for _, id := range ids {
				out = append(out, id)
			}
			return out
		}
		return value
	}
	if len(ids) == 1 {
		return ids[0]
	}
	return value
}

// objectIDList decodes the two shapes an object relation can have on disk: a
// single id string or a JSON array of id strings.
func objectIDList(value any) ([]string, bool) {
	switch list := value.(type) {
	case string:
		return []string{list}, true
	case []string:
		return append([]string(nil), list...), true
	case []any:
		ids := make([]string, 0, len(list))
		for _, entry := range list {
			id, ok := entry.(string)
			if !ok {
				return nil, false
			}
			ids = append(ids, id)
		}
		return ids, true
	default:
		return nil, false
	}
}

// relationTarget resolves a relation target that is either live or in the
// trash. A trashed target is restorable, so it is a valid relation value (the
// same rule the write path uses).
func (a *App) relationTarget(id string) (Object, error) {
	if o, err := a.store.Object(id); err == nil {
		return o, nil
	}
	trashed, err := a.store.TrashedObject(id)
	if err != nil {
		return Object{}, err
	}
	return trashed.Object, nil
}

func isEmptyPropertyValue(value any) bool {
	switch typed := value.(type) {
	case nil:
		return true
	case string:
		return strings.TrimSpace(typed) == ""
	case []any:
		return len(typed) == 0
	case []string:
		return len(typed) == 0
	case map[string]any:
		return len(typed) == 0
	}
	return false
}

func scalarText(value any) string {
	switch typed := value.(type) {
	case nil:
		return ""
	case string:
		return typed
	case float64:
		return strconv.FormatFloat(typed, 'f', -1, 64)
	case bool:
		return strconv.FormatBool(typed)
	default:
		raw, err := json.Marshal(typed)
		if err != nil {
			return fmt.Sprint(typed)
		}
		return string(raw)
	}
}

// relationLabel renders an object relation by title, falling back to the id
// when the target no longer resolves. The text fallback is for a human reading
// the converted note; it is not a machine-readable relation.
func (a *App) relationLabel(id string) string {
	if o, err := a.store.Object(id); err == nil && strings.TrimSpace(o.Title) != "" {
		return o.Title
	}
	if trashed, err := a.store.TrashedObject(id); err == nil && strings.TrimSpace(trashed.Object.Title) != "" {
		return trashed.Object.Title
	}
	return id
}

func (a *App) relationText(ids []string) string {
	labels := make([]string, 0, len(ids))
	for _, id := range ids {
		labels = append(labels, a.relationLabel(id))
	}
	return strings.Join(labels, ", ")
}

// propertyValueText serializes a value that cannot move so it can be appended
// to the content. Lists are joined readably; object relations use titles.
func (a *App) propertyValueText(schema propertySchema, value any) string {
	switch typed := value.(type) {
	case []string:
		if schema.Kind == "object" {
			return a.relationText(typed)
		}
		return strings.Join(typed, ", ")
	case []any:
		if schema.Kind == "object" {
			if ids, ok := objectIDList(typed); ok {
				return a.relationText(ids)
			}
		}
		parts := make([]string, 0, len(typed))
		for _, entry := range typed {
			parts = append(parts, scalarText(entry))
		}
		return strings.Join(parts, ", ")
	default:
		return scalarText(value)
	}
}

func sortedStringKeys[V any](values map[string]V) []string {
	keys := make([]string, 0, len(values))
	for key := range values {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}

type convertRequest struct {
	TargetTypeID string            `json:"target_type_id"`
	PropertyMap  map[string]string `json:"property_map"`
	BaseRevision uint64            `json:"base_revision"`
}

// convertObjectData builds the converted object without persisting it. The
// caller holds writeMu and commits it through saveObject, which bumps the
// revision and appends the mobile sync journal entry.
func (a *App) convertObjectData(o Object, req convertRequest) (Object, error) {
	targetTypeID := strings.TrimSpace(req.TargetTypeID)
	if targetTypeID == "" {
		return o, errors.New("target_type_id is required")
	}
	if targetTypeID == o.TypeID {
		return o, errors.New("The object already has this type")
	}
	targetType, err := a.store.Type(targetTypeID)
	if err != nil {
		return o, errors.New("Unknown object type")
	}
	sourceType, err := a.store.Type(o.TypeID)
	if err != nil {
		return o, errors.New("Unknown object type")
	}
	sourceSchemas := propertySchemasOf(sourceType)
	targetSchemas := propertySchemasOf(targetType)

	mapped := make(map[string]string, len(req.PropertyMap))
	for sourceKey, targetKey := range req.PropertyMap {
		sourceKey = strings.TrimSpace(sourceKey)
		targetKey = strings.TrimSpace(targetKey)
		if sourceKey == "" || targetKey == "" {
			return o, errors.New("property_map entries must name a source and a target property")
		}
		if reservedPropertyKey(sourceKey) || reservedPropertyKey(targetKey) {
			return o, errors.New("aliases is a default property available on every type and cannot be remapped")
		}
		if _, declared := sourceSchemas[sourceKey]; !declared {
			if _, present := o.Properties[sourceKey]; !present {
				return o, fmt.Errorf("property_map source %q is not a property of this object", sourceKey)
			}
		}
		if _, declared := targetSchemas[targetKey]; !declared {
			return o, fmt.Errorf("property_map target %q is not a property of type %s", targetKey, targetTypeID)
		}
		mapped[sourceKey] = targetKey
	}

	properties := make(map[string]any, len(o.Properties))
	filled := make(map[string]string, len(mapped))
	leftovers := []string{}
	leftover := func(key string, value any) {
		leftovers = append(leftovers, "Property "+key+": "+a.propertyValueText(sourceSchemas[key], value))
	}

	// Explicit mappings run first and reserve their target: the user's choice
	// wins over the automatic same-name rule. A second source mapped onto the
	// same target is written into the content instead of overwriting it.
	for _, sourceKey := range sortedStringKeys(mapped) {
		value, exists := o.Properties[sourceKey]
		if !exists || isEmptyPropertyValue(value) {
			continue
		}
		targetKey := mapped[sourceKey]
		if owner, taken := filled[targetKey]; taken && owner != sourceKey {
			leftover(sourceKey, value)
			continue
		}
		schema := targetSchemas[targetKey]
		if !a.valueFitsSchema(schema, value) {
			leftover(sourceKey, value)
			continue
		}
		properties[targetKey] = normalizeForSchema(schema, value)
		filled[targetKey] = sourceKey
	}

	for _, key := range sortedStringKeys(o.Properties) {
		value := o.Properties[key]
		if _, explicit := mapped[key]; explicit {
			continue
		}
		if key == aliasesPropertyKey {
			// aliases is a default property of every object, independent of
			// the type schema, so it always travels with the object.
			if !isEmptyPropertyValue(value) {
				properties[key] = value
			}
			continue
		}
		if isEmptyPropertyValue(value) {
			continue
		}
		if schema, declared := targetSchemas[key]; declared {
			if _, taken := filled[key]; taken {
				leftover(key, value)
				continue
			}
			if !a.valueFitsSchema(schema, value) {
				leftover(key, value)
				continue
			}
			properties[key] = normalizeForSchema(schema, value)
			continue
		}
		if _, declared := sourceSchemas[key]; declared {
			leftover(key, value)
			continue
		}
		// A key declared by neither type is hidden metadata (summary, assets,
		// rich_text, mobile_conflict...). It is not a typed property, so it is
		// carried over untouched instead of being serialized into the content.
		properties[key] = value
	}

	o.TypeID = targetTypeID
	o.Properties = properties
	if len(leftovers) > 0 {
		block := strings.Join(leftovers, "\n")
		if o.Document != nil {
			appendPlainText(&o, block)
			o.UpdatedAt = now()
			if err := a.validateObject(&o); err != nil {
				return o, err
			}
			return o, nil
		}
		if strings.TrimSpace(o.Content) == "" {
			o.Content = block
		} else {
			o.Content = strings.TrimRight(o.Content, "\n") + "\n\n" + block
		}
		// The rich representations describe the old body. Keeping them would
		// let a client render a view that hides the migrated values.
		delete(o.Properties, "blocks")
		delete(o.Properties, "rich_text")
	}
	o.UpdatedAt = now()
	if err := a.validateObject(&o); err != nil {
		return o, err
	}
	return o, nil
}

// convertCollectionCleanup removes the converted object from collections whose
// item_type_id no longer matches its new type. Collections are curated lists of
// exactly one type; a stale item would both render a wrong-type card and make
// every later collection write fail validateCollection. The cleanup mirrors the
// deletion path (removePropertyReference preserves the order of the remaining
// items) and is committed in the same transaction as the conversion.
func (a *App) convertCollectionCleanup(o Object) []Record {
	records := []Record{}
	for _, otherID := range a.store.BacklinkIDs(o.ID) {
		other, err := a.store.Object(otherID)
		if err != nil || other.TypeID != "collection" {
			continue
		}
		ids, err := collectionItemIDs(other.Properties["items"])
		if err != nil {
			continue
		}
		member := false
		for _, id := range ids {
			if id == o.ID {
				member = true
				break
			}
		}
		if !member || collectionItemType(&other) == o.TypeID {
			continue
		}
		if a.removePropertyReference(&other, o.ID) {
			other.UpdatedAt = now()
			records = append(records, a.prepareSave(&other)...)
		}
	}
	return records
}
