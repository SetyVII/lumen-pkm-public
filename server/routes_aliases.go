package main

import (
	"net/http"
	"strings"
)

// searchResult keeps the historical GET /v1/search shape (a JSON array of
// objects) and adds the alias that produced the match, so a client can insert
// it as the link text. Embedding Object flattens its fields: clients that only
// read objects keep working unchanged.
type searchResult struct {
	Object
	MatchedAlias string `json:"matched_alias,omitempty"`
}

// objectsForList applies the optional type_id filter shared by the object list
// and the search endpoint.
func (a *App) objectsForList(r *http.Request) ([]Object, error) {
	if typeID := r.URL.Query().Get("type_id"); typeID != "" {
		return a.store.ObjectsByType(typeID)
	}
	return a.store.Objects()
}

// matchesSearchQuery applies the GET /v1/search matcher: a case-insensitive
// substring over title and plain content, then the weblink-reader article,
// then aliases. It returns the alias that produced the match ("" for title,
// content and reader hits) so a client can use it as link text. An empty
// query matches everything, exactly like the HTTP endpoint. The MCP search
// tool (backend stage mcp-server) reuses this matcher.
func matchesSearchQuery(o Object, lowerQuery string) (bool, string) {
	if lowerQuery == "" || strings.Contains(strings.ToLower(o.Title+" "+o.Content), lowerQuery) {
		return true, ""
	}
	// Weblink reader articles are searchable like any other body text.
	if reader, _ := o.Properties["reader_content"].(string); reader != "" {
		if strings.Contains(strings.ToLower(reader), lowerQuery) {
			return true, ""
		}
	}
	if alias := matchAlias(o, lowerQuery); alias != "" {
		return true, alias
	}
	return false, ""
}

// searchObjects serves GET /v1/search. It matches titles, content and aliases.
// MatchedAlias is set only when the alias is the reason an object matched: a
// title or content hit already offers better link text.
func (a *App) searchObjects(w http.ResponseWriter, r *http.Request) {
	query := strings.ToLower(r.URL.Query().Get("q"))
	includeTemplates, err := queryBool(r, "include_templates")
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	objects, err := a.objectsForList(r)
	if err != nil {
		jsonError(w, 500, "Could not read objects")
		return
	}
	results := make([]searchResult, 0, len(objects))
	ordered := make([]Object, 0, len(objects))
	for _, o := range objects {
		if !includeTemplates && isTemplateObject(o) {
			continue
		}
		if matched, alias := matchesSearchQuery(o, query); matched {
			results = append(results, searchResult{Object: o, MatchedAlias: alias})
			ordered = append(ordered, o)
		}
	}
	// Pagination runs after every filter (see pagination.go). A request
	// without limit and cursor keeps the full result list.
	if !writePageSlice(w, r, ordered, func(start, end int) {
		jsonResponse(w, 200, results[start:end])
	}) {
		return
	}
}
