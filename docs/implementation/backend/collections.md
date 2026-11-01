# Collections (backend stage 5)

## What & why

Capacities collections are hand-curated, ordered groups **inside one object
type**: you add each item yourself and remove it when it no longer belongs.
They are deliberately not tags (which cross types), labels (fixed categories) or
queries (rule-based and self-updating), and Capacities recommends against
nesting them. See:

- <https://docs.capacities.io/reference/collections>
- <https://docs.capacities.io/reference/organizational-structures>

Lumen already has typed objects, object relations and backlinks, so a collection
is modelled as an object of a new default type `collection` (singular
"Collection", plural "Collections") with `item_type_id` (the one type it holds)
and `items` (an ordered, multiple object relation). No second storage engine or
parallel relation system is introduced: collections inherit encryption, the
mobile sync journal, backlinks and transactional deletion cleanup from the
existing object store.

## API

All routes require the existing bearer token. Collections are created, renamed
and deleted through the generic object API; list access and mutation use the
helpers below.

### Create a collection

```http
POST /v1/objects
Content-Type: application/json

{
  "type_id": "collection",
  "title": "Holiday books",
  "properties": {
    "item_type_id": "book",
    "items": ["<book-id-1>", "<book-id-2>"],
    "summary": "Curated by hand"
  }
}
```

`201 Created` returns the object with `property_links` mirroring the items and
`items` normalized to an array. `items` may be omitted or empty. Validation
rejects a missing/unknown `item_type_id`, `item_type_id: "collection"`, items of
another type, nonexistent items, trashed items and duplicates (`400`).

### Rename or describe

```http
PATCH /v1/objects/<collection-id>
Content-Type: application/json

{"title": "Holiday reading", "properties": {"item_type_id": "book", "items": ["<book-id-1>", "<book-id-2>"], "summary": "Shortlist"}}
```

`properties` is the existing explicit-replacement document, so a client that
edits `items` through `PATCH` must resend `item_type_id` (and any other property
it wants to keep). Changing `item_type_id` returns `400`.

### Append or insert an item

```http
POST /v1/collections/<collection-id>/items
Content-Type: application/json

{"object_id": "<book-id-3>", "position": 1}
```

`position` is an optional 0-based index; omitted or equal to the current length
appends. `200 OK` returns the updated collection. Errors: `400` wrong type,
nonexistent or trashed item, invalid position; `404` collection not found; `409`
item already in the collection.

### Reorder the whole list

```http
PUT /v1/collections/<collection-id>/items
Content-Type: application/json

{"items": ["<book-id-3>", "<book-id-1>", "<book-id-2>"]}
```

`200 OK` returns the updated collection. `items` is required; `[]` clears the
list. An identical list is a no-op and does not bump `revision`.

### Remove an item

```http
DELETE /v1/collections/<collection-id>/items/<book-id-1>
```

`200 OK` returns the updated collection with the remaining order preserved.
`404` means the object is not in the collection; a retrying client may treat it
as already removed.

### Read the list in order

```http
GET /v1/collections/<collection-id>/items
```

```json
{
  "collection_id": "<collection-id>",
  "item_type_id": "book",
  "item_ids": ["<book-id-3>", "<book-id-1>"],
  "items": [
    {"id": "<book-id-3>", "type_id": "book", "title": "Third", "...": "..."},
    {"id": "<book-id-1>", "type_id": "book", "title": "First", "...": "..."}
  ]
}
```

`item_ids` is the stored order. `items` skips IDs that no longer resolve or
are trashed as defense in depth; deletion cleanup should make that impossible.

### Which collections contain an object?

Items are property links, so backlinks already flow. The backlinks endpoint
gained an optional `type_id` filter:

```http
GET /v1/objects/<book-id>/backlinks?type_id=collection
```

The response is the list of collection objects containing that book. Without the
filter, backlinks keep returning every relation (journals, notes, other
properties), so the change is backward compatible.

## Data model

- New default type record `collection` (`Name: "Collection"`, icon
  `view-list-details`, color `#8f7fb5`), seeded by `NewApp` on startup. Because
  types are records, an existing workspace gets it on the next start; there is
  **no SQL migration** and no change to `schemaVersion`.
- Properties:
  - `item_type_id`: `{"type":"text"}` — validated against `GET /v1/types` and
    immutable after creation.
  - `items`: `{"type":"object","multiple":true}` — ordered array of object IDs.
  - `tags`: inherited from the shared content-type convention.
- No new SQLite tables, columns or in-memory index structures. `items` produces
  the same `property_links` as tags or any object property; the reverse index
  already answers `GET /v1/objects/{id}/backlinks`.
- Order lives in the encrypted object payload (`properties.items` array order).
  `property_links` is derived and deduplicated, so it is a set, not the
  ordering source.
- Every item mutation is a normal object save: `revision` increments,
  `updated_at` changes and one `object` row is appended to `sync_changes`.
- No fixture change was needed: `contracts/fixtures/ids.json` keeps its stable
  IDs untouched, and the type is seeded automatically rather than loaded from a
  fixture.

## How & why

**Type plus properties instead of a `collection_items` table.** A side table
would need its own encryption, sync journal, deletion cleanup and index
rebuild, and would duplicate the relation machinery that already exists. Using
the object store means mobile sync, `Fsck`, backups and the encrypted payload
format work unchanged. The trade-off is that a collection is also a normal
object (it has a title, content and tags), which matches Capacities: a
collection has its own page.

**Ordering decision.** The `items` array order is authoritative. There is no
per-item `position` field: inserting or moving rewrites the array, which is
atomic in one object save and cannot drift out of sync with the list. The
`POST .../items` helper inserts at a 0-based index; `PUT .../items` replaces the
whole list for drag-and-drop. `property_links` is derived from the array and
deduplicated, so it must not be used as the ordering source. Order survives
`GET`, `PATCH` and restart (the encrypted payload round-trip is covered by a
test); sync pull carries the same property payload unchanged.

**Duplicates rejected.** A curated list is an ordered set. Allowing the same
object twice would make drag-and-drop ambiguous, make `DELETE .../items/{id}`
unable to name one occurrence, and diverge from the deduplicated
`property_links`. `POST` returns `409` for an existing member; create and `PUT`
return `400` for a list containing duplicates.

**Deleting an item cleans every collection.** `DELETE /v1/objects/{id}` already
walks backlinks and calls `removePropertyReference`, which rewrites object
properties and preserves array order. Collections ride on that path, so the
collection is updated in the same SQLite transaction as the tombstone and the
mobile journal sees the collection `put`. The choice is cleanup-on-delete rather
than skip-on-read: clients never observe a collection pointing at a dead object,
`Fsck` stays clean and the stored property remains truthful. `GET .../items`
still skips a missing or trashed ID as defense in depth for data written by an older
or interrupted version; it never turns a read into a `500`.

**Immutability is enforced centrally.** `validateCollection` runs from
`validateObject` on create, PATCH, capture and sync push. For an existing
collection it reads the stored object and rejects a different `item_type_id`.
This covers the generic `PATCH` and the mobile sync path without duplicating the
rule in each handler. Changing an object's `type_id` away from `collection` (or
into it) remains the generic type-change behavior; full type conversion with
property mapping is stage 9 and is out of scope here.

**The collection type's structural schema is protected.** `PATCH
/v1/types/collection` can rename labels, change the icon/color or drop `tags`,
but `validateCollectionTypeSchema` rejects removing `item_type_id`/`items` or
changing their kinds. Without this guard, a schema edit would silently drop item
backlinks and make deletion cleanup stop rewriting `items`, leaving stale IDs in
every collection.

**No nested collections.** `item_type_id: "collection"` is rejected. Capacities
explicitly recommends queries or a new object type instead of nested collection
trees, and nesting would allow self-references and cycles with no product
benefit.

**Trash compatibility.** The trash stage is a parallel wave-1 branch. Item
validation checks both existence and the tombstone record, so it rejects a
trashed item whether trash keeps the object row or removes it and keeps only the
tombstone. There is no server-side "pending" object state to consider: every
write is committed with `sync_status: "synced"` before the HTTP response, so
existence plus trash is the complete eligibility rule. No dependency on the
trash branch is introduced.

**Validation cost.** Every collection write re-reads every item (one decrypted
read per item) to keep create, PATCH, item helpers and sync push on one code
path. Curated lists are small; the simplicity is worth the linear cost. A future
optimization could validate only changed IDs, but it would need a second,
easier-to-bypass path.

**Concurrency.** Item helpers take `writeMu` and reload the collection inside
the lock, so concurrent appends/removals do not lose updates. `PUT` is
last-write-wins for the whole list; a client that needs compare-and-swap can use
`PATCH /v1/objects/{id}` with `base_revision`. Helpers do not accept
`base_revision` in this stage.

**Backlinks filter instead of a new list filter.** `?type_id=` on the existing
backlinks endpoint answers "collections containing X" without touching
`GET /v1/objects`, which stage 7 (`query-filters`) will extend with its own
`collection` filter. The two features compose: stage 7 can reuse
`properties.items`/`property_links` without a schema change.

## Contracts

The following section was added to `contracts/SYNC.md`.

> ## Collections
>
> `collection` is a default object type: a curated, ordered list of objects of
> exactly one type (Capacities: <https://docs.capacities.io/reference/collections>).
> Collections are ordinary objects, so they are created, renamed, read and deleted
> with the generic object API. The type adds two properties:
>
> - `item_type_id` (`text`): ID of the single type whose objects the collection
>   holds. It must reference an existing type, cannot be `collection` (no nested
>   collections) and is immutable after creation; `PATCH` and mobile sync reject a
>   different value.
> - `items` (`object` relation, multiple): ordered array of object IDs. The array
>   order is authoritative; `property_links` and backlinks expose the same IDs as
>   a derived set, not as a second ordering.
> - `tags` is inherited like every other content type.
>
> The built-in `collection` type keeps these two structural properties:
> `PATCH /v1/types/collection` may rename labels, icon, color or the tags
> property, but removing `item_type_id`/`items` or changing their kinds returns
> `400`.
>
> Invariants enforced on create, `PATCH`, capture and sync push:
>
> - every item exists, is not trashed and has `type_id == item_type_id`;
> - an object appears at most once per collection;
> - a collection cannot contain itself or another collection;
> - `items` is always stored as a JSON array (`[]` when empty).
>
> Deleting an object removes it from every collection in the same SQLite
> transaction (the existing backlink cleanup rewrites `items`) while preserving
> the order of the remaining items. Reads skip an ID that no longer resolves or
> is trashed as defense in depth, but the stored property is authoritative.
>
> ### Endpoints
>
> All routes require the bearer token and are additive to the existing object API.
> Create with `POST /v1/objects` (`type_id: "collection"`, `properties.item_type_id`,
> optional `properties.items`); rename or describe with `PATCH /v1/objects/{id}`.
>
> `POST /v1/collections/{id}/items` with `{"object_id":"<id>","position":0}`.
> `object_id` is required. `position` is an optional 0-based index; omitted or
> equal to the current length appends. Returns `200` with the updated collection
> object. Errors: `400` for a wrong-type, nonexistent or trashed item, an invalid
> position or an invalid body; `404` when the collection does not exist; `409`
> when the item is already in the collection.
>
> `PUT /v1/collections/{id}/items` with `{"items":["<id-1>","<id-2>"]}` replaces
> the full ordered list; this is the reorder primitive for drag-and-drop. `items`
> is required and `[]` clears the collection. The same validation as creation
> applies. Returns `200` with the updated collection; an identical list is a no-op
> and does not bump `revision`.
>
> `DELETE /v1/collections/{id}/items/{object_id}` removes one item and preserves
> the order of the rest. Returns `200` with the updated collection; `404` when the
> object is not in the collection (clients may treat that as already removed).
>
> `GET /v1/collections/{id}/items` returns
> `{"collection_id":"<id>","item_type_id":"book","item_ids":["<id-1>","<id-2>"],"items":[{...},{...}]}`.
> `item_ids` is the stored order; `items` skips IDs that no longer resolve or are trashed.
>
> ### Listing collections that contain an object
>
> Items are property links, so backlinks already flow. Use the optional `type_id`
> filter: `GET /v1/objects/<object-id>/backlinks?type_id=collection`. The response
> is the list of collection objects containing that object. Without the filter the
> endpoint keeps returning every backlink. To list the candidate objects of one
> type use `GET /v1/objects?type_id=<item_type_id>`; to read a collection's
> contents in order use `GET /v1/collections/{id}/items`.
>
> ### Sync
>
> Collections are ordinary objects in `sync_changes`; item changes are object
> `put`s. `properties.items` is an ordered list of IDs, so a mobile conflict merge
> unions the lists as ordered sets (server order first, incoming-only IDs appended)
> and deduplicates. `item_type_id` is immutable on the server: a push that changes
> it is rejected with an operation error. Deleting an item on any client goes
> through the normal delete path and cleans every collection.

## Testing

`server/collections_test.go` covers:

- `TestCollectionTypeCreationAndValidation`: `GET /v1/types` exposes the type and
  its two properties; create with items; title/summary PATCH; immutable
  `item_type_id`; no-op PATCH keeps `revision`; rejection of missing/unknown
  item type, nested collections, wrong-type, nonexistent, duplicate and
  non-string items; empty collection; `GET /v1/objects?type_id=collection`.
- `TestCollectionTypeSchemaIsProtected`: removing `item_type_id`/`items` or
  changing their kinds returns `400`; renaming the type still works.
- `TestCollectionItemEndpoints`: append, insert at 0/1/end, duplicate `409`,
  wrong type, missing object, out-of-range positions, resolved `GET .../items`
  order, `PUT` reorder, no-op `PUT`, invalid replacements, `DELETE` preserving
  order, absent-item `404`, PATCH reorder, non-collection `400`, missing
  collection `404`.
- `TestCollectionObjectDeletionCleansItems`: deleting a book removes it from two
  collections in one transaction, preserves the remaining order, refreshes
  `property_links`, and `?type_id=collection` backlinks stop advertising the
  deleted object.
- `TestCollectionSyncPushCannotChangeItemType`: a mobile `put` that changes
  `item_type_id` is rejected with an operation error and leaves the stored
  collection untouched.
- `TestCollectionOrderSurvivesRestart`: closing and reopening the encrypted
  store keeps both the stored array order and the resolved order.

The shared `makeApp` cleanup runs `Store.Fsck` after every test, so the index,
backlinks and property links are verified against disk.

Commands (run from `server/`):

```sh
go test ./...
go test -race ./...
go vet ./...
gofmt -l .
```

Manual verification against the disposable sandbox (port 8879, never the
personal install):

```sh
curl -s -H "Authorization: Bearer $TOKEN" http://127.0.0.1:8879/v1/types | grep collection
```

## Client follow-ups

- **Desktop**: add the `collection` type to the type sidebar; render a collection
  page with the title, `summary` and an ordered list resolved through
  `GET /v1/collections/{id}/items`; drag from list/gallery to
  `POST .../items`; drag to reorder and persist with `PUT .../items`; render
  `item_type_id` as a type picker on creation and read-only afterwards; show
  "collections containing this object" from
  `GET /v1/objects/{id}/backlinks?type_id=collection`.
- **Mobile**: collections arrive as normal objects through sync pull. Resolve
  `properties.items` against the local store in array order; do not reorder by
  `updated_at`. Editing a collection through sync push must keep `item_type_id`
  unchanged or the server rejects the operation. Treat `404` from the item
  delete helper as already removed.
- **Extension**: optional "add to collection" action using
  `POST /v1/collections/{id}/items` after loading collections with
  `GET /v1/objects?type_id=collection`.
- **API v2 / MCP (later stages)**: `list_by_collection` can resolve
  `properties.items` and reuse `GET .../items`; no new storage is needed.
