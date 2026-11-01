# Desktop stage: type dashboards (D4)

Status: implemented on `desktop/type-dashboards` (2026-10-31). Desktop-only
stage of the Capacities parity program; the server, `contracts/` and the
mobile/extension clients are untouched. No contract changes: the stage only
consumes the documented list filters (`contracts/SYNC.md` §Object list filters)
and the query object format that already exist.

## What & why

In Capacities, opening an object type from the sidebar shows the type
dashboard: tabs ("sections") under the type title, where each tab is a
different view of the same type's objects — a built-in overview, curated
collections, or saved queries
(<https://docs.capacities.io/reference/content-types#object-dashboards>,
<https://docs.capacities.io/reference/sections>). Built-in sections include
"Recently opened objects", "Untagged objects", "Objects with no backlinks" and
"Collections"; a `+` control adds or removes sections per type, and removing a
tab only hides it.

Lumen's type page had only the D2 collection chip row (All + collections) and
the overview/all internal toggle. This stage adds the section tab bar —
**All** (default, previous behavior), **Recent**, **Untagged**,
**No backlinks**, **Collections**, and **one tab per saved query of that
type** — without introducing a second list engine: every tab is the existing
`LumenViews`/`LumenQuery` machinery over a different id set, so the six
presentations, search, filters, kanban moves and in-place edits keep working
on each section.

Composition with D2 (deliberate, per the stage scope): the collection chip
row stays as the type header — it is Capacities' "All + collections" row and
the drag-and-drop target — and the **Collections** tab complements it as a
section view of the same data: the type's collections rendered as cards/rows
in the view machinery, clicking one opening the D2 collection page.

## Tab / data-source mapping

| Tab | Data source | Where the set is computed |
|---|---|---|
| All | workspace cache (`GET /v1/objects`, unchanged) | `LumenQuery::evaluate`, full type scope |
| Recent | client-side recents of the type, newest first, capped at 20 | `recentTypeIds()` over the in-RAM recents list |
| Untagged | **client-side** (see decision below) | `untaggedTypeIds()`: objects of the type whose `tags` property is missing/empty |
| No backlinks | **server-side filter** `GET /v1/objects?type_id=<tid>&backlinked_count=0` | `requestNoBacklinkIds()`; ids cached per type and re-fetched with the workspace poll |
| Collections | the type's collection objects (D2 model) | `collectionsForType()`; rendered scoped to the synthetic `collection` type |
| `<query>` tabs | the saved query's `properties.view` spec, read fresh | `queriesForType()`: query objects whose view spec's `type_id` equals the type |

### Untagged: client vs server

The server's filter grammar today (`server/query.go`, `contracts/SYNC.md`
§Object list filters) covers link/backlink counts, `content_empty`,
`collection` membership and date ranges — there is **no empty-tags filter**
(`tags` is an ordinary relation/text property). Rather than extend the
contract in a desktop-only stage, the Untagged section derives its id set
client-side from the workspace cache the app already holds in RAM. The
result feeds the same evaluator as every other tab (`ids` + the normal
listing order), so search/sort inside the tab is unchanged. If a server-side
`tags_empty` filter is added later, `untaggedTypeIds()` is the single place to
switch to it (follow-up).

### No backlinks: server-side by contract

`backlinked_count=0` is exactly the documented "objects no other object
links to" filter, so the tab uses the API rather than recomputing backlinks
in the client. The id set is fetched when the type page builds (so the count
badge is warm) and re-fetched by the workspace poll and by collection
mutations (adding an item to a collection gives the member a backlink). While
the first request is in flight the badge stays empty instead of showing a
wrong 0, and the tab re-applies only the `ids` over the live view spec when
the response lands, so a search typed in the meantime survives.

### Queries: per-type tabbing

Saved queries are ordinary objects of type `query` with `properties.view`
(the view spec, including its `type_id` scope). A query therefore already
identifies its type: a query tab appears on exactly the type named in its
spec. Queries **without** a type scope (all-objects queries) are not shown on
type pages — they remain in the sidebar's saved-queries section and as the
standalone query page, which is unchanged (route `query`, no dashboard).
Rendering a query tab reads the query's spec fresh, like the standalone query
page; in-tab tweaks are session-only. Documented choice; a "pinned vs scoped"
query model would be a contract change.

## Recents design

"Recently opened" was already client-side session state: `rememberRecent()`
keeps an in-RAM list (newest first, journals excluded) capped at 80 entries,
feeding the overview's "Recientemente abierto" section — but it did not
persist. This stage layers persistence on it:

- the **session list keeps its 80-entry window** (unchanged behavior for the
  overview sections);
- `saveRecentIds()` persists a snapshot of the **newest 20** ids on every
  update — the "~20" cap from the reference design — under
  `recentObjects/<host:port>` in QSettings, keyed by the server authority so
  switching workspaces switches recents (the connect dialog reloads them);
- `loadRecentIds()` restores the snapshot at startup; ids that no longer
  resolve are simply skipped when the tab filters by type;
- the **Recent tab shows at most 20 ids of that type**, newest first
  (`order: "manual"` + `ids`), so the curated recency order wins over the
  default updated_at sort.

Only object **ids** are persisted — never note content — which keeps the
"QSettings is for visual preferences, not a note cache" product rule
intact: the recents list is navigation state, the same class as the app
geometry, and the AGENTS-mandated design for this stage explicitly allows
this QSettings use.

## Tab state and persistence

- **Visibility**: built-in sections are per-type, persisted in QSettings as
  `typeDashboards/<type_id>/hidden` — a list of *hidden* section keys, so
  "never configured" (all visible) and "all toggled off" stay distinct.
  "All" is always present and not removable; query tabs always render (they
  are the user's saved objects).
- **"+" menu**: `QToolButton#dashboardTabAdd` opens a checkable picker of the
  four built-in sections (Capacities' `+`). Right-clicking a built-in tab
  offers "Remove … from this dashboard" (Capacities' "Remove"), which only
  hides the tab for that type. Removing the *active* section falls back to
  All.
- **Per-section view memory**: each tab remembers its live tweaks (search,
  sort, presentation, visible properties, column widths) under
  `collectionSpecs["dashboard:<type>:<section>"]`, separately from the type's
  base All spec (`collectionSpecs[<type>]`) and from D2's per-collection
  specs — switching tabs can never leak one section's id set into another.
- **History**: the active section is part of the navigation state
  (`NavState.section`), so back/forward and per-window tab restore return to
  the same dashboard tab. Switching tabs records a history entry; the page is
  not rebuilt (the same `LumenViews` is re-scoped in place).
- **Counts**: every tab carries an object-count badge computed from the same
  id sets the views use (`dashboardSectionCount`); the badge refreshes on
  workspace polls, collection mutations and query saves, and the
  "No backlinks" badge appears when its filtered listing returns.

## Empty states

Each section has a scoped empty message (spec keys `empty_title` /
`empty_description`, honored only when the section's own base set is empty —
a user search that empties the view still shows the regular "no results"
state):

| Section | Empty title |
|---|---|
| Recent | "Nothing opened recently" |
| Untagged | "Every \<Type\> has tags" |
| No backlinks | "Every \<Type\> has backlinks" |
| Collections | "No collections yet" |

The Collections tab's empty state remaps "New" to D2's new-collection dialog
(fixed to the current type), exactly like the D2 collection page does.

## Files

| File | Change |
|---|---|
| `desktop/src/views/main_window_dashboards.cpp` | new: the whole stage (sections, id sets, tab bar, persistence, refresh) |
| `desktop/src/views/lumen_views.{hpp,cpp}` | `clearCollectionMode()`; host-scoped empty-state titles in the spec |
| `desktop/src/views/main_window_views.cpp` | `showCollection` gains a `section` parameter and builds the tab bar; type pages configure the view through `applyTypeSection` |
| `desktop/src/views/main_window_collections.cpp` | `rememberActiveViewSpec` is section-aware; collection mutations refresh the dashboard |
| `desktop/src/app/main_window.{hpp,cpp}` | `NavState.section`, members, sidebar-independent navigation wiring, theme styles for the tabs, recents load at startup |
| `desktop/src/sync/main_window_sync.cpp` | recents persistence (`recentSettingsKey`/`loadRecentIds`/`saveRecentIds`), dashboard refresh on the workspace poll, workspace switch reloads recents |
| `desktop/tests/dashboards_test.cpp` + `fake_api.hpp` + `CMakeLists.txt` | new `lumen-dashboards` target; the fake API implements the `type_id` + `backlinked_count` list filters |

## Testing

New offscreen target `lumen-dashboards` (`desktop/tests/dashboards_test.cpp`),
wired into `desktop/CMakeLists.txt` and `ctest`, using the in-process
loopback fake API (no server). The fake gained the `type_id` +
`backlinked_count` filters for `GET /v1/objects`, mirroring the server's
deduped backlink union (a collection's `items` count as backlink sources, like
`property_links` on the real backend). The test covers:

- the tab bar renders per type with All/Recent/Untagged/No backlinks/
  Collections plus the type's saved query, next to the D2 chip row;
- All unchanged (full listing, internal overview/all toggle) and restored
  after switching tabs;
- counts match the fake data (22/0/21/21/1/1) including the empty-state
  zero-count type;
- "No backlinks" issues `GET /v1/objects?type_id=book&backlinked_count=0`
  and lists exactly the un-backlinked objects;
- Untagged excludes tagged objects; Recent shows the scoped empty state,
  then recents newest-first capped at 20 (22 opened);
- the Collections tab lists the type's collection and its card opens the D2
  collection page; the query tab evaluates the saved query;
- scoped empty states ("Every Person has tags", "Every Web link has
  backlinks", "No collections yet");
- the "+" menu removes a section, the choice lands in QSettings, removing
  the active section falls back to All, and a **second MainWindow** (restart)
  sees both the hidden section and the persisted recents; "+" re-adds it;
- QSettings writes are isolated to a throwaway `XDG_CONFIG_HOME`.

Commands (docker image `lumen-qt-make:latest`, always `--parallel 2`):

```sh
cmake --preset test -S desktop
cmake --build .build/desktop-test --parallel 2 --target lumen-dashboards
QT_QPA_PLATFORM=offscreen .build/desktop-test/lumen-dashboards
```

All eleven offscreen targets are green (`views`, `collections`, `find-replace`,
`dashboards`, `calendar`, `properties`, `editor-media`, `graph`, `code-math`,
`tasks`, `editor-blocks`); `lumen-dashboards` and `lumen-collections` (whose
files this stage touches) are also green under the `asan` preset
(`-fsanitize=address,undefined`, leaks disabled) and the dashboards test
passed consecutive non-ASan runs. Offscreen tests exercise the widget tree
and the API surface, not the compositor; two grab captures of the type page
(All overview and the Untagged tab) are in
`scratch/2026-10-31-capacities-parity/dashboards-preview/` for visual review
(this stage's agent could not read images; the orchestrator can).

## Follow-ups

1. **Server-side empty-tags filter.** Untagged is client-side only because
   the filter grammar has no "tags empty" test. A `tags_empty=true` filter
   (or a generic `props.<name>_empty`) would let the tab use the same
   server-side path as No backlinks.
2. **"Not in a collection" section.** Capacities also ships "Objects not in
   a collection"; the backend already has the `collection` filter, but it
   cannot express "in NO collection of this type" in one request. Either a
   `collection=none` filter or a client-side set difference over
   `collectionsForType` would add the fifth built-in section.
3. **Task-type built-ins.** Capacities' task dashboards add Inbox/Today
   sections; the tasks app (`LumenTasks`) has its own tabs and is not part
   of this stage.
4. **Query tabs from the "+" menu.** Capacities can pin queries from `+` and
   create one from the dashboard. Lumen shows every query of the type
   automatically; an explicit pin/unpin model would be a contract change
   (queries have no "pinned" property).
5. **Empty-state "Import" on the Collections tab** is inert (there is
   nothing sensible to import into a collection); `LumenViews` always shows
   the Import action in empty states. A per-spec action list would clean
   this up.
6. **Remembered All spec can inherit a query's spec**: opening a standalone
   saved query stores its view spec under the type key (pre-existing D2
   behavior), so the next type page can restore the query's filters. The
   section-aware keys added here isolate the new tabs; fixing the legacy
   `collectionSpecs[<type>]` path is a separate hygiene change.
7. **Live no-backlinks refresh after collection mutations** waits for the
   next poll (a collection add/remove changes backlink counts). Re-fetching
   on every item mutation would be one more request per click for little
   visible gain.
8. **Counts on hover for query tabs** could show the query's filter summary;
   today the badge is the plain result count.
