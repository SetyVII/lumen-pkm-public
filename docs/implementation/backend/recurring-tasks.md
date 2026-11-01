# Recurring tasks (backend parity stage 8)

Status: implemented on `backend/recurring-tasks` (2026-10-31).
Capacities reference:
<https://docs.capacities.io/reference/task-management#recurring-tasks>.

## What & why

Capacities lets a task carry a recurrence rule; completing the task records the
occurrence and schedules the next one, which is how recurring chores, weekly
reviews and monthly bills stay out of the inbox until they are due. Lumen had
no recurrence semantics at all: the desktop only parsed `!! priority` and dates
client-side, and the server treated every task as a one-off object.

This stage adds the server-side recurrence engine: an optional `recurrence`
property on task objects, strict validation, and
`POST /v1/tasks/{id}/complete`, which closes the current occurrence and creates
the next one with a calendar-correct due date. It is the backend half of
`PLAN-PARIDAD-CAPACITIES.md` §F2.6; the desktop and mobile UIs are follow-ups.

Capacities keeps one task object and moves its date forward. The parity program
specifies a **new object per occurrence** for Lumen, and this stage follows that
spec: every occurrence keeps its own revision, date and notes, the completed
occurrence remains visible in "Completed" views, and the next occurrence reaches
mobile clients through the normal sync journal instead of a special in-place
date mutation. The trade-offs are more objects and a title suffix (see below).

## API

### `POST /v1/tasks/{id}/complete`

Request: no body.

Response `200` (recurring task):

```json
{
  "task": {
    "id": "8c1f...",
    "type_id": "task",
    "title": "Water plants",
    "content": "Use the blue can",
    "properties": {
      "status": "done",
      "completed_at": "2026-10-31T09:12:44.123456789Z",
      "priority": 2,
      "due_date": "2026-10-30",
      "recurrence": {"mode": "daily"},
      "occurrences": [
        {
          "completed_at": "2026-10-31T09:12:44.123456789Z",
          "scheduled_for": "2026-10-30",
          "next_due": "2026-10-31",
          "next_occurrence_id": "1d9a..."
        }
      ],
      "next_occurrence_id": "1d9a..."
    },
    "links": [],
    "revision": 2
  },
  "next": {
    "id": "1d9a...",
    "type_id": "task",
    "title": "Water plants (2026-10-31)",
    "content": "",
    "properties": {
      "status": "todo",
      "priority": 2,
      "due_date": "2026-10-31",
      "recurrence": {"mode": "daily"},
      "previous_occurrence_id": "8c1f..."
    },
    "links": [],
    "revision": 1
  },
  "next_date": "2026-10-31",
  "created": true
}
```

Response `200` (non-recurring task, or an already completed task):

```json
{
  "task": {"id": "8c1f...", "properties": {"status": "done"}},
  "next": null,
  "created": false
}
```

`next_date` is omitted when no occurrence was scheduled. `created` is `false`
when the call did not create a new object.

Errors: `404` unknown id, `400` non-task object, invalid recurrence, missing or
invalid `due_date` on a recurring task, or a malformed `occurrences` value. A
failed completion leaves the task untouched (all validation happens before the
single write).

Completing a recurring task by PATCHing `properties.status` to `done` is a
manual completion: it does not spawn the next occurrence. Clients must call this
endpoint to advance the series.

## Data model

New optional property on `task` objects:

```json
{
  "recurrence": {
    "mode": "weekly",
    "weekdays": [1, 3]
  }
}
```

| Field | Type | Rules |
|---|---|---|
| `mode` | string | `daily`, `weekly`, `monthly` or `every_n_days` (lowercase, required) |
| `n` | integer | Only for `every_n_days`; 1..3660 |
| `weekdays` | array of integers | Only for `weekly`; 1=Monday..7=Sunday, no repeats, at most 7 |

Absent or `null` means a normal task. The property is schemaless (the type
schema only types some properties); validation is implemented in
`validateTaskRecurrence`, which `validateObject` calls on every write. The
server also canonicalizes the stored value: `weekdays` is sorted ascending and
`n` is stored as a JSON number. `recurrence` is reserved for tasks; a non-task
object with a non-null `recurrence` is rejected instead of silently ignoring a
rule the engine would never run.

A recurring task must also have a valid `due_date` (`YYYY-MM-DD`). The date is
required at write time because the next occurrence is computed from it; a rule
without a schedule would be uncompletable. Clients can still create a task
first and add the recurrence together with the date in a later PATCH.

Server-managed companion properties (reserved names, not object links):

| Property | On | Meaning |
|---|---|---|
| `completed_at` | completed task | UTC timestamp of the completion |
| `occurrences` | completed task | List of completion log entries (newest last, capped at 1000) |
| `next_occurrence_id` | completed task | Id of the spawned next occurrence |
| `previous_occurrence_id` | next occurrence | Id of the completed occurrence |

Occurrence log entry:

```json
{
  "completed_at": "2026-10-31T09:12:44.123456789Z",
  "scheduled_for": "2026-10-30",
  "next_due": "2026-10-31",
  "next_occurrence_id": "1d9a..."
}
```

No migration and no index change: the recurrence lives inside the encrypted
object payload, and the in-memory index keeps only metadata as before. The
completed task and the new occurrence are committed in one `PutBatch`
transaction, so the mobile journal receives two `object` puts atomically.

## How

### Which property is flipped

`properties.status` is set to `"done"` and `properties.completed_at` records
the completion timestamp. The desktop already treats `"done"` and `"completed"`
(case-insensitive) as closed, and writes `"todo"` on creation, so the endpoint
reuses that existing convention instead of introducing a second done flag. The
next occurrence starts at `"todo"` with no `completed_at`.

### Date math

All math is pure calendar arithmetic on the parsed `YYYY-MM-DD` due date
(`time.Parse` / `time.Date` in UTC). `time.Now()` is only used for the
`completed_at` timestamp and `updated_at`, never to pick the next date.

| Mode | Rule | Example |
|---|---|---|
| `daily` | due date + 1 day | `2026-10-31` -> `2026-11-01` |
| `weekly` (no weekdays) | due date + 7 days | `2026-10-28` -> `2026-11-04` |
| `weekly` + weekdays | next listed weekday **strictly after** the due date | due Mon, `[1,3]` -> Wed; due Wed, `[1,3]` -> next Mon |
| `monthly` | same calendar day next month, clamped to month end | `2026-01-31` -> `2026-02-28`; `2028-01-31` -> `2028-02-29` |
| `every_n_days` | due date + `n` days | `2026-10-30`, n=3 -> `2026-11-02` |

Notes:

- Weekday numbers are ISO: 1=Monday ... 7=Sunday. A listed weekday equal to the
  due date's weekday advances a full week (strictly after).
- `monthly` is implemented manually because `time.AddDate` normalizes
  `Jan 31 + 1 month` into `Mar 3`. The clamp is applied to each occurrence's own
  due date, so a clamped series drifts (`Jan 31 -> Feb 28 -> Mar 28`). This is
  the documented rule; anchoring to the original day-of-month would need an
  extra property and is left as a follow-up.
- Leap years are handled by the calendar itself: `2028-02-28 + 1 day` is
  `2028-02-29`, `2028-02-29 + 1 month` is `2028-03-29`, and
  `2028-01-31 + 1 month` is `2028-02-29`.
- There are no timezones beyond the server's existing behavior: dates are
  calendar dates, and timestamps keep the UTC `now()` format used by the rest
  of the store.

### Overdue catch-up policy

The next date is always computed from the task's due date, not from the
completion time. A daily task due `2026-10-01` and completed on `2026-10-31`
schedules `2026-10-02`, which is still overdue. This is deliberate:

- the series keeps its original cadence instead of silently shifting every
  future occurrence;
- no missed occurrence is skipped without the user seeing it;
- the rule is deterministic and testable, independent of when the request
  arrives.

The cost is that a very overdue task needs one completion per missed period.
Capacities offers an explicit catch-up choice; a `catch_up` option (advance to
"now" or "before now") is a follow-up, not part of this stage.

### Occurrence log: list property, not a text block

`properties.occurrences` is chosen over appending a text block to `content`:

- **Contract-legal**: `properties` is a free-form map; the server preserves
  unknown properties and only types the ones declared in the type schema. A
  list property is therefore valid without a schema change.
- **Cheaper and safer**: appending to `content` would rewrite the note body and
  require invalidating/rewriting `blocks`/`rich_text` (the richer content
  representations). A list property leaves the completed occurrence's notes
  untouched and gives clients structured data (timestamp, scheduled date, next
  date, next id) to render a log or compute streaks later.
- **Bounded**: the log keeps the most recent 1000 entries and prunes the oldest,
  so a long-running daily task cannot grow the encrypted record without limit.

The log is append-only per occurrence object: each occurrence is completed once,
so it normally holds one entry. Reopening and re-completing an already processed
occurrence does not append a second entry (see the guard below).

### Double-spawn guard

Three layers protect against two next occurrences for one completion:

1. **Status check**: the endpoint only spawns on a not-done -> done transition.
   An already completed task returns `200` unchanged, so a retried request or a
   double click is a no-op.
2. **Recorded child**: `next_occurrence_id` on the completed task points to the
   spawned occurrence. If the task is reopened and completed again, the endpoint
   marks the task done and returns the recorded child without spawning or
   logging again. The pointer itself is the guard: even if that child was
   deleted, the parent does not fork the series. To start a new chain, the
   client creates a new task or removes the recurrence first.
3. **Atomic transaction + `writeMu`**: the completed task, its log entry and the
   new occurrence are written in one SQLite transaction while the app's write
   mutex is held. Concurrent completes serialize; a crash cannot leave a task
   marked done without its child or vice versa.

Deleting the completed task leaves the next occurrence intact; the lineage
property simply points at a tombstone and is not an object link.

### Idempotency choice: 200, not 409

Completing an already completed non-recurring task returns `200` with the object
unchanged. Completion is a desired end state, not a conflicting edit; `409`
would force every client to special-case retries and double clicks for no
benefit. The task's `revision` is not bumped when nothing changes.

### Copy rules: definition vs instance state

The next occurrence copies the task **definition** and resets **instance
state**:

| Copied | Reset / not copied |
|---|---|
| title (base + occurrence date), `priority`, `context_id`, typed `context`/`tags` relations, `links`, `recurrence`, custom definition properties, `summary`, `time` | `status` (`"todo"`), `due_date` (next date), `completed_at`, `occurrences`, `next_occurrence_id`, `previous_occurrence_id` (set to parent), `content`, `blocks`, `rich_text`, attachments (`assets`, `image_asset_id`, `asset_id`), `content_checksum`, `mobile_conflict` |

`property_links` is derived state: the child's properties are revalidated, so
typed relations are recomputed instead of copied. Notes and attachments stay
with the completed occurrence, matching Capacities' "notes remain in the task"
behavior for the occurrence that was just closed.

### Title uniqueness

Lumen enforces globally unique titles (the in-memory index maps one title to one
id). The next occurrence is therefore titled `<base> (YYYY-MM-DD)`, where
`<base>` is the parent title with a trailing `(YYYY-MM-DD)` or
`(YYYY-MM-DD #N)` suffix removed, so a chain keeps a stable base
(`Water plants (2026-10-30)` -> `Water plants (2026-10-31)`). If an unrelated
object already owns that title, the discriminator goes inside the parentheses:
`<base> (YYYY-MM-DD #2)`, `#3`... The title is truncated on a UTF-8 boundary to
the 1000-byte limit enforced by `validateObject`.

### Validation

- `mode` is required and must be one of the four lowercase values.
- `n` is required only for `every_n_days`, must be an integer in 1..3660, and
  is rejected for the other modes.
- `weekdays` is allowed only for `weekly`; entries must be 1..7, distinct, and
  at most seven. An explicit empty array is rejected; omit the field for plain
  weekly.
- Unknown fields, non-object values, non-integer `n`, non-integer weekdays and
  fractional `n` are rejected.
- A recurring task must have a valid `due_date`; the rule and the date are
  validated together on every write.

### rrule-go vs hand-rolled date math

`docs/tracking/REUSE-CATALOG.md` pre-approves
`github.com/teambition/rrule-go` (MIT), an RFC 5545 implementation. This stage
deliberately uses hand-rolled date math instead:

- **Monthly clamping is not RFC 5545.** `MONTHLY;BYMONTHDAY=31` skips months
  without a 31st; the stage spec requires clamping to month end. rrule-go would
  need a special case for one of the four modes, so the library would not remove
  the custom logic.
- **The mode set is tiny.** Daily, every-N-days, weekly with an optional weekday
  list and monthly clamp are about 80 lines of pure date arithmetic, each rule
  covered by table tests. `rule.After(due, false)` would handle three modes
  correctly, but mixing it with a hand-rolled monthly rule creates two mental
  models and more code than the direct implementation.
- **No new dependency.** The server keeps its small `go.mod` and avoids
  `go.sum` churn and supply-chain surface for a feature that does not need a
  general recurrence engine.
- **No timezone surprises.** The math operates on calendar dates only; a
  general RFC 5545 engine brings DTSTART/timezone semantics this stage does not
  need.

If a later stage adds yearly rules, ordinal weekdays ("first Monday"), end
dates or skip/excuse semantics, rrule-go becomes the better trade-off and
should be adopted then.

## Contracts

The following section was added to `contracts/SYNC.md`:

````markdown
## Recurring tasks (backend parity stage 8)

*English section added by the backend parity program; the rest of this file
predates it.*

A task object may carry an optional `properties.recurrence` object. Absent or
`null` means a normal task, so every pre-existing task stays valid without a
migration. The server validates the rule strictly on every task write
(`POST/PATCH /v1/objects`, sync push) and rejects invalid rules with 400.
`recurrence` is reserved for `task` objects: a non-task object with a non-null
`recurrence` is rejected instead of silently ignoring the rule.

| Field | Type | Rules |
|---|---|---|
| `mode` | string | `daily`, `weekly`, `monthly` or `every_n_days` (lowercase, required) |
| `n` | integer | Only for `every_n_days`; 1..3660 |
| `weekdays` | array of integers | Only for `weekly`; 1=Monday..7=Sunday, no repeats, at most 7 |

- A recurring task must also have a valid `due_date` (`YYYY-MM-DD`). The date is
  required because the next occurrence is computed from it; a rule without a
  schedule would be uncompletable.
- `weekly` without `weekdays` advances seven days. An explicit empty
  `weekdays` array is rejected.
- `weekly` with `weekdays` picks the next listed weekday strictly after the due
  date; a listed weekday equal to the due date advances a full week. The server
  stores `weekdays` sorted ascending.
- `monthly` keeps the same calendar day in the next month and clamps to the
  last day when the target month is shorter (`2026-01-31` -> `2026-02-28`,
  `2028-01-31` -> `2028-02-29`). Clamping is applied to each occurrence's own
  due date, so a clamped series can drift (`Jan 31 -> Feb 28 -> Mar 28`).
- `every_n_days` adds `n` calendar days.
- Dates are calendar dates (`YYYY-MM-DD`), not instants: no timezone is involved
  in the math. Timestamps (`completed_at`, `created_at`) keep the server's
  existing UTC format.

### `POST /v1/tasks/{id}/complete`

Marks the task done by setting `properties.status` to `"done"` and records
`properties.completed_at`. The desktop already treats `"done"` and `"completed"`
as closed. When the task has a `recurrence`, the server also creates the next
occurrence as a new object in the same SQLite transaction.

Request: no body. Response `200`:

```json
{
  "task": { "...": "the completed task, with occurrences and next_occurrence_id" },
  "next": { "...": "the new occurrence, or null" },
  "next_date": "2026-11-01",
  "created": true
}
```

- `next_date` is omitted for non-recurring tasks.
- `created` is `false` when the call did not spawn a new occurrence.
- The next due date is computed from the task's `due_date`, never from the
  completion time. An overdue task therefore advances exactly one period per
  completion and may still be overdue (catch-up policy: the series keeps its
  cadence and no occurrence is silently skipped).
- The next occurrence copies the task definition: title (with the occurrence
  date appended, see below), `priority`, `context_id`, typed relations
  (`links`/`property_links`), `tags`, `recurrence` and other definition
  properties. It starts with `status: "todo"`, the new `due_date`, no content,
  no `blocks`/`rich_text`, no attachments, no `occurrences`, no
  `completed_at` and no `next_occurrence_id`.
- Lumen keeps titles unique per workspace, so the next occurrence is titled
  `<base> (YYYY-MM-DD)`, where `<base>` is the parent title without a trailing
  `(YYYY-MM-DD)` or `(YYYY-MM-DD #N)` suffix. If that title is taken, the
  discriminator goes inside the parentheses: `<base> (YYYY-MM-DD #2)`, `#3`...
- The completed task keeps its content and notes. It receives one entry in
  `properties.occurrences` (a list property, not a text block, so the note body
  is untouched):

```json
{
  "completed_at": "2026-10-31T12:34:56.000000000Z",
  "scheduled_for": "2026-10-30",
  "next_due": "2026-10-31",
  "next_occurrence_id": "<next occurrence id>"
}
```

- `properties.next_occurrence_id` on the completed task remembers the spawned
  occurrence. `properties.previous_occurrence_id` on the new occurrence points
  back to the completed one. Both are server-managed metadata, not object links,
  so they do not create backlinks.
- The log keeps the most recent 1000 entries; older entries are pruned so a
  long-running daily task cannot grow the encrypted record without bound.
- Idempotency: completing an already completed task returns `200` with the task
  unchanged and `created: false` (never 409); a retried request cannot spawn a
  second occurrence. A task reopened after completion and completed again marks
  itself done again but does not spawn or log a second occurrence. The
  `next_occurrence_id` pointer is the guard: even if the recorded child was
  deleted, the parent does not fork the series.
- Completing a recurring task by PATCHing `properties.status` to `done` is a
  manual completion: it does not spawn the next occurrence. Clients must call
  this endpoint to advance the series.
- Non-task objects return 400; unknown ids return 404.
- The completed task and the new occurrence are committed together and produce
  two `object` entries in the mobile sync journal.
````

No fixture changes were needed: the recurrence format is exercised by Go tests
and the existing `contracts/fixtures/ids.json` has no task entry.

## Testing

`cd server && go test ./...`, `go test -race ./...`, `go vet ./...` and
`gofmt -l .` are clean.

New tests:

- `recurrence_test.go` — table-driven next-date math for every mode: daily
  across year/leap boundaries, every-N-days, weekly with and without weekday
  lists (strictly-after and wrap-around), monthly across year, month-end clamps
  (`Jan 31` in leap and non-leap years, 30-day months) and leap-day monthly;
  invalid due dates; strict validation (bad/uppercase mode, `n <= 0`, fractional
  and oversized `n`, `n` on other modes, weekday 0/8/duplicates, empty
  weekdays, weekdays on other modes); property parsing (absent/null, canonical
  sorting, unknown fields, wrong JSON types); occurrence-title suffix stripping
  and truncation; status semantics (`done`/`completed`); bounded occurrence log.
- `routes_recurring_tasks_test.go` — endpoint behavior: non-recurring
  completion and idempotent repeat; non-task/missing ids; each mode through the
  API; child copies title/priority/context/tags/links/recurrence and resets
  status/content/attachments/completion state; occurrence log entry; chain
  completion; double completion and reopen/re-complete do not spawn or log
  twice; the guard holds even when the recorded child was deleted; overdue task
  advances from the due date; monthly clamp and post-clamp drift; missing due
  date is rejected at write time; create/PATCH validation; recurrence rejected
  on non-task objects; title collision fallback; sync pull includes both
  objects; sync push validates recurrence.

The test suite uses the in-process `httptest` app with a temporary encrypted
SQLite file; no Docker is required. `makeApp` runs `Fsck` after each test, which
also checks that the new objects have journal entries and consistent backlinks.

Manual verification (disposable server, never the personal install):

```sh
# 1. Start sandbox/compose.test.yaml (port 8879) with its own secrets.
# 2. Create a recurring task:
curl -sS -H "Authorization: Bearer $TOKEN" -H 'Content-Type: application/json' \
  -d '{"type_id":"task","title":"Water plants","properties":{"status":"todo","due_date":"2026-10-30","recurrence":{"mode":"daily"}}}' \
  http://127.0.0.1:8879/v1/objects
# 3. Complete it and inspect task/next/next_date/created:
curl -sS -X POST -H "Authorization: Bearer $TOKEN" \
  http://127.0.0.1:8879/v1/tasks/<id>/complete
# 4. Repeat step 3: created must be false and next must keep the same id.
```

## Client follow-ups

- **Desktop**: replace the direct `status = done` PATCH for tasks with
  `POST /v1/tasks/{id}/complete` (otherwise no next occurrence is spawned);
  add a recurrence picker (daily/weekly/monthly/every N days + weekday chips);
  hide `recurrence`, `occurrences`, `next_occurrence_id`,
  `previous_occurrence_id` and `completed_at` from the generic property editor;
  render the occurrence log and the next occurrence.
- **Mobile**: call the same endpoint from the task checkbox/notification action;
  the next occurrence arrives as a new object through the normal sync pull, so
  no special merge code is needed. Hide the reserved properties in the task
  editor.
- **Extension**: no task UI today; if it creates tasks, it may send
  `properties.recurrence` with the documented shape.
- **Queries**: an "is recurring" filter belongs to the query-filters stage; the
  property is already present on every recurring task.

## Follow-ups

- Catch-up option (`catch_up=now|before_now`) to absorb missed periods in one
  call, matching Capacities' catch-up prompt.
- Yearly mode, recurrence end date, skip/excuse and streak/heatmap stats are
  outside the scoped subset.
- Monthly clamping drift (`Jan 31 -> Feb 28 -> Mar 28`) could be anchored to the
  original day-of-month with an extra property.
- The occurrence log is capped at 1000 entries; a side table would preserve full
  history if stats need it.
- Desktop/mobile UI and the `CHANGELOG.md`/`VERIFICATION.md` program entries are
  handled by the integration stage.
