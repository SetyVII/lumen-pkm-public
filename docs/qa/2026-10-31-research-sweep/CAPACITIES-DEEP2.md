# Capacities Deep Dive 2 — What the Docs Don't Tell You

**Date:** 2026-10-31
**Scope:** Capacities releases 60–73, marketing site, blog, YouTube descriptions/chapters, pricing, feedback board, community mechanics, workflow content, and performance/scale signals.
**Audience:** Lumen product/design/engineering. This is a research artifact, not a spec.
**Method:** official changelog JSON (`capacities.io/api/releases.json`), release pages, docs, blog, pricing, feedback board, YouTube pages via text extraction, and public user complaints. All links are in §F.

---

## Executive summary

1. **Capacities' moat is not the feature list. It is the loop:** daily note as inbox → object types → two-way links/backlinks → contextual tasks → weekly review. Their marketing and docs repeat this loop more than any individual feature.
2. **2026 was a polish year, not a data-model year.** The big shipped items were whiteboards, global graph, Queries 2.0, audio recording, PDF/weblink readers, version history, and a calmer desktop shell. Much of the changelog is interaction detail: tabs, side panel, hover previews, focus mode, widgets, share sheet, settings search.
3. **Their open flank is performance and mobile depth.** Long notes, blank screens, and sync complaints recur in public user threads; their own roadmap now lists performance as a priority. Mobile is deliberately a companion: list-only views, no whiteboard editing, no advanced search, no formula editor. Lumen can differentiate by being fast and by choosing where to offer real mobile parity.
4. **The paid gate is simple and workflow-shaped.** Basic is generous on notes, links, daily notes, graph, and full-text search. Pro sells workflow depth: tasks, queries, calendar integrations, whiteboards, version history, media analysis, API/MCP, unlimited media. Believer is a support/beta tier (plus cosmetics). Plus is only an AI budget add-on.
5. **The most copyable UI/UX patterns are:** tabs as workspaces, side-panel stacking, the daily-note review ritual, the focus timer island, contextual tasks, the universal command palette, interactive onboarding, and the feedback → roadmap → What's New loop.

---

## A. New / missed features ranked by user value

### A.0 Release 60–73 inventory

The public JSON feed currently exposes releases 64–73 only. Releases 60–63 were fetched from their individual `whats-new` pages. The "Not explicit in your list" column is relative to the docs-parity list you gave me (object types, properties, blocks, weblinks/reader/annotations, collections, tags, queries, dashboards, daily notes/calendar, tasks/recurrence, templates, aliases, trash, version history, export/import, search/palette, graph, whiteboards, presentation, page layouts, mobile/tablet, sync/offline, MCP, API).

| Release | Date | Headline | Not explicit in your list |
|---|---|---|---|
| 60 | 2026-03-23 | New side panel, media properties, object sidebar, space templates, Trash 2.0 | Side-panel stacking/swap/edge drag; restartable onboarding; offline trash; media Category/Topic/Colors properties |
| 61 | 2026-04-01 | Image analysis, tag aliases, backlink defaults, community spotlight | Tag aliases; default open/closed state for backlinks; backlink content view setting; community Android app |
| 62 | 2026-04-14 | AI Chat Connectors, Bulk Import beta, storage rework | Bulk-import beta limits; community integration contribution path; storage rework |
| 63 | 2026-04-22 | Related Content, task deadlines, Search 3.0, UI upgrade | Related Content; deadlines; Auto/Exact/Semantic search modes; wide daily note; hover preview cards; side-panel tabs at top level |
| 64 | 2026-05-05 | Recurring tasks, task stats, model provider picker | Task occurrence log, streak stats, heatmap; skip/excuse; recurring section in task dashboard |
| 65 | 2026-05-19 | Bulk Import for everyone, image analysis Pro, side-panel Explore | Explore tab as default side panel; last-day/last-weekday recurrence; deadline carry-forward; community integrations (MelliLex, CapNote) |
| 66 | 2026-06-24 | AI Chat Connectors 2.0, tabs polish, editor spacing, block linking | Tab hover previews, pin/unpin, overflow, shrink-to-fit; block reference counter; drag objects into side panel |
| 67 | 2026-07-08 | API 2.0, weblink analysis, presentation mode, find in page 2.0 | Presentation layout rules/resume slide; find & replace across embeds/toggles/block refs; colored icons; pinnable inspector; Shared overview |
| 68 | 2026-08-06 | Mobile App 2.0, widgets, Explore AI, weblink analysis Pro | Floating mobile chrome; Home/Search as real pages; home-screen widgets; faster share sheet; floating audio player |
| 69 | 2026-08-17 | Pro+/Believer+, quieter Focus Mode, mobile polish | Focus timer island (5–60 min, chime, completion card); re-tap Home/Tasks shortcuts; graph mobile toolbar; per-space startup object |
| 70 | 2026-09-01 | PDF annotations/reader for Believers, Version History, Apple/CalDAV calendars | PDF reader/annotations/snapshots; version history cadence/limits; Apple Calendar and CalDAV; AI image generation |
| 71 | 2026-09-18 | Audio recording/transcription, calmer desktop, bulk property editing, merge tags, Reviewed daily notes, Object Studio | Audio recording; PDF export styles (Notes/Essay/Academic); bulk property edit; merge tags; Reviewed/Unreviewed daily notes; table header sort/filter; date variables; task On hold category |
| 72 | 2026-10-26 | Whiteboards, global graph, Queries 2.0, view customization, calculations, feedback board | Global graph; colored app icons; Queries 2.0; calculations; in-app feedback; widgets; per-tab history; merge objects; Trash 2.0 offline |
| 73 | 2026-10-28 | Whiteboard embeds, What's New reading position, polish | Embed a whiteboard in a page; What's New remembers reading position; focus-exit zoom; 50 linked objects in property list; large queries no longer slow sync/search |

**What this table says:** the docs-level feature set was already broad. The 2026 delta is mostly *interaction quality*: how tabs behave, how side panels stack, how capture works from a widget, how a daily note is reviewed, how a task is parsed from one line, how a query is created, how a board is embedded. That is exactly the layer docs underserve.

### A.1 Ranking method

- **Value** = daily frequency × pain relieved × differentiation. 5 = used daily by most users and visibly missing; 1 = cosmetic.
- **Effort** = rough Lumen build cost on Qt6/Compose, not Capacities' cost. S = days, M = weeks, L = multi-week/month.
- **Parity** = whether the capability is explicitly in your docs-parity list. "Partial" means the category is covered but the interaction detail is not.
- **Ranking blends value with effort and timing.** A high-value item can rank lower if it is an activation/business feature rather than a daily-use feature, or if it depends on a larger prerequisite (e.g., reader annotations need a reader).

### A.2 Ranked table

| # | Feature / pattern | Key releases | Parity | Value | Effort | Why it matters |
|---|---|---|---|---|---|---|
| 1 | Performance & large-note UX | ongoing | Not explicit | 5 | L | Their most visible public weakness; roadmap now prioritizes it. Speed is a feature users feel every minute. |
| 2 | Tabs as workspaces | 66, 72 | Not explicit | 5 | M | Per-tab history, pinned tabs, hover previews, drag-to-side-panel. Removes the "where was I?" tax. |
| 3 | Daily-note review loop | 71 | Not explicit | 4 | S | Reviewed checkbox, white/grey/green calendar dots, Unreviewed dashboard. Cheap habit/retention feature. |
| 4 | Quick capture surfaces | 68, 72 | Partial | 5 | M | Widgets, share sheet "Save in Space", WhatsApp/Telegram caption parsing, palette paste. Capture is the core habit. |
| 5 | Contextual task depth | 63, 64, 71 | Partial | 5 | M | Context auto-link, deadlines vs schedules, On hold, progress rings, recurring stats/streak/excuse, task actions. |
| 6 | Focus mode timer island | 69, 73 | Not explicit | 4 | S | Zoomed page, 5–60 min timer, chime, completion card, sound cues. High delight per line of code. |
| 7 | Query 2.0 + calculations | 72 | Partial | 4 | L | Start from all notes, natural-language query box, linked/backlinked filters, sums at top/footer, per-view customization. |
| 8 | Object hygiene toolkit | 70, 71, 72 | Partial | 4 | M | Merge objects/tags, bulk property edit, version history, offline trash. Long-term users need repair tools. |
| 9 | Reader/annotation stack | 67, 70, 71 | Partial | 4 | L | PDF/weblink reader, highlights→notes, snapshots, page-linked notes. Core research workflow. |
| 10 | Onboarding as first object/link/daily note | 60, 71 | Not explicit | 5 | M | Interactive setup, seeded content, restartable onboarding, 6-email series. Targets their stated churn reasons. |
| 11 | Search 3.0 modes + palette actions | 63, 72 | Partial | 4 | M | Auto/Exact/Semantic, `^` anchor, quotes, type filters, `((` block search, `@/type/name` create, paste anything. |
| 12 | Presentation mode | 67, 68, 69 | Partial | 3 | M | Headings become slides, `---` breaks, resume slide, watermark on free. Reuses notes; no export. |
| 13 | Calendar integrations + event↔note | 70, 72 | Not explicit | 3 | M | Apple Calendar/CalDAV, create meeting note from event, event follows note, drop block/object on day. |
| 14 | Mobile 2.0 patterns | 68, 69, 72 | Partial | 4 | L | Floating chrome, Home/Search as pages, re-tap shortcuts, week strip/month overview, Timeline, block selection panel. |
| 15 | Data view polish | 71, 72 | Partial | 3 | M | Table header sort/filter, calculations, card/list customization, 3 properties per row, empty group "New object". |
| 16 | Audio recording | 71 | Not explicit | 3 | M | Record meetings/voice notes, system audio on desktop, background recording on mobile, floating player. |
| 17 | Settings search/app zoom/per-space startup | 69, 72 | Not explicit | 2 | S | Searchable settings, app zoom, per-space "open on start" object. Polish that reduces support. |
| 18 | Whiteboard embed/JSON Canvas/mobile read-only | 72, 73 | Partial | 3 | L | Embed a board in a page, JSON Canvas export, mobile canvas/outline view. Makes boards part of the knowledge base. |
| 19 | Graph timeline/tag network/PNG | 72 | Partial | 2 | M | Timeline animation, tag network, hide indirect links, type filter, PNG export, hover preview. Discovery, not daily work. |
| 20 | Feedback/What's New loop | 72, 73 | Not explicit | 2 | S | In-app feedback form, reading position in What's New, roadmap voting, "What's not next". Trust and retention. |
| 21 | PDF export styles | 71 | Partial | 2 | S | Notes/Essay/Academic layouts. Niche but differentiating for students/writers. |
| 22 | Community integrations | 61, 65 | Not explicit | 2 | M | Raycast, Web Highlights, MelliLex, CapNote, Quick Note Pro. Ecosystem without building every connector. |
| 23 | Colored app icons | 72 | Not explicit | 1 | S | Believer cosmetic perk across phone/desktop/browser. Low priority. |

**Not shipped but worth watching:** **task reminders.** Their roadmap lists reminders as in progress, and users repeatedly ask in comments. This is an opportunity to beat Capacities to a core task feature rather than a catch-up item.

### A.3 Top 10 in detail

#### 1. Performance and large-note UX

**What shipped.** Release 72 says long pages "do less work while you type, scroll, and move the pointer," large queries no longer slow sync/search, and navigation remembers where you were. Release 67 says large data views feel faster and use less memory. Release 68 says the phone uses less memory for PDFs, images, and long sessions and starts much quicker. Release 69 says images load faster, tables stay smoother, code blocks feel faster, and PDFs scroll/zoom better. Release 73 repeats that large queries no longer slow sync/search. Their roadmap now lists performance across spaces, large notes, search, sync, and media.

**UI detail.** The user-facing surface is not a benchmark; it is a set of small behaviors: controls fold instead of stack in narrow panes, placeholders match the final layout, the outline marker follows the heading you are reading, the data view keeps its scroll position when you go back, and the offline/sync settings show whether the device is fully downloaded. Their conflict dialog shows word count, a full preview, and a "copy markdown" escape hatch before you choose local vs remote.

**Why it matters.** Public user threads complain about long notes lagging, blank white screens when switching screens, and sync losing recent edits. One farewell post described the app as feeling "like a web app wrapped in a shell." Another thread is literally titled "The app is not designed for large notes?" These are anecdotal, but they align with Capacities' own performance-focused roadmap. Performance is the one area where a smaller, native-feeling Lumen can win trust quickly.

**Lumen action.** Treat large-note performance as a product feature, not an optimization ticket. Benchmark a 100k-word note, a 10k-object space, and a 50k-link graph. Virtualize block rendering, index incrementally, keep the command palette under ~50 ms, show sync state visibly, and design a conflict dialog with preview/copy. Publish the numbers. Capacities does not publish benchmarks; Lumen can.

#### 2. Tabs as workspaces

**What shipped.** Release 66 standardized space tabs and side-panel tabs, made many tabs shrink to fit with an overflow menu, added pin/unpin from the right-click menu, double-click to close a side-panel tab, and hover previews. Release 72 made each tab keep its own history, including after a restart, with phone/tablet/computer keeping separate histories. Back/forward now move through the current tab; a preview opens over the page you were on and closing it returns you there. Pinned tabs stay on their page; opening another page opens a new tab.

**UI detail.** Each tab keeps its history, breadcrumbs, and side panel on that device. Cmd/Ctrl-click opens content in a new tab. Dragging an inactive tab to the right edge opens the side panel. Dragging a side-panel tab onto the main editor swaps it with the current page. Up to three panes can stack in the side panel. The active tab does not show a hover preview; inactive tabs do.

**Why users like it.** It turns the app into a set of parallel work contexts instead of a single browser-like page. You can keep a project, a meeting note, and a source open without losing scroll position or filters. The hover preview answers "which tab was this?" without a click. The side-panel stack is the closest thing to a research desk in a notes app.

**Lumen action.** Implement a per-tab navigation stack (page, scroll, filters, calendar day, data-view state), pinned tabs, hover preview cards, tab overflow, and drag-to-side-panel. Persist per device, not per account. This is a medium-effort, high-frequency win.

#### 3. Daily-note review loop

**What shipped.** Release 71 added a **Reviewed** checkbox to each daily note. Reviewed days show a green dot in the mini calendar; grey means a note exists but is unreviewed; white means no note. The Daily Notes dashboard has a built-in **Unreviewed** section. If you do not want the workflow, turn it off in Daily Note settings. Release 63 added an optional wide layout for daily notes, and the `⋯` menu (next to Reviewed and the mic) is visible even before a note exists so you can create one from a template.

**UI detail.** The calendar sidebar is the progress bar: white/grey/green days at a glance. The daily note header has Reviewed, a mic button for audio, and a menu for wide layout/templates. The Unreviewed section is a standing queue of days that still need attention.

**Why it matters.** This is a tiny feature with a large behavioral effect. It turns daily notes from a passive log into a reviewable habit, and it gives the calendar a reason to be opened every day. It also creates a natural weekly-review surface without building a separate review module.

**Lumen action.** Add a Reviewed toggle, three calendar dot states, an Unreviewed dashboard section, and a setting to disable it. Make the toggle visible in the daily note header and the calendar legend discoverable. This is an S-effort feature with retention value.

#### 4. Quick capture surfaces

**What shipped.** Mobile 2.0 added a faster share sheet, home-screen widgets (Quick Open, Recents, Pinned, Tasks, Quick Capture, Quick Create, Daily Note, AI Assistant, Record audio), iOS App Shortcuts/Control Center, and Android quick settings. Release 72 added a "Save in Space" share-sheet option: images, audio, links, and files become objects in the space you pick, are not added to the daily note, and can be tagged; several links/files list each item and hide the text field. WhatsApp/Telegram images use a heading or link in the caption as the title and save the rest as notes. The command palette accepts pasted text, links, files, and images. Raycast and email are additional capture paths.

**UI detail.** Widgets ask you to pick a space; the Daily Note widget can show Today/Yesterday/Tomorrow; weekday widgets use the weekday color, show a task count, and list up to three tasks; tapping a task opens it, tapping the day name opens that daily note. The share sheet closes as soon as you send the page and capture continues in the background. Multiple items become separate objects; one item keeps the text field and adds that text to the object.

**Why users like it.** Capture succeeds when it costs less than remembering. The widget/share-sheet/palette paths remove the "open app → find daily note → decide where it goes" chain. The "Save in Space" option is important because not every capture belongs in today's note; sometimes it is a source for a project.

**Lumen action.** On Android, ship an `ACTION_SEND` target, a home-screen widget, and a Quick Settings tile that write to a local queue first. On desktop, add a tray/global-hotkey capture dialog and palette paste. Add "save to space" with optional tags and background upload. Parse captions for title/notes. This is the highest-leverage capture work.

#### 5. Contextual task depth

**What shipped.** Release 63 added task deadlines, progress rings for tasks containing to-dos/subtasks, and deadline-aware views. Release 64 added recurring tasks with scheduled-date vs completion-date recurrence, skip/excuse, an occurrence log, streak stats, a heatmap, a completion log, and a Recurring section. Release 65 added last-day/last-weekday monthly recurrence and deadline carry-forward. Release 71 added **On hold** as a status category so waiting statuses are not treated as in progress in the calendar. The task page documents one-line parsing (`Prepare weekly meeting !! tomorrow 2pm`), context auto-fill from the page, a Tasks tab on every object, Inbox/Today/Scheduled/Context/Open/Completed/All views, four priorities, deadlines vs schedules, and task actions to Todoist/Things/TickTick/Apple Reminders/Google Tasks/Microsoft To Do.

**UI detail.** A task created inside a project, meeting, or person page automatically gets that object as Context. The object's Tasks tab has All/Open/Status board/Scheduled. The dashboard has Inbox (no date, no status), Today (scheduled, overdue, in progress, done today), Scheduled (grouped by date), and Completed. Recurring future occurrences show a dashed checkbox in the calendar. Status categories can be renamed/recolored/reordered; On hold is a category, not a status label.

**Why users like it.** It keeps the "why" attached to the "what." You capture where the work lives, and the task resurfaces there. The one-line parser and automatic context remove the second screen. The recurring stats/heatmap make habits visible without a separate habit tracker.

**Lumen action.** Auto-fill context from the current object; parse `!`/`!!`/`!!!` and natural-language dates; separate deadline from schedule; add On hold as a status category; show progress rings; implement recurring occurrence logs/streaks/excuse; add external task actions. Then beat them to **reminders**, which are still only on their roadmap.

#### 6. Focus mode timer island

**What shipped.** Release 69 replaced the old focus mode with a quieter writing space: the page zooms in, and a small island at the bottom left keeps Focus, an optional timer (5–60 minutes), and page navigation within reach. The last duration is remembered. When time is up, a short chime plays and a completion card stays until you continue. Entering and leaving Focus Mode also plays a short sound cue. Release 73 says leaving focus mode zooms the page back out so you can still see it.

**UI detail.** The island is deliberately minimal: focus toggle, timer, back/forward. The rest of the interface steps aside. The timer is optional, so focus mode can be pure distraction-free writing or a pomodoro-like session. The completion card is persistent, not a toast that disappears.

**Why users like it.** It turns "deep work" into a one-click ritual with a clear end. The sound cues and completion card give a small reward loop. The zoom transition preserves context, so leaving focus does not feel like closing a document.

**Lumen action.** Build a focus mode with a page zoom transition, a bottom-left island, a 5–60 minute timer with remembered duration, a chime, a completion card, and optional sound cues. Put the entry point in the sidebar footer next to theme/account, as Capacities does. S-effort, high delight.

#### 7. Query 2.0 + calculations

**What shipped.** Release 72 made a new query start from **all notes** instead of a type, added a query creation box where you describe what you want, allowed saving a backlink view as a query, and made queries editable on mobile. New filters include blocks content empty/not empty, linked objects (count/include/exclude/type), and backlinked objects. Calculations (sum, count, average, earliest date) show at the top of most views or in a table column footer, and work across groups. View customization is per view/query: card/list layouts, up to three properties on a list row, tags as one of those properties. Groups can include multiple object types; dates can group by hour; a query can use "This object" as a reference; embedded queries can use that variable on a text property too.

**UI detail.** The query editor starts with "All notes" and a "Describe a change" box. The customize menu holds layout, properties, calculations, and grouping. The view controls stay in the order Edit → layout → Customize. On narrow screens, Edit and Query become icons so the search field keeps its width. A collapsed query shows its full title; an embedded query's title shortens before the option buttons.

**Why users like it.** It turns the space into a set of standing questions ("Open projects," "Tasks per client," "5-star books," "Definitions not reviewed in 3 months") without building a dashboard. Starting from all notes is more intuitive than starting from a type, because most questions are cross-type. Calculations make data views useful for inventories, budgets, and reading stats.

**Lumen action.** If Lumen's query builder starts from a type, add a "start from everything" mode. Add a natural-language "describe the query" box that maps to filters. Add linked/backlinked object filters, blocks-empty filters, calculations, per-view layout/property customization, and mobile query editing. This is L-effort but a core power-user differentiator.

#### 8. Object hygiene toolkit

**What shipped.** Release 70 added Version History: browse/restore earlier versions of a page, task, or file; a new version every 15 minutes after edits; up to 50 versions per object; each saved for 30 days; preview before restore. Release 71 added merging one tag into another and bulk property editing: select several objects, edit shared properties; same-type objects can edit text, numbers, checkboxes, icons, dates, labels, links, tags, aliases, collections; mixed types can still edit title, description, icon, aliases, tags. Release 72 added merging one object into another of the same type: keep one, fill empty properties from the other, append content, repoint links. Trash 2.0 can be opened offline; permanent delete/empty still needs a connection; deleted items restore for 30 days; emptying asks you to type "Delete" in your app language.

**UI detail.** Merge lives in the object menu. Version History lives in the object menu and shows filled-property/block counts per version. Bulk edit is in the multi-select action menu. Object pickers exclude trash. The trash can be opened offline, which matters when you are trying to recover something on a plane.

**Why it matters.** Long-lived knowledge bases accumulate duplicates, stale tags, and accidental edits. Merge/bulk-edit/version-history/trash are the repair toolkit that makes a system safe to use for years. They are also the safety net for AI edits, which Capacities explicitly markets.

**Lumen action.** Prioritize merge objects/tags and bulk property edit before advanced automation. Add version history with a clear cadence and retention policy, offline trash, and restore. Make these discoverable in the object menu and multi-select bar, not buried in settings.

#### 9. Reader/annotation stack

**What shipped.** Release 67 introduced weblink analysis, reader view, and annotations for Believers; release 68 moved them to Pro. Release 70 added PDF annotations, snapshots, reader view, and analysis for Believers; release 71 moved PDF annotations/reader/analysis to Pro. Release 73 fixed highlights moved into embedded objects or turned into objects so they stay on the PDF/webpage; the Reader opens at the page you were on; each annotation can jump to its note.

**UI detail.** A weblink has Browser / Reader / Summary tabs. A PDF has PDF / Reader / Summary tabs. Selecting text lets you highlight, pick a color, and add a note. Passages with notes show a small icon; hover for a preview; jump back to the place in the PDF. On desktop you can capture a snapshot of an area and drop it into notes; scrolling to the snapshot returns you to its surrounding context. Search inside a PDF can land on the matching page. The PDF viewer has its own dark-mode toggle.

**Why users like it.** It turns reading into note-taking without leaving the app. Highlights become note blocks, so they are linkable and searchable. Snapshots preserve the visual context that text extraction loses. Page-linked notes are the difference between a quote and a source you can revisit.

**Lumen action.** Build the reader/highlight/snapshot layer even if AI analysis is out of scope. The non-AI parts (reader view, highlights, notes, page links, search, snapshots) are the durable value. Add "turn highlight into object" and "jump to note/page" early.

#### 10. Onboarding as first object/link/daily note

**What shipped.** Release 60 added a new space-creation flow with templates and a restartable onboarding from Account Settings that creates a new space without touching existing content. The March 2026 blog post explains the redesign: an interactive onboarding that takes users through creating their first object, first link, and first daily note, then greets them with content they just made plus further resources. They also updated the onboarding email series to six daily emails sharing "aha moments." Release 71 improved Object Studio into a gallery of object types with built-in queries, and added a "Basic Setup" for new spaces (people, meetings, projects, books). Release 72 added searchable settings and continued onboarding fixes (object types chosen at signup persist; mobile First steps fills the screen).

**UI detail.** The onboarding is interactive, not a carousel. The user creates real content, sees it on the daily note, and lands in a non-empty app. The space templates and Object Studio give a head start without locking the user in. The restartable onboarding is a safe reset that does not delete the old space.

**Why it matters.** Capacities states its biggest churn reasons are "I'm not using the app enough" and "it's too complicated." Onboarding targets both: it flattens the perceived learning curve, gets users into the app quickly, and makes the core concepts (objects, links, daily notes) concrete. The seeded content avoids the blank-canvas problem.

**Lumen action.** Replace any tour-only onboarding with an interactive first object → first link → first daily note flow. Seed the new space with the user's own creations plus a few templates. Add a restartable onboarding that creates a new space. Follow up with a short email series of concrete "aha" workflows. This is activation work, not cosmetic.

### A.4 Watchlist (next 10 + extras)

- **Search 3.0:** Auto/Exact/Semantic modes; `^` anchor search; quoted phrases; type filters; `((` block search; `@/person/Name`, `/task/...`, `+meeting/...` creation patterns; paste anything into the palette; command actions ("Open today", "Create task", "Send feedback"); extended search as a full page (Pro). This is a daily-use surface.
- **Presentation mode:** headings H1–H4 become slides, `---` adds a custom break, title slide, trailing image side-by-side, auto text sizing, toggles independent of the note, continuous numbered lists, toolbar auto-hide, remembers last slide. Free plan has a watermark; Pro removes it; desktop/tablet only. Cheap delight, especially for students/teachers.
- **Calendar integrations:** Google, Microsoft, Apple Calendar, CalDAV (Nextcloud, Fastmail); create a meeting note from an event; the note follows the event when it moves; create an event from a date; drop a block/object on a day to create that day's note and put the content there; calendar in the side panel; Day view tabs.
- **Mobile 2.0:** floating top bar/tabs/search over content; glass chrome; Home and Search as real pages; 4–6 bottom tabs; re-tap shortcuts (Home → space switcher, Calendar → focus daily note, Calendar long-press → quick note, Tasks → new task, Search → leave); week strip + month overview; Daily note/Tasks/Timeline modes; block selection action panel above the keyboard; list-only views; no whiteboard editing.
- **Data view polish:** table header sort/filter; calculations; card/list customization per view; up to three properties per row; empty group "New object"; responsive controls; drag-rectangle selection; progress bars; number formats (unit, compact, prefix/suffix).
- **Audio:** record from New > Audio, daily note mic, widget, or mobile; system audio on desktop; background recording on mobile; audio object with player; transcript/summary via Analyze (AI). Recording itself is a non-AI feature; transcription is the AI part.
- **Settings:** grouped Account/General/Space/Integrations; searchable; app zoom; per-space "when this space opens" (where I left off, calendar, today, specific object); offline/sync state; diagnostics footer.
- **Whiteboard polish:** embed a board in a page; cards vs embeds; multi-select view changes; per-board zoom/position; keyboard add connected text node; JSON Canvas export; mobile canvas/outline read-only. If whiteboards are already in the parity program, these are the details that make them feel native.
- **Graph polish:** global graph in sidebar; search narrows; timeline animation; tag network; hide indirect links; type filter; PNG export; hover preview; object count; Cmd-click new tab.
- **Feedback/What's New:** in-app feedback form; What's New remembers reading position per device; new version opens at top; release notes with video; roadmap voting; "What's not next".
- **PDF export styles:** Notes (looks like the page), Essay (single-column reading), Academic (compact two-column, numbered headings). Pro/Believer.
- **Community integrations:** Raycast, Web Highlights, MelliLex (Windows Ctrl+right-click dictionary), CapNote (macOS menu-bar daily-note append), Quick Note Pro (Android quick capture). OAuth required for new integrations.
- **Colored app icons:** Believer cosmetic perk; phone/desktop/browser.
- **Task reminders:** not shipped; roadmap "in progress." Users repeatedly ask in comments. This is an opening, not a catch-up item.

### A.5 Recommended sequence for Lumen

**Sprint 1 (cheap, high-frequency):**
1. Daily-note review loop (Reviewed, calendar dots, Unreviewed).
2. Focus mode timer island.
3. Tabs with per-tab history + pinned tabs.
4. Command palette actions + paste-anything.
5. Settings search + app zoom.

**Sprint 2 (core UX):**
6. Quick capture surfaces (Android share intent/widget/tile; desktop tray/global hotkey; palette paste).
7. Contextual task depth (auto-context, one-line parse, deadlines vs schedule, On hold, progress rings).
8. Task reminders (differentiator; Capacities has it only on the roadmap).
9. Object hygiene (merge objects/tags, bulk property edit, offline trash).
10. Interactive onboarding + seeded content + restartable flow.

**Sprint 3 (power/differentiation):**
11. Query 2.0 (start-from-all, NL box, linked/backlinked filters, calculations, per-view customization).
12. Reader/annotation stack (reader, highlights→notes, snapshots, page links).
13. Presentation mode.
14. Performance benchmarks + large-note UX.

**Performance is a parallel track, not a Sprint 3 item.** Start the benchmark harness in Sprint 1, then ship virtualized rendering and incremental indexing as the features land. The sequence above is about user-visible features; performance work should run alongside it.

### A.6 Copy / don't copy

**Copy:**
- The content-first calm shell and semantic color system.
- Tabs as workspaces + side-panel stacking.
- The daily-note review loop.
- Contextual tasks + one-line capture.
- The command palette as a universal action bar.
- Interactive onboarding with seeded content.
- Query "start from everything" + natural-language box.
- Object hygiene (merge, bulk edit, version history, offline trash).
- Reader/highlights/snapshots/page links.
- The feedback → roadmap → What's New loop.
- Performance discipline and public benchmarks.

**Don't copy:**
- The AI-first roadmap and AI budget complexity (out of scope by policy).
- Mobile as a companion with silent feature gaps. Choose companion or parity explicitly.
- The no-collaboration/no-E2EE/no-plugins non-goals. These are deliberate trade-offs; Lumen can choose differently where it has a right to win.
- Whiteboards as a headline before the basics. They are expensive and mobile read-only.
- Graph physics overinvestment. It is a discovery surface, not a daily driver.
- The presentation watermark. Lumen can be more generous.
- Colored app icons and other cosmetics before core UX.
- The Pro/Pro+/Believer/Believer+ pricing surface. Keep non-AI plans simple.
- The specific "what's not next" list. Copy the transparency, not the conclusions.

---

## B. UI/UX pattern catalog

These are interaction patterns, not features. Each entry says what it looks like, why users like it, and what Lumen should take from it. Cross-references to §A are intentional; the catalog is the design vocabulary, §A is the priority list. *Why users like it* is a synthesis of Capacities' marketing copy, release notes, and public comments, not a controlled usability study.

### B.1 Shell and navigation

**1. Content-first canvas.**
- *What it looks like:* the main content sits on a rounded, slightly lifted panel; the surrounding area is a darker calm gray; there are few separator lines; shadows in light mode and contrast in dark mode do the elevation work. Colors are semantic: green for create, blue for reuse, red for delete, orange for tags, violet for emoji.
- *Why users like it:* it puts the note, not the chrome, at the center of attention. The semantic colors make the UI learnable without labels. The 2022 redesign post describes this as "lifting the content above the rest of the user interface" and "less lines and separators."
- *Lumen note:* audit the shell for equal-weight sidebars/toolbars. Give the editor a distinct surface and reserve saturated color for meaning, not decoration.

**2. Left sidebar as a stable map.**
- *What it looks like:* space switcher top-left; `+ New Content`, search, AI, Calendar, Tasks as quick access; object types listed below; collapsible pin sections; Help/Feedback/Trash at the bottom; settings, focus mode, theme, and account at the very bottom. Double-clicking Calendar goes to Today; Shift-clicking Today opens that day's daily note in the side panel.
- *Why users like it:* the map never changes shape. You can go to a type, a pin, today, or a tool without a hamburger menu. The bottom corner is a consistent "app control panel."
- *Lumen note:* keep global navigation in one stable rail; avoid moving core destinations into per-page menus. Put focus/theme/account in the same footer cluster.

**2b. Space overview as a personal dashboard.**
- *What it looks like:* Home/Space Overview is a full page, not a launcher. It shows the space switcher, theme toggle, and settings at the top; object types; recents/pinned/custom sections; and an Account & support area with Help, the mobile Guide, and What's New. On desktop, sidebar sections are pins; the overview is the full-page version. You can rename/delete the default Pinned section (at least one section stays) and drag pins/types between sections.
- *Why users like it:* it makes a space feel like a place with a front page, not a database with a sidebar. New users see their content instead of an empty canvas. Custom sections are a lightweight dashboard without building queries.
- *Lumen note:* make Home a real page with recents, pins, types, and a help/What's New area. Support custom sections and drag/drop. Do not make Home a blank launcher or a modal.

**3. Tabs as workspaces.**
- *What it looks like:* a top tab strip where each tab keeps its own history, breadcrumbs, and side panel. Pinned tabs stay put. Many tabs shrink to fit and spill into an overflow menu. Hovering an inactive tab shows an object preview. Cmd/Ctrl-click opens content in a new tab; dragging an inactive tab to the right edge opens the side panel; dragging a side-panel tab onto the main editor swaps it.
- *Why users like it:* it supports parallel work without losing place. The hover preview answers "which tab is this?" before you click. Per-tab history means Back is predictable.
- *Lumen note:* this is the highest-value navigation pattern in the catalog. Implement per-tab state first, then pinned tabs, then hover previews, then drag-to-panel.

**4. Side panel as a context desk.**
- *What it looks like:* a right-side panel that opens with an **Explore** tab by default (graph, backlinks, objects inside, related content). You can open an object with Shift-click, stack up to three panes, resize them, drag a tab into the main editor to swap, or drag an object from a view/sidebar into the panel. Dragging from the right edge opens it.
- *Why users like it:* it keeps the main page intact while you consult a source, a backlink, or a related note. The Explore tab gives the blank panel a purpose instead of a dead rectangle. Stacking three panes feels like a research desk.
- *Lumen note:* make the empty side panel useful (recent/related/backlinks), support at least two stacked panes, and allow drag/drop into it. This pairs naturally with tabs.

**5. Preview modal as a low-commitment peek.**
- *What it looks like:* clicking a link opens a fully editable preview over the current page. Closing it (or going Back) returns to the exact page, scroll, and filters. In a collection or tag, arrow keys browse the previews.
- *Why users like it:* it answers "what is this?" without a navigation commitment. Because it is editable, it also supports quick fixes. The underlying page stays alive.
- *Lumen note:* distinguish "peek" from "open" explicitly. Preserve the underlying page state. Add arrow-key browsing in list/collection contexts.

**6. Command palette as a universal action bar.**
- *What it looks like:* `Cmd/Ctrl+P` opens a full-text search over content plus commands. Prefixes: `^` anchor search, quotes for exact phrases, type names to filter, `((` for block search, `@/person/Name` or `/task/...` or `+meeting/...` to create. Pasting text/links/files/images adds them to the space. Actions include "Open today," "Create task," "Send feedback," "Open docs," "Open community."
- *Why users like it:* it collapses navigation, creation, and capture into one keyboard surface. It is forgiving (semantic/auto modes) but can be strict (exact mode). It is the fastest path for power users and a discovery tool for new users.
- *Lumen note:* one palette, multiple modes, visible shortcuts, recent/frequent ranking, and paste-anything. Keep it under ~50 ms. This is one of the cheapest high-value surfaces.

**7. Settings as a searchable map.**
- *What it looks like:* settings grouped into Account, General, Space, and Integrations; a search box that also lists a page's sections when the page name matches; app zoom; per-space "when this space opens" (where I left off, calendar, today, specific object); offline/sync state; a diagnostics footer.
- *Why users like it:* it scales without a maze. Per-space startup is a small feature that makes multiple spaces feel intentional. Searchable settings reduce support load.
- *Lumen note:* group by scope (account vs space vs device), add search, and make startup behavior per-space/per-device. Put diagnostics behind a clear "copy details" action.

### B.2 Writing and focus

**8. Floating inspector.**
- *What it looks like:* a slim indicator on the right edge of an object page. Hover to open a two-tab inspector: **Outline** (automatic table of contents from headings) and **Info** (text stats, created-at). A pin button keeps it open. The outline tab stays visible even with no headings, with an empty state.
- *Why users like it:* it gives long-document navigation without a permanent sidebar. Pinning supports long editing sessions. The empty state teaches what the panel is for.
- *Lumen note:* implement a hover/pin inspector with outline + stats; keep it out of the way until needed. This is cheap and makes long notes feel navigable.

**9. Focus island.**
- *What it looks like:* focus mode hides the UI and zooms the page. A bottom-left island holds focus toggle, optional 5–60 minute timer, and back/forward. Last duration is remembered. A chime and completion card mark the end; entering/leaving plays a sound cue; exiting zooms back out.
- *Why users like it:* it is a one-click deep-work ritual with a clear end and a small reward. It does not require a separate timer app.
- *Lumen note:* copy the island, the remembered duration, the completion card, and the zoom transition. Keep sounds optional.

**10. Block-first editor chrome.**
- *What it looks like:* block drag handle; slash menu; `@`/`[[` linking; `#` tags; `()` task; `---` divider; `/standard` and `/small`; a formatting bar that appears with the selection; block selection; hover previews on object links; code blocks with 100+ languages, autocomplete, Mermaid, line numbers; tables with rich text; toggles; two-column blocks; embeds.
- *Why users like it:* the editor is powerful without a modal dialog for every block type. The same symbols work on desktop and mobile. The block handle makes structure tangible.
- *Lumen note:* keep the block menu and slash menu consistent; make the block handle the primary structural affordance; ensure mobile has the same symbols, even if the action panel differs.

**11. Presentation as note reuse.**
- *What it looks like:* fullscreen overlay; H1–H4 headings start slides; `---` adds a custom break; the first slide is a centered title; a trailing image on a slide goes side-by-side with text; short slides get larger text; toggles start in the note's state but can be flipped without changing the note; numbered lists continue across slides; the toolbar auto-hides; the last slide is remembered; free plan has a watermark.
- *Why users like it:* the note is the deck. No export, no rebuilding. The layout rules are predictable enough to trust. The watermark is a soft upsell, not a hard block.
- *Lumen note:* build presentation on top of the existing block renderer. The rules (heading = slide, divider = break, trailing image = side-by-side) are the whole feature.

**12. Find and replace in page.**
- *What it looks like:* `Cmd/Ctrl+F` opens a find bar in the app; it searches the whole object including closed toggles, embeds, block references, tables, and code blocks; a replace toggle adds a second field; match case/whole word live in the bar menu. It works in the side panel and in Reader view.
- *Why users like it:* it matches the browser/OS expectation but works across the note's hidden structure. Replace is scoped to one object, which is the right safety boundary.
- *Lumen note:* implement in-page find across embeds/toggles/block refs; keep replace scoped to the open object; show match count and arrows.

**13. Semantic color system.**
- *What it looks like:* green = create, blue = reuse/link, red = delete, orange = tags, violet = emoji; tags get a random color on creation; icons can be colored; the color picker is searchable.
- *Why users like it:* color becomes a navigation cue, not decoration. Users learn the app faster. The palette stays calm because saturated color is rare.
- *Lumen note:* define a semantic palette and enforce it in menus, buttons, and hints. Avoid color as the only signal (accessibility), but use it consistently.

### B.3 Time and tasks

**14. Calendar as home.**
- *What it looks like:* Month/Week/Three days/Day views; Day view tabs for the daily note, **Date references**, and **Created today**; a right-side mini calendar colored white/grey/green by daily-note review state; quick-add buttons for object types with date properties; a natural-language date picker (`in one month`, `next Wednesday`, `$start`/`$end` variables); date links; date ranges; a calendar sidebar listing timed objects.
- *Why users like it:* time is the spine of the app. The calendar is not a separate module; it is the front door to daily notes, tasks, and dated objects. The review colors make progress visible.
- *Lumen note:* make the calendar a first-class home, not a plugin. Add date-reference and created-today tabs; add review-state colors; support natural-language dates and date links.

**15. Daily note as inbox + review ritual.**
- *What it looks like:* one page per day, next to the calendar; a Reviewed checkbox; mic button; `⋯` menu for wide layout/templates; tags; backlinks; incoming WhatsApp/Telegram/email content appended automatically; an Unreviewed dashboard section.
- *Why users like it:* it is the lowest-friction capture surface and the natural weekly-review unit. The Reviewed checkbox turns it into a habit. The wide layout supports long journaling days.
- *Lumen note:* treat the daily note as the default inbox, not a special page type. Add review state, template menu, and input integrations. Do not make users create the note manually.

**16. Task dashboard without setup.**
- *What it looks like:* Inbox/Today/Scheduled/Context/Open/Completed/All tabs; a Tasks tab on every object with All/Open/Status board/Scheduled; one-line capture (`Prepare weekly meeting !! tomorrow 2pm`); context auto-filled from the current page; four priorities; deadlines vs schedules; status categories; progress rings; recurring tasks with stats/heatmap/excuse/skip; task actions to external apps.
- *Why users like it:* it works out of the box and stays contextual. You do not build a board before you can use it. The task remembers the project/person/meeting it came from.
- *Lumen note:* auto-context and one-line parsing are the magic. Build the default views before custom query sections. Add On hold and progress rings. Keep external task actions as an escape hatch, not a requirement.

**17. Task actions as escape hatch.**
- *What it looks like:* a task can be sent to Todoist, Things, TickTick, Apple Reminders, Google Tasks, or Microsoft To Do in one step; Capacities keeps the link back to the source.
- *Why users like it:* it respects existing habits. You do not have to abandon your task app to use Capacities for notes. It positions Capacities as the map, not the daily checklist.
- *Lumen note:* if Lumen builds tasks, add export/handoff to one or two popular apps before building a full task ecosystem. The link back is the important part.

### B.4 Data and discovery

**18. Data view pill strip + per-view customization.**
- *What it looks like:* a consistent pill strip at the top-right of every data view (Edit, layout, Customize); cards/list/gallery/wall/table/kanban; per-view card/list customization; up to three properties on a list row; table header sort/filter; calculations at the top or column footer; empty groups have "New object"; controls fold on narrow screens; drag-rectangle selection; progress bars; number formats.
- *Why users like it:* the same controls appear everywhere, so a new view is learnable in seconds. Per-view customization lets the same data look right in different contexts without changing the type. Calculations make views useful, not just pretty.
- *Lumen note:* standardize the view-control strip and the customize menu. Add calculations and header sort/filter before advanced charting. Keep the row-property limit (three) to avoid clutter.

**19. Query as standing question.**
- *What it looks like:* a new query starts with all notes; a "Describe a change" box; filters by type, tag, property, date, links, backlinks, blocks empty/not empty; "This object" variable; save a backlink view as a query; embed the query; edit on mobile; search mode Auto/Exact/Semantic.
- *Why users like it:* it turns a question into a living view. Starting from all notes matches how people think ("show me everything about X"), and the natural-language box lowers the barrier. Embeds make the query reusable on project/person pages.
- *Lumen note:* start-from-all + NL box + linked/backlinked filters are the three highest-value changes. Add calculations and per-view layout next.

**20. Graph as serendipity surface.**
- *What it looks like:* global graph in the sidebar; search narrows the graph to a topic; timeline animation by creation date; tag network; hide indirect links; filter by type; export PNG; hover preview; object count; Cmd-click opens in a new tab.
- *Why users like it:* it is a "fun" way to rediscover notes and spot missing links. The topic search makes it useful, not just decorative. The timeline animation is shareable.
- *Lumen note:* do not overinvest in graph physics. Ship global graph + topic search + type filter + PNG export. Treat it as a discovery surface, not a daily driver.

**21. Whiteboard as spatial layer.**
- *What it looks like:* infinite canvas; cards are existing objects; add with `@`, search, drag/drop, or paste; `+` creates embeds; frames; text/shapes/arrows; arrow labels and styles; PDF page flip and audio playback on the board; multi-select view changes; per-board zoom/position; embed a board in a page; JSON Canvas export; mobile canvas/outline read-only.
- *Why users like it:* it is a different way to see the same notes, not a separate silo. The cards open into the real objects. Frames and arrows make it useful for research, moodboards, and diagrams.
- *Lumen note:* if whiteboards are already planned, prioritize "cards are real objects," frames, arrows, embed-in-page, and JSON Canvas export. Mobile read-only is a reasonable scope cut.

**22. Search as three-mode retrieval.**
- *What it looks like:* everyday search is forgiving; queries can choose Auto/Exact/Semantic; `^` anchors to the start; quotes force phrases; type names filter; `((` searches blocks; `@/type/name` creates while linking; extended search is a full page with filters; results show snippets and type.
- *Why users like it:* it handles both "I know the exact word" and "I know the idea." The modes are explicit, so users can correct bad results. The create-while-linking patterns keep flow.
- *Lumen note:* expose search modes in queries, not just a hidden setting. Add block search and create-from-search. Keep exact match ranked above fuzzy prefix.

**23. Media as first-class objects.**
- *What it looks like:* every image, PDF, audio, and weblink is an object with properties, tags, links, and notes; a media page has viewer/reader/summary tabs; highlights become note blocks; snapshots link back to a page region; audio has a floating player; images can be analyzed for OCR/colors/category (AI).
- *Why users like it:* sources are not attachments; they are thinkable objects. You can tag, link, query, and annotate them like notes. The reader view removes ads and clutter.
- *Lumen note:* make media objects first-class even without AI. Reader/highlights/snapshots/page links are the durable core. AI analysis can come later.

### B.5 Capture, mobile, and community

**24. Capture at the edge.**
- *What it looks like:* home-screen widgets; share sheet with space/destination/tags; WhatsApp/Telegram/email input; Raycast/global launcher; command-palette paste; mobile Quick Note; community apps like Quick Note Pro.
- *Why users like it:* capture happens where the thought happens. The user does not have to open the app, find the daily note, and decide where it belongs. Background upload makes it feel instant.
- *Lumen note:* build the Android share intent + widget + tile first; then desktop tray/global hotkey; then palette paste. Add "save to space" with tags, not just "append to daily note."

**25. Mobile 2.0 chrome.**
- *What it looks like:* floating top bar, tabs, and search over content; glass chrome; Home and Search as real pages; 4–6 bottom tabs (Home, Calendar, Tasks, Create, Explore, Search); re-tap shortcuts (Home → space switcher, Calendar → focus daily note, Calendar long-press → quick note, Tasks → new task, Search → leave); Home is a full-screen Space Overview with space switcher, theme toggle, settings, object types, recents/pinned/custom sections, and Account & support (Help, mobile Guide, What's New); week strip + month overview; Daily note/Tasks/Timeline modes; block selection action panel above the keyboard; list-only views; no whiteboard editing.
- *Why users like it:* the content gets more of the screen, and the tab bar doubles as a shortcut surface. The re-tap behaviors are discoverable through use. The mobile app is honest about being a companion, not a shrunken desktop.
- *Lumen note:* decide whether Lumen wants a companion or full-parity mobile. If companion, copy the tab/re-tap model and the capture focus. If full parity, the list-only/table/whiteboard gaps are the differentiator.

**26. Onboarding as first object/link/daily note.**
- *What it looks like:* interactive creation of a first object, first link, and first daily note; seeded content from the onboarding; a non-empty home; space templates; Object Studio gallery with types and queries; restartable onboarding that creates a new space without deleting the old one; six daily emails with "aha" workflows.
- *Why users like it:* it teaches by doing, not by tour. The user sees their own content in the app before the first empty state. The restartable flow is a safe reset for people who want a clean start.
- *Lumen note:* replace carousel onboarding with a 3-step create/link/daily-note flow. Seed the space. Add a restart that preserves the old space. Follow up with concrete workflow emails.

**27. Feedback/What's New loop.**
- *What it looks like:* an in-app Feedback form in the sidebar; a public feedback board with boards for ideas/bugs, statuses (Open, In Progress, Under Review), votes, comments, search/filter, and auto-attached diagnostics; a roadmap page with Planned/In Progress/Recently launched; a What's New dialog with video + highlights + full notes that remembers reading position per device; a "What's not next" page.
- *Why users like it:* it closes the loop. Users see their vote, status, and release note. The in-app form removes the "where do I report this?" friction. The "What's not next" page prevents endless requests.
- *Lumen note:* copy the loop, not the exact tool. Add an in-app feedback form with diagnostics, a public roadmap with statuses, and a What's New surface that remembers where the user stopped reading. Publish a "what's not next" page to set expectations.

---

## C. Plan-gating matrix

### C.1 Prices and plan logic

As of 2026-10-31:

- **Basic:** free forever. Unlimited spaces, objects, blocks; sync across devices; unlimited custom object types; import/export; capture from WhatsApp/Telegram/Twitter/email; view-only sharing; full-text search; offline support; media uploads 5 GB total, then 100 MB/month.
- **Pro:** $9.99/month billed yearly (16% off) or $11.99/month. Everything in Basic plus AI assistant, whiteboards, task management, smart queries, calendar integration, image/PDF/weblink analysis, Readwise/Kindle, version history, AI Chat Connectors, public API, unlimited media (fair use), task actions.
- **Believer:** from $12.49/month billed yearly or $14.99/month. Everything in Pro plus beta access to major new features, colored app icons, and support for the independent team. Same feature set as Pro; the difference is early access and cosmetics/support.
- **Plus (Pro+ / Believer+):** $8.33/month billed yearly. Optional AI budget add-on: much larger monthly AI budget, top-ups (including auto top-up). All AI workflows/models remain available on Pro/Believer; Plus only changes budget. Top-ups require Plus.
- **AI Chat Connectors budget nuance:** chats through ChatGPT/Claude/Cursor run on the plan you already pay for there; they do not spend the Capacities AI budget. The connector itself is a Pro feature.
- **Trial:** free Pro trial in the app, no credit card. Cancel anytime; downgrade to Basic; content retained.
- **Discounts:** Back to School 40% off annual Pro for students (current promo); Pro discount waiting list. Believer cannot be discounted.
- **Billing:** card, PayPal, Apple Pay, Google Pay; Paddle is merchant of record; VAT support.
- **No team/organization plans.** Capacities is explicitly for individuals.

### C.2 Feature matrix

| Capability | Basic | Pro | Believer | Plus add-on |
|---|---|---|---|---|
| Unlimited spaces/objects/blocks | Yes | Yes | Yes | n/a |
| Unlimited custom object types | Yes | Yes | Yes | n/a |
| Daily notes, backlinks, graph view | Yes | Yes | Yes | n/a |
| Full-text search | Yes | Yes | Yes | n/a |
| Smart search by type/tag/meaning (extended/semantic) | No | Yes | Yes | n/a |
| Related Content and Unlinked Mentions | No | Yes | Yes | n/a |
| Whiteboards | No | Yes | Yes | n/a |
| Version history | No | Yes | Yes | n/a |
| Presentation mode | Yes, with watermark | Yes, no watermark | Yes, no watermark | n/a |
| Focus mode | No | Yes | Yes | n/a |
| Tabs and side panel | Yes | Yes | Yes | n/a |
| Merge objects/tags | Likely free (not labeled Pro) | Yes | Yes | n/a |
| Bulk property editing | Likely free (not labeled Pro) | Yes | Yes | n/a |
| Reviewed daily notes / Unreviewed section | Likely free (not labeled Pro) | Yes | Yes | n/a |
| Queries (saved filters, embeds, calculations) | No | Yes | Yes | n/a |
| Collections and tags | Yes | Yes | Yes | n/a |
| Type dashboards/sections | Default type views | Yes + query sections | Yes + query sections | n/a |
| Calendar and daily notes | Yes | Yes | Yes | n/a |
| Calendar integrations (Google/Microsoft/Apple/CalDAV) | No | Yes | Yes | n/a |
| Task management (Inbox/Today/Scheduled/Context/Open/Completed) | No (to-do blocks only) | Yes | Yes | n/a |
| Recurring tasks | No | Yes | Yes | n/a |
| Task actions (Todoist/Things/TickTick/Reminders/Google Tasks/MS To Do) | No | Yes | Yes | n/a |
| Task reminders | Not shipped | Not shipped | Not shipped | n/a |
| AI assistant (all workflows/models) | No | Light monthly budget | Light monthly budget | Larger budget + top-ups |
| AI Chat Connectors / MCP | No | Yes | Yes | n/a |
| Public API | No | Yes | Yes | n/a |
| Media uploads | 5 GB total, then 100 MB/month | Unlimited, fair use | Unlimited, fair use | n/a |
| Image/PDF/weblink analysis | No | Yes | Yes | n/a |
| Audio recording | No | After early access | Early access | n/a |
| Readwise/Kindle import | No | Yes | Yes | n/a |
| Import/export (Markdown, CSV, PDF) | Yes | Yes | Yes | n/a |
| Whiteboard JSON Canvas export | No | Yes | Yes | n/a |
| Automated exports | Yes | Yes | Yes | n/a |
| View-only sharing / public read access | Yes | Yes | Yes | n/a |
| PDF export styles (Notes/Essay/Academic) | No | Yes | Yes | n/a |
| Formulas in tables | No | Yes | Yes | n/a |
| Colored app icons | No | No | Yes | n/a |
| Beta access to major features | No | No | Yes | n/a |
| Priority support | No | No | Early Believer benefit (not on current matrix) | n/a |

**Caveats:** Some release notes label a feature Pro/Believer but the pricing page does not list it separately (e.g., merge/bulk edit/reviewed notes are not labeled Pro in the release notes). I marked those "not labeled Pro" rather than asserting they are free. Presentation mode is documented as free with a watermark, which is a softer gate than the pricing page implies. Audio recording is Believer early access and is planned for Pro after early access. The Pro page also lists "Formulas in tables," "Curated icon set," "Raycast," and "Web Highlights" under "Every Pro feature," but the pricing page does not list them individually; treat them as likely Pro and verify before gating.

### C.3 Paid capabilities not covered by your docs-parity list

These are the paid capabilities that are either absent from, or only partially covered by, the docs-parity list you gave me. AI is listed for completeness even though it is out of scope by policy.

**Most likely gaps in a docs-parity implementation:** AI (out of scope), Readwise/Kindle import, task actions to external apps, unlimited media, smart search/Related Content/Unlinked Mentions, calendar integrations, audio recording, PDF export styles, formulas in tables, Raycast/Web Highlights, colored app icons, beta/priority support, and the Plus AI budget. The rest are either already in your list (whiteboards, version history, API/MCP, presentation) or interaction details rather than separate features.

- **AI assistant and all AI workflows:** chat, autofill ("Fill object"), AI menu, image generation, media analysis, audio transcription/summary, AI web search, Explore AI, auto-approve, model picker, AI budget. *Out of scope by policy; listed for completeness.*
- **AI Chat Connectors / MCP:** read/write access from ChatGPT, Claude, Cursor, Le Chat, etc.; OAuth-scoped to one space. If MCP is in your list, this is covered; the paid gate is Pro. Bring-your-own-key (BYOK) was discontinued in favor of MCP.
- **Image/PDF/weblink analysis (AI) and reader/annotations (non-AI, Pro):** OCR, summaries, keywords, categories, color palettes; reader view, highlights, notes, snapshots. The reader/highlight parts are durable even without AI; the analysis is AI.
- **Audio recording/transcription:** recording is a non-AI capture feature; transcription/summary is AI. Believer early access, Pro later.
- **Readwise and Kindle import:** selective document import, highlight sync, chapter structure, highlight colors.
- **Task actions:** send tasks to Todoist, Things, TickTick, Apple Reminders, Google Tasks, Microsoft To Do.
- **Unlimited media/storage:** Basic 5 GB total then 100 MB/month; Pro/Believer unlimited fair use; 100 MB per file.
- **Smart search / Related Content / Unlinked Mentions:** semantic/extended search, related notes below each object, unlinked mentions. Pro.
- **Calendar integrations:** Google, Microsoft, Apple Calendar, CalDAV (Nextcloud, Fastmail); event↔note linking. Pro.
- **Whiteboards:** Pro. If whiteboards are in your list, the missed details are embed-in-page, JSON Canvas export, mobile read-only, frames/arrows/text nodes.
- **Version history:** Pro; 15-minute cadence, 50 versions, 30 days. If in your list, the missed details are the cadence/limits and the "Basic can use automated exports" fallback.
- **Presentation watermark removal:** presentation is free with a watermark; Pro/Believer removes it. This gating detail is easy to miss.
- **Focus mode:** Pro; timer island, sounds, completion card. The mission explicitly mentions focus mode behavior, so this is likely a gap.
- **Graph topic search:** graph view is free; topic search is Pro. If graph is in your list, this is a gating detail.
- **Task stats/heatmap/streak:** part of Pro task management; recurring occurrence log, streak stats, heatmap, completion log.
- **PDF export styles:** Notes/Essay/Academic; Pro/Believer.
- **Formulas in tables:** calculate values, show sums/progress bars; Pro. The pricing page does not list it separately, but the Pro page does.
- **Colored app icons:** Believer cosmetic perk.
- **Beta access:** Believer; early access to major features.
- **Priority support:** Believer/Early Believer benefit.
- **Plus AI budget/top-ups:** Pro+/Believer+ add-on; top-ups require Plus.
- **Task reminders:** not shipped; roadmap in progress. Not a paid feature yet, but a likely paid/Pro capability when it ships.

### C.4 Gating lessons

1. **Free is a real product, not a trial.** Basic includes unlimited notes/objects/spaces, daily notes, backlinks, graph, full-text search, sync, offline, import/export, and view-only sharing. That generosity is a trust signal and a growth loop.
2. **Paid is workflow depth, not content hostage.** Pro sells tasks, queries, calendar integrations, whiteboards, version history, media analysis, API/MCP, and unlimited media. The free plan never holds the user's notes hostage.
3. **AI is budget-gated, not feature-gated.** Every AI workflow and model is available on Pro/Believer; Plus only adds budget and top-ups. This is a cleaner model than tiering individual AI features.
4. **Believer is a support/beta tier, not a feature tier.** Same features as Pro; beta access, colored icons, and support are the differentiators. This avoids a three-way feature matrix.
5. **Soft gates work.** Presentation mode is free with a watermark; focus mode is a hard Pro gate. The watermark is a visible, low-friction upsell. Lumen can choose which gates are soft.
6. **Mobile is deliberately a companion.** Advanced search, formula editor, whiteboard editing, gallery/wall/table/embed views, bulk actions, and template editing are missing on phone. This is a scope choice, not an oversight. Lumen should make the same choice explicitly.
7. **The non-goals are explicit.** No team plans, no lifetime subscription, no E2EE, no plugins, no handwriting, no flashcards/spaced repetition, no RTL, no advanced automation/formulas; object inheritance is "up for discussion." These are published on "What's not next." That clarity is a feature.

---

## D. Killer workflows and community mechanics

### D.1 The spine: the daily note is the inbox

This is the workflow Capacities sells hardest. The landing page's three steps are: **start with today**, **turn thoughts into objects**, **watch connections grow**. The "Why people choose Capacities" page calls the daily note "Today is the inbox" and says "Brain dump now. Organize later (or never)." The docs describe the daily note as the place where input integrations (WhatsApp, Telegram, email, Raycast) append content automatically.

Why it is the killer workflow:

- It works before the user has built a system. There is no folder to design, no database to create, no template to choose.
- It gives every capture a default home. The user can link later, when the meaning is clear.
- It creates a natural review unit. The Reviewed checkbox and Unreviewed section turn the daily note into a habit loop.
- It anchors objects in time. Every object created that day can be found in the calendar's "Created today" tab.

Lumen action: make "today" the default landing surface, make capture-to-today the shortest path, and make linking optional at capture time. Do not require a destination before the user has written a word.

### D.2 PARA: structure without folders

Capacities has an official PARA use case. The mapping is opinionated:

- **Projects:** a Project object type with status, time frame, collaborator, and a project hub page. Resources are linked into the project, not moved into a subfolder.
- **Areas:** an Area object type (or tags) linked to projects. An object-select property can formalize the relationship and show up as a backlink.
- **Resources:** the whole space is a resource; if you want an explicit bucket, use a `#resource` tag. The useful question is "where would this resurface?", not "where does this live?"
- **Archive:** a `#archive` tag or a status filter, not a separate place. Archived content stays searchable; a saved query hides it from everyday views.
- **Inbox:** the daily note.

The key sentence in their docs is: "unlike files in folders and subfolders, your content doesn't *live* anywhere specific. Objects aren't moved from A to B; they are just linked to whatever they are useful for." That is the philosophical core of the product.

Why it works: it removes the filing decision, allows one resource to serve multiple projects, and makes archiving a filter instead of a migration. It also gives the user a gradual path from daily notes to a structured system.

Lumen action: ship Project and Area templates with the right properties and queries; support `#archive`/`#resource` conventions; make "archive" a saved query/status filter, not a folder. Do not build a folder tree to imitate PARA.

### D.3 GTD: contextual task management

Capacities has an official GTD use case (Pro). The video chapters are: initial setup, projects and areas, contextual task management, context and tags, people and meetings, weekly review, perspective and planning.

Their task philosophy is explicit: tasks have short lifespans; notes have long lifespans. Tasks should be simple, actionable, and contextual, not multi-level projects. You should not need admin to stay on top of them. The only questions are: where should this live, how important is it, and when will you do it. You can skip all three and process later in the task dashboard.

The GTD mapping:

- **Projects and Areas:** object types, as in PARA.
- **Contexts:** task Context field (an object link) plus tags. Context can be a project, person, meeting, or place.
- **Next actions:** tasks with a status like Next up/In progress, surfaced in Today and the object's Tasks tab.
- **Waiting for:** a custom status moved to the **On hold** category so it is not treated as in progress in the calendar.
- **Weekly review:** daily notes plus task views plus calendar; the review is a cadence, not a separate module.
- **Perspective and planning:** Areas/Projects/Goals as objects, linked to tasks and notes.

Why it works: it keeps the "why" attached to the "what." A task created in a meeting note stays linked to that meeting and its project. The weekly review is a natural loop over daily notes, not a separate app.

Lumen action: auto-context, On hold category, a weekly-review query/dashboard, and a project Tasks tab. If Lumen ships reminders, it can beat Capacities at the one GTD feature users still ask for.

### D.4 Reading workflow: highlights into understanding

Capacities has an official reading workflow. It is a three-step "menu, not a recipe":

1. **Tidy up:** rearrange highlights by theme, delete what no longer matters, add quick notes, merge split highlights.
2. **Link:** tag the source and individual highlights with themes; link highlights to existing concepts with `@`; create new objects for new things; turn standout highlights into their own objects.
3. **Review backlinks:** open a concept and read what all its backlinks collectively teach you; write the synthesis in the note; check Mentions for unlinked connections.

The docs are careful to say this is not a pipeline and not a prescription. The point is intentionality: highlight less, process what you keep, and let backlinks accumulate as a ready-made work environment. The "review backlinks" step is explicitly ad hoc, not a daily obligation.

Why it works: it turns reading from collection into synthesis. The backlinks section becomes the place where multiple sources meet. Turning a highlight into an object gives it a life outside the source. The workflow is sustainable because it does not require processing everything immediately.

Lumen action: reader/highlights, "turn highlight into object," backlinks + mentions, and a reading query (unprocessed highlights, highlights by theme). AI summarization is optional; the durable value is the linking/review loop.

### D.5 Network of people and meeting notes

Capacities has official use cases for both. The pattern:

- A **Person** object with properties (role, organization, projects, tags) and backlinks to every meeting, daily note, and project where they appear.
- A **Meeting** object with date, attendees, project, notes, and tasks. Create it from a calendar event; the note stays linked, and when the event moves, the note follows.
- Tasks created in the meeting inherit the meeting as Context and show up on the project's Tasks tab.
- Unlinked Mentions surface places where the person/place/idea is named but not linked.

Why it works: it is a lightweight relationship CRM and meeting-notes system in one. You open a person and see everything; you open a project and see the meetings and tasks. The context is automatic, not maintained by hand.

Lumen action: Person/Meeting templates, attendee properties, calendar-event linking, backlinks, and unlinked mentions. This is a high-value vertical for consultants, managers, researchers, and writers.

### D.6 Object types over folders: "think in things"

The meta-workflow behind everything else is: **objects have structure, properties, and links; there is no single hierarchy.** A book is a Book, a person is a Person, a meeting is a Meeting. Add a property to a type and every object of that type gets it. A note can belong to a class, a project, and a person at the same time. You never have to pick one folder.

Why it works: it matches how people think about things, not where they file them. It also makes the system maintainable: change the type once, not every note. The landing page calls this "structure built in" and "meets you halfway" between a blank canvas and a rigid template.

Lumen action: ship a small set of opinionated default types (Person, Meeting, Project, Area, Book, Page) with properties and queries, plus an Object Studio-like gallery for adding more. Make it easy to change a type without losing data. Do not force a folder hierarchy.

### D.7 Weekly review, Sunday audit, and the five habits

The "5 Core Habits" video frames the system as **capture, clarify, organize, act, reflect**, and explicitly says "this is not a pipeline." The habits can be used independently; you do not have to do them in order every day.

The weekly review is the maintenance loop:

- Move back through daily notes with the arrows; see tasks completed/due, dates referenced, and objects created.
- Open the calendar and review the week.
- Process the task dashboard: Inbox, Today, Scheduled, Context, Open, Completed.
- Review backlinks/mentions on active concepts.
- Archive what is no longer active (`#archive` or status filter).

The Substack post "My maintenance practice & A Sunday Morning Systems Audit" is part of the same content strategy: they sell maintenance as a habit, not a one-time setup.

Why it works: most PKM systems fail at maintenance, not capture. A weekly review with a visible queue (Unreviewed days, Inbox tasks, orphan notes) makes maintenance concrete.

Lumen action: build a Review dashboard that combines Unreviewed daily notes, Inbox tasks, recently created uncategorized objects, orphan notes, and a "stale projects" query. Make it a weekly ritual, not a hidden feature.

### D.8 What they do NOT push: Zettelkasten

This is a useful negative finding. Capacities' official use cases are meeting notes, daily notes, network of people, reading workflow, GTD, and PARA. There is no official Zettelkasten use case. They use "networked notes," "atomic notes," and "backlinks," but the marketed method is **daily-note-first + objects + PARA/GTD**, not a Zettelkasten workflow.

Community creators do digital gardens, LifeOS setups, and atomic-note workflows (Wesley Anna, Beth McClelland), but those are creator content, not the official docs. The official emphasis is on lowering the barrier, not on a rigorous slip-box method.

Implication for Lumen: do not over-index on Zettelkasten terminology or workflows. The mass-market killer is the daily note + lightweight structure + contextual tasks. Zettelkasten is a power-user niche; support it with backlinks/atomic notes, but do not lead with it.

### D.9 Community mechanics

Capacities runs a full feedback-to-release loop, not just a forum:

- **Feedback board:** powered by Quackback. Boards for feature ideas and bugs; statuses Open, In Progress, Under Review; votes; comments; search/filter; Trending/Top/New. Bug reports auto-attach diagnostics (app version, OS, browser, device, timestamp). Login required.
- **In-app feedback:** release 72 added a Feedback entry in the left sidebar that opens a form, "prompting all the details we need to fix things efficiently."
- **Roadmap:** public page with Planned (Tablet 2.0, simplicity/design consistency), In Progress (Reminders for tasks), Pro (Performance), and Recently launched (Whiteboards, Queries 2.0, CalDAV, Apple Calendar, Weblinks 2.0, PDF 2.0, Audio 2.0, Version History). Vote links go to the feedback board.
- **What's New:** release notes with a video, highlights, and full notes. Release 73 made it remember where you stopped reading on this device; a new version opens at the top. The dialog stays off login screens.
- **What's Next:** published September 8, 2026. Reminders, better AI, performance, tablet/mobile polish; team of 7.
- **What's Not Next:** handwriting, advanced automation/formulas, flashcards/spaced repetition, RTL, collaboration/team plans, E2EE, lifetime subscription, plugins. Object inheritance is listed as "up for discussion" rather than a hard no. This is a trust and expectation-setting page.
- **Beta access:** Believers get early access to major features (audio, PDF annotations, bulk import, image analysis were all Believer betas). This creates advocates and feedback before broad rollout.
- **Integrations and community apps:** Raycast, Web Highlights, MelliLex (Windows Ctrl+right-click dictionary), CapNote (macOS menu-bar daily-note append), Quick Note Pro (Android quick capture). Integrations Hub; OAuth required for new community integrations; contribution guidelines.
- **Channels:** Discord (10k+), Reddit r/capacitiesapp, YouTube (12.4k), Substack, X, Instagram, LinkedIn, Threads, Bluesky, Mastodon.
- **Onboarding content:** six daily emails with "aha moments"; Tip of the Day cards; community spotlight in release notes; user stories on the why-capacities page.

Why it works: users see their feedback move from Open → In Progress → shipped, and the release notes name the feature. The "What's Not Next" page prevents endless re-litigation. The beta program turns Believers into testers and advocates. The in-app form removes the friction that kills most feedback loops.

Lumen action: copy the loop, not the tool. In-app feedback with diagnostics, a public roadmap with statuses, a What's New surface that remembers reading position, a beta channel, and a "what's not next" page. This is cheap relative to the trust it builds.

### D.10 Which workflow is the "killer" one?

Ranked by mass-market reach and fit with Lumen's likely audience:

1. **Daily-note-first inbox.** Universal, lowest friction, works before the system exists. This is the one to lead with.
2. **PARA.** The most popular structure for knowledge workers; maps cleanly to objects/tags/queries.
3. **GTD.** The most popular task methodology; maps to contextual tasks, contexts, On hold, weekly review.
4. **Reading workflow.** High-value for researchers/writers/students; depends on reader/highlights/backlinks.
5. **Network of people / meeting notes.** High-value for consultants/managers; a strong vertical differentiator.
6. **Object types over folders.** The meta-workflow that makes the others possible.
7. **Weekly review / five habits.** The maintenance loop that keeps the system alive.
8. **Zettelkasten / digital garden.** Community niche; support it, don't lead with it.

The real killer is the combination of #1 and #6: a daily note that captures anything, and object types that give it structure when the user is ready. PARA/GTD are the "how to organize" layers on top.

---

## E. Performance and scale signals

### E.1 Public claims

- **50,000+ active knowledge workers**, **10,000+ community members**, Product of the Day on Product Hunt, 4.8 rating, 198 reviews. (Pricing/landing pages.)
- **7-person team**, fully remote across Europe. (What's Next, September 2026.)
- **No storage limits for notes and spaces.** Basic media: 5 GB total, then 100 MB/month; Pro/Believer unlimited fair use; 100 MB per file. (Pricing, docs FAQ.)
- **Offline-first:** after login, Capacities downloads all notes to the device; changes sync when online. Media can be "Everything" or "Required only." On mobile, media downloads in the background on Wi-Fi. (Docs offline support.)
- **Version conflicts:** if local and server versions differ, a dialog asks you to keep local or remote; the other version is deleted and cannot be recovered. The dialog shows word count, a full preview, and a "copy markdown" escape hatch. (Docs offline support.)
- **No E2EE:** data is encrypted in transit and at rest on encrypted disks, but not end-to-end encrypted; notes are stored in the app's database format, not as markdown files. (Docs offline support, What's Not Next.)
- **Qualitative performance claims:** long pages do less work while typing/scrolling; large queries no longer slow sync/search; mobile starts much quicker; phone uses less memory for PDFs/images/long sessions; images load faster; tables/code/PDFs smoother. No public benchmarks or object-count limits that I could find.

### E.2 Architecture signals

- **PostgreSQL migration (January 2026):** Capacities moved from self-hosted Dgraph to PostgreSQL on a managed cloud provider. They report a **70% annual infrastructure cost reduction** and database costs at **1/10th** of the original. The schema separates `objects` and `links` tables; graph traversal uses `WITH RECURSIVE` CTEs and careful indexing; they used Kysely for type-safe SQL; rollout was zero-downtime with double writes, consistency testing, a two-week backfill, and feature flags. They also had to sanitize legacy UTF-8/null bytes during migration.
- **Offline-first local database:** notes are stored in the app/browser storage, not as files; logout deletes local data; there is no "disable sync" option. This is a deliberate trade-off for a consistent object model and cross-device sync.
- **Cross-platform web tech:** desktop (Mac/Windows/Linux), web, iOS, Android. Desktop is the most powerful version; mobile is a companion; tablet sits between. This explains some of the "web app wrapped in a shell" criticism and the mobile feature gaps.
- **Search/indexing:** full-text search works offline; semantic search and related content are AI/Pro; reindexing is available in Settings > Offline & Sync > Recalculate content index.

### E.3 User complaints and weak spots

These are anecdotal, from public threads, but they are consistent enough to matter:

- **Long notes lag / blank screens.** A Reddit thread titled "The app is not designed for large notes?" describes transferring large notes and hitting performance problems. A farewell post says switching screens produced delays and blank white screens, and editing long notes caused serious lag; the author described the app as feeling "like a web app wrapped in a shell." The same post says performance did not noticeably improve after offline mode.
- **Sync issues.** A "Lots of syncing issues?" thread describes lost parts of notes and the last note created, especially when closing the app immediately. Another user switched to the web app to avoid native sync problems.
- **Reminders missing.** Comments on the GTD video: "The only thing missing is for Capacities to have reminders so I can completely switch my task and project management system over to it. Please add REMINDERS!!!" Their roadmap now lists reminders as in progress.
- **Price/value.** The farewell post argues Capacities is the most expensive object-based note app and that some Pro features feel half-finished. This is a perception risk for any premium PKM tool.
- **Mobile gaps.** Docs explicitly list missing mobile features: no bulk actions, no Move To, no template editing, no link view options, no gallery/wall/table/embed views, no advanced search, no formula editor, no whiteboard editing. This is a deliberate companion strategy, but power users notice.

### E.4 Implications for Lumen

1. **Performance is the open flank.** Capacities' own roadmap now prioritizes performance, and public complaints confirm the pain. A native Qt/Compose app that opens large notes instantly, keeps typing latency low, and syncs visibly can win users who outgrow Capacities.
2. **Large notes are the stress test.** Virtualize block rendering, avoid full-document re-layout on every keystroke, incrementally index search, and lazy-load embeds/media. Benchmark a 100k-word note and a 10k-object space.
3. **Sync needs a visible state machine.** Show "offline-ready," "syncing," "last synced," and conflicts. Capacities' conflict dialog is a good model: preview, word count, copy markdown, explicit choice. Do not silently lose edits.
4. **Mobile scope is a product decision, not a bug.** Capacities chose a companion app. Lumen can choose full parity (differentiator) or focused capture/reading (cheaper). Make the choice explicit in the UI: don't show disabled desktop controls on phone without explanation.
5. **Do not copy the AI budget complexity.** Pro/Pro+/Believer/Believer+ plus top-ups is a lot of pricing surface. Lumen's AI policy is out of scope; keep the non-AI plans simple.
6. **Publish performance numbers.** Capacities does not publish benchmarks. A public "performance" page with cold start, note open, search latency, sync delta, and memory would be a credible differentiator.

### E.5 What to measure and publish

- Cold start to interactive.
- Space open (1k / 10k / 50k objects).
- Note open and first keystroke (10k / 50k / 100k words).
- Keystroke-to-paint latency in a long note.
- Search p50/p95 (exact and semantic).
- Sync delta after 1/100/1,000 edits; conflict rate.
- Memory footprint with 1k media objects; battery impact on mobile.
- Offline readiness time for a 10k-object space.
- Crash-free sessions and sync error rate.

These map directly to the complaints above and to Capacities' roadmap priorities.

---

## F. Links

### Changelog and releases

- Changelog JSON: https://capacities.io/api/releases.json
- What's New index: https://capacities.io/whats-new
- Release 60: https://capacities.io/whats-new/release-60
- Release 61: https://capacities.io/whats-new/release-61
- Release 62: https://capacities.io/whats-new/release-62
- Release 63: https://capacities.io/whats-new/release-63
- Release 64: https://capacities.io/whats-new/release-64
- Release 65: https://capacities.io/whats-new/release-65
- Release 66: https://capacities.io/whats-new/release-66
- Release 67: https://capacities.io/whats-new/release-67
- Release 68: https://capacities.io/whats-new/release-68
- Release 69: https://capacities.io/whats-new/release-69
- Release 70: https://capacities.io/whats-new/release-70
- Release 71: https://capacities.io/whats-new/release-71
- Release 72: https://capacities.io/whats-new/release-72
- Release 73: https://capacities.io/whats-new/release-73

### Docs

- Docs home: https://docs.capacities.io/
- Getting started: https://docs.capacities.io/tutorials/getting-started
- User interface: https://docs.capacities.io/reference/user-interface
- Navigation (tabs, side panel, preview, back/forward): https://docs.capacities.io/reference/navigation
- Mobile: https://docs.capacities.io/reference/mobile
- Dates and daily notes: https://docs.capacities.io/reference/dates-and-daily-notes
- Search and command palette: https://docs.capacities.io/reference/search
- Presentation mode: https://docs.capacities.io/reference/presentation-mode
- Use cases index: https://docs.capacities.io/reference/use-cases
- PARA: https://docs.capacities.io/reference/use-cases/para-in-capacities
- GTD: https://docs.capacities.io/reference/use-cases/getting-things-done
- Daily notes use case: https://docs.capacities.io/reference/use-cases/daily-notes
- Reading workflow: https://docs.capacities.io/reference/use-cases/reading-workflow
- Network of people: https://docs.capacities.io/reference/use-cases/network-of-people
- Meeting notes: https://docs.capacities.io/reference/use-cases/meeting-notes
- Organizational structures: https://docs.capacities.io/reference/organizational-structures
- Whiteboards: https://docs.capacities.io/reference/whiteboards
- Graph view: https://docs.capacities.io/reference/graph-view
- Version history: https://docs.capacities.io/reference/version-history
- Calendar integrations: https://docs.capacities.io/reference/calendar-integrations
- Task management: https://docs.capacities.io/reference/task-management
- Task actions: https://docs.capacities.io/reference/task-actions
- Queries: https://docs.capacities.io/reference/queries
- Related content: https://docs.capacities.io/reference/related-content
- Unlinked mentions: https://docs.capacities.io/reference/unlinked-mentions
- Export: https://docs.capacities.io/reference/export
- Bulk import: https://docs.capacities.io/reference/bulk-import
- Subscriptions: https://docs.capacities.io/reference/subscriptions
- AI Chat Connectors: https://docs.capacities.io/reference/ai-chat-connectors
- AI assistant: https://docs.capacities.io/reference/ai-assistant
- Media analysis: https://docs.capacities.io/reference/media-analysis
- PDFs: https://docs.capacities.io/reference/basic-types/pdfs
- Weblinks: https://docs.capacities.io/reference/basic-types/weblinks
- Images: https://docs.capacities.io/reference/basic-types/images
- Audio: https://docs.capacities.io/reference/basic-types/audio
- Tables: https://docs.capacities.io/reference/tables
- Table view customization: https://docs.capacities.io/reference/table-view-customization
- Number formatting: https://docs.capacities.io/reference/number-formatting
- Page layouts: https://docs.capacities.io/reference/page-layouts
- Properties: https://docs.capacities.io/reference/properties
- Block-based linking: https://docs.capacities.io/reference/block-based-linking
- Shortcuts: https://docs.capacities.io/reference/shortcuts
- Sections: https://docs.capacities.io/reference/sections
- Spaces: https://docs.capacities.io/reference/spaces
- Offline support: https://docs.capacities.io/misc/offline-support
- Storage limits FAQ: https://docs.capacities.io/faq/account/storage-limits
- Structure without folders: https://docs.capacities.io/faq/editing/structure-without-folders
- Table of contents: https://docs.capacities.io/faq/editing/table-of-contents
- Text stats: https://docs.capacities.io/faq/editing/text-stats-and-created-at
- Daily note buttons: https://docs.capacities.io/faq/editing/daily-note-buttons
- Customize daily note task sections: https://docs.capacities.io/faq/editing/customize-daily-note-task-sections
- Create object types on mobile: https://docs.capacities.io/faq/general/create-object-types-mobile
- Mind map/canvas/whiteboard: https://docs.capacities.io/faq/general/mind-map-canvas-whiteboard
- Sharing tutorial: https://docs.capacities.io/tutorials/sharing
- Networked note-taking tutorial: https://docs.capacities.io/tutorials/networked-note-taking
- Input integrations: https://docs.capacities.io/tutorials/input-integrations

### Marketing, product, pricing

- Landing: https://capacities.io
- Product/features: https://capacities.io/product
- Pro: https://capacities.io/pro
- Pricing: https://capacities.io/pricing
- Pricing promise: https://capacities.io/pricing/promise
- Pricing rationale: https://capacities.io/pricing/why
- Discount waiting list: https://capacities.io/pricing/discount
- Believer: https://capacities.io/believer
- Task management: https://capacities.io/task-management
- Whiteboards/visual note-taking: https://capacities.io/visual-note-taking
- AI knowledge base: https://capacities.io/ai-knowledge-base
- Developers: https://capacities.io/developers
- Why people choose Capacities: https://capacities.io/why-capacities
- Roadmap: https://capacities.io/roadmap
- What's next: https://capacities.io/whats-next
- What's not next: https://capacities.io/whats-not-next
- Download: https://capacities.io/download-app
- Start: https://app.capacities.io/start
- Team: https://capacities.io/about/team
- Principles: https://capacities.io/about/principles
- FAQ: https://capacities.io/faq
- Help: https://capacities.io/help
- Terms: https://capacities.io/terms-and-conditions
- Back to School promo: https://capacities.io/back-to-school
- Compare Notion: https://capacities.io/compare/notion
- Compare Obsidian: https://capacities.io/compare/obsidian
- Compare Anytype: https://capacities.io/compare/anytype
- Compare Tana: https://capacities.io/compare/tana
- Compare Heptabase: https://capacities.io/compare/heptabase
- Compare Evernote: https://capacities.io/compare/evernote
- Compare OneNote: https://capacities.io/compare/onenote
- Compare Apple Notes: https://capacities.io/compare/apple-notes
- Compare Google Keep: https://capacities.io/compare/google-keep

### Blog

- Blog index: https://capacities.io/blog
- Dev blog: https://capacities.io/blog/dev
- Getting started should be easy (onboarding): https://capacities.io/blog/new-onboarding
- Why we designed task management like this: https://capacities.io/blog/task-management-philosophy
- Why we are introducing Pro+: https://capacities.io/blog/why-we-are-introducing-pro-plus
- Why we limit customization: https://capacities.io/blog/why-limited-customization
- Our first redesign: https://capacities.io/blog/redesign
- Scaling Capacities: Dgraph → PostgreSQL: https://capacities.io/blog/migration-to-postgres
- Our cross-platform approach: https://capacities.io/blog/cross-platform-approach
- How we developed tables: https://capacities.io/blog/how-we-developed-tables
- Why we focus on individuals: https://capacities.io/blog/why-individuals
- Why we are focusing on integrations: https://capacities.io/blog/why-integrations
- Why Capacities will become offline-first: https://capacities.io/blog/why-offline-first
- What offline-first means for you: https://capacities.io/blog/offline-first-for-you
- What if Capacities was gone tomorrow?: https://capacities.io/blog/what-if-capacities-was-gone-tomorrow
- Sustainability: https://capacities.io/blog/sustainability-at-capacities
- Transparency: https://capacities.io/blog/transparency
- How we decide which features to add: https://capacities.io/blog/building-capacities
- New vs. existing features: https://capacities.io/blog/new-vs-existing
- Next-level discovery: https://capacities.io/blog/next-level-discovery
- Not all notes are created equal: https://capacities.io/blog/not-all-notes-are-created-equal
- A second brain: https://capacities.io/blog/a-second-brain
- How to create the knowledge base for your life: https://capacities.io/blog/how-to-create-the-knowledge-base-for-your-life
- A Guide to Personal Knowledge Management: https://capacities.io/blog/guide-to-pkm
- Kindle highlights workflow: https://capacities.io/blog/kindle-highlights
- Better linking with Hookmark: https://capacities.io/blog/better-linking-with-hookmark
- Why we added AI chat: https://capacities.io/blog/why-ai-chat
- How we use AI: https://capacities.io/blog/how-we-use-ai
- AI systems at Capacities: https://capacities.io/blog/ai-systems-at-capacities
- AI for meaningful work: https://capacities.io/blog/ai-for-meaningful-work
- Using collaborative tools alongside Capacities: https://capacities.io/blog/how-we-collab
- What challenges do we face in growing Capacities?: https://capacities.io/blog/challenges
- Moving apps is like moving house: https://capacities.io/blog/moving-apps-is-like-moving-house
- Capacities is a feeling collaborative tools can't give you: https://capacities.io/blog/capacities-is-for-individuals
- Team AMA: https://capacities.io/blog/ama-2
- Why the name Capacities: https://capacities.io/blog/why-the-name-capacities

### YouTube (official and workflow)

- Getting Started With Capacities: https://youtu.be/tWeo9bVAfqM
- Getting Things Done with Capacities: https://youtu.be/bTkcWPiypGg
- PARA in Capacities (2026): https://youtu.be/8mWSYjA9hFg
- The 5 Core Habits for Sustainable Personal Systems: https://youtu.be/Jfi_rZtSlSk
- Tasks, Projects and Progress Tracking Through Your Daily Note: https://youtu.be/cvnA20hVEDg
- How to Capture, Review, and Use Your Notes: https://youtu.be/8ecdjdOqA5w
- What to Do After Reading: A Sustainable Note-Processing Workflow: https://youtu.be/38tK5WFrG-8
- Recreating Steffen's Daily Note Template: https://youtu.be/6wuEXxb0Foo
- Introducing Task Management in Capacities: https://youtu.be/u3za6T6iRt0
- Release 64 (recurring tasks): https://youtu.be/4Tan7PNNZKQ
- Release 70 (PDF annotations, version history): https://youtu.be/xpZBCTflw0k
- Release 71 (audio, calmer desktop, bulk edit): https://youtu.be/74f1Gukq220
- Release 72 (whiteboards, graph, 300+ improvements): https://youtu.be/CBQtlMcXPFI
- Mobile App 2.0: https://youtu.be/9TVL_y1HvbA
- API 2.0 / presentation mode: https://youtu.be/7LbXSY1ocxs
- AI Chat Connectors 2.0: https://youtu.be/cvv5qxdD6FU
- Whiteboards short: https://youtu.be/RP8aq6_uh_U
- Welcome to Capacities: https://youtu.be/wclggL5IyqI
- Every major Capacities update in 2026 so far: https://youtu.be/mw5b4zX3Suw
- Wesley Anna, 2026 Capacities tour: https://youtu.be/xAoYH2bWq6g
- Wesley Anna, digital garden in Capacities: https://youtu.be/TgeqRXoB3c8
- Beth McClelland, LifeOS setup: https://youtu.be/193Ph5OEnoM
- Beth McClelland, work week in Capacities: https://youtu.be/CnNvrhzCNjw
- Beth McClelland, central daily note LifeOS: https://youtu.be/TuFe2zlh9RM

### Community, feedback, developer

- Feedback board: https://feedback.capacities.io
- Reminders ticket: https://feedback.capacities.io/b/1v35e680/feature-ideas/reminders-for-tasks
- Discord: https://discord.gg/3eBP9YxHgQ
- Reddit: https://www.reddit.com/r/capacitiesapp/
- YouTube channel: https://www.youtube.com/@CapacitiesHQ
- Substack: https://substack.com/@capacities
- X: https://x.com/CapacitiesHQ
- Instagram: https://www.instagram.com/capacitieshq/
- LinkedIn: https://www.linkedin.com/company/capacitieshq/
- Threads: https://www.threads.com/@capacitieshq
- Bluesky: https://bsky.app/profile/capacities.bsky.social
- Mastodon: https://pkm.social/@capacities
- Developer portal: https://developers.capacities.io
- API docs: https://docs.capacities.io/developer/api
- MCP docs: https://docs.capacities.io/developer/model-context-protocol
- Community integration contribution guidelines: https://docs.capacities.io/reference/integrations/community/contribute
- Readwise integration docs: https://docs.capacities.io/reference/integrations/readwise
- Kindle integration docs: https://docs.capacities.io/reference/integrations/kindle
- Raycast integration docs: https://docs.capacities.io/reference/integrations/raycast
- Web Highlights docs: https://docs.capacities.io/reference/integrations/community/web-highlights
- MelliLex docs: https://docs.capacities.io/reference/integrations/community/mellilex
- CapNote docs: https://docs.capacities.io/reference/integrations/community/cap-note
- WhatsApp docs: https://docs.capacities.io/reference/integrations/whatsapp
- AI privacy: https://docs.capacities.io/more/ai-privacy
- Data protection: https://docs.capacities.io/more/data-protection
- E2EE rationale: https://docs.capacities.io/more/end-to-end-encryption
- Data protection memo: https://docs.capacities.io/more/data-protection-memo

### Critical user threads (anecdotal)

- "The app is not designed for large notes?": https://www.reddit.com/r/capacitiesapp/comments/1ethvjl/the_app_is_not_designed_for_large_notes
- "Farewell to Capacities" (performance/sync/price): https://www.reddit.com/r/capacitiesapp/comments/1nnr3gt/farewell_to_capacities
- "Lots of syncing issues?": https://www.reddit.com/r/capacitiesapp/comments/1mp6mgy/lots_of_syncing_issues
- "Why did you choose Capacities over Notion or Obsidian?": https://www.reddit.com/r/capacitiesapp/comments/1wqllus/why_did_you_choose_capacities_over_notion_or/
- Saner.AI review (third-party): https://www.saner.ai/blogs/capacities-review

---

## G. Method and caveats

- The public changelog JSON currently exposes releases 64–73. Releases 60–63 were fetched from their individual `whats-new` pages. The JSON `latest` field says `release-72` even though release 73 exists; the feed includes 73 as a continuous-improvement entry.
- Pricing and plan details are as of 2026-10-31. The Back to School 40% student discount is a current promotion and may change. The Pro monthly/yearly prices on the pricing page and the structured data differ slightly ($9.99/mo yearly vs $11.99/mo monthly); I used the displayed yearly price and noted the monthly.
- Some features are labeled Pro/Believer in release notes but not listed separately on the pricing page (merge objects/tags, bulk property edit, reviewed daily notes). I marked those "not labeled Pro" rather than asserting they are free.
- Presentation mode is documented as free with a watermark, which is a softer gate than the pricing page's feature list implies. Focus mode, tasks, queries, whiteboards, version history, calendar integrations, media analysis, API/MCP, and unlimited media are hard Pro gates.
- Reddit and YouTube comments are anecdotal and not representative. They are used only to identify recurring pain points, not to quantify them.
- AI is out of scope by Lumen policy. It is included in the inventory and gating matrix for completeness, not as a recommendation.
- No repository files were modified. This document is the only deliverable written for this research task.
