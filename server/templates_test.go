package main

import (
	"encoding/json"
	"strings"
	"testing"
	"time"
)

func decodeObjects(t *testing.T, body []byte) []Object {
	t.Helper()
	var out []Object
	if err := json.Unmarshal(body, &out); err != nil {
		t.Fatal(err)
	}
	return out
}

func templateProperty(typ ObjectType) bool {
	for _, raw := range typ.Properties {
		schema, ok := raw.(map[string]any)
		if ok && schemaKey(schema) == "is_template" && schema["type"] == "boolean" {
			return true
		}
	}
	return false
}

func TestTemplateFlagListingSearchAndBacklinks(t *testing.T) {
	app, _, _ := makeApp(t)
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Template target"}))
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Template note", "properties": map[string]any{"is_template": true}, "links": []string{target.ID},
	}))
	normal := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Normal note", "links": []string{target.ID},
	}))

	list := decodeObjects(t, api(t, app, "GET", "/v1/objects?type_id=note", nil).Body.Bytes())
	if len(list) != 2 || list[0].ID == tpl.ID || list[1].ID == tpl.ID {
		t.Fatalf("templates leaked into the default listing: %+v", list)
	}
	withTemplates := decodeObjects(t, api(t, app, "GET", "/v1/objects?type_id=note&include_templates=true", nil).Body.Bytes())
	if len(withTemplates) != 3 {
		t.Fatalf("include_templates did not return the template: %+v", withTemplates)
	}
	if got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+tpl.ID, nil)); got.ID != tpl.ID || !isTemplateObject(got) {
		t.Fatal("GET /v1/objects/{id} must always return a template")
	}
	search := api(t, app, "GET", "/v1/search?q=Template", nil)
	if strings.Contains(search.Body.String(), tpl.ID) {
		t.Fatal("template polluted normal search results")
	}
	search = api(t, app, "GET", "/v1/search?q=Template&include_templates=true", nil)
	if !strings.Contains(search.Body.String(), tpl.ID) {
		t.Fatal("include_templates did not include the template in search")
	}
	backlinks := decodeObjects(t, api(t, app, "GET", "/v1/objects/"+target.ID+"/backlinks", nil).Body.Bytes())
	if len(backlinks) != 1 || backlinks[0].ID != normal.ID {
		t.Fatalf("template polluted normal backlinks: %+v", backlinks)
	}
	backlinks = decodeObjects(t, api(t, app, "GET", "/v1/objects/"+target.ID+"/backlinks?include_templates=true", nil).Body.Bytes())
	if len(backlinks) != 2 {
		t.Fatalf("include_templates did not include the template backlink: %+v", backlinks)
	}
	if rr := api(t, app, "GET", "/v1/objects?include_templates=maybe", nil); rr.Code != 400 {
		t.Fatal("invalid include_templates was accepted")
	}
}

func TestFromTemplateCopiesPropertiesBlocksAndLinks(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Template tag"}))
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "project", "title": "Weekly {date:long}", "content": "Template body",
		"properties": map[string]any{
			"is_template": true,
			"status":      "active",
			"due_date":    "{date}",
			"tags":        []string{tag.ID},
			"rich_text":   "<p>Template body</p>",
			"blocks": []any{
				map[string]any{"id": "template-block-a", "type": "paragraph", "text": "One", "runs": []any{map[string]any{"text": "One", "bold": true}}},
				map[string]any{"id": "template-block-b", "type": "heading", "level": float64(2), "text": "Two"},
			},
		},
		"links": []string{tag.ID},
	}))
	created := objectFrom(t, api(t, app, "POST", "/v1/objects/from-template", map[string]any{
		"template_id": tpl.ID, "date": "2026-10-31",
	}))
	if created.ID == tpl.ID || created.TypeID != "project" || created.Title != "Weekly October 31, 2026" {
		t.Fatalf("unexpected created object: %+v", created)
	}
	if created.Content != "Template body" || created.Properties["status"] != "active" || created.Properties["due_date"] != "2026-10-31" {
		t.Fatalf("properties or content were not copied: %+v", created.Properties)
	}
	if _, exists := created.Properties["is_template"]; exists {
		t.Fatal("created object inherited is_template")
	}
	tags, ok := created.Properties["tags"].([]any)
	if !ok || len(tags) != 1 || tags[0] != tag.ID {
		t.Fatalf("tag relation was not copied: %#v", created.Properties["tags"])
	}
	if len(created.Links) != 1 || created.Links[0] != tag.ID {
		t.Fatalf("explicit links were not copied: %+v", created.Links)
	}
	blocks, ok := created.Properties["blocks"].([]any)
	if !ok || len(blocks) != 2 {
		t.Fatalf("blocks were not copied: %#v", created.Properties["blocks"])
	}
	seen := map[string]bool{}
	for _, raw := range blocks {
		block := raw.(map[string]any)
		id, _ := block["id"].(string)
		if id == "" || id == "template-block-a" || id == "template-block-b" || seen[id] {
			t.Fatalf("block id was not freshly minted: %#v", block)
		}
		seen[id] = true
	}
	if blocks[0].(map[string]any)["text"] != "One" || blocks[1].(map[string]any)["level"] != float64(2) {
		t.Fatalf("block payload changed during copy: %#v", blocks)
	}
	original := objectFrom(t, api(t, app, "GET", "/v1/objects/"+tpl.ID, nil))
	originalBlocks := original.Properties["blocks"].([]any)
	if originalBlocks[0].(map[string]any)["id"] != "template-block-a" || !isTemplateObject(original) {
		t.Fatal("template was mutated by applying it")
	}
	// Deep copy: editing the new object must not change the template.
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+created.ID, map[string]any{"properties": map[string]any{"status": "done"}}))
	original = objectFrom(t, api(t, app, "GET", "/v1/objects/"+tpl.ID, nil))
	if original.Properties["status"] != "active" {
		t.Fatal("created object shares property storage with its template")
	}
	custom := objectFrom(t, api(t, app, "POST", "/v1/objects/from-template", map[string]any{
		"template_id": tpl.ID, "title": "Custom {date:iso}", "date": "2026-10-31",
	}))
	if custom.Title != "Custom 2026-10-31" {
		t.Fatalf("explicit title was not resolved: %q", custom.Title)
	}
	if rr := api(t, app, "POST", "/v1/objects/from-template", map[string]any{"template_id": tpl.ID, "title": "Custom 2026-10-31", "date": "2026-10-31"}); rr.Code != 400 {
		t.Fatal("duplicate resolved title was accepted")
	}
	for _, body := range []map[string]any{
		{"template_id": "missing"},
		{"template_id": tag.ID},
		{"template_id": tpl.ID, "date": "2026-99-99"},
		{},
	} {
		if rr := api(t, app, "POST", "/v1/objects/from-template", body); rr.Code != 400 && rr.Code != 404 {
			t.Fatalf("invalid from-template request accepted: %+v => %d %s", body, rr.Code, rr.Body.String())
		}
	}
}

func TestDateVariableFormats(t *testing.T) {
	cases := map[string]string{
		"Journal {date}":                   "Journal 2026-10-31",
		"Journal {date:iso}":               "Journal 2026-10-31",
		"Journal {date:long}":              "Journal October 31, 2026",
		"Journal {date:short}":             "Journal 10/31/26",
		"Journal {date:weekday}":           "Journal Saturday",
		"Journal {date:unknown}":           "Journal {date:unknown}",
		"{date:iso} / {date:weekday}":      "2026-10-31 / Saturday",
		"no variables":                     "no variables",
		"Journal {date:long} {date:short}": "Journal October 31, 2026 10/31/26",
		"Journal {date:}":                  "Journal 2026-10-31",
		"Journal {date:long":               "Journal {date:long",
		"Journal {date:LONG}":              "Journal October 31, 2026",
	}
	for input, want := range cases {
		if got := resolveDateVariables(input, "2026-10-31"); got != want {
			t.Errorf("resolveDateVariables(%q) = %q, want %q", input, got, want)
		}
	}

	app, _, _ := makeApp(t)
	typ := decodeBody[ObjectType](t, api(t, app, "POST", "/v1/types", map[string]any{
		"name": "Meeting", "icon": "calendar", "color": "#123456",
		"properties": []any{
			map[string]any{"id": "when", "type": "date"},
			map[string]any{"id": "week_start", "type": "date"},
			map[string]any{"id": "label", "type": "text"},
		},
	}))
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": typ.ID, "title": "Meeting {date:long} / {date:short} / {date:weekday} / {date:iso} / {date}",
		"properties": map[string]any{"is_template": true, "when": "{date}", "week_start": "{date:iso}", "label": "Week {date:iso}"},
	}))
	created := objectFrom(t, api(t, app, "POST", "/v1/objects/from-template", map[string]any{
		"template_id": tpl.ID, "date": "2026-10-31",
	}))
	wantTitle := "Meeting October 31, 2026 / 10/31/26 / Saturday / 2026-10-31 / 2026-10-31"
	if created.Title != wantTitle || created.Properties["when"] != "2026-10-31" || created.Properties["week_start"] != "2026-10-31" {
		t.Fatalf("date variables did not resolve: title=%q props=%#v", created.Title, created.Properties)
	}
	if created.Properties["label"] != "Week {date:iso}" {
		t.Fatalf("text properties must not be substituted: %#v", created.Properties["label"])
	}
	unknown := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": typ.ID, "title": "Keep {date:unknown}", "properties": map[string]any{"is_template": true},
	}))
	kept := objectFrom(t, api(t, app, "POST", "/v1/objects/from-template", map[string]any{"template_id": unknown.ID, "date": "2026-10-31"}))
	if kept.Title != "Keep {date:unknown}" {
		t.Fatalf("unknown format was not left as-is: %q", kept.Title)
	}
	bad := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": typ.ID, "title": "Bad date property", "properties": map[string]any{"is_template": true, "when": "{date:long}"},
	}))
	if rr := api(t, app, "POST", "/v1/objects/from-template", map[string]any{"template_id": bad.ID, "date": "2026-10-31"}); rr.Code != 400 {
		t.Fatal("a formatted date was accepted in a typed date property")
	}
}

func TestTypeDefaultTemplateValidationAndJournal(t *testing.T) {
	app, _, _ := makeApp(t)
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "journal", "title": "Daily {date:long}", "content": "Daily body",
		"properties": map[string]any{
			"is_template": true,
			"date":        "{date}",
			"blocks":      []any{map[string]any{"id": "daily-block", "type": "paragraph", "text": "Daily body"}},
		},
	}))
	if rr := api(t, app, "POST", "/v1/types", map[string]any{
		"name": "New type", "icon": "text-x-generic", "color": "#123456", "default_template_id": tpl.ID,
	}); rr.Code != 400 {
		t.Fatal("a new type accepted a default template")
	}
	if rr := api(t, app, "PATCH", "/v1/types/journal", map[string]any{"default_template_id": tpl.ID}); rr.Code != 200 {
		t.Fatalf("default template rejected: %s", rr.Body.String())
	}
	types := decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil))
	found := false
	for _, typ := range types {
		if typ.ID == "journal" {
			found = typ.DefaultTemplateID == tpl.ID
		}
	}
	if !found {
		t.Fatal("GET /v1/types did not return default_template_id")
	}
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-31?apply_default_template=true", nil))
	if journal.ID != "journal-2026-10-31" || journal.Title != "2026-10-31" || journal.Content != "Daily body" {
		t.Fatalf("journal was not created from its default template: %+v", journal)
	}
	if journal.Properties["date"] != "2026-10-31" || journal.Properties["is_template"] != nil {
		t.Fatalf("journal properties are wrong: %#v", journal.Properties)
	}
	blocks, ok := journal.Properties["blocks"].([]any)
	if !ok || len(blocks) != 1 || blocks[0].(map[string]any)["id"] == "daily-block" {
		t.Fatalf("journal blocks were not copied with fresh ids: %#v", journal.Properties["blocks"])
	}
	again := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-31?apply_default_template=true", nil))
	if again.Revision != journal.Revision {
		t.Fatal("applying the default template again changed an existing journal note")
	}
	plain := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-11-01", nil))
	if plain.Content != "" || plain.Properties["blocks"] != nil {
		t.Fatalf("journal without the flag applied the default template: %+v", plain)
	}
	if rr := api(t, app, "GET", "/v1/journal/2026-11-02?apply_default_template=maybe", nil); rr.Code != 400 {
		t.Fatal("invalid apply_default_template was accepted")
	}

	noteTpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Note template", "properties": map[string]any{"is_template": true},
	}))
	plainJournal := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "journal", "title": "Plain journal"}))
	for _, body := range []map[string]any{
		{"default_template_id": noteTpl.ID},
		{"default_template_id": plainJournal.ID},
		{"default_template_id": "missing"},
	} {
		if rr := api(t, app, "PATCH", "/v1/types/journal", body); rr.Code != 400 {
			t.Fatalf("invalid default template accepted: %+v => %d", body, rr.Code)
		}
	}
	if rr := api(t, app, "PATCH", "/v1/types/journal", map[string]any{"default_template_id": ""}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	types = decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil))
	for _, typ := range types {
		if typ.ID == "journal" && typ.DefaultTemplateID != "" {
			t.Fatal("empty default_template_id did not clear the reference")
		}
	}
	// JSON null also clears the optional reference.
	if rr := api(t, app, "PATCH", "/v1/types/journal", map[string]any{"default_template_id": tpl.ID}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "PATCH", "/v1/types/journal", map[string]any{"default_template_id": nil}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	types = decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil))
	for _, typ := range types {
		if typ.ID == "journal" && typ.DefaultTemplateID != "" {
			t.Fatal("null default_template_id did not clear the reference")
		}
	}
	// Deleting a template clears the type reference in the same transaction.
	if rr := api(t, app, "PATCH", "/v1/types/journal", map[string]any{"default_template_id": tpl.ID}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+tpl.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	types = decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil))
	for _, typ := range types {
		if typ.ID == "journal" && typ.DefaultTemplateID != "" {
			t.Fatal("deleting a template left a dangling default reference")
		}
	}
}

func TestTemplatePropertyExistsOnEveryTypeAndValidation(t *testing.T) {
	app, _, _ := makeApp(t)
	for _, typ := range decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil)) {
		if !templateProperty(typ) {
			t.Fatalf("type %s is missing the reserved is_template property", typ.ID)
		}
	}
	custom := decodeBody[ObjectType](t, api(t, app, "POST", "/v1/types", map[string]any{
		"name": "Custom", "icon": "text-x-generic", "color": "#112233", "properties": []any{"custom_field"},
	}))
	if !templateProperty(custom) {
		t.Fatal("new custom type is missing is_template")
	}
	custom = decodeBody[ObjectType](t, api(t, app, "PATCH", "/v1/types/"+custom.ID, map[string]any{"properties": []any{}}))
	if !templateProperty(custom) {
		t.Fatal("schema patch dropped is_template")
	}
	if rr := api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": custom.ID, "title": "String flag", "properties": map[string]any{"is_template": "true"},
	}); rr.Code != 400 {
		t.Fatal("non-boolean is_template was accepted")
	}
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": custom.ID, "title": "", "properties": map[string]any{"is_template": true},
	}))
	if tpl.Title != "" {
		t.Fatalf("empty template title was changed: %q", tpl.Title)
	}
	listed := decodeObjects(t, api(t, app, "GET", "/v1/objects?type_id="+custom.ID, nil).Body.Bytes())
	if len(listed) != 0 {
		t.Fatal("custom type template leaked into the default listing")
	}
	created := objectFrom(t, api(t, app, "POST", "/v1/objects/from-template", map[string]any{"template_id": tpl.ID}))
	if created.Title != "" {
		t.Fatalf("empty template title did not default to empty: %q", created.Title)
	}
}

func TestTemplateDefaultCleanupOnUnflagAndTypeChange(t *testing.T) {
	app, _, _ := makeApp(t)
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "project", "title": "Project template", "properties": map[string]any{"is_template": true},
	}))
	if rr := api(t, app, "PATCH", "/v1/types/project", map[string]any{"default_template_id": tpl.ID}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+tpl.ID, map[string]any{"properties": map[string]any{}}))
	types := decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil))
	for _, typ := range types {
		if typ.ID == "project" && typ.DefaultTemplateID != "" {
			t.Fatal("unflagging a template left the type default in place")
		}
	}
	// Re-flag, set the default again, then change the template type.
	tpl = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+tpl.ID, map[string]any{"properties": map[string]any{"is_template": true}}))
	if rr := api(t, app, "PATCH", "/v1/types/project", map[string]any{"default_template_id": tpl.ID}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+tpl.ID, map[string]any{"type_id": "note"}))
	types = decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil))
	for _, typ := range types {
		if typ.ID == "project" && typ.DefaultTemplateID != "" {
			t.Fatal("changing a template type left the old type default in place")
		}
	}
}

func TestSyncPushUnflaggingTemplateClearsDefault(t *testing.T) {
	app, _, _ := makeApp(t)
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "project", "title": "Sync template", "properties": map[string]any{"is_template": true},
	}))
	if rr := api(t, app, "PATCH", "/v1/types/project", map[string]any{"default_template_id": tpl.ID}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	push := map[string]any{
		"device_id": "pixel-template-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-unflag-template-1", "action": "put", "base_revision": tpl.Revision,
			"object": map[string]any{
				"id": tpl.ID, "type_id": "project", "title": tpl.Title, "content": "",
				"properties": map[string]any{"is_template": false}, "links": []string{},
				"created_at": tpl.CreatedAt, "updated_at": now(),
			},
		}},
	}
	resp := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", push))
	if len(resp.Errors) != 0 || len(resp.Applied) != 1 {
		t.Fatalf("push failed: %+v", resp)
	}
	types := decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil))
	for _, typ := range types {
		if typ.ID == "project" && typ.DefaultTemplateID != "" {
			t.Fatal("sync unflagging left a dangling default template")
		}
	}
}

func TestTemplateTitleDoesNotBlockResolvedTitle(t *testing.T) {
	app, _, _ := makeApp(t)
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Same name", "properties": map[string]any{"is_template": true},
	}))
	created := objectFrom(t, api(t, app, "POST", "/v1/objects/from-template", map[string]any{"template_id": tpl.ID}))
	if created.Title != "Same name" {
		t.Fatalf("resolved title changed: %q", created.Title)
	}
	second := objectFrom(t, api(t, app, "POST", "/v1/objects/from-template", map[string]any{"template_id": tpl.ID}))
	if second.ID == created.ID || second.Title != "Same name (2)" {
		t.Fatalf("repeated default title was not suffixed: %+v", second)
	}
	if rr := api(t, app, "POST", "/v1/objects/from-template", map[string]any{"template_id": tpl.ID, "title": "Same name"}); rr.Code != 400 {
		t.Fatal("explicit title conflict was auto-suffixed")
	}
	if rr := api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Same name"}); rr.Code != 400 {
		t.Fatal("normal title uniqueness was relaxed")
	}
}

func TestCaptureIgnoresWebTemplatesForDuplicateDetection(t *testing.T) {
	app, _, _ := makeApp(t)
	url := "https://example.com/article"
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "web", "title": "Bookmark template",
		"properties": map[string]any{"is_template": true, "url": url},
	}))
	captured := objectFrom(t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": url, "selection": "Saved text", "fetch_preview": false,
	}))
	if captured.ID == tpl.ID || captured.TypeID != "web" {
		t.Fatalf("capture treated a template as an existing link: %+v", captured)
	}
	duplicate := api(t, app, "POST", "/v1/capture", map[string]any{
		"url": url, "selection": "Saved again", "fetch_preview": false,
	})
	if duplicate.Code != 200 || !strings.Contains(duplicate.Body.String(), `"duplicate":true`) {
		t.Fatalf("normal duplicate detection broke: %d %s", duplicate.Code, duplicate.Body.String())
	}
}

func TestFromTemplateRejectsInvalidBlocks(t *testing.T) {
	app, _, _ := makeApp(t)
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Broken blocks", "properties": map[string]any{"is_template": true, "blocks": "not-an-array"},
	}))
	rr := api(t, app, "POST", "/v1/objects/from-template", map[string]any{"template_id": tpl.ID})
	if rr.Code != 400 || !strings.Contains(rr.Body.String(), "blocks") {
		t.Fatalf("invalid blocks were accepted: %d %s", rr.Code, rr.Body.String())
	}
}

func TestJournalDefaultTemplateMissingFallsBackToPlainNote(t *testing.T) {
	app, _, _ := makeApp(t)
	// Simulate a manual store edit that leaves a dangling reference.
	typ, err := app.store.Type("journal")
	if err != nil {
		t.Fatal(err)
	}
	typ.DefaultTemplateID = "missing-template"
	if err := app.store.Put("type", typ.ID, typ); err != nil {
		t.Fatal(err)
	}
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-12-01?apply_default_template=true", nil))
	if journal.Content != "" || journal.Properties["date"] != "2026-12-01" {
		t.Fatalf("dangling default did not fall back to a plain note: %+v", journal)
	}
}

func TestTemplateDateDefaultsToToday(t *testing.T) {
	app, _, _ := makeApp(t)
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Today {date:iso}", "properties": map[string]any{"is_template": true},
	}))
	created := objectFrom(t, api(t, app, "POST", "/v1/objects/from-template", map[string]any{"template_id": tpl.ID}))
	if created.Title != "Today "+time.Now().Format("2006-01-02") {
		t.Fatalf("default date is not server-local today: %q", created.Title)
	}
}

func TestConvertTemplateClearsOldTypeDefault(t *testing.T) {
	app, _, _ := makeApp(t)
	source := makeType(t, app, "Template source", []any{})
	target := makeType(t, app, "Template target", []any{})
	tpl := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": source.ID, "title": "Movable template", "properties": map[string]any{"is_template": true},
	}))
	if rr := api(t, app, "PATCH", "/v1/types/"+source.ID, map[string]any{"default_template_id": tpl.ID}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	converted := objectFrom(t, api(t, app, "POST", "/v1/objects/"+tpl.ID+"/convert", map[string]any{"target_type_id": target.ID}))
	if converted.TypeID != target.ID || !isTemplateObject(converted) {
		t.Fatalf("template conversion failed: %+v", converted)
	}
	types := decodeBody[[]ObjectType](t, api(t, app, "GET", "/v1/types", nil))
	for _, typ := range types {
		if typ.ID == source.ID && typ.DefaultTemplateID != "" {
			t.Fatalf("converted template left a dangling default on %s", typ.ID)
		}
	}
}
