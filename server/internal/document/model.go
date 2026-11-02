// Package document defines the platform-independent canonical Lumen document.
package document

import (
	"bytes"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"net/url"
	"regexp"
	"slices"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"
)

const Version = 2
const MaxBytes = 8 << 20
const MaxNodes = 20000
const MaxDepth = 32

type Document struct {
	SchemaVersion int    `json:"schema_version"`
	Blocks        []Node `json:"blocks"`
}
type Node struct {
	ID       string         `json:"id"`
	Type     string         `json:"type"`
	Attrs    map[string]any `json:"attrs,omitempty"`
	Content  []Inline       `json:"content,omitempty"`
	Children []Node         `json:"children,omitempty"`
}
type Inline struct {
	Type     string         `json:"type"`
	Text     string         `json:"text,omitempty"`
	Marks    map[string]any `json:"marks,omitempty"`
	Href     string         `json:"href,omitempty"`
	ObjectID string         `json:"object_id,omitempty"`
	BlockID  string         `json:"block_id,omitempty"`
	Label    string         `json:"label,omitempty"`
	Latex    string         `json:"latex,omitempty"`
	Attrs    map[string]any `json:"attrs,omitempty"`
}
type Reference struct {
	Kind     string `json:"kind"`
	NodeID   string `json:"node_id"`
	ObjectID string `json:"object_id,omitempty"`
	BlockID  string `json:"block_id,omitempty"`
	AssetID  string `json:"asset_id,omitempty"`
	Value    string `json:"value,omitempty"`
}
type Error struct {
	Path    string `json:"path"`
	Message string `json:"message"`
}

func (e *Error) Error() string    { return e.Path + ": " + e.Message }
func fail(path, msg string) error { return &Error{path, msg} }

func Empty() Document { return Document{SchemaVersion: Version, Blocks: []Node{}} }

// UnmarshalJSON keeps HTTP and sync decoding as strict as Decode.
func (d *Document) UnmarshalJSON(data []byte) error {
	if !utf8.Valid(data) {
		return fail("$", "document JSON must be valid UTF-8")
	}
	type wire Document
	var fields map[string]json.RawMessage
	if err := json.Unmarshal(data, &fields); err != nil {
		return err
	}
	if fields["schema_version"] == nil || fields["blocks"] == nil || bytes.Equal(bytes.TrimSpace(fields["blocks"]), []byte("null")) {
		return fail("$", "schema_version and blocks array are required")
	}
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.DisallowUnknownFields()
	var out wire
	if err := dec.Decode(&out); err != nil {
		return err
	}
	*d = Document(out)
	return nil
}

// Decode refuses unknown structural fields; extension data belongs in attrs.
func Decode(data []byte) (Document, error) {
	if len(data) > MaxBytes {
		return Document{}, fail("$", "document exceeds 8 MiB")
	}
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.DisallowUnknownFields()
	var d Document
	if err := dec.Decode(&d); err != nil {
		return Document{}, err
	}
	if err := dec.Decode(new(any)); err != io.EOF {
		return Document{}, fail("$", "expected one JSON document")
	}
	return Normalize(d)
}

// Normalize returns an independent canonical copy and never manufactures identities.
func Normalize(d Document) (Document, error) {
	if err := Validate(d); err != nil {
		return Document{}, err
	}
	if d.Blocks == nil {
		d.Blocks = []Node{}
	}
	data, err := json.Marshal(d)
	if err != nil {
		return Document{}, fail("$", "document must contain finite JSON values")
	}
	if len(data) > MaxBytes {
		return Document{}, fail("$", "document exceeds 8 MiB")
	}
	var out Document
	if err = json.Unmarshal(data, &out); err != nil {
		return Document{}, err
	}
	if out.Blocks == nil {
		out.Blocks = []Node{}
	}
	if err = Validate(out); err != nil {
		return Document{}, err
	}
	return out, nil
}
func validID(s string) bool {
	return len(s) > 0 && len(s) <= 128 && utf8.ValidString(s) && strings.IndexFunc(s, func(r rune) bool { return unicode.IsControl(r) || unicode.IsSpace(r) }) < 0
}
func textAttr(m map[string]any, k string) string { s, _ := m[k].(string); return s }
func number(v any) (float64, bool) {
	switch n := v.(type) {
	case float64:
		return n, !math.IsNaN(n) && !math.IsInf(n, 0)
	case int:
		return float64(n), true
	case int64:
		return float64(n), true
	case json.Number:
		f, e := n.Float64()
		return f, e == nil && !math.IsNaN(f) && !math.IsInf(f, 0)
	}
	return 0, false
}
func one(s string, options ...string) bool { return slices.Contains(options, s) }
func requireString(m map[string]any, k, p string, nonempty bool) error {
	v, ok := m[k].(string)
	if !ok || (!utf8.ValidString(v)) || (nonempty && v == "") {
		return fail(p+"."+k, "expected string")
	}
	return nil
}
func optionalBool(m map[string]any, k, p string) error {
	if v, ok := m[k]; ok {
		if _, yes := v.(bool); !yes {
			return fail(p+"."+k, "expected boolean")
		}
	}
	return nil
}
func optionalPositive(m map[string]any, k, p string, integer bool) error {
	if v, ok := m[k]; ok {
		n, yes := number(v)
		if !yes || n <= 0 || (integer && n != math.Trunc(n)) {
			return fail(p+"."+k, "expected positive finite number")
		}
	}
	return nil
}

var color = regexp.MustCompile(`^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$`)

func validURL(s string) bool {
	u, e := url.Parse(s)
	return e == nil && u.Host != "" && one(u.Scheme, "https", "http", "mailto") || strings.HasPrefix(s, "mailto:") && len(s) > 7
}

func Validate(d Document) error {
	if d.SchemaVersion != Version {
		return fail("$.schema_version", "unsupported document version")
	}
	ids := map[string]bool{}
	count := 0
	addID := func(id, path string) error {
		if !validID(id) {
			return fail(path, "expected nonempty identity without whitespace (max 128 bytes)")
		}
		if ids[id] {
			return fail(path, "duplicate identity")
		}
		ids[id] = true
		count++
		if count > MaxNodes {
			return fail(path, "too many nodes")
		}
		return nil
	}
	var visit func([]Node, string, int, string) error
	visit = func(nodes []Node, path string, depth int, parent string) error {
		if depth > MaxDepth {
			return fail(path, "tree exceeds depth limit")
		}
		for i, n := range nodes {
			p := fmt.Sprintf("%s[%d]", path, i)
			if err := addID(n.ID, p+".id"); err != nil {
				return err
			}
			if !one(n.Type, "paragraph", "heading", "list_item", "quote", "group", "toggle", "code", "math", "divider", "image", "object_embed", "columns", "column", "table", "table_row", "table_cell") {
				return fail(p+".type", "unsupported block type")
			}
			if one(n.Type, "column", "table_row", "table_cell") && !((n.Type == "column" && parent == "columns") || (n.Type == "table_row" && parent == "table") || (n.Type == "table_cell" && parent == "table_row")) {
				return fail(p, "structural node has invalid parent")
			}
			if parent == "columns" && n.Type != "column" || parent == "table" && n.Type != "table_row" || parent == "table_row" && n.Type != "table_cell" || parent == "table_cell" && n.Type != "paragraph" {
				return fail(p, "invalid child type for container")
			}
			if len(n.Content) > 0 && !one(n.Type, "paragraph", "heading", "list_item", "quote", "toggle") {
				return fail(p+".content", "block does not allow inline body")
			}
			if len(n.Children) > 0 && one(n.Type, "code", "math", "divider", "image", "object_embed") {
				return fail(p+".children", "atomic block cannot contain children")
			}
			if err := validateAttrs(n, p); err != nil {
				return err
			}
			if n.Type == "table" {
				cols, ok := n.Attrs["columns"].([]any)
				if !ok || len(cols) < 1 || len(cols) > 100 {
					return fail(p+".attrs.columns", "expected 1..100 column definitions")
				}
				for c, raw := range cols {
					m, ok := raw.(map[string]any)
					if !ok {
						return fail(p+".attrs.columns", "invalid column")
					}
					id, _ := m["id"].(string)
					if err := addID(id, fmt.Sprintf("%s.attrs.columns[%d].id", p, c)); err != nil {
						return err
					}
					w, ok := number(m["width"])
					if !ok || w <= 0 {
						return fail(p+".attrs.columns", "column width must be positive")
					}
				}
				if len(n.Children) < 1 {
					return fail(p+".children", "table needs at least one row")
				}
				if len(n.Children)*len(cols) > 10000 {
					return fail(p, "table exceeds 10000 cells")
				}
				for _, r := range n.Children {
					if len(r.Children) != len(cols) {
						return fail(p+".children", "all rows must match column count")
					}
				}
			}
			if n.Type == "columns" && (len(n.Children) < 1 || len(n.Children) > 8) {
				return fail(p+".children", "layout needs 1..8 columns")
			}
			for j, in := range n.Content {
				if err := validateInline(in, fmt.Sprintf("%s.content[%d]", p, j)); err != nil {
					return err
				}
			}
			if err := visit(n.Children, p+".children", depth+1, n.Type); err != nil {
				return err
			}
		}
		return nil
	}
	if err := visit(d.Blocks, "$.blocks", 0, ""); err != nil {
		return err
	}
	data, err := json.Marshal(d)
	if err != nil || len(data) > MaxBytes {
		return fail("$", "invalid or oversized JSON document")
	}
	return nil
}
func validateAttrs(n Node, p string) error {
	a := n.Attrs
	p += ".attrs"
	if v, ok := a["alignment"]; ok && !one(fmt.Sprint(v), "left", "center", "right", "justify") {
		return fail(p+".alignment", "invalid alignment")
	}
	for _, k := range []string{"foreground", "background"} {
		if v, ok := a[k]; ok {
			if s, ok := v.(string); !ok || !color.MatchString(s) {
				return fail(p+"."+k, "expected hex color")
			}
		}
	}
	switch n.Type {
	case "heading":
		v, ok := number(a["level"])
		if !ok || v < 1 || v > 6 || v != math.Trunc(v) {
			return fail(p+".level", "expected heading level 1..6")
		}
	case "list_item":
		if !one(textAttr(a, "kind"), "bullet", "decimal", "alpha", "roman", "task") {
			return fail(p+".kind", "invalid list kind")
		}
		if textAttr(a, "kind") == "task" {
			if _, ok := a["checked"].(bool); !ok {
				return fail(p+".checked", "task requires boolean checked")
			}
		}
		if err := optionalPositive(a, "start", p, true); err != nil {
			return err
		}
		if v, ok := a["case"]; ok && !one(fmt.Sprint(v), "lower", "upper") {
			return fail(p+".case", "invalid letter case")
		}
	case "toggle":
		if _, ok := a["expanded"].(bool); !ok {
			return fail(p+".expanded", "expected explicit boolean")
		}
	case "code", "math":
		if err := requireString(a, "source", p, false); err != nil {
			return err
		}
	case "image":
		if !validID(textAttr(a, "asset_id")) {
			return fail(p+".asset_id", "image requires an asset identity")
		}
		for _, k := range []string{"width", "height"} {
			if err := optionalPositive(a, k, p, false); err != nil {
				return err
			}
		}
		if v, ok := a["fit"]; ok && !one(fmt.Sprint(v), "contain", "cover") {
			return fail(p+".fit", "invalid fit")
		}
		if v, ok := a["caption"]; ok {
			if s, ok := v.(string); !ok || !utf8.ValidString(s) {
				return fail(p+".caption", "expected caption string")
			}
		}
	case "object_embed":
		if value, exists := a["block_id"]; exists {
			id, ok := value.(string)
			if !ok || !validID(id) {
				return fail(p+".block_id", "expected block identity")
			}
		}
		if !validID(textAttr(a, "object_id")) {
			return fail(p+".object_id", "expected object identity")
		}
		if !one(textAttr(a, "view"), "inline", "link_block", "small_card", "wide_card", "embed") {
			return fail(p+".view", "invalid object representation")
		}
	case "column":
		if err := optionalPositive(a, "width", p, false); err != nil {
			return err
		}
	case "table_row":
		if err := optionalBool(a, "header", p); err != nil {
			return err
		}
	}
	return nil
}
func validateInline(in Inline, p string) error {
	if !utf8.ValidString(in.Text) || !utf8.ValidString(in.Label) || !utf8.ValidString(in.Latex) {
		return fail(p, "invalid UTF-8")
	}
	switch in.Type {
	case "text", "break":
	case "link":
		if !validURL(in.Href) {
			return fail(p+".href", "expected absolute http(s) or mailto link")
		}
	case "object_ref", "block_ref":
		if !validID(in.ObjectID) {
			return fail(p+".object_id", "expected object identity")
		}
		if in.Type == "block_ref" && !validID(in.BlockID) {
			return fail(p+".block_id", "expected block identity")
		}
	case "math":
		if in.Latex == "" {
			return fail(p+".latex", "expected formula source")
		}
	case "temporal":
		if err := validateTemporal(in.Attrs, p+".attrs"); err != nil {
			return err
		}
	default:
		return fail(p+".type", "unsupported inline type")
	}
	for k, v := range in.Marks {
		switch k {
		case "bold", "italic", "underline", "strike", "code":
			if _, ok := v.(bool); !ok {
				return fail(p+".marks."+k, "expected boolean")
			}
		case "color", "highlight":
			s, ok := v.(string)
			if !ok || !color.MatchString(s) {
				return fail(p+".marks."+k, "expected hex color")
			}
		case "point_size":
			n, ok := number(v)
			if !ok || n <= 0 || n > 256 {
				return fail(p+".marks.point_size", "expected size in (0,256]")
			}
		default:
			return fail(p+".marks."+k, "unsupported mark")
		}
	}
	return nil
}
func validateTemporal(a map[string]any, p string) error {
	kind, start, end := textAttr(a, "kind"), textAttr(a, "start"), textAttr(a, "end")
	format := "2006-01-02"
	if kind == "datetime" {
		format = time.RFC3339Nano
		zone := textAttr(a, "timezone")
		if zone == "" {
			return fail(p+".timezone", "datetime requires IANA timezone")
		}
		if _, e := time.LoadLocation(zone); e != nil {
			return fail(p+".timezone", "unknown timezone")
		}
	} else if kind != "date" {
		return fail(p+".kind", "expected date or datetime")
	}
	s, e := time.Parse(format, start)
	if e != nil {
		return fail(p+".start", "invalid date/time")
	}
	if _, ok := a["end"]; ok {
		f, e := time.Parse(format, end)
		if e != nil || f.Before(s) {
			return fail(p+".end", "invalid or inverted interval")
		}
	}
	return nil
}
