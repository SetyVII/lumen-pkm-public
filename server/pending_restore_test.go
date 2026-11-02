package main

import (
	"fmt"
	"reflect"
	"strings"
	"testing"

	"lumen/server/internal/document"
)

func pendingRestoreSource(t *testing.T, app *App, id, targetID string) Object {
	t.Helper()
	if rr := api(t, app, "PATCH", "/v1/types/note", map[string]any{"properties": []any{map[string]any{"key": "related", "type": "object", "target_type": "note"}}}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	d := documentFixture("Original pending body")
	d.Blocks[0].Attrs = map[string]any{"future_state": map[string]any{"preserved": true}}
	d.Blocks[0].Content = append(d.Blocks[0].Content, document.Inline{Type: "object_ref", ObjectID: targetID, Label: "Pending target"})
	source := Object{ID: id, TypeID: "note", Title: id, Document: &d, Links: []string{targetID}, Properties: map[string]any{"related": targetID}}
	pushed := pushPendingFixture(t, app, "create-"+id, source)
	if len(pushed.Errors) != 0 {
		t.Fatal(pushed.Errors)
	}
	stored, err := app.store.Object(id)
	if err != nil {
		t.Fatal(err)
	}
	return stored
}

func editPendingRestoreBody(t *testing.T, app *App, source Object, text, opID string) Object {
	t.Helper()
	d := documentFixture(text)
	source.Document = &d
	response := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", syncPushRequest{DeviceID: "pending-restore-device", Operations: []syncOperation{{OpID: opID, Action: "put", BaseRevision: source.Revision, Object: &source}}}))
	if len(response.Errors) != 0 {
		t.Fatal(response.Errors)
	}
	stored, err := app.store.Object(source.ID)
	if err != nil {
		t.Fatal(err)
	}
	return stored
}

func TestPendingReferencesSurviveReopenHistoryAndTrashRestore(t *testing.T) {
	app, path, key := makeApp(t)
	source := pendingRestoreSource(t, app, "pending-restore-source", "target-not-uploaded-yet")
	original := cloneObject(source)
	source = editPendingRestoreBody(t, app, source, "Changed body", "pending-restore-body-edit")
	if err := app.store.Close(); err != nil {
		t.Fatal(err)
	}
	reopened, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	app.store = reopened
	if err = app.store.Fsck(); err != nil {
		t.Fatal("pending references did not survive reopen", err)
	}
	restored := objectFrom(t, api(t, app, "POST", fmt.Sprintf("/v1/objects/%s/restore/1", source.ID), map[string]any{"base_revision": source.Revision}))
	if !reflect.DeepEqual(restored.Document, original.Document) || !sameStrings(restored.Links, original.Links) || restored.Properties["related"] != original.Properties["related"] || !sameStrings(restored.PropertyLinks, original.PropertyLinks) {
		t.Fatal("history restore discarded pending relations or rich state")
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+source.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	restored = objectFrom(t, api(t, app, "POST", "/v1/objects/"+source.ID+"/restore", nil))
	if !reflect.DeepEqual(restored.Document, original.Document) || !sameStrings(restored.Links, original.Links) || restored.Properties["related"] != original.Properties["related"] || !sameStrings(restored.PropertyLinks, original.PropertyLinks) {
		t.Fatal("trash restore discarded pending relations or rich state")
	}
}

func TestRestoringKnownWrongTypedRelationRequiresReview(t *testing.T) {
	app, _, _ := makeApp(t)
	source := pendingRestoreSource(t, app, "review-restore-source", "wrong-restored-target")
	source = editPendingRestoreBody(t, app, source, "Current body", "review-restore-body-edit")
	targetDoc := documentFixture("Target")
	target := Object{ID: "wrong-restored-target", TypeID: "book", Title: "Wrong restored target", Document: &targetDoc, Properties: map[string]any{}, Links: []string{}}
	if pushed := pushPendingFixture(t, app, "wrong-restored-target-op", target); len(pushed.Errors) != 0 {
		t.Fatal(pushed.Errors)
	}
	rr := api(t, app, "POST", fmt.Sprintf("/v1/objects/%s/restore/1", source.ID), map[string]any{"base_revision": source.Revision})
	if rr.Code != 400 || !strings.Contains(rr.Body.String(), "requires objects of type note") {
		t.Fatalf("known mismatch silently restored history %d %s", rr.Code, rr.Body.String())
	}
	current, _ := app.store.Object(source.ID)
	if !reflect.DeepEqual(current, source) {
		t.Fatal("failed history restore mutated current object")
	}
	if rr = api(t, app, "DELETE", "/v1/objects/"+source.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	rr = api(t, app, "POST", "/v1/objects/"+source.ID+"/restore", nil)
	if rr.Code != 400 || !strings.Contains(rr.Body.String(), "requires objects of type note") {
		t.Fatalf("known mismatch silently restored trash %d %s", rr.Code, rr.Body.String())
	}
	if !app.store.Has("trash", source.ID) || app.store.Has("object", source.ID) {
		t.Fatal("failed trash restore lost original state")
	}
	objectFrom(t, api(t, app, "POST", "/v1/objects/"+target.ID+"/convert", map[string]any{"target_type_id": "note"}))
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+source.ID+"/restore", nil))
	if !reflect.DeepEqual(restored.Document, source.Document) || restored.Properties["related"] != target.ID {
		t.Fatal("reviewed restore lost preserved state")
	}
}

func TestTrashRestoreKeepsOfflineMediaUploadIdentityAndNotes(t *testing.T) {
	app, _, _ := makeApp(t)
	d := documentFixture("Notes before upload")
	image := Object{ID: "pending-media-restore", TypeID: "image", Title: "Pending media restore", Document: &d, Properties: map[string]any{"media_state": "upload_pending", "asset_id": "queued-media-bytes", "image_asset_id": "queued-media-bytes"}, Links: []string{}}
	if pushed := pushPendingFixture(t, app, "pending-media-restore-op", image); len(pushed.Errors) != 0 {
		t.Fatal(pushed.Errors)
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+image.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+image.ID+"/restore", nil))
	if !reflect.DeepEqual(restored.Document, &d) || restored.Properties["asset_id"] != "queued-media-bytes" || restored.Properties["image_asset_id"] != "queued-media-bytes" || restored.Properties["media_state"] != "upload_pending" {
		t.Fatal("restoring pending media discarded queued upload identity or notes")
	}
}
