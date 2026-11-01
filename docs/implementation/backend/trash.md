# Backend stage: trash (soft delete, restore, purge)

Status: implemented on `backend/trash` (2026-10-31). Server-only stage of the
Capacities parity program; clients still need the UI work listed at the end.

## What & why

Capacities treats deletion as a safety net: an object leaves the workspace but
stays recoverable in **Trash** for a limited time (30 days in the reference
product), with per-item restore, permanent delete and "empty trash"
(<https://docs.capacities.io/reference/trash>). Lumen already had tombstones and
anti-resurrection, but `DELETE /v1/objects/{id}` destroyed the payload, so a
mis-click was unrecoverable.

This stage turns the existing hard delete into a two-step lifecycle:

1. `DELETE /v1/objects/{id}` moves the object to the trash (soft delete).
2. `DELETE /v1/trash/{id}` (or `POST /v1/trash/empty`) performs the old hard
   delete permanently.

The tombstone and its anti-resurrection guarantees are unchanged. Retention is
not automated in this stage, but `deleted_at` is stored so a future janitor can
add Capacities-style expiry.

## API

All endpoints require the existing bearer token and return JSON.

### Trash an object (changed)

```http
DELETE /v1/objects/8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21
```

```json
{
  "deleted": true,
  "trashed": true,
  "id": "8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21",
  "deleted_at": "2026-10-31T12:00:00.000000000Z",
  "sync_status": "synced"
}
```

`deleted` and `sync_status` are kept for older clients. `trashed` and
`deleted_at` are additive. `404` when the object is not live (already trashed or
purged).

### List the trash

```http
GET /v1/trash?q=meeting&type_id=note
```

```json
[
  {
    "id": "8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21",
    "type_id": "note",
    "title": "Meeting notes",
    "deleted_at": "2026-10-31T12:00:00.000000000Z"
  }
]
```

- Metadata only: content and properties are never returned by the list.
- `q` matches title or content, case-insensitively; `type_id` is exact.
- Ordered by `deleted_at` descending, ties by id.
- The response is always an array (`[]` when empty).

### Restore

```http
POST /v1/objects/8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21/restore
```

```json
{
  "id": "8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21",
  "type_id": "note",
  "title": "Meeting notes",
  "content": "Recovered body",
  "properties": {"tags": ["..."]},
  "links": ["..."],
  "property_links": ["..."],
  "created_at": "2026-10-20T09:00:00.000000000Z",
  "updated_at": "2026-10-31T12:05:00.000000000Z",
  "revision": 9,
  "incarnation": "5b0d...",
  "sync_status": "synced"
}
```

- `404` when the id is not in the trash. A purged id can never be restored.
- The restored revision is `max(payload.revision, tombstone.revision) + 1`, so
  it is always newer than the deletion. The original `incarnation` is kept:
  restore brings back the same object, it does not recreate it.
- If a live object took the title while this one was in the trash, the restored
  title gets a numeric suffix (`Meeting notes (2)`). The response shows the
  final title.
- References to objects that were permanently purged while this one was in the
  trash are dropped, as are attachment references whose blob no longer exists.
  References to other trashed objects are kept (they can be restored later).
- Restoring is lenient about property values: a schema edit during the stay in
  the trash does not make the object impossible to restore. The stored values
  are kept and derived relations are recomputed.

### Purge one object

```http
DELETE /v1/trash/8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21
```

```json
{"purged": true, "id": "8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21"}
```

This is the old hard delete: inbound `links` and object properties are cleaned,
exclusive attachments and blobs are deleted, promoted media objects are
reassigned, and the tombstone stays. `404` when the id is not in the trash.

### Empty the trash

```http
POST /v1/trash/empty
POST /v1/trash/empty?type_id=note
```

```json
{"purged": 3}
```

Purging happens one entry per transaction. A crash halfway leaves the remaining
entries in the trash and a retry finishes the job.

## Data model

- New record kind **`trash`**, keyed by object id, value:

  ```json
  {"object": { ...full Object... }, "deleted_at": "2026-10-31T12:00:00.000000000Z"}
  ```

  It is encrypted with the same AES-256-GCM scheme as every other record, with
  associated data `trash/<id>`.
- The tombstone keeps its existing shape (`id`, `revision`, `deleted_at`). On
  trash, `revision = object.revision + 1`.
- No new SQLite columns or tables: `records(kind, id, payload)` is generic.
  `schemaVersion` goes from 2 to 3 (no DDL) so a pre-trash binary refuses to
  open a workspace whose trash payloads it would ignore.
- The in-memory index is unchanged. Trashed objects leave the live index, so
  listings, search, backlinks and title uniqueness stop seeing them without any
  new filter. `ObjectMeta` does not need a deleted flag.
- `Fsck` checks that every trash record has a matching tombstone with a newer
  revision, that the payload id matches its key, and that no id is live and
  trashed at the same time.

## How

### Why a side record and not the tombstone

Tombstones are part of the mobile sync payload (`POST /v1/sync/pull` returns
`tombstones`), and they are permanent anti-resurrection markers. Embedding the
full object in the tombstone would:

- inflate every mobile pull with up to megabytes of content the phone must
  discard, and
- keep the payload forever, because the tombstone must survive purge.

A separate `trash` record is temporary (deleted by restore or purge), is never
synced, and is decrypted only by `GET /v1/trash` and restore. The tombstone
stays a few dozen bytes.

### Why not a `deleted_at` flag on the live object

Keeping the object row and marking it deleted would force every read path
(`GET /v1/objects`, search, `ObjectsByType`, backlinks, title uniqueness, sync
pull, property validation, pickers, export later) to filter it. One missed path
leaks trashed content. Removing the live row reuses the existing invariant "the
`object` table contains exactly the live objects", so the index, listings and
search need no changes at all.

### References while trashed

Trashing does **not** rewrite other objects' `links` or object properties. This
is what makes "restore keeps backlinks intact" true, and it mirrors Capacities'
mention-based model: the mention stays in the text, the target is simply not
resolvable while it is in the trash. The trade-off is that a live object can
hold a reference to an id that `GET /v1/objects/{id}` answers with 404. Clients
should render those references as "in trash" (see follow-ups). Purge performs
the old reference cleanup, so nothing dangles permanently.

Validation was relaxed accordingly: `links` and typed object properties may
point to a trashed id (restorable), but not to a purged id (only a tombstone).
Mobile pushes keep trashed references and strip purged ones, so an offline
outbox never gets stuck and a later purge still cleans the relation.

### Restore and anti-resurrection

- Purged ids have no trash record, so restore is a `404` and the tombstone keeps
  blocking id reuse exactly as before.
- A stale mobile `put` for a trashed id is ignored with `ignored_tombstoned`
  because the tombstone revision is newer than the client's `base_revision`.
- Restore deletes the tombstone in the same transaction and writes a revision
  above it. A client that saw the deletion and later pulls the restore receives
  the object again with a newer revision.
- The original incarnation is preserved; `prepareSave`'s "new incarnation on
  recreate" rule is deliberately bypassed for restore.

### Journal safety

`GET /v1/journal/{date}` used to recreate a deleted fixed-id journal as an empty
note. With trash, that would silently discard the trashed text on the next app
start. Opening a date whose journal is in the trash now restores it instead.
Explicit recreation from a mobile append still follows the old rule (the phone
sends a full journal payload); restoring from mobile is a follow-up.

### Empty trash

`POST /v1/trash/empty` purges one entry per transaction. This keeps memory
bounded and makes a crash recoverable (the remaining entries are still in the
trash). It is not atomic across the whole trash, which is acceptable for a
maintenance action. Each purge reloads attachment metadata, so the operation is
O(trashed objects × assets); a personal workspace is far from that being a
problem, and a bulk path can be added if it ever is.

### Mobile sync decision and trade-off

Trash writes exactly the journal entries the old hard delete wrote: `object`
delete plus `tombstone` put. A phone that pulls while an object is trashed
deletes it locally and never learns that a trash payload exists. When the object
is restored, the phone receives an `object` put with a newer revision and a
`tombstone` delete. The trade-off is deliberate: mobile keeps its simple
"deletion is deletion" model, a phone that never restores anything is fine, and
restoring from mobile (trash UI plus a restore operation) is a follow-up. The
current Android client ignores `deleted.kind == "tombstone"`, so its local
tombstone row goes stale; that is harmless today but should be cleaned up.

## Contracts

The following section was added to `contracts/SYNC.md`:

```markdown
## Trash (soft delete)

Implemented 2026-10-31 (backend stage `trash`). `DELETE /v1/objects/{id}` no
longer destroys the payload: it moves the object to the trash. The tombstone and
its anti-resurrection rules are unchanged; the difference is that the payload is
kept in a separate encrypted record (`kind=trash`, id = object id) until the
object is restored or purged. The tombstone stays small because it is part of
the mobile sync payload; the trash payload is never synced.

### Lifecycle

- **Trash** (`DELETE /v1/objects/{id}`): writes the tombstone
  (`revision = object.revision + 1`, `deleted_at`), writes the `trash` record
  with the full object and the same `deleted_at`, and removes the live object
  row in one transaction. Response:
  `{"deleted":true,"trashed":true,"id":"...","deleted_at":"...","sync_status":"synced"}`.
- **Restore** (`POST /v1/objects/{id}/restore`): writes the object again with a
  fresh revision (`max(object.revision, tombstone.revision) + 1`), keeps the
  original `incarnation`, and deletes the tombstone and the trash record in the
  same transaction. `404` when the id is not in the trash (including purged
  ids). If another live object took the title while this one was trashed, the
  restored title gets a numeric suffix (`Title (2)`).
- **Purge** (`DELETE /v1/trash/{id}`): today's hard delete. Removes the trash
  record, cleans inbound `links`/`properties` references, deletes or reassigns
  attachments and blobs, and **keeps the tombstone**. `404` when the id is not
  in the trash.
- **Empty** (`POST /v1/trash/empty[?type_id=...]`): purges every trashed object,
  optionally restricted to one type. Response `{"purged":N}`. Entries are
  purged one transaction at a time; a crash leaves the rest recoverable.

`deleted_at` is stored on both the tombstone and the trash record so a future
janitor can implement retention (Capacities uses 30 days). This stage does not
auto-purge.

### Listing and exclusions

- `GET /v1/trash?q=&type_id=` returns metadata only:
  `[{"id","type_id","title","deleted_at"}]`, ordered by `deleted_at` descending
  (ties by id). `q` matches title or content, case-insensitively.
- A trashed object is not a live object: it is absent from `GET /v1/objects`,
  `GET /v1/search`, `GET /v1/objects/{id}` (404), backlinks as a source, and
  `GET /v1/objects/{id}/backlinks` (404). The in-memory index keeps only live
  objects, so no extra filters are needed.
- Inbound references are **not** rewritten when an object is trashed. Other
  objects keep the id in `links`/object properties, so a restore brings the
  backlinks back intact. The target is simply not resolvable until then.
  Clients should render such references as "in trash" (follow-up). Purge is the
  operation that cleans references, exactly as the old hard delete did.
- While trashed, the object's attachments and blobs are kept encrypted so a
  restore does not lose covers or files. Purge deletes exclusive attachments and
  reassigns promoted media objects, as before.

### Mobile sync

- Trash writes the same journal entries as the old hard delete: `object` delete
  plus `tombstone` put. A phone sees the deletion on its next pull and removes
  the object locally; it never sees the trash payload (the `trash` record kind
  is not a sync kind).
- Restore writes `object` put plus `tombstone` delete. The object is delivered
  again with a newer revision; `deleted` contains
  `{"kind":"tombstone","id":...}` so clients can drop their local tombstone row
  (the current Android client ignores that kind; harmless but a follow-up).
- Anti-resurrection is unchanged: a `put` for a trashed or purged id with
  `base_revision <= tombstone.revision` is ignored with `ignored_tombstoned`.
  Fixed journal ids keep the explicit-recreation exception.
- A mobile `delete` operation moves the object to the trash too.
- Mobile pushes keep references to trashed objects (they are restorable) and
  strip references to purged objects, so a stale outbox never gets stuck and a
  later purge still cleans the relation on the server.
- Restoring from mobile is a follow-up: the phone can pull the restored object
  but has no trash UI or restore operation yet.

### Storage

- The trash payload is a normal encrypted record (`kind=trash`,
  AAD `trash/<id>`), not part of the tombstone. Tombstones are synced to every
  device and must stay small; embedding a multi-megabyte document would inflate
  every pull and keep the payload forever. The side record is temporary and is
  deleted by restore or purge.
- Schema version 3: the generic `records` table needs no DDL, but the bump
  prevents a pre-trash binary from opening a workspace whose trash payloads it
  would ignore.
- `Fsck` verifies that every trash record has a matching tombstone with a newer
  revision and that no id is live and trashed at the same time.
```

## Testing

`server/trash_test.go` covers:

- create → delete → listed in trash → restore: content, properties and
  `incarnation` survive, revision is fresh, the object reappears in listings,
  search and both explicit and property backlinks; restoring twice is a 404.
- purge → the object is gone from the trash, restore is a 404, inbound
  references are cleaned, and a stale mobile `put` with the old revision is
  `ignored_tombstoned`.
- empty trash, with and without `?type_id=`, and the empty response shape.
- `GET /v1/trash` filters (`q` on title/content, `type_id`) and `deleted_at`
  descending order.
- restore with a title collision (`Title (2)`) and with a reference to an object
  that was purged while it was in the trash.
- opening a trashed journal restores it instead of creating an empty one.
- sync pull after trash delivers `object` delete + tombstone; after restore it
  delivers the object + `tombstone` delete; a put while trashed is ignored.
- a mobile `delete` operation moves the object to the trash and still reaches
  the journal.
- trash survives a store restart (encrypted side record + `Fsck`).
- a trashed media object keeps its blob and owner link until purge; purge
  deletes the blob and cleans the owner.

Existing tests were updated where the contract intentionally changed:
`TestTrashKeepsReferencesUntilPurge` (was `TestDeletionRemovesReferencesFromDiary`),
the typed-relation test in `schema_test.go`, the media lifecycle in
`index_test.go`, the stale-mobile-reference test in `index_test.go`, and the
schema version assertion in `migration_test.go`.

Commands:

```sh
cd server
go test ./...
go test -race ./...
go vet ./...
gofmt -l .
```

Manual verification (disposable sandbox, port 8879):

```sh
docker compose -f sandbox/compose.test.yaml up -d --build
# create a note, DELETE it, GET /v1/trash, POST .../restore, DELETE /v1/trash/{id}
docker compose -f sandbox/compose.test.yaml down
```

## Client follow-ups

- **Desktop**: add the Trash view (list with search and type filter, restore,
  permanent delete, empty trash); refresh the object, its backlinks and search
  after restore; hide trashed objects from pickers (the server already excludes
  them from `GET /v1/objects`); render `links`/object properties that point to a
  trashed id as "in trash" instead of a broken link; offer "restore" when
  opening a URL that is already in the trash.
- **Mobile**: handle `deleted.kind == "tombstone"` in `applyPull` to drop the
  local tombstone row; trash/restore UI and a restore operation are a later
  stage (the phone can already receive a restored object).
- **Extension**: no change required; it uses `GET /v1/objects` and capture.
- **Future janitor**: use `deleted_at` for retention (Capacities: 30 days) and
  call the same purge path; do not add auto-purge before the retention policy is
  decided.
- **Future bulk empty**: if the trash grows large, preload attachment metadata
  once instead of once per purged object.
- **Large trash**: `GET /v1/trash` decrypts every trash payload to read its
  title. If the trash ever grows large, stream the list or keep a metadata-only
  index record next to the payload.

## Follow-ups (not bugs fixed here)

- Trashed assets remain downloadable by id until purge. This is deliberate (a
  restore must not lose covers), but a future asset ACL could hide them.
- `GET /v1/assets?object_id=` still returns attachments of a trashed owner; the
  desktop does not reach that panel for a 404 object, but it is worth aligning
  when the trash UI lands.
- Capture deduplication only looks at live web objects, so capturing a URL that
  is in the trash creates a new object instead of offering a restore.
