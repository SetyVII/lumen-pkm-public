# Qt 6 find/replace reference + D3 in-flight review

- **Stage:** D3 `desktop/find-replace` (plan F2.7: Ctrl+F find bar, replace current/all, one undo step).
- **Worktree:** `scratch/2026-10-31-capacities-parity/wt/desktop-findreplace`, branch `desktop/find-replace`, 7 modified files + 1 new test, uncommitted.
- **Build environment:** Qt **6.4.2** in `lumen-qt-make:latest` (`qmake6 -query QT_VERSION`). Docs below are the real Qt 6 docs (`doc.qt.io/qt-6`, currently rendered for 6.12); every API used here exists in 6.4.2. The empirical probes were compiled and run against the project's Qt 6.4.2.
- **Read-only:** no repo file was modified. Probe sources/binaries and this document live only in this `research/` directory.

### Snapshot update (important)

The worktree changed repeatedly **while this review was running**. **Latest state (12:50): the stage is green.**

- `lumen-find-replace` **builds and passes** (`LUMEN_FIND_REPLACE_OK`), including an **ASan/UBSan** build with no reports.
- The agent fixed the 12:10 gaps: highlight-all + flash union (`rebuildFindSelections`/`applyExtraSelections`), removal of the toggle auto-open, unconditional refresh before replace, table/image classification (`classifyFindMatch`), `ShortcutOverride` for the find fields, theme-aware icons, preview wiring (per-editor `QScrollArea`, no main-session leak), Ctrl+R toggle behavior, the destructor QPointer UB (event filters removed before deleting the bar), the `FakeApi` harness UAF (QPointer guards), and the stale test expectations.
- Remaining minor/open: **Esc is not global** (focus in the title or another widget does not close the bar); **the object title is not searched**; **table cells are intentionally skipped from replace** (search still finds them; this is a policy decision, not a bug); preview find is not covered by the test; minor geometry/notice polish.
- The 12:10 findings and the delta table in §1.3 are retained as history. Where they conflict with this block, **the latest state wins**.

The Qt reference in §2 remains valid and is the part to use for future changes.

---

## 0. What you probably need (actionable summary)

1. **Keep the single-`QTextDocument` design.** The editor is one `QTextEdit` per object (`lumen_editor.hpp:20`), not one document per block. That makes `QTextDocument::find()` cover paragraphs, toggles, tables, code and chips in one pass, and makes `beginEditBlock()/endEditBlock()` around all replacements **one undo action**. The feared per-block undo limitation does **not** apply here. Do not build a custom undo journal for this stage.
2. **Verify the new highlight-all implementation.** The 12:10 code only selected the current match; the current source adds `rebuildFindSelections()` (`lumen_editor.cpp:2425`), `applyExtraSelections()` (`2444`), and `findSelections_`/`flashSelections_`/`flashTimer_` (`lumen_editor.hpp:244-245`). `setExtraSelections()` replaces the whole list; the new `applyExtraSelections()` unions find highlights with the `focusBlock()` flash, which is the right shape. Check the colors, the current-match distinction, and that closing the bar clears only the find selections (not the flash).
3. **The toggle auto-open was removed in the current source.** `selectFindMatch()` (`lumen_editor.cpp:2377-2397`) now explicitly does not flip the marker; the old `closedToggleFor()`/`openClosedToggleFor()` code is gone. Lumen toggles are flat markers and never hide content, so this is the correct behavior. **The new test now aligns with the current `▶` behavior (it passes).** If product wants Capacities' open-on-match, restore it as a deliberate, single undoable action; the 12:10 probe showed the old flip created an undo entry and autosaved on mere search navigation.
4. **Stale-refresh guard is now in place.** `replaceFindCurrent()` (`lumen_editor.cpp:2492`) and `replaceFindAll()` (`2530`) call `refreshFindMatches(false)` unconditionally. Keep this; the live refresh is a 0 ms timer (`286-296`), so a document edit immediately followed by a replace could otherwise use stale offsets.
5. **Preview wiring is fixed in the latest source; keep it.** `wireEditor(..., bool preview = false)` now skips `findStateChanged` for preview editors (`main_window_ui.cpp:141-160`), and `revealFindMatch` walks up from the editor to its own `QScrollArea` (main page or preview dialog) and uses `findBarHeight() + 24` as the margin. Add a preview test if preview find is in scope; the old bug was `QWidget::mapTo` being called with the main `documentScroll` viewport, which is not an ancestor of the preview editor.
6. **Verify the new `ShortcutOverride` handling.** The current source accepts `QEvent::ShortcutOverride` for the query/replace edits (`lumen_editor.cpp:3222-3229`), which is the right fix for the app's `WindowShortcut`s. Test Ctrl+B/I/N/T/W while the bar has focus. The app shortcuts themselves are still `WindowShortcut` (`main_window.cpp:285-288`); if you scope them later, keep the bar's own F3/Esc/Ctrl+F handling working.
7. **Test one undo action by calling `undo()` once**, not by asserting `availableUndoSteps() == 1`. Qt reports internal commands, not user-visible steps: a two-insert edit block reported 3, a two-replacement block reported 6, a five-replacement replace-all reported 11 — yet one `undo()` restored everything in each case. The new test already does the right thing (`editor.undo()` then compare the full text).
8. **Decide the table policy; the source now skips table cells.** Live search/replace inside a `QTextTable` works, but `structuredBlocks()` flattens tables (`lumen_editor.cpp:990-1078`) and `contracts/SYNC.md:774` says the editor has no serialized table. The current source marks table matches via `classifyFindMatch()` and `replaceFindAll()` skips them with a "table cells" notice, which avoids silently destroying the table on autosave. **The new test now aligns with the skip behavior (it passes).** If product wants replacement in tables, table serialization must be added first.
9. **The new test target is green; keep it that way.** `desktop/tests/find_replace_test.cpp` (435 lines) and the `lumen-find-replace` target were added during the review. It builds and passes, including ASan/UBSan; the earlier stale toggle/table expectations and the `FakeApi` harness UAF were fixed. Add preview/Esc cases if those paths matter.
10. **Counter/options/reference-chip protection are implemented and work.** The counter shows `current/count` and updates live; match case/whole word are wired to `FindCaseSensitively`/`FindWholeWords`; replace skips `lumen://object/...` and `lumen://block/...` chips (probe confirmed).

---

## 1. In-flight work analysis

### 1.1 Architecture: one `QTextDocument` per object

`RichEditor` derives from `QTextEdit` (`lumen_editor.hpp:20`), so it owns exactly one `QTextDocument`. Blocks are `QTextBlock`s carrying `LumenSyntax::BlockStyleProperty`; tables are `QTextTable`s; code blocks are styled blocks with `CodeLanguageProperty`; toggles are plain blocks whose text starts with `▶ ` or `▼ `. `setStructuredBlocks()` builds that one document (`lumen_editor.cpp:1079+`) and `structuredBlocks()` walks it (`990+`).

Consequences, verified with Qt 6.4.2:

- `QTextDocument::find()` searched a document containing a paragraph, a table cell and a code block and returned **4/4** matches, including the table cell and code block. It also found the toggle child text. So the search scope required by the stage is naturally satisfied.
- `QTextCursor::beginEditBlock()` + `endEditBlock()` around five replacements in that one document produced **one undo action**: a single `document()->undo()` restored all five originals. The per-document undo limitation is real, but it is **not triggered** by Lumen's design.
- `availableUndoSteps()` is not a user-step count; see §2.3 and §3.

### 1.2 What is implemented (code map)

> Line numbers in this table are the **12:10 snapshot**. The current source added `classifyFindMatch`/`rebuildFindSelections`/`applyExtraSelections`/`updateFindBarIcons` in the find section, shifting later line numbers by roughly +50. Use the symbol names and `grep -n` for the current positions.

| Area | Functions / lines | Status |
|---|---|---|
| Find bar UI | `ensureFindBar()` `2018-2166`; `positionFindBar()` `2168-2188`; `showFindBar()` `2190-2214`; `closeFindBar()` `2216-2220` | Complete, styled, pinned to the scroll viewport |
| Search engine | `refreshFindMatches()` `2304-2338`; `selectFindMatch()` `2340-2365`; `updateFindUi()` `2367-2390` | Works; no highlight-all |
| Options | `setFindMatchCase()` `2251-2261`; `setFindWholeWord()` `2263-2273`; flags at `2312-2315` | Works (Qt flags verified) |
| Navigation | `findNextMatch()` `2433-2447`; `findPreviousMatch()` `2449-2463` | Works; wraps |
| Replace current | `replaceFindCurrent()` `2465-2497` | Works; skips chips; stale-match risk |
| Replace all | `replaceFindAll()` `2499-2540` | Works; one undo action; skips chips; stale-match risk |
| Read-only chips | `isReferenceChipFormat()` `113-119`; `findRangeReadOnly()` `2289-2302` | Works (probe: `RefTitle` skipped) |
| Session | `MainWindow::findQuery_` etc. `main_window.hpp:39-40`; `wireEditor()` `147-154`; `setFindSession()` `2226-2249` | Works for main editor; leaks from preview |
| Reveal | `revealFindMatch` lambda `main_window_ui.cpp:158-170` | Works for main editor; wrong for preview |
| Keys | `main_window.cpp:208-219`; `keyPressEvent()` `2849-2863`; `eventFilter()` `3175-3217` | Ctrl+F/R/F3 work; Esc not global; app shortcuts steal keys |
| Autosave | `QTextEdit::textChanged` → `markDirty()` `main_window_ui.cpp:735-740`; `saveTimer` 450 ms `main_window.cpp:171-175` | Replace edits save; toggle flip saves unintentionally |

### 1.3 Findings, ordered by severity

**Current-state delta (12:10 snapshot → 12:35 source):**

| Finding | 12:10 snapshot | Current source | Remaining action |
|---|---|---|---|
| F1 highlight-all | only current match selected | `rebuildFindSelections()`/`applyExtraSelections()` + flash union (`2425-2449`) | verify colors/current/clear |
| F2 toggle mutation | flips `▶`→`▼`, undo/autosave | removed from `selectFindMatch()` (`2377-2397`); test aligned | product decision (current: keep closed) |
| F3 preview wiring | main `documentScroll` for every editor | fixed: per-editor `QScrollArea`, no session leak | add a preview test |
| F4 global shortcuts | `WindowShortcut` steals find-field keys | fixed: `ShortcutOverride` at `3222-3229` | add a Ctrl+B/I/N/T/W test |
| F5 stale refresh | refresh only if list empty | unconditional `refreshFindMatches(false)` (`2494`, `2532`) | keep |
| F6 Esc routing | not global; read-only skip | unchanged | add window filter/scoped shortcut |
| F7 tests | none | 435-line test + target; **green**, ASan/UBSan clean | add preview/Esc cases |
| F8 tables | searched, flattened on save | table matches skipped with notice (`classifyFindMatch`, `2308`); test aligned | confirm policy; serialization is a follow-up |
| F9 images | not protected | image/divider matches marked read-only | verify |
| F10 polish | icons/geometry | icons fixed; Esc/title/geometry remain | minor |
| F11 destructor | manual delete of parented bar | fixed: event filters removed before delete (`307-315`) | none |
| F12 test harness | n/a | fixed: QPointer guards + disconnect | none |

#### F1 — No "highlight all matches" (stage requirement) — **FIXED IN CURRENT SOURCE**
At the 12:10 snapshot only the current match was selected via `setTextCursor()`; there was no `ExtraSelection` in the find path. The current source adds `rebuildFindSelections()` (`lumen_editor.cpp:2425`), `applyExtraSelections()` (`2444`), and `findSelections_`/`flashSelections_`/`flashTimer_` (`lumen_editor.hpp:244-245`). `applyExtraSelections()` unions the find highlights with the `focusBlock()` flash, which is the right shape. **Verify** the colors (all matches vs current), that closing the bar clears only `findSelections_`, and that the flash timer does not clear the find highlights.

#### F2 — Toggle auto-open mutates the document on search — **REMOVED IN CURRENT SOURCE**
At the 12:10 snapshot `selectFindMatch()` called `openClosedToggleFor()` and flipped `▶ ` → `▼ `, creating an undo entry and autosaving on mere search navigation. The current source removed the flip (`selectFindMatch()`, `2377-2397`) and the old `closedToggleFor()`/`openClosedToggleFor()` definitions. Lumen toggles are flat markers and never hide content, so this is the correct behavior. **The new test now aligns with the current behavior** (it passes with `▶`). If product explicitly wants Capacities' open-on-match, restore it as a deliberate, single undoable action and do not run it during live-search keystrokes.

#### F3 — Preview editor is wired to the main window's scroll area — **CURRENT: fixed in latest**
`wireEditor()` is called for the main editor (`main_window_ui.cpp:711`) and for the preview editor (`1782`). It unconditionally sets:

```cpp
edit->revealFindMatch = [this, guard = QPointer<RichEditor>(edit)](const QRect &rect) {
    if (!guard || !documentScroll) return;
    QWidget *viewport = documentScroll->viewport();
    const QPoint top = guard->viewport()->mapTo(viewport, rect.topLeft());
    ...
};
```

`documentScroll` is the main window's `QScrollArea` (`main_window_ui.cpp:496`, `main_window.hpp:55`). The preview editor lives in a separate `GuardedDialog`/`QScrollArea` (`1566`, `1780-1782`), so `documentScroll->viewport()` is **not an ancestor** of the preview editor. The real Qt docs for `QWidget::mapTo()` say: "The parent must not be nullptr and must be a parent of the calling widget." So the preview path is invalid: it can scroll the main window behind the dialog or compute a bogus offset. `findStateChanged` (`148-154`) has the same problem: typing in the preview's find bar overwrites `MainWindow::findQuery_`/options.

**Fixed in latest:** `wireEditor(..., bool preview = false)` now skips `findStateChanged` for preview editors, and `revealFindMatch` walks up from the editor to its own `QScrollArea` (main page or preview dialog) and uses `findBarHeight() + 24` as the margin. Add a preview test to keep it that way.

#### F4 — Global `QShortcut`s steal keys from the find fields — **CURRENT: `ShortcutOverride` added; verify**
`MainWindow::shortcut()` (`main_window.cpp:285-288`) does:

```cpp
auto *s = new QShortcut(QKeySequence(key), this);
```

The default context is `Qt::WindowShortcut`. The real Qt docs: "The shortcut is active when its parent widget is a logical subwidget of the active top-level window." Focus does not matter. The find bar's `QLineEdit`s are children of the scroll viewport, which is a child of the main window, so every app shortcut fires while the user types in the find field. Probe with a `QLineEdit` focused: Ctrl+B, Ctrl+F, Ctrl+R, Ctrl+W and Ctrl+N **all fired** and the line edit stayed empty.

In Lumen this means Ctrl+B/I (format notes), Ctrl+N (new object), Ctrl+W (close tab), Ctrl+T (new tab), Ctrl+J (journal), etc. can fire while the caret is in the find query. The find bar's `eventFilter` only handles Esc/Ctrl+F/Ctrl+R/F3/Up/Down/Enter (`3175-3217`); it does not stop the rest.

**Direction:** in the find bar's event filter, accept `QEvent::ShortcutOverride` for the query/replace edits, then handle the bar's own keys in `keyPressEvent`/the filter. This prevents `QShortcut` activation and lets the `QLineEdit` edit normally. Alternatively scope the app shortcuts to the widgets that should own them, but that is a larger change.

#### F5 — Replace can use stale match offsets — **CURRENT: fixed**
At the 12:10 snapshot, `replaceFindCurrent()` refreshed only when `findMatches_` was empty (`2466`); `replaceFindAll()` did the same (`2500`). The latest source refreshes unconditionally (`2492`/`2530`). The live refresh is a 0 ms timer started from `contentsChanged` (`286-296`). If the document changed and the timer has not run (e.g., a programmatic edit, a reference reconciliation, an image load, or a test), the stored `start/end` offsets can point at the wrong text. The reverse-order loop in `replaceFindAll()` assumes all offsets are valid.

**Direction:** call `refreshFindMatches(false)` at the top of both replace functions unconditionally, or store a document revision and verify it. The cost is one scan, which is already done on every keystroke.

#### F6 — Esc is not global; read-only path skips it — **CURRENT: unchanged**
`keyPressEvent()` handles Ctrl+F/F3/Ctrl+R before the read-only check (`2851-2860`), but handles Esc **after** `if (isReadOnly()) { QTextEdit::keyPressEvent(event); return; }` and after `handleInlineKey()` (`2861-2863`). So:
- In a read-only editor, Esc never closes the find bar from the editor.
- With an inline menu open, Esc closes the menu first (reasonable), but the bar stays.
- If focus is in the title editor or another widget, Esc does nothing to the bar; only the two line edits have the event filter (`3175-3217`).

**Direction:** handle Esc before the read-only/inline checks when `findBarVisible()`, and add a window-level event filter (or a scoped `QShortcut`) so Esc closes the bar regardless of focus. Do not use a plain `Qt::WindowShortcut` for Esc without checking popups/menus.

#### F7 — Tests were added during the review; they are now green
The 12:10 snapshot had no tests. The current source adds `desktop/tests/find_replace_test.cpp` (435 lines) and a `lumen-find-replace` target in `desktop/CMakeLists.txt`. It now **builds and passes** (`LUMEN_FIND_REPLACE_OK`), including an ASan/UBSan build. The earlier failures were stale toggle/table expectations (the source now keeps toggles closed and skips table cells) and a `FakeApi` harness UAF (`buffers` vs `server` destruction order), both fixed. Add preview/Esc cases; see §4 for the intended coverage.

#### F8 — Tables are searched but not persisted — **CURRENT: table matches skipped**
Live search inside a `QTextTable` works: the probe found `needle in table`. At the 12:10 snapshot `replaceFindAll("X")` also replaced it, but `structuredBlocks()` serializes each table cell block as a `paragraph` and does not emit a `table` object (`lumen_editor.cpp:990-1078`); `contracts/SYNC.md:774` says "The editor has no serialized table or column layout yet and ignores them." `exportText()` also flattens cells into lines. `saveCurrent()` stores both `blocks` and `rich_text`, but `loadEditor()` prefers `blocks` when non-empty (`main_window_ui.cpp:388-404`), so an autosave after a table replace would lose the table on reload. The latest source therefore marks table matches via `classifyFindMatch()` and skips them from replace with a notice. If product wants replacement in tables, table serialization must be added first; otherwise document the skip as the current scope.

#### F9 — Images/math/dividers are not protected from replace — **CURRENT: images marked read-only**
`findRangeReadOnly()` only checks anchors (`2289-2302`). A query containing `U+FFFC` (object replacement character) can match an image, math formula or divider fragment, and replace-all would delete it. This is low probability (the user would have to paste `U+FFFC`, e.g., by seeding the query from a selected image), but a robust implementation should also treat `format.isImageFormat()` as read-only.

#### F10 — Smaller issues / polish — **CURRENT: icons fixed; Esc/title/geometry remain**
- **Title is not searched.** The object title is a separate `DocumentTitle` (a `QTextEdit`), not part of `RichEditor`'s document. Ctrl+F in the title opens the notes find bar (global `WindowShortcut`) and searches only the body. If "the open object" includes the title, this is a scope gap; at minimum, scope Ctrl+F to the notes editor so it does not steal the title's Ctrl+F.
- **Ctrl+F behavior change.** The old editor used `QKeySequence::Find` to open the command palette via `paletteRequested` (removed in this diff; old line at `lumen_editor.cpp` ~2997). Now Ctrl+F is find and the palette is Ctrl+P/Ctrl+K. This matches the stage intent and Capacities, but confirm no acceptance test/user expectation relies on Ctrl+F for the palette.
- **Ctrl+R is inconsistent.** The window shortcut calls `showFindBar(true)` (opens/keeps the replace row); the line-edit event filter toggles `!findReplaceVisible_` (`3191-3193`). Because the window shortcut usually wins, Ctrl+R never toggles the row closed. Pick one behavior.
- **`findBarHost()` fallback uses `mapTo(self)`.** For a standalone editor, `host == viewport()` and `positionFindBar()` calls `viewport()->mapTo(host, ...)`; the docs require `parent` to be an ancestor (a widget is not its own parent). It works in the probe, but avoid it with `if (host == viewport()) { ... }`.
- **Fixed 72 px reveal margin.** If the replace row + notice make the bar taller than 72 px, a match can be revealed underneath the bar. Use `findBar_->height() + 24` as the top margin.
- **Long notices / narrow windows.** `findNoticeLabel_` has `setWordWrap(false)` and the query/replace edits are fixed at 230 px; the bar can grow wider than a narrow editor or be clamped at x=8 while its right edge overflows. Clamp the notice width or enable word wrap, and consider a minimum window width for the full bar.
- **Icon contrast.** `tool()` uses `actionIcon()` → `LumenIcons::icon(name)` with the default `#a5a4ac` (`lumen_icons.hpp`). On the light `#fcfcfc` bar this is low contrast; the rest of the editor passes explicit dark/light colors. Consider a theme-aware color.
- **Counter includes read-only chip titles.** Replace-all skips them and reports "skipped N reference titles", which is transparent; the counter itself does not distinguish. Acceptable, but a tooltip/notice could explain.
- **`findNextMatch()` with a restored session but hidden bar** selects a match without showing the bar (e.g., F3 after switching objects). Minor UX.
- **`findBar_` is a child of the scroll viewport, not the scroll area's widget.** It survives a `setWidget()` replacement; the app recreates `documentScroll` per render, so it is not hit today, but a future reuse would leave the bar floating. A note for future refactors.
- **`setAppearance()` restyles the bar but does not re-run `positionFindBar()`.** If the theme changes the bar's size, it can be slightly misplaced until the next resize/toggle.

#### F11 — Destructor-order QPointer UB (real bug, found by ASan/UBSan; fixed in latest)
`RichEditor::~RichEditor()` (`lumen_editor.cpp:307-312`) manually deletes `findBar_`:

```cpp
if (findBar_) {
    findBar_->hide();
    delete findBar_.data();
}
```

The bar is a child of the scroll viewport, and its child `QLineEdit`s have the editor as event filter. During `delete`, `QLineEdit::~QLineEdit()` calls `clearFocus()`, which sends a focus event through the event filters; `RichEditor::eventFilter()` (`3222-3231`) evaluates `findBar_ && findBar_->parentWidget()`, dereferencing the `QPointer<QFrame>` while the `QFrame` is already partially destroyed (vptr `QWidget`). UBSan reports "downcast ... object is of type 'QWidget'" at `qpointer.h:41`; the stack points to `RichEditor::~RichEditor()` line 310 and `eventFilter` line 3231. **Fixed in latest:** the destructor now removes the event filters from `findQueryEdit_`, `findReplaceEdit_` and `findBarHost_` before deleting the bar (`307-315`). Keep that ordering; do not re-add a bare `delete findBar_.data()` without removing the filters first.

#### F12 — Test-harness UAF in `FakeApi` (found by ASan; fixed in latest)
`fake_api.hpp` declares `QTcpServer server` (line 25) before `QHash<QTcpSocket*, QByteArray> buffers` (line 35), so `buffers` is destroyed first. The server's child sockets are destroyed during `~FakeApi`, their `disconnected` lambda (line 43) calls `buffers.remove(socket)`, and ASan reports heap-use-after-free. **Fixed in latest:** `FakeApi` uses `QPointer<QTcpSocket>` guards for `readyRead` and the deferred re-read, and calls `socket->disconnect(this)` on disconnect. Keep the guards; the `buffers`/`server` declaration order is still worth cleaning up if the harness is reused.

### 1.4 What is healthy

- The single-document architecture is the right call for this stage; it avoids the per-block undo problem and makes table/code/toggle search natural.
- `QTextDocument::find()` with the string overload is used correctly (no regex injection); flags are correct; whole-word and case behavior verified.
- Replace-all is genuinely one undo action (verified), and it skips reference chips (verified).
- The find bar UI is complete: query, counter, prev/next, options, replace row, notice, close; it is pinned to the visible scroll viewport and styled with the existing layer styles.
- The current source now also implements highlight-all with a flash union (`rebuildFindSelections`/`applyExtraSelections`), table/image classification (`classifyFindMatch`), unconditional refresh before replace, `ShortcutOverride` for the find fields, and theme-aware icons.
- Session persistence across objects is a nice Capacities-like touch; it just needs to be kept out of the preview editor.
- The existing `lumen-editor-blocks` target compiles and passes; the new `lumen-find-replace` target builds and passes, including ASan/UBSan. The remaining items are the minor Esc/title/table-policy questions, not test failures.

---

## 2. Qt 6 ground truth

All quotes below are from the official Qt 6 documentation. The build image is Qt 6.4.2; the cited pages are the current `qt-6` pages. APIs used here are present in 6.4.2 unless noted.

### 2.1 Searching: `QTextDocument::find()`

**Signatures** (`https://doc.qt.io/qt-6/qtextdocument.html#find`):

```cpp
QTextCursor QTextDocument::find(const QString &subString,
                                int position = 0,
                                QTextDocument::FindFlags options = FindFlags()) const;

QTextCursor QTextDocument::find(const QString &subString,
                                const QTextCursor &cursor,
                                QTextDocument::FindFlags options = FindFlags()) const;

QTextCursor QTextDocument::find(const QRegularExpression &expr,
                                int from = 0,
                                QTextDocument::FindFlags options = FindFlags()) const;

QTextCursor QTextDocument::find(const QRegularExpression &expr,
                                const QTextCursor &cursor,
                                QTextDocument::FindFlags options = FindFlags()) const;
```

**Flags** (`https://doc.qt.io/qt-6/qtextdocument.html#FindFlag-enum`):

| Flag | Value | Doc text |
|---|---|---|
| `QTextDocument::FindBackward` | `0x00001` | "Search backwards instead of forwards." |
| `QTextDocument::FindCaseSensitively` | `0x00002` | "By default find works case insensitive. Specifying this option changes the behaviour to a case sensitive find operation." |
| `QTextDocument::FindWholeWords` | `0x00004` | "Makes find match only complete words." |

**What the docs say about the string overload** (exact):

> "Finds the next occurrence of the string, *subString*, in the document. The search starts at the given *position*, and proceeds forwards through the document unless specified otherwise in the search options. ... Returns a cursor with the match selected if *subString* was found; otherwise returns a null cursor. If the *position* is 0 (the default) the search begins from the beginning of the document; otherwise it begins at the specified position."

> "By default the search is case insensitive, and can match text anywhere in the document."

**Important distinction:** the *regex* overloads are documented as "within the same paragraph". The *string* overloads are documented as "in the document". Our Qt 6.4.2 probe confirms the string overload finds text inside `QTextTable` cells and code blocks. Do not assume the string overload is per-block.

**`QTextEdit::find()` is a different API** (`https://doc.qt.io/qt-6/qtextedit.html#find`):

```cpp
bool QTextEdit::find(const QString &exp,
                     QTextDocument::FindFlags options = QTextDocument::FindFlags());
bool QTextEdit::find(const QRegularExpression &exp,
                     QTextDocument::FindFlags options = QTextDocument::FindFlags());
```

> "Finds the next occurrence of the string, *exp*, using the given *options*. Returns `true` if *exp* was found and changes the cursor to select the match; otherwise returns `false`."

It moves the visible cursor and returns only a bool; it is **not** an enumeration API. The implementation correctly uses `QTextDocument::find()` to enumerate all matches.

**Pitfalls:**

- `find()` returns a **null cursor** when there is no match; test `found.isNull()`, not `found.position() == 0`.
- A `QTextCursor` with a selection starts searching **after** the selection (`find(subString, cursor, ...)`); the int overload starts at `position`. The implementation uses `from = match.end`, which is correct for non-overlapping enumeration.
- `FindWholeWords` uses word boundaries: probe with `"alphabet alpha alpha2"` found exactly **1** match for `"alpha"`, not 3.
- The string overload is not regex; `(`, `[`, `*`, etc. are literal. This is good for a find bar.
- `QTextDocument::find()` searches the document text, including table cell blocks and code blocks. It does not search the object title because the title is a separate widget/document.
- `QTextDocument::find()` returns a cursor whose `selectionStart()/selectionEnd()` are document positions; store those, not `QTextCursor` objects, if the document may change (the implementation does this).
- `FindBackward` exists; the implementation enumerates forward and wraps in `findPreviousMatch()`, which is fine but less efficient.

### 2.2 Highlighting all matches: `QTextEdit::ExtraSelection`

**Struct** (`https://doc.qt.io/qt-6/qtextedit-extraselection.html`):

```cpp
struct QTextEdit::ExtraSelection {
    QTextCursor cursor;      // "A cursor that contains a selection in a QTextDocument"
    QTextCharFormat format;  // "A format that is used to specify a foreground or background brush/color for the selection."
};
```

**API** (`https://doc.qt.io/qt-6/qtextedit.html#setExtraSelections`):

```cpp
void QTextEdit::setExtraSelections(const QList<QTextEdit::ExtraSelection> &selections);
QList<QTextEdit::ExtraSelection> QTextEdit::extraSelections() const;
```

Doc text: "This function allows temporarily marking certain regions in the document with a given color, specified as *selections*. This can be useful for example in a programming editor to mark a whole line of text with a given background color to indicate the existence of a breakpoint."

**Canonical pattern:**

```cpp
QList<QTextEdit::ExtraSelection> selections;
selections.reserve(findMatches_.size());
for (int i = 0; i < findMatches_.size(); ++i) {
    const FindMatch &m = findMatches_.at(i);
    QTextEdit::ExtraSelection sel;
    sel.cursor = QTextCursor(document());
    sel.cursor.setPosition(m.start);
    sel.cursor.setPosition(m.end, QTextCursor::KeepAnchor);
    sel.format.setBackground(i == findIndex_ ? currentMatchColor : matchColor);
    // Optional: sel.format.setForeground(...);
    selections.append(sel);
}
setExtraSelections(selections);
```

**Pitfalls:**

- `setExtraSelections()` **replaces the entire list**. Lumen already uses it for the `focusBlock()` flash (`lumen_editor.cpp:933-938`): `setExtraSelections({selected})` and a timer that calls `setExtraSelections({})`. If find also calls it, the two features clobber each other. Keep one list in the editor (e.g., `findSelections_` + `flashSelection_`) and rebuild the union; or clear the flash when find opens and restore it when find closes.
- The `format` is a `QTextCharFormat`; use `setBackground(QColor/QBrush)` and/or `setForeground`. `QTextFormat::FullWidthSelection` gives a full-line highlight (the existing flash uses it).
- Extra selections are painted **behind** the text but the actual `QTextCursor` selection (from `setTextCursor`) is painted by the editor's selection palette and may cover the extra selection for the current match. That is acceptable; the current match can also be distinguished by a stronger background or a border-like foreground.
- Extra selections are **not** part of the document and do not affect undo, saving, or `toHtml()`. They are the right tool for transient highlights.
- `extraSelections()` returns the last list; useful for merging.

### 2.3 One-undo-step multi-edit: `beginEditBlock()` / `endEditBlock()`

**Signatures** (`https://doc.qt.io/qt-6/qtextcursor.html#beginEditBlock`, `#endEditBlock`, `#joinPreviousEditBlock`):

```cpp
void QTextCursor::beginEditBlock();
void QTextCursor::endEditBlock();
void QTextCursor::joinPreviousEditBlock();
```

Doc text for `beginEditBlock()`:

> "Indicates the start of a block of editing operations on the document that should appear as a single operation from an undo/redo point of view."

The doc example:

```cpp
QTextCursor cursor(textDocument);
cursor.beginEditBlock();
cursor.insertText("Hello");
cursor.insertText("World");
cursor.endEditBlock();

textDocument->undo();   // removes both "World" and "Hello"
```

> "It is possible to nest calls to beginEditBlock and endEditBlock. The top-most pair will determine the scope of the undo/redo operation."

`joinPreviousEditBlock()` doc text:

> "Like beginEditBlock() indicates the start of a block of editing operations that should appear as a single operation for undo/redo. However unlike beginEditBlock() it does not start a new block but reverses the previous call to endEditBlock() and therefore makes following operations part of the previous edit block created."

**The per-`QTextDocument` limitation (confirmed):**

- The undo/redo system belongs to the `QTextDocument`, not to the cursor or the widget. `QTextDocument` owns `undo()`, `redo()`, `availableUndoSteps()`, `availableRedoSteps()`, `clearUndoRedoStacks()`, and the `undoAvailable`/`redoAvailable` signals (`https://doc.qt.io/qt-6/qtextdocument.html#undo`, `#availableUndoSteps`, `#clearUndoRedoStacks`, `#undoRedoEnabled-prop`). `QTextEdit::undo()` delegates to its document (`https://doc.qt.io/qt-6/qtextedit.html#undo`).
- `beginEditBlock()` groups operations **on that document**. There is no cross-document edit block in Qt.
- Therefore, **if** Lumen used one `QTextDocument` per block, a cross-block Replace-all could not be a single Qt undo step: each block's document has its own stack, and `QTextEdit::undo()` only affects the current document. Workarounds would be:
  1. a **custom undo journal** owned by the editor: record `(document, position, removedText, insertedText, formats)` for every replacement and implement a single editor-level undo/redo action that applies the inverse across documents; or
  2. accept **per-block undo** (the user may need multiple Ctrl+Z presses), which is usually unacceptable for "Replace all".
- **Lumen does not have this problem:** it uses one document per object. The implementation's `replaceFindAll()` (`lumen_editor.cpp:2517-2528`) opens one edit block on one cursor and performs all replacements inside it. The probe confirmed one `undo()` restored all five originals.
- If the editor ever moves to a block-per-document model (e.g., for virtualization), this stage's undo guarantee must be revisited. Do not assume `beginEditBlock()` spans documents.

**The `availableUndoSteps()` trap (docs vs reality):**

`https://doc.qt.io/qt-6/qtextdocument.html#availableUndoSteps` says only "Returns the number of available undo steps." In practice, an edit block creates multiple internal commands. Probe results with Qt 6.4.2:

| Operation | `availableUndoSteps()` after | One `undo()` result |
|---|---|---|
| Same cursor, block with two `insertText` | **3** | both removed |
| Two cursors, block with two `insertText` | **3** | both removed |
| One cursor, block with two reverse replacements | **6** | both restored |
| Five reverse replacements (replace-all) | **11** | all five restored |

So a test like `QCOMPARE(document()->availableUndoSteps(), 1)` after replace-all will fail even though the behavior is correct. Test the **effect**: call `undo()` once and compare the full text to the original. If you need a user-step count, maintain your own counter, not `availableUndoSteps()`.

**Other undo pitfalls:**

- `QTextEdit::setHtml()`, `setPlainText()` and `clear()` clear the undo/redo history (the `QTextEdit` property docs say so explicitly for `html`, `plainText`, `markdown` and `clear`). Lumen's `setStructuredBlocks()` also calls `document()->clearUndoRedoStacks()` (`lumen_editor.cpp:1277`), so the history starts clean after loading an object. Good.
- `QTextDocument::undoRedoEnabled` defaults to true; if it is ever disabled, the undo stack is cleared and no items are added (`https://doc.qt.io/qt-6/qtextdocument.html#undoRedoEnabled-prop`).
- `ensureBlockIds()` uses `joinPreviousEditBlock()` (`lumen_editor.cpp:969-987`). If it runs after a replace-all, block-format/ID changes may be joined to the replace-all undo action. That is usually harmless (IDs were already assigned), but be aware if you assert exact undo contents.
- `QTextCursor::insertText()` inside an edit block still emits `contentsChanged` per insertion; autosave debouncing handles it, but don't connect a synchronous expensive slot without a guard.

### 2.4 Revealing/scrolling to a match

**APIs and exact docs:**

- `void QTextEdit::setTextCursor(const QTextCursor &cursor)` — "Sets the visible cursor." (`https://doc.qt.io/qt-6/qtextedit.html#setTextCursor`). It does **not** scroll.
- `void QTextEdit::ensureCursorVisible()` — "Ensures that the cursor is visible by scrolling the text edit if necessary." (`https://doc.qt.io/qt-6/qtextedit.html#ensureCursorVisible`).
- `QRect QTextEdit::cursorRect(const QTextCursor &cursor) const` — "returns a rectangle (in viewport coordinates) that includes the *cursor*." (`https://doc.qt.io/qt-6/qtextedit.html#cursorRect`).
- `void QScrollArea::ensureVisible(int x, int y, int xmargin = 50, int ymargin = 50)` — "Scrolls the contents of the scroll area so that the point (x, y) is visible inside the region of the viewport with margins specified in pixels by xmargin and ymargin. If the specified point cannot be reached, the contents are scrolled to the nearest valid position." (`https://doc.qt.io/qt-6/qscrollarea.html#ensureVisible`). The point is in the scroll area's **widget/contents** coordinate system, not viewport coordinates.
- `void QScrollArea::ensureWidgetVisible(QWidget *childWidget, int xmargin = 50, int ymargin = 50)` — scrolls so a child **widget** of the scroll area's widget is visible (`https://doc.qt.io/qt-6/qscrollarea.html#ensureWidgetVisible`). It is not suitable for a text position inside one tall editor; it scrolls to the editor's top.
- `void QWidget::ensurePolished() const` — "Ensures that the widget and its children have been polished by QStyle (i.e., have a proper font and palette)." (`https://doc.qt.io/qt-6/qwidget.html#ensurePolished`). It is **not** a scrolling API.
- `QPoint QWidget::mapTo(const QWidget *parent, const QPoint &pos) const` — "Translates the widget coordinate pos to the coordinate system of parent. The parent must not be nullptr and must be a parent of the calling widget." (`https://doc.qt.io/qt-6/qwidget.html#mapTo`).

**There is no public `scrollTextArea` API in Qt 6.** If you saw that name in a blog/snippet, it is not Qt 6 Widgets. The real options are the ones above.

**Why the implementation's manual reveal is justified for the main editor:**

The editor is deliberately as tall as its document inside `documentScroll` (`main_window_ui.cpp:719-726`), with its own vertical scrollbar off. `QTextEdit::ensureCursorVisible()` therefore scrolls the editor's internal (hidden) scroll area, not the outer `QScrollArea`; the match can remain off-screen. The lambda maps `cursorRect()` (viewport coordinates) to `documentScroll->viewport()` and adjusts the outer scrollbar. That is a valid approach.

**If you prefer the built-in API:** map the match rect to `documentScroll->widget()` coordinates (add the current scrollbar value / use `mapTo(documentScroll->widget(), ...)`) and call `documentScroll->ensureVisible(x, y, 72, 72)`. Do not pass viewport coordinates to `ensureVisible()`; they are different coordinate systems.

**Pitfalls:**

- `mapTo(parent, ...)` requires `parent` to be an ancestor. The preview editor's lambda violates this (see F3). For a standalone editor, `findBarHost()` returns the editor's own viewport and `positionFindBar()` calls `viewport()->mapTo(viewport(), ...)`; avoid self-mapping by special-casing `host == viewport()`.
- If blocks can be collapsed/hidden in the future, `cursorRect()` may return an invalid/empty rect for a hidden block. Make the block visible first (`QTextBlock::setVisible(true)` where supported, or your own collapse model), force a layout (`document()->markContentsDirty(...)` / `documentLayout()->documentSizeChanged`), then compute `cursorRect()`. Lumen has no collapsing today, so this is a future concern.
- `cursorRect()` returns the caret rectangle, not the full match rectangle; for a multi-line match, use the start and end cursors and union their rects if you want to reveal the whole match. The implementation reveals the start, which is fine for a single-line query (QLineEdit cannot contain newlines).
- The find bar is an overlay; a fixed 72 px top margin can put the match under the bar when the replace row and notice are visible. Use `findBar_->height() + 24` or the actual overlay geometry.
- `QScrollArea::ensureWidgetVisible(editor)` scrolls to the editor's top, not to the match; do not use it for this.

### 2.5 Key handling: `QShortcut` context vs `keyPressEvent`

**`QShortcut` signature** (`https://doc.qt.io/qt-6/qshortcut.html#QShortcut-2`):

```cpp
QShortcut(const QKeySequence &key, QObject *parent,
          const char *member = nullptr, const char *ambiguousMember = nullptr,
          Qt::ShortcutContext context = Qt::WindowShortcut);
void QShortcut::setContext(Qt::ShortcutContext context);
```

**`QShortcut` context** (`https://doc.qt.io/qt-6/qshortcut.html#context-prop`, `https://doc.qt.io/qt-6/qt.html#ShortcutContext-enum`):

> "A shortcut's context decides in which circumstances a shortcut is allowed to be triggered. The normal context is Qt::WindowShortcut, which allows the shortcut to trigger if the parent (the widget containing the shortcut) is a subwidget of the active top-level window."

| Context | Value | Doc text |
|---|---|---|
| `Qt::WidgetShortcut` | `0` | "The shortcut is active when its parent widget has focus." |
| `Qt::WidgetWithChildrenShortcut` | `3` | "The shortcut is active when its parent widget, or any of its children has focus. Children which are top-level widgets, except pop-ups, are not affected by this shortcut context." |
| `Qt::WindowShortcut` | `1` | "The shortcut is active when its parent widget is a logical subwidget of the active top-level window." |
| `Qt::ApplicationShortcut` | `2` | "The shortcut is active when one of the applications windows are active." |

**What this means for Lumen:**

- `MainWindow::shortcut()` uses the default `WindowShortcut` (`main_window.cpp:285-288`). The find bar's `QLineEdit`s are logical subwidgets of the main window, so **all** app shortcuts fire while the user types in the find field. Probe confirmed Ctrl+B/F/R/W/N all fired with a focused `QLineEdit`.
- If you want find shortcuts to work only when the editor (or the find bar) has focus, parent them to the editor/find bar and use `Qt::WidgetWithChildrenShortcut`. But the find bar is a child of the **scroll viewport**, not the editor, so a `WidgetWithChildrenShortcut` on the editor will not cover it. Either parent the find bar to the editor's viewport (and accept that it scrolls with the document) or put the find shortcuts on the find bar itself.
- A modal dialog is a separate top-level window; while it is active, the main window is not the active top-level window, so its `WindowShortcut` shortcuts are inactive. The preview editor's own `keyPressEvent` handles Ctrl+F/R/F3 while the preview dialog is active.

**`QEvent::ShortcutOverride`:** when a widget has focus, Qt sends a `ShortcutOverride` event before activating a `QShortcut`. If the focus widget accepts the event, the shortcut is not triggered and the key press is delivered normally. This is the clean way to protect the find bar's line edits: in the find bar's event filter, accept `ShortcutOverride` for the query/replace edits, then handle the bar's own keys in `keyPressEvent`/the filter. `QLineEdit` already does this for standard editing keys (copy/paste/undo/select-all), but not for app-specific ones like Ctrl+B/I/N/T/W.

**Standard keys:** use `QKeySequence::Find`, `QKeySequence::FindNext`, `QKeySequence::FindPrevious` rather than hard-coded strings where possible (`https://doc.qt.io/qt-6/qkeysequence.html#StandardKey-enum`). `QKeyEvent::matches(QKeySequence::StandardKey)` is the correct test. The current code uses `event->matches(QKeySequence::Find)` for Ctrl+F and hard-coded F3/Shift+F3; F3 has no standard "find next" on all platforms, so hard-coding is acceptable, but consider `QKeySequence::FindNext`.

**Conflicts:**

- `QTextEdit`'s built-in editing key bindings do **not** include Ctrl+F, Ctrl+R or F3 (the docs list Ctrl+C/V/X/Z/Y, navigation, etc.). The conflicts are with Lumen's own `QShortcut`s, not with `QTextEdit`.
- Ctrl+F previously opened the command palette via `paletteRequested`; that line was removed (`lumen_editor.cpp` old ~2997) and the palette is now Ctrl+P/Ctrl+K. This is a deliberate behavior change; confirm the product decision.
- Ctrl+R is not a standard Qt editing key; it is safe from `QTextEdit`, but it is a common "reload" key in other apps. In Lumen it is now "open replace"; no existing binding was found.
- F3/Shift+F3 are not used elsewhere in the app (grep); safe.
- Esc is not a `QShortcut`; it is handled in `keyPressEvent`/eventFilter. It only works when focus is in the editor or the find bar. For a page-level find bar, add a window-level event filter or a scoped shortcut; do not use a plain `WindowShortcut` for Esc without considering open menus/popups.

### 2.6 Replace: selection and insertion

**APIs:**

```cpp
QString QTextCursor::selectedText() const;
void QTextCursor::insertText(const QString &text);
void QTextCursor::insertText(const QString &text, const QTextCharFormat &format);
int QTextCursor::selectionStart() const;
int QTextCursor::selectionEnd() const;
void QTextCursor::setPosition(int pos, QTextCursor::MoveMode m = MoveAnchor);
```

**Doc text:**

- `selectedText()`: "Returns the current selection's text (which may be empty). This only returns the text, with no rich text formatting information. ... If the selection obtained from an editor spans a line break, the text will contain a Unicode U+2029 paragraph separator character instead of a newline `\n` character. Use QString::replace() to replace these characters with newlines." (`https://doc.qt.io/qt-6/qtextcursor.html#selectedText`).
- `insertText()`: "Inserts text at the current position, using the current character format. If there is a selection, the selection is deleted and replaced by text... Any ASCII linefeed characters (`\n`) in the inserted text are transformed into unicode block separators, corresponding to insertBlock() calls." (`https://doc.qt.io/qt-6/qtextcursor.html#insertText`).

**Pitfalls:**

- The replacement string comes from a `QLineEdit`, so it cannot contain `\n`; if you later allow multi-line replacement, `insertText()` will create new blocks. The reverse-order replace-all still works for earlier positions, but the undo block will include block insertions and the table/cell structure may change.
- `insertText()` uses the **current character format** at the cursor. If a match spans multiple formats (e.g., bold + normal), the replacement takes the format at the selection start and applies it to the whole replacement. That is typical find/replace behavior, but it can flatten formatting. If fidelity matters, split the replacement per original fragment or use `QTextDocumentFragment`.
- `selectedText()` is not used by the implementation (it uses `selectionStart/End`), which is good: it avoids the U+2029 ambiguity. The seeding code in `showFindBar()` does use `selectedText()` and correctly replaces U+2029/U+2028 with spaces (`lumen_editor.cpp:2200-2210`).
- `setPosition(pos, KeepAnchor)` is the correct way to build a selection; `QTextCursor::selectionStart/End` are document positions. Store those, not `QTextCursor` objects, if the document can change.
- If the replacement is empty, `insertText("")` deletes the selection; that is the correct "replace with nothing" behavior.

### 2.7 Autosave / undo integration

- `QTextDocument::contentsChanged` is emitted "whenever the document's content changes; for example, when text is inserted or deleted, or when formatting is applied" (`https://doc.qt.io/qt-6/qtextdocument.html#contentsChanged`). `QTextEdit::textChanged` is the widget-level signal.
- Lumen's main editor connects `QTextEdit::textChanged` to `markDirty()` (`main_window_ui.cpp:735-740`), which starts a 450 ms single-shot `saveTimer` (`main_window.cpp:171-175`). Any replace edit therefore schedules a save; no extra integration is needed.
- The find bar's live-refresh timer is also connected to `contentsChanged` (`lumen_editor.cpp:286-296`); it is guarded by `findUpdating_` so replace-all's internal edits don't trigger a recursive refresh. `QTextEdit::textChanged` is **not** guarded by `findUpdating_`, so replace edits still mark the object dirty (desired). The 12:10 toggle auto-open also marked it dirty; that code is now removed.
- `QTextDocument::isModified()`/`setModified()` are separate from Lumen's `dirty` flag; Lumen uses `textChanged`/`markDirty`, so don't rely on `isModified` for autosave.
- `QTextDocument::clearUndoRedoStacks()` is called after `setStructuredBlocks()` (`lumen_editor.cpp:1277`); the undo history starts clean after loading. `QTextEdit::setHtml/setPlainText/clear` also clear history (docs).
- `QTextDocument::undo()`/`redo()` are document-level; `QTextEdit::undo()`/`redo()` delegate. One `undo()` after a replace-all edit block restores the whole block (verified). `availableUndoSteps()` is not a user-step count (see §2.3).

### 2.8 Tables, code and toggles

- **Tables:** `QTextDocument::find()` searches table cell text (probe: `needle in table` found). `QTextTable` cells are `QTextBlock`s; `QTextCursor::currentTable()` and `selectedTableCells()` exist for table-aware operations (`https://doc.qt.io/qt-6/qtextcursor.html#currentTable`, `#selectedTableCells`). A query cannot span cells because a cell boundary is not a normal text character; a single-cell match works. Replace via `QTextCursor::insertText()` works at the Qt level, but Lumen does not serialize tables (`structuredBlocks()` flattens; `contracts/SYNC.md:774`), so an autosave after a table replace would lose the table. The latest source therefore skips table cells from replace with a notice; that is the current scope decision.
- **Code blocks:** code is stored in a normal block with `BlockStyleProperty == "code"` and lines separated by `U+2028` (`QChar::LineSeparator`), not by block boundaries (`exportText()` at `lumen_editor.cpp:4447+`). `QTextDocument::find()` finds words inside code (probe). A query containing a space will not match across a `U+2028` line break; that is expected. Replace works; the syntax highlighter re-runs on `contentsChanged`.
- **Toggles:** toggles are flat text markers `▶ ` / `▼ `; content is never hidden. Search finds the child text without any special handling. The current auto-open code mutates the marker (see F2); since the content is visible, the correct behavior is not to mutate. If Lumen later implements real collapsing (hiding blocks), then search must temporarily reveal the block before `cursorRect()`; use the block visibility/layout APIs and force a layout, and make the reveal explicit/undoable.
- **Reference chips:** object/block references are anchors with `lumen://object/...` or `lumen://block/...`; `QTextCharFormat::isAnchor()`/`anchorHref()` are the checks (`https://doc.qt.io/qt-6/qtextcharformat.html#isAnchor`, `#anchorHref`). The implementation's `isReferenceChipFormat()` (`lumen_editor.cpp:113-119`) and `findRangeReadOnly()` (`2289-2302`) correctly skip them; probe confirmed `RefTitle` was not replaced. Images/math/dividers are image formats (`QTextCharFormat::isImageFormat()`); they are **not** currently skipped, so a `U+FFFC` query could delete them.
- **Title:** `DocumentTitle` is a separate `QTextEdit`; it is not part of the `RichEditor` document. Search does not see it. If "the open object" includes the title, this is a scope gap.

### 2.9 `QTextEdit::setReadOnly` and programmatic replace

`https://doc.qt.io/qt-6/qtextedit.html#readOnly-prop`: "In a read-only text edit the user can only navigate through the text and select text; modifying the text is not possible." This describes **user** input; programmatic `QTextCursor` edits still work. `replaceFindCurrent()`/`replaceFindAll()` do not check `isReadOnly()`, so a read-only `RichEditor` could be modified through the find bar. The main and preview editors are currently editable, but the comment at `lumen_editor.cpp:2850` says find works in a read-only editor; guard replace with `isReadOnly()` if that path is real.

---

## 3. Empirical probes (Qt 6.4.2)

All probes were compiled against the worktree's editor sources and the Qt 6.4.2 in `lumen-qt-make:latest`, run with `QT_QPA_PLATFORM=offscreen`. They are in this directory; they are not part of the repo.

| Probe | What it proves |
|---|---|
| `qt_probe.cpp` | `QTextDocument::find("alpha")` found 4 matches: 2 in a paragraph, 1 in a table cell, 1 in a code block. `FindWholeWords` found 1/3 for `"alphabet alpha alpha2"`. A two-cursor edit block was undone by one `undo()`. `ExtraSelection` compiled and `setExtraSelections({sel})` returned 1. `selectedText()` across blocks contains U+2029. |
| `undo_probe.cpp` | Same-cursor two-insert edit block: `availableUndoSteps()==3`, one `undo()` removed both. Two-cursor block: same. Reverse two-replacement block: `availableUndoSteps()==6`, one `undo()` restored both. |
| `find_probe.cpp` | A document with paragraph/table/code/chip: 5 `"needle"` matches; `replaceFindAll("X")` replaced all 5; one `undo()` restored the original text; `RefTitle` chip was skipped (replace returned 0, text unchanged). |
| `toggle_probe.cpp` | `findNextMatch()` into a closed toggle child changed `▶ Toggle title` to `▼ Toggle title` and added an undo entry. |
| `table_probe.cpp` | `structuredBlocks()` serialized a 1x2 table as four `paragraph` blocks (cell texts as separate paragraphs); `exportText()` flattened the table into lines. |
| `shortcut_probe.cpp` | With a `QLineEdit` focused, `WindowShortcut`s for Ctrl+B, Ctrl+F, Ctrl+R, Ctrl+W and Ctrl+N **all fired**; the line edit stayed empty. |
| `bar_probe.cpp` | The find bar grew to fit the "No matches" notice (size 450x64, notice inside the bar); the notice-clipping concern did not reproduce in the standalone editor. |

Build/test status of the worktree (latest source, out-of-tree build):

- `cmake` configure + `lumen-editor-blocks` build: **OK**; `EDITOR_BLOCKS_TEST_OK`.
- `lumen-find-replace` build: **OK**.
- `lumen-find-replace` run: **PASS** — `LUMEN_FIND_REPLACE_OK: multi-block/toggle/table/code matching, counter, wrap-around, match case/whole word, replace current/all, one-step undo, read-only chips, no-match, session reopen, Ctrl+F and autosave save path`.
- ASan/UBSan build + run of `lumen-find-replace`: **PASS**, no reports.
- Historical: the 12:10 snapshot had no tests. The first test run failed on stale toggle/table expectations and a `FakeApi` harness UAF; ASan/UBSan also exposed the destructor QPointer UB. All three were fixed in the latest source (F7/F11/F12).

---

## 4. Test plan for D3

The new `desktop/tests/find_replace_test.cpp` already covers most of this; fix its stale expectations and the `FakeApi` UAF (F7/F12), then use this list as the acceptance checklist. Use `RichEditor` directly; `showFindBar()`/`setFindSession()`/`findNextMatch()`/`replaceFindAll()` are public.

1. **Scope/count:** document with a normal `"needle"`, a closed toggle `"▶ Toggle"` + indented `"needle child"`, a `QTextTable` cell `"needle cell"`, a code block `"needle code"`, and a reference chip `"RefTitle"`. `setFindSession("needle", false, false, false)`, `showFindBar()`, assert `findMatchCount() == 4` (or 5 with the table cell) and `findCurrentMatch() == 1`.
2. **Navigation/wrap:** `findNextMatch()` through all matches, assert the current index wraps to 1; `findPreviousMatch()` wraps back. Assert the toggle marker is **unchanged** (after removing the auto-open) or that the document is not dirty (if the auto-open is kept).
3. **Options:** `"Needle"` with match case off finds the same count; with match case on finds 0 (or only the exact-case ones). `"need"` with whole word on finds 0; `"needle"` finds the count.
4. **Replace current:** select the first match, `replaceFindCurrent("X")`, assert the text and that the next match is selected; repeat until the end; assert wrap.
5. **Replace all + one undo:** clear undo stacks, `replaceFindAll("X")`, assert the count and the text; call `document()->undo()` **once**; assert the full original text is restored. Do **not** assert `availableUndoSteps() == 1`.
6. **Reference chips:** `replaceFindAll("HACKED")` for `"RefTitle"` returns 0, the chip text is unchanged, and the notice mentions "skipped". Also test a match that partially overlaps a chip (should be skipped conservatively).
7. **Tables/code:** the current source skips table cells from replace (notice "table cells") to avoid losing the table on autosave; assert the cell is unchanged and the notice is shown. Replace inside a code block and assert the live text. If table serialization is added later, change the table expectation to a real replacement and assert `structuredBlocks()` contains a `table` object that round-trips.
8. **Stale refresh:** edit the document directly (`textCursor().insertText(...)`) without processing the 0 ms timer, then call `replaceFindAll()`; assert it refreshes and replaces the correct occurrences (after the F5 fix).
9. **Key routing:** `QTest::keyClick` on the find query with Ctrl+B/I/N/T/W; assert no app action fires (after the F4 fix). Esc from the editor and from the query closes the bar; F3/Shift+F3 navigate.
10. **Preview:** open the preview dialog, Ctrl+F, navigate to a match; assert the preview's scroll area scrolls and the main `documentScroll` does not; assert the main window's `findQuery_` is unchanged (or the intended per-editor behavior).
11. **Geometry:** show the bar, resize the window, scroll the document; assert the bar stays pinned to the top-right of the visible pane and the match is revealed below it.
12. **Read-only:** if a read-only `RichEditor` is supported, assert find works and replace is refused (or the buttons are disabled).

---

## 5. Documentation URL index

| Topic | URL |
|---|---|
| `QTextDocument::find` | https://doc.qt.io/qt-6/qtextdocument.html#find |
| `FindFlag` / `FindFlags` | https://doc.qt.io/qt-6/qtextdocument.html#FindFlag-enum |
| `QTextDocument::undo` | https://doc.qt.io/qt-6/qtextdocument.html#undo |
| `QTextDocument::availableUndoSteps` | https://doc.qt.io/qt-6/qtextdocument.html#availableUndoSteps |
| `QTextDocument::clearUndoRedoStacks` | https://doc.qt.io/qt-6/qtextdocument.html#clearUndoRedoStacks |
| `QTextDocument::undoRedoEnabled` | https://doc.qt.io/qt-6/qtextdocument.html#undoRedoEnabled-prop |
| `QTextDocument::contentsChanged` | https://doc.qt.io/qt-6/qtextdocument.html#contentsChanged |
| `QTextDocument::characterCount` | https://doc.qt.io/qt-6/qtextdocument.html#characterCount |
| `QTextCursor::beginEditBlock` | https://doc.qt.io/qt-6/qtextcursor.html#beginEditBlock |
| `QTextCursor::endEditBlock` | https://doc.qt.io/qt-6/qtextcursor.html#endEditBlock |
| `QTextCursor::joinPreviousEditBlock` | https://doc.qt.io/qt-6/qtextcursor.html#joinPreviousEditBlock |
| `QTextCursor::insertText` | https://doc.qt.io/qt-6/qtextcursor.html#insertText |
| `QTextCursor::selectedText` | https://doc.qt.io/qt-6/qtextcursor.html#selectedText |
| `QTextCursor::selectionStart` | https://doc.qt.io/qt-6/qtextcursor.html#selectionStart |
| `QTextCursor::selectionEnd` | https://doc.qt.io/qt-6/qtextcursor.html#selectionEnd |
| `QTextCursor::setPosition` | https://doc.qt.io/qt-6/qtextcursor.html#setPosition |
| `QTextCursor::currentTable` | https://doc.qt.io/qt-6/qtextcursor.html#currentTable |
| `QTextCursor::selectedTableCells` | https://doc.qt.io/qt-6/qtextcursor.html#selectedTableCells |
| `QTextEdit::ExtraSelection` (struct) | https://doc.qt.io/qt-6/qtextedit-extraselection.html |
| `QTextEdit::setExtraSelections` | https://doc.qt.io/qt-6/qtextedit.html#setExtraSelections |
| `QTextEdit::extraSelections` | https://doc.qt.io/qt-6/qtextedit.html#extraSelections |
| `QTextEdit::setTextCursor` | https://doc.qt.io/qt-6/qtextedit.html#setTextCursor |
| `QTextEdit::ensureCursorVisible` | https://doc.qt.io/qt-6/qtextedit.html#ensureCursorVisible |
| `QTextEdit::cursorRect` | https://doc.qt.io/qt-6/qtextedit.html#cursorRect |
| `QTextEdit::find` | https://doc.qt.io/qt-6/qtextedit.html#find |
| `QTextEdit::undo` | https://doc.qt.io/qt-6/qtextedit.html#undo |
| `QTextEdit::readOnly` | https://doc.qt.io/qt-6/qtextedit.html#readOnly-prop |
| `QScrollArea::ensureVisible` | https://doc.qt.io/qt-6/qscrollarea.html#ensureVisible |
| `QScrollArea::ensureWidgetVisible` | https://doc.qt.io/qt-6/qscrollarea.html#ensureWidgetVisible |
| `QScrollArea::setWidget` | https://doc.qt.io/qt-6/qscrollarea.html#setWidget |
| `QWidget::ensurePolished` | https://doc.qt.io/qt-6/qwidget.html#ensurePolished |
| `QWidget::mapTo` | https://doc.qt.io/qt-6/qwidget.html#mapTo |
| `QShortcut::context` | https://doc.qt.io/qt-6/qshortcut.html#context-prop |
| `Qt::ShortcutContext` | https://doc.qt.io/qt-6/qt.html#ShortcutContext-enum |
| `QKeySequence::StandardKey` | https://doc.qt.io/qt-6/qkeysequence.html#StandardKey-enum |
| `QEvent::Type` (`ShortcutOverride`) | https://doc.qt.io/qt-6/qevent.html#Type-enum |
| `QTextCharFormat::isAnchor` | https://doc.qt.io/qt-6/qtextcharformat.html#isAnchor |
| `QTextCharFormat::anchorHref` | https://doc.qt.io/qt-6/qtextcharformat.html#anchorHref |
| `QTextCharFormat::isImageFormat` | https://doc.qt.io/qt-6/qtextcharformat.html#isImageFormat |
| `QTextTable` | https://doc.qt.io/qt-6/qtexttable.html |
| `QTextBlock` | https://doc.qt.io/qt-6/qtextblock.html |

---

## 6. Bottom line

The core design is sound and the hardest Qt question has a favorable answer: **one document per object means replace-all is already one undo action**, and `QTextDocument::find()` already covers tables, code and toggles. The latest source has closed the 12:10 gaps: highlight-all with a flash union, removal of the toggle mutation, table/image classification, unconditional refresh before replace, `ShortcutOverride` for the find fields, theme-aware icons, preview wiring, the destructor QPointer UB, and the `FakeApi` harness UAF. `lumen-find-replace` passes, including ASan/UBSan.

Remaining (non-blocking) items:

- Esc is still not global: focus in the title or another widget does not close the bar (F6).
- The object title is not searched; decide whether "the open object" includes it (F10).
- Table cells are intentionally skipped from replace to avoid losing the table on autosave; confirm this is the product decision and document it (F8).
- Preview find and Esc-from-title are not covered by the test; add cases if those paths matter (F7/F10).
- Minor geometry/notice polish (F10).

Do not rewrite the engine or add a custom undo journal for this stage; the single-document design already gives the required one-undo replace-all.
