# Desktop stage: find in page with replace (D3)

Status: implemented on `desktop/find-replace` (2026-10-31). Desktop-only stage
of the Capacities parity program; the server and `contracts/` are untouched.
The reference is Capacities' find-in-page
(<https://docs.capacities.io/reference/search#find-in-page>): `Ctrl+F` over the
open object, arrows between matches, a replace toggle with Replace and Replace
all, and match-case / whole-word options.

## What & why

Capacities' find-in-page searches **one open object** and explicitly does not
replace across the space. It covers text inside closed toggles, embedded
objects, block references, tables and code blocks, and it highlights every
match while the bar is open. Lumen had no find bar in the editor at all;
`Ctrl+F` opened the command palette. This stage adds the bar, the matching and
replace engine, and the session/autosave integration.

| Capacities behavior | Lumen after this stage |
|---|---|
| `Ctrl+F` / `Cmd+F` opens a find bar over the open object | `Ctrl+F` opens/focuses the bar; the current editor selection seeds the query when no session query exists |
| Arrows jump between matches | Prev/next buttons, `Enter`/`Down` and `Shift+Enter`/`Up` in the bar, `F3`/`Shift+F3` from the editor or the window |
| Replace toggle with Replace current / Replace all | Checkable replace button (`Ctrl+R`) reveals a second field and both actions |
| Match case + whole word in a small menu | `Aa` menu with both checkable options |
| Searches the whole object | One `QTextDocument` scan: paragraphs, toggles, tables, code blocks, reference chips |
| No cross-object replace | Scope is the one open object; documented below |

Existing behavior that keeps working: D1's group/turn-into/styles/block
reference chips, D2's collection pages, the `/` block menu, markdown prefixes,
`@` references, drag & drop, the selection toolbar, autosave and undo. Existing
Spanish strings were not migrated; every string added by this stage is English.

## Find-bar UX

The bar is a floating `QFrame#findBar` styled by the editor's existing
`styleLayer()` (same palette, border radius and control styles as the selection
toolbar). It is parented to the **visible scroll viewport** of the pane that
contains the editor (the document pane, or a preview dialog's pane), not to the
editor itself: the editor is deliberately as tall as its document, so a bar
parented to it would scroll away. The bar is pinned to the top-right of the
text column and repositions on pane resize. Standalone editors (tests) fall
back to the editor viewport.

Layout:

1. Query field (`findQuery`, placeholder "Find in this object"), counter
   (`n/m`, or `0/0` with a "No matches" notice), previous, next, options
   (`Aa`), replace toggle, close.
2. Replace row (hidden until toggled): replace field, **Replace**, **Replace
   all**.
3. Notice line: "No matches", "Replaced N occurrences · skipped …",
   "Reference titles are read-only", "Table cells are not replaceable yet".

Behavior:

- Opening: `Ctrl+F` focuses the bar. If the bar was closed and the session has
  no query, the current selection seeds the query (line breaks flattened to
  spaces, trimmed, capped at 200 chars). If a session query exists, it is
  restored and selected.
- Closing: `Esc` (in the bar or the editor) or the close button hides the bar.
  The query, options and replace-row visibility are kept for the session.
- Reopening restores the query with the field text selected.
- Switching objects/pages destroys the editor and therefore hides the bar; the
  session query/options are restored on the next object.
- `Enter`/`Down` = next, `Shift+Enter`/`Up` = previous in the query field.
  `Enter` in the replace field = Replace current; `Shift+Enter` = previous.
- `Ctrl+R` toggles the replace row (opens the bar with it when closed).
- `F3`/`Shift+F3` repeat next/previous from the editor or the window.
- The bar's fields accept `QEvent::ShortcutOverride`, so window shortcuts
  (`Ctrl+B/I/N/T/W`, …) do not fire while the user types a query. The bar
  handles its own keys in the event filter.
- Every match is highlighted with `QTextEdit::ExtraSelection` (soft yellow,
  stronger for the current match). The list is rebuilt from
  `findSelections_ + flashSelections_` so the existing `focusBlock()` flash is
  not clobbered. Extra selections are transient and never enter undo/save.

## Matching semantics

- **Scope**: the open object's editor document. Block content includes
  paragraphs, list items, toggles, tables, code blocks and reference chips.
  The object **title** is a separate `DocumentTitle` widget and is not part of
  the editor document; it is not searched (follow-up).
- **Plain substring**, no regex. `QTextDocument::find(QString, …)` is used, so
  the query is never interpreted as a pattern.
- **Case-insensitive by default**; the match-case option adds
  `FindCaseSensitively`. **Whole word** adds `FindWholeWords`. Both are
  documented Qt 6.4 flags.
- **Non-overlapping, left-to-right** matches. The counter is `current/count`
  (1-based); `0/0` plus "No matches" when the query has no hits. The bar stays
  open and does not block.
- **Wrap-around**: next from the last match selects the first; previous from
  the first selects the last. Documented choice; it matches browser find bars.
- **Live incremental search**: every query/option change rescans the in-memory
  `QTextDocument` synchronously. No debounce is needed (no disk/network); a
  0 ms timer refreshes the counter after document edits while the bar is open.
- **Toggles**: Lumen toggles are flat markers (`▶ ` / `▼ `) and never hide
  their content. Search therefore finds toggle children without any special
  handling. The bar deliberately **does not flip the marker** on navigation:
  that would mutate the document, add an undo entry and autosave on a mere
  `F3`. Capacities' "open the closed toggle" behavior is unnecessary here
  because there is nothing hidden; this is documented as a parity adaptation.
- **Reference chips**: object/block reference titles are matched (they are
  text in the document) but are **read-only** for replace. Replace current
  skips them and shows "Reference titles are read-only"; Replace all skips
  them and reports the count.
- **Images, formulas and dividers** are object-replacement characters; a query
  containing `U+FFFC` is treated as read-only so replace can never delete them.
- **Tables**: table cells are searched and counted, but replace skips them
  with "Table cells are not replaceable yet". `structuredBlocks()` flattens
  tables (the editor has no serialized table yet, `contracts/SYNC.md`), so
  replacing a cell and autosaving would silently lose the table. Search stays
  honest; replace refuses to corrupt. Table serialization is a follow-up.

## Replace semantics

- **Replace current**: replaces the selected match through
  `QTextCursor::insertText()` and advances to the first remaining match at or
  after the replacement. If none remains, it wraps to the first; if the list
  is empty, the counter shows `0/0`. A read-only chip/table match is skipped
  with a notice and the selection advances (or stays when it is the only
  match).
- **Replace all**: refreshes the match list first (the live refresh is a 0 ms
  timer, so a programmatic edit followed by Replace could otherwise use stale
  offsets), then replaces every editable match from the end of the document
  backwards inside one `beginEditBlock()`/`endEditBlock()` pair. Chips, table
  cells and embedded objects are skipped and reported. The cursor moves to
  the first replacement.
- **Undo**: the editor is one `QTextDocument`, so the edit block is one
  user-visible undo action; one `undo()` restores every block. Tests assert the
  effect, not `availableUndoSteps()` (Qt reports internal commands; see the Qt
  reference in `research/QT-FIND-REFERENCE.md` §2.3).
- **Autosave**: replacements are normal document edits, so `textChanged`
  reaches `MainWindow::markDirty()`, the 450 ms debounce runs `saveCurrent()`,
  and `structuredBlocks()`/`exportText()`/`rich_text` are saved exactly like
  typed edits. The MainWindow test verifies the PATCH body and the undo save.
- Formatting fidelity: `insertText()` uses the character format at the match
  start. A match spanning several formats flattens to that format; this is
  normal find/replace behavior and is documented as a follow-up if richer
  fidelity is wanted.

## Keys and collisions

| Key | Action | Notes |
|---|---|---|
| `Ctrl+F` | Open/focus find | Previously opened the command palette from the editor; the palette remains `Ctrl+P`/`Ctrl+K` |
| `Ctrl+R` | Toggle replace row | Unused before this stage |
| `F3` / `Shift+F3` | Next / previous | Unused before this stage |
| `Esc` | Close the bar | Works with focus in the bar or the editor |
| `Enter` / `Down` | Next | In the query field |
| `Shift+Enter` / `Up` | Previous | In the query field |
| `Enter` | Replace current | In the replace field |

`Ctrl+F` is registered as a window shortcut so it also works when the title or
another page control has focus; the editor handles it directly for standalone
use. The find fields claim `ShortcutOverride` so the window's other shortcuts
do not fire while typing a query.

## Session and preview editors

`MainWindow` keeps `findQuery_`, `findMatchCase_`, `findWholeWord_` and
`findReplaceVisible_`; `wireEditor()` seeds every new page editor and
`findStateChanged` copies changes back. Preview editors (`openPreview`) get
their own transient session and do not overwrite the main window's query. The
reveal callback walks up from each editor to its **own** `QScrollArea`, so a
preview's find bar scrolls the preview pane, not the page behind it.

## Testing

New target `lumen-find-replace` (`desktop/tests/find_replace_test.cpp`), wired
into `desktop/CMakeLists.txt` and `ctest`. It runs offscreen and has two parts:

1. **RichEditor directly**: a multi-block object with paragraphs, a closed
   toggle with an indented child, a code block with two lines, a table cell, a
   plural word and an object-reference chip. Covers 7 matches across all of
   them, counter `1/7`, next/previous and wrap-around, highlight-all
   (`extraSelections().size() == 7`), match case, whole word, replace current
   advancing, replace all (5 editable matches, table skipped), **one `undo()`
   restoring every block**, chip and table refusals with notices, no-match
   state, session reopen, and `F3`/`Shift+F3`.
2. **MainWindow + `FakeApi`**: `Ctrl+F` over a seeded object, live counter,
   `Ctrl+B` in the find field not formatting the note (ShortcutOverride),
   `Ctrl+R` toggling the replace row, navigation into a closed toggle without
   flipping its marker, Replace all reaching the autosave PATCH, the saved
   `properties.blocks` containing the replacements, one undo restoring and
   saving all blocks, the bar hiding on object switch, and the session query
   restored on the next object.

The fake API also gained a socket-lifetime fix (QPointer-guarded deferred
reads and a destructor that detaches sockets before the buffer hash dies):
ASan found a use-after-free in the test server while this stage's extra
requests exercised it. The fix benefits `lumen-collections` too.

Commands (docker image `lumen-qt-make:latest`, always `--parallel 2`):

```sh
cmake --preset test -S desktop
cmake --build .build/desktop-test --parallel 2 \
  --target lumen-views lumen-collections lumen-calendar lumen-properties \
           lumen-editor-media lumen-graph lumen-code-math lumen-tasks \
           lumen-editor-blocks lumen-find-replace
QT_QPA_PLATFORM=offscreen .build/desktop-test/lumen-find-replace
# ... and the other nine targets
```

All ten offscreen targets are green. `lumen-find-replace` is also green under
the `asan` preset (`-fsanitize=address,undefined`, leaks disabled), 8
consecutive runs, and 12 consecutive non-ASan runs; this guards the
ExtraSelection merge, the bar reparenting and the fake API's sockets.

## Follow-ups

1. **Table serialization** (editor/contract): `structuredBlocks()` flattens
   tables, so replace-in-table is refused for now. Once tables serialize,
   remove the table skip and extend the test to replace a cell end to end.
2. **Object title scope**: the title is a separate `DocumentTitle`; search
   covers block content only. If "the whole object" must include the title,
   the find engine needs a second document or a shared search model.
3. **Cross-object replace** is intentionally absent, matching Capacities
   ("only works inside the one object you have open"). A space-wide replace
   would be a separate, explicit feature.
4. **Regex / multi-line matches** are intentionally absent (plain substring,
   no regex; `QLineEdit` cannot contain newlines). Document if product wants
   them later.
5. **Replace formatting fidelity**: a match spanning multiple character
   formats is replaced with the format at its start. Splitting the replacement
   per original fragment is the next refinement.
6. **Global `Esc`**: the bar closes with focus in the bar or the editor. A
   window-level filter could close it from the title or another control, but
   must not steal `Esc` from open menus/popups.
7. **`F3` with a hidden bar** navigates the restored session query without
   showing the bar. Browsers do the same; if product prefers showing the bar,
   call `showFindBar()` first.
8. **Preview find session** is transient by design; persisting it per preview
   object would need a session key separate from the main page.
9. **Object menu entry**: Capacities also exposes find-in-page from the
   object's `...` menu. Lumen uses `Ctrl+F` only; adding the menu item is a
   small UI follow-up.
10. **Very large objects**: the query scan is synchronous and rescans on every
    keystroke. Notes are small, but a debounce or a match cap may be worth it
    for objects with thousands of blocks.
