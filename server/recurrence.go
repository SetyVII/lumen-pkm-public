package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"regexp"
	"sort"
	"strings"
	"time"
	"unicode/utf8"
)

// Recurrence modes supported by the task recurrence engine. They cover the
// fixed rhythms of Capacities' recurring tasks that this parity stage scoped:
// daily, weekly (optionally pinned to weekdays), monthly (calendar day) and a
// plain every-N-days interval.
const (
	recurrenceDaily       = "daily"
	recurrenceWeekly      = "weekly"
	recurrenceMonthly     = "monthly"
	recurrenceEveryNDays  = "every_n_days"
	recurrenceYearly      = "yearly"
	recurrenceWeekdays    = "weekdays"
	recurrenceLastDay     = "last_day"
	recurrenceLastWeekday = "last_weekday"

	// maxRecurrenceDays bounds every_n_days so a typo cannot schedule an
	// occurrence a decade away.
	maxRecurrenceDays = 3660

	// maxOccurrenceLog keeps the completed task's record bounded. A daily task
	// completed for ten years would otherwise grow the encrypted record without
	// limit; the oldest entries are pruned first.
	maxOccurrenceLog = 1000
)

// recurrenceRule is the validated shape of properties.recurrence. The JSON
// tags are part of the public contract documented in contracts/SYNC.md.
type recurrenceRule struct {
	Mode     string `json:"mode"`
	N        int    `json:"n,omitempty"`
	Weekdays []int  `json:"weekdays,omitempty"`
}

// recurrenceFromProperties reads the optional recurrence property. Absent or
// null means a normal, non-recurring task, which keeps every pre-existing task
// valid.
func recurrenceFromProperties(properties map[string]any) (recurrenceRule, bool, error) {
	raw, exists := properties["recurrence"]
	if !exists || raw == nil {
		return recurrenceRule{}, false, nil
	}
	rule, err := parseRecurrence(raw)
	if err != nil {
		return recurrenceRule{}, false, err
	}
	return rule, true, nil
}

// parseRecurrence validates the property strictly: lowercase modes, n only for
// every_n_days, weekdays only for weekly and always in 1..7 without repeats.
// Unknown fields are rejected so a typo cannot silently change the schedule.
func parseRecurrence(raw any) (recurrenceRule, error) {
	object, ok := raw.(map[string]any)
	if !ok {
		return recurrenceRule{}, errors.New("Property recurrence must be an object with mode, optional n and optional weekdays")
	}
	mode, _ := object["mode"].(string)
	switch mode {
	case recurrenceDaily, recurrenceWeekly, recurrenceMonthly, recurrenceEveryNDays,
		recurrenceYearly, recurrenceWeekdays, recurrenceLastDay, recurrenceLastWeekday:
	default:
		return recurrenceRule{}, errors.New("Property recurrence.mode must be daily, weekly, monthly, every_n_days, yearly, weekdays, last_day or last_weekday")
	}
	rule := recurrenceRule{Mode: mode}
	for key := range object {
		switch key {
		case "mode":
		case "n":
			if mode != recurrenceEveryNDays {
				return recurrenceRule{}, errors.New("Property recurrence.n is only valid for every_n_days")
			}
		case "weekdays":
			if mode != recurrenceWeekly {
				return recurrenceRule{}, errors.New("Property recurrence.weekdays is only valid for weekly")
			}
		default:
			return recurrenceRule{}, fmt.Errorf("Property recurrence has an unknown field %q", key)
		}
	}
	if mode == recurrenceEveryNDays {
		n, ok := integerValue(object["n"])
		if !ok || n < 1 || n > maxRecurrenceDays {
			return recurrenceRule{}, fmt.Errorf("Property recurrence.n must be an integer between 1 and %d", maxRecurrenceDays)
		}
		rule.N = n
	}
	if mode == recurrenceWeekly {
		if rawWeekdays, exists := object["weekdays"]; exists {
			list, ok := weekdayValues(rawWeekdays)
			if !ok || len(list) == 0 {
				return recurrenceRule{}, errors.New("Property recurrence.weekdays must be a nonempty list of ISO weekdays 1 (Monday) through 7 (Sunday)")
			}
			seen := make(map[int]bool, len(list))
			for _, entry := range list {
				day, ok := integerValue(entry)
				if !ok || day < 1 || day > 7 {
					return recurrenceRule{}, errors.New("Property recurrence.weekdays must use 1 (Monday) through 7 (Sunday)")
				}
				if seen[day] {
					return recurrenceRule{}, errors.New("Property recurrence.weekdays must not repeat a weekday")
				}
				seen[day] = true
				rule.Weekdays = append(rule.Weekdays, day)
			}
			sort.Ints(rule.Weekdays)
		}
	}
	return rule, nil
}

// validate re-checks a rule built by internal callers. It mirrors parseRecurrence
// so a hand-built recurrenceRule cannot bypass the contract.
func (r recurrenceRule) validate() error {
	switch r.Mode {
	case recurrenceDaily, recurrenceMonthly, recurrenceYearly, recurrenceWeekdays, recurrenceLastDay, recurrenceLastWeekday:
		if r.N != 0 {
			return errors.New("Property recurrence.n is only valid for every_n_days")
		}
		if len(r.Weekdays) != 0 {
			return errors.New("Property recurrence.weekdays is only valid for weekly")
		}
	case recurrenceWeekly:
		if r.N != 0 {
			return errors.New("Property recurrence.n is only valid for every_n_days")
		}
		if len(r.Weekdays) > 7 {
			return errors.New("Property recurrence.weekdays must use 1 (Monday) through 7 (Sunday)")
		}
		seen := make(map[int]bool, len(r.Weekdays))
		for _, day := range r.Weekdays {
			if day < 1 || day > 7 {
				return errors.New("Property recurrence.weekdays must use 1 (Monday) through 7 (Sunday)")
			}
			if seen[day] {
				return errors.New("Property recurrence.weekdays must not repeat a weekday")
			}
			seen[day] = true
		}
	case recurrenceEveryNDays:
		if r.N < 1 || r.N > maxRecurrenceDays {
			return fmt.Errorf("Property recurrence.n must be an integer between 1 and %d", maxRecurrenceDays)
		}
		if len(r.Weekdays) != 0 {
			return errors.New("Property recurrence.weekdays is only valid for weekly")
		}
	default:
		return errors.New("Property recurrence.mode must be daily, weekly, monthly, every_n_days, yearly, weekdays, last_day or last_weekday")
	}
	return nil
}

// NextDate returns the due date of the next occurrence. It is pure calendar
// math on the current due date, never on the completion time: a late
// completion does not shift the series (catch-up policy).
func (r recurrenceRule) NextDate(due string) (string, error) {
	if err := r.validate(); err != nil {
		return "", err
	}
	date, err := time.Parse("2006-01-02", due)
	if err != nil || date.Format("2006-01-02") != due {
		return "", errors.New("Property due_date must be YYYY-MM-DD to schedule the next occurrence")
	}
	var next time.Time
	switch r.Mode {
	case recurrenceDaily:
		next = date.AddDate(0, 0, 1)
	case recurrenceEveryNDays:
		next = date.AddDate(0, 0, r.N)
	case recurrenceWeekly:
		if len(r.Weekdays) == 0 {
			next = date.AddDate(0, 0, 7)
		} else {
			next = nextWeekdayAfter(date, r.Weekdays)
		}
	case recurrenceMonthly:
		next = nextMonthSameDay(date)
	case recurrenceYearly:
		next = time.Date(date.Year()+1, date.Month(), 1, 0, 0, 0, 0, time.UTC)
		last := time.Date(next.Year(), next.Month()+1, 0, 0, 0, 0, 0, time.UTC).Day()
		next = next.AddDate(0, 0, min(date.Day(), last)-1)
	case recurrenceWeekdays:
		next = nextWeekdayAfter(date, []int{1, 2, 3, 4, 5})
	case recurrenceLastDay, recurrenceLastWeekday:
		next = time.Date(date.Year(), date.Month()+1, 0, 0, 0, 0, 0, time.UTC)
		if r.Mode == recurrenceLastWeekday {
			for isoWeekday(next) > 5 {
				next = next.AddDate(0, 0, -1)
			}
		}
		if !next.After(date) {
			next = time.Date(date.Year(), date.Month()+2, 0, 0, 0, 0, 0, time.UTC)
			if r.Mode == recurrenceLastWeekday {
				for isoWeekday(next) > 5 {
					next = next.AddDate(0, 0, -1)
				}
			}
		}
	default:
		return "", errors.New("Property recurrence.mode must be daily, weekly, monthly, every_n_days, yearly, weekdays, last_day or last_weekday")
	}
	return next.Format("2006-01-02"), nil
}

// isoWeekday maps Go's Sunday=0..Saturday=6 to the contract's Monday=1..Sunday=7.
func isoWeekday(date time.Time) int {
	if date.Weekday() == time.Sunday {
		return 7
	}
	return int(date.Weekday())
}

// nextWeekdayAfter returns the first listed weekday strictly after date. A
// weekday equal to the current one therefore advances a full week.
func nextWeekdayAfter(date time.Time, weekdays []int) time.Time {
	current := isoWeekday(date)
	delta := 8
	for _, weekday := range weekdays {
		candidate := weekday - current
		if candidate <= 0 {
			candidate += 7
		}
		if candidate < delta {
			delta = candidate
		}
	}
	return date.AddDate(0, 0, delta)
}

// nextMonthSameDay advances one calendar month and clamps to the last day of
// the target month (Jan 31 -> Feb 28/29). It never normalizes into the
// following month the way time.AddDate does.
func nextMonthSameDay(date time.Time) time.Time {
	year, month, day := date.Date()
	month++
	if month > time.December {
		month = time.January
		year++
	}
	lastDay := time.Date(year, month+1, 0, 0, 0, 0, 0, time.UTC).Day()
	if day > lastDay {
		day = lastDay
	}
	return time.Date(year, month, day, 0, 0, 0, 0, time.UTC)
}

// taskIsDone mirrors the desktop's status semantics: both "done" and
// "completed" close a task, case-insensitively.
func taskIsDone(task Object) bool {
	status, _ := task.Properties["status"].(string)
	switch strings.ToLower(strings.TrimSpace(status)) {
	case "done", "completed":
		return true
	}
	return false
}

var occurrenceTitleSuffix = regexp.MustCompile(`\s+\(\d{4}-\d{2}-\d{2}(?: #\d+)?\)\s*$`)

// occurrenceTitle builds a readable title for the next occurrence. Lumen keeps
// titles unique across the workspace, so the occurrence date is appended and
// any previous occurrence suffix is stripped first to keep the series stable.
func occurrenceTitle(parentTitle, date string) string {
	return occurrenceTitleWithDiscriminator(parentTitle, date, 0)
}

func occurrenceTitleWithDiscriminator(parentTitle, date string, counter int) string {
	base := strings.TrimSpace(occurrenceTitleSuffix.ReplaceAllString(strings.TrimSpace(parentTitle), ""))
	if base == "" {
		base = "Task"
	}
	suffix := " (" + date
	if counter > 1 {
		suffix += fmt.Sprintf(" #%d", counter)
	}
	suffix += ")"
	return truncateTitle(base, suffix)
}

func truncateTitle(base, suffix string) string {
	// Object titles are capped at 1000 bytes by validateObject, so truncate on
	// a rune boundary instead of counting runes.
	limit := 1000 - len(suffix)
	if len(base) > limit {
		cut := limit
		for cut > 0 && !utf8.RuneStart(base[cut]) {
			cut--
		}
		base = base[:cut]
	}
	return strings.TrimSpace(base) + suffix
}

// uniqueOccurrenceTitle adds a numeric discriminator only when the computed
// title is already taken by an unrelated object.
func (a *App) uniqueOccurrenceTitle(parentTitle, date string) string {
	title := occurrenceTitle(parentTitle, date)
	for counter := 2; a.store.TitleExists(title); counter++ {
		title = occurrenceTitleWithDiscriminator(parentTitle, date, counter)
	}
	return title
}

// instanceProperties hold per-occurrence state and are not copied to the next
// occurrence. Content representations and attachments stay with the completed
// occurrence, matching Capacities' "notes remain in the task" behavior.
var instanceProperties = map[string]bool{
	"status":                 true,
	"due_date":               true,
	"completed_at":           true,
	"occurrence_outcome":     true,
	"resolved_at":            true,
	"occurrences":            true,
	"next_occurrence_id":     true,
	"previous_occurrence_id": true,
	"blocks":                 true,
	"rich_text":              true,
	"assets":                 true,
	"image_asset_id":         true,
	"asset_id":               true,
	"content_checksum":       true,
	"mobile_conflict":        true,
}

// nextOccurrence builds the new task for the following date. It copies the
// task definition (title, priority, context, tags, custom properties and the
// recurrence itself) but starts with clean instance state.
func (a *App) nextOccurrence(parent *Object, nextDate string) *Object {
	properties := make(map[string]any, len(parent.Properties)+3)
	for key, value := range parent.Properties {
		if instanceProperties[key] {
			continue
		}
		properties[key] = cloneJSONValue(value)
	}
	properties["status"] = "todo"
	properties["due_date"] = nextDate
	properties["previous_occurrence_id"] = parent.ID
	links := make([]string, 0, len(parent.Links))
	for _, id := range parent.Links {
		if id != parent.ID {
			links = append(links, id)
		}
	}
	return &Object{
		ID:         newID(),
		TypeID:     parent.TypeID,
		Title:      a.uniqueOccurrenceTitle(parent.Title, nextDate),
		Properties: properties,
		Links:      links,
		CreatedAt:  now(),
		UpdatedAt:  now(),
	}
}

// appendOccurrence adds one structured entry to the completed task's log. A
// list property is contract-legal (properties are free-form), keeps the note
// body untouched and is cheaper than rewriting content.
func appendOccurrence(properties map[string]any, entry map[string]any) ([]any, error) {
	entries := []any{}
	if raw, exists := properties["occurrences"]; exists && raw != nil {
		list, ok := raw.([]any)
		if !ok {
			return nil, errors.New("Property occurrences must be a list")
		}
		entries = append(entries, list...)
	}
	entries = append(entries, entry)
	if len(entries) > maxOccurrenceLog {
		entries = entries[len(entries)-maxOccurrenceLog:]
	}
	return entries, nil
}

// recurrenceValue renders the canonical property value stored on a task:
// sorted weekdays, n only when it applies, and JSON number types so a no-op
// PATCH compares equal to the value loaded from the encrypted store.
func recurrenceValue(rule recurrenceRule) map[string]any {
	value := map[string]any{"mode": rule.Mode}
	if rule.Mode == recurrenceEveryNDays {
		value["n"] = float64(rule.N)
	}
	if rule.Mode == recurrenceWeekly && len(rule.Weekdays) > 0 {
		weekdays := make([]any, len(rule.Weekdays))
		for i, day := range rule.Weekdays {
			weekdays[i] = float64(day)
		}
		value["weekdays"] = weekdays
	}
	return value
}

// validateTaskRecurrence is called from validateObject. Recurrence is a
// reserved task property: non-task objects keep arbitrary properties except
// this one, which would otherwise be silently ignored by the engine.
func validateTaskRecurrence(o *Object) error {
	raw, exists := o.Properties["recurrence"]
	if !exists || raw == nil {
		return nil
	}
	if o.TypeID != "task" {
		return errors.New("Property recurrence is only supported on task objects")
	}
	rule, _, err := recurrenceFromProperties(o.Properties)
	if err != nil {
		return err
	}
	due, ok := o.Properties["due_date"].(string)
	if !ok || strings.TrimSpace(due) == "" {
		return errors.New("A recurring task needs a due_date in YYYY-MM-DD format")
	}
	if _, err := rule.NextDate(due); err != nil {
		return err
	}
	o.Properties["recurrence"] = recurrenceValue(rule)
	return nil
}

// integerValue accepts the JSON number shapes that can reach a property map.
func integerValue(value any) (int, bool) {
	switch number := value.(type) {
	case float64:
		if math.IsNaN(number) || math.IsInf(number, 0) || math.Trunc(number) != number || number < -1e9 || number > 1e9 {
			return 0, false
		}
		return int(number), true
	case int:
		return number, true
	case int64:
		return int(number), true
	case json.Number:
		parsed, err := number.Int64()
		if err != nil {
			return 0, false
		}
		return int(parsed), true
	default:
		return 0, false
	}
}

// weekdayValues accepts the []any produced by encoding/json as well as the
// []int used by internal callers and tests.
func weekdayValues(value any) ([]any, bool) {
	switch list := value.(type) {
	case []any:
		return list, true
	case []int:
		out := make([]any, len(list))
		for i, day := range list {
			out[i] = day
		}
		return out, true
	default:
		return nil, false
	}
}
