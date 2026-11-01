package main

// Markdown <-> block conversion for the object block schema.
//
// The block schema is the one the desktop editor persists in
// properties.blocks (RichEditor::structuredBlocks in
// desktop/src/editor/lumen_editor.cpp). That editor code is the source of
// truth: block ids are UUIDs, text is the concatenation of the run texts,
// list nesting lives in `indent`, and todo/toggle blocks carry their marker
// glyph in the text so the editor keeps the block type when it reloads.
//
// Parsing is built on goldmark (MIT), the CommonMark implementation used by
// Hugo and many Go tools: it supplies the standard block/inline grammar,
// GFM tables, strikethrough, linkify and task lists, and its documented
// inline-parser extension point adds Lumen's [[wikilinks]], @mentions,
// ==highlights== and $math$. Lumen-only block types that CommonMark does not
// model (toggle/group/small/alphabetic lists/two-column layouts) are mapped
// from the AST or, for the ::: columns container, extracted before parsing.
// Emission back to Markdown is hand-written so output is deterministic.
//
// The additive converter extensions `rows` (GFM table cells) and `columns`
// (layout columns) are also read and written by the Qt and Android editors.

import (
	"bytes"
	"errors"
	"strconv"
	"strings"
	"unicode"
	"unicode/utf8"

	"github.com/yuin/goldmark"
	"github.com/yuin/goldmark/ast"
	"github.com/yuin/goldmark/extension"
	extast "github.com/yuin/goldmark/extension/ast"
	"github.com/yuin/goldmark/parser"
	"github.com/yuin/goldmark/text"
	"github.com/yuin/goldmark/util"
)

// Block types persisted in properties.blocks.
const (
	blockParagraph  = "paragraph"
	blockSmall      = "small"
	blockHeading    = "heading"
	blockBullet     = "bullet_list"
	blockNumbered   = "numbered_list"
	blockAlphabetic = "alphabetic_list"
	blockQuote      = "quote"
	blockTodo       = "todo"
	blockToggle     = "toggle"
	blockGroup      = "group"
	blockCode       = "code"
	blockMath       = "math"
	blockDivider    = "divider"
	blockEmbed      = "embed"
	// Additive table/layout extensions, shared with both native editors.
	blockTable   = "table"
	blockColumns = "columns"
)

const (
	todoOpen          = "○ "
	todoDone          = "✔ "
	toggleOpen        = "▶ "
	toggleClosed      = "▼ "
	objectReplacement = "\uFFFC" // image/math placeholder, as in QTextDocument
	lineSeparator     = "\u2028" // in-block line break, as in QTextBlock text
	objectHrefPrefix  = "lumen://object/"
	dividerImage      = "lumen-rule://divider/light"
	columnTokenPrefix = "\uE000lumen-columns-"
	columnTokenSuffix = "\uE000"
	inlineSentinel    = "\u200B"
	maxColumnDepth    = 4
	maxInlineDepth    = 16
)

// Block is one entry of an object's properties.blocks array.
//
// Field names and shapes match the editor. Optional fields are omitted when
// they carry their zero value; the editor reads missing keys with the same
// defaults (level 0, indent 1 for lists, false booleans).
type Block struct {
	ID       string `json:"id"`
	Type     string `json:"type"`
	Text     string `json:"text"`
	Level    int    `json:"level,omitempty"`
	Indent   int    `json:"indent,omitempty"`
	Language string `json:"language,omitempty"`
	Runs     []Run  `json:"runs"`
	// Table extension: first row is the GFM header row. Align holds
	// "left", "center", "right" or "" per column and is omitted when no
	// column declares an alignment.
	Align []string      `json:"align,omitempty"`
	Rows  [][]BlockCell `json:"rows,omitempty"`
	// Columns extension: each entry is one column of full blocks.
	Columns  [][]Block `json:"columns,omitempty"`
	Children []Block   `json:"children,omitempty"`
}

// BlockCell is one table cell. Cells reuse runs so inline formatting survives
// a table round-trip.
type BlockCell struct {
	Runs []Run `json:"runs"`
}

// Run is one inline span. The editor always writes the boolean flags; the
// converter omits false ones, which the editor reads as false.
type Run struct {
	Text      string  `json:"text"`
	Bold      bool    `json:"bold,omitempty"`
	Italic    bool    `json:"italic,omitempty"`
	Underline bool    `json:"underline,omitempty"`
	Strike    bool    `json:"strike,omitempty"`
	Code      bool    `json:"code,omitempty"`
	Highlight bool    `json:"highlight,omitempty"` // extension, editor ignores it
	Href      string  `json:"href,omitempty"`
	TypeID    string  `json:"type_id,omitempty"`
	ObjectID  string  `json:"object_id,omitempty"`
	Type      string  `json:"type,omitempty"` // "math" or "image"
	Latex     string  `json:"latex,omitempty"`
	Display   bool    `json:"display,omitempty"`
	ImageName string  `json:"image_name,omitempty"`
	Width     float64 `json:"width,omitempty"`
	Height    float64 `json:"height,omitempty"`
	// ReferenceIcon and PointSize are visual metadata the editor writes;
	// the converter drops them because Markdown cannot express them.
	ReferenceIcon bool    `json:"reference_icon,omitempty"`
	PointSize     float64 `json:"point_size,omitempty"`
}

// lumenMarkdown is the parser shared by every request. goldmark Markdown
// values are safe for concurrent use after configuration.
var lumenMarkdown = goldmark.New(
	goldmark.WithExtensions(
		extension.Table,
		extension.Strikethrough,
		extension.Linkify,
		extension.TaskList,
	),
	goldmark.WithParserOptions(parser.WithInlineParsers(
		util.Prioritized(&wikilinkParser{}, 150),
		util.Prioritized(&mentionParser{}, 150),
		util.Prioritized(&highlightParser{}, 150),
		util.Prioritized(&mathParser{}, 150),
	)),
)

// MarkdownToBlocks parses Markdown into editor-compatible blocks. Every block
// gets a fresh id. The parser never rejects well-formed UTF-8 input: unknown
// or unsupported syntax degrades to paragraph text.
func MarkdownToBlocks(md string) ([]Block, error) {
	if !utf8.ValidString(md) {
		return nil, errors.New("markdown must be valid UTF-8")
	}
	md = strings.TrimPrefix(md, "\uFEFF")
	md = strings.ReplaceAll(md, "\r\n", "\n")
	md = strings.ReplaceAll(md, "\r", "\n")
	md = strings.ReplaceAll(md, lineSeparator, "\n")
	return markdownToBlocks(md, 0)
}

func markdownToBlocks(md string, depth int) ([]Block, error) {
	md, columns := extractColumns(md, depth)
	source := []byte(md)
	doc := lumenMarkdown.Parser().Parse(text.NewReader(source))
	converter := &blockConverter{source: source, columns: columns, depth: depth}
	blocks := converter.blocks(doc)
	if blocks == nil {
		blocks = []Block{}
	}
	return blocks, nil
}

// BlocksToMarkdown renders blocks as canonical Markdown. The output is
// deterministic: ids never appear, list markers are normalized, blocks are
// separated by one blank line and list items by one newline.
func BlocksToMarkdown(blocks []Block) string {
	type rendered struct {
		text   string
		kind   string
		indent int
	}
	items := make([]rendered, 0, len(blocks))
	// columns[level] is the column where the most recent item of that level
	// starts; widths[level] is its marker width. A nested item is indented by
	// its parent's content column, which differs for "- ", "1. " and "- [ ] ".
	columns := map[int]int{}
	widths := map[int]int{}
	for _, block := range blocks {
		level := block.Indent
		if !isListFamily(block.Type) {
			level = 0
		}
		column := 0
		if level > 1 {
			parentColumn := columns[level-1]
			width := widths[level-1]
			if width == 0 {
				width = 2
			}
			column = parentColumn + width
		}
		text := renderBlock(block, column)
		if len(block.Children) > 0 {
			text += "\n\n" + BlocksToMarkdown(block.Children)
		}
		if text == "" {
			continue
		}
		items = append(items, rendered{text: text, kind: block.Type, indent: level})
		if level > 0 {
			columns[level] = column
			widths[level] = markerWidth(block)
			for deeper := range columns {
				if deeper > level {
					delete(columns, deeper)
					delete(widths, deeper)
				}
			}
		} else {
			for deeper := range columns {
				delete(columns, deeper)
				delete(widths, deeper)
			}
		}
	}
	var out strings.Builder
	for i, item := range items {
		if i > 0 {
			previous := items[i-1]
			// Adjacent items of the same list family, or a nested item, stay
			// in one Markdown list; a change of list type starts a new one.
			if listContinuation(previous.kind, previous.indent, item.kind, item.indent) {
				out.WriteString("\n")
			} else {
				out.WriteString("\n\n")
			}
		}
		out.WriteString(item.text)
	}
	if out.Len() > 0 {
		out.WriteString("\n")
	}
	return out.String()
}

func isListFamily(kind string) bool {
	switch kind {
	case blockBullet, blockNumbered, blockAlphabetic, blockTodo:
		return true
	}
	return false
}

// listContinuation reports whether two adjacent list-family blocks belong to
// the same Markdown list. Todo is a bullet item with a checkbox, so it shares
// a list with bullets; numbered and alphabetic lists are distinct families.
func listContinuation(previousKind string, previousIndent int, kind string, indent int) bool {
	if !isListFamily(previousKind) || !isListFamily(kind) {
		return false
	}
	if indent > previousIndent {
		return true
	}
	return listFamily(previousKind) == listFamily(kind)
}

func listFamily(kind string) string {
	switch kind {
	case blockBullet, blockTodo:
		return "bullet"
	case blockNumbered:
		return "numbered"
	case blockAlphabetic:
		return "alphabetic"
	}
	return ""
}

func markerWidth(block Block) int {
	switch block.Type {
	case blockNumbered:
		return 3 // "1. "
	case blockTodo:
		return 6 // "- [ ] "
	case blockBullet, blockAlphabetic:
		return 2 // "- " / "a. "
	}
	return 2
}

// ---------------------------------------------------------------------------
// goldmark AST to blocks
// ---------------------------------------------------------------------------

type blockConverter struct {
	source  []byte
	columns map[string][][]Block
	depth   int
}

func (c *blockConverter) blocks(node ast.Node) []Block {
	blocks := []Block{}
	for child := node.FirstChild(); child != nil; child = child.NextSibling() {
		blocks = append(blocks, c.block(child, 0)...)
	}
	return blocks
}

func (c *blockConverter) block(node ast.Node, depth int) []Block {
	switch n := node.(type) {
	case *ast.Heading:
		return []Block{c.heading(n)}
	case *ast.Paragraph:
		return c.paragraph(n)
	case *ast.TextBlock:
		return []Block{newBlockFromRuns(blockParagraph, 0, 0, c.runs(n, Run{}))}
	case *ast.Blockquote:
		return c.blockquote(n)
	case *ast.List:
		return c.list(n, depth)
	case *ast.FencedCodeBlock:
		return []Block{c.code(n)}
	case *ast.CodeBlock:
		return []Block{c.code(n)}
	case *ast.ThematicBreak:
		return []Block{newDividerBlock()}
	case *extast.Table:
		return []Block{c.table(n)}
	case *ast.HTMLBlock:
		return []Block{c.htmlBlock(n)}
	case *ast.LinkReferenceDefinition:
		return nil
	default:
		// Recurse into unknown block containers instead of dropping content.
		return c.blocks(node)
	}
}

func (c *blockConverter) heading(n *ast.Heading) Block {
	level := n.Level
	if level < 1 {
		level = 1
	}
	if level > 4 {
		level = 4
	}
	return newBlockFromRuns(blockHeading, level, 0, c.runs(n, Run{}))
}

func (c *blockConverter) paragraph(n *ast.Paragraph) []Block {
	if blocks, ok := c.columnsBlock(c.plainText(n)); ok {
		return blocks
	}
	raw := rawLines(n, c.source)
	if blocks, ok := c.lineBlocks(raw); ok {
		return blocks
	}
	if latex, ok := mathBlockLines(raw); ok {
		return []Block{newMathBlock(latex)}
	}
	return []Block{newBlockFromRuns(blockParagraph, 0, 0, c.runs(n, Run{}))}
}

// lineBlocks handles Lumen block constructs that CommonMark parses as
// paragraph lines: toggles, small text, bare checkboxes, single-line math and
// alphabetic lists. A paragraph is converted only when every line is one of
// those constructs; otherwise it stays a paragraph.
func (c *blockConverter) lineBlocks(lines []string) ([]Block, bool) {
	if len(lines) == 0 {
		return nil, false
	}
	blocks := []Block{}
	listStack := []int{}
	for _, line := range lines {
		if marker, content, ok := toggleLine(line); ok {
			runs := append([]Run{{Text: marker + " "}}, c.inlineRuns(content, Run{})...)
			blocks = append(blocks, newBlockFromRuns(blockToggle, 0, 0, runs))
			listStack = nil
			continue
		}
		if content, ok := smallLine(line); ok {
			blocks = append(blocks, newBlockFromRuns(blockSmall, 0, 0, c.inlineRuns(content, Run{})))
			listStack = nil
			continue
		}
		if checked, content, ok := bareTodoLine(line); ok {
			blocks = append(blocks, newTodoBlockFromRuns(1, checked, c.inlineRuns(content, Run{})))
			listStack = nil
			continue
		}
		if latex, ok := singleLineMath(line); ok {
			blocks = append(blocks, newMathBlock(latex))
			listStack = nil
			continue
		}
		columns, rest := leadingSpaces(line)
		if len(rest) >= 2 && isASCIILetter(rest[0]) && (rest[1] == '.' || rest[1] == ')') &&
			(len(rest) == 2 || rest[2] == ' ' || rest[2] == '\t') {
			for len(listStack) > 0 && columns < listStack[len(listStack)-1] {
				listStack = listStack[:len(listStack)-1]
			}
			if len(listStack) == 0 || columns > listStack[len(listStack)-1] {
				listStack = append(listStack, columns)
			}
			level := len(listStack)
			if level > 8 {
				level = 8
			}
			blocks = append(blocks, newBlockFromRuns(blockAlphabetic, 0, level, c.inlineRuns(strings.TrimLeft(rest[2:], " \t"), Run{})))
			continue
		}
		return nil, false
	}
	return blocks, true
}

func (c *blockConverter) blockquote(n *ast.Blockquote) []Block {
	if c.isGroupBlockquote(n) {
		inner := n.FirstChild().(*ast.Blockquote)
		if content := firstContent(inner); content != nil {
			return []Block{newBlockFromRuns(blockGroup, 0, 0, c.runs(content, Run{}))}
		}
	}
	blocks := []Block{}
	for child := n.FirstChild(); child != nil; child = child.NextSibling() {
		switch v := child.(type) {
		case *ast.Paragraph, *ast.TextBlock:
			blocks = append(blocks, newBlockFromRuns(blockQuote, 0, 0, c.runs(v, Run{})))
		case *ast.Blockquote:
			blocks = append(blocks, c.blockquote(v)...)
		case *ast.List:
			blocks = append(blocks, c.list(v, 0)...)
		case *ast.Heading:
			blocks = append(blocks, c.heading(v))
		case *ast.FencedCodeBlock, *ast.CodeBlock:
			blocks = append(blocks, c.code(v))
		case *ast.ThematicBreak:
			blocks = append(blocks, newDividerBlock())
		default:
			blocks = append(blocks, c.block(child, 0)...)
		}
	}
	return blocks
}

// isGroupBlockquote distinguishes Lumen's ">> group" from a nested "> >"
// quote by looking at the raw source at the blockquote position.
func (c *blockConverter) isGroupBlockquote(n *ast.Blockquote) bool {
	pos := n.Pos()
	if pos < 0 || pos+1 >= len(c.source) {
		return false
	}
	if c.source[pos] != '>' || c.source[pos+1] != '>' {
		return false
	}
	inner, ok := n.FirstChild().(*ast.Blockquote)
	if !ok || inner.NextSibling() != nil {
		return false
	}
	return firstContent(inner) != nil
}

func (c *blockConverter) list(n *ast.List, depth int) []Block {
	indent := depth + 1
	if indent > 8 {
		indent = 8
	}
	blocks := []Block{}
	for item := n.FirstChild(); item != nil; item = item.NextSibling() {
		listItem, ok := item.(*ast.ListItem)
		if !ok {
			continue
		}
		content := firstContent(listItem)
		var box *extast.TaskCheckBox
		var runs []Run
		if content != nil {
			box = findTaskCheckBox(content)
			runs = c.runs(content, Run{})
		}
		if box != nil {
			blocks = append(blocks, newTodoBlockFromRuns(indent, box.IsChecked, runs))
		} else {
			kind := blockBullet
			if n.IsOrdered() {
				kind = blockNumbered
			}
			blocks = append(blocks, newBlockFromRuns(kind, 0, indent, runs))
		}
		for child := listItem.FirstChild(); child != nil; child = child.NextSibling() {
			if child == content {
				continue
			}
			switch v := child.(type) {
			case *ast.List:
				blocks = append(blocks, c.list(v, indent)...)
			case *ast.Paragraph, *ast.TextBlock:
				blocks = append(blocks, newBlockFromRuns(blockParagraph, 0, 0, c.runs(v, Run{})))
			case *ast.Blockquote:
				blocks = append(blocks, c.blockquote(v)...)
			case *ast.FencedCodeBlock, *ast.CodeBlock:
				blocks = append(blocks, c.code(v))
			case *ast.ThematicBreak:
				blocks = append(blocks, newDividerBlock())
			}
		}
	}
	return blocks
}

func (c *blockConverter) code(n ast.Node) Block {
	language := ""
	var body string
	switch v := n.(type) {
	case *ast.FencedCodeBlock:
		body = string(v.Lines().Value(c.source))
		language = string(v.Language(c.source))
	case *ast.CodeBlock:
		body = string(v.Lines().Value(c.source))
	}
	body = strings.TrimSuffix(body, "\n")
	body = strings.TrimSuffix(body, "\r")
	return newCodeBlock(language, body)
}

func (c *blockConverter) table(n *extast.Table) Block {
	align := make([]string, len(n.Alignments))
	for i, value := range n.Alignments {
		switch value {
		case extast.AlignLeft:
			align[i] = "left"
		case extast.AlignRight:
			align[i] = "right"
		case extast.AlignCenter:
			align[i] = "center"
		}
	}
	rows := [][]BlockCell{}
	for child := n.FirstChild(); child != nil; child = child.NextSibling() {
		switch v := child.(type) {
		case *extast.TableHeader:
			rows = append(rows, c.tableCells(v))
		case *extast.TableRow:
			rows = append(rows, c.tableCells(v))
		}
	}
	return newTableBlockFromCells(align, rows)
}

func (c *blockConverter) tableCells(row ast.Node) []BlockCell {
	cells := []BlockCell{}
	for child := row.FirstChild(); child != nil; child = child.NextSibling() {
		if cell, ok := child.(*extast.TableCell); ok {
			cells = append(cells, BlockCell{Runs: c.runs(cell, Run{})})
		}
	}
	return cells
}

func (c *blockConverter) htmlBlock(n *ast.HTMLBlock) Block {
	text := strings.TrimSpace(string(n.Lines().Value(c.source)))
	return newBlockFromRuns(blockParagraph, 0, 0, []Run{{Text: text}})
}

func (c *blockConverter) columnsBlock(text string) ([]Block, bool) {
	columns, ok := c.columns[strings.TrimSpace(text)]
	if !ok {
		return nil, false
	}
	return []Block{newColumnsBlock(columns)}, true
}

func (c *blockConverter) plainText(node ast.Node) string {
	return runsText(c.runs(node, Run{}))
}

// inlineRuns parses a fragment of Markdown into runs. A zero-width sentinel
// keeps the fragment in a paragraph even when it starts with a block marker.
func (c *blockConverter) inlineRuns(markdown string, base Run) []Run {
	if strings.TrimSpace(markdown) == "" {
		return []Run{}
	}
	if c.depth >= maxInlineDepth {
		return []Run{withText(base, markdown)}
	}
	source := []byte(inlineSentinel + markdown)
	doc := lumenMarkdown.Parser().Parse(text.NewReader(source))
	sub := &blockConverter{source: source, columns: c.columns, depth: c.depth + 1}
	for child := doc.FirstChild(); child != nil; child = child.NextSibling() {
		switch v := child.(type) {
		case *ast.Paragraph, *ast.TextBlock:
			return stripInlineSentinel(sub.runs(v, base))
		}
	}
	return []Run{}
}

func stripInlineSentinel(runs []Run) []Run {
	if len(runs) > 0 && strings.HasPrefix(runs[0].Text, inlineSentinel) {
		runs[0].Text = strings.TrimPrefix(runs[0].Text, inlineSentinel)
		if runs[0].Text == "" {
			runs = runs[1:]
		}
	}
	return runs
}

// runs flattens the inline children of a node into editor runs.
func (c *blockConverter) runs(node ast.Node, base Run) []Run {
	out := []Run{}
	underline := base.Underline
	for child := node.FirstChild(); child != nil; child = child.NextSibling() {
		current := base
		current.Underline = underline
		switch n := child.(type) {
		case *ast.Text:
			text := string(n.Value(c.source))
			if !n.IsRaw() {
				text = unescapeText(text)
			}
			if text != "" {
				run := current
				run.Text = text
				out = append(out, run)
			}
			if n.HardLineBreak() {
				out = append(out, withText(current, lineSeparator))
			} else if n.SoftLineBreak() {
				out = append(out, withText(current, " "))
			}
		case *ast.String:
			text := string(n.Value)
			if !n.IsRaw() {
				text = unescapeText(text)
			}
			if text != "" {
				run := current
				run.Text = text
				out = append(out, run)
			}
		case *ast.CodeSpan:
			text := codeSpanText(n, c.source)
			if text != "" {
				run := current
				run.Code = true
				run.Text = text
				out = append(out, run)
			}
		case *ast.Emphasis:
			nested := current
			if n.Level&1 != 0 {
				nested.Italic = true
			}
			if n.Level&2 != 0 {
				nested.Bold = true
			}
			out = append(out, c.runs(n, nested)...)
		case *extast.Strikethrough:
			nested := current
			nested.Strike = true
			out = append(out, c.runs(n, nested)...)
		case *ast.Link:
			nested := current
			nested.Href = string(n.Destination)
			out = append(out, c.runs(n, nested)...)
		case *ast.AutoLink:
			href := string(n.URL(c.source))
			label := string(n.Label(c.source))
			if n.AutoLinkType == ast.AutoLinkEmail {
				if !strings.HasPrefix(href, "mailto:") {
					href = "mailto:" + href
				}
				if label == "" {
					label = strings.TrimPrefix(href, "mailto:")
				}
			}
			run := current
			run.Text = label
			run.Href = href
			out = append(out, run)
		case *ast.Image:
			alt := runsText(c.runs(n, Run{}))
			href := string(n.Destination)
			if strings.TrimSpace(alt) == "" {
				alt = href
			}
			run := current
			run.Text = alt
			run.Href = href
			out = append(out, run)
		case *ast.RawHTML:
			tag := rawHTMLText(n, c.source)
			switch strings.ToLower(strings.TrimSpace(tag)) {
			case "<u>":
				underline = true
			case "</u>":
				underline = false
			default:
				// Inline HTML other than <u> is not interpreted; keep the
				// literal tag so no content disappears.
				if tag != "" {
					run := current
					run.Text = tag
					out = append(out, run)
				}
			}
		case *extast.TaskCheckBox:
			// The list walker turns the checkbox into a todo block.
		case *lumenInline:
			out = append(out, c.lumenRuns(n, current)...)
		default:
			out = append(out, c.runs(child, current)...)
		}
	}
	return mergeRuns(out)
}

func (c *blockConverter) lumenRuns(n *lumenInline, base Run) []Run {
	switch n.kind {
	case lumenWikilink, lumenMention:
		run := base
		run.Text = n.text
		run.Href = objectHrefPrefix
		run.ObjectID = ""
		run.TypeID = ""
		return []Run{run}
	case lumenHighlight:
		nested := base
		nested.Highlight = true
		return c.inlineRuns(n.text, nested)
	case lumenMath:
		run := base
		run.Type = "math"
		run.Latex = n.latex
		run.Display = n.display
		run.Text = objectReplacement
		return []Run{run}
	}
	return nil
}

func rawLines(node ast.Node, source []byte) []string {
	segments := node.Lines()
	if segments == nil {
		return nil
	}
	lines := make([]string, 0, segments.Len())
	for i := 0; i < segments.Len(); i++ {
		segment := segments.At(i)
		value := string(segment.Value(source))
		value = strings.TrimSuffix(value, "\n")
		value = strings.TrimSuffix(value, "\r")
		lines = append(lines, value)
	}
	return lines
}

func firstContent(node ast.Node) ast.Node {
	for child := node.FirstChild(); child != nil; child = child.NextSibling() {
		switch child.(type) {
		case *ast.Paragraph, *ast.TextBlock:
			return child
		}
	}
	return nil
}

func findTaskCheckBox(node ast.Node) *extast.TaskCheckBox {
	for child := node.FirstChild(); child != nil; child = child.NextSibling() {
		if box, ok := child.(*extast.TaskCheckBox); ok {
			return box
		}
	}
	return nil
}

func codeSpanText(node *ast.CodeSpan, source []byte) string {
	var out strings.Builder
	for child := node.FirstChild(); child != nil; child = child.NextSibling() {
		if textNode, ok := child.(*ast.Text); ok {
			out.Write(textNode.Value(source))
		}
	}
	return strings.ReplaceAll(out.String(), "\n", " ")
}

func rawHTMLText(node *ast.RawHTML, source []byte) string {
	var out strings.Builder
	for i := 0; i < node.Segments.Len(); i++ {
		segment := node.Segments.At(i)
		out.Write(segment.Value(source))
	}
	return out.String()
}

func unescapeText(text string) string {
	value := []byte(text)
	value = util.UnescapePunctuations(value)
	value = util.ResolveNumericReferences(value)
	value = util.ResolveEntityNames(value)
	return string(value)
}

// ---------------------------------------------------------------------------
// Lumen inline syntax (goldmark inline parsers)
// ---------------------------------------------------------------------------

var kindLumenInline = ast.NewNodeKind("LumenInline")

type lumenInlineKind int

const (
	lumenWikilink lumenInlineKind = iota
	lumenMention
	lumenHighlight
	lumenMath
)

type lumenInline struct {
	ast.BaseInline
	kind    lumenInlineKind
	text    string
	latex   string
	display bool
}

func (n *lumenInline) Kind() ast.NodeKind {
	return kindLumenInline
}

func (n *lumenInline) Dump(source []byte, level int) {
	ast.DumpHelper(n, source, level, nil, nil)
}

type wikilinkParser struct{}

func (p *wikilinkParser) Trigger() []byte { return []byte{'['} }

func (p *wikilinkParser) Parse(parent ast.Node, block text.Reader, pc parser.Context) ast.Node {
	line, _ := block.PeekLine()
	if len(line) < 4 || line[0] != '[' || line[1] != '[' {
		return nil
	}
	index := bytes.Index(line[2:], []byte("]]"))
	if index < 0 {
		return nil
	}
	title := strings.TrimSpace(string(line[2 : 2+index]))
	if title == "" || strings.ContainsAny(title, "\n\r") {
		return nil
	}
	block.Advance(2 + index + 2)
	return &lumenInline{kind: lumenWikilink, text: string(util.UnescapePunctuations([]byte(title)))}
}

type mentionParser struct{}

func (p *mentionParser) Trigger() []byte { return []byte{'@'} }

func (p *mentionParser) Parse(parent ast.Node, block text.Reader, pc parser.Context) ast.Node {
	line, _ := block.PeekLine()
	if len(line) < 2 || line[0] != '@' {
		return nil
	}
	previous := block.PrecendingCharacter()
	if previous != '\n' && (isWordRune(previous) || previous == '@') {
		return nil
	}
	first, size := utf8.DecodeRune(line[1:])
	if size == 0 || !unicode.IsLetter(first) {
		return nil
	}
	consumed := 1 + size
	for consumed < len(line) {
		r, runeSize := utf8.DecodeRune(line[consumed:])
		if unicode.IsLetter(r) || unicode.IsDigit(r) || r == '_' || r == '-' {
			consumed += runeSize
			continue
		}
		if r == '.' {
			next, nextSize := utf8.DecodeRune(line[consumed+runeSize:])
			if nextSize > 0 && (unicode.IsLetter(next) || unicode.IsDigit(next)) {
				consumed += runeSize
				continue
			}
		}
		break
	}
	name := string(line[1:consumed])
	block.Advance(consumed)
	return &lumenInline{kind: lumenMention, text: name}
}

type highlightParser struct{}

func (p *highlightParser) Trigger() []byte { return []byte{'='} }

func (p *highlightParser) Parse(parent ast.Node, block text.Reader, pc parser.Context) ast.Node {
	line, _ := block.PeekLine()
	if len(line) < 5 || line[0] != '=' || line[1] != '=' {
		return nil
	}
	index := bytes.Index(line[2:], []byte("=="))
	if index <= 0 {
		return nil
	}
	inner := string(line[2 : 2+index])
	block.Advance(2 + index + 2)
	return &lumenInline{kind: lumenHighlight, text: inner}
}

type mathParser struct{}

func (p *mathParser) Trigger() []byte { return []byte{'$'} }

func (p *mathParser) Parse(parent ast.Node, block text.Reader, pc parser.Context) ast.Node {
	line, _ := block.PeekLine()
	if len(line) < 3 || line[0] != '$' {
		return nil
	}
	if line[1] == '$' {
		index := bytes.Index(line[2:], []byte("$$"))
		if index < 0 {
			return nil
		}
		latex := string(line[2 : 2+index])
		if strings.TrimSpace(latex) == "" {
			return nil
		}
		block.Advance(2 + index + 2)
		return &lumenInline{kind: lumenMath, latex: latex, display: true}
	}
	if line[1] == ' ' || line[1] == '\t' || (line[1] >= '0' && line[1] <= '9') {
		return nil
	}
	index := bytes.IndexByte(line[1:], '$')
	if index < 0 {
		return nil
	}
	latex := string(line[1 : 1+index])
	if latex == "" || strings.HasSuffix(latex, " ") {
		return nil
	}
	end := 1 + index + 1
	if end < len(line) && line[end] >= '0' && line[end] <= '9' {
		return nil
	}
	block.Advance(end)
	return &lumenInline{kind: lumenMath, latex: latex}
}

// ---------------------------------------------------------------------------
// Columns pre-pass
// ---------------------------------------------------------------------------

// extractColumns replaces every "::: columns" container with a placeholder
// paragraph and converts each column recursively. goldmark treats the fences
// as paragraph text, so the container is lifted out before parsing.
func extractColumns(md string, depth int) (string, map[string][][]Block) {
	if depth >= maxColumnDepth {
		return md, nil
	}
	lines := strings.Split(md, "\n")
	out := make([]string, 0, len(lines))
	groups := map[string][][]Block{}
	inFence := false
	fenceChar := byte(0)
	fenceLen := 0
	inMath := false
	for i := 0; i < len(lines); i++ {
		line := lines[i]
		trimmed := strings.TrimSpace(line)
		if inFence {
			out = append(out, line)
			if isClosingFence(line, fenceChar, fenceLen) {
				inFence = false
			}
			continue
		}
		if inMath {
			out = append(out, line)
			if trimmed == "$$" {
				inMath = false
			}
			continue
		}
		if fence, _, ok := codeFenceOpen(line); ok {
			inFence, fenceChar, fenceLen = true, fence.char, fence.length
			out = append(out, line)
			continue
		}
		if trimmed == "$$" {
			inMath = true
			out = append(out, line)
			continue
		}
		if isColumnsFence(trimmed) && leadingColumns(line) <= 3 {
			end := findColumnsEnd(lines, i+1)
			if end < 0 {
				out = append(out, line)
				continue
			}
			inner := lines[i+1 : end]
			sections := splitColumnSections(inner)
			columns := make([][]Block, 0, len(sections))
			for _, section := range sections {
				blocks, _ := markdownToBlocks(strings.Join(section, "\n"), depth+1)
				columns = append(columns, blocks)
			}
			token := columnTokenPrefix + strconv.Itoa(len(groups)) + columnTokenSuffix
			groups[token] = columns
			// A columns block is block-level: keep the placeholder in its own
			// paragraph even when the source had no blank lines around it.
			if len(out) > 0 && strings.TrimSpace(out[len(out)-1]) != "" {
				out = append(out, "")
			}
			out = append(out, token)
			if end+1 < len(lines) && strings.TrimSpace(lines[end+1]) != "" {
				out = append(out, "")
			}
			i = end
			continue
		}
		out = append(out, line)
	}
	return strings.Join(out, "\n"), groups
}

func findColumnsEnd(lines []string, start int) int {
	// One "in column" flag per open ::: columns group. A bare ::: closes the
	// current column first and the group only when no column is open.
	states := []bool{false}
	inFence := false
	fenceChar := byte(0)
	fenceLen := 0
	inMath := false
	for i := start; i < len(lines); i++ {
		line := lines[i]
		trimmed := strings.TrimSpace(line)
		if inFence {
			if isClosingFence(line, fenceChar, fenceLen) {
				inFence = false
			}
			continue
		}
		if inMath {
			if trimmed == "$$" {
				inMath = false
			}
			continue
		}
		if fence, _, ok := codeFenceOpen(line); ok {
			inFence, fenceChar, fenceLen = true, fence.char, fence.length
			continue
		}
		if trimmed == "$$" {
			inMath = true
			continue
		}
		if leadingColumns(line) > 3 {
			continue
		}
		if isColumnsFence(trimmed) {
			states = append(states, false)
			continue
		}
		if isColumnFence(trimmed) {
			states[len(states)-1] = true
			continue
		}
		if trimmed == ":::" {
			top := len(states) - 1
			if states[top] {
				states[top] = false
				continue
			}
			states = states[:top]
			if len(states) == 0 {
				return i
			}
		}
	}
	return -1
}

func splitColumnSections(lines []string) [][]string {
	sections := [][]string{}
	current := []string{}
	started := false
	inFence := false
	fenceChar := byte(0)
	fenceLen := 0
	flush := func() {
		if started || len(current) > 0 {
			sections = append(sections, trimBlankLines(current))
			current = nil
		}
	}
	for _, line := range lines {
		trimmed := strings.TrimSpace(line)
		if inFence {
			current = append(current, line)
			if isClosingFence(line, fenceChar, fenceLen) {
				inFence = false
			}
			continue
		}
		if fence, _, ok := codeFenceOpen(line); ok {
			inFence, fenceChar, fenceLen = true, fence.char, fence.length
			current = append(current, line)
			continue
		}
		if isColumnFence(trimmed) {
			flush()
			started = true
			continue
		}
		if trimmed == ":::" && started {
			flush()
			started = false
			continue
		}
		current = append(current, line)
	}
	flush()
	if len(sections) == 0 {
		sections = append(sections, []string{})
	}
	return sections
}

func trimBlankLines(lines []string) []string {
	start := 0
	for start < len(lines) && strings.TrimSpace(lines[start]) == "" {
		start++
	}
	end := len(lines)
	for end > start && strings.TrimSpace(lines[end-1]) == "" {
		end--
	}
	return lines[start:end]
}

// ---------------------------------------------------------------------------
// Block constructors
// ---------------------------------------------------------------------------

func newBlockFromRuns(kind string, level, indent int, runs []Run) Block {
	runs = mergeRuns(runs)
	return Block{ID: newID(), Type: kind, Text: runsText(runs), Level: level, Indent: indent, Runs: runs}
}

func newTodoBlockFromRuns(indent int, checked bool, runs []Run) Block {
	marker := todoOpen
	if checked {
		marker = todoDone
	}
	runs = append([]Run{{Text: marker}}, mergeRuns(runs)...)
	return Block{ID: newID(), Type: blockTodo, Text: runsText(runs), Indent: indent, Runs: runs}
}

func newCodeBlock(language, body string) Block {
	text := strings.ReplaceAll(body, "\n", lineSeparator)
	runs := []Run{{Text: text, Code: true}}
	return Block{ID: newID(), Type: blockCode, Text: text, Language: language, Runs: runs}
}

func newMathBlock(latex string) Block {
	run := Run{Text: objectReplacement, Type: "math", Latex: latex, Display: true}
	return Block{ID: newID(), Type: blockMath, Text: objectReplacement, Runs: []Run{run}}
}

func newDividerBlock() Block {
	run := Run{Text: objectReplacement, Type: "image", ImageName: dividerImage}
	return Block{ID: newID(), Type: blockDivider, Text: objectReplacement, Runs: []Run{run}}
}

func newTableBlockFromCells(align []string, rows [][]BlockCell) Block {
	width := len(align)
	if width == 0 && len(rows) > 0 {
		width = len(rows[0])
	}
	normalized := make([][]BlockCell, 0, len(rows))
	for _, row := range rows {
		cells := make([]BlockCell, width)
		for i := 0; i < width; i++ {
			if i < len(row) {
				cells[i] = row[i]
			} else {
				cells[i] = BlockCell{Runs: []Run{}}
			}
		}
		normalized = append(normalized, cells)
	}
	block := Block{ID: newID(), Type: blockTable, Runs: []Run{}, Rows: normalized}
	aligned := false
	for _, value := range align {
		if value != "" {
			aligned = true
			break
		}
	}
	if aligned {
		block.Align = make([]string, width)
		copy(block.Align, align)
	}
	return block
}

func newColumnsBlock(columns [][]Block) Block {
	return Block{ID: newID(), Type: blockColumns, Runs: []Run{}, Columns: columns}
}

// ---------------------------------------------------------------------------
// Block rendering
// ---------------------------------------------------------------------------

func renderBlock(block Block, column int) string {
	switch block.Type {
	case blockHeading:
		level := block.Level
		if level < 1 {
			level = 1
		}
		if level > 4 {
			level = 4
		}
		text := renderInline(blockRuns(block), false, false)
		if text == "" {
			return strings.Repeat("#", level)
		}
		return strings.Repeat("#", level) + " " + text
	case blockParagraph, "":
		return protectParagraph(renderInline(blockRuns(block), false, true))
	case blockSmall:
		return "<small>" + renderInline(blockRuns(block), false, false) + "</small>"
	case blockQuote:
		text := renderInline(blockRuns(block), false, false)
		if text == "" {
			return ">"
		}
		return "> " + text
	case blockGroup:
		text := renderInline(blockRuns(block), false, false)
		if text == "" {
			return ">>"
		}
		return ">> " + text
	case blockTodo:
		return renderTodo(block, column)
	case blockToggle:
		return renderToggle(block)
	case blockBullet, blockNumbered, blockAlphabetic:
		return renderListItem(block, column)
	case blockCode:
		return renderCode(block)
	case blockMath:
		return renderMath(block)
	case blockDivider:
		return "---"
	case blockTable:
		return renderTable(block)
	case blockColumns:
		return renderColumns(block)
	case blockEmbed:
		return protectParagraph(renderInline(blockRuns(block), false, false))
	default:
		// Unknown block types keep their text instead of disappearing.
		return protectParagraph(renderInline(blockRuns(block), false, true))
	}
}

func blockRuns(block Block) []Run {
	if len(block.Runs) > 0 {
		return block.Runs
	}
	if block.Text == "" {
		return nil
	}
	return []Run{{Text: block.Text}}
}

func renderTodo(block Block, column int) string {
	runs := blockRuns(block)
	checked := false
	if len(runs) > 0 {
		switch {
		case strings.HasPrefix(runs[0].Text, todoDone):
			checked = true
			first := runs[0]
			first.Text = strings.TrimPrefix(first.Text, todoDone)
			runs = append([]Run{first}, runs[1:]...)
		case strings.HasPrefix(runs[0].Text, todoOpen):
			first := runs[0]
			first.Text = strings.TrimPrefix(first.Text, todoOpen)
			runs = append([]Run{first}, runs[1:]...)
		}
	}
	text := renderInline(runs, false, false)
	box := "[ ]"
	if checked {
		box = "[x]"
	}
	indent := strings.Repeat(" ", column)
	if text == "" {
		return indent + "- " + box
	}
	return indent + "- " + box + " " + text
}

func renderToggle(block Block) string {
	runs := blockRuns(block)
	marker := toggleOpen
	if len(runs) > 0 {
		first := runs[0]
		switch {
		case strings.HasPrefix(first.Text, toggleClosed):
			marker = toggleClosed
		case strings.HasPrefix(first.Text, toggleOpen):
			marker = toggleOpen
		default:
			goto rendered
		}
		first.Text = strings.TrimPrefix(first.Text, marker)
		runs = append([]Run{first}, runs[1:]...)
	}
rendered:
	text := renderInline(runs, false, false)
	if text == "" {
		return strings.TrimSpace(marker)
	}
	return strings.TrimSpace(marker) + " " + text
}

func renderListItem(block Block, column int) string {
	text := renderInline(blockRuns(block), false, false)
	indent := strings.Repeat(" ", column)
	marker := "- "
	switch block.Type {
	case blockNumbered:
		marker = "1. "
	case blockAlphabetic:
		marker = "a. "
	}
	if text == "" {
		return indent + strings.TrimSpace(marker)
	}
	return indent + marker + text
}

func renderCode(block Block) string {
	body := strings.ReplaceAll(block.Text, lineSeparator, "\n")
	language := ""
	if fields := strings.Fields(block.Language); len(fields) > 0 {
		language = strings.ReplaceAll(fields[0], "\u0060", "")
	}
	longest := 0
	current := 0
	for i := 0; i < len(body); i++ {
		if body[i] == '\u0060' {
			current++
			if current > longest {
				longest = current
			}
		} else {
			current = 0
		}
	}
	fence := strings.Repeat("\u0060", max(3, longest+1))
	if body == "" {
		return fence + language + "\n" + fence
	}
	return fence + language + "\n" + body + "\n" + fence
}

func renderMath(block Block) string {
	latex := ""
	for _, run := range block.Runs {
		if run.Type == "math" {
			latex = run.Latex
			break
		}
	}
	if latex == "" {
		latex = strings.ReplaceAll(block.Text, objectReplacement, "")
	}
	latex = strings.TrimSpace(latex)
	if latex == "" {
		return ""
	}
	if strings.Contains(latex, "\n") {
		return "$$\n" + latex + "\n$$"
	}
	return "$$" + latex + "$$"
}

func renderTable(block Block) string {
	if len(block.Rows) == 0 || len(block.Rows[0]) == 0 {
		return ""
	}
	width := len(block.Rows[0])
	var out strings.Builder
	writeRow := func(cells []BlockCell) {
		out.WriteString("|")
		for i := 0; i < width; i++ {
			text := ""
			if i < len(cells) {
				text = renderInline(cells[i].Runs, true, false)
			}
			text = strings.ReplaceAll(text, "\n", " ")
			if text != "" {
				out.WriteString(" ")
				out.WriteString(text)
				out.WriteString(" ")
			}
			out.WriteString("|")
		}
		out.WriteString("\n")
	}
	writeRow(block.Rows[0])
	out.WriteString("|")
	for i := 0; i < width; i++ {
		align := ""
		if i < len(block.Align) {
			align = block.Align[i]
		}
		switch align {
		case "left":
			out.WriteString(" :--- |")
		case "center":
			out.WriteString(" :---: |")
		case "right":
			out.WriteString(" ---: |")
		default:
			out.WriteString(" --- |")
		}
	}
	out.WriteString("\n")
	for _, row := range block.Rows[1:] {
		writeRow(row)
	}
	return strings.TrimRight(out.String(), "\n")
}

func renderColumns(block Block) string {
	if len(block.Columns) == 0 {
		return ""
	}
	var out strings.Builder
	out.WriteString("::: columns\n")
	for _, column := range block.Columns {
		out.WriteString("::: column\n")
		inner := strings.TrimSuffix(BlocksToMarkdown(column), "\n")
		if inner != "" {
			out.WriteString(inner)
			out.WriteString("\n")
		}
		out.WriteString(":::\n")
	}
	out.WriteString(":::")
	return out.String()
}

// ---------------------------------------------------------------------------
// Inline rendering
// ---------------------------------------------------------------------------

func renderInline(runs []Run, inTable, breaks bool) string {
	var out strings.Builder
	for i := 0; i < len(runs); i++ {
		run := runs[i]
		// Group consecutive highlighted runs so a highlight that contains
		// mixed formatting emits one ==...== pair instead of ====.
		if run.Highlight && run.Type == "" {
			j := i
			for j < len(runs) && runs[j].Highlight && runs[j].Type == "" {
				j++
			}
			group := make([]Run, j-i)
			copy(group, runs[i:j])
			for k := range group {
				group[k].Highlight = false
			}
			out.WriteString("==" + renderInline(group, inTable, breaks) + "==")
			i = j - 1
			continue
		}
		switch run.Type {
		case "math":
			if run.Latex == "" {
				continue
			}
			if run.Display {
				out.WriteString("$$" + run.Latex + "$$")
			} else {
				out.WriteString("$" + run.Latex + "$")
			}
			continue
		case "image":
			if run.ImageName == "" {
				continue
			}
			out.WriteString("![](" + linkDestination(run.ImageName, inTable) + ")")
			continue
		}
		text := strings.ReplaceAll(run.Text, objectReplacement, "")
		if text == "" {
			continue
		}
		if run.Href != "" {
			title := strings.TrimSpace(text)
			if strings.HasPrefix(run.Href, objectHrefPrefix) {
				if title == "" {
					title = strings.TrimPrefix(run.Href, objectHrefPrefix)
				}
				if title != "" {
					out.WriteString("[[" + title + "]]")
				}
				continue
			}
			if title == run.Href && !strings.ContainsAny(title, " \t<>|") {
				out.WriteString("<" + title + ">")
				continue
			}
			out.WriteString("[" + escapeText(title, inTable, false) + "](" + linkDestination(run.Href, inTable) + ")")
			continue
		}
		if run.Code {
			out.WriteString(codeSpan(text, inTable))
			continue
		}
		escaped := escapeText(text, inTable, breaks)
		if run.Strike {
			escaped = "~~" + escaped + "~~"
		}
		if run.Italic {
			escaped = "*" + escaped + "*"
		}
		if run.Bold {
			escaped = "**" + escaped + "**"
		}
		if run.Underline {
			escaped = "<u>" + escaped + "</u>"
		}
		out.WriteString(escaped)
	}
	return out.String()
}

func escapeText(text string, inTable, breaks bool) string {
	text = strings.ReplaceAll(text, lineSeparator, "\n")
	parts := strings.Split(text, "\n")
	for i := range parts {
		parts[i] = escapeInlineSegment(parts[i], inTable)
	}
	if breaks {
		return strings.Join(parts, "  \n")
	}
	return strings.Join(parts, " ")
}

func escapeInlineSegment(text string, inTable bool) string {
	var out strings.Builder
	for i := 0; i < len(text); i++ {
		c := text[i]
		switch c {
		case '\\', '`', '*', '[', ']', '$', '<':
			out.WriteByte('\\')
			out.WriteByte(c)
		case '_':
			// Intraword underscores cannot open emphasis in CommonMark.
			if i > 0 && i+1 < len(text) && isWordByte(text[i-1]) && isWordByte(text[i+1]) {
				out.WriteByte(c)
			} else {
				out.WriteString("\\_")
			}
		case '~', '=':
			if (i > 0 && text[i-1] == c) || (i+1 < len(text) && text[i+1] == c) {
				out.WriteByte('\\')
			}
			out.WriteByte(c)
		case '|':
			if inTable {
				out.WriteString("\\|")
			} else {
				out.WriteByte(c)
			}
		default:
			out.WriteByte(c)
		}
	}
	return out.String()
}

func codeSpan(text string, inTable bool) string {
	if inTable {
		text = strings.ReplaceAll(text, "|", "\\|")
	}
	longest := 0
	current := 0
	for i := 0; i < len(text); i++ {
		if text[i] == '\u0060' {
			current++
			if current > longest {
				longest = current
			}
		} else {
			current = 0
		}
	}
	fence := strings.Repeat("\u0060", max(1, longest+1))
	pad := ""
	if strings.HasPrefix(text, "\u0060") || strings.HasSuffix(text, "\u0060") ||
		(strings.HasPrefix(text, " ") && strings.HasSuffix(text, " ") && strings.TrimSpace(text) != "") {
		pad = " "
	}
	return fence + pad + text + pad + fence
}

func linkDestination(href string, inTable bool) string {
	if inTable {
		href = strings.ReplaceAll(href, "|", "\\|")
	}
	if strings.ContainsAny(href, " ()<>") {
		escaped := strings.NewReplacer("\\", "\\\\", ">", "\\>", "<", "\\<").Replace(href)
		return "<" + escaped + ">"
	}
	return href
}

func protectParagraph(text string) string {
	lines := strings.Split(text, "\n")
	for i := range lines {
		lines[i] = protectLineStart(lines[i])
	}
	return strings.Join(lines, "\n")
}

// protectLineStart escapes a paragraph line that would otherwise be read as a
// block marker on the next parse.
func protectLineStart(line string) string {
	switch {
	case line == "":
		return line
	case strings.HasPrefix(line, "#"):
		return "\\" + line
	case strings.HasPrefix(line, ">"):
		return "\\" + line
	case strings.HasPrefix(line, ":::"):
		return "\\" + line
	case strings.HasPrefix(line, "---") && strings.Trim(line, "-") == "":
		return "\\" + line
	case strings.HasPrefix(line, "- "), strings.HasPrefix(line, "-\t"), line == "-":
		return "\\" + line
	case strings.HasPrefix(line, "+ "), strings.HasPrefix(line, "+\t"), line == "+":
		return "\\" + line
	}
	i := 0
	for i < len(line) && line[i] >= '0' && line[i] <= '9' {
		i++
	}
	if i > 0 && i < len(line) && (line[i] == '.' || line[i] == ')') &&
		(i+1 == len(line) || line[i+1] == ' ' || line[i+1] == '\t') {
		return line[:i] + "\\" + line[i:]
	}
	if len(line) >= 2 && isASCIILetter(line[0]) && (line[1] == '.' || line[1] == ')') &&
		(len(line) == 2 || line[2] == ' ' || line[2] == '\t') {
		return line[:1] + "\\" + line[1:]
	}
	return line
}

// ---------------------------------------------------------------------------
// Shared helpers
// ---------------------------------------------------------------------------

func withText(base Run, text string) Run {
	base.Text = text
	return base
}

func mergeRuns(runs []Run) []Run {
	out := make([]Run, 0, len(runs))
	for _, run := range runs {
		if run.Text == "" && run.Type == "" {
			continue
		}
		if len(out) > 0 && sameRunFormat(out[len(out)-1], run) {
			out[len(out)-1].Text += run.Text
			continue
		}
		out = append(out, run)
	}
	if out == nil {
		out = []Run{}
	}
	return out
}

func sameRunFormat(a, b Run) bool {
	if a.Type != "" || b.Type != "" {
		return false
	}
	return a.Bold == b.Bold && a.Italic == b.Italic && a.Underline == b.Underline &&
		a.Strike == b.Strike && a.Code == b.Code && a.Highlight == b.Highlight &&
		a.Href == b.Href && a.TypeID == b.TypeID && a.ObjectID == b.ObjectID
}

func runsText(runs []Run) string {
	var out strings.Builder
	for _, run := range runs {
		out.WriteString(run.Text)
	}
	return out.String()
}

func leadingSpaces(line string) (int, string) {
	columns := 0
	i := 0
	for i < len(line) {
		switch line[i] {
		case ' ':
			columns++
			i++
		case '\t':
			columns += 4 - columns%4
			i++
		default:
			return columns, line[i:]
		}
	}
	return columns, ""
}

func leadingColumns(line string) int {
	columns, _ := leadingSpaces(line)
	return columns
}

func isColumnsFence(trimmed string) bool {
	switch strings.ToLower(trimmed) {
	case "::: columns", ":::columns", "::: {.columns}":
		return true
	}
	return false
}

func isColumnFence(trimmed string) bool {
	switch strings.ToLower(trimmed) {
	case "::: column", ":::column", "::: {.column}":
		return true
	}
	return false
}

func codeFenceOpen(line string) (codeFence, string, bool) {
	columns, rest := leadingSpaces(line)
	if columns > 3 || len(rest) < 3 {
		return codeFence{}, "", false
	}
	c := rest[0]
	if c != '`' && c != '~' {
		return codeFence{}, "", false
	}
	length := 1
	for length < len(rest) && rest[length] == c {
		length++
	}
	if length < 3 {
		return codeFence{}, "", false
	}
	info := strings.TrimSpace(rest[length:])
	if c == '`' && strings.Contains(info, "`") {
		return codeFence{}, "", false
	}
	return codeFence{char: c, length: length}, info, true
}

type codeFence struct {
	char   byte
	length int
}

func isClosingFence(line string, char byte, length int) bool {
	columns, rest := leadingSpaces(line)
	if columns > 3 || len(rest) < length {
		return false
	}
	for i := 0; i < length; i++ {
		if rest[i] != char {
			return false
		}
	}
	return strings.TrimSpace(rest[length:]) == ""
}

func toggleLine(line string) (string, string, bool) {
	columns, rest := leadingSpaces(line)
	if columns > 3 || rest == "" {
		return "", "", false
	}
	marker, size := utf8.DecodeRuneInString(rest)
	if marker != '▶' && marker != '▼' {
		return "", "", false
	}
	after := rest[size:]
	if after != "" && after[0] != ' ' && after[0] != '\t' {
		return "", "", false
	}
	return string(marker), strings.TrimLeft(after, " \t"), true
}

func smallLine(line string) (string, bool) {
	trimmed := strings.TrimSpace(line)
	if strings.HasPrefix(trimmed, "<small>") && strings.HasSuffix(trimmed, "</small>") && len(trimmed) >= len("<small></small>") {
		return trimmed[len("<small>") : len(trimmed)-len("</small>")], true
	}
	return "", false
}

func bareTodoLine(line string) (bool, string, bool) {
	trimmed := strings.TrimLeft(line, " \t")
	if len(trimmed) < 3 || trimmed[0] != '[' || trimmed[2] != ']' {
		return false, "", false
	}
	if trimmed[1] != ' ' && trimmed[1] != 'x' && trimmed[1] != 'X' {
		return false, "", false
	}
	after := trimmed[3:]
	if after != "" && after[0] != ' ' && after[0] != '\t' {
		return false, "", false
	}
	return trimmed[1] != ' ', strings.TrimLeft(after, " \t"), true
}

func singleLineMath(line string) (string, bool) {
	trimmed := strings.TrimSpace(line)
	if len(trimmed) > 4 && strings.HasPrefix(trimmed, "$$") && strings.HasSuffix(trimmed, "$$") {
		latex := strings.TrimSpace(trimmed[2 : len(trimmed)-2])
		if latex != "" {
			return latex, true
		}
	}
	return "", false
}

func mathBlockLines(lines []string) (string, bool) {
	if len(lines) < 2 {
		return "", false
	}
	if strings.TrimSpace(lines[0]) != "$$" || strings.TrimSpace(lines[len(lines)-1]) != "$$" {
		return "", false
	}
	latex := strings.Join(lines[1:len(lines)-1], "\n")
	if strings.TrimSpace(latex) == "" {
		return "", false
	}
	return latex, true
}

func isASCIILetter(c byte) bool {
	return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
}

func isWordByte(c byte) bool {
	return isASCIILetter(c) || (c >= '0' && c <= '9') || c == '_' || c >= 0x80
}

func isWordRune(r rune) bool {
	return unicode.IsLetter(r) || unicode.IsDigit(r) || r == '_'
}
