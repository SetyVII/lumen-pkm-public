# Markdown ↔ blocks converter

Stage 4 of the backend parity program (`docs/tracking/BACKEND-PARITY-STRUCTURE.md`).
Branch: `backend/markdown-converter`.

## What & why

Capacities exposes Markdown as a **compressed, reduced representation** of page
content: the API accepts and returns Markdown instead of full block trees for
prose-heavy work, and the bulk importer accepts `.md` files with front matter,
wikilinks and embedded media
([Markdown concept](https://developers.capacities.io/api/concepts/markdown),
[bulk import](https://docs.capacities.io/reference/bulk-import)). Lumen already
stores a block tree in `properties.blocks`, but the server had no way to parse
or emit it: Markdown captured by the extension or imported from another tool
was kept as plain text in `content`.

This stage adds the missing conversion layer so Markdown can be a compact wire
format for scripts, imports, exports and the future developer API, while the
block JSON stays the lossless representation. The converter is the foundation
for stage 11 (full export writes one `.md` per object) and stage 12 (`POST
/v1/objects/markdown`, append-markdown, daily-note append).

## API

Both endpoints are additive, require the bearer token, accept up to 8 MB of
JSON and do not touch the store or the sync journal.

### `POST /v1/markdown/to-blocks`

```http
POST /v1/markdown/to-blocks
Authorization: Bearer <token>
Content-Type: application/json

{"markdown": "# Title\n\n- [ ] task\n"}
```

```json
{
  "blocks": [
    {"id": "…", "type": "heading", "text": "Title", "level": 1, "runs": [{"text": "Title"}]},
    {"id": "…", "type": "todo", "text": "○ task", "indent": 1, "runs": [{"text": "○ "}, {"text": "task"}]}
  ]
}
```

Every block gets a fresh UUID from `newID()`, the same helper the store uses
for object ids. Missing `markdown`, invalid JSON and invalid UTF-8 return 400.
An empty string returns `{"blocks": []}`.

### `POST /v1/markdown/from-blocks`

```http
POST /v1/markdown/from-blocks
Authorization: Bearer <token>
Content-Type: application/json

{"blocks": [{"id": "…", "type": "heading", "text": "Title", "level": 1, "runs": [{"text": "Title"}]}]}
```

```json
{"markdown": "# Title\n"}
```

Missing `blocks` returns 400. Unknown block types render as paragraphs instead
of disappearing; blocks that render to nothing are skipped. The output always
ends with a single newline when non-empty.

## Data model

No migrations, indexes or store changes. The converter defines the Go types
`Block`, `BlockCell` and `Run` in `server/markdown.go`; they mirror the JSON the
desktop editor writes in `RichEditor::structuredBlocks`
(`desktop/src/editor/lumen_editor.cpp`, the source of truth for field names and
semantics).

Two additions are converter extensions because the editor has no serialized
shape for them yet:

- `table` blocks: `align` (per-column `left`/`center`/`right`/`""`) and `rows`
  (arrays of `BlockCell{runs}`; the first row is the GFM header row).
- `columns` blocks: `columns` (arrays of full blocks, one per column).

`Run.highlight` is also additive (`==text==`); the editor ignores it until it
grows a highlight character format. Everything else (ids, `text`, `level`,
`indent`, `language`, `runs`, todo/toggle marker glyphs, U+2028 line breaks,
U+FFFC math/divider placeholders) uses the editor's field names, types and
semantics. False booleans and zero fields are omitted; the editor reads missing
keys with the same defaults. The `TestBlockJSONMatchesEditorSchema` golden test
pins the field names and `omitempty` behaviour.

## How & why

**Parsing foundation: goldmark v1.8.5 (MIT, pure Go).** The program's reuse
policy points at goldmark, and it is the right tool for the CommonMark half of
the job: emphasis, links, code spans, escaping, lists, blockquotes, fenced and
indented code, setext headings, GFM tables, strikethrough, linkify and task
lists all come from the library. Writing those correctly by hand is the part
most likely to be subtly wrong, so the converter does not. Reference links,
setext headings and indented code also come for free; reference links are
resolved and inlined on output.

**What goldmark does not cover, and how it is handled.** Lumen's block schema
is not CommonMark: `toggle`, `group`, `small`, `alphabetic_list`, bare `[ ]`
todos, `$$` display math and `::: columns` have no standard block syntax.
These are mapped without a second Markdown parser:

- `toggle`, `small`, bare `[ ]`/`[x]` and single-line `$$…$$` are paragraph
  lines that every line of the paragraph must match; `lineBlocks` turns them
  into blocks, otherwise the paragraph is kept verbatim.
- `group` is a blockquote whose raw source starts with `>>` and whose only
  child is another blockquote; `> >` remains a nested quote.
- `alphabetic_list` is a paragraph whose lines look like `a.`/`A)` items; the
  same indentation stack as Markdown lists assigns `indent`.
- `::: columns` is a container, so a small fence-aware pre-pass lifts it out,
  converts each column recursively and leaves a placeholder paragraph that the
  walker turns into a `columns` block. The pre-pass ignores fences, `$$` math
  and indented code.
- `[[Title]]`, `@Name`, `==highlight==` and `$math$` are registered as
  goldmark inline parsers (priority 150, ahead of the link parser at 200), so
  they compose with emphasis and links instead of being regexes over text.
- `<u>`/`</u>` are raw-HTML nodes that toggle an underline flag; other inline
  HTML is kept as literal text so nothing disappears.

**Reference: notionmd.** `github.com/brittonhayes/notionmd` (MIT) was read as
the requested AST-walking reference. It actually parses with `gomarkdown`, not
goldmark, and supports a smaller subset (headings, bold/italic, ordered and
unordered lists, code, links), so it is not a drop-in. The patterns borrowed
are the node-type dispatch with `SkipChildren` for leaf blocks
(`internal/converter/converter.go`) and treating the first paragraph of a list
item as the item text while nested lists are converted separately
(`internal/converter/list.go`). Lumen's flat `indent` model replaces Notion's
nested `children`.

**Deterministic emission.** `BlocksToMarkdown` is hand-written and stateless
apart from list indentation bookkeeping. It never prints ids, normalizes
ordered markers to `1.` and alphabetic markers to `a.`, indents nested items
by the parent's content column (2 for `- `, 3 for `1. `, 6 for `- [ ] `), grows
code fences to contain backtick runs, escapes Markdown punctuation, groups
consecutive highlighted runs so `==**bold** mark==` does not become `====`,
and separates blocks by one blank line (list items by one newline; a change of
list family starts a new list).

**Trade-off.** A full goldmark AST walker was chosen over a hand-rolled
CommonMark parser because the standard grammar is the risky part. The cost is a
dependency (justified above, `go mod tidy` run) and a small pre-pass for
columns; the benefit is correct emphasis/link/code-span parsing and future
CommonMark fixes for free. `rich_text` HTML is deliberately not generated: the
editor already writes it and the converter's job is the block tree.

## Contracts

The authoritative section is in `contracts/SYNC.md` under "Markdown and
blocks". Copied here for the stage record:

Block fields: `id` (UUID), `type` (one of `paragraph`, `small`, `heading`,
`bullet_list`, `numbered_list`, `alphabetic_list`, `quote`, `todo`, `toggle`,
`group`, `code`, `math`, `divider`, `embed`, `table`*, `columns`*), `text`
(concatenation of `runs[].text`; code bodies use U+2028; math/divider use
U+FFFC), `level` (heading 1-4), `indent` (list nesting 1-8), `language`
(code fence), `runs`, plus the extension fields `align`*, `rows`* and
`columns`*. `*` marks converter extensions the editor ignores.

Run fields: `text`, `bold`, `italic`, `underline`, `strike`, `code`,
`highlight`*, `href`, `type_id`, `object_id`, `type` (`math`/`image`),
`latex`, `display`, `image_name`, `width`, `height`, `reference_icon`,
`point_size`. Absent booleans mean false.

Invariants: todo blocks start with `○ ` or `✔ `; toggle blocks start with
`▶ ` or `▼ `; in-block line breaks are U+2028; math and divider blocks carry
U+FFFC plus a typed run; unresolved object links use `href:
"lumen://object/"` with no `object_id` and the title in `text`.

| Markdown | Block |
|---|---|
| `#`..`####` | `heading` level 1-4 (`#####`/`######` clamp to 4) |
| paragraph | `paragraph` |
| `-`/`*`/`+` | `bullet_list` + `indent` |
| `1.`/`1)` | `numbered_list` |
| `a.`/`A)` | `alphabetic_list` (Lumen extension) |
| `>` | `quote` |
| `>>` | `group` (Lumen extension; `> >` stays a quote) |
| `- [ ]`/`- [x]`/bare `[ ]` | `todo` with `○`/`✔` marker |
| `▶`/`▼` | `toggle` |
| `<small>…</small>` | `small` |
| fenced code | `code` + `language` |
| `$$…$$` / `$…$` | `math` block / math run |
| `---`/`***`/`___` | `divider` |
| GFM table | `table` + `rows`/`align` |
| `::: columns` | `columns` (Lumen extension) |
| `**b**`, `*i*`, `` `c` ``, `~~s~~`, `==h==`, `<u>u</u>` | run flags |
| `[text](url)`, `<url>`, bare URL | link run |
| `[[Title]]`, `@Name` | object-link run, title + no id |
| `![alt](url)` | link run (no image import yet) |

Canonical output: one blank line between blocks and one newline between list
items (a list-family change at the same or a shallower indent starts a new
list); ordered markers normalized to `1.`, alphabetic to `a.`; nested items
indented by the parent's content column (2 for `- `, 3 for `1. `, 6 for
`- [ ] `); code fences grow to contain backtick runs; Markdown punctuation is
backslash-escaped; object links render as `[[Title]]`; U+FFFC is dropped and
U+2028 becomes a hard break; empty blocks are skipped. Full endpoint examples
and the known-limits list are in `contracts/SYNC.md`. The converter does not
change fixtures: `contracts/fixtures/` contains object ids, not block
documents.

## Testing

`cd server && go test ./...`, `go vet ./...` and `gofmt -l .` are clean.

`server/markdown_test.go` covers:

- every construct in one representative document (types, heading level, list
  indents, todo markers, code language, math, table alignment, object links,
  toggle/group/small, columns);
- an exact canonical round-trip plus idempotence and structural equality
  (ids normalized);
- determinism of both directions;
- escaping edge cases: literal `*`, `_`, backticks, brackets, `<`, `$`, `~`,
  `=` and pipes in paragraphs and tables; code spans containing `|`;
- nested bullet/numbered lists with mixed types;
- wikilink and mention spans (`lumen://object/`, empty id, `[[Title]]` output);
- bare URLs and autolinks;
- inline and block math;
- code fences containing backticks;
- columns with and without blank lines, including blank lines inside a column;
- empty input and invalid UTF-8;
- golden JSON for todo/heading/code/math/divider/table blocks;
- the `text == concat(runs[].text)` invariant and unique block ids;
- editor-shaped JSON with `alignment`, `point_size` and explicit false flags;
- setext headings, indented code and reference links (goldmark bonus);
- HTML blocks preserved as literal text;
- bare todos, alphabetic lists, `>>` vs `> >`, highlight with mixed
  formatting, and `<u>` underline.

`server/routes_markdown_test.go` covers both endpoints, the round-trip through
them, validation errors (missing/null/wrong types), empty input and the 401
without a bearer token.

Manual verification against the disposable sandbox (port 8879, never the
personal install):

```sh
cd server
TOKEN=$(cat "$LUMEN_TEST_SECRETS_DIR/api.token")   # disposable sandbox secret
curl -s -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"markdown":"# Hi\n\n- [ ] task\n"}' http://127.0.0.1:8879/v1/markdown/to-blocks
```

## Client follow-ups

- **Desktop**: render `table` and `columns` blocks (the editor currently
  ignores the extension fields), and honour `Run.highlight` when it grows a
  character format.
- **Desktop**: resolve `[[Title]]`/`@Name` spans to real object ids with the
  existing title search, then rewrite `href`/`object_id`. Title resolution is
  deliberately out of scope here.
- **Desktop/extension**: use `to-blocks` when importing a Markdown selection
  and `from-blocks` for copy-as-Markdown; keep writing `content` and
  `rich_text` as today.
- **Mobile**: no change required; blocks already travel in properties.
- **Stage 11 (full export)**: call `BlocksToMarkdown` per object and rewrite
  `lumen://image/<id>` image runs to relative asset paths.
- **Stage 12 (api-v2)**: compose `MarkdownToBlocks` into create-from-markdown,
  append-markdown and daily-note append.

## Follow-ups

- Parse YAML front matter (`type`, `title`, `tags`, `createdAt`, `collection`)
  for bulk import; today a leading `---` is a divider.
- Import Markdown images as image objects/assets instead of links.
- Resolve wikilink/mention titles to object ids (needs a title index lookup
  and a policy for ambiguous titles).
- Add editor support for table/columns blocks so the extension fields survive
  a desktop round-trip.
- Consider `rich_text` generation from runs if a client ever needs HTML from
  the server.
