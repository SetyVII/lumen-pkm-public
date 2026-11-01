package main

import (
	"encoding/json"
	"os"
	"strings"
	"testing"
)

func TestToggleBodyAndStateAcrossDesktopAPIAndMobileSync(t *testing.T) {
	app, _, _ := makeApp(t)
	raw, err := os.ReadFile("../contracts/fixtures/toggles.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture struct {
		Objects []Object `json:"objects"`
	}
	if err = json.Unmarshal(raw, &fixture); err != nil {
		t.Fatal(err)
	}
	created := objectFrom(t, api(t, app, "POST", "/v1/objects", fixture.Objects[0]))
	head, _ := app.store.HeadSeq()
	blocks := created.Properties["blocks"].([]any)
	toggle := blocks[0].(map[string]any)
	toggle["text"] = "▶ Esto es un toggle"
	toggle["runs"] = []any{map[string]any{"text": toggle["text"]}}
	child := toggle["children"].([]any)[0].(map[string]any)
	child["text"] = "Editado en Android"
	child["runs"] = []any{map[string]any{"text": child["text"]}}
	created.Content = "▶ Esto es un toggle\nEditado en Android\n▶ Toggle anidado\nTexto anidado editable\nFuera del toggle"
	result := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{
		"device_id": "toggle-mobile", "operations": []any{map[string]any{
			"op_id": "close-toggle", "action": "put", "base_revision": created.Revision, "object": created,
		}},
	}))
	if len(result.Errors) != 0 || len(result.Applied) != 1 {
		t.Fatalf("push failed: %+v", result)
	}
	desktop := objectFrom(t, api(t, app, "GET", "/v1/objects/"+created.ID, nil))
	desktopBlock := desktop.Properties["blocks"].([]any)[0].(map[string]any)
	if desktopBlock["text"] != "▶ Esto es un toggle" || !strings.Contains(desktop.Content, "Editado en Android") {
		t.Fatalf("desktop lost toggle state/body: %+v", desktop)
	}
	head, _ = app.store.HeadSeq()
	desktopBlock["text"] = "▼ Esto es un toggle"
	desktopBlock["runs"] = []any{map[string]any{"text": desktopBlock["text"]}}
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+created.ID, map[string]any{"properties": desktop.Properties, "content": strings.Replace(desktop.Content, "▶ Esto", "▼ Esto", 1)}))
	pull := decodeBody[syncPullResponse](t, api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": head}))
	found := false
	for _, object := range pull.Objects {
		if object.ID == created.ID {
			block := object.Properties["blocks"].([]any)[0].(map[string]any)
			if block["text"] != "▼ Esto es un toggle" || len(block["children"].([]any)) != 2 {
				t.Fatalf("mobile pull lost state/body: %+v", block)
			}
			found = true
		}
	}
	if !found {
		t.Fatal("mobile pull missing updated toggle")
	}
}
