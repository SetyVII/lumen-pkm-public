# Weblink reader content

Stage: `backend/weblink-reader` (backend parity program, wave 1).
Status: implemented on branch `backend/weblink-reader`.
Scope: server only. Clients render the stored Markdown; no client code is part
of this stage.

## What & why

Capacities treats a saved URL as a first-class object and offers a Reader view
that shows the article without the surrounding site chrome. The equivalent
Capacities reference is
<https://docs.capacities.io/reference/basic-types/weblinks>. Capacities fills
its Reader with an AI analysis for Pro users. Lumen's product policy forbids AI
features, so this stage implements a pure, deterministic extraction instead:
the same HTML that `/v1/capture` already fetches is parsed locally and stored as
Markdown. No external reader service, no model, no second network request.

The result is a `web` object with `properties.reader_content`, searchable
together with the title and body, plus an explicit
`POST /v1/objects/{id}/refresh-reader` endpoint for pages that change after the
first capture.

## API

### Capture

`POST /v1/capture` keeps its request and response shape. For `type_id: "web"`
it now also stores:

- `properties.reader_content`: Markdown string, at most 500 KiB, `""` when the
  page has no readable article or the preview fetch fails.
- `properties.favicon`: absolute URL, only when Readability finds a PNG
  favicon (additive metadata for the pending favicon item F4.6).

Example response (abridged):

```json
{
  "id": "01J...",
  "type_id": "web",
  "title": "The Quiet Web",
  "properties": {
    "url": "https://example.com/quiet-web",
    "status": "Para revisar",
    "preview_status": "ready",
    "summary": "A field guide to reading slowly.",
    "reader_content": "# The Quiet Web\n\nBy Ada Reader\n\n...",
    "favicon": "https://example.com/favicon.png"
  },
  "revision": 1
}
```

Capture behavior details:

- The HTML is fetched once by the existing preview pipeline. Extraction reuses
  that body; it never calls `readability.FromURL` and never fetches the article
  again. The one fetch is still governed by the existing SSRF guards.
- If the client supplies `preview` or sets `fetch_preview: false`, no page is
  fetched, so `reader_content` is absent until `refresh-reader` runs.
- A failed preview fetch (for example HTTP 404) stores `reader_content: ""`,
  sets `preview_status: "unavailable"`, and still returns `201`. Extraction
  failure never fails the capture.
- An empty or unreadable page stores `reader_content: ""` and
  `preview_status: "ready"`.

### Refresh

`POST /v1/objects/{id}/refresh-reader`

No request body. Re-fetches the saved `properties.url` under the same SSRF
guards, replaces `reader_content`, updates `favicon` when the new page declares
one, and bumps `revision`.

```json
POST /v1/objects/01J.../refresh-reader
Authorization: Bearer <token>

200 OK
{
  "id": "01J...",
  "type_id": "web",
  "title": "The Quiet Web",
  "properties": {
    "url": "https://example.com/quiet-web",
    "reader_content": "# The Quiet Web\n\n...",
    "favicon": "https://example.com/favicon.png"
  },
  "revision": 4
}
```

Errors:

| Status | Condition |
|---|---|
| `404` | Object does not exist, or its type is not `web`. |
| `400` | The web object has no `url`. |
| `409` | The URL changed while the page was being fetched. Body: `{"error":"conflict","conflict":true,"current":<object>}`. |
| `502` | The page cannot be fetched or is not HTML. The previous `reader_content` is kept. |

The fetch happens before `writeMu`; the object is re-read under the lock, so a
slow site cannot block unrelated writes and concurrent edits are preserved.

### Search

`GET /v1/search?q=...` and `GET /v1/objects?q=...` now match `title`,
`content`, and `properties.reader_content` with the same case-insensitive
substring rule. There is no weighting: a title hit and a reader hit rank the
same. Results keep the existing order (`updated_at` descending, then `id`
ascending), which is deterministic.

## Data model

- New optional `web` property `reader_content` (string, Markdown). It is not
  added to the type schema because it is server-generated metadata, like
  `og_title` or `preview_status`; clients should not render it as an editable
  property row.
- New optional `web` property `favicon` (string URL), server-generated.
- No SQLite schema migration: properties live inside the encrypted object
  payload. Existing objects simply lack the keys.
- No change to `contracts/fixtures/`: the new properties are optional and not
  required for object validity.
- The in-memory index stays metadata-only. `reader_content` can be 500 KiB per
  object, so putting it in `ObjectMeta` would violate the index's memory
  invariant. Search decrypts objects on demand as it already did.
- Sync: both properties travel as normal object properties. Mobile clients may
  ignore them. The 500 KiB cap bounds the extra sync payload per changed
  weblink.

## How & why

### Extraction library

The article detection uses
`github.com/go-shiori/go-readability` v0.0.0-20251205110129-5db1dc9836f0
(MIT), a line-by-line Go port of Mozilla's Readability.js 0.5, the same
algorithm behind Firefox Reader View. It is a pure Go module; `go mod tidy`
adds `go-shiori/dom`, `andybalholm/cascadia`, `araddon/dateparse`,
`gogs/chardet`, and `golang.org/x/text` as indirect, pure-Go dependencies. No
CGO, no service, no model.

The module is archived upstream and its successor is
`codeberg.org/readeck/go-readability/v2` (Readability.js 0.6). The reuse catalog
selects the MIT go-shiori module because the successor's license is unverified.
Switching later is a one-function change in `extractReaderArticle`.

### One fetch, same guards

`Preview` already fetches the HTML for Open Graph metadata. The extraction call
sits next to `parsePreview` and receives the same `body` and final URL, so the
page is fetched once. `PreviewReddit` reuses the first successful body from its
existing multi-step pipeline. `refresh-reader` calls `secureFetcher.Reader`,
which performs exactly one `fetch`.

`Reader` and `Preview` share `safeURL`, the DNS-validating `DialContext`, the
redirect `CheckRedirect`, the `text/html` MIME check, and the 2 MB body cap.
`readability.FromURL` is deliberately never used: it would create an
unauthenticated second fetch outside those guards.

### Markdown walker

Readability returns a cleaned `*html.Node` plus metadata. A small walker in
`server/reader.go` emits:

- `#`-`######` headings, paragraphs, nested bullet and numbered lists,
  blockquotes, fenced code blocks with a language when the class survives,
  links, images, tables, and horizontal rules;
- inline `**bold**`, `*italic*`, `~~strike~~`, and backtick code;
- absolute `http`/`https` URLs only; `javascript:`, `data:`, and `mailto:`
  links degrade to their text.

Readability promotes the article's `h1` to `Article.Title` and removes it from
the content node. The walker restores it as the first `#` heading so
`reader_content` is self-contained for export and search. Clients that already
show the object title can hide a first H1 that duplicates it.

`parser.KeepClasses = true` is set so fenced code keeps `language-*` classes.
A second, defensive pass still drops `script`, `style`, `nav`, `footer`,
`aside`, forms, embeds, hidden nodes, ARIA chrome roles, and class/id tokens
such as `ad`, `sidebar`, `share`, `comments`, `newsletter`, `cookie`, and
`breadcrumbs`.

### Cap and failure behavior

`readerContentLimit` is 500 KiB (512000 bytes). Truncation backs off to a UTF-8
rune boundary and trims trailing whitespace; it is silent. A page that yields
no text stores `""`. A defensive `recover` turns any parser panic on hostile
HTML into an empty article. Capture never returns an error because extraction
failed. Refresh keeps the old content on a fetch error (`502`) and stores `""`
only when the fetch succeeded but the page genuinely has no readable article.

### Server-managed properties

`reader_content` and `favicon` are derived data. `PATCH /v1/objects/{id}`
restores the stored values when `properties` omits them and ignores
client-supplied replacements; `POST /v1/objects` strips them for `web` objects.
Changing a web object to another type deletes them. `capture` and
`refresh-reader` are the only supported writers. This avoids losing a 500 KiB
article on every status edit and keeps the search corpus trustworthy.

### Alternatives considered

- **Hand-rolled article detection.** A deterministic `<article>`/`<main>`
  scorer was implemented first and passed the tests, but Readability.js is
  battle-tested on real sites and is the reuse-catalog choice. The walker is
  still ours; only the article detection is delegated.
- **`readability.FromURL`.** Rejected: it fetches again and bypasses Lumen's
  SSRF guards.
- **Storing the article in `content`.** Rejected: `content` is the user's
  editable body and is overwritten by edits; the reader is derived metadata.
- **Indexing `reader_content` in RAM.** Rejected: the index is metadata-only
  by contract and the cap is 500 KiB per object.
- **Weighted search.** Rejected for this stage: equal substring matching plus
  the existing deterministic order is simpler and does not change ranking for
  existing clients.

## Contracts

The following section was added to `contracts/SYNC.md`:

````markdown
## Weblink reader content

`/v1/capture` and `POST /v1/objects/{id}/refresh-reader` extract a readable
article from a public HTML page and store it on `web` objects as
`properties.reader_content` (Markdown string). Extraction is deterministic and
local: it reuses the HTML fetch already governed by the preview SSRF guards
(public `http`/`https` only, ports 80/443, DNS validation, redirect validation,
2 MB HTML cap). It never calls an external reader service and never uses AI.

- `reader_content` is capped at 500 KiB (512000 bytes) and truncated on a UTF-8
  boundary.
- When the page has no readable article, or the preview fetch fails,
  `reader_content` is `""` and the capture still succeeds. A client-supplied
  preview or `fetch_preview=false` skips the fetch, so the property is absent
  until `refresh-reader` runs.
- `favicon` (string URL) is set on `web` objects when Readability finds a PNG
  favicon. It is additive metadata for the pending favicon work (F4.6).
- Both properties are server-managed. `PATCH /v1/objects/{id}` restores the
  stored values when `properties` omits them, and ignores client-supplied
  replacements; `POST /v1/objects` strips them for `web` objects. `capture` and
  `refresh-reader` are the only supported writers.

### Search

`GET /v1/search?q=...` (and `GET /v1/objects?q=...`) matches the query against
`title`, `content` and `properties.reader_content` with the same
case-insensitive substring rule. There is no weighting: a title hit and a
reader hit rank the same. Results keep the existing order (`updated_at`
descending, then `id` ascending), so the behavior is deterministic. The
in-memory index remains metadata-only; `reader_content` is decrypted on demand
during the scan, so a 500 KiB article is never pinned in RAM.

### Refresh endpoint

`POST /v1/objects/{id}/refresh-reader` re-fetches the saved URL and replaces
`reader_content` (and `favicon` when non-empty) on an existing `web` object.
No request body is needed. The fetch happens before the write lock; the object
is re-read under the lock, so concurrent edits are preserved.

```json
POST /v1/objects/01J.../refresh-reader
Authorization: Bearer <token>

200 OK
{
  "id": "01J...",
  "type_id": "web",
  "title": "The Quiet Web",
  "properties": {
    "url": "https://example.com/quiet-web",
    "reader_content": "# The Quiet Web\n\n...",
    "favicon": "https://example.com/favicon.png"
  },
  "revision": 4
}
```

- `404` if the object does not exist or its type is not `web`.
- `400` if the web object has no `url`.
- `409` with `{"error":"conflict","conflict":true,"current":<object>}` if the
  URL changed while the page was being fetched.
- `502` if the page cannot be fetched or is not HTML; the previous
  `reader_content` is kept.

`reader_content` is a normal property in sync payloads; clients may ignore it.
The fixtures are unchanged: `reader_content` and `favicon` are optional
server-generated properties, not required for object validity.
````

## Testing

`cd server && go test ./...` (also run with `-race`), `gofmt -l .`, and
`go vet ./...` are clean.

Coverage in `server/reader_test.go`:

- `TestExtractReaderContentStripsChromeAndKeepsMarkdown`: a realistic article
  fixture with site header, breadcrumbs, sidebar, footer, ad div, and scripts;
  asserts headings, paragraphs, bold/italic, links, nested lists, blockquote,
  fenced code with language, image, and table survive, while every chrome token
  is absent.
- `TestExtractReaderContentPrefersLargestArticleAndRoleMain`: Readability
  selects the substantive article over a teaser and honors `role="main"`.
- `TestExtractReaderContentDegradesGracefully`: empty, chrome-only, and
  whitespace-only pages return `""`; malformed HTML still yields text.
- `TestExtractReaderContentTruncationCap`: a 600 KiB paragraph is capped at
  500 KiB and remains valid UTF-8.
- `TestCaptureStoresReaderContentAndSearchFindsIt`: `httptest` serves the
  fixture; capture stores `reader_content` and `favicon`, makes exactly one
  request, search finds the article token, and search does not find a token
  that extraction stripped.
- `TestCaptureFailureStoresEmptyReaderContent`: HTTP 404 and empty-body pages
  store `""` and capture still returns `201`.
- `TestPatchPreservesReaderContent`: a properties snapshot without the derived
  keys keeps them; a forged `reader_content` is ignored.
- `TestCreateWebObjectStripsReaderMetadata`: `POST /v1/objects` cannot seed
  `reader_content` or `favicon` on a web object.
- `TestRefreshReaderUpdatesContentAndBumpsRevision`: the second page replaces
  the first, revision advances, one extra request is made, and non-web/missing/
  no-URL objects return `404`/`404`/`400`.
- `TestRefreshReaderFailureKeepsPreviousContent`: an upstream 500 returns `502`
  and does not erase the stored article.

The `httptest` fetcher keeps the production `safeURL` validator and only
replaces the dialer, so the tests exercise the real SSRF policy without
touching the network or weakening the guard.

Manual verification (disposable server, never the personal library):

```sh
cd sandbox && docker compose -f compose.test.yaml up -d
# POST /v1/capture with a public article URL, then
# GET /v1/objects/{id} and POST /v1/objects/{id}/refresh-reader.
```

## Client follow-ups

- Desktop: add a Reader pane/tab for `web` objects that renders
  `properties.reader_content` as Markdown; show an empty state when it is `""`
  or absent, and offer a refresh action that calls
  `POST /v1/objects/{id}/refresh-reader`.
- Hide the first H1 when it duplicates the object title, or show it as the
  article title. Do not treat `reader_content` as an editable property.
- Mobile: render the same Markdown in the link detail screen; the property is
  already in sync payloads. Consider lazy rendering because a page can be up to
  500 KiB.
- Extension: after capture, read the returned `reader_content` for an instant
  reader preview; do not send `reader_content` or `favicon` back in PATCH.
- Search: show that a result matched the reader body (for example a snippet)
  without changing the server's equal-weight ordering.

## Follow-ups

- Pages that Readability rejects (very short notes, unusual layouts) store
  `""`. A deterministic fallback extractor could be added later; it was
  intentionally not kept to avoid two article-selection algorithms.
- Images are stored as Markdown links, not downloaded as assets. A later stage
  could mirror them into the asset store.
- Duplicate captures return the existing object without refreshing its reader.
  Clients should call `refresh-reader` when they want a fresh copy.
- `refresh-reader` uses the standard single HTML fetch. Reddit's multi-step
  preview pipeline (direct page, vxreddit, archive) is only used by capture, so
  a Reddit refresh may yield less content than the original capture.
- `reader_content` is not excluded from mobile sync. If the 500 KiB cap proves
  heavy for large libraries, a sync filter can be added without changing the
  object contract.
- Favicons are URLs only (PNG, as detected by Readability); no favicon blob is
  downloaded or cached.
- Type conversion (stage 9) should drop `reader_content` and `favicon` when a
  web object leaves the `web` type; `PATCH` already does this.
