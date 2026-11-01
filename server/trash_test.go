package main

import (
	"encoding/base64"
	"strings"
	"testing"
	"time"
)

func TestTrashRestoreKeepsContentAndBacklinks(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Trash tag"}))
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Trash note", "content": "Recoverable body",
		"properties": map[string]any{"tags": []string{tag.ID}},
	}))
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-30", nil))
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+journal.ID, map[string]any{"links": []string{note.ID}}))

	deleted := decodeBody[map[string]any](t, api(t, app, "DELETE", "/v1/objects/"+note.ID, nil))
	if deleted["deleted"] != true || deleted["trashed"] != true || deleted["deleted_at"] == "" {
		t.Fatalf("unexpected delete response: %+v", deleted)
	}
	if rr := api(t, app, "GET", "/v1/objects/"+note.ID, nil); rr.Code != 404 {
		t.Fatalf("trashed object is still readable: %d", rr.Code)
	}
	if rr := api(t, app, "GET", "/v1/objects/"+note.ID+"/backlinks", nil); rr.Code != 404 {
		t.Fatalf("trashed object still exposes backlinks: %d", rr.Code)
	}
	for _, o := range decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects?type_id=note", nil)) {
		if o.ID == note.ID {
			t.Fatal("trashed object still appears in listings")
		}
	}
	if found := decodeBody[[]Object](t, api(t, app, "GET", "/v1/search?q=Recoverable", nil)); len(found) != 0 {
		t.Fatalf("trashed object still appears in search: %+v", found)
	}
	if found := decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+tag.ID+"/backlinks", nil)); len(found) != 0 {
		t.Fatalf("trashed object still appears as a backlink source: %+v", found)
	}
	trash := decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash", nil))
	if len(trash) != 1 || trash[0].ID != note.ID || trash[0].TypeID != "note" || trash[0].Title != note.Title || trash[0].DeletedAt == "" {
		t.Fatalf("unexpected trash list: %+v", trash)
	}
	// Inbound references survive while the object is in the trash.
	kept := objectFrom(t, api(t, app, "GET", "/v1/objects/"+journal.ID, nil))
	if len(kept.Links) != 1 || kept.Links[0] != note.ID {
		t.Fatalf("trash dropped inbound references: %+v", kept.Links)
	}

	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+note.ID+"/restore", nil))
	if restored.Revision <= note.Revision || restored.Content != "Recoverable body" || restored.Incarnation != note.Incarnation {
		t.Fatalf("restore lost identity or content: %+v", restored)
	}
	if restored.Properties["tags"] == nil {
		t.Fatalf("restore lost properties: %+v", restored.Properties)
	}
	if found := decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+tag.ID+"/backlinks", nil)); len(found) != 1 || found[0].ID != note.ID {
		t.Fatalf("property backlink was not restored: %+v", found)
	}
	if found := decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+note.ID+"/backlinks", nil)); len(found) != 1 || found[0].ID != journal.ID {
		t.Fatalf("explicit backlink was not restored: %+v", found)
	}
	if found := decodeBody[[]Object](t, api(t, app, "GET", "/v1/search?q=Recoverable", nil)); len(found) != 1 || found[0].ID != note.ID {
		t.Fatalf("restored object missing from search: %+v", found)
	}
	if trash = decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash", nil)); len(trash) != 0 {
		t.Fatalf("trash was not emptied by restore: %+v", trash)
	}
	if rr := api(t, app, "POST", "/v1/objects/"+note.ID+"/restore", nil); rr.Code != 404 {
		t.Fatalf("restoring a live object returned %d", rr.Code)
	}
}

func TestTrashPurgeKeepsAntiResurrectionAndCleansReferences(t *testing.T) {
	app, _, _ := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Purge me", "content": "gone"}))
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-31", nil))
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+journal.ID, map[string]any{"links": []string{note.ID}}))
	if rr := api(t, app, "DELETE", "/v1/trash/"+journal.ID, nil); rr.Code != 404 {
		t.Fatalf("purging a live object returned %d", rr.Code)
	}

	if rr := api(t, app, "DELETE", "/v1/objects/"+note.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "DELETE", "/v1/trash/"+note.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "GET", "/v1/objects/"+note.ID, nil); rr.Code != 404 {
		t.Fatalf("purged object still readable: %d", rr.Code)
	}
	if body := api(t, app, "GET", "/v1/trash", nil).Body.String(); strings.Contains(body, note.ID) {
		t.Fatalf("purged object still in trash: %s", body)
	}
	if rr := api(t, app, "POST", "/v1/objects/"+note.ID+"/restore", nil); rr.Code != 404 {
		t.Fatalf("purged object was restored: %d", rr.Code)
	}
	clean := objectFrom(t, api(t, app, "GET", "/v1/objects/"+journal.ID, nil))
	if len(clean.Links) != 0 {
		t.Fatalf("purge left a dangling reference: %+v", clean.Links)
	}
	if rr := api(t, app, "DELETE", "/v1/trash/"+note.ID, nil); rr.Code != 404 {
		t.Fatalf("purging the same object twice returned %d", rr.Code)
	}

	// The tombstone still blocks id reuse from a stale mobile outbox.
	push := map[string]any{
		"device_id": "pixel-trash-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-purged-id-1", "action": "put", "base_revision": note.Revision,
			"object": map[string]any{
				"id": note.ID, "type_id": "note", "title": "Resurrected", "content": "",
				"properties": map[string]any{}, "links": []string{},
				"created_at": now(), "updated_at": now(),
			},
		}},
	}
	resp := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", push))
	if len(resp.Conflicts) != 1 || resp.Conflicts[0].Resolution != "ignored_tombstoned" {
		t.Fatalf("purged id was not protected: %+v", resp)
	}
	if _, err := app.store.Object(note.ID); err == nil {
		t.Fatal("stale push resurrected a purged object")
	}
	if app.store.Has("trash", note.ID) {
		t.Fatal("purge left a trash payload behind")
	}
}

func TestEmptyTrashByTypeAndEverything(t *testing.T) {
	app, _, _ := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Trash note"}))
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Trash book"}))
	for _, id := range []string{note.ID, book.ID} {
		if rr := api(t, app, "DELETE", "/v1/objects/"+id, nil); rr.Code != 200 {
			t.Fatal(rr.Body.String())
		}
	}
	empty := decodeBody[map[string]any](t, api(t, app, "POST", "/v1/trash/empty?type_id=book", nil))
	if empty["purged"] != float64(1) {
		t.Fatalf("unexpected purge count: %+v", empty)
	}
	trash := decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash", nil))
	if len(trash) != 1 || trash[0].ID != note.ID {
		t.Fatalf("type filter purged the wrong entries: %+v", trash)
	}
	if rr := api(t, app, "POST", "/v1/objects/"+note.ID+"/restore", nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+note.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	empty = decodeBody[map[string]any](t, api(t, app, "POST", "/v1/trash/empty", nil))
	if empty["purged"] != float64(1) {
		t.Fatalf("unexpected purge count: %+v", empty)
	}
	if body := api(t, app, "GET", "/v1/trash", nil).Body.String(); body != "[]\n" {
		t.Fatalf("empty trash must be an empty array: %q", body)
	}
}

func TestTrashSyncPullDeliversDeletionAndRestore(t *testing.T) {
	app, _, _ := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Sync trash"}))
	head, err := app.store.HeadSeq()
	if err != nil {
		t.Fatal(err)
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+note.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	pull := decodeBody[syncPullResponse](t, api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": head}))
	if len(pull.Objects) != 0 {
		t.Fatalf("trashed object was delivered as live: %+v", pull.Objects)
	}
	if len(pull.Tombstones) != 1 || pull.Tombstones[0].ID != note.ID || pull.Tombstones[0].DeletedAt == "" {
		t.Fatalf("mobile did not receive the tombstone: %+v", pull.Tombstones)
	}
	foundDelete := false
	for _, deleted := range pull.Deleted {
		if deleted.Kind == "object" && deleted.ID == note.ID {
			foundDelete = true
		}
	}
	if !foundDelete {
		t.Fatalf("mobile did not receive the object deletion: %+v", pull.Deleted)
	}
	// While the object is in the trash, a stale mobile edit must not resurrect it.
	push := map[string]any{
		"device_id": "pixel-trash-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-trashed-put-1", "action": "put", "base_revision": note.Revision,
			"object": map[string]any{
				"id": note.ID, "type_id": "note", "title": note.Title, "content": "stale",
				"properties": map[string]any{}, "links": []string{},
				"created_at": note.CreatedAt, "updated_at": now(),
			},
		}},
	}
	resp := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", push))
	if len(resp.Conflicts) != 1 || resp.Conflicts[0].Resolution != "ignored_tombstoned" {
		t.Fatalf("trashed object accepted a stale put: %+v", resp)
	}

	head, err = app.store.HeadSeq()
	if err != nil {
		t.Fatal(err)
	}
	if rr := api(t, app, "POST", "/v1/objects/"+note.ID+"/restore", nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	pull = decodeBody[syncPullResponse](t, api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": head}))
	if len(pull.Objects) != 1 || pull.Objects[0].ID != note.ID || pull.Objects[0].Revision <= note.Revision {
		t.Fatalf("mobile did not receive the restored object: %+v", pull.Objects)
	}
	if len(pull.Tombstones) != 0 {
		t.Fatalf("restore left a tombstone in the pull: %+v", pull.Tombstones)
	}
	foundTombstoneDelete := false
	for _, deleted := range pull.Deleted {
		if deleted.Kind == "tombstone" && deleted.ID == note.ID {
			foundTombstoneDelete = true
		}
	}
	if !foundTombstoneDelete {
		t.Fatalf("mobile was not told to drop the tombstone: %+v", pull.Deleted)
	}
}

func TestTrashFiltersAndOrdersByDeletedAtDesc(t *testing.T) {
	app, _, _ := makeApp(t)
	first := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Alpha note", "content": "needle body"}))
	second := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Beta book"}))
	if rr := api(t, app, "DELETE", "/v1/objects/"+first.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	time.Sleep(time.Millisecond)
	if rr := api(t, app, "DELETE", "/v1/objects/"+second.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	all := decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash", nil))
	if len(all) != 2 || all[0].ID != second.ID || all[1].ID != first.ID {
		t.Fatalf("trash is not ordered by deleted_at desc: %+v", all)
	}
	byType := decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash?type_id=note", nil))
	if len(byType) != 1 || byType[0].ID != first.ID {
		t.Fatalf("type filter failed: %+v", byType)
	}
	byContent := decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash?q=needle", nil))
	if len(byContent) != 1 || byContent[0].ID != first.ID {
		t.Fatalf("text filter failed: %+v", byContent)
	}
	if none := decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash?q=missing", nil)); len(none) != 0 {
		t.Fatalf("unexpected filter matches: %+v", none)
	}
}

func TestRestoreResolvesTitleCollisionAndDropsPurgedReferences(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Doomed tag"}))
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Shared title",
		"properties": map[string]any{"tags": []string{tag.ID}},
	}))
	if rr := api(t, app, "DELETE", "/v1/objects/"+note.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	replacement := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Shared title"}))
	if replacement.ID == note.ID {
		t.Fatal("trashed title was not released")
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+tag.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "DELETE", "/v1/trash/"+tag.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+note.ID+"/restore", nil))
	if restored.Title != "Shared title (2)" {
		t.Fatalf("title collision was not resolved: %q", restored.Title)
	}
	if _, exists := restored.Properties["tags"]; exists {
		t.Fatalf("reference to a purged object survived: %#v", restored.Properties)
	}
	if len(restored.PropertyLinks) != 0 {
		t.Fatalf("purged property link survived: %+v", restored.PropertyLinks)
	}
}

func TestJournalInTrashIsRestoredWhenOpened(t *testing.T) {
	app, _, _ := makeApp(t)
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-29", nil))
	journal = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+journal.ID, map[string]any{"content": "Kept text"}))
	if rr := api(t, app, "DELETE", "/v1/objects/"+journal.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	reopened := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-29", nil))
	if reopened.ID != journal.ID || reopened.Content != "Kept text" {
		t.Fatalf("opening a trashed journal lost its text: %+v", reopened)
	}
	if reopened.Revision <= journal.Revision {
		t.Fatalf("restored journal did not get a fresh revision: %d <= %d", reopened.Revision, journal.Revision)
	}
	if body := api(t, app, "GET", "/v1/trash", nil).Body.String(); body != "[]\n" {
		t.Fatalf("journal restore left a trash entry: %q", body)
	}
}

func TestSyncPushDeleteMovesObjectToTrash(t *testing.T) {
	app, _, _ := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Remote delete"}))
	head, err := app.store.HeadSeq()
	if err != nil {
		t.Fatal(err)
	}
	push := map[string]any{
		"device_id": "pixel-trash-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-trash-delete-1", "action": "delete", "object_id": note.ID,
		}},
	}
	resp := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", push))
	if len(resp.Errors) != 0 || len(resp.Applied) != 1 {
		t.Fatalf("remote delete failed: %+v", resp)
	}
	if rr := api(t, app, "GET", "/v1/objects/"+note.ID, nil); rr.Code != 404 {
		t.Fatalf("remote delete left the object live: %d", rr.Code)
	}
	trash := decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash", nil))
	if len(trash) != 1 || trash[0].ID != note.ID {
		t.Fatalf("remote delete did not reach the trash: %+v", trash)
	}
	pull := decodeBody[syncPullResponse](t, api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": head}))
	found := false
	for _, deleted := range pull.Deleted {
		if deleted.Kind == "object" && deleted.ID == note.ID {
			found = true
		}
	}
	if !found || len(pull.Tombstones) != 1 || pull.Tombstones[0].ID != note.ID {
		t.Fatalf("remote delete did not reach the journal: %+v", pull)
	}
	if rr := api(t, app, "POST", "/v1/objects/"+note.ID+"/restore", nil); rr.Code != 200 {
		t.Fatalf("restore after remote delete failed: %s", rr.Body.String())
	}
}

func TestTrashSurvivesRestart(t *testing.T) {
	app, path, key := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Persistent trash", "content": "still here"}))
	if rr := api(t, app, "DELETE", "/v1/objects/"+note.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if err := app.store.Close(); err != nil {
		t.Fatal(err)
	}
	store, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	app.store = store
	trash := decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash", nil))
	if len(trash) != 1 || trash[0].ID != note.ID || trash[0].DeletedAt == "" {
		t.Fatalf("trash did not survive restart: %+v", trash)
	}
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+note.ID+"/restore", nil))
	if restored.Content != "still here" {
		t.Fatalf("restored content changed across restart: %+v", restored)
	}
}

func TestTrashMediaRestoreKeepsBlobAndOwnerLink(t *testing.T) {
	app, _, _ := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Media owner"}))
	png, err := base64.StdEncoding.DecodeString("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j0ioAAAAASUVORK5CYII=")
	if err != nil {
		t.Fatal(err)
	}
	asset := uploadFixture(t, app, note.ID, "cover.png", png)
	if asset.MediaObjectID == "" {
		t.Fatal("upload did not create a media object")
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+asset.MediaObjectID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if _, err := app.store.Blob(asset.ID); err != nil {
		t.Fatal("trash removed the attachment bytes")
	}
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+asset.MediaObjectID+"/restore", nil))
	if restored.TypeID != "image" || restored.Properties["asset_id"] != asset.ID {
		t.Fatalf("media restore lost its asset: %+v", restored)
	}
	if _, err := app.store.Blob(asset.ID); err != nil {
		t.Fatal("restore lost the attachment bytes")
	}
	owner := objectFrom(t, api(t, app, "GET", "/v1/objects/"+note.ID, nil))
	if len(owner.Links) != 1 || owner.Links[0] != asset.MediaObjectID {
		t.Fatalf("owner link was not preserved: %+v", owner.Links)
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+asset.MediaObjectID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "DELETE", "/v1/trash/"+asset.MediaObjectID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if _, err := app.store.Blob(asset.ID); err == nil {
		t.Fatal("purge left the attachment bytes behind")
	}
	owner = objectFrom(t, api(t, app, "GET", "/v1/objects/"+note.ID, nil))
	if len(owner.Links) != 0 || owner.Properties["image_asset_id"] != nil {
		t.Fatalf("purge left dead media references: %+v", owner)
	}
}
