# PKM UI/UX Pattern Catalog — Research Agent C

**Date:** 2026-10-31 · **Scope:** UI/UX patterns Lumen should adopt next, cataloged across the PKM/productivity landscape.
**Lumen context:** Qt6 Widgets desktop (no Electron/web) + Android Compose app. Capacities' documented feature set is assumed already implemented (object types, daily notes, relations, whiteboards, etc.). This catalog is about *interaction patterns and UX polish*, not data-model parity.
**Method:** websearch + webfetch over official docs, release notes/changelogs, help centers, and verifiable user evidence (reviews, Reddit, HN-adjacent, forum quotes). Sources linked inline.

Ranking inside each area: **value × Qt-cheapness** (best value-per-effort first). Feasibility notes are for Qt Widgets unless marked **Android/Compose**.

---

## 1. Quick capture

### 1.1 Quick Entry panel with modes + destinations (Craft — best in class)
- **What:** A small system-level capture panel with 4 modes (New Document / New Task / Add to Daily Note / Add to Document), a destination dropdown (any folder/doc/**any date** — you can back-date to yesterday or send to tomorrow), Schedule/Deadline buttons so tasks arrive dated, and three send variants: `⌘⏎` send, `⇧⌘⏎` send-and-open, `⌥⌘⏎` send-and-continue. Typed Markdown arrives as real blocks. Closing the panel keeps your text for ~1 minute. Docs: [Craft Quick Entry](https://support.craft.do/en/introduction/quick-entry), [Craft update post](https://www.craft.do/whats-new/b/9C46E805-47A7-43D2-8E4C-BF840FCD16D9/Craft-Update---Quick-Entry-Daily-No).
- **Why users love it:** capture without leaving context, and "mode + destination" kills the where-does-this-go decision that makes capture slow. Reviewer: "While I used Craft I got more stuff done" ([Norbert Hires](https://www.norberthires.blog/craft-docs-review)).
- **Fit for Lumen:** Global hotkey → frameless QDialog with a mode row + destination combo backed by existing object types. **Medium** (global shortcut registration is the only nontrivial part on Linux).

### 1.2 Lock-screen/Control-Center quick capture without loading the app (Obsidian 1.14)
- **What:** iOS Quick Capture creates notes from the Lock Screen, Control Center, Home Screen widgets or Shortcuts **"without waiting for your vault to load"** — the widget writes to a configured "Location" (new note, today's daily note, bookmarked note, any note) with optional templates, then syncs later. Obsidian claims it "launches faster than the iOS animation takes to complete" ([mobile changelog](https://obsidian.md/changelog/2026-10-05-mobile-v1.14.4), [roadmap](https://obsidian.md/roadmap)).
- **Why users love it:** capture latency = OS animation, not app+vault startup. r/ObsidianMD 1.14 threads pulled 640+ upvotes; the top praise was speed.
- **Fit for Lumen (Android/Compose):** home-screen quick-capture widget (Glance) that writes to a local queue/sink first and syncs later + a Quick Settings tile. **Medium** (Android lock-screen actions are limited vs iOS; widget + tile is the right subset).

### 1.3 Zero-organization auto-capture (MyMind)
- **What:** One-click save of articles/images/links/notes via extension; AI auto-detects content type, extracts concepts, and auto-tags. No folders, no filing; retrieval via keyword/visual/associative search and "Smart Spaces" made by dragging cards. Reviews: [Hypertools](https://hypertools.so/tool/mymind), [aigregator](https://aigregator.com/tools/mymind).
- **Why users love it:** "Manual tagging is why bookmark systems fail. Nobody maintains tags consistently. mymind removes that friction entirely." Known limit: "no folders" breaks down at scale when AI misses ([Fabric critique](https://fabric.so/blog/mymind-has-no-folders)) — auto-tagging must stay overridable.
- **Fit for Lumen:** On capture, auto-suggest object type + tags (client-side heuristics first, AI later), always editable. **Medium**; the *UX rule* — never force a destination at capture time — is **cheap** and is the real takeaway.

### 1.4 Highlight ingestion as a first-class capture stream (Readwise Reader)
- **What:** Every source (web, PDF, EPUB, RSS, newsletters, YouTube) funnels into one inbox; highlights are color-coded, tagged, and exported to your note app; a Daily Review resurfaces highlights via spaced repetition. Docs: [readwise.io/read](https://readwise.io/read), [workflow guide](https://blog.readwise.io/reading-workflow-part-2).
- **Why users love it:** "the first read-it-later app that enables a true workflow for serious readers… hard to imagine ever going back" (Pinterest, Superhuman testimonials on the page); users on X: "Compound returns are wild", "I wish I could go back in time and highlight everything."
- **Fit for Lumen:** Treat an "inbox of highlights/clips" as a native object list with a daily resurfacing queue; browser extension already exists in Lumen's stack. **Medium** (extension→server pipeline already exists; the review queue is cheap once ingestion lands).

### 1.5 Global hotkey capture popup (Amplenote / Obsidian ecosystem)
- **What:** A desktop-wide hotkey opens a tiny input from any app; Amplenote's popup supports the full editor (rich footnotes, slash commands, deadlines) and also a "global quick open" hotkey. Docs: [Amplenote help](https://www.amplenote.com/help/global_task_todo_capture_via_hotkey_on_desktop_plus_global_note_lookup_on_desktop); Obsidian users bolt this on via `obsidian://` URIs + Global Hotkeys/Quickly plugins ([Obsidian Rocks](https://obsidian.rocks/obsidian-quick-capture)).
- **Why users love it:** "this plugin changed everything… it adds just enough friction to keep me from doing it" is the *anti*-quote — users celebrate removing that friction. The pattern is consistently among the most-requested in every PKM forum.
- **Fit for Lumen:** QSystemTrayIcon + platform global-shortcut registration (X11 easy, Wayland needs care) → same Quick Entry dialog as 1.1. **Medium**.

### 1.6 OS-native capture surfaces everywhere (Apple Quick Note, Craft widgets, Notion share sheet)
- **What:** Meet users at every OS capture surface: Apple's Quick Note (hot corner / Fn+Q, [MacStories](https://www.macstories.net/stories/fast-capture-with-quick-note-for-ipad-and-mac-the-macstories-overview)), Craft's Lock Screen + Control Center + Shortcuts entries, Notion on Android auto-registering in the browser share sheet ([help](https://www.notion.com/help/web-clipper)).
- **Why users love it:** capture that requires remembering which app you're in is capture that doesn't happen.
- **Fit for Lumen (Android/Compose):** `ACTION_SEND` intent-filter (cheap), home-screen widget (medium), Quick Settings tile (medium). Desktop: tray icon "New capture" (cheap).

---

## 2. Search & command palette

### 2.1 One palette, prefix modes, fuzzy scoring (Obsidian — gold standard)
- **What:** `Ctrl+P` opens the palette; fuzzy matching over all commands makes it a *discovery tool*, not just a launcher. The design rules that make it work: single entry point; mode prefixes (Better Command Palette uses `/` for files, `#` for tags); show hotkeys next to results; recent/frequent items first; plugins can register commands. Docs/evidence: [Obsidian Rocks](https://obsidian.rocks/for-beginners-and-pros-alike-the-command-palette-in-obsidian), [Better Command Palette](https://www.obsidianstats.com/plugins/obsidian-better-command-palette), pattern deep-dive: [Designing a Command Palette](https://destiner.io/blog/post/designing-a-command-palette).
- **Why users love it:** "there is no faster way to run a command" + "a phenomenal command discovery tool" (Obsidian Rocks); "the palette shortens the path and lets users skip the linear information architecture" (UX Patterns).
- **Fit for Lumen:** QDialog + QListView + fuzzy matcher over (commands + objects + recent + "create new X" actions), mode prefixes (`>` commands, `#` tags, `@` objects). **Cheap** — highest value-per-line-of-code in this catalog.

### 2.2 Palette performance as a feature (Obsidian 1.14)
- **What:** In 1.14 the quick switcher got ~5× faster and now supports 10,000+ file vaults ([changelog](https://obsidian.md/changelog), [LinkedIn post](https://www.linkedin.com/posts/obsidianmd_obsidian-114-is-now-available-with-five-activity-7512939636168683520-JGHw)). Obsidian ships palette latency work in release notes like a headline feature.
- **Why users love it:** sub-50ms palettes get used hundreds of times a day; slow ones train users back to the mouse.
- **Fit for Lumen:** Keep the candidate list pre-indexed (sqlite FTS + in-memory fuzzy score), never rebuild the model per keystroke. **Cheap** (perf discipline, not new UI).

### 2.3 Search results with live previews (Notion)
- **What:** The Dec 2023 search overhaul surfaced popular/verified pages and **inline previews** in results ([release notes](https://www.notion.com/releases/2023-12-20)); sidebar search doubles as a jump-to-recent surface (`Cmd+K`).
- **Why users love it:** previews let you confirm the hit without a round-trip; Notion framed it as fixing its most-complained-about surface (Fast Company covered the overhaul as "long-overdue").
- **Fit for Lumen:** Preview pane (or hover popup, see 6.1) beside palette results for objects. **Medium** (needs a read-only object renderer; Lumen's editor blocks can render it).

### 2.4 Saved searches / live "search nodes" (Tana)
- **What:** Any node can embed a live query ("find nodes with tag #book where Status = Reading"), viewable as cards/tables, editable through a visual query builder; saved searches nest *inside* supertag templates so every instance shows contextual results (e.g., each #Author shows their books). Docs: [Tana search nodes](https://outliner.tana.inc/search-nodes), [supertags](https://outliner.tana.inc/learn/features/supertags), [Cortex Futura guide](https://www.cortexfutura.com/tana-supertags-plus-search-tana-fundamentals).
- **Why users love it:** "The feeling of being able to dump a fleeting idea onto a node and letting it categorize itself has been amazing" (PM quoted in Tana docs).
- **Fit for Lumen:** Saved queries are the natural evolution of Capacities-style object types: a "saved search" object type whose instances render as views. **Medium** (query builder UI is the cost).

### 2.5 Slash-menu parallel to the palette (Obsidian/Notion/Reflect)
- **What:** The same fuzzy-matched action list available in-editor via `/`, so insertion commands and workspace commands share one vocabulary ([Obsidian video evidence](https://www.youtube.com/watch?v=4iHsuAW_Pw4)).
- **Why users love it:** one mental model, two entry points; also the #1 editor-discovery surface in Reflect's reviews ([XDA review](https://www.xda-developers.com/started-using-ai-powered-tool-for-all-my-notes)).
- **Fit for Lumen:** Reuse the palette's action registry behind a `/` trigger in the editor. **Cheap** if 2.1 is built as a shared registry.

---

## 3. Empty states & onboarding

### 3.1 Learn-by-doing "Getting Started" object (Notion — best in class)
- **What:** New workspaces get a fully functional checklist page that teaches by using: "Type `/` for slash commands", hover tooltips on first-use elements, and tasks you actually complete as the tutorial. Teardown: [Appcues UX analysis](https://goodux.appcues.com/blog/notions-lightweight-onboarding).
- **Why users love it:** learn-by-doing beats tours; the checklist *is* the product, so the object model gets taught in real objects.
- **Fit for Lumen:** Seed a "Getting started" daily-note/page object on first run with interactive checklist blocks. **Cheap** — content work, not code.

### 3.2 Template gallery reachable from the first empty state (Notion Marketplace)
- **What:** 250+ categorized templates, creators, and one-click "duplicate into workspace" — surfaced from the sidebar *inside* the app, and used as the personalization step of onboarding ("What do you need? How do you work?"). Docs: [ultimate guide to templates](https://www.notion.com/help/guides/the-ultimate-guide-to-notion-templates), [Candu teardown](https://www.candu.ai/blog/how-notion-crafts-a-personalized-onboarding-experience-6-lessons-to-guide-new-users).
- **Why users love it:** an empty workspace is the #1 churn moment; a gallery converts it into a starting point and teaches what the tool is *for*.
- **Fit for Lumen:** In-app template browser (QListWidget + preview) with "use template" creating real objects from bundles shipped as fixtures; couple it to object types Lumen already has. **Cheap–Medium**.

### 3.3 Opinionated starter structure (Heptabase)
- **What:** The official wiki prescribes a concrete first structure — three parent whiteboards (Projects / Topics / Life) with child boards — instead of "here's a blank canvas". Doc: [Organize knowledge & projects](https://wiki.heptabase.com/organize-knowledge-and-projects).
- **Why users love it:** "Heptabase just clicked for me in a way that addressed about 95% of my CRIMP symptoms" (long-time outliner user, [OutlinerSoftware](https://www.outlinersoftware.com/topics/viewt/10244)).
- **Fit for Lumen:** On first run, offer "start with the recommended setup" — a small set of pre-linked object types + spaces. **Cheap** (fixtures + a dialog).

### 3.4 Progressive-disclosure tips in-context (Anytype)
- **What:** Docs bake product philosophy into UI guidance: "Start simple with just one widget… Build a daily dashboard" — the UI ships with exactly one obvious next action and expands later ([Anytype widgets doc](https://doc.anytype.io/anytype/basics/sidebar/widgets)).
- **Why users love it:** reduces the "1,000 features, which one first?" paralysis that kills local-first tools with steep learning curves (a common Anytype community theme).
- **Fit for Lumen:** First-run sidebar shows 2–3 suggested actions, not the full feature tree. **Cheap**.

### 3.5 Sample content you can safely delete (Logseq/Notion)
- **What:** Both ship sample content (welcome journal entries, demo databases) so search, backlinks, and queries have something to show on minute one. Evidence of the effect in Logseq's design: the journal-first flow "aims to make the practice of note-taking as friction-less as possible" ([1-year user review](https://www.reddit.com/r/logseq/comments/1gxc7ra/a_review_of_logseq_after_using_it_for_more_than/)).
- **Why users love it:** every surface (search, graph, daily note) demonstrates itself immediately.
- **Fit for Lumen:** Seed a handful of sample objects + a daily note entry marked "safe to delete". **Cheap**.

---

## 4. Daily note / journal UX

### 4.1 Journal-as-default-destination (Logseq — outliner journals)
- **What:** The app opens directly into today's journal; everything captured lands in the timeline; pages emerge by linking. `#card` turns any block into a flashcard with built-in spaced repetition; `/cloze` for cloze deletions. Docs: [nesslabs Roam→Logseq guide](https://nesslabs.com/roam-to-logseq), [Logseq DB update notes](https://discuss.logseq.com/t/whats-new-with-logseq-db-may-16th-2026/35020).
- **Why users love it:** removes the biggest daily-note friction: *where do I write?* Users: journal style "aims to make the practice of note taking as friction-less as possible".
- **Fit for Lumen:** Since Capacities-style daily notes exist, make the default "capture → today's daily note, one keystroke" universal across desktop/mobile/palette. **Cheap** (policy + shortcut, not UI).

### 4.2 Vertical time-scroll: one continuous timeline across days (Reflect — best in class)
- **What:** Opens on today; scroll up for yesterday and beyond — the notebook metaphor with infinite vertical history; a small calendar side panel navigates; pin + full per-note history live in the same panel. Reviews: [Stephen Zeoli](https://stephenjzeoli.medium.com/reflect-almost-perfect-daily-notes-af682e2ca753), [Antoine Bischinger](https://antoine.fi/reflect-notes-review).
- **Why users love it:** "It is a great way to improve your working memory by allowing you to quickly go back to details of previous days"; testimonials: "my writing and introspective ability went up 10x" ([reflect.app](https://reflect.app)).
- **Fit for Lumen:** A "timeline" mode for daily notes (virtualized concatenation of day objects) is **Medium** in Qt Widgets; the cheap 80% is a date-jump bar + "yesterday/continue" affordances.

### 4.3 Auto-magic entry metadata (Day One — best in class)
- **What:** Every entry auto-logs location, weather, altitude, moon phase, music playing, time and step count: "All you have to add is what you'd like to remember about the day" ([Wirecutter](https://www.nytimes.com/wirecutter/reviews/day-one-journaling-app-review), [user review](https://kendranicole.net/day-one-iphone-app-review)).
- **Why users love it:** rich re-reading surfaces ("On This Day") with zero user effort; Wirecutter: "logs more detail than I've seen in most other journaling apps."
- **Fit for Lumen:** Auto-append weather/location line into the daily note front-matter; **Medium** (needs a light geo/weather service; make it opt-in, offline-safe).

### 4.4 Back-dateable and template-driven daily notes (Craft)
- **What:** Daily notes live in a calendar tab (aggregating Tasks + calendar Events + the note); Quick Entry can target *any* date, creating the note if missing; daily notes support default templates. Docs: [Craft daily notes](https://support.craft.do/en/plan-and-do/daily-notes), [Quick Entry](https://support.craft.do/en/introduction/quick-entry).
- **Why users love it:** "unfinished task → move to next day" becomes a planning funnel users describe as "more effective… than my long-term Notion setups" ([review](https://www.norberthires.blog/craft-docs-review)).
- **Fit for Lumen:** Date picker on capture (any date), template hook per daily note, per-day task rollup. **Cheap–Medium**.

### 4.5 Streaks with forgiveness + one default prompt (Day One / Apple Journal)
- **What:** Apple Journal "shows your current streak at a glance or presents writing prompts that change throughout the day" ([App Store](https://apps.apple.com/pt/app/journal/id6447391597)); Day One pairs streaks with prompts/templates for the blank-page problem. Counter-evidence worth heeding: streak pressure can *reduce* habit formation — "replace 'every day forever' with a flexible weekly target" and "use one default prompt" ([Glimmo's journal-friction analysis](https://www.glimmo.io/blog/digital-journal-app-feels-like-a-chore)).
- **Why users love it:** visible momentum + prompts remove the two biggest blank-page anxieties (starting, and mattering).
- **Fit for Lumen:** Streak counter + single configurable prompt injected into the daily note template. **Cheap**; design it forgiving from day one.

### 4.6 Review flows: resurfacing old material on a schedule (Readwise)
- **What:** The Daily Review resurfaces highlights/notes via spaced repetition, by email or app — "a simple habit leading to lasting insight" ([readwise.io](https://readwise.io)). Logseq binds this to flashcards; NeuraCache does it for Kindle/Obsidian highlights.
- **Why users love it:** "When I want to scroll social media, I read those instead" (user on X) — the review slot replaces doomscrolling.
- **Fit for Lumen:** A "Review" queue mixing due flashcards (Logseq-style `#card` blocks) + resurfaced old daily notes. **Medium** (SRS scheduler is the work; UI is one list).

---

## 5. Mobile-first interactions

### 5.1 Settings and detail views as bottom sheets — "swipe anywhere to dismiss" (Obsidian 1.14)
- **What:** Mobile settings now "open in a sheet over the app. Swipe anywhere to dismiss" — the whole screen is a dismissal target, not just the handle ([mobile changelog](https://obsidian.md/changelog/2026-10-05-mobile-v1.14.4)). Material 3 formalizes this: drag handle with 48dp hit target, 28dp top radius, optional snap points ([M3 specs](https://m3.material.io/components/bottom-sheets/overview)).
- **Why users love it:** NN/g's guidance is blunt: handles alone are missed by users — visible close buttons + full-screen swipe + Back-key dismissal make sheets reliable ([NN/g](https://www.nngroup.com/articles/bottom-sheet)).
- **Fit for Lumen (Android/Compose):** Use `ModalBottomSheet` for object details, filters, and settings; adopt the swipe-anywhere + close-button + Back-handling trio. **Cheap** (M3 gives it for free; the trio is discipline).

### 5.2 Home-screen widgets: pinned note, collection feed, quick-capture pill (Google Keep — best in class)
- **What:** Three widget archetypes: *Single note* (one live note pinned), *Note collection* (filtered feed), and the pill-shaped *Quick capture* widget that opens a popup ([Google Keep help](https://support.google.com/keep/answer/13302793)). Keep keeps refining this a decade later ([9to5Google, 2026 widget tweak](https://9to5google.com/2026/09/17/google-keep-widget-tweak)).
- **Why users love it:** the widget *is* the app for the top-1 use case; widget quality shows up in every Keep-vs-competitor review.
- **Fit for Lumen (Android/Compose):** Ship all three archetypes: "today's daily note" widget (view/edit), "recent objects" feed, and quick-capture. **Medium** (Glance).

### 5.3 Context-aware default capture mode (Craft iPhone)
- **What:** Quick Entry opens in the mode matching where you are: in a folder → new document there; in Tasks → new task; in Calendar → add to *that day's* daily note ([Craft docs](https://support.craft.do/en/introduction/quick-entry)).
- **Why users love it:** the sheet pre-computes the only decision users must make; zero extra taps.
- **Fit for Lumen (Android/Compose):** FAB behavior derives default type/space from current screen; long-press FAB = capture palette. **Cheap**.

### 5.4 One-hand reachability: bottom nav + thumb-zone actions (Notion mobile)
- **What:** Notion's mobile redesign put core navigation in a bottom tab bar with a create action in reach; later updates made database interactions thumb-driven: "Databases are way easier to use on mobile. Tap, view, edit, or create filters with a couple flicks of your thumb" ([Notion 2.52 release](https://www.notion.com/releases/2025-07-10), [matthiasfrank.de updates](https://matthiasfrank.de/en/notion-updates/)).
- **Why users love it:** desktop-ported sidebars are the #1 complaint against PKM apps on mobile; thumb-first filters are praised specifically in release-note commentary.
- **Fit for Lumen (Android/Compose):** Bottom navigation + filters as bottom-sheet chips; ensure every destructive action sits below the fold. **Cheap–Medium**.

### 5.5 Share-sheet as a first-class citizen (Notion / Logseq)
- **What:** Notion on Android registers itself in the browser share menu with no extra steps; Logseq users built an entire Siri Shortcut ecosystem ("Save to Logseq") because the built-in path was weaker ([Notion help](https://www.notion.com/help/web-clipper), [RoutineHub Save to Logseq](https://routinehub.co/shortcut/12917)).
- **Why users love it:** share-target is mobile's truest quick-capture; community shortcut downloads are the demand signal for Logseq.
- **Fit for Lumen (Android/Compose):** `ACTION_SEND` + `ACTION_PROCESS_TEXT` intent filters mapping shared text/URLs to typed objects. **Cheap**.

---

## 6. Editor niceties

### 6.1 Hover previews (with on-demand modifier) across every surface (Obsidian — best in class)
- **What:** The Page Preview core plugin pops a rendered preview when hovering internal links — and critically it works over **search results, backlinks, and unlinked mentions** too; users can require `Ctrl+hover` so browsing lists isn't spammy ([forum thread](https://forum.obsidian.md/t/standardize-mouse-hover-ctrl-for-hover-preview/12994), [Hover Editor plugin](https://github.com/nothingislost/obsidian-hover-editor) upgrades the popup to a *resizable, editable* mini-window).
- **Why users love it:** peeking without navigation preserves the reading flow; the demand is evidenced by whole plugins built around the popover.
- **Fit for Lumen:** A QWidget frameless popup rendering the target object read-only (Lumen's editor blocks can paint it); apply in palette results, backlinks, and links. **Medium**.

### 6.2 Side peek / center peek / full-page — per-view open behavior (Notion)
- **What:** Database rows open in a **side peek** (right panel) by default; each database view chooses how pages open: Side Peek / Center Peek / Full Page, and `⤡` expands inline databases to full pages. Docs: [intro to databases](https://www.notion.com/help/intro-to-databases), original [side peek release](https://www.notion.com/releases/2022-07-20), [how-to](https://bullet.so/blog/how-to-open-center-peek-notion).
- **Why users love it:** "so I was so happy to discover this" — creators immediately rebuilt their dashboards around it; it's the default in Notion now.
- **Fit for Lumen:** Editor-open policy per view: side peek = QSplitter pane beside the list, center peek = QDialog, full page = normal navigation. **Cheap–Medium** (Lumen already has an embeddable editor).

### 6.3 Backlinks pane with unlinked mentions (Obsidian/Reflect)
- **What:** A persistent pane lists backlinks *with context snippets*, plus "unlinked mentions" you can convert to links in one click; Reflect attaches incoming meeting/calendar backlinks to daily notes automatically ([Reflect review](https://antoine.fi/reflect-notes-review)).
- **Why users love it:** backlinks are the #1 "aha" of networked notes; unlinked-mentions is the feature that converts passive notes into links without re-typing.
- **Fit for Lumen:** Dock pane fed by the existing relations index; add "linkify" action on unlinked mentions. **Cheap** given Capacities-style relations exist.

### 6.4 Tabs, tab stacks, linked views, saved layouts (Obsidian 1.0)
- **What:** Browser-style tabs (pin, reorder, `Ctrl+1..9`, recently-closed), split tab groups any way you like, **stacked tabs** (Andy Matuschak's sliding panes), linked views (two panes showing the same note in different modes), and the Workspaces plugin to save/restore whole layouts. Doc: [Obsidian tabs](https://obsidian.md/help/tabs).
- **Why users love it:** "Pane Relief delivers everything I wanted… per-tab navigation history" — users install plugins to compensate when tab management lags ([MakeUseOf](https://www.makeuseof.com/obsidian-plugin-finally-gave-me-dual-pane-layout-i-wanted)).
- **Fit for Lumen:** QTabWidget + QSplitter first (cheap); tab stacks and linked views later (**Medium/Hard**). A "writing workspace" preset (source left / draft right) is the 80% value at **cheap**.

### 6.5 Focus/zen mode: sentence dimming + typewriter scroll (iA Writer — best in class)
- **What:** Focus Mode grays out everything except the current sentence/paragraph; Typewriter mode keeps the cursor vertically centered; stats (word count, reading time) live in a quiet bottom bar. Doc: [Write with Focus](https://ia.net/writer/how-to/write-with-focus).
- **Why users love it:** decades of loyalty from long-form writers; Bear users literally beg for it: "If you have tested this feature, you will never write without it" ([Bear community](https://community.bear.app/t/basically-a-perfect-app-but-typewriter-scrolling-focus-mode/17397), [Obsidian forum request](https://forum.obsidian.md/t/zen-mode-and-typewriter-mode-for-focused-writing/2788)).
- **Fit for Lumen:** Hide chrome (fullscreen), dim non-current block via QTextCharFormat, center-cursor scroll mode. **Cheap–Medium** in a Widgets editor.

### 6.6 Open-anything-beside-me side panel (Heptabase)
- **What:** Clicking a card on a whiteboard opens it in a right-hand panel so the spatial context stays visible while you read/write ("you can open cards on this right-hand panel; it makes life a lot easier for those who are researching" — [review](https://www.youtube.com/watch?v=HAFGsad4MYE)).
- **Why users love it:** whiteboards stay as the *map* while detail work happens beside them; users cite it as Heptabase's core research loop.
- **Fit for Lumen:** Same mechanism as 6.2 applied to the whiteboard/canvas: object detail in side panel over canvas. **Medium**.

### 6.7 Breadcrumbs you can click (Notion/Craft/Heptabase nested spaces)
- **What:** Nested content always exposes its full path as clickable crumbs (Notion's breadcrumb trail atop pages; Heptabase's nested whiteboards behave as folders with visible hierarchy).
- **Why users love it:** the escape hatch that makes deep nesting safe; absence is a recurring complaint in tools with deep objects.
- **Fit for Lumen:** Clickable breadcrumb QLabel row above the editor (space > object > sub-object). **Cheap**.

---

## 7. Visual organization

### 7.1 Live sidebar widgets = dashboard without building one (Anytype — best in class)
- **What:** Any pinned object/query becomes a **live, interactive sidebar widget** — an upcoming calendar, a task list you check off in place, a gallery — with per-widget layouts (Same as Object / Compact / Detailed / Link / Hierarchical), object-count limits, drag reorder, and a dense "Links" mode for archives. Docs: [Anytype widgets](https://doc.anytype.io/anytype/basics/sidebar/widgets), [sidebar](https://doc.anytype.io/anytype/basics/sidebar).
- **Why users love it:** docs' own tip says it best: "Build a daily dashboard. Pair a Calendar widget alongside a Tasks query filtered for 'Due Today'" — navigation becomes an at-a-glance home page.
- **Fit for Lumen:** Sidebar section rendering embedded views of saved queries/objects, with check-off and peek interactions. **Medium** (reuses view rendering; the sidebar host is the work).

### 7.2 Checkbox-driven progress bars (Tana)
- **What:** Any node shows a progress bar computed from the checkboxes beneath it; **supertags can map "done" to a field value** (e.g., Status = Done), so progress is schema-aware, not just cosmetic. Doc: [checkboxes and progress bars](https://outliner.tana.inc/learn/features/checkboxes-and-progress-bars).
- **Why users love it:** projects feel alive at a glance without rollup plumbing.
- **Fit for Lumen:** Progress bar in object header/table views computed from child blocks with a done-state; delegate painting in views. **Cheap**.

### 7.3 Kanban: drag cards + a real Group menu (Obsidian Bases 1.14)
- **What:** Bases gained a kanban view ("organize notes into columns and drag cards between groups") plus a **Group menu to choose, reorder, hide, or add groups** in any view, and collapsible groups in table/cards/list layouts ([changelog](https://obsidian.md/changelog), [1.9.10 Bases release](https://obsidian.md/changelog/2025-08-18-desktop-v1.9.10), [help](https://obsidian.md/help/bases)).
- **Why users love it:** the Group menu details (reorder/hide/add without editing the query) are exactly the polish that made Bases feel finished; heavy upvote reception on r/ObsidianMD for 1.14.
- **Fit for Lumen:** Board view over any object set with group-by property, drag between groups, per-view group ordering/hiding. **Medium–Hard** in Qt Widgets (custom drag/drop list); consider QGraphicsView if the whiteboard canvas already exists.

### 7.4 Databases-as-containers with view tabs (Notion 2.52, July 2025)
- **What:** Any data source can be added **as a tab within one container** — "your dashboards visually cleaner and far easier to navigate… a more modular, app-like experience" — plus a gentler database visual refresh ([Notion 2.52](https://www.notion.com/releases/2025-07-10), [analysis](https://www.notionapps.com/blog/notion-data-sources-update-2025)).
- **Why users love it:** one hub, many views — users rebuilt entire workspaces into single "Marketing Hub"-style containers.
- **Fit for Lumen:** Allow an object/page to embed several saved views as tabs (QTabWidget of view widgets). **Medium**.

### 7.5 Home: today's tasks, upcoming, recently visited (Notion Home)
- **What:** A default Home surface aggregating Today's tasks, upcoming calendar, recently visited pages, and pinned databases — an automatic dashboard users don't have to build ([Thomas Frank guide](https://thomasjfrank.com/notion-home-everything-you-need-to-know/), [SweetSetup update](https://thesweetsetup.com/the-notion-home-page-gets-another-round-of-updates/)).
- **Why users love it:** answers "what now?" on launch; Reddit threads show users setting Home as their landing page by default.
- **Fit for Lumen:** A start page assembling existing primitives (today's daily note + due tasks + recents + pinned widgets from 7.1). **Medium**; nearly free once 7.1 exists.

### 7.6 Color as a first-class citizen (Obsidian highlight colors / AppFlowy palettes)
- **What:** Obsidian 1.14 added six highlight colors to the editor ([changelog](https://obsidian.md/changelog)); AppFlowy shipped expanded cover/select/block colors with a custom picker ([update post](https://appflowy.com/blog/appflowy-updates-web-real-time-editing-mentions-gpt-5-and-more)).
- **Why users love it:** small, cheap, shipped-as-headline features — both got outsized reception because visual scanning is a core PKM job.
- **Fit for Lumen:** Text background color palette + object accent colors/covers. **Cheap**.

### 7.7 Gentle visual refresh of dense surfaces (Notion database makeover)
- **What:** Notion's 2.52 database restyle: "clearer names and cleaner tables… gentler visuals, and fewer distractions", a smoother "+ property" flow, group/insert/rename properties from the property menu itself ([release](https://www.notion.com/releases/2025-07-10)).
- **Why users love it:** density without visual punishment is why people describe Notion databases as "pleasant" despite their power.
- **Fit for Lumen:** Table/board polish pass — row hover affordances, property menu consolidation, calmer headers. **Cheap** (QSS/styling work).

### 7.8 Doc mode / Edgeless mode on the same document (AFFiNE)
- **What:** Every doc has Page Mode and Edgeless Mode over the same blocks; reading order is preserved and visualized across modes; properties like "Doc mode" and "Edgeless theme" make the mode part of the document state, not the viewer ([AFFiNE docs](https://docs.affine.pro/core-concepts/elements-of-affine/page-mode), [Nov 2024 update](https://affine.pro/blog/whats-new-affine-nov-update)).
- **Why users love it:** thinking flips between linear and spatial without export/import — the exact Notion-canvas pain point AFFiNE markets against.
- **Fit for Lumen:** If Capacities-parity includes whiteboards, add "open this object's board view" as a mode, preserving order. **Hard** (two renderers over one model) — long-term item.

---

## Cross-cutting: Top 10 for Lumen (value × Qt-cheapness across all areas)

| # | Pattern | Area | Source app | Qt feasibility |
|---|---------|------|------------|----------------|
| 1 | Fuzzy command palette with modes (`>`, `#`, `@`), recents, hotkey hints | Search | Obsidian | Cheap |
| 2 | Side peek / per-view open policy for objects | Editor | Notion | Cheap–Medium |
| 3 | Quick Entry panel: mode + destination + send/send-and-open/send-and-continue | Capture | Craft | Medium |
| 4 | Backlinks pane + unlinked-mentions linkify | Editor | Obsidian/Reflect | Cheap |
| 5 | Learn-by-doing Getting Started object + template gallery in first-run | Onboarding | Notion | Cheap |
| 6 | Bottom sheets with swipe-anywhere dismiss (Android) | Mobile | Obsidian 1.14 / M3 | Cheap (Compose) |
| 7 | Android home-screen quick-capture + "today" note widgets | Mobile/Capture | Obsidian / Google Keep | Medium |
| 8 | Sidebar live widgets (calendar/task/query feeds) → Home start page | Visual org | Anytype/Notion | Medium |
| 9 | Hover previews across palette results, backlinks, links | Editor | Obsidian | Medium |
| 10 | Checkbox-computed progress bars + highlight colors | Visual org | Tana/Obsidian | Cheap |

Adjacent best-of notes for the roadmap: Logseq's `#card` flashcards (SRS) as the journal retention layer (Medium); Craft-style back-datable daily notes (Cheap); Notion synced blocks (Hard — avoid until the object model demands it).

---

*End of catalog. Compiled from official docs/changelogs and cited user evidence; all links fetched/verified 2026-10-31 via websearch/webfetch.*
