# Backend Parity Program — Completion Report

Status: **BACKEND COMPLETE** (2026-10-31). All 13 stages plus the QA
hardening stage are implemented, tested, and merged into
`backend/parity-integration` (base: the user's v0.6 HEAD `ddebc58` via
`backend/base`). Desktop stages continue under `desktop/*` branches.

## Delivered stages

| # | Stage | Highlights | Implementation doc |
|---|---|---|---|
| 1 | aliases | Default `aliases` property, alias-aware search with `matched_alias` | docs/implementation/backend/aliases.md |
| 2 | trash | Soft delete/restore/purge/empty; tombstones stay tiny; mobile sync unchanged | docs/implementation/backend/trash.md |
| 3 | weblink-reader | Readability extraction (go-readability, single fetch, SSRF-safe) + favicon + refresh endpoint | docs/implementation/backend/weblink-reader.md |
| 4 | markdown-converter | goldmark-based Markdown <-> blocks; wikilink/@mention/highlight/math inline parsers; round-trip tests | docs/implementation/backend/markdown-converter.md |
| 5 | collections | `collection` type with ordered `items`, add/remove/reorder endpoints, invariant enforcement | docs/implementation/backend/collections.md |
| 6 | templates | `is_template` everywhere, from-template with `{date:*}` variables, type default templates, journal integration | docs/implementation/backend/templates.md |
| 7 | query-filters | Relational filters (linked/backlinked), content-empty, collection filter, date ranges `{start,end}` | docs/implementation/backend/query-filters.md |
| 8 | recurring-tasks | 4 recurrence modes, overdue catch-up from due date, double-spawn guard, occurrence log | docs/implementation/backend/recurring-tasks.md |
| 9 | convert-merge | Type conversion with no-data-loss blocks; merge rewrites every referrer, source to trash | docs/implementation/backend/convert-merge.md |
| 10 | version-history | Encrypted snapshots (cap 20), list/get/restore with CAS; never synced | docs/implementation/backend/version-history.md |
| 11 | full-export | `GET/POST /v1/export` ZIP: md + YAML front matter + local links + CSV + assets + manifest; memory-safe | docs/implementation/backend/full-export.md |
| 12 | api-v2 | Scopes (`api:read`/`api:write`), `X-Lumen-Api-Version`, from-url, markdown create/append, journal append, chunked upload, keyset pagination, `GET /v1/api` discovery | docs/implementation/backend/api-v2.md |
| 13 | mcp-server | 14 MCP tools at `/mcp` via the official `modelcontextprotocol/go-sdk` v1.8.0 (streamable HTTP, bearer + scopes) | docs/implementation/backend/mcp-server.md |
| 14 | user-flows (QA) | 10 user-journey scenarios + 4 composition bug fixes (journal tags, convert-vs-collections, template defaults, purge-vs-versions) | docs/implementation/backend/user-flows.md + docs/guides/BACKEND-TESTING.md |

Every stage follows the program rules: contracts-first (`contracts/SYNC.md`
carries all 14 sections), English-only, conventional commits, tests
(`-race`, vet, gofmt) green at every merge.

## Cross-stage reconciliations performed at merge time

- **Trash semantics composed with everything**: `isDeleted` (tombstone =
  trashed or purged) for read paths; `isPurged` for reference preservation
  (trashed references stay restorable; purge runs the cleanup). Collections
  keep stored items through a trash; restore revives membership; version
  restores keep trashed references.
- **Unified search matching**: one canonical `objectMatchesQuery`
  (title + content + aliases + reader_content) shared by search, list
  filters and MCP; template exclusion by default (`include_templates`).
- **Template reference hygiene**: trashing, purging or converting a template
  clears `default_template_id` references transactionally in every path.
- **api-v2 pagination is conditional**: defaults apply only when `limit` or
  `cursor` is present, so existing desktop/mobile/extension clients keep
  full lists (breaking changes were forbidden).

## Verification state

- `go test -race -count=1 ./...` green on the merged integration branch
  (unit + 10 scenario journeys + 7 MCP e2e).
- `go vet ./...` and `gofmt -l .` clean.
- Desktop acceptance 0.6 SIGSEGV root-caused and fixed (D1); acceptance run
  prints `LUMEN_ACCEPTANCE_OK` under ASan.

## How to adopt

- `backend/parity-integration` is the single integration line. Merge it into
  the user's working branch when ready (`git merge backend/parity-integration`).
- Per-stage docs under `docs/implementation/backend/` list the client
  follow-ups (desktop/mobile/extension) for each feature.
- `docs/guides/BACKEND-TESTING.md` explains the suites, the scenario
  catalog, and how to add scenarios.

## Known follow-ups (not blockers)

- Block-level references backend (desktop D1 shipped the
  `lumen://block/<id>` format; resolution, counters and transclusion are a
  future stage; the markdown converter does not serialize group `children`
  yet).
- Sync (desktop disk cache), spaces, publish/share, whiteboards: open
  decisions in PLAN-PARIDAD-CAPACITIES.md §5, intentionally not built.
- Restoring a converted/merged object leaves documented degraded duplicates.
