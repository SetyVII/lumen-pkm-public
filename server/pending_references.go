package main

import (
	"bytes"
	"database/sql"
	"encoding/base64"
	"encoding/json"
	"errors"
	"io"
	"net/http"
	"sort"
	"unicode"
	"unicode/utf8"
)

func validObjectReferenceID(id string) bool {
	if len(id) == 0 || len(id) > 128 || !utf8.ValidString(id) {
		return false
	}
	for _, r := range id {
		if unicode.IsControl(r) || unicode.IsSpace(r) {
			return false
		}
	}
	return true
}

// referenceTarget distinguishes a target not uploaded yet from a broken read.
// Trash payloads remain type-checkable and restorable reference targets.
func (a *App) referenceTarget(id string) (Object, bool, error) {
	target, err := a.store.Object(id)
	if err == nil {
		return target, true, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return Object{}, false, err
	}
	trash, err := a.store.TrashedObject(id)
	if err == nil {
		return trash.Object, true, nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return Object{}, false, err
	}
	return Object{}, false, nil
}

func (a *App) purgedReference(id string) (bool, error) {
	var marker tombstone
	if err := a.store.Get("tombstone", id, &marker); err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return false, nil
		}
		return false, err
	}
	_, exists, err := a.referenceTarget(id)
	return !exists, err
}

// referenceTypeIssue records a pending relation whose target has now arrived
// with an incompatible type. Both objects remain intact for explicit repair.
type referenceTypeIssue struct {
	SourceObjectID string `json:"source_object_id"`
	PropertyKey    string `json:"property_key"`
	TargetObjectID string `json:"target_object_id"`
	ExpectedType   string `json:"expected_type"`
	ActualType     string `json:"actual_type"`
}

const referenceIssuePageSize = 200

type referenceIssueCursor struct {
	Source   string `json:"source"`
	Property int    `json:"property"`
}

func encodeReferenceIssueCursor(source string, property int) string {
	raw, _ := json.Marshal(referenceIssueCursor{source, property})
	return base64.RawURLEncoding.EncodeToString(raw)
}

func (a *App) referenceTypeIssues(target Object, cursor string) ([]referenceTypeIssue, string, error) {
	position := referenceIssueCursor{}
	if cursor != "" {
		if len(cursor) > 1024 {
			return nil, "", &httpError{400, "Invalid reference issue cursor"}
		}
		raw, err := base64.RawURLEncoding.DecodeString(cursor)
		if err != nil {
			return nil, "", &httpError{400, "Invalid reference issue cursor"}
		}
		decoder := json.NewDecoder(bytes.NewReader(raw))
		decoder.DisallowUnknownFields()
		if err = decoder.Decode(&position); err != nil || position.Property < 0 || !validObjectReferenceID(position.Source) {
			return nil, "", &httpError{400, "Invalid reference issue cursor"}
		}
		if err = decoder.Decode(&struct{}{}); err != io.EOF {
			return nil, "", &httpError{400, "Invalid reference issue cursor"}
		}
	}
	ids := a.store.BacklinkIDs(target.ID)
	sort.Strings(ids)
	start := sort.SearchStrings(ids, position.Source)
	end := min(start+referenceIssuePageSize, len(ids))
	issues := []referenceTypeIssue{}
	for _, id := range ids[start:end] {
		source, err := a.store.Object(id)
		if errors.Is(err, sql.ErrNoRows) {
			continue
		}
		if err != nil {
			return nil, "", err
		}
		typ, err := a.store.Type(source.TypeID)
		if err != nil {
			return nil, "", err
		}
		for propertyIndex, raw := range typ.Properties {
			if id == position.Source && propertyIndex < position.Property {
				continue
			}
			schema, ok := raw.(map[string]any)
			if !ok || schema["type"] != "object" {
				continue
			}
			expected, _ := schema["target_type"].(string)
			if expected == "" || expected == target.TypeID {
				continue
			}
			key := schemaKey(schema)
			values, _ := toStrings(source.Properties[key])
			if value, ok := source.Properties[key].(string); ok {
				values = []string{value}
			}
			for _, value := range values {
				if value == target.ID {
					issues = append(issues, referenceTypeIssue{source.ID, key, target.ID, expected, target.TypeID})
					if len(issues) >= referenceIssuePageSize {
						return issues, encodeReferenceIssueCursor(id, propertyIndex+1), nil
					}
					break
				}
			}
		}
	}
	next := ""
	if end < len(ids) {
		next = encodeReferenceIssueCursor(ids[end], 0)
	}
	return issues, next, nil
}

func (a *App) readReferenceIssues(w http.ResponseWriter, r *http.Request) {
	target, err := a.store.Object(r.PathValue("id"))
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			jsonError(w, 404, "Object not found")
		} else {
			jsonError(w, 500, "Could not read reference target")
		}
		return
	}
	issues, next, err := a.referenceTypeIssues(target, r.URL.Query().Get("cursor"))
	if err != nil {
		var apiErr *httpError
		if errors.As(err, &apiErr) {
			writeCoreError(w, err)
		} else {
			jsonError(w, 500, "Could not check object reference types")
		}
		return
	}
	jsonResponse(w, 200, map[string]any{"object_id": target.ID, "issues": issues, "next_cursor": next})
}

// Trash restore remains lenient about unrelated scalar values after schema
// changes, but never silently discards a pending relation or validates a known
// incompatible target as if it were still unresolved.
func (a *App) validateRestoredRelations(o *Object, typ ObjectType) error {
	references := typ
	references.Properties = nil
	for _, raw := range typ.Properties {
		if schema, ok := raw.(map[string]any); ok && schema["type"] == "object" {
			references.Properties = append(references.Properties, raw)
		}
	}
	if err := a.validatePropertyValuesReferences(o, references, true); err != nil {
		return err
	}
	seen := map[string]bool{}
	links := []string{}
	for _, id := range o.Links {
		if id == o.ID || seen[id] {
			continue
		}
		if !validObjectReferenceID(id) {
			return errors.New("Linked object identity is invalid")
		}
		if _, _, err := a.referenceTarget(id); err != nil {
			return err
		}
		seen[id] = true
		links = append(links, id)
	}
	o.Links = links
	return nil
}
