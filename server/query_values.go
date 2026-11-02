package main

import (
	"errors"
	"html"
	"lumen/server/internal/document"
	"regexp"
	"strings"
	"time"
)

// datePropertyValue is the normalized form of a date property. A single date
// has an empty end; a range stores both inclusive days as YYYY-MM-DD.
type datePropertyValue struct {
	start string
	end   string
}

func (v datePropertyValue) hasEnd() bool { return v.end != "" }

// lastDay is the inclusive end used for overlap tests.
func (v datePropertyValue) lastDay() string {
	if v.end != "" {
		return v.end
	}
	return v.start
}

// parseDatePropertyValue accepts both the legacy plain string and the extended
// {start,end} object. Invalid or non-date values simply do not match filters;
// schema validation rejects them on write when a property is typed as date.
func parseDatePropertyValue(raw any) (datePropertyValue, bool) {
	switch value := raw.(type) {
	case string:
		if !validDay(value) {
			return datePropertyValue{}, false
		}
		return datePropertyValue{start: value}, true
	case map[string]any:
		start, ok := value["start"].(string)
		if !ok || !validDay(start) {
			return datePropertyValue{}, false
		}
		end := ""
		switch rawEnd := value["end"].(type) {
		case nil:
		case string:
			end = rawEnd
			if end != "" && !validDay(end) {
				return datePropertyValue{}, false
			}
		default:
			return datePropertyValue{}, false
		}
		if end != "" && end < start {
			return datePropertyValue{}, false
		}
		return datePropertyValue{start: start, end: end}, true
	case map[string]string:
		start := value["start"]
		if !validDay(start) {
			return datePropertyValue{}, false
		}
		end := value["end"]
		if end != "" && (!validDay(end) || end < start) {
			return datePropertyValue{}, false
		}
		return datePropertyValue{start: start, end: end}, true
	}
	return datePropertyValue{}, false
}

// validateDatePropertyValue is the write-path check for a date property.
func validateDatePropertyValue(raw any) error {
	if _, ok := parseDatePropertyValue(raw); !ok {
		return errInvalidDateValue
	}
	return nil
}

var errInvalidDateValue = errors.New("date properties must be YYYY-MM-DD or {start,end} with end on or after start")

func validDay(value string) bool {
	parsed, err := time.Parse("2006-01-02", value)
	return err == nil && parsed.Format("2006-01-02") == value
}

var htmlTag = regexp.MustCompile(`<[^>]*>`)

// objectHasText reports whether the representation a client renders contains
// non-whitespace text. The desktop editor prefers a non-empty blocks array,
// then rich_text, then the plain content field; this mirrors that precedence so
// "content empty" agrees with what the user sees.
func objectHasText(o Object) bool {
	if o.Document != nil {
		return strings.TrimSpace(document.PlainText(*o.Document)) != ""
	}
	if blocks, ok := o.Properties["blocks"].([]any); ok && len(blocks) > 0 {
		for _, raw := range blocks {
			if blockHasText(raw) {
				return true
			}
		}
		return false
	}
	if rich, ok := o.Properties["rich_text"].(string); ok {
		return strings.TrimSpace(richTextPlain(rich)) != ""
	}
	return strings.TrimSpace(o.Content) != ""
}

func blockHasText(raw any) bool {
	block, ok := raw.(map[string]any)
	if !ok {
		return false
	}
	if text, ok := block["text"].(string); ok && strings.TrimSpace(text) != "" {
		return true
	}
	runs, _ := block["runs"].([]any)
	for _, rawRun := range runs {
		run, ok := rawRun.(map[string]any)
		if !ok {
			continue
		}
		if text, ok := run["text"].(string); ok && strings.TrimSpace(text) != "" {
			return true
		}
	}
	return false
}

// richTextPlain strips editor HTML tags and decodes entities so markup that
// renders no words (<p></p>, <p>&nbsp;</p>) counts as empty.
func richTextPlain(value string) string {
	return html.UnescapeString(htmlTag.ReplaceAllString(value, " "))
}
