# Desktop stage: templates UI (D5)

Status: implemented on `desktop/templates-ui` (2026-10-31). Desktop-only stage
of the Capacities parity program; the server, `contracts/` and the
mobile/extension clients are untouched. No contract changes: the stage only
consumes the templates contract implemented by the backend stage
(`docs/implementation/backend/templates.md`, `contracts/SYNC.md` §Templates).

## What & why

In Capacities, a template is an ordinary object of its type flagged as a
template: you save an object as a template, create new objects from it (with
`{date:…}` variables resolved), mark one as the type's default, and the daily
note applies that default on creation
(<https://docs.capacities.io/reference/templates>). Lumen's backend models
exactly that — the reserved boolean `properties.is_template` on every type,
`POST /v1/objects/from-template`, `default_template_id` on types, and
`GET /v1/journal/{date}?apply_default_template=true` — so this stage's whole
job is to surface it in the Qt client without introducing a second editor,
list engine or navigation model: templates are edited with the normal editor,
listed with the normal view machinery, and flagged with the normal PATCH.

| Capacities behavior | Lumen after this stage |
|---|---|
| "Save as template" on an object / "Unsave" on a template | The object `…` menu (page and preview) and the row context menu flag/unflag via `PATCH /v1/objects/{id}` with the full editable property map |
| Templates are excluded from normal lists/search | The server already excludes them from `GET /v1/objects`; the desktop mirrors flagged objects separately and never lets them into the general cache |
| Visual cue on templates | A "Template" chip on the object page (clickable → the type's Templates tab), a badge in preview dialogs, and a painted pill on cards |
| Create an object from a template | The new-object dialog gains a "Start from template" picker (Blank + the type's templates) whenever the type has templates; picking one calls `POST /v1/objects/from-template` and opens the result |
| A type can have a default template | The type settings dialog (`Propiedades de <tipo>`, the existing rename/color/property-schema dialog) gains a "Default template" picker wired to `PATCH /v1/types/{id}` `default_template_id` |
| Daily notes start from the journal's default template | `navigateJournal` appends `?apply_default_template=true` when the journal type has a default; only the lazy-creation request carries it |
| Template management per type | A built-in "Templates" section on the D4 type dashboard listing the flagged objects with open / new-from-this-template / unsave actions |

Existing behavior that keeps working: D1 block menu, D2 collection chips and
pages, D3 find bar, D4 dashboard tabs and saved queries, the six
presentations, sidebar, command palette, capture/attach and sync. Existing
Spanish strings were not migrated; every string added by this stage is
English.

## The flagged-objects mirror

The backend keeps templates out of `GET /v1/objects` and `GET /v1/search`
unless `include_templates=true`. The desktop therefore keeps **two caches**:

- `objects` — the normal listing cache (polls, `rememberObject`), guaranteed
  template-free: `rememberObject()` routes flagged objects to the mirror and
  drops them from `objects`, and unflagged objects evict any stale mirror
  entry. All/Recent/Untagged tabs, sidebar counts, the journal's
  "created today" sections, backlink panels and the palette search can never
  show a template — the client never fights the server's exclusion.
- `templates` — the flagged mirror, refreshed per type by
  `requestTemplates(typeId)`:
  `GET /v1/objects?type_id=<tid>&include_templates=true`, filtered
  client-side to `properties.is_template === true` exactly as the backend doc
  prescribes for pickers. This is the **only** listing request the desktop
  makes with the flag: it warms the type page (with the dashboard build, like
  D4's no-backlinks count), the dashboard poll refresh, the create picker and
  the type-settings picker.

The Templates tab renders the mirror through the same `LumenViews`
machinery: its data array is `objects` + the mirror
(`objectsWithTemplates()`), and the spec carries the template id set, so the
`ids` filter keeps the listing template-only while search/sort/presentation
work as everywhere else.

## UI flows

### Save as template / Unsave template

- **Object `…` menu** (`showObjectOptionsMenu`, page and preview): a
  checkable-looking entry "Save as template" (or "Unsave template" when the
  object is flagged) next to "Propiedades del tipo…". It calls
  `setObjectTemplateFlag(id, !flagged)`.
- **Row context menu** (`buildObjectMenu`, every view/table row): the same
  entry for normal objects. (The Templates tab replaces the menu wholesale —
  see below.)
- `setObjectTemplateFlag` PATCHes the **whole editable property map** with
  `is_template` set (or removed — absent means false, per the contract; the
  server clears dangling `default_template_id` references transactionally).
  On success the caches swap sides (general ↔ mirror), the page re-renders so
  the cue appears/disappears, the sidebar recounts, and the type's mirror is
  re-fetched so the Templates tab count updates immediately.
- **Cues**: the object page header gains a "Template" chip
  (`#templateChip`, theme-styled) next to the type badge; clicking it opens
  the type page's Templates section. Preview dialogs show the same badge as a
  read-only label. Cards paint a small "Template" pill next to the type name
  (compact rows append a "Template" marker to the meta line), so every
  presentation gets the cue for free.

### Template picker on create

`newObject()` (the "+ New" sidebar button, `Ctrl+N`, the palette's "Crear ·"
entries) gains a "Start from template" row between the type combo and the
summary field:

- "Blank" is always the first option and the default; the row lists the
  selected type's templates (type icon + title).
- The row **hides itself** for template-less types, so the flow is byte-for-byte
  the previous one when there is nothing to offer; switching the type resets
  the picker to Blank and warms that type's mirror.
- With a template selected, an **empty title is allowed** — the server then
  uses the template's own resolved title (`{date:…}` included), which is the
  Capacities behavior; blank creation still requires a title. The summary
  field is ignored for template creation (the template supplies the body).
- Creating calls `POST /v1/objects/from-template` `{template_id, title?}` —
  `date` is the open day when creating from a daily note, matching the note
  `date` property convention — and the created object opens through the
  normal `createdObject` path (journal origins also link it via `add_links`).

The @-search create dialog was left unchanged (the stage scope allowed either
flow); its creation modal predates this stage and templates there are a
natural follow-up.

### Type default template

The **type settings dialog** (`propertyDialog`, reached from "Editar tipo" in
the sidebar and "Propiedades del tipo…" everywhere) is where the default
picker lives: a "Default template" combo ("None" + the type's templates)
between the color and the property schema. Saving PATCHes
`/v1/types/{id}` with `name`, `color`, `properties` **and**
`default_template_id` (an empty string clears it, per the contract; the
server validates existence/type/flag). **Decision — dialog, not a dashboard
tab**: the default template is type *configuration* (like the name, color and
schema it sits between), while the dashboard is per-type *views*; Capacities
also configures defaults from the type's settings surface.

### Journal

`navigateJournal` appends `?apply_default_template=true` to the journal GET
**only when `typeFor("journal").default_template_id` is set**, and only on
the uncached (possibly lazy-creating) request. The cached path's live-refresh
GET never carries it, matching the contract's "the parameter only affects
lazy creation; existing notes are never re-applied". The note keeps its
canonical date title; the template supplies body, properties and links.

### Templates dashboard section

A fifth built-in section key joins D4's tab machinery with the same
persistence model (per-type hidden list in QSettings), one difference:

- **Visibility is data-driven by default**: the tab appears when the type has
  templates (mirror non-empty), and hides otherwise. Once the user touches
  the `+` menu at all (any section toggle writes the hidden list), the tab
  follows the configured state like every other built-in — so "add it on
  demand" (a template-less type) and "remove it" both work through `+`.
  `toggleTypeSection` always carries the current templates state into the
  written list, so toggling another section can never silently change it.
- The count badge comes from the mirror; until the first listing lands it
  stays blank like D4's "No backlinks" badge. If a fetch reveals the tab is
  no longer offered while it is active, the section falls back to All (an
  on-demand empty tab keeps its scoped empty state).
- The tab lists the flagged objects through the shared view machinery with
  its own remembered spec (`dashboard:<type>:templates`), a scoped empty
  state ("No templates yet"), and a row/context menu of its own:
  **Open**, **Open in new tab**, **New from this template**, **Unsave
  template**. The empty state's "New" opens the create dialog (whose picker
  offers the templates again); "Import" is inert there, as on D4's
  Collections tab.
- History, counts and poll refresh follow D4: the active section is part of
  the navigation state, and the poll re-applies the id set over the live spec
  (plus the merged data array) without losing in-tab search/sort tweaks.

## API calls used

All calls use the existing bearer-token `Api` client.

| Flow | Call |
|---|---|
| Mirror refresh (tab/pickers) | `GET /v1/objects?type_id=<tid>&include_templates=true`, filtered client-side to `properties.is_template === true` |
| Save / unsave template | `PATCH /v1/objects/{id}` `{properties: <full editable map ± is_template>}` |
| Create from template | `POST /v1/objects/from-template` `{template_id, title?, date?}` |
| Type default set/clear | `PATCH /v1/types/{id}` `{name, color, properties, default_template_id}` (empty string clears) |
| Journal with a default | `GET /v1/journal/{date}?apply_default_template=true` (lazy-creating request only) |

Everything else (workspace poll, object open, backlinks, saves) is unchanged;
in particular the poll still lists **without** `include_templates`.

## Files

| File | Change |
|---|---|
| `desktop/src/views/main_window_templates.cpp` | new: the whole stage (mirror, request/populate, flag toggling, from-template creation, journal check, section visibility, template row menu) |
| `desktop/src/app/main_window.{hpp,cpp}` | members + method declarations; `navigateJournal` parameter; `#templateChip` style |
| `desktop/src/ui/main_window_ui.cpp` | create-dialog picker, type-settings default picker (dialog objectName `typeSettingsDialog`), `…`-menu entry, page/preview cues |
| `desktop/src/views/main_window_dashboards.cpp` | "templates" section key/label/count, tab rendering + `+` state, `applyTypeSection`/`refreshTypeDashboard` branches, toggle semantics |
| `desktop/src/views/main_window_collections.cpp` | row-menu "Save as template"/"Unsave template" |
| `desktop/src/views/main_window_views.cpp` | type page warms the mirror with the dashboard build |
| `desktop/src/sync/main_window_sync.cpp` | `rememberObject` routes flagged objects to the mirror; the Templates tab's poll data array is merged |
| `desktop/src/views/lumen_views.cpp` | painted "Template" pill on cards (gallery) and compact-row marker |
| `desktop/tests/templates_test.cpp` + `fake_api.hpp` + `CMakeLists.txt` | new `lumen-templates` target; the fake gains the templates slice (listing exclusion, from-template deep copy with fresh block ids and `{date:…}`, type default validation, journal default application) |

## Testing

New offscreen target `lumen-templates` (`desktop/tests/templates_test.cpp`),
wired into `desktop/CMakeLists.txt` and `ctest`, running against the
loopback fake API (no server). It covers:

- the workspace cache never contains templates (the fake mirrors the
  server's default listing exclusion);
- the Templates tab appears with its count badge, lists **only** the flagged
  objects, and renders them as cards; "All" stays template-free;
- the tab's row menu actions; "New from this template" POSTs
  `/v1/objects/from-template` with the template id and **no title**, the
  result opens with the resolved title, copied blocks have fresh ids, and
  the created object does not inherit `is_template`;
- the save/unsave round-trip: the row menu's "Save as template" PATCHes the
  full property map with `is_template: true` (author property survives), the
  page gains the Template chip, the object leaves the general cache and
  enters the mirror, and unflagging PATCHes the map without the key and
  restores everything;
- the create-dialog picker: populates with the type's templates, picking one
  + empty title creates through the endpoint and opens the resolved result;
  a template-less type hides the picker row and blank creation still POSTs
  `/v1/objects` with no from-template call;
- the type-settings default: selecting the journal template PATCHes
  `default_template_id`, clearing it PATCHes an empty string, and
  `journalTypeHasDefaultTemplate()` follows both;
- the journal: a lazy-created day is requested **with**
  `apply_default_template=true` and is built from the default template
  (canonical date title, `properties.date`, fresh block ids, no
  `is_template`, body rendered in the editor); navigating the same day again
  sends no further parameter; after clearing the default the next day is
  requested **without** it and is created plain;
- the `+` model: template-less types show no tab by default, the tab can be
  added on demand (scoped empty state) and removing the active section falls
  back to All.

Commands (docker image `lumen-qt-make:latest`, always `--parallel 2`):

```sh
cmake --preset test -S desktop
cmake --build .build/desktop-test --parallel 2 --target lumen-templates
QT_QPA_PLATFORM=offscreen .build/desktop-test/lumen-templates
```

All twelve offscreen targets are green (`views`, `collections`,
`find-replace`, `dashboards`, `templates`, `calendar`, `properties`,
`editor-media`, `graph`, `code-math`, `tasks`, `editor-blocks`), and the
main `lumen` + `lumen-acceptance` targets compile. `lumen-templates` is
green under the `asan` preset (`-fsanitize=address,undefined`, leaks
disabled) in three consecutive runs — one real bug the slow sanitizer
build exposed is fixed in the shipped code: the mirror's fetch callback used
to bounce an active Templates tab back to All whenever a listing returned
empty, which wrongly closed the on-demand empty tab; the fallback now only
applies when the section is no longer offered. `lumen-dashboards` and
`lumen-collections` (whose files this stage touches) are also green under
ASan, and `lumen-templates` passed repeated non-ASan runs.

Offscreen tests exercise the widget tree and the API surface, not the
compositor. Three grab captures of the real window against the fake API —
the type page with the Templates tab (card badge), a flagged object's page
with the Template chip, and the create dialog with the picker — are in
`scratch/2026-10-31-capacities-parity/templates-preview/` for visual review
(this stage's agent could not read images; the orchestrator can).

## Follow-ups

1. **@-search create dialog**: the inline creation modal
   (`referenceCreateRequested`) still creates blank objects only; adding the
   same "From template" section there would reuse `fillTemplateCombo` and
   `createFromTemplate` verbatim.
2. **Template references in the editor**: because flagged objects never enter
   `objects`, an `@`-mention of a template cannot resolve its title chip
   (the reference data is the general cache). Resolving template titles in
   reference data — or a "New from template" action in the `/` block menu —
   would close the gap.
3. **Mirror lifetime**: the mirror refreshes for the open type page (build +
   poll) and picker/settings opens. A template deleted remotely from
   another client lingers in other types' mirrors until their next refresh;
   a from-template call then 404s with the app's normal error notice. A
   cheap improvement would be re-fetching every mirrored type on the
   workspace poll's periodic tick.
4. **Blank-title conflicts**: the server suffixes a repeated default title
   (`(2)`, `(3)`…) but never rewrites an explicit one (400 on conflict). The
   picker surfaces the error as the app's non-blocking notice; a pre-flight
   name check would smooth the rare conflict.
5. **Preview dialog cue is static**: the preview's Template badge reflects
   the object as opened; flagging from the preview's `…` menu does not live-
   update the badge (closing/reopening does). The page cue does live-update.
6. **`is_template` in the property panel**: the flag is a reserved schema
   property, so it may appear in `LumenProperties` for types that expose it;
   hiding it from the editable panel (like the server hides it from counts)
   would avoid double-editing paths.
7. **Card pill for other presentations**: the painted cue covers gallery
   cards and compact rows; the table presentation shows templates via the
   `type_id`/property columns only. A table badge column is a small polish.
