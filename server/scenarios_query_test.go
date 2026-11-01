package main

// User journey: curate a reading list, reorder it, remove an item, and use the
// list as a query base: outgoing-link counts, backlinks from a review, empty
// notes, and a filtered CSV export. This combines collections, query filters,
// tags, aliases and export.

import (
	"testing"
)

func TestScenarioCollectionQueryExportJourney(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := scenarioTag(t, app, "favourite")

	book1 := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "book", "title": "Book one", "content": "Book one notes",
		"properties": map[string]any{
			"author": "Ada", "rating": 5, "tags": []string{tag.ID}, "aliases": []string{"B1"},
		},
	}))
	book2 := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "book", "title": "Book two", "content": "Book two notes",
		"properties": map[string]any{"author": "Grace"},
	}))
	book3 := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "book", "title": "Book three", "content": "Book three notes",
	}))
	book4 := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "book", "title": "Book four", "content": "Book four notes",
	}))
	book5 := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "book", "title": "Book five",
	}))

	// Step 1: build the collection in order.
	collection := createCollection(t, app, "Reading list", "book", nil)
	for _, book := range []Object{book1, book2, book3, book4, book5} {
		collection = objectFrom(t, api(t, app, "POST", "/v1/collections/"+collection.ID+"/items", map[string]any{
			"object_id": book.ID,
		}))
	}
	if ids := storedCollectionItemIDs(t, collection); !sameStrings(ids, []string{book1.ID, book2.ID, book3.ID, book4.ID, book5.ID}) {
		t.Fatalf("append order = %v", ids)
	}

	// Step 2: reorder the list, then remove one book.
	collection = objectFrom(t, api(t, app, "PUT", "/v1/collections/"+collection.ID+"/items", map[string]any{
		"items": []string{book5.ID, book3.ID, book1.ID, book4.ID, book2.ID},
	}))
	collection = objectFrom(t, api(t, app, "DELETE", "/v1/collections/"+collection.ID+"/items/"+book2.ID, nil))
	if ids := storedCollectionItemIDs(t, collection); !sameStrings(ids, []string{book5.ID, book3.ID, book1.ID, book4.ID}) {
		t.Fatalf("reorder/remove order = %v", ids)
	}
	resolved := decodeBody[collectionItemsResponse](t, api(t, app, "GET", "/v1/collections/"+collection.ID+"/items", nil))
	if len(resolved.Items) != 4 || resolved.Items[0].ID != book5.ID || resolved.Items[3].ID != book4.ID {
		t.Fatalf("resolved collection order = %+v", resolved.ItemIDs)
	}

	// Step 3: query the collection, its relation counts and its empty notes.
	inCollection := queryObjects(t, app, "collection="+collection.ID)
	if len(inCollection) != 4 || scenarioContainsID(objectIDs(inCollection), book2.ID) {
		t.Fatalf("collection filter = %v", objectIDs(inCollection))
	}
	noLinks := queryObjects(t, app, "collection="+collection.ID+"&linked_count=0")
	if len(noLinks) != 3 || scenarioContainsID(objectIDs(noLinks), book1.ID) {
		t.Fatalf("linked_count=0 = %v", objectIDs(noLinks))
	}
	oneLink := queryObjects(t, app, "collection="+collection.ID+"&linked_count=1")
	if len(oneLink) != 1 || oneLink[0].ID != book1.ID {
		t.Fatalf("linked_count=1 = %v", objectIDs(oneLink))
	}
	empty := queryObjects(t, app, "collection="+collection.ID+"&content_empty=true")
	if len(empty) != 1 || empty[0].ID != book5.ID {
		t.Fatalf("content_empty=true = %v", objectIDs(empty))
	}

	// Step 4: a review links a person and a book. The backlink filters find
	// both directions of that relation.
	person := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "person", "title": "The critic",
	}))
	review := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Review of Book one", "content": "A careful review",
		"links": []string{person.ID, book1.ID},
	}))
	reviewedBooks := queryObjects(t, app, "type_id=book&backlinked_includes="+review.ID)
	if len(reviewedBooks) != 1 || reviewedBooks[0].ID != book1.ID {
		t.Fatalf("backlinked_includes review = %v", objectIDs(reviewedBooks))
	}
	reviewedPeople := queryObjects(t, app, "type_id=person&backlinked_includes="+review.ID)
	if len(reviewedPeople) != 1 || reviewedPeople[0].ID != person.ID {
		t.Fatalf("person linked from the review = %v", objectIDs(reviewedPeople))
	}

	// Step 5: export only books. The CSV has the schema columns plus the extra
	// stored aliases key, one row per book, and no notes CSV.
	entries := exportEntries(t, app, "POST", "/v1/export", map[string]any{
		"type_ids": []string{"book"},
	})
	rows := csvTable(t, entries, "books.csv")
	wantHeader := []string{"id", "title", "aliases", "author", "medium", "rating", "recommended_by", "status", "tags", "is_template"}
	if len(rows) != 6 {
		t.Fatalf("books.csv rows = %d, want header + 5", len(rows))
	}
	if !sameStrings(rows[0], wantHeader) {
		t.Fatalf("books.csv header = %v, want %v", rows[0], wantHeader)
	}
	row := csvRow(t, rows, book1.ID)
	if scenarioCSVValue(t, rows, row, "title") != "Book one" ||
		scenarioCSVValue(t, rows, row, "aliases") != "B1" ||
		scenarioCSVValue(t, rows, row, "author") != "Ada" ||
		scenarioCSVValue(t, rows, row, "rating") != "5" ||
		scenarioCSVValue(t, rows, row, "tags") != tag.ID ||
		scenarioCSVValue(t, rows, row, "is_template") != "false" {
		t.Fatalf("book one CSV row = %v", row)
	}
	row = csvRow(t, rows, book5.ID)
	if scenarioCSVValue(t, rows, row, "title") != "Book five" || scenarioCSVValue(t, rows, row, "author") != "" {
		t.Fatalf("book five CSV row = %v", row)
	}
	if _, exists := entries["notes.csv"]; exists {
		t.Fatal("filtered export wrote an unrelated notes.csv")
	}
	manifest := manifestFrom(t, entries)
	if manifest.Counts["book"] != 5 || manifest.Counts["note"] != 0 {
		t.Fatalf("manifest counts = %v", manifest.Counts)
	}
}
