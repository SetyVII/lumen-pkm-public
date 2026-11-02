package main

import (
	"errors"
	"fmt"
	"lumen/server/internal/document"
	"net/http"
	"regexp"
	"strconv"
	"strings"
	"time"
)

// isTemplateObject reports the reserved flag. Absent or false means a normal
// object of its type.
func isTemplateObject(o Object) bool {
	value, ok := o.Properties["is_template"].(bool)
	return ok && value
}

func queryBool(r *http.Request, key string) (bool, error) {
	raw := strings.TrimSpace(r.URL.Query().Get(key))
	if raw == "" {
		return false, nil
	}
	value, err := strconv.ParseBool(raw)
	if err != nil {
		return false, fmt.Errorf("%s must be a boolean", key)
	}
	return value, nil
}

var dateVariable = regexp.MustCompile(`\{date(?::([^{}]*))?\}`)

// resolveDateVariables expands the template date syntax. Unknown formats are
// left untouched so a typo never destroys the template text. Dates are parsed
// as calendar dates, matching the journal's date-only semantics.
func resolveDateVariables(text, date string) string {
	if !strings.Contains(text, "{date") {
		return text
	}
	parsed, err := time.Parse("2006-01-02", date)
	if err != nil {
		return text
	}
	return dateVariable.ReplaceAllStringFunc(text, func(token string) string {
		match := dateVariable.FindStringSubmatch(token)
		format := ""
		if len(match) > 1 {
			format = strings.ToLower(strings.TrimSpace(match[1]))
		}
		switch format {
		case "", "iso":
			return parsed.Format("2006-01-02")
		case "long":
			return parsed.Format("January 2, 2006")
		case "short":
			return parsed.Format("01/02/06")
		case "weekday":
			return parsed.Format("Monday")
		default:
			return token
		}
	})
}

func parseTemplateDate(raw string) (string, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return time.Now().Format("2006-01-02"), nil
	}
	parsed, err := time.Parse("2006-01-02", raw)
	if err != nil || parsed.Format("2006-01-02") != raw {
		return "", errors.New("Date must be YYYY-MM-DD")
	}
	return raw, nil
}

// uniqueTemplateTitle keeps the default title of a template usable more than
// once. Lumen requires unique titles for normal objects, so a fixed template
// title gets the same " (2)", " (3)" suffix convention as media objects. An
// explicitly supplied title is never rewritten; its conflict is a 400.
func (a *App) uniqueTemplateTitle(base string) string {
	base = strings.TrimSpace(base)
	if base == "" || a.store.TitleOwner(base) == "" {
		return base
	}
	for suffix := 2; ; suffix++ {
		suffixText := fmt.Sprintf(" (%d)", suffix)
		runes := []rune(base)
		if limit := 1000 - len([]rune(suffixText)); len(runes) > limit {
			runes = runes[:limit]
		}
		candidate := string(runes) + suffixText
		if a.store.TitleOwner(candidate) == "" {
			return candidate
		}
	}
}

// cloneBlocksWithNewIDs deep-copies the block array and mints a fresh ID for
// every block. References inside runs (object links, images) are preserved:
// applying a template links existing objects instead of duplicating them.
func cloneBlocksWithNewIDs(raw any) ([]any, error) {
	if raw == nil {
		return nil, nil
	}
	blocks, ok := raw.([]any)
	if !ok {
		return nil, errors.New("Template blocks must be an array")
	}
	out := make([]any, 0, len(blocks))
	for _, entry := range blocks {
		block, ok := entry.(map[string]any)
		if !ok {
			return nil, errors.New("Template blocks must contain objects")
		}
		copied := cloneStringMap(block)
		out = append(out, copied)
	}
	walkBlocks(out, 0, func(block map[string]any) { block["id"] = newID() })
	return out, nil
}

// isTemplateDateProperty identifies the properties that receive date-variable
// substitution. Typed date properties are authoritative; the built-in schemas
// still use legacy name-only definitions, so common date keys are recognized
// too. A property explicitly defined as another type is never substituted.
func isTemplateDateProperty(typ ObjectType, key string) bool {
	for _, raw := range typ.Properties {
		schema, ok := raw.(map[string]any)
		if !ok || schemaKey(schema) != key {
			continue
		}
		return schema["type"] == "date"
	}
	switch key {
	case "date", "due_date":
		return true
	}
	return strings.HasSuffix(key, "_date") || strings.HasPrefix(key, "date_")
}

// buildObjectFromTemplate copies the template payload into a new, unsaved
// object. The caller supplies the resolved title and reference date.
func (a *App) buildObjectFromTemplate(tpl Object, title, date string) (Object, error) {
	typ, err := a.store.Type(tpl.TypeID)
	if err != nil {
		return Object{}, errors.New("Template type does not exist")
	}
	properties := cloneStringMap(tpl.Properties)
	if properties == nil {
		properties = map[string]any{}
	}
	// The new object is never a template. Everything else is copied, including
	// links to existing objects and attachment references (linked, not cloned).
	delete(properties, "is_template")
	for key, value := range properties {
		if !isTemplateDateProperty(typ, key) {
			continue
		}
		if text, ok := value.(string); ok {
			properties[key] = resolveDateVariables(text, date)
		}
	}
	if rawBlocks, exists := properties["blocks"]; exists {
		if rawBlocks == nil {
			delete(properties, "blocks")
		} else {
			blocks, err := cloneBlocksWithNewIDs(rawBlocks)
			if err != nil {
				return Object{}, err
			}
			properties["blocks"] = blocks
		}
	}
	links := append([]string{}, tpl.Links...)
	var canonical *document.Document
	if tpl.Document != nil {
		copied := freshCanonicalDocument(*tpl.Document)
		canonical = &copied
		delete(properties, "blocks")
		delete(properties, "rich_text")
	}
	return Object{Document: canonical,
		TypeID:     tpl.TypeID,
		Title:      title,
		Content:    tpl.Content,
		Properties: properties,
		Links:      links,
		CreatedAt:  now(),
		UpdatedAt:  now(),
	}, nil
}

func (a *App) fromTemplate(w http.ResponseWriter, r *http.Request) {
	var req struct {
		TemplateID string  `json:"template_id"`
		Title      *string `json:"title"`
		Date       string  `json:"date"`
	}
	if !decode(w, r, &req) {
		return
	}
	req.TemplateID = strings.TrimSpace(req.TemplateID)
	if req.TemplateID == "" {
		jsonError(w, 400, "template_id is required")
		return
	}
	date, err := parseTemplateDate(req.Date)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	tpl, err := a.store.Object(req.TemplateID)
	if err != nil {
		jsonError(w, 404, "Template not found")
		return
	}
	if !isTemplateObject(tpl) {
		jsonError(w, 400, "Object is not a template")
		return
	}
	title := tpl.Title
	explicitTitle := req.Title != nil
	if explicitTitle {
		title = *req.Title
	}
	title = resolveDateVariables(title, date)
	if !explicitTitle {
		title = a.uniqueTemplateTitle(title)
	}
	o, err := a.buildObjectFromTemplate(tpl, title, date)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	o.ID = newID()
	o.Revision = 0
	o.Incarnation = ""
	if err := a.validateObject(&o); err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	if err := a.saveObject(r.Context(), &o); err != nil {
		jsonError(w, 500, "Could not persist encrypted object")
		return
	}
	jsonResponse(w, 201, o)
}

func (a *App) validateDefaultTemplate(typ ObjectType) error {
	if typ.DefaultTemplateID == "" {
		return nil
	}
	tpl, err := a.store.Object(typ.DefaultTemplateID)
	if err != nil {
		return errors.New("Default template does not exist")
	}
	if tpl.TypeID != typ.ID {
		return errors.New("Default template must belong to this type")
	}
	if !isTemplateObject(tpl) {
		return errors.New("Default template must be a template")
	}
	return nil
}

// clearDefaultTemplateRecords returns type records that drop a dangling
// default reference. Callers commit them with the object change.
func (a *App) clearDefaultTemplateRecords(objectID string) []Record {
	records := []Record{}
	for _, typ := range a.store.Types() {
		if typ.DefaultTemplateID != objectID {
			continue
		}
		typ.DefaultTemplateID = ""
		records = append(records, Record{Kind: "type", ID: typ.ID, Value: typ})
	}
	return records
}

func (a *App) templateDefaultCleanup(before, after Object) []Record {
	if !isTemplateObject(before) {
		return nil
	}
	if isTemplateObject(after) && before.TypeID == after.TypeID {
		return nil
	}
	return a.clearDefaultTemplateRecords(before.ID)
}

// applyJournalDefaultTemplate fills a lazily created daily note from the
// journal type's default template. The note keeps its canonical date title;
// only content, properties and links come from the template.
func (a *App) applyJournalDefaultTemplate(o *Object, date string) error {
	typ, err := a.store.Type("journal")
	if err != nil || typ.DefaultTemplateID == "" {
		return nil
	}
	tpl, err := a.store.Object(typ.DefaultTemplateID)
	if err != nil {
		// A dangling reference is ignored so the journal stays usable. Deleting
		// a template clears the reference transactionally, so this only happens
		// after manual store edits.
		return nil
	}
	if !isTemplateObject(tpl) || tpl.TypeID != "journal" {
		return nil
	}
	built, err := a.buildObjectFromTemplate(tpl, date, date)
	if err != nil {
		return err
	}
	o.Document = cloneDocument(built.Document)
	o.Content = built.Content
	o.Properties = built.Properties
	o.Links = built.Links
	o.Properties["date"] = date
	return a.validateObject(o)
}
