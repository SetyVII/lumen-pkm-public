package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

func versionSummaries(t *testing.T, app *App, objectID string) []objectVersionSummary {
	t.Helper()
	return decodeBody[[]objectVersionSummary](t, api(t, app, "GET", "/v1/objects/"+objectID+"/versions", nil))
}

func versionSnapshot(t *testing.T, app *App, objectID string, revision uint64) objectVersionResponse {
	t.Helper()
	return decodeBody[objectVersionResponse](t, api(t, app, "GET", fmt.Sprintf("/v1/objects/%s/versions/%d", objectID, revision), nil))
}

func assertNoVersionJournalRows(t *testing.T, app *App) {
	t.Helper()
	var count int
	if err := app.store.db.QueryRow("SELECT count(*) FROM sync_changes WHERE kind='version'").Scan(&count); err != nil {
		t.Fatal(err)
	}
	if count != 0 {
		t.Fatalf("version snapshots leaked into the sync journal: %d rows", count)
	}
}

func TestVersionHistorySnapshotsPreviousState(t *testing.T) {
	app, _, _ := makeApp(t)
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Version target"}))
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Versioned note", "document": documentFixture("first version"),
		"properties": map[string]any{
			"summary": "first summary",
			"meta":    map[string]any{"nested": true},
			"labels":  []any{"a", "b"},
		},
		"links": []string{target.ID},
	}))
	if o.Revision != 1 {
		t.Fatalf("create revision = %d", o.Revision)
	}
	if versions := versionSummaries(t, app, o.ID); len(versions) != 0 {
		t.Fatalf("creating an object must not create a snapshot: %+v", versions)
	}

	o = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, documentPatchPayload(t, app, o.ID, map[string]any{"document": documentFixture("second version")})))
	if o.Revision != 2 {
		t.Fatalf("patch revision = %d", o.Revision)
	}
	versions := versionSummaries(t, app, o.ID)
	if len(versions) != 1 || versions[0].Revision != 1 {
		t.Fatalf("expected one snapshot for revision 1: %+v", versions)
	}
	if versions[0].WordCount != 2 || versions[0].Bytes != len("first version") {
		t.Fatalf("unexpected size metadata: %+v", versions[0])
	}
	if versions[0].Timestamp == "" {
		t.Fatal("snapshot has no timestamp")
	}

	snapshot := versionSnapshot(t, app, o.ID, 1)
	storedSnapshot, err := app.store.Version(o.ID, 1)
	if err != nil || storedSnapshot.Document == nil || !reflect.DeepEqual(*storedSnapshot.Document, documentFixture("first version")) {
		t.Fatalf("snapshot lost canonical document or marks: %+v, %v", storedSnapshot.Document, err)
	}
	if snapshot.Content != "first version" || snapshot.Title != "Versioned note" || snapshot.TypeID != "note" {
		t.Fatalf("snapshot lost the old document: %+v", snapshot)
	}
	if snapshot.Properties["summary"] != "first summary" {
		t.Fatalf("snapshot lost properties: %+v", snapshot.Properties)
	}
	meta, _ := snapshot.Properties["meta"].(map[string]any)
	if meta["nested"] != true {
		t.Fatalf("snapshot lost nested properties: %+v", snapshot.Properties)
	}
	if len(snapshot.Links) != 1 || snapshot.Links[0] != target.ID {
		t.Fatalf("snapshot lost links: %+v", snapshot.Links)
	}
	if snapshot.WordCount != 2 || snapshot.Bytes != len("first version") {
		t.Fatalf("snapshot size metadata is wrong: %+v", snapshot)
	}
	if rr := api(t, app, "GET", fmt.Sprintf("/v1/objects/%s/versions/%d", o.ID, o.Revision), nil); rr.Code != 404 {
		t.Fatalf("the current revision must not be a stored snapshot: HTTP %d", rr.Code)
	}
}

func TestVersionHistoryKeepsNewestTwentySnapshots(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Capped history", "document": documentFixture("v0")}))
	for i := 1; i <= 25; i++ {
		o = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, documentPatchPayload(t, app, o.ID, map[string]any{"document": documentFixture(fmt.Sprintf("v%d", i))})))
	}
	if o.Revision != 26 {
		t.Fatalf("current revision = %d", o.Revision)
	}
	versions := versionSummaries(t, app, o.ID)
	if len(versions) != maxObjectVersions {
		t.Fatalf("kept %d snapshots, want %d", len(versions), maxObjectVersions)
	}
	if versions[0].Revision != 25 || versions[len(versions)-1].Revision != 6 {
		t.Fatalf("wrong pruning window: newest %d, oldest %d", versions[0].Revision, versions[len(versions)-1].Revision)
	}
	for i := 1; i < len(versions); i++ {
		if versions[i].Revision >= versions[i-1].Revision {
			t.Fatalf("versions are not newest first: %+v", versions)
		}
	}
	if rr := api(t, app, "GET", fmt.Sprintf("/v1/objects/%s/versions/5", o.ID), nil); rr.Code != 404 {
		t.Fatalf("pruned revision still readable: HTTP %d", rr.Code)
	}
	if rr := api(t, app, "GET", fmt.Sprintf("/v1/objects/%s/versions/6", o.ID), nil); rr.Code != 200 {
		t.Fatalf("oldest kept revision is not readable: HTTP %d", rr.Code)
	}
}

func TestVersionHistoryRestoreCreatesNewRevisionAndKeepsHistory(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Restored note", "document": documentFixture("alpha version"),
		"properties": map[string]any{"summary": "alpha summary"},
	}))
	o = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, documentPatchPayload(t, app, o.ID, map[string]any{
		"document": documentFixture("beta version"), "properties": map[string]any{"summary": "beta summary"},
	})))
	o = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, documentPatchPayload(t, app, o.ID, map[string]any{
		"document": documentFixture("gamma version"), "properties": map[string]any{"summary": "gamma summary"},
	})))
	if o.Revision != 3 {
		t.Fatalf("current revision = %d", o.Revision)
	}

	restored := objectFrom(t, api(t, app, "POST", fmt.Sprintf("/v1/objects/%s/restore/1", o.ID), map[string]any{"base_revision": o.Revision}))
	if restored.Revision != 4 || restored.Content != "alpha version" || restored.Properties["summary"] != "alpha summary" {
		t.Fatalf("restore did not create a new revision with the old state: %+v", restored)
	}
	if restored.Title != "Restored note" {
		t.Fatalf("restore changed the title: %+v", restored)
	}
	if restored.Document == nil || !reflect.DeepEqual(*restored.Document, documentFixture("alpha version")) {
		t.Fatal("restore flattened the canonical document or lost rich marks")
	}
	current := objectFrom(t, api(t, app, "GET", "/v1/objects/"+o.ID, nil))
	if current.Content != "alpha version" || current.Revision != 4 {
		t.Fatalf("restored state was not persisted: %+v", current)
	}

	versions := versionSummaries(t, app, o.ID)
	if len(versions) != 3 || versions[0].Revision != 3 || versions[1].Revision != 2 || versions[2].Revision != 1 {
		t.Fatalf("restore lost history: %+v", versions)
	}
	if snapshot := versionSnapshot(t, app, o.ID, 3); snapshot.Content != "gamma version" || snapshot.Properties["summary"] != "gamma summary" {
		t.Fatalf("the pre-restore state was not snapshotted: %+v", snapshot)
	}
	if snapshot := versionSnapshot(t, app, o.ID, 1); snapshot.Content != "alpha version" {
		t.Fatalf("the restored snapshot changed: %+v", snapshot)
	}
}

func TestVersionHistoryRestoreHonorsBaseRevision(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "CAS history", "document": documentFixture("alpha")}))
	o = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, documentPatchPayload(t, app, o.ID, map[string]any{"document": documentFixture("beta")})))
	if o.Revision != 2 {
		t.Fatalf("current revision = %d", o.Revision)
	}

	conflict := api(t, app, "POST", fmt.Sprintf("/v1/objects/%s/restore/1", o.ID), map[string]any{"base_revision": 1})
	if conflict.Code != 409 {
		t.Fatalf("stale base_revision returned HTTP %d: %s", conflict.Code, conflict.Body.String())
	}
	var body struct {
		Error    string `json:"error"`
		Conflict bool   `json:"conflict"`
		Current  Object `json:"current"`
	}
	if err := json.Unmarshal(conflict.Body.Bytes(), &body); err != nil {
		t.Fatal(err)
	}
	if body.Error != "conflict" || !body.Conflict || body.Current.Revision != 2 || body.Current.Content != "beta" {
		t.Fatalf("unexpected conflict body: %+v", body)
	}
	if got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+o.ID, nil)); got.Content != "beta" || got.Revision != 2 {
		t.Fatalf("conflicted restore modified the object: %+v", got)
	}

	restored := objectFrom(t, api(t, app, "POST", fmt.Sprintf("/v1/objects/%s/restore/1", o.ID), map[string]any{"base_revision": 2}))
	if restored.Content != "alpha" || restored.Revision != 3 {
		t.Fatalf("restore with the current base_revision failed: %+v", restored)
	}
	// The body is optional, like PATCH without base_revision.
	restored = objectFrom(t, api(t, app, "POST", fmt.Sprintf("/v1/objects/%s/restore/1", o.ID), nil))
	if restored.Content != "alpha" || restored.Revision != 4 {
		t.Fatalf("restore without a body failed: %+v", restored)
	}
	if rr := api(t, app, "POST", fmt.Sprintf("/v1/objects/%s/restore/99", o.ID), nil); rr.Code != 404 {
		t.Fatalf("unknown revision returned HTTP %d", rr.Code)
	}
	if rr := api(t, app, "POST", fmt.Sprintf("/v1/objects/%s/restore/not-a-number", o.ID), nil); rr.Code != 400 {
		t.Fatalf("invalid revision returned HTTP %d", rr.Code)
	}
	if rr := api(t, app, "POST", "/v1/objects/missing/restore/1", nil); rr.Code != 404 {
		t.Fatalf("missing object returned HTTP %d", rr.Code)
	}
	if rr := api(t, app, "GET", "/v1/objects/missing/versions", nil); rr.Code != 404 {
		t.Fatalf("versions of a missing object returned HTTP %d", rr.Code)
	}
}

func TestVersionHistoryStaysOutOfSyncJournal(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Sync history", "document": documentFixture("SYNC_OLD_SENTINEL_1111")}))
	beforePatch := decodeBody[syncStatusResponse](t, api(t, app, "GET", "/v1/sync/status", nil))
	o = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, documentPatchPayload(t, app, o.ID, map[string]any{"document": documentFixture("SYNC_NEW_SENTINEL_2222")})))
	afterPatch := decodeBody[syncStatusResponse](t, api(t, app, "GET", "/v1/sync/status", nil))
	if afterPatch.HeadSeq <= beforePatch.HeadSeq {
		t.Fatalf("the object change did not reach the sync journal: %d -> %d", beforePatch.HeadSeq, afterPatch.HeadSeq)
	}
	assertNoVersionJournalRows(t, app)
	pull := api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": beforePatch.HeadSeq})
	if strings.Contains(pull.Body.String(), "SYNC_OLD_SENTINEL_1111") {
		t.Fatalf("a snapshot leaked into sync pull: %s", pull.Body.String())
	}
	if !strings.Contains(pull.Body.String(), "SYNC_NEW_SENTINEL_2222") {
		t.Fatalf("the current object is missing from sync pull: %s", pull.Body.String())
	}
	if snapshot := versionSnapshot(t, app, o.ID, 1); snapshot.Content != "SYNC_OLD_SENTINEL_1111" {
		t.Fatalf("history is not readable server-side: %+v", snapshot)
	}

	beforeRestore := decodeBody[syncStatusResponse](t, api(t, app, "GET", "/v1/sync/status", nil))
	objectFrom(t, api(t, app, "POST", fmt.Sprintf("/v1/objects/%s/restore/1", o.ID), nil))
	afterRestore := decodeBody[syncStatusResponse](t, api(t, app, "GET", "/v1/sync/status", nil))
	if afterRestore.HeadSeq <= beforeRestore.HeadSeq {
		t.Fatalf("restore did not reach the sync journal: %d -> %d", beforeRestore.HeadSeq, afterRestore.HeadSeq)
	}
	assertNoVersionJournalRows(t, app)
	restorePull := api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": beforeRestore.HeadSeq})
	if !strings.Contains(restorePull.Body.String(), "SYNC_OLD_SENTINEL_1111") {
		t.Fatalf("restored content did not sync as a normal object: %s", restorePull.Body.String())
	}
	if strings.Contains(restorePull.Body.String(), "SYNC_NEW_SENTINEL_2222") {
		t.Fatalf("the pre-restore snapshot leaked into sync pull: %s", restorePull.Body.String())
	}
}

func TestVersionHistorySkipsNoOpPatch(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "No-op history", "document": documentFixture("same")}))
	unchanged := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, documentPatchPayload(t, app, o.ID, map[string]any{"document": documentFixture("same")})))
	if unchanged.Revision != o.Revision {
		t.Fatalf("no-op patch bumped the revision: %d -> %d", o.Revision, unchanged.Revision)
	}
	if versions := versionSummaries(t, app, o.ID); len(versions) != 0 {
		t.Fatalf("no-op patch created a snapshot: %+v", versions)
	}
}

func TestVersionHistoryCapturesSyncPush(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Mobile history", "document": documentFixture("desktop content")}))
	push := map[string]any{
		"device_id": "pixel-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-version-01", "action": "put", "base_revision": o.Revision,
			"object": map[string]any{
				"id": o.ID, "type_id": "note", "title": o.Title, "document": documentFixture("mobile content"),
				"properties": map[string]any{}, "links": []string{},
				"created_at": o.CreatedAt, "updated_at": now(),
			},
		}},
	}
	result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", push))
	if len(result.Errors) != 0 || len(result.Applied) != 1 {
		t.Fatalf("sync push failed: %+v", result)
	}
	versions := versionSummaries(t, app, o.ID)
	if len(versions) != 1 || versions[0].Revision != 1 {
		t.Fatalf("sync push did not snapshot the previous state: %+v", versions)
	}
	if snapshot := versionSnapshot(t, app, o.ID, 1); snapshot.Content != "desktop content" {
		t.Fatalf("sync push snapshot is wrong: %+v", snapshot)
	}
}

func TestVersionHistoryRestoreKeepsRestorableAndDropsPurgedLinks(t *testing.T) {
	app, _, _ := makeApp(t)
	keep := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Kept link"}))
	gone := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Deleted link"}))
	purged := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Purged link"}))
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Link restore", "document": documentFixture("with links"),
		"links": []string{keep.ID, gone.ID, purged.ID},
	}))
	o = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, documentPatchPayload(t, app, o.ID, map[string]any{"document": documentFixture("changed")})))
	if rr := api(t, app, "DELETE", "/v1/objects/"+gone.ID, nil); rr.Code != 200 {
		t.Fatalf("delete link target: %s", rr.Body.String())
	}
	for _, path := range []string{"/v1/objects/" + purged.ID, "/v1/trash/" + purged.ID} {
		if rr := api(t, app, "DELETE", path, nil); rr.Code != 200 {
			t.Fatal(rr.Body.String())
		}
	}
	restored := objectFrom(t, api(t, app, "POST", fmt.Sprintf("/v1/objects/%s/restore/1", o.ID), nil))
	if restored.Content != "with links" || !sameStrings(restored.Links, []string{keep.ID, gone.ID}) {
		t.Fatalf("restore did not preserve trash and remove only purged link: %+v", restored)
	}
}

func TestVersionHistoryIsEncryptedAndSurvivesRestart(t *testing.T) {
	app, path, key := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Encrypted history", "document": documentFixture("VERSION_SECRET_OLD_1234")}))
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, documentPatchPayload(t, app, o.ID, map[string]any{"document": documentFixture("VERSION_SECRET_NEW_5678")})))
	app.store.db.Exec("PRAGMA wal_checkpoint(TRUNCATE)")
	files, _ := filepath.Glob(path + "*")
	for _, file := range files {
		data, err := os.ReadFile(file)
		if err != nil {
			t.Fatal(err)
		}
		if bytes.Contains(data, []byte("VERSION_SECRET_OLD_1234")) {
			t.Fatalf("snapshot plaintext leaked into %s", file)
		}
	}
	if err := app.store.Close(); err != nil {
		t.Fatal(err)
	}
	store, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	app.store = store
	if snapshot := versionSnapshot(t, app, o.ID, 1); snapshot.Content != "VERSION_SECRET_OLD_1234" {
		t.Fatalf("snapshot did not survive a restart: %+v", snapshot)
	}
}

func TestVersionHistoryPurgeDeletesSnapshots(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Purged history", "document": documentFixture("first")}))
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, documentPatchPayload(t, app, o.ID, map[string]any{"document": documentFixture("second")})))
	if versions := versionSummaries(t, app, o.ID); len(versions) != 1 {
		t.Fatalf("fixture did not create history: %+v", versions)
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+o.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "DELETE", "/v1/trash/"+o.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	var count int
	if err := app.store.db.QueryRow("SELECT count(*) FROM object_versions WHERE object_id=?", o.ID).Scan(&count); err != nil {
		t.Fatal(err)
	}
	if count != 0 {
		t.Fatalf("purge left %d version snapshots behind", count)
	}
}
