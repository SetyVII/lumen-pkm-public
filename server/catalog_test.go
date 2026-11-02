package main

import (
	"encoding/json"
	"fmt"
	"github.com/google/jsonschema-go/jsonschema"
	"net/url"
	"os"
	"strings"
	"testing"
	"unicode/utf8"
)

func catalogFixture(t *testing.T, app *App, id string, properties map[string]any, body string) Object {
	t.Helper()
	d := documentFixture(body)
	object := Object{ID: id, TypeID: "note", Title: "Catalog " + id, Document: &d, Properties: properties, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
	if err := app.saveObject(t.Context(), &object); err != nil {
		t.Fatal(err)
	}
	return object
}

func TestCatalogMetadataPagesDoNotReadBodies(t *testing.T) {
	app, _, _ := makeApp(t)
	for i := range 140 {
		catalogFixture(t, app, fmt.Sprintf("catalog-%03d", i), map[string]any{"reader_content": "PRIVATE_READER_SENTINEL"}, "PRIVATE_DOCUMENT_SENTINEL")
	}
	var encrypted []byte
	if err := app.store.db.QueryRow("SELECT payload FROM records WHERE kind='object' AND id='catalog-139'").Scan(&encrypted); err != nil {
		t.Fatal(err)
	}
	defer func() {
		_, _ = app.store.db.Exec("UPDATE records SET payload=? WHERE kind='object' AND id='catalog-139'", encrypted)
	}()
	if _, err := app.store.db.Exec("UPDATE records SET payload=? WHERE kind='object' AND id='catalog-139'", []byte("broken ciphertext")); err != nil {
		t.Fatal(err)
	}
	seen := map[string]bool{}
	path := "/v1/catalog?limit=64"
	for pages := 0; pages < 4; pages++ {
		rr := api(t, app, "GET", path, nil)
		if strings.Contains(rr.Body.String(), "PRIVATE_") || strings.Contains(rr.Body.String(), `"document"`) || strings.Contains(rr.Body.String(), `"content"`) {
			t.Fatal("catalog exposed body content")
		}
		page := decodeBody[catalogResponse](t, rr)
		if page.Projection != "catalog/v1" || len(page.Entries) > 64 {
			t.Fatal("not an explicit bounded catalog projection")
		}
		for _, entry := range page.Entries {
			if seen[entry.ID] {
				t.Fatal("unchanged object repeated across pages")
			}
			seen[entry.ID] = true
		}
		if !page.HasMore {
			if page.NextCursor != "" {
				t.Fatal("terminal cursor present")
			}
			break
		}
		if page.NextCursor == "" {
			t.Fatal("page did not advance")
		}
		path = "/v1/catalog?limit=64&cursor=" + url.QueryEscape(page.NextCursor)
	}
	if len(seen) != 140 {
		t.Fatalf("catalog lost objects: %d", len(seen))
	}
	if rr := api(t, app, "GET", "/v1/catalog?q=catalog-139&excerpt_chars=10", nil); rr.Code != 500 {
		t.Fatal("explicit excerpt did not read requested payload")
	}
}

func TestCatalogRequestedFieldsAreExplicitBoundedAndLosslessAtSource(t *testing.T) {
	app, _, _ := makeApp(t)
	object := catalogFixture(t, app, "requested-fields", map[string]any{"status": "todo", "reader_content": "READER_PRIVATE", "large": strings.Repeat("z", catalogPropertyBytes+1), "custom": map[string]any{"preserve": true}}, strings.Repeat("é😀", 1000))
	page := decodeBody[catalogResponse](t, api(t, app, "GET", "/v1/catalog?property=status&property=custom&property=large&excerpt_chars=7", nil))
	if len(page.Entries) != 1 {
		t.Fatal("missing catalog entry")
	}
	entry := page.Entries[0]
	if entry.Properties["status"] != "todo" || entry.Properties["custom"].(map[string]any)["preserve"] != true || len(entry.OmittedProperties) != 1 || entry.OmittedProperties[0] != "large" {
		t.Fatal("requested properties lost or silently truncated")
	}
	if utf8.RuneCountInString(entry.Excerpt) != 7 || !entry.ExcerptTruncated {
		t.Fatal("excerpt is not rune bounded")
	}
	full := objectFrom(t, api(t, app, "GET", "/v1/objects/"+object.ID, nil))
	if full.Document == nil || full.Properties["reader_content"] != "READER_PRIVATE" || full.Properties["large"] != object.Properties["large"] {
		t.Fatal("catalog changed full source object")
	}
	for _, path := range []string{"/v1/catalog?limit=129", "/v1/catalog?cursor=invalid!", "/v1/catalog?property=reader_content", "/v1/catalog?excerpt_chars=513", "/v1/catalog?content_empty=true"} {
		if rr := api(t, app, "GET", path, nil); rr.Code != 400 {
			t.Fatalf("invalid catalog request accepted %s %d", path, rr.Code)
		}
	}
}

func TestCatalogByteBudgetAdvancesBeforeRequestedPageIsFull(t *testing.T) {
	app, _, _ := makeApp(t)
	properties := map[string]any{}
	query := url.Values{}
	for i := range 64 {
		key := fmt.Sprintf("p%02d", i)
		properties[key] = strings.Repeat("x", catalogPropertyBytes-2)
		query.Add("property", key)
	}
	catalogFixture(t, app, "budget-a", properties, "A")
	catalogFixture(t, app, "budget-b", properties, "B")
	rr := api(t, app, "GET", "/v1/catalog?"+query.Encode(), nil)
	page := decodeBody[catalogResponse](t, rr)
	if len(page.Entries) != 1 || !page.HasMore || page.NextCursor == "" || rr.Body.Len() > catalogPageBytes {
		t.Fatalf("byte budget not enforced: entries=%d bytes=%d more=%v", len(page.Entries), rr.Body.Len(), page.HasMore)
	}
	query.Set("cursor", page.NextCursor)
	next := decodeBody[catalogResponse](t, api(t, app, "GET", "/v1/catalog?"+query.Encode(), nil))
	if len(next.Entries) != 1 || next.Entries[0].ID == page.Entries[0].ID || next.HasMore {
		t.Fatal("byte-limited cursor skipped/repeated data")
	}
	encoded, _ := json.Marshal(next.Entries[0].Properties)
	if len(encoded) < 4<<20 {
		t.Fatal("budget test did not exercise large projected properties")
	}
}

func TestCatalogSchemaMatchesFixtureAndActualResponse(t *testing.T) {
	schemaRaw, err := os.ReadFile("../contracts/catalog.schema.json")
	if err != nil {
		t.Fatal(err)
	}
	var schema jsonschema.Schema
	if err = json.Unmarshal(schemaRaw, &schema); err != nil {
		t.Fatal(err)
	}
	resolved, err := schema.Resolve(nil)
	if err != nil {
		t.Fatal(err)
	}
	fixture, err := os.ReadFile("../contracts/fixtures/catalog-v1/summary-page.json")
	if err != nil {
		t.Fatal(err)
	}
	app, _, _ := makeApp(t)
	catalogFixture(t, app, "schema-object", map[string]any{"status": "todo"}, "Text")
	actual := api(t, app, "GET", "/v1/catalog?property=status&excerpt_chars=2", nil)
	for _, raw := range [][]byte{fixture, actual.Body.Bytes()} {
		var value any
		if err = json.Unmarshal(raw, &value); err != nil {
			t.Fatal(err)
		}
		if err = resolved.Validate(value); err != nil {
			t.Fatal(err)
		}
	}
}
