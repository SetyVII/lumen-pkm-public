package main

// User journey: a recurring chore. Create a task linked to a project, complete
// it, get the next weekday occurrence, query the calendar window, complete the
// next one, and verify the overdue catch-up rule. This combines recurring
// tasks, date-range filters and the existing relation model.

import (
	"testing"
	"time"
)

func TestScenarioRecurringTaskJourney(t *testing.T) {
	app, _, _ := makeApp(t)

	project := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "project", "title": "Garden renovation",
	}))

	// Step 1: a weekly task on Mondays and Wednesdays, due Monday 2026-10-26.
	task := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "task", "title": "Water plants", "content": "Use the blue can",
		"properties": map[string]any{
			"status":     "todo",
			"priority":   2,
			"due_date":   "2026-10-26",
			"context_id": project.ID,
			"recurrence": map[string]any{"mode": "weekly", "weekdays": []int{1, 3}},
		},
		"links": []string{project.ID},
	}))
	if task.Properties["due_date"] != "2026-10-26" {
		t.Fatalf("task due date = %v", task.Properties["due_date"])
	}

	// Step 2: complete it. The next occurrence is Wednesday, not tomorrow.
	first := complete(t, app, task.ID)
	if !first.Created || first.Next == nil {
		t.Fatalf("completion did not spawn the next occurrence: %+v", first)
	}
	if first.NextDate != "2026-10-28" || first.Next.Properties["due_date"] != "2026-10-28" {
		t.Fatalf("next weekday = %q / %v, want 2026-10-28", first.NextDate, first.Next.Properties["due_date"])
	}
	if first.Task.Properties["status"] != "done" {
		t.Fatalf("completed task status = %v", first.Task.Properties["status"])
	}
	if first.Task.Properties["due_date"] != "2026-10-26" {
		t.Fatalf("completion changed the occurrence's own due date: %v", first.Task.Properties["due_date"])
	}
	occurrences, ok := first.Task.Properties["occurrences"].([]any)
	if !ok || len(occurrences) != 1 {
		t.Fatalf("occurrence log = %#v", first.Task.Properties["occurrences"])
	}
	entry := occurrences[0].(map[string]any)
	if entry["scheduled_for"] != "2026-10-26" || entry["next_due"] != "2026-10-28" || entry["next_occurrence_id"] != first.Next.ID {
		t.Fatalf("occurrence entry is wrong: %#v", entry)
	}

	// Step 3: the next occurrence copies the definition (context, links,
	// recurrence, priority) and resets instance state.
	next := first.Next
	if next.Title != "Water plants (2026-10-28)" {
		t.Fatalf("next occurrence title = %q", next.Title)
	}
	if next.Properties["status"] != "todo" || next.Properties["context_id"] != project.ID {
		t.Fatalf("next occurrence did not copy the definition: %#v", next.Properties)
	}
	if next.Properties["previous_occurrence_id"] != task.ID {
		t.Fatalf("next occurrence does not point back: %#v", next.Properties)
	}
	if !scenarioContainsID(next.Links, project.ID) {
		t.Fatalf("next occurrence lost the project link: %v", next.Links)
	}
	recurrence, ok := next.Properties["recurrence"].(map[string]any)
	if !ok || recurrence["mode"] != "weekly" {
		t.Fatalf("next occurrence lost its recurrence: %#v", next.Properties["recurrence"])
	}
	if next.Content != "" || next.Properties["blocks"] != nil || next.Properties["completed_at"] != nil {
		t.Fatalf("next occurrence inherited instance state: %+v", next)
	}

	// Step 4: the calendar window query finds the next occurrence, not the
	// completed one.
	window := queryObjects(t, app, "type_id=task&props.due_date.from=2026-10-27&props.due_date.to=2026-10-29")
	if !scenarioContainsID(objectIDs(window), next.ID) || scenarioContainsID(objectIDs(window), task.ID) {
		t.Fatalf("date-range query = %v, want only the next occurrence", objectIDs(window))
	}
	completedWindow := queryObjects(t, app, "type_id=task&props.due_date.from=2026-10-26&props.due_date.to=2026-10-26")
	if !scenarioContainsID(objectIDs(completedWindow), task.ID) {
		t.Fatalf("due-date query missed the completed occurrence: %v", objectIDs(completedWindow))
	}

	// Step 5: complete the Wednesday occurrence; the next one is Monday.
	second := complete(t, app, next.ID)
	if !second.Created || second.NextDate != "2026-11-02" {
		t.Fatalf("second completion next date = %q: %+v", second.NextDate, second)
	}
	if second.Next.Properties["previous_occurrence_id"] != next.ID {
		t.Fatalf("chain lineage is broken: %#v", second.Next.Properties)
	}

	// Step 6: overdue catch-up. A daily task due three days ago advances one
	// day from its due date, not from the completion time.
	overdueDue := time.Now().AddDate(0, 0, -3).Format("2006-01-02")
	overdue := createTask(t, app, "Overdue daily chore", map[string]any{
		"status": "todo", "due_date": overdueDue, "recurrence": map[string]any{"mode": "daily"},
	})
	late := complete(t, app, overdue.ID)
	wantLate := time.Now().AddDate(0, 0, -2).Format("2006-01-02")
	if late.NextDate != wantLate {
		t.Fatalf("overdue next date = %q, want %q (due + 1 day)", late.NextDate, wantLate)
	}
	if late.NextDate == time.Now().Format("2006-01-02") {
		t.Fatal("overdue completion jumped to the completion date instead of keeping the cadence")
	}
	lateEntries := late.Task.Properties["occurrences"].([]any)
	lateEntry := lateEntries[0].(map[string]any)
	if lateEntry["scheduled_for"] != overdueDue || lateEntry["next_due"] != wantLate {
		t.Fatalf("overdue occurrence log = %#v", lateEntry)
	}

	// Step 7: completing the same task again is a no-op, not a fork.
	again := complete(t, app, overdue.ID)
	if again.Created || again.Next == nil || again.Next.ID != late.Next.ID {
		t.Fatalf("repeat completion forked the series: %+v", again)
	}
}
