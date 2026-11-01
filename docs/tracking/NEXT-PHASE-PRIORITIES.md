# Next Phase — Research-Driven Priorities

Status: ACTIVE backlog. Created 2026-10-31 after the parity program shipped
(v0.6.1, backend 13 stages + desktop D1-D7). Sources — three parallel research
sweeps, all in `scratch/2026-10-31-capacities-parity/research/`:

- `CAPACITIES-DEEP2.md` — Capacities releases 60-73, pricing gates, blog, UX
  chrome (agent A).
- `REDDIT-SENTIMENT.md` — 159 Reddit links, 534 comments, 40 App Store reviews,
  HN (agent B).
- `PKM-UI-PATTERNS.md` — ~40 UI patterns from 12 apps ranked for Qt (agent C).

## Strategic reads

1. **A real competitor exists now**: Vasta (AGPL, Oct 2026) — self-hosted
   Capacities-like with a **Capacities importer** and one-container deploy.
   Lumen's differentiators to defend: native Qt speed, offline-first, no-AI
   privacy, Android + extension. Vasta's importer is the feature to match.
2. **Capacities' open flank is performance** (long-note lag, sync complaints,
   blank screens). Native Qt is our answer: make it measurable.
3. **Trust beats features** (data-loss stories do more damage than missing
   features). We HAVE trash/version-history/backup — the gap is making them
   VISIBLE and one-click.
4. **Reminders are the most-requested missing feature in PKM right now** —
   and Capacities has NOT shipped them. Chance to lead.
5. **Mobile capture reliability is the #1 churn driver** in Capacities' own
   reviews. Our Android share target must be bulletproof: offline queue,
   search-to-block, share sheet parity.
6. Their official workflow story is **daily-note-first + PARA/GTD**
   (Zettelkasten is NOT an official use case). Onboard to that.

## P0 — Trust, retention and capture (do first)

| # | Item | Why (evidence) | Area | Effort |
|---|---|---|---|---|
| 1 | **Android capture hardening**: reliable share target, offline queue with visible backlog, dedupe, search-to-block, bottom sheets w/ swipe-dismiss (C6) | B1 (24/40 negative reviews are mobile capture) | mobile | M |
| 2 | **Reminders**: local notifications (task due, daily-note nudge) desktop + Android, notification actions (complete/snooze) | B2 (most requested; Capacities unshipped) A5 | desktop+mobile+server | M |
| 3 | **Trust surface**: visible sync state, conflict resolution UI, one-click restore from version history/trash in-context, "everything is backed up" onboarding moment | B3, A1 | desktop | M |
| 4 | **Daily-note review loop**: Reviewed checkbox, calendar dots (white/grey/green), Unreviewed dashboard section | A3 (cheap retention habit) | desktop | S |
| 5 | **Onboarding**: "Getting Started" object that teaches by doing + template gallery first-run + seeded sample content, restartable | A10 (their stated churn reasons), C5 | desktop+server | M |
| 6 | **Quick capture everywhere**: desktop global hotkey/tray quick-note, palette paste (any content), Craft-style Quick Entry (mode + destination incl. any date + send/send-and-open/send-and-continue) | A4, C3 | desktop | M |

## P1 — Navigation, search and editor polish

| # | Item | Why | Area | Effort |
|---|---|---|---|---|
| 7 | **Palette 2.0**: mode prefixes (`>` actions, `#` tags, `@` objects), recents, hotkey hints, AND-by-default with operators, jump-to-block | C1, B7 (search is a cheap visible win) | desktop | M |
| 8 | **Unlinked mentions + one-click linkify** (alias-aware; backend mostly exists from aliases stage) | C4, plan F3.6 | desktop+server | S |
| 9 | **Tabs as workspaces**: per-tab history (have), pinned tabs, "open in" policy (side/center/full) per view, drag-to-side-panel | A2 (highest-value nav gap), C2 | desktop | M |
| 10 | **Hover previews** reused across palette results, backlinks and links | C9, A2 | desktop | M |
| 11 | **Backlinks pane polish** + progress bars aware of schema (checkbox/field-driven) + highlight colors | C4, C10 | desktop | S |
| 12 | **Focus mode + timer island** (5-60 min, chime, completion card) | A6 (S effort, high delight) | desktop | S |

## P2 — Big bets and positioning

| # | Item | Why | Area | Effort |
|---|---|---|---|---|
| 13 | **Capacities importer**: consume their Full Export ZIP (we designed ours compatible) — one-command migration + "your server" onboarding | B6 (Vasta ships one; self-host demand is real) | server+tools | M |
| 14 | **Performance story**: large-note benchmarks (10k blocks), block virtualization, cold-start metrics, published numbers | A1 (their flank = our marketing) | desktop | M-L |
| 15 | **Whiteboard (object-aware canvas)**: now "table stakes" (B9, shipped to delight Oct 2026) — keep scope simple: objects as cards, text, shapes, arrows, frames | B9, plan F5.1 | desktop | L |
| 16 | **Mobile widgets v2**: quick-capture pill, pinned today, recent-objects feed (C7) + live sidebar feeds / Home page (C8) | C7-C8 | mobile+desktop | M |
| 17 | **Reader/annotation stack**: weblink reader exists; add highlights→note blocks, PDF annotations (plan F3.3) | A9 | server+desktop | L |
| 18 | **Task depth**: one-line parser (`!! tomorrow 2pm` exists on desktop — extend), deadline vs scheduled, On hold, recurring stats/streaks, occurrence log UI | A5, B2 | desktop+mobile | M |

## Free by policy (never build; note in docs)

AI chat/autofill/semantic search/media analysis (product policy), Readwise /
Kindle ingestion (third-party), collaboration/multi-user, hosted sync.

## Positioning one-liners (from the research)

- "Your notes, your machine: native-fast (no Electron), offline-first,
  encrypted at rest, no AI looking at your data."
- "The features Capacities paywalls — queries, tasks, version history, API,
  MCP — are free here, forever."
- "Import your Capacities export in one command."

## Immediate next stage queue (updated from RESUME.md)

D8 recurring-tasks-ui (already specced, unchanged) → then, in order: P0-4
daily-note review loop (S, desktop) → P1-8 unlinked mentions + linkify (S) →
P1-12 focus mode (S) → P0-2 reminders (M) → P0-6 quick capture (M) → P0-3
trust surface (M) → P1-7 palette 2.0 (M) → P0-5 onboarding (M) → then P2 in
order. Mobile hardening (P0-1) can run as its own mobile/* stage in parallel
with any desktop stage (different toolchain, no build contention).

Research files remain in scratch; this doc is the durable distillation.

## Addendum: Vasta intel (2026-10-31, research/VASTA-INTEL.md)

Vasta (github.com/ariedotme/vasta, AGPL, alpha, solo dev) leads on
onboarding/distribution: public image, 4-command install, local mode, PWA,
in-app Capacities importer. Gaps = our openings: no export (their #1 issue),
no app-level encryption (plaintext calendar passwords), LAN HTTP breakage,
no native mobile, 42s replica fill at 50k objects. Queue additions: S6
one-command quickstart (S), S7 desktop local mode with bundled server (M),
Capacities importer elevated to first wave (M), benchmark suite with
published numbers incl. 50k-adopt stat (M), trust comparison README section
(S), read-only web share page (M, decision), Telegram capture (decision —
policy change required). Full queue: scratch/.../CONTINUATION-PLAN.md.

## Addendum 2: landscape sweep + community demand (agents D+E, 2026-10-31)

Lumen meets 10/20 of the aggregate switch checklist. Adoption wave added to
the continuation plan (A1-A10): A1 Capacities importer (the #1 precondition;
Capacities cannot restore its own export), A2 importer family (Obsidian/
Notion/Evernote/Apple Notes — importers are the moat: Obsidian ships 15),
A3 public install + product docs + self-hosted checklist gaps (OIDC/S3/
webhooks/ARM64), A4 trust comparison + benchmarks (50k vs Vasta's 42s),
A5 read-only share + capture page (cheap answer to the web-UI gap; full web
editor remains an explicit big decision), A6 no-AI rules DSL + signed
webhooks (n8n/Zapier), A7 email-in via self-hosted IMAP (decision: arguably
in-policy, biggest object-PKM gap), A8 OCR (Tesseract, table stakes),
A9 ICS calendar sync, A10 FSRS flashcards (deterministic; Capacities says
no). Positioning validated: "the third, self-hosted object-based PKM";
sell architecture + API/MCP; never a cloud AI button. Launch playbook:
r/selfhosted megathread + r/PKMS monthly (no AI-written posts) +
r/NoteTaking; formats that worked: "I built the thing" + demo + limits,
"we listened" follow-ups, honest comparison + migration guides.
Raw: docs/qa/2026-10-31-research-sweep/{CAPACITIES-LIKES-SWEEP,COMMUNITY-DEMAND}.md
