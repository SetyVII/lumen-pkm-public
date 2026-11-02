package main

import (
	"encoding/json"
	"sort"
	"testing"
)

func createObject(t *testing.T, app *App, body map[string]any) Object {
	t.Helper()
	return objectFrom(t, api(t, app, "POST", "/v1/objects", body))
}

func queryObjects(t *testing.T, app *App, query string) []Object {
	t.Helper()
	rr := api(t, app, "GET", "/v1/objects?"+query, nil)
	if rr.Code != 200 {
		t.Fatalf("GET /v1/objects?%s: HTTP %d: %s", query, rr.Code, rr.Body.String())
	}
	var objects []Object
	if err := json.Unmarshal(rr.Body.Bytes(), &objects); err != nil {
		t.Fatal(err)
	}
	return objects
}

func objectIDs(objects []Object) []string {
	ids := make([]string, 0, len(objects))
	for _, o := range objects {
		ids = append(ids, o.ID)
	}
	sort.Strings(ids)
	return ids
}

func assertIDs(t *testing.T, objects []Object, want ...string) {
	t.Helper()
	got := objectIDs(objects)
	sort.Strings(want)
	if len(got) != len(want) {
		t.Fatalf("got ids %v, want %v", got, want)
	}
	for i := range got {
		if got[i] != want[i] {
			t.Fatalf("got ids %v, want %v", got, want)
		}
	}
}

func TestListFiltersRelational(t *testing.T) {
	app, _, _ := makeApp(t)
	bookA := createObject(t, app, map[string]any{"type_id": "book", "title": "Filter book A"})
	bookB := createObject(t, app, map[string]any{"type_id": "book", "title": "Filter book B"})
	tag := createObject(t, app, map[string]any{"type_id": "tag", "title": "Filter tag"})
	noteA := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Filter note A",
		"links":      []string{bookA.ID, bookB.ID},
		"properties": map[string]any{"tags": []string{tag.ID}},
	})
	noteB := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Filter note B", "links": []string{bookA.ID},
	})
	noteC := createObject(t, app, map[string]any{"type_id": "note", "title": "Filter note C"})
	// The same target as an explicit link and as a property relation counts once.
	noteD := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Filter note D",
		"links":      []string{tag.ID},
		"properties": map[string]any{"tags": []string{tag.ID}},
	})

	if noteA.PropertyLinks[0] != tag.ID || noteD.PropertyLinks[0] != tag.ID {
		t.Fatalf("property relations not derived: %+v %+v", noteA.PropertyLinks, noteD.PropertyLinks)
	}

	// Outgoing link counts: A links to two books and a tag (3), B to one book,
	// C to nothing, D to the tag twice through two representations (1).
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_count=3"), noteA.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_count=0"), noteC.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_count=1,2"), noteB.ID, noteD.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_count=2,"), noteA.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_count=,1"), noteB.ID, noteC.ID, noteD.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_count=0,0"), noteC.ID)

	assertIDs(t, queryObjects(t, app, "type_id=note&linked_includes="+bookA.ID), noteA.ID, noteB.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_includes="+bookA.ID+"&linked_includes="+bookB.ID), noteA.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_includes="+tag.ID), noteA.ID, noteD.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_includes="+noteC.ID))

	assertIDs(t, queryObjects(t, app, "type_id=note&linked_type=book"), noteA.ID, noteB.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_type=tag"), noteA.ID, noteD.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_type=person"))
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_type=book&linked_type=tag"), noteA.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&linked_type=book&linked_count=1"), noteB.ID)

	// Incoming link counts: book A is linked from A and B, book B from A, the
	// tag from A and D.
	assertIDs(t, queryObjects(t, app, "type_id=book&backlinked_count=2"), bookA.ID)
	assertIDs(t, queryObjects(t, app, "type_id=book&backlinked_count=0"))
	assertIDs(t, queryObjects(t, app, "type_id=tag&backlinked_count=2"), tag.ID)
	assertIDs(t, queryObjects(t, app, "type_id=book&backlinked_includes="+noteA.ID), bookA.ID, bookB.ID)
	assertIDs(t, queryObjects(t, app, "type_id=book&backlinked_includes="+noteA.ID+"&backlinked_includes="+noteB.ID), bookA.ID)
	assertIDs(t, queryObjects(t, app, "type_id=book&backlinked_includes="+noteC.ID))
	assertIDs(t, queryObjects(t, app, "type_id=book&backlinked_type=note"), bookA.ID, bookB.ID)
	assertIDs(t, queryObjects(t, app, "type_id=tag&backlinked_type=note"), tag.ID)
	assertIDs(t, queryObjects(t, app, "type_id=book&backlinked_type=tag"))
}

func TestListFiltersContentEmpty(t *testing.T) {
	app, _, _ := makeApp(t)
	empty := createObject(t, app, map[string]any{"type_id": "note", "title": "Empty content"})
	whitespace := createObject(t, app, map[string]any{"type_id": "note", "title": "Whitespace content", "content": " \n\t "})
	richEmpty := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Empty rich text",
		"document": documentFixture(" "),
	})
	richText := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Rich text",
		"document": documentFixture("Hello"),
	})
	blocksEmpty := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Empty blocks",
		"properties": map[string]any{"blocks": []any{map[string]any{"id": "b1", "type": "paragraph", "text": ""}}},
	})
	blocksRuns := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Blocks with runs",
		"properties": map[string]any{"blocks": []any{map[string]any{
			"id": "b2", "type": "paragraph", "text": "",
			"runs": []any{map[string]any{"text": "Hi"}},
		}}},
	})
	plain := createObject(t, app, map[string]any{"type_id": "note", "title": "Plain text", "content": "Some words"})

	assertIDs(t, queryObjects(t, app, "type_id=note&content_empty=true"),
		empty.ID, whitespace.ID, richEmpty.ID, blocksEmpty.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&content_empty=false"),
		richText.ID, blocksRuns.ID, plain.ID)
}

func TestListFiltersDateRanges(t *testing.T) {
	app, _, _ := makeApp(t)
	properties := []any{
		map[string]any{"key": "period", "name": "Period", "type": "date"},
		map[string]any{"key": "review.date", "name": "Review date", "type": "date"},
	}
	if rr := api(t, app, "PATCH", "/v1/types/note", map[string]any{"properties": properties}); rr.Code != 200 {
		t.Fatalf("date schema: %s", rr.Body.String())
	}
	single := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Date single",
		"properties": map[string]any{"period": "2026-10-10"},
	})
	early := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Date early",
		"properties": map[string]any{"period": map[string]any{"start": "2026-10-01", "end": "2026-10-05"}},
	})
	middle := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Date middle",
		"properties": map[string]any{"period": map[string]any{"start": "2026-10-08", "end": "2026-10-12"}},
	})
	late := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Date late",
		"properties": map[string]any{"period": map[string]any{"start": "2026-10-20", "end": "2026-10-25"}},
	})
	createObject(t, app, map[string]any{"type_id": "note", "title": "Date none"})
	dotted := createObject(t, app, map[string]any{
		"type_id": "note", "title": "Date dotted property",
		"properties": map[string]any{"review.date": map[string]any{"start": "2026-11-01", "end": "2026-11-02"}},
	})

	stored := objectFrom(t, api(t, app, "GET", "/v1/objects/"+middle.ID, nil))
	period, ok := stored.Properties["period"].(map[string]any)
	if !ok || period["start"] != "2026-10-08" || period["end"] != "2026-10-12" {
		t.Fatalf("range was not stored as {start,end}: %#v", stored.Properties["period"])
	}

	// Overlap is inclusive: [from,to] intersects the object range.
	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.from=2026-10-09&props.period.to=2026-10-11"),
		single.ID, middle.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.from=2026-10-06"),
		single.ID, middle.ID, late.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.to=2026-10-03"), early.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.from=2026-10-05&props.period.to=2026-10-08"),
		early.ID, middle.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.from=2026-10-10&props.period.to=2026-10-10"),
		single.ID, middle.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.from=2026-10-13"), late.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.to=2026-09-30"))

	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.has_end=true"), early.ID, middle.ID, late.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.has_end=false"), single.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.has_end=true&props.period.from=2026-10-10"),
		middle.ID, late.ID)
	// Property names may contain dots; the operation is split from the right.
	assertIDs(t, queryObjects(t, app, "type_id=note&props.review.date.from=2026-11-01"), dotted.ID)

	for _, body := range []map[string]any{
		{"type_id": "note", "title": "Bad date range", "properties": map[string]any{"period": map[string]any{"start": "2026-10-10", "end": "2026-10-09"}}},
		{"type_id": "note", "title": "Bad date start", "properties": map[string]any{"period": map[string]any{"start": "2026-10-32"}}},
		{"type_id": "note", "title": "Missing date start", "properties": map[string]any{"period": map[string]any{"end": "2026-10-10"}}},
		{"type_id": "note", "title": "Bad date type", "properties": map[string]any{"period": 12}},
	} {
		if rr := api(t, app, "POST", "/v1/objects", body); rr.Code != 400 {
			t.Fatalf("invalid date value accepted (%v): %s", body["title"], rr.Body.String())
		}
	}
	// PATCH validates the same format and can turn a single date into a range.
	if rr := api(t, app, "PATCH", "/v1/objects/"+single.ID, map[string]any{
		"properties": map[string]any{"period": map[string]any{"start": "2026-10-10", "end": "2026-10-09"}},
	}); rr.Code != 400 {
		t.Fatalf("invalid date range accepted by PATCH: %s", rr.Body.String())
	}
	patched := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+single.ID, map[string]any{
		"properties": map[string]any{"period": map[string]any{"start": "2026-10-09", "end": "2026-10-11"}},
	}))
	if period, ok := patched.Properties["period"].(map[string]any); !ok || period["end"] != "2026-10-11" {
		t.Fatalf("PATCH did not store the range: %#v", patched.Properties["period"])
	}
	assertIDs(t, queryObjects(t, app, "type_id=note&props.period.has_end=true&props.period.from=2026-10-11"),
		middle.ID, late.ID, single.ID)
	for _, query := range []string{
		"props.period.from=2026-10-32",
		"props.period.from=2026-10-10&props.period.to=2026-10-09",
		"props.period.has_end=maybe",
		"props.period.bogus=1",
		"props.period=2026-10-10",
		"props..from=2026-10-10",
	} {
		if rr := api(t, app, "GET", "/v1/objects?"+query, nil); rr.Code != 400 {
			t.Fatalf("invalid date filter %q returned %d: %s", query, rr.Code, rr.Body.String())
		}
	}
}

func TestListFiltersCollection(t *testing.T) {
	app, _, _ := makeApp(t)
	// The collections stage owns this default type. Registering the same shape
	// here keeps the query filter testable without that branch.
	if err := app.store.Put("type", "collection", ObjectType{
		ID: "collection", Name: "Collection", Icon: "folder", Color: "#123456",
		Properties: []any{
			map[string]any{"key": "item_type_id", "name": "Item type", "type": "text"},
			map[string]any{"key": "items", "name": "Items", "type": "object", "multiple": true},
		},
	}); err != nil {
		t.Fatal(err)
	}
	one := createObject(t, app, map[string]any{"type_id": "note", "title": "Collection note one"})
	two := createObject(t, app, map[string]any{"type_id": "note", "title": "Collection note two"})
	three := createObject(t, app, map[string]any{"type_id": "note", "title": "Collection note three"})
	first := createObject(t, app, map[string]any{
		"type_id": "collection", "title": "Collection first",
		"properties": map[string]any{"item_type_id": "note", "items": []string{two.ID, one.ID}},
	})
	second := createObject(t, app, map[string]any{
		"type_id": "collection", "title": "Collection second",
		"properties": map[string]any{"item_type_id": "note", "items": []string{one.ID}},
	})
	empty := createObject(t, app, map[string]any{
		"type_id": "collection", "title": "Collection empty",
		"properties": map[string]any{"item_type_id": "note"},
	})

	assertIDs(t, queryObjects(t, app, "collection="+first.ID), one.ID, two.ID)
	assertIDs(t, queryObjects(t, app, "collection="+first.ID+"&collection="+second.ID), one.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&collection="+first.ID), one.ID, two.ID)
	assertIDs(t, queryObjects(t, app, "collection="+empty.ID))
	assertIDs(t, queryObjects(t, app, "collection="+first.ID+"&content_empty=true"), one.ID, two.ID)
	assertIDs(t, queryObjects(t, app, "collection="+first.ID+"&content_empty=false"))
	// The collection object itself is not one of its items.
	assertIDs(t, queryObjects(t, app, "collection="+first.ID+"&type_id=collection"))

	if rr := api(t, app, "GET", "/v1/objects?collection=missing-collection", nil); rr.Code != 400 {
		t.Fatalf("missing collection returned %d: %s", rr.Code, rr.Body.String())
	}
	if rr := api(t, app, "GET", "/v1/objects?collection="+three.ID, nil); rr.Code != 400 {
		t.Fatalf("non-collection object returned %d: %s", rr.Code, rr.Body.String())
	}
}

func TestListFilterParserAndLegacyParams(t *testing.T) {
	app, _, _ := makeApp(t)
	note := createObject(t, app, map[string]any{"type_id": "note", "title": "Parser note"})
	book := createObject(t, app, map[string]any{"type_id": "book", "title": "Parser book"})

	// No filters keeps returning every object in the same display order.
	all := queryObjects(t, app, "")
	stored, err := app.objects()
	if err != nil {
		t.Fatal(err)
	}
	if len(all) != len(stored) {
		t.Fatalf("unfiltered listing changed: %v vs %v", objectIDs(all), objectIDs(stored))
	}
	for i := range stored {
		if all[i].ID != stored[i].ID {
			t.Fatalf("unfiltered listing changed order: %v vs %v", objectIDs(all), objectIDs(stored))
		}
	}
	assertIDs(t, queryObjects(t, app, "type_id=note"), note.ID)
	assertIDs(t, queryObjects(t, app, "q=parser+note"), note.ID)
	assertIDs(t, queryObjects(t, app, "q=PARSER+BOOK"), book.ID)

	// Unknown parameters are ignored for forward compatibility.
	assertIDs(t, queryObjects(t, app, "unknown=1&include_templates=true&type_id=note"), note.ID)
	assertIDs(t, queryObjects(t, app, "type_id=note&type_id=book"), note.ID)

	// /v1/search shares the handler and keeps working.
	search := api(t, app, "GET", "/v1/search?q=parser+book", nil)
	if search.Code != 200 || !containsObjectID(search.Body.Bytes(), book.ID) {
		t.Fatalf("search regressed: %d %s", search.Code, search.Body.String())
	}

	for _, query := range []string{
		"linked_count=abc",
		"linked_count=-1",
		"linked_count=5,2",
		"linked_count=1,2,3",
		"linked_count=",
		"backlinked_count=x",
		"content_empty=maybe",
		"linked_includes=",
		"backlinked_includes=",
		"linked_type=",
		"backlinked_type=",
		"collection=",
	} {
		if rr := api(t, app, "GET", "/v1/objects?"+query, nil); rr.Code != 400 {
			t.Fatalf("invalid filter %q returned %d: %s", query, rr.Code, rr.Body.String())
		}
	}
}

func containsObjectID(body []byte, id string) bool {
	var objects []Object
	if json.Unmarshal(body, &objects) != nil {
		return false
	}
	for _, o := range objects {
		if o.ID == id {
			return true
		}
	}
	return false
}

func TestParseDatePropertyValue(t *testing.T) {
	for _, tc := range []struct {
		name       string
		raw        any
		ok         bool
		start, end string
	}{
		{"legacy string", "2026-10-31", true, "2026-10-31", ""},
		{"range", map[string]any{"start": "2026-10-01", "end": "2026-10-05"}, true, "2026-10-01", "2026-10-05"},
		{"open range", map[string]any{"start": "2026-10-01"}, true, "2026-10-01", ""},
		{"empty end", map[string]any{"start": "2026-10-01", "end": ""}, true, "2026-10-01", ""},
		{"same day range", map[string]any{"start": "2026-10-01", "end": "2026-10-01"}, true, "2026-10-01", "2026-10-01"},
		{"string map", map[string]string{"start": "2026-10-01", "end": "2026-10-02"}, true, "2026-10-01", "2026-10-02"},
		{"end before start", map[string]any{"start": "2026-10-05", "end": "2026-10-01"}, false, "", ""},
		{"invalid start", "2026-10-32", false, "", ""},
		{"missing start", map[string]any{"end": "2026-10-01"}, false, "", ""},
		{"wrong end type", map[string]any{"start": "2026-10-01", "end": 5}, false, "", ""},
		{"number", 5.0, false, "", ""},
		{"nil", nil, false, "", ""},
	} {
		t.Run(tc.name, func(t *testing.T) {
			value, ok := parseDatePropertyValue(tc.raw)
			if ok != tc.ok || value.start != tc.start || value.end != tc.end {
				t.Fatalf("got %+v ok=%v, want start=%q end=%q ok=%v", value, ok, tc.start, tc.end, tc.ok)
			}
		})
	}
}

func TestObjectHasText(t *testing.T) {
	for _, tc := range []struct {
		name string
		o    Object
		want bool
	}{
		{"plain", Object{Content: "hi"}, true},
		{"blank", Object{Content: "  "}, false},
		{"rich markup only", Object{Properties: map[string]any{"rich_text": "<p>&nbsp;</p>"}}, false},
		{"rich text", Object{Properties: map[string]any{"rich_text": "<p>Hi</p>"}}, true},
		{"empty rich text wins over content", Object{Content: "text", Properties: map[string]any{"rich_text": ""}}, false},
		{"blocks text", Object{Properties: map[string]any{"blocks": []any{map[string]any{"text": "Hi"}}}}, true},
		{"blocks empty", Object{Properties: map[string]any{"blocks": []any{map[string]any{"text": ""}}}}, false},
		{"block runs", Object{Properties: map[string]any{"blocks": []any{map[string]any{"text": "", "runs": []any{map[string]any{"text": "Hi"}}}}}}, true},
		{"blocks win over stale content", Object{Content: "stale", Properties: map[string]any{"blocks": []any{map[string]any{"text": ""}}}}, false},
		{"empty blocks fall back", Object{Content: "text", Properties: map[string]any{"blocks": []any{}}}, true},
	} {
		t.Run(tc.name, func(t *testing.T) {
			if got := objectHasText(tc.o); got != tc.want {
				t.Fatalf("objectHasText=%v, want %v", got, tc.want)
			}
		})
	}
}
