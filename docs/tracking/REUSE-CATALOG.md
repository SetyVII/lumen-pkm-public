# Reuse catalog — vetted libraries & references per stage

Status: ACTIVE. Created 2026-10-31 after a user-directed search ("do not
reinvent the wheel"). This catalog amends the dependency guidance in
BACKEND-PARITY-STRUCTURE.md: the libraries below are PRE-APPROVED for the
stages listed, provided the stage doc still documents why the dependency was
adopted and `go mod tidy` is run. Lumen is AGPL-3.0-or-later; every entry
below is permissively licensed (MIT/Apache) and therefore compatible.

## Go (server)

### Article extraction (stage: weblink-reader)
- **github.com/go-shiori/go-readability** — MIT. Line-by-line Go port of
  Mozilla's Readability.js (used by Firefox Reader View); battle-tested in
  the Shiori bookmark manager. Parses main content + metadata from HTML.
  USE `Parser.ParseDocument(doc, url)` / `FromReader` on the HTML Lumen has
  ALREADY fetched (never `FromURL`, which would bypass our SSRF guards).
  Bonus: it also returns Title, Excerpt, SiteName and **Favicon** — cover the
  favicon follow-up (F4.6) for free by persisting `article.Favicon`.
  Repo archived but stable (tracks Readability.js 0.5). The successor
  `codeberg.org/readeck/go-readability/v2` tracks 0.6 — VERIFY its license
  before use (the Readeck app is AGPL; the library's terms must be checked).
  HTML→Markdown of the extracted article: walk `article.Node`
  (*x/net/html.Node) with a small walker (headings/p/lists/quotes/links).

### Markdown parsing (stage: markdown-converter)
- **github.com/yuin/goldmark (v1)** — MIT. 100% CommonMark-compliant Go
  parser, clean AST, built-in extensions (tables, strikethrough, task
  lists), extensible inline parsers (add `[[wikilink]]` and `@mention` as
  custom inline parsers). v2 exists but is early-stage with fewer
  extensions; use v1. Depends only on stdlib. This is the recommended
  foundation for `MarkdownToBlocks`: walk the goldmark AST and emit Lumen
  blocks per contracts/SYNC.md.
- **github.com/brittonhayes/notionmd** — MIT. Go library converting Markdown
  to Notion blocks built ON goldmark. Not a drop-in (Notion's block shape
  differs from Lumen's), but the best available REFERENCE implementation
  for the AST-walking patterns (headings/lists/nesting/rich-text runs).
  Read its converter before designing ours; cite what was borrowed.

### Recurrence (stage: recurring-tasks)
- **github.com/teambition/rrule-go** — MIT. RFC 5545 recurrence rules (port
  of python-dateutil's rrule). Our modes map directly: daily/weekly/
  monthly/every_n_days → FREQ + INTERVAL + BYDAY; `rule.After(dueDate,false)`
  gives the next occurrence from the due date — exactly our overdue catch-up
  policy. Alternative: hand-rolled date math (~100 lines) if the agent
  prefers zero deps; either is acceptable, document the choice.

### MCP server (stage: mcp-server)
- **github.com/modelcontextprotocol/go-sdk** — Apache-2.0/MIT. THE official
  Go SDK (maintained with Google). Provides `mcp.NewServer`,
  `mcp.AddTool(handler)`, and `mcp.NewStreamableHTTPHandler(...)` returning
  an `http.Handler` that can be mounted on our existing net/http mux at
  /mcp behind the bearer token. Do NOT hand-roll JSON-RPC 2.0 or the
  streamable transport. Spec versions up to 2026-07-28 supported.

## Qt/C++ (desktop follow-ups — reference, not yet implemented)

- **mity/md4c** — MIT. The Markdown parser Qt itself ships
  (qtbase 3rd-party). C, single file, CommonMark-compliant, push-model
  callbacks. Candidate for desktop Markdown import (F3.4 client side) and
  any future MD tooling in C++.
- **pbek/qmarkdowntextedit** — MIT. Qt QPlainTextEdit with Markdown
  highlighting, code highlighting, and a built-in FIND/REPLACE bar
  (Ctrl+F / Ctrl+R). Lumen's editor is custom, so this is a REFERENCE for
  the find-in-page UI (F2.7 desktop) and for highlighting rules, not a
  drop-in.
- **Mermaid (F3.2)**: no mature C++/Qt renderer exists; the practical route
  is mermaid.js inside a QtWebEngine view fed with the code-block source,
  or rendering via mermaid-cli to PNG at export time. Decide in the desktop
  stage; nothing to reuse blindly here.
- **QtPdf (QPdfDocument/QPdfView)** for the PDF stage (annotations F3.3):
  Qt6's own module; pair with the existing media viewer.

## Rules

1. Adding a catalog dependency still requires: justification in the stage
   doc, `go mod tidy`, license name recorded in the stage doc.
2. Anything NOT in this catalog: default policy from BACKEND-PARITY-
   STRUCTURE.md applies (prefer stdlib, justify permissive pure-Go deps).
3. Vendoring: do not vendor; use go modules normally.
