package main

import (
	"bytes"
	"database/sql"
	"encoding/json"
	"errors"
	"net/http/httptest"
	"reflect"
	"strings"
	"sync"
	"testing"

	"lumen/server/internal/document"
)

func TestDocumentCommitFailureIsAtomicAndRetryable(t *testing.T) {
	a, _, _ := makeApp(t)
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Atomic", "document": documentFixture("original")}))
	head, _ := a.store.HeadSeq()
	_, err := a.store.db.Exec(`CREATE TEMP TRIGGER fail_document_update BEFORE UPDATE ON records WHEN NEW.kind='object' BEGIN SELECT RAISE(ABORT,'injected'); END`)
	if err != nil {
		t.Fatal(err)
	}
	req := map[string]any{"base_revision": o.Revision, "op_id": "failed-then-retry", "document": documentFixture("secret-rich-content")}
	r := api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", req)
	if r.Code != 500 {
		t.Fatalf("expected failed commit: %d %s", r.Code, r.Body.String())
	}
	stored, _ := a.store.Object(o.ID)
	if stored.Revision != o.Revision || stored.Content != "original" {
		t.Fatal("failed commit changed object")
	}
	if after, _ := a.store.HeadSeq(); after != head {
		t.Fatal("failed commit advanced sync journal")
	}
	var receipt DocumentReceipt
	if err := a.store.Get("document_receipt", "failed-then-retry", &receipt); !errors.Is(err, sql.ErrNoRows) {
		t.Fatal("failed commit wrote an acknowledgement")
	}
	if _, err = a.store.db.Exec(`DROP TRIGGER fail_document_update`); err != nil {
		t.Fatal(err)
	}
	saved := objectFrom(t, api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", req))
	if saved.Content != "secret-rich-content" {
		t.Fatal("retry did not commit")
	}
	var sealed []byte
	if err = a.store.db.QueryRow("SELECT payload FROM records WHERE kind='object' AND id=?", o.ID).Scan(&sealed); err != nil {
		t.Fatal(err)
	}
	if bytes.Contains(sealed, []byte("secret-rich-content")) {
		t.Fatal("canonical content was stored unencrypted")
	}
}

func TestDocumentConcurrentWritersCannotLoseCommittedEdit(t *testing.T) {
	a, _, _ := makeApp(t)
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Race", "document": documentFixture("original")}))
	statuses := make(chan int, 2)
	var wg sync.WaitGroup
	for _, text := range []string{"first", "second"} {
		wg.Go(func() {
			r := api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", map[string]any{"base_revision": o.Revision, "op_id": "concurrent-" + text, "document": documentFixture(text)})
			statuses <- r.Code
		})
	}
	wg.Wait()
	close(statuses)
	counts := map[int]int{}
	for status := range statuses {
		counts[status]++
	}
	if counts[200] != 1 || counts[409] != 1 {
		t.Fatalf("writers: %v", counts)
	}
	stored, _ := a.store.Object(o.ID)
	if stored.Revision != o.Revision+1 {
		t.Fatal("more than one stale edit committed")
	}
}

func TestDocumentReceiptCompactionCannotReplayAnOldEdit(t *testing.T) {
	a, _, _ := makeApp(t)
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Receipt retention", "document": documentFixture("start")}))
	req := map[string]any{"base_revision": o.Revision, "op_id": "retention-original", "document": documentFixture("first edit")}
	first := objectFrom(t, api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", req))
	// Force the result cache over budget without allocating a huge document.
	if _, err := a.store.db.Exec("UPDATE receipt_results SET result_bytes=? WHERE id=?", maxReceiptResultBytes+1, "retention-original"); err != nil {
		t.Fatal(err)
	}
	next := objectFrom(t, api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", map[string]any{"base_revision": first.Revision, "op_id": "retention-newer", "document": documentFixture("newer edit")}))
	if r := api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", req); r.Code != 410 {
		t.Fatalf("compacted receipt: %d %s", r.Code, r.Body.String())
	}
	stored, _ := a.store.Object(o.ID)
	if stored.Revision != next.Revision || stored.Content != "newer edit" {
		t.Fatal("old compacted retry modified the object")
	}
	deleted := api(t, a, "DELETE", "/v1/objects/"+o.ID, nil)
	if deleted.Code != 200 {
		t.Fatalf("delete: %s", deleted.Body.String())
	}
	purged := api(t, a, "DELETE", "/v1/trash/"+o.ID, nil)
	if purged.Code != 200 {
		t.Fatalf("purge: %s", purged.Body.String())
	}
	var count int
	if err := a.store.db.QueryRow("SELECT count(*) FROM receipt_results WHERE object_id=?", o.ID).Scan(&count); err != nil || count != 0 {
		t.Fatal("purge kept cached object replies")
	}
	if r := api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", req); r.Code != 404 {
		t.Fatalf("purged receipt replay: %d", r.Code)
	}
}

func TestDocumentProtocolAndMediaCreation(t *testing.T) {
	a, _, _ := makeApp(t)
	r := httptest.NewRequest("POST", "/v1/objects", strings.NewReader(`{"type_id":"note","title":"Old client"}`))
	r.Header.Set("Authorization", "Bearer "+testToken)
	w := httptest.NewRecorder()
	a.Handler().ServeHTTP(w, r)
	if w.Code != 426 {
		t.Fatalf("old writer was not rejected: %d", w.Code)
	}
	if response := api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "image", "title": "Empty image"}); response.Code != 400 {
		t.Fatal("empty image created")
	}
	if response := api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "pdf", "title": "Empty PDF"}); response.Code != 400 {
		t.Fatal("empty PDF created by default")
	}
	draft := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "pdf", "title": "Explicit draft", "properties": map[string]any{"media_state": "draft"}}))
	if draft.Document == nil {
		t.Fatal("PDF has no content document")
	}
	for _, kind := range []string{"note", "book", "tag", "quote", "audio", "web", "person"} {
		o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": kind, "title": "Document for " + kind}))
		if o.Document == nil || o.Document.SchemaVersion != 2 {
			t.Fatalf("%s has no canonical editor document", kind)
		}
	}
}

func documentFixture(text string) document.Document {
	return document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "paragraph-1", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: text, Marks: map[string]any{"bold": true}}}}}}
}

func TestDocumentCommitRetryConflictAndDurability(t *testing.T) {
	a, path, key := makeApp(t)
	initial := documentFixture("Hola 👋")
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Canonical", "document": initial}))
	if o.Document == nil || o.Content != "Hola 👋" {
		t.Fatalf("document projection: %+v", o)
	}
	next := documentFixture("Actualizado")
	req := map[string]any{"base_revision": o.Revision, "op_id": "document-test-op-1", "document": next}
	saved := objectFrom(t, api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", req))
	replay := objectFrom(t, api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", req))
	if replay.Revision != saved.Revision || !reflect.DeepEqual(replay.Document, saved.Document) {
		t.Fatal("retry did not return original receipt")
	}
	changed := map[string]any{"base_revision": saved.Revision, "op_id": "document-test-op-1", "document": initial}
	if r := api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", changed); r.Code != 409 {
		t.Fatalf("op collision: %d", r.Code)
	}
	stale := map[string]any{"base_revision": o.Revision, "op_id": "document-stale-op", "document": initial}
	if r := api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", stale); r.Code != 409 {
		t.Fatalf("stale edit: %d %s", r.Code, r.Body.String())
	}
	if r := api(t, a, "PATCH", "/v1/objects/"+o.ID, map[string]any{"content": "flatten"}); r.Code != 400 {
		t.Fatal("text projection overwrote canonical document")
	}
	if r := api(t, a, "PATCH", "/v1/objects/"+o.ID, map[string]any{"document": initial}); r.Code != 400 {
		t.Fatal("revisionless document edit accepted")
	}
	// Reopen through another Store, not the application's in-memory object cache.
	reopened, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	defer reopened.Close()
	stored, err := reopened.Object(o.ID)
	if err != nil {
		t.Fatal(err)
	}
	if stored.Revision != saved.Revision || !reflect.DeepEqual(stored.Document, saved.Document) {
		t.Fatal("document did not survive reopening encrypted store")
	}
	version, err := reopened.Version(o.ID, o.Revision)
	if err != nil {
		t.Fatal(err)
	}
	if version.Document == nil || document.PlainText(*version.Document) != "Hola 👋" {
		t.Fatal("history omitted canonical document")
	}
	var receipt DocumentReceipt
	if err := reopened.Get("document_receipt", "document-test-op-1", &receipt); err != nil || receipt.Object.Revision != saved.Revision {
		t.Fatal("durable receipt missing")
	}
}

func TestDocumentReferenceProvenanceAndAtomicCommands(t *testing.T) {
	a, _, _ := makeApp(t)
	target := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Book"}))
	d := documentFixture("")
	d.Blocks[0].Content = []document.Inline{{Type: "object_ref", ObjectID: target.ID, Label: "Libro"}}
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Ref", "links": []string{target.ID}, "document": d}))
	if len(o.DocumentLinks) != 1 {
		t.Fatal("document references were not derived")
	}
	req := map[string]any{"base_revision": o.Revision, "op_id": "clear-references", "document": documentFixture("plain")}
	saved := objectFrom(t, api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", req))
	if len(saved.DocumentLinks) != 0 || !containsString(saved.Links, target.ID) {
		t.Fatal("removing a mention destroyed manual relation")
	}
	bad := map[string]any{"base_revision": saved.Revision, "op_id": "atomic-commands-1", "commands": []map[string]any{{"type": "set_attrs", "node_id": "paragraph-1", "attrs": map[string]any{"alignment": "center"}}, {"type": "delete", "node_id": "does-not-exist"}}}
	r := api(t, a, "POST", "/v1/objects/"+o.ID+"/document/commands", bad)
	if r.Code != 400 {
		t.Fatalf("bad commands: %d %s", r.Code, r.Body.String())
	}
	stored, _ := a.store.Object(o.ID)
	if stored.Revision != saved.Revision || !reflect.DeepEqual(stored.Document, saved.Document) {
		t.Fatal("failed batch partially persisted")
	}
}

func TestCanonicalSyncReceiptAndConflict(t *testing.T) {
	a, _, _ := makeApp(t)
	d := documentFixture("offline")
	op := syncOperation{OpID: "canonical-sync-op", Action: "put", Object: &Object{ID: "offline-note", TypeID: "note", Title: "Offline", Document: &d}}
	request := syncPushRequest{DeviceID: "test-phone", Operations: []syncOperation{op}}
	r := api(t, a, "POST", "/v1/sync/push", request)
	var response syncPushResponse
	if r.Code != 200 || json.Unmarshal(r.Body.Bytes(), &response) != nil || len(response.Errors) > 0 {
		t.Fatalf("sync: %s", r.Body.String())
	}
	if len(response.Receipts) != 1 || response.Receipts[0].Revision == 0 {
		t.Fatalf("missing receipt: %s", r.Body.String())
	}
	r = api(t, a, "POST", "/v1/sync/push", request)
	if json.Unmarshal(r.Body.Bytes(), &response) != nil || len(response.Receipts) != 1 {
		t.Fatal("retry missing durable receipt")
	}
	changed := documentFixture("different")
	op.Object.Document = &changed
	request.Operations = []syncOperation{op}
	r = api(t, a, "POST", "/v1/sync/push", request)
	_ = json.Unmarshal(r.Body.Bytes(), &response)
	if len(response.Errors) != 1 {
		t.Fatal("reused sync op accepted a different payload")
	}
	op.OpID = "stale-document-sync"
	op.BaseRevision = 999
	request.Operations = []syncOperation{op}
	r = api(t, a, "POST", "/v1/sync/push", request)
	_ = json.Unmarshal(r.Body.Bytes(), &response)
	if len(response.Errors) != 1 {
		t.Fatal("stale canonical snapshot was merged")
	}
}

func TestDocumentFsckAllowsUnresolvedOfflineReferencesAndChecksProjections(t *testing.T) {
	a, _, _ := makeApp(t)
	d := documentFixture("")
	d.Blocks[0].Content = []document.Inline{{Type: "object_ref", ObjectID: "offline-object-pending", Label: "Pending source"}}
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Offline reference", "document": d}))
	if err := a.store.Fsck(); err != nil {
		t.Fatalf("valid unresolved offline reference: %v", err)
	}
	// Bypass the storage boundary deliberately to simulate damaged disk data.
	var originalPayload []byte
	if err := a.store.db.QueryRow("SELECT payload FROM records WHERE kind='object' AND id=?", o.ID).Scan(&originalPayload); err != nil {
		t.Fatal(err)
	}
	defer func() {
		if _, err := a.store.db.Exec("UPDATE records SET payload=? WHERE kind='object' AND id=?", originalPayload, o.ID); err != nil {
			t.Error(err)
		}
	}()
	o.Content = "incorrect projection"
	raw, err := json.Marshal(o)
	if err != nil {
		t.Fatal(err)
	}
	payload, err := a.store.encrypt(raw, "object/"+o.ID)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = a.store.db.Exec("UPDATE records SET payload=? WHERE kind='object' AND id=?", payload, o.ID); err != nil {
		t.Fatal(err)
	}
	if err := a.store.Fsck(); err == nil || !strings.Contains(err.Error(), "document projections") {
		t.Fatalf("corrupt projection was not detected: %v", err)
	}
}

func TestDocumentReceiptExpiresWithoutAnInterveningWrite(t *testing.T) {
	a, _, _ := makeApp(t)
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Idle", "document": documentFixture("start")}))
	req := map[string]any{"base_revision": o.Revision, "op_id": "idle-expired-document", "document": documentFixture("committed")}
	saved := objectFrom(t, api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", req))
	if _, err := a.store.db.Exec("UPDATE receipt_results SET expires_at='2000-01-01T00:00:00.000000000Z' WHERE id=?", "idle-expired-document"); err != nil {
		t.Fatal(err)
	}
	if r := api(t, a, "PUT", "/v1/objects/"+o.ID+"/document", req); r.Code != 410 {
		t.Fatalf("expired idle reply: %d %s", r.Code, r.Body.String())
	}
	current, _ := a.store.Object(o.ID)
	if current.Revision != saved.Revision {
		t.Fatal("expired operation replayed")
	}
	patch := map[string]any{"base_revision": saved.Revision, "op_id": "idle-expired-patch", "document": documentFixture("patch saved"), "title": "Idle patched"}
	objectFrom(t, api(t, a, "PATCH", "/v1/objects/"+o.ID, patch))
	if _, err := a.store.db.Exec("UPDATE receipt_results SET expires_at='2000-01-01T00:00:00.000000000Z' WHERE id=?", "idle-expired-patch"); err != nil {
		t.Fatal(err)
	}
	if r := api(t, a, "PATCH", "/v1/objects/"+o.ID, patch); r.Code != 410 {
		t.Fatalf("expired metadata+document patch: %d %s", r.Code, r.Body.String())
	}

	d := documentFixture("sync content")
	op := syncOperation{OpID: "idle-expired-sync", Action: "put", Object: &Object{ID: "idle-offline", TypeID: "note", Title: "Idle sync", Document: &d}}
	push := syncPushRequest{DeviceID: "phone", Operations: []syncOperation{op}}
	r := api(t, a, "POST", "/v1/sync/push", push)
	var response syncPushResponse
	if err := json.Unmarshal(r.Body.Bytes(), &response); err != nil || len(response.Receipts) != 1 {
		t.Fatalf("sync commit: %s", r.Body.String())
	}
	revision := response.Receipts[0].Revision
	if _, err := a.store.db.Exec("UPDATE receipt_results SET expires_at='2000-01-01T00:00:00.000000000Z' WHERE id=?", op.OpID); err != nil {
		t.Fatal(err)
	}
	r = api(t, a, "POST", "/v1/sync/push", push)
	response = syncPushResponse{}
	if err := json.Unmarshal(r.Body.Bytes(), &response); err != nil || len(response.Receipts) != 1 || len(response.Errors) != 0 {
		t.Fatalf("sync replay: %s", r.Body.String())
	}
	if response.Receipts[0].Object != nil || response.Receipts[0].Revision != revision {
		t.Fatal("expired sync receipt did not retain just the committed identity and revision")
	}
}

func TestCreateObjectReservesDraftIdentityAndPreservesSelfBlockReferences(t *testing.T) {
	a, _, _ := makeApp(t)
	d := documentFixture("source")
	d.Blocks = append(d.Blocks, document.Node{ID: "ref", Type: "paragraph", Content: []document.Inline{{Type: "block_ref", ObjectID: "draft-object-id", BlockID: "paragraph-1", Label: "source block"}}})
	req := map[string]any{"id": "draft-object-id", "type_id": "note", "title": "Draft with block reference", "document": d}
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", req))
	if o.ID != "draft-object-id" || o.Document.Blocks[1].Content[0].ObjectID != o.ID {
		t.Fatal("creation changed draft reference identity")
	}
	if r := api(t, a, "POST", "/v1/objects", req); r.Code != 409 {
		t.Fatalf("existing identity overwritten: %d", r.Code)
	}
	if r := api(t, a, "POST", "/v1/objects", map[string]any{"id": "invalid identity", "type_id": "note", "title": "Invalid"}); r.Code != 400 {
		t.Fatalf("invalid identity accepted: %d", r.Code)
	}
}

func TestDocumentOnlyEditPreservesUnresolvedOrMismatchedMetadata(t *testing.T) {
	a, _, _ := makeApp(t)
	if r := api(t, a, "PATCH", "/v1/types/note", map[string]any{"properties": []any{map[string]any{"key": "related", "type": "object", "target_type": "note"}}}); r.Code != 200 {
		t.Fatal(r.Body.String())
	}
	d := documentFixture("Offline body")
	source := Object{ID: "pending-body-owner", TypeID: "note", Title: "Pending body source", Document: &d, Links: []string{"future-manual"}, Properties: map[string]any{"related": "future-typed"}}
	result := pushPendingFixture(t, a, "pending-body-source-op", source)
	if len(result.Errors) != 0 {
		t.Fatalf("pending source: %+v", result)
	}
	stored, _ := a.store.Object(source.ID)
	for index, text := range []string{"Editable before target", "Editable with type warning"} {
		if index == 1 {
			objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"id": "future-typed", "type_id": "book", "title": "A real book"}))
		}
		saved := objectFrom(t, api(t, a, "PUT", "/v1/objects/"+source.ID+"/document", map[string]any{"base_revision": stored.Revision, "op_id": "pending-body-edit-" + text, "document": documentFixture(text)}))
		if !reflect.DeepEqual(saved.Properties, stored.Properties) || !sameStrings(saved.Links, stored.Links) || !sameStrings(saved.PropertyLinks, stored.PropertyLinks) {
			t.Fatal("body edit modified unrelated relationships")
		}
		patch := objectFrom(t, api(t, a, "PATCH", "/v1/objects/"+source.ID, map[string]any{"base_revision": saved.Revision, "op_id": "pending-body-patch-" + text, "title": saved.Title + "!", "document": documentFixture(text + " via patch"), "properties": saved.Properties, "links": saved.Links}))
		if !reflect.DeepEqual(patch.Properties, saved.Properties) || !sameStrings(patch.Links, saved.Links) {
			t.Fatal("metadata-preserving patch dropped existing pending relations")
		}
		stored = patch
	}
}

func TestDocumentRoutesAcceptSharedSizeBeyondLegacyThreeMiB(t *testing.T) {
	a, _, _ := makeApp(t)
	d := documentFixture(strings.Repeat("x", (3<<20)+128))
	created := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Large canonical document", "document": d}))
	patched := objectFrom(t, api(t, a, "PATCH", "/v1/objects/"+created.ID, map[string]any{"base_revision": created.Revision, "op_id": "large-canonical-patch", "document": d}))
	if !reflect.DeepEqual(patched.Document, created.Document) {
		t.Fatal("large document was truncated")
	}
}
