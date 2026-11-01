package main

import (
	"strings"
	"testing"
)

func TestRecurrenceNextDate(t *testing.T) {
	cases := []struct {
		name string
		rule recurrenceRule
		due  string
		want string
	}{
		{"daily", recurrenceRule{Mode: recurrenceDaily}, "2026-10-31", "2026-11-01"},
		{"daily across year", recurrenceRule{Mode: recurrenceDaily}, "2026-12-31", "2027-01-01"},
		{"daily into leap day", recurrenceRule{Mode: recurrenceDaily}, "2028-02-28", "2028-02-29"},
		{"daily after leap day", recurrenceRule{Mode: recurrenceDaily}, "2028-02-29", "2028-03-01"},
		{"every n days", recurrenceRule{Mode: recurrenceEveryNDays, N: 3}, "2026-10-30", "2026-11-02"},
		{"every n days across year", recurrenceRule{Mode: recurrenceEveryNDays, N: 10}, "2026-12-28", "2027-01-07"},
		{"weekly plain", recurrenceRule{Mode: recurrenceWeekly}, "2026-10-28", "2026-11-04"},
		{"weekly monday stays monday", recurrenceRule{Mode: recurrenceWeekly, Weekdays: []int{1}}, "2026-10-26", "2026-11-02"},
		{"weekly next listed weekday", recurrenceRule{Mode: recurrenceWeekly, Weekdays: []int{1, 3}}, "2026-10-26", "2026-10-28"},
		{"weekly wraps to monday", recurrenceRule{Mode: recurrenceWeekly, Weekdays: []int{1, 3}}, "2026-10-28", "2026-11-02"},
		{"weekly friday", recurrenceRule{Mode: recurrenceWeekly, Weekdays: []int{5}}, "2026-10-30", "2026-11-06"},
		{"weekly sunday", recurrenceRule{Mode: recurrenceWeekly, Weekdays: []int{7}}, "2026-11-01", "2026-11-08"},
		{"weekly workdays from friday", recurrenceRule{Mode: recurrenceWeekly, Weekdays: []int{1, 2, 3, 4, 5}}, "2026-10-30", "2026-11-02"},
		{"monthly", recurrenceRule{Mode: recurrenceMonthly}, "2026-10-15", "2026-11-15"},
		{"monthly across year", recurrenceRule{Mode: recurrenceMonthly}, "2026-12-15", "2027-01-15"},
		{"monthly january clamp", recurrenceRule{Mode: recurrenceMonthly}, "2026-01-31", "2026-02-28"},
		{"monthly january leap clamp", recurrenceRule{Mode: recurrenceMonthly}, "2028-01-31", "2028-02-29"},
		{"monthly january 30 leap clamp", recurrenceRule{Mode: recurrenceMonthly}, "2028-01-30", "2028-02-29"},
		{"monthly march clamp", recurrenceRule{Mode: recurrenceMonthly}, "2026-03-31", "2026-04-30"},
		{"monthly may clamp", recurrenceRule{Mode: recurrenceMonthly}, "2026-05-31", "2026-06-30"},
		{"monthly august clamp", recurrenceRule{Mode: recurrenceMonthly}, "2026-08-31", "2026-09-30"},
		{"monthly leap day", recurrenceRule{Mode: recurrenceMonthly}, "2028-02-29", "2028-03-29"},
		{"monthly december", recurrenceRule{Mode: recurrenceMonthly}, "2026-12-31", "2027-01-31"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			got, err := tc.rule.NextDate(tc.due)
			if err != nil {
				t.Fatal(err)
			}
			if got != tc.want {
				t.Fatalf("NextDate(%s) = %s, want %s", tc.due, got, tc.want)
			}
		})
	}
}

func TestRecurrenceNextDateRejectsInvalidDueDate(t *testing.T) {
	for _, due := range []string{"", "2026-2-3", "31/10/2026", "2026-02-30", "tomorrow"} {
		if _, err := (recurrenceRule{Mode: recurrenceDaily}).NextDate(due); err == nil {
			t.Fatalf("accepted due date %q", due)
		}
	}
}

func TestRecurrenceValidation(t *testing.T) {
	valid := []recurrenceRule{
		{Mode: recurrenceDaily},
		{Mode: recurrenceWeekly},
		{Mode: recurrenceWeekly, Weekdays: []int{7, 1, 3}},
		{Mode: recurrenceMonthly},
		{Mode: recurrenceEveryNDays, N: 1},
		{Mode: recurrenceEveryNDays, N: maxRecurrenceDays},
	}
	for _, rule := range valid {
		if err := rule.validate(); err != nil {
			t.Fatalf("%+v rejected: %v", rule, err)
		}
	}
	invalid := []recurrenceRule{
		{},
		{Mode: "Daily"},
		{Mode: "unsupported"},
		{Mode: recurrenceDaily, N: 2},
		{Mode: recurrenceDaily, Weekdays: []int{1}},
		{Mode: recurrenceWeekly, N: 2},
		{Mode: recurrenceWeekly, Weekdays: []int{0}},
		{Mode: recurrenceWeekly, Weekdays: []int{8}},
		{Mode: recurrenceWeekly, Weekdays: []int{1, 1}},
		{Mode: recurrenceWeekly, Weekdays: []int{1, 2, 3, 4, 5, 6, 7, 8}},
		{Mode: recurrenceMonthly, Weekdays: []int{1}},
		{Mode: recurrenceEveryNDays},
		{Mode: recurrenceEveryNDays, N: -1},
		{Mode: recurrenceEveryNDays, N: maxRecurrenceDays + 1},
		{Mode: recurrenceEveryNDays, Weekdays: []int{1}},
	}
	for _, rule := range invalid {
		if err := rule.validate(); err == nil {
			t.Fatalf("%+v accepted", rule)
		}
	}
}

func TestRecurrenceFromProperty(t *testing.T) {
	if _, recurring, err := recurrenceFromProperties(map[string]any{}); err != nil || recurring {
		t.Fatal("absent recurrence must be a normal task")
	}
	if _, recurring, err := recurrenceFromProperties(map[string]any{"recurrence": nil}); err != nil || recurring {
		t.Fatal("null recurrence must be a normal task")
	}
	rule, recurring, err := recurrenceFromProperties(map[string]any{
		"recurrence": map[string]any{"mode": "weekly", "weekdays": []any{float64(3), float64(1)}},
	})
	if err != nil || !recurring || rule.Mode != recurrenceWeekly || len(rule.Weekdays) != 2 || rule.Weekdays[0] != 1 || rule.Weekdays[1] != 3 {
		t.Fatalf("unexpected rule: %+v, %v, %v", rule, recurring, err)
	}
	rule, recurring, err = recurrenceFromProperties(map[string]any{
		"recurrence": map[string]any{"mode": "every_n_days", "n": float64(3)},
	})
	if err != nil || !recurring || rule.N != 3 {
		t.Fatalf("unexpected every_n_days rule: %+v, %v, %v", rule, recurring, err)
	}
	// Internal callers may build the property with Go ints and []int.
	rule, recurring, err = recurrenceFromProperties(map[string]any{
		"recurrence": map[string]any{"mode": "weekly", "weekdays": []int{5, 1}},
	})
	if err != nil || !recurring || len(rule.Weekdays) != 2 || rule.Weekdays[0] != 1 || rule.Weekdays[1] != 5 {
		t.Fatalf("unexpected int rule: %+v, %v, %v", rule, recurring, err)
	}
	for _, invalid := range []any{
		"daily",
		[]any{"daily"},
		map[string]any{"mode": "daily", "interval": 1},
		map[string]any{"mode": "every_n_days", "n": 2.5},
		map[string]any{"mode": "every_n_days", "n": "3"},
		map[string]any{"mode": "every_n_days"},
		map[string]any{"mode": "weekly", "weekdays": []any{"Monday"}},
		map[string]any{"mode": "weekly", "weekdays": []any{}},
		map[string]any{"mode": "weekly", "weekdays": nil},
		map[string]any{"mode": "weekly", "weekdays": []any{float64(1), float64(1)}},
		map[string]any{"mode": "monthly", "weekdays": []any{float64(1)}},
		map[string]any{"mode": "daily", "n": float64(2)},
	} {
		if _, _, err := recurrenceFromProperties(map[string]any{"recurrence": invalid}); err == nil {
			t.Fatalf("accepted %#v", invalid)
		}
	}
}

func TestOccurrenceTitle(t *testing.T) {
	cases := map[string]string{
		"Water plants":                 "Water plants (2026-11-01)",
		"Water plants (2026-10-31)":    "Water plants (2026-11-01)",
		"Water plants (2026-10-31 #2)": "Water plants (2026-11-01)",
		"Review (2026-10-31) notes":    "Review (2026-10-31) notes (2026-11-01)",
		"   ":                          "Task (2026-11-01)",
	}
	for input, want := range cases {
		if got := occurrenceTitle(input, "2026-11-01"); got != want {
			t.Fatalf("occurrenceTitle(%q) = %q, want %q", input, got, want)
		}
	}
	long := strings.Repeat("a", 1200)
	title := occurrenceTitle(long, "2026-11-01")
	if len([]rune(title)) != 1000 {
		t.Fatalf("long title has %d runes, want 1000", len([]rune(title)))
	}
	if !strings.HasSuffix(title, " (2026-11-01)") {
		t.Fatalf("long title lost its occurrence suffix: %q", title[len(title)-20:])
	}
}

func TestTaskIsDoneStatusSemantics(t *testing.T) {
	for status, want := range map[string]bool{"done": true, "completed": true, " DONE ": true, "todo": false, "": false, "in progress": false} {
		task := Object{Properties: map[string]any{"status": status}}
		if got := taskIsDone(task); got != want {
			t.Fatalf("status %q: got %v, want %v", status, got, want)
		}
	}
}

func TestAppendOccurrenceIsBounded(t *testing.T) {
	properties := map[string]any{}
	for i := 0; i < maxOccurrenceLog+5; i++ {
		entries, err := appendOccurrence(properties, map[string]any{"i": i})
		if err != nil {
			t.Fatal(err)
		}
		properties["occurrences"] = entries
	}
	entries, ok := properties["occurrences"].([]any)
	if !ok || len(entries) != maxOccurrenceLog {
		t.Fatalf("log has %d entries, want %d", len(entries), maxOccurrenceLog)
	}
	if first := entries[0].(map[string]any); first["i"] != 5 {
		t.Fatalf("oldest entry was not pruned: %+v", first)
	}
	if last := entries[len(entries)-1].(map[string]any); last["i"] != maxOccurrenceLog+4 {
		t.Fatalf("newest entry was not kept: %+v", last)
	}
	if _, err := appendOccurrence(map[string]any{"occurrences": "not-a-list"}, map[string]any{}); err == nil {
		t.Fatal("invalid occurrence log was accepted")
	}
}
