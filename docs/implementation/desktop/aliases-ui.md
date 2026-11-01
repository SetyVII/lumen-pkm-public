# Desktop stage: aliases UI (D6)

Status: implemented on `desktop/aliases-ui` (2026-10-31). Desktop-only stage
of the Capacities parity program; the server, `contracts/` and the
mobile/extension clients are untouched. No contract changes: the stage only
consumes the aliases contract implemented by the backend stage
(`docs/implementation/backend/aliases.md`, `contracts/SYNC.md` §Aliases).

## What & why

In Capacities, **aliases** are alternate names stored as a default property
of every object: an object titled "HP1" can be found by typing "Harry
Potter", and when a search result matched through an alias, the **alias
becomes the inserted link text**
(<https://docs.capacities.io/reference/properties#aliases>). Lumen's backend
already models that: `properties.aliases` (normalized, limited to 50 x 200,
carried by the properties PATCH), `GET /v1/search` matching title, content
and aliases and reporting `matched_alias` for alias-only hits, and
`GET /v1/objects?q=` using the same matcher without the field. This stage
surfaces all of it in the Qt client without adding a second editor, search
engine or navigation model: the alias editor lives in the existing "…"
Customize menu next to the cover/description actions, the display lives
under the page title, and every search surface extends the matcher it
already had.

| Capacities behavior | Lumen after this stage |
|---|---|
| Per-object alias list, add by typing + Enter, remove by the chip's x / Backspace | "Aliases…" in the object's "…" menu (page and preview) opens a chip editor (`showAliasEditor`); commits through the normal full-properties PATCH |
| Limits (Capacities does not document hard numbers; Lumen's contract does) | The dialog hints "Up to 50 aliases per object, 200 characters each."; violations come back as the server's 400s and surface through the app's non-blocking notice, never a modal block |
| Aliases shown close to the title | A small muted comma-separated label under the object-page title (`aliasesLabel`), word-wrapping, hidden when empty |
| Search finds objects by alias everywhere search is used | Command palette (typed queries → `GET /v1/search`), editor `@`-linking dropdown (all three branches: global, `@<type>/`, in-type), and the shared view search (`LumenQuery::evaluate`, the type-page search) |
| A result that matched via an alias is distinguishable | The palette and dropdown rows carry the matched alias as a subtle right-aligned hint (tooltip "Matched alias: …") |
| Picking an alias-matched result inserts the alias as the link text | The signature rule — see below |

Existing behavior that keeps working: D1 block menu, D2 collections, D3
find bar, D4 dashboard tabs, D5 templates, the six presentations, sidebar,
capture/attach and sync. Existing Spanish strings were not migrated; every
string added by this stage is English.

## The matched-alias insertion rule

One rule, applied at every linking surface, mirroring the server's
`matched_alias` semantics:

- Matching is a case-insensitive substring; **the title wins**: when the
  query matches the title (or content), the result carries no alias and the
  canonical title is inserted as the link text.
- When only an alias matched, the stored alias (first match, the same value
  the server reports) is carried on the row — `matched_alias` from
  `GET /v1/search` in the palette, `Qt::UserRole + 4` in the editor
  dropdown — and displayed as the row hint.
- Picking such a row inserts **the alias as the chip's display text** while
  the anchor href, the tooltip, the reference id and the block-reference
  semantics keep the canonical object. The rule is implemented once in
  `insertReferenceAt`/`insertReferenceBlockAt` (`lumen_editor.cpp`,
  optional `linkText`), so inline picks, block references and embeds behave
  identically.

`desktop/src/ui/lumen_aliases.hpp` is the client mirror of the server's
matcher (one implementation, used by the palette fallback, the editor
dropdown and `LumenQuery`), documented to stay compatible with
`routes_aliases.go`.

## UI flows

### Alias editor ("…" → Aliases…)

- Reached from `showObjectOptionsMenu` — the same "…" menu that edits the
  cover and description, on the object page and in preview dialogs alike.
- A chip row (`WrapBar`, so the chips themselves can never clip) plus an
  input: **Enter** commits the typed alias as a chip (trimmed; an exact
  case-insensitive duplicate is skipped, matching the server's
  keep-first-spelling dedupe), the **chip's x** removes that alias,
  **Backspace on the empty input** removes the last one — the Capacities
  pattern.
- **Save** PATCHes `{"properties": <full editable map ± aliases>}` — the
  same full-replacement document the template flag uses. An empty list
  removes the key, exactly like the server normalizer.
- The dialog never blocks on validation: the limits are hinted in a muted
  line, and the server's 400s ("Each alias supports up to 200 characters",
  "An object supports up to 50 aliases") arrive after the dialog closed and
  surface through the app's non-blocking notice (`error()`).
- The editor commits against the freshest copy the client has (the preview's
  object when opened from a preview, the open page's object — which carries
  unsaved property-panel edits — when opened from the page, the workspace
  cache otherwise).
- After a successful save the caches update (`rememberObject`), the
  preview's status refreshes, and the open page updates: a clean page
  re-renders (the label appears/disappears); **a dirty page is never
  rebuilt under the user** — the saved aliases are merged into the local
  copy and the read-only label is refreshed in place.

### Object page display

A muted, comma-separated label under the title, hidden when the object has
no aliases. It word-wraps, so long lists never clip (the header chips row
needed the opposite fix — see below). **Display-only is a deliberate
choice**: editing lives in the "…" menu, so there is one write path and the
label cannot fight the property panel. Click-to-palette seeding was
considered and dropped as the expensive half of the feature (the palette is
a modal dialog; seeding it per-alias adds little over typing the alias).

### Search surfaces

- **Command palette**: with a query typed, object rows come from
  `GET /v1/search?q=` — the server matcher (title, content, aliases, and
  weblink reader articles, which the local filter does not know) — and a
  result carrying `matched_alias` shows the alias as a right-aligned hint.
  The empty query keeps the instant full-cache listing; the local rows
  (same alias rule) stand in while the request is in flight and when it
  fails, so search keeps working offline. Stale responses are dropped by a
  generation counter; a closed palette is detected through the list's
  `QPointer`.
- **Editor @-linking dropdown**: the three list branches (global, scoped
  `@<type>/`, in-type) extend their title filter with the alias rule; the
  row hint shows the matched alias; picking inserts the alias as the chip
  text. The block-reference path (`@` → embed) uses the same rule. The
  `linkObject()` picker shares this machinery, so it inherits the behavior.
- **View search** (`LumenQuery::evaluate`): the type page, "All objects",
  collections and saved queries surface alias matches. The desktop holds
  the whole workspace in RAM (its architectural invariant), so this is the
  client's `GET /v1/objects?q=` equivalent: same matcher, no
  `matched_alias` semantics (nothing is inserted there), and the server
  endpoint remains authoritative for cacheless clients.

### Sync

Aliases ride inside the properties document, so the existing save path
carries them unchanged (`saveCurrent` sends `current.properties` with
`rich_text`/`blocks`); mobile-conflict alias unioning is server-side. The
test asserts an autosave PATCH still carries the stored aliases after the
page was reloaded.

## API calls used

All calls use the existing bearer-token `Api` client.

| Flow | Call |
|---|---|
| Save aliases | `PATCH /v1/objects/{id}` `{"properties": <full editable map ± aliases>}` (empty list removes the key) |
| Palette (typed query) | `GET /v1/search?q=<query>` — results carry `matched_alias` for alias-only hits |
| Everything else (open, autosave, poll, listings) | unchanged; aliases arrive inside each object payload |

## The header chips overflow fix (included)

An independent visual review found the object-page header clipping its
last chips mid-word at the right edge ("Coleccione…", "Añadir por…",
"Propiedade…" before the "…" button). This stage adds the generic
**`WrapBar`** container (`lumen_widgets.hpp`) and uses it for the object
page and preview headers:

- chips wrap onto extra lines when the row runs out of width;
- a chip alone wider than the row **elides** its text
  (`QFontMetrics::elidedText`), keeps the full text as tooltip and restores
  it when space allows — no label is ever clipped mid-word;
- the trailing group (cover, attach, properties, "…") stays right-aligned
  whenever everything fits on one line, so wide windows look exactly as
  before;
- the measure pass never mutates chip text (the natural hint is
  reconstructed arithmetically from the elided one) and the applied rows
  account for the widgets' minimum heights — `QWidget::setGeometry` clamps
  to them, which was the subtle part.

The aliases display (a wrapping `QLabel`) and the alias editor's chip row
inherit the same no-clipping guarantee.

## Files

| File | Change |
|---|---|
| `desktop/src/ui/lumen_aliases.hpp` | new: the client mirror of the server matcher (one place) |
| `desktop/src/ui/lumen_widgets.hpp` | new `WrapBar` (wrap + elide + right-aligned trailing group) |
| `desktop/src/views/main_window_aliases.cpp` | new: the alias editor dialog + its properties PATCH + the page/preview refresh paths |
| `desktop/src/ui/main_window_ui.cpp` | "Aliases…" menu entry, the page aliases label, the palette's `GET /v1/search` integration, both header rows on `WrapBar` |
| `desktop/src/editor/lumen_editor.cpp` / `.hpp` | the three dropdown branches match aliases with hints; `insertReferenceAt`/`insertReferenceBlockAt` accept the link text |
| `desktop/src/views/lumen_views.cpp` | `LumenQuery::evaluate` surfaces alias matches |
| `desktop/src/app/main_window.hpp` | `showAliasEditor` declaration + stage comment |
| `desktop/tests/fake_api.hpp` | the aliases contract slice (normalization, limits/400s, `GET /v1/search` with `matched_alias`, `GET /v1/objects?q=`) |
| `desktop/tests/aliases_test.cpp` + `CMakeLists.txt` | new `lumen-aliases` target, wired into `ctest` |

## Testing

New offscreen target `lumen-aliases` (`desktop/tests/aliases_test.cpp`),
running against the loopback fake API (no server). It covers:

- the "…" menu carries "Aliases…" and opens the editor from the page
  (preview shares the action);
- add via Enter (with server-side trimming asserted), the duplicate skip,
  the full editable property map in the PATCH, and the stored
  normalization;
- remove via the chip's x and via Backspace on the empty input; an empty
  list removes the key and the page display;
- the limits: a 201-character alias and a 51st alias both 400 and surface
  through the non-blocking notice while the stored aliases survive;
- the object-page label appears, refreshes and disappears;
- the palette: `GET /v1/search?q=` is called, the alias-matched row carries
  the hint, picking it opens the object, a title hit carries no hint, and a
  server-only title (stale client cache) renders — proving the palette
  consumes the server response;
- **the signature rule**: picking an alias-matched `@` result inserts the
  alias as the chip display text with `documentReferenceIds` and the
  `lumen://object/<id>` anchor intact; the title-match regression; the
  scoped `@<type>/` branch; the block-reference embed; a no-alias object
  still inserts by title;
- the type-page search surfaces alias matches (both aliases) and still
  filters non-matches;
- aliases ride the autosave PATCH inside the properties document;
- the header fix: a wide window keeps the single right-aligned row, a
  560 px window wraps with every chip fully inside the bar, a standalone
  chip wider than the row elides ("…" + full tooltip) and restores, and
  the preview header never clips.

Commands (docker image `lumen-qt-make:latest`, always `--parallel 2`):

```sh
cmake --preset test -S desktop
cmake --build .build/desktop-test --parallel 2
ctest --test-dir .build/desktop-test
```

All thirteen offscreen ctest targets are green (`views`, `collections`,
`find-replace`, `dashboards`, `templates`, `aliases`, `calendar`,
`properties`, `editor-media`, `graph`, `code-math`, `tasks`,
`editor-blocks`), and the main `lumen` + `lumen-acceptance` targets compile.
`lumen-aliases` passed three consecutive non-ASan runs.

## Follow-ups

1. **Alias picker seed from the display**: clicking an alias under the
   title could pre-fill the command palette (the palette is modal, so it
   needs an initial-query parameter); display-only was the deliberate cheap
   choice.
2. **`is_template`-style reserved-property hiding**: aliases never appear
   in `LumenProperties` (reserved key), but a custom type that declares an
   `aliases` property would double-edit; hiding reserved keys from the
   panel (like the server hides them from counts) is a small polish.
3. **Palette server-search for the `@`-create dialog**: the inline creation
   modal still creates blank objects only (D5 follow-up that would also
   let new objects gain aliases at creation time).
4. **Unlinked mentions** (matching aliases inside plain text) is a later
   client feature per the backend stage's scope note.
5. **Collection chips row** (`buildCollectionChips`, D2) still uses a plain
   `QHBoxLayout`; migrating it to `WrapBar` would give the same overflow
   guarantee on narrow type pages.
6. **WrapBar rows for the sidebar's saved-query rows** and other chip rows
   were left untouched (the visual review only flagged the object header);
   migrating them is mechanical.
