package main

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"fmt"
	"strings"

	"lumen/server/internal/document"
)

// canonicalDocumentFromLegacy is the ingestion boundary for existing text,
// Markdown and tool payloads. It is not a second persisted representation.
func canonicalDocumentFromLegacy(o Object) (document.Document, error) {
	if o.Document != nil {
		return document.Normalize(*o.Document)
	}
	var blocks []Block
	if raw := o.Properties["blocks"]; raw != nil {
		data, e := json.Marshal(raw)
		if e != nil {
			return document.Document{}, e
		}
		if e = json.Unmarshal(data, &blocks); e != nil {
			return document.Document{}, e
		}
	}
	if len(blocks) == 0 && o.Content != "" {
		var e error
		blocks, e = MarkdownToBlocks(o.Content)
		if e != nil {
			return document.Document{}, e
		}
	}
	return canonicalDocumentFromBlocks(blocks)
}
func canonicalDocumentFromBlocks(blocks []Block) (document.Document, error) {
	return document.Normalize(document.Document{SchemaVersion: document.Version, Blocks: canonicalNodes(blocks)})
}
func canonicalNodes(blocks []Block) []document.Node {
	out := []document.Node{}
	for bi := 0; bi < len(blocks); bi++ {
		b := blocks[bi]
		n := document.Node{ID: b.ID, Type: b.Type, Attrs: map[string]any{}}
		if n.ID == "" {
			n.ID = newID()
		}
		n.Content = canonicalInlines(b.Runs)
		if len(n.Content) == 0 && b.Text != "" {
			n.Content = []document.Inline{{Type: "text", Text: strings.ReplaceAll(b.Text, lineSeparator, "\n")}}
		}
		switch b.Type {
		case blockSmall:
			n.Type = "paragraph"
			n.Attrs["small"] = true
		case blockHeading:
			n.Attrs["level"] = b.Level
		case blockBullet, blockNumbered, blockAlphabetic, blockTodo:
			n.Type = "list_item"
			kind := "bullet"
			if b.Type == blockNumbered {
				kind = "decimal"
			}
			if b.Type == blockAlphabetic {
				kind = "alpha"
			}
			if b.Type == blockTodo {
				kind = "task"
				n.Attrs["checked"] = strings.HasPrefix(b.Text, todoDone)
				n.Content = stripCanonicalPrefix(n.Content, 2)
			}
			n.Attrs["kind"] = kind
		case blockToggle:
			n.Attrs["expanded"] = !strings.HasPrefix(b.Text, toggleClosed)
			if strings.HasPrefix(b.Text, toggleOpen) || strings.HasPrefix(b.Text, toggleClosed) {
				n.Content = stripCanonicalPrefix(n.Content, 2)
			}
		case blockCode:
			n.Content = nil
			n.Attrs["source"] = strings.ReplaceAll(b.Text, lineSeparator, "\n")
			n.Attrs["language"] = b.Language
		case blockMath:
			n.Content = nil
			source := b.Text
			for _, r := range b.Runs {
				if r.Latex != "" {
					source = r.Latex
					break
				}
			}
			if source == objectReplacement {
				source = ""
			}
			n.Attrs["source"] = source
		case blockDivider:
			n.Content = nil
		case blockGroup:
			n.Content = nil
		case blockEmbed:
			n.Content = nil
			n.Type = "object_embed"
			target := ""
			for _, r := range b.Runs {
				if r.ObjectID != "" {
					target = r.ObjectID
					break
				}
				if strings.HasPrefix(r.Href, objectHrefPrefix) {
					target = strings.TrimPrefix(r.Href, objectHrefPrefix)
					break
				}
			}
			if target == "" {
				n.Type = "paragraph"
				n.Content = canonicalInlines(b.Runs)
			} else {
				n.Attrs["object_id"] = target
				n.Attrs["view"] = "embed"
			}
		case blockColumns:
			n.Content = nil
			for _, col := range b.Columns {
				n.Children = append(n.Children, document.Node{ID: newID(), Type: "column", Attrs: map[string]any{"width": 1}, Children: canonicalNodes(col)})
			}
		case blockTable:
			n.Content = nil
			defs := []any{}
			width := 0
			for _, r := range b.Rows {
				if len(r) > width {
					width = len(r)
				}
			}
			if width == 0 {
				continue
			}
			for i := 0; i < width; i++ {
				defs = append(defs, map[string]any{"id": newID(), "width": 160})
			}
			n.Attrs["columns"] = defs
			for ri, row := range b.Rows {
				r := document.Node{ID: newID(), Type: "table_row", Attrs: map[string]any{"header": ri == 0}}
				for ci := 0; ci < width; ci++ {
					runs := []document.Inline{}
					if ci < len(row) {
						runs = canonicalInlines(row[ci].Runs)
					}
					p := document.Node{ID: newID(), Type: "paragraph", Content: runs}
					if ci < len(b.Align) && b.Align[ci] != "" {
						p.Attrs = map[string]any{"alignment": b.Align[ci]}
					}
					r.Children = append(r.Children, document.Node{ID: newID(), Type: "table_cell", Children: []document.Node{p}})
				}
				n.Children = append(n.Children, r)
			}
		default:
			if !oneCanonicalType(n.Type) {
				n.Type = "paragraph"
			}
		}
		if b.Type != blockColumns && b.Type != blockTable {
			n.Children = canonicalNodes(b.Children)
		}
		if n.Type == "list_item" {
			level := b.Indent
			if level < 1 {
				level = 1
			}
			end := bi + 1
			for end < len(blocks) {
				next := blocks[end]
				if next.Type != blockBullet && next.Type != blockNumbered && next.Type != blockAlphabetic && next.Type != blockTodo {
					break
				}
				if next.Indent <= level {
					break
				}
				end++
			}
			if end > bi+1 {
				n.Children = append(n.Children, canonicalNodes(blocks[bi+1:end])...)
				bi = end - 1
			}
		}
		out = append(out, n)
	}
	return out
}
func oneCanonicalType(t string) bool { return t == "paragraph" || t == "quote" }
func stripCanonicalPrefix(in []document.Inline, count int) []document.Inline {
	for i := range in {
		r := []rune(in[i].Text)
		take := min(count, len(r))
		in[i].Text = string(r[take:])
		count -= take
		if count == 0 {
			break
		}
	}
	return in
}
func canonicalInlines(runs []Run) []document.Inline {
	out := []document.Inline{}
	for _, r := range runs {
		in := document.Inline{Type: "text", Text: strings.ReplaceAll(r.Text, lineSeparator, "\n"), Marks: map[string]any{}}
		for k, v := range map[string]bool{"bold": r.Bold, "italic": r.Italic, "underline": r.Underline, "strike": r.Strike, "code": r.Code} {
			if v {
				in.Marks[k] = true
			}
		}
		if r.Highlight {
			in.Marks["highlight"] = "#fff59d"
		}
		switch {
		case r.Type == "math" && r.Latex != "":
			in.Type = "math"
			in.Latex = r.Latex
			in.Text = ""
		case r.ObjectID != "" || strings.HasPrefix(r.Href, objectHrefPrefix):
			in.Type = "object_ref"
			in.ObjectID = r.ObjectID
			if in.ObjectID == "" {
				in.ObjectID = strings.TrimPrefix(r.Href, objectHrefPrefix)
			}
			in.Label = strings.ReplaceAll(in.Text, objectReplacement, "")
			if in.ObjectID == "" {
				in.Type = "text"
				in.Text = in.Label
				in.Label = ""
			} else {
				in.Text = ""
			}
		case strings.HasPrefix(r.Href, blockHrefPrefix):
			// Old links did not identify the source object. Keep them as readable text;
			// the canonical block_ref requires an unambiguous object+block pair.
			in.Text = strings.TrimSpace(in.Text + " " + r.Href)
		case strings.HasPrefix(r.Href, "https://") || strings.HasPrefix(r.Href, "http://") || strings.HasPrefix(r.Href, "mailto:"):
			in.Type = "link"
			in.Href = r.Href
		case r.Type == "image":
			if strings.HasPrefix(r.ImageName, "lumen://image/") {
				in.Type = "object_ref"
				in.ObjectID = strings.TrimPrefix(r.ImageName, "lumen://image/")
				in.Label = "Imagen"
				in.Text = ""
			} else if strings.HasPrefix(r.ImageName, "https://") || strings.HasPrefix(r.ImageName, "http://") {
				in.Type = "link"
				in.Href = r.ImageName
				in.Text = "Imagen"
			}
		}
		if in.Text == objectReplacement {
			in.Text = ""
		}
		out = append(out, in)
	}
	return out
}
func setCanonicalDocument(o *Object, d document.Document) error {
	n, e := document.Normalize(d)
	if e != nil {
		return e
	}
	o.Document = &n
	o.Content = document.PlainText(n)
	if o.Properties != nil {
		delete(o.Properties, "blocks")
		delete(o.Properties, "rich_text")
	}
	return nil
}
func appendCanonicalMarkdown(o *Object, markdown string, position *int) error {
	if strings.TrimSpace(markdown) == "" {
		return fmt.Errorf("Provide markdown")
	}
	d, e := canonicalDocumentFromLegacy(*o)
	if e != nil {
		return e
	}
	b, e := MarkdownToBlocks(markdown)
	if e != nil {
		return e
	}
	addition, e := canonicalDocumentFromBlocks(b)
	if e != nil {
		return e
	}
	pos := len(d.Blocks)
	if position != nil {
		pos = *position
	}
	if pos < 0 || pos > len(d.Blocks) {
		return fmt.Errorf("position outside document")
	}
	nodes := append([]document.Node{}, d.Blocks[:pos]...)
	nodes = append(nodes, addition.Blocks...)
	nodes = append(nodes, d.Blocks[pos:]...)
	d.Blocks = nodes
	return setCanonicalDocument(o, d)
}

// documentMarkdownBlocks is a deliberately reduced projection for Markdown
// export/tools. It never becomes the source for editing an existing document.
func documentMarkdownBlocks(d document.Document) []Block { return projectedBlocks(d.Blocks, 1) }
func projectedBlocks(ns []document.Node, indent int) []Block {
	out := []Block{}
	for _, n := range ns {
		b := Block{ID: n.ID, Type: n.Type, Runs: projectedRuns(n.Content)}
		b.Text = runsText(b.Runs)
		switch n.Type {
		case "heading":
			if v, ok := n.Attrs["level"].(float64); ok {
				b.Level = int(v)
			}
		case "list_item":
			b.Indent = indent
			kind, _ := n.Attrs["kind"].(string)
			switch kind {
			case "decimal", "roman":
				b.Type = blockNumbered
			case "alpha":
				b.Type = blockAlphabetic
			case "task":
				b.Type = blockTodo
				prefix := todoOpen
				if checked, _ := n.Attrs["checked"].(bool); checked {
					prefix = todoDone
				}
				b.Runs = append([]Run{{Text: prefix}}, b.Runs...)
			default:
				b.Type = blockBullet
			}
			b.Text = runsText(b.Runs)
		case "code", "math":
			b.Text, _ = n.Attrs["source"].(string)
			b.Runs = []Run{{Text: b.Text}}
			b.Language, _ = n.Attrs["language"].(string)
		case "toggle":
			prefix := toggleClosed
			if expanded, _ := n.Attrs["expanded"].(bool); expanded {
				prefix = toggleOpen
			}
			b.Runs = append([]Run{{Text: prefix}}, b.Runs...)
			b.Text = runsText(b.Runs)
		case "object_embed":
			id, _ := n.Attrs["object_id"].(string)
			b.Type = blockEmbed
			b.Runs = []Run{{Text: id, ObjectID: id, Href: objectHrefPrefix + id}}
			b.Text = id
		case "image":
			id, _ := n.Attrs["object_id"].(string)
			asset, _ := n.Attrs["asset_id"].(string)
			caption, _ := n.Attrs["caption"].(string)
			if id != "" {
				asset = "lumen://image/" + id
			}
			b.Type = blockParagraph
			b.Runs = []Run{{Text: caption, Type: "image", ObjectID: id, ImageName: asset}}
			b.Text = caption
		case "columns":
			b.Runs = []Run{}
			b.Text = ""
			for _, c := range n.Children {
				b.Columns = append(b.Columns, projectedBlocks(c.Children, indent))
			}
		case "table":
			b.Runs = []Run{}
			b.Text = ""
			for _, row := range n.Children {
				r := []BlockCell{}
				for _, cell := range row.Children {
					runs := []Run{}
					for i, p := range cell.Children {
						if i > 0 {
							runs = append(runs, Run{Text: "\n"})
						}
						runs = append(runs, projectedRuns(p.Content)...)
					}
					r = append(r, BlockCell{Runs: runs})
				}
				b.Rows = append(b.Rows, r)
			}
		}
		if n.Type != "columns" && n.Type != "table" {
			childIndent := indent
			if n.Type == "list_item" {
				childIndent++
			}
			b.Children = projectedBlocks(n.Children, childIndent)
		}
		out = append(out, b)
	}
	return out
}
func projectedRuns(ins []document.Inline) []Run {
	out := []Run{}
	for _, in := range ins {
		r := Run{Text: in.Text}
		for k, v := range in.Marks {
			b, _ := v.(bool)
			switch k {
			case "bold":
				r.Bold = b
			case "italic":
				r.Italic = b
			case "underline":
				r.Underline = b
			case "strike":
				r.Strike = b
			case "code":
				r.Code = b
			case "highlight":
				r.Highlight = true
			}
		}
		switch in.Type {
		case "break":
			r.Text = "\n"
		case "link":
			r.Href = in.Href
		case "object_ref":
			r.Text = in.Label
			if r.Text == "" {
				r.Text = in.ObjectID
			}
			r.ObjectID = in.ObjectID
			r.Href = objectHrefPrefix + in.ObjectID
		case "block_ref":
			r.Text = in.Label
			if r.Text == "" {
				r.Text = in.BlockID
			}
			r.Href = blockHrefPrefix + in.BlockID
		case "math":
			r.Text = in.Latex
			r.Type = "math"
			r.Latex = in.Latex
		case "temporal":
			r.Text, _ = in.Attrs["start"].(string)
			if end, _ := in.Attrs["end"].(string); end != "" {
				r.Text += " - " + end
			}
		}
		out = append(out, r)
	}
	return out
}

func freshCanonicalDocument(d document.Document) document.Document {
	copied := cloneDocument(&d)
	if copied == nil {
		return document.Empty()
	}
	var renew func([]document.Node)
	renew = func(ns []document.Node) {
		for i := range ns {
			ns[i].ID = newID()
			if ns[i].Type == "table" {
				if cols, ok := ns[i].Attrs["columns"].([]any); ok {
					for _, col := range cols {
						col.(map[string]any)["id"] = newID()
					}
				}
			}
			renew(ns[i].Children)
		}
	}
	renew(copied.Blocks)
	return *copied
}
func rewriteCanonicalObjectReferences(o *Object, from, to string) bool {
	return rewriteCanonicalMergedReferences(o, from, to, nil)
}

func rewriteCanonicalMergedReferences(o *Object, from, to string, blockIDs map[string]string) bool {
	if o.Document == nil {
		return false
	}
	changed := false
	var visit func([]document.Node)
	visit = func(ns []document.Node) {
		for i := range ns {
			n := &ns[i]
			if n.Attrs["object_id"] == from {
				if blockID, ok := n.Attrs["block_id"].(string); ok && blockIDs[blockID] != "" {
					n.Attrs["block_id"] = blockIDs[blockID]
				}
				n.Attrs["object_id"] = to
				changed = true
			}
			for j := range n.Content {
				if n.Content[j].ObjectID == from {
					if remapped := blockIDs[n.Content[j].BlockID]; remapped != "" {
						n.Content[j].BlockID = remapped
					}
					n.Content[j].ObjectID = to
					changed = true
				}
			}
			visit(n.Children)
		}
	}
	visit(o.Document.Blocks)
	return changed
}
func (a *App) mutateCanonicalMCP(ctx context.Context, o *Object, c document.Command, base uint64, opID string) error {
	if c.Node != nil {
		d := document.Document{SchemaVersion: 2, Blocks: []document.Node{*c.Node}}
		stableIngestionIDs(&d, opID)
		*c.Node = d.Blocks[0]
		if c.Type == "replace" {
			c.Node.ID = c.NodeID
		}
	}
	updated, e := a.commitDocument(o.ID, base, opID, nil, []document.Command{c})
	if e == nil {
		*o = updated
	}
	return e
}

// Stable ingestion identities make replayed command payloads byte-identical.
func stableIngestionIDs(d *document.Document, opID string) {
	var visit func([]document.Node, string)
	identity := func(path string) string {
		sum := sha256.Sum256([]byte(opID + "/" + path))
		return hex.EncodeToString(sum[:16])
	}
	visit = func(ns []document.Node, path string) {
		for i := range ns {
			p := fmt.Sprintf("%s/%d", path, i)
			ns[i].ID = identity(p)
			if ns[i].Type == "table" {
				cols, _ := ns[i].Attrs["columns"].([]any)
				for j, raw := range cols {
					raw.(map[string]any)["id"] = identity(fmt.Sprintf("%s/column/%d", p, j))
				}
			}
			visit(ns[i].Children, p)
		}
	}
	visit(d.Blocks, "root")
}
func canonicalAppendCommands(markdown, opID string, position *int, section string) ([]document.Command, error) {
	if strings.TrimSpace(markdown) == "" {
		return nil, fmt.Errorf("Provide markdown")
	}
	blocks, e := MarkdownToBlocks(markdown)
	if e != nil {
		return nil, e
	}
	d, e := canonicalDocumentFromBlocks(blocks)
	if e != nil {
		return nil, e
	}
	stableIngestionIDs(&d, opID)
	commands := []document.Command{}
	if section != "" {
		h := document.Node{ID: opID + "-section", Type: "heading", Attrs: map[string]any{"level": 2}, Content: []document.Inline{{Type: "text", Text: section}}, Children: d.Blocks}
		if len(h.ID) > 128 {
			sum := sha256.Sum256([]byte(h.ID))
			h.ID = hex.EncodeToString(sum[:16])
		}
		return []document.Command{{Type: "append_section", Node: &h}}, nil
	}
	for i, n := range d.Blocks {
		c := document.Command{Type: "append", Node: &n}
		if position != nil {
			c.Type = "insert"
			c.Position = *position + i
		}
		commands = append(commands, c)
	}
	return commands, nil
}

// Document node/column identities are local to an object. A merge retains all
// target identities and renews only colliding source identities, returning the
// map needed to preserve compound object/block references across the workspace.
func avoidCanonicalMergeIDCollisions(target document.Document, source *document.Document) map[string]string {
	used := map[string]bool{}
	all := map[string]bool{}
	visitIDs := func(d document.Document, consume func(string)) {
		document.Walk(d, func(n document.Node) {
			consume(n.ID)
			if cols, ok := n.Attrs["columns"].([]any); ok && n.Type == "table" {
				for _, raw := range cols {
					if col, ok := raw.(map[string]any); ok {
						if id, ok := col["id"].(string); ok {
							consume(id)
						}
					}
				}
			}
		})
	}
	visitIDs(target, func(id string) { used[id] = true; all[id] = true })
	visitIDs(*source, func(id string) { all[id] = true })
	mapping := map[string]string{}
	renew := func(id string) string {
		if !used[id] {
			used[id] = true
			return id
		}
		fresh := newID()
		for all[fresh] {
			fresh = newID()
		}
		all[fresh] = true
		used[fresh] = true
		mapping[id] = fresh
		return fresh
	}
	var visit func([]document.Node)
	visit = func(nodes []document.Node) {
		for i := range nodes {
			n := &nodes[i]
			n.ID = renew(n.ID)
			if cols, ok := n.Attrs["columns"].([]any); ok && n.Type == "table" {
				for _, raw := range cols {
					col := raw.(map[string]any)
					col["id"] = renew(col["id"].(string))
				}
			}
			visit(n.Children)
		}
	}
	visit(source.Blocks)
	return mapping
}
