package main

import (
	"fmt"
	"testing"
	"time"
)

type completeResponse struct {
	Task     Object  `json:"task"`
	Next     *Object `json:"next"`
	NextDate string  `json:"next_date"`
	Created  bool    `json:"created"`
}

func complete(t *testing.T, app *App, id string) completeResponse {
	t.Helper()
	return decodeBody[completeResponse](t, api(t, app, "POST", "/v1/tasks/"+id+"/complete", nil))
}

func createTask(t *testing.T, app *App, title string, properties map[string]any) Object {
	t.Helper()
	return objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "task", "title": title, "properties": properties,
	}))
}

func TestCompleteNonRecurringTaskIsIdempotent(t *testing.T) {
	app, _, _ := makeApp(t)
	task := createTask(t, app, "One-off task", map[string]any{"status": "todo", "due_date": "2026-10-31", "priority": 2})
	response := complete(t, app, task.ID)
	if response.Created || response.Next != nil || response.NextDate != "" {
		t.Fatalf("non-recurring task spawned an occurrence: %+v", response)
	}
	if response.Task.Properties["status"] != "done" || response.Task.Revision != task.Revision+1 {
		t.Fatalf("task was not completed: %+v", response.Task)
	}
	if response.Task.Properties["completed_at"] == nil {
		t.Fatal("completion timestamp was not recorded")
	}
	again := complete(t, app, task.ID)
	if again.Task.Revision != response.Task.Revision || again.Created || again.Next != nil {
		t.Fatalf("second completion changed the task: %+v", again)
	}
	if again.Task.Properties["status"] != "done" {
		t.Fatal("status changed on repeat")
	}
	// A task that was already done before the endpoint existed is also a no-op.
	completed := createTask(t, app, "Already completed", map[string]any{"status": "completed", "due_date": "2026-10-31"})
	unchanged := complete(t, app, completed.ID)
	if unchanged.Task.Revision != completed.Revision || unchanged.Created || unchanged.Next != nil {
		t.Fatalf("already completed task was rewritten: %+v", unchanged)
	}
}

func TestCompleteRejectsNonTaskObjects(t *testing.T) {
	app, _, _ := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Not a task"}))
	if rr := api(t, app, "POST", "/v1/tasks/"+note.ID+"/complete", nil); rr.Code != 400 {
		t.Fatalf("non-task completion: HTTP %d: %s", rr.Code, rr.Body.String())
	}
	if rr := api(t, app, "POST", "/v1/tasks/missing/complete", nil); rr.Code != 404 {
		t.Fatalf("missing task completion: HTTP %d", rr.Code)
	}
}

func TestCompleteRecurringTaskSchedulesEachMode(t *testing.T) {
	cases := []struct {
		name       string
		recurrence map[string]any
		due        string
		want       string
	}{
		{"daily", map[string]any{"mode": "daily"}, "2026-10-30", "2026-10-31"},
		{"weekly weekdays", map[string]any{"mode": "weekly", "weekdays": []int{1}}, "2026-10-30", "2026-11-02"},
		{"monthly clamp", map[string]any{"mode": "monthly"}, "2026-01-31", "2026-02-28"},
		{"every n days", map[string]any{"mode": "every_n_days", "n": 3}, "2026-10-30", "2026-11-02"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			app, _, _ := makeApp(t)
			task := createTask(t, app, "Recurring "+tc.name, map[string]any{"status": "todo", "due_date": tc.due, "recurrence": tc.recurrence})
			response := complete(t, app, task.ID)
			if !response.Created || response.Next == nil {
				t.Fatalf("no next occurrence: %+v", response)
			}
			if response.NextDate != tc.want || response.Next.Properties["due_date"] != tc.want {
				t.Fatalf("next date = %q / %v, want %q", response.NextDate, response.Next.Properties["due_date"], tc.want)
			}
			if response.Task.Properties["due_date"] != tc.due {
				t.Fatalf("completed occurrence changed its own due date: %+v", response.Task.Properties)
			}
		})
	}
}

func TestCompleteRecurringTaskCopiesDefinitionAndLogsOccurrence(t *testing.T) {
	app, _, _ := makeApp(t)
	project := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "project", "title": "Garden"}))
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "home"}))
	// Declare a typed context relation and tags on the task type so the test
	// exercises property_links as well as plain properties.
	properties := []any{
		"status", "due_date", "context_id",
		map[string]any{"key": "context", "type": "object", "target_type": "project"},
		map[string]any{"key": "tags", "type": "object", "target_type": "tag", "multiple": true},
	}
	if rr := api(t, app, "PATCH", "/v1/types/task", map[string]any{"properties": properties}); rr.Code != 200 {
		t.Fatalf("task schema: %s", rr.Body.String())
	}
	task := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "task", "title": "Water plants", "content": "Use the blue can",
		"properties": map[string]any{
			"status": "todo", "priority": 2, "due_date": "2026-10-30", "time": "09:00",
			"context_id": project.ID, "context": project.ID, "tags": []string{tag.ID},
			"recurrence": map[string]any{"mode": "daily"},
			"rich_text":  "<p>Use the blue can</p>",
		},
		"links": []string{project.ID},
	}))
	response := complete(t, app, task.ID)
	next := response.Next
	if next == nil || !response.Created {
		t.Fatalf("missing next occurrence: %+v", response)
	}
	if next.ID == task.ID || next.Revision != 1 || next.TypeID != "task" {
		t.Fatalf("next occurrence identity: %+v", next)
	}
	if next.Title != "Water plants (2026-10-31)" || next.Properties["due_date"] != "2026-10-31" || next.Properties["status"] != "todo" {
		t.Fatalf("next occurrence basics: %+v", next)
	}
	if next.Properties["priority"] != float64(2) || next.Properties["context_id"] != project.ID || next.Properties["context"] != project.ID {
		t.Fatalf("priority/context not copied: %+v", next.Properties)
	}
	if next.Properties["time"] != "09:00" {
		t.Fatalf("scheduled time not copied: %+v", next.Properties)
	}
	if next.Properties["previous_occurrence_id"] != task.ID {
		t.Fatalf("lineage missing: %+v", next.Properties)
	}
	if next.Content != "" || next.Properties["rich_text"] != nil || next.Properties["occurrences"] != nil || next.Properties["completed_at"] != nil {
		t.Fatalf("instance state leaked into the next occurrence: %+v", next)
	}
	recurrence, ok := next.Properties["recurrence"].(map[string]any)
	if !ok || recurrence["mode"] != "daily" {
		t.Fatalf("recurrence not copied: %+v", next.Properties["recurrence"])
	}
	if len(next.Links) != 1 || next.Links[0] != project.ID {
		t.Fatalf("links not copied: %+v", next.Links)
	}
	if len(next.PropertyLinks) != 2 {
		t.Fatalf("property links not recomputed: %+v", next.PropertyLinks)
	}
	tags, ok := next.Properties["tags"].([]any)
	if !ok || len(tags) != 1 || tags[0] != tag.ID {
		t.Fatalf("tags not copied: %+v", next.Properties["tags"])
	}
	completed := response.Task
	if completed.Properties["status"] != "done" || completed.Properties["next_occurrence_id"] != next.ID {
		t.Fatalf("parent not marked done or not linked: %+v", completed.Properties)
	}
	if completed.Content != "Use the blue can" || completed.Properties["rich_text"] != "<p>Use the blue can</p>" {
		t.Fatal("completing the task rewrote its notes")
	}
	entries, ok := completed.Properties["occurrences"].([]any)
	if !ok || len(entries) != 1 {
		t.Fatalf("occurrence log: %+v", completed.Properties["occurrences"])
	}
	entry, _ := entries[0].(map[string]any)
	if entry["scheduled_for"] != "2026-10-30" || entry["next_due"] != "2026-10-31" || entry["next_occurrence_id"] != next.ID || entry["completed_at"] == "" {
		t.Fatalf("occurrence entry: %+v", entry)
	}
	// The next occurrence is itself recurring and can spawn the following one.
	following := complete(t, app, next.ID)
	if following.Next == nil || following.Next.Properties["due_date"] != "2026-11-01" {
		t.Fatalf("chain did not advance: %+v", following)
	}
	if following.Next.Title != "Water plants (2026-11-01)" {
		t.Fatalf("chain title: %q", following.Next.Title)
	}
	if following.Next.Properties["previous_occurrence_id"] != next.ID {
		t.Fatalf("chain lineage: %+v", following.Next.Properties)
	}
}

func TestCompleteRecurringTaskTwiceDoesNotSpawnTwice(t *testing.T) {
	app, _, _ := makeApp(t)
	task := createTask(t, app, "Daily standup", map[string]any{
		"status": "todo", "due_date": "2026-10-30",
		"recurrence": map[string]any{"mode": "daily"},
	})
	first := complete(t, app, task.ID)
	if !first.Created || first.Next == nil {
		t.Fatalf("first completion did not spawn: %+v", first)
	}
	second := complete(t, app, task.ID)
	if second.Created || second.Next == nil || second.Next.ID != first.Next.ID {
		t.Fatalf("second completion spawned another occurrence: %+v", second)
	}
	if second.Task.Revision != first.Task.Revision {
		t.Fatalf("idempotent completion bumped revision: %d -> %d", first.Task.Revision, second.Task.Revision)
	}
	// Reopen and complete again: the occurrence was already processed.
	properties := map[string]any{}
	for key, value := range second.Task.Properties {
		properties[key] = value
	}
	properties["status"] = "todo"
	if rr := api(t, app, "PATCH", "/v1/objects/"+task.ID, map[string]any{"properties": properties}); rr.Code != 200 {
		t.Fatalf("reopen: %s", rr.Body.String())
	}
	third := complete(t, app, task.ID)
	if third.Created || third.Next == nil || third.Next.ID != first.Next.ID {
		t.Fatalf("re-completion spawned another occurrence: %+v", third)
	}
	if third.Task.Properties["status"] != "done" {
		t.Fatal("re-completion did not close the task")
	}
	entries, _ := third.Task.Properties["occurrences"].([]any)
	if len(entries) != 1 {
		t.Fatalf("occurrence log duplicated: %+v", entries)
	}
	objects, err := app.store.Objects()
	if err != nil {
		t.Fatal(err)
	}
	children := 0
	for _, candidate := range objects {
		if candidate.Properties["previous_occurrence_id"] == task.ID {
			children++
		}
	}
	if children != 1 {
		t.Fatalf("found %d next occurrences for one completion", children)
	}
}

func TestCompleteRecurringTaskKeepsGuardWhenNextOccurrenceWasDeleted(t *testing.T) {
	app, _, _ := makeApp(t)
	task := createTask(t, app, "Guard after delete", map[string]any{
		"status": "todo", "due_date": "2026-10-30",
		"recurrence": map[string]any{"mode": "daily"},
	})
	first := complete(t, app, task.ID)
	if rr := api(t, app, "DELETE", "/v1/objects/"+first.Next.ID, nil); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	properties := map[string]any{}
	for key, value := range first.Task.Properties {
		properties[key] = value
	}
	properties["status"] = "todo"
	if rr := api(t, app, "PATCH", "/v1/objects/"+task.ID, map[string]any{"properties": properties}); rr.Code != 200 {
		t.Fatalf("reopen: %s", rr.Body.String())
	}
	second := complete(t, app, task.ID)
	if second.Created || second.Next != nil {
		t.Fatalf("deleted next occurrence was replaced and forked the series: %+v", second)
	}
	if second.Task.Properties["status"] != "done" {
		t.Fatal("reopened parent was not closed")
	}
	tasks, err := app.store.ObjectsByType("task")
	if err != nil || len(tasks) != 1 {
		t.Fatalf("expected only the completed parent, got %d tasks (%v)", len(tasks), err)
	}
	entries, _ := second.Task.Properties["occurrences"].([]any)
	if len(entries) != 1 {
		t.Fatalf("occurrence log changed on the guarded completion: %+v", entries)
	}
}

func TestCompleteOverdueRecurringTaskAdvancesFromDueDate(t *testing.T) {
	app, _, _ := makeApp(t)
	task := createTask(t, app, "Overdue daily", map[string]any{
		"status": "todo", "due_date": "2020-01-01",
		"recurrence": map[string]any{"mode": "daily"},
	})
	response := complete(t, app, task.ID)
	if response.Next == nil || response.Next.Properties["due_date"] != "2020-01-02" {
		t.Fatalf("catch-up used completion time instead of the due date: %+v", response)
	}
}

func TestCompleteYesterdayTaskSchedulesTodayFromYesterday(t *testing.T) {
	app, _, _ := makeApp(t)
	yesterday := time.Now().AddDate(0, 0, -1).Format("2006-01-02")
	today := time.Now().Format("2006-01-02")
	task := createTask(t, app, "Yesterday daily", map[string]any{
		"status": "todo", "due_date": yesterday,
		"recurrence": map[string]any{"mode": "daily"},
	})
	response := complete(t, app, task.ID)
	if response.Next == nil || response.Next.Properties["due_date"] != today {
		t.Fatalf("next occurrence should be today (yesterday + 1), got %+v", response.Next)
	}
}

func TestCompleteMonthlyTaskClampsAndDocumentsDrift(t *testing.T) {
	app, _, _ := makeApp(t)
	task := createTask(t, app, "Monthly budget", map[string]any{
		"status": "todo", "due_date": "2026-01-31",
		"recurrence": map[string]any{"mode": "monthly"},
	})
	first := complete(t, app, task.ID)
	if first.Next == nil || first.Next.Properties["due_date"] != "2026-02-28" {
		t.Fatalf("month-end clamp failed: %+v", first)
	}
	// Clamping is not sticky: the next calculation starts from the clamped
	// date, so the series continues on the 28th rather than jumping back to 31.
	second := complete(t, app, first.Next.ID)
	if second.Next == nil || second.Next.Properties["due_date"] != "2026-03-28" {
		t.Fatalf("unexpected post-clamp date: %+v", second)
	}
}

func TestRecurringTaskRequiresDueDate(t *testing.T) {
	app, _, _ := makeApp(t)
	rr := api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "task", "title": "Undated recurring",
		"properties": map[string]any{"status": "todo", "recurrence": map[string]any{"mode": "daily"}},
	})
	if rr.Code != 400 {
		t.Fatalf("undated recurring task accepted: HTTP %d: %s", rr.Code, rr.Body.String())
	}
	task := createTask(t, app, "Dated recurring", map[string]any{
		"status": "todo", "due_date": "2026-10-31",
		"recurrence": map[string]any{"mode": "daily"},
	})
	// A PATCH replaces the whole property document; dropping due_date while
	// keeping recurrence is rejected instead of leaving an uncompletable task.
	if rr := api(t, app, "PATCH", "/v1/objects/"+task.ID, map[string]any{
		"properties": map[string]any{"status": "todo", "recurrence": map[string]any{"mode": "daily"}},
	}); rr.Code != 400 {
		t.Fatalf("PATCH without due_date accepted: %s", rr.Body.String())
	}
}

func TestRecurrenceValidationOnTaskWrites(t *testing.T) {
	app, _, _ := makeApp(t)
	base := func() map[string]any {
		return map[string]any{"status": "todo", "due_date": "2026-10-31"}
	}
	invalid := map[string]any{
		"missing mode":        map[string]any{},
		"unknown mode":        map[string]any{"mode": "unsupported"},
		"uppercase mode":      map[string]any{"mode": "Daily"},
		"zero interval":       map[string]any{"mode": "every_n_days", "n": 0},
		"negative interval":   map[string]any{"mode": "every_n_days", "n": -3},
		"fractional interval": map[string]any{"mode": "every_n_days", "n": 1.5},
		"interval on daily":   map[string]any{"mode": "daily", "n": 2},
		"weekday zero":        map[string]any{"mode": "weekly", "weekdays": []int{0}},
		"weekday eight":       map[string]any{"mode": "weekly", "weekdays": []int{8}},
		"empty weekdays":      map[string]any{"mode": "weekly", "weekdays": []int{}},
		"duplicate weekday":   map[string]any{"mode": "weekly", "weekdays": []int{2, 2}},
		"weekdays on monthly": map[string]any{"mode": "monthly", "weekdays": []int{1}},
		"unknown field":       map[string]any{"mode": "daily", "interval": 2},
		"not an object":       "daily",
	}
	for name, recurrence := range invalid {
		properties := base()
		properties["recurrence"] = recurrence
		rr := api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "task", "title": "Invalid " + name, "properties": properties})
		if rr.Code != 400 {
			t.Fatalf("%s accepted: HTTP %d: %s", name, rr.Code, rr.Body.String())
		}
	}
	valid := []map[string]any{
		{"mode": "daily"},
		{"mode": "weekly"},
		{"mode": "weekly", "weekdays": []int{7, 1, 3}},
		{"mode": "monthly"},
		{"mode": "every_n_days", "n": 1},
	}
	for i, recurrence := range valid {
		properties := base()
		properties["recurrence"] = recurrence
		created := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "task", "title": fmt.Sprintf("Valid %d", i), "properties": properties}))
		if i == 2 {
			weekdays, ok := created.Properties["recurrence"].(map[string]any)["weekdays"].([]any)
			if !ok || len(weekdays) != 3 || weekdays[0] != float64(1) || weekdays[1] != float64(3) || weekdays[2] != float64(7) {
				t.Fatalf("weekdays were not normalized: %+v", created.Properties["recurrence"])
			}
		}
	}
	// A null recurrence clears it and keeps the task valid.
	properties := base()
	properties["recurrence"] = nil
	cleared := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "task", "title": "Null recurrence", "properties": properties}))
	if cleared.Properties["recurrence"] != nil {
		t.Fatalf("null recurrence was stored: %+v", cleared.Properties)
	}
	// Recurrence is a reserved task property; a note cannot silently carry a
	// rule the engine would never run.
	if rr := api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Note with recurrence",
		"properties": map[string]any{"recurrence": map[string]any{"mode": "daily"}},
	}); rr.Code != 400 {
		t.Fatalf("recurrence on a note accepted: %s", rr.Body.String())
	}
	// PATCH validates the same rules.
	task := createTask(t, app, "Patch target", map[string]any{"status": "todo", "due_date": "2026-10-31"})
	bad := map[string]any{"status": "todo", "due_date": "2026-10-31", "recurrence": map[string]any{"mode": "weekly", "weekdays": []int{9}}}
	if rr := api(t, app, "PATCH", "/v1/objects/"+task.ID, map[string]any{"properties": bad}); rr.Code != 400 {
		t.Fatalf("PATCH accepted an invalid recurrence: %s", rr.Body.String())
	}
}

func TestRecurrencePatchNoOpDoesNotBumpRevision(t *testing.T) {
	app, _, _ := makeApp(t)
	task := createTask(t, app, "Stable recurrence", map[string]any{
		"status": "todo", "due_date": "2026-10-31",
		"recurrence": map[string]any{"mode": "every_n_days", "n": 3},
	})
	same := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+task.ID, map[string]any{"properties": task.Properties}))
	if same.Revision != task.Revision {
		t.Fatalf("canonical recurrence caused a false revision bump: %d -> %d", task.Revision, same.Revision)
	}
	weekly := createTask(t, app, "Stable weekdays", map[string]any{
		"status": "todo", "due_date": "2026-10-31",
		"recurrence": map[string]any{"mode": "weekly", "weekdays": []int{5, 1}},
	})
	same = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+weekly.ID, map[string]any{"properties": weekly.Properties}))
	if same.Revision != weekly.Revision {
		t.Fatalf("sorted weekdays caused a false revision bump: %d -> %d", weekly.Revision, same.Revision)
	}
}

func TestNextOccurrenceTitleStaysUnique(t *testing.T) {
	app, _, _ := makeApp(t)
	// Occupy the computed title with an unrelated object.
	objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Water plants (2026-10-31)"}))
	task := createTask(t, app, "Water plants", map[string]any{
		"status": "todo", "due_date": "2026-10-30",
		"recurrence": map[string]any{"mode": "daily"},
	})
	response := complete(t, app, task.ID)
	if response.Next == nil || response.Next.Title != "Water plants (2026-10-31 #2)" {
		t.Fatalf("collision not resolved: %+v", response.Next)
	}
	// The discriminator is stripped again, so the following occurrence returns
	// to the plain date suffix instead of accumulating counters.
	following := complete(t, app, response.Next.ID)
	if following.Next == nil || following.Next.Title != "Water plants (2026-11-01)" {
		t.Fatalf("series title drifted: %+v", following.Next)
	}
}

func TestCompleteTaskSyncJournalIncludesBothOccurrences(t *testing.T) {
	app, _, _ := makeApp(t)
	task := createTask(t, app, "Synced recurring", map[string]any{
		"status": "todo", "due_date": "2026-10-30",
		"recurrence": map[string]any{"mode": "daily"},
	})
	head, err := app.store.HeadSeq()
	if err != nil {
		t.Fatal(err)
	}
	response := complete(t, app, task.ID)
	if response.Next == nil {
		t.Fatal("no next occurrence")
	}
	pull := decodeBody[syncPullResponse](t, api(t, app, "POST", "/v1/sync/pull", map[string]any{"since_seq": head}))
	seen := map[string]bool{}
	for _, object := range pull.Objects {
		seen[object.ID] = true
	}
	if !seen[task.ID] || !seen[response.Next.ID] {
		t.Fatalf("sync journal missed the completed task or its successor: %+v", pull.Objects)
	}
	if pull.HeadSeq <= head {
		t.Fatalf("sync head did not advance: %d -> %d", head, pull.HeadSeq)
	}
}

func TestSyncPushValidatesRecurrence(t *testing.T) {
	app, _, _ := makeApp(t)
	stamp := now()
	push := func(opID, id string, recurrence map[string]any) syncPushResponse {
		return decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{
			"device_id": "pixel-recurrence-test", "base_seq": 0,
			"operations": []any{map[string]any{
				"op_id": opID, "action": "put", "base_revision": 0,
				"object": map[string]any{
					"id": id, "type_id": "task", "title": "Synced " + id,
					"content": "", "properties": map[string]any{"due_date": "2026-10-31", "recurrence": recurrence},
					"links": []string{}, "created_at": stamp, "updated_at": stamp,
				},
			}},
		}))
	}
	invalid := push("op-invalid-recurrence-1", newID(), map[string]any{"mode": "unsupported"})
	if len(invalid.Errors) != 1 || len(invalid.Applied) != 0 {
		t.Fatalf("invalid recurrence was accepted by sync push: %+v", invalid)
	}
	id := newID()
	valid := push("op-valid-recurrence-1", id, map[string]any{"mode": "every_n_days", "n": 2})
	if len(valid.Errors) != 0 || len(valid.Applied) != 1 {
		t.Fatalf("valid recurrence was rejected by sync push: %+v", valid)
	}
	stored, err := app.store.Object(id)
	if err != nil {
		t.Fatal(err)
	}
	rule, ok := stored.Properties["recurrence"].(map[string]any)
	if !ok || rule["mode"] != "every_n_days" || rule["n"] != float64(2) {
		t.Fatalf("synced recurrence was not stored: %+v", stored.Properties["recurrence"])
	}
}
