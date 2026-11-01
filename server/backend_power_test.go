package main

import (
	"bytes"
	"encoding/json"
	"fmt"
	"net/url"
	"os"
	"strings"
	"sync"
	"testing"
)

func TestExtendedRecurrenceCalendarAndSkipJourney(t *testing.T) {
	cases := []struct{ mode, due, want string }{
		{"yearly", "2028-02-29", "2029-02-28"}, {"yearly", "2026-12-31", "2027-12-31"},
		{"weekdays", "2026-10-30", "2026-11-02"}, {"weekdays", "2026-11-01", "2026-11-02"},
		{"last_day", "2026-01-15", "2026-01-31"}, {"last_day", "2026-01-31", "2026-02-28"},
		{"last_day", "2028-02-01", "2028-02-29"}, {"last_weekday", "2026-10-01", "2026-10-30"},
		{"last_weekday", "2026-10-30", "2026-11-30"}, {"last_weekday", "2026-10-31", "2026-11-30"},
	}
	for _, tc := range cases {
		t.Run(tc.mode+tc.due, func(t *testing.T) {
			rule, err := parseRecurrence(map[string]any{"mode": tc.mode})
			if err != nil {
				t.Fatal(err)
			}
			got, err := rule.NextDate(tc.due)
			if err != nil || got != tc.want {
				t.Fatalf("%s: %s %v, want %s", tc.mode, got, err, tc.want)
			}
		})
	}
	app, _, _ := makeApp(t)
	for _, outcome := range []string{"skip", "excuse"} {
		task := createTask(t, app, "Outcome "+outcome, map[string]any{"due_date": "2026-10-30", "recurrence": map[string]any{"mode": "weekdays"}})
		head, _ := app.store.HeadSeq()
		first := decodeBody[completeTaskResponse](t, api(t, app, "POST", "/v1/tasks/"+task.ID+"/"+outcome, nil))
		if first.Next == nil || !first.Created || first.NextDate != "2026-11-02" || first.Task.Properties["completed_at"] != nil {
			t.Fatalf("invalid outcome: %+v", first)
		}
		again := decodeBody[completeTaskResponse](t, api(t, app, "POST", "/v1/tasks/"+task.ID+"/"+outcome, nil))
		if again.Created || again.Next.ID != first.Next.ID || again.Task.Revision != first.Task.Revision {
			t.Fatal("retry forked series or changed revision")
		}
		if first.Next.Properties["occurrence_outcome"] != nil {
			t.Fatal("successor inherited resolution")
		}
		pull := decodeBody[syncPullResponse](t, api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": head}))
		if len(pull.Objects) != 2 {
			t.Fatalf("outcome/successor not synced together: %+v", pull)
		}
	}
}

func TestHabitTrackingPersistenceConflictsAndStreaks(t *testing.T) {
	app, path, key := makeApp(t)
	habit := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "habit", "title": "Private reading habit sentinel", "properties": map[string]any{"habit_start": "2026-10-26", "habit_schedule": []int{1, 2, 3, 4, 5}, "habit_target": 10}}))
	stale := habit.Revision
	for _, date := range []string{"2026-10-26", "2026-10-27", "2026-10-28", "2026-10-29", "2026-10-30"} {
		habit = objectFrom(t, api(t, app, "PUT", "/v1/habits/"+habit.ID+"/entries/"+date, map[string]any{"value": 10, "base_revision": habit.Revision}))
	}
	stats := decodeBody[habitStatistics](t, api(t, app, "GET", "/v1/habits/"+habit.ID+"/stats?as_of=2026-11-02", nil))
	if stats.CompletedDays != 5 || stats.CurrentStreak != 5 || stats.BestStreak != 5 || stats.Total != 50 {
		t.Fatalf("weekend/today broke streak: %+v", stats)
	}
	broken := decodeBody[habitStatistics](t, api(t, app, "GET", "/v1/habits/"+habit.ID+"/stats?as_of=2026-11-03", nil))
	if broken.CurrentStreak != 0 {
		t.Fatalf("missed Monday did not break streak: %+v", broken)
	}
	if rr := api(t, app, "PUT", "/v1/habits/"+habit.ID+"/entries/2026-10-30", map[string]any{"value": 20, "base_revision": stale}); rr.Code != 409 {
		t.Fatal("stale update accepted")
	}
	for date, value := range map[string]any{"2026-10-31": 10, "2026-02-30": 10, "2026-10-25": 10, "2026-10-30": -1} {
		if rr := api(t, app, "PUT", "/v1/habits/"+habit.ID+"/entries/"+date, map[string]any{"value": value, "base_revision": habit.Revision}); rr.Code != 400 {
			t.Fatalf("invalid habit entry accepted: %s %s", date, rr.Body.String())
		}
	}
	same := objectFrom(t, api(t, app, "PUT", "/v1/habits/"+habit.ID+"/entries/2026-10-30", map[string]any{"value": 10, "base_revision": habit.Revision}))
	if same.Revision != habit.Revision {
		t.Fatal("same daily value created a revision")
	}
	habit = objectFrom(t, api(t, app, "DELETE", "/v1/habits/"+habit.ID+"/entries/2026-10-28", map[string]any{"base_revision": habit.Revision}))
	stats = decodeBody[habitStatistics](t, api(t, app, "GET", "/v1/habits/"+habit.ID+"/stats?as_of=2026-11-01", nil))
	if stats.CurrentStreak != 2 || stats.BestStreak != 2 {
		t.Fatalf("correction did not recompute: %+v", stats)
	}
	if err := app.store.Close(); err != nil {
		t.Fatal(err)
	}
	reopened, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	app.store = reopened
	stored := objectFrom(t, api(t, app, "GET", "/v1/objects/"+habit.ID, nil))
	if len(stored.Properties["habit_entries"].(map[string]any)) != 4 {
		t.Fatal("habit entries lost on restart")
	}
	for _, suffix := range []string{"", "-wal"} {
		raw, _ := os.ReadFile(path + suffix)
		if bytes.Contains(raw, []byte(habit.Title)) {
			t.Fatal("plaintext habit leaked to SQLite")
		}
	}
}

func TestBlockReferencesEditDeleteRestoreAndNestedJourney(t *testing.T) {
	app, _, _ := makeApp(t)
	source := objectFrom(t, api(t, app, "POST", "/v1/objects/markdown", map[string]any{"type_id": "note", "title": "Source blocks", "markdown": "Original source\n\nSecond paragraph"}))
	list := rawObjectBlocks(source)
	id := list[0].(map[string]any)["id"].(string)
	reference := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Embedded source", "properties": map[string]any{"blocks": []any{map[string]any{"id": "embed-one", "type": "embed", "text": "Source", "runs": []any{map[string]any{"text": "Source", "href": blockHrefPrefix + id}}}}}}))
	refs := api(t, app, "GET", "/v1/blocks/"+id+"/references", nil)
	if refs.Code != 200 || !strings.Contains(refs.Body.String(), `"count":1`) || !strings.Contains(refs.Body.String(), reference.ID) {
		t.Fatal(refs.Body.String())
	}
	body := map[string]any{"base_revision": source.Revision, "block": map[string]any{"type": "paragraph", "text": "Updated source"}}
	edited := api(t, app, "PATCH", "/v1/objects/"+source.ID+"/blocks/"+id, body)
	if edited.Code != 200 {
		t.Fatal(edited.Body.String())
	}
	live := decodeBody[blockHit](t, api(t, app, "GET", "/v1/blocks/"+id, nil))
	if live.Block["text"] != "Updated source" || live.Block["id"] != id {
		t.Fatal("reference did not follow source edit")
	}
	if rr := api(t, app, "PATCH", "/v1/objects/"+source.ID+"/blocks/"+id, body); rr.Code != 409 {
		t.Fatal("stale block overwrite accepted")
	}
	search := api(t, app, "GET", "/v1/blocks?q="+url.QueryEscape("Updated source"), nil)
	if !strings.Contains(search.Body.String(), id) {
		t.Fatal("search missed edited block")
	}
	before := objectFrom(t, api(t, app, "GET", "/v1/objects/"+source.ID, nil))
	if !strings.Contains(before.Content, "Updated source") || before.Properties["rich_text"] != nil {
		t.Fatal("block edit left obsolete body")
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+source.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "GET", "/v1/blocks/"+id, nil); rr.Code != 404 {
		t.Fatal("trashed block still resolves")
	}
	objectFrom(t, api(t, app, "POST", "/v1/objects/"+source.ID+"/restore", nil))
	if rr := api(t, app, "GET", "/v1/blocks/"+id, nil); rr.Code != 200 {
		t.Fatal("restored block does not resolve")
	}
	source = objectFrom(t, api(t, app, "GET", "/v1/objects/"+source.ID, nil))
	nested := map[string]any{"type": "group", "text": "Group", "children": []any{map[string]any{"type": "paragraph", "text": "Nested sentinel"}}}
	added := api(t, app, "POST", "/v1/objects/"+source.ID+"/blocks", map[string]any{"base_revision": source.Revision, "block": nested})
	if added.Code != 201 {
		t.Fatal(added.Body.String())
	}
	source = objectFrom(t, api(t, app, "GET", "/v1/objects/"+source.ID, nil))
	var nestedID string
	walkBlocks(rawObjectBlocks(source), 0, func(b map[string]any) {
		if b["text"] == "Nested sentinel" {
			nestedID, _ = b["id"].(string)
		}
	})
	if nestedID == "" || !strings.Contains(source.Content, "Nested sentinel") {
		t.Fatal("nested block lost")
	}
	deleted := api(t, app, "DELETE", "/v1/objects/"+source.ID+"/blocks/"+nestedID, map[string]any{"base_revision": source.Revision})
	if deleted.Code != 200 {
		t.Fatal(deleted.Body.String())
	}
	if rr := api(t, app, "GET", "/v1/blocks/"+nestedID, nil); rr.Code != 404 {
		t.Fatal("deleted nested block resolves")
	}
}

func TestBlockAmbiguityReadScopesAndFixture(t *testing.T) {
	raw, err := os.ReadFile("../contracts/fixtures/backend-power.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture map[string]any
	if err = json.Unmarshal(raw, &fixture); err != nil {
		t.Fatal(err)
	}
	app, _, _ := makeApp(t)
	for i := 0; i < 2; i++ {
		objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": fmt.Sprintf("Duplicate block %d", i), "properties": map[string]any{"blocks": []any{fixture["block_reference"]}}}))
	}
	if rr := api(t, app, "GET", "/v1/blocks/reference-example", nil); rr.Code != 409 {
		t.Fatal("ambiguous ID resolved arbitrarily")
	}
	if err := app.SetScopes("api:read"); err != nil {
		t.Fatal(err)
	}
	if rr := api(t, app, "POST", "/v1/objects/missing/blocks", map[string]any{"base_revision": 1, "block": fixture["block_reference"]}); rr.Code != 403 {
		t.Fatal("read token edited blocks")
	}
	if rr := api(t, app, "PUT", "/v1/habits/missing/entries/2026-10-31", map[string]any{"base_revision": 1, "value": 1}); rr.Code != 403 {
		t.Fatal("read token edited habits")
	}
	if rr := api(t, app, "POST", "/v1/tasks/missing/skip", nil); rr.Code != 403 {
		t.Fatal("read token skipped task")
	}
}

func TestConcurrentTaskOutcomesCannotFork(t *testing.T) {
	app, _, _ := makeApp(t)
	task := createTask(t, app, "Concurrent outcome", map[string]any{"due_date": "2026-10-30", "recurrence": map[string]any{"mode": "daily"}})
	var wg sync.WaitGroup
	for _, action := range []string{"skip", "complete", "excuse"} {
		wg.Add(1)
		go func(action string) {
			defer wg.Done()
			rr := api(t, app, "POST", "/v1/tasks/"+task.ID+"/"+action, nil)
			if rr.Code != 200 {
				t.Errorf("%s failed: %s", action, rr.Body.String())
			}
		}(action)
	}
	wg.Wait()
	objects, err := app.objects()
	if err != nil || len(objects) != 2 {
		t.Fatalf("forked series: %d %v", len(objects), err)
	}
}

func TestHabitSyncExportRestoreJourney(t *testing.T) {
	app, _, _ := makeApp(t)
	raw, err := os.ReadFile("../contracts/fixtures/backend-power.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture struct {
		Habit Object `json:"habit"`
	}
	if err = json.Unmarshal(raw, &fixture); err != nil {
		t.Fatal(err)
	}
	habit := objectFrom(t, api(t, app, "POST", "/v1/objects", fixture.Habit))
	head, _ := app.store.HeadSeq()
	habit = objectFrom(t, api(t, app, "PUT", "/v1/habits/"+habit.ID+"/entries/2026-10-28", map[string]any{"base_revision": habit.Revision, "value": 20}))
	pull := decodeBody[syncPullResponse](t, api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": head}))
	if len(pull.Objects) != 1 || len(pull.Objects[0].Properties["habit_entries"].(map[string]any)) != 3 {
		t.Fatal("habit check-in missing from sync")
	}
	habit.Properties["habit_entries"].(map[string]any)["2026-10-29"] = float64(10)
	pushed := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "habit-test", "operations": []any{map[string]any{"op_id": "habit-write", "action": "put", "base_revision": habit.Revision, "object": habit}}}))
	if len(pushed.Errors) != 0 || len(pushed.Applied) != 1 {
		t.Fatalf("habit push rejected: %+v", pushed)
	}
	invalid := objectFrom(t, api(t, app, "GET", "/v1/objects/"+habit.ID, nil))
	invalid.Properties["habit_target"] = float64(-1)
	rejected := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "habit-test", "operations": []any{map[string]any{"op_id": "habit-invalid", "action": "put", "base_revision": invalid.Revision, "object": invalid}}}))
	if len(rejected.Errors) != 1 {
		t.Fatal("sync bypassed habit validation")
	}
	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	found := false
	for name, data := range entries {
		if strings.HasSuffix(name, ".md") && bytes.Contains(data, []byte(habit.Title)) {
			found = bytes.Contains(data, []byte("habit_entries")) && bytes.Contains(data, []byte("2026-10-29"))
		}
	}
	if !found {
		t.Fatal("habit entries not exported")
	}
	if len(versionSummaries(t, app, habit.ID)) < 2 {
		t.Fatal("habit changes not snapshotted")
	}
	if rr := api(t, app, "DELETE", "/v1/objects/"+habit.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	if rr := api(t, app, "GET", "/v1/habits/"+habit.ID+"/stats", nil); rr.Code != 404 {
		t.Fatal("trashed habit still visible")
	}
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+habit.ID+"/restore", nil))
	if len(restored.Properties["habit_entries"].(map[string]any)) != 4 {
		t.Fatal("restore lost habit entries")
	}
}

func TestBlockEditsProtectUnrepresentedBodyAndClearText(t *testing.T) {
	app, _, _ := makeApp(t)
	source := objectFrom(t, api(t, app, "POST", "/v1/objects/markdown", map[string]any{"type_id": "note", "title": "Protected block body", "markdown": "Visible block"}))
	id := rawObjectBlocks(source)[0].(map[string]any)["id"].(string)
	// Preserve a deliberately mixed legacy body for the regression.
	source.Content = "Visible block\nOlder text missing from blocks"
	source = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+source.ID, map[string]any{"content": source.Content, "properties": source.Properties}))
	rr := api(t, app, "PATCH", "/v1/objects/"+source.ID+"/blocks/"+id, map[string]any{"base_revision": source.Revision, "block": map[string]any{"type": "paragraph", "text": "Replacement"}})
	if rr.Code != 409 {
		t.Fatal("block edit erased unrepresented legacy text")
	}
	intact := objectFrom(t, api(t, app, "GET", "/v1/objects/"+source.ID, nil))
	if intact.Content != source.Content {
		t.Fatal("rejected edit changed body")
	}
	source = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+source.ID, map[string]any{"content": "Visible block", "properties": source.Properties}))
	rr = api(t, app, "PATCH", "/v1/objects/"+source.ID+"/blocks/"+id, map[string]any{"base_revision": source.Revision, "block": map[string]any{"type": "paragraph", "text": ""}})
	if rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	hit := decodeBody[blockHit](t, api(t, app, "GET", "/v1/blocks/"+id, nil))
	if hit.Block["text"] != "" {
		t.Fatal("could not clear block without changing ID")
	}
}

func TestTemplateCopiesMintFreshNestedBlockIDs(t *testing.T) {
	source := []any{map[string]any{"id": "group-source", "type": "group", "text": "Group", "children": []any{map[string]any{"id": "child-source", "type": "paragraph", "text": "Child"}}, "columns": []any{[]any{map[string]any{"id": "column-source", "type": "paragraph", "text": "Column"}}}}}
	first, err := cloneBlocksWithNewIDs(source)
	if err != nil {
		t.Fatal(err)
	}
	second, err := cloneBlocksWithNewIDs(source)
	if err != nil {
		t.Fatal(err)
	}
	seen := map[string]bool{"group-source": true, "child-source": true, "column-source": true}
	for _, list := range [][]any{first, second} {
		walkBlocks(list, 0, func(b map[string]any) {
			id, _ := b["id"].(string)
			if id == "" || seen[id] {
				t.Errorf("copied block reused an ID: %s", id)
			}
			seen[id] = true
		})
	}
	if source[0].(map[string]any)["id"] != "group-source" {
		t.Fatal("copy modified original template")
	}
}

func TestBlockSearchFindsTableCellsAndNestedColumns(t *testing.T) {
	app, _, _ := makeApp(t)
	source := objectFrom(t, api(t, app, "POST", "/v1/objects/markdown", map[string]any{"type_id": "note", "title": "Search table", "markdown": "| Header |\n| --- |\n| TableNeedle |"}))
	result := api(t, app, "GET", "/v1/blocks?q=TableNeedle&object_id="+source.ID, nil)
	if result.Code != 200 || !strings.Contains(result.Body.String(), `"type":"table"`) {
		t.Fatal("table cell not searchable: " + result.Body.String())
	}
	nested := []any{map[string]any{"id": "column-parent", "type": "columns", "columns": []any{[]any{map[string]any{"id": "nested-column", "type": "paragraph", "text": "ColumnNeedle", "runs": []any{map[string]any{"text": "ColumnNeedle"}}}}}}}
	source = objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Nested search", "content": "ColumnNeedle", "properties": map[string]any{"blocks": nested}}))
	result = api(t, app, "GET", "/v1/blocks?q=ColumnNeedle&object_id="+source.ID, nil)
	if !strings.Contains(result.Body.String(), `"id":"nested-column"`) {
		t.Fatal("nested column not searchable")
	}
}

func TestReopenedOccurrenceCanBeExcusedWithoutCompletionTimestamp(t *testing.T) {
	app, _, _ := makeApp(t)
	task := createTask(t, app, "Reopened occurrence", map[string]any{"due_date": "2026-10-30", "recurrence": map[string]any{"mode": "daily"}})
	first := complete(t, app, task.ID)
	props := cloneStringMap(first.Task.Properties)
	props["status"] = "todo"
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+task.ID, map[string]any{"properties": props}))
	result := decodeBody[completeTaskResponse](t, api(t, app, "POST", "/v1/tasks/"+task.ID+"/excuse", nil))
	if result.Created || result.Next == nil || result.Next.ID != first.Next.ID || result.Task.Properties["completed_at"] != nil || result.Task.Properties["status"] != "excused" {
		t.Fatalf("reopening forked or retained a completed state: %+v", result)
	}
}
