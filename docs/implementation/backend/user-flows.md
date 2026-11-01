# User-flow scenarios (backend parity stage 14)

Status: implemented 2026-10-31 on `backend/user-flows`. Program:
`docs/tracking/BACKEND-PARITY-STRUCTURE.md` (addition #14). This is a QA
hardening stage: it adds no new API surface and no new storage. It drives the
merged HTTP API the way a person uses the app and fixes the composition bugs
those journeys expose.

## What & why

The eleven parity stages were each tested in isolation. A user does not use one
stage at a time: they open the daily note, mention a person, tag the note, paste
a URL, complete a chore, delete the wrong card, restore it, merge a duplicate
and export the space. Those combinations are where contracts meet, and that is
where regressions hide: a tag that is stored but never becomes a relation, a
collection that becomes uneditable after a type change, a purge that leaves old
snapshots behind.

This stage adds ten end-to-end scenario tests. Each is one Go test function that
walks a story through the HTTP handler, asserts the state after every step, and
uses the same helpers as the rest of the suite (`makeApp`, `api`,
`objectFrom`, `decodeBody`, `httptest` fixtures). The tests are deterministic,
fast and offline: the only network is an in-process `httptest` server wired
through the production SSRF-safe fetcher.

The stage also ships `docs/guides/BACKEND-TESTING.md`, the testing guide that
explains how to run the suite, what each scenario guards, how to add another
one, and how to verify manually against the disposable sandbox.

## API

No endpoints were added or changed. The scenarios exercise:

- `GET /v1/journal/{date}` (lazy creation and `apply_default_template`);
- `POST/PATCH/DELETE /v1/objects`, `GET /v1/objects`, `/v1/search`;
- `GET /v1/objects/{id}/backlinks`, `POST /v1/objects/{id}/restore`;
- `GET /v1/trash`, `DELETE /v1/trash/{id}`;
- `POST /v1/capture`, `POST /v1/objects/{id}/refresh-reader` (indirectly via
  capture);
- `POST /v1/assets`, `POST /v1/assets/{id}/promote`, `GET /v1/assets/{id}/content`;
- `POST /v1/objects/from-template`, `PATCH /v1/types/{id}`;
- `POST /v1/tasks/{id}/complete`;
- `POST /v1/objects/{id}/convert`, `POST /v1/objects/{id}/merge`;
- `GET/POST /v1/export`;
- `POST /v1/markdown/to-blocks`, `POST /v1/markdown/from-blocks`;
- `GET /v1/objects/{id}/versions`, `POST /v1/objects/{id}/restore/{revision}`;
- `POST /v1/sync/pull` to verify what mobile receives.

`POST /v1/objects/markdown` and `POST /v1/objects/{id}/append-markdown` belong
to stage 12 (`api-v2`), which is not part of this branch. The Markdown scenarios
compose the stage-4 converter (`/v1/markdown/to-blocks`) with the normal object
API instead, which is the same block tree a user would produce by pasting
Markdown into the editor.

## Data model

No object-format change. Two small schema/cleanup corrections were needed (see
"Bugs found & fixed"):

- the built-in `journal` type now declares the shared `tags` relation, so a
  daily note can be tagged like any other content type. `NewApp` already adds
  newly declared default properties to existing built-in types, so an existing
  workspace receives it on the next start; no migration or schema-version bump.
- permanent purge now deletes the object's `object_versions` rows in the same
  transaction as the trash payload and the reference cleanup. Version history
  remains server-side only and is never synced.

## Scenario catalog

| Test | User story | Features combined | Regressions guarded |
|---|---|---|---|
| `TestScenarioDailyNoteCaptureJourney` | Open today's note, mention a person and a project, tag it, paste a URL, find everything again. | Journal, aliases, capture, weblink reader, tags, blocks, query filters, search. | Journal tags become real relations; capture links the journal; reader content is searchable; alias search reports `matched_alias`. |
| `TestScenarioRecurringTaskJourney` | A weekly chore linked to a project, completed twice, plus an overdue task. | Recurring tasks, date-range filters, relations, occurrence log. | Next weekday math, copied context/links, occurrence log, date-window queries, catch-up from the due date, idempotent completion. |
| `TestScenarioMediaAttachDeleteRestoreExportJourney` | Attach a photo, delete the page by mistake, restore it, export the space. | Assets, media objects, image blocks, trash, backlinks, export. | Asset bytes survive trash; managed metadata survives PATCH; restore brings the backlink; exported bytes are identical and the `.md` references the asset path. |
| `TestScenarioDeleteRegretPurgeJourney` | Delete a person used by a page and a collection, restore, then purge. | Trash, collections, backlinks, search, version history. | References kept while trashed, hidden from reads, restored intact; purge cleans links and collection items; version snapshots survive restore and disappear on purge. |
| `TestScenarioTemplateAndJournalJourney` | Save a Meeting template, make it the type default, create a meeting, apply a daily-note template. | Templates, custom types, date variables, blocks, journal, convert. | Title/date resolution, deep copy with fresh block IDs, template filtering, journal default template, default cleanup after conversion. |
| `TestScenarioDuplicateMergeJourney` | Merge a duplicate book into the canonical one, then restore the duplicate. | Markdown converter, merge, trash, collections, tags, aliases. | Referrer rewrite and revision bump, block append, collection dedupe, trash and the documented degraded restore. |
| `TestScenarioConvertTypeJourney` | Turn an article into a book, mapping renamed properties. | Convert, property maps, tags, aliases, collections, backlinks. | No data loss for unmappable values, aliases travel, backlinks survive, incompatible collection membership is cleaned. |
| `TestScenarioVersionHistoryJourney` | Edit a note three times, restore an older version, check sync. | Version history, tags, backlinks, sync journal. | Snapshot list/word counts, restore as a new revision, history not truncated, snapshots never in a pull, tag relations restored. |
| `TestScenarioCollectionQueryExportJourney` | Curate five books, reorder/remove, query the list, export its CSV. | Collections, query filters, tags, aliases, export. | Order, `collection`/`linked_count`/`backlinked_includes`/`content_empty` composition, CSV columns and rows, filtered export. |
| `TestScenarioMarkdownRoundTripJourney` | Paste Markdown, append more, round-trip the blocks, export. | Markdown converter, blocks, export. | Headings/lists/code/math/wikilinks survive; canonical output is stable; export body matches after the documented wikilink degradation. |

## How

- **One test per story.** Each function is a linear narrative with numbered
  steps. A failure points at the step that broke, not at a table row.
- **Assert after every step.** The scenarios check revisions, links, property
  links, backlinks, trash state, search hits and exported bytes immediately
  after the action that should change them. This is what catches a stage that
  writes data but forgets to update the index or the relation table.
- **Deterministic dates.** Calendar stories use fixed dates (`2026-10-26`,
  `2026-10-31`, ...). The overdue catch-up test derives its dates from
  `time.Now()` only to express "three days late"; the assertion is the relative
  rule (due + 1 day), never a hard-coded today.
- **Offline capture.** `readerTestFetcher` keeps the production `safeURL`
  policy and only replaces the dialer, so the scenario exercises the real
  capture pipeline against an in-process `httptest` page.
- **Helper reuse.** The scenarios use the existing `makeApp`, `api`,
  `objectFrom`, `decodeBody`, `searchFor`, `queryObjects`, `versionSummaries`,
  `uploadFixture`, `exportEntries`, `csvTable` and `manifestFrom` helpers. The
  new `scenarios_test.go` only adds JSON/URL boilerplate helpers, never business
  logic.
- **No external network, no Docker.** `go test ./...` runs the whole catalog in
  about a second on a laptop.

## Bugs found & fixed

### 1. Tagging a daily note produced no tag relation

**Symptom.** The journal accepted `properties.tags`, returned it on read, but
the tag page (`GET /v1/objects?type_id=journal&linked_includes=<tag>`) and the
tag's backlinks did not include the note. The tag was inert data.

**Root cause.** `tags` is a per-type object relation: `validatePropertyValues`
and `recomputePropertyLinks` only derive property links for properties declared
by the type schema. The built-in `journal` type declared only `date`, so the
server stored the array without creating a relation. The desktop property
editor also inferred the key as text, not as a relation.

**Fix.** The journal default type now uses `withTags("date")`. `NewApp` already
appends newly declared default properties to existing built-in types, so the
schema is repaired on the next start without a migration. The daily-note
scenario is the regression test.

### 2. Converting an object left wrong-type items in collections

**Symptom.** A book in a reading-list collection was converted to a note. The
collection still listed the converted object under `item_type_id: "book"`, and
the next collection write (`POST/PUT/DELETE .../items`) returned 400 because
`validateCollection` re-read the stale item and rejected its type. The
collection became uneditable.

**Root cause.** `convert` changed `type_id` through the normal save path, which
recomputes property links for the new type but never revalidates collection
membership. Collections enforce "exactly one type" on every write, so the stale
item poisoned every later write.

**Fix.** `convertCollectionCleanup` runs in the conversion transaction. It walks
the object's live backlinks, finds collections whose `items` contain the object
and whose `item_type_id` no longer matches, removes the id with
`removePropertyReference` (preserving the order of the remaining items),
recomputes the collection revision and journals the update. The conversion and
the collection cleanup commit together. Regression tests:
`TestConvertRemovesObjectFromIncompatibleCollections` and the convert scenario.

### 3. Converting a template left a dangling type default

**Symptom.** A template was set as its type's `default_template_id`, then
converted to another type. `GET /v1/types` still advertised the old type's
default, and the journal default-template flow silently fell back to a plain
note because the reference no longer resolved to a template of that type.

**Root cause.** `PATCH`, deletion and sync push call `templateDefaultCleanup`
when a template stops being a template of its type. `convert` is also a type
change but bypassed that cleanup.

**Fix.** The convert handler appends `templateDefaultCleanup(before, converted)`
to the same transaction. Regression test:
`TestConvertTemplateClearsOldTypeDefault`, plus the template scenario.

### 4. Permanent purge left version snapshots behind

**Symptom.** After deleting and purging an object that had been edited, the
`object_versions` rows stayed in SQLite. The API could not reach them (the
object was gone), but the deleted content remained encrypted on disk and a
recreated fixed-id journal could inherit old history.

**Root cause.** The version-history stage added the side table and documented
that purge must delete its rows, but the trash stage's purge path only deleted
`records`/`blobs`. The two stages were merged without composing that cleanup.

**Fix.** `purgeObjectData` adds a `versions` entry to the delete list, and
`PutBatchBlobs` handles it inside the same transaction (`DELETE FROM
object_versions WHERE object_id=?`). It is not a sync kind and does not touch
the index. Regression test: `TestVersionHistoryPurgeDeletesSnapshots`, plus the
delete/regret scenario.

### Documented behavior, not a bug

- **Export degrades unresolved wikilinks.** `[[Wiki Link]]` has no object id in
  this stage (title resolution is a follow-up), so the exporter writes the plain
  title instead of a broken `lumen://` link. The Markdown scenario asserts the
  canonical body and the documented export rewrite separately.
- **Trash keeps attachments.** Deleting a page does not delete its asset bytes;
  a restore must not lose the photo. The media scenario asserts this and the
  export path; permanent purge still removes exclusive attachments.

## Contracts

`contracts/SYNC.md` gained two clarifications that match the fixes:

- **Convert**: collection membership is the one inbound relation that is not
  unchanged. An object that no longer matches a collection's `item_type_id` is
  removed from that collection in the same transaction, because collections
  hold exactly one type and a stale item would make every later write fail.
- **Version history**: permanent purge deletes the object's `object_versions`
  rows in the same transaction as the trash payload, so a purged object leaves
  no history behind.

The journal `tags` declaration is additive and is not enumerated in the
contract; it follows the existing "standard content types include tags" rule.

## Testing

```sh
cd server
go test ./...                 # whole suite, including the ten scenarios
go test -race -count=1 ./...  # race detector, no test cache
go vet ./...
gofmt -l .                    # must print nothing
```

Run one scenario by name:

```sh
cd server
go test -run TestScenarioDailyNoteCaptureJourney -count=1 -v
go test -run 'TestScenario.*Journey' -count=1 -v
```

The full guide, including the scenario table, the rules for adding a scenario
and the disposable-server curl flow, is `docs/guides/BACKEND-TESTING.md`.

## Follow-ups

- **Restored collections can hold a stale item.** If a collection is trashed,
  one of its items is converted, and the collection is restored, the restored
  payload is a frozen snapshot and still contains the old item. `GET
  .../items` would show it and the next write would reject it. A future restore
  pass could filter items whose type no longer matches, or the read path could
  skip them (the same defense-in-depth rule already used for missing/trashed
  items).
- **Converting a journal is not guarded.** The desktop excludes the journal
  from type pickers, but the API accepts the conversion; `GET /v1/journal/{date}`
  would then return an object of another type for that fixed id. A guard
  (`400` for `type_id == "journal"`) or a type check in `ensureJournal` is a
  small follow-up.
- **`POST /v1/objects/markdown` and append-markdown** belong to stage 12
  (`api-v2`). The scenarios use the stage-4 converter until that stage lands.
- **Wikilink title resolution** is still a follow-up from the Markdown stage;
  the export scenario documents the resulting plain-text degradation.
- **The journal tags migration is additive but strict.** An old journal that
  stored `tags` as free-text strings will now fail validation on the next save
  (tags are object relations). The desktop's relation picker makes this
  visible; a one-time normalization could rewrite legacy strings to tag
  objects, but that needs a product decision about creating tags from text.

## Client follow-ups

- **Desktop**: show the tag property on daily notes (the schema now declares
  it), refresh the collection view after a type conversion, and refresh the
  type settings after converting a template (its default reference is cleared).
- **Desktop**: offer "purge" with a clear warning that version history is
  deleted too; the trash UI can keep the existing restore flow unchanged.
- **Mobile**: no protocol change. A converted object arrives with a new
  `type_id`; a collection that lost an item arrives as a normal object put.
  Purged history never reached the phone in the first place.
- **Extension**: no change; captures already link the journal and store reader
  content.
