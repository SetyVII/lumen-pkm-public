package document

import "strings"

func inlineText(in Inline) string {
	switch in.Type {
	case "text", "link":
		return in.Text
	case "break":
		return "\n"
	case "math":
		return in.Latex
	case "object_ref", "block_ref":
		if in.Label != "" {
			return in.Label
		}
		return in.ObjectID
	case "temporal":
		s := textAttr(in.Attrs, "start")
		if e := textAttr(in.Attrs, "end"); e != "" {
			s += " - " + e
		}
		return s
	}
	return ""
}
func nodeText(n Node) string {
	if n.Type == "table" {
		rows := []string{}
		for _, r := range n.Children {
			cells := []string{}
			for _, c := range r.Children {
				cells = append(cells, nodeText(c))
			}
			rows = append(rows, strings.Join(cells, "\t"))
		}
		return strings.Join(rows, "\n")
	}
	parts := []string{}
	var own strings.Builder
	for _, in := range n.Content {
		own.WriteString(inlineText(in))
	}
	if one(n.Type, "code", "math") {
		own.WriteString(textAttr(n.Attrs, "source"))
	}
	if n.Type == "image" {
		own.WriteString(textAttr(n.Attrs, "caption"))
	}
	if own.Len() > 0 {
		parts = append(parts, own.String())
	}
	for _, c := range n.Children {
		parts = append(parts, nodeText(c))
	}
	return strings.Join(parts, "\n")
}

// OwnText projects only this node's semantic text, excluding descendants. It
// supports block-level search without falsely matching every ancestor.
func OwnText(n Node) string {
	n.Children = nil
	return nodeText(n)
}

// PlainText is a derived projection; it is never an alternative editable source.
func PlainText(d Document) string {
	parts := make([]string, len(d.Blocks))
	for i, n := range d.Blocks {
		parts[i] = nodeText(n)
	}
	return strings.Join(parts, "\n")
}
func Walk(d Document, visit func(Node)) {
	var walk func([]Node)
	walk = func(ns []Node) {
		for _, n := range ns {
			visit(n)
			walk(n.Children)
		}
	}
	walk(d.Blocks)
}

// References includes nested cell content and preserves provenance per source node.
func References(d Document) []Reference {
	out := []Reference{}
	Walk(d, func(n Node) {
		if n.Type == "image" {
			out = append(out, Reference{Kind: "asset", NodeID: n.ID, AssetID: textAttr(n.Attrs, "asset_id"), ObjectID: textAttr(n.Attrs, "object_id")})
		}
		if n.Type == "object_embed" {
			r := Reference{Kind: "object", NodeID: n.ID, ObjectID: textAttr(n.Attrs, "object_id")}
			if block := textAttr(n.Attrs, "block_id"); block != "" {
				r.Kind = "block"
				r.BlockID = block
			}
			out = append(out, r)
		}
		for _, in := range n.Content {
			r := Reference{NodeID: n.ID}
			switch in.Type {
			case "object_ref":
				r.Kind = "object"
				r.ObjectID = in.ObjectID
			case "block_ref":
				r.Kind = "block"
				r.ObjectID = in.ObjectID
				r.BlockID = in.BlockID
			case "link":
				r.Kind = "url"
				r.Value = in.Href
			case "temporal":
				r.Kind = "temporal"
				r.Value = textAttr(in.Attrs, "start")
			default:
				continue
			}
			out = append(out, r)
		}
	})
	return out
}
func Find(d Document, id string) (Node, bool) {
	var result Node
	found := false
	Walk(d, func(n Node) {
		if n.ID == id {
			result = n
			found = true
		}
	})
	return result, found
}
