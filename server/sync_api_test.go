package main

import (
	"encoding/base64"
	"encoding/json"
	"net/http/httptest"
	"strings"
	"testing"
)

func decodeBody[T any](t *testing.T, rr *httptest.ResponseRecorder) T {
	t.Helper()
	if rr.Code < 200 || rr.Code >= 300 {
		t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
	}
	var out T
	if err := json.Unmarshal(rr.Body.Bytes(), &out); err != nil {
		t.Fatal(err)
	}
	return out
}

func TestCleanURLRemovesTrackingAndNormalizesShortLinks(t *testing.T) {
	cases := map[string]string{
		"https://Example.com/a?utm_source=x&utm_medium=y&keep=1#frag": "https://example.com/a?keep=1",
		"https://youtu.be/abc123?si=tracking":                         "https://www.youtube.com/watch?v=abc123",
		"https://www.youtube.com/shorts/abc123?feature=share":         "https://www.youtube.com/watch?v=abc123",
		"https://redd.it/abc123":                                      "https://www.reddit.com/comments/abc123",
		"https://example.com/a?fbclid=1&gclid=2&ref=nav":              "https://example.com/a",
	}
	for input, want := range cases {
		got, err := cleanURL(input)
		if err != nil || got != want {
			t.Fatalf("cleanURL(%q) = %q, %v; want %q", input, got, err, want)
		}
	}
	if _, err := cleanURL("javascript:alert(1)"); err == nil {
		t.Fatal("non-http URL must be rejected")
	}
}

func TestSyncPullPushIdempotentJournalAppendAndConflict(t *testing.T) {
	app, _, _ := makeApp(t)

	status := decodeBody[syncStatusResponse](t, api(t, app, "GET", "/v1/sync/status", nil))
	if status.ServerID == "" || status.HeadSeq == 0 || status.Protocol != syncProtocolVersion {
		t.Fatalf("unexpected status: %+v", status)
	}
	pull := decodeBody[syncPullResponse](t, api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": 0}))
	if len(pull.Types) == 0 {
		t.Fatal("initial pull must include built-in types")
	}
	webTypeFound := false
	for _, typ := range pull.Types {
		if typ.ID == "web" {
			webTypeFound = true
			for _, raw := range typ.Properties {
				if key, ok := raw.(string); ok && key == "status" {
					break
				}
			}
		}
	}
	if !webTypeFound {
		t.Fatal("web type must be present")
	}

	now := now()
	noteID := "mobile-note-1"
	put := map[string]any{
		"device_id": "pixel-test",
		"base_seq":  pull.HeadSeq,
		"operations": []any{map[string]any{
			"op_id": "op-note-0001", "action": "put", "base_revision": 0,
			"object": map[string]any{
				"id": noteID, "type_id": "note", "title": "Nota desde el móvil",
				"content": "Hola desde Android", "properties": map[string]any{}, "links": []string{},
				"created_at": now, "updated_at": now,
			},
		}},
	}
	push := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", put))
	if len(push.Errors) != 0 || len(push.Applied) != 1 {
		t.Fatalf("push failed: %+v", push)
	}
	// Retrying the exact op after a dropped response must not duplicate data.
	pushAgain := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", put))
	if len(pushAgain.Applied) != 1 || len(pushAgain.Errors) != 0 {
		t.Fatalf("idempotent retry failed: %+v", pushAgain)
	}
	objects, _ := app.objects()
	matches := 0
	for _, o := range objects {
		if o.ID == noteID {
			matches++
		}
	}
	if matches != 1 {
		t.Fatalf("retry duplicated mobile note: %d", matches)
	}

	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-30", nil))
	appendOp := map[string]any{
		"device_id": "pixel-test", "base_seq": pull.HeadSeq,
		"operations": []any{map[string]any{
			"op_id": "op-journal-01", "action": "put", "append": true,
			"base_revision": journal.Revision,
			"object": map[string]any{
				"id": journal.ID, "type_id": "journal", "title": journal.Title,
				"content": "Foto del día: café en el parque", "properties": map[string]any{},
				"links": []string{noteID}, "created_at": journal.CreatedAt, "updated_at": now,
			},
		}},
	}
	first := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", appendOp))
	if len(first.Errors) != 0 {
		t.Fatalf("journal append failed: %+v", first)
	}
	second := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", appendOp))
	if len(second.Errors) != 0 {
		t.Fatalf("journal retry failed: %+v", second)
	}
	journal = objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-30", nil))
	if strings.Count(journal.Content, "Foto del día") != 1 {
		t.Fatalf("journal append was duplicated: %q", journal.Content)
	}
	if len(journal.Links) != 1 || journal.Links[0] != noteID {
		t.Fatalf("journal links were not merged: %+v", journal.Links)
	}

	conflict := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Conflicto de edición", "content": "versión PC"}))
	conflict = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+conflict.ID, map[string]any{"content": "versión PC actualizada"}))
	conflictPush := map[string]any{
		"device_id": "pixel-test", "base_seq": pull.HeadSeq,
		"operations": []any{map[string]any{
			"op_id": "op-conflict-1", "action": "put", "base_revision": conflict.Revision - 1,
			"object": map[string]any{
				"id": conflict.ID, "type_id": "note", "title": conflict.Title,
				"content": "versión móvil", "properties": map[string]any{}, "links": []string{},
				"created_at": conflict.CreatedAt, "updated_at": now,
			},
		}},
	}
	conflictResult := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", conflictPush))
	if len(conflictResult.Conflicts) != 1 || conflictResult.Conflicts[0].Resolution != "server_kept_mobile_appended" {
		t.Fatalf("expected explicit conflict resolution: %+v", conflictResult)
	}
	conflict = objectFrom(t, api(t, app, "GET", "/v1/objects/"+conflict.ID, nil))
	if !strings.Contains(conflict.Content, "versión PC actualizada") || !strings.Contains(conflict.Content, "Edición desde móvil") || !strings.Contains(conflict.Content, "versión móvil") {
		t.Fatalf("conflict lost a version: %q", conflict.Content)
	}
}

func TestSyncTaskUsesNewestUpdatedAtAndAssetPush(t *testing.T) {
	app, _, _ := makeApp(t)
	task := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "task", "title": "Tarea móvil", "properties": map[string]any{"status": "todo"}}))
	task = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+task.ID, map[string]any{"properties": map[string]any{"status": "done"}}))
	future := "2099-01-01T10:00:00Z"
	push := map[string]any{
		"device_id": "pixel-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-task-0001", "action": "put", "base_revision": task.Revision,
			"object": map[string]any{
				"id": task.ID, "type_id": "task", "title": task.Title, "content": "",
				"properties": map[string]any{"status": "todo"}, "links": []string{},
				"created_at": task.CreatedAt, "updated_at": future,
			},
		}},
	}
	result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", push))
	if len(result.Errors) != 0 || len(result.Conflicts) != 1 || result.Conflicts[0].Resolution != "task_latest_updated_at" {
		t.Fatalf("task timestamp merge failed: %+v", result)
	}
	task = objectFrom(t, api(t, app, "GET", "/v1/objects/"+task.ID, nil))
	if task.Properties["status"] != "todo" {
		t.Fatalf("newer mobile task state was not applied: %+v", task.Properties)
	}

	imageBytes := []byte{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 0}
	assetPush := map[string]any{
		"device_id": "pixel-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-asset-001", "action": "asset",
			"asset": map[string]any{
				"id": "asset-mobile-1", "object_id": task.ID, "name": "photo.png",
				"mime": "image/png", "created_at": now(),
				"data_base64": base64.StdEncoding.EncodeToString(imageBytes),
			},
		}},
	}
	assetResult := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", assetPush))
	if len(assetResult.Errors) != 0 {
		t.Fatalf("asset push failed: %+v", assetResult)
	}
	var asset Asset
	if err := app.store.Get("asset", "asset-mobile-1", &asset); err != nil || asset.Size != len(imageBytes) {
		t.Fatalf("asset metadata missing: %+v %v", asset, err)
	}
	stored, err := app.store.Blob("asset-mobile-1")
	if err != nil || string(stored) != string(imageBytes) {
		t.Fatal("asset bytes were not stored encrypted")
	}
}

func TestCaptureCleansURLSetsReviewStatusAndDeduplicates(t *testing.T) {
	app, _, _ := makeApp(t)
	first := decodeBody[map[string]any](t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": "https://example.com/article?utm_source=news&keep=1", "title": "Artículo",
		"fetch_preview": false,
	}))
	if first["id"] == nil || first["type_id"] != "web" {
		t.Fatalf("unexpected capture: %+v", first)
	}
	props, _ := first["properties"].(map[string]any)
	if props["status"] != "Para revisar" || props["url"] != "https://example.com/article?keep=1" {
		t.Fatalf("capture did not clean URL/status: %+v", props)
	}
	second := decodeBody[map[string]any](t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": "https://example.com/article?keep=1&fbclid=abc", "title": "Otro título",
		"fetch_preview": false,
	}))
	if second["duplicate"] != true || second["id"] != first["id"] {
		t.Fatalf("duplicate URL was not collapsed: %+v", second)
	}

	png := base64.StdEncoding.EncodeToString([]byte{0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n', 0, 0, 0, 0})
	clientPreview := decodeBody[map[string]any](t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": "https://example.org/client-preview", "title": "Preview local",
		"fetch_preview": false,
		"preview":       map[string]any{"title": "Preview local", "description": "desc", "site_name": "example.org", "image_base64": png, "image_mime": "image/png"},
	}))
	previewProps, _ := clientPreview["properties"].(map[string]any)
	if previewProps["preview_status"] != "ready" || previewProps["image_asset_id"] == nil {
		t.Fatalf("client preview was not stored: %+v", previewProps)
	}
	if previewProps["summary"] != "desc" {
		t.Fatalf("preview description must be stored as summary: %+v", previewProps)
	}
	if _, legacy := previewProps["description"]; legacy {
		t.Fatalf("preview must not duplicate summary as description: %+v", previewProps)
	}
	patched := decodeBody[map[string]any](t, api(t, app, "PATCH", "/v1/objects/"+clientPreview["id"].(string), map[string]any{
		"properties": map[string]any{"summary": "nuevo", "description": "viejo"},
	}))
	patchedProps, _ := patched["properties"].(map[string]any)
	if patchedProps["summary"] != "nuevo" {
		t.Fatalf("summary was not preserved: %+v", patchedProps)
	}
	if _, legacy := patchedProps["description"]; legacy {
		t.Fatalf("description must be removed when summary is present: %+v", patchedProps)
	}
}
