package main

import (
	"fmt"
	"lumen/server/internal/document"
	"net/http/httptest"
	"testing"
	"time"
)

func TestBlockSearchDoesNotAcquireGlobalWriterLock(t *testing.T) {
	app, _, _ := makeApp(t)
	objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Search while writing", "document": documentFixture("Needle")}))
	app.writeMu.Lock()
	locked := true
	defer func() {
		if locked {
			app.writeMu.Unlock()
		}
	}()
	done := make(chan *httptest.ResponseRecorder, 1)
	go func() {
		rr := httptest.NewRecorder()
		app.searchBlocks(rr, httptest.NewRequest("GET", "/v1/blocks?q=Needle", nil))
		done <- rr
	}()
	select {
	case rr := <-done:
		if rr.Code != 200 {
			t.Fatal(rr.Body.String())
		}
	case <-time.After(2 * time.Second):
		app.writeMu.Unlock()
		locked = false
		<-done
		t.Fatal("block discovery waited for global writer lock")
	}
}

func TestBlockSearchScopesAndStopsBeforeUnrelatedPayloads(t *testing.T) {
	app, _, _ := makeApp(t)
	for i := range 8 {
		d := documentFixture("Needle")
		o := Object{ID: fmt.Sprintf("000-search-%d", i), TypeID: "note", Title: fmt.Sprintf("Needle %d", i), Document: &d, Properties: map[string]any{}, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
		if err := app.saveObject(t.Context(), &o); err != nil {
			t.Fatal(err)
		}
	}
	d := documentFixture("Unrelated")
	other := Object{ID: "zzz-unrelated", TypeID: "note", Title: "Unrelated", Document: &d, Properties: map[string]any{}, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
	if err := app.saveObject(t.Context(), &other); err != nil {
		t.Fatal(err)
	}
	var payload []byte
	if err := app.store.db.QueryRow("SELECT payload FROM records WHERE kind='object' AND id=?", other.ID).Scan(&payload); err != nil {
		t.Fatal(err)
	}
	defer func() {
		_, _ = app.store.db.Exec("UPDATE records SET payload=? WHERE kind='object' AND id=?", payload, other.ID)
	}()
	if _, err := app.store.db.Exec("UPDATE records SET payload=? WHERE kind='object' AND id=?", []byte("broken ciphertext"), other.ID); err != nil {
		t.Fatal(err)
	}
	for _, path := range []string{"/v1/blocks?q=Needle&limit=1", "/v1/blocks?q=Needle&object_id=000-search-0"} {
		rr := api(t, app, "GET", path, nil)
		if rr.Code != 200 {
			t.Fatalf("search read unrelated payload %s %d %s", path, rr.Code, rr.Body.String())
		}
		result := decodeBody[struct {
			Results []blockHit `json:"results"`
		}](t, rr)
		if len(result.Results) != 1 || result.Results[0].ObjectID != "000-search-0" {
			t.Fatalf("wrong scope/limit %+v", result)
		}
	}
}

func TestBlockSearchMatchesAcrossMarksAndTemporalValues(t *testing.T) {
	app, _, _ := makeApp(t)
	d := document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "parent", Type: "group", Children: []document.Node{{ID: "split", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Hello", Marks: map[string]any{"bold": true}}, {Type: "text", Text: "World"}, {Type: "temporal", Attrs: map[string]any{"kind": "date", "start": "2026-12-07"}}}}}}}}
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Semantic search", "document": d}))
	for _, query := range []string{"helloworld", "2026-12-07"} {
		result := decodeBody[struct {
			Results []blockHit `json:"results"`
		}](t, api(t, app, "GET", "/v1/blocks?q="+query+"&object_id="+o.ID, nil))
		if len(result.Results) != 1 || result.Results[0].Block["id"] != "split" {
			t.Fatalf("semantic own text search failed %+v", result)
		}
	}
}
