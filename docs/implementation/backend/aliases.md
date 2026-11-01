# Aliases (default property)

Status: implemented on `backend/aliases` (2026-10-31). Server-side only; the
client work is listed at the end.

## What & why

Capacities treats **aliases** as a default property of every object: alternate
names used by the command palette and by linking search, so an object titled
"HP1" can be found by typing "Harry Potter", and the chosen alias can be
inserted as the link text
(<https://docs.capacities.io/reference/properties#aliases>). Lumen only knew
titles, so every object had exactly one searchable name. This stage adds
`aliases` to the object model, validates it centrally, keeps it in the
in-memory index, and teaches `GET /v1/search` to match it while reporting which
alias matched. Unlinked mentions are explicitly out of scope.

## API

### Create and update

`POST /v1/objects` and `PATCH /v1/objects/{id}` accept the property inside the
existing `properties` document:

```json
{
  "type_id": "note",
  "title": "Harry Potter and the Philosopher's Stone",
  "properties": {"aliases": ["HP1", "The Stone"]}
}
```

The response (201/200) is the stored object:

```json
{
  "id": "0f7c1f0e-6a4e-4f2a-9b8e-3d1c2a5b7e90",
  "type_id": "note",
  "title": "Harry Potter and the Philosopher's Stone",
  "content": "",
  "properties": {"aliases": ["HP1", "The Stone"]},
  "links": [],
  "created_at": "2026-10-31T10:00:00.000000000Z",
  "updated_at": "2026-10-31T10:00:00.000000000Z",
  "revision": 1,
  "sync_status": "synced"
}
```

`properties` remains a full replacement document. To clear aliases send
`{"properties": {}}` or `{"properties": {"aliases": []}}`; both remove the key.
Omitting `aliases` while replacing other properties also removes it.

### Read

`GET /v1/objects` and `GET /v1/objects/{id}` return `properties.aliases` when
present. Objects without aliases have no key (never `null`).

### Search

```http
GET /v1/search?q=harry
```

```json
[
  {
    "id": "0f7c1f0e-6a4e-4f2a-9b8e-3d1c2a5b7e90",
    "type_id": "note",
    "title": "HP1",
    "content": "",
    "properties": {"aliases": ["Harry Potter", "The Stone"]},
    "links": [],
    "created_at": "2026-10-31T10:00:00.000000000Z",
    "updated_at": "2026-10-31T10:00:00.000000000Z",
    "revision": 1,
    "sync_status": "synced",
    "matched_alias": "Harry Potter"
  }
]
```

- Matching is the existing case-insensitive substring over title and content,
  extended to aliases.
- `matched_alias` appears only when the object matched through an alias and the
  title did not match. It carries the stored spelling so a client can insert it
  as link text.
- The response is still a JSON array of objects; the new field is additive.
- `type_id` still narrows the search.
- `GET /v1/objects?q=` uses the same matcher (without `matched_alias`) so both
  list paths agree.

## Data model

- `properties.aliases`: JSON array of strings, the same shape `tags` uses for
  its object IDs. No new column, no schema version bump, no migration: objects
  without the key behave exactly as before.
- `aliases` is a **default property**, not a per-type schema entry. It is
  available on every type, including custom ones, and is not listed in
  `GET /v1/types`.
- In-memory index: `ObjectMeta.Aliases []string` mirrors the stored list. It is
  rebuilt from the encrypted properties on startup and compared by `Fsck`, so
  the index cannot silently drift. Search still reads the decrypted object (it
  also needs content), so the index is metadata parity, not a search cache.
- Limits: at most 50 aliases per object, 200 characters each (counted in runes).
- Normalization on every write: trim, drop empty entries, deduplicate
  case-insensitively keeping the first spelling, remove the key when empty.

## How

- **Why a server-owned default property instead of `withTags`-style schema
  entries.** `tags` is a typed relation declared per type because not every type
  wants it. `aliases` is defined by Capacities as available everywhere, so
  repeating it in every default schema plus every custom type would be
  redundant and would still miss types created later. The server validates the
  reserved key in `validateObject` (the same place `summary`/`description` are
  reconciled), and type schemas cannot override its semantics.
- **Why dedupe instead of rejecting duplicates.** Mobile conflict merges union
  string lists (`mergeStringSlices`) before validation. Rejecting duplicates
  would make a legitimate merged object unsavable and could wedge a phone's
  outbox. Dedupe is idempotent, so retries and merges stay safe. The limits are
  still enforced with a 400 instead of truncating, because silently dropping a
  user-provided name is worse.
- **Why `[]any` internally.** Every list property decoded from JSON is `[]any`.
  Storing `[]string` would make `reflect.DeepEqual` report a difference on a
  no-op PATCH and bump the revision for nothing.
- **Why `matched_alias` rides on the object instead of a wrapper.** The endpoint
  historically returns `[]Object`; wrapping each entry would break every
  existing consumer. Embedding `Object` in `searchResult` keeps the flat shape
  and adds one optional field.
- **Why the title wins over an alias.** When the query already matches the
  title, the title is the better link text; reporting an alias would make
  clients insert a less canonical name.
- **Aliases are not globally unique.** Titles are unique per workspace; aliases
  are alternate names and two objects may share one. Search returns every
  match, which is the useful behavior for ambiguous shorthand.
- **Reserved key guards.** `validatePropertyValues`, `recomputePropertyLinks`
  and `removePropertyReference` skip `aliases`, so a custom type that happens to
  declare the key cannot reinterpret alias strings as object IDs or reject the
  normalized array.

Alternatives considered: a separate `aliases` table (overkill, breaks the
"properties are one encrypted document" invariant); a dedicated inverted alias
map in the index (unnecessary while search decrypts content anyway); a new
top-level `Object` field (would change the sync payload shape and every client
model).

## Contracts

Copied from `contracts/SYNC.md`:

> ## Aliases
>
> `aliases` is a default property available on every object, independent of its
> type schema. It lives inside the existing `properties` map as an array of
> strings, the same shape `tags` uses for its object IDs, so the store schema
> and the sync payload do not change:
>
> ```json
> {"properties": {"aliases": ["HP1", "The Stone"]}}
> ```
>
> - `POST /v1/objects` and `PATCH /v1/objects/{id}` accept
>   `properties.aliases`. `properties` remains a full replacement document:
>   omitting the key removes the aliases, and an empty array also removes the
>   key (objects without aliases have no `aliases` key).
> - `GET /v1/objects` and `GET /v1/objects/{id}` return the stored array.
> - The server trims each alias, drops empty entries and deduplicates
>   case-insensitively keeping the first spelling. Limits: 50 aliases per
>   object, 200 characters each; violations return 400 instead of truncating.
> - `GET /v1/search?q=` matches titles, content and aliases (case-insensitive
>   substring). A result that matched only through an alias carries
>   `"matched_alias": "<stored alias>"`; title or content matches omit the
>   field. The response is still a JSON array of objects, so the change is
>   additive. `GET /v1/objects?q=` uses the same matcher without
>   `matched_alias`.
> - Aliases are not globally unique (titles are): two objects may share one and
>   search returns both.
> - Sync carries aliases inside the object payload unchanged. Conflict merges
>   union list properties, so aliases are unioned and then normalized by the
>   server.

## Testing

`server/aliases_test.go` covers:

- storage, normalization and return through create, detail, list and the index;
- custom types accept aliases (the property is not tied to a type schema);
- replace, empty-array clear and omitted-key clear;
- validation: non-array, non-string entries, more than 50 aliases, more than 200
  characters, exact limits accepted, and the same rules on PATCH;
- search by alias reports `matched_alias`; title and content matches omit it;
  `type_id` filtering; `GET /v1/objects?q=` parity; no false positives;
- removing aliases removes the search hit; aliases survive a store restart and
  the rebuilt index matches;
- a mobile push conflict unions aliases and normalizes case-insensitive
  duplicates;
- every test runs `Fsck` on cleanup, which now also compares indexed aliases.

```sh
cd server
go test ./...
go test -race ./...
gofmt -l .
go vet ./...
```

Manual check against the disposable server (`sandbox/compose.test.yaml`, port
8879) — never the personal installation:

```sh
curl -s -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"type_id":"note","title":"HP1","properties":{"aliases":["Harry Potter"]}}' \
  http://127.0.0.1:8879/v1/objects
curl -s -H "Authorization: Bearer $TOKEN" 'http://127.0.0.1:8879/v1/search?q=harry'
```

## Client follow-ups

- **Desktop**: add an alias editor to the object panel (chips, add/remove,
  respect the 50-alias limit) and show aliases near the title; make palette and
  `@`/`[[` linking search use `matched_alias` as the inserted link text when
  present. The current save path already round-trips unknown properties, so
  aliases are not lost today.
- **Mobile**: keep passing `properties.aliases` through the object payload
  (already round-tripped), add an alias field to the editor, and use
  `matched_alias` in search results.
- **Extension**: no change; captures do not set aliases.
- **Fixtures**: unchanged; `contracts/fixtures/ids.json` only lists IDs.

## Follow-ups (not this stage)

- Unlinked mentions (matching aliases inside plain text) is a later client
  feature and is intentionally not implemented here.
- A server-side alias autocomplete endpoint (prefix match over the index) can
  reuse `ObjectMeta.Aliases` if the palette needs suggestions.
