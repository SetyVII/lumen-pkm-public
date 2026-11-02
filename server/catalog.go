package main

import (
	"database/sql"
	"encoding/json"
	"errors"
	"net/http"
	"sort"
	"strconv"
	"strings"
	"time"
	"unicode/utf8"
)

const catalogPageLimit = 128
const catalogPageBytes = 8 << 20
const catalogPropertyBytes = 64 << 10
const catalogRelationLimit = 256

type catalogOptions struct {
	limit            int
	cursor           string
	typeID           string
	query            string
	includeTemplates bool
	includeRelations bool
	properties       []string
	excerptChars     int
}

// CatalogEntry is explicitly a read-only projection, never an Object snapshot.
// Missing bodies/properties must not be round-tripped through replacement writes.
type CatalogEntry struct {
	ID                 string         `json:"id"`
	TypeID             string         `json:"type_id"`
	Title              string         `json:"title"`
	Aliases            []string       `json:"aliases,omitempty"`
	IsTemplate         bool           `json:"is_template,omitempty"`
	CreatedAt          string         `json:"created_at"`
	UpdatedAt          string         `json:"updated_at"`
	Revision           uint64         `json:"revision"`
	Incarnation        string         `json:"incarnation,omitempty"`
	Properties         map[string]any `json:"properties,omitempty"`
	OmittedProperties  []string       `json:"omitted_properties,omitempty"`
	Excerpt            string         `json:"excerpt,omitempty"`
	ExcerptTruncated   bool           `json:"excerpt_truncated,omitempty"`
	RelatedIDs         []string       `json:"related_ids,omitempty"`
	RelationsTruncated bool           `json:"relations_truncated,omitempty"`
}
type catalogResponse struct {
	Projection          string         `json:"projection"`
	Entries             []CatalogEntry `json:"entries"`
	RequestedProperties []string       `json:"requested_properties,omitempty"`
	NextCursor          string         `json:"next_cursor"`
	HasMore             bool           `json:"has_more"`
}

func parseCatalogOptions(r *http.Request) (catalogOptions, error) {
	options := catalogOptions{limit: catalogPageLimit}
	query := r.URL.Query()
	for key := range query {
		switch key {
		case "limit", "cursor", "type_id", "q", "include_templates", "include_relations", "property", "excerpt_chars":
		default:
			return options, errors.New("Unsupported catalog parameter " + key)
		}
	}
	if raw := query.Get("limit"); raw != "" {
		value, err := strconv.Atoi(raw)
		if err != nil || value < 1 || value > catalogPageLimit {
			return options, errors.New("catalog limit must be 1-128")
		}
		options.limit = value
	}
	options.cursor = query.Get("cursor")
	options.typeID = query.Get("type_id")
	options.query = strings.ToLower(strings.TrimSpace(query.Get("q")))
	if !utf8.ValidString(options.query) || utf8.RuneCountInString(options.query) > 256 {
		return options, errors.New("catalog q supports at most 256 characters")
	}
	var err error
	if options.includeTemplates, err = queryBool(r, "include_templates"); err != nil {
		return options, err
	}
	if options.includeRelations, err = queryBool(r, "include_relations"); err != nil {
		return options, err
	}
	if raw := query.Get("excerpt_chars"); raw != "" {
		options.excerptChars, err = strconv.Atoi(raw)
		if err != nil || options.excerptChars < 0 || options.excerptChars > 512 {
			return options, errors.New("excerpt_chars must be 0-512")
		}
	}
	seen := map[string]bool{}
	for _, key := range query["property"] {
		if key == "" || len(key) > 128 || !utf8.ValidString(key) {
			return options, errors.New("Invalid catalog property key")
		}
		switch key {
		case "blocks", "rich_text", "reader_content", "document":
			return options, errors.New("Body properties are unavailable in the catalog; fetch the full object")
		}
		if !seen[key] {
			options.properties = append(options.properties, key)
			seen[key] = true
		}
	}
	if len(options.properties) > 64 {
		return options, errors.New("Catalog supports at most 64 requested properties")
	}
	return options, nil
}

func catalogMatches(meta ObjectMeta, options catalogOptions) bool {
	if meta.IsTemplate && !options.includeTemplates {
		return false
	}
	if options.typeID != "" && meta.TypeID != options.typeID {
		return false
	}
	if options.query == "" || strings.Contains(strings.ToLower(meta.Title), options.query) {
		return true
	}
	for _, alias := range meta.Aliases {
		if strings.Contains(strings.ToLower(alias), options.query) {
			return true
		}
	}
	return false
}

// Choose a small metadata page under the index lock; no store reads or body
// decryption are performed while holding this lock or before the page is known.
func (s *Store) catalogMetas(options catalogOptions) ([]ObjectMeta, bool, error) {
	if s.index == nil {
		return nil, false, errors.New("Catalog metadata index unavailable")
	}
	var anchor ObjectMeta
	if options.cursor != "" {
		decoded, err := decodePageCursor(options.cursor)
		if err != nil || !validObjectReferenceID(decoded.ID) {
			return nil, false, errInvalidCursor
		}
		if _, err = time.Parse(time.RFC3339Nano, decoded.UpdatedAt); err != nil {
			return nil, false, errInvalidCursor
		}
		anchor = ObjectMeta{ID: decoded.ID, UpdatedAt: decoded.UpdatedAt}
	}
	result := make([]ObjectMeta, 0, options.limit)
	more := false
	s.index.readRelations(func(index relationIndex) {
		ids := index.candidates(options.typeID)
		start := 0
		if anchor.ID != "" {
			start = sort.Search(len(ids), func(i int) bool { return metaLess(anchor, index.objects[ids[i]]) })
		}
		for _, id := range ids[start:] {
			meta := index.objects[id]
			if !catalogMatches(meta, options) {
				continue
			}
			if len(result) >= options.limit {
				more = true
				break
			}
			// The source index already owns immutable string values. Clone bounded
			// slices only for this page, not the entire catalog or its payloads.
			projected := meta
			projected.Aliases = append([]string(nil), meta.Aliases...)
			if options.includeRelations {
				projected.Links = nil
				projected.PropertyLinks = nil
				seen := map[string]bool{}
				for _, group := range [][]string{meta.Links, meta.PropertyLinks} {
					for _, target := range group {
						if seen[target] {
							continue
						}
						seen[target] = true
						projected.Links = append(projected.Links, target)
						if len(projected.Links) > catalogRelationLimit {
							break
						}
					}
					if len(projected.Links) > catalogRelationLimit {
						break
					}
				}
			} else {
				projected.Links = nil
				projected.PropertyLinks = nil
			}
			result = append(result, projected)
		}
	})
	return result, more, nil
}

func catalogExcerpt(text string, limit int) (string, bool) {
	text = strings.TrimSpace(text)
	count := 0
	for index := range text {
		if count == limit {
			return strings.Clone(text[:index]), true
		}
		count++
	}
	return strings.Clone(text), false
}

func catalogEntry(meta ObjectMeta, options catalogOptions) CatalogEntry {
	entry := CatalogEntry{ID: meta.ID, TypeID: meta.TypeID, Title: meta.Title, Aliases: meta.Aliases, IsTemplate: meta.IsTemplate, CreatedAt: meta.CreatedAt, UpdatedAt: meta.UpdatedAt, Revision: meta.Revision, Incarnation: meta.Incarnation}
	if options.includeRelations {
		seen := map[string]bool{}
		for _, group := range [][]string{meta.Links, meta.PropertyLinks} {
			for _, id := range group {
				if seen[id] {
					continue
				}
				seen[id] = true
				if len(entry.RelatedIDs) >= catalogRelationLimit {
					entry.RelationsTruncated = true
					return entry
				}
				entry.RelatedIDs = append(entry.RelatedIDs, id)
			}
		}
	}
	return entry
}

func (a *App) listCatalog(w http.ResponseWriter, r *http.Request) {
	options, err := parseCatalogOptions(r)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	metas, more, err := a.store.catalogMetas(options)
	if err != nil {
		if errors.Is(err, errInvalidCursor) {
			jsonError(w, 400, err.Error())
		} else {
			jsonError(w, 503, "Could not read catalog metadata")
		}
		return
	}
	response := catalogResponse{Projection: "catalog/v1", Entries: []CatalogEntry{}, RequestedProperties: options.properties, HasMore: more}
	bytesUsed := 1024
	var last *ObjectMeta
	for i, meta := range metas {
		entry := catalogEntry(meta, options)
		if options.excerptChars > 0 || len(options.properties) > 0 {
			object, err := a.store.Object(meta.ID)
			if errors.Is(err, sql.ErrNoRows) {
				last = &metas[i]
				continue
			}
			if err != nil {
				jsonError(w, 500, "Could not read requested catalog fields")
				return
			}
			fresh := metaFromObject(object)
			if !catalogMatches(fresh, options) {
				last = &metas[i]
				continue
			}
			entry = catalogEntry(fresh, options)
			if options.excerptChars > 0 {
				entry.Excerpt, entry.ExcerptTruncated = catalogExcerpt(object.Content, options.excerptChars)
			}
			if len(options.properties) > 0 {
				entry.Properties = map[string]any{}
			}
			for _, key := range options.properties {
				value, ok := object.Properties[key]
				if !ok {
					continue
				}
				encoded, err := json.Marshal(value)
				if err != nil {
					jsonError(w, 500, "Could not project catalog property")
					return
				}
				if len(encoded) > catalogPropertyBytes {
					entry.OmittedProperties = append(entry.OmittedProperties, key)
					continue
				}
				var detached any
				if err = json.Unmarshal(encoded, &detached); err != nil {
					jsonError(w, 500, "Could not project catalog property")
					return
				}
				entry.Properties[key] = detached
			}
		}
		encoded, err := json.Marshal(entry)
		if err != nil {
			jsonError(w, 500, "Could not encode catalog entry")
			return
		}
		if len(response.Entries) > 0 && bytesUsed+len(encoded) > catalogPageBytes {
			response.HasMore = true
			break
		}
		response.Entries = append(response.Entries, entry)
		bytesUsed += len(encoded)
		last = &metas[i]
	}
	if response.HasMore && last != nil {
		response.NextCursor, _ = encodePageAnchor(pageAnchor{UpdatedAt: last.UpdatedAt, ID: last.ID})
	}
	jsonResponse(w, 200, response)
}
