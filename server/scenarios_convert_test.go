package main

// User journey: an article turns out to be a book. Convert the type, map the
// renamed properties, keep the aliases and backlinks, and preserve the value
// the target type cannot hold as a content line. This combines convert,
// property maps, tags, aliases and backlinks.

import (
	"strings"
	"testing"
)

func TestScenarioConvertTypeJourney(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := scenarioTag(t, app, "reading")

	sourceType := makeType(t, app, "Article", []any{
		tagsProperty,
		map[string]any{"id": "author_name", "type": "text"},
		map[string]any{"id": "published_on", "type": "date"},
		map[string]any{"id": "reading_status", "type": "select", "options": []any{"open", "abandoned"}},
	})
	targetType := makeType(t, app, "Book", []any{
		tagsProperty,
		map[string]any{"id": "author", "type": "text"},
		map[string]any{"id": "published", "type": "date"},
		map[string]any{"id": "rating", "type": "number"},
	})

	article := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": sourceType.ID, "title": "The Left Hand of Darkness", "content": "Reading notes",
		"properties": map[string]any{
			"author_name":    "Ursula K. Le Guin",
			"published_on":   "1969-03-01",
			"reading_status": "abandoned",
			"tags":           []string{tag.ID},
			"aliases":        []string{"LHoD"},
		},
	}))
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Converted link", "content": "See the article",
		"links": []string{article.ID},
	}))
	articleCollection := createCollection(t, app, "Articles to read", sourceType.ID, []string{article.ID})

	// Step 1: convert and map the renamed properties.
	converted := objectFrom(t, api(t, app, "POST", "/v1/objects/"+article.ID+"/convert", map[string]any{
		"target_type_id": targetType.ID,
		"property_map":   map[string]any{"author_name": "author", "published_on": "published"},
	}))
	if converted.ID != article.ID || converted.TypeID != targetType.ID {
		t.Fatalf("conversion did not keep the identity or change the type: %+v", converted)
	}
	if converted.Revision != article.Revision+1 {
		t.Fatalf("conversion revision = %d, want %d", converted.Revision, article.Revision+1)
	}
	if converted.Title != article.Title || converted.CreatedAt != article.CreatedAt {
		t.Fatalf("conversion changed identity fields: %+v", converted)
	}
	if converted.Properties["author"] != "Ursula K. Le Guin" || converted.Properties["published"] != "1969-03-01" {
		t.Fatalf("mapped properties did not move: %#v", converted.Properties)
	}
	if _, exists := converted.Properties["author_name"]; exists {
		t.Fatalf("old property name survived: %#v", converted.Properties)
	}
	if aliases := aliasesOf(t, converted); !sameStrings(aliases, []string{"LHoD"}) {
		t.Fatalf("aliases did not travel with the object: %v", aliases)
	}
	if tags, ok := converted.Properties["tags"].([]any); !ok || len(tags) != 1 || tags[0] != tag.ID {
		t.Fatalf("tags did not move: %#v", converted.Properties["tags"])
	}

	// Step 2: the unmappable value is preserved as a content line, not lost.
	if !strings.Contains(converted.Content, "Property reading_status: abandoned") {
		t.Fatalf("unmappable property was lost: %q", converted.Content)
	}
	if _, exists := converted.Properties["reading_status"]; exists {
		t.Fatalf("unmappable property stayed in properties: %#v", converted.Properties)
	}

	// Step 3: backlinks survive the type change.
	backlinks := scenarioBacklinks(t, app, converted.ID)
	if !scenarioContainsID(objectIDs(backlinks), note.ID) {
		t.Fatalf("explicit backlink did not survive conversion: %+v", backlinks)
	}
	tagBacklinks := scenarioBacklinks(t, app, tag.ID)
	if !scenarioContainsID(objectIDs(tagBacklinks), converted.ID) {
		t.Fatalf("tag backlink did not survive conversion: %+v", tagBacklinks)
	}

	// Step 4: the object leaves a collection whose item type no longer matches,
	// so the curated list stays truthful and editable.
	articleCollection = scenarioGet(t, app, articleCollection.ID)
	stored, err := collectionItemIDs(articleCollection.Properties["items"])
	if err != nil {
		t.Fatal(err)
	}
	if len(stored) != 0 {
		t.Fatalf("converted object stayed in an article collection: %v", stored)
	}

	// Step 5: the object moved to the new type listing.
	if ids := objectIDs(queryObjects(t, app, "type_id="+targetType.ID)); !scenarioContainsID(ids, converted.ID) {
		t.Fatalf("converted object is missing from its new type: %v", ids)
	}
	if ids := objectIDs(queryObjects(t, app, "type_id="+sourceType.ID)); scenarioContainsID(ids, converted.ID) {
		t.Fatalf("converted object is still listed under its old type: %v", ids)
	}
}
