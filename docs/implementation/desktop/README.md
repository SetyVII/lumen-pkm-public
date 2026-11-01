# Desktop implementation docs

One file per stage of the Capacities parity program (desktop/Qt side). See
`docs/tracking/BACKEND-PARITY-STRUCTURE.md` for the program rules and
`PLAN-PARIDAD-CAPACITIES.md` for the motivating research.

- [block-handle-parity.md](block-handle-parity.md) — six-dots handle menu at
  Capacities parity: multi-block groups, block references, turn into, block
  styles, move actions; includes the 0.6 acceptance SIGSEGV fix.
- [collections-ui.md](collections-ui.md) — curated collections in the desktop:
  type-page chips, collection page with the shared view machinery, add/remove/
  reorder flows, object-page Collections section and sidebar rows.
- [find-replace.md](find-replace.md) — Capacities find-in-page over the open
  object: floating bar, highlight-all, match case/whole word, replace current/
  all in one undo step, read-only reference chips and the table-serialization
  boundary.
- [type-dashboards.md](type-dashboards.md) — Capacities object dashboards: the
  type page's section tab bar (All, Recent, Untagged, No backlinks,
  Collections, saved queries), each tab the shared view machinery over a
  different id set; per-type tab visibility and recents persisted in QSettings.
- [templates-ui.md](templates-ui.md) — Capacities templates: save/unsave the
  `is_template` flag with page cues, a "Start from template" picker in the new
  object flow, `POST /v1/objects/from-template`, the type default template in
  the type settings dialog, the journal default on lazy daily-note creation,
  and the Templates dashboard section over a separate flagged-objects mirror.
- [aliases-ui.md](aliases-ui.md) — Capacities aliases: the per-object alias
  editor in the "…" Customize menu, the read-only display under the title,
  search everywhere (palette over `GET /v1/search`, the `@`-linking dropdown,
  view search), the matched-alias insertion rule, and the WrapBar header
  chips overflow fix.
- [view-calculations.md](view-calculations.md) — Capacities view
  calculations: the op-per-type engine (number/date/checkbox/count ops), the
  table column footer, the summary strip and per-group lines on the other
  presentations, and the Customize "Calculations" add/edit/remove flow, all
  client-side over the view's own objects.
