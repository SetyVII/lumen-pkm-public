package main

import (
	"database/sql"
	"errors"
	"fmt"
	"net/url"
	"sort"
	"strconv"
	"strings"
)

// objectFilters is the parsed and validated form of the GET /v1/objects query
// string. The grammar is documented in
// docs/implementation/backend/query-filters.md and contracts/SYNC.md.
//
// Unknown query parameters are ignored on purpose: clients and later stages add
// parameters (pagination, include_templates, ...) and an older server must not
// reject a request just because it does not know one of them. Known parameters
// with malformed values are rejected with HTTP 400.
type objectFilters struct {
	typeID string
	query  string

	linkedCount        *countRange
	linkedIncludes     []string
	linkedTypes        []string
	backlinkedCount    *countRange
	backlinkedIncludes []string
	backlinkedTypes    []string

	contentEmpty *bool
	collections  []string

	dates []propertyDateFilter
}

// countRange is an inclusive [min,max] interval; a nil bound is open.
type countRange struct {
	min *int
	max *int
}

func (r countRange) contains(value int) bool {
	if r.min != nil && value < *r.min {
		return false
	}
	if r.max != nil && value > *r.max {
		return false
	}
	return true
}

// propertyDateFilter is the conjunction of the props.<name>.* filters for one
// property. from/to are inclusive YYYY-MM-DD days and an empty value is open.
type propertyDateFilter struct {
	property string
	from     string
	to       string
	hasEnd   *bool
}

// filterError marks a problem with the request (HTTP 400) as opposed to a
// store failure (HTTP 500).
type filterError struct{ message string }

func (e filterError) Error() string { return e.message }

func invalidFilter(format string, args ...any) error {
	return filterError{message: fmt.Sprintf(format, args...)}
}

func parseObjectFilters(values url.Values) (objectFilters, error) {
	f := objectFilters{}
	// url.Values is a map; visiting sorted keys keeps error messages stable.
	keys := make([]string, 0, len(values))
	for key := range values {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	dateByProperty := map[string]*propertyDateFilter{}
	for _, key := range keys {
		raw := values[key]
		switch key {
		case "type_id":
			f.typeID = strings.TrimSpace(firstValue(raw))
		case "q":
			// Unchanged legacy semantics: case-insensitive substring over title
			// and plain content. No trimming, so existing clients behave the same.
			f.query = strings.ToLower(firstValue(raw))
		case "linked_count":
			bounds, err := parseCountRange(firstValue(raw))
			if err != nil {
				return f, invalidFilter("invalid linked_count: %v", err)
			}
			f.linkedCount = bounds
		case "backlinked_count":
			bounds, err := parseCountRange(firstValue(raw))
			if err != nil {
				return f, invalidFilter("invalid backlinked_count: %v", err)
			}
			f.backlinkedCount = bounds
		case "linked_includes":
			ids, err := parseIDList(raw)
			if err != nil {
				return f, invalidFilter("invalid linked_includes: %v", err)
			}
			f.linkedIncludes = append(f.linkedIncludes, ids...)
		case "backlinked_includes":
			ids, err := parseIDList(raw)
			if err != nil {
				return f, invalidFilter("invalid backlinked_includes: %v", err)
			}
			f.backlinkedIncludes = append(f.backlinkedIncludes, ids...)
		case "linked_type":
			ids, err := parseIDList(raw)
			if err != nil {
				return f, invalidFilter("invalid linked_type: %v", err)
			}
			f.linkedTypes = append(f.linkedTypes, ids...)
		case "backlinked_type":
			ids, err := parseIDList(raw)
			if err != nil {
				return f, invalidFilter("invalid backlinked_type: %v", err)
			}
			f.backlinkedTypes = append(f.backlinkedTypes, ids...)
		case "content_empty":
			value, err := parseBoolParam(key, firstValue(raw))
			if err != nil {
				return f, err
			}
			f.contentEmpty = &value
		case "collection":
			ids, err := parseIDList(raw)
			if err != nil {
				return f, invalidFilter("invalid collection: %v", err)
			}
			f.collections = append(f.collections, ids...)
		default:
			if !strings.HasPrefix(key, "props.") {
				continue
			}
			property, op, err := parsePropertyFilterKey(key)
			if err != nil {
				return f, err
			}
			entry := dateByProperty[property]
			if entry == nil {
				entry = &propertyDateFilter{property: property}
				dateByProperty[property] = entry
			}
			switch op {
			case "from":
				day, err := parseFilterDay(key, firstValue(raw))
				if err != nil {
					return f, err
				}
				entry.from = day
			case "to":
				day, err := parseFilterDay(key, firstValue(raw))
				if err != nil {
					return f, err
				}
				entry.to = day
			case "has_end":
				value, err := parseBoolParam(key, firstValue(raw))
				if err != nil {
					return f, err
				}
				entry.hasEnd = &value
			}
		}
	}
	for _, property := range sortedKeys(dateByProperty) {
		entry := dateByProperty[property]
		if entry.from != "" && entry.to != "" && entry.from > entry.to {
			return f, invalidFilter("props.%s.from must not be after props.%s.to", property, property)
		}
		f.dates = append(f.dates, *entry)
	}
	return f, nil
}

func firstValue(values []string) string {
	if len(values) == 0 {
		return ""
	}
	return values[0]
}

func sortedKeys(values map[string]*propertyDateFilter) []string {
	keys := make([]string, 0, len(values))
	for key := range values {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}

// parsePropertyFilterKey splits props.<name>.<op> from the right because
// property keys may themselves contain dots (the schema allows them).
func parsePropertyFilterKey(key string) (property, op string, err error) {
	rest := strings.TrimPrefix(key, "props.")
	dot := strings.LastIndex(rest, ".")
	if dot <= 0 || dot == len(rest)-1 {
		return "", "", invalidFilter("invalid property filter %q: expected props.<name>.from, props.<name>.to or props.<name>.has_end", key)
	}
	property, op = rest[:dot], rest[dot+1:]
	switch op {
	case "from", "to", "has_end":
	default:
		return "", "", invalidFilter("invalid property filter %q: unknown operation %q", key, op)
	}
	if !propertyKey.MatchString(property) {
		return "", "", invalidFilter("invalid property filter %q: property name is not valid", key)
	}
	return property, op, nil
}

func parseCountRange(raw string) (*countRange, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" {
		return nil, errors.New("expected an integer or min,max")
	}
	parts := strings.Split(raw, ",")
	if len(parts) > 2 {
		return nil, errors.New("expected an integer or min,max")
	}
	parseBound := func(part string) (*int, error) {
		part = strings.TrimSpace(part)
		if part == "" {
			return nil, nil
		}
		number, err := strconv.Atoi(part)
		if err != nil || number < 0 {
			return nil, errors.New("counts must be non-negative integers")
		}
		return &number, nil
	}
	min, err := parseBound(parts[0])
	if err != nil {
		return nil, err
	}
	if len(parts) == 1 {
		if min == nil {
			return nil, errors.New("expected an integer or min,max")
		}
		exact := *min
		return &countRange{min: &exact, max: &exact}, nil
	}
	max, err := parseBound(parts[1])
	if err != nil {
		return nil, err
	}
	if min == nil && max == nil {
		return nil, errors.New("expected at least one bound")
	}
	if min != nil && max != nil && *max < *min {
		return nil, errors.New("max must not be lower than min")
	}
	return &countRange{min: min, max: max}, nil
}

func parseIDList(values []string) ([]string, error) {
	out := make([]string, 0, len(values))
	for _, value := range values {
		value = strings.TrimSpace(value)
		if value == "" {
			return nil, errors.New("object and type ids must not be empty")
		}
		out = append(out, value)
	}
	return out, nil
}

func parseBoolParam(name, raw string) (bool, error) {
	switch strings.ToLower(strings.TrimSpace(raw)) {
	case "true":
		return true, nil
	case "false":
		return false, nil
	}
	return false, invalidFilter("%s must be true or false", name)
}

func parseFilterDay(name, raw string) (string, error) {
	raw = strings.TrimSpace(raw)
	if !validDay(raw) {
		return "", invalidFilter("%s must be a YYYY-MM-DD date", name)
	}
	return raw, nil
}

// relationIndex is a read-only view of the in-memory relation index. It is only
// valid inside the callback passed to memIndex.readRelations; the callback must
// not call back into the index or the store.
type relationIndex struct {
	objects      map[string]ObjectMeta
	backlinkSets map[string]map[string]struct{}
	ordered      []string
	byType       map[string][]string
}

// readRelations exposes the index under its read lock so a list request can
// evaluate relational filters without copying every ObjectMeta.
func (m *memIndex) readRelations(fn func(relationIndex)) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	fn(relationIndex{
		objects:      m.objects,
		backlinkSets: m.backlinkSets,
		ordered:      m.ordered,
		byType:       m.byType,
	})
}

// candidates returns object ids in display order (most recently updated first).
func (idx relationIndex) candidates(typeID string) []string {
	if typeID == "" {
		return idx.ordered
	}
	ids := append([]string(nil), idx.byType[typeID]...)
	sort.Slice(ids, func(i, j int) bool {
		return metaLess(idx.objects[ids[i]], idx.objects[ids[j]])
	})
	return ids
}

// backlinkSources returns the live sources of the deduped backlink union.
func (idx relationIndex) backlinkSources(id string) []string {
	set := idx.backlinkSets[id]
	if len(set) == 0 {
		return nil
	}
	out := make([]string, 0, len(set))
	for source := range set {
		if _, ok := idx.objects[source]; ok {
			out = append(out, source)
		}
	}
	return out
}

// applyObjectFilters returns the objects matching f in display order.
//
// Relational filters run against the in-memory index only, so the server does
// not decrypt a single payload to count links or backlinks. Content, date and
// text filters then decrypt only the candidates that survived that first pass.
func (a *App) applyObjectFilters(f objectFilters) ([]Object, error) {
	// Resolve collections first: a bad id is a client error, not an empty list.
	collectionSets := make([]map[string]struct{}, 0, len(f.collections))
	for _, id := range f.collections {
		items, err := a.collectionItemIDs(id)
		if err != nil {
			return nil, err
		}
		set := make(map[string]struct{}, len(items))
		for _, item := range items {
			set[item] = struct{}{}
		}
		collectionSets = append(collectionSets, set)
	}

	ids := []string{}
	a.store.index.readRelations(func(idx relationIndex) {
		for _, id := range idx.candidates(f.typeID) {
			meta, ok := idx.objects[id]
			if !ok {
				continue
			}
			if f.matchRelations(idx, meta) {
				ids = append(ids, id)
			}
		}
	})

	if len(collectionSets) > 0 {
		kept := ids[:0]
		for _, id := range ids {
			matches := true
			for _, set := range collectionSets {
				if _, ok := set[id]; !ok {
					matches = false
					break
				}
			}
			if matches {
				kept = append(kept, id)
			}
		}
		ids = kept
	}

	out := make([]Object, 0, len(ids))
	for _, id := range ids {
		o, err := a.store.Object(id)
		if err != nil {
			// A concurrent delete can remove an object between the index pass
			// and this load; a list request should simply omit it.
			if errors.Is(err, sql.ErrNoRows) {
				continue
			}
			return nil, err
		}
		if f.matchObject(o) {
			out = append(out, o)
		}
	}
	return out, nil
}

// matchRelations is the index-only half of the filter. It never touches
// encrypted payloads. The link and backlink unions are only built when a
// relational filter is present, so an unfiltered or type-only listing stays
// allocation-free per object.
func (f objectFilters) matchRelations(idx relationIndex, meta ObjectMeta) bool {
	if f.hasLinkFilters() {
		links := allObjectLinks(Object{Links: meta.Links, PropertyLinks: meta.PropertyLinks})
		if f.linkedCount != nil && !f.linkedCount.contains(len(links)) {
			return false
		}
		if len(f.linkedIncludes) > 0 {
			set := stringSet(links)
			for _, id := range f.linkedIncludes {
				if _, ok := set[id]; !ok {
					return false
				}
			}
		}
		if len(f.linkedTypes) > 0 {
			types := relationTypes(idx, links)
			for _, typeID := range f.linkedTypes {
				if _, ok := types[typeID]; !ok {
					return false
				}
			}
		}
	}

	if f.hasBacklinkFilters() {
		backlinks := idx.backlinkSources(meta.ID)
		if f.backlinkedCount != nil && !f.backlinkedCount.contains(len(backlinks)) {
			return false
		}
		if len(f.backlinkedIncludes) > 0 {
			set := stringSet(backlinks)
			for _, id := range f.backlinkedIncludes {
				if _, ok := set[id]; !ok {
					return false
				}
			}
		}
		if len(f.backlinkedTypes) > 0 {
			types := relationTypes(idx, backlinks)
			for _, typeID := range f.backlinkedTypes {
				if _, ok := types[typeID]; !ok {
					return false
				}
			}
		}
	}
	return true
}

func (f objectFilters) hasLinkFilters() bool {
	return f.linkedCount != nil || len(f.linkedIncludes) > 0 || len(f.linkedTypes) > 0
}

func (f objectFilters) hasBacklinkFilters() bool {
	return f.backlinkedCount != nil || len(f.backlinkedIncludes) > 0 || len(f.backlinkedTypes) > 0
}

func relationTypes(idx relationIndex, ids []string) map[string]struct{} {
	types := make(map[string]struct{}, len(ids))
	for _, id := range ids {
		if meta, ok := idx.objects[id]; ok {
			types[meta.TypeID] = struct{}{}
		}
	}
	return types
}

func stringSet(values []string) map[string]struct{} {
	set := make(map[string]struct{}, len(values))
	for _, value := range values {
		set[value] = struct{}{}
	}
	return set
}

// matchObject is the payload half of the filter. It runs after the index pass.
func (f objectFilters) matchObject(o Object) bool {
	if f.query != "" && !objectMatchesQuery(o, f.query) {
		return false
	}
	if f.contentEmpty != nil && objectHasText(o) == *f.contentEmpty {
		return false
	}
	for _, date := range f.dates {
		if !date.match(o) {
			return false
		}
	}
	return true
}

func (f propertyDateFilter) match(o Object) bool {
	raw, ok := o.Properties[f.property]
	if !ok || raw == nil {
		return false
	}
	value, ok := parseDatePropertyValue(raw)
	if !ok {
		return false
	}
	if f.hasEnd != nil && value.hasEnd() != *f.hasEnd {
		return false
	}
	if f.from != "" && value.lastDay() < f.from {
		return false
	}
	if f.to != "" && value.start > f.to {
		return false
	}
	return true
}
