package main

import (
	"encoding/json"
	"reflect"
	"strings"
	"testing"
)

// representativeMarkdown exercises every construct the converter supports.
func representativeMarkdown() string {
	return strings.Join([]string{
		"# Title",
		"",
		"Intro with **bold**, *italic*, `code`, ~~strike~~, ==mark== and a [link](https://example.com).",
		"",
		"- one",
		"  - nested",
		"- two",
		"",
		"1. first",
		"2. second",
		"",
		"> quoted",
		"",
		"- [ ] open task",
		"- [x] done task",
		"",
		"```go",
		`fmt.Println("hi")`,
		"```",
		"",
		"$$",
		"E = mc^2",
		"$$",
		"",
		"| a | b |",
		"| --- | ---: |",
		"| 1 | 2 |",
		"",
		"---",
		"",
		"[[Wiki Page]] and @alice",
		"",
		"▶ toggle",
		"",
		">> group",
		"",
		"<small>small note</small>",
		"",
		"::: columns",
		"::: column",
		"left **one**",
		":::",
		"::: column",
		"right",
		":::",
		":::",
		"",
	}, "\n")
}

func canonicalMarkdown() string {
	return strings.Join([]string{
		"# Title",
		"",
		"Intro with **bold**, *italic*, `code`, ~~strike~~, ==mark== and a [link](https://example.com).",
		"",
		"- one",
		"  - nested",
		"- two",
		"",
		"1. first",
		"1. second",
		"",
		"> quoted",
		"",
		"- [ ] open task",
		"- [x] done task",
		"",
		"```go",
		`fmt.Println("hi")`,
		"```",
		"",
		"$$E = mc^2$$",
		"",
		"| a | b |",
		"| --- | ---: |",
		"| 1 | 2 |",
		"",
		"---",
		"",
		"[[Wiki Page]] and [[alice]]",
		"",
		"▶ toggle",
		"",
		">> group",
		"",
		"<small>small note</small>",
		"",
		"::: columns",
		"::: column",
		"left **one**",
		":::",
		"::: column",
		"right",
		":::",
		":::",
		"",
	}, "\n")
}

func TestMarkdownToBlocksEveryConstruct(t *testing.T) {
	blocks, err := MarkdownToBlocks(representativeMarkdown())
	if err != nil {
		t.Fatal(err)
	}
	wantTypes := []string{
		blockHeading, blockParagraph,
		blockBullet, blockBullet, blockBullet,
		blockNumbered, blockNumbered,
		blockQuote,
		blockTodo, blockTodo,
		blockCode, blockMath, blockTable, blockDivider,
		blockParagraph, blockToggle, blockGroup, blockSmall, blockColumns,
	}
	if len(blocks) != len(wantTypes) {
		t.Fatalf("got %d blocks, want %d: %+v", len(blocks), len(wantTypes), blocks)
	}
	for i, want := range wantTypes {
		if blocks[i].Type != want {
			t.Fatalf("block %d type = %q, want %q", i, blocks[i].Type, want)
		}
	}
	if blocks[0].Level != 1 || blocks[0].Text != "Title" {
		t.Fatalf("heading = %+v", blocks[0])
	}
	if blocks[2].Indent != 1 || blocks[3].Indent != 2 || blocks[4].Indent != 1 {
		t.Fatalf("list indents = %d,%d,%d", blocks[2].Indent, blocks[3].Indent, blocks[4].Indent)
	}
	if !strings.HasPrefix(blocks[8].Text, todoOpen) || !strings.HasPrefix(blocks[9].Text, todoDone) {
		t.Fatalf("todo markers = %q, %q", blocks[8].Text, blocks[9].Text)
	}
	if blocks[10].Language != "go" || blocks[10].Text != `fmt.Println("hi")` {
		t.Fatalf("code block = %+v", blocks[10])
	}
	if len(blocks[11].Runs) != 1 || blocks[11].Runs[0].Latex != "E = mc^2" || !blocks[11].Runs[0].Display {
		t.Fatalf("math block = %+v", blocks[11])
	}
	if !reflect.DeepEqual(blocks[12].Align, []string{"", "right"}) {
		t.Fatalf("table align = %v", blocks[12].Align)
	}
	if len(blocks[12].Rows) != 2 || len(blocks[12].Rows[0]) != 2 {
		t.Fatalf("table shape = %+v", blocks[12].Rows)
	}
	links := []Run{}
	for _, run := range blocks[14].Runs {
		if run.Href == objectHrefPrefix {
			links = append(links, run)
		}
	}
	if len(links) != 2 || links[0].Text != "Wiki Page" || links[1].Text != "alice" {
		t.Fatalf("object links = %+v", links)
	}
	if blocks[15].Text != "▶ toggle" || blocks[16].Text != "group" || blocks[17].Text != "small note" {
		t.Fatalf("toggle/group/small = %q, %q, %q", blocks[15].Text, blocks[16].Text, blocks[17].Text)
	}
	if len(blocks[18].Columns) != 2 || len(blocks[18].Columns[0]) != 1 {
		t.Fatalf("columns = %+v", blocks[18].Columns)
	}
	first := blocks[18].Columns[0][0]
	if first.Type != blockParagraph || len(first.Runs) != 2 || !first.Runs[1].Bold || first.Runs[1].Text != "one" {
		t.Fatalf("column content = %+v", first)
	}
}

func TestMarkdownRoundTrip(t *testing.T) {
	blocks, err := MarkdownToBlocks(representativeMarkdown())
	if err != nil {
		t.Fatal(err)
	}
	canonical := BlocksToMarkdown(blocks)
	if canonical != canonicalMarkdown() {
		t.Fatalf("canonical markdown mismatch\n--- got ---\n%s\n--- want ---\n%s", canonical, canonicalMarkdown())
	}
	again, err := MarkdownToBlocks(canonical)
	if err != nil {
		t.Fatal(err)
	}
	if second := BlocksToMarkdown(again); second != canonical {
		t.Fatalf("round-trip is not idempotent\n--- first ---\n%s\n--- second ---\n%s", canonical, second)
	}
	if !reflect.DeepEqual(normalizeBlockIDs(blocks), normalizeBlockIDs(again)) {
		t.Fatalf("round-trip changed the block structure")
	}
}

func TestBlocksToMarkdownDeterministic(t *testing.T) {
	first, err := MarkdownToBlocks(representativeMarkdown())
	if err != nil {
		t.Fatal(err)
	}
	second, err := MarkdownToBlocks(representativeMarkdown())
	if err != nil {
		t.Fatal(err)
	}
	if a, b := BlocksToMarkdown(first), BlocksToMarkdown(second); a != b {
		t.Fatal("same input produced different markdown")
	}
	if a, b := BlocksToMarkdown(first), BlocksToMarkdown(first); a != b {
		t.Fatal("repeated rendering produced different markdown")
	}
	if !reflect.DeepEqual(normalizeBlockIDs(first), normalizeBlockIDs(second)) {
		t.Fatal("same input produced different block structures")
	}
}

func TestInlineEscapingRoundTrip(t *testing.T) {
	input := "A literal \\* star, \\_under\\_, `tick`, \\[brackets\\], \\<angle\\>, \\$5, a \\~ b, a \\= b and a | pipe.\n"
	blocks, err := MarkdownToBlocks(input)
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 1 || blocks[0].Type != blockParagraph {
		t.Fatalf("blocks = %+v", blocks)
	}
	want := "A literal * star, _under_, tick, [brackets], <angle>, $5, a ~ b, a = b and a | pipe."
	if got := runsText(blocks[0].Runs); got != want {
		t.Fatalf("text = %q, want %q", got, want)
	}
	rendered := BlocksToMarkdown(blocks)
	if !strings.Contains(rendered, "\\*") || !strings.Contains(rendered, "\\_") || !strings.Contains(rendered, "\\$") {
		t.Fatalf("markers were not escaped: %q", rendered)
	}
	reparsed, err := MarkdownToBlocks(rendered)
	if err != nil {
		t.Fatal(err)
	}
	if got := runsText(reparsed[0].Runs); got != want {
		t.Fatalf("round-trip text = %q, want %q", got, want)
	}
}

func TestTableEscapingAndFormatting(t *testing.T) {
	input := strings.Join([]string{
		"| a \\| b | `x\\|y` |",
		"| --- | --- |",
		"| **bold** | c |",
		"",
	}, "\n")
	blocks, err := MarkdownToBlocks(input)
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 1 || blocks[0].Type != blockTable {
		t.Fatalf("blocks = %+v", blocks)
	}
	table := blocks[0]
	if got := runsText(table.Rows[0][0].Runs); got != "a | b" {
		t.Fatalf("escaped pipe cell = %q", got)
	}
	if len(table.Rows[0][1].Runs) != 1 || !table.Rows[0][1].Runs[0].Code || table.Rows[0][1].Runs[0].Text != "x|y" {
		t.Fatalf("code cell = %+v", table.Rows[0][1].Runs)
	}
	if len(table.Rows[1][0].Runs) != 1 || !table.Rows[1][0].Runs[0].Bold {
		t.Fatalf("bold cell = %+v", table.Rows[1][0].Runs)
	}
	rendered := BlocksToMarkdown(blocks)
	if !strings.Contains(rendered, "a \\| b") || !strings.Contains(rendered, "`x\\|y`") {
		t.Fatalf("table pipes were not escaped: %q", rendered)
	}
	reparsed, err := MarkdownToBlocks(rendered)
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(normalizeBlockIDs(blocks), normalizeBlockIDs(reparsed)) {
		t.Fatal("table round-trip changed the block structure")
	}
}

func TestNestedLists(t *testing.T) {
	input := "- a\n  - b\n    - c\n- d\n\n1. one\n   - sub\n2. two\n"
	blocks, err := MarkdownToBlocks(input)
	if err != nil {
		t.Fatal(err)
	}
	wantTypes := []string{blockBullet, blockBullet, blockBullet, blockBullet, blockNumbered, blockBullet, blockNumbered}
	wantIndents := []int{1, 2, 3, 1, 1, 2, 1}
	if len(blocks) != len(wantTypes) {
		t.Fatalf("blocks = %+v", blocks)
	}
	for i := range wantTypes {
		if blocks[i].Type != wantTypes[i] || blocks[i].Indent != wantIndents[i] {
			t.Fatalf("block %d = %s/%d, want %s/%d", i, blocks[i].Type, blocks[i].Indent, wantTypes[i], wantIndents[i])
		}
	}
	reparsed, err := MarkdownToBlocks(BlocksToMarkdown(blocks))
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(normalizeBlockIDs(blocks), normalizeBlockIDs(reparsed)) {
		t.Fatal("nested list round-trip changed the block structure")
	}
}

func TestWikilinkAndMentionSpans(t *testing.T) {
	blocks, err := MarkdownToBlocks("See [[Project Alpha]] and @alice.\n")
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 1 {
		t.Fatalf("blocks = %+v", blocks)
	}
	runs := blocks[0].Runs
	if len(runs) != 5 {
		t.Fatalf("runs = %+v", runs)
	}
	if runs[1].Text != "Project Alpha" || runs[1].Href != objectHrefPrefix || runs[1].ObjectID != "" || runs[1].TypeID != "" {
		t.Fatalf("wikilink run = %+v", runs[1])
	}
	if runs[3].Text != "alice" || runs[3].Href != objectHrefPrefix || runs[3].ObjectID != "" {
		t.Fatalf("mention run = %+v", runs[3])
	}
	if got := BlocksToMarkdown(blocks); got != "See [[Project Alpha]] and [[alice]].\n" {
		t.Fatalf("markdown = %q", got)
	}
}

func TestMathInlineAndBlock(t *testing.T) {
	blocks, err := MarkdownToBlocks("Inline $x^2$ and display:\n\n$$\nE = mc^2\n$$\n")
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 2 || blocks[0].Type != blockParagraph || blocks[1].Type != blockMath {
		t.Fatalf("blocks = %+v", blocks)
	}
	inline := false
	for _, run := range blocks[0].Runs {
		if run.Type == "math" && run.Latex == "x^2" && !run.Display {
			inline = true
		}
	}
	if !inline {
		t.Fatalf("inline math missing: %+v", blocks[0].Runs)
	}
	if blocks[1].Runs[0].Latex != "E = mc^2" || !blocks[1].Runs[0].Display {
		t.Fatalf("block math = %+v", blocks[1].Runs)
	}
	if got := BlocksToMarkdown(blocks); got != "Inline $x^2$ and display:\n\n$$E = mc^2$$\n" {
		t.Fatalf("markdown = %q", got)
	}
}

func TestCodeFenceWithBackticks(t *testing.T) {
	input := "````\n```\ninner\n```\n````\n"
	blocks, err := MarkdownToBlocks(input)
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 1 || blocks[0].Type != blockCode {
		t.Fatalf("blocks = %+v", blocks)
	}
	if got := strings.ReplaceAll(blocks[0].Text, lineSeparator, "\n"); got != "```\ninner\n```" {
		t.Fatalf("code text = %q", got)
	}
	rendered := BlocksToMarkdown(blocks)
	if rendered != input {
		t.Fatalf("markdown = %q, want %q", rendered, input)
	}
}

func TestColumnsWithBlankLines(t *testing.T) {
	input := "::: columns\n::: column\nfirst\n\nsecond\n:::\n::: column\nright\n:::\n:::\n"
	blocks, err := MarkdownToBlocks(input)
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 1 || blocks[0].Type != blockColumns {
		t.Fatalf("blocks = %+v", blocks)
	}
	if len(blocks[0].Columns) != 2 {
		t.Fatalf("columns = %+v", blocks[0].Columns)
	}
	if len(blocks[0].Columns[0]) != 2 || len(blocks[0].Columns[1]) != 1 {
		t.Fatalf("column blocks = %+v", blocks[0].Columns)
	}
	rendered := BlocksToMarkdown(blocks)
	if rendered != input {
		t.Fatalf("markdown = %q, want %q", rendered, input)
	}
	reparsed, err := MarkdownToBlocks(rendered)
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(normalizeBlockIDs(blocks), normalizeBlockIDs(reparsed)) {
		t.Fatal("columns round-trip changed the block structure")
	}
}

func TestColumnsWithoutSurroundingBlankLines(t *testing.T) {
	input := "before\n::: columns\n::: column\nleft\n:::\n:::\nafter\n"
	blocks, err := MarkdownToBlocks(input)
	if err != nil {
		t.Fatal(err)
	}
	wantTypes := []string{blockParagraph, blockColumns, blockParagraph}
	if len(blocks) != len(wantTypes) {
		t.Fatalf("blocks = %+v", blocks)
	}
	for i := range wantTypes {
		if blocks[i].Type != wantTypes[i] {
			t.Fatalf("block %d = %s, want %s", i, blocks[i].Type, wantTypes[i])
		}
	}
	if blocks[0].Text != "before" || blocks[2].Text != "after" {
		t.Fatalf("texts = %q, %q", blocks[0].Text, blocks[2].Text)
	}
	reparsed, err := MarkdownToBlocks(BlocksToMarkdown(blocks))
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(normalizeBlockIDs(blocks), normalizeBlockIDs(reparsed)) {
		t.Fatal("columns round-trip changed the block structure")
	}
}

func TestBareURLs(t *testing.T) {
	blocks, err := MarkdownToBlocks("See https://example.com/path?q=1 and <https://auto.example>.\n")
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 1 {
		t.Fatalf("blocks = %+v", blocks)
	}
	links := []Run{}
	for _, run := range blocks[0].Runs {
		if run.Href != "" {
			links = append(links, run)
		}
	}
	if len(links) != 2 {
		t.Fatalf("links = %+v", links)
	}
	if links[0].Href != "https://example.com/path?q=1" || links[0].Text != links[0].Href {
		t.Fatalf("bare URL = %+v", links[0])
	}
	if links[1].Href != "https://auto.example" || links[1].Text != links[1].Href {
		t.Fatalf("autolink = %+v", links[1])
	}
	if got := BlocksToMarkdown(blocks); got != "See <https://example.com/path?q=1> and <https://auto.example>.\n" {
		t.Fatalf("markdown = %q", got)
	}
}

func TestReferenceLink(t *testing.T) {
	blocks, err := MarkdownToBlocks("[text][ref] and [shortcut]\n\n[ref]: https://example.com\n[shortcut]: https://short.example\n")
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 1 {
		t.Fatalf("blocks = %+v", blocks)
	}
	if got := BlocksToMarkdown(blocks); got != "[text](https://example.com) and [shortcut](https://short.example)\n" {
		t.Fatalf("markdown = %q", got)
	}
}

func TestEmptyAndInvalidInput(t *testing.T) {
	blocks, err := MarkdownToBlocks("")
	if err != nil || len(blocks) != 0 {
		t.Fatalf("empty input = %+v, %v", blocks, err)
	}
	if got := BlocksToMarkdown(nil); got != "" {
		t.Fatalf("nil blocks = %q", got)
	}
	if _, err := MarkdownToBlocks(string([]byte{0xff, 0xfe})); err == nil {
		t.Fatal("invalid UTF-8 was accepted")
	}
}

func TestBlockJSONMatchesEditorSchema(t *testing.T) {
	cases := []struct {
		name  string
		block Block
		want  string
	}{
		{
			name:  "todo",
			block: Block{ID: "b1", Type: blockTodo, Text: "○ task", Indent: 1, Runs: []Run{{Text: "○ "}, {Text: "task"}}},
			want:  `{"id":"b1","type":"todo","text":"○ task","indent":1,"runs":[{"text":"○ "},{"text":"task"}]}`,
		},
		{
			name:  "heading with bold",
			block: Block{ID: "b2", Type: blockHeading, Text: "Hi", Level: 2, Runs: []Run{{Text: "Hi", Bold: true}}},
			want:  `{"id":"b2","type":"heading","text":"Hi","level":2,"runs":[{"text":"Hi","bold":true}]}`,
		},
		{
			name:  "code",
			block: Block{ID: "b3", Type: blockCode, Text: "a\u2028b", Language: "go", Runs: []Run{{Text: "a\u2028b", Code: true}}},
			want:  `{"id":"b3","type":"code","text":"a\u2028b","language":"go","runs":[{"text":"a\u2028b","code":true}]}`,
		},
		{
			name:  "math",
			block: Block{ID: "b4", Type: blockMath, Text: objectReplacement, Runs: []Run{{Text: objectReplacement, Type: "math", Latex: "x", Display: true}}},
			want:  `{"id":"b4","type":"math","text":"￼","runs":[{"text":"￼","type":"math","latex":"x","display":true}]}`,
		},
		{
			name:  "divider",
			block: Block{ID: "b5", Type: blockDivider, Text: objectReplacement, Runs: []Run{{Text: objectReplacement, Type: "image", ImageName: dividerImage}}},
			want:  `{"id":"b5","type":"divider","text":"￼","runs":[{"text":"￼","type":"image","image_name":"lumen-rule://divider/light"}]}`,
		},
		{
			name: "table",
			block: Block{ID: "b6", Type: blockTable, Runs: []Run{}, Align: []string{"", "right"}, Rows: [][]BlockCell{
				{{Runs: []Run{{Text: "a"}}}, {Runs: []Run{{Text: "b"}}}},
				{{Runs: []Run{{Text: "1"}}}, {Runs: []Run{{Text: "2"}}}},
			}},
			want: `{"id":"b6","type":"table","text":"","runs":[],"align":["","right"],"rows":[[{"runs":[{"text":"a"}]},{"runs":[{"text":"b"}]}],[{"runs":[{"text":"1"}]},{"runs":[{"text":"2"}]}]]}`,
		},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			raw, err := json.Marshal(tc.block)
			if err != nil {
				t.Fatal(err)
			}
			if string(raw) != tc.want {
				t.Fatalf("json mismatch\n got: %s\nwant: %s", raw, tc.want)
			}
		})
	}
}

func TestTextIsConcatenationOfRuns(t *testing.T) {
	blocks, err := MarkdownToBlocks(representativeMarkdown())
	if err != nil {
		t.Fatal(err)
	}
	var check func([]Block)
	check = func(items []Block) {
		for _, block := range items {
			if block.Text != runsText(block.Runs) {
				t.Fatalf("%s block text %q != runs %q", block.Type, block.Text, runsText(block.Runs))
			}
			for _, column := range block.Columns {
				check(column)
			}
		}
	}
	check(blocks)
}

func TestBlockIDsAreUnique(t *testing.T) {
	blocks, err := MarkdownToBlocks(representativeMarkdown())
	if err != nil {
		t.Fatal(err)
	}
	seen := map[string]bool{}
	var check func([]Block)
	check = func(items []Block) {
		for _, block := range items {
			if block.ID == "" || seen[block.ID] {
				t.Fatalf("empty or duplicate block id %q", block.ID)
			}
			seen[block.ID] = true
			for _, column := range block.Columns {
				check(column)
			}
		}
	}
	check(blocks)
}

func TestEditorShapedBlocksRender(t *testing.T) {
	raw := `[
		{"id":"x","type":"paragraph","text":"Hi","level":0,"alignment":1,"runs":[{"text":"Hi","bold":false,"italic":false,"underline":false,"strike":false,"code":false,"point_size":12}]},
		{"id":"y","type":"todo","text":"○ task","runs":[{"text":"○ "},{"text":"task","bold":false}]}
	]`
	var blocks []Block
	if err := json.Unmarshal([]byte(raw), &blocks); err != nil {
		t.Fatal(err)
	}
	if got := BlocksToMarkdown(blocks); got != "Hi\n\n- [ ] task\n" {
		t.Fatalf("markdown = %q", got)
	}
}

func TestSetextAndIndentedCode(t *testing.T) {
	blocks, err := MarkdownToBlocks("Title\n=====\n\n    code line\n")
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 2 || blocks[0].Type != blockHeading || blocks[0].Level != 1 || blocks[0].Text != "Title" {
		t.Fatalf("setext heading = %+v", blocks)
	}
	if blocks[1].Type != blockCode || strings.ReplaceAll(blocks[1].Text, lineSeparator, "\n") != "code line" {
		t.Fatalf("indented code = %+v", blocks[1])
	}
}

func TestHTMLBlockIsPreservedAsText(t *testing.T) {
	blocks, err := MarkdownToBlocks("<div>html block</div>\n")
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 1 || blocks[0].Text != "<div>html block</div>" {
		t.Fatalf("blocks = %+v", blocks)
	}
	if got := BlocksToMarkdown(blocks); got != "\\<div>html block\\</div>\n" {
		t.Fatalf("markdown = %q", got)
	}
	reparsed, err := MarkdownToBlocks(BlocksToMarkdown(blocks))
	if err != nil {
		t.Fatal(err)
	}
	if reparsed[0].Text != "<div>html block</div>" {
		t.Fatalf("round-trip text = %q", reparsed[0].Text)
	}
}

func TestBareTodoAndAlphabeticLists(t *testing.T) {
	blocks, err := MarkdownToBlocks("[ ] bare task\n[x] done bare\n\na. alpha\nb. beta\n")
	if err != nil {
		t.Fatal(err)
	}
	wantTypes := []string{blockTodo, blockTodo, blockAlphabetic, blockAlphabetic}
	if len(blocks) != len(wantTypes) {
		t.Fatalf("blocks = %+v", blocks)
	}
	for i := range wantTypes {
		if blocks[i].Type != wantTypes[i] {
			t.Fatalf("block %d = %s, want %s", i, blocks[i].Type, wantTypes[i])
		}
	}
	if !strings.HasPrefix(blocks[0].Text, todoOpen) || !strings.HasPrefix(blocks[1].Text, todoDone) {
		t.Fatalf("todo markers = %q, %q", blocks[0].Text, blocks[1].Text)
	}
	if got := BlocksToMarkdown(blocks); got != "- [ ] bare task\n- [x] done bare\n\na. alpha\na. beta\n" {
		t.Fatalf("markdown = %q", got)
	}
}

func TestQuoteVsGroup(t *testing.T) {
	blocks, err := MarkdownToBlocks(">> group text\n\n> > nested quote\n")
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 2 || blocks[0].Type != blockGroup || blocks[1].Type != blockQuote {
		t.Fatalf("blocks = %+v", blocks)
	}
	if blocks[0].Text != "group text" || blocks[1].Text != "nested quote" {
		t.Fatalf("texts = %q, %q", blocks[0].Text, blocks[1].Text)
	}
	if got := BlocksToMarkdown(blocks); got != ">> group text\n\n> nested quote\n" {
		t.Fatalf("markdown = %q", got)
	}
}

func TestHighlightWithMixedFormatting(t *testing.T) {
	blocks, err := MarkdownToBlocks("==**bold** mark==\n")
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 1 {
		t.Fatalf("blocks = %+v", blocks)
	}
	if got := BlocksToMarkdown(blocks); got != "==**bold** mark==\n" {
		t.Fatalf("markdown = %q", got)
	}
	reparsed, err := MarkdownToBlocks(BlocksToMarkdown(blocks))
	if err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(normalizeBlockIDs(blocks), normalizeBlockIDs(reparsed)) {
		t.Fatal("highlight round-trip changed the block structure")
	}
}

func TestUnderline(t *testing.T) {
	blocks, err := MarkdownToBlocks("<u>under</u> and **<u>both</u>**\n")
	if err != nil {
		t.Fatal(err)
	}
	if len(blocks) != 1 {
		t.Fatalf("blocks = %+v", blocks)
	}
	runs := blocks[0].Runs
	if len(runs) != 3 || !runs[0].Underline || runs[0].Text != "under" || !runs[2].Underline || !runs[2].Bold {
		t.Fatalf("runs = %+v", runs)
	}
	if got := BlocksToMarkdown(blocks); got != "<u>under</u> and <u>**both**</u>\n" {
		t.Fatalf("markdown = %q", got)
	}
}

// normalizeBlockIDs deep-copies blocks with stable ids so structural
// comparisons ignore the random ids minted on every parse.
func normalizeBlockIDs(blocks []Block) []Block {
	out := make([]Block, len(blocks))
	for i, block := range blocks {
		block.ID = "id"
		block.Runs = append([]Run(nil), block.Runs...)
		if block.Rows != nil {
			rows := make([][]BlockCell, len(block.Rows))
			for r, row := range block.Rows {
				rows[r] = append([]BlockCell(nil), row...)
			}
			block.Rows = rows
		}
		if block.Columns != nil {
			columns := make([][]Block, len(block.Columns))
			for c, column := range block.Columns {
				columns[c] = normalizeBlockIDs(column)
			}
			block.Columns = columns
		}
		out[i] = block
	}
	return out
}
