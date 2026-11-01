# Query filters (stage 7)

Status: implemented 2026-10-31 on `backend/query-filters`.
Program: `docs/tracking/BACKEND-PARITY-STRUCTURE.md` §7.
Plan: `docs/tracking/PLAN-PARIDAD-CAPACITIES.md` §F2.1 and §F2.5.
Capacities reference: <https://docs.capacities.io/reference/queries>.

## What & why

Capacities queries filter by linked and backlinked objects, empty block
content, collections and date ranges. That is what makes orphan queues
("notes with no links and no backlinks"), maintenance views ("meetings with no
project") and multi-day calendars possible without downloading the whole
space. Lumen already exposed `type_id` and `q` on `GET /v1/objects`; this stage
adds the relational, content, collection and date filters on the server so
every client gets the same semantics.

The same stage extends the date property format with an optional `end`
(`{start,end}`) so a meeting, trip or conference can span several days. Single
dates keep working unchanged: the legacy plain string is still accepted and
stored as-is.

## API

`GET /v1/objects` and `GET /v1/search` share the handler, so every filter works
on both. All filters are ANDed. Unknown parameters are ignored; a malformed
value for a known parameter returns HTTP 400 with `{"error":"..."}`.

### Grammar

```text
query        := param ("&" param)*
param        := known | unknown
known        := "type_id=" type-id
              | "q=" text
              | "linked_count=" count-range
              | "linked_includes=" object-id
              | "linked_type=" type-id
              | "backlinked_count=" count-range
              | "backlinked_includes=" object-id
              | "backlinked_type=" type-id
              | "content_empty=" bool
              | "collection=" object-id
              | "props." property-name "." date-op "=" value
count-range  := integer | integer "," integer | integer "," | "," integer
date-op      := "from" | "to" | "has_end"
bool         := "true" | "false"          (case-insensitive)
value        := "YYYY-MM-DD"              (from, to)
              | bool                      (has_end)
```

- `type_id`, `q`, `content_empty`, `linked_count`, `backlinked_count` and the
  `props.*` parameters are scalar: when repeated, the first value wins, which
  matches the legacy parser.
- `linked_includes`, `linked_type`, `backlinked_includes`, `backlinked_type`
  and `collection` are repeatable and ANDed: every listed object/type must be
  present.
- `props.<name>` is split from the right, so property keys containing dots
  (`review.date`) work.
- Unknown parameters are ignored. This keeps the server compatible with newer
  clients and with parallel stages that add parameters (`include_templates`,
  pagination cursors, ...).
- `from` later than `to` is rejected instead of silently returning nothing.

### Parameters

| Parameter | Value | Matches |
|---|---|---|
| `type_id` | type id | objects of that type (unchanged) |
| `q` | text | case-insensitive substring of title and plain content (unchanged) |
| `linked_count` | `N`, `min,max`, `min,`, `,max` | number of distinct outgoing relations: explicit `links` plus `property_links` |
| `linked_includes` | object id, repeatable | links to every listed object |
| `linked_type` | type id, repeatable | links to at least one object of every listed type |
| `backlinked_count` | `N`, `min,max`, `min,`, `,max` | number of distinct objects that link to this one (deduped union) |
| `backlinked_includes` | object id, repeatable | is linked from every listed object |
| `backlinked_type` | type id, repeatable | is linked from at least one object of every listed type |
| `content_empty` | `true` or `false` | `true`: no text in blocks, rich_text or content; `false`: some text |
| `collection` | collection object id, repeatable | listed in the `items` property of every listed collection |
| `props.<name>.from` | `YYYY-MM-DD` | the date range ends on or after this day |
| `props.<name>.to` | `YYYY-MM-DD` | the date range starts on or before this day |
| `props.<name>.has_end` | `true` or `false` | `true`: the date has an explicit end; `false`: single date |

### Examples

```http
GET /v1/objects?type_id=note&linked_count=0
GET /v1/objects?type_id=note&linked_count=2,5&linked_type=person
GET /v1/objects?linked_includes=ID1&linked_includes=ID2
GET /v1/objects?backlinked_count=0&type_id=note
GET /v1/objects?backlinked_type=project&backlinked_includes=PROJECT_ID
GET /v1/objects?content_empty=true
GET /v1/objects?collection=COLLECTION_ID
GET /v1/objects?props.due_date.from=2026-10-01&props.due_date.to=2026-10-31
GET /v1/objects?props.period.has_end=true
GET /v1/search?q=garden&type_id=note&linked_count=0
```

Response (trimmed; the shape is the existing object array):

```json
[
  {
    "id": "5dbdfb44-e054-40ce-850e-8cb8f22d152f",
    "type_id": "note",
    "title": "Orphan note",
    "content": "",
    "properties": {"period": {"start": "2026-10-01", "end": "2026-10-05"}},
    "links": [],
    "property_links": [],
    "created_at": "2026-10-31T09:00:00.000000000Z",
    "updated_at": "2026-10-31T09:00:00.000000000Z",
    "sync_status": "synced",
    "revision": 1
  }
]
```

Error example:

```http
GET /v1/objects?props.period.from=2026-13-01
HTTP/1.1 400 Bad Request
{"error":"props.period.from must be a YYYY-MM-DD date"}
```

## Data model

### Date properties

A `date` property accepts both forms:

```json
{"date": "2026-10-31"}
{"period": {"start": "2026-10-01", "end": "2026-10-05"}}
```

- `start` is required and uses `YYYY-MM-DD`.
- `end` is optional, uses `YYYY-MM-DD`, and must be on or after `start`.
- A plain string is equivalent to `{start: <date>}`: a single day with no end.
- A missing or empty `end` means "no end". `end` equal to `start` is allowed
  and is still an explicit end.
- The server validates both forms on create, PATCH and mobile sync push.
- No schema version bump, no migration and no index change: the value stays
  inside the encrypted `properties` payload. Existing fixtures contain no date
  values, so `contracts/fixtures/` does not change.
- Time of day is not part of the format yet (follow-up).

### Index and collections

The in-memory index is unchanged. It already stores `Links`, `PropertyLinks`
and the reverse `backlinkSets`; the filters read those structures directly.
Collection objects are read on demand through `collectionItemIDs` (see
`server/query_collection.go`), which is the only place the filters touch the
collections stage. When `backend/collections` is merged, that helper can be
replaced by the collections package helper without touching the parser or the
relation matcher.

## How

### Two-phase evaluation

Payloads (content and properties) are encrypted in SQLite and decrypted on
demand, while the RAM index keeps metadata, links and backlinks. The filter
pipeline therefore runs in two phases:

1. **Index phase** (`matchRelations`): `type_id`, `linked_*` and `backlinked_*`
   are evaluated against `memIndex` under its read lock. No object is
   decrypted. Backlink counts and membership are hash lookups on
   `backlinkSets`; outgoing counts use the deduped union of `links` and
   `property_links`, the same union that `/v1/objects/{id}/backlinks` exposes.
2. **Payload phase** (`matchObject`): only the candidates that survived phase 1
   are decrypted, then `q`, `content_empty` and `props.*` date filters run.

The `collection` filter sits between the phases: it decrypts one collection
object per filter value and intersects its `items` with the candidate ids.

Complexity, with `C` candidates after `type_id`, `L` outgoing links and `B`
backlinks per object, `M` objects surviving phase 1 and `R` results:

| Step | Cost |
|---|---|
| Index phase | `O(C + Σ(L+B))` time, `O(R)` ids in memory, zero decryptions |
| `linked_count` / `backlinked_count` | `O(1)` per object (hash set size) |
| `linked_includes` / `backlinked_includes` | `O(1)` per listed id |
| `linked_type` / `backlinked_type` | `O(L)` / `O(B)` per object with map lookups |
| `collection` | one decryption plus `O(items)` per collection |
| Payload phase | `O(M)` decryptions |
| Serialization | `O(R)` |

A request without filters keeps the previous behavior: every object is
decrypted and returned, in the same most-recently-updated-first order.

### Content emptiness

`content_empty` mirrors the desktop editor precedence: a non-empty `blocks`
array is authoritative; otherwise a `rich_text` string; otherwise the plain
`content` field. Markup that renders no words (`<p></p>`, `<p>&nbsp;</p>`)
counts as empty. Image-only blocks are empty because they carry no text.
The alternative (an object is non-empty if any representation has text) would
disagree with what the user sees when a stale representation is present.

### Date overlap

A date value is normalized to an inclusive `[start,end]` range; a single date
is `[date,date]`. An object matches `props.<name>.from`/`to` when its range
intersects the filter range:

```text
object.start <= to   AND   object.end >= from
```

Missing bounds are open. An object without the property, or with a non-date
value, does not match. `has_end=true` requires an explicit `end`;
`has_end=false` requires a single date (it does not mean "property empty").

### Error policy

- Malformed value for a known parameter: 400.
- Unknown parameter: ignored (forward compatibility).
- Missing collection, or an object that is not a collection: 400.
- Unknown object id in `linked_includes` / `backlinked_includes` or unknown
  type id in `linked_type` / `backlinked_type`: no match, not an error. Links
  are cleaned when a target is deleted, so a stale id simply matches nothing.
- Store/decryption failure: 500.

### Alternatives considered

- **SQL pushdown**: payloads are encrypted, so SQLite cannot filter on them
  without a second plaintext index. The RAM index already has the relation
  metadata; using it avoids a schema change.
- **Storing link counts in `ObjectMeta`**: saves the `O(L)` union per object
  but adds memory and invalidation paths. The index already stores the links,
  and list filters are not a hot enough path to justify it.
- **Normalizing legacy dates to `{start}` on write**: rewrites existing data
  and breaks clients that expect a string. Both forms are accepted instead.
- **Rejecting unknown parameters**: would break newer clients and parallel
  stages. Ignoring them is the compatible choice.
- **A generic AND/OR expression parser**: Capacities' UI supports OR groups;
  this stage keeps the flat, ANDed query-string grammar. OR can be expressed
  with multiple requests, and a later query-object stage can add groups.

## Contracts

The following sections were added to `contracts/SYNC.md`.

### Date properties with an optional end

A `date` property accepts the legacy plain string and the extended
`{start,end}` object. Both forms coexist; clients must accept both when
reading.

```json
{"properties": {"date": "2026-10-31"}}
{"properties": {"period": {"start": "2026-10-01", "end": "2026-10-05"}}}
```

- `start` is required and uses `YYYY-MM-DD`.
- `end` is optional, uses `YYYY-MM-DD`, and must be on or after `start`.
- A plain string is equivalent to `{start: <date>}`: a single day with no end.
- A missing or empty `end` means "no end". `end` equal to `start` is allowed
  and is still an explicit end.
- The server validates both forms on create, PATCH and mobile sync push.
- No schema version bump and no index change: the value stays inside the
  encrypted `properties` payload. Existing fixtures contain no date values, so
  they do not change.
- Time of day is not part of the format yet (follow-up).

### Object list filters

`GET /v1/objects` and `GET /v1/search` (they share the handler) accept these
query parameters. Every filter is ANDed with the others. Unknown parameters are
ignored so older servers tolerate newer clients; a malformed value for a known
parameter returns HTTP 400 with `{"error":"..."}`. Repeated scalar parameters
use the first value; repeated `*_includes`, `*_type` and `collection` values are
ANDed.

| Parameter | Value | Matches |
|---|---|---|
| `type_id` | type id | objects of that type (unchanged) |
| `q` | text | case-insensitive substring of title and plain content (unchanged) |
| `linked_count` | `N`, `min,max`, `min,`, `,max` | number of distinct outgoing relations: explicit `links` plus `property_links` |
| `linked_includes` | object id, repeatable | links to every listed object |
| `linked_type` | type id, repeatable | links to at least one object of every listed type |
| `backlinked_count` | `N`, `min,max`, `min,`, `,max` | number of distinct objects that link to this one (deduped union) |
| `backlinked_includes` | object id, repeatable | is linked from every listed object |
| `backlinked_type` | type id, repeatable | is linked from at least one object of every listed type |
| `content_empty` | `true` or `false` | `true`: no text in blocks, rich_text or content; `false`: some text |
| `collection` | collection object id, repeatable | listed in the `items` property of every listed collection |
| `props.<name>.from` | `YYYY-MM-DD` | the date range ends on or after this day |
| `props.<name>.to` | `YYYY-MM-DD` | the date range starts on or before this day |
| `props.<name>.has_end` | `true` or `false` | `true`: the date has an explicit end; `false`: single date |

Examples:

```http
GET /v1/objects?type_id=note&linked_count=0
GET /v1/objects?type_id=note&linked_count=2,5&linked_type=person
GET /v1/objects?linked_includes=ID1&linked_includes=ID2
GET /v1/objects?backlinked_count=0&type_id=note
GET /v1/objects?backlinked_type=project&backlinked_includes=PROJECT_ID
GET /v1/objects?content_empty=true
GET /v1/objects?collection=COLLECTION_ID
GET /v1/objects?props.due_date.from=2026-10-01&props.due_date.to=2026-10-31
GET /v1/objects?props.period.has_end=true
```

Date range filters are inclusive and use overlap semantics: an object matches
when its date range `[start,end]` intersects `[from,to]`. A single date is the
range `[date,date]`. An object without the property, or with a non-date value,
does not match. `props.<name>` may contain dots; the operation is split from
the right. `from` later than `to` is rejected.

`content_empty` follows the editor precedence: a non-empty `blocks` array is
authoritative, otherwise a `rich_text` string, otherwise `content`. Markup that
renders no words (`<p></p>`, `<p>&nbsp;</p>`) counts as empty.

The `collection` filter requires an existing object of type `collection` with
an `items` object relation. A missing id or an object of another type returns
HTTP 400. Collections themselves are not part of their own item list. The
filter selects membership only: results keep the normal listing order (most
recently updated first). Use the collections endpoints for the curated order.

## Testing

New Go tests in `server/query_test.go`:

- `TestListFiltersRelational`: outgoing/incoming counts, min/max ranges,
  `*_includes` (AND), `*_type` (AND), property relations, and deduplication
  when the same target is both an explicit link and a property relation.
- `TestListFiltersContentEmpty`: plain, whitespace, rich-text markup, blocks,
  block runs and the blocks-over-content precedence.
- `TestListFiltersDateRanges`: legacy string and `{start,end}` values, inclusive
  overlap boundaries, open bounds, `has_end`, dotted property names, write
  validation and malformed filter values.
- `TestListFiltersCollection`: a manually registered `collection` type with
  `item_type_id` and `items`, single and multiple collections, empty
  collections, combination with `content_empty`, and 400 for missing or
  non-collection ids.
- `TestListFilterParserAndLegacyParams`: unfiltered listing order, `type_id`,
  `q`, `/v1/search`, ignored unknown parameters, first-value semantics and the
  malformed-value matrix.
- `TestParseDatePropertyValue` and `TestObjectHasText`: table-driven unit
  coverage for the value helpers.

Commands run on the stage branch:

```sh
cd server
gofmt -l .          # no output
go vet ./...        # clean
go test ./...       # ok
go test -race ./... # ok
```

Manual verification (disposable sandbox only, never the personal install):

```sh
docker compose -f sandbox/compose.test.yaml up -d
curl -s -H "Authorization: Bearer $LUMEN_TOKEN" \
  "http://127.0.0.1:8879/v1/objects?type_id=note&linked_count=0"
curl -s -H "Authorization: Bearer $LUMEN_TOKEN" \
  "http://127.0.0.1:8879/v1/objects?props.period.has_end=true"
docker compose -f sandbox/compose.test.yaml down
```

## Client follow-ups

- **Desktop**: move query views from client-side filtering to these server
  parameters; render a date range on every day it covers in the calendar;
  write `{start,end}` from the date editor and keep accepting plain strings on
  read.
- **Desktop/mobile**: parse both date forms anywhere a date property is read
  (task due dates, journal, property editors). A client that assumes a string
  will break on ranged values.
- **Desktop**: use `content_empty=true` for the "not written yet" section and
  `linked_count=0&backlinked_count=0` for the orphan queue.
- **Desktop/mobile**: use `collection=<id>` for collection views instead of
  filtering locally; collections must expose `items` as an object relation.
- **Mobile**: the sync payload already carries the new date object unchanged;
  no protocol change is needed, only value parsing.
- **Extension**: no change.

## Follow-ups

- Time of day in date properties (PLAN §F2.5 mentions optional time) is not
  implemented; the format is date-only for now.
- `q` still decrypts every candidate because content is encrypted. A full-text
  index is a separate stage.
- OR groups and variable queries (PLAN §F2.3) are out of scope; the current
  grammar is flat and ANDed.
- After `backend/collections` is merged, replace `collectionItemIDs` with the
  collections helper if one exists and re-run the collection tests.
- After `backend/trash` is merged, verify that trashed objects stay out of the
  index; these filters inherit that behavior automatically.
- `linked_count` counts collection `items` as property relations for the
  collection object itself. That is consistent with backlinks but worth
  remembering when building collection dashboards.
- No pagination yet; `api-v2` adds it. Unknown parameters are already ignored,
  so adding `limit`/`cursor` later will not break this parser.
