package main

import (
	"encoding/json"
	"fmt"
	"strings"
	"testing"

	"lumen/server/internal/document"
)

func pullPage(t *testing.T, a *App, since int64) syncPullResponse {
	t.Helper()
	r := api(t, a, "POST", "/v1/sync/pull", syncPullRequest{SinceSeq: since})
	var page syncPullResponse
	if r.Code != 200 || json.Unmarshal(r.Body.Bytes(), &page) != nil {
		t.Fatalf("pull: %d %s", r.Code, r.Body.String())
	}
	return page
}

func TestSyncPullBoundsPagesWithoutSkippingConcurrentChanges(t *testing.T) {
	a, _, _ := makeApp(t)
	since, _ := a.store.HeadSeq()
	for i := range maxSyncPullRecords + 3 {
		o := Object{ID: fmt.Sprintf("pull-page-%03d", i), TypeID: "note", Title: fmt.Sprintf("Pull page %03d", i), CreatedAt: now(), UpdatedAt: now()}
		if err := a.saveObject(t.Context(), &o); err != nil {
			t.Fatal(err)
		}
	}
	first := pullPage(t, a, since)
	count := func(p syncPullResponse) int {
		return len(p.Objects) + len(p.Types) + len(p.Tombstones) + len(p.Assets) + len(p.Deleted)
	}
	if !first.HasMore || count(first) != maxSyncPullRecords || len(first.Objects) == 0 || first.NextSeq >= first.HeadSeq || first.NextSeq <= since {
		t.Fatalf("unbounded/incorrect first page: next=%d head=%d count=%d more=%v", first.NextSeq, first.HeadSeq, count(first), first.HasMore)
	}
	updated, _ := a.store.Object(first.Objects[0].ID)
	updated.Document = &document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "new", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Concurrent change"}}}}}
	if err := a.saveObject(t.Context(), &updated); err != nil {
		t.Fatal(err)
	}
	seen := map[string]Object{}
	for _, o := range first.Objects {
		seen[o.ID] = o
	}
	last := first
	for attempts := 0; last.HasMore; attempts++ {
		if attempts > 10 {
			t.Fatal("pull cursor did not progress")
		}
		next := pullPage(t, a, last.NextSeq)
		if next.NextSeq <= last.NextSeq || count(next) > maxSyncPullRecords {
			t.Fatal("pull page did not remain bounded or advance")
		}
		for _, o := range next.Objects {
			seen[o.ID] = o
		}
		last = next
	}
	if last.NextSeq != last.HeadSeq || len(seen) != maxSyncPullRecords+3 || seen[updated.ID].Content != "Concurrent change" {
		t.Fatal("concurrent update or object identity lost at a page boundary")
	}
	empty := pullPage(t, a, last.NextSeq)
	if count(empty) != 0 || empty.HasMore || empty.NextSeq != last.NextSeq {
		t.Fatal("completed cursor replayed records")
	}
}

func TestSyncPullByteBudgetPreservesWholeLargeDocuments(t *testing.T) {
	a, _, _ := makeApp(t)
	since, _ := a.store.HeadSeq()
	d := documentFixture(strings.Repeat("x", (3<<20)+128))
	for i := range 2 {
		o := Object{ID: fmt.Sprintf("large-pull-%d", i), TypeID: "note", Title: fmt.Sprintf("Large pull %d", i), Document: &d, CreatedAt: now(), UpdatedAt: now()}
		if err := a.saveObject(t.Context(), &o); err != nil {
			t.Fatal(err)
		}
	}
	first := pullPage(t, a, since)
	if !first.HasMore || len(first.Objects) != 1 || len(first.Objects[0].Content) != (3<<20)+128 {
		t.Fatal("first large page was truncated or unbounded")
	}
	second := pullPage(t, a, first.NextSeq)
	if second.HasMore || len(second.Objects) != 1 || first.Objects[0].ID == second.Objects[0].ID || len(second.Objects[0].Content) != (3<<20)+128 {
		t.Fatal("large document skipped or duplicated between pages")
	}
}

func TestSyncPullReadFailureIsNotAcknowledgedAsDeletion(t *testing.T) {
	a, _, _ := makeApp(t)
	since, _ := a.store.HeadSeq()
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Readable pull source"}))
	var sealed []byte
	if err := a.store.db.QueryRow("SELECT payload FROM records WHERE kind='object' AND id=?", o.ID).Scan(&sealed); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if _, err := a.store.db.Exec("UPDATE records SET payload=? WHERE kind='object' AND id=?", sealed, o.ID); err != nil {
			t.Error(err)
		}
	}()
	if _, err := a.store.db.Exec("UPDATE records SET payload=? WHERE kind='object' AND id=?", []byte("damaged ciphertext"), o.ID); err != nil {
		t.Fatal(err)
	}
	if r := api(t, a, "POST", "/v1/sync/pull", syncPullRequest{SinceSeq: since}); r.Code != 500 {
		t.Fatalf("corruption silently became deletion: %d %s", r.Code, r.Body.String())
	}
}
