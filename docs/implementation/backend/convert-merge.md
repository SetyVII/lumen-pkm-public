# Backend stage: type conversion and object merge

Status: implemented on `backend/convert-merge` (2026-10-31). Server-only stage
of the Capacities parity program; the client work is listed at the end.

## What & why

Capacities lets a user change an object's type without losing its content
(<https://docs.capacities.io/reference/content-types#type-conversions>): a
dialog compares the properties of both types and the user decides where each
value goes. It also lets a user merge one object into another of the same type
(<https://docs.capacities.io/reference/content-types#merge-two-objects>): the
kept object retains its own values, empty properties are filled, lists are
unioned, the removed object's content is appended, and every link that pointed
at the removed object points at the kept one.

Lumen could only change `type_id` through a raw `PATCH`, which silently left
property values that the new type does not understand, and had no merge at all.
This stage adds two explicit operations that never lose data:

- `POST /v1/objects/{id}/convert` changes the type and moves every property
  value it can. Values that cannot move are written into the content as text.
- `POST /v1/objects/{id}/merge` merges the object in the URL into a target of
  the same type, rewrites every reference, and moves the source to the trash.

Both are single SQLite transactions and both append the normal mobile sync
journal entries, so a phone that pulls afterwards sees the result without a new
protocol.

## API

All endpoints require the existing bearer token and return JSON.

### Convert

```http
POST /v1/objects/8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21/convert
```

```json
{
  "target_type_id": "book",
  "property_map": {"author_name": "author"},
  "base_revision": 7
}
```

```json
{
  "id": "8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21",
  "type_id": "book",
  "title": "The Left Hand of Darkness",
  "content": "Reading notes\n\nProperty reading_status: abandoned",
  "properties": {
    "author": "Ursula K. Le Guin",
    "rating": 5,
    "tags": ["..."],
    "aliases": ["LHoD"]
  },
  "links": ["..."],
  "property_links": ["..."],
  "created_at": "2026-10-20T09:00:00.000000000Z",
  "updated_at": "2026-10-31T12:00:00.000000000Z",
  "revision": 8,
  "incarnation": "5b0d...",
  "sync_status": "synced"
}
```

- The response is the stored object. `type_id` changed, the revision is one
  higher, and everything else keeps its identity.
- `base_revision` is optional. When present and different from the current
  revision, the endpoint returns
  `409 {"error":"conflict","conflict":true,"current":<object>}` exactly like
  `PATCH`.
- `400` for the same type, an unknown type, or an invalid `property_map`; a
  converted object that fails validation against the target type is also a
  `400` (a bug guard — values that cannot move go to the content instead).
  `404` for a missing or trashed object.

### Merge

```http
POST /v1/objects/8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21/merge
```

```json
{
  "target_id": "1d9e7b30-4a2c-4f18-8b77-0c5a2d9e6f41",
  "base_revision": 4,
  "target_base_revision": 9
}
```

The object in the URL is the **source** (the one the user wants to remove); it
is merged into `target_id` (the one the user wants to keep). The response is the
updated target:

```json
{
  "id": "1d9e7b30-4a2c-4f18-8b77-0c5a2d9e6f41",
  "type_id": "meeting",
  "title": "Weekly sync",
  "content": "Target body\n\nSource body",
  "properties": {
    "status": "open",
    "tags": ["...", "..."],
    "aliases": ["Weekly", "Sync"]
  },
  "links": ["..."],
  "property_links": ["..."],
  "created_at": "2026-10-01T09:00:00.000000000Z",
  "updated_at": "2026-10-31T12:00:00.000000000Z",
  "revision": 10,
  "incarnation": "9a3c...",
  "sync_status": "synced"
}
```

- `base_revision` guards the source (the path object, consistent with `PATCH`
  on the same URL); `target_base_revision` optionally guards the target. Both
  are optional and both return the PATCH `409` shape with the current object.
- `400` when the source is trashed, the target is missing or trashed, source
  and target are the same object, or the types differ. `404` when the source
  does not exist.
- The source is not in the response: it is in the trash
  (`GET /v1/trash`) and can be restored, although the restore is a degraded
  duplicate (see "Trash interplay").

## Data model

No new fields, record kinds, columns or migrations; `schemaVersion` stays at 3.

- **Convert** is a normal object write. It changes `type_id`, replaces
  `properties`, recomputes `property_links` for the new type, bumps `revision`
  and `updated_at`, and writes one `object` record. The in-memory index's
  `byType` map is updated by the existing `applyIndex` path, so listings by type
  move the object immediately. `links`, `created_at` and `incarnation` are
  untouched.
- **Merge** writes the target object, every rewritten referrer, the reassigned
  asset records, the source tombstone and the source trash payload in one
  `PutBatch`. The source's live row is deleted. Nothing about the object format
  changes.
- The trash record is the existing `trashedObject` (`kind=trash`, full object +
  `deleted_at`), so a merged source is restorable with the existing endpoint.

## How

### Convert: the property compatibility table

A source value moves when the target property has the same kind and accepts the
value. The check mirrors `validatePropertyValues`, so a converted object can
always be saved again through `PATCH`.

| Source kind | Target kind | Moves when |
|---|---|---|
| `text` | `text` | always |
| `number` | `number` | finite and within the target `min`/`max` |
| `date` | `date` | a valid `YYYY-MM-DD` string |
| `boolean` | `boolean` | always |
| `select` | `select` | the target has no options, or includes the value |
| `object` | `object` | every id resolves (live or trashed) and satisfies the target `target_type`; a list needs `multiple: true` or exactly one id |
| untyped (legacy bare name) | any | the value's JSON shape matches the target kind |
| any | untyped (legacy bare name) | always; the store does not validate untyped properties |
| any other pair | any other pair | never: the value goes to the content |

Cross-kind moves (`number`→`text`, `select`→`text`, ...) are deliberately not
automatic. A client that wants one can map the value explicitly, but the
mapping is still checked against the table and falls back to the content when
the kinds differ. This keeps the result predictable and avoids inventing
formatting rules for every pair.

Cardinality is normalized: a single id stored as a string becomes a one-element
array for a `multiple` target, and a one-element list becomes a string for a
single-value target. A list with more than one id cannot move into a
single-value target and goes to the content.

`aliases` is a default property on every type (not part of any type schema), so
it always travels with the object. `tags` is a normal per-type relation: it
survives when the target type declares a compatible `tags` (the default types
all use `object`→`tag`), and otherwise the tag names are written into the
content. The alternative — keeping an undeclared `tags` key — would store data
that the target type does not render and that the index does not treat as a
relation, which is a silent loss in practice.

### Convert: mapping algorithm and no-data-loss rules

1. Validate `property_map`: both keys must be non-empty; `aliases` cannot be
   remapped; a source key must be declared by the source type or already exist
   on the object; a target key must be declared by the target type.
2. Apply explicit mappings first, sorted by source key for determinism. A
   mapping that fits fills the target and reserves it. A mapping that does not
   fit, or whose target was already filled by an earlier mapping, is a
   leftover.
3. Walk the object's remaining property keys in sorted order. `aliases` is kept.
   A key declared by the target moves when it fits and the target is not
   reserved; otherwise it is a leftover. A key declared only by the source is a
   leftover. A key declared by neither type is hidden metadata and is carried
   over untouched.
4. Serialize every leftover as one line, `Property <name>: <value>`, and append
   the lines to the content after a blank line (or as the whole content when it
   was empty). Object relations are rendered by title, falling back to the id
   when the target no longer resolves; lists are joined with `, `; numbers use
   their shortest decimal form; maps use compact JSON.
5. If anything was appended, drop `properties.blocks` and
   `properties.rich_text`: they describe the old body, and keeping them would
   let a client render a view that hides the migrated values. When nothing is
   appended, the rich formats are untouched.
6. Validate the converted object against the target type and save it.

Hidden keys (`summary`, `assets`, `image_asset_id`, `asset_id`,
`source_object_id`, `mobile_conflict`, `og_*`, ...) are not typed properties of
either schema. They are carried over unchanged because dropping them would lose
data and serializing them into the content would fill it with internal
metadata. They stay invisible in the target type's property panel, exactly as
they were before the conversion.

### Merge: property and content rules

- The target keeps its own title, creation time, incarnation and non-empty
  property values. A property is empty when it is missing, `null`, an empty or
  whitespace-only string, or an empty list/map.
- Empty target properties take the source value.
- Declared multiple relations (`tags`, any `multiple: true` object property)
  and `aliases` are unioned with `mergeStringSlices` (the helper the aliases
  stage left for this purpose). The union normalizes a single id string to a
  list first, deduplicates and keeps the target's order.
- A property with a single value stays as it is, even when the source has a
  different value; the source's value remains recoverable in the trash.
- The source body is appended to the target body after a blank line. If the
  target body is empty, the source body becomes the body.
- `properties.blocks`: when both objects carry a non-empty block array, the
  source blocks are appended and their ids are preserved. A source block id that
  already exists in the target (only possible after a duplicated object or a
  client bug) gets a fresh id, because two blocks sharing an id would make
  block-level edits ambiguous. When only one side has blocks, both `blocks` and
  `rich_text` are dropped: a partial rich representation would hide the
  appended text from a client that prefers blocks. `rich_text` is always
  dropped when the body or the blocks change; `content` is canonical.
- Attachments: assets owned by the source (`Asset.ObjectID == source.id`) are
  reassigned to the target and their `properties.assets` metadata is unioned.
  An asset that *is* the source media object (`MediaObjectID == source.id`,
  merging two images or PDFs) stays with the source, so purging the source
  cannot delete the target's data. A cover (`image_asset_id`) or `asset_id` is
  filled from the source only when the referenced asset moved.

### Merge: link-rewrite mechanics

Relations are stored twice: explicitly in `links`, and derived in
`property_links` from declared object properties. The index keeps a reverse
`backlinkSets` map built from both.

1. The target adopts the source's explicit links (`mergeLinks`), rewrites its
   own references to the source, and drops the resulting self-references.
2. Every live object in `BacklinkIDs(source.id)` is loaded. Its `links` and its
   declared object properties are rewritten from the source id to the target
   id, deduplicated, and self-references are dropped. `source_object_id` is
   rewritten too because it is a provenance relation. Hidden keys that are not
   declared relations are not rewritten; the index does not track them, and
   guessing which strings are ids would corrupt unrelated text.
3. Each rewritten object gets `updated_at = now()`, a revision bump through
   `prepareSave`, and therefore an `object` put in the sync journal. The target
   is validated last, then every referrer, so the transaction is rejected as a
   whole if any rewritten relation is invalid. Same-type enforcement is what
   makes this safe: a referrer whose typed property accepted the source also
   accepts the target, because the target has the source's type.
4. The source's live row is deleted and its trash payload is written. The
   index removes the source's outgoing relations and, because every referrer
   was rewritten, its incoming backlink set becomes empty. `Fsck` verifies both
   directions.

   The target and the source cannot be the same object, so a merge never rewrites
   the source into itself.

Trashed objects are frozen snapshots. A reference held by a trashed object is
not rewritten: reading and rewriting every trash payload would break the
"trash is the pre-deletion object" invariant, and the source is restorable, so
restoring both later leaves a consistent reference. Only live referrers are
rewritten.

### Merge: trash interplay

The source goes through the existing `trashObjectData` path, not a hard delete:

- Tombstone `revision = source.revision + 1`, `deleted_at` set.
- Trash payload with the full pre-merge source.
- `object` delete and `tombstone` put in the sync journal; the trash record is
  not a sync kind.

Restoring the source brings back its own content and properties, with a fresh
revision above the tombstone and the original incarnation. It is a degraded
duplicate: references that used to point at it now point at the target, so the
restored source has no backlinks, and any attachment whose ownership moved to
the target is listed in the restored `assets` metadata but owned by the target.
That is acceptable for an undo-style safety net and is documented rather than
hidden; a future "unmerge" would need a link-rewrite log, which this stage does
not keep.

Merging an already trashed source is rejected with `400` instead of silently
merging stale data. A purged source is a `404` (its tombstone no longer has a
payload).

### Why one transaction and why the target is returned

A merge touches the target, an unbounded number of referrers, assets and the
trash. Committing them separately would leave a window where some referrers
point at a trashed object or where the source is gone but the target does not
have its content. `PutBatch` already commits encrypted records, journal entries
and deletes atomically, so the whole merge is one call under `writeMu`.

The response is the target because it is the object that survives and the one
the client must refresh. The source is observable through `GET /v1/trash` and
the sync journal; returning both would duplicate the trash response shape.

### Why `base_revision` guards the source and `target_base_revision` the target

`POST /v1/objects/{id}/merge` has two mutable objects, so one CAS field cannot
protect both. The path object is the source, and `PATCH /v1/objects/{id}` uses
`base_revision` for the path object; keeping that meaning avoids a surprising
difference between two endpoints on the same URL. `target_base_revision` is the
additive second guard for clients that also hold the target's revision from the
picker. Both are optional, so older clients keep working.

## Contracts

The following section was added to `contracts/SYNC.md`:

````markdown
## Convert and merge

Implemented 2026-10-31 (backend stage `convert-merge`). Two object lifecycle
operations from Capacities
(<https://docs.capacities.io/reference/content-types#type-conversions>,
<https://docs.capacities.io/reference/content-types#merge-two-objects>). Neither
changes the stored object format: both are normal object writes, so the sync
payload, the in-memory index and the mobile journal keep their existing shapes.

### Convert

`POST /v1/objects/{id}/convert`

```json
{"target_type_id": "book", "property_map": {"author_name": "author"}, "base_revision": 7}
```

- The object keeps its id, title, content, links, creation time and
  incarnation. `type_id` and `properties` change, the revision is bumped, and
  one `object` put reaches the sync journal.
- `base_revision` is optional and follows the PATCH CAS contract: a mismatch
  returns `409 {"error":"conflict","conflict":true,"current":<object>}`.
- Converting to the type the object already has, or to an unknown type, is
  `400`. A missing or trashed object is `404`.
- `property_map` moves a source property to a differently named target
  property. Both keys must be declared by the corresponding type (a source key
  may also be an existing hidden property). `aliases` cannot be remapped.
  Explicit mappings win over the automatic same-name rule; a second source
  mapped onto an already filled target is written into the content.
- Compatibility: `text`→`text`, `number`→`number`, `date`→`date`,
  `boolean`→`boolean`, `select`→`select` (when the target options admit the
  value), `object`→`object` (when every id resolves and satisfies the target
  `target_type`; a list needs a multiple target or exactly one id). Legacy
  untyped properties accept any value; a typed target accepts a value whose
  JSON shape matches. Cross-kind moves never happen automatically.
- Unmapped source properties whose name and kind are compatible with a target
  property stay automatically.
- Every source property value that cannot move (no compatible target,
  incompatible kind, blocked mapping, target already filled) is appended to the
  content as one line per property, `Property <name>: <value>`, after a blank
  line. Object relations are rendered by title (falling back to the id); lists
  are joined with `, `. When anything is appended, `properties.blocks` and
  `properties.rich_text` are dropped so no client renders a stale body.
- `aliases` is a default property and always travels with the object. `tags`
  only survives as a relation when the target type declares a compatible
  `tags`; otherwise the tag names are written into the content.
- Hidden keys declared by neither type (`summary`, `assets`, `image_asset_id`,
  `rich_text`, `mobile_conflict`, ...) are carried over untouched.
- `links` and the object's inbound backlinks are unchanged. Property links are
  recomputed for the new type, and the index moves the object to the new type.

### Merge

`POST /v1/objects/{id}/merge`

```json
{"target_id": "8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21", "base_revision": 4, "target_base_revision": 9}
```

- The object in the URL is the **source**: it is merged into `target_id` and
  moved to the trash. The response is the updated target.
- Both objects must be live and of the same type (`400` otherwise). Source and
  target must differ. Merging an already trashed source is `400`; an unknown
  source is `404`; an unknown target is `400`.
- `base_revision` guards the source (the path object, like PATCH);
  `target_base_revision` optionally guards the target. A mismatch returns the
  PATCH `409` shape with the current object.
- Target property values win. Empty target properties (missing, null, empty
  string, empty list) take the source value. Declared multiple relations
  (`tags`, any `multiple: true` object property) and `aliases` are unioned with
  `mergeStringSlices`; a single-value property stays as it is.
- The source body is appended to the target body after a blank line. When both
  objects carry `properties.blocks`, the source blocks are appended and their
  ids are preserved (a colliding source id gets a fresh id); `rich_text` is
  dropped. When only one side has blocks, both `blocks` and `rich_text` are
  dropped so the plain-text `content` stays canonical.
- Attachments owned by the source are reassigned to the target and their
  metadata is unioned. An asset that is the source media object itself (merging
  two images or PDFs) stays with the source.
- The target adopts the source's explicit links. Every live object whose
  `links` or declared object properties point at the source is rewritten to the
  target in the same transaction, with a revision bump and an `object` put in
  the sync journal. `source_object_id` is rewritten too. Hidden keys that are
  not declared relations are not rewritten. Trashed objects are frozen
  snapshots: a reference they hold to the source is not rewritten, and
  restoring both later keeps that reference (the source is restorable).
- The source is trashed exactly like `DELETE /v1/objects/{id}`: tombstone
  (`revision = source.revision + 1`), trash payload, `object` delete and
  `tombstone` put in the journal. It stays restorable; restoring it brings back
  its own content and properties as a duplicate, while the links now point at
  the target (so it has no backlinks).
- The whole merge is one SQLite transaction: target, referrers, assets,
  tombstone and trash commit together.
````

## Testing

`server/convert_test.go` covers:

- happy path: a note-like custom type converted to a compatible type keeps the
  id, title, content, creation time and aliases, moves every typed property,
  recomputes `property_links`, keeps both explicit and property backlinks,
  moves the object between type listings, and delivers one `object` put with
  the new type through `POST /v1/sync/pull`.
- no-data-loss: a number into a text property, a select option the target does
  not have, and a tag list into a type without `tags` are appended to the
  content as `Property ...` lines; the old values are gone from `properties`,
  `blocks`/`rich_text` are dropped, and the tag backlink disappears because the
  relation became text.
- explicit `property_map`: a differently named source property fills the
  target, an unmapped same-name property stays automatically, and a second
  source mapped onto the same target is written into the content.
- guards: same type, unknown type, missing type id, invalid source/target
  mapping, `aliases` remapping, missing and trashed objects, CAS `409` with the
  current object, and a matching `base_revision`.
- rich formats survive when nothing is appended.
- relation cardinality normalization in both directions.

`server/merge_test.go` covers:

- happy path: target values win, empty target properties are filled, `tags` and
  `aliases` are unioned and normalized, the body is appended, the source is in
  the trash and restorable, and the sync pull delivers the target plus the
  source tombstone and deletion.
- link rewrite: an explicit journal link, two typed property relations, and the
  target's own reference to the source are all rewritten; every referrer gets a
  revision bump and an `object` put; the target's backlinks contain every
  referrer; no live object still points at the source; the source's tag
  relation moves to the target.
- guards: same object, different type, missing target id, missing target,
  trashed target, missing source, source CAS, target CAS, and the double-merge
  rejection.
- blocks: appended with stable ids, colliding source ids re-identified,
  `rich_text` dropped; an empty source body leaves the target body and rich
  formats untouched.
- attachments: ownership and metadata move to the target, and purging the
  source does not delete the moved attachment.
- media objects: merging two images keeps the target's asset and leaves the
  source's blob in the trash, where purging it deletes only the source blob.

Every test runs `Fsck` on cleanup, which also verifies the index backlinks
after the rewrites.

Commands:

```sh
cd server
go test ./...
go test -race ./...
go vet ./...
gofmt -l .
```

Manual verification (disposable sandbox, port 8879 — never the personal
installation):

```sh
docker compose -f sandbox/compose.test.yaml up -d --build
# create two notes, POST .../merge, check GET /v1/trash and the target;
# create a note, POST .../convert with a property_map, read the content
docker compose -f sandbox/compose.test.yaml down
```

## Client follow-ups

- **Desktop**: add a "Change type" dialog that loads both type schemas and lets
  the user map source properties to target properties, showing which values
  will be written into the content; after conversion, refresh the object, the
  type listings, search and both backlink directions. Add a "Merge into
  object" action with a target picker (same type only), a confirmation that
  names the object that will be removed, and a refresh of the target, its
  backlinks and the trash. Render `Property <name>: <value>` lines as ordinary
  paragraphs; there is no dedicated block type.
- **Mobile**: no new UI is required to stay consistent. A pull delivers the
  converted object with a new `type_id` and the merged target with a bumped
  revision; the source arrives as a tombstone, so the existing `applyPull`
  removes it. A later mobile editor must keep sending `content` as the
  canonical body (the merge cannot move blocks that exist only in
  `properties.blocks`).
- **Extension**: no change; captures do not convert or merge.
- **Fixtures**: unchanged; no format changed.

## Follow-ups (not this stage)

- Converting to a type without a `tags` relation writes the tag names into the
  content instead of preserving the relation. A future "add the property to the
  target type" flow could keep the relation and ask the user to confirm the
  schema change.
- Merging an object whose body exists only in `properties.blocks` (empty
  `content`) does not move those blocks into the target; `content` is the
  canonical body in this store. The markdown-converter stage can later rebuild
  blocks from content.
- Hidden keys that are not declared relations are not rewritten on merge. A
  future metadata schema could register them as relations.
- Merging journals is allowed but odd: the source date's journal goes to the
  trash and `GET /v1/journal/{date}` restores it, creating a duplicate. A
  journal-aware guard can be added if the desktop ever exposes merge on a
  journal.
- Capacities warns when an object has too many references. This stage always
  merges; a future cap can use `len(BacklinkIDs(source.id))` to return a
  `400` with a clear message.
- Restoring a merged source duplicates its links (it has no backlinks, and its
  `assets` metadata may name attachments now owned by the target). This is
  documented as acceptable; a true unmerge would need a rewrite log.
- `summary` is server-managed and not part of any type schema, so it is carried
  over as hidden metadata instead of being mapped to a target property.
