package main

import (
	"encoding/json"
	"fmt"
	"net/url"
	"strings"
	"testing"
)

func aliasesOf(t *testing.T, o Object) []string {
	t.Helper()
	raw, exists := o.Properties[aliasesPropertyKey]
	if !exists {
		return nil
	}
	values, err := aliasStrings(raw)
	if err != nil {
		t.Fatalf("stored aliases are invalid: %v", err)
	}
	return values
}

func searchFor(t *testing.T, app *App, query string) []searchResult {
	t.Helper()
	rr := api(t, app, "GET", "/v1/search?q="+url.QueryEscape(query), nil)
	if rr.Code != 200 {
		t.Fatalf("search HTTP %d: %s", rr.Code, rr.Body.String())
	}
	var results []searchResult
	if err := json.Unmarshal(rr.Body.Bytes(), &results); err != nil {
		t.Fatal(err)
	}
	return results
}

func TestAliasesAreStoredNormalizedAndReturned(t *testing.T) {
	app, _, _ := makeApp(t)
	want := []string{"HP1", "The Stone"}
	created := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note",
		"title":   "Harry Potter and the Philosopher's Stone",
		"properties": map[string]any{
			"aliases": []any{"  HP1  ", "hp1", "", "   ", "The Stone"},
		},
	}))
	if got := aliasesOf(t, created); !sameStrings(got, want) {
		t.Fatalf("created aliases = %v, want %v", got, want)
	}
	detail := objectFrom(t, api(t, app, "GET", "/v1/objects/"+created.ID, nil))
	if got := aliasesOf(t, detail); !sameStrings(got, want) {
		t.Fatalf("detail aliases = %v, want %v", got, want)
	}
	var listed []Object
	if err := json.Unmarshal(api(t, app, "GET", "/v1/objects", nil).Body.Bytes(), &listed); err != nil {
		t.Fatal(err)
	}
	found := false
	for _, o := range listed {
		if o.ID == created.ID {
			found = true
			if got := aliasesOf(t, o); !sameStrings(got, want) {
				t.Fatalf("listed aliases = %v, want %v", got, want)
			}
		}
	}
	if !found {
		t.Fatal("created object is missing from GET /v1/objects")
	}
	// The in-memory index carries aliases as metadata, like titles.
	meta, ok := app.store.index.getMeta(created.ID)
	if !ok || !sameStrings(meta.Aliases, want) {
		t.Fatalf("index aliases = %v, want %v", meta.Aliases, want)
	}
	// Resending the same aliases is a no-op and must not bump the revision.
	revision := created.Revision
	same := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+created.ID, map[string]any{
		"title": created.Title, "content": created.Content, "properties": created.Properties, "links": created.Links,
	}))
	if same.Revision != revision {
		t.Fatalf("no-op alias patch bumped revision %d -> %d", revision, same.Revision)
	}
}

func TestAliasesAreAvailableOnCustomTypes(t *testing.T) {
	app, _, _ := makeApp(t)
	typ := decodeBody[ObjectType](t, api(t, app, "POST", "/v1/types", map[string]any{
		"name": "Receta", "icon": "text-x-generic", "color": "#123456", "properties": []any{},
	}))
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id":    typ.ID,
		"title":      "Paella",
		"properties": map[string]any{"aliases": []any{"Arroz valenciano"}},
	}))
	if got, want := aliasesOf(t, o), []string{"Arroz valenciano"}; !sameStrings(got, want) {
		t.Fatalf("custom type aliases = %v, want %v", got, want)
	}
}

func TestAliasesCanBeReplacedAndCleared(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Alias edit",
		"properties": map[string]any{"aliases": []any{"First"}},
	}))
	replaced := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{
		"properties": map[string]any{"aliases": []any{"Second", "Third"}},
	}))
	if got, want := aliasesOf(t, replaced), []string{"Second", "Third"}; !sameStrings(got, want) {
		t.Fatalf("replaced aliases = %v, want %v", got, want)
	}
	if replaced.Revision != o.Revision+1 {
		t.Fatalf("alias replacement did not bump the revision: %+v", replaced)
	}
	// An empty array clears the property instead of storing [].
	cleared := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{
		"properties": map[string]any{"aliases": []any{}},
	}))
	if _, exists := cleared.Properties[aliasesPropertyKey]; exists {
		t.Fatalf("empty aliases must remove the key: %+v", cleared.Properties)
	}
	// properties is a replacement document: omitting the key also clears it.
	again := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{
		"properties": map[string]any{"aliases": []any{"Temporary"}},
	}))
	if got := aliasesOf(t, again); !sameStrings(got, []string{"Temporary"}) {
		t.Fatalf("aliases were not restored: %v", got)
	}
	omitted := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{
		"properties": map[string]any{"summary": "Sin alias"},
	}))
	if _, exists := omitted.Properties[aliasesPropertyKey]; exists {
		t.Fatalf("omitted aliases must be removed: %+v", omitted.Properties)
	}
}

func TestAliasValidationRejectsInvalidValues(t *testing.T) {
	app, _, _ := makeApp(t)
	tooMany := make([]any, 0, maxAliasesPerObject+1)
	for i := 0; i <= maxAliasesPerObject; i++ {
		tooMany = append(tooMany, fmt.Sprintf("alias-%d", i))
	}
	cases := []struct {
		name  string
		value any
		want  string
	}{
		{"not an array", "HP1", "array of strings"},
		{"non-string entry", []any{"ok", 7}, "array of strings"},
		{"too many", tooMany, "up to 50 aliases"},
		{"too long", []any{strings.Repeat("a", maxAliasLength+1)}, "200 characters"},
	}
	for i, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			rr := api(t, app, "POST", "/v1/objects", map[string]any{
				"type_id": "note", "title": fmt.Sprintf("Invalid alias %d", i),
				"properties": map[string]any{"aliases": tc.value},
			})
			if rr.Code != 400 || !strings.Contains(rr.Body.String(), tc.want) {
				t.Fatalf("expected 400 containing %q, got %d: %s", tc.want, rr.Code, rr.Body.String())
			}
		})
	}
	// The same rules apply to PATCH.
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Alias limits"}))
	rr := api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{
		"properties": map[string]any{"aliases": []any{strings.Repeat("b", maxAliasLength+1)}},
	})
	if rr.Code != 400 || !strings.Contains(rr.Body.String(), "200 characters") {
		t.Fatalf("PATCH accepted an oversized alias: %d %s", rr.Code, rr.Body.String())
	}
	// Exact limits are accepted.
	atLimit := make([]any, 0, maxAliasesPerObject)
	for i := 0; i < maxAliasesPerObject; i++ {
		atLimit = append(atLimit, fmt.Sprintf("%03d", i)+strings.Repeat("x", maxAliasLength-3))
	}
	accepted := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Alias at the limit",
		"properties": map[string]any{"aliases": atLimit},
	}))
	if got := aliasesOf(t, accepted); len(got) != maxAliasesPerObject {
		t.Fatalf("accepted %d aliases, want %d", len(got), maxAliasesPerObject)
	}
}

func TestSearchMatchesAliasesAndReportsThem(t *testing.T) {
	app, _, _ := makeApp(t)
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "HP1", "content": "Wizard school",
		"properties": map[string]any{"aliases": []any{"Harry Potter", "The Stone"}},
	}))
	objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Unrelated"}))

	byAlias := searchFor(t, app, "harry")
	if len(byAlias) != 1 || byAlias[0].ID != book.ID || byAlias[0].MatchedAlias != "Harry Potter" {
		t.Fatalf("alias search = %+v", byAlias)
	}
	if byAlias[0].Title != book.Title || byAlias[0].TypeID != book.TypeID {
		t.Fatalf("alias search lost object fields: %+v", byAlias[0])
	}
	// A title hit keeps the historical shape: no matched_alias.
	byTitle := searchFor(t, app, "hp1")
	if len(byTitle) != 1 || byTitle[0].ID != book.ID || byTitle[0].MatchedAlias != "" {
		t.Fatalf("title search = %+v", byTitle)
	}
	rr := api(t, app, "GET", "/v1/search?q=hp1", nil)
	if strings.Contains(rr.Body.String(), "matched_alias") {
		t.Fatalf("empty matched_alias must be omitted: %s", rr.Body.String())
	}
	// Content hits do not report an alias either.
	byContent := searchFor(t, app, "wizard")
	if len(byContent) != 1 || byContent[0].ID != book.ID || byContent[0].MatchedAlias != "" {
		t.Fatalf("content search = %+v", byContent)
	}
	// GET /v1/objects shares the alias filter but keeps returning plain objects.
	var listed []Object
	if err := json.Unmarshal(api(t, app, "GET", "/v1/objects?q=harry", nil).Body.Bytes(), &listed); err != nil {
		t.Fatal(err)
	}
	if len(listed) != 1 || listed[0].ID != book.ID {
		t.Fatalf("object list alias filter = %+v", listed)
	}
	// type_id narrows alias matches like any other search.
	if got := decodeBody[[]searchResult](t, api(t, app, "GET", "/v1/search?q=harry&type_id=tag", nil)); len(got) != 0 {
		t.Fatalf("type filter ignored aliases: %+v", got)
	}
	if got := searchFor(t, app, "zephyrine"); len(got) != 0 {
		t.Fatalf("unexpected match: %+v", got)
	}
}

func TestAliasRemovalRemovesItFromSearchAndSurvivesRestart(t *testing.T) {
	app, path, key := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Ephemeral alias",
		"properties": map[string]any{"aliases": []any{"Zephyrine"}},
	}))
	if got := searchFor(t, app, "zephyrine"); len(got) != 1 || got[0].MatchedAlias != "Zephyrine" {
		t.Fatalf("alias was not searchable: %+v", got)
	}
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{"properties": map[string]any{}}))
	if got := searchFor(t, app, "zephyrine"); len(got) != 0 {
		t.Fatalf("removed alias still matches: %+v", got)
	}
	// Aliases are rebuilt from the encrypted properties on restart.
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{
		"properties": map[string]any{"aliases": []any{"Zephyrine"}},
	}))
	if err := app.store.Close(); err != nil {
		t.Fatal(err)
	}
	reopened, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	app.store = reopened
	if got := searchFor(t, app, "zephyrine"); len(got) != 1 || got[0].MatchedAlias != "Zephyrine" {
		t.Fatalf("alias lost after restart: %+v", got)
	}
	meta, ok := app.store.index.getMeta(o.ID)
	if !ok || !sameStrings(meta.Aliases, []string{"Zephyrine"}) {
		t.Fatalf("index aliases after restart = %v", meta.Aliases)
	}
}

func TestSyncPushUnionsAliasesAndNormalizesThem(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Shared aliases",
		"properties": map[string]any{"aliases": []any{"Server Name"}},
	}))
	o = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{"content": "server text"}))
	push := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{
		"device_id": "pixel-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-alias-0001", "action": "put", "base_revision": o.Revision - 1,
			"object": map[string]any{
				"id": o.ID, "type_id": "note", "title": o.Title, "content": "",
				"properties": map[string]any{"aliases": []any{"server name", "Mobile Name"}},
				"links":      []string{}, "created_at": o.CreatedAt, "updated_at": o.UpdatedAt,
			},
		}},
	}))
	if len(push.Errors) != 0 || len(push.Conflicts) != 1 || push.Conflicts[0].Resolution != "server_kept_mobile_appended" {
		t.Fatalf("unexpected sync result: %+v", push)
	}
	merged := objectFrom(t, api(t, app, "GET", "/v1/objects/"+o.ID, nil))
	if got, want := aliasesOf(t, merged), []string{"Server Name", "Mobile Name"}; !sameStrings(got, want) {
		t.Fatalf("merged aliases = %v, want %v", got, want)
	}
}
