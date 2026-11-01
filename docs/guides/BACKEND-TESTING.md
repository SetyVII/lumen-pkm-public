# Backend testing guide

This guide explains how to run the Lumen Go server test suite, what the
end-to-end scenarios cover, how to add another one, and how to verify the API
manually against the disposable sandbox. It complements
`docs/implementation/backend/user-flows.md` (the stage that introduced the
scenario catalog) and `contracts/SYNC.md` (the behavior under test).

The suite is offline and deterministic. It uses an in-process `httptest`
handler with a temporary encrypted SQLite file; no Docker, no external service
and no network access are required. The only HTTP fixture is an in-process
`httptest.Server` wired through the production SSRF-safe fetcher.

## Quick start

```sh
cd server

go test ./...                 # whole suite
go test -race -count=1 ./...  # race detector, bypass the test cache
go vet ./...                  # static checks
gofmt -l .                    # must print nothing
go build -o /tmp/lumen-server .
```

`-count=1` is useful when you want to be sure a cached result is not hiding a
change. `-race` is the release gate; it is slower but still a few seconds.

### Run one test or one family

```sh
# One scenario by exact name.
go test -run TestScenarioDailyNoteCaptureJourney -count=1 -v

# Every user-journey scenario.
go test -run 'TestScenario.*Journey' -count=1 -v

# One feature file (Go test names are package-wide, so use a pattern).
go test -run 'TestCollection|TestConvert' -count=1 -v

# Everything with verbose output, useful when a scenario fails mid-story.
go test -v ./...
```

A failing scenario prints the failing step and the HTTP body of the request
that broke, for example:

```
scenarios_daily_test.go:139: tag page missed the journal: []
```

That line names the file, the assertion and the state, so you can jump directly
to the step.

## What the suite covers

The package has two complementary layers:

| Layer | Files | Purpose |
|---|---|---|
| Feature tests | `*_test.go` for each stage (`trash_test.go`, `templates_test.go`, `query_test.go`, ...) | One feature at a time: validation, edge cases, storage, sync. |
| User-flow scenarios | `scenarios_*.go` | Several features in one story, asserting the state after every step. |

The scenarios are the composition layer. They do not replace the feature
tests; they catch the bugs that appear when two correct features meet (a tag
that is stored but not related, a collection that becomes uneditable after a
type change, a purge that leaves history behind).

### Scenario catalog

| Scenario | User story | Features combined | Guards |
|---|---|---|---|
| `TestScenarioDailyNoteCaptureJourney` | Open today's note, mention a person and a project, tag it, paste a URL, find everything again. | Journal, aliases, capture, weblink reader, tags, blocks, query filters, search. | Journal tags become relations; capture links the journal; reader content is searchable; alias search reports `matched_alias`. |
| `TestScenarioRecurringTaskJourney` | A weekly chore linked to a project, completed twice, plus an overdue task. | Recurring tasks, date-range filters, relations, occurrence log. | Next weekday math, copied context/links, occurrence log, date-window queries, catch-up from the due date, idempotent completion. |
| `TestScenarioMediaAttachDeleteRestoreExportJourney` | Attach a photo, delete the page by mistake, restore it, export the space. | Assets, media objects, image blocks, trash, backlinks, export. | Asset bytes survive trash; managed metadata survives PATCH; restore brings the backlink; exported bytes are identical and the `.md` references the asset path. |
| `TestScenarioDeleteRegretPurgeJourney` | Delete a person used by a page and a collection, restore, then purge. | Trash, collections, backlinks, search, version history. | References kept while trashed, hidden from reads, restored intact; purge cleans links and collection items; version snapshots survive restore and disappear on purge. |
| `TestScenarioTemplateAndJournalJourney` | Save a Meeting template, make it the type default, create a meeting, apply a daily-note template. | Templates, custom types, date variables, blocks, journal, convert. | Title/date resolution, deep copy with fresh block IDs, template filtering, journal default template, default cleanup after conversion. |
| `TestScenarioDuplicateMergeJourney` | Merge a duplicate book into the canonical one, then restore the duplicate. | Markdown converter, merge, trash, collections, tags, aliases. | Referrer rewrite and revision bump, block append, collection dedupe, trash and the documented degraded restore. |
| `TestScenarioConvertTypeJourney` | Turn an article into a book, mapping renamed properties. | Convert, property maps, tags, aliases, collections, backlinks. | No data loss for unmappable values, aliases travel, backlinks survive, incompatible collection membership is cleaned. |
| `TestScenarioVersionHistoryJourney` | Edit a note three times, restore an older version, check sync. | Version history, tags, backlinks, sync journal. | Snapshot list/word counts, restore as a new revision, history not truncated, snapshots never in a pull, tag relations restored. |
| `TestScenarioCollectionQueryExportJourney` | Curate five books, reorder/remove, query the list, export its CSV. | Collections, query filters, tags, aliases, export. | Order, `collection`/`linked_count`/`backlinked_includes`/`content_empty` composition, CSV columns and rows, filtered export. |
| `TestScenarioMarkdownRoundTripJourney` | Paste Markdown, append more, round-trip the blocks, export. | Markdown converter, blocks, export. | Headings/lists/code/math/wikilinks survive; canonical output is stable; export body matches after the documented wikilink degradation. |

The feature tests remain the place for exhaustive matrices (every recurrence
mode, every filter error, every CSV value type). The scenarios deliberately use
one representative value per feature and spend their assertions on the
interactions.

### Fixes guarded by the scenarios

The scenario stage found and fixed four composition bugs. Each has a focused
regression test next to its feature and an end-to-end assertion in a scenario:

| Fix | Root cause | Regression test | Scenario |
|---|---|---|---|
| Daily notes can be tagged. | The journal type declared only `date`, so `properties.tags` was stored without a property link. | `TestJournalTagsAreAddedToExistingWorkspace` | `TestScenarioDailyNoteCaptureJourney` |
| Converting an object removes it from incompatible collections. | `convert` changed the type without revalidating collection membership; the stale item made every later collection write fail. | `TestConvertRemovesObjectFromIncompatibleCollections` | `TestScenarioConvertTypeJourney` |
| Converting a template clears its old type default. | `convert` bypassed the `templateDefaultCleanup` that PATCH and sync push already ran. | `TestConvertTemplateClearsOldTypeDefault` | `TestScenarioTemplateAndJournalJourney` |
| Permanent purge deletes version history. | The trash purge path only deleted `records` and `blobs`, not `object_versions`. | `TestVersionHistoryPurgeDeletesSnapshots` | `TestScenarioDeleteRegretPurgeJourney` |

The details and root-cause reasoning are in
`docs/implementation/backend/user-flows.md`.

## How to add a scenario

1. **Pick a real story.** "A person does X, then Y, then Z." If the story needs
   more than about ten steps, split it into two scenarios.
2. **Create a `scenarios_<theme>_test.go` file.** One exported test function per
   story, named `TestScenario<Theme>Journey`. Keep the story linear and number
   the steps in comments.
3. **Reuse the shared helpers.** `makeApp`, `api`, `objectFrom`, `decodeBody`,
   `searchFor`, `queryObjects`, `versionSummaries`, `uploadFixture`,
   `exportEntries`, `csvTable`, `manifestFrom`, `pullSince` and the small
   helpers in `scenarios_test.go`. Do not reimplement HTTP or JSON plumbing.
4. **Assert after every step.** Check the revision, the links, the property
   links, the backlinks, the trash state, the search hit or the exported bytes
   immediately after the action that should change them. A scenario that only
   asserts the final state misses the index/relation bugs that this layer
   exists to catch.
5. **Use deterministic dates.** Calendar stories use fixed dates. If the story
   is about "today" or "three days late", derive the date from `time.Now()` and
   assert the relative rule (for example, due + 1 day), never a hard-coded
   today.
6. **Use `httptest` for capture.** Create an `httptest.NewServer` with a small
   HTML fixture and assign `app.fetcher = readerTestFetcher(server)`. This keeps
   the production SSRF policy and makes exactly one fetch. Never call a real
   website from a test.
7. **Do not sleep.** The store is synchronous; every HTTP response means the
   data is committed. Use `time.Sleep` only where the contract is explicitly
   about ordering by timestamp, and even then prefer a millisecond.
8. **Let `makeApp` clean up.** Its `t.Cleanup` runs `Store.Fsck`, which verifies
   the index, backlinks, tombstones, trash records, version snapshots and
   encryption invariants after the test. A scenario that corrupts the store
   fails there even if its own assertions pass.
9. **Run the focused test, then the suite.**

```sh
cd server
go test -run TestScenarioMyNewJourney -count=1 -v
go test ./...
go test -race -count=1 ./...
gofmt -l .
go vet ./...
```

10. **Document it.** Add a row to the scenario catalog in this guide and in
    `docs/implementation/backend/user-flows.md`.

### Style rules

- English identifiers, comments and failure messages.
- One test function per story; no table-driven scenarios (tables hide which
  step broke).
- Failure messages name the expected state and print the actual value.
- Prefer `scenarioStatus(t, rr, 200)` and `objectFrom` over manual JSON.
- Keep the fixture data small and obviously fake. No personal data, no real
  URLs, no secrets.
- If a scenario exposes a bug, add a focused regression test next to the
  feature (for example `convert_test.go`) and keep the scenario as the
  end-to-end proof.

## Manual verification against the disposable sandbox

The unit tests do not need Docker. For a curl-level check of a real server, use
the disposable sandbox on port **8879**. It has its own volume and secrets and
must never point at the personal installation (`ops/personal/compose.yaml`,
port 8765).

```sh
cd ${LUMEN_WORKSPACE}/lumen-pkm   # or your checkout root

# 1. Create disposable secrets (never reuse the personal ones).
rm -rf /tmp/lumen-test-secrets
mkdir -p /tmp/lumen-test-secrets
(cd server && go build -o /tmp/lumen-server .)
/tmp/lumen-server -init-secrets /tmp/lumen-test-secrets

# 2. Start the sandbox with its own secrets directory.
LUMEN_TEST_SECRETS_DIR=/tmp/lumen-test-secrets \
  docker compose -f sandbox/compose.test.yaml up -d --build

# 3. Read the disposable token and wait for health.
TOKEN=$(cat /tmp/lumen-test-secrets/api.token)
curl --fail http://127.0.0.1:8879/health
```

A small end-to-end curl story (create a tag, open the journal, tag it, capture
a URL, query the tag page, create and complete a task, export):

```sh
API=http://127.0.0.1:8879
AUTH="Authorization: Bearer $TOKEN"
JSON="Content-Type: application/json"

TAG=$(curl -sS -H "$AUTH" -H "$JSON" -d '{"type_id":"tag","title":"manual-check"}' \
  "$API/v1/objects" | jq -r .id)

JOURNAL=$(curl -sS -H "$AUTH" "$API/v1/journal/2026-10-31" | jq -r .id)
curl -sS -X PATCH -H "$AUTH" -H "$JSON" \
  -d "{\"content\":\"Manual check\",\"properties\":{\"date\":\"2026-10-31\",\"tags\":[\"$TAG\"]}}" \
  "$API/v1/objects/$JOURNAL" | jq '{id,revision,properties}'

# Capture a public article. The sandbox needs outbound network for the preview
# and reader extraction; without it the link is still saved with empty reader
# content, which is the documented graceful degradation.
curl -sS -H "$AUTH" -H "$JSON" \
  -d '{"url":"https://example.com/","date":"2026-10-31"}' \
  "$API/v1/capture" | jq '{id,type_id,journal_id,reader_content:(.properties.reader_content|length)}'

# The tag page query finds the journal through the tag relation.
curl -sS -H "$AUTH" "$API/v1/objects?type_id=journal&linked_includes=$TAG" | jq '.[].title'

TASK=$(curl -sS -H "$AUTH" -H "$JSON" \
  -d '{"type_id":"task","title":"Manual chore","properties":{"status":"todo","due_date":"2026-10-26","recurrence":{"mode":"weekly","weekdays":[1,3]}}}' \
  "$API/v1/objects" | jq -r .id)
curl -sS -X POST -H "$AUTH" "$API/v1/tasks/$TASK/complete" | jq '{next_date,created,next:.next.properties.due_date}'

curl -sS -H "$AUTH" -o /tmp/lumen-export.zip "$API/v1/export"
unzip -l /tmp/lumen-export.zip | head
unzip -p /tmp/lumen-export.zip manifest.json | jq '{object_count,asset_count,counts}'
```

Clean up the disposable server and its volume:

```sh
LUMEN_TEST_SECRETS_DIR=/tmp/lumen-test-secrets \
  docker compose -f sandbox/compose.test.yaml down -v
rm -rf /tmp/lumen-test-secrets /tmp/lumen-export.zip
```

`down -v` deletes the sandbox volume. Never run it against the personal
compose file. If you only want to stop the sandbox and keep its data for another
round, use `down` without `-v`.

## Reading a failure

- **`fsck after test: ...`** — the scenario left the store inconsistent
  (index, backlinks, tombstones, trash, versions or encryption). The scenario's
  own assertions may have passed; fix the invariant before the test.
- **`HTTP 400: {"error":"..."}`** — the request violated a validation rule.
  Read the message; it usually names the property or the relation.
- **`tag page missed ...` / `backlinks ...`** — the write succeeded but the
  derived relation did not. Check `property_links`, the type schema and the
  index update path.
- **`export ... differs`** — compare the exported body with the canonical
  Markdown; remember the documented link rewrite (unresolved wikilinks become
  plain text, exported object links become relative file links).
- **`race detector`** — run the same test with `-race -count=1`; the report
  names the two goroutines. Most server writes are serialized by `writeMu`; a
  race usually means a new read path touched shared state without the index
  lock.

## Related documents

- `contracts/SYNC.md` — the API, storage and sync contract.
- `docs/implementation/backend/user-flows.md` — the scenario stage, its fixes
  and follow-ups.
- `docs/implementation/backend/*.md` — one document per feature stage.
- `docs/guides/QT-DESKTOP-WORKFLOW.md` — building and running the Qt client,
  which is a separate toolchain.
