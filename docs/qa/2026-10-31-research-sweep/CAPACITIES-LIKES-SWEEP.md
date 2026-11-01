# Capacities-Likes Sweep — Long Tail, Adjacent Frontiers, and Self-Hosted Adoption

**Research agent D** · 2026-10-31 · Read-only sweep; no repo changes, no commits.
**Scope:** every credible object-based / Capacities-like PKM not already covered by agents A–C, plus the feature frontiers adjacent apps are shipping, the self-hosted adoption checklist, and import-format demand.
**Audience:** Lumen product/engineering (self-hosted Go server + Qt6 desktop + Android + extension; typed objects; no AI; single-user).

---

## 0. TL;DR — what this sweep changes

1. **The category is real but narrow.** After ~100 apps, only a handful combine *typed objects + local/self-hosted storage + native apps*: Capacities, Anytype, Tana, Fibery, SiYuan, TriliumNext, Vasta, Lumen. Most "Capacities alternatives" are either markdown vaults (Obsidian, Logseq, HelixNotes), read-later/bookmark tools (Karakeep, Linkwarden, Readeck), AI wrappers (Mem, Fabric, Recall, Saner), or visual canvases (Heptabase, Scrintal, Kosmik, Milanote).
2. **Importers are the adoption moat, not a nice-to-have.** Obsidian ships **15 importers** (Apple Notes, Notion, Evernote, OneNote, Roam, Logseq, Bear, Google Keep, Airtable, CSV, HTML, Markdown, Textbundle, Tomboy, Apple Journal). Twos advertises **17 import sources**. Capacities only ingests generic Markdown/CSV/DOCX/HTML/LaTeX/TXT and ZIP. Lumen currently imports `.md`/`.txt` as **plain text** (no Markdown→blocks, no property mapping, no link resolution). This is the single highest-ROI gap.
3. **Email-in capture is the biggest unclaimed object-PKM feature.** Capacities has email → daily note, plus WhatsApp/Telegram. Twos accepts email, SMS, WhatsApp, Slack, Instagram, X, Discord, Bluesky. Fabric has Email-to-Fabric. Saner turns Gmail/Outlook into tasks. No self-hosted object PKM does this well. A self-hosted IMAP poller with a per-instance address is a defensible differentiator.
4. **No-AI automation is shipping everywhere except object PKMs.** Karakeep has trigger/condition/action rules. Notion, Fibery and Coda have database automations. Poznote has signed webhooks. Lumen has API + MCP but no rules engine and no webhooks. A small deterministic rules DSL is cheap and unlocks n8n/Zapier-style workflows without violating the no-AI policy.
5. **OCR is table stakes in capture apps and absent from object PKMs.** Evernote, Apple Notes, OneNote, Karakeep and Eclaire all index text inside images/PDFs. Lumen's weblink reader scrapes HTML but does not OCR screenshots, scans or PDFs. Local Tesseract is classical CV, not AI, and would make Lumen's search visibly better.
6. **Public read-only sharing is a quiet differentiator.** Fabric publishes spaces as portals with passwords and analytics; Blinko publishes notes to a blog; UpNote/Amplenote/Notion Sites/Docmost/Outline all ship public links. Lumen's plan lists "public share" as an open decision. A tokenized read-only page (object or collection) is a small, isolated Go feature.
7. **Calendar sync + time-blocking is the task frontier.** Amplenote (two-way Google Calendar, drag-to-schedule, task score), NotePlan (calendar+notes+kanban), Twos (Today rollover, Apple/Google/Outlook sync), Fibery (calendar/timeline views). Lumen has calendar views and tasks but no external calendar sync and no time-blocking surface.
8. **Spaced repetition is a proven retention feature and Capacities explicitly refuses it.** RemNote, SiYuan (built-in FSRS), Recall (quizzes on saved content), Obsidian's Spaced Repetition plugin, Traverse, Mochi, Anki. FSRS is a deterministic algorithm, not AI, so it fits Lumen's policy. Niche but extremely loyal.
9. **The self-hosted checklist is now explicit and public.** One-container Compose + SQLite, visible backup/restore, reverse-proxy/HTTPS docs, ARM64, mobile/PWA/offline, OIDC, S3, 2FA/audit, API/MCP/webhooks. Poznote, Kept, Jotty, Anchor and Docmost are the current benchmark; Lumen is excellent on footprint and health checks, weak on OIDC/S3/webhooks/public deployment docs.
10. **Market churn is a migration opportunity.** Pocket shut down (2025), Omnivore was acquired by ElevenLabs and its hosted service closed (2024), Skiff shut down after the Notion acquisition (2024), Evernote keeps raising prices/limiting free plans, Kortex is pivoting to Eden (2026), Amie pivoted to AI notetaking, xTiles pivoted to an AI daily planner. Every wave needs an importer and a "your data is yours" story.
11. **Do not chase the long tail's breadth.** AI agents/chat (policy), multi-user collaboration (design), whiteboards (high cost, low value for personal PKM), plugin marketplaces (ecosystem cost), team wikis (different buyer). Lumen's niche — typed objects, native speed, offline-first, self-hosted, no AI — is narrow but real. Win on import, capture and trust, not on feature count.
12. **The best positioning sentence from this sweep:** *"The self-hosted object PKM that imports everything, captures from anywhere, and never phones home."*

---

## 1. Method and coverage

### 1.1 What was read

- **Directories:** [awesome-selfhosted — note-taking](https://awesome-selfhosted.net/tags/note-taking--editors.html), [awesome-note-taking (100+ apps)](https://github.com/tehtbl/awesome-note-taking), [selfh.st/apps](https://selfh.st/apps/) (SSO filter: built-in / plugin / enterprise-only), [noteapps.info](https://noteapps.info/) (40 apps × 368 features).
- **GitHub:** [topic:pkm](https://github.com/topics/pkm), [topic:note-taking](https://github.com/topics/note-taking), [search: self-hosted notes created >2025](https://github.com/search?q=self-hosted+notes+created%3A%3E2025-01-01&type=repositories&s=stars&o=desc).
- **Vendor docs read in full or near-full:** Capacities import/export/bulk-import/integrations, Anytype import/export/local API, Notion export, Obsidian Importer README, SiYuan, TriliumNext docs index, SilverBullet, Docmost, Jotty, Poznote, Kept, Anchor, Glass Keep, Karakeep, Linkwarden, Readeck, Zotero, HelixNotes, NoteDiscovery, Blinko, Eclaire, Fibery, Supernotes, xTiles, Amplenote, Twos, UpNote, Milanote, Mem, Fabric, Saner, Kortex, Lazy, Recall, RemNote.
- **Community signal:** Reddit search snippets (direct Reddit fetches were 403; thread titles/snippets are cited), Hacker News, selfh.st, comparison articles, Product Hunt (blocked by 403; launches found via search snippets).
- **Repo state (read-only):** `docs/tracking/PLAN-PARIDAD-CAPACITIES.md`, `NEXT-PHASE-PRIORITIES.md`, `README.md`, `sandbox/compose.test.yaml`, server routes. This is used only to mark Lumen status accurately.

### 1.2 Caveats

- Features/pricing/licenses change monthly; all claims are "as of Oct 2026".
- "Unique thing Lumen lacks" is relative to Lumen's current shipped state, not a judgment that Lumen should build it.
- Reddit content is cited from search snippets because Reddit blocks automated fetches; treat those as directional, not verbatim.
- Some smaller apps are pre-release or single-maintainer; they are included for pattern value, not as stable competitors.

---

## 2. The landscape

### 2.1 Covered baseline (one line each — not re-researched)

| App | Model | License | Link | What it is | Why it still matters to Lumen |
|---|---|---|---|---|---|
| **Capacities** | SaaS | Proprietary | [capacities.io](https://capacities.io/) | Object-based "studio for your mind" | The parity target; shipped whiteboards + global graph Oct 2026; still no self-host/E2EE/collab. |
| **Vasta** | Self-hosted / Tauri alpha | AGPL-3.0 | [github.com/ariedotme/vasta](https://github.com/ariedotme/vasta) | Self-hosted Capacities-like with importer | Closest direct competitor; one container + SQLite; no export/encryption yet. |
| **Obsidian** | Local-first | Proprietary (free personal) | [obsidian.md](https://obsidian.md/) | Markdown vault + plugins | Bases (database views), 15 importers, Publish; the ecosystem benchmark. |
| **Notion** | SaaS | Proprietary | [notion.com](https://www.notion.com/) | Docs + databases + wiki | Automations, Sites, email-to-page, Markdown/CSV/HTML export; the export format everyone must parse. |
| **Anytype** | Local-first / P2P | Source-available | [anytype.io](https://anytype.io/) | Typed objects + E2EE | Local API + MCP server; Markdown/Any-Block/PDF/HTML export; no self-hosted server. |
| **Logseq** | Local-first | AGPL-3.0 | [logseq.com](https://logseq.com/) | Outliner / markdown graph | Block refs, journals; DB version transition; huge refugee pool if it stumbles. |
| **Tana** | SaaS | Proprietary | [tana.inc](https://tana.inc/) | Supertags / node graph | Supertags, voice capture, AI; steep learning curve; cloud-only. |
| **AppFlowy** | Self-hosted / desktop | AGPL-3.0 | [appflowy.io](https://appflowy.io/) | Open-source Notion alternative | Databases, local AI, cloud; the "self-hosted Notion" default. |
| **AFFiNE** | Self-hosted / desktop | MIT | [affine.pro](https://affine.pro/) | Docs + whiteboard + database | Edgeless canvas; strong visual collaboration. |
| **Reflect** | SaaS | Proprietary | [reflect.app](https://reflect.app/) | Networked notes + AI | E2EE, Kindle/Readwise, daily notes; premium-only. |
| **Heptabase** | SaaS | Proprietary | [heptabase.com](https://heptabase.com/) | Visual whiteboard cards | PDF annotation, maps, learning workflow; best-in-class visual sense-making. |
| **MyMind** | SaaS | Proprietary | [mymind.com](https://mymind.com/) | Visual private memory | Auto-tagging, visual search, "no folders" philosophy. |
| **Craft** | SaaS | Proprietary | [craft.do](https://craft.do/) | Native docs | Craft 3.0, AI, publishing; best native document feel. |

### 2.2 Typed-object, card-based, visual and learning PKMs (the direct neighborhood)

| App | Model | License | Link | One-line | Unique thing Lumen lacks |
|---|---|---|---|---|---|
| **Fibery** | SaaS | Proprietary | [fibery.io](https://fibery.io/) | Connected work platform built on typed entities and relations | Per-view automations, reports, map/timeline/gantt views, MCP server. |
| **Supernotes** | SaaS | Proprietary | [supernotes.app](https://supernotes.app/) | Atomic notecards with hierarchy + backlinks | Hard 1,200-char cards (forces atomicity), 2D/3D graph, real-time collab, API. |
| **xTiles** | SaaS | Proprietary | [xtiles.app](https://xtiles.app/) | Visual tile boards, now an AI daily planner | Tile/"compiled data" layout, AI daily brief, MCP connections, ADHD templates. |
| **Clover** | SaaS | Proprietary | [cloverapp.com](https://cloverapp.com/) | Docs + canvas + AI workspace | Combined doc/canvas workspace (site was flaky during sweep; verify). |
| **Amplenote** | SaaS | Proprietary | [amplenote.com](https://www.amplenote.com/) | Notes + tasks + calendar for GTD | Task Score auto-prioritization, drag-to-calendar time-blocking, mood/energy/habit streaks, publish-to-web. |
| **Twos** | SaaS | Proprietary | [twosapp.com](https://www.twosapp.com/) | "Things": notes, to-dos, reminders, events | Capture via email/SMS/WhatsApp/Slack/Instagram/X/Discord/Bluesky + **17 import sources** + Today rollover. |
| **UpNote** | SaaS | Proprietary | [getupnote.com](https://getupnote.com/) | Fast cross-platform notes | **$39.99 lifetime**, import from Evernote/Apple Notes/Bear/Simplenote, secret share links, version history. |
| **Milanote** | SaaS | Proprietary | [milanote.com](https://milanote.com/) | Visual boards for creative projects | Moodboard/board workflow, web clipper, client collaboration. |
| **Notejoy** | SaaS | Proprietary | [notejoy.com](https://notejoy.com/) | Team notebooks with comments | Per-note comments/libraries (team buyer; not Lumen's market). |
| **Taskade** | SaaS | Proprietary | [taskade.com](https://www.taskade.com/) | AI project workspace | AI agents + mind-map/kanban/calendar views + templates. |
| **Mem** | SaaS | Proprietary | [get.mem.ai](https://get.mem.ai/) | AI workspace + agent | Push-to-Remember voice capture, bot-free meeting notes, calendar integration, AI memory. |
| **Fabric** | SaaS | Proprietary | [fabric.so](https://fabric.so/) | AI file/notes/bookmarks workspace | Email-to-Fabric, publish portals with passwords/analytics, OCR/handwriting, 50+ integrations, CLI. |
| **Saner.ai** | SaaS | Proprietary | [saner.ai](https://www.saner.ai/) | ADHD-first AI assistant | Gmail/Slack/Calendar/Drive/Outlook triage into tasks, voice-to-text, timeline view. |
| **Kortex → Eden** | SaaS | Proprietary | [kortex.co](https://www.kortex.co/) / [eden.so](https://eden.so) | Creator AI second brain, pivoting to Eden | 25+ AI writing workflows, highlights library, spatial AI canvas; **Kortex is being migrated to Eden**. |
| **Lazy** | SaaS | Proprietary | [lazy.so](https://lazy.so/) | Universal clipper + AI capture | OS-level capture from any app, voice input, "no context switching". |
| **Recall** | SaaS | Proprietary | [recall.it](https://www.recall.it/) | AI knowledge base for saved content | YouTube/podcast/PDF ingestion, auto-tagging, **quizzes + spaced repetition**, Markdown export. |
| **Napkin** | SaaS | Proprietary | [napkin.one](https://napkin.one/) | Idea-only note app | Explicit **no-import** philosophy (anti-migration), AI connection suggestions. |
| **Scrintal** | SaaS | Proprietary | [scrintal.com](https://scrintal.com/) | Visual research cards | Whiteboard + bidirectional cards + PDF annotations + citations. |
| **Kosmik** | SaaS | Proprietary | [kosmik.app](https://www.kosmik.app/) | Infinite canvas for visual research | Spatial canvas for web clips/PDFs/images, Apple-native apps. |
| **Muse** | SaaS | Proprietary | [museapp.com](https://museapp.com/) | Canvas for iPad/Mac | Pencil/handwriting canvas, spatial thinking. |
| **RemNote** | SaaS | Proprietary | [remnote.com](https://www.remnote.com/) | Notes + flashcards | Built-in spaced repetition, PDF annotation, exam workflow. |
| **Traverse** | SaaS | Proprietary | [traverse.link](https://traverse.link/) | SRS-first notes | Cloze/SRS scheduling integrated with linking. |
| **Mochi** | SaaS | Proprietary | [mochi.cards](https://mochi.cards/) | Markdown flashcards | Markdown-first SRS with a clean review UI. |
| **Anki** | Desktop/mobile | AGPL-3.0 | [apps.ankiweb.net](https://apps.ankiweb.net/) | Gold-standard SRS | FSRS/SM-2 scheduling, add-ons, massive shared decks. |
| **MintDeck** | SaaS | Proprietary | [mintdeck.app](https://www.mintdeck.app/) | Phone-first AI flashcards | AI card generation from notes/PDFs. |
| **Deckbase** | SaaS | Proprietary | [deckbase.co](https://www.deckbase.co/) | AI flashcards | AI card generation + study plans. |
| **Revu** | macOS | GPL-3.0 | [github.com/JuliusBrussee/revu-swift](https://github.com/JuliusBrussee/revu-swift) | Local-first SRS notes | FSRS + Anki import + study guides. |

### 2.3 Local-first / open-source note tools

| App | Model | License | Link | One-line | Unique thing Lumen lacks |
|---|---|---|---|---|---|
| **SiYuan** | Local-first / self-hosted | AGPL-3.0 | [b3log.org/siyuan/en](https://b3log.org/siyuan/en/) | Block-based knowledge workspace | **Built-in FSRS spaced repetition**, relation/rollup databases, E2EE sync, Docker server, PDF annotation. |
| **TriliumNext** | Self-hosted / desktop | AGPL-3.0 | [triliumnext.github.io/Docs](https://triliumnext.github.io/Docs/) | Hierarchical knowledge base | Attributes + scripting, ETAPI REST, relation map, MFA, reverse-proxy docs, Evernote/OneNote import. |
| **SilverBullet** | Self-hosted | MIT | [silverbullet.md](https://silverbullet.md/) | Markdown PWA with scripting | **Space Lua** queries, live team wiki, docs-as-code, single container. |
| **HelixNotes** | Local-first | Open source (GitLab) | [helixnotes.com](https://helixnotes.com/) | Rich editor on plain `.md` files | Rich editing saved as plain Markdown, WebDAV sync, tasks calendar, graph, Obsidian import, mobile apps. |
| **Tangent Notes** | Local-first | Apache-2.0 | [tangentnotes.com](https://www.tangentnotes.com/) | Markdown writing environment | Thread-based writing/focus, local files. |
| **Zettlr** | Desktop | GPL-3.0 | [zettlr.com](https://www.zettlr.com/) | Academic Markdown editor | Zotero/CSL citations, export, research workflow. |
| **VNote** | Desktop | LGPL-3.0 | [github.com/vnotex/vnote](https://github.com/vnotex/vnote) | Markdown notes | Mind-map view + Typst support. |
| **NoteGen** | Local-first | Open source | [github.com/codexu/note-gen](https://github.com/codexu/note-gen) | Capture-first Markdown app | Capture inbox, MCP, WebDAV, AI (optional). |
| **Reor** | Local-first | AGPL-3.0 | [github.com/reorproject/reor](https://github.com/reorproject/reor) | Local AI PKM | Local vector search/LLM over Markdown (AI; policy mismatch). |
| **IWE** | CLI/LSP | Apache-2.0 | [github.com/iwe-org/iwe](https://github.com/iwe-org/iwe) | Markdown knowledge graph for editors | LSP server, CLI, MCP memory for agents. |
| **nb** | CLI/web | AGPL-3.0 | [github.com/xwmx/nb](https://github.com/xwmx/nb) | Plain-text notes/bookmarks | Git versioning, Pandoc conversion, single portable script. |
| **zk** | CLI | GPL-3.0 | [github.com/mickael-menu/zk](https://github.com/mickael-menu/zk) | Zettelkasten CLI | CLI-first note graph. |
| **QOwnNotes** | Desktop | GPL-2.0 | [qownnotes.org](https://www.qownnotes.org/) | Plain-text notes | Nextcloud/ownCloud integration, CalDAV tasks. |
| **Cherrytree** | Desktop | GPL-3.0 | [giuspen.net/cherrytree](https://www.giuspen.net/cherrytree/) | Hierarchical rich-text notes | Tree + rich text + code highlighting. |
| **Zim** | Desktop | GPL-2.0 | [zim-wiki.org](https://zim-wiki.org/) | Desktop wiki | Wiki links, plugins, offline. |
| **Leo** | Desktop | MIT | [leo-editor.github.io](https://leo-editor.github.io/) | Outliner/PIM | Literate outliner, scripts. |
| **Org-mode / org-roam** | Emacs | GPL-3.0 | [orgmode.org](https://orgmode.org/) / [orgroam.com](https://www.orgroam.com/) | Org-mode PKM | Agenda, Babel, roam graph, plain text. |
| **FSNotes** | macOS/iOS | MIT | [fsnot.es](https://fsnot.es/) | Native Apple Markdown notes | Native Apple apps, iCloud, fast. |
| **Dendron** | VS Code | Apache-2.0 | [dendron.so](https://www.dendron.so/) | Hierarchy-first PKM | VS Code-native hierarchy + schemas. |
| **Foam** | VS Code | MIT | [foambubble.github.io](https://foambubble.github.io/) | Markdown PKM in VS Code | Wikilinks/backlinks inside VS Code. |
| **Emanote** | Static site | AGPL-3.0 | [emanote.srid.ca](https://emanote.srid.ca/) | Markdown site/graph | Typed/structured Markdown publishing. |
| **Bangle.io** | Browser | AGPL-3.0 | [bangle.io](https://bangle.io/) | Local Markdown in the browser | No install, local-first browser notes. |
| **Databyss** | Local-first/web | AGPL-3.0 | [databyss.org](https://databyss.org/) | Open-source notes + citation management | Bibliographic sources, citations and publishing in one editor; small/quiet project (~46 GitHub stars). |

### 2.4 Self-hosted notes, wikis and knowledge bases

| App | Model | License | Link | One-line | Unique thing Lumen lacks |
|---|---|---|---|---|---|
| **Memos** | Self-hosted | MIT | [usememos.com](https://www.usememos.com/) | Timeline micro-notes | Single Go binary + SQLite, timeline-first capture. |
| **Joplin** | Sync/desktop/mobile | AGPL-3.0 | [joplinapp.org](https://joplinapp.org/) | E2EE notes with sync | Plugins, web clipper, Evernote/ENEX import, mature mobile apps. |
| **Notesnook** | Self-hosted sync | GPL-3.0 | [notesnook.com](https://notesnook.com/) | Zero-knowledge notes | Self-hostable E2EE sync server, all platforms. |
| **Standard Notes** | Self-hosted | AGPL-3.0 | [standardnotes.com](https://standardnotes.com/) | Encrypted extensible notes | Self-hosted server + extensions + longevity pledge. |
| **TiddlyWiki** | Single-file | BSD-3-Clause | [tiddlywiki.com](https://tiddlywiki.com/) | Self-contained wiki | One HTML file, macros, plugins, offline. |
| **TidGi** | Desktop | MPL-2.0 | [github.com/tiddly-gittly/TidGi-Desktop](https://github.com/tiddly-gittly/TidGi-Desktop) | TiddlyWiki desktop | Git auto-backup, blog publishing, Anki connect, REST API. |
| **Flatnotes** | Self-hosted | MIT | [github.com/dullage/flatnotes](https://github.com/dullage/flatnotes) | Flat Markdown notes | Database-less flat folder, dead simple. |
| **NoteDiscovery** | Self-hosted | MIT | [notediscovery.com](https://www.notediscovery.com/) | Markdown knowledge base | MCP, public sharing, drawing editor, LaTeX/Mermaid, graph, PWA. |
| **Jotty** | Self-hosted | AGPL-3.0 | [jotty.page](https://jotty.page/) | Checklists + notes + kanban | OIDC **and LDAP**, MFA, audit log, REST + MCP, encryption, kanban + calendar. |
| **Poznote** | Self-hosted | MIT | [poznote.com](https://poznote.com/) | Notes/wiki for families/teams | OIDC, **S3**, Git sync, **signed webhooks**, Whisper transcription, multi-user, ARM64, Helm. |
| **Anchor** | Self-hosted | AGPL-3.0 | [github.com/ZhFahim/anchor](https://github.com/ZhFahim/anchor) | Offline-first notes | OIDC, multi-user, Android app, backup ZIP, Postgres. |
| **Glass Keep** | Self-hosted | MIT | [github.com/nikunjsingh93/react-glass-keep](https://github.com/nikunjsingh93/react-glass-keep) | Google Keep alternative | Google Takeout import, public links with expiry, real-time collab, local AI, ARM64. |
| **Blinko** | Self-hosted | AGPL-3.0 | [blinko.space](https://blinko.space/) | Quick notes + AI + blog | Note → personal blog publishing, plugin marketplace. |
| **Docmost** | Self-hosted | AGPL-3.0 | [docmost.com](https://docmost.com/) | Enterprise wiki | SAML/SSO, RBAC, page verification, public sharing, Confluence/Notion import, kanban/bases. |
| **Outline** | Self-hosted | BSL-1.1 | [getoutline.com](https://www.getoutline.com/) | Team wiki | SSO, polished editor, public sharing, API. |
| **HedgeDoc** | Self-hosted | AGPL-3.0 | [hedgedoc.org](https://hedgedoc.org/) | Collaborative Markdown | Real-time multi-cursor editing, slide mode. |
| **Karakeep** | Self-hosted | AGPL-3.0 | [karakeep.app](https://karakeep.app/) | Bookmark everything | **OCR**, **automation rules**, RSS, full-page archive, semantic search, MCP/CLI. |
| **Linkwarden** | Self-hosted | AGPL-3.0 | [linkwarden.app](https://linkwarden.app/) | Bookmarks with preservation | Multi-format page preservation (HTML/PDF/screenshot), annotations, RSS, public collections. |
| **Readeck** | Self-hosted | AGPL-3.0 | [readeck.org](https://readeck.org/en/) | Read-later | Video transcripts, ebook export, highlights. |
| **Wallabag** | Self-hosted | MIT | [wallabag.org](https://wallabag.org/) | Read-later | Mature read-later, mobile apps, API. |
| **Zotero** | Desktop/web | AGPL-3.0 | [zotero.org](https://www.zotero.org/) | Reference manager | 9,000+ citation styles, PDF annotation, groups, Word/LibreOffice/Google Docs plugins. |

### 2.5 Capture, read-later and research

| App | Model | License | Link | One-line | Unique thing Lumen lacks |
|---|---|---|---|---|---|
| **Readwise Reader** | SaaS | Proprietary | [readwise.io/read](https://readwise.io/read) | Read-later + highlights | Highlight sync to 20+ apps, Kindle/Readwise ecosystem. |
| **Matter** | SaaS | Proprietary | [hq.getmatter.com](https://hq.getmatter.com/) | Read-later | Clean reading experience, highlights, newsletter ingestion. |
| **Cubox** | SaaS | Proprietary | [cubox.pro](https://cubox.pro/) | Read-later + AI | AI summaries, Chinese-market polish. |
| **Raindrop.io** | SaaS | Proprietary | [raindrop.io](https://raindrop.io/) | Bookmark manager | Polished collections, full-text search, integrations. |
| **Glasp** | SaaS | Proprietary | [glasp.co](https://glasp.co/) | Social highlights | Social layer around highlights. |
| **Hypothesis** | Open | BSD-2-Clause | [hypothes.is](https://hypothes.is/) | Web annotation | Open annotation layer, groups, API. |
| **Memex** | SaaS | Proprietary | [memex.garden](https://memex.garden/) | Annotate pages/PDFs/YouTube | In-context annotation + MCP server. |
| **Save** | SaaS | Proprietary | [savemarkdown.co](https://www.savemarkdown.co/) | Webpage → Markdown | Saves clean Markdown to a local vault + MCP. |
| **Eclaire** | Self-hosted | MIT | [github.com/eclaire-labs/eclaire](https://github.com/eclaire-labs/eclaire) | Local AI data assistant | OCR, documents/photos/bookmarks, Telegram, scheduled automations, PWA. |

### 2.6 Journaling, voice, geo and habits

| App | Model | License | Link | One-line | Unique thing Lumen lacks |
|---|---|---|---|---|---|
| **Day One** | SaaS | Proprietary | [dayoneapp.com](https://dayoneapp.com/) | Journal | Geo/map entries, photos, E2EE, print books, "On This Day". |
| **Journey** | SaaS | Proprietary | [journey.cloud](https://journey.cloud/) | Journal | Geo + mood + cross-platform. |
| **Polarsteps** | SaaS | Proprietary | [polarsteps.com](https://www.polarsteps.com/) | Travel journal | Automatic route tracking + travel map + photo book. |
| **AudioPen** | SaaS | Proprietary | [audiopen.ai](https://audiopen.ai/) | Voice → polished text | Voice-to-prose rewriting. |
| **Voicenotes** | SaaS | Proprietary | [voicenotes.com](https://voicenotes.com/) | Voice-first notes | Voice capture + AI summaries. |
| **Otter** | SaaS | Proprietary | [otter.ai](https://otter.ai/) | Meeting transcription | Speaker diarization, meeting notes. |
| **JournalCalls** | SaaS | Proprietary | [journalcalls.com](https://journalcalls.com/) | Voice journal over a phone call | Phone-call capture, Markdown/Notion export. |
| **DailyVox** | iOS | MIT | [github.com/intrepidkarthi/dailyvox](https://github.com/intrepidkarthi/dailyvox) | Offline voice diary | On-device transcription, mood tracking, knowledge graph. |
| **Whisper Notes** | iOS/macOS | Proprietary | [whispernotes.app](https://whispernotes.app/) | Offline transcription | On-device Whisper, no cloud. |
| **Habitica** | SaaS | Proprietary | [habitica.com](https://habitica.com/) | Gamified habits | RPG mechanics, parties, quests. |
| **Loop Habit** | Android | GPL-3.0 | [github.com/iSoron/uhabits](https://github.com/iSoron/uhabits) | Habit tracker | Offline streaks, widgets, no account. |
| **Exist** | SaaS | Proprietary | [exist.io](https://exist.io/) | Quantified self | Cross-service health correlation. |

### 2.7 Publishing and automation (adjacent infrastructure)

| App | Model | License | Link | One-line | Unique thing Lumen lacks |
|---|---|---|---|---|---|
| **Obsidian Publish** | SaaS | Proprietary | [obsidian.md/publish](https://obsidian.md/publish) | Publish a vault | One-click site, graph, custom domain. |
| **Quartz** | Static site | MIT | [quartz.jzhao.xyz](https://quartz.jzhao.xyz/) | Digital garden | Free, self-hosted publish from Markdown. |
| **Flowershow** | SaaS/OSS | MIT | [flowershow.app](https://flowershow.app/) | Markdown publishing | Publish from Obsidian, comments, analytics. |
| **Ghost** | Self-hosted | MIT | [ghost.org](https://ghost.org/) | Blog/newsletter | Newsletter + memberships + publishing. |
| **btw** | Self-hosted | GPL-3.0 | [github.com/btw-so/btw](https://github.com/btw-so/btw) | Personal website builder | Website from notes, self-hosted. |
| **n8n** | Self-hosted | Fair-code | [n8n.io](https://n8n.io/) | Workflow automation | Visual Zapier-like workflows, 400+ integrations. |
| **Activepieces** | Self-hosted | MIT | [activepieces.com](https://www.activepieces.com/) | Open-source Zapier | No-code automation, self-hosted. |
| **Huginn** | Self-hosted | MIT | [github.com/huginn/huginn](https://github.com/huginn/huginn) | Personal agents | Watch agents, event streams, self-hosted IFTTT. |
| **Node-RED** | Self-hosted | Apache-2.0 | [nodered.org](https://nodered.org/) | Flow automation | IoT/flow programming, MQTT/HTTP. |

### 2.8 Watchlist — dead, pivoted, or anti-migration

| App | Status | Why it matters |
|---|---|---|
| **Omnivore** | Acquired by ElevenLabs (2024); hosted service shut down; open-source repo remains | Read-later/highlight refugees need a JSON importer. [GitHub](https://github.com/omnivore-app/omnivore) |
| **Pocket** | Mozilla shut down Pocket (2025) | Huge read-later refugee wave; HTML/CSV export. [getpocket.com](https://getpocket.com/) |
| **Skiff** | Acquired by Notion (2024); shut down | Privacy-notes refugees; export/import opportunity. [skiff.com](https://skiff.com/) |
| **Kortex** | Pivoting to Eden (2026) | Creator notes refugees; Markdown export. [kortex.co](https://www.kortex.co/) |
| **Amie** | Pivoted from calendar to AI notetaker (2025–2026) | Calendar/notes users looking for a stable home. [amie.so](https://amie.so/) |
| **xTiles** | Pivoted to AI daily planner/ADHD (2026) | Visual planners may churn; export quality unclear. [xtiles.app](https://xtiles.app/) |
| **Napkin** | Explicitly no imports | Not a migration target; proves the "no imports" anti-pattern. [napkin.one](https://napkin.one/) |
| **Evernote** | Bending Spoons price hikes + free-plan limits (2023–2026) | The largest recurring refugee wave; ENEX importer is mandatory. [evernote.com](https://evernote.com/) |

### 2.9 2025–2026 new / small projects (pattern value, not yet stable)

| App | Model | License | Link | One-line | Unique thing Lumen lacks |
|---|---|---|---|---|---|
| **Talorys** | Self-hosted / Cloudflare | Open source | [github.com/rociiu/talorys](https://github.com/rociiu/talorys) | Personal AI agent with notes, tasks, reminders | One-command deploy into your own Cloudflare account. |
| **Nowen Note** | Self-hosted | Open source | [github.com/cropflre/nowen-note](https://github.com/cropflre/nowen-note) | Notes + AI + mind map + tasks | Mind map + task management in one workspace. |
| **FrankMD** | Self-hosted | Open source | [github.com/akitaonrails/FrankMD](https://github.com/akitaonrails/FrankMD) | Simple Markdown web editor | Minimal single-binary editor, no database. |
| **QBin** | Self-hosted | Open source | [github.com/Quick-Bin/qbin](https://github.com/Quick-Bin/qbin) | Paste/edit/share platform | Monaco + Cherry Markdown, PWA, short links/passwords. |
| **SkySend** | Self-hosted | Open source | [github.com/Skyfay/SkySend](https://github.com/Skyfay/SkySend) | E2EE file/note sharing | Zero-knowledge server, encrypted in the browser. |
| **Open Notebook** | Self-hosted | Open source | [github.com/lfnovo/open-notebook](https://github.com/lfnovo/open-notebook) | Open-source NotebookLM | Source-grounded research notebooks with citations. |
| **Khoj** | Self-hosted | AGPL-3.0 | [khoj.dev](https://khoj.dev/) | AI second brain | Chat/search over notes and documents (AI; policy mismatch). |
| **Storyflow** | SaaS | Proprietary | [storyflow.so](https://storyflow.so/) | AI project canvas | Canvas + AI for project/story planning. |
| **Brainstream** | SaaS | Proprietary | [Product Hunt](https://www.producthunt.com/products/brainstream) | Agentic AI notes | AI briefs/tasks from scattered notes. |
| **CogniMemo** | SaaS | Proprietary | [Product Hunt](https://www.producthunt.com/products/cognimemo) | AI memory | "Remembers/learns" across content. |

**Pattern:** the 2025–2026 wave is overwhelmingly AI-first and cloud-only. Very few new projects choose local-first + self-hosted + typed objects; that is both a warning (the category is crowded with AI wrappers) and an opening (the privacy/self-host segment is underserved).

### 2.10 Names specifically checked (and what they turned out to be)

| Name | What it actually is | Verdict |
|---|---|---|
| **Amie** | Calendar/tasks app that pivoted to an AI notetaker (2025–2026) | Watchlist; calendar/notes refugees. [amie.so](https://amie.so/) |
| **Bullets** | No live product at `bullets.new`, `bulletsapp.com`, or `getbullets.com` (all unreachable during sweep) | Unverified; not included. |
| **Bangle.io** | Browser-local Markdown notes | Included in §2.3. [bangle.io](https://bangle.io/) |
| **Napkin** | Idea-only notes with an explicit no-import stance | Included/watchlist. [napkin.one](https://napkin.one/) |
| **Rebook** | `rebook.app` is a domain-for-sale page | Not a product; excluded. |
| **Databyss** | Open-source notes + citation management (AGPL-3.0, ~46 stars) | Included in §2.3. [databyss.org](https://databyss.org/) |
| **Saner.ai** | ADHD-first AI assistant for notes/email/tasks | Included in §2.2. [saner.ai](https://www.saner.ai/) |
| **Catapult** | `catapult.app` is a domain-for-sale page | Not a product; excluded. |
| **Katta** | `katta.io` is an S3 encryption/mounting product (Mountain Duck + Cryptomator) | Not a PKM; excluded. [katta.io](https://katta.io/) |
| **Nopy** | `nopy.ai` is an internal PPC platform; `nopy.app` is an ERP product-registration app | Not a PKM; excluded. |
| **Roam Research** | Outliner pioneer; declining but still used; important as an import format | Appendix / import source. [roamresearch.com](https://roamresearch.com/) |

---

## 3. Feature frontiers ranked by actionability for Lumen

Ranking = (user value × differentiation × fit with Lumen's policy/architecture) ÷ cost. "Now" means it should be in the next 1–2 stages; "Next" the following quarter; "Later" after trust/capture; "No" means do not build.

| # | Frontier | Who ships it | Why it matters | Lumen fit | Verdict |
|---|---|---|---|---|---|
| 1 | **Native importers** (Obsidian, Notion, Evernote, Apple Notes, Capacities, Keep, OneNote, Bear, Roam, Logseq) | Obsidian (15), Twos (17), UpNote (4), Capacities (generic MD/CSV), Poznote (Obsidian) | Switching cost is the #1 adoption blocker; every refugee wave needs a door. | Server-side parser + mapping UI; no AI; fits Go. | **Now** |
| 2 | **Email-in capture** (per-instance address → daily note/inbox) | Capacities (email/WhatsApp/Telegram), Twos (email/SMS/WhatsApp/Slack/social), Fabric (Email-to-Fabric), Saner (Gmail/Outlook) | Capture without opening the app; object PKMs are weak here; self-hosted IMAP is a differentiator. | IMAP poller + allowlist + size limits; no third-party cloud. | **Now/Next** |
| 3 | **Automation rules + webhooks** (trigger/condition/action; no AI) | Karakeep (rules), Notion/Fibery/Coda (automations), Poznote (signed webhooks) | Turns a passive PKM into a workflow tool; unlocks n8n/Zapier without AI. | Small JSON DSL + event bus; deterministic. | **Next** |
| 4 | **OCR** for images/PDFs/screenshots | Evernote, Apple Notes, OneNote, Karakeep, Eclaire | Search inside scans/screenshots is visibly magical; object PKMs ignore it. | Tesseract (classical CV), index into FTS; optional language packs. | **Next** |
| 5 | **Public read-only share/publish** (object or collection, token, expiry, optional password) | Fabric (portals + analytics), Blinko (blog), UpNote (secret link), Amplenote (publish), Notion Sites, Docmost/Outline | Writers/researchers want to share without a web editor; small isolated Go feature. | Tokenized read-only HTML page; no editor, optional password. | **Next** |
| 6 | **Calendar sync + time-blocking** (ICS/CalDAV first; Google/Microsoft later) | Amplenote (2-way Google, drag-to-schedule), NotePlan, Twos (Apple/Google/Outlook), Fibery (calendar/timeline) | Tasks without a calendar are a list; time-blocking is the daily planning loop. | ICS read-only now; CalDAV two-way later; no AI. | **Next** |
| 7 | **Web clipper depth** (highlight/annotate, full-page archive, YouTube timestamps, PDF clip) | Obsidian Web Clipper, Karakeep, Linkwarden, Readeck, Memex, Fabric | Capture quality decides whether the PKM is trusted; Lumen's clipper is shallow. | Extension + reader + highlight blocks; server-side readability. | **Next** |
| 8 | **RSS / read-later ingestion** (feed → weblink objects with reader content) | Karakeep (RSS), Linkwarden (RSS), Readeck, Readwise | Turns consumption into objects automatically; low cost, high habit value. | Go feed poller + existing weblink reader. | **Later** |
| 9 | **Spaced repetition** (FSRS; cards from blocks) | RemNote, SiYuan (FSRS), Recall (quizzes), Obsidian plugin, Traverse, Mochi, Anki | Proven retention for students/lifelong learners; Capacities says no. | FSRS is deterministic, not AI; optional module. | **Later** |
| 10 | **Voice capture** (audio attachment + manual notes; optional external transcription) | Mem (Push-to-Remember), Saner (voice-to-text), Poznote (Whisper), Eclaire (watch dictation), AudioPen/Voicenotes | Capture while walking/driving; mobile is Lumen's strength. | Audio recorder (Qt Multimedia/Android) + attachment; **transcription is AI → out of policy** unless user-supplied external adapter. | **Later** |
| 11 | **Geolocation / maps** (lat/lon properties + map view) | Day One, Journey, Polarsteps, Kept (location reminders) | Journaling/travel/life-logging; cheap to add via properties. | `place` type + map view; offline tiles optional. | **Later** |
| 12 | **Habits / health** (habit object + check-ins + streaks + mood) | Amplenote (mood/energy/streaks), Habitica, Loop Habit, Exist | Daily-use retention; object model already supports it. | Template + query/streak calculation; no new engine. | **Later** |
| 13 | **Kanban + calendar fusion** (unified daily planning surface) | NotePlan, Amplenote, Twos, Jotty, Fibery | Lumen has both views but not one planning loop. | A "Plan" view combining status columns + calendar lane. | **Later** |
| 14 | **S3-compatible attachment storage** | Poznote, Outline, Docmost, Immich-style apps | Media-heavy users want object storage; NAS/homelab norm. | Optional asset backend; keep local default. | **Later** |
| 15 | **Multi-user / SSO** | Anytype (spaces), Docmost/Outline (SAML), Anchor/Kept/Jotty/Poznote (OIDC), AppFlowy/AFFiNE | A top r/selfhosted demand; conflicts with Lumen's single-user design. | **No** for full multi-user; consider OIDC for the single account + app passwords for API. | **No (strategic)** |

### 3.1 Frontier notes (what to copy, what to avoid)

**Importers — copy Capacities' review step, not just its parsers.** Capacities' bulk import does: choose target type → upload folder/ZIP → configure fallback type, marker (block/tag), folder handling (ignore/block/tag), type extraction from folder names → review mapping → import. It supports Markdown, CSV, ZIP, images, audio, PDF; maps frontmatter/CSV columns to properties; resolves `[[wikilinks]]`, relative links and absolute import-root links; preserves dates; creates image objects from embeds. Limits: 20k objects cumulative, 10 GB/import, 100 files, 100 MB/file. Lumen should match the *mapping review* and *dry-run report*, and beat Capacities on native app importers. [Capacities bulk import docs](https://docs.capacities.io/reference/bulk-import)

**Email-in — the security model matters.** A self-hosted IMAP poller is safer than running an SMTP server: connect to an existing mailbox (user's own), poll with UIDVALIDITY, allowlist senders, strip HTML, cap attachments, quarantine unknown senders, and write to an "Inbox" object/collection. A per-instance address like `inbox+<token>@user-domain` is easy to explain. Capacities' email integration forwards to the daily note; Lumen can do the same plus attachments. [Capacities email integration](https://docs.capacities.io/reference/integrations/email)

**Automation rules — keep it deterministic.** A minimal schema:

```json
{
  "trigger": {"type": "object.created", "object_type": "weblink"},
  "conditions": [
    {"field": "url", "op": "contains", "value": "youtube.com"}
  ],
  "actions": [
    {"type": "add_tag", "tag": "video"},
    {"type": "set_property", "property": "status", "value": "to-watch"},
    {"type": "webhook", "url": "https://n8n.example/webhook/lumen"}
  ]
}
```

Triggers: `object.created`, `object.updated`, `property.changed`, `task.completed`, `date.reached`, `import.completed`. Actions: set property, add/remove tag, add to collection, create task, create object, send webhook, run query. No AI, no natural-language rules. Karakeep's rule builder is the UX reference. [Karakeep automation](https://karakeep.app/#automation)

**OCR — index, don't just store.** Run Tesseract on image/PDF assets at upload or on demand; store `ocr_text` as a hidden property; include it in FTS; show a "text found in image" badge. Evernote/Apple Notes prove the expectation; Karakeep proves it can be self-hosted. [Karakeep OCR](https://karakeep.app/#search)

**Public sharing — read-only, tokenized, revocable.** `POST /v1/shares` → `{token, object_id|collection_id, expires_at, password_hash?}`; `GET /s/<token>` renders a clean HTML page with images and links; no editing, no JS app. Fabric's password/analytics and Glass Keep's expiry choices (1/7/30/90 days, 1 year, never) are good defaults. [Fabric publish](https://fabric.so/features/publish), [Glass Keep public links](https://github.com/nikunjsingh93/react-glass-keep)

**Calendar sync — ICS first.** Generate a read-only ICS feed per user/space (`/calendar.ics`) for Google/Apple/Outlook subscription; later add CalDAV for two-way. Amplenote's drag-to-calendar and task score are the UX north star, but task score is optional. [Amplenote calendar](https://www.amplenote.com/help/planning_day_week_with_suggested_agenda_calendar_tasks)

**Web clipper depth — highlights are the feature.** Lumen already captures URL + OG metadata + reader text. The missing layer is: selection highlight → highlight block anchored by text/position; full-page archive (HTML + readability text); YouTube transcript/timestamps; PDF clip. Obsidian Web Clipper templates and Karakeep/Linkwarden preservation are the references. [Obsidian Web Clipper](https://obsidian.md/clipper)

**RSS — feeds as object sources.** Poll feeds, create `weblink` objects with `reader_content`, apply per-feed rules (auto-tag, auto-collection), dedupe by GUID/link. Karakeep and Linkwarden ship it; it is a natural extension of Lumen's reader. [Karakeep RSS](https://karakeep.app/#rss-feeds)

**SRS — FSRS, optional, block-based.** Cards generated from blocks: basic (front/back), cloze (highlight), and "reverse". Review UI with Again/Hard/Good/Easy; FSRS scheduler; stats. SiYuan proves it fits a block editor; RemNote proves the study workflow. Keep it off by default so it doesn't complicate the core. [SiYuan FSRS](https://b3log.org/siyuan/en/), [RemNote SRS](https://help.remnote.com/en/articles/6022755-getting-started-with-spaced-repetition)

**Voice — record now, transcribe later (outside the app).** Add an audio recorder and waveform to desktop/mobile; store as `audio` asset with a manual `transcript` property. Do **not** bundle Whisper/LLM transcription (policy). Offer a documented "external transcription adapter" (user points to their own Whisper server) as an integration, not a built-in AI feature. Poznote's transcription is exactly this pattern (OpenAI-compatible `/v1/audio/transcriptions`). [Poznote transcription](https://github.com/timothepoznanski/poznote#transcription-speech-to-text)

**Geo — properties + map, no new database.** Add `latitude`, `longitude`, `place_name` properties to a `place`/`journal` type; render a map view (Leaflet/MapLibre with offline tiles optional); link entries to places. Day One/Journey/Polarsteps prove the emotional value; Kept proves location reminders are useful on mobile. [Day One map](https://dayoneapp.com/guides/release-notes/ios-release-notes/)

**Habits — a template, not a subsystem.** `habit` object (name, cadence, target), `habit_check` object (date, value, note), streak calculation via query/calculation. Add `mood`/`energy` number properties to daily notes. Amplenote's mood/energy/streak model is the reference. [Amplenote reflection](https://www.amplenote.com/help/mood_energy_level_rating_overview_benefits)

**Kanban + calendar fusion — one "Plan" surface.** Left: status columns (todo/doing/done) filtered to today/this week; right: calendar lane with time blocks; drag between them. NotePlan and Amplenote are the references; Lumen already has the primitives (tasks, dates, kanban, calendar). [NotePlan](https://noteplan.co/), [Amplenote](https://www.amplenote.com/)

**S3 — optional, not default.** Keep SQLite + local assets as the default (15 MB container is a feature). Add an optional `LUMEN_ASSET_BACKEND=s3` with MinIO/R2/B2 support for media-heavy installs. Poznote's S3 docs are a good template. [Poznote S3](https://github.com/timothepoznanski/poznote#s3-attachment-storage)

**Multi-user/SSO — say no, but explain why.** Lumen's encryption/index model assumes one space; multi-user means per-user keys, sharing ACLs, audit, admin UI — a different product. The self-hosted crowd will ask; answer with a clear "single-user by design, here's how to run one instance per person" and offer OIDC for the single account if requested. [r/selfhosted SSO list](https://www.reddit.com/r/selfhosted/comments/1w47nrq/here_is_a_list_of_self_hosted_apps_support_sso/)

### 3.2 The white space: what object PKMs still don't ship

| Capability | Object PKMs that ship it | Adjacent apps that prove demand | Lumen opportunity |
|---|---|---|---|
| **Email-in capture** | Capacities (email/WhatsApp/Telegram); Notion (email to page, limited) | Twos, Fabric, Saner, Evernote | High: self-hosted IMAP, no cloud dependency. |
| **Voice capture** | Tana (voice), Mem (Push-to-Remember); Capacities (audio objects) | AudioPen, Voicenotes, Otter, JournalCalls, DailyVox | Medium: record + attach; transcription out of policy. |
| **Automation rules** | Notion, Fibery, Coda (database automations); Tana (AI-ish) | Karakeep (rules), n8n, Activepieces, Huginn | High: deterministic rules + webhooks. |
| **OCR** | None native in Capacities/Tana/Anytype/Notion | Evernote, Apple Notes, OneNote, Karakeep, Eclaire | High: Tesseract + FTS. |
| **Public publishing** | Notion Sites; Capacities/Anytype/Tana no | Fabric, Blinko, UpNote, Amplenote, Obsidian Publish, Quartz | Medium-high: tokenized read-only pages. |
| **Spaced repetition** | None (Capacities says "not next"; Tana/Anytype/Notion no) | RemNote, SiYuan (FSRS), Recall, Obsidian plugin, Anki | Medium: optional FSRS module. |
| **Geo/maps** | Fibery (map view); Capacities/Anytype/Tana no | Day One, Journey, Polarsteps, Kept | Medium-low: properties + map view. |
| **Habits/health** | None native; Amplenote has mood/streaks | Habitica, Loop Habit, Exist, Amplenote | Medium-low: template + streaks. |
| **Kanban + calendar fusion** | Notion/Fibery (separate views); Capacities (queries + calendar) | NotePlan, Amplenote, Twos, Jotty | Medium: Lumen has both primitives. |
| **Read-later / RSS** | None native; Notion via integrations | Karakeep, Linkwarden, Readeck, Readwise | Medium: feed → weblink objects. |
| **Web clipper depth** | Capacities (reader/highlights), Notion Web Clipper, Tana | Obsidian Web Clipper, Karakeep, Linkwarden, Memex | High: highlights + archive + YouTube. |
| **Importers** | Capacities (generic MD/CSV); Anytype (MD); Notion (export) | Obsidian (15), Twos (17), UpNote (4), Poznote (Obsidian) | Highest: switching moat. |

**Reading:** the object-PKM category has converged on *structure* (types, relations, queries, views) and is now competing on *capture* and *workflow*. Lumen already has the structure; the white space is capture (email, voice, OCR, clipper), workflow (rules, calendar, SRS), and portability (importers, share). That is a better roadmap than adding more object-model features.

---

## 4. Self-hosted adoption checklist (ranked by community demand)

Frequency is inferred from r/selfhosted threads, selfh.st filters, awesome-selfhosted listings, and the feature sets of the apps that win there (Poznote, Kept, Jotty, Anchor, Docmost, Immich-style projects). "Lumen status" is based on the repo as of 2026-10-31.

| # | Requirement | Evidence / examples | Lumen status | Action |
|---|---|---|---|---|
| 1 | **One-command Docker Compose + single container + SQLite (no external DB)** | Memos, Jotty, Anchor, Kept, Poznote, SilverBullet all lead with this | ✅ single Go container + SQLite; compose exists | Keep; publish a copy-paste quickstart and a `compose.yaml` that doesn't require editing secrets by hand. |
| 2 | **Backup + restore + open export (Markdown/JSON/CSV), visible in UI** | Data-loss threads are the #1 self-hosted fear; Capacities' automated export is a trust feature; Kept/Poznote ship one-click restore | ✅ `/v1/export`; 🟡 restore docs require SQLite+blobs+keys | Add a "Backup" panel: export now, schedule, verify, restore walkthrough; test restore in CI. |
| 3 | **Reverse-proxy + HTTPS docs (Nginx/Caddy/Traefik), WebSocket notes** | Kept ships Apache/Nginx examples; Trilium ships Apache/Nginx/TLS; every "New Project Megathread" asks for it | 🟡 README says public deployment "not validated"; no proxy guide | Write `docs/guides/REVERSE-PROXY.md` with Caddy/Nginx/Traefik, WebSocket, HSTS, and a "behind Tailscale" section. |
| 4 | **ARM64 / multi-arch images (Raspberry Pi, NAS)** | Poznote, Glass Keep, Immich, Jotty all ship multi-arch; Pi/NAS is the default homelab | 🟡 Go server cross-compiles; test compose pins `linux/amd64`; desktop Qt ARM not | Ship `linux/amd64` + `linux/arm64` server images; document desktop x86_64 only; Android is ARM. |
| 5 | **Mobile access (PWA or native) + offline** | Every r/selfhosted notes thread asks for mobile; Kept/Anchor/Glass Keep ship native/PWA | ✅ Android native + offline; ❌ web/PWA; 🟡 iOS absent | Keep Android as the differentiator; consider a read-only PWA for sharing/quick capture (not a full web editor). |
| 6 | **Multi-user + per-user isolation + sharing** | Families/teams run one instance; Poznote/Anchor/Kept/Docmost/Jotty all ship it | ❌ by design | Don't build full multi-user; document "one instance per person"; design `space_id` only if household demand is proven. |
| 7 | **SSO/OIDC (Authentik, Authelia, Keycloak, Pocket ID); LDAP/SAML for enterprise** | selfh.st has an SSO filter; r/selfhosted maintains an "SSO for free" list and an "SSO tax" list; Jotty ships OIDC+LDAP; Kept/Anchor/Poznote ship OIDC | ❌ | Add OIDC for the single account (cheap, high perceived value); app passwords for API; skip SAML/LDAP. |
| 8 | **S3-compatible attachment storage (MinIO, R2, B2)** | Poznote/Outline/Docmost; media-heavy notes/PDFs; NAS users expect it | ❌ local assets only | Optional backend; keep local default; document migration. |
| 9 | **2FA/MFA + audit log + RBAC** | Exposed instances need it; Jotty/Poznote/Docmost/Trilium ship MFA/audit | ❌ | Add TOTP + audit log (login, export, delete, share); RBAC is N/A for single-user. |
| 10 | **API + MCP + webhooks** | Karakeep/Poznote/Jotty/Kept ship MCP; webhooks are the automation bridge | ✅ API v2 + MCP; ❌ webhooks | Add signed webhooks; document MCP setup; scoped API tokens. |
| 11 | **Low resource footprint + health endpoint** | 15 MB container is a selling point; `/health` with `status/source_of_truth` is better than most | ✅ | Publish RAM/CPU/latency numbers; keep the health contract stable. |
| 12 | **Upgrade path, migrations, release notes** | Self-hosters fear breaking updates; Poznote documents beta/stable tags and backup-before-upgrade | 🟡 rebuild image; migration story unclear | Add schema version + `lumen migrate` + "backup before upgrade" check; publish release notes. |
| 13 | **SMTP for invites/password reset/notifications** | Teams/families expect it; Poznote/Kept/Docmost ship SMTP | ❌ (single-user) | Only needed if multi-user/OIDC; defer. |
| 14 | **Full-text search + OCR + semantic search** | Search is the #1 daily feature; OCR is table stakes in capture apps; semantic is rising but AI | ✅ FTS; ❌ OCR; ❌ semantic (policy) | Add OCR; skip semantic (policy); improve operators/AND default. |
| 15 | **Public sharing links** | Docmost/Outline/Glass Keep/NoteDiscovery/UpNote/Fabric | ❌ (decision pending) | Add read-only tokenized share pages. |
| 16 | **Import from incumbents** | Obsidian 15 importers, Twos 17, Poznote Obsidian import; switching friction | 🟡 plain `.md`/`.txt` only | Build the importer suite (see §5). |
| 17 | **Themes/customization** | Lumen already has themes; low priority for adoption | ✅ | Keep. |
| 18 | **Kubernetes/Helm** | Poznote ships Helm; niche but vocal | ❌ | Defer; provide a plain manifest example if asked. |
| 19 | **Plugin system** | Obsidian/Logseq/SiYuan/Trilium have plugins; huge ecosystem cost | ❌ | Don't build; expose API/MCP/webhooks instead. |
| 20 | **Observability (metrics/logs)** | Homelab users want Prometheus/Grafana; health endpoint is the minimum | 🟡 `/health`; no metrics | Add optional `/metrics` (Prometheus) and structured logs. |

### 4.1 What the winners do that Lumen should copy

- **Poznote** is the current "self-hosted checklist in one app": multi-arch, OIDC, app passwords, S3, Git sync, signed webhooks, scheduled backups, Obsidian import, transcription adapter, MCP, multi-user, audit log, rootless, Helm, Proxmox. [GitHub](https://github.com/timothepoznanski/poznote)
- **Kept** shows how to make a small app feel trustworthy: offline device-only mode, native iOS/Android, OIDC, OAuth app access, MCP, 2FA, backup/restore, JSON/CSV/Markdown export, reverse-proxy examples, Google Keep import. [keepitkept.xyz](https://www.keepitkept.xyz/)
- **Jotty** shows the "one container, no database" story plus OIDC/LDAP/MFA/audit/REST/MCP, kanban+calendar, and encryption. [jotty.page](https://jotty.page/)
- **Anchor** shows offline-first mobile + OIDC + multi-user + backup ZIP in a small AGPL project. [GitHub](https://github.com/ZhFahim/anchor)
- **Docmost** shows the enterprise checklist: SAML/SSO, RBAC, page verification, public sharing, Confluence/Notion import, air-gapped deployment. [docmost.com](https://docmost.com/)

### 4.2 What Lumen should *not* copy

- **Multi-user as a first-class model** — it changes encryption, sync, search, permissions and UI. Stay single-user; offer one instance per person.
- **Team wiki features** (comments, mentions, review workflows, RBAC) — different buyer, different product.
- **Plugin marketplace** — ecosystem cost dwarfs the value; API/MCP/webhooks are enough.
- **AI chat/agents** — policy and trust; the long tail shows AI is commoditizing, not differentiating.
- **Whiteboards** — Capacities shipped them, but Lumen's personal-PKM user is better served by fast capture/search/import. Revisit only with demand.

### 4.3 Trust nuance: export ≠ backup

Lumen's `/v1/export` is a **portable snapshot** (Markdown/CSV/assets) for migration and inspection, not a byte-for-byte backup. A real disaster-recovery backup needs the encrypted SQLite store, the blobs, and the keys (see `README.md` and `server/README.md`). Users conflate the two, and the confusion is dangerous: an export that loses block IDs, revisions or assets is not a restore. Document both paths explicitly, test restore in CI, and show a "last verified backup" timestamp in the UI. This is the cheapest trust win available.

---

## 5. Import-format demand (ranked)

Rank = frequency of demand in migration threads + number of apps shipping an importer + switching-wave size. "Who ships it" is evidence that the format is expected, not an endorsement.

| # | Format | Why it matters | Who ships it | Difficulty | Notes for Lumen |
|---|---|---|---|---|---|
| 1 | **Obsidian vault** (Markdown + wikilinks + frontmatter + attachments + canvas) | Largest local-first community; vaults are the default "exit" format | Obsidian itself, Capacities (bulk MD), Poznote, HelixNotes, Twos, Anytype (MD), Joplin | M | Resolve `[[wikilinks]]`, aliases, embeds, relative links; map folders → types/collections; preserve frontmatter properties; import `.canvas` as whiteboard/JSON later. |
| 2 | **Notion export** (Markdown & CSV + subpages + databases + HTML callouts) | The most common SaaS exit; databases are the hard part | Obsidian Importer, Twos, Docmost, Capacities (generic MD/CSV), Anytype (MD) | M–L | Parse CSV databases into types/collections; map relation/rollup columns; handle Notion's UUID suffixes in filenames; convert callouts; preserve subpage hierarchy. |
| 3 | **Evernote ENEX** | Decades of notes; recurring refugee wave from price hikes | Obsidian Importer (Yarle), Joplin, UpNote, Trilium, Capacities (via MD) | M | ENEX is XML with attachments/base64; preserve created/updated dates, tags, notebook → collection; handle duplicates and large files. |
| 4 | **Apple Notes** | Default Apple user lock-in; export is awkward | Obsidian Importer, UpNote, Twos | M–L | AppleScript/HTML export; preserve folders, attachments, checklists, dates; handle iCloud-only notes; no official API. |
| 5 | **Capacities export** (Markdown + CSV + media + local links) | Direct competitor; Vasta already ships an importer | Vasta, Capacities itself (re-import) | S–M | Capacities export is clean: frontmatter properties, CSV collections, local links, media folders. Lumen should ship a one-command importer and a "your server" onboarding. [Docs](https://docs.capacities.io/reference/export) |
| 6 | **Roam Research JSON** | Block refs/daily notes; cult-like community | Obsidian Importer, Logseq, Twos | M | Parse nested blocks, `[[links]]`, `((block refs))`, attributes; map daily notes; preserve timestamps. |
| 7 | **Logseq** (Markdown/EDN + journals + block refs) | Local-first outliner community; DB transition may create refugees | Obsidian Importer, Twos | M | Journals → daily notes; block refs → block links; properties → frontmatter; TODO/DOING → tasks. |
| 8 | **OneNote** | Enterprise/education lock-in | Obsidian Importer, Trilium, Twos | L | OneNote export is HTML/PDF-ish; sections → collections; attachments; handwriting/images need OCR. |
| 9 | **Bear** (`.bear2bk`) | Apple-native markdown community | Obsidian Importer, UpNote | S–M | Bear backup is SQLite/JSON-ish; tags → tags; attachments; preserve dates. |
| 10 | **Google Keep** (Takeout JSON/HTML) | Simple capture refugees; Glass Keep/Kept ship importers | Obsidian Importer, Glass Keep, Kept, Twos | S | Takeout JSON + HTML; checklists, labels, colors, images, drawings; archived/trashed states. |
| 11 | **CSV/JSON generic** | Databases/spreadsheets; the universal escape hatch | Capacities, Notion, Airtable, many | S | Column → property mapping UI; type inference; dry-run; idempotent. |
| 12 | **HTML** | Web exports, old blogs, OneNote/Evernote fallbacks | Obsidian Importer, Capacities, Docmost | S–M | Readability extraction; images; links; preserve headings/lists. |
| 13 | **Markdown bundle / ZIP** | The lowest common denominator | Capacities, Obsidian, Joplin, most | S | Folder tree → collections; frontmatter; assets; link resolution. |
| 14 | **Textbundle** | Apple/Markdown packaged format | Obsidian Importer | S | `.textbundle`/`.textpack`; images + `info.json`. |
| 15 | **Airtable CSV** | Database users | Obsidian Importer | S | CSV + attachments; relation columns are lossy. |
| 16 | **Craft export** | Apple-native docs | Craft itself (MD/PDF) | M | Craft's export is Markdown/PDF; nested pages; images; no official importer elsewhere. |
| 17 | **Standard Notes** | Encrypted notes | Standard Notes export (JSON/MD) | M | Encrypted backup needs decryption; map super notes/blocks. |
| 18 | **Joplin JEX** | Joplin community | Joplin itself | M | JEX is a tar of Markdown + metadata; preserve notebooks/tags/resources. |
| 19 | **Notion API** | Live migration without export | Obsidian Importer (API mode) | M | Rate limits, blocks API, database queries; good for incremental migration. |
| 20 | **Evernote legacy / ENEX variants** | Old exports | Joplin/Yarle | M | Handle malformed XML, missing attachments, duplicate GUIDs. |

### 5.1 Minimum viable importer spec (what "good" looks like)

1. **Input:** folder, ZIP, or single file; 10k+ objects with progress and cancel.
2. **Parse:** frontmatter/properties, Markdown → blocks (headings, lists, quotes, tables, code, math, images, `[[wikilinks]]`, relative links).
3. **Map:** source folder/tag/type → target type/collection/tag; property mapping UI with type coercion; fallback type.
4. **Resolve:** links by title/alias/path; create missing objects optionally; preserve backlinks.
5. **Dates:** created/modified from metadata; daily notes by filename/date.
6. **Assets:** copy images/PDFs/audio; rewrite paths; dedupe by hash.
7. **Review:** dry-run preview, counts, warnings, mapping report; no silent data loss.
8. **Idempotency:** re-run without duplicates (source ID + hash); rollback/undo an import batch.
9. **Report:** "imported X objects, Y links, Z assets, N warnings"; exportable JSON.
10. **UX:** one command (`lumen import <path> --from obsidian`) + desktop wizard; docs per source.

### 5.2 Import quality pitfalls observed

- **Obsidian:** plugin-specific syntax (Dataview, Templater, callouts) is not standard Markdown; Capacities explicitly says it does not support "content created or modified by Obsidian plugins". Lumen should preserve unknown syntax as text and warn.
- **Notion:** exported filenames include UUIDs; CSV databases lose relations/rollups; callouts become HTML; subpage hierarchy is folder-based.
- **Evernote:** ENEX has no folder hierarchy beyond notebooks; created/modified dates are often lost by importers (Obsidian Importer issue reports).
- **Apple Notes:** folders are not preserved by some exporters; attachments are separate; checklists need conversion.
- **Roam/Logseq:** block refs and daily notes are the value; flattening them destroys the graph.
- **Capacities:** export is high quality, but object types/properties need mapping; bulk import has a 20k-object cap and 100-file/10GB limits.

---

## 6. Market churn and switching waves

| Event | When | Refugees | Opportunity for Lumen |
|---|---|---|---|
| **Pocket shutdown** | 2025 | Read-later users with years of saved articles | Import Pocket HTML/CSV into `weblink` objects with reader content; "your read-later, self-hosted". [Pocket](https://getpocket.com/) |
| **Omnivore acquisition/shutdown** | 2024 | Read-later/highlight users | Import Omnivore JSON; preserve highlights/labels; open-source repo remains. [GitHub](https://github.com/omnivore-app/omnivore) |
| **Skiff shutdown** | 2024 | Privacy-first notes/docs users | Import Skiff export; emphasize no-AI, encrypted-at-rest, self-hosted. [Skiff](https://skiff.com/) |
| **Evernote price hikes/free limits** | 2023–2026 | Evernote users | ENEX importer + "no subscription, no AI, your data" landing page. [Evernote](https://evernote.com/) |
| **Notion AI/price changes** | 2023–2026 | Notion users with simple personal setups | Notion export importer + object-model mapping; "Notion without the workspace". [Notion](https://www.notion.com/) |
| **Kortex → Eden pivot** | 2026 | Creator/writer notes | Markdown importer; "your notes outlive the startup". [Kortex](https://www.kortex.co/) |
| **Amie pivot to AI notetaker** | 2025–2026 | Calendar/notes users | ICS + Markdown import; stable local alternative. [Amie](https://amie.so/) |
| **xTiles pivot to AI daily planner** | 2026 | Visual planners/ADHD users | Export/import if available; template gallery for daily planning. [xTiles](https://xtiles.app/) |
| **Capacities no self-host/E2EE/collab** | ongoing | Privacy/self-host seekers | Capacities importer + self-hosted story + E2EE roadmap honesty. [Capacities](https://capacities.io/) |
| **Vasta alpha** | 2026 | Self-hosted Capacities seekers | Match importer/export; beat on native apps, encryption, stability. [Vasta](https://github.com/ariedotme/vasta) |

**Pattern:** every shutdown/pivot produces a short window where users will tolerate a rough importer. The winner is the app with the best importer *and* a credible "you can leave again" export story. Lumen already has export; it needs import.

---

## 7. Strategic implications for Lumen

### 7.1 Defend

- **Native Qt speed** vs Electron/Tauri competitors; publish benchmarks (10k blocks, 50k objects, cold start).
- **Offline-first Android + desktop**; Capacities' #1 churn driver is mobile capture.
- **No AI / no telemetry / encrypted at rest**; a clear privacy story in a market drowning in AI.
- **15 MB server + SQLite + single container**; homelab-friendly.
- **Object model + queries + calculations + tasks + version history** already at parity; don't rebuild.

### 7.2 Attack (in order)

1. **Importers** — Obsidian, Notion, Evernote, Apple Notes, Capacities, Keep, OneNote, Bear, Roam, Logseq.
2. **Email-in capture** — self-hosted IMAP poller, per-instance address, allowlist, attachments.
3. **Automation rules + webhooks** — deterministic, no AI, n8n/Zapier bridge.
4. **OCR** — Tesseract, FTS indexing, image/PDF search.
5. **Public read-only share** — tokenized object/collection pages.
6. **Calendar sync + time-blocking** — ICS first, CalDAV later.
7. **Web clipper depth** — highlights/annotations/full-page archive/YouTube.
8. **RSS/read-later ingestion** — feeds → weblink objects.
9. **SRS (optional)** — FSRS, block-based cards.
10. **Geo/habits (cheap)** — properties + map/streak views.

### 7.3 Don't build

- AI chat/agents/semantic search/media analysis (policy).
- Full multi-user/collaboration/RBAC (different product).
- Whiteboards (unless demand proves otherwise; Capacities' move doesn't make it Lumen's fight).
- Plugin marketplace (API/MCP/webhooks are enough).
- Team wiki features (comments, mentions, review workflows).
- Native iOS/macOS apps (Android + desktop + extension is the current surface).

### 7.4 Positioning

> **"The self-hosted object PKM that imports everything, captures from anywhere, and never phones home."**

Supporting lines:
- "Your notes, your machine: native-fast (no Electron), offline-first, encrypted at rest, no AI looking at your data."
- "The features Capacities paywalls — queries, tasks, version history, API, MCP — are free here, forever."
- "Import your Capacities/Obsidian/Notion/Evernote export in one command."
- "One container, one SQLite file, one backup you can actually own."

### 7.5 90-day roadmap (proposal)

| Window | Work | Why |
|---|---|---|
| Weeks 1–3 | Obsidian + Capacities importers (dry-run, mapping, report) | Highest switching value; Vasta already ships Capacities import. |
| Weeks 3–5 | Email-in IMAP poller + Inbox object + allowlist | Capture differentiator; no object PKM does it self-hosted. |
| Weeks 5–7 | Rules engine + signed webhooks + n8n recipe | Automation without AI; unlocks integrations. |
| Weeks 7–9 | OCR (Tesseract) + FTS indexing + badge | Visible search win; cheap. |
| Weeks 9–11 | Public read-only share pages (object/collection, expiry) | Writers/researchers; small Go surface. |
| Weeks 11–13 | ICS calendar feed + "Plan" view (kanban + calendar) | Daily planning loop; uses existing primitives. |
| Continuous | Backup/restore UX, reverse-proxy docs, ARM64 image, OIDC | Self-hosted adoption checklist. |

### 7.6 Metrics to watch

- **Import:** time-to-first-import, import success rate, warnings/object, re-run duplicate rate.
- **Capture:** capture latency (share sheet → saved), email-in delivery rate, offline queue depth.
- **Trust:** backup restore success in CI, export completeness (objects/links/assets), version-history restore time.
- **Retention:** daily-note review rate, tasks completed, searches per day, objects created per week.
- **Adoption:** Docker pulls by arch, reverse-proxy guide views, OIDC setup completion, ARM64 share.

---

## 8. Source index

### 8.1 Directories and datasets

- [awesome-selfhosted — note-taking](https://awesome-selfhosted.net/tags/note-taking--editors.html)
- [awesome-note-taking (100+ apps)](https://github.com/tehtbl/awesome-note-taking)
- [selfh.st/apps](https://selfh.st/apps/) — SSO filter: built-in / plugin / enterprise-only
- [noteapps.info](https://noteapps.info/) — 40 apps × 368 features
- [GitHub topic: pkm](https://github.com/topics/pkm)
- [GitHub topic: note-taking](https://github.com/topics/note-taking)
- [GitHub topic: self-hosted](https://github.com/topics/self-hosted)
- [awesome-selfhosted](https://github.com/awesome-selfhosted/awesome-selfhosted)
- [GitHub search: self-hosted notes created >2025](https://github.com/search?q=self-hosted+notes+created%3A%3E2025-01-01&type=repositories&s=stars&o=desc)

### 8.2 Vendor docs read

- Capacities: [import](https://docs.capacities.io/reference/import), [bulk import](https://docs.capacities.io/reference/bulk-import), [export](https://docs.capacities.io/reference/export), [integrations](https://docs.capacities.io/reference/integrations), [email](https://docs.capacities.io/reference/integrations/email)
- Anytype: [import/export](https://doc.anytype.io/anytype/data/import-and-export.md), [local API](https://doc.anytype.io/anytype/features/integrations/local-api.md)
- Notion: [export your content](https://www.notion.com/help/export-your-content)
- Obsidian: [Importer README](https://github.com/obsidianmd/obsidian-importer), [Bases](https://help.obsidian.md/bases), [Web Clipper](https://obsidian.md/clipper)
- SiYuan: [homepage](https://b3log.org/siyuan/en/)
- TriliumNext: [docs](https://triliumnext.github.io/Docs/)
- SilverBullet: [homepage](https://silverbullet.md/)
- Docmost: [homepage](https://docmost.com/)
- Jotty: [homepage](https://jotty.page/)
- Poznote: [GitHub](https://github.com/timothepoznanski/poznote)
- Kept: [homepage](https://www.keepitkept.xyz/)
- Anchor: [GitHub](https://github.com/ZhFahim/anchor)
- Glass Keep: [GitHub](https://github.com/nikunjsingh93/react-glass-keep)
- Karakeep: [homepage](https://karakeep.app/)
- Linkwarden: [homepage](https://linkwarden.app/)
- Readeck: [homepage](https://readeck.org/en/)
- Zotero: [homepage](https://www.zotero.org/)
- HelixNotes: [homepage](https://helixnotes.com/)
- NoteDiscovery: [homepage](https://www.notediscovery.com/)
- Blinko: [homepage](https://blinko.space/)
- Eclaire: [GitHub](https://github.com/eclaire-labs/eclaire)
- Fibery: [homepage](https://fibery.io/)
- Supernotes: [homepage](https://supernotes.app/)
- xTiles: [homepage](https://xtiles.app/)
- Amplenote: [homepage](https://www.amplenote.com/)
- Twos: [homepage](https://www.twosapp.com/)
- UpNote: [homepage](https://getupnote.com/)
- Milanote: [homepage](https://milanote.com/)
- Mem: [homepage](https://get.mem.ai/)
- Fabric: [homepage](https://fabric.so/), [publish](https://fabric.so/features/publish)
- Saner: [homepage](https://www.saner.ai/)
- Kortex: [homepage](https://www.kortex.co/)
- Lazy: [homepage](https://lazy.so/)
- Recall: [homepage](https://www.recall.it/)
- RemNote: [SRS guide](https://help.remnote.com/en/articles/6022755-getting-started-with-spaced-repetition)
- Day One: [release notes](https://dayoneapp.com/guides/release-notes/ios-release-notes/)
- Pocket: [getpocket.com](https://getpocket.com/)
- Omnivore: [GitHub](https://github.com/omnivore-app/omnivore)

### 8.3 Community threads (titles/snippets; Reddit blocks automated fetches)

- [r/selfhosted — self-hosted note-taking app?](https://www.reddit.com/r/selfhosted/comments/1bs8uzh/self_hosted_note_taking_app/)
- [r/selfhosted — SSO/OIDC list](https://www.reddit.com/r/selfhosted/comments/1w47nrq/here_is_a_list_of_self_hosted_apps_support_sso/)
- [r/selfhosted — SSO tax list](https://www.reddit.com/r/selfhosted/comments/1w1fmde/i_built_the_selfhosted_equivalent_of_ssotax_11/)
- [r/selfhosted — losing data](https://www.reddit.com/r/selfhosted/comments/1fpbnbc/losing_data_the_only_reason_i_am_scarred_of_selhosting/)
- [r/selfhosted — backups best practices](https://www.reddit.com/r/selfhosted/comments/1xa7fl7/best_practices_and_practical_solutions_for_backups/)
- [r/PKMS — self-promotion thread Sept 2026](https://www.reddit.com/r/PKMS/comments/1w4vx4h/self_promotion_thread_september_2026/)
- [r/PKMS — Saner alternatives](https://www.reddit.com/r/PKMS/comments/1pex5zu/saner_ai_alternatives/)
- [r/capacitiesapp — object-based notetaking](https://www.reddit.com/r/capacitiesapp/comments/1g9q48l/trying_to_figure_out_whats_new_about_objectbased/)
- [r/NoteTaking — self-hosted Capacities clone (Vasta)](https://www.reddit.com/r/NoteTaking/comments/1xc0h5o/made_my_own_selfhosted_notes_app_because/)

### 8.4 Comparison/analysis articles

- [Tool Finder — Capacities alternatives 2026](https://toolfinder.com/alternatives/capacities)
- [Memex — Capacities alternatives 2026](https://memex.garden/capacities-alternatives-2026)
- [NoteApps — best web-clipping apps](https://noteapps.info/showdown/best-note-apps-for-web-clipping)
- [xTom — self-hosted note apps compared](https://xtom.com/blog/comparing-top-self-hosted-note-taking-apps/)
- [OpenSourceAlternatives — best open-source note apps 2026](https://www.opensourcealternatives.to/blog/best-open-source-note-taking-apps)
- [Kosmik — best second-brain apps](https://www.kosmik.app/blog/best-second-brain-apps)
- [Capacities — best PKM apps 2026](https://capacities.io/personal-knowledge-management-apps)

---

## 9. Appendix — apps checked but not treated as Capacities-likes (and why)

| App | Why excluded |
|---|---|
| **Bear** | Markdown notes; covered as an import source, not an object PKM. |
| **Evernote / OneNote / Google Keep / Apple Notes** | Incumbent capture apps; relevant as import sources, not as Capacities-likes. |
| **Simplenote / Zoho Notebook / Workflowy** | Simple notes/outliners; no object model. |
| **Coda / Airtable / NocoDB / Baserow / Teable / Grist / Rowy / Undb** | Database/spreadsheet tools; adjacent, not PKM. |
| **Directus / Payload / Strapi** | Headless CMS/backend frameworks; not end-user PKM. |
| **Notion / AppFlowy / AFFiNE / Anytype / Tana / Heptabase / Reflect / Craft / MyMind / Obsidian / Logseq** | Covered baseline; not re-researched. |
| **Roam Research** | Covered as an import format; the app itself is a known outliner. |
| **Cubox / Matter / Raindrop / Glasp / Hypothesis** | Read-later/bookmark/annotation; included only as frontier evidence. |
| **Habitica / Loop Habit / Exist** | Habit/health trackers; included only as frontier evidence. |
| **n8n / Activepieces / Huginn / Node-RED** | Automation infrastructure; included only as frontier evidence. |
| **Ghost / Quartz / Flowershow / btw / Obsidian Publish** | Publishing tools; included only as frontier evidence. |
| **Muse / Kosmik / Scrintal / Milanote** | Visual canvas/board tools; included as visual frontier evidence. |
| **Kortex / Amie / xTiles / Napkin** | Pivoted/anti-import; included in the watchlist. |
| **Omnivore / Pocket / Skiff** | Dead/shut down; included in the watchlist. |

---

*End of sweep. All links checked during the sweep; some small-project sites are flaky by nature. No repo files were modified; this document is the only write.*
