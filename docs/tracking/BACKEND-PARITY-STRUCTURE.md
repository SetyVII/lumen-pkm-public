# Backend Parity Program — Structure & Conventions

Status: **BACKEND COMPLETE** (see docs/implementation/PROGRAM-REPORT.md).
Created 2026-10-31. Owner: the orchestrating agent. Desktop stages
(D1 done, D2+ in progress) run on `desktop/*` branches under the same rules.
This document is the single source of truth for the backend implementation
program derived from `docs/tracking/PLAN-PARIDAD-CAPACITIES.md` (the Capacities
gap analysis). Every stage agent MUST read this file completely before
writing code.

## Scope

Implement the backend (Go server) portion of the Capacities parity plan in
`lumen-pkm/server/`. Client work (desktop/mobile/extension) is OUT OF SCOPE
for stage agents: they only ship server code, contracts, and documentation
that describes what clients must do next.

Explicitly excluded (product policy, AGENTS.md):
- AI features of any kind (no chat, no auto-tagging, no semantic search, no
  media analysis, no transcription).
- Third-party ingestion integrations (Readwise, Kindle, WhatsApp, Telegram,
  email-to-note).
- Telemetry, external services, multi-user accounts.

## Hard rules for every stage (from AGENTS.md, plus program rules)

1. **Language**: ALL new code, comments, identifiers, commit messages,
   documentation, and `HANDOFF.md` files are in **English**. (An i18n
   compatibility layer for Spanish comes later, in clients.)
2. **Contracts first**: if a stage changes the object format or the API
   surface, it MUST update `contracts/SYNC.md` and, when the change affects
   fixtures, `contracts/fixtures/` in the same commit. Stages that only add
   endpoints still document them in SYNC.md.
3. **Tests**: `cd server && go test ./...` must pass before every commit
   batch. Every stage adds Go tests for its new behavior. Run `gofmt -l .`
   and `go vet ./...` clean.
4. **Commits**: Conventional Commits, English, focused (e.g.
   `feat(server): add trash restore endpoint`). Commit to YOUR branch only.
   Never `git push`, never touch other worktrees or the user's main working
   tree, never rebase the base.
5. **Diff hygiene**: touch only what your stage needs. Keep changes to
   shared files (`main.go`, `model.go`, `store.go`, `index.go`,
   `schema.go`) minimal and additive. Register new HTTP routes in a new file
   `routes_<stage>.go` when reasonable, or append routes in one clearly
   labeled block — never reorder existing code. Several stages run in
   parallel branches and will be merged in order, so additive diffs matter.
6. **Dependencies**: prefer the standard library. If a dependency is
  genuinely needed, prefer a pure-Go, permissively licensed one, justify it
   in your stage doc, and run `go mod tidy`.
7. **No secrets, no personal data in tests**. Use the disposable test server
   conventions (`sandbox/compose.test.yaml`, port 8879) if you need to
   describe manual verification in docs; unit tests must not need Docker.
8. **Self-contained**: do not "fix" unrelated bugs you notice. Note them in
   your stage doc under "Follow-ups" instead.

## Deliverables per stage (definition of done)

1. Working server code on the stage branch, tests green.
2. `docs/implementation/backend/<stage-name>.md` (English) with:
   - **What & why**: the Capacities feature parity rationale (one paragraph),
     pointing to the equivalent Capacities doc page when it exists.
   - **API**: every new/changed endpoint with request/response JSON examples.
   - **Data model**: new fields, migrations, index changes.
   - **How**: the key design decisions and the reasoning behind them
     (alternatives considered, trade-offs).
   - **Contracts**: a copy of the SYNC.md section you added.
   - **Testing**: how the behavior is covered; any manual verification steps.
   - **Client follow-ups**: a short list of what desktop/mobile/extension
     must implement to surface the feature.
3. `contracts/SYNC.md` updated (and fixtures if the format changed).
4. If you get **blocked** (design decision you cannot make, hidden
   dependency, failing integration you cannot resolve): STOP, write
   `HANDOFF.md` in the worktree root with (a) exact blocker, (b) what you
   tried, (c) options you see, (d) current state of the branch; commit
   everything that compiles with tests green, and finish your run with a
   summary that starts with `BLOCKED: <one line>`. Another agent will pick
   it up from HANDOFF.md. A clean BLOCKED handoff is a SUCCESSFUL run; a
   broken build is not.

## Stage registry

| # | Branch | Stage | Depends on | Wave |
|---|---|---|---|---|
| 1 | `backend/aliases` | Aliases default property + search integration | — | 1 |
| 2 | `backend/trash` | Trash: soft delete, restore, purge, empty | — | 1 |
| 3 | `backend/weblink-reader` | Reader content extraction in web capture | — | 1 |
| 4 | `backend/markdown-converter` | Markdown <-> blocks converters (+ endpoints) | — | 1 |
| 5 | `backend/collections` | Collections: curated lists within one type | — | 2 |
| 6 | `backend/templates` | Templates per type with `{date:}` variables | — | 2 |
| 7 | `backend/query-filters` | List filters (links/backlinks/blocks/collection) + date ranges | 5 | 2 |
| 8 | `backend/recurring-tasks` | Recurrence engine for tasks | — | 2 |
| 9 | `backend/convert-merge` | Type conversion + object merge | 2 | 3 |
| 10 | `backend/version-history` | Object snapshots, list versions, restore | — | 3 |
| 11 | `backend/full-export` | Full space export (ZIP: MD + front matter + assets + links + CSV) | 4 | 3 |
| 12 | `backend/api-v2` | API v2: scopes, version header, create-from-URL, MD create/append, chunked upload, pagination | 4, 5, 8, 11 | 4 |
| 13 | `backend/mcp-server` | MCP over HTTP (tools: search/read/create/append/blocks) | 12 | 5 |

Branches are created from `backend/base`. Merging happens into
`backend/parity-integration` (created from `backend/base`) in wave order;
the orchestrator resolves conflicts and re-runs the full test suite after
every merge. Stage agents never merge.

## Merge order

waves: 1 (aliases, trash, weblink-reader, markdown-converter) →
2 (collections, templates, recurring-tasks, query-filters) →
3 (convert-merge, version-history, full-export) → 4 (api-v2) → 5 (mcp-server).
Within a wave the order is the registry order above. `query-filters` needs
`collections` first (in_collection filter); `convert-merge` needs `trash`
(merge creates a deletion); `full-export` needs `markdown-converter`
(blocks -> MD); `api-v2` composes endpoints from earlier stages.

## Stage specs (authoritative scope per stage)

### 1. backend/aliases
Add `aliases` (array of strings) as a default property available on every
object (server side only). It must be searchable: `GET /v1/search` returns
objects matching an alias, and the alias is distinguishable in the result
metadata (so a client can insert the alias as link text later). PATCH
`/v1/objects/{id}` accepts `properties.aliases`. Update the index. Do NOT
implement unlinked mentions (a later client feature uses aliases).

### 2. backend/trash
`DELETE /v1/objects/{id}` moves the object to trash instead of hard-deleting
(keep today's tombstone/anti-resurrection semantics for the trash entry).
New endpoints: `GET /v1/trash` (list with type/title/deleted_at and optional
`q` filter), `POST /v1/objects/{id}/restore` (undelete, bump incarnation or
revision as the store requires, verify anti-resurrection still holds),
`DELETE /v1/trash/{id}` (permanent purge), `POST /v1/trash/empty` (purge all
or by type). Trashed objects are excluded from search, backlinks, listings
(audit `index.go`, `/v1/objects` and sync pull: mobile must still see the
deletion via the sync journal — document how). Retention: no auto-purge in
this stage, but store `deleted_at` so a later janitor can add it.

### 3. backend/weblink-reader
In `preview.go`'s capture pipeline: after fetching the page (same fetch,
same SSRF guards — never fetch twice), extract a readable article:
strip nav/scripts/ads, prefer `<article>`/main content, keep headings/
paragraphs/lists/links as simple Markdown. Store it as property
`reader_content` (cap at ~500 KB) on the `web` object created by
`/v1/capture`; include it in full-text search. Degrade gracefully (empty
property when extraction fails). Add `POST /v1/objects/{id}/refresh-reader`
to re-run extraction for an existing weblink. No external services or AI:
pure extraction.

### 4. backend/markdown-converter
First read `contracts/SYNC.md` for the exact block/rich_text format and
check `document_formats.go` for existing import code. Implement a converter
package in the server: `MarkdownToBlocks(md string) ([]Block, error)` and
`BlocksToMarkdown(blocks []Block) string` covering: headings 1-4, paragraphs,
bullet/numbered lists (nested), quotes, tasks/checkboxes, code fences with
language, math ($$ and inline), tables, dividers, bold/italic/code/strike/
highlight inline, URLs, and `[[Title]]` wikilinks emitted as object-link
spans (resolution to IDs is a follow-up; emit the span with title and empty
id). Expose utility endpoints `POST /v1/markdown/to-blocks` and
`POST /v1/markdown/from-blocks` (JSON in/out). Round-trip tests are
mandatory.

### 5. backend/collections
A collection is a curated, ordered list of objects of ONE type. Model it as
an object of a new default type `collection` (singular: "Collection") with
properties: `item_type_id` (fixed type id, immutable after creation) and
`items` (ordered object-relation, multiple, enforced to the item type).
Endpoints: create via `POST /v1/objects` (type `collection`); mutation via
PATCH; helpers `POST /v1/collections/{id}/items` (append, with optional
position), `DELETE /v1/collections/{id}/items/{objectId}`. Enforce: items
must exist, must be of `item_type_id`, must not be trashed. Backlinks: an
object listing its collections (reuse backlinks or a derived lookup;
document choice). Register the type in the default types list and in
fixtures/SYNC docs.

### 6. backend/templates
A template is an object of an existing type flagged `is_template: true`
(new boolean property allowed on every type; excluded from normal listings
by default — add `include_templates=true` query param to `GET /v1/objects`).
Add `POST /v1/objects/from-template` with `{template_id, title?, date?}`:
creates a new object of the template's type, copying template properties
and blocks (deep copy, new block ids), resolving `{date:...}` variables in
title and in date properties (support at least `iso`, `long`, `short`,
`weekday`; use the same date semantics as the journal). Also allow a type to
declare a `default_template_id` (PATCH /v1/types/{id}) and document the
journal default-template flow (`GET /v1/journal/{date}` accepts
`?apply_default_template=true`).

### 7. backend/query-filters
Extend `GET /v1/objects` (and the shared filter parser) with: `linked_count`
(min/max), `linked_includes` (object id), `linked_type` (type id),
`backlinked_count`, `backlinked_includes`, `backlinked_type`,
`content_empty` (true/false — blocks with no text), `collection` (collection
id), and date property ranges: `props.<name>.from` / `props.<name>.to` plus
`props.<name>.has_end` (date properties gain an optional `end` value —
extend the property format in SYNC.md and store). Document every filter
with an example. Merge dependency: needs `backend/collections` merged first;
write your code assuming `collection` objects exist (branch from
`backend/parity-integration` if it exists by then — the orchestrator
will tell you in the prompt).

### 8. backend/recurring-tasks
Add optional `recurrence` to task objects: `{mode: "daily"|"weekly"|
"monthly"|"every_n_days", n?, weekdays?[]}` stored in properties. New
endpoint `POST /v1/tasks/{id}/complete` which: marks the task done, and when
it has a recurrence, creates the NEXT occurrence (new object) with the
next date computed (respect weekdays for weekly, calendar-aware), copying
context/links, and appends an occurrence log entry (text block or a
`occurrences` list property — document choice). Overdue catch-up policy:
next occurrence is computed from the due date, not from completion time;
document the reasoning. Pure date math, no timezones beyond server-local
(consistent with journal).

### 9. backend/convert-merge
`POST /v1/objects/{id}/convert` `{target_type_id, property_map}`: changes
object type; properties map 1:1 when names+types are compatible; unmapped
compatible properties stay; incompatible/unmapped values that cannot move
are appended as a text block at the end of content (never silent data
loss); blocks are untouched. `POST /v1/objects/{id}/merge` `{target_id}`:
merges source INTO target (same type enforced): target keeps its property
values; empty target properties get source values; lists (tags, aliases)
union; source content appended to target content; every link pointing to
source is rewritten to target; source goes to trash (not hard delete). Both
operations return the updated object and are covered by tests including
sync-journal effects.

### 10. backend/version-history
Snapshot an object's content+properties on every change when the new
revision differs (store snapshots in a side table, keyed by object id +
revision; cap at N=20 per object, oldest pruned). Endpoints:
`GET /v1/objects/{id}/versions` (list: revision, timestamp, size, word
count), `GET /v1/objects/{id}/versions/{revision}` (full snapshot),
`POST /v1/objects/{id}/restore/{revision}` (restores by creating a NEW
revision with the old content — never deletes history). Document the
interaction with mobile sync (snapshots are server-side only, never synced)
and CAS conflicts.

### 11. backend/full-export
`GET /v1/export` produces a ZIP (streamed or temp file, document choice):
- one folder per object type (plural name, slugified), one `.md` per object
  with YAML front matter (id, type, title, created/updated, properties) and
  body from `BlocksToMarkdown` (stage 4);
- inter-object links rewritten as relative file links between exported .md
  files;
- assets exported under `assets/` with human-readable names, referenced from
  the .md files;
- one `<type>.csv` per type with id, title, properties (one column each,
  links as id lists);
- `manifest.json` (schema version, counts).
Also `POST /v1/export` with options `{type_ids?, include_assets?, q?}`.
Memory-safe for big spaces (iterate the store, do not load everything).
Document retention of reversibility (export is not a backup of the store).

### 12. backend/api-v2
Compose the earlier stages into a coherent developer API (modeled on
developers.capacities.io, see PLAN-PARIDAD-CAPACITIES.md §5.3):
- token scopes (`api:read`, `api:write`) — extend the token model with a
  scope field, keep backward compatibility with the existing single token;
- `X-Lumen-Api-Version` header handling;
- `POST /v1/objects/from-url` (reuses `/v1/capture` + weblink-reader);
- `POST /v1/objects/markdown` (create object from markdown via stage 4) and
  `POST /v1/objects/{id}/append-markdown`;
- `POST /v1/journal/{date}/append` (append markdown to the daily note);
- chunked asset upload (`/v1/assets/upload/init`, `/part`, `/complete`,
  `/abort`) for files >50 MB, reusing the existing 50 MB path for small ones;
- pagination (`limit`/`cursor`) on list endpoints.
Breaking changes are FORBIDDEN: only additive endpoints; document the
permutations in SYNC.md.

### 13. backend/mcp-server
Minimal MCP-over-HTTP server (JSON-RPC 2.0, streamable HTTP transport,
following the MCP spec's Streamable HTTP scheme) mounted at `/mcp` behind
the same bearer token, exposing tools: `search`, `read_object`,
`create_object` (markdown), `append_to_daily_note`, `append_md_to_object`,
`list_by_type`, `list_by_tag`, `list_by_collection`, `read_object_blocks`,
`add_block`, `update_block`, `delete_block`. Scope: no OAuth (single-user,
self-hosted, token auth — document why we deviate from Capacities' OAuth
2.1, which serves multi-tenant SaaS). Depends on api-v2 being merged.

## Image-reading protocol (for stuck agents)

Stage agents work on a Go backend and should not need screenshots. If a
stage genuinely needs visual context (e.g., reproducing a Capacities UI
behavior from a screenshot), do NOT guess: write in HANDOFF.md exactly
which image would unblock you and where it is. The orchestrator has
image-reading support (GLM-5.3-Flash) and will transcribe the needed context
into the handoff for the next agent.

## Worktree layout

Worktrees live OUTSIDE the repo, under
`${LUMEN_WORKSPACE}/scratch/2026-10-31-capacities-parity/wt/<stage>/`
(scratch zone per AGENTS.md; disposable after merge). The branch is what
persists. The user's main working tree (`lumen-pkm/`) is on their own
branch with their own uncommitted work — NEVER touch it.
