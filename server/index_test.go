package main

import (
	"encoding/base64"
	"encoding/json"
	"strings"
	"testing"
)

func TestMemIndexBacklinksAndNoOpPatch(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Índice"}))
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Nota indexada", "properties": map[string]any{"tags": []string{tag.ID}}}))
	if len(note.PropertyLinks) != 1 || note.PropertyLinks[0] != tag.ID {
		t.Fatalf("property links not derived: %+v", note.PropertyLinks)
	}
	var backlinks []Object
	rr := api(t, app, "GET", "/v1/objects/"+tag.ID+"/backlinks", nil)
	if err := json.Unmarshal(rr.Body.Bytes(), &backlinks); err != nil || len(backlinks) != 1 || backlinks[0].ID != note.ID {
		t.Fatalf("indexed backlinks failed: %s", rr.Body.String())
	}
	revision := note.Revision
	same := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+note.ID, map[string]any{
		"title": note.Title, "content": note.Content, "properties": note.Properties, "links": note.Links,
	}))
	if same.Revision != revision {
		t.Fatalf("no-op patch bumped revision %d -> %d", revision, same.Revision)
	}
	plain := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Sin relaciones"}))
	plainRevision := plain.Revision
	plain = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+plain.ID, map[string]any{
		"title": plain.Title, "content": plain.Content, "properties": plain.Properties, "links": plain.Links,
	}))
	if plain.Revision != plainRevision {
		t.Fatalf("no-op patch on an object without relations bumped revision %d -> %d", plainRevision, plain.Revision)
	}
	head, _ := app.store.HeadSeq()
	same = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+note.ID, map[string]any{"content": "changed"}))
	if same.Revision != revision+1 {
		t.Fatalf("real patch did not bump revision: %+v", same)
	}
	after, _ := app.store.HeadSeq()
	if after <= head {
		t.Fatal("real patch did not append a sync change")
	}
}

func TestPatchBaseRevisionConflict(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "CAS"}))
	rr := api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{"content": "stale", "base_revision": o.Revision + 10})
	if rr.Code != 409 || !strings.Contains(rr.Body.String(), `"conflict":true`) {
		t.Fatalf("expected 409, got %d: %s", rr.Code, rr.Body.String())
	}
	var conflict struct {
		Current Object `json:"current"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &conflict); err != nil || conflict.Current.ID != o.ID || conflict.Current.Revision != o.Revision {
		t.Fatalf("409 did not return current object: %s", rr.Body.String())
	}
	ok := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{"content": "fresh", "base_revision": o.Revision}))
	if ok.Revision != o.Revision+1 || ok.Content != "fresh" {
		t.Fatalf("matching base_revision failed: %+v", ok)
	}
}

func TestFsckDetectsCleanStoreAndAssetLifecycle(t *testing.T) {
	app, _, _ := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Fsck owner"}))
	png, err := base64.StdEncoding.DecodeString("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j0ioAAAAASUVORK5CYII=")
	if err != nil {
		t.Fatal(err)
	}
	asset := uploadFixture(t, app, note.ID, "fsck.png", png)
	if asset.MediaObjectID == "" {
		t.Fatal("upload did not create a media object")
	}
	if err := app.store.Fsck(); err != nil {
		t.Fatal(err)
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+asset.MediaObjectID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if err := app.store.Fsck(); err != nil {
		t.Fatal(err)
	}
	if _, err := app.store.Blob(asset.ID); err != nil {
		t.Fatal("trashing a media object must keep its blob for restore")
	}
	if rr := api(t, app, "DELETE", "/v1/trash/"+asset.MediaObjectID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if err := app.store.Fsck(); err != nil {
		t.Fatal(err)
	}
	if _, err := app.store.Blob(asset.ID); err == nil {
		t.Fatal("purging a media object left its blob behind")
	}
}

func TestSyncPushIgnoresTombstoneAndFiltersDeadLinks(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Borrada"}))
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Offline note", "properties": map[string]any{"tags": []string{tag.ID}}}))
	if rr := api(t, app, "DELETE", "/v1/objects/"+tag.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	// A stale mobile edit still references the trashed tag. The push must apply
	// and keep the reference: the tag is restorable, so the relation is not dead.
	nowValue := now()
	push := map[string]any{
		"device_id": "pixel-index-test",
		"base_seq":  0,
		"operations": []any{map[string]any{
			"op_id": "op-dead-link-1", "action": "put", "base_revision": note.Revision,
			"object": map[string]any{
				"id": note.ID, "type_id": "note", "title": note.Title, "content": "offline edit",
				"properties": map[string]any{"tags": []string{tag.ID}}, "links": []string{tag.ID},
				"created_at": note.CreatedAt, "updated_at": nowValue,
			},
		}},
	}
	resp := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", push))
	if len(resp.Errors) != 0 || len(resp.Applied) != 1 {
		t.Fatalf("push failed: %+v", resp)
	}
	got, err := app.store.Object(note.ID)
	if err != nil {
		t.Fatal(err)
	}
	if len(got.Links) != 1 || got.Links[0] != tag.ID || len(got.PropertyLinks) != 1 || got.PropertyLinks[0] != tag.ID {
		t.Fatalf("restorable reference was dropped: %+v", got)
	}
	// Purging the tag makes the reference permanently dead. The next mobile
	// edit must strip it instead of leaving the outbox stuck.
	if rr := api(t, app, "DELETE", "/v1/trash/"+tag.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	got, err = app.store.Object(note.ID)
	if err != nil {
		t.Fatal(err)
	}
	push["operations"] = []any{map[string]any{
		"op_id": "op-dead-link-2", "action": "put", "base_revision": got.Revision,
		"object": map[string]any{
			"id": note.ID, "type_id": "note", "title": note.Title, "content": "after purge",
			"properties": map[string]any{"tags": []string{tag.ID}}, "links": []string{tag.ID},
			"created_at": note.CreatedAt, "updated_at": now(),
		},
	}}
	resp = decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", push))
	if len(resp.Errors) != 0 || len(resp.Applied) != 1 {
		t.Fatalf("push after purge failed: %+v", resp)
	}
	got, err = app.store.Object(note.ID)
	if err != nil {
		t.Fatal(err)
	}
	if len(got.Links) != 0 || len(got.PropertyLinks) != 0 {
		t.Fatalf("purged references survived: %+v", got)
	}
	// A put for an already tombstoned object is ignored instead of resurrecting it.
	resurrect := map[string]any{
		"device_id": "pixel-index-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-resurrect-1", "action": "put", "base_revision": 0,
			"object": map[string]any{"id": tag.ID, "type_id": "tag", "title": "Borrada", "content": "", "properties": map[string]any{}, "links": []string{}, "created_at": nowValue, "updated_at": nowValue},
		}},
	}
	resp = decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", resurrect))
	if len(resp.Conflicts) != 1 || resp.Conflicts[0].Resolution != "ignored_tombstoned" {
		t.Fatalf("tombstone was not enforced: %+v", resp)
	}
	if _, err := app.store.Object(tag.ID); err == nil {
		t.Fatal("stale push resurrected a deleted object")
	}
}

func TestSyncConflictMergesObjectTagLists(t *testing.T) {
	app, _, _ := makeApp(t)
	one := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Uno"}))
	two := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Dos"}))
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Merge tags", "properties": map[string]any{"tags": []string{one.ID}}}))
	base := note.Revision
	// Desktop adds tag two while the phone still has tag one.
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+note.ID, map[string]any{"properties": map[string]any{"tags": []string{two.ID}}}))
	nowValue := now()
	push := map[string]any{
		"device_id": "pixel-merge-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-tag-merge-1", "action": "put", "base_revision": base,
			"object": map[string]any{
				"id": note.ID, "type_id": "note", "title": note.Title, "content": note.Content,
				"properties": map[string]any{"tags": []string{one.ID}}, "links": []string{},
				"created_at": note.CreatedAt, "updated_at": nowValue,
			},
		}},
	}
	resp := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", push))
	if len(resp.Errors) != 0 {
		t.Fatalf("merge failed: %+v", resp)
	}
	got, err := app.store.Object(note.ID)
	if err != nil {
		t.Fatal(err)
	}
	tags, ok := got.Properties["tags"].([]any)
	if !ok || len(tags) != 2 {
		t.Fatalf("tag lists were not unioned: %#v", got.Properties["tags"])
	}
}

func TestTypeChangeRederivesPropertyLinks(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Tipo cambiado"}))
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Nota con relación", "properties": map[string]any{"tags": []string{tag.ID}}}))
	if len(note.PropertyLinks) != 1 {
		t.Fatalf("expected relation before type change: %+v", note.PropertyLinks)
	}
	if rr := api(t, app, "PATCH", "/v1/types/note", map[string]any{"properties": []any{}}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	got, err := app.store.Object(note.ID)
	if err != nil {
		t.Fatal(err)
	}
	if len(got.PropertyLinks) != 0 {
		t.Fatalf("schema change left stale property links: %+v", got.PropertyLinks)
	}
	if rr := api(t, app, "GET", "/v1/objects/"+tag.ID+"/backlinks", nil); strings.Contains(rr.Body.String(), note.ID) {
		t.Fatal("schema change left a stale backlink")
	}
}

func TestDefaultTypesExposeTagsAndVideo(t *testing.T) {
	app, _, _ := makeApp(t)
	types := app.store.Types()
	foundVideo := false
	foundTags := false
	for _, typ := range types {
		if typ.ID == "video" {
			foundVideo = true
		}
		if typ.ID == "note" {
			for _, raw := range typ.Properties {
				if schema, ok := raw.(map[string]any); ok && schemaKey(schema) == "tags" && schema["target_type"] == "tag" {
					foundTags = true
				}
			}
		}
	}
	if !foundVideo || !foundTags {
		t.Fatalf("default types missing video/tags: video=%v tags=%v", foundVideo, foundTags)
	}
}

func TestHealthIsReadyWithoutExporter(t *testing.T) {
	app, _, _ := makeApp(t)
	rr := api(t, app, "GET", "/health", nil)
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), `"status":"ready"`) || !strings.Contains(rr.Body.String(), `"source_of_truth":"lumen-store"`) {
		t.Fatalf("unexpected health: %s", rr.Body.String())
	}
	if strings.Contains(rr.Body.String(), "logseq-db") {
		t.Fatal("health still advertises Logseq as the source of truth")
	}
}
