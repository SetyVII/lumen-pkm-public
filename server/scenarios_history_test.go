package main

// User journey: edit a note three times, browse its history, restore an older
// version, and verify that history is not truncated and that the restore syncs
// while the snapshots themselves never reach a mobile pull. This combines
// version history, tags, backlinks and the sync journal.

import (
	"encoding/json"
	"strings"
	"testing"
)

func TestScenarioVersionHistoryJourney(t *testing.T) {
	app, _, _ := makeApp(t)
	tagA := scenarioTag(t, app, "alpha")
	tagB := scenarioTag(t, app, "beta")
	tagC := scenarioTag(t, app, "gamma")

	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Draft one", "document": documentFixture("alpha ALPHA_TOKEN"),
		"properties": map[string]any{"tags": []string{tagA.ID}},
	}))
	note = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+note.ID, documentPatchPayload(t, app, note.ID, map[string]any{
		"title": "Draft two", "document": documentFixture("beta BETA_TOKEN two"),
		"properties": map[string]any{"tags": []string{tagB.ID}},
	})))
	note = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+note.ID, documentPatchPayload(t, app, note.ID, map[string]any{
		"title": "Draft three", "document": documentFixture("gamma GAMMA_TOKEN three four"),
		"properties": map[string]any{"tags": []string{tagC.ID}},
	})))
	note = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+note.ID, documentPatchPayload(t, app, note.ID, map[string]any{
		"title": "Draft four", "document": documentFixture("delta DELTA_TOKEN four"),
		"properties": map[string]any{"tags": []string{tagA.ID}},
	})))
	if note.Revision != 4 {
		t.Fatalf("current revision = %d", note.Revision)
	}

	// Step 1: the three previous states are listed with word counts.
	versions := versionSummaries(t, app, note.ID)
	if len(versions) != 3 {
		t.Fatalf("version count = %d, want 3: %+v", len(versions), versions)
	}
	wantRevisions := []uint64{3, 2, 1}
	wantWords := []int{4, 3, 2}
	for i, version := range versions {
		if version.Revision != wantRevisions[i] || version.WordCount != wantWords[i] {
			t.Fatalf("version[%d] = %+v, want revision %d with %d words", i, version, wantRevisions[i], wantWords[i])
		}
	}
	snapshot2 := versionSnapshot(t, app, note.ID, 2)
	if snapshot2.Content != "beta BETA_TOKEN two" || snapshot2.Title != "Draft two" {
		t.Fatalf("revision 2 snapshot is wrong: %+v", snapshot2)
	}
	if tags, ok := snapshot2.Properties["tags"].([]any); !ok || len(tags) != 1 || tags[0] != tagB.ID {
		t.Fatalf("revision 2 snapshot lost its tags: %#v", snapshot2.Properties["tags"])
	}

	// Step 2: restore revision 2. The current title is preserved (titles are
	// workspace-unique and restore only brings back content/properties/links),
	// while content and tags come from the snapshot.
	head, err := app.store.HeadSeq()
	if err != nil {
		t.Fatal(err)
	}
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+note.ID+"/restore/2", nil))
	if restored.Revision != 5 {
		t.Fatalf("restore revision = %d, want 5", restored.Revision)
	}
	if restored.Content != "beta BETA_TOKEN two" {
		t.Fatalf("restore content = %q", restored.Content)
	}
	if restored.Title != "Draft four" {
		t.Fatalf("restore changed the current title: %q", restored.Title)
	}
	if tags, ok := restored.Properties["tags"].([]any); !ok || len(tags) != 1 || tags[0] != tagB.ID {
		t.Fatalf("restore did not bring the old tags back: %#v", restored.Properties["tags"])
	}

	// Step 3: history is not truncated. The pre-restore state was snapshotted
	// as revision 4, and the older snapshots are still readable.
	versions = versionSummaries(t, app, note.ID)
	if len(versions) != 4 || versions[0].Revision != 4 {
		t.Fatalf("restore truncated history: %+v", versions)
	}
	if snapshot := versionSnapshot(t, app, note.ID, 2); snapshot.Content != "beta BETA_TOKEN two" {
		t.Fatalf("restored snapshot changed: %+v", snapshot)
	}
	if snapshot := versionSnapshot(t, app, note.ID, 4); snapshot.Content != "delta DELTA_TOKEN four" {
		t.Fatalf("pre-restore state was not snapshotted: %+v", snapshot)
	}

	// Step 4: the restored state syncs as a normal object change; snapshots
	// never appear in a pull.
	pull := pullSince(t, app, head)
	if len(pull.Objects) != 1 || pull.Objects[0].ID != note.ID || pull.Objects[0].Content != "beta BETA_TOKEN two" {
		t.Fatalf("restore did not sync as one object change: %+v", pull.Objects)
	}
	raw, err := json.Marshal(pull)
	if err != nil {
		t.Fatal(err)
	}
	for _, leaked := range []string{"gamma GAMMA_TOKEN", "delta DELTA_TOKEN"} {
		if strings.Contains(string(raw), leaked) {
			t.Fatalf("a version snapshot leaked into sync pull: %s", leaked)
		}
	}

	// Step 5: the restored tag relation is live again; the abandoned tag is not.
	if ids := objectIDs(scenarioBacklinks(t, app, tagB.ID)); !scenarioContainsID(ids, note.ID) {
		t.Fatalf("restored tag backlink is missing: %v", ids)
	}
	if ids := objectIDs(scenarioBacklinks(t, app, tagC.ID)); scenarioContainsID(ids, note.ID) {
		t.Fatalf("abandoned tag backlink survived the restore: %v", ids)
	}
}
