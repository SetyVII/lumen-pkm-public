package main

// User journey: define a Meeting type, save a template with a date variable,
// make it the type default, create a meeting from it, and apply a daily-note
// template to a new date. This combines templates, custom types, blocks,
// default templates and the lazy journal.

import (
	"lumen/server/internal/document"
	"strings"
	"testing"
)

func TestScenarioTemplateAndJournalJourney(t *testing.T) {
	app, _, _ := makeApp(t)

	meetingType := makeType(t, app, "Meeting", []any{
		map[string]any{"id": "when", "type": "date"},
		map[string]any{"id": "agenda", "type": "text"},
		tagsProperty,
	})
	tag := scenarioTag(t, app, "team")

	// Step 1: save a template with a date variable, properties and blocks.
	template := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": meetingType.ID, "title": "Weekly sync {date:long}", "document": document.Document{SchemaVersion: 2, Blocks: []document.Node{
			{ID: "template-heading", Type: "heading", Attrs: map[string]any{"level": 2}, Content: []document.Inline{{Type: "text", Text: "Agenda"}}},
			{ID: "template-body", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Template body"}}},
		}},
		"properties": map[string]any{
			"is_template": true,
			"when":        "{date}",
			"agenda":      "Standup",
			"tags":        []string{tag.ID},
		},
	}))
	if !isTemplateObject(template) {
		t.Fatal("template flag was not stored")
	}

	// Step 2: make it the type default and verify the type registry reports it.
	scenarioStatus(t, api(t, app, "PATCH", "/v1/types/"+meetingType.ID, map[string]any{
		"default_template_id": template.ID,
	}), 200)
	types := decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil))
	foundDefault := false
	for _, typ := range types {
		if typ.ID == meetingType.ID {
			foundDefault = typ.DefaultTemplateID == template.ID
		}
	}
	if !foundDefault {
		t.Fatal("GET /v1/types did not return the type default template")
	}

	// Step 3: create a meeting from the template.
	created := objectFrom(t, api(t, app, "POST", "/v1/objects/from-template", map[string]any{
		"template_id": template.ID, "date": "2026-10-31",
	}))
	if created.ID == template.ID || created.TypeID != meetingType.ID {
		t.Fatalf("from-template returned the wrong object: %+v", created)
	}
	if created.Title != "Weekly sync October 31, 2026" {
		t.Fatalf("date variable was not resolved: %q", created.Title)
	}
	if created.Properties["when"] != "2026-10-31" || created.Properties["agenda"] != "Standup" {
		t.Fatalf("properties were not copied: %#v", created.Properties)
	}
	if _, exists := created.Properties["is_template"]; exists {
		t.Fatal("created object inherited is_template")
	}
	if tags, ok := created.Properties["tags"].([]any); !ok || len(tags) != 1 || tags[0] != tag.ID {
		t.Fatalf("tag relation was not copied: %#v", created.Properties["tags"])
	}
	blocks := created.Document.Blocks
	if len(blocks) != 2 || blocks[0].ID == "template-heading" || blocks[1].ID == "template-body" {
		t.Fatalf("blocks were not deep-copied with fresh ids: %+v", blocks)
	}
	if blocks[0].Content[0].Text != "Agenda" || blocks[1].Content[0].Text != "Template body" {
		t.Fatalf("block payload changed during copy: %+v", blocks)
	}
	// The template itself is untouched.
	original := scenarioGet(t, app, template.ID)
	originalBlocks := original.Document.Blocks
	if originalBlocks[0].ID != "template-heading" {
		t.Fatal("applying the template mutated it")
	}

	// Step 4: templates stay out of normal listings and search.
	listed := queryObjects(t, app, "type_id="+meetingType.ID)
	if scenarioContainsID(objectIDs(listed), template.ID) {
		t.Fatalf("template leaked into the default listing: %v", objectIDs(listed))
	}
	withTemplates := queryObjects(t, app, "type_id="+meetingType.ID+"&include_templates=true")
	if !scenarioContainsID(objectIDs(withTemplates), template.ID) {
		t.Fatalf("include_templates did not return the template: %v", objectIDs(withTemplates))
	}
	search := scenarioSearchIDs(t, app, "Weekly sync")
	if scenarioContainsID(search, template.ID) || !scenarioContainsID(search, created.ID) {
		t.Fatalf("search template filtering is wrong: %v", search)
	}
	searchWithTemplates := api(t, app, "GET", "/v1/search?q=Weekly+sync&include_templates=true", nil)
	scenarioStatus(t, searchWithTemplates, 200)
	if !strings.Contains(searchWithTemplates.Body.String(), template.ID) {
		t.Fatal("include_templates did not include the template in search")
	}

	// Step 5: a daily-note template applied to a brand-new date.
	journalTemplate := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "journal", "title": "Daily {date:long}", "document": documentFixture("Daily body"),
		"properties": map[string]any{
			"is_template": true,
			"date":        "{date}",
		},
	}))
	scenarioStatus(t, api(t, app, "PATCH", "/v1/types/journal", map[string]any{
		"default_template_id": journalTemplate.ID,
	}), 200)
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-11-01?apply_default_template=true", nil))
	if journal.ID != "journal-2026-11-01" || journal.Title != "2026-11-01" {
		t.Fatalf("journal identity is wrong: %+v", journal)
	}
	if journal.Content != "Daily body" || journal.Properties["date"] != "2026-11-01" {
		t.Fatalf("journal did not copy the template: %+v", journal)
	}
	if _, exists := journal.Properties["is_template"]; exists {
		t.Fatal("journal inherited is_template")
	}
	journalBlocks := journal.Document.Blocks
	if len(journalBlocks) != 1 || journalBlocks[0].ID == journalTemplate.Document.Blocks[0].ID || journalBlocks[0].Content[0].Text != "Daily body" {
		t.Fatalf("journal blocks were not copied with fresh ids: %+v", journalBlocks)
	}
	again := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-11-01?apply_default_template=true", nil))
	if again.Revision != journal.Revision {
		t.Fatal("applying the default template again rewrote an existing journal")
	}
	plain := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-11-02", nil))
	if plain.Content != "" || plain.Properties["blocks"] != nil {
		t.Fatalf("journal without the flag applied the default template: %+v", plain)
	}

	// Step 6: converting the template to another type clears the old type's
	// default reference instead of leaving it dangling.
	convertedTemplate := objectFrom(t, api(t, app, "POST", "/v1/objects/"+template.ID+"/convert", map[string]any{
		"target_type_id": "note",
	}))
	if convertedTemplate.TypeID != "note" || !isTemplateObject(convertedTemplate) {
		t.Fatalf("template conversion failed: %+v", convertedTemplate)
	}
	types = decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil))
	for _, typ := range types {
		if typ.ID == meetingType.ID && typ.DefaultTemplateID != "" {
			t.Fatalf("converted template left a dangling default on %s", typ.ID)
		}
	}
}
