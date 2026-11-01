package main

import (
	"encoding/json"
	"strings"
	"testing"
)

func TestTypeSchemaPatchPreservesIdentityNotesAndLegacyProperties(t *testing.T) {
	app, _, _ := makeApp(t)
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Schema keeps notes", "content": "Untouched", "properties": map[string]any{"author": "Stored author", "old_property": "Do not delete"}}))
	properties := []any{"author", map[string]any{"id": "read_on", "name": "Fecha de lectura", "type": "date"}, map[string]any{"id": "rating", "name": "Puntuación", "type": "number"}, map[string]any{"id": "status", "name": "Estado", "type": "select", "options": []any{"Pendiente", "Leído"}}}
	result := api(t, app, "PATCH", "/v1/types/book", map[string]any{"name": "Biblioteca", "color": "#123aBc", "icon": "accessories-dictionary", "properties": properties})
	if result.Code != 200 {
		t.Fatal(result.Body.String())
	}
	var changed ObjectType
	if err := json.Unmarshal(result.Body.Bytes(), &changed); err != nil {
		t.Fatal(err)
	}
	if changed.ID != "book" || changed.Name != "Biblioteca" || len(changed.Properties) != 5 {
		t.Fatal("schema patch changed identity or omitted properties")
	}
	templateProperty := false
	for _, raw := range changed.Properties {
		if schema, ok := raw.(map[string]any); ok && schemaKey(schema) == "is_template" && schema["type"] == "boolean" {
			templateProperty = true
		}
	}
	if !templateProperty {
		t.Fatal("schema patch dropped the reserved is_template property")
	}
	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+book.ID, nil))
	if got.Content != "Untouched" || got.Properties["author"] != "Stored author" || got.Properties["old_property"] != "Do not delete" || got.Revision != book.Revision {
		t.Fatal("schema change rewrote an existing note")
	}
	for _, patch := range []map[string]any{
		{"id": "must-not-change"},
		{"properties": []any{map[string]any{"id": "wrong key", "type": "text"}}},
		{"properties": []any{"author", map[string]any{"id": "author", "type": "text"}}},
		{"properties": []any{map[string]any{"id": "x", "type": "script"}}},
		{"properties": []any{map[string]any{"id": "x", "type": "select", "options": []any{"same", "same"}}}},
		{"properties": []any{map[string]any{"id": "x", "type": "select", "options": "x,y"}}},
		{"color": "red;url(https://example.com)"},
		{"icon": "/tmp/private.svg"},
	} {
		if rr := api(t, app, "PATCH", "/v1/types/book", patch); rr.Code != 400 {
			t.Fatalf("invalid schema accepted: %+v => %s", patch, rr.Body.String())
		}
	}
	if rr := api(t, app, "PATCH", "/v1/types/missing", map[string]any{"name": "Missing"}); rr.Code != 404 {
		t.Fatal("missing type update created a definition")
	}
	if rr := api(t, app, "GET", "/v1/types", nil); !strings.Contains(rr.Body.String(), "Biblioteca") {
		t.Fatal("schema patch not persisted")
	}
}

func TestTypedPropertyValidationAndRelationsRemainSeparateFromExplicitLinks(t *testing.T) {
	app, _, _ := makeApp(t)
	schema := []any{
		map[string]any{"id": "related_books", "type": "object", "target_type": "book"},
		map[string]any{"id": "pages", "type": "number", "min": 0},
		map[string]any{"id": "finished", "type": "boolean"},
		map[string]any{"id": "date_read", "type": "date"},
		map[string]any{"id": "status", "type": "select", "options": []any{"open", "done"}},
	}
	if rr := api(t, app, "PATCH", "/v1/types/note", map[string]any{"properties": schema}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Property relation target"}))
	person := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "person", "title": "Wrong relation type"}))
	properties := map[string]any{"related_books": []string{book.ID, book.ID}, "pages": 124, "finished": false, "date_read": "2026-10-29", "status": "open", "unknown_legacy": map[string]any{"kept": true}}
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Typed properties", "properties": properties, "links": []string{book.ID}}))
	if len(note.Links) != 1 || len(note.PropertyLinks) != 1 || note.PropertyLinks[0] != book.ID {
		t.Fatal("typed relation did not preserve independent explicit relation")
	}
	note = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+note.ID, map[string]any{"links": []string{}}))
	if len(note.Links) != 0 || len(note.PropertyLinks) != 1 {
		t.Fatal("removing an explicit link removed its independent property relation")
	}
	var backlinks []Object
	if rr := api(t, app, "GET", "/v1/objects/"+book.ID+"/backlinks", nil); json.Unmarshal(rr.Body.Bytes(), &backlinks) != nil || len(backlinks) != 1 {
		t.Fatal("backlinks omitted property relations")
	}
	for key, invalid := range map[string]any{"related_books": []string{person.ID}, "pages": "124", "finished": "yes", "date_read": "2026-99-99", "status": "missing"} {
		changed := map[string]any{}
		for k, v := range properties {
			changed[k] = v
		}
		changed[key] = invalid
		if rr := api(t, app, "PATCH", "/v1/objects/"+note.ID, map[string]any{"properties": changed}); rr.Code != 400 {
			t.Fatalf("invalid %s accepted: %s", key, rr.Body.String())
		}
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+book.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	// Trash keeps the typed relation so a restore can recover it.
	note = objectFrom(t, api(t, app, "GET", "/v1/objects/"+note.ID, nil))
	related, _ := note.Properties["related_books"].([]any)
	if len(note.PropertyLinks) != 1 || note.PropertyLinks[0] != book.ID || len(related) != 2 {
		t.Fatal("trash dropped a restorable typed relation")
	}
	if rr := api(t, app, "DELETE", "/v1/trash/"+book.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	note = objectFrom(t, api(t, app, "GET", "/v1/objects/"+note.ID, nil))
	if len(note.PropertyLinks) != 0 || len(note.Links) != 0 || len(note.Properties["related_books"].([]any)) != 0 || note.Properties["unknown_legacy"] == nil {
		t.Fatal("purge failed to clean typed relation or damaged hidden legacy data")
	}
}

func TestJournalTagsAreAddedToExistingWorkspace(t *testing.T) {
	app, path, key := makeApp(t)
	// Simulate a workspace created before daily notes declared tags.
	journalType, err := app.store.Type("journal")
	if err != nil {
		t.Fatal(err)
	}
	journalType.Properties = []any{"date", isTemplateProperty}
	if err := app.store.Put("type", "journal", journalType); err != nil {
		t.Fatal(err)
	}
	if err := app.store.Close(); err != nil {
		t.Fatal(err)
	}
	store, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := NewApp(store, testToken, nil); err != nil {
		t.Fatal(err)
	}
	app.store = store

	migrated, err := app.store.Type("journal")
	if err != nil {
		t.Fatal(err)
	}
	found := false
	for _, raw := range migrated.Properties {
		schema, ok := raw.(map[string]any)
		if ok && schemaKey(schema) == "tags" && schema["type"] == "object" && schema["target_type"] == "tag" {
			found = true
		}
	}
	if !found {
		t.Fatalf("startup normalization did not add tags to the journal type: %+v", migrated.Properties)
	}
	// The migrated schema is functional: a tag on a journal becomes a relation.
	tag := scenarioTag(t, app, "migrated-journal-tag")
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-12-24", nil))
	journal = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+journal.ID, map[string]any{
		"properties": map[string]any{"date": "2026-12-24", "tags": []string{tag.ID}},
	}))
	if !scenarioContainsID(journal.PropertyLinks, tag.ID) {
		t.Fatalf("migrated journal tags did not become a relation: %+v", journal.PropertyLinks)
	}
}
