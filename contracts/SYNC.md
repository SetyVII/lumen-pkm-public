# Shared document and synchronization contract

The new coordinated server/Qt/Android generation owns editable content as
`Object.document = {schema_version:2,blocks:[...]}` for EVERY object type,
including media objects whose editor is secondary/collapsed. The service derives
`content` and relations from this document; Qt HTML and `properties.blocks` are
not alternate sources of editable content.

Normative structural schemas: `document.schema.json` and
`document-commands.schema.json`. Semantic rules and command payloads:
`../docs/architecture/document-v2.md`. Domain package:
`server/internal/document`. Shared direct documents:
`fixtures/document-v2/*.document.json`; command vectors:
`fixtures/document-commands/*.json`.

A node owns stable id/type, attrs, inline content and real children. Explicit
checked/expanded/list kind replace text glyphs and indent heuristics. Columns
own independent column nodes; tables own identified rows/cells and identified
width definitions. Image nodes require asset IDs and have separate captions.
Refs identify objects, or object+block, and temporal references preserve date
versus datetime, timezone and optional interval end. Unknown JSON attrs survive;
unknown structural fields/types and invalid semantics are rejected.

Server domain operations are atomic and leave their input unchanged on failure.
Snapshot/command envelopes, object existence and asset lifecycle, revision/CAS,
idempotent receipts and durable sync commit are service responsibilities. This
section defines persistence semantics. HTTP routes are exposed through API
discovery; client interaction coverage can differ from the structural contract.

Markdown ingestion and MCP body editing use the same Document v2 preconditions:
`base_revision` and a unique `op_id` (8..128 characters). A byte-equivalent retry
returns its committed snapshot; changed payload reuse or a stale new operation
returns conflict. Read/create the daily note with `GET /v1/journal/{date}` before
appending, then supply its revision. Append does not create a missing journal.

### Operational invariants of protocol 2

All mutating REST/sync requests send `X-Lumen-Document-Version: 2`; old writers
are refused with 426. Sync reports `lumen-sync/2`. SQLite schema is 6, distinct
from these protocol/document versions. Snapshot edits and command batches use
CAS and a stable operation identity; metadata updates also require the read
revision. All stored objects, including internal daily notes/tasks, have a
canonical document. Plain `content` is a derived projection, never an alternate
body editor for an existing document.

Encrypted receipts preserve committed identities and revisions. Full reply
snapshots are cached for at most seven days and within a 64 MiB aggregate
budget. Expired document-edit replay returns 410 and requires reload; expired
sync replay still acknowledges the exact operation and revision without an
`object` snapshot. Operation identity remains reserved, so expiration cannot
repeat an old mutation. TTL applies on reads even if no later write occurred.

Offline image/PDF capture must originate from an actual selected local file.
Its first object put may carry `properties.media_state: "upload_pending"` and
the nonempty `asset_id` of the queued upload. This permits storing its notes
while the bytes are in transit; it is not a blank media creation option. The
asset operation then commits the file and marks its owning media object
`ready` atomically, retaining its document. A document image node can commit
only after its image asset exists. Failed/wrong-MIME uploads leave the operation
unacknowledged and the media pending. Normal POST object creation requires the
actual source asset; PDF placeholders require the explicit `draft` state.

A committed asset identity is immutable: a new operation may resend identical
bytes to the same owner without creating another history edit. Different bytes
or ownership require a new asset ID. Snapshot puts, uploads and document edits
must retain their dependency order. Bootstrap resend creates new operations;
clients must not mutate the payload behind an existing operation ID.

Fixture: `fixtures/document-v2/offline-media-upload.json`. Its three operations
are separate commits in the shown order, not one aggregate transaction. The
owner remains visibly pending if the upload fails; the journal image is not
committed before its source exists.
Offline object relations (`links`, typed object properties and canonical inline/
block references) may point to an object not uploaded yet. Sync preserves these
pending identities, enabling cyclic references and arbitrary upload ordering.
Syntax and property types remain checked; when a live or trashed target exists,
its actual type must match the relation schema. A storage/decryption failure is
an error, never evidence that a target is missing. Only permanently purged
relations are stripped; canonical references keep their readable labels.
Direct object-creation/edit APIs retain strict target-existence validation for
manual links and typed properties. Pending references appear in the reverse
index and resolve when their targets arrive.

REST object creation accepts an optional valid `id` (1..128 non-whitespace,
non-control UTF-8 bytes) allocated by a native draft editor. This makes compound
internal references valid before the creation form is confirmed. An occupied,
trashed or tombstoned identity returns 409 and is never overwritten by POST.
Omitting `id` still allocates an identity on the server. Revisions always belong
to the store; client-supplied revisions do not select the created revision.

Sync receipts include `previous_revision`, the stored revision immediately
before the acknowledged object write (zero for creation). For asset operations
that update a media owner, clients may advance a later *unsent* local snapshot
only when its base equals this exact previous revision. If another editor had
changed the owner before upload, a different previous revision prevents rebasing
over that unseen edit. Retried/compacted receipts retain this value. Asset
resends that are no-ops do not manufacture an owner revision.
### Structured editor clipboard

MIME `application/vnd.lumen.document+json` carries an envelope described by
`clipboard.schema.json`: `{clipboard_version:1, source_object_id, operation,
document}`. `source_object_id` is the source editor object's ID or an empty string
when genuinely unknown; `operation` is `copy` or `move`; `document` is a complete
Document2 whose root blocks are the selected roots, including their descendants.
An interoperable fixture is
`fixtures/clipboard-v1/internal-and-external-reference.json`.

For copy/paste, renew all selected node and table-column identities. Retarget a
compound block reference only when its `object_id` equals the nonempty
`source_object_id` AND its `block_id` identifies a copied node. Set its object ID
to the known target editor owner and its block ID to the fresh copied node ID.
Apply the same rule to `object_embed.attrs.object_id/block_id`. Preserve external
references even when their block ID text equals a selected local node ID. Keep
asset IDs, reference labels, sizes, marks, hierarchical state and unknown attrs.
When target ownership is unknown, preserve compound references rather than
inventing a target ID.

`move` may retain IDs only when they are absent from the destination document;
colliding destination IDs must be renewed with the same reference rules. The
clipboard operation does not authorize silently deleting another stored object;
source cutting and destination pasting are separate document transactions.
A missing/unsupported framed version or invalid structured document must leave
the target document unchanged. An explicit text-only fallback may use the
`text/plain` flavor. A raw Document2 can be recognized separately as a safe copy
with unknown source ownership and no internal-reference retargeting. New writes
always emit the versioned envelope; parsers must not silently flatten malformed
structured data or consume an unsupported future version as plain text.

If a pending typed relation's target later arrives with an incompatible type,
the target still syncs and both objects retain their original data. Its first
sync receipt includes bounded `reference_issues` entries with `source_object_id`,
`property_key`, `target_object_id`, `expected_type` and `actual_type`. This is a
validation warning requiring repair, never a claim that the relation is valid.
`reference_issues_cursor` indicates additional diagnostics to inspect.
`GET /v1/objects/{id}/reference-issues?cursor=...` refreshes the incoming-target
report as `{object_id,issues,next_cursor}`; it inspects at most 200 referrers and
returns at most 200 issues per page, including pagination within one source's
properties. Correcting the target type clears its current report. Receipts remain
historical acknowledgements. The diagnostic does not modify the source document,
append messages to its text, or silently drop the property. UI presentation of
these warnings remains a client task. See `fixtures/reference-issues/type-resolution.json`.

History and trash restoration preserve pending offline manual/typed/canonical
references, including after server reopen. Only permanently purged targets are
removed; a trashed target remains restorable. A now-known incompatible typed
relation makes restore return a reviewable validation error and leaves the
current object/trash/history intact. Restoring an offline media object retains
its queued `upload_pending` asset identity and editor. Restored media whose
source already exists is normalized to ready; missing committed source bytes
are not silently published as an empty image/PDF.

Canonical object create, PATCH and dedicated document routes allow the shared
8 MiB document limit plus 1 MiB of envelope/metadata. The former 3 MiB generic
JSON limit does not truncate or reject an otherwise valid document on these
routes. Domain validation still enforces the document's own byte/node/depth caps.
Body-only commits validate the new document and actual asset references while
preserving existing unrelated metadata, even if a typed relationship is pending
or has a reported mismatch. A body/title PATCH with unchanged metadata follows
the same rule. Changing the properties/links/type still validates those values.
Media source validation also applies to type changes and conversions, not just
the main object creation form.

### Bounded pull and startup memory

`POST /v1/sync/pull` returns at most 128 change identities per page and an 8 MiB
soft payload budget. A single larger valid object is returned whole so progress
is possible; document data is never truncated. The response distinguishes:

- `head_seq`: server head observed for this request, for progress display.
- `next_seq`: cursor whose records have actually been delivered by this page.
- `has_more`: request another page using `since_seq=next_seq`.

Clients commit `next_seq` in the same transaction as the page's data and never
jump to `head_seq` while `has_more` is true. Disconnecting between pages must
resume from the last applied page. New writes between pages may deliver a newer
version of an identity again; identities are upserted, not duplicated. Read or
decryption errors produce an error response, never a fabricated deletion and
acknowledgement. Fixture: `fixtures/sync-v2/pull-pages.json`.

Startup builds the in-memory metadata index by decrypting one stored object at
a time. It does not retain the whole library's documents to sort the index.
This generation refuses objects without Document 2 at startup; a coordinated
clean-library upgrade is required rather than silently interpreting an old
workspace as a complete new schema. The implementation does not delete it.

### Field-level property intents

`PATCH /v1/objects/{id}` accepts `property_updates: {key:value}` and
`remove_properties: [key,...]` with the object's `base_revision`. These operations
change only named fields, preserving unfetched properties and the canonical
body. They are mutually exclusive with full `properties` replacement, which
keeps its existing replacement semantics. A key cannot be set and removed in
the same request; at most 256 field intents are accepted. Server-managed
`assets`, `reader_content`, `favicon` and obsolete body formats cannot be
changed through these intents. Cover updates require an existing image asset.
An optional `op_id` provides the same durable retry receipt as other PATCH
operations. Validation and all field changes commit atomically with history
and sync; failed or stale field edits do not partly update an object.
### Explicit read-only catalog projection

`GET /v1/catalog` returns `{projection:"catalog/v1", entries, next_cursor,
has_more}`. This is a distinct summary DTO, never a full `Object`: it contains no
`document`, `content` or reader article. Existing full-object APIs remain intact.
The default/maximum page is 128 entries with an approximately 8 MiB response
budget. The cursor uses metadata display order (updated_at descending, ID
ascending). Concurrent edits can move entries; clients deduplicate by ID/revision
and must not treat this discovery page as an immutable sync snapshot.

Metadata-only requests page the in-memory metadata index without reading object
payloads. Optional `type_id`, `q` (title/aliases only) and `include_templates`
filter this metadata. Unknown parameters are rejected. Full-text body search is
a separate operation, not the meaning of catalog `q`.

Optional repeated `property=key` (up to 64 distinct keys) and
`excerpt_chars=0..512` decrypt only selected page objects, one at a time.
`requested_properties` identifies the projection. Values larger than 64 KiB are
reported through `omitted_properties` rather than silently truncated. Body fields
`blocks`, `rich_text`, `reader_content` and `document` cannot be requested here.
Excerpts are rune bounded, copied into short independent strings, and flagged
`excerpt_truncated` when needed. `include_relations=true` optionally returns up
to 256 unique `related_ids`, with `relations_truncated` if more exist. Fetch the
full object to obtain complete omitted values or relation lists.

Open an object with `GET /v1/objects/{id}` before editing its full document.
A catalog entry must never be submitted as a replacement snapshot or replacement
`properties` map. Native catalog adoption requires partial property intents or
fresh-object field merging and bounded full-object caches. That client migration
is deliberately pending; the endpoint does not silently alter existing Qt views.
Schema: `catalog.schema.json`; fixture: `fixtures/catalog-v1/summary-page.json`.
