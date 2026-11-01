package main

import (
	"encoding/json"
	"fmt"
	"net/http/httptest"
	"strings"
	"testing"
)

func createNotes(t *testing.T, app *App, prefix string, count int) []Object {
	t.Helper()
	out := make([]Object, 0, count)
	for i := 1; i <= count; i++ {
		o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
			"type_id": "note", "title": fmt.Sprintf("%s %02d", prefix, i),
		}))
		out = append(out, o)
	}
	return out
}

func decodeObjectPage(t *testing.T, rr *httptest.ResponseRecorder) []Object {
	t.Helper()
	var objects []Object
	if err := json.Unmarshal(rr.Body.Bytes(), &objects); err != nil {
		t.Fatal(err)
	}
	return objects
}

func TestPaginationBackwardCompatibleWithoutParameters(t *testing.T) {
	app, _, _ := makeApp(t)
	created := createNotes(t, app, "Unpaged", 12)
	listed := api(t, app, "GET", "/v1/objects?type_id=note", nil)
	if listed.Code != 200 {
		t.Fatal(listed.Body.String())
	}
	objects := decodeObjectPage(t, listed)
	if len(objects) != len(created) {
		t.Fatalf("unparameterized request must return the full list: %d of %d", len(objects), len(created))
	}
	if listed.Header().Get(nextCursorHeader) != "" {
		t.Fatal("unparameterized request must not advertise a cursor")
	}
}

func TestPaginationCursorWalksWholeList(t *testing.T) {
	app, _, _ := makeApp(t)
	createNotes(t, app, "Paged", 12)
	seen := []Object{}
	cursor := ""
	pages := 0
	for {
		path := "/v1/objects?type_id=note&limit=5"
		if cursor != "" {
			path += "&cursor=" + cursor
		}
		rr := api(t, app, "GET", path, nil)
		if rr.Code != 200 {
			t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
		}
		page := decodeObjectPage(t, rr)
		if len(page) == 0 || len(page) > 5 {
			t.Fatalf("page length %d", len(page))
		}
		seen = append(seen, page...)
		pages++
		cursor = rr.Header().Get(nextCursorHeader)
		if cursor == "" {
			break
		}
	}
	if pages != 3 || len(seen) != 12 {
		t.Fatalf("walked %d pages, %d objects", pages, len(seen))
	}
	ids := map[string]bool{}
	for _, o := range seen {
		if ids[o.ID] {
			t.Fatal("cursor walk repeated an object")
		}
		ids[o.ID] = true
	}
	// Page order matches the unpaginated display order.
	full := decodeObjectPage(t, api(t, app, "GET", "/v1/objects?type_id=note", nil))
	for i := range full {
		if full[i].ID != seen[i].ID {
			t.Fatalf("page %d differs from display order: %s != %s", i, full[i].ID, seen[i].ID)
		}
	}
}

func TestPaginationParameterValidation(t *testing.T) {
	app, _, _ := makeApp(t)
	createNotes(t, app, "Validated", 3)
	for _, raw := range []string{"0", "-1", "201", "abc", "1.5"} {
		rr := api(t, app, "GET", "/v1/objects?limit="+raw, nil)
		if rr.Code != 400 {
			t.Fatalf("limit=%s returned HTTP %d", raw, rr.Code)
		}
	}
	if rr := api(t, app, "GET", "/v1/objects?limit=200", nil); rr.Code != 200 {
		t.Fatalf("limit=200 rejected: HTTP %d", rr.Code)
	}
	for _, raw := range []string{"!!!", "not-base64", "e30="} {
		rr := api(t, app, "GET", "/v1/objects?cursor="+raw, nil)
		if rr.Code != 400 {
			t.Fatalf("cursor=%s returned HTTP %d", raw, rr.Code)
		}
	}
}

func TestPaginationComposesWithFiltersAndSearch(t *testing.T) {
	app, _, _ := makeApp(t)
	createNotes(t, app, "Compose note", 5)
	for i := 1; i <= 3; i++ {
		objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
			"type_id": "book", "title": fmt.Sprintf("Compose book %d", i),
		}))
	}
	books := []Object{}
	cursor := ""
	for {
		path := "/v1/objects?type_id=book&limit=2"
		if cursor != "" {
			path += "&cursor=" + cursor
		}
		rr := api(t, app, "GET", path, nil)
		if rr.Code != 200 {
			t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
		}
		var page []Object
		if err := json.Unmarshal(rr.Body.Bytes(), &page); err != nil {
			t.Fatal(err)
		}
		books = append(books, page...)
		cursor = rr.Header().Get(nextCursorHeader)
		if cursor == "" {
			break
		}
	}
	if len(books) != 3 {
		t.Fatalf("filter composition lost books: %d", len(books))
	}

	// Search paginates after the query filter and keeps the array shape.
	results := []string{}
	cursor = ""
	for {
		path := "/v1/search?q=Compose&limit=4"
		if cursor != "" {
			path += "&cursor=" + cursor
		}
		rr := api(t, app, "GET", path, nil)
		if rr.Code != 200 {
			t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
		}
		var page []map[string]any
		if err := json.Unmarshal(rr.Body.Bytes(), &page); err != nil {
			t.Fatal(err)
		}
		if !strings.HasPrefix(rr.Body.String(), "[") {
			t.Fatal("search response stopped being a JSON array")
		}
		for _, entry := range page {
			id, _ := entry["id"].(string)
			results = append(results, id)
		}
		cursor = rr.Header().Get(nextCursorHeader)
		if cursor == "" {
			break
		}
	}
	if len(results) != 8 {
		t.Fatalf("search pagination returned %d of 8 matches", len(results))
	}
}

func TestPaginationDefaultLimitAppliesOnlyWithCursorOrLimit(t *testing.T) {
	app, _, _ := makeApp(t)
	createNotes(t, app, "Bulk", 55)
	// First page opts in with limit=1; the following pages send only the
	// cursor and must use the default limit of 50.
	first := api(t, app, "GET", "/v1/objects?type_id=note&limit=1", nil)
	if first.Code != 200 || first.Header().Get(nextCursorHeader) == "" {
		t.Fatalf("first page: HTTP %d cursor %q", first.Code, first.Header().Get(nextCursorHeader))
	}
	second := api(t, app, "GET", "/v1/objects?type_id=note&cursor="+first.Header().Get(nextCursorHeader), nil)
	if second.Code != 200 {
		t.Fatal(second.Body.String())
	}
	page := decodeObjectPage(t, second)
	if len(page) != defaultPageLimit {
		t.Fatalf("cursor-only page returned %d objects, want the default %d", len(page), defaultPageLimit)
	}
	if second.Header().Get(nextCursorHeader) == "" {
		t.Fatal("more pages exist, cursor missing")
	}
	third := api(t, app, "GET", "/v1/objects?type_id=note&cursor="+second.Header().Get(nextCursorHeader), nil)
	page = decodeObjectPage(t, third)
	if len(page) != 4 || third.Header().Get(nextCursorHeader) != "" {
		t.Fatalf("last page wrong: %d objects, cursor %q", len(page), third.Header().Get(nextCursorHeader))
	}
}

func TestPaginationExcludesTemplatesBeforePaging(t *testing.T) {
	app, _, _ := makeApp(t)
	createNotes(t, app, "Visible", 3)
	template := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Hidden template",
		"properties": map[string]any{"is_template": true},
	}))
	rr := api(t, app, "GET", "/v1/objects?type_id=note&limit=2", nil)
	page := decodeObjectPage(t, rr)
	if len(page) != 2 {
		t.Fatalf("page length %d", len(page))
	}
	for _, o := range page {
		if o.ID == template.ID {
			t.Fatal("template leaked into a paginated listing")
		}
	}
}
