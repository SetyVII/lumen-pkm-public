package main

import (
	"errors"
	"math"
	"net/http"
	"sort"
	"time"
)

func calendarDate(raw string) (time.Time, error) {
	date, err := time.Parse("2006-01-02", raw)
	if err != nil || date.Format("2006-01-02") != raw || date.Year() < 1 {
		return time.Time{}, errors.New("Date must be YYYY-MM-DD")
	}
	return date, nil
}

// Habits are ordinary encrypted objects. Date-keyed counts replace, rather
// than increment, so a network retry cannot count the same activity twice.
func validateHabit(o *Object) error {
	if o.TypeID != "habit" {
		return nil
	}
	if o.Properties == nil {
		o.Properties = map[string]any{}
	}
	if o.Properties["habit_start"] == nil {
		o.Properties["habit_start"] = time.Now().UTC().Format("2006-01-02")
	}
	start, ok := o.Properties["habit_start"].(string)
	if !ok {
		return errors.New("habit_start must be YYYY-MM-DD")
	}
	if _, err := calendarDate(start); err != nil {
		return err
	}
	if o.Properties["habit_target"] == nil {
		o.Properties["habit_target"] = float64(1)
	}
	target, ok := finiteNumber(o.Properties["habit_target"])
	if !ok || target <= 0 || target > 1e9 {
		return errors.New("habit_target must be a positive finite number up to 1000000000")
	}
	o.Properties["habit_target"] = target
	if o.Properties["habit_schedule"] == nil {
		o.Properties["habit_schedule"] = []any{float64(1), float64(2), float64(3), float64(4), float64(5), float64(6), float64(7)}
	}
	values, ok := weekdayValues(o.Properties["habit_schedule"])
	if !ok || len(values) == 0 || len(values) > 7 {
		return errors.New("habit_schedule must contain ISO weekdays 1-7")
	}
	seen := map[int]bool{}
	days := []int{}
	for _, v := range values {
		day, ok := integerValue(v)
		if !ok || day < 1 || day > 7 || seen[day] {
			return errors.New("habit_schedule must contain distinct ISO weekdays 1-7")
		}
		seen[day] = true
		days = append(days, day)
	}
	sort.Ints(days)
	normalized := []any{}
	for _, day := range days {
		normalized = append(normalized, float64(day))
	}
	o.Properties["habit_schedule"] = normalized
	if o.Properties["habit_entries"] == nil {
		o.Properties["habit_entries"] = map[string]any{}
	}
	entries, ok := o.Properties["habit_entries"].(map[string]any)
	if !ok || len(entries) > 10000 {
		return errors.New("habit_entries must be a date-keyed object with at most 10000 entries")
	}
	for raw, v := range entries {
		date, err := calendarDate(raw)
		if err != nil {
			return err
		}
		if raw < start || !seen[isoWeekday(date)] {
			return errors.New("Habit entries must be on scheduled days on or after habit_start")
		}
		value, ok := finiteNumber(v)
		if !ok || value < 0 || value > 1e9 {
			return errors.New("Habit values must be finite numbers between 0 and 1000000000")
		}
		entries[raw] = value
	}
	return nil
}

func finiteNumber(raw any) (float64, bool) {
	var v float64
	switch n := raw.(type) {
	case float64:
		v = n
	case int:
		v = float64(n)
	default:
		return 0, false
	}
	return v, !math.IsNaN(v) && !math.IsInf(v, 0)
}

func (a *App) registerHabitRoutes(mux *http.ServeMux) {
	mux.HandleFunc("PUT /v1/habits/{id}/entries/{date}", a.editHabitEntry)
	mux.HandleFunc("DELETE /v1/habits/{id}/entries/{date}", a.editHabitEntry)
	mux.HandleFunc("GET /v1/habits/{id}/stats", a.habitStats)
}

func (a *App) editHabitEntry(w http.ResponseWriter, r *http.Request) {
	var req struct {
		BaseRevision uint64   `json:"base_revision"`
		Value        *float64 `json:"value"`
	}
	if !decode(w, r, &req) {
		return
	}
	if req.BaseRevision == 0 {
		jsonError(w, 400, "base_revision is required")
		return
	}
	if _, err := calendarDate(r.PathValue("date")); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if r.Method == http.MethodPut && req.Value == nil {
		jsonError(w, 400, "value is required")
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, err := a.store.Object(r.PathValue("id"))
	if err != nil {
		jsonError(w, 404, "Habit not found")
		return
	}
	if o.TypeID != "habit" {
		jsonError(w, 400, "This object is not a habit")
		return
	}
	if o.Revision != req.BaseRevision {
		jsonResponse(w, 409, map[string]any{"error": "conflict", "current": o})
		return
	}
	if err = validateHabit(&o); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	entries := o.Properties["habit_entries"].(map[string]any)
	date := r.PathValue("date")
	if r.Method == http.MethodDelete {
		if _, exists := entries[date]; !exists {
			jsonResponse(w, 200, o)
			return
		}
		delete(entries, date)
	} else {
		if current, exists := entries[date]; exists && current == *req.Value {
			jsonResponse(w, 200, o)
			return
		}
		entries[date] = *req.Value
	}
	if err = a.validateObject(&o); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	o.UpdatedAt = now()
	if err = a.saveObject(r.Context(), &o); err != nil {
		jsonError(w, 500, "Could not save habit entry")
		return
	}
	jsonResponse(w, 200, o)
}

type habitStatistics struct {
	ObjectID      string  `json:"object_id"`
	AsOf          string  `json:"as_of"`
	ScheduledDays int     `json:"scheduled_days"`
	CompletedDays int     `json:"completed_days"`
	CurrentStreak int     `json:"current_streak"`
	BestStreak    int     `json:"best_streak"`
	Total         float64 `json:"total"`
}

func computeHabitStats(o Object, asOf string) (habitStatistics, error) {
	result := habitStatistics{ObjectID: o.ID, AsOf: asOf}
	if err := validateHabit(&o); err != nil {
		return result, err
	}
	end, err := calendarDate(asOf)
	if err != nil {
		return result, err
	}
	start, _ := calendarDate(o.Properties["habit_start"].(string))
	if end.Before(start) {
		return result, nil
	}
	if end.Sub(start).Hours() > 24*36600 {
		return result, errors.New("Statistics support at most 100 years per request")
	}
	schedule := map[int]bool{}
	for _, raw := range o.Properties["habit_schedule"].([]any) {
		day, _ := integerValue(raw)
		schedule[day] = true
	}
	target, _ := finiteNumber(o.Properties["habit_target"])
	entries := o.Properties["habit_entries"].(map[string]any)
	run := 0
	for date := start; !date.After(end); date = date.AddDate(0, 0, 1) {
		if !schedule[isoWeekday(date)] {
			continue
		}
		key := date.Format("2006-01-02")
		value, _ := finiteNumber(entries[key])
		result.Total += value
		result.ScheduledDays++
		if value >= target {
			result.CompletedDays++
			run++
			result.BestStreak = max(result.BestStreak, run)
		} else if key != asOf {
			run = 0
		}
	}
	result.CurrentStreak = run
	return result, nil
}

func (a *App) habitStats(w http.ResponseWriter, r *http.Request) {
	asOf := r.URL.Query().Get("as_of")
	if asOf == "" {
		asOf = time.Now().UTC().Format("2006-01-02")
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	o, err := a.store.Object(r.PathValue("id"))
	if err != nil {
		jsonError(w, 404, "Habit not found")
		return
	}
	if o.TypeID != "habit" {
		jsonError(w, 400, "This object is not a habit")
		return
	}
	result, err := computeHabitStats(o, asOf)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	jsonResponse(w, 200, result)
}
