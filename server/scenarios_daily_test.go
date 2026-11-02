package main

// User journey: capture a day. Open today's note, write it with links to a
// person and a project, tag it, paste a URL, and find everything again through
// the tag page and search. This combines aliases, weblink reader, journal,
// capture and query filters.

import (
	"lumen/server/internal/document"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestScenarioDailyNoteCaptureJourney(t *testing.T) {
	app, _, _ := makeApp(t)

	// A public-looking URL served by an in-process fixture. readerTestFetcher
	// keeps the production SSRF policy and only replaces the dialer.
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		_, _ = w.Write([]byte(readerArticlePage(`<p>SCENARIO_READER_TOKEN_4411</p>`)))
	}))
	defer server.Close()
	app.fetcher = readerTestFetcher(server)

	// Step 1: opening the day creates the daily note lazily.
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-31", nil))
	if journal.ID != "journal-2026-10-31" || journal.TypeID != "journal" {
		t.Fatalf("journal identity is wrong: %+v", journal)
	}
	if journal.Title != "2026-10-31" || journal.Properties["date"] != "2026-10-31" {
		t.Fatalf("journal date is wrong: title=%q properties=%v", journal.Title, journal.Properties)
	}
	if journal.Revision != 1 {
		t.Fatalf("lazy journal revision = %d", journal.Revision)
	}

	// Step 2: the people, project and tag the note is about.
	person := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "person", "title": "Ada Lovelace",
	}))
	project := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "project", "title": "Lumen parity",
	}))
	tag := scenarioTag(t, app, "daily-log")

	// Step 3: write the note. The block runs carry the @-mentions and the
	// explicit links array is what the relation index and backlinks use.
	journalDoc := document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "daily-block-1", Type: "paragraph", Content: []document.Inline{
		{Type: "text", Text: "Coffee with "}, {Type: "object_ref", ObjectID: person.ID, Label: "Ada"}, {Type: "text", Text: " about the "}, {Type: "object_ref", ObjectID: project.ID, Label: "Lumen parity project"},
	}}}}
	journal = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+journal.ID, documentPatchPayload(t, app, journal.ID, map[string]any{"document": journalDoc, "properties": map[string]any{"date": "2026-10-31", "tags": []string{tag.ID}, "aliases": []string{"Weekend log"}}})))

	if journal.Revision != 2 {
		t.Fatalf("journal write revision = %d", journal.Revision)
	}
	if !scenarioContainsID(journal.DocumentLinks, person.ID) || !scenarioContainsID(journal.DocumentLinks, project.ID) {
		t.Fatalf("journal lost its @-mentions: %v", journal.Links)
	}
	if aliases := aliasesOf(t, journal); !sameStrings(aliases, []string{"Weekend log"}) {
		t.Fatalf("journal alias was not stored: %v", aliases)
	}
	if tags, ok := journal.Properties["tags"].([]any); !ok || len(tags) != 1 || tags[0] != tag.ID {
		t.Fatalf("journal tag was not stored: %#v", journal.Properties["tags"])
	}
	if blocks := journal.Document.Blocks; len(blocks) != 1 || len(blocks[0].Content) != 4 {
		t.Fatalf("journal block tree was not stored: %+v", blocks)
	}

	// Step 4: paste a URL into the same day. Capture fetches the fixture once,
	// stores the readable article and links the weblink to the journal.
	captured := objectFrom(t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": "http://8.8.8.8/quiet-web", "title": "The Quiet Web", "date": "2026-10-31",
	}))
	if captured.TypeID != "web" || captured.Title != "The Quiet Web" {
		t.Fatalf("capture did not create the weblink: %+v", captured)
	}
	reader, _ := captured.Properties["reader_content"].(string)
	if !strings.Contains(reader, "SCENARIO_READER_TOKEN_4411") || !strings.Contains(reader, "# The Quiet Web") {
		t.Fatalf("capture did not store the readable article:\n%s", reader)
	}
	if captured.Properties["favicon"] != "http://8.8.8.8/favicon.png" {
		t.Fatalf("capture favicon = %v", captured.Properties["favicon"])
	}

	// Step 5: tag the weblink. `properties` is a full replacement, so the URL
	// is resent; reader metadata is server-managed and must survive.
	captured = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+captured.ID, map[string]any{
		"properties": map[string]any{
			"url":    "http://8.8.8.8/quiet-web",
			"status": "Para revisar",
			"tags":   []string{tag.ID},
		},
	}))
	if got, _ := captured.Properties["reader_content"].(string); got != reader {
		t.Fatal("tagging the weblink erased its reader content")
	}

	// Step 6: the journal now links to the weblink as well.
	journal = scenarioGet(t, app, journal.ID)
	if !scenarioContainsID(journal.Links, captured.ID) {
		t.Fatalf("capture did not link the journal: %v", journal.Links)
	}
	if journal.Revision <= 2 {
		t.Fatalf("linking the capture did not bump the journal revision: %d", journal.Revision)
	}

	// Step 7: the weblink's backlinks show the journal.
	backlinks := scenarioBacklinks(t, app, captured.ID)
	if len(backlinks) != 1 || backlinks[0].ID != journal.ID {
		t.Fatalf("weblink backlinks = %v, want the journal", scenarioCompactJSON(t, backlinks))
	}

	// Step 8: the tag page query (type + tag relation) finds both the journal
	// and the weblink.
	webHits := queryObjects(t, app, "type_id=web&linked_includes="+tag.ID)
	if !scenarioContainsID(objectIDs(webHits), captured.ID) {
		t.Fatalf("tag page missed the weblink: %v", objectIDs(webHits))
	}
	journalHits := queryObjects(t, app, "type_id=journal&linked_includes="+tag.ID)
	if !scenarioContainsID(objectIDs(journalHits), journal.ID) {
		t.Fatalf("tag page missed the journal: %v", objectIDs(journalHits))
	}

	// Step 9: search finds the weblink through its reader body and the journal
	// through the alias added in step 3.
	if ids := scenarioSearchIDs(t, app, "SCENARIO_READER_TOKEN_4411"); !scenarioContainsID(ids, captured.ID) {
		t.Fatalf("search missed the reader content: %v", ids)
	}
	aliasHits := searchFor(t, app, "Weekend log")
	found := false
	for _, hit := range aliasHits {
		if hit.ID == journal.ID {
			found = true
			if hit.MatchedAlias != "Weekend log" {
				t.Fatalf("journal search hit did not report the alias: %+v", hit)
			}
		}
	}
	if !found {
		t.Fatalf("search missed the journal alias: %+v", aliasHits)
	}
}
