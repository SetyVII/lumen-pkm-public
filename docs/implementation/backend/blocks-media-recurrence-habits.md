# Blocks, media, recurrence and habits

Implemented locally on 2026-11-01. This is a backend/API increment, not a
release or a deployment. It uses the existing encrypted store and sync journal.
The personal service and its data were not restarted or migrated.

## Blocks and references

- Read an object's blocks with `GET /v1/objects/{id}/blocks`.
- Add with POST on that path, replace/delete with PATCH/DELETE on
  `/v1/objects/{id}/blocks/{block_id}`. Supply `base_revision` and, except
  for DELETE, `block`. POST optionally accepts a zero-based `position`.
- Mutations retain the source object's ID, version history and sync semantics.
  Stale revisions return 409. New blocks receive fresh IDs; replacement keeps
  the existing root ID. Nested groups/columns retain their descendants and
  extension fields, and the plain-text projection includes nested content.
- Search with `GET /v1/blocks?q=...&limit=50`; optionally restrict `object_id`.
  Resolve a source using `GET /v1/blocks/{block_id}`. The response includes
  `object_id`, `object_title`, `revision` and the current raw `block`.
- Link runs use `lumen://block/<id>`. A run in an `embed` block describes a
  transclusion: the client resolves the source and edits that source with its
  revision, rather than copying its text into the reference object.
- `GET /v1/blocks/{block_id}/references` returns incoming source-block rows,
  an embedded flag and a count (one per referring block). Cycles are never
  recursively expanded. Deleted/trashed sources return 404; restore makes
  them resolvable again. Legacy duplicate IDs return 409; an `object_id`
  can disambiguate duplicates belonging to different objects.

Reads scan encrypted object payloads on demand; no second plaintext content
index is introduced. Search is a bounded result list (1-200), not a paginated
or fuzzy search service. IDs below 32 container levels are supported. Only
canonical `properties.blocks` participate; HTML-only notes need conversion.
Block edits derive `content` from that tree and discard stale `rich_text`.
If the old plain-text body contains additional text absent from the tree,
the API rejects the edit with 409 until the representations are reconciled.
Template copies mint fresh IDs for nested blocks too.
Markdown retains internal block URIs; full export does not yet turn them into
portable cross-file block anchors. Nested text/media are preserved on export.

## More media

New small/chunked uploads and explicit promotions create reusable `image`,
`pdf`, `audio`, `video` or `file` objects. Mobile sync accepts the same formats
when `create_media_object` is requested. Classification checks the content
signature first (including FLAC/Ogg), then audio/video or generic MIME hints
when the signature is unknown. It is classification, not full codec validation.
A misleading PNG filename on a text payload produces a file, never an image.
Generic files download as attachments with `nosniff` and the existing CSP.

Encryption, authentication and limits remain the existing ones: 50 MB for
multipart, 500 MB for chunked uploads, 20 MB for mobile sync. Old attachments
are not mass-promoted. The original note can be purged without deleting a
promoted medium; purging the medium cleans its blob and stale references,
using the existing image/PDF ownership rules for all five types.
No player, transcoding, thumbnails or media duration extraction is added.

## Recurrence

The four original rules remain valid. Additions:

| Mode | Next date strictly after the current due date |
|---|---|
| `yearly` | Same month/day next year, clamped for February 29. |
| `weekdays` | Next Monday-Friday; no public-holiday inference. |
| `last_day` | Last calendar day of this month, or next month if already reached. |
| `last_weekday` | Last Monday-Friday of this month, or the next eligible month. |

POST `/v1/tasks/{id}/skip` or `/excuse` resolves a recurring occurrence
without setting it done. It records `occurrence_outcome`, `resolved_at` and
an occurrence-log entry, then creates one clean successor in the same
transaction. Repeated/concurrent complete/skip/excuse requests cannot fork
that occurrence. As before, due date anchors catch-up; a late action does not
silently discard missed dates. PATCH reopening preserves the successor guard.
The rules are available through the existing MCP `create_task` schema too.

## Habits

A built-in `habit` type uses ordinary object CRUD, tags, history, sync,
export and trash. Example properties:

```json
{
  "habit_start": "2026-10-26",
  "habit_schedule": [1, 2, 3, 4, 5],
  "habit_target": 10,
  "habit_entries": {"2026-10-26": 10}
}
```

Use target 1 for a yes/no habit, or a higher number for pages, minutes, etc.
ISO weekdays are 1=Monday through 7=Sunday. Defaults are all weekdays,
target 1 and today's UTC date. Explicit dates are civil dates, not instants.

PUT `/v1/habits/{id}/entries/YYYY-MM-DD` accepts `value` and mandatory
`base_revision`; DELETE on that path accepts `base_revision`. Values replace
rather than increment. Unchanged values do not create revisions. A stale
retry returns 409 and cannot overwrite a newer edit. Counts must be finite,
nonnegative and at most 1e9. Dates must be scheduled and at/after the start.
There is a 10000-entry cap; changing a schedule/start to exclude existing
entries is rejected until those entries are explicitly corrected.

GET `/v1/habits/{id}/stats?as_of=YYYY-MM-DD` returns scheduled/completed days,
current/best streak and total count. Future entries are excluded. Unscheduled
days do not break streaks; an unfinished as-of day is still allowed to finish,
so yesterday's streak is retained. An earlier missed scheduled day breaks it.
Statistics cover at most 100 years. The API accepts explicitly dated records;
it does not infer the client's timezone or prohibit planned future entries.

## Verification and client follow-ups

Feature and composition regressions live in `server/backend_power_test.go`
and `server/media_types_test.go`, alongside the updated media/recurrence/
chunked suites. They cover live references, nested IDs, stale writes, scopes,
trash/restore, restart/encryption, recurrence calendar boundaries, concurrent
outcomes, mobile uploads, byte preservation and final-owner blob cleanup.

Desktop/mobile still need controls for the new rules and habits, transclusion
rendering and media players. Existing generic object/API support alone does
not demonstrate those user journeys in either app.

Validation on 2026-11-01: full Go suite with `-race -count=1` passed;
final focused recurrence/concurrency regressions, `go vet`, formatting/diff
checks and the server build passed. See `docs/tracking/VERIFICATION.md` for
scope. The test server is in-process and backed by temporary encrypted SQLite.
