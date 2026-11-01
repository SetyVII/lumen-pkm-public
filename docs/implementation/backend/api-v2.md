# API v2 — developer surface

Stage 12 of the backend parity program (`docs/tracking/BACKEND-PARITY-STRUCTURE.md`).
Branch: `backend/api-v2`. Depends on the merged stages 1–11 (capture +
weblink-reader, markdown-converter, journals, collections, trash, version
history, export).

## What & why

Capacities ships a developer API (<https://developers.capacities.io>) that
lets scripts and agents work on a space: bearer tokens with
`api:read`/`api:write` scopes, an `X-Capacities-Api-Version` header,
create-object-from-URL, Markdown create/append, chunked media upload,
pagination and a discovery document. Lumen had all the underlying machinery
(capture pipeline, Markdown converter, journals, assets) but no coherent way
for an external script or agent to drive it: `/v1/capture` is shaped for the
browser extension, `POST /v1/objects` expects a full block tree, and assets
above 50 MB had no upload path at all.

This stage composes the earlier stages into that developer surface. Every
change is **additive**: no existing endpoint, parameter or response shape
changed. The next stage (13, `backend/mcp-server`) mounts MCP over this API
and consumes `GET /v1/api`.

## Scope model: why not OAuth 2.1

Capacities' tokens ride on OAuth 2.1 + PKCE because it is a multi-tenant SaaS:
many users must authorize many third-party apps against many spaces, with
consent screens, client registration and refresh-token rotation. Lumen is
**single-user and self-hosted** with exactly one bearer token
(`LUMEN_TOKEN_FILE`): there is no third party to authorize and no tenant
boundary to enforce. An OAuth dance would add client registration, token
storage and an authorization endpoint without producing any isolation the
deployment does not already have.

What Lumen borrows instead is the *scoping* idea: the one token can be
restricted to the developer API with **`LUMEN_TOKEN_SCOPES`** (comma-separated
`api:read` and/or `api:write`; `api:write` implies `api:read`; unknown values
fail startup). The mechanism is chosen over a scopes file because scopes are
deployment configuration, not secrets — the token itself already lives in a
secret file, and an environment variable composes with the existing compose
and systemd units without a second file to provision and back up.

- **Unset (default): full access.** Today's behavior, so no existing
  deployment changes.
- **`api:read`:** GET/HEAD everywhere, plus the POST endpoints that never
  mutate the store (`/v1/markdown/to-blocks`, `/v1/markdown/from-blocks`,
  `/v1/export`). Anything that writes returns `403` with
  `This token only has the api:read scope; this request modifies data and
  requires api:write`.
- **`api:write`:** read plus writes.
- **Sync endpoints (`/v1/sync/*`) are full-trust** and bypass the scope check
  by design: the Android client is a first-party app sharing the one
  deployment token, and scoping the token for scripting must never break the
  phone's push/pull. A future multi-token deployment can revisit this.

## Version header

`X-Lumen-Api-Version: 1.0.0` is echoed on **every** response (set in the
handler middleware, so even `/health` and `401`s carry it). Requests may send
`X-Lumen-Api-Version` (added to the CORS allow/expose lists for the
extension); an absent or unknown value is **never a 400** — the server always
answers with the version it actually speaks. Capacities uses the same
never-break-on-unknown-version posture for `X-Capacities-Api-Version`.

## API

All endpoints require the bearer token. `X-Lumen-Api-Version` is echoed on
every response; this table lists only the new endpoints.

| Endpoint | Scope | Purpose |
|---|---|---|
| `GET /v1/api` | `api:read` | Discovery: version, token scopes, limits, endpoint catalog |
| `POST /v1/objects/from-url` | `api:write` | Create a web object from a URL (capture pipeline) |
| `POST /v1/objects/markdown` | `api:write` | Create an object whose body comes from Markdown |
| `POST /v1/objects/{id}/append-markdown` | `api:write` | Append Markdown to an object |
| `POST /v1/journal/{date}/append` | `api:write` | Append Markdown to a daily note |
| `POST /v1/assets/upload/init` | `api:write` | Start a chunked upload |
| `POST /v1/assets/upload/{upload_id}/part?index=N` | `api:write` | Upload one sequential part |
| `POST /v1/assets/upload/{upload_id}/complete` | `api:write` | Commit the parts as an Asset |
| `POST /v1/assets/upload/{upload_id}/abort` | `api:write` | Discard a staged upload |

Additionally `GET /v1/objects` and `GET /v1/search` gained `limit`/`cursor`
parameters. The full catalog, including the pre-existing endpoints and their
scope classes, is served by `GET /v1/api` (source: `apiCatalog` in
`server/routes_api_v2.go`).

### `GET /v1/api`

```json
{
  "version": "1.0.0",
  "token_scopes": ["full_access"],
  "limits": {
    "max_small_asset_bytes": 52428800,
    "max_chunked_asset_bytes": 524288000,
    "min_chunk_part_bytes": 5242880,
    "max_chunk_part_bytes": 26214400,
    "upload_session_ttl_hours": 24,
    "default_page_limit": 50,
    "max_page_limit": 200
  },
  "endpoints": [
    {"name": "create_object_from_url", "method": "POST", "path": "/v1/objects/from-url", "scopes": "api:write"},
    {"name": "sync_push", "method": "POST", "path": "/v1/sync/push", "scopes": "full"}
  ]
}
```

`token_scopes` reports the effective set: `["full_access"]`, `["api:read"]`
or `["api:read","api:write"]`. Endpoint `scopes` values: `public` (no token),
`api:read`, `api:write` (implies read) and `full` (the first-party sync
protocol, not governed by scopes).

### `POST /v1/objects/from-url`

```json
{
  "url": "https://example.com/article?utm_source=news",
  "title": "Optional title",
  "tags": ["reading", "to-process"],
  "notes_markdown": "# Notes\n\n- [ ] summarize",
  "journal_link": true
}
```

- `url` is required and runs through the **exact `/v1/capture` pipeline**:
  `cleanURL` + `safeURL` + reddit-share resolution (`normalizeCaptureURL`),
  the SSRF-guarded preview + reader fetch (`capturePreview` →
  `secureFetcher.Preview`/`PreviewReddit`), and the duplicate detection over
  the cleaned URL (`findWebObjectByURL`). No fetch logic is duplicated; the
  shared helpers live in `server/capture_pipeline.go` and `/v1/capture` was
  refactored to call them too.
- `title` wins over the fetched `og:title`, which wins over the hostname.
- `tags` are names, resolved get-or-create to `tag` objects (a name owned by
  a non-tag object is a 400). The tag objects commit in the same transaction
  as the web object.
- `notes_markdown` becomes `properties.blocks` (via `MarkdownToBlocks`) plus
  the plain-text `content` mirror on a new object, and is **appended** when
  the URL already exists.
- `journal_link: true` links today's journal (the extension flow);
  the response carries `journal_id`.
- **Dedupe:** the same cleaned URL returns the existing object with
  `existing: true` (HTTP 200), applying notes/tags first; a fresh capture
  responds 201 with `existing: false`.

### `POST /v1/objects/markdown`

```json
{"type_id": "note", "title": "Imported", "markdown": "# Title\n\n- [ ] task", "tags": ["inbox"], "properties": {"priority": "high"}}
```

Creates an object with `properties.blocks` from `MarkdownToBlocks` and
`content` as the plain-text mirror (`blocksPlainText`). `properties` passes
through except `blocks`, `rich_text` and `tags`, which this endpoint owns
(400). `type_id: "collection"` is rejected (use the collections endpoints),
and so is `properties.is_template: true` (templates are created like any
other object through `POST /v1/objects`). A `web` object created this way
goes through `preserveReaderMetadata`, so client-supplied `reader_content`
cannot be forged.

### `POST /v1/objects/{id}/append-markdown`

```json
{"markdown": "## Section\n\nAdded later.", "position": 0}
```

Appends Markdown following the **convert-merge representation rules**:

- an object with a non-empty `properties.blocks` gets the new blocks
  appended — or inserted at `position` (0-based, `0..len(blocks)`, else 400)
  — and loses `rich_text`, so no client renders a stale formatted body;
- an object with no text anywhere (fresh note) **gains** the block
  representation: an empty body cannot be hidden by it;
- a plain-text object (content or `rich_text`, no blocks) never gains
  blocks: the block array is authoritative in the editor, so promoting it
  would hide the existing plain body. Only `content` is appended and
  `rich_text` is dropped.

The plain-text `content` is always extended and stays canonical. `position`
is ignored for plain-text objects (documented). The save is a normal object
write: revision bump, version-history snapshot of the previous state, and a
mobile sync-journal entry, all in one transaction. Unknown id → 404; empty or
whitespace `markdown` → 400.

### `POST /v1/journal/{date}/append`

```json
{"markdown": "- walked the dog", "section": "Log"}
```

Get-or-create with the existing lazy semantics (`ensureJournal`, no default
template application — that stays opt-in on `GET /v1/journal/{date}`), then
append. With `section`, the content goes under a level-2 heading of that
name: in the block representation the heading is a `heading` block (level 2)
and the content is inserted before the next heading of the same or a higher
level; in a plain-text note the section is a `## <name>` line and the content
is inserted before the next line starting with `#`. Missing sections are
created at the end. The response is the updated journal object.

### Chunked uploads

For files above the 50 MB limit of `POST /v1/assets` (which is unchanged):

```http
POST /v1/assets/upload/init
{"filename": "video.mp4", "mime": "video/mp4", "size": 536870912, "object_id": "<optional owner>"}
```

```json
{"upload_id": "…", "asset_id": "…", "size": 536870912, "received": 0,
 "part_min_bytes": 5242880, "part_max_bytes": 26214400, "expires_at": "…"}
```

1. **part** — `POST /v1/assets/upload/{upload_id}/part?index=N` with the raw
   bytes as the body. Parts are sequential and 1-based (`index` must equal
   `parts+1`, else 400 out-of-order), 1 B..25 MB each; **only the final part
   may be smaller than 5 MB**, and the running total may never exceed the
   declared `size`. A part beyond 25 MB, an empty part, a negative/zero index
   and a part that overflows the declared size are all 400.
2. **complete** — `POST …/complete` (no body) assembles a standard `Asset`
   response, identical in shape and semantics to `POST /v1/assets`: images and
   PDFs create their media object (`newMediaObject`), the owner receives the
   `assets` metadata / `image_asset_id` / link, the mime is sniffed from the
   first part's bytes with the declared mime as fallback for unknown
   signatures, and a standalone upload must still be an image or PDF.
   Incomplete uploads → 400.
3. **abort** — `POST …/abort` removes the staging area; later part/complete
   calls for that id → 404.

Limits: total size 1 B..**500 MB** (`chunkedUploadMaxSize`), per part 5..25 MB
(except the last), mime allowlist identical to `POST /v1/assets`
(standalone = image/PDF, attached = anything), TTL **24 h** with a janitor
that sweeps on every upload request, and a disk-space guard (507
`Insufficient Storage` when the remaining bytes plus a 64 MB margin do not
fit on the staging filesystem).

**Staging lives outside the encrypted store** — `$TMPDIR/lumen-uploads/<id>/`
(override with `LUMEN_UPLOAD_DIR`, mode 0700): parts are not committed assets,
must not bloat the database or sync journal, and an interrupted upload has to
be trivially cleanable. Nothing in the staging dir is plaintext: each part is
sealed with the store key on arrival (AAD `asset/<asset-id>`, the same AAD
the final blob uses) and the session metadata is sealed too (AAD
`upload/<id>`), so the directory survives a restart and never leaks content.
Sessions recovered from disk let an upload continue across a server restart.

### Pagination

`GET /v1/objects` and `GET /v1/search` accept `limit` (integer 1..200,
default 50; outside the range → 400) and `cursor` (opaque). The response is
still a JSON array — the contract forbids changing it — so the next page is
advertised with the **`X-Lumen-Next-Cursor`** response header, absent on the
last page. Browser callers can read it: it is in the CORS
`Access-Control-Expose-Headers` list.

```http
GET /v1/objects?type_id=note&limit=50
X-Lumen-Next-Cursor: eyJ1cGRhdGVkX2F0Ijoi…

GET /v1/objects?type_id=note&limit=50&cursor=eyJ1cGRhdGVkX2F0Ijoi…
```

- Pagination runs **after every filter** (the query-filter grammar,
  `include_templates`, the search matcher), so it composes with them all.
- The cursor is a keyset over the display order (`updated_at` descending,
  then `id` ascending): base64 of `{"updated_at":…,"id":…}`. A cursor whose
  anchor object was deleted or filtered out still works — only its position
  matters — and objects edited between pages may shift (recency order,
  last-write-wins), but pages never skip or repeat unchanged objects.
- **Backward compatibility:** a request with neither `limit` nor `cursor`
  keeps today's behavior (the full list, no header). The default of 50 only
  applies once a client opts in, so existing clients can never lose objects
  they currently receive.

## Data model

- `App` gains the scope fields, the upload staging directory and the live
  upload-session map; the middleware gains the version header and the scope
  check. No existing record kind changed.
- **Schema version 4** adds one side table, `blob_chunks(asset_id, seq,
  payload)`: a large attachment is stored as independently sealed pieces
  instead of one `blobs` row. `Store.Blob` transparently reassembles chunked
  blobs, so the exporter and every other reader keep working;
  `GET /v1/assets/{id}/content` streams chunked attachments piece by piece
  (a 500 MB video never sits decrypted in RAM); `PutBatchBlobChunks` commits
  records plus pre-sealed chunks atomically; blob deletion (trash purge,
  merges) removes both shapes together; `Fsck` verifies that every chunk
  decrypts with its asset AAD and that every asset has a whole blob or
  chunks. The version bump stops a pre-api-v2 binary from opening a
  workspace whose large attachments it would not find.
- Upload sessions are **not** in the database: they are sealed JSON files in
  the staging directory, recoverable after a restart, removed by complete,
  abort or the TTL janitor.
- No fixtures change: the object format is untouched.

## How & why (key decisions)

- **Reuse over duplication.** `/v1/objects/from-url` calls the same helpers
  `/v1/capture` uses (`normalizeCaptureURL`, `findWebObjectByURL`,
  `capturePreview`, `linkJournalObject`), extracted into
  `capture_pipeline.go`; capture itself was refactored onto them so the two
  entry points cannot drift. The complete path of chunked uploads calls the
  same `newMediaObject`/`assetProperties`/`mediaType` helpers as
  `POST /v1/assets`.
- **Tags after validation.** The tag objects created by a request commit in
  the same transaction as the object, so they do not exist as store rows when
  the relation check would run; the object is validated first and the tag
  relation is attached afterwards (valid by construction: `resolveTagNames`
  only returns live tag objects).
- **Blocks follow convert-merge rules.** Appending Markdown never invents a
  block array for an object that renders from `content`/`rich_text`, because
  blocks are authoritative in the editor and would hide the plain body — the
  same reasoning as the merge stage's "drop the richer representation"
  rule. `rich_text` is always dropped on append so no client renders a
  stale body; `content` is always extended so plain-text clients (mobile)
  stay correct.
- **Chunked blobs instead of bigger bodies.** A 500 MB upload through
  `PutBatchBlobs` would pin ~1 GB (plaintext + ciphertext) in RAM. Parts are
  sealed at arrival with the final AAD, so `complete` only moves ciphertext
  from files into `blob_chunks` (one part in RAM at a time, read inside the
  transaction). Trade-off: the SQLite writer lock is held while the sealed
  part files are read — bounded by part size and acceptable on a
  single-writer personal server; the alternative (gather before BEGIN) would
  spike RAM exactly like the naive path.
- **Pagination via header.** Capacities-style pagination usually wraps the
  response in `{data, nextCursor}`; Lumen's contract pins these endpoints to
  a JSON array, so the cursor rides a response header. Trade-off: HTTP
  header size limits are irrelevant at this scale, and non-browser clients
  read headers trivially.
- **Scope checks in the middleware**, not per handler: one place decides
  read vs write, the catalog documents it, and a future endpoint is
  automatically protected unless explicitly exempted.

## Contracts

The following section was added to `contracts/SYNC.md`:

```markdown
## API v2 (developer surface)

Implemented 2026-10-31 (backend stage `api-v2`). Modeled on the Capacities
developer API (developers.capacities.io), composing the earlier parity
stages into endpoints for scripts and agents. Everything in this section is
additive: no existing endpoint, parameter or response shape changed.

### Token scopes

Lumen is single-user and self-hosted with one bearer token
(`LUMEN_TOKEN_FILE`), so there is no OAuth 2.1 authorization framework as in
Capacities' multi-tenant SaaS; what Lumen adopts is the scoping idea. The
optional `LUMEN_TOKEN_SCOPES` environment variable restricts the token:

- unset/empty: full access (today's behavior, the default);
- `api:read`: GET/HEAD everywhere, plus the POST endpoints that never mutate
  the store (`/v1/markdown/to-blocks`, `/v1/markdown/from-blocks`,
  `/v1/export`); any write returns 403 with a clear error;
- `api:write`: read plus writes (implies `api:read`);
- unknown values fail at startup instead of silently granting more access.

The sync endpoints (`/v1/sync/*`) are full-trust and bypass scopes: the
Android client is a first-party app sharing the deployment token, and
scoping the token for scripting must never break mobile push/pull.

### Version header

`X-Lumen-Api-Version: 1.0.0` is echoed on every response (including
`/health` and error responses). Requests may send the header; an absent or
unknown value is never a 400 — the server always answers with the version it
speaks. The header is in the CORS allow and expose lists.

### POST /v1/objects/from-url

Reuses the exact /v1/capture pipeline (URL cleaning, SSRF-guarded preview +
reader fetch, duplicate detection over the cleaned URL). `title` wins over
`og:title`, which wins over the hostname. `tags` are names resolved
get-or-create to tag objects; the tag objects commit with the web object.
`notes_markdown` becomes properties.blocks + the plain-text content on a new
object and is appended when the URL already exists. `journal_link: true`
links today's journal. Fresh capture: 201 with `existing: false`; duplicate:
200 with the existing object and `existing: true` (notes appended, tags
unioned). Invalid URLs: 400.

### POST /v1/objects/markdown

Creates an object with properties.blocks from MarkdownToBlocks and content
as the plain-text mirror. properties.blocks, properties.rich_text and
properties.tags are owned by the endpoint (400 when supplied). type_id
"collection" is rejected (use the collections endpoints), and so is
properties.is_template: true. Response: 201 with the created object.

### POST /v1/objects/{id}/append-markdown

Appends Markdown with the convert-merge representation rules: an object
with blocks gets the new blocks appended (or inserted at `position`, 0-based,
0..len, else 400) and loses rich_text; an object with no text gains the block
representation; a plain-text object never gains blocks (blocks are
authoritative in the editor and would hide the plain body) — only content is
appended and rich_text is dropped, and `position` is ignored. The write is a
normal object save: revision bump, version snapshot, sync-journal entry.
Unknown id 404; empty markdown 400.

### POST /v1/journal/{date}/append

Get-or-create with the existing lazy journal semantics, then append as
above. `section` places the content under a level-2 heading of that name —
a heading block inserted before the next same-or-higher-level heading, or a
"## <name>" line inserted before the next "#" line in plain text — creating
the heading at the end when missing. Invalid date or empty markdown: 400.

### Chunked uploads

init -> part -> complete for files above the 50 MB limit of POST /v1/assets
(unchanged). Parts are sequential, 1-based, 1 B..25 MB; only the final part
may be smaller than 5 MB; the running total may never exceed the declared
size; total limit 500 MB; the mime allowlist matches POST /v1/assets
(standalone = image/PDF, attached = any) with the final mime sniffed from
the first part. complete produces a standard Asset with the same semantics
as the small path; abort discards. Sessions are staged OUTSIDE the
encrypted store ($TMPDIR/lumen-uploads/<id>/, override LUMEN_UPLOAD_DIR,
0700): parts are sealed with the store key on arrival and the session
metadata is sealed too, so staging never holds plaintext, survives a
restart, and is removed by complete, abort or the 24 h TTL janitor that
sweeps on every upload request. A disk-space guard answers 507 when the
remaining bytes plus a 64 MB margin do not fit.

Storage: large attachments persist as sealed rows of the new blob_chunks
table (schema version 4) instead of one blobs row. Fsck verifies the chunk
invariants; blob deletion removes both shapes; asset content streams
chunked attachments piece by piece.

### Pagination

GET /v1/objects and GET /v1/search accept `limit` (integer 1..200, default
50, else 400) and `cursor` (opaque; malformed 400). The response stays a
JSON array; when more results exist the response carries the
X-Lumen-Next-Cursor header (absent on the last page, CORS-exposed).
Pagination runs after every filter. The cursor is a keyset over the display
order (updated_at desc, id asc). Backward compatibility: a request with
neither limit nor cursor keeps the full list and no header.

### GET /v1/api

Discovery document: version, the current token's effective scopes, the
operational limits and the endpoint catalog (name, method, path, scopes).
Endpoint scope values: public, api:read, api:write, full (the first-party
sync protocol).
```

## Testing

`cd server && go test -race ./...`, `go vet ./...` and `gofmt -l .` are
clean. New coverage in `server/api_v2_test.go`,
`server/pagination_test.go` and `server/upload_chunked_test.go`:

- scope parsing (implication, dedup, unknown values) and the enforcement
  matrix across read, write, pure-POST and sync endpoint classes, plus the
  403 message contract;
- version header on `/health`, authenticated routes, unknown request
  versions and 401 responses;
- discovery: version, scope reporting for full/read tokens, limits, catalog
  entries with their scope classes;
- from-url through the real SSRF-guarded fetcher (reader-test dialer):
  preview + reader content, URL cleaning, tag get-or-create, notes blocks,
  journal link, dedupe on a differently-spelled URL, notes/union on the
  duplicate, tag-title conflicts, URL validation;
- Markdown create round-trip (blocks, content mirror, property passthrough,
  tags) and every rejection; append to block/plain/empty objects, position
  insert and validation, revision bump, version snapshot, sync-journal
  reach;
- journal append: lazy creation, repeat appends, sections in both block and
  plain-text representations, section containment, date/markdown
  validation;
- pagination: full-list compatibility, cursor walk with no repeats in
  display order, limit validation, default limit via cursor-only pages,
  composition with `type_id` and search filters, template exclusion;
- chunked uploads: 3-part lifecycle with exact byte round-trip and
  Content-Length, storage shape (no whole-blob row, chunk rows),
  restart survival, single-part uploads, mime sniffing and the standalone
  image/PDF rule, part ordering/size rules, complete-before-done, abort,
  TTL janitor on access, purge cleaning chunk rows, and the untouched
  whole-blob path.

Manual verification against the disposable sandbox (port 8879, never the
personal install):

```sh
cd server
TOKEN=$(cat "$LUMEN_TEST_SECRETS_DIR/api.token")   # disposable sandbox secret
curl -s -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8879/v1/api | jq .
curl -s -H "Authorization: Bearer $TOKEN" 'http://127.0.0.1:8879/v1/objects?limit=2' -D - -o /dev/null | grep -i x-lumen
```

## Client follow-ups

- **Desktop**: none required — the new endpoints are server-side additions;
  adopting `append-markdown` for "paste as Markdown" and the pagination
  header for large libraries is optional future work.
- **Mobile**: unaffected; the sync protocol is explicitly full-trust.
  Appending to the daily note could later use `/v1/journal/{date}/append`.
- **Extension**: may switch its share flow to `/v1/objects/from-url` to get
  notes/tags in one request; the CORS list already allows
  `X-Lumen-Api-Version` and exposes `X-Lumen-Next-Cursor`.
- **Stage 13 (mcp-server)**: consume `GET /v1/api` for the tool catalog and
  limits; use the Markdown and journal-append endpoints for the create and
  append tools.

## Follow-ups

- Stream chunked attachments in the exporter too (today `Store.Blob`
  reassembles them in RAM, like whole blobs).
- Optional `base_revision` CAS on the append endpoints (PATCH semantics).
- A second token for scripting, so the main client token can stay
  unrestricted (needs a token file per purpose and per-token scope files).
- Wikilink/mention resolution to object ids inside `notes_markdown`
  (depends on the markdown-converter follow-up).
- A `/v1/objects/{id}/blocks` block-level CRUD endpoint if the MCP stage
  needs finer granularity than append.
- Export upload-session state for observability (bytes staged, sessions
  count).
