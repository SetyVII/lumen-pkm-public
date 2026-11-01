# Object version history

Status: implemented on `backend/version-history` (2026-10-31). Server-side only;
the client work is listed at the end.

## What & why

Capacities keeps a version history per object: every save creates a version you
can preview and restore, and restoring creates a new version instead of deleting
history (<https://docs.capacities.io/reference/version-history>). Lumen already
assigned a monotonic `revision` per object and used `base_revision` for CAS and
mobile conflict detection, but it kept no old content: once a `PATCH` committed,
the previous text was gone. This stage adds bounded, encrypted snapshots of the
previous state, three additive endpoints to list, read and restore them, and
documents the interaction with CAS and mobile sync. Snapshots are server-side
only: they are not part of the sync protocol and never reach a client's local
database.

## API

### List versions

`GET /v1/objects/{id}/versions`

Response 200, newest first:

```json
[
  {"revision": 7, "timestamp": "2026-10-31T09:12:44.123456789Z", "word_count": 184, "bytes": 1120},
  {"revision": 6, "timestamp": "2026-10-31T08:40:02.000000000Z", "word_count": 179, "bytes": 1094}
]
```

- `timestamp` is the `updated_at` of the snapshotted state (legacy objects
  without `updated_at` fall back to the capture time).
- `word_count` counts whitespace-separated tokens in `content`; `bytes` is the
  UTF-8 length of `content`.
- `[]` when the object exists but has no previous revisions. 404 if the object
  does not exist.
- The current revision is not listed; `GET /v1/objects/{id}` is the current
  state.

### Read a version

`GET /v1/objects/{id}/versions/{revision}`

Response 200:

```json
{
  "object_id": "0f7c1f0e-6a4e-4f2a-9b8e-3d1c2a5b7e90",
  "revision": 6,
  "timestamp": "2026-10-31T08:40:02.000000000Z",
  "type_id": "note",
  "title": "Reading notes",
  "content": "Old body",
  "properties": {"summary": "Old summary"},
  "links": ["5d2e..."],
  "word_count": 2,
  "bytes": 8
}
```

404 when the object or the snapshot does not exist (including the current
revision); 400 for a non-numeric revision.

### Restore

`POST /v1/objects/{id}/restore/{revision}`

Optional body, same CAS field as `PATCH`:

```json
{"base_revision": 7}
```

Response 200 is the updated object with a new `revision` (8 in this example) and
the `content`, `properties` and `links` from revision 6. The pre-restore state
(revision 7) is added to history by the same write.

- `base_revision` omitted or `0`: no CAS.
- Mismatch: 409
  `{"error":"conflict","conflict":true,"current":{...}}`; nothing is written.
- 404 unknown object/version; 400 invalid revision/body or a property value that
  no longer validates under the current type.
- `title` and `type_id` are preserved; links and object-property values that
  point to objects deleted since the snapshot are dropped.
- Restoring a trashed/deleted object is out of scope (see Contracts).

## Data model

New table:

```sql
CREATE TABLE IF NOT EXISTS object_versions(
  object_id TEXT NOT NULL,
  revision INTEGER NOT NULL,
  captured_at TEXT NOT NULL,
  payload BLOB NOT NULL,
  PRIMARY KEY(object_id, revision)
);
```

- `payload` is the JSON `ObjectVersion` (`object_id`, `revision`, `type_id`,
  `title`, `content`, `properties`, `links`, `updated_at`, `captured_at`),
  encrypted with AES-256-GCM (random nonce prefixed, associated data
  `version/<object_id>/<revision>`), the same path as object and blob payloads.
- `captured_at` is the wall-clock time the snapshot row was written. It is
  plaintext like `revision` and the object ID; `Fsck` checks that it matches the
  encrypted payload.
- No `schema_version` bump: the table is created with `CREATE TABLE IF NOT
  EXISTS` on every `OpenStore`, no existing rows are rewritten, and an older
  server ignores the table (downgrade-safe). There is no in-memory index change:
  history is read from SQLite on demand and never cached in RAM.
- `Fsck` decrypts every snapshot, verifies its identity and `captured_at`, and
  enforces the 20-row cap.

## How

### Side table, not `records`

`records` is the sync-visible store: every put/delete goes through
`recordSyncChange`, and `Fsck` rejects unknown kinds. Reusing it would either
journal history (leaking it to mobile) or require special cases in the journal
and `Fsck`. A dedicated table makes the guarantee structural: snapshots cannot
enter `sync_changes` because they are never inserted into `records`. It also
makes pruning a single indexed `DELETE` and keeps `Store.List("object")` and
`ObjectCount` unaffected.

### One hook: `prepareSave`

Every revision bump goes through `App.prepareSave` (audited): `PATCH`, journal
creation/linking, capture, asset upload/promote, type-schema relation
recomputation, deletion cleanup of backlinks, mobile `sync/push`
put/delete/asset, and restore itself. Before incrementing the revision,
`prepareSave` reads the stored object and appends a `VersionRecord` with the
previous state. New objects have no previous state and therefore no snapshot. A
no-op `PATCH` returns before `prepareSave`, so it creates no revision and no
copy.

### Same transaction

`Store.PutBatchBlobs` intercepts `VersionRecord`s before the generic record
path, serializes and encrypts them before `BEGIN IMMEDIATE`, and inserts them
plus the prune inside the same transaction as the object row. A failed save
leaves neither the new revision nor a snapshot; a successful HTTP response means
both are on disk (the SYNC.md durability rule). The prune keeps the newest 20
rows by revision.

### Cap: 20

Each snapshot is a full copy (no diff or compression), so unbounded history
would grow the encrypted store with every autosave. Twenty previous states is
enough for the common "undo the last few edits" flow, matches the bounded-history
spirit of Capacities, and keeps the list endpoint to at most 20 decrypts. The
constant `maxObjectVersions` is the single place to raise it; a later version can
make it configurable per workspace. Pruning is oldest-first by revision, so the
newest history is always retained.

### Restore semantics

Restore writes `content`, `properties` and `links` from the snapshot as a new
revision; it never mutates or deletes snapshot rows. The write goes through
`prepareSave`, so the current state is snapshotted first and can itself be
restored. `title` and `type_id` are preserved: titles are unique workspace-wide
(restoring an old title could collide with another object) and type conversion is
a separate stage with its own property mapping. If the snapshot references
objects deleted since it was captured, those links and property values are
dropped so the restore still succeeds. A property value that no longer validates
under the current type returns 400 rather than writing an invalid object.

### CAS

`base_revision` is optional and uses the same semantics as `PATCH`: omitted or
`0` means no CAS, a mismatch returns 409 with the current object. The check and
the write happen under `writeMu`, so a concurrent `PATCH` cannot slip between
them.

### Sync

Snapshots are server-side only. They are not in `records`, not in
`sync_changes`, and `ChangesSince` only selects object/type/tombstone/asset
kinds. A restore is a normal object put: mobile receives the restored content in
the next pull exactly like a `PATCH`, and never receives history. This
complements the existing mobile conflict merge: the server keeps the pre-merge
state in history, but the client still gets one current object.

### Trash composition

This branch's `DELETE` still hard-deletes and writes a tombstone. Snapshots are
retained for a future trash restore (they are keyed by object ID and revision,
and revisions continue after the tombstone), but they are not exposed for a
missing object: the versions endpoints return 404. The trash stage owns soft
delete/restore and permanent purge; purge must delete `object_versions` rows.
Restoring a trashed object will compose because the object ID and revision
history survive.

## Contracts

The `contracts/SYNC.md` section added by this stage:

> ## Object version history (server-side only)
>
> Every successful object change that bumps `revision` stores a snapshot of the
> **previous** state in the `object_versions` side table, keyed by
> `(object_id, revision)`. The snapshot contains the previous `content`,
> `properties`, `links`, `title`, `type_id` and `updated_at`. It is encrypted
> with AES-256-GCM like every other payload, with associated data
> `version/<object_id>/<revision>`.
>
> - The server keeps at most **20 snapshots per object**. The oldest rows are
>   pruned in the same transaction as the new snapshot, so the cap is a durable
>   invariant (`Fsck` verifies it). The current state is never stored as a
>   snapshot; `GET /v1/objects/{id}` is the current revision.
> - Snapshots are created by the central save path, so they cover `PATCH`,
>   journal creation/linking, capture, asset upload/promote, type-schema
>   relation recomputation, deletion cleanup and mobile `sync/push` merges.
>   Creating an object does not snapshot (there is no previous state), and a
>   `PATCH` that changes nothing returns 200 without a new revision or
>   snapshot.
> - A snapshot row never enters `records` or `sync_changes`. It is
>   **server-side only**: `POST /v1/sync/pull` and the push protocol never carry
>   history. A restore is a normal object `put`, so mobile clients receive the
>   restored content like any other `PATCH` and keep their own single current
>   copy.
> - Objects created before revisions existed may have a snapshot at revision
>   `0`; it is listed and restorable like any other revision.
>
> ### Endpoints
>
> - `GET /v1/objects/{id}/versions` → newest first:
>   `[{"revision": 7, "timestamp": "...", "word_count": 184, "bytes": 1120}, ...]`.
>   `timestamp` is the `updated_at` of that state; `word_count` counts
>   whitespace-separated words and `bytes` is the UTF-8 length of `content`.
> - `GET /v1/objects/{id}/versions/{revision}` → the full snapshot
>   (`object_id`, `revision`, `timestamp`, `type_id`, `title`, `content`,
>   `properties`, `links`, `word_count`, `bytes`). The current revision is not a
>   snapshot and returns 404.
> - `POST /v1/objects/{id}/restore/{revision}` with optional
>   `{"base_revision": N}` → restores `content`, `properties` and `links` from
>   the snapshot as a **new** revision and returns the updated object. It never
>   deletes history: the pre-restore state is snapshotted by the restore write
>   itself. `title` and `type_id` are preserved (titles are unique
>   workspace-wide and type conversion is a separate operation). Links or
>   object properties that point to objects deleted since the snapshot are
>   dropped instead of failing the restore.
> - `base_revision` follows `PATCH` semantics: omitted or `0` disables CAS; a
>   mismatch returns `409` with
>   `{"error":"conflict","conflict":true,"current":<object>}`. Unknown
>   object/version → 404; invalid revision/body → 400.
>
> Restoring a deleted or trashed object is out of scope: the trash stage owns
> restoring the object itself, and its restore will compose with this history
> because snapshots are keyed by object ID and revision. Permanent purge must
> delete the object's `object_versions` rows; this stage does not purge them.

## Testing

`server/version_history_test.go` covers:

- create → no snapshot; `PATCH` → previous revision in history with the exact
  content, properties and links, plus sane `word_count`/`bytes`;
- list ordering newest first and the 20-snapshot cap (revision 5 pruned,
  revision 6 readable);
- restore creates a new revision with the old content/properties, keeps the
  pre-restore state and all older snapshots;
- CAS: stale `base_revision` → 409 with `current`, correct base → 200, empty
  body → 200, unknown revision → 404, invalid revision → 400, missing object →
  404;
- snapshots do not appear in `sync_changes` or pull; restore is one normal
  object change and syncs; pre-restore content does not leak into pull;
- no-op `PATCH` creates no revision or snapshot;
- mobile `sync/push` changes are snapshotted;
- restore drops links to deleted objects;
- snapshot plaintext does not appear in the SQLite/WAL files and survives a
  restart;
- every test runs `Fsck` in cleanup, which validates the encrypted snapshots
  and the cap.

Commands:

```sh
cd server
gofmt -l .
go vet ./...
go test -race ./...
```

Manual check against the disposable server (port 8879,
`sandbox/compose.test.yaml`; never the personal installation):

```sh
curl -H "Authorization: Bearer $TOKEN" localhost:8879/v1/objects/$ID/versions
curl -H "Authorization: Bearer $TOKEN" \
  -X POST -d '{"base_revision":3}' \
  localhost:8879/v1/objects/$ID/restore/1
```

## Client follow-ups

- Desktop: history panel for the open object (list with timestamp/size,
  preview, restore with confirmation); send the open object's `revision` as
  `base_revision`; on 409 reload the object and show the conflict; refresh the
  editor and backlinks after restore. Do not add history to the offline cache:
  it is server-side only.
- Mobile: no protocol change; optionally expose a read-only history screen
  through the same endpoints. Restore is a normal object update and arrives
  through pull.
- Extension: no change.

## Follow-ups

- Trash/purge stage: delete `object_versions` rows on permanent purge; keep
  them on soft delete/restore.
- Make the cap configurable (env var or workspace setting) if 20 is too small
  for heavy editors.
- Consider compression or delta storage for large notes; the current full-copy
  design is bounded by the cap.
- A client-side diff view (side-by-side or inline) is a UI feature, not a
  server change.
- If type conversion or merge need to restore the old type, extend restore with
  an explicit `restore_type` option; this stage preserves the current type by
  design.
