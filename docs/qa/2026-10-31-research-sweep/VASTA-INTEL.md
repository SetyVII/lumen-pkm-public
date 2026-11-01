# Vasta — competitor intel for Lumen

**Date:** 2026-10-31
**Analyst:** competitor-intel subagent
**Target:** Vasta (`ariedotme/vasta`), self-hosted Capacities/Notion alternative
**Comparison baseline:** Lumen 0.6.1 (Go server + encrypted SQLite, Qt 6 desktop, Android, extension, MCP/API v2)
**Confidence:** high on repository facts and issue text; medium on launch sentiment (one Reddit thread, no HN/PH coverage found); low on private adoption metrics (stars/forks only).

---

## TL;DR

1. **Vasta is real and directly aimed at Lumen's niche.** Repo: <https://github.com/ariedotme/vasta>. AGPL-3.0-only, TypeScript/Vue 3/Tauri 2/Node 24 + Hono/SQLite/Yjs. Alpha `0.4.1`, created 2026-10-22, 41 stars, 4 forks, 1 contributor, 0 merged PRs, 7 releases in 6 days.
2. **It is not feature-superior to Lumen; it is distribution-superior.** Its launch pitch is: download the app and start writing with no server, or run one container with four commands; open it in a browser or on your phone; import your Capacities export. Lumen has no public image, no local mode, no importer, no web/PWA, and Spanish-first internal docs.
3. **Its gaps are Lumen's openings:** no data export (#1), no live collaboration/presence (#22), no native mobile app, web/PWA bugs (#43, #32), no app-level at-rest encryption (source grep; plaintext calendar passwords #16, media authz bug #2), single maintainer, untested Windows/Linux desktop builds (#29, #30), no user guide (#36).
4. **The launch was one r/NoteTaking post, not an HN moment.** 2026-10-21, 35 comments, 16 external commenters, mostly "open source it / I'd love to try it / installed the Windows build, felt at home". No HN, Product Hunt, Lobsters or r/selfhosted launch found. The main skepticism was **AI-generated code** ("I used AI a lot to build it" — one hostile reply).
5. **Lumen's top levers are not more features.** They are: one-command public install, desktop "start on this computer" local mode, a Capacities full-export importer, English quickstart + demo, and a visible export/encryption trust story. A full web frontend is **not** the top blocker for Lumen's single-user native niche; it is a blocker only if Lumen chases Vasta's multi-user/team segment, which Lumen has deliberately excluded.

---

## (a) Product found

### Identity

| Field | Value |
|---|---|
| Name | Vasta |
| Repo | <https://github.com/ariedotme/vasta> |
| Description | "Open-source, self-hosted alternative to Capacities and Notion: notes, tasks and daily notes as typed objects. Local-first, works offline, MCP for your agents." |
| License | AGPL-3.0-only (`package.json`; GitHub license AGPL-3.0) |
| Created | 2026-10-22T18:44Z |
| First commit | 2026-10-22T19:21Z, "Vasta: first public version" |
| Latest release | `0.4.1`, 2026-10-27T20:11Z |
| Status | Alpha (README: "Vasta is in alpha... Make backups") |
| Stars / forks / watchers | 41 / 4 / 2 (2026-10-31) |
| Open / closed issues | 39 / 4 |
| Contributors / PRs / commits | 1 / 0 / 35 |
| Homepage / demo | none |
| Docs site | none; Markdown in `docs/` |
| In awesome-selfhosted? | no (checked the list on 2026-10-31) |

### What it is

Vasta is a Capacities-shaped PKM: every note is a typed object (Page, Task, Book, Person, or a user-made type) with properties, relations, tags and rich text; each day has a daily note; queries and views sit on top. It adds a personal layer Capacities does not have: habits, a food diary, reminders, shared spaces, comments/mentions, sprints and flow metrics. It has no AI inside; agents use an MCP endpoint and a CLI.

The author built it for himself and his girlfriend, then open-sourced it after a Reddit post. The README is candid: one person, alpha, some parts new, make backups.

### Stack

| Layer | Technology |
|---|---|
| Server | Node.js 24, Hono, TypeScript |
| Database | SQLite (server), sqlite-wasm + OPFS (browser replica) |
| Sync | Yjs CRDT for rich text; HLC + last-writer-wins per field for everything else; tombstones; SSE `rev` events |
| Web | Vue 3, Tailwind, TipTap, Vite; installable PWA |
| Desktop | Tauri 2 (Rust), loads the server's web app in a webview; optional bundled local server |
| Mobile | PWA only; no native app |
| Media | Local disk or any S3-compatible bucket (Garage, MinIO, R2, B2, AWS) |
| Agents | MCP Streamable HTTP endpoint + `vasta` CLI |
| Packaging | Docker image `ghcr.io/ariedotme/vasta`, `linux/amd64` and `linux/arm64` |

GitHub language bytes: TypeScript 1.82 MB, Vue 723 KB, JavaScript 235 KB, Rust 66 KB, CSS 22 KB, HTML 7.8 KB, Shell 4.4 KB, Dockerfile 2.6 KB. It is a real codebase, not a wrapper.

### Deploy in practice

The README's self-host path is four commands:

```sh
mkdir vasta && cd vasta
curl -o compose.yml https://raw.githubusercontent.com/ariedotme/vasta/main/compose.example.yml
curl -o .env https://raw.githubusercontent.com/ariedotme/vasta/main/.env.example
docker compose up -d
docker compose logs vasta      # setup code for the first account
```

Then open `http://127.0.0.1:5174`, create the first account with the setup code, and invite others with one-time links. The compose file publishes only on `127.0.0.1`; HTTPS is a Caddy reverse proxy in `docs/deploy.md`. Backups are `docker exec vasta vasta backup /data/backups/...` plus the media directory or bucket. Media can move to S3 with `vasta media push`.

The desktop path is even shorter: download the `.dmg`/`.exe`/`.AppImage`, choose **Start on this computer**, and the app runs a bundled Node server locally. Later, **Settings › Server › Move to a server** copies notes, history and files into a server account. This is the single best onboarding idea in the product.

### Release cadence

| Version | Date (UTC) | Notable |
|---|---|---|
| 0.2.0 | 2026-10-22 19:51 | first public release |
| 0.3.0 | 2026-10-23 02:09 | — |
| 0.3.1 | 2026-10-23 11:59 | "Start on this computer" local mode; move to server |
| 0.3.2 | 2026-10-23 14:01 | quick-capture fixes |
| 0.3.3 | 2026-10-25 18:10 | macOS local-mode crash fixed |
| 0.4.0 | 2026-10-26 18:05 | in-app Capacities importer; Mermaid |
| 0.4.1 | 2026-10-27 20:11 | template questions in quick capture; toggle nesting |

Seven releases in six days. The changelog is user-facing and specific. This is a pace Lumen should not try to match blindly (see "what not to copy"), but it is visible momentum.

### Community and launch

| Signal | Value |
|---|---|
| Reddit launch | r/NoteTaking, 2026-10-21T21:46Z, "Made my own self-hosted notes app because Capacities was almost what I wanted" |
| Thread engagement | 36 RSS entries: 1 post + 35 comments; 16 external commenters; 18 author replies |
| HN | no story or comment found (Algolia search for "vasta" returns only unrelated hits: Vastaamo and ordinary-word uses) |
| Product Hunt / Lobsters | no launch found (sites bot-blocked; no indexed result) |
| r/selfhosted | no thread found (Reddit search RSS for "vasta selfhosted" returned one unrelated Italian thread) |
| Stars after 9 days | 41 |
| External issues after 9 days | 2 (#43, #35) |

### What I could not verify

- No public demo, no hosted instance, no homepage.
- No Docker image size published; the Dockerfile is Node 24-slim + production `node_modules` + built web app, so it is materially larger than Lumen's 15 MB Go container, but I did not pull the image.
- No star timeline (GitHub stargazers API requires auth), so I cannot show a launch spike.
- Reddit score/karma not exposed to anonymous RSS; comment count is reliable, upvotes are not.
- No private analytics, retention or download counts. The README's downloads badge exists, but I did not resolve it.
- I did inspect the `v0.4.1` source tarball for the encryption, `randomUUID`, share-target and calendar-password claims; see the appendix. I did not run the app.

---

## (b) Feature diff vs Lumen 0.6.1

Legend: **Vasta** = shipped in 0.4.1; **Lumen** = shipped in 0.6.1; "—" = not documented. This compares shipped behavior, not roadmaps.

| Area | Vasta 0.4.1 | Lumen 0.6.1 | Edge |
|---|---|---|---|
| License | AGPL-3.0-only | AGPL-3.0-or-later | tie |
| Server | Node 24 + Hono, TypeScript | Go, own encrypted SQLite store | Lumen (size, at-rest crypto) |
| Container | Node image, amd64/arm64 | 15 MB single container | Lumen |
| Web UI | Vue 3 SPA + installable PWA | none | **Vasta** |
| Desktop | Tauri 2 webview wrapper around the web app | native Qt 6 C++ Widgets | Lumen (native), Vasta (cross-platform) |
| Mobile | PWA only; no native app | native Android (Kotlin/Compose), widgets, share target, sealed media | **Lumen** |
| Offline | full SQLite replica per device; offline edits merge | desktop keeps notes in RAM; Android local-first for text/tasks/media | **Vasta** |
| Sync | Yjs CRDT + HLC per-field LWW, tombstones, SSE | incremental LAN/USB sync; server is source of truth; limited conflict resolution | **Vasta** |
| Multi-user | accounts, spaces, roles, invites | none (single-user) | **Vasta** |
| Collaboration | comments, @mentions, notifications, activity, sprints, flow metrics | none | **Vasta** |
| Live co-editing / presence | no (#22) | no | tie (both weak) |
| Encryption at rest | no app-level at-rest encryption (source grep); SQLite + media on disk; calendar passwords plaintext (#16) | AES-256-GCM for store and attachments | **Lumen** |
| E2EE | no | no | tie |
| Object types / properties / relations | text, number, date, select, relation, formula, checkbox, URL | text, number, date, boolean, select, relation | tie |
| Views | list, table, board, grid, wall, gallery; calendar layout planned (#41) | list, gallery, wall, table, kanban, embed; graph | tie / Lumen (graph) |
| Queries / formulas | filters, sorts, groups, formulas | filters, groups, calculations, saved queries | tie |
| Templates | placeholders, `ask()`, `choose()`, conditions, snippets | templates | Vasta (formula depth) |
| Daily notes / calendar | daily notes, month/week/day, iCal subscriptions | daily notes, day/3-day/week/month; no external calendars | Vasta (iCal) |
| Tasks | status, priority, due; recurrence not documented | tasks + recurrence | Lumen |
| Habits / nutrition | habits with streaks, food diary with goals | no | Vasta (scope) |
| Reminders | ntfy, Telegram, desktop notifications | no | Vasta |
| Capture | Telegram bot, browser extension, HTTP API, desktop global shortcut, iOS Shortcuts, MCP/CLI; email/Raycast planned | Android share target, browser extension, desktop capture, API, MCP | tie / Vasta (Telegram) |
| MCP | yes, broad tool set (objects, queries, habits, food, comments) | yes | tie |
| Public read/write API | no (#23); capture-only API + OpenAPI | API v2 | **Lumen** |
| Import | Capacities full export in-app; no Obsidian/Notion/Markdown (#5) | Markdown/txt/images/PDF; no Capacities importer | **Vasta** |
| Export | none yet (#1); only the SQLite file | full export + Markdown converter | **Lumen** |
| Version history | not documented | yes | **Lumen** |
| Trash | 30-day trash for objects, types, spaces | yes | tie |
| Tabs / split view | no (#9) | tabs; split view not documented | Lumen |
| Themes / accessibility | light/dark, colors | Capacities/Lumen/Noir/Terminal/Papel, density, text size, reduced motion | **Lumen** |
| Media | disk or S3; 25 MB import cap | sealed media; 50 MB attachment cap | tie |
| Deploy | public image, compose, setup code, HTTPS/backup docs | build from source, personal compose, no public image, no validated public deploy | **Vasta** |
| Local mode | desktop runs bundled server; move to server later | requires a server | **Vasta** |
| Docs | 14 operator docs; no user guide (#36); READMEs in 5 languages | Spanish internal docs; no public user guide | Vasta (breadth), tie (user guide) |
| Demo | none | none | tie |
| Signed builds | no; Gatekeeper/SmartScreen warnings | Linux binary; Qt 6 libs required on host | Vasta (cross-platform), Lumen (Linux native) |
| AI features | none | none | tie |
| Community | 41 stars, 1 contributor, 0 PRs, 39 open issues | private/small; v0.6.1 shipped | Vasta (public) |
| Maturity | alpha 0.4.1 | 0.6.1 shipped | **Lumen** |

### Where Vasta genuinely leads

1. **Time to first note.** Download, "Start on this computer", write. No Docker, no server, no token, no config. Lumen requires a running API and a token file before the desktop opens.
2. **Time to phone.** Open the server URL, "Add to Home Screen", done. No APK, no store, no USB. Lumen has a native Android app, but it is an APK/sideload, not a URL.
3. **Capacities migration.** In-app import of a Capacities Full Export ZIP, with a report. This is the single most relevant feature for the audience Lumen wants.
4. **Multi-user.** Shared spaces, invites, roles, comments, @mentions, notifications, activity, sprints, flow metrics. Lumen has none, by design.
5. **Capture breadth.** Telegram bot, iOS Shortcuts recipe, desktop global shortcut, browser extension, HTTP API. Lumen has Android share/extension/desktop/API but no Telegram or iOS.
6. **Distribution.** Public image, compose file, `.env.example`, setup code in logs, Caddy snippet, backup CLI, 5-language READMEs, public roadmap/issues. Lumen has no public image, no public compose, and no 5-language README.
7. **Personal trackers.** Habits and a food diary are unusual for a PKM and create daily retention. They are also scope creep for Lumen; see "what not to copy".
8. **Docs depth for operators.** `docs/sync.md`, `docs/data-model.md`, `docs/accounts.md` are unusually detailed for a 9-day-old project.

### Where Lumen genuinely leads

1. **Native desktop.** Qt 6 C++ Widgets, no Electron, no webview. Vasta's desktop is a Tauri window around the web app; its performance and failure modes are the browser's.
2. **Native Android.** Kotlin/Compose, widgets, share target, sealed media. Vasta's mobile is a PWA with no share target (#26) and Safari/WebKit replica problems (#32).
3. **Encryption at rest.** AES-256-GCM for the store and attachments. Vasta has no app-level at-rest encryption (source grep); calendar passwords are plaintext (#16); media authorization is per-login, not per-space (#2).
4. **15 MB Go server.** Vasta ships a Node 24 image with a built web app. Lumen's footprint is a genuine differentiator for homelabs and small VPSes.
5. **Data exit.** Full export + Markdown converter. Vasta's #1 issue is literally "People must be able to leave with their data. Today the only way out is the SQLite file."
6. **Version history, tabs, graph, find-in-page, block grouping, dashboards.** Small but real desktop-power-user advantages.
7. **API v2 read/write + MCP.** Vasta's public API is capture-only; read/write is planned (#23).
8. **Task recurrence.** Vasta documents status/priority/due but not recurrence.
9. **Themes/accessibility.** Density, text size, reduced motion, five themes.
10. **Maturity.** 0.6.1 shipped vs alpha 0.4.1 with untested Windows/Linux builds.

### Capability gaps: close, watch, or ignore

| Vasta capability | Lumen today | Verdict |
|---|---|---|
| Web/PWA access | none | **Watch.** Not the top blocker for the native niche; a read-only share/capture page is the 80/20. Full web editor only if multi-user becomes a goal. |
| Desktop local mode (no server) | requires a server | **Close.** Highest-leverage onboarding gap; Lumen's 15 MB Go server makes it feasible. |
| Capacities importer | Markdown/txt only | **Close.** The audience is literally leaving Capacities; this is the migration wedge. |
| Multi-user / shared spaces | none | **Ignore.** Explicit Lumen policy; Vasta's own live collab is missing (#22). |
| CRDT offline merge | limited conflict resolution | **Watch.** Architecturally expensive; Lumen's native/Android sync is good enough for personal use. Do not chase Vasta's web-CRDT design. |
| Telegram / iOS Shortcuts capture | Android share, extension, API | **Maybe.** Telegram is a cheap, visible win if the no-external-services policy allows opt-in. |
| iCal / CalDAV calendars | internal calendar only | **Later.** Nice parity, not an adoption blocker. |
| S3-compatible media | local blobs | **Later.** Homelab nicety; not a first-100-users need. |
| Habits / food diary | none | **Ignore.** Scope creep; not Capacities parity. |
| Whiteboards | on Lumen's priority list | **Watch.** Capacities shipped them; Vasta has a community implementation unmerged. Shipping first is a visible parity win, but not before import/install. |
| Rating / select icons / calendar layout | partial | **Low.** Small parity polish; do after the trust/distribution work. |

### Both weak / deliberate non-goals

- **Live co-editing and presence.** Vasta #22 is open; Lumen has no multi-user at all. Neither can claim Google-Docs-style collaboration.
- **E2EE.** Neither has it. Lumen encrypts at rest but decrypts in RAM; Vasta has no app-level at-rest encryption. Do not let marketing imply E2EE.
- **Public demo.** Neither has one. Vasta has GIFs; Lumen has screenshots. A demo is an open goal.
- **User guide.** Vasta #36; Lumen's docs are internal/Spanish. Both leave new users to figure out the model.
- **Obsidian/Notion import.** Vasta #5; Lumen imports Markdown as plain text, not as a vault with links/front matter.
- **Whiteboards.** Vasta #38 (community implementation offered, unmerged); Lumen has it on its priority list. Capacities shipped whiteboards, so this is becoming table stakes.
- **CalDAV/Google/iCloud.** Vasta has iCal links only (#14); Lumen has no external calendars.
- **Signed desktop builds.** Vasta is unsigned; Lumen's Linux binary depends on host Qt libs. Both have installer friction.

---

## (c) Issues catalog, ranked

### Method and caveat

There are **43 issues total: 39 open, 4 closed**. **41 of 43 were filed by the author** (`ariedotme`); only two are external user reports (#43 by `yazdipour`, #35 by `gentlice`). Every issue has **0 reactions**; the maximum comment count is **1**. There is no upvote signal to rank by.

So this is not a "most-upvoted bugs" list. It is a **severity × adoption-impact** ranking, with a "Signal" column: **User** (external report), **Author** (maintainer roadmap/bug), **User+Author** (user request the author adopted). Where the evidence is a Reddit comment rather than an issue, I say so.

Label counts (overlapping): `enhancement` 29, `performance` 3, `security` 2, `bug` 2, `tests` 3, `help wanted` 3, `good first issue` 2, `maintenance` 1.

### Ranked catalog

#### P0 — adoption or trust blockers

| # | Issue | Type | Signal | Why it matters | Lumen angle |
|---|---|---|---|---|---|
| 1 | **#1 No Markdown/JSON export** — "Today the only way out is the SQLite file." | data portability | Author | A self-hosted PKM without export is a lock-in trap; users ask about moving between Mac/Windows in the launch thread. | Lumen already exports; make it one-click, tested, and marketed. |
| 2 | **#43 Cannot create a new property** on Docker + web over `http://192.168.x.x` | bug | **User** | Root cause (from comment): `crypto.randomUUID` is unavailable outside secure contexts, so core editing breaks on a plain-HTTP LAN — a very common self-host setup. | Lumen rejects remote HTTP and needs HTTPS; ship an HTTPS wizard/Caddy recipe so users never hit this class of bug. |
| 3 | **#5 Import Markdown folders, Obsidian, Notion** | migration | Author | Vasta only imports Capacities; Obsidian/Notion refugees have no path. | Lumen's Markdown import is partial (plain text); make it a real importer with front matter/links. |
| 4 | **#3 / #4 Performance at 50k objects** — calendar month ~1 s, type page 1.3 s, new replica fill 42 s | performance | Author | The web/SQLite architecture has visible ceilings; "fast" is not a Vasta claim. | Lumen is native; publish reproducible 50k-object numbers and win the benchmark. |
| 5 | **#2 `/media/*` checks login but not space** | security | Author | A logged-in user who learns a file hash can fetch media from a space they are not in. | Lumen is single-user, but scope media to owner/space anyway; document it. |
| 6 | **#16 Encrypt stored calendar passwords** | security | Author | Subscribed-calendar passwords are plaintext in the DB. | Lumen's encrypted store is a direct answer; publish the threat model. |
| 7 | **#22 Live collaboration in a note** | collaboration | Author | Multi-user is a headline feature, but edits merge seconds late with no presence. | Lumen has no multi-user; do not chase this unless policy changes. |
| 8 | **#35 macOS local mode does not start** (fixed in 0.3.3) | install | **User** | "Start on this computer" crashed on macOS because the bundled Node lacked JIT entitlement. Fixed in one day, but shows desktop packaging fragility. | Lumen's native Qt avoids bundled Node, but must bundle Qt libs and test installers. |
| 9 | **#29 / #30 Windows and Linux desktop builds untested** | install | Author | The author admits he only tested macOS; Windows/Linux builds exist but were never opened. | Lumen is Linux-first; test AppImage/Flatpak and publish a support matrix. |
| 10 | **#36 No user guide** | onboarding | Author/User | "People who just installed the app have nothing to read"; requested by a Windows Capacities refugee. | Lumen's docs are internal/Spanish; an English quickstart + user guide is a fast win. |

#### P1 — important gaps / quality risks

| # | Issue | Type | Signal | Why it matters | Lumen angle |
|---|---|---|---|---|---|
| 11 | **#26 Android share target missing** | mobile | Author | The PWA cannot receive shares on Android; iOS has no share target at all. | Lumen's Android share target is a direct advantage; show it in the demo. |
| 12 | **#32 WebKit replica fails with `SQLITE_CORRUPT`** | data integrity | Author | Safari/WebKit users may run without a replica, or worse, with a corrupt one. | Native Android/desktop avoids browser storage; say so carefully (Lumen still has its own sync risks). |
| 13 | **#34 Translate the interface** | i18n | Author | UI is English-only; READMEs are translated, which sets a false expectation. | Lumen is Spanish-first; add English UI/docs for global reach. |
| 14 | **#23 Public HTTP API to read/edit objects** | integration | Author | Capture API only; agents use MCP/CLI. Integrators must wait. | Lumen's API v2 is ahead; document recipes. |
| 15 | **#8 Map Capacities saved views** | import fidelity | Author | Capacities import brings types/properties but not saved views; queries need filters again. | Lumen can leapfrog by mapping saved views/queries in its importer. |
| 16 | **#25 Editor chunk is 553 KB (171 KB gzip)** | performance | Author | The object page loads a large TipTap/ProseMirror chunk; slow on mobile. | Native desktop has no bundle; benchmark cold start. |
| 17 | **#9 Tabs and split view** | desktop UX | Author | No tabs, no side-by-side; "not planned for now". | Lumen has tabs; split view may still be a gap. |
| 18 | **#13 Name the reverse side of a relation** | data model | Author | A person shows "Books" instead of "Wrote"; Capacities parity gap. | Check Lumen's reverse-relation labels; small parity win. |
| 19 | **#14 CalDAV, Google and iCloud calendars** | calendar | Author | Only iCal links today; no two-way calendar sync. | Lumen has no external calendars; later. |
| 20 | **#38 Whiteboards** | parity | Author/User | Capacities shipped infinite whiteboards; a user offered a full iPad/Apple Pencil implementation, unmerged. | Lumen has it on its priority list; shipping first would be a visible parity win. |

#### P2 — parity polish / housekeeping

| # | Issue | Type | Signal | Why it matters | Lumen angle |
|---|---|---|---|---|---|
| 21 | **#39 Rating property** (stars, half steps) | parity | Author | Books/Movies need ratings; currently a number. | Low priority unless users ask. |
| 22 | **#40 Select option customization** (icons, defaults, descriptions) | parity | Author | Capacities-style select chips are richer. | Lumen already has colored selects; check icons/defaults. |
| 23 | **#41 Calendar layout for type views** | parity | Author | Type views lack a calendar layout. | Lumen has calendar + views; verify parity. |
| 24 | **#42 Telegram template questions** | capture depth | Author | Quick capture can ask template questions on desktop, not Telegram. | Lumen has no Telegram; skip unless Telegram ships. |
| 25 | **#10 Email capture / #11 Raycast extension** | capture breadth | Author | Email and Raycast are planned, not shipped. | Lumen: later; Android share covers the common case. |
| 26 | **#24 Remove account UI, #28 extension repo, #31 icon, #6 comment scroll, #7 flaky nutrition test** | housekeeping | Author | Small polish/maintenance. | Not adoption-relevant. |

### The five issues that matter most

**1. No export (#1).** The exact text: *"People must be able to leave with their data. Today the only way out is the SQLite file."* This is a trust killer for self-hosters, and it showed up in the launch thread when a user asked whether data moves between Mac and Windows; the author pointed at the SQLite file and a future export. Lumen's full export is a genuine differentiator — but only if it is easy, documented and tested. A "leave anytime" page with a restore drill is worth more than another view type.

**2. Core editing breaks on a plain-HTTP LAN (#43).** A user on Docker + web could not add a property. The comment diagnosed it: `crypto.randomUUID` only exists in secure contexts, so `http://192.168.1.x:5174` blocks it. This is the classic self-hosted trap: the app works on localhost, then breaks when a second device opens it. Lumen already refuses remote HTTP and requires HTTPS/tunnel, which avoids the bug but creates setup friction. The winning move is not "we don't have that bug"; it is **an HTTPS setup that takes one command** (Caddy or self-signed LAN mode) plus a clear error message.

**3. Performance at 50k objects (#3, #4).** Vasta's own numbers: a month of calendar reads all 12,000 tasks (~1 s), a 30,000-page type page takes 1.3 s, and filling a new replica at 50k objects takes 42 s. The proposed fixes are derived date tables and avoiding `content`/`clock` columns. This is the cost of the web/SQLite-everywhere architecture. Lumen is native and has no replica fill; a published benchmark ("50k objects: search X ms, calendar month Y ms, cold start Z s") would be credible and hard to answer.

**4. Security debt (#2, #16).** Media is authorized by login, not space membership; calendar passwords are plaintext. Neither is catastrophic for a single-user instance, but both are exactly what self-hosters audit. Lumen's AES-256-GCM store is a strong story if Lumen is honest that it is **not E2EE** and publishes a threat model. Vasta has no app-level at-rest encryption at all, so Lumen can own "encrypted at rest, keys you control" without overclaiming.

**5. Onboarding: no user guide (#36) and a late importer (#37).** A Windows Capacities user asked for both; the author shipped the in-app importer in 0.4.0 within days and fixed the toggle/indent behavior in 0.4.1 within two. That responsiveness is admirable, but it also shows the product was developer-documented, not user-documented. Lumen's docs are even more internal (Spanish, architecture/QA-heavy). An English "first 15 minutes" guide plus a Capacities import walkthrough is the highest-leverage content Lumen can write.

### What the tracker says about the product

- **It is a public roadmap, not a support channel.** 95% of issues are author-filed; only two external users opened issues. 41 stars and 16 Reddit commenters produced almost no issue volume. Either most launch interest did not install, or installed users lurk. Do not read 39 open issues as 39 angry users.
- **The maintainer is responsive.** macOS crash fixed in one day; Capacities importer within days; toggle behavior in two. This is Vasta's best cultural asset.
- **Alpha quality is visible.** Untested Windows/Linux builds, a WebKit replica corruption, a LAN-HTTP editing bug, and a 553 KB editor chunk. The product is broad but shallow in places.
- **Security and portability are acknowledged, not solved.** No export, no app-level at-rest encryption, plaintext calendar passwords, media authz. These are the first things a skeptical self-hoster will find.
- **Community contribution is nascent.** 0 merged PRs; a user offered a complete whiteboard implementation and it is still a comment. The bus factor is 1.

---

## (d) Launch sentiment

### The launch

Vasta launched as a single Reddit post in **r/NoteTaking** on **2026-10-21 21:46 UTC**, titled *"Made my own self-hosted notes app because Capacities was almost what I wanted"* by `/u/LilPranxis` (the author). The repo was created ~21 hours later, on 2026-10-22. This was not a coordinated HN/Product Hunt launch; it was a show-and-tell that converted into an open-source release.

The post is candid to the point of being a case study:

- *"I really liked Capacities, but it was not everything I needed, and it didn't fit my philosophy of self-hosting (almost) everything. So I built this app for me and my girlfriend."*
- *"PS: I used AI a lot to build it, and I reviewed the code as much as my free time allowed."*
- *"It was made for my own use, at home, with my girlfriend, so I don't even know if I will release it publicly."*
- *"I only tested it on macOS, the Windows and Linux builds exist but I never opened them."*
- *"An importer from Capacities, because that's where all my notes were."*

Thread size: **35 comments, 16 external commenters, 18 author replies**. Reddit's anonymous RSS does not expose the score, so I cannot report upvotes.

### Themes

Counts are approximate, from the 35 comments.

| Theme | Count | Representative quote | Implication |
|---|---|---|---|
| Excitement / "I'd try it" | ~8 | *"Would love to try it! Looks awesome!"* — supernitin | Demand for a self-hosted Capacities is real, even in a general note-taking subreddit. |
| Open-source encouragement | 2 | *"Open source it and we'll all participate."* — Old-and-grumpy | License/community is part of the pitch; AGPL was accepted without debate. |
| Capacities refugee fit | 2 | *"Installed the Windows version. As a current user of Capacities (free), I felt right at home here. Even better - Vasta provides features you don't get with Capacities Free: task management and search queries."* — Important_Oil_7108 | The object model + free queries/tasks is the wedge. |
| Import success | 1 | *"Just used it - worked pretty well. Way better than copying notes manually."* — Important_Oil_7108 | The Capacities importer is a killer feature, even with fidelity gaps. |
| Docs / onboarding | 1 | *"Is there any user documentation available? Trying to figure out how to import from Capacities..."* — Important_Oil_7108 | Even a well-documented repo lacks a user guide. |
| Feature request | 1 | *"The normal behavior is to use [tab] key to indent one level - this does not work for me."* — Important_Oil_7108 | Capacities muscle memory matters; fixed in 0.4.1 within two days. |
| AI-code skepticism | 1 | *"I built-" immediately followed with "Ai did it and I reviewed-". ... Cuck coders really wanting credit...* — Precarious3145 | A visible trust barrier, even if only one commenter. |
| Brand/name confusion | 1 | *"Nombre?"* — Murky-Relation-7337 | "Vasta" is a common word; discoverability is weak. |

### What people praised

- **The scope for a solo developer.** Multiple commenters said some version of "looks great, well done, you put a lot of thought into it."
- **The Capacities feel.** The Windows user said he "felt right at home" and even joked about forgetting which app he was in.
- **Free features that Capacities paywalls.** Tasks and search queries were explicitly called out.
- **The one-line command bar.** *"The one-line command bar is so good"* — Mayur__007; the author clarified the Tauri quick capture opens a tiny window.
- **The importer.** A Windows Capacities user came back after using it: *"Just used it - worked pretty well."*
- **Responsiveness.** The toggle/indent request was fixed in 0.4.1 within two days; the author replied to nearly every comment.

### What skeptics asked

- **"Did AI write this?"** This was the only hostile thread. The author defended himself as a 12-year developer and tech lead who reviewed the code, and said he disclosed AI use precisely because some people dislike it. The exchange matters: for a certain segment, "vibe-coded" is a disqualifier regardless of features. Lumen should lead with tests, verification and engineering transparency, not with speed of generation.
- **"Is there documentation?"** A practical, not ideological, concern. The answer was "no proper user guide yet."
- **"Can I move data between Mac and Windows?"** The answer exposed the missing export: copy the SQLite folder, or wait for the roadmap. This is the data-portability concern in the wild.
- **"Which AI did you use?"** Curiosity mixed with suspicion; the author answered.
- **No one asked "why not Obsidian?" or "why not Capacities?"** The README's "Why Vasta" section preempts those arguments: Capacities is closed/cloud; Notion needs an account; Obsidian is Markdown with paid sync and hard sharing; AFFiNE/AppFlowy need Postgres/Redis and many services. That section is doing the launch messaging work.

### What was absent

- **No Hacker News thread.** HN Algolia returns only unrelated hits for "vasta" (the Vastaamo breach and ordinary-word uses): the 2024 BBC story (53 points), the 2026 Guardian story (176 points, 169 comments), and the 2021 Wired story. There is no "Show HN: Vasta" and no comment mentioning the PKM app.
- **No Product Hunt or Lobsters launch found.** Both sites bot-blocked; no indexed result surfaced.
- **No r/selfhosted thread.** Reddit search RSS for "vasta selfhosted" returned one unrelated Italian cooking thread.
- **No license or pricing debate.** AGPL-3.0 and free were accepted without argument. The audience is not afraid of self-hosting; it is afraid of lock-in and AI slop.

### Brand and discoverability

"Vasta" is a bad search name. It is an ordinary word in Italian/Spanish/Portuguese, and it collides with **Vastaamo**, the Finnish psychotherapy data breach that dominates search results and HN. A user searching "Vasta notes" or "Vasta app" will not reliably find the PKM. The repo has no homepage, no demo domain and no docs site, so there is no strong SEO anchor.

For Lumen, this is an opening: own the category queries — "self-hosted Capacities alternative", "open source Capacities", "Capacities import", "Capacities alternative Linux", "encrypted PKM self-hosted". Vasta cannot easily own its own name; Lumen can own the problem.

### What it means for Lumen

- **The demand is real but small and high-intent.** 16 commenters asked to try it; 41 stars; 2 external issues. This is not a mass migration. It is exactly the audience Lumen's own research describes: technical, privacy-motivated, willing to run Docker, influential in their circles.
- **The conversion path is import + docs + local-first.** The launch thread's questions were about importing, documentation, and moving data — not about whiteboards, AI, or themes.
- **Vasta's alpha gaps are Lumen's openings.** No export, no live collab, no native mobile, web/PWA bugs, no app-level at-rest encryption, one maintainer. Lumen should not out-feature Vasta; it should out-trust and out-onboard it.
- **Speed matters.** Vasta shipped an importer in days and a toggle fix in two. Lumen cannot take a quarter to respond to the same requests if it wants the same users.

---

## (e) Adoption levers, ranked

Ranking is by expected effect on **self-hosted Capacities-seeker adoption** per unit of effort, given Lumen's current architecture and its explicit non-goals (no AI, no multi-user/collaboration, native desktop).

| Rank | Lever | Why (evidence) | Effort | Impact | Horizon |
|---|---|---|---|---|---|
| 1 | **Public one-command install** — published image, compose, `.env.example`, setup code, HTTPS/backup docs | Vasta's entire launch is four commands + a public image; Lumen has no public image and no validated public deploy (README). Self-hosters do not build from source. | M | Very high | 2–4 weeks |
| 2 | **Desktop local mode** — bundle the Go server, "start on this computer", move to server later | Vasta 0.3.1's best onboarding feature; the launch thread's Windows user installed the desktop app, not Docker. Lumen's 15 MB Go server makes this feasible. | M | Very high | 3–6 weeks |
| 3 | **Capacities full-export importer** — in-app ZIP, types/properties/tags/links/daily/media, report | Vasta shipped it in 0.4.0 after a Windows Capacities user asked; a Windows Capacities user in the launch thread said it "worked pretty well". Lumen's own priority list has it (#13). | M | Very high | 3–6 weeks |
| 4 | **English quickstart + user guide + 90-second demo** | Vasta #36 and the Reddit "Is there any user documentation?" question. Lumen's docs are Spanish/internal; there is no public "first 15 minutes". | M | High | 2–4 weeks |
| 5 | **Export/backup trust surface** — one-click export, restore drill, "leave anytime" page | Vasta #1: "the only way out is the SQLite file." Lumen already exports; the gap is packaging and proof. | S–M | High | 1–3 weeks |
| 6 | **Encryption-at-rest proof** — threat model, `lumen verify`, media scoping, key rotation docs | Vasta has no app-level at-rest encryption (source grep), plaintext calendar passwords (#16), media authz bug (#2). Lumen's AES-256-GCM is a real wedge if explained honestly (not E2EE). | S–M | High | 1–2 weeks |
| 7 | **Telegram capture** (policy-sensitive) | Vasta's headline capture channel; low cost via Bot API long polling; Lumen has Android share/extension but no Telegram. | S–M | Medium-high | 2–4 weeks |
| 8 | **50k-object performance benchmark** — cold start, search, calendar month, type page, memory | Vasta #3/#4: ~1 s calendar month, 1.3 s type page, 42 s replica fill at 50k. Lumen is native; publish reproducible numbers. | M | Medium-high | 2–4 weeks |
| 9 | **Bundled/signed desktop builds** — AppImage/Flatpak with Qt libs; Windows/macOS later | Vasta #29/#30: Windows/Linux builds untested, unsigned, Gatekeeper/SmartScreen warnings. Lumen's binary needs host Qt 6 libs. | M | Medium-high | 3–6 weeks |
| 10 | **Read-only web share/capture page** — view a note/collection, capture form, tokenized link; **not** a full editor | Vasta's web/PWA is core for phone/sharing; but Lumen has native Android and excludes multi-user. 80/20 is view + capture, not a second editor. | M–L | Medium now; high if multi-user | 6–10 weeks |

### The top five in detail

#### 1. Public one-command install

**What to ship:** a multi-arch image (amd64/arm64), a compose file, a `.env.example`, a first-run setup code printed in logs (or a one-time setup URL), a Caddy/HTTPS recipe, `lumen backup`/`lumen restore`, a healthcheck, and a "copy this, paste that" quickstart. Sign the image and publish an SBOM; Vasta publishes `:latest` and version tags, which is convenient but not a trust story.

**Why it is #1:** Lumen's README currently says the public deployment has not been validated and the compose file lives in `ops/personal/`. That is fine for the author's machine; it is a wall for everyone else. Vasta's four-command flow is not technically impressive, but it is the difference between "I'll try it" and "I'll read the source." The launch thread shows people will install a desktop app; the self-hosted audience will install a container only if the first command works.

**Definition of done:** a clean VPS, no repo checkout, `curl` + `docker compose up -d` + open URL + create account, under five minutes, with HTTPS and a backup taken. Record it; put the video in the README.

#### 2. Desktop local mode

**What to ship:** on first run, the desktop offers **Start on this computer** or **Connect to a server**. Local mode spawns a bundled `lumen-server` on `127.0.0.1` with a random port and a data directory, stores the API token in the OS keyring, and opens the normal Qt UI. **Settings › Move to a server** exports the local store and imports it into a server account, then switches the client.

**Why it is #2:** this is Vasta's best idea and the launch thread's path of least resistance. Lumen currently requires a server URL and a token file before the first note. That is backwards for a desktop-first product. The Go server is ~15 MB; bundling it is an engineering task, not a rewrite.

**Trade-offs to decide explicitly:**
- Lumen's desktop currently keeps notes in RAM and QSettings only for visual preferences. Local mode necessarily writes an encrypted store to disk. That is a privacy-posture change; document it and put the key in the OS keyring, not in QSettings.
- Local mode needs a migration path to a server (export/import, not a raw file copy) and a clear "backups are yours" warning, like Vasta's.
- Support matrix: Linux first, then Windows/macOS. Do not claim cross-platform until tested.

**Why it beats a web frontend for Lumen:** it gives the "try it in one click" experience without building a second UI, and it preserves the native-first architecture.

#### 3. Capacities full-export importer

**What to ship:** Settings › Import, choose a Capacities Full Export ZIP, pick a space, map types/properties/tags/links/daily notes/media, show a report of what was skipped. Add a dry-run mode and idempotency (Vasta explicitly warns that importing the same export twice makes copies). Map saved views/queries where possible; Vasta leaves query filters behind (#8), so this is a chance to leapfrog.

**Why it is #3:** the audience is literally "people leaving Capacities." Vasta's importer is the feature a Windows Capacities user praised, saying it worked "way better than copying notes manually." Lumen's current import treats Markdown as plain text, which is not a migration; it is a copy. Lumen's own priority list already has "Capacities importer: consume their Full Export ZIP... one-command migration" (#13). Do it before whiteboards.

**Definition of done:** a real Capacities export with 1k+ objects imports in one pass; links become mentions; daily notes land on the right dates; media arrives; the report names unsupported queries; re-import does not duplicate.

#### 4. English quickstart + user guide + demo

**What to ship:** `README.en.md`, `QUICKSTART.md` (install → first note → @ mention → daily note → query → export), a 90-second screen recording, a downloadable sample library (fictional data) that opens with one command, and a troubleshooting page. Link the guide from the app's first-run screen.

**Why it is #4:** Vasta's user guide issue (#36) was opened because "people who just installed the app have nothing to read." The Reddit thread asked the same question. Lumen's docs are Spanish, architecture-heavy, and written for a returning agent, not a new user. The product may be better, but the first 15 minutes are worse.

**Demo note:** Lumen has no web UI, so a hosted "click here" demo is hard. The substitute is a **downloadable sample library + video**. If local mode ships, the demo becomes "download the app, click Start, you're in the sample library" — better than a web demo because it shows the real native app.

#### 5. Export/backup trust surface

**What to ship:** a **Data** settings page with "Export everything" (Markdown + JSON + media), "Back up server" (encrypted store + keys + blobs), "Restore drill" instructions, and a plain-language page: what is encrypted, what the server can read, what happens if you lose the key, how to leave. Test the restore into a fresh container and publish the result.

**Why it is #5:** Vasta's #1 issue is the clearest self-hosted trust failure: no export, only a SQLite file. Lumen already has export and version history; the gap is that nobody can see it, and the backup story (SQLite + blobs + keys) is more complex than Vasta's. Turn the complexity into a checklist and a command, not a paragraph.

### Is a web frontend a real adoption blocker?

**Short answer: not for Lumen's current single-user, native-first niche; yes for the multi-user/team segment Vasta targets, which Lumen has deliberately excluded.**

**Evidence that web matters:**
- Vasta's web app is its **only** UI. The desktop is a Tauri window around it; the phone is a PWA. Its pitch ("open it in a browser, on your phone or in the desktop app") depends on the web.
- Three of its most painful issues are web/PWA-specific: #43 (secure-context `crypto.randomUUID` on a LAN), #32 (WebKit `SQLITE_CORRUPT`), #26 (no Android share target). A web UI creates a class of bugs a native app does not have.
- Self-hosted users often expect to open a URL on a laptop/tablet/Chromebook without installing anything. A link is the lowest-friction demo and the easiest way to share with a non-technical person.
- If Lumen ever wants families/teams, a browser client is table stakes; Vasta's shared spaces are browser-first.

**Evidence that web is not the top blocker:**
- The launch thread's Windows Capacities refugee **installed the desktop app** and asked for docs/import/toggle behavior, not a web UI.
- Lumen already has a native Android app with a share target and widgets — the phone job is covered for the personal-use case.
- A full web editor would duplicate the Qt editor, conflict with the "native desktop, no webview" product decision, and split QA across two UIs. That is a large, strategic cost, not a checklist item.
- Lumen's own policy lists collaboration/multi-user as "never build"; a full web UI is most valuable precisely for that segment. Building it without multi-user would be a demo, not a product.

**Recommendation:**
1. **Do not build a full web editor now.**
2. **Do build a read-only web share/capture page** as a small, separate surface: tokenized link to view a note/collection, plus a capture form/PWA share target. No editor, no sync, no second source of truth. This covers "open from any device" and "share with family" at 20% of the cost.
3. **Revisit the full web UI only if** Lumen changes its multi-user policy, or if user research shows browser access is the #1 reason people bounce. The evidence today does not support it.
4. **Use local mode, not a web app, as the try-before-install funnel.** It is more differentiated and cheaper.

### What not to copy

- **Multi-user/shared spaces.** Lumen's policy excludes collaboration/multi-user. Vasta's own live collaboration is missing (#22), so the feature is half-built even there. Do not chase it.
- **Habits and food/nutrition.** Personal scope creep; not Capacities parity; distracts from the core object/daily-note loop. Vasta's author added them for himself.
- **Sprints and flow metrics.** Team PM features for a personal PKM; wrong audience.
- **Seven releases in six days.** Impressive, but the same period produced a macOS crash, a WebKit corruption report, and untested Windows/Linux builds. Lumen should ship stable, tested releases with a changelog, not a daily cadence.
- **AI-generated velocity.** The launch thread's hostile comment is a warning: some users will disqualify a project they perceive as vibe-coded. Lumen should emphasize tests, verification and review — not speed.
- **A web-only UI.** It creates secure-context, WebKit and PWA bugs that Lumen's native clients avoid.
- **A generic name.** "Vasta" collides with Vastaamo and ordinary words; Lumen should own category keywords, not a dictionary word.

### 30/60/90-day plan

**0–30 days (trust and distribution):**
- Publish the image + compose + `.env.example` + setup code + Caddy/HTTPS + backup/restore docs.
- `README.en.md` + `QUICKSTART.md` + a 90-second demo video + screenshots.
- One-click export page + restore drill; publish the result.
- Encryption-at-rest page: threat model, `lumen verify`, media scoping, "not E2EE" honesty.
- Open the awesome-selfhosted PR (Vasta is not listed; first-mover advantage).

**30–60 days (migration and proof):**
- Capacities Full Export importer, with report and idempotency.
- 50k-object benchmark, reproducible script, published numbers.
- Telegram capture spike (if policy allows; otherwise email/IMAP or Android share improvements).
- Agent recipes: Claude Code / Codex MCP setup, API v2 examples.

**60–90 days (friction removal):**
- Local mode beta (Linux first): bundled server, keyring, move-to-server.
- AppImage/Flatpak with bundled Qt libs; test on a clean distro.
- Read-only share/capture page RFC (not a full editor).
- Contributor program: `CONTRIBUTING.md`, good-first-issues, public roadmap.

### Positioning one-liners

- "Capacities' object model. Native speed. Your encrypted server."
- "No Electron, no webview, no AI, no subscription."
- "Import your Capacities export. Export everything back."
- "One 15 MB container, one encrypted SQLite file, desktop and Android."
- "Start on your laptop; move to your server when you're ready." *(only after local mode ships)*
- "The features Capacities paywalls — queries, tasks, version history, API, MCP — are free here, forever."

---

## (f) Links

### Primary

- Repo: <https://github.com/ariedotme/vasta>
- README: <https://github.com/ariedotme/vasta/blob/main/README.md>
- Releases: <https://github.com/ariedotme/vasta/releases>
- Latest release 0.4.1: <https://github.com/ariedotme/vasta/releases/tag/v0.4.1>
- Changelog: <https://github.com/ariedotme/vasta/blob/main/CHANGELOG.md>
- License: <https://github.com/ariedotme/vasta/blob/main/LICENSE>
- Roadmap project: <https://github.com/users/ariedotme/projects/1>

### Docs

- Deploy / HTTPS / backups: <https://github.com/ariedotme/vasta/blob/main/docs/deploy.md>
- Import from Capacities: <https://github.com/ariedotme/vasta/blob/main/docs/import.md>
- Sync design: <https://github.com/ariedotme/vasta/blob/main/docs/sync.md>
- Accounts / spaces / security: <https://github.com/ariedotme/vasta/blob/main/docs/accounts.md>
- Capture channels: <https://github.com/ariedotme/vasta/blob/main/docs/capture.md>
- Agents / MCP / CLI: <https://github.com/ariedotme/vasta/blob/main/docs/agents.md>
- Reminders: <https://github.com/ariedotme/vasta/blob/main/docs/reminders.md>
- Desktop app: <https://github.com/ariedotme/vasta/blob/main/docs/desktop.md>
- Data model: <https://github.com/ariedotme/vasta/blob/main/docs/data-model.md>
- Capacities research notes: <https://github.com/ariedotme/vasta/blob/main/docs/capacities-research.md>
- Compose example: <https://github.com/ariedotme/vasta/blob/main/compose.example.yml>
- Env example: <https://github.com/ariedotme/vasta/blob/main/.env.example>

### Key issues

- #1 No export: <https://github.com/ariedotme/vasta/issues/1>
- #2 Media authz: <https://github.com/ariedotme/vasta/issues/2>
- #3 Calendar/type pages read every row: <https://github.com/ariedotme/vasta/issues/3>
- #4 Replica fill 42 s: <https://github.com/ariedotme/vasta/issues/4>
- #5 Markdown/Obsidian/Notion import: <https://github.com/ariedotme/vasta/issues/5>
- #8 Capacities saved views: <https://github.com/ariedotme/vasta/issues/8>
- #9 Tabs/split view: <https://github.com/ariedotme/vasta/issues/9>
- #13 Reverse relation naming: <https://github.com/ariedotme/vasta/issues/13>
- #14 CalDAV/Google/iCloud: <https://github.com/ariedotme/vasta/issues/14>
- #16 Plaintext calendar passwords: <https://github.com/ariedotme/vasta/issues/16>
- #22 Live collaboration: <https://github.com/ariedotme/vasta/issues/22>
- #23 Public read/write API: <https://github.com/ariedotme/vasta/issues/23>
- #25 553 KB editor chunk: <https://github.com/ariedotme/vasta/issues/25>
- #26 Android share target: <https://github.com/ariedotme/vasta/issues/26>
- #29 Windows desktop untested: <https://github.com/ariedotme/vasta/issues/29>
- #30 Linux desktop untested: <https://github.com/ariedotme/vasta/issues/30>
- #32 WebKit SQLITE_CORRUPT: <https://github.com/ariedotme/vasta/issues/32>
- #34 Translate the UI: <https://github.com/ariedotme/vasta/issues/34>
- #35 macOS local mode crash: <https://github.com/ariedotme/vasta/issues/35>
- #36 User guide: <https://github.com/ariedotme/vasta/issues/36>
- #37 In-app Capacities importer: <https://github.com/ariedotme/vasta/issues/37>
- #38 Whiteboards: <https://github.com/ariedotme/vasta/issues/38>
- #39 Rating property: <https://github.com/ariedotme/vasta/issues/39>
- #40 Select customization: <https://github.com/ariedotme/vasta/issues/40>
- #41 Calendar layout: <https://github.com/ariedotme/vasta/issues/41>
- #42 Telegram template questions: <https://github.com/ariedotme/vasta/issues/42>
- #43 Cannot create property over LAN HTTP: <https://github.com/ariedotme/vasta/issues/43>

Other issues referenced: [#6](https://github.com/ariedotme/vasta/issues/6) · [#7](https://github.com/ariedotme/vasta/issues/7) · [#10](https://github.com/ariedotme/vasta/issues/10) · [#11](https://github.com/ariedotme/vasta/issues/11) · [#12](https://github.com/ariedotme/vasta/issues/12) · [#15](https://github.com/ariedotme/vasta/issues/15) · [#17](https://github.com/ariedotme/vasta/issues/17) · [#18](https://github.com/ariedotme/vasta/issues/18) · [#19](https://github.com/ariedotme/vasta/issues/19) · [#20](https://github.com/ariedotme/vasta/issues/20) · [#21](https://github.com/ariedotme/vasta/issues/21) · [#24](https://github.com/ariedotme/vasta/issues/24) · [#27](https://github.com/ariedotme/vasta/issues/27) · [#28](https://github.com/ariedotme/vasta/issues/28) · [#31](https://github.com/ariedotme/vasta/issues/31) · [#33](https://github.com/ariedotme/vasta/issues/33)

### Launch and context

- Reddit launch thread: <https://www.reddit.com/r/NoteTaking/comments/1xc0h5o/made_my_own_selfhosted_notes_app_because/>
- Reddit RSS (used to read it anonymously): <https://www.reddit.com/r/NoteTaking/comments/1xc0h5o/.rss>
- HN Algolia search for "vasta" (no PKM result): <https://hn.algolia.com/api/v1/search?query=vasta&tags=story>
- HN Vastaamo story (brand collision): <https://news.ycombinator.com/item?id=46656045>
- awesome-selfhosted list (Vasta not present): <https://github.com/awesome-selfhosted/awesome-selfhosted>
- Lumen own priorities (Capacities importer #13, performance #14, whiteboard #15): `lumen-pkm/docs/tracking/NEXT-PHASE-PRIORITIES.md`
- Lumen README (no public image, no validated public deploy, no multiuser): `lumen-pkm/README.md`

### Sources that were inaccessible

- Reddit JSON API: 403/login wall from this network; RSS worked intermittently and was used for the launch thread.
- Product Hunt: Cloudflare challenge.
- Lobsters: Anubis bot challenge.
- GitHub stargazers timeline: requires authentication.
- Docker image size: not pulled; no published size.

---

## Appendix: raw metrics and method

### Repository snapshot (2026-10-31)

| Metric | Value |
|---|---|
| Stars | 41 |
| Forks | 4 |
| Watchers/subscribers | 2 |
| Open issues | 39 |
| Closed issues | 4 |
| Contributors | 1 (`ariedotme`, 35 contributions) |
| Pull requests | 0 |
| Commits | 35 |
| Created | 2026-10-22T18:44:07Z |
| First commit | 2026-10-22T19:21:43Z |
| Last commit | 2026-10-27T20:50:00Z |
| Last push | 2026-10-31T18:30:39Z |
| Default branch | `main` |
| Topics | capacities, crdt, daily-notes, knowledge-management, local-first, mcp, note-taking, notes, notion-alternative, obsidian-alternative, offline-first, personal-knowledge-management, pkm, second-brain, self-hosted, sqlite, tauri, typescript, vue, yjs |

### Issue label counts (overlapping)

| Label | Count |
|---|---|
| enhancement | 29 |
| performance | 3 |
| tests | 3 |
| help wanted | 3 |
| security | 2 |
| bug | 2 |
| good first issue | 2 |
| maintenance | 1 |

### Issue authorship

| Author | Issues |
|---|---|
| `ariedotme` (maintainer) | 41 |
| `yazdipour` | 1 (#43) |
| `gentlice` | 1 (#35) |

### Source spot-checks (v0.4.1 tarball)

I downloaded the `v0.4.1` source tarball (12 MB) to `/tmp` and grepped it. These checks confirm three claims that the docs alone only implied:

| Claim | Check | Result |
|---|---|---|
| No application-level encryption at rest | `grep -RIn -E "encrypt\|createCipher\|aes-256\|E2EE\|end-to-end"` | No cipher/encryption code. The only hit is an iframe `allow="... encrypted-media ..."` in `EmbedFrame.vue`. The SQLite store and media are not encrypted by the app. |
| #43 root cause is widespread | `grep -RIn "randomUUID" packages` | 59 call sites, including property creation (`packages/web/src/lib/properties.ts`) and view/type actions. No secure-context fallback. |
| #26 share target missing | `grep -RIn "share_target"` | No `share_target` in the PWA manifest. |
| #16 plaintext calendar passwords | `grep -RIn "password" packages/core/src/store.ts` | `calendar_sources.password` is written and read as a plain string. |

This does not replace a hands-on install, but it makes the security and self-hosting claims primary-source, not just README inference.

### Method

1. Located the repo via GitHub repository search (`vasta notes`, `vasta pkm`, `capacities self-hosted`) and confirmed the description/license/topics.
2. Pulled repo metadata, languages, releases, tags, contributors, commits and all issues (open + closed) through the GitHub REST API.
3. Read the README, `docs/deploy.md`, `docs/import.md`, `docs/sync.md`, `docs/accounts.md`, `docs/capture.md`, `docs/agents.md`, `docs/reminders.md`, `docs/desktop.md`, `docs/data-model.md`, `docs/capacities-research.md`, `CHANGELOG.md`, `Dockerfile`, `compose.example.yml`, `.env.example`, `package.json`.
4. Read issue comments on #43, #35 and #38; read all issue bodies.
5. Searched HN Algolia for "vasta" (stories and comments), "capacities self-hosted", and date-sorted "capacities"; no Vasta PKM launch.
6. Searched Reddit via RSS for the launch thread and for "vasta selfhosted"; read the launch thread's 36 RSS entries.
7. Checked awesome-selfhosted's README for Vasta/Capacities alternatives; Vasta is absent.
8. Compared against Lumen 0.6.1 using the user-provided feature list and the local repo README/docs (read-only).

### Caveats

- **No upvote data.** GitHub reactions are 0 and Reddit scores are not exposed to anonymous RSS. The ranking is analyst judgment, not a vote count.
- **One launch thread.** Absence of HN/PH/Lobsters coverage is based on searches and bot-blocked sites, not an exhaustive web crawl. It is possible a launch post exists on a site I could not access.
- **Alpha product.** Vasta changes fast (7 releases in 6 days); issue status and feature claims may be stale within weeks.
- **No hands-on install.** I did not run Vasta. Deployment and performance claims are from its docs/issues, not from a local test.
- **Lumen comparison is shipped-feature based.** I did not audit Lumen's code for every row; I used the user-provided 0.6.1 feature list and the repo README/docs. Where Lumen's behavior is undocumented (e.g., split view, reverse-relation labels), I marked it "not documented" rather than guessing.
