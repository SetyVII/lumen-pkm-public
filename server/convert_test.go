package main

import (
	"encoding/json"
	"reflect"
	"strings"
	"testing"
)

func makeType(t *testing.T, app *App, name string, properties []any) ObjectType {
	t.Helper()
	return decodeBody[ObjectType](t, api(t, app, "POST", "/v1/types", map[string]any{
		"name": name, "icon": "text-x-generic", "color": "#123456", "properties": properties,
	}))
}

func pullSince(t *testing.T, app *App, since int64) syncPullResponse {
	t.Helper()
	return decodeBody[syncPullResponse](t, api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": since}))
}

func objectsContainID(objects []Object, id string) bool {
	for _, o := range objects {
		if o.ID == id {
			return true
		}
	}
	return false
}

func TestConvertMovesCompatiblePropertiesAndKeepsRelations(t *testing.T) {
	app, _, _ := makeApp(t)
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Related book"}))
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Parity"}))
	sourceType := makeType(t, app, "Article", []any{
		tagsProperty,
		map[string]any{"id": "pages", "type": "number"},
		map[string]any{"id": "finished", "type": "boolean"},
		map[string]any{"id": "read_on", "type": "date"},
		map[string]any{"id": "status", "type": "select", "options": []any{"open", "done"}},
		map[string]any{"id": "related", "type": "object", "target_type": "book", "multiple": true},
	})
	targetType := makeType(t, app, "Report", []any{
		tagsProperty,
		map[string]any{"id": "pages", "type": "number"},
		map[string]any{"id": "finished", "type": "boolean"},
		map[string]any{"id": "read_on", "type": "date"},
		map[string]any{"id": "status", "type": "select", "options": []any{"open", "done", "reading"}},
		map[string]any{"id": "related", "type": "object", "target_type": "book", "multiple": true},
	})
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": sourceType.ID, "title": "Converted article", "content": "Body stays",
		"properties": map[string]any{
			"pages": 12, "finished": true, "read_on": "2026-10-31", "status": "open",
			"tags": []string{tag.ID}, "related": []string{book.ID},
			"aliases": []any{"Alias one"}, "summary": "Hidden summary",
		},
	}))
	head, err := app.store.HeadSeq()
	if err != nil {
		t.Fatal(err)
	}

	converted := objectFrom(t, api(t, app, "POST", "/v1/objects/"+o.ID+"/convert", map[string]any{"target_type_id": targetType.ID}))
	if converted.ID != o.ID || converted.TypeID != targetType.ID {
		t.Fatalf("conversion did not keep the identity or change the type: %+v", converted)
	}
	if converted.Revision != o.Revision+1 {
		t.Fatalf("conversion did not bump the revision: %d -> %d", o.Revision, converted.Revision)
	}
	if converted.Title != o.Title || converted.Content != "Body stays" || converted.CreatedAt != o.CreatedAt {
		t.Fatalf("conversion changed title, content or creation time: %+v", converted)
	}
	for key, want := range map[string]any{
		"pages": float64(12), "finished": true, "read_on": "2026-10-31", "status": "open",
	} {
		if converted.Properties[key] != want {
			t.Fatalf("property %s = %#v, want %#v", key, converted.Properties[key], want)
		}
	}
	if tags, ok := converted.Properties["tags"].([]any); !ok || len(tags) != 1 || tags[0] != tag.ID {
		t.Fatalf("tags did not move: %#v", converted.Properties["tags"])
	}
	if related, ok := converted.Properties["related"].([]any); !ok || len(related) != 1 || related[0] != book.ID {
		t.Fatalf("typed relation did not move: %#v", converted.Properties["related"])
	}
	if aliases := aliasesOf(t, converted); !sameStrings(aliases, []string{"Alias one"}) {
		t.Fatalf("aliases did not travel with the object: %v", aliases)
	}
	if converted.Properties["summary"] != "Hidden summary" {
		t.Fatalf("hidden metadata was dropped: %#v", converted.Properties)
	}
	if len(converted.PropertyLinks) != 2 {
		t.Fatalf("derived relations were not recomputed: %+v", converted.PropertyLinks)
	}
	if found := decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+tag.ID+"/backlinks", nil)); len(found) != 1 || found[0].ID != o.ID {
		t.Fatalf("tag backlink did not survive conversion: %+v", found)
	}
	if found := decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+book.ID+"/backlinks", nil)); len(found) != 1 || found[0].ID != o.ID {
		t.Fatalf("relation backlink did not survive conversion: %+v", found)
	}
	// The in-memory index moved the object to the new type.
	if !objectsContainID(decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects?type_id="+targetType.ID, nil)), o.ID) {
		t.Fatal("converted object is missing from its new type listing")
	}
	if objectsContainID(decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects?type_id="+sourceType.ID, nil)), o.ID) {
		t.Fatal("converted object is still listed under its old type")
	}
	pull := pullSince(t, app, head)
	if len(pull.Objects) != 1 || pull.Objects[0].ID != o.ID || pull.Objects[0].TypeID != targetType.ID {
		t.Fatalf("mobile did not receive the converted object: %+v", pull.Objects)
	}
	if pull.Objects[0].Revision != converted.Revision {
		t.Fatalf("mobile received a stale revision: %+v", pull.Objects[0])
	}
}

func TestConvertWritesUnmovableValuesIntoContent(t *testing.T) {
	app, _, _ := makeApp(t)
	tagOne := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Tag one"}))
	tagTwo := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Tag two"}))
	sourceType := makeType(t, app, "Article", []any{
		tagsProperty,
		map[string]any{"id": "pages", "type": "number"},
		map[string]any{"id": "status", "type": "select", "options": []any{"open"}},
	})
	targetType := makeType(t, app, "Memo", []any{
		map[string]any{"id": "pages", "type": "text"},
		map[string]any{"id": "status", "type": "select", "options": []any{"draft"}},
	})
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": sourceType.ID, "title": "Lossless", "content": "Keep me",
		"properties": map[string]any{
			"pages": 124, "status": "open", "tags": []string{tagOne.ID, tagTwo.ID},
			"blocks":    []any{map[string]any{"id": "block-1", "text": "Keep me"}},
			"rich_text": "<p>Keep me</p>",
		},
	}))
	converted := objectFrom(t, api(t, app, "POST", "/v1/objects/"+o.ID+"/convert", map[string]any{"target_type_id": targetType.ID}))
	want := "Keep me\nProperty pages: 124\nProperty status: open\nProperty tags: Tag one, Tag two"
	if converted.Content != want {
		t.Fatalf("content fallback = %q, want %q", converted.Content, want)
	}
	for _, key := range []string{"pages", "status", "tags", "blocks", "rich_text"} {
		if _, exists := converted.Properties[key]; exists {
			t.Fatalf("property %s should have moved to the content: %#v", key, converted.Properties)
		}
	}
	if converted.Revision != o.Revision+1 {
		t.Fatalf("content fallback did not bump the revision: %+v", converted)
	}
	// The tags relation became plain text, so the tag no longer has this
	// object as a backlink.
	if found := decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+tagOne.ID+"/backlinks", nil)); len(found) != 0 {
		t.Fatalf("dropped tag relation left a backlink: %+v", found)
	}
}

func TestConvertHonorsPropertyMapAndBlocksDuplicateTargets(t *testing.T) {
	app, _, _ := makeApp(t)
	sourceType := makeType(t, app, "Draft", []any{
		map[string]any{"id": "author_name", "type": "text"},
		map[string]any{"id": "writer", "type": "text"},
		map[string]any{"id": "rating", "type": "number"},
	})
	targetType := makeType(t, app, "Review", []any{
		map[string]any{"id": "author", "type": "text"},
		map[string]any{"id": "rating", "type": "number"},
	})
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": sourceType.ID, "title": "Mapped", "content": "",
		"properties": map[string]any{"author_name": "Ada", "writer": "Grace", "rating": 5},
	}))
	converted := objectFrom(t, api(t, app, "POST", "/v1/objects/"+o.ID+"/convert", map[string]any{
		"target_type_id": targetType.ID,
		"property_map":   map[string]any{"author_name": "author", "writer": "author"},
	}))
	if converted.Properties["author"] != "Ada" {
		t.Fatalf("explicit mapping did not win: %#v", converted.Properties)
	}
	if converted.Properties["rating"] != float64(5) {
		t.Fatalf("unmapped same-name property did not stay: %#v", converted.Properties)
	}
	if _, exists := converted.Properties["author_name"]; exists {
		t.Fatalf("mapped source property was kept: %#v", converted.Properties)
	}
	if !strings.Contains(converted.Content, "Property writer: Grace") {
		t.Fatalf("blocked mapping was not preserved in the content: %q", converted.Content)
	}

	// A hidden property can be mapped to a declared target property.
	hiddenTarget := makeType(t, app, "Hidden target", []any{map[string]any{"id": "description", "type": "text"}})
	hidden := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Hidden source", "properties": map[string]any{"summary": "Hidden text"},
	}))
	hiddenConverted := objectFrom(t, api(t, app, "POST", "/v1/objects/"+hidden.ID+"/convert", map[string]any{
		"target_type_id": hiddenTarget.ID, "property_map": map[string]any{"summary": "description"},
	}))
	if hiddenConverted.Properties["description"] != "Hidden text" {
		t.Fatalf("hidden property was not mapped: %#v", hiddenConverted.Properties)
	}
}

func TestConvertBetweenDefaultTypesKeepsTagsAndAliases(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Default"}))
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Default note", "content": "Body",
		"properties": map[string]any{"tags": []string{tag.ID}, "aliases": []any{"Default alias"}},
	}))
	converted := objectFrom(t, api(t, app, "POST", "/v1/objects/"+note.ID+"/convert", map[string]any{"target_type_id": "book"}))
	if converted.TypeID != "book" || converted.Content != "Body" {
		t.Fatalf("default type conversion failed: %+v", converted)
	}
	if tags, ok := converted.Properties["tags"].([]any); !ok || len(tags) != 1 || tags[0] != tag.ID {
		t.Fatalf("default tags did not move to the book: %#v", converted.Properties["tags"])
	}
	if aliases := aliasesOf(t, converted); !sameStrings(aliases, []string{"Default alias"}) {
		t.Fatalf("aliases did not survive the default conversion: %v", aliases)
	}
}

func TestConvertRejectsInvalidRequests(t *testing.T) {
	app, _, _ := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Guard",
		"properties": map[string]any{"summary": "Present", "aliases": []any{"Alias"}},
	}))
	other := makeType(t, app, "Other", []any{})
	cases := []struct {
		name string
		id   string
		body map[string]any
		want int
	}{
		{"same type", note.ID, map[string]any{"target_type_id": "note"}, 400},
		{"unknown type", note.ID, map[string]any{"target_type_id": "missing"}, 400},
		{"missing type id", note.ID, map[string]any{}, 400},
		{"missing object", "missing-id", map[string]any{"target_type_id": other.ID}, 404},
		{"unknown source mapping", note.ID, map[string]any{"target_type_id": other.ID, "property_map": map[string]any{"nope": "x"}}, 400},
		{"unknown target mapping", note.ID, map[string]any{"target_type_id": other.ID, "property_map": map[string]any{"summary": "nope"}}, 400},
		{"reserved source mapping", note.ID, map[string]any{"target_type_id": other.ID, "property_map": map[string]any{"aliases": "x"}}, 400},
		{"reserved target mapping", note.ID, map[string]any{"target_type_id": other.ID, "property_map": map[string]any{"summary": "aliases"}}, 400},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			rr := api(t, app, "POST", "/v1/objects/"+tc.id+"/convert", tc.body)
			if rr.Code != tc.want {
				t.Fatalf("HTTP %d, want %d: %s", rr.Code, tc.want, rr.Body.String())
			}
		})
	}

	rr := api(t, app, "POST", "/v1/objects/"+note.ID+"/convert", map[string]any{
		"target_type_id": other.ID, "base_revision": note.Revision + 5,
	})
	if rr.Code != 409 || !strings.Contains(rr.Body.String(), `"conflict":true`) {
		t.Fatalf("expected 409, got %d: %s", rr.Code, rr.Body.String())
	}
	var conflict struct {
		Current Object `json:"current"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &conflict); err != nil || conflict.Current.ID != note.ID || conflict.Current.Revision != note.Revision {
		t.Fatalf("409 did not return the current object: %s", rr.Body.String())
	}
	fresh := objectFrom(t, api(t, app, "POST", "/v1/objects/"+note.ID+"/convert", map[string]any{
		"target_type_id": other.ID, "base_revision": note.Revision,
	}))
	if fresh.Revision != note.Revision+1 || fresh.TypeID != other.ID {
		t.Fatalf("matching base_revision failed: %+v", fresh)
	}

	trashed := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Trashed convert"}))
	if rr = api(t, app, "DELETE", "/v1/objects/"+trashed.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr = api(t, app, "POST", "/v1/objects/"+trashed.ID+"/convert", map[string]any{"target_type_id": other.ID}); rr.Code != 404 {
		t.Fatalf("converting a trashed object returned %d", rr.Code)
	}
}

func TestConvertKeepsRichFormatsWhenNothingIsAppended(t *testing.T) {
	app, _, _ := makeApp(t)
	sourceType := makeType(t, app, "Article", []any{map[string]any{"id": "pages", "type": "number"}})
	targetType := makeType(t, app, "Report", []any{map[string]any{"id": "pages", "type": "number"}})
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": sourceType.ID, "title": "Rich", "document": documentFixture("Body"),
		"properties": map[string]any{
			"pages": 3, "blocks": []any{map[string]any{"id": "b1", "text": "Body"}}, "rich_text": "<p>Body</p>",
		},
	}))
	converted := objectFrom(t, api(t, app, "POST", "/v1/objects/"+o.ID+"/convert", map[string]any{"target_type_id": targetType.ID}))
	if converted.Content != "Body" {
		t.Fatalf("content changed without leftovers: %q", converted.Content)
	}
	if !reflect.DeepEqual(converted.Document, o.Document) {
		t.Fatal("conversion changed canonical formatting without leftovers")
	}
}

func TestConvertNormalizesRelationCardinality(t *testing.T) {
	app, _, _ := makeApp(t)
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Target book"}))
	multi := makeType(t, app, "Multi", []any{
		map[string]any{"id": "related", "type": "object", "target_type": "book", "multiple": true},
	})
	single := makeType(t, app, "Single", []any{
		map[string]any{"id": "related", "type": "object", "target_type": "book"},
	})
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": multi.ID, "title": "Cardinality", "properties": map[string]any{"related": []string{book.ID}},
	}))
	converted := objectFrom(t, api(t, app, "POST", "/v1/objects/"+o.ID+"/convert", map[string]any{"target_type_id": single.ID}))
	if converted.Properties["related"] != book.ID {
		t.Fatalf("single relation was not normalized to a string: %#v", converted.Properties["related"])
	}
	again := objectFrom(t, api(t, app, "POST", "/v1/objects/"+converted.ID+"/convert", map[string]any{"target_type_id": multi.ID}))
	related, ok := again.Properties["related"].([]any)
	if !ok || len(related) != 1 || related[0] != book.ID {
		t.Fatalf("multiple relation was not normalized to a list: %#v", again.Properties["related"])
	}
}

func TestConvertRemovesObjectFromIncompatibleCollections(t *testing.T) {
	app, _, _ := makeApp(t)
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Collection convert book"}))
	other := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Collection convert other"}))
	collection := createCollection(t, app, "Convert reading list", "book", []string{book.ID, other.ID})

	converted := objectFrom(t, api(t, app, "POST", "/v1/objects/"+book.ID+"/convert", map[string]any{"target_type_id": "note"}))
	if converted.TypeID != "note" {
		t.Fatalf("conversion failed: %+v", converted)
	}
	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+collection.ID, nil))
	if ids := storedCollectionItemIDs(t, got); scenarioContainsID(ids, book.ID) {
		t.Fatalf("converted object stayed in a book collection: %v", ids)
	}
	if ids := storedCollectionItemIDs(t, got); !sameStrings(ids, []string{other.ID}) {
		t.Fatalf("collection lost the remaining item or order: %v", ids)
	}
	// The collection must stay editable: a stale wrong-type item used to make
	// every later write fail validation.
	third := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Collection convert third"}))
	if rr := api(t, app, "POST", "/v1/collections/"+collection.ID+"/items", map[string]any{"object_id": third.ID}); rr.Code != 200 {
		t.Fatalf("collection became uneditable after conversion: HTTP %d %s", rr.Code, rr.Body.String())
	}
}
