# Desktop stage: view calculations (D7)

Status: implemented on `desktop/view-calculations` (2026-10-31). Desktop-only
stage of the Capacities parity program; the server, `contracts/` and the
mobile/extension clients are untouched. No contract changes: a calculation is
pure client-side evaluation over the object set the view already holds in RAM.

## What & why

In Capacities, a calculation summarizes one property across the objects
shown in a data view: a table shows the result in the column footer,
list/gallery/wall/kanban/embed show it at the top, and a grouped view shows
each group's own result. The menu only offers calculations that fit the
property — sums and friends for numbers, date bounds for dates, checked
counts/percents for checkboxes, and counts (including empty) for everything
else (<https://docs.capacities.io/reference/views#calculations>).

Lumen's view machinery (`LumenViews`/`LumenQuery`) already holds the whole
workspace in RAM and re-evaluates on every refresh, so the whole feature is
one client-side engine plus rendering in the six existing presentations —
no second evaluator, no new query surface, no server round-trips.

## Model and spec

A view spec gains an optional `calculations` array — one `{property, op}`
pair per calculation (the key names are documented here for later contract
adoption; the view spec today is a client-side construct stored inside the
saved query's `properties.view` and the per-host `collectionSpecs`, so no
`contracts/SYNC.md` change is needed):

```json
"calculations": [ {"property": "properties.pages", "op": "sum"} ]
```

Removing the last calculation drops the key entirely, so saved specs never
carry an empty array. Stale specs (property renamed, op that no longer fits
the property's type) simply evaluate to "nothing to show" — see the omit
rule below — instead of breaking the view.

### Op-per-type table

The picker only ever offers the ops valid for the selected property's type,
exactly like Capacities' "the menu only lists calculations that fit the
property":

| Property kind (type definitions) | Ops offered |
|---|---|
| `number`, `integer`, `rating` | Sum, Average, Median, Min, Max, Range, Count filled, Count empty |
| `date` (and the `created_at`/`updated_at` system fields) | Earliest, Latest, Range (days), Count filled, Count empty |
| `boolean`, `checkbox` | Checked count, Checked percent, Count filled, Count empty |
| everything else (`text`, `url`, `select`, `enum`, `tags`, `relation`, `title`, links…) | Count filled, Count empty |

Kind resolution (`LumenCalculations::kindOf`) prefers the type definition
of the property (first definition of the key wins, matching how the app
resolves definitions elsewhere); for a property no type defines it infers
`number`/`boolean` from the stored JSON values, falling back to text. The
op dispatch in `compute()` is kind-guarded, so `sum` on a text property or
`checked_count` on a number is never computed, never shown.

## Rendering

| Presentation | Ungrouped | Grouped |
|---|---|---|
| table | one footer row per calculation, under the calculated property's column (see below) | same footer over the whole table — Lumen's table ignores `group_by` today, so there are no groups to summarize; documented choice, not a regression |
| list / gallery / wall / embed | a muted summary strip at the top (`QLabel#viewCalculations`, e.g. "Sum (pages): 878 · Checked (read): 4/6") | each group's own result as a muted line under the group header (`QLabel#groupCalculations`); no top strip, so the strip never duplicates the per-group lines |
| kanban | (kanban is always grouped) | the group result sits under each column heading, inside the column |

The summary strip/lines reuse the WrapBar conventions of D6 — plain muted
`QLabel`s with `wordWrap`, styled through the view's theme stylesheet, so
long summaries wrap instead of clipping.

Table footer: `ObjectTable` grows by one row per calculation that renders;
the value lands in the calculated property's column (same model, so column
resizes/moves keep the value under its column) and every other footer cell
stays empty. Footer rows carry `Qt::NoItemFlags` (nothing selects or edits
them; the right-click object menu and Ctrl+Enter open skip them), render
value-only like Capacities' column footer, are bold, and carry the
"Sum (pages): 878" form as tooltip. Two calculations on the same property
produce two footer rows under the same column. A calculation whose property
has no visible column adds nothing.

The collection **overview** presentation shows no calculations: it is a
dashboard of recent cards, not a data view, and its customize controls are
hidden anyway.

## Omit rule (no "Sum: 0" noise)

- Sum/Average/Median/Min/Max/Range over a property no shown object has a
  value for → the segment/footer row is omitted entirely (0 is a *value*
  and does count; *missing* does not).
- Earliest/Latest/Range with no parseable dates, Checked count/percent with
  no filled checkbox values → likewise omitted.
- Count filled / Count empty always render for a non-empty view (a zero
  count is the answer, not noise).
- An **empty view** (no objects match) shows the existing empty state and
  no calculation UI anywhere; the configuration stays in the spec and
  returns with the data.

## Formatting

- Sums, Min, Max and numeric Range use the app's general number format
  (`'g'`, 12 significant digits — the same as `LumenQuery::displayValue`).
  There is no thousands separator anywhere in the app today; introducing one
  is a follow-up, not a silent change.
- Average and Median use up to two decimals with trailing zeros trimmed
  (146.333 → "146.33", 107.5 → "107.5", 8 → "8") — the app has no dedicated
  number formatting for derived values, so this is the documented choice.
- Dates render in the app's date format: ISO `yyyy-MM-dd` (what table cells
  and cards show for dates). Date Range renders as "N days" ("1 day").
- Checked count renders as "checked/filled" ("4/6"); Checked percent as a
  whole rounded percent ("67%").
- Counts render as plain integers.

## UI to add them

The existing view customize control is the sort/group menu button
(`viewSortButton` → "Ordenar y agrupar"); Capacities keeps groups and
calculations in the same Customize view menu, so **Calculations** is a
submenu right next to **Agrupar por**:

- **Add calculation…** opens a small modal (property combo → calculation
  combo, rebuilt on property change so only fitting ops are listed —
  Capacities' property-then-calculation flow in one dialog).
- Existing calculations are listed as "Op · Property" entries; picking one
  reopens the same dialog pre-filled with an extra **Remove** button.
- Duplicate (property, op) pairs are skipped silently.
- The dialog is a regular modal (the alias-editor precedent for form-style
  editors) with button-level connects to the dialog — the popup style is
  reserved for the in-place filter/properties editors.

Persistence rides the existing view-spec mechanism unchanged:
`viewSpec()` returns `d->spec` including `calculations`, so saved queries
(`properties.view` via `onSaveView`), the per-host `collectionSpecs`
(`rememberActiveViewSpec`) and the dashboard section specs carry
calculations exactly like filters, sort and grouping.

## Live recalculation

Calculations are computed inside `rebuild()`, which every existing refresh
path already calls: the workspace poll (`setData`), search/filter/group/
sort/presentation changes, kanban moves and undo, and in-place table edits.
Nothing new was added to the refresh paths.

## Files

| File | Change |
|---|---|
| `desktop/src/views/lumen_calculations.hpp` / `.cpp` | new: the engine — kind resolution, the op-per-type tables, evaluation and formatting |
| `desktop/src/views/lumen_views.hpp` | `editCalculation(int)` declaration |
| `desktop/src/views/lumen_views.cpp` | Calculations submenu in the customize menu; the add/edit/remove dialog; the table footer (`ObjectTable`); the summary strip and per-group lines; theme styles; footer guards for context menu/open |
| `desktop/tests/calculations_test.cpp` | new `lumen-calculations` target |
| `desktop/CMakeLists.txt` | `lumen-calculations` target wired into ctest; `lumen_calculations.cpp` added to `LUMEN_MODULES`, `lumen-views` and `lumen-calendar` |

## Testing

New offscreen target `lumen-calculations` (no server), in the existing
harness style, covering:

- engine: kind resolution (definitions, inference, system dates); the exact
  op lists per kind (invalid ops never offered — no Sum on a text property);
- number ops over a fixture with a real 0 and a missing value: sum 878,
  average 146.33, median 107.5, min 0, max 340, range 340;
- date ops: earliest/latest ISO dates, range in days, timestamps resolving
  to their calendar day;
- checkbox ops: "4/6" and "67%"; count filled/empty for select, text and
  relation properties; ops that do not fit the property never render;
- table: footer rows extend the model under the right column (values,
  tooltips, empty cells, `Qt::NoItemFlags`, edit rejection), hidden column
  and empty-data calculations add no footer;
- list: the summary strip, live recalculation through search narrowing, the
  polling `setData` path and clearing the search;
- grouped list: per-group lines (120/758/0) and no top strip; kanban:
  per-column results, a column with no checkbox values stays silent;
- the customize flow: add via the menu → dialog → spec + rendering, the
  op-per-type offering inside the dialog, cancel leaves the spec untouched,
  remove clears the spec (key dropped) and the rendering;
- persistence: the `viewSpec()` round trip and a fresh `LumenViews`
  restoring strip and footer from the spec;
- empty view: empty state, no calculation UI, config preserved.

Commands (docker image `lumen-qt-make:latest`, always `--parallel 2`):

```sh
cmake --preset test -S desktop
cmake --build .build/desktop-test --parallel 2
ctest --test-dir .build/desktop-test
```

All offscreen ctest targets are green, including the new `calculations` and
the pre-existing `views`/`calendar`/`collections`/`dashboards`/`templates`/
`aliases`/`find-replace` (whose sources this stage touches through
`lumen_views.cpp`), and the main `lumen` + `lumen-acceptance` targets
compile. Offscreen tests exercise the widget tree, not the compositor.

## Follow-ups

1. **Capacities' "Calculate" shortcut in the table footer** — clicking the
   footer directly to add/change that column's calculation. The footer is
   currently model-only (not clickable).
2. **Number formatting**: no thousands separator or Compact exists in the
   app; when a shared number format lands (`QLocale` grouping), the engine's
   format functions are the single place to adopt it.
3. **Grouped table summaries**: Lumen's table presentation ignores
   `group_by` today; when (if) it renders group headers, the per-group line
   pattern from the card presentations applies directly.
4. **Date ranges with end dates** (F2.5, `from`/`to`): the engine reads
   single dates; a range-valued date property should contribute both ends
   to Earliest/Latest.
5. **Contract adoption**: when the view-spec gains documented contract
   status, adopt `calculations[] = {property, op}` with the op ids from the
   table above.
6. **The collection chips row** (`buildCollectionChips`, D2) and other
   plain-`QHBoxLayout` rows could migrate to `WrapBar` for the same overflow
   guarantee the strip labels already have via word-wrap.
