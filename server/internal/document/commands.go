package document

import (
	"bytes"
	"crypto/rand"
	"encoding/json"
	"fmt"
	"math"
	"slices"
	"sort"
	"strconv"
	"strings"
	"unicode/utf8"
)

type Command struct {
	Type      string            `json:"type"`
	NodeID    string            `json:"node_id,omitempty"`
	NodeIDs   []string          `json:"node_ids,omitempty"`
	ParentID  string            `json:"parent_id,omitempty"`
	Position  int               `json:"position,omitempty"`
	Node      *Node             `json:"node,omitempty"`
	Content   []Inline          `json:"content,omitempty"`
	Attrs     map[string]any    `json:"attrs,omitempty"`
	NewIDs    map[string]string `json:"new_ids,omitempty"`
	ColumnID  string            `json:"column_id,omitempty"`
	Width     float64           `json:"width,omitempty"`
	Direction string            `json:"direction,omitempty"`
	Numeric   bool              `json:"numeric,omitempty"`
}

// UnmarshalJSON applies the same strict structural contract to command payloads
// and their nested nodes/inlines as to full document decoding.
func (c *Command) UnmarshalJSON(data []byte) error {
	if !utf8.Valid(data) {
		return fail("$.commands", "command JSON must be valid UTF-8")
	}
	if len(data) > MaxBytes {
		return fail("$.commands", "command exceeds 8 MiB")
	}
	type wire Command
	var out wire
	dec := json.NewDecoder(bytes.NewReader(data))
	dec.DisallowUnknownFields()
	if err := dec.Decode(&out); err != nil {
		return err
	}
	if out.Type == "" {
		return fail("$.commands.type", "command type is required")
	}
	*c = Command(out)
	return nil
}

func locate(ns *[]Node, id string) (*[]Node, int) {
	for i := range *ns {
		if (*ns)[i].ID == id {
			return ns, i
		}
		if a, j := locate(&(*ns)[i].Children, id); a != nil {
			return a, j
		}
	}
	return nil, -1
}
func children(d *Document, parent string) (*[]Node, error) {
	if parent == "" {
		return &d.Blocks, nil
	}
	s, i := locate(&d.Blocks, parent)
	if s == nil {
		return nil, fmt.Errorf("parent %q not found", parent)
	}
	return &(*s)[i].Children, nil
}
func splice(s *[]Node, pos int, nodes []Node) error {
	if pos < 0 || pos > len(*s) {
		return fmt.Errorf("position outside container")
	}
	out := slices.Clone((*s)[:pos])
	out = append(out, nodes...)
	out = append(out, (*s)[pos:]...)
	*s = out
	return nil
}
func remove(s *[]Node, i int) Node { n := (*s)[i]; *s = slices.Delete(*s, i, i+1); return n }
func isDesc(n Node, id string) bool {
	if n.ID == id {
		return true
	}
	for _, c := range n.Children {
		if isDesc(c, id) {
			return true
		}
	}
	return false
}
func newID(ids map[string]string, old string) string {
	if v := ids[old]; v != "" {
		return v
	}
	return rand.Text()
}
func renew(n *Node, ids map[string]string) {
	old := n.ID
	n.ID = newID(ids, old)
	if n.Type == "table" {
		if cols, ok := n.Attrs["columns"].([]any); ok {
			for _, raw := range cols {
				m := raw.(map[string]any)
				m["id"] = newID(ids, textAttr(m, "id"))
			}
		}
	}
	for i := range n.Children {
		renew(&n.Children[i], ids)
	}
}

// Apply is an all-or-nothing transaction on an independent copy. Position for move
// is interpreted in the destination AFTER selected roots have been removed.
func Apply(d Document, commands []Command) (Document, error) {
	out, err := Normalize(d)
	if err != nil {
		return Document{}, err
	}
	if len(commands) > 1000 {
		return Document{}, fail("$.commands", "at most 1000 commands")
	}
	rawCommands, err := json.Marshal(commands)
	if err != nil {
		return Document{}, fail("$.commands", "commands must contain finite JSON values")
	}
	if len(rawCommands) > MaxBytes {
		return Document{}, fail("$.commands", "command batch exceeds 8 MiB")
	}
	if err = json.Unmarshal(rawCommands, &commands); err != nil {
		return Document{}, err
	}
	for i, c := range commands {
		if len(c.NodeIDs) > MaxNodes || len(c.NewIDs) > MaxNodes {
			return Document{}, fail(fmt.Sprintf("$.commands[%d]", i), "selection or identity map exceeds node limit")
		}
		if err = applyOne(&out, c); err != nil {
			return Document{}, fail(fmt.Sprintf("$.commands[%d]", i), err.Error())
		}
		if err = Validate(out); err != nil {
			return Document{}, fail(fmt.Sprintf("$.commands[%d]", i), err.Error())
		}
	}
	return Normalize(out)
}
func applyOne(d *Document, c Command) error {
	switch c.Type {
	case "append":
		if c.Node == nil {
			return fmt.Errorf("append requires node")
		}
		target, e := children(d, c.ParentID)
		if e != nil {
			return e
		}
		return splice(target, len(*target), []Node{*c.Node})
	case "append_section":
		if c.Node == nil || c.Node.Type != "heading" {
			return fmt.Errorf("append_section requires heading with child entries")
		}
		heading := *c.Node
		entries := heading.Children
		heading.Children = nil
		title := nodeText(heading)
		pos := len(d.Blocks)
		found := false
		level, _ := number(heading.Attrs["level"])
		for i, n := range d.Blocks {
			if n.Type != "heading" {
				continue
			}
			l, _ := number(n.Attrs["level"])
			if found && l <= level {
				pos = i
				break
			}
			if nodeText(n) == title {
				found = true
				level = l
			}
		}
		if !found {
			entries = append([]Node{heading}, entries...)
		}
		return splice(&d.Blocks, pos, entries)
	case "insert":
		if c.Node == nil {
			return fmt.Errorf("insert requires node")
		}
		s, e := children(d, c.ParentID)
		if e != nil {
			return e
		}
		return splice(s, c.Position, []Node{*c.Node})
	case "move", "copy", "delete":
		ids := c.NodeIDs
		if len(ids) == 0 && c.NodeID != "" {
			ids = []string{c.NodeID}
		}
		if len(ids) == 0 {
			return fmt.Errorf("selection required")
		}
		chosen := map[string]bool{}
		for _, id := range ids {
			if chosen[id] {
				continue
			}
			chosen[id] = true
			if s, _ := locate(&d.Blocks, id); s == nil {
				return fmt.Errorf("node %q not found", id)
			}
		}
		roots := []Node{}
		var collect func([]Node)
		collect = func(ns []Node) {
			for _, n := range ns {
				if chosen[n.ID] {
					roots = append(roots, n)
				} else {
					collect(n.Children)
				}
			}
		}
		collect(d.Blocks)
		if c.Type != "delete" {
			for _, n := range roots {
				if isDesc(n, c.ParentID) {
					return fmt.Errorf("destination is within selected subtree")
				}
			}
		}
		if c.Type == "copy" {
			for i := range roots {
				data, _ := json.Marshal(roots[i])
				var copied Node
				_ = json.Unmarshal(data, &copied)
				roots[i] = copied
				renew(&roots[i], c.NewIDs)
			}
		} else {
			for _, n := range roots {
				s, i := locate(&d.Blocks, n.ID)
				remove(s, i)
			}
		}
		if c.Type == "delete" {
			return nil
		}
		s, e := children(d, c.ParentID)
		if e != nil {
			return e
		}
		return splice(s, c.Position, roots)
	case "replace_inline", "set_attrs", "replace", "wrap", "unwrap", "flatten_columns":
		s, i := locate(&d.Blocks, c.NodeID)
		if s == nil {
			return fmt.Errorf("node not found")
		}
		n := &(*s)[i]
		switch c.Type {
		case "replace_inline":
			n.Content = c.Content
		case "set_attrs":
			if n.Attrs == nil {
				n.Attrs = map[string]any{}
			}
			for k, v := range c.Attrs {
				if v == nil {
					delete(n.Attrs, k)
				} else {
					n.Attrs[k] = v
				}
			}
		case "replace":
			if c.Node == nil || c.Node.ID != n.ID {
				return fmt.Errorf("replacement must retain node identity")
			}
			*n = *c.Node
		case "wrap":
			if c.Node == nil || len(c.Node.Children) > 0 {
				return fmt.Errorf("wrap requires empty container node")
			}
			wrapper := *c.Node
			wrapper.Children = []Node{*n}
			*n = wrapper
		case "unwrap":
			if !one(n.Type, "group", "quote", "column") {
				return fmt.Errorf("only group, quote or column can be unwrapped")
			}
			if len(n.Content) > 0 {
				return fmt.Errorf("cannot unwrap nonempty inline title")
			}
			ns := n.Children
			remove(s, i)
			return splice(s, i, ns)
		case "flatten_columns":
			if n.Type != "columns" {
				return fmt.Errorf("expected columns")
			}
			ns := []Node{}
			for _, col := range n.Children {
				ns = append(ns, col.Children...)
			}
			remove(s, i)
			return splice(s, i, ns)
		}
		return nil
	case "table_insert_row", "table_delete_row", "table_move_row", "table_insert_column", "table_delete_column", "table_move_column", "table_set_width", "table_sort":
		return tableCommand(d, c)
	default:
		return fmt.Errorf("unsupported command %q", c.Type)
	}
}
func tableCommand(d *Document, c Command) error {
	s, i := locate(&d.Blocks, c.NodeID)
	if s == nil || (*s)[i].Type != "table" {
		return fmt.Errorf("expected table")
	}
	t := &(*s)[i]
	cols := t.Attrs["columns"].([]any)
	col := -1
	for j, r := range cols {
		if textAttr(r.(map[string]any), "id") == c.ColumnID {
			col = j
		}
	}
	switch c.Type {
	case "table_insert_row":
		if c.Node == nil || c.Node.Type != "table_row" {
			return fmt.Errorf("provide complete table_row node")
		}
		return splice(&t.Children, c.Position, []Node{*c.Node})
	case "table_delete_row", "table_move_row":
		row := -1
		for j, r := range t.Children {
			if r.ID == c.ParentID {
				row = j
			}
		}
		if row < 0 {
			return fmt.Errorf("row identity not found (parent_id)")
		}
		n := remove(&t.Children, row)
		if c.Type == "table_move_row" {
			return splice(&t.Children, c.Position, []Node{n})
		}
		return nil
	case "table_insert_column":
		if !validID(c.ColumnID) || c.Width <= 0 {
			return fmt.Errorf("provide new column_id and width")
		}
		if c.Position < 0 || c.Position > len(cols) {
			return fmt.Errorf("column position outside table")
		}
		defs := slices.Clone(cols[:c.Position])
		defs = append(defs, map[string]any{"id": c.ColumnID, "width": c.Width})
		defs = append(defs, cols[c.Position:]...)
		t.Attrs["columns"] = defs
		for r := range t.Children {
			row := &t.Children[r]
			cell := Node{ID: newID(c.NewIDs, row.ID+"/cell"), Type: "table_cell", Children: []Node{{ID: newID(c.NewIDs, row.ID+"/paragraph"), Type: "paragraph"}}}
			if err := splice(&row.Children, c.Position, []Node{cell}); err != nil {
				return err
			}
		}
		return nil
	case "table_delete_column", "table_move_column", "table_set_width", "table_sort":
		if col < 0 {
			return fmt.Errorf("column not found")
		}
	}
	switch c.Type {
	case "table_set_width":
		cols[col].(map[string]any)["width"] = c.Width
	case "table_delete_column":
		t.Attrs["columns"] = slices.Delete(cols, col, col+1)
		for r := range t.Children {
			remove(&t.Children[r].Children, col)
		}
	case "table_move_column":
		v := cols[col]
		cols = slices.Delete(cols, col, col+1)
		if c.Position < 0 || c.Position > len(cols) {
			return fmt.Errorf("column position outside table")
		}
		out := slices.Clone(cols[:c.Position])
		out = append(out, v)
		out = append(out, cols[c.Position:]...)
		t.Attrs["columns"] = out
		for r := range t.Children {
			cell := remove(&t.Children[r].Children, col)
			if err := splice(&t.Children[r].Children, c.Position, []Node{cell}); err != nil {
				return err
			}
		}
	case "table_sort":
		if !one(c.Direction, "asc", "desc") {
			return fmt.Errorf("direction must be asc or desc")
		}
		headers, body := []Node{}, []Node{}
		for _, r := range t.Children {
			if h, _ := r.Attrs["header"].(bool); h {
				headers = append(headers, r)
			} else {
				body = append(body, r)
			}
		}
		if c.Numeric {
			for _, r := range body {
				v := strings.TrimSpace(nodeText(r.Children[col]))
				if v != "" {
					if x, e := strconv.ParseFloat(v, 64); e != nil || math.IsNaN(x) || math.IsInf(x, 0) {
						return fmt.Errorf("numeric sort contains nonnumeric cell")
					}
				}
			}
		}
		sort.SliceStable(body, func(i, j int) bool {
			a, b := strings.TrimSpace(nodeText(body[i].Children[col])), strings.TrimSpace(nodeText(body[j].Children[col]))
			if a == "" || b == "" {
				return a != "" && b == ""
			}
			cmp := strings.Compare(strings.ToLower(a), strings.ToLower(b))
			if c.Numeric {
				x, _ := strconv.ParseFloat(a, 64)
				y, _ := strconv.ParseFloat(b, 64)
				cmp = 0
				if x < y {
					cmp = -1
				} else if x > y {
					cmp = 1
				}
			}
			if c.Direction == "desc" {
				return cmp > 0
			}
			return cmp < 0
		})
		t.Children = append(headers, body...)
	}
	return nil
}
