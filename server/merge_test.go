package main

import (
	"encoding/base64"
	"encoding/json"
	"strings"
	"testing"
)

// makeMeetingType creates a custom type whose `related` property points at
// itself. The type id is server-generated, so the self reference needs a
// second PATCH.
func makeMeetingType(t *testing.T, app *App, properties ...any) ObjectType {
	t.Helper()
	typ := makeType(t, app, "Meeting", properties)
	withRelated := append(append([]any{}, properties...), map[string]any{
		"id": "related", "type": "object", "target_type": typ.ID, "multiple": true,
	})
	return decodeBody[ObjectType](t, api(t, app, "PATCH", "/v1/types/"+typ.ID, map[string]any{"properties": withRelated}))
}

func TestMergeFillsEmptyPropertiesUnionsListsAndTrashesSource(t *testing.T) {
	app, _, _ := makeApp(t)
	meeting := makeMeetingType(t, app,
		tagsProperty,
		map[string]any{"id": "status", "type": "text"},
		map[string]any{"id": "rating", "type": "number"},
		map[string]any{"id": "pages", "type": "number"},
	)
	tagOne := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "One"}))
	tagTwo := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Two"}))
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": meeting.ID, "title": "Weekly", "content": "Target body",
		"properties": map[string]any{
			"status": "open", "rating": 4, "tags": []string{tagOne.ID}, "aliases": []any{"Weekly"},
		},
	}))
	source := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": meeting.ID, "title": "Weekly notes", "content": "Source body",
		"properties": map[string]any{
			"status": "done", "rating": 5, "pages": 10,
			"tags": []string{tagOne.ID, tagTwo.ID}, "aliases": []any{"weekly", "Sync"},
		},
	}))
	head, err := app.store.HeadSeq()
	if err != nil {
		t.Fatal(err)
	}

	merged := objectFrom(t, api(t, app, "POST", "/v1/objects/"+source.ID+"/merge", map[string]any{"target_id": target.ID}))
	if merged.ID != target.ID || merged.TypeID != meeting.ID {
		t.Fatalf("merge did not return the target: %+v", merged)
	}
	if merged.Title != target.Title || merged.CreatedAt != target.CreatedAt {
		t.Fatalf("merge changed the target identity: %+v", merged)
	}
	if merged.Revision <= target.Revision {
		t.Fatalf("merge did not bump the target revision: %d -> %d", target.Revision, merged.Revision)
	}
	if merged.Content != "Target body\n\nSource body" {
		t.Fatalf("content was not appended: %q", merged.Content)
	}
	// The target keeps its own single values; empty ones are filled.
	if merged.Properties["status"] != "open" || merged.Properties["rating"] != float64(4) {
		t.Fatalf("target values were overwritten: %#v", merged.Properties)
	}
	if merged.Properties["pages"] != float64(10) {
		t.Fatalf("empty target property was not filled: %#v", merged.Properties)
	}
	if tags, ok := merged.Properties["tags"].([]any); !ok || len(tags) != 2 || tags[0] != tagOne.ID || tags[1] != tagTwo.ID {
		t.Fatalf("tags were not unioned: %#v", merged.Properties["tags"])
	}
	if aliases := aliasesOf(t, merged); !sameStrings(aliases, []string{"Weekly", "Sync"}) {
		t.Fatalf("aliases were not unioned and normalized: %v", aliases)
	}
	if found := decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+tagTwo.ID+"/backlinks", nil)); len(found) != 1 || found[0].ID != target.ID {
		t.Fatalf("unioned tag did not reach the target backlinks: %+v", found)
	}

	// The source is in the trash, not hard-deleted, and can be restored.
	if rr := api(t, app, "GET", "/v1/objects/"+source.ID, nil); rr.Code != 404 {
		t.Fatalf("merged source is still live: %d", rr.Code)
	}
	if rr := api(t, app, "GET", "/v1/objects/"+source.ID+"/backlinks", nil); rr.Code != 404 {
		t.Fatalf("merged source still exposes backlinks: %d", rr.Code)
	}
	trash := decodeBody[[]trashEntry](t, api(t, app, "GET", "/v1/trash", nil))
	if len(trash) != 1 || trash[0].ID != source.ID {
		t.Fatalf("merged source did not reach the trash: %+v", trash)
	}

	pull := pullSince(t, app, head)
	if !objectsContainID(pull.Objects, target.ID) {
		t.Fatalf("mobile did not receive the merged target: %+v", pull.Objects)
	}
	foundTombstone := false
	for _, marker := range pull.Tombstones {
		if marker.ID == source.ID {
			foundTombstone = true
		}
	}
	if !foundTombstone {
		t.Fatalf("mobile did not receive the source tombstone: %+v", pull.Tombstones)
	}
	foundDelete := false
	for _, deleted := range pull.Deleted {
		if deleted.Kind == "object" && deleted.ID == source.ID {
			foundDelete = true
		}
	}
	if !foundDelete {
		t.Fatalf("mobile did not receive the source deletion: %+v", pull.Deleted)
	}

	// The source is restorable: restoring brings back its own body and
	// properties as a duplicate (the links now point at the target).
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+source.ID+"/restore", nil))
	if restored.Content != "Source body" || restored.Properties["status"] != "done" || restored.Revision <= source.Revision {
		t.Fatalf("restored source lost its own data: %+v", restored)
	}
}

func TestMergeRewritesEveryReferenceToTheSource(t *testing.T) {
	app, _, _ := makeApp(t)
	meeting := makeMeetingType(t, app, tagsProperty)
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Shared"}))
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": meeting.ID, "title": "Keep"}))
	source := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": meeting.ID, "title": "Remove", "properties": map[string]any{"tags": []string{tag.ID}},
	}))
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-31", nil))
	journal = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+journal.ID, map[string]any{"links": []string{source.ID}}))
	noteOne := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": meeting.ID, "title": "One", "properties": map[string]any{"related": []string{source.ID, target.ID}},
	}))
	noteTwo := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": meeting.ID, "title": "Two", "properties": map[string]any{"related": []string{source.ID}},
	}))
	// The target itself points at the source: the rewritten relation becomes a
	// self-reference and must disappear.
	target = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+target.ID, map[string]any{
		"properties": map[string]any{"related": []string{source.ID}},
	}))
	head, err := app.store.HeadSeq()
	if err != nil {
		t.Fatal(err)
	}

	merged := objectFrom(t, api(t, app, "POST", "/v1/objects/"+source.ID+"/merge", map[string]any{"target_id": target.ID}))
	if _, exists := merged.Properties["related"]; exists {
		t.Fatalf("self-reference survived the merge: %#v", merged.Properties)
	}
	journal = objectFrom(t, api(t, app, "GET", "/v1/objects/"+journal.ID, nil))
	if len(journal.Links) != 1 || journal.Links[0] != target.ID {
		t.Fatalf("explicit link was not rewritten: %+v", journal.Links)
	}
	if journal.Revision <= 1 {
		t.Fatalf("rewritten referrer did not get a revision bump: %+v", journal)
	}
	for _, note := range []Object{noteOne, noteTwo} {
		got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+note.ID, nil))
		related, ok := got.Properties["related"].([]any)
		if !ok || len(related) != 1 || related[0] != target.ID {
			t.Fatalf("property relation was not rewritten on %s: %#v", note.Title, got.Properties["related"])
		}
		if got.Revision <= note.Revision {
			t.Fatalf("rewritten referrer %s did not get a revision bump: %+v", note.Title, got)
		}
	}
	// No live object still points at the source, and the target now owns every
	// inbound relation.
	objects, err := app.store.Objects()
	if err != nil {
		t.Fatal(err)
	}
	for _, o := range objects {
		for _, id := range allObjectLinks(o) {
			if id == source.ID {
				t.Fatalf("live object %s still references the merged source", o.ID)
			}
		}
	}
	backlinks := decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+target.ID+"/backlinks", nil))
	for _, want := range []string{journal.ID, noteOne.ID, noteTwo.ID} {
		if !objectsContainID(backlinks, want) {
			t.Fatalf("target backlinks are missing %s: %+v", want, backlinks)
		}
	}
	if found := decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+tag.ID+"/backlinks", nil)); len(found) != 1 || found[0].ID != target.ID {
		t.Fatalf("source tag relation did not move to the target: %+v", found)
	}

	pull := pullSince(t, app, head)
	for _, want := range []string{target.ID, journal.ID, noteOne.ID, noteTwo.ID} {
		if !objectsContainID(pull.Objects, want) {
			t.Fatalf("sync pull is missing rewritten object %s: %+v", want, pull.Objects)
		}
	}
}

func TestMergeRejectsInvalidRequests(t *testing.T) {
	app, _, _ := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Source note"}))
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Different type"}))
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Target note"}))
	trashedTarget := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Trashed target"}))
	if rr := api(t, app, "DELETE", "/v1/objects/"+trashedTarget.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	cases := []struct {
		name   string
		source string
		body   map[string]any
		want   int
	}{
		{"same object", note.ID, map[string]any{"target_id": note.ID}, 400},
		{"different type", note.ID, map[string]any{"target_id": book.ID}, 400},
		{"missing target id", note.ID, map[string]any{}, 400},
		{"missing target", note.ID, map[string]any{"target_id": "missing-id"}, 400},
		{"trashed target", note.ID, map[string]any{"target_id": trashedTarget.ID}, 400},
		{"missing source", "missing-id", map[string]any{"target_id": target.ID}, 404},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			rr := api(t, app, "POST", "/v1/objects/"+tc.source+"/merge", tc.body)
			if rr.Code != tc.want {
				t.Fatalf("HTTP %d, want %d: %s", rr.Code, tc.want, rr.Body.String())
			}
		})
	}

	rr := api(t, app, "POST", "/v1/objects/"+note.ID+"/merge", map[string]any{
		"target_id": target.ID, "base_revision": note.Revision + 5,
	})
	if rr.Code != 409 || !strings.Contains(rr.Body.String(), `"conflict":true`) {
		t.Fatalf("expected 409, got %d: %s", rr.Code, rr.Body.String())
	}
	var conflict struct {
		Current Object `json:"current"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &conflict); err != nil || conflict.Current.ID != note.ID {
		t.Fatalf("409 did not return the current source: %s", rr.Body.String())
	}
	rr = api(t, app, "POST", "/v1/objects/"+note.ID+"/merge", map[string]any{
		"target_id": target.ID, "target_base_revision": target.Revision + 5,
	})
	if rr.Code != 409 || !strings.Contains(rr.Body.String(), `"conflict":true`) {
		t.Fatalf("expected 409 for the target, got %d: %s", rr.Code, rr.Body.String())
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &conflict); err != nil || conflict.Current.ID != target.ID {
		t.Fatalf("409 did not return the current target: %s", rr.Body.String())
	}
	objectFrom(t, api(t, app, "POST", "/v1/objects/"+note.ID+"/merge", map[string]any{
		"target_id": target.ID, "base_revision": note.Revision, "target_base_revision": target.Revision,
	}))
	// The source is now in the trash: merging it again is rejected instead of
	// silently merging stale data.
	if rr = api(t, app, "POST", "/v1/objects/"+note.ID+"/merge", map[string]any{"target_id": target.ID}); rr.Code != 400 {
		t.Fatalf("double merge returned %d: %s", rr.Code, rr.Body.String())
	}
}

func TestMergeAppendsBlocksAndKeepsIdsStable(t *testing.T) {
	app, _, _ := makeApp(t)
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Blocks target", "content": "Target",
		"properties": map[string]any{
			"blocks": []any{map[string]any{"id": "block-target", "text": "Target"}}, "rich_text": "<p>Target</p>",
		},
	}))
	source := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Blocks source", "content": "Source",
		"properties": map[string]any{
			"blocks": []any{map[string]any{"id": "block-source", "text": "Source"}}, "rich_text": "<p>Source</p>",
		},
	}))
	merged := objectFrom(t, api(t, app, "POST", "/v1/objects/"+source.ID+"/merge", map[string]any{"target_id": target.ID}))
	blocks, ok := merged.Properties["blocks"].([]any)
	if !ok || len(blocks) != 2 {
		t.Fatalf("blocks were not appended: %#v", merged.Properties["blocks"])
	}
	if blocks[0].(map[string]any)["id"] != "block-target" || blocks[1].(map[string]any)["id"] != "block-source" {
		t.Fatalf("block ids were not preserved: %#v", blocks)
	}
	if _, exists := merged.Properties["rich_text"]; exists {
		t.Fatalf("stale rich_text survived the content merge: %#v", merged.Properties)
	}

	// A colliding source id is re-identified so block edits stay unambiguous.
	first := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Collision target", "content": "A",
		"properties": map[string]any{"blocks": []any{map[string]any{"id": "same", "text": "A"}}},
	}))
	second := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Collision source", "content": "B",
		"properties": map[string]any{"blocks": []any{map[string]any{"id": "same", "text": "B"}}},
	}))
	collided := objectFrom(t, api(t, app, "POST", "/v1/objects/"+second.ID+"/merge", map[string]any{"target_id": first.ID}))
	collidedBlocks, ok := collided.Properties["blocks"].([]any)
	if !ok || len(collidedBlocks) != 2 {
		t.Fatalf("colliding blocks were not appended: %#v", collided.Properties["blocks"])
	}
	if collidedBlocks[0].(map[string]any)["id"] != "same" {
		t.Fatalf("target block id changed: %#v", collidedBlocks[0])
	}
	if collidedBlocks[1].(map[string]any)["id"] == "same" {
		t.Fatalf("colliding source block kept a duplicate id: %#v", collidedBlocks[1])
	}
}

func TestMergeWithEmptySourceContentKeepsTargetBody(t *testing.T) {
	app, _, _ := makeApp(t)
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Empty source target", "content": "Target body",
		"properties": map[string]any{
			"blocks": []any{map[string]any{"id": "b1", "text": "Target body"}}, "rich_text": "<p>Target body</p>",
		},
	}))
	source := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Empty source", "properties": map[string]any{"aliases": []any{"Empty"}},
	}))
	merged := objectFrom(t, api(t, app, "POST", "/v1/objects/"+source.ID+"/merge", map[string]any{"target_id": target.ID}))
	if merged.Content != "Target body" {
		t.Fatalf("empty source changed the target body: %q", merged.Content)
	}
	if merged.Properties["blocks"] == nil || merged.Properties["rich_text"] != "<p>Target body</p>" {
		t.Fatalf("rich formats were dropped without a content change: %#v", merged.Properties)
	}
	if aliases := aliasesOf(t, merged); !sameStrings(aliases, []string{"Empty"}) {
		t.Fatalf("aliases were not merged: %v", aliases)
	}
}

func TestMergeMovesAttachmentsToTheTarget(t *testing.T) {
	app, _, _ := makeApp(t)
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Attachment target"}))
	source := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Attachment source"}))
	asset := uploadFixture(t, app, source.ID, "source.pdf", []byte("%PDF-1.4\nsource attachment\n"))

	merged := objectFrom(t, api(t, app, "POST", "/v1/objects/"+source.ID+"/merge", map[string]any{"target_id": target.ID}))
	assets, ok := merged.Properties["assets"].([]any)
	if !ok || len(assets) != 1 || assets[0].(map[string]any)["id"] != asset.ID {
		t.Fatalf("attachment metadata did not move: %#v", merged.Properties["assets"])
	}
	var stored Asset
	if err := app.store.Get("asset", asset.ID, &stored); err != nil || stored.ObjectID != target.ID {
		t.Fatalf("attachment ownership did not move: %+v %v", stored, err)
	}
	if !strings.Contains(api(t, app, "GET", "/v1/assets?object_id="+target.ID, nil).Body.String(), asset.ID) {
		t.Fatal("target cannot list the moved attachment")
	}
	// Purging the source must not delete an attachment that now belongs to the
	// target.
	if rr := api(t, app, "DELETE", "/v1/trash/"+source.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if _, err := app.store.Blob(asset.ID); err != nil {
		t.Fatal("purging the merged source deleted the target attachment")
	}
}

func TestMergeMediaObjectsKeepsTheTargetBlob(t *testing.T) {
	app, _, _ := makeApp(t)
	png, err := base64.StdEncoding.DecodeString("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+j0ioAAAAASUVORK5CYII=")
	if err != nil {
		t.Fatal(err)
	}
	first := uploadFixture(t, app, "", "first.png", png)
	second := uploadFixture(t, app, "", "second.png", png)
	if first.MediaObjectID == "" || second.MediaObjectID == "" {
		t.Fatal("uploads did not create media objects")
	}
	merged := objectFrom(t, api(t, app, "POST", "/v1/objects/"+first.MediaObjectID+"/merge", map[string]any{"target_id": second.MediaObjectID}))
	if merged.Properties["asset_id"] != second.ID {
		t.Fatalf("target lost its own asset: %#v", merged.Properties)
	}
	// The source's own blob stays with the source in the trash.
	if rr := api(t, app, "DELETE", "/v1/trash/"+first.MediaObjectID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if _, err := app.store.Blob(first.ID); err == nil {
		t.Fatal("purging the merged media source left its blob behind")
	}
	if _, err := app.store.Blob(second.ID); err != nil {
		t.Fatal("purging the merged media source deleted the target blob")
	}
}
