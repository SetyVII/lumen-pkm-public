package main

import (
	"bytes"
	"fmt"
	"net/url"
	"strings"
	"unicode"
	"unicode/utf8"

	readability "github.com/go-shiori/go-readability"
	"golang.org/x/net/html"
)

// readerContentLimit caps the Markdown stored in properties.reader_content.
// The HTML fetch is already capped at 2 MB; this second cap keeps a single
// weblink from bloating the encrypted store or mobile sync payloads.
const readerContentLimit = 500 * 1024

// readerSkipTags are never part of a readable article. Navigation, forms and
// embeds are chrome, not prose; script/style text would poison search.
var readerSkipTags = map[string]bool{
	"script": true, "style": true, "noscript": true, "template": true,
	"svg": true, "canvas": true, "iframe": true, "object": true, "embed": true,
	"form": true, "button": true, "input": true, "select": true, "textarea": true,
	"option": true, "nav": true, "footer": true, "aside": true, "dialog": true,
	"head": true, "link": true, "meta": true,
}

var readerSkipRoles = map[string]bool{
	"navigation": true, "banner": true, "contentinfo": true, "complementary": true,
	"search": true, "dialog": true, "alertdialog": true, "menu": true,
	"menubar": true, "toolbar": true, "form": true,
}

// readerBoilerplateWords are matched against class/id tokens split on dashes
// and underscores. Exact tokens only: "commentary" is prose, "comments" is not.
var readerBoilerplateWords = map[string]bool{
	"ad": true, "ads": true, "advert": true, "advertisement": true, "advertising": true,
	"promo": true, "promotion": true, "sponsor": true, "sponsored": true,
	"newsletter": true, "subscribe": true, "subscription": true,
	"cookie": true, "cookies": true, "consent": true, "gdpr": true,
	"share": true, "sharing": true, "social": true,
	"related": true, "recommended": true, "recirculation": true,
	"sidebar": true, "menu": true, "navbar": true, "navigation": true,
	"breadcrumb": true, "breadcrumbs": true,
	"comment": true, "comments": true, "disqus": true,
	"popup": true, "modal": true, "overlay": true,
	"banner": true, "toolbar": true,
}

// readerArticle is the deterministic result of one HTML fetch: Markdown for
// properties.reader_content plus the favicon URL, when the page declares one.
type readerArticle struct {
	Content string
	Favicon string
}

// extractReaderArticle runs Mozilla Readability (via the MIT go-shiori port) on
// the HTML the capture pipeline already fetched, then walks the cleaned node to
// produce Markdown. It never fails: an empty result means the page had no usable
// article text, and callers must keep the capture.
func extractReaderArticle(body []byte, base *url.URL) (result readerArticle) {
	// Untrusted HTML reaches a third-party parser. A panic must degrade to an
	// empty article, never fail the capture or the refresh request.
	defer func() {
		if recover() != nil {
			result = readerArticle{}
		}
	}()
	if len(body) == 0 {
		return readerArticle{}
	}
	doc, err := html.Parse(bytes.NewReader(body))
	if err != nil {
		return readerArticle{}
	}
	parser := readability.NewParser()
	// Readability strips classes by default. Keeping them lets the Markdown
	// walker recover fenced-code languages such as class="language-go".
	parser.KeepClasses = true
	article, err := parser.ParseDocument(doc, base)
	if err != nil || article.Node == nil {
		return readerArticle{}
	}
	r := &readerRenderer{base: base}
	text := normalizeReaderMarkdown(r.renderContainer(article.Node, 0))
	// Readability promotes the article's h1 to Article.Title and removes it
	// from the content node. Lumen stores the object title separately, but a
	// self-contained Markdown body is more useful for export and search, so the
	// title is restored unless the body already starts with the same heading.
	if title := strings.TrimSpace(article.Title); text != "" && title != "" && !readerHeadingPresent(text, title) {
		text = "# " + title + "\n\n" + text
	}
	return readerArticle{
		Content: truncateReaderContent(text),
		Favicon: readerFavicon(article.Favicon),
	}
}

// readerFavicon keeps only a bounded absolute http(s) URL. Readability resolves
// relative hrefs, but a hostile page can still declare a data: icon.
func readerFavicon(raw string) string {
	raw = strings.TrimSpace(raw)
	if raw == "" || len(raw) > 2048 {
		return ""
	}
	parsed, err := url.Parse(raw)
	if err != nil || parsed.Host == "" || (parsed.Scheme != "http" && parsed.Scheme != "https") {
		return ""
	}
	return parsed.String()
}

func readerHeadingPresent(content, title string) bool {
	for _, line := range strings.Split(content, "\n") {
		if !strings.HasPrefix(line, "#") {
			continue
		}
		if strings.EqualFold(strings.TrimSpace(strings.TrimLeft(line, "#")), title) {
			return true
		}
	}
	return false
}

// extractReaderContent is the Markdown-only view used by tests and callers that
// do not need the favicon.
func extractReaderContent(body []byte, base *url.URL) string {
	return extractReaderArticle(body, base).Content
}

func findElement(n *html.Node, tag string) *html.Node {
	if n.Type == html.ElementNode && n.Data == tag {
		return n
	}
	for child := n.FirstChild; child != nil; child = child.NextSibling {
		if found := findElement(child, tag); found != nil {
			return found
		}
	}
	return nil
}

func readerSkipped(n *html.Node) bool {
	if n.Type != html.ElementNode {
		return false
	}
	if readerSkipTags[n.Data] {
		return true
	}
	// A site header is chrome; an <header> inside <article> usually carries the
	// title and byline, so only direct body children are dropped.
	if n.Data == "header" && n.Parent != nil && n.Parent.Data == "body" {
		return true
	}
	for _, attr := range n.Attr {
		switch strings.ToLower(attr.Key) {
		case "hidden":
			return true
		case "aria-hidden":
			if strings.EqualFold(strings.TrimSpace(attr.Val), "true") {
				return true
			}
		case "role":
			if readerSkipRoles[strings.ToLower(strings.TrimSpace(attr.Val))] {
				return true
			}
		case "class", "id":
			if readerBoilerplate(attr.Val) {
				return true
			}
		}
	}
	return false
}

func readerBoilerplate(value string) bool {
	for _, token := range strings.Fields(strings.ToLower(value)) {
		for _, part := range strings.FieldsFunc(token, func(r rune) bool { return r == '-' || r == '_' }) {
			if readerBoilerplateWords[part] {
				return true
			}
		}
	}
	return false
}

type readerRenderer struct {
	base *url.URL
}

func (r *readerRenderer) renderContainer(n *html.Node, depth int) string {
	var out strings.Builder
	var inline strings.Builder
	flush := func() {
		text := strings.TrimSpace(inline.String())
		inline.Reset()
		appendReaderBlock(&out, text)
	}
	for child := n.FirstChild; child != nil; child = child.NextSibling {
		switch child.Type {
		case html.TextNode:
			inline.WriteString(collapseReaderText(child.Data))
		case html.ElementNode:
			if readerSkipped(child) {
				continue
			}
			if readerBlockTag(child.Data) {
				flush()
				appendReaderBlock(&out, r.renderBlock(child, depth))
			} else {
				inline.WriteString(r.renderInline(child))
			}
		}
	}
	flush()
	return out.String()
}

func appendReaderBlock(out *strings.Builder, block string) {
	block = strings.Trim(block, "\n")
	if block == "" {
		return
	}
	if out.Len() > 0 {
		out.WriteString("\n\n")
	}
	out.WriteString(block)
}

func (r *readerRenderer) renderBlock(n *html.Node, depth int) string {
	switch n.Data {
	case "h1", "h2", "h3", "h4", "h5", "h6":
		level := int(n.Data[1] - '0')
		text := strings.TrimSpace(r.renderInlineChildren(n))
		if text == "" {
			return ""
		}
		return strings.Repeat("#", level) + " " + text
	case "p":
		return strings.TrimSpace(r.renderInlineChildren(n))
	case "ul":
		return r.renderList(n, depth, false)
	case "ol":
		return r.renderList(n, depth, true)
	case "li":
		return "- " + strings.TrimSpace(r.renderInlineChildren(n))
	case "blockquote":
		inner := strings.TrimSpace(r.renderContainer(n, depth))
		if inner == "" {
			return ""
		}
		lines := strings.Split(inner, "\n")
		for i, line := range lines {
			if line == "" {
				lines[i] = ">"
			} else {
				lines[i] = "> " + line
			}
		}
		return strings.Join(lines, "\n")
	case "pre":
		return r.renderCodeBlock(n)
	case "hr":
		return "---"
	case "table":
		return r.renderTable(n)
	default:
		if readerBlockTag(n.Data) {
			return r.renderContainer(n, depth)
		}
		return strings.TrimSpace(r.renderInline(n))
	}
}

func (r *readerRenderer) renderInlineChildren(n *html.Node) string {
	var out strings.Builder
	for child := n.FirstChild; child != nil; child = child.NextSibling {
		switch child.Type {
		case html.TextNode:
			out.WriteString(collapseReaderText(child.Data))
		case html.ElementNode:
			if readerSkipped(child) {
				continue
			}
			if readerBlockTag(child.Data) {
				// Block content inside an inline context (for example a <div>
				// inside a <p>) keeps its text instead of disappearing.
				out.WriteString(" ")
				out.WriteString(strings.TrimSpace(r.renderBlock(child, 0)))
				out.WriteString(" ")
			} else {
				out.WriteString(r.renderInline(child))
			}
		}
	}
	return strings.TrimSpace(out.String())
}

func (r *readerRenderer) renderInline(n *html.Node) string {
	switch n.Data {
	case "br":
		return "\n"
	case "strong", "b":
		return wrapReaderInline("**", r.renderInlineChildren(n))
	case "em", "i":
		return wrapReaderInline("*", r.renderInlineChildren(n))
	case "del", "s", "strike":
		return wrapReaderInline("~~", r.renderInlineChildren(n))
	case "code", "kbd", "samp":
		text := strings.TrimSpace(collapseReaderText(readerTextContent(n)))
		if text == "" {
			return ""
		}
		fence := "`"
		for strings.Contains(text, fence) {
			fence += "`"
		}
		return fence + text + fence
	case "a":
		text := r.renderInlineChildren(n)
		if text == "" {
			return ""
		}
		if target := r.resolveLink(readerAttr(n, "href")); target != "" {
			return "[" + text + "](" + target + ")"
		}
		return text
	case "img":
		source := r.resolveLink(readerAttr(n, "src"))
		if source == "" {
			return ""
		}
		return "![" + strings.TrimSpace(readerAttr(n, "alt")) + "](" + source + ")"
	default:
		return r.renderInlineChildren(n)
	}
}

func wrapReaderInline(marker, text string) string {
	if text == "" {
		return ""
	}
	return marker + text + marker
}

func (r *readerRenderer) resolveLink(raw string) string {
	raw = strings.TrimSpace(raw)
	if raw == "" || strings.HasPrefix(raw, "#") {
		return ""
	}
	parsed, err := url.Parse(raw)
	if err != nil {
		return ""
	}
	if r.base != nil {
		parsed = r.base.ResolveReference(parsed)
	}
	if parsed.Scheme != "http" && parsed.Scheme != "https" {
		return ""
	}
	return parsed.String()
}

func (r *readerRenderer) renderList(n *html.Node, depth int, ordered bool) string {
	indent := strings.Repeat("  ", depth)
	var out strings.Builder
	index := 1
	for child := n.FirstChild; child != nil; child = child.NextSibling {
		if child.Type != html.ElementNode || child.Data != "li" || readerSkipped(child) {
			continue
		}
		marker := "- "
		if ordered {
			marker = fmt.Sprintf("%d. ", index)
			index++
		}
		var inline strings.Builder
		var nestedLists []string
		var nestedBlocks []string
		for item := child.FirstChild; item != nil; item = item.NextSibling {
			switch item.Type {
			case html.TextNode:
				inline.WriteString(collapseReaderText(item.Data))
			case html.ElementNode:
				if readerSkipped(item) {
					continue
				}
				switch {
				case item.Data == "ul" || item.Data == "ol":
					nestedLists = append(nestedLists, r.renderList(item, depth+1, item.Data == "ol"))
				case readerBlockTag(item.Data):
					nestedBlocks = append(nestedBlocks, r.renderBlock(item, depth+1))
				default:
					inline.WriteString(r.renderInline(item))
				}
			}
		}
		text := strings.TrimSpace(inline.String())
		if text == "" && len(nestedLists) == 0 && len(nestedBlocks) == 0 {
			continue
		}
		if out.Len() > 0 {
			out.WriteString("\n")
		}
		out.WriteString(indent + marker + text)
		for _, block := range nestedBlocks {
			block = strings.TrimSpace(block)
			if block == "" {
				continue
			}
			for _, line := range strings.Split(block, "\n") {
				out.WriteString("\n")
				if line != "" {
					out.WriteString(indent + "  " + line)
				}
			}
		}
		for _, list := range nestedLists {
			list = strings.TrimRight(list, "\n")
			if list == "" {
				continue
			}
			out.WriteString("\n")
			out.WriteString(list)
		}
	}
	return out.String()
}

func (r *readerRenderer) renderCodeBlock(n *html.Node) string {
	language := ""
	if code := findElement(n, "code"); code != nil {
		language = readerLanguage(code)
	}
	if language == "" {
		language = readerLanguage(n)
	}
	text := strings.TrimRight(readerTextContent(n), "\n")
	if strings.TrimSpace(text) == "" {
		return ""
	}
	fence := "```"
	for strings.Contains(text, fence) {
		fence += "`"
	}
	return fence + language + "\n" + text + "\n" + fence
}

func readerLanguage(n *html.Node) string {
	for _, class := range strings.Fields(readerAttr(n, "class")) {
		if strings.HasPrefix(class, "language-") {
			return strings.TrimPrefix(class, "language-")
		}
	}
	return ""
}

func (r *readerRenderer) renderTable(n *html.Node) string {
	rows := [][]string{}
	var walk func(*html.Node)
	walk = func(node *html.Node) {
		if node.Type == html.ElementNode && node.Data == "tr" {
			cells := []string{}
			for cell := node.FirstChild; cell != nil; cell = cell.NextSibling {
				if cell.Type == html.ElementNode && (cell.Data == "td" || cell.Data == "th") {
					cells = append(cells, escapeReaderTableCell(r.renderInlineChildren(cell)))
				}
			}
			if len(cells) > 0 {
				rows = append(rows, cells)
			}
			return
		}
		for child := node.FirstChild; child != nil; child = child.NextSibling {
			walk(child)
		}
	}
	walk(n)
	if len(rows) == 0 {
		return ""
	}
	width := 0
	for _, row := range rows {
		if len(row) > width {
			width = len(row)
		}
	}
	var out strings.Builder
	for i, row := range rows {
		for len(row) < width {
			row = append(row, "")
		}
		out.WriteString("| " + strings.Join(row, " | ") + " |")
		if i == 0 {
			out.WriteString("\n|")
			for range row {
				out.WriteString(" --- |")
			}
		}
		if i < len(rows)-1 {
			out.WriteString("\n")
		}
	}
	return out.String()
}

func escapeReaderTableCell(value string) string {
	value = strings.ReplaceAll(value, "|", `\|`)
	return strings.ReplaceAll(value, "\n", " ")
}

func readerBlockTag(tag string) bool {
	switch tag {
	case "address", "article", "aside", "blockquote", "caption", "dd", "details",
		"dialog", "div", "dl", "dt", "fieldset", "figcaption", "figure", "footer",
		"form", "h1", "h2", "h3", "h4", "h5", "h6", "header", "hr", "li", "main",
		"nav", "ol", "p", "pre", "section", "summary", "table", "tbody", "td",
		"tfoot", "th", "thead", "tr", "ul":
		return true
	}
	return false
}

func readerAttr(n *html.Node, key string) string {
	for _, attr := range n.Attr {
		if strings.EqualFold(attr.Key, key) {
			return attr.Val
		}
	}
	return ""
}

func readerTextContent(n *html.Node) string {
	var out strings.Builder
	var walk func(*html.Node)
	walk = func(node *html.Node) {
		if node.Type == html.TextNode {
			out.WriteString(node.Data)
			return
		}
		if node.Type == html.ElementNode && readerSkipped(node) {
			return
		}
		for child := node.FirstChild; child != nil; child = child.NextSibling {
			walk(child)
		}
	}
	walk(n)
	return out.String()
}

// collapseReaderText keeps inline words separated while dropping the newlines
// and indentation that HTML source uses for layout.
func collapseReaderText(value string) string {
	var out strings.Builder
	pendingSpace := false
	for _, r := range value {
		if unicode.IsSpace(r) {
			pendingSpace = true
			continue
		}
		if pendingSpace {
			out.WriteByte(' ')
			pendingSpace = false
		}
		out.WriteRune(r)
	}
	if pendingSpace {
		out.WriteByte(' ')
	}
	return out.String()
}

func normalizeReaderMarkdown(value string) string {
	lines := strings.Split(value, "\n")
	out := make([]string, 0, len(lines))
	blank := false
	for _, line := range lines {
		line = strings.TrimRight(line, " \t\r")
		if line == "" {
			if len(out) > 0 && !blank {
				out = append(out, "")
				blank = true
			}
			continue
		}
		out = append(out, line)
		blank = false
	}
	for len(out) > 0 && out[len(out)-1] == "" {
		out = out[:len(out)-1]
	}
	return strings.Join(out, "\n")
}

func truncateReaderContent(value string) string {
	if len(value) <= readerContentLimit {
		return value
	}
	cut := readerContentLimit
	for cut > 0 && !utf8.RuneStart(value[cut]) {
		cut--
	}
	return strings.TrimRight(value[:cut], " \t\n")
}

// objectMatchesQuery is the full-text predicate shared by /v1/objects and
// /v1/search. Title, content and the extracted article use the same
// case-insensitive substring rule (no weighting); result order remains
// updated_at desc, id asc.
// preserveReaderMetadata keeps the server-derived article and favicon when a
// client sends a full properties snapshot. capture and refresh-reader are the
// only writers; PATCH must not erase them or forge different values.
func preserveReaderMetadata(o *Object, previous map[string]any) {
	if o.TypeID != "web" {
		delete(o.Properties, "reader_content")
		delete(o.Properties, "favicon")
		return
	}
	for _, key := range []string{"reader_content", "favicon"} {
		if value, ok := previous[key]; ok {
			o.Properties[key] = value
		} else {
			delete(o.Properties, key)
		}
	}
}
