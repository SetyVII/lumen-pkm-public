# Desktop stage: block handle parity (D1)

Status: implemented on `desktop/block-handle-parity` (2026-10-31). Desktop-only
stage of the Capacities parity program; the server and `contracts/` are
untouched. Backend dependencies found while building this are listed under
"Follow-ups".

## What & why

In Capacities, the six-dots handle to the left of every block opens the block
menu, and the block system is where hierarchy, lists, quotes, to-dos, toggles,
groups, block references and styles live together
(<https://docs.capacities.io/reference/blocks>). Lumen had a handle menu, but it
was a flat list of type conversions, "Agrupar" could only restyle the one block
under the cursor, there was no block-reference copy/paste, no per-block style
colors, and no move actions.

This stage brings the handle menu to parity for the UI side only:

| Capacities feature | Lumen before | Lumen after |
|---|---|---|
| Group block: "select the blocks you want to group and use Cmd/Ctrl+G, or click the 6 dots and choose group" | "Agrupar" restyled only the cursor block | `Ctrl+G` / menu wraps every block touched by the selection into ONE group box; members keep their types, ids and order |
| Ungroup | absent | Menu entry when the cursor block is in a group; removes the box and restores the members |
| Copy > Copy block reference (<https://docs.capacities.io/reference/block-based-linking>) | absent | Copies `lumen://block/<block-id>` on every block, including reference/object blocks |
| Paste block reference: empty block → embedded view, inline → simple link | plain text paste | Empty block → embedded snapshot chip; inline → plain chip run |
| "Turn into": one submenu with hierarchy, lists, to-do, quote, toggle, code, math | separate "Jerarquía"/"Lista" submenus plus top-level To-do/Citar/Toggle; no code/math | One "Turn into" submenu in Capacities order (Standard, Small, Heading 1–4, bullet/numbered/alphabetic, To-do, Quote, Toggle, Code, Math); behaviors unchanged |
| Block styles: text and background color of the whole block | absent | "Style" submenu with a fixed palette derived from the app theme presets; reset included |
| Move up/down, indent/outdent from the menu | absent | "Move" submenu using the exact `Tab`/`Shift+Tab` core; keybindings unchanged |
| Emoji interface ("Icon...") | picker existed, unreachable from the handle | Opens the existing `showIconPicker()` |

Existing behavior that keeps working: the "/" block menu and its styles,
markdown prefixes (`#`, `>>`, `[]`, `a.`…), `@`-references, block → object
conversion ("Convertir en..."), duplicate (`Ctrl+D`), delete, drag & drop and
the selection toolbar. Existing Spanish strings were not migrated (the i18n
layer is a later stage); every string added by this stage is English.

## The group model

A multi-block group is a contiguous run of `QTextBlock`s that share a group
UUID in a new block-format property (`GroupBlockProperty`,
`QTextFormat::UserProperty + 709`). Members keep their own block style
(paragraph, todo, quote…), so a group can contain typed blocks exactly like
Capacities. The box is drawn from the shared property: background
`#222228` (dark) / `#f4f4f7` (light), left/right margin 16, top margin 8 on the
first member and bottom margin 8 on the last, 0 between members.

A single-block group (no selection, or a selection inside one block) keeps the
legacy shape used since 0.6: the block itself is group-styled
(`BlockStyleProperty == "group"`), serializes as `{type: "group", text: …}` and
"Ungroup" turns it back into a paragraph. This preserves backward
compatibility with every stored note.

Membership follows drag & drop and the new move actions (all routed through
`performBlockMove`): a block dragged out of its group leaves the box
(intrinsic margins are restored by type), a block dropped between two members
of the same group joins it, and after every move the box margins are
recomputed so the first/last members frame the run again.

## Block-reference format (chosen)

```
lumen://block/<block-id>
```

- `<block-id>` is the block UUID the editor already persists in the `id` field
  of every block (`BlockIdProperty`, stable across saves and reloads).
- The editor does not know which object contains the block at menu time
  (`RichEditor` has no current-object context), so no object id is embedded.
  Resolving `block-id → (object, block)` is a backend index lookup and is
  listed as a follow-up for the backend block-references stage.
- Clipboard content is exactly the URL string; the same string pasted back is
  recognized (trim + single token + `lumen:` scheme).
- Chip runs serialize with `href: "lumen://block/<id>"` and
  `type_id: "block"`; existing `lumen://object/` spans are untouched, so
  object references, backlinks and `documentReferenceIds()` are unaffected.

### Paste semantics (snapshot, not transclusion)

- Empty block → the block becomes an `embed`-styled box containing a chip that
  renders the referenced block's text at paste time (first 120 chars,
  single line).
- Inline → a chip run is inserted inline, same rendering.
- Unresolvable id (e.g. pasted in another object) → the chip keeps the URL as
  its text, so no data is lost and a later backend can resolve it.
- Live transclusion (edits propagate to every embed, reference counters,
  "jump to origin") is a follow-up; the mission explicitly allowed snapshot
  rendering.

## Block style colors

Stored on the block format via the existing property mechanism:

| QTextFormat property | Value | Serialized block key |
|---|---|---|
| `BlockTextColorProperty` (UserProperty + 710) | `#rrggbb` string | `text_color` |
| `BlockBackgroundProperty` (UserProperty + 711) | `#rrggbb` string | `background_color` |

Keys are emitted only when set, so old payloads stay valid. Precedence when
several backgrounds could apply: user palette color > group box > type default
(code/embed/group). Text color is merged into the existing fragments (the same
pattern `applyBlockCharacterStyle` uses) so it reaches current text, not only
new typing; chips keep their own foreground. Clearing a color merges the
palette's default text color back and recomputes the background.

The palette is fixed and theme-aware (7 swatches, light/dark pairs derived from
the app theme presets and existing chip/surface colors: muted gray, red, gold,
green, blue, purple, pink; the purple pair is the app accent
`#73519b`/`#d2b7f5`, the purple chip tint `#f2edf8`/`#2d2638` etc.). Dark and
background variants are chosen from `dark_`, like the rest of the editor.

## Serialization changes (editor-local, additive)

`structuredBlocks()`:

- A group run (members sharing `GroupBlockProperty`) serializes as one block:
  `{"id": <group-uuid>, "type": "group", "text": "", "level": 0,
  "alignment": 0, "runs": [], "children": [<member blocks in order>]}`.
  Members serialize with their own `id`, `type`, `text`, `runs`, `indent` and
  colors — the same per-block serializer as top-level blocks.
- Blocks with style colors gain `text_color` / `background_color`.

`setStructuredBlocks()`:

- A `group` entry with `children` inserts each child and then stamps the
  group id + box formats on the members.
- A `group` entry without `children` keeps the legacy single-block behavior.
- `text_color` is applied to the run formats (chips keep their own colors);
  `background_color` is applied to the block format after type defaults.

Everything round-trips: group children order, member ids, group ids and style
colors survive `structuredBlocks() → setStructuredBlocks()` (covered by tests).

## Menu layout (showBlockActionMenu, after this stage)

1. **Turn into** ▸ Estándar, Pequeño, Encabezado 1–4, ─, Con viñetas,
   Numerada, Alfabética, ─, To-do, Citar, Toggle, Código, Matemáticas
   (checkmarks reflect the current block).
2. **Style** ▸ Text color ▸ palette + Default · Background color ▸ palette +
   Default · Reset to default.
3. **Icon...** → the emoji picker.
4. **Agrupar Ctrl G** (wraps the selection, or the block when nothing is
   selected) / **Ungroup** when the block is in a group.
5. **Move** ▸ Move up · Move down · Indent Tab · Outdent Shift+Tab
   (boundaries disabled).
6. ─ **Convertir en...** (block → object, unchanged).
7. **Copiar** ▸ Copiar texto · Copiar bloque · ─ · **Copy block reference**.
8. **Duplicar Ctrl D**, **Eliminar**.

## Hygiene fix included: the 0.6 acceptance SIGSEGV

The 0.6 acceptance run died with an offscreen SIGSEGV right after
`10-empty-typed-collection` (`docs/tracking/VERIFICATION.md` §0.6). Reproduced
under ASan against the disposable 8879 server: **heap-use-after-free** in
`LumenViews::visibleIds()`. `clearLayout()` removed replaced page widgets with
`deleteLater()` but never hid them, so the stale page stayed visible and
findable until the event loop ran the deferred delete; the acceptance helper
(and anything walking visible children) received the freed instance.
Fixed by hiding widgets before `deleteLater()` (the pattern
`lumen_calendar.cpp` already used), commit `fix(desktop): hide cleared body
widgets before deferred delete`. The full acceptance run now prints
`LUMEN_ACCEPTANCE_OK` (exit 0) under ASan.

## Testing

All offscreen in docker (`lumen-qt-make:latest`, `--parallel 2`):

- `lumen-editor-blocks` — pre-existing coverage plus new sections: single
  block `Ctrl+G` (key path and selection-inside-one-block), multi-block
  grouping (one group entry, 3 children in order, member ids/types
  preserved), save/load roundtrip of the group and children ids, ungroup
  (top-level blocks, order, ids, legacy group → paragraph), member moved out
  of the box, clipboard format `lumen://block/<id>`, paste into an empty
  block → `embed` chip with the referenced text, inline paste → chip run,
  turn-into code (`code:cpp` keeps type+language), block style colors
  set/serialize/roundtrip/clear, move up/down (ids preserved, boundary
  no-ops), indent/outdent on normal and list blocks.
- `lumen-editor-media` — unchanged and green (covers JSON roundtrips,
  @-mentions, "/" menu; guards the serialization refactor).
- `lumen-code-math` — unchanged and green.
- `lumen-views`, `lumen-calendar`, `lumen-tasks`, `lumen-properties`,
  `lumen-graph` — green after the `clearLayout` fix (ASan build).
- `lumen-acceptance` — `LUMEN_ACCEPTANCE_OK`, exit 0, ASan build, against the
  disposable server (`sandbox/compose.test.yaml`, port 8879, own keys and
  volume).

Note on formatting: `desktop/.clang-format` exists, but the checked-in sources
are not clang-format-clean with clang-format 23 (violations on untouched
lines), so whole-file reformatting would create unrelated diffs. New code
follows the surrounding hand style; diff hygiene wins.

## Follow-ups

Backend (needed for full parity; this stage is UI-only):

1. **Block references (backend stage)**: resolve `lumen://block/<block-id>` →
   containing object + block, so cross-object pastes resolve; serve the
   block's current text for live embeds; reference counters and
   "jump to origin" links.
2. **Contracts adoption** (`contracts/SYNC.md` + fixtures, in one commit with
   any server change): the additive block keys `children` (group members,
   same shape as the converter's `columns` extension),
   `text_color`, `background_color`; the run `href` scheme
   `lumen://block/<id>` with `type_id: "block"`. Until adopted, these keys are
   editor-local and survive in `properties.blocks` because the server stores
   blocks opaquely.
3. **Markdown converter**: `BlocksToMarkdown` should render group `children`
   (e.g. as `>>` blockquotes around the children) and ignore/preserve the new
   color keys; today a multi-block group emits a bare `>>`.

Client (later stages):

4. Live transclusion for embedded block references (edits propagate);
   navigation on click (open the origin object at the block).
5. Rectangle (marquee) block selection and Ctrl/Cmd+Shift+click
   block-precise selection; this stage uses the standard text selection
   (which Ctrl+Shift+click already extends across blocks).
6. Group settings (layout width Standard/Wide/Full) and collapse behavior.
7. The todo-style stale-widget window inside `LumenViews::rebuild()`
   (`oldWidgets` deleted after the swap) is bounded to one function and
   harmless today; hiding them early would match the `clearLayout` fix.
