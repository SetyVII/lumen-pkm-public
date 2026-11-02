package main

import (
	"encoding/base64"
	"encoding/json"
	"errors"
	"net/http"
	"strconv"
)


const (
	defaultPageLimit = 50
	maxPageLimit     = 200
	nextCursorHeader = "X-Lumen-Next-Cursor"
)

var errInvalidCursor = errors.New("The cursor is invalid")

// pageAnchor is the decoded cursor: the last object of the previous page,
// identified by its position fields in the display order.
type pageAnchor struct {
	UpdatedAt string `json:"updated_at"`
	ID        string `json:"id"`
}

func encodePageAnchor(anchor pageAnchor) (string, error) {
	raw, err := json.Marshal(anchor)
	if err != nil {
		return "", err
	}
	return base64.RawURLEncoding.EncodeToString(raw), nil
}

// decodePageCursor treats the cursor as opaque: only the server produces and
// consumes the encoding, and a malformed value is a client error.
func decodePageCursor(raw string) (pageAnchor, error) {
	if raw == "" {
		return pageAnchor{}, errInvalidCursor
	}
	decoded, err := base64.RawURLEncoding.DecodeString(raw)
	if err != nil {
		return pageAnchor{}, errInvalidCursor
	}
	var anchor pageAnchor
	if json.Unmarshal(decoded, &anchor) != nil || anchor.ID == "" || anchor.UpdatedAt == "" {
		return pageAnchor{}, errInvalidCursor
	}
	return anchor, nil
}

// pageWindow computes the [start,end) slice of the current page over an
// already filtered, display-ordered object list, plus the next-cursor header
// value ("" when this is the last page). Pagination runs after every filter,
// so limit/cursor compose with the query-filter grammar.
func pageWindow(r *http.Request, ordered []Object) (start, end int, nextCursor string, err error) {
	query := r.URL.Query()
	return pageWindowParams(query.Get("limit"), query.Get("cursor"), ordered)
}

// pageWindowParams is pageWindow without the HTTP request: the MCP tools
// (backend stage mcp-server) page through the same keyset cursors as the
// REST endpoints, so a cursor obtained from one surface works on the other.
func pageWindowParams(rawLimit, rawCursor string, ordered []Object) (start, end int, nextCursor string, err error) {
	if rawLimit == "" && rawCursor == "" {
		return 0, len(ordered), "", nil
	}
	limit := defaultPageLimit
	if rawLimit != "" {
		parsed, parseErr := strconv.Atoi(rawLimit)
		if parseErr != nil || parsed < 1 || parsed > maxPageLimit {
			return 0, 0, "", filterError{message: "limit must be an integer between 1 and " + strconv.Itoa(maxPageLimit)}
		}
		limit = parsed
	}
	start = 0
	if rawCursor != "" {
		anchor, cursorErr := decodePageCursor(rawCursor)
		if cursorErr != nil {
			return 0, 0, "", cursorErr
		}
		// Keyset skip: everything at or before the anchor was already
		// delivered. The anchor object itself may be deleted or filtered out
		// by now; only its position in the order matters, so paging never
		// gets stuck on a missing row.
		for start < len(ordered) && !objectLess(anchorObject(anchor), ordered[start]) {
			start++
		}
	}
	if start >= len(ordered) {
		return start, start, "", nil
	}
	end = start + limit
	if end > len(ordered) {
		end = len(ordered)
	}
	if end < len(ordered) {
		last := ordered[end-1]
		if nextCursor, err = encodePageAnchor(pageAnchor{UpdatedAt: last.UpdatedAt, ID: last.ID}); err != nil {
			return 0, 0, "", err
		}
	}
	return start, end, nextCursor, nil
}

// writePageSlice applies the page window to the response: it slices the
// ordered results, sets the next-cursor header when more pages exist and
// reports parameter errors as HTTP 400.
func writePageSlice(w http.ResponseWriter, r *http.Request, ordered []Object, slice func(start, end int)) bool {
	start, end, nextCursor, err := pageWindow(r, ordered)
	if err != nil {
		jsonError(w, 400, err.Error())
		return false
	}
	if nextCursor != "" {
		w.Header().Set(nextCursorHeader, nextCursor)
	}
	slice(start, end)
	return true
}

func anchorObject(anchor pageAnchor) Object {
	return Object{ID: anchor.ID, UpdatedAt: anchor.UpdatedAt}
}
