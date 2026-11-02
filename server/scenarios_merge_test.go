package main

// User journey: two copies of the same book, one linked from a reading list.
// Merge the duplicate into the canonical copy, verify the referrer was
// rewritten, then restore the duplicate and inspect the documented degraded
// state. This combines the Markdown converter, tags, aliases, merge, trash and
// backlinks.

import (
	"reflect"
	"strings"
	"testing"
)

func TestScenarioDuplicateMergeJourney(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := scenarioTag(t, app, "reading")

	createBook := func(title, body string, aliases []string) Object {
		t.Helper()
		markdown := "# " + title + "\n\n" + body + "\n"
		blocks := scenarioMarkdownBlocks(t, app, markdown)
		doc, err := canonicalDocumentFromBlocks(blocks)
		if err != nil {
			t.Fatal(err)
		}
		return objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
			"type_id": "book", "title": title, "document": doc,
			"properties": map[string]any{
				"tags":    []string{tag.ID},
				"aliases": aliases,
			},
		}))
	}

	canonical := createBook("Deep Work", "Canonical body", []string{"DW"})
	duplicate := createBook("Deep Work draft", "Duplicate body", []string{"Deep Work draft"})
	if canonical.ID == duplicate.ID {
		t.Fatal("fixture created one book twice")
	}

	// A reading list links the duplicate, which is the reference merge must
	// rewrite.
	linker := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Reading list", "document": documentFixture("See the draft"),
		"links": []string{duplicate.ID},
	}))
	linkerRevision := linker.Revision
	collection := createCollection(t, app, "Reading queue", "book", []string{canonical.ID, duplicate.ID})

	// Step 1: merge the duplicate into the canonical copy.
	merged := objectFrom(t, api(t, app, "POST", "/v1/objects/"+duplicate.ID+"/merge", map[string]any{
		"target_id": canonical.ID,
	}))
	if merged.ID != canonical.ID || merged.Title != "Deep Work" {
		t.Fatalf("merge did not return the canonical book: %+v", merged)
	}
	if merged.Revision <= canonical.Revision {
		t.Fatalf("merge did not bump the canonical revision: %d -> %d", canonical.Revision, merged.Revision)
	}
	scenarioAssertContains(t, merged.Content, "Canonical body", "merged content")
	scenarioAssertContains(t, merged.Content, "Duplicate body", "merged content")
	blocks := merged.Document.Blocks
	if len(blocks) != 4 {
		t.Fatalf("merge did not append the duplicate's blocks: %+v", blocks)
	}
	if blocks[2].Content[0].Text != "Deep Work draft" || blocks[3].Content[0].Text != "Duplicate body" {
		t.Fatalf("appended blocks are wrong: %+v", blocks[2:])
	}
	tags, ok := merged.Properties["tags"].([]any)
	if !ok || len(tags) != 1 || tags[0] != tag.ID {
		t.Fatalf("tag union is wrong: %#v", merged.Properties["tags"])
	}
	if aliases := aliasesOf(t, merged); !sameStrings(aliases, []string{"DW", "Deep Work draft"}) {
		t.Fatalf("alias union is wrong: %v", aliases)
	}

	// Step 2: the duplicate is in the trash, not destroyed.
	if !scenarioContainsID(scenarioTrashIDs(t, app), duplicate.ID) {
		t.Fatal("merged duplicate is not in the trash")
	}
	scenarioStatus(t, api(t, app, "GET", "/v1/objects/"+duplicate.ID, nil), 404)

	// Step 3: the reading list now points at the canonical copy and its
	// revision was bumped.
	linker = scenarioGet(t, app, linker.ID)
	if len(linker.Links) != 1 || linker.Links[0] != canonical.ID {
		t.Fatalf("referrer link was not rewritten: %v", linker.Links)
	}
	if linker.Revision <= linkerRevision {
		t.Fatalf("referrer revision was not bumped: %d -> %d", linkerRevision, linker.Revision)
	}
	if backlinks := scenarioBacklinks(t, app, canonical.ID); !scenarioContainsID(objectIDs(backlinks), linker.ID) {
		t.Fatalf("canonical backlinks do not include the rewritten referrer: %+v", backlinks)
	}
	// A collection that held both copies must keep one canonical item, not a
	// dangling duplicate id.
	collection = scenarioGet(t, app, collection.ID)
	if ids := storedCollectionItemIDs(t, collection); !sameStrings(ids, []string{canonical.ID}) {
		t.Fatalf("collection items were not rewritten and deduplicated: %v", ids)
	}
	if rr := api(t, app, "GET", "/v1/objects/"+duplicate.ID+"/backlinks", nil); rr.Code != 404 {
		t.Fatalf("trashed duplicate advertises backlinks: HTTP %d %s", rr.Code, rr.Body.String())
	}

	// Step 4: restoring the duplicate is an undo-style safety net. It brings
	// back its own content and properties, but the links now point at the
	// canonical copy, so it has no backlinks: a documented degraded duplicate.
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+duplicate.ID+"/restore", nil))
	if !reflect.DeepEqual(restored.Document, duplicate.Document) {
		t.Fatalf("restored duplicate lost its own body: %q", restored.Content)
	}
	if !strings.Contains(restored.Title, "Deep Work draft") {
		t.Fatalf("restored duplicate title = %q", restored.Title)
	}
	if backlinks := scenarioBacklinks(t, app, restored.ID); len(backlinks) != 0 {
		t.Fatalf("restored duplicate unexpectedly has backlinks: %+v", backlinks)
	}
	// The canonical copy keeps the merged body; restoring the duplicate does
	// not undo the merge.
	canonical = scenarioGet(t, app, canonical.ID)
	scenarioAssertContains(t, canonical.Content, "Duplicate body", "canonical content after restore")
}
