package main

import (
	"net/http"
	"strings"
)

// completeTaskResponse is the body of POST /v1/tasks/{id}/complete. `next` is
// the spawned (or previously spawned) occurrence and `created` tells the client
// whether this call created it.
type completeTaskResponse struct {
	Task     Object  `json:"task"`
	Next     *Object `json:"next"`
	NextDate string  `json:"next_date,omitempty"`
	Created  bool    `json:"created"`
}

// completeTask marks one task occurrence as done and, when the task carries a
// recurrence, creates the next occurrence from the task's due date. The
// completed task, its occurrence-log entry and the new object commit in one
// transaction, so a retried request can never observe a half-completed series.
func (a *App) completeTask(w http.ResponseWriter, r *http.Request) {
	a.resolveTask(w, r, "completed")
}

func (a *App) skipTask(w http.ResponseWriter, r *http.Request)   { a.resolveTask(w, r, "skipped") }
func (a *App) excuseTask(w http.ResponseWriter, r *http.Request) { a.resolveTask(w, r, "excused") }

func (a *App) resolveTask(w http.ResponseWriter, r *http.Request, outcome string) {
	a.writeMu.Lock()
	defer a.writeMu.Unlock()

	var task Object
	if err := a.store.Get("object", r.PathValue("id"), &task); err != nil {
		jsonError(w, 404, "Task not found")
		return
	}
	if task.TypeID != "task" {
		jsonError(w, 400, "Only task objects can be completed")
		return
	}
	if task.Properties == nil {
		task.Properties = map[string]any{}
	}

	// An already completed task is never processed twice. This is what makes a
	// retried request, a double click or a late mobile replay safe. The response
	// still resolves the next occurrence recorded by the first call.
	if status, _ := task.Properties["status"].(string); taskIsDone(task) || status == "skipped" || status == "excused" {
		next := a.recordedNextOccurrence(task)
		jsonResponse(w, 200, completeTaskResponse{Task: task, Next: next, NextDate: nextDateOf(next), Created: false})
		return
	}

	// A task that was completed, reopened and completed again already spawned
	// its successor. The pointer itself is the guard: even if that child was
	// deleted, this completion must not fork the series. To start a new chain,
	// the client creates a new task or removes the recurrence first.
	if nextID, _ := task.Properties["next_occurrence_id"].(string); nextID != "" {
		task.Properties["status"] = "done"
		if outcome != "completed" {
			task.Properties["status"] = outcome
		}
		task.Properties["occurrence_outcome"] = outcome
		task.Properties["completed_at"] = now()
		task.Properties["resolved_at"] = now()
		if outcome != "completed" {
			delete(task.Properties, "completed_at")
		}
		task.UpdatedAt = now()
		if err := a.validateObject(&task); err != nil {
			jsonError(w, 400, err.Error())
			return
		}
		if err := a.saveObject(r.Context(), &task); err != nil {
			jsonError(w, 500, "Could not persist completed task")
			return
		}
		next := a.recordedNextOccurrence(task)
		jsonResponse(w, 200, completeTaskResponse{Task: task, Next: next, NextDate: nextDateOf(next), Created: false})
		return
	}

	rule, recurring, err := recurrenceFromProperties(task.Properties)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}

	if !recurring && outcome != "completed" {
		jsonError(w, 400, "Only recurring tasks can be skipped or excused")
		return
	}

	var child *Object
	nextDate := ""
	if recurring {
		dueDate, _ := task.Properties["due_date"].(string)
		if strings.TrimSpace(dueDate) == "" {
			jsonError(w, 400, "A recurring task needs a due_date to schedule its next occurrence")
			return
		}
		nextDate, err = rule.NextDate(dueDate)
		if err != nil {
			jsonError(w, 400, err.Error())
			return
		}
		child = a.nextOccurrence(&task, nextDate)
		if err := a.validateObject(child); err != nil {
			jsonError(w, 400, err.Error())
			return
		}
	}

	completedAt := now()
	task.Properties["status"] = "done"
	task.Properties["completed_at"] = completedAt
	task.Properties["occurrence_outcome"] = outcome
	task.Properties["resolved_at"] = completedAt
	if outcome != "completed" {
		task.Properties["status"] = outcome
		delete(task.Properties, "completed_at")
	}
	if child != nil {
		dueDate, _ := task.Properties["due_date"].(string)
		entries, err := appendOccurrence(task.Properties, map[string]any{
			"completed_at":       task.Properties["completed_at"],
			"outcome":            outcome,
			"resolved_at":        completedAt,
			"scheduled_for":      dueDate,
			"next_due":           nextDate,
			"next_occurrence_id": child.ID,
		})
		if err != nil {
			jsonError(w, 400, err.Error())
			return
		}
		task.Properties["occurrences"] = entries
		task.Properties["next_occurrence_id"] = child.ID
	}
	task.UpdatedAt = now()
	if err := a.validateObject(&task); err != nil {
		jsonError(w, 400, err.Error())
		return
	}

	records := a.prepareSave(&task)
	if child != nil {
		records = append(records, a.prepareSave(child)...)
	}
	if err := a.store.PutBatch(records, nil); err != nil {
		jsonError(w, 500, "Could not persist completed task")
		return
	}
	jsonResponse(w, 200, completeTaskResponse{Task: task, Next: child, NextDate: nextDate, Created: child != nil})
}

// recordedNextOccurrence resolves the next occurrence remembered on the task.
// It returns nil when the pointer is absent or the child is gone.
func (a *App) recordedNextOccurrence(task Object) *Object {
	id, _ := task.Properties["next_occurrence_id"].(string)
	if id == "" {
		return nil
	}
	next, err := a.store.Object(id)
	if err != nil {
		return nil
	}
	return &next
}

func nextDateOf(next *Object) string {
	if next == nil {
		return ""
	}
	date, _ := next.Properties["due_date"].(string)
	return date
}
