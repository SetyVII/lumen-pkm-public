# Full space export

Stage 11 of the backend parity program (`docs/tracking/BACKEND-PARITY-STRUCTURE.md`).
Branch: `backend/full-export`. Status: implemented 2026-10-31.

## What & why

Capacities exports a whole space as a portable ZIP: Markdown files with front
matter, embedded assets and a manifest, so a user can move their notes to
another tool or inspect them without the app
(<https://docs.capacities.io/reference/export>). Lumen already had a lossless
block tree and a Markdown converter (stage 4), but no way to take the library
out in one operation. This stage adds `GET /v1/export` and `POST /v1/export`,
which produce a deterministic ZIP layout, rewrite inter-object links to
relative file links, export attachment bytes, and emit one CSV per type plus a
machine-readable manifest.

The export is a **portable snapshot, not a backup**. The encrypted SQLite
store remains the source of truth; version history, the mobile sync journal,
tombstones and the trash payload are server-side only and are deliberately not
included. The ZIP is also not a transactional snapshot: each object is read
atomically, but a concurrent edit may appear in some files and not others.

## API

Both endpoints require the same bearer token as the rest of `/v1` and return
`application/zip`.

### `GET /v1/export`

Options are query parameters:

```http
GET /v1/export?type_ids=note&type_ids=book&include_assets=false&q=reading
Authorization: Bearer <token>
```

`type_ids` may be repeated or comma-separated. `include_assets` and
`include_templates` are booleans (default `true`); `q` is the canonical text
filter. Unknown query parameters are ignored, matching the rest of the API.

### `POST /v1/export`

```http
POST /v1/export
Authorization: Bearer <token>
Content-Type: application/json

{
  "type_ids": ["note", "book"],
  "include_assets": true,
  "include_templates": true,
  "q": "reading"
}
```

An empty body or `{}` means all defaults. The body is limited to 1 MB. Unknown
JSON fields are ignored for forward compatibility.

| Option | Default | Meaning |
|---|---|---|
| `type_ids` | all types | Only objects of these type ids. Empty entries are ignored; unknown ids return 400. |
| `include_assets` | `true` | Write attachment bytes under `assets/`. When false, image runs degrade to text and `manifest.asset_count` is 0; the front matter keeps asset ids/metadata, only the bytes are omitted. |
| `include_templates` | `true` | Include objects with `properties.is_template: true`. |
| `q` | empty | Case-insensitive substring over title, content, `reader_content` and aliases, using `objectMatchesQuery`, the same matcher as `GET /v1/objects` and `GET /v1/search`. The value is trimmed; empty or whitespace-only means no filter. |

### Response

```http
200 OK
Content-Type: application/zip
Content-Disposition: attachment; filename="lumen-export-20261031-091500.zip"
Content-Length: 123456
Cache-Control: no-store
```

The ZIP is built in a private temporary file first, then streamed. A store or
ZIP error returns a JSON `500` before any archive bytes are sent; a client
disconnect cancels the build and removes the temporary file. The response
extends the HTTP write deadline to 30 minutes because the server-wide 45-second
`WriteTimeout` is too short for a large space.

Errors: `400` for invalid JSON/options or an unknown type id, `401` without a
bearer token, `500` if a payload cannot be read or the archive cannot be built.
The export does not modify objects, revisions or the sync journal.

## ZIP layout

```
manifest.json
types.json
assets/<asset-id>-<human-readable-name>
<type-folder>/<slug>-<object-id>.md
<type-folder>.csv
```

Example:

```
manifest.json
types.json
assets/45c0e1e9-0bac-4618-a54f-4ebdbbba3306-cover.png
books/deep-work-35a67906-8c05-49de-8159-61889b9fd600.md
books/book-template-9b1f...md
books.csv
notes/linked-note-0f7c...md
notes.csv
tags/go-240a...md
tags.csv
```

- `types.json` is the full type registry (`[]ObjectType`: id, name, icon,
  color, properties, `default_template_id`), sorted by id. It is written even
  for a filtered export so an importer can recreate schemas. A
  `default_template_id` may point at a template that is absent when
  `include_templates=false`; an importer must clear dangling references.
- `manifest.json` is written last and describes the archive:
  `schema_version` (1), `format` (`lumen-export`), `generator`, `exported_at`,
  effective `options`, `counts` per type, `object_count`, `asset_count`,
  `template_count`, the `types` table (id, name, folder, csv, count) and the
  `assets` table (id, path, name, mime, size, object_id, media_object_id).
- A type with no exported objects has no folder and no CSV. It is still listed
  in `manifest.json` (count 0) and in `types.json` (schema).
- ZIP entry names always use `/`; object and asset names are sanitized so a
  title or attachment name can never introduce `..` or a path separator.

## Filename policy

### Type folders

`<type-folder>` is the pluralized, slugified type name:

- Built-in type ids are already English slugs, so they are pluralized as
  English: `note` -> `notes`, `book` -> `books`, `query` -> `queries`,
  `person` -> `persons` (not "people"), `collection` -> `collections`,
  `journal` -> `journals`, `pdf` -> `pdfs`, `web` -> `webs`.
- Custom types have UUID ids, so their folder comes from the display name with
  Spanish pluralization (the UI language): "Receta" -> `recetas`,
  "Colección" -> `colecciones`, "Lápiz" -> `lapices`.
- Accents are transliterated to ASCII; other separators collapse to one `-`;
  the slug is capped at 60 runes.
- Collisions get `-2`, `-3`, ... in type-id order. `assets` is reserved, so a
  type that would land there becomes `assets-type`.
- A dangling type id (the store was edited outside the API) uses
  `<type-id>-objects/` and the type id as its display name, so its objects are
  not silently dropped.

Built-in folders are stable because their type ids are stable. A custom type
folder follows the display name, so renaming a custom type changes its folder
between exports; the manifest maps type id to folder, so importers should use
that mapping rather than the folder name.

### Object files

`<slug>-<object-id>.md`, for example
`deep-work-35a67906-8c05-49de-8159-61889b9fd600.md`.

- The title slug is lowercase, accent-folded, punctuation-collapsed and capped
  at 60 runes. An empty title becomes `untitled`.
- The full object id is the uniqueness guarantee. Titles are unique only for
  normal objects; a template may share a title with a normal object, and a
  filtered export may contain only one of two same-titled objects. Using the
  id avoids every collision and keeps link resolution unambiguous.
- IDs are server-generated UUIDs or `journal-YYYY-MM-DD`; a defensive
  sanitizer still replaces any character outside `[A-Za-z0-9._-]`.

### Assets

`assets/<asset-id>-<sanitized-name>`, for example
`assets/45c0e1e9-...-cover.png`.

- The original name is taken with `filepath.Base`; characters outside
  `[A-Za-z0-9._-]` become `-`, a separator before the extension is removed
  (`a-b-c-.pdf` -> `a-b-c.pdf`), and the name is capped at 80 runes.
- An empty name becomes `asset`. The asset id prefix makes every file unique.
- The manifest keeps the original `name`, `mime`, `size`, `object_id` and
  `media_object_id`; the `path` is the sanitized ZIP path.

### CSVs

`<type-folder>.csv` at the ZIP root, for example `books.csv`. The file is
written only when the type has at least one exported object.

## Markdown front matter

Every `.md` file starts with YAML front matter:

```yaml
---
id: 0f7c1f0e-6a4e-4f2a-9b8e-3d1c2a5b7e90
type_id: book
title: Deep Work
created_at: "2026-10-31T09:00:00.000000000Z"
updated_at: "2026-10-31T09:12:44.123456789Z"
revision: 3
is_template: true
links:
  - 5d2e8a44-0d3e-4b6a-9c11-2f6f0f6a1b21
properties:
  aliases:
    - DW
  author: Cal Newport
  tags:
    - 8a1f0c2e-...
  reader_content: "line one\nline two"
---
# Deep Work
```

- `is_template` is emitted only when true. Absent means a normal object.
- `links` is the explicit `links` array (ids). `property_links` is derived and
  omitted.
- `properties` contains every stored property except `blocks` (rendered as the
  body), `rich_text` (editor HTML snapshot) and `is_template` (top-level).
  `aliases`, `tags`, `reader_content`, `assets`, `image_asset_id`, `asset_id`,
  date ranges, recurrence objects, collection items and query views are all
  preserved.
- Internal fields (`incarnation`, `sync_status`, `sync_error`) are not
  exported.
- The body is `BlocksToMarkdown(properties.blocks)` when the block array is
  non-empty. Otherwise it is `content`; if that is empty too, a plain-text
  extraction of `rich_text` is used so an old object does not export an empty
  body. A malformed block array also falls back to `content`/`rich_text`.
- The front matter is separated from the body by one blank line when the body
  is non-empty.
- YAML is emitted without a new dependency. Strings are plain only when they
  are unambiguous; otherwise they are JSON-quoted, which is valid YAML
  double-quoted scalar syntax. Reserved words (`true`, `null`, `yes`, ...),
  numeric-looking values, dates, punctuation, newlines and control characters
  are quoted. The `reader_content` property can therefore be a 500 KiB
  article on one quoted line without breaking the front matter.

## Link-rewrite rules

Only block-model links become file links. Property relations and explicit
`links` stay ids in the front matter and CSV.

| Input | Output when the target is exported | Output when it is not |
|---|---|---|
| run `href: lumen://object/<id>` or `object_id: <id>` | `[Title](../<type-folder>/<slug>-<id>.md)` | `Title (id: <id>)`, or `id: <id>` without a title |
| run `href: lumen://object/` (unresolved wikilink) | — | its plain title |
| image run `image_name: lumen://image/<id>` | `![](../assets/<asset-id>-<name>)` | `image (id: <id>)` |
| external `http(s)` link | unchanged | unchanged |

- Every `.md` file is one directory deep under its type folder, so
  `../<folder>/<file>` is correct even for a link inside the same folder.
- A target is "exported" only if it is in the selection: live, matching
  `type_ids`/`q`, and (for templates) allowed by `include_templates`. A link to
  a trashed object, a filtered-out object or an excluded template therefore
  degrades to text and never points at a missing file.
- For a degraded link, the visible title comes from the run text first, then
  from the in-memory metadata index (for a live but filtered-out object). A
  trashed object has no live metadata, so the run text or the id is used.
- Table cells and `columns` blocks are rewritten recursively. Inline math,
  code, formatting flags and external links are untouched.
- Image runs resolve `lumen://image/<id>` to the media object's asset. The
  block schema has no PDF block: a PDF is a media object with an `asset_id`
  property, so its asset is exported under `assets/` and the PDF object's
  `.md` front matter keeps `asset_id`. An image or PDF object linked as an
  object also gets its own `.md` file.
- `content` fallback text and `reader_content` are not rewritten: they are
  plain text or web Markdown, not the block model.

## CSV columns and values

Each type CSV has:

```
id,title,<property columns sorted>,is_template
```

- Property columns are the union of the type schema keys and every extra key
  stored on the exported objects, excluding `blocks`, `rich_text`,
  `is_template`, `id` and `title` (the last three have dedicated columns).
- Dates are ISO (`YYYY-MM-DD`); a `{start,end}` range is
  `YYYY-MM-DD..YYYY-MM-DD`.
- Lists are `;`-joined. Object relations (tags, collection items, ...) are
  `;`-joined id lists. User text that itself contains `;` is not escaped; the
  CSV is a flat convenience table, the front matter is authoritative.
- Maps (recurrence, query view, asset metadata) are compact JSON. A map with
  `start`/`end` is treated as a date range.
- Missing or null values are empty; booleans are `true`/`false`; numbers use
  decimal notation; `is_template` is `true`/`false`.
- CSV quoting follows RFC 4180 (`encoding/csv`), so multiline
  `reader_content` survives.

## Memory and streaming strategy

The server already keeps a metadata-only in-memory index (id, type, title,
dates, revision, links). The exporter uses that index for degraded-link titles
and keeps a small selection map (id, type, folder, filename) for link
resolution; it never loads the workspace into RAM:

1. **Selection pass** (`Store.ObjectBatches`, keyset pagination, batch 50):
   decrypt each live object once, apply `type_ids`/`include_templates`/`q`,
   keep only metadata for selected objects, collect CSV property keys and the
   asset/media ids referenced by properties and block image runs.
2. **Asset pass** (`Store.AssetBatches`, batch 50): decrypt one asset blob at a
   time with `Store.Blob`, the same path `GET /v1/assets/{id}/content` uses,
   and write it with `zip.Store` (images/PDFs are already compressed).
3. **Emission pass** (`Store.ObjectBatches` again): decrypt each selected
   object, render the front matter and `BlocksToMarkdown` body, write the
   `.md`, and append its CSV row to a per-type temporary file.
4. **CSV and manifest**: close the CSV temp files, add the non-empty ones to
   the ZIP, then write `manifest.json`.

Rows are fully read and the SQLite cursor is closed before each callback, so
the callback can call `Store.Blob` or the metadata index without deadlocking
the single-connection pool. CSV rows go to private temporary files (mode 0700
directory, 0600 files) instead of RAM. The ZIP itself is written to a 0600
temporary file and streamed only after it is complete, so a mid-export error
never becomes a 200 response with a truncated archive. Every temporary file is
removed on success, error and client disconnect.

Trade-offs: the selection pass decrypts the library once and the emission pass
decrypts the selected objects again. That is CPU work, not RAM, and export is
not a hot path. The alternative (keep every selected object in memory) would
violate the memory requirement; the alternative (a long SQLite read
transaction) would block writes for the duration of the export.

## Data model

No store schema, migration, index or fixture change. The only new store code
is two read-only iteration helpers:

```go
func (s *Store) ObjectBatches(batch int, fn func([]Object) error) error
func (s *Store) AssetBatches(batch int, fn func([]Asset) error) error
```

Both use keyset pagination (`WHERE kind='object' AND id > ? ORDER BY id LIMIT ?`)
and decrypt one batch at a time. Trashed objects are stored under the separate
`trash` record kind, so iterating `kind='object'` excludes them without an
extra filter. The export never writes to `records`, `blobs`, `sync_changes`,
`object_versions` or `meta`, and it does not bump revisions.

## Contracts

The authoritative section is `contracts/SYNC.md` under "Full space export".
Copied here for the stage record:

> Implemented 2026-10-31 (backend stage `full-export`). Capacities exports a
> space as a ZIP of Markdown files with front matter, assets and a manifest
> (<https://docs.capacities.io/reference/export>). Lumen exposes the same idea
> as two additive endpoints. The export is a portable snapshot for migration
> and inspection, not a backup of the encrypted store: version history, the
> sync journal and the trash payload are server-side only and are not
> included.
>
> ### Endpoints
>
> `GET /v1/export` and `POST /v1/export` both require the bearer token and
> return `application/zip` with
> `Content-Disposition: attachment; filename="lumen-export-YYYYMMDD-HHMMSS.zip"`.
> The ZIP is built in a private temporary file and streamed with
> `Content-Length`; a store error returns JSON before any archive bytes are
> sent.
>
> `GET` options are query parameters; `POST` accepts the same options as JSON
> (an empty body means all defaults):
>
> ```json
> {
>   "type_ids": ["note", "book"],
>   "include_assets": true,
>   "include_templates": true,
>   "q": "reading"
> }
> ```
>
> | Option | Default | Meaning |
> |---|---|---|
> | `type_ids` | all types | Only objects of these type ids. Repeated and comma-separated values are accepted; unknown ids return 400. |
> | `include_assets` | `true` | Write attachment bytes under `assets/`. When false, image runs degrade to text and `manifest.asset_count` is 0; the front matter keeps asset ids/metadata, only the bytes are omitted. |
> | `include_templates` | `true` | Include objects with `properties.is_template: true`. |
> | `q` | empty | Case-insensitive substring over title, content, `reader_content` and aliases, using the same matcher as `GET /v1/objects`/`GET /v1/search`. The value is trimmed; empty or whitespace-only means no filter. |
>
> The export is a best-effort snapshot, not a transaction: each object is read
> atomically, but a concurrent edit may be reflected in some files and not
> others. Use the encrypted store backup for point-in-time recovery.
>
> ### ZIP layout
>
> ```
> manifest.json
> types.json
> assets/<asset-id>-<human-readable-name>
> <type-folder>/<slug>-<object-id>.md
> <type-folder>.csv
> ```
>
> - `<type-folder>` is the pluralized, slugified type name, stable for the same
>   type. Built-in type ids are English slugs (`note` -> `notes`, `query` ->
>   `queries`, `person` -> `persons`); custom types use the display name with
>   Spanish pluralization because their ids are UUIDs. Collisions get `-2`,
>   `-3`; `assets` is reserved (`assets-type`).
> - Object files are `<slug>-<id>.md`, not title-only: titles are unique only
>   for normal objects, and a template may share a title with a normal object.
>   The full id makes every filename unambiguous; an empty title becomes
>   `untitled`.
> - A type with no exported objects has no folder and no CSV; it is still
>   listed in `manifest.json` and `types.json`.
> - `types.json` is the full type registry (id, name, icon, color, properties,
>   `default_template_id`) so an importer can recreate schemas. It is written
>   even for a filtered export.
> - `manifest.json` contains `schema_version` (1), `format`
>   (`lumen-export`), `generator`, `exported_at`, effective `options`,
>   `counts` per type, `object_count`, `asset_count`, `template_count`, the
>   `types` table (id, name, folder, csv, count) and the `assets` table (id,
>   path, name, mime, size, object_id, media_object_id).
>
> ### Markdown files
>
> Each `.md` starts with YAML front matter:
>
> ```yaml
> ---
> id: 0f7c...
> type_id: book
> title: Deep Work
> created_at: "2026-10-31T09:00:00.000000000Z"
> updated_at: "2026-10-31T09:00:00.000000000Z"
> revision: 3
> is_template: true
> links:
>   - 5d2e...
> properties:
>   aliases:
>     - DW
>   author: Cal Newport
>   tags:
>     - 8a1f...
> ---
> # Deep Work
> ```
>
> - `is_template` is emitted only when true. `links` is the explicit link
>   array (ids); `property_links` is derived and omitted.
> - `properties` contains every stored property except `blocks` (rendered as
>   the body), `rich_text` (editor HTML snapshot) and `is_template`
>   (top-level). `aliases`, `tags`, `reader_content`, `assets`,
>   `image_asset_id`, `asset_id`, date ranges and recurrence objects are all
>   preserved.
> - The body is `BlocksToMarkdown(properties.blocks)` when the block array is
>   non-empty. Otherwise it is `content`; if that is empty too, a plain-text
>   extraction of `rich_text` is used so an old object does not export an
>   empty body.
> - YAML strings are quoted conservatively (JSON-quoted when they contain
>   punctuation, newlines, reserved words or numeric-looking values). No YAML
>   dependency is added; JSON strings are valid YAML double-quoted scalars.
>
> ### Link rewriting
>
> - A block run with `href: "lumen://object/<id>"` or `object_id: <id>`
>   becomes `[Title](../<type-folder>/<slug>-<id>.md)` when the target is
>   exported.
> - A target that is not exported (trashed, filtered out by `type_ids`/`q`, or
>   excluded by `include_templates=false`) becomes plain text:
>   `Title (id: <id>)`, or `id: <id>` when no title is available. No link
>   points at a file that is not in the ZIP.
> - A run with `href: "lumen://object/"` and no id (an unresolved wikilink)
>   becomes its plain title.
> - An image run with `image_name: "lumen://image/<id>"` becomes
>   `![](../assets/<asset-id>-<name>)` when the media object's asset is
>   exported. Otherwise it becomes `image (id: <id>)`.
> - External links, `reader_content` links and `content` fallback text are
>   left unchanged. Property relations and explicit `links` stay ids in the
>   front matter and CSV; only block-model links become file links.
> - Table cells and `columns` blocks are rewritten recursively.
>
> ### CSV files
>
> `<type-folder>.csv` has `id`, `title`, one column per property (the type
> schema keys plus any extra stored keys, sorted) and `is_template`:
>
> - dates are ISO (`YYYY-MM-DD`); ranges are `start..end`;
> - lists are `;`-joined; object relations are `;`-joined id lists;
> - maps (recurrence, view, asset metadata) are compact JSON;
> - missing values are empty; booleans are `true`/`false`;
> - `blocks`, `rich_text`, `is_template`, `id` and `title` are not property
>   columns (the first two are not tabular; the last three have dedicated
>   columns).
>
> ### What is and is not included
>
> Included: live objects, templates by default, explicit links, typed and
> untyped properties, aliases, tags, reader content, asset metadata and bytes,
> type schemas, one CSV per type with objects, and the manifest.
>
> Excluded: trashed objects (they stay restorable through `/v1/trash`; a later
> `export_trash` option can add them), version history (`object_versions` is
> server-side only), the sync journal (`sync_changes`, `sync_ops`),
> tombstones, and internal fields (`incarnation`, `sync_status`,
> `sync_error`, `property_links`). Assets owned only by trashed objects are
> not exported.
>
> ### Memory and streaming
>
> The exporter never loads the workspace into RAM. It keeps the metadata index
> that the server already maintains, then makes two bounded passes over
> `records` with keyset pagination (`exportBatchSize = 50`): one to select
> objects and collect CSV columns/asset references, one to render bodies.
> Asset blobs are decrypted one at a time, exactly like
> `GET /v1/assets/{id}/content`. CSV rows go to private temporary files per
> type; the ZIP is written to a 0600 temporary file and streamed only after it
> is complete. The temporary files are removed on every path.
>
> Errors: `400` for invalid JSON/options or an unknown type id; `401` without
> a bearer token; `500` if a store payload cannot be read or the ZIP cannot be
> built. The export does not change objects, revisions or the sync journal.

## Testing

`cd server && go test ./...`, `go test -race ./...`, `go vet ./...` and
`gofmt -l .` are clean.

`server/export_test.go` covers:

- full round-trip: several types, a tag relation, aliases, a date range, a
  template, a trashed object, an uploaded PNG asset, a block object link and
  an image run; verifies the ZIP entries, front matter, body links, CSV
  columns/values, manifest counts, `types.json`, and that every relative
  Markdown link resolves to a real ZIP entry;
- asset bytes are byte-identical to the uploaded bytes, including a PDF media
  object whose front matter keeps `asset_id`;
- filtered exports (`type_ids`, `q`, both GET and POST), unknown type id ->
  400, and link degradation to `Title (id: <id>)` when the target is filtered
  out;
- template inclusion by default, `include_templates=false`, and the
  `is_template: true` front-matter flag;
- trashed objects excluded, and restored objects present again;
- `include_assets=false`: no `assets/` entries, `asset_count` 0, image run
  degrades to `image (id: <id>)`;
- empty space: a valid ZIP with only `manifest.json` and `types.json`;
- 300 seeded objects: bounded batching, 300 `.md` files, 301 CSV rows
  (header + 300);
- `ObjectBatches`/`AssetBatches` keyset pagination with small batches;
- 401 without a bearer token;
- YAML escaping for titles, multiline `reader_content`, reserved words and
  date strings;
- CSV value conversion for ranges, lists, maps, booleans and numbers;
- folder/filename policy: English and Spanish pluralization, custom-type
  collisions, reserved `assets`, accent folding, empty titles, same-title
  objects, asset name sanitization;
- GET and POST produce the same entry set;
- `rich_text`/`content` body fallbacks;
- object links inside table cells and `columns` blocks;
- a link to a trashed object degrades to text.

Manual verification against the disposable sandbox (port 8879,
`sandbox/compose.test.yaml`; never the personal installation):

```sh
cd server
TOKEN=$(cat "$LUMEN_TEST_SECRETS_DIR/api.token")   # disposable sandbox secret
curl -sS -H "Authorization: Bearer $TOKEN" \
  -o /tmp/lumen-export.zip http://127.0.0.1:8879/v1/export
unzip -l /tmp/lumen-export.zip
unzip -p /tmp/lumen-export.zip manifest.json | jq .
```

The export is not a backup: restoring a space from it is a future bulk-import
feature, and point-in-time recovery still uses the encrypted store backup.

## Client follow-ups

- **Desktop**: add "Export space" (whole space) and "Export this view"
  (current `type_ids`/`q`) actions that stream the ZIP to a user-chosen file;
  show progress for large spaces and do not buffer the archive in RAM.
- **Desktop**: a restore/import flow can read `manifest.json` + `types.json`,
  create missing types, then import `.md` files with front matter. That is a
  later stage; this endpoint only produces the archive.
- **Mobile**: no protocol change. A phone can download the ZIP through the
  same bearer token if it needs an offline copy; the normal sync path remains
  the delta pull.
- **Extension**: no change; the export is a server-side operation.
- Clients should treat `manifest.json` as the authoritative index and use
  `types[].folder` rather than guessing folder names, especially for custom
  types.

## Follow-ups

- Add an `export_trash=true` option that writes trashed objects to a separate
  `trash/` tree (with `deleted_at`), so a user can inspect or migrate them
  without restoring first.
- Add a bulk-import counterpart that consumes this layout (`types.json`,
  front matter, assets, CSV) and resolves relative links back to object ids.
- Consider a streaming ZIP writer that sends bytes while building, once the
  server can tolerate a truncated response on store errors; the temp-file
  design was chosen for clean error handling.
- Consider a `include_version_history=true` option if users want snapshots in
  the archive; today history is server-side only by design.
- Consider a per-export `since`/`updated_after` filter for incremental
  snapshots; `q` and `type_ids` cover the common cases.
- The `;` list separator is ambiguous for user text containing `;`. A future
  format version could use JSON arrays in CSV cells; version 1 keeps the flat
  representation documented above.
- Custom type folders follow the display name, so a rename changes the folder
  between exports. If that becomes a problem, pin a folder in the type schema
  (a new optional `export_folder` field) in a later contract version.
