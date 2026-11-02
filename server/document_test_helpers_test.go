package main

import "testing"

// documentPatchPayload is explicit at canonical edit call sites. Unlike api(),
// it requires a document and supplies a fresh CAS base only when the scenario
// did not deliberately provide a stale one. Metadata and documents are untouched.
func documentPatchPayload(t *testing.T, app *App, id string, payload map[string]any) map[string]any {
	t.Helper()
	if _, ok := payload["document"]; !ok {
		t.Fatal("canonical patch fixture requires document")
	}
	if _, ok := payload["base_revision"]; !ok {
		current, err := app.store.Object(id)
		if err != nil {
			t.Fatal(err)
		}
		payload["base_revision"] = current.Revision
	}
	payload["op_id"] = newID()
	return payload
}
