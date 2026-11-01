package main

// User journey: paste a Markdown note, append more Markdown, convert the block
// tree back to canonical Markdown, and export the note. This combines the
// Markdown converter, block storage and the full export.

import (
	"strings"
	"testing"
)

func TestScenarioMarkdownRoundTripJourney(t *testing.T) {
	app, _, _ := makeApp(t)

	markdown := "# Field notes\n\n" +
		"Intro with **bold** and a [[Wiki Link]].\n\n" +
		"- one\n- two\n\n" +
		"```go\nfmt.Println(\"hi\")\n```\n\n" +
		"$$\nx^2\n$$\n"
	blocks := scenarioMarkdownBlocks(t, app, markdown)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Field notes", "content": markdown,
		"properties": map[string]any{"blocks": blocks},
	}))
	if len(scenarioObjectBlocks(t, note)) != len(blocks) {
		t.Fatal("create did not store the converted block tree")
	}

	// Step 1: append more Markdown, as a user would at the end of the note.
	appended := "## Later\n\n- [ ] follow up\n\nMore text with $a+b$.\n"
	moreBlocks := scenarioMarkdownBlocks(t, app, appended)
	allBlocks := append(append([]Block{}, blocks...), moreBlocks...)
	note = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+note.ID, map[string]any{
		"content":    markdown + "\n" + appended,
		"properties": map[string]any{"blocks": allBlocks},
	}))
	if len(scenarioObjectBlocks(t, note)) != len(allBlocks) {
		t.Fatal("append did not store the combined block tree")
	}

	// Step 2: BlocksToMarkdown produces the canonical Markdown.
	canonical := scenarioBlocksMarkdown(t, app, allBlocks)
	for _, want := range []string{
		"# Field notes", "**bold**", "[[Wiki Link]]", "- one", "- two",
		"```go", "x^2", "## Later", "- [ ] follow up", "$a+b$",
	} {
		scenarioAssertContains(t, canonical, want, "canonical Markdown")
	}

	// Step 3: a full round-trip through the endpoints is stable.
	roundTrip := scenarioBlocksMarkdown(t, app, scenarioMarkdownBlocks(t, app, canonical))
	if roundTrip != canonical {
		t.Fatalf("Markdown round-trip changed the document:\n--- first ---\n%s\n--- second ---\n%s", canonical, roundTrip)
	}

	// Step 4: the exported .md body is the canonical Markdown after the
	// documented export link rewrite: an unresolved wikilink has no object id
	// yet (title resolution is a follow-up), so the export degrades it to its
	// plain title instead of emitting a broken lumen:// link.
	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	_, body := frontMatterBody(t, objectEntry(t, entries, app, note))
	expectedExport := strings.Replace(canonical, "[[Wiki Link]]", "Wiki Link", 1)
	if strings.TrimSpace(body) != strings.TrimSpace(expectedExport) {
		t.Fatalf("exported body differs from canonical Markdown:\n--- export ---\n%s\n--- canonical ---\n%s", body, canonical)
	}
	if strings.Contains(body, "[[Wiki Link]]") {
		t.Fatal("export kept an unresolved wikilink instead of degrading it")
	}
}
