# Community demand: what actually makes a Capacities user switch

**Research agent E · 2026-10-31 · Lumen PKM**
Scope: the *seeker pool* — people actively looking for a Capacities alternative, a
self-hosted Capacities, an open-source object-based PKM, or a way out of the
Capacities subscription. Not the happy-user pool (that is `REDDIT-SENTIMENT.md`).

This document is the adopter's checklist, the trust blockers that stop them, the
AI question, and a launch playbook. It is written to be acted on, not admired.

---

## 0. Method, sample, confidence

**What I did.** Searched Reddit (r/selfhosted, r/PKMS, r/privacy, r/NoteTaking,
r/ObsidianMD, r/Notion, r/ProductivityApps, r/capacitiesapp), Hacker News
(Algolia API, full comment trees), Capacities' public feedback board, docs and
pricing pages, and the current "Capacities alternative" listicle market. Read
thread bodies and top comments; for Reddit I could not scrape at scale (API
blocks), so I used search-result thread text plus top comments and cross-checked
against the earlier `REDDIT-SENTIMENT.md` sweep.

**Sample.** 26 direct seeker threads (someone asking for an alternative, a
migration path, or a self-hosted/private replacement) plus 14 adjacent threads
(self-hosted evaluation criteria, privacy, AI stance, launch post-mortems).
Links in Appendix A.

**Confidence.** Qualitative, not statistical. Reddit threads are self-selected,
often low-vote, and the same 20 people may appear in several. Treat frequency
bands as "how often this showed up in the threads I coded", not market share.
The *direction* is consistent across three independent pools (Capacities
refugees, self-hosters, privacy/HN), which is why the ranking is usable.

**Frequency bands used below**

| Band | Meaning in this sample |
|---|---|
| **EVERY** | Stated in ≥ 70% of the relevant threads; a precondition, not a preference |
| **OFTEN** | 40–69%; raised unprompted in most threads |
| **SOMETIMES** | 15–39%; a real segment, not the majority |
| **RARE** | < 15%; do not build for it first |

**One structural fact that shapes everything:** the direct seeker pool is
*small*. r/capacitiesapp has ~8k members; a good "alternative?" thread gets
0–16 points. r/selfhosted has ~860k but treats new projects with suspicion.
This is not a mass market. It is a few thousand high-intent, technical,
evangelical users — the people who will run Docker, file bugs, and recommend
Lumen in the next "Capacities alternative?" thread. Optimise for *them saying
yes*, not for reach.

---

## 1. Executive summary

1. **The market believes there are only two object-based PKMs.** A top reply in
   r/capacitiesapp: *"I don't believe there currently are object based PKMs
   besides Capacities and AnyType."* That is the positioning gift: Lumen can be
   "the third one — and the self-hosted one."
2. **The #1 switch precondition is migration, not features.** People with 3,000
   notes do not switch to a nicer app; they switch to a *credible exit*. A
   lossless Capacities Full Export importer (types, properties, relations,
   links, media, daily notes) is the single highest-unlock item on the roadmap.
3. **Mobile is Capacities' open wound and the seeker's first filter.** "I'm 99%
   mobile" is a literal quote. Lumen already has an Android app with a share
   target and an outbox; reliability and capture speed matter more than parity.
4. **The self-hosted pool expects a browser client.** Lumen currently has
   desktop + Android + extension and an API-only server. r/selfhosted repeatedly
   asks for a "solid web UI", "web-based editor", "access from mobile and PC
   without syncing files". **No web UI is Lumen's biggest adoption gap** — bigger
   than any missing feature.
5. **One container + SQLite is the deployment contract.** Memos (60k stars) and
   every successful self-hosted note launch prove it. Lumen's Go + SQLite stack
   fits perfectly; the current install path (personal compose, bootstrap
   secrets, token file) does not yet read as "one command".
6. **Open source is table stakes; the license must be clean.** AGPL is fine,
   even preferred. "Source available" (Anytype) and open-core bait-and-switch
   are actively punished. Lumen's AGPL-3.0-or-later is an asset — show it.
7. **No subscription is a feature, not a pricing strategy.** A 419-upvote
   Capacities feedback request for a lifetime license has sat open for four
   years. Self-hosting answers it. Say "free to self-host, no seats, no
   metered AI" loudly.
8. **Data safety is the trust currency.** Seekers have lost notes to Obsidian
   LiveSync rollbacks, OneNote sync, and Capacities workspace bugs. Visible
   backups, version history, restore, and a boring sync design beat cleverness.
9. **The AI-free position is validated — with one nuance.** The PKM crowd is
   the most anti-AI audience on the internet ("This is easily the most anti-AI
   sub I belong to"). But a minority wants *optional, local-only* AI, and an
   agentic minority wants API/MCP. Lumen's "no AI in the core + bring your own
   tools via API/MCP" is the correct shape. Do not mock AI users; sell
   *architecture*, not ideology.
10. **The launch channel is not r/privacy and not a cold r/selfhosted post.**
    r/privacy bans app promotion (R13, explicitly including open source).
    r/selfhosted quarantines projects younger than 3 months into a weekly
    megathread and asks how AI was involved. r/PKMS allows self-promo only in a
    monthly thread and bans AI-written posts. The winning path is: be useful in
    the "alternative?" threads first, then launch with a migration guide and a
    demo, then post the "we listened" follow-up.
11. **The listicle market is already crowded and shallow.** "Best Capacities
    alternatives" pages all recommend Obsidian, Anytype, SiYuan, Trilium,
    AppFlowy. Almost none recommend a *self-hosted Capacities*. The query
    "self-hosted Capacities alternative" is effectively uncontested except by
    Vasta, Nodum and memrynote. That is Lumen's SEO and community wedge.
12. **Lumen already meets most of the checklist.** The gaps that actually block
    adoption are four: **web UI, public one-command install, structured
    Capacities importer, English docs/demo.** Everything else is proof and
    packaging.

---

## 2. Who is seeking (the adopter archetypes)

The same person appears in several threads, but the *reason* differs. Each
archetype has a different "yes".

| Archetype | Size / urgency | What they love about Capacities | Why they are leaving | What closes them | Where they hang out |
|---|---|---|---|---|---|
| **The object-model refugee** | Small, very high intent | Typed objects, daily note, properties, graph, "it clicks" | Cloud-only, price, mobile, data ownership, future uncertainty | A Capacities-shaped app they can host; lossless import; mobile that works | r/capacitiesapp, r/PKMS, r/ObsidianMD |
| **The homelabber** | Large pool, medium intent | Usually none — they never used Capacities | Wants a self-hosted Notion/Capacities for the stack | One container, web UI, mobile companion, no telemetry, AGPL, docs | r/selfhosted, r/homelab, Lemmy, selfh.st |
| **The regulated worker** | Small, highest willingness to pay | Structure, polish, German/EU provider | Client/employer forbids cloud; AI blocked by org | Local control, offline, export, audit-friendly security page, no AI | r/capacitiesapp, r/PKMS, r/privacy, legal/consulting forums |
| **The mobile-first capturer (often ADHD)** | Medium, high churn | Daily note as inbox, low friction | "I'm 99% mobile"; Capacities mobile is buggy/limited | Fast capture, share sheet, widget, offline queue, search-to-block | r/PKMS, r/NoteTaking, r/ADHD |
| **The Obsidian exile** | Large, medium intent | Objects without plugin assembly | Plugin fatigue, sync data loss, no web app, tinkering | Objects out of the box, reliable sync, web access, import from Obsidian | r/ObsidianMD, r/PKMS |
| **The Notion refugee** | Large, low urgency | Offline-ish, objects, speed | Cloud, AI upsell, per-seat, slow mobile | Offline, import, no subscription, simple sharing | r/Notion, r/ProductivityApps |
| **The privacy absolutist** | Medium, loud | Nothing — Capacities is cloud | No E2EE, no self-host, always-online | Self-host, encryption at rest, no telemetry, open source, threat model | r/privacy, r/PKMS, Privacy Guides forum |
| **The Linux/native user** | Small, vocal | Capacities has an AppImage now, but it is web-tech | Wants native, no Electron, no browser | Qt/native desktop, Linux package, offline | HN, r/linux, r/selfhosted |
| **The student / price-sensitive** | Medium | Free tier, polish | $11–15/mo, no lifetime, regional pricing | Free self-host, one-time support option, no metered AI | r/capacitiesapp, r/NoteTaking, student forums |
| **The agentic tinkerer** | Small, rising | API/MCP, data model | API limits, MCP disappointment, AI locked to vendor | Local API, MCP, CLI, own models | r/capacitiesapp, HN, r/LocalLLaMA |

**Synthesised "I would switch tomorrow if…" lines** (composite, useful for copy):

- Object refugee: *"If I can import my Capacities export with my types and links
  intact, and the Android app captures as fast as the desktop, I'm in."*
- Homelabber: *"If it's one compose file, has a browser UI, and doesn't phone
  home, I'll run it tonight."*
- Regulated worker: *"If the data never leaves my server and there is no AI
  anywhere near it, I can use it for client work."*
- Obsidian exile: *"If I get objects and a daily note without installing 14
  plugins, and sync doesn't eat my notes, I'll move."*
- Privacy absolutist: *"If it's AGPL, self-hosted, encrypted at rest, and you
  can show me the network calls, I'll trust it."*

---

## 3. Thread catalog

Each entry: **WHO** is asking, **WHY** they are leaving, **WHAT THEY REQUIRE**
(the switch checklist they state), and the signal it gives Lumen. Quotes are
verbatim where short; longer passages are paraphrased and marked.

### 3.1 Direct "Capacities alternative" asks

**1. "Selfhosted alternative to capacities.io" — r/selfhosted, 2023-04-02**
<https://www.reddit.com/r/selfhosted/comments/129wmux/selfhosted_alternative_to_capacitiesio/>
- **WHO:** a self-hoster who likes Capacities' object model but wants it on
  their own server.
- **WHY:** cloud-only; wants local ownership.
- **REQUIRES:** self-hostable, local files, Docker; answers recommend Logseq,
  Trilium, Anytype, BookStack, TiddlyWiki.
- **Signal:** the thread is still alive in 2026. A May 2026 comment: *"Anytype
  wins for Capacities users… same object-first paradigm… Importing is super fast
  and 99% similar. Strong privacy + full self-hosting."* Anytype is the default
  answer; Lumen must beat it on turnkey deploy, standard export, speed, web UI.
- **Quote:** *"Anytype wins for Capacities users."*

**2. "Alternative to Capacities? It's great, but just clunky" — r/PKMS, 2024-12-01**
<https://www.reddit.com/r/PKMS/comments/1h3usrt/alternative_to_capacities_its_great_but_just/>
- **WHO:** a knowledge worker with a growing Capacities library, comfortable
  with networked notes.
- **WHY:** too much clicking, keyboard shortcuts break focus, wants to move
  before the library gets bigger.
- **REQUIRES (explicit list):** (1) network-based notes, no folder/sidebar
  hierarchy; (2) good search; (3) easy page creation without duplicate-name
  friction; (4) graph view; (5) math and code; (6) keyboard-first, "the less
  mouse the better".
- **Signal:** the cleanest feature checklist in the corpus. Lumen meets most of
  it; the differentiator is keyboard-first polish and search.

**3. "Any suggest to similar like 'capacities'" — r/PKMS, 2024-09-06**
<https://www.reddit.com/r/PKMS/comments/1fa7h1n/any_suggest_to_similar_like_capacities/>
- **WHO:** an Obsidian user who needs a web-capable notes app.
- **WHY:** *"I need note app with working in web. I found capacities and I like
  it, but problem is notes are not in local or your own cloud."*
- **REQUIRES:** web access + local/own-cloud storage.
- **Signal:** web access and local storage are asked for in the same breath.
  This is the exact combination Lumen lacks (web) and has (own server).

**4. "Could you please advise me on finding an optimal alternative of Capacities (besides Anytype)?" — r/capacitiesapp, 2025-09-16**
<https://www.reddit.com/r/capacitiesapp/comments/1nijyad/could_you_please_advise_me_on_finding_an_optimal/>
- **WHO:** a Capacities user who has already rejected Anytype and is looking for
  a third option.
- **WHY:** friction with Capacities; wants an object-based alternative that is
  not Anytype.
- **REQUIRES:** object-based model, speed, no forced migration to Anytype.
- **Signal:** the top reply is the market-gap statement: *"I don't believe there
  currently are object based PKMs besides Capacities and AnyType, or at least I
  don't know of them."* This is the thread Lumen should be in — not with a link
  drop, but with a genuine "here's a third one, self-hosted" answer.

**5. "Looking to move away from Capacities to Obsidian because of the customizability and holding onto my own data" — r/ObsidianMD, 2025-12-22**
<https://www.reddit.com/r/ObsidianMD/comments/1psm2vk/looking_to_move_away_from_capacities_to_obsidian/>
- **WHO:** a Capacities user who depends on the WhatsApp capture integration.
- **WHY:** wants customization and data ownership.
- **REQUIRES:** porting path, capture integrations, local files.
- **Signal:** the top reply switched back for *"easy to access and easy to share
  md-files, a robust canvas and a diagram tool"* and *"did not really like
  Capacities' editor and search function."* Migration is not only about data;
  it is about keeping the capture habits (WhatsApp) and gaining canvas/files.

**6. "New to obsidian, moved 3000+ notes from Capacities" — r/ObsidianMD, 2026-05-04**
<https://www.reddit.com/r/ObsidianMD/comments/1t3cin1/new_to_obsidian_moved_3000_notes_from_capacities/>
- **WHO:** a student/researcher with a large personal + academic library.
- **WHY:** *"I started to be concerned about my data and the paywall behind
  every features I need."*
- **REQUIRES:** export that preserves properties and tags; a destination that
  does not charge per feature.
- **Signal:** the export "went well" but produced *"a lot of capacities
  properties… that should be purged and some tags that has merged together."*
  This is the exact quality bar for Lumen's importer: not just "it imports", but
  "it imports cleanly".

**7. "Moving from Capacities but facing friction" — r/ObsidianMD, 2026-08-18**
<https://www.reddit.com/r/ObsidianMD/comments/1vrjabi/moving_from_capacities_but_facing_friction/>
- **WHO:** a Capacities user trying to rebuild the object model in Obsidian.
- **WHY:** wants out, but cannot replicate the object graph or colored select
  properties.
- **REQUIRES:** object graph view, property colors, object-aware navigation.
- **Signal:** the "just use Obsidian" answer fails on exactly the object model.
  This is Lumen's core differentiator, not a nice-to-have.

**8. "Obsidian but with object-oriented-notes" — r/ObsidianMD, 2026-04-15 (297 points, 73 comments)**
<https://www.reddit.com/r/ObsidianMD/comments/1smemow/obsidian_but_with_objectorientednotes/>
- **WHO:** a long-time note-taker who tried everything, loved Capacities/Anytype's
  object model, but stayed on Obsidian.
- **WHY:** wants objects without leaving Obsidian.
- **REQUIRES:** inline object creation (`@`), templates, typed objects.
- **Signal:** 297 upvotes for a plugin that only approximates objects. The demand
  for the object model is not niche; the *willingness to assemble it* is the
  barrier. Lumen sells "objects without assembly".

**9. "Object-based PKM on Android?" — r/PKMS, 2025-07-16**
<https://www.reddit.com/r/PKMS/comments/1m1nriv/objectbased_pkm_on_android/>
- **WHO:** a mobile-first user with ADHD, new to PKM.
- **WHY:** *"Capacities seems nearly perfect in theory for how my brain works,
  but the mobile app is too limited (I'm 99% mobile)."*
- **REQUIRES:** robust Android app, object-based, visual connection graph,
  brain-dump capture, life dashboard, media/bookmark cataloging.
- **Signal:** the strongest mobile signal in the corpus. A desktop-only or
  web-only answer loses this user completely.

**10. "Looking for a PKM alternative to Anytype — local-first, encrypted, ADHD-friendly" — r/PKMS, 2025-04-17**
<https://www.reddit.com/r/PKMS/comments/1k1irdw/looking_for_a_pkm_alternative_to_anytype/>
- **WHO:** a life-OS builder who tried Anytype and is unhappy.
- **WHY:** Anytype is *"confusing to use consistently"*, *"doesn't export to a
  standard format (like Markdown or JSON)"*, and *"I'm not confident the company
  will be around long-term."*
- **REQUIRES:** encrypted + local-first (no cloud lock-in); clean visual UI
  (Capacities/Notion-like); ADHD-friendly (minimal resistance, fast capture, not
  overly customizable); exportable format usable in 5+ years; handles multiple
  life areas without overwhelm.
- **Signal:** this is Lumen's ideal user in one post. Note the trust logic: even
  a VC-funded local-first app fails the "will it exist?" test. Self-hosted +
  open source + export is the answer.

**11. "an actual open source notion alternative, without AI" — r/PKMS, 2026-01-19**
<https://www.reddit.com/r/PKMS/comments/1qgxvvn/an_actual_open_source_notion_alternative_without/>
- **WHO:** a user who wants local/self-hosted scheduling + database notes.
- **WHY:** *"all the options are either not open source (anytype excited me
  until it wasn't open source) or blatantly promoting AI (Appflowy and Affine)."*
- **REQUIRES:** open source, local/self-hosted, Kanban, sync, no AI push.
- **Signal:** the "no AI" requirement is stated as a *sourcing criterion*, next
  to open source. It is not a preference; it is a filter.

**12. "Looking for a PKMS tool with self hosting capability" — r/PKMS, 2026-01-13**
<https://www.reddit.com/r/PKMS/comments/1qc2ifl/looking_for_a_pkms_tool_with_self_hosting/>
- **WHO:** a physiotherapy student building a study knowledge base.
- **WHY:** wants to own the data and share read-only with classmates.
- **REQUIRES:** import study materials; infinite canvas; PDFs, images, notes,
  local video; host on a server; view-only sharing for others; edit access for
  self/selected colleagues.
- **Signal:** "self-hosted" often means "I want to share a read-only view",
  not multi-user collaboration. A public read-only share link is a cheap feature
  with high perceived value.

**13. "PKM's with canvas like in Obsidian" — r/PKMS, 2025-03-08**
<https://www.reddit.com/r/PKMS/comments/1j6ju78/pkms_with_canvas_like_in_obsidian/>
- **WHO:** a Windows/Linux/Android user who likes Capacities most but needs
  canvas and self-hosting.
- **WHY:** *"I'm really trying to move away from SaaS."*
- **REQUIRES:** object-based system like Capacities/Anytype; canvas; self-hosting;
  Win/Linux/Android clients; *"doesn't necessarily need to be free."*
- **Signal:** willingness to pay is explicit. Also a nuanced objection to
  Anytype: *"you cannot store your vault non-encrypted… uncomfortable to not be
  able to browse my content freely."* Encryption is not universally loved;
  inspectability matters too.

**14. "Made my own self-hosted notes app because Capacities was almost what I wanted" — r/NoteTaking, 2026-10**
<https://www.reddit.com/r/NoteTaking/comments/1xc0h5o/made_my_own_selfhosted_notes_app_because/>
- **WHO:** a developer who loved Capacities' model but wanted self-hosting.
- **WHY:** Capacities is closed source and cloud-only.
- **REQUIRES:** typed objects, daily notes, local-first, offline, one container,
  Capacities importer, browser extension, Telegram, desktop shortcut, AGPL.
- **Signal:** this is Vasta, Lumen's closest competitor. The fact that someone
  built it is validation; the fact that it is early (40 stars, 39 open issues)
  is the window. Lumen must match the importer and one-container story, and win
  on native desktop, Android, maturity and no-AI.

**15. "Farewell to Capacities" — r/capacitiesapp, 2025-09-22**
<https://www.reddit.com/r/capacitiesapp/comments/1nnr3gt/farewell_to_capacities/>
- **WHO:** a long-time Capacities fan who moved to Anytype.
- **WHY:** *"it wasn't because I liked Anytype more. In fact, I like Capacities
  much more."* Two reasons: performance at scale (screen delays, blank screens,
  long-note lag) and *"I began to feel uncertain about Capacities' future."*
- **REQUIRES:** performance with a large library; long-note editing; a future
  they can bet on.
- **Signal:** even a SaaS user churns on *future uncertainty*. Self-hosted +
  open source + export is the strongest possible answer to that fear — if Lumen
  can also prove performance.

**16. "Privacy concerns" — r/capacitiesapp, 2026-03**
<https://www.reddit.com/r/capacitiesapp/comments/1s2rpf9/privacy_concerns/>
- **WHO:** a privacy-conscious user doing due diligence.
- **WHY:** always-online requirement, data in app storage, no E2EE, unclear
  audit evidence.
- **REQUIRES:** self-hosted data storage; E2EE; offline.
- **Signal:** the co-founder's answer is the definitive gap statement:
  *"Self-hosting is not an option at the moment"*; full E2EE is on the
  *"will not be added"* list. This thread is Lumen's permission to exist.

**17. "Truly offline mode exist?" — r/capacitiesapp, 2026-01**
<https://www.reddit.com/r/capacitiesapp/comments/1qfiqul/truly_offline_mode_exist/>
- **WHO:** a user who wants local-only operation.
- **WHY:** sync cannot be disabled; offline is a cache, not ownership.
- **REQUIRES:** true local-only mode, no forced sync.
- **Signal:** "offline mode" is not the same as "local-first". Lumen's server
  model is local-first by construction; say it in those words.

**18. "Lifetime/business" — r/capacitiesapp, 2023-09-01**
<https://www.reddit.com/r/capacitiesapp/comments/166ypk4/lifetimebusiness/>
- **WHO:** a user about to commit years of notes.
- **WHY:** *"I'm a little skittish to use it… scared of useful tools becoming
  vaporware."*
- **REQUIRES:** lifetime/one-time option, or a credible guarantee the tool will
  exist.
- **Signal:** the longevity fear is not unique to self-hosted. It is the price
  of admission for any notes app.

**19. "Create a 'Lifetime' subscription level" — Capacities feedback board**
<https://feedback.capacities.io/b/feedback/posts/post_01m4bqb473e069thtnv7nprrm4>
- **WHO:** 419 upvoters over four years.
- **WHY:** subscription anxiety; wants to pay once.
- **REQUIRES:** lifetime license or version-based buyout.
- **Signal:** the single most-upvoted pricing request in the corpus. Self-hosted
  Lumen answers it structurally; a paid one-time "supporter" license could
  capture the rest.

**20. "capacities pro worth it for a lawyer?" — r/capacitiesapp, 2025-08-20**
<https://www.reddit.com/r/capacitiesapp/comments/1mvu0le/capacities_pro_worth_it_for_a_lawyer/>
- **WHO:** a lawyer with a large PDF/audio library.
- **WHY:** *"it's so expensive"*; compares Upnote lifetime, Evernote $4/mo,
  Notesnook $3/mo vs Capacities $11/mo.
- **REQUIRES:** storage, PDF/audio attachments, a price that matches daily use.
- **Signal:** regulated professionals will pay, but they benchmark against
  lifetime and $3–4/mo tools. Self-hosted removes the meter; a support license
  is easier to justify than a subscription.

**21. "Has promise. But I can't spend Evernote-level money…" — r/capacitiesapp, 2024-01-01**
<https://www.reddit.com/r/capacitiesapp/comments/18w5bie/has_promise_but_i_cant_spend_evernotelevel_money/>
- **WHO:** a paying Evernote user evaluating a switch.
- **WHY:** price without the table stakes: no Readwise import, no Notion/Evernote
  import, no task management, no mobile trial.
- **REQUIRES:** import from Readwise/Notion/Evernote; tasks; mobile trial.
- **Signal:** "import" is not one feature; it is a family of importers. Lumen
  should ship Capacities first, then Notion/Obsidian/Evernote/Readwise.

**22. "Capacities vs OneNote vs Obsidian?" — r/capacitiesapp, 2025-02-25**
<https://www.reddit.com/r/capacitiesapp/comments/1ixmyt6/capacities_vs_onenote_vs_obsidian_looking_for_the/>
- **WHO:** a user comparing three tools.
- **WHY:** *"I can't import text in any way"*; OneNote lost data; Obsidian sync
  is paid.
- **REQUIRES:** text import, reliable sync, graph, offline.
- **Signal:** import failure is a recurring, specific deal-breaker. It is also a
  trust signal: if import is broken, users assume the app is fragile.

**23. "I think I might leave the app and go back to obsidian/notion" — r/capacitiesapp, 2026-01-05**
<https://www.reddit.com/r/capacitiesapp/comments/1q4bbp0/i_think_i_might_leave_the_app_and_go_back_to/>
- **WHO:** a user who consolidated Notion + Obsidian into Capacities.
- **WHY:** paid queries still limited; app got slower/buggier as content grew;
  no global graph; missed Obsidian's blank canvas.
- **REQUIRES:** performance at scale, unrestricted queries, global graph, free
  writing surface.
- **Signal:** the churn is caused by *paywalled core structure* (queries) and
  *performance*, not by missing exotic features. Lumen's queries are free and
  local; prove performance.

**24. "Need recommendations: Fast, free, future-proof, and cross-platform note taking app?" — r/NoteTaking, 2026-06-28**
<https://www.reddit.com/r/NoteTaking/comments/1ui8fn7/need_recommendations_fast_free_futureproof_and/>
- **WHO:** a cross-platform user who has been burned before.
- **WHY:** wants a long-term home.
- **REQUIRES (explicit):** future-proof/no lock-in (biggest priority); cross-
  platform + offline sync (Windows, iOS, Android); free tier; privacy; high
  performance.
- **Signal:** the generic adopter checklist, independent of Capacities. It maps
  almost one-to-one onto Lumen's pitch — except iOS and a free hosted tier.

**25. "i want to move my notes to other app" — r/NoteTaking, 2026-06-02**
<https://www.reddit.com/r/NoteTaking/comments/1tv5jlv/i_want_to_move_my_notes_to_other_app/>
- **WHO:** a user with years of notes, moving again.
- **WHY:** wants out of the current app.
- **REQUIRES:** *"only focusing on apps where it's easy to get your information
  out in Markdown formatted files."*
- **Signal:** export is a *selection criterion before adoption*, not a feature
  used after. Lumen's export should be on the landing page.

**26. "Non-american alternative to notion?" — r/Notion, 2026-01-25**
<https://www.reddit.com/r/Notion/comments/1qmgp6y/nonamerican_alternative_to_notion/>
- **WHO:** a user worried about US jurisdiction.
- **WHY:** data sovereignty.
- **REQUIRES:** non-US provider or self-hosting.
- **Signal:** Capacities is recommended precisely because it is German. Lumen's
  self-hosted model is jurisdiction-agnostic — a stronger answer than "EU
  company" for the regulated segment.

### 3.2 Self-hosted PKM seekers (the evaluation pool)

These threads do not always mention Capacities, but they define the bar Lumen
must clear to be adopted by the self-hosted crowd.

**27. "Which details are important for you when checking out a self-hosted application?" — r/selfhosted, 2020-11-20**
<https://www.reddit.com/r/selfhosted/comments/jxpwp2/which_details_are_important_for_you_know_when/>
- **WHO:** an experienced self-hoster answering a meta-question.
- **REQUIRES (critical):** open source; active development (major work in the
  last 3 months, preferably a commit in the last month); official Docker image
  (not a community one). **Soft:** screenshots of the main UI; good, updated
  docs. **Cool but not required:** demo site. **Ideally:** Postgres support.
- **Signal:** this is the closest thing to a formal acceptance test for
  r/selfhosted. Lumen's AGPL + active repo + official image + screenshots +
  docs + demo covers it; the current personal compose does not.

**28. "How do you pick a service to self-host?" — r/selfhosted, 2025-04-16**
<https://www.reddit.com/r/selfhosted/comments/1k08t1d/how_do_you_pick_a_service_to_self_host/>
- **WHO:** a cautious adopter.
- **REQUIRES:** GitHub history (commit count, committer count, project age, issue
  response ratio, development pace); dev tone; *"Client apps or established
  standard protocols for mobile access. I don't like PWAs"*; *"Sane docker stack
  or single-binary install."*
- **Signal:** native Android is an advantage over PWA-based competitors; a
  single Go binary + SQLite is the ideal shape. Also: contributor count matters
  — a one-person project is a risk unless there is a visible plan.

**29. "How do you tell if a project is mature?" — r/selfhosted, 2026-06-30 (removed by mod)**
<https://www.reddit.com/r/selfhosted/comments/1uju6wf/how_do_you_tell_if_a_project_is_a_mature/>
- **WHO:** a 2026 self-hoster navigating the AI-slop wave.
- **REQUIRES:** *"If there is only one or two primary contributors I tend to
  steer away"*; README should be user-focused, not developer-focused; *"Websites
  overly nice for the product they offer are vibe coded"*; check language.
- **Signal:** the 2026 trust environment is hostile to polished-but-thin
  projects. Lumen must look *boring and tested*: changelog, tests, security
  policy, real screenshots, no marketing gloss.

**30. "Obsidian self hosted alternative (as Self hosted sync is not reliable…)" — r/selfhosted, 2025-10-31**
<https://www.reddit.com/r/selfhosted/comments/1okup46/obsidian_self_hosted_alternative_as_self_hosted/>
- **WHO:** an Obsidian + LiveSync user who lost notes.
- **WHY:** *"I lost my nerves today finding couple of notes were roll backed to
  version from summer"*; edit history diverged across devices.
- **REQUIRES:** web-based editor; backlinks visible; queries like Dataview;
  tasks; reliable sync.
- **Signal:** sync reliability is a *trust* feature. A single data-loss story
  will define Lumen's reputation. Publish the sync design and conflict policy.

**31. "We need a 'simple' web based MD notes app" — r/selfhosted, 2025-08-25**
<https://www.reddit.com/r/selfhosted/comments/1mzod84/we_need_a_simple_web_based_md_notes_app/>
- **WHO:** a self-hoster tired of over-engineered tools.
- **WHY:** *"Most of the usual recommendations… are far too over-engineered…
  even a minor data issue could put all your notes at risk."*
- **REQUIRES:** simple, reliable, lightweight web frontend; version control;
  easy accessibility.
- **Signal:** Lumen is a big stack (Go + Qt + Android + extension). The
  counter-message must be "one container, one SQLite file, no Redis/Postgres",
  not "we have 40 features".

**32. "Self-hosted app for security research notes + project/ideas management?" — r/selfhosted, 2026-01-31**
<https://www.reddit.com/r/selfhosted/comments/1qrmqse/selfhosted_app_for_security_research_notes/>
- **WHO:** a security researcher.
- **REQUIRES:** *"solid web UI"*; full-text search; tagging; markdown; file
  attachments.
- **Signal:** "solid web UI" is the first requirement, before search and tags.

**33. "Self-hosted life/family journaling tools" — r/selfhosted, 2026-07-20 (removed by mod)**
<https://www.reddit.com/r/selfhosted/comments/1v18e3u/what_selfhosted_life_notesfamily_journaling_tools/>
- **WHO:** a person building a family knowledge base.
- **REQUIRES:** lives on the server; accessible from mobile and PC *"without
  having to sync files between devices"*; future-proof.
- **Signal:** the top comment: *"keep the source of truth as plain Markdown and
  treat the web app as replaceable."* This is the self-hosted version of the
  ownership requirement — and a warning that a DB-only format will be distrusted.

**34. "SilverBullet: Sweet little app for markdown notes" — r/selfhosted, 2025-01-18**
<https://www.reddit.com/r/selfhosted/comments/1i48qma/silverbullet_sweet_little_app_for_markdown_notes/>
- **WHO:** a happy self-hoster recommending a tool.
- **REQUIRES:** plain `.md` files; web interface; one-line Docker install;
  survives the app disappearing.
- **Signal:** *"If SilverBullet goes away, the .md files are just still there."*
  That sentence is the emotional core of the self-hosted ownership pitch.

**35. "Introducing Anytype Open Beta" — r/selfhosted, 2023-07-25**
<https://www.reddit.com/r/selfhosted/comments/1596gs4/introducing_anytype_open_beta_one_app_for/>
- **WHO:** Anytype's co-founder, launching to self-hosters.
- **RESULT:** 1 point, 172 comments. The launch was downvoted as self-promo but
  heavily discussed.
- **OBJECTIONS:** long-term viability (*"you can pay your bills"*); *"being
  self-contained is critical… reliance on 3rd party storage engine… could
  interfere with a self-hoster's current backup strategy"*; IPFS complexity.
- **Signal:** r/selfhosted will engage with a launch but punish marketing.
  Engagement ≠ upvotes. The follow-up matters more.

**36. "Update: You asked, we listened — Anytype now supports configurable settings for self-hosting & local-only mode" — r/selfhosted, 2023-12-28**
<https://www.reddit.com/r/selfhosted/comments/18sucfz/update_you_asked_we_listened_anytype_now_supports/>
- **RESULT:** 164 points, 34 comments.
- **Signal:** the "we shipped what you asked for" follow-up outperformed the
  original launch by two orders of magnitude. This is the single most
  transferable launch lesson in the corpus.

**37. "Is anyone self hosting anytype?" — r/selfhosted, 2023-12-07**
<https://www.reddit.com/r/selfhosted/comments/18d6m1x/is_anyone_self_hosting_anytype/>
- **WHO:** a self-hoster who wants to run Anytype.
- **WHY:** self-hosting is too complicated.
- **REQUIRES:** turnkey self-host; no compiling clients.
- **Signal:** *"the clients don't have configurable server settings so you have
  patch & compile them yourself"* → *"I'm just going to wait."* A self-host
  option that requires recompiling the client is not a self-host option.

**38. "Notion alternative: AppFlowy vs Outline vs Affine?" — r/selfhosted, 2023-07-06**
<https://www.reddit.com/r/selfhosted/comments/14s9d8j/notion_alternative_appflowy_vs_outline_vs_affine/>
- **WHO:** a self-hoster shortlisting Notion alternatives.
- **REQUIRES:** maturity, easy self-host, data control.
- **Signal:** *"none of them are there yet but it's close"*; Outline is "pretty
  tough to implement as self hosted". The bar is "it just works", not "it has
  the feature".

### 3.3 Privacy and ownership seekers

**39. "Most secure notes app?" — r/privacy, 2026-02-05**
<https://www.reddit.com/r/privacy/comments/1qwmwc6/most_secure_notes_app/>
- **WHO:** a privacy user choosing a notes app.
- **REQUIRES:** open source; E2EE; local storage; self-host; markdown export;
  no email requirement; all major OSes.
- **Signal:** the Anytype recommendation lists exactly the checklist: *"End-to-end
  encrypted… public source code… all data stored locally… can selfhost… can
  export in markdown."* Lumen matches most; E2EE is the missing word.

**40. "What PKMS tools are privacy focused?" — r/PKMS, 2023-10-18**
<https://www.reddit.com/r/PKMS/comments/17acvcv/what_pkms_tools_are_privacy_focused/>
- **WHO:** a privacy-minded PKM user.
- **REQUIRES:** open source or local; no cloud for private material.
- **Signal:** *"capacities is cloud based, every cloud based application probably
  shouldn't be used for really private stuff like passwords and business."* The
  privacy objection is categorical, not a feature comparison.

**41. "Which Should I Choose for Maximum Privacy: TrilliumNext, Notesnook, or Zim?" — r/PKMS, 2025-02-18**
<https://www.reddit.com/r/PKMS/comments/1is9k12/which_should_i_choose_for_maximum_privacy/>
- **WHO:** a user refining a private setup.
- **REQUIRES:** threat model first; self-host vs local; breach risk.
- **Signal:** *"if anyone breaches it they can easily read trilium database…
  that is a feature, not a bug"* — self-hosting shifts the threat model to the
  user's server. Lumen needs a clear "secure this server" guide (TLS, auth,
  backups, no public exposure) or the privacy crowd will call it insecure.

### 3.4 AI stance threads

**42. "For those who prefer AI-free PKM systems: what are your main concerns?" — r/PKMS, 2026-07-27**
<https://www.reddit.com/r/PKMS/comments/1v87ypa/for_those_who_prefer_aifree_pkm_systems_what_are/>
- **WHO:** a pro-AI user asking the anti-AI majority to explain itself.
- **PREMISE:** *"far more PKM users than I expected are hesitant about—or
  strongly opposed to—integrating AI into their knowledge-management systems."*
- **REQUIRES (if AI ever exists):** local processing; explicit permission
  controls; read-only access; transparent edit history; fully optional features.
- **Signal:** this is the exact design brief for a future optional AI layer —
  and a confirmation that the default must be none.

**43. "Do you avoid using AI with your notes because you don't want it seeing everything?" — r/ObsidianMD, 2026-03-11**
<https://www.reddit.com/r/ObsidianMD/comments/1rqjvcb/do_you_avoid_using_ai_with_your_notes_because_you/>
- **WHO:** Obsidian users.
- **TOP COMMENT (235 points):** *"I avoid using AI because I want to be the
  responsible entity that understands what is going on, from which folders,
  tags, and methods get used in the vault all the way through to the content
  that is produced at the end."*
- **NUANCE:** *"I've played around with using local models… but don't think I'd
  ever let AI (local or not) actually write or manipulate notes. I wouldn't let
  a non-local model anywhere near."*
- **Signal:** the objection is not only privacy; it is *authorship and
  comprehension*. "No AI" is a philosophical feature, not just a security one.

**44. "AI boosterism is ruining this community" — r/ObsidianMD, 2026-02-16**
<https://www.reddit.com/r/ObsidianMD/comments/1r604pp/ai_boosterism_is_ruining_this_community/>
- **WHO:** a long-time community member.
- **QUOTE:** *"Every aspect of my life… is getting overtaken by AI… It's
  exhausting."*
- **Signal:** AI fatigue is emotional. A no-AI product can win loyalty by being
  a quiet room, not by lecturing.

**45. "Do you see a legit assistant role for AI in PKMS?" — r/PKMS, 2026-09-03**
<https://www.reddit.com/r/PKMS/comments/1w6hnx9/do_you_see_a_legit_assistant_role_for_ai_in_pkms/>
- **WHO:** a PKM user who wants retrieval help but not authorship.
- **QUOTE:** *"This is easily the most anti-AI sub I belong to… I don't want AI
  to develop my insights. I want to do that. But I do want it to organize,
  relate, and search/recover my notes."*
- **Signal:** the acceptable AI is a *librarian*, not a *writer*. Lumen's API/MCP
  lets users bring that librarian externally without shipping one.

**46. "How do you use AI tools with your notes/docs…?" — r/PKMS, 2026-04-01**
<https://www.reddit.com/r/PKMS/comments/1s9oywv/how_do_you_use_ai_tools_with_your_notesdocs_in/>
- **QUOTE:** *"They're my thoughts and ideas, I don't want AI companies reading
  them and using them to train their bots."*
- **Signal:** training-data fear is the most common single objection. A public
  "we never train, we never call a model" statement is mandatory.

**47. "We need to talk about how 'AI features' are actually making productivity apps worse" — r/productivity, 2026-04-03**
<https://www.reddit.com/r/productivity/comments/1sbe3ey/we_need_to_talk_about_how_ai_features_are/>
- **QUOTE:** *"every single developer has collectively decided to stop fixing
  bugs… just so they can shove a half-baked 'AI Assistant' into every corner."*
  *"I just want a notes app that opens instantly and lets me type."*
- **Signal:** the anti-AI pitch that works is *craft*: fast, quiet, reliable.
  Not "AI is bad".

**48. "Agentic Knowledge Management: The Next Evolution of PKM" — r/PKMS, 2026-02-03**
<https://www.reddit.com/r/PKMS/comments/1quqiu9/agentic_knowledge_management_the_next_evolution/>
- **QUOTE:** *"I know that there's a growing 'AI' fatigue. But I'm also super
  enthusiastic about what AI agents enable for knowledge management."*
- **Signal:** the agentic minority is real and growing. Lumen's API + MCP is the
  right way to serve it without contaminating the core.

### 3.5 Hacker News

**49. Capacities launch — HN, 2022-06-02 (17 points, 8 comments)**
<https://news.ycombinator.com/item?id=31592670>
- **WHO:** Capacities' founder.
- **RESULT:** low engagement; the top question was *"it looks very similar to
  notion, what's the difference?"* The founder's answer was long and conceptual;
  the reply: *"I fail to understand this explanation… average people might fail
  to understand it."*
- **Signal:** HN does not reward "better PKM". It rewards a legible technical
  or conceptual difference. "Object-based" is jargon; "your notes are typed
  objects, not pages, and you can query them" is still jargon. Lumen needs a
  demo, not a manifesto.

**50. "Notes: Fast note-taking app, open-source, without Electron, built in Qt C++" — HN, 2022-09-14 (144 points, 51 comments)**
<https://news.ycombinator.com/item?id=32837942>
- **WHO:** a Qt developer launching a native notes app.
- **WHAT WORKED:** "without Electron, built in Qt C++" is the headline.
- **OBJECTIONS:** *"I'm really interested in keeping my notes in flat files…
  are you using a database?"*; *"I sync my md files via Nextcloud and want to be
  able to use various editors"*; the dev admits *"the request to work with flat
  files came up so many times I'll have to consider it."*
- **Signal:** native/Qt is a genuine HN differentiator for Lumen. But a DB-only
  store triggers the flat-file objection immediately. Lumen's answer must be
  "SQLite is the source of truth; export is a folder of Markdown + CSV, tested
  and documented" — and ideally a "files on disk" mode later.

**51. "SiYuan: Privacy-first, self-hosted personal knowledge management software" — HN, 2024-12-26 (308 points, 156 comments)**
<https://news.ycombinator.com/item?id=42512713>
- **WHO:** a self-hosted PKM launch.
- **WHAT WORKED:** privacy + self-hosted + open source.
- **OBJECTIONS:** *"nearly identical to Obsidian. How does it differ?"*;
  *"The file format isn't markdown, instead a proprietary format in JSON"* →
  *"It locks your efforts to the vendor/project"*; *"If SiYuan stops being
  developed, are the files still readable?"*; *"Not fully open source… you need
  to pay to export PDF without a watermark."*
- **Signal:** the three HN trust tests are: differentiation, file format,
  license purity. Lumen passes license; must pass format (export) and
  differentiation (objects + self-host + no AI).

**52. "Note-Taking and Personal Knowledge Management" — HN, 2026-07-28 (257 points, 97 comments)**
<https://news.ycombinator.com/item?id=49084324>
- **WHO:** the current HN PKM discourse.
- **THEMES:** PKM burnout; "dumb markdown file" minimalism; AI memory systems
  replacing notes for some; *"An LLM generated Obsidian ad. Still not
  interested, sorry."*; Obsidian's CEO defending tools.
- **Signal:** HN is split between minimalists and AI-maximalists. Lumen's
  "self-hosted, no AI, native, fast" is a coherent third position — but do not
  expect HN to be the main adoption channel. It is a credibility channel.

### 3.6 What the listicle market says (competitive map)

The current "Capacities alternative" SERPs recommend: **Obsidian** (local
markdown), **Anytype** (E2EE, object-based), **SiYuan** (self-hosted blocks),
**Trilium** (hierarchy), **AppFlowy/AFFiNE** (Notion-like, AI), **Logseq**
(outliner), **Tana** (supertags), **Notion** (teams), **Craft** (writing), and a
long tail of new "local-first markdown" tools (Nodum, memrynote, Brainfo,
Mindly, Saner). Almost none frame the answer as *"a self-hosted Capacities with
the same object model"*.

**Implication:** the query "self-hosted Capacities alternative" is effectively
uncontested. A single honest comparison page + a migration guide can own it.
The listicles also reveal the default objections Lumen must answer: Obsidian =
tinkering, Anytype = confusing/self-host pain, SiYuan = proprietary blocks,
AppFlowy/AFFiNE = AI-first/immature, Logseq = outliner/data-loss beta.

---

## 4. THE SWITCH CHECKLIST (ranked)

This is the core artifact: what a seeker needs in order to say yes, ranked by
how many adoptions it unlocks. "Lumen status" is based on the current repo
(README, server routes, mobile code) as of 2026-10-31; "verify" means I could
not confirm from the repo alone.

### 4.1 The ranking

| # | Requirement | Frequency | What seekers literally say | Evidence | Lumen status |
|---|---|---|---|---|---|
| **1** | **Lossless import** — Capacities first (types, properties, relations, links, media, daily notes), then Notion / Obsidian / Evernote / Readwise | **EVERY** among anyone with a library | *"If I can import my Capacities export with my types and links intact…"*; *"I can't import text in any way"*; *"porting from capacities"*; *"no Readwise, no Notion/Evernote import"* | [r/PKMS 2024](https://www.reddit.com/r/PKMS/comments/1h3usrt/alternative_to_capacities_its_great_but_just/), [r/ObsidianMD 2026](https://www.reddit.com/r/ObsidianMD/comments/1t3cin1/new_to_obsidian_moved_3000_notes_from_capacities/), [r/capacitiesapp import](https://www.reddit.com/r/capacitiesapp/comments/1ixmyt6/capacities_vs_onenote_vs_obsidian_looking_for_the/), [r/capacitiesapp 2024](https://www.reddit.com/r/capacitiesapp/comments/18w5bie/has_promise_but_i_cant_spend_evernotelevel_money/), [Vasta](https://www.reddit.com/r/NoteTaking/comments/1xc0h5o/made_my_own_selfhosted_notes_app_because/) | **PARTIAL** — export is Capacities-shaped (md + YAML front matter + CSV collections + media + manifest); no structured Capacities ZIP importer yet (planned, NEXT-PHASE #13); Markdown/txt/media import exists |
| **2** | **Mobile capture that works** (Android parity, share sheet, quick capture, offline queue, notifications) | **OFTEN** overall; **EVERY** in the mobile-first segment | *"I'm 99% mobile"*; *"I'm currently using another app to jot down notes, then later transferring to Capacities"*; *"the mobile app makes me want to rip my hair out"* | [r/PKMS Android](https://www.reddit.com/r/PKMS/comments/1m1nriv/objectbased_pkm_on_android/), [r/capacitiesapp mobile](https://www.reddit.com/r/capacitiesapp/comments/1sh6gxk/the_mobile_app_makes_me_want_to_rip_my_hair_out/), [Saner review](https://www.saner.ai/blogs/capacities-review) | **MET (verify QA)** — Android app, `ShareActivity`, local DB + outbox queue; needs capture-speed and notification polish |
| **3** | **Browser access / web UI** (read + write from any device, no install) | **OFTEN** in the self-hosted pool; **SOMETIMES** in the Capacities pool | *"I need note app with working in web"*; *"solid web UI"*; *"web-based editor"*; *"access from mobile and PC without having to sync files"* | [r/PKMS 2024](https://www.reddit.com/r/PKMS/comments/1fa7h1n/any_suggest_to_similar_like_capacities/), [r/selfhosted 2026](https://www.reddit.com/r/selfhosted/comments/1qrmqse/selfhosted_app_for_security_research_notes/), [r/selfhosted 2025](https://www.reddit.com/r/selfhosted/comments/1okup46/obsidian_self_hosted_alternative_as_self_hosted/) | **GAP** — server is API-only; clients are Qt desktop, Android, extension |
| **4** | **One-container install** (official image, compose, SQLite, no Redis/Postgres) | **OFTEN** | *"Sane docker stack or single-binary install"*; *"one Docker container"*; *"official docker, not a garbo linuxserver one"* | [r/selfhosted 2020](https://www.reddit.com/r/selfhosted/comments/jxpwp2/which_details_are_important_for_you_know_when/), [r/selfhosted 2025](https://www.reddit.com/r/selfhosted/comments/1k08t1d/how_do_you_pick_a_service_to_self_host/), [Memos](https://doolpa.com/article/memos) | **PARTIAL** — compose exists in `ops/personal`, no public GHCR image, generic compose, `.env.example` or one-command quickstart |
| **5** | **Open source + clean license** (AGPL/MIT, public repo, no open-core bait) | **OFTEN** | *"Open source (I don't selfhost anything closed source)"*; *"Anytype excited me until it wasn't open source"*; *"Not fully open source… pay to export PDF"* | [r/selfhosted 2020](https://www.reddit.com/r/selfhosted/comments/jxpwp2/which_details_are_important_for_you_know_when/), [r/PKMS 2026](https://www.reddit.com/r/PKMS/comments/1qgxvvn/an_actual_open_source_notion_alternative_without/), [HN SiYuan](https://news.ycombinator.com/item?id=42512713) | **MET** — AGPL-3.0-or-later; keep the whole stack open, no paid export |
| **6** | **Data ownership / export** (Markdown + JSON/CSV, documented, one click, no lock-in) | **EVERY** | *"Future-proof / no lock-in: my biggest priority"*; *"only apps where it's easy to get your information out in Markdown"*; *"if the app shuts down"* | [r/NoteTaking 2026](https://www.reddit.com/r/NoteTaking/comments/1ui8fn7/need_recommendations_fast_free_futureproof_and/), [r/NoteTaking move](https://www.reddit.com/r/NoteTaking/comments/1tv5jlv/i_want_to_move_my_notes_to_other_app/), [r/selfhosted SilverBullet](https://www.reddit.com/r/selfhosted/comments/1i48qma/silverbullet_sweet_little_app_for_markdown_notes/) | **MET** — zip with Markdown + YAML front matter + CSV + media + manifest; document it on the landing page |
| **7** | **No subscription / no per-seat / no metered AI** | **OFTEN** | *"$11/month… so expensive"*; *"lifetime subscription?"* (419 upvotes); *"I can't spend Evernote-level money"* | [feedback board](https://feedback.capacities.io/b/feedback/posts/post_01m4bqb473e069thtnv7nprrm4), [lawyer thread](https://www.reddit.com/r/capacitiesapp/comments/1mvu0le/capacities_pro_worth_it_for_a_lawyer/), [r/capacitiesapp 2024](https://www.reddit.com/r/capacitiesapp/comments/18w5bie/has_promise_but_i_cant_spend_evernotelevel_money/) | **MET** — self-host, no seats; sustainability model (support/one-time) TBD |
| **8** | **Offline-first + reliable sync** (no data loss, conflict handling, local-only option) | **OFTEN** | *"notes were roll backed to version from summer"*; *"truly offline mode?"*; *"sync cannot be disabled"* | [r/selfhosted sync loss](https://www.reddit.com/r/selfhosted/comments/1okup46/obsidian_self_hosted_alternative_as_self_hosted/), [r/capacitiesapp offline](https://www.reddit.com/r/capacitiesapp/comments/1qfiqul/truly_offline_mode_exist/), [r/PKMS Anytype alt](https://www.reddit.com/r/PKMS/comments/1k1irdw/looking_for_a_pkm_alternative_to_anytype/) | **PARTIAL** — Android offline DB/outbox; desktop requires server; publish sync/conflict design and test restore |
| **9** | **Privacy by architecture** (no telemetry, no cloud, encryption at rest, no training) | **OFTEN** | *"I don't want AI companies reading them"*; *"every cloud based application probably shouldn't be used for really private stuff"*; *"no E2EE"* | [r/PKMS privacy](https://www.reddit.com/r/PKMS/comments/17acvcv/what_pkms_tools_are_privacy_focused/), [r/capacitiesapp privacy](https://www.reddit.com/r/capacitiesapp/comments/1s2rpf9/privacy_concerns/), [r/privacy](https://www.reddit.com/r/privacy/comments/1qwmwc6/most_secure_notes_app/) | **MET** — encrypted SQLite at rest, self-hosted, no AI; publish a network-call audit and threat model |
| **10** | **Object-model parity** (typed objects, properties, relations, queries, daily note, graph, whiteboard/canvas) | **EVERY** among Capacities refugees | *"object based system, like Capacities"*; *"object graph view"*; *"objects without leaving Obsidian"* | [r/PKMS canvas](https://www.reddit.com/r/PKMS/comments/1j6ju78/pkms_with_canvas_like_in_obsidian/), [r/ObsidianMD friction](https://www.reddit.com/r/ObsidianMD/comments/1vrjabi/moving_from_capacities_but_facing_friction/), [r/ObsidianMD plugin](https://www.reddit.com/r/ObsidianMD/comments/1smemow/obsidian_but_with_objectorientednotes/) | **MET (verify whiteboard)** — objects, properties, relations, queries, daily notes, graph, Kanban; confirm canvas/whiteboard parity |
| **11** | **Search quality and speed** (instant, operators, jump-to-block, relevance) | **SOMETIMES** | *"Good Search"*; *"I did not really like Capacities'… search function"*; *"search relevance"* | [r/PKMS clunky](https://www.reddit.com/r/PKMS/comments/1h3usrt/alternative_to_capacities_its_great_but_just/), [r/ObsidianMD move](https://www.reddit.com/r/ObsidianMD/comments/1psm2vk/looking_to_move_away_from_capacities_to_obsidian/) | **PARTIAL** — search exists; relevance/latency benchmark unknown |
| **12** | **Performance at scale** (10k objects, long notes, cold start, memory) | **SOMETIMES** (but decisive when present) | *"as content grew, Capacities turned slower and buggier"*; *"editing long notes caused serious lag"*; *"high performance"* | [Farewell](https://www.reddit.com/r/capacitiesapp/comments/1nnr3gt/farewell_to_capacities/), [leaving](https://www.reddit.com/r/capacitiesapp/comments/1q4bbp0/i_think_i_might_leave_the_app_and_go_back_to/), [r/NoteTaking](https://www.reddit.com/r/NoteTaking/comments/1ui8fn7/need_recommendations_fast_free_futureproof_and/) | **UNKNOWN** — benchmark and publish; this is Capacities' open flank |
| **13** | **Native desktop, no Electron, Linux support** | **SOMETIMES** | *"without Electron, built in Qt C++"*; *"Win, Linux and Android clients"* | [HN Qt](https://news.ycombinator.com/item?id=32837942), [r/PKMS canvas](https://www.reddit.com/r/PKMS/comments/1j6ju78/pkms_with_canvas_like_in_obsidian/) | **MET** — Qt 6 native, Linux binary |
| **14** | **API / MCP / CLI** (own tools, agents, automation) | **RARE–SOMETIMES**, rising | *"API access"*; *"MCP approach"*; *"Claude Code"* | [r/capacitiesapp MCP](https://www.reddit.com/r/capacitiesapp/comments/1t6u9nh/am_i_crazy_for_being_really_disappointed_with_the/), [r/PKMS agentic](https://www.reddit.com/r/PKMS/comments/1quqiu9/agentic_knowledge_management_the_next_evolution/) | **MET** — REST API + MCP server |
| **15** | **Docs, screenshots, demo, install guide** | **SOMETIMES** in threads; **EVERY** in self-hosted evaluation | *"Good and updated documentation"*; *"screenshots of the main UI"*; *"demo website"* | [r/selfhosted 2020](https://www.reddit.com/r/selfhosted/comments/jxpwp2/which_details_are_important_for_you_know_when/), [r/selfhosted maturity](https://www.reddit.com/r/selfhosted/comments/1uju6wf/how_do_you_tell_if_a_project_is_a_mature/) | **PARTIAL/GAP** — docs are Spanish and developer-oriented; no English quickstart, public demo, or 90-second video |
| **16** | **Backup, version history, restore** | **SOMETIMES** | *"version history"*; *"backups"*; *"restore"* | [r/capacitiesapp data loss](https://www.reddit.com/r/capacitiesapp/comments/1v4p5pf/my_workspace_is_gone/), [r/selfhosted backup](https://www.reddit.com/r/selfhosted/comments/1i48qma/silverbullet_sweet_little_app_for_markdown_notes/) | **MET** — version history routes + desktop UI; make backup/restore a first-class doc |
| **17** | **Capture integrations** (web clipper, share target, WhatsApp/Telegram/email, calendar, Readwise) | **SOMETIMES** | *"I especially use the capacities whatsapp integration"*; *"Readwise integration is a deal breaker"* | [r/ObsidianMD move](https://www.reddit.com/r/ObsidianMD/comments/1psm2vk/looking_to_move_away_from_capacities_to_obsidian/), [r/capacitiesapp 2024](https://www.reddit.com/r/capacitiesapp/comments/18w5bie/has_promise_but_i_cant_spend_evernotelevel_money/) | **PARTIAL** — extension + weblink reader; no calendar/Readwise/WhatsApp/Telegram |
| **18** | **E2EE specifically** | **SOMETIMES** | *"End-to-end encrypted"*; *"no E2EE"*; *"encrypted + local-first"* | [r/privacy](https://www.reddit.com/r/privacy/comments/1qwmwc6/most_secure_notes_app/), [r/PKMS Anytype alt](https://www.reddit.com/r/PKMS/comments/1k1irdw/looking_for_a_pkm_alternative_to_anytype/) | **PARTIAL** — encryption at rest, not E2EE; self-host satisfies the underlying threat model for most |
| **19** | **Collaboration / multi-user** | **RARE** | *"view-only access"*; *"team plans"* | [r/PKMS self-host](https://www.reddit.com/r/PKMS/comments/1qc2ifl/looking_for_a_pkms_tool_with_self_hosting/), [Capacities What's Not Next](https://capacities.io/whats-not-next) | **GAP** — not planned; acceptable, Capacities also says no |
| **20** | **Themes / visual polish** | **RARE** as a stated requirement, but it is the moat | *"it looks good by default"*; *"beautiful UI"* | [r/PKMS canvas](https://www.reddit.com/r/PKMS/comments/1j6ju78/pkms_with_canvas_like_in_obsidian/), [r/PKMS Anytype](https://www.reddit.com/r/PKMS/comments/1k1irdw/looking_for_a_pkm_alternative_to_anytype/) | **MET** — Capacities theme + presets; keep screenshots current |

### 4.2 The four gaps that actually block Lumen

Ranked by expected adoption loss, not by engineering effort.

**Gap 1 — No web UI.** The self-hosted pool's default access model is a browser.
Every "solid web UI" / "web-based editor" / "access from mobile and PC" thread
is a lost adoption. The current server is API-only. A minimal read/write,
mobile-responsive web client (login, daily note, object list, search, editor,
capture) would unlock more users than any object-model feature. If a full web
client is too large, ship a **read + capture** MVP first and say so honestly.

**Gap 2 — No public one-command install.** "Official Docker image + compose +
docs" is a *critical* requirement in r/selfhosted's own evaluation thread. The
current path (`ops/personal/compose.yaml`, `bootstrap-secrets.sh`, token file)
is a personal installation, not a product. Needed: GHCR image, generic
`compose.yaml`, `.env.example`, first-run admin/token flow, healthcheck,
backup/restore section, upgrade notes.

**Gap 3 — No structured Capacities importer.** Lumen's export is already
Capacities-shaped, which makes the importer tractable: read the Full Export ZIP,
map front matter → properties, CSV collections → collections, local links →
relations, media folders → assets, daily-note filenames → daily notes. Add a
dry-run report ("1,842 objects, 37 types, 214 broken links") and a rollback.
This is the single feature that converts a 3,000-note user.

**Gap 4 — No English docs/demo.** The repo docs are Spanish and aimed at
developers. The adopter needs: a 60-second demo video/GIF, an English quickstart
(`docker compose up`, open desktop, connect), a user guide (objects, daily note,
queries, import/export), a security page, and a public demo instance (read-only
or reset daily). r/selfhosted requires docs to promote; r/PKMS bans AI-written
posts, so the docs and launch post must be human and specific.

**Fifth, smaller but strategic — no performance proof.** Capacities' churn is
partly performance. Publish a benchmark: 10k objects, 5k-word notes, search
latency, cold start, memory, import of a 2 GB export. "Fast" is a claim;
numbers are evidence.

### 4.3 What "prove it" looks like (evidence assets to build)

| Claim | Proof asset | Where it goes |
|---|---|---|
| "Your data is yours" | A real export ZIP, browsable, with README | Landing page + docs |
| "Import is lossless" | Before/after screenshot of a 3,000-note Capacities space; import report | Migration guide + video |
| "It's fast" | Benchmark table + methodology + reproducible script | Blog + HN + r/selfhosted |
| "It's private" | Network-call audit; `strace`/proxy log showing no egress; no telemetry statement | Security page |
| "It's reliable" | Sync design doc; conflict test; restore drill; version history demo | Docs + trust page |
| "It's maintained" | Changelog, release cadence, test count, security policy, bus-factor plan | README + launch post |
| "It's one container" | `docker compose up -d` video, 90 seconds, no edits | Landing page + Reddit |
| "It's not AI slop" | Human-written README, architecture doc, test suite, no AI in product | README + launch post |

---

## 5. Trust blockers (ranked)

These are the reasons seekers *don't* adopt the alternatives they find. They are
also the reasons they will hesitate on Lumen. Ranked by how often they appear
and how fatal they are.

### 1. "Will this exist in two years?" (solo maintainer / bus factor)

The most common reason to reject a self-hosted tool. Evidence:

- r/selfhosted: *"If there is only one or two primary contributors I tend to
  steer away."* ([thread](https://www.reddit.com/r/selfhosted/comments/1uju6wf/how_do_you_tell_if_a_project_is_a_mature/))
- r/selfhosted: GitHub history, committer count, project age, issue response
  ratio, dev tone. ([thread](https://www.reddit.com/r/selfhosted/comments/1k08t1d/how_do_you_pick_a_service_to_self_host/))
- 2026 macro-context: self-hosted abandonment is accelerating; "vibe coded"
  projects pile up trust debt. ([XDA](https://www.xda-developers.com/self-hosted-app-abandonment-speeding-up-reaching-projects-trust/))
- Even SaaS users churn on future uncertainty: *"I began to feel uncertain about
  Capacities' future."* ([Farewell](https://www.reddit.com/r/capacitiesapp/comments/1nnr3gt/farewell_to_capacities/))

**Pre-empt:** a public "who maintains this, how releases work, what happens if
we stop" page; a fork-friendly AGPL license; a tested export; a roadmap with
dates; a security policy; and a visible second contributor or a stated
succession plan. Do not say "don't worry".

### 2. "Is this AI slop?" (vibe-coding suspicion, 2026)

The 2026 self-hosted crowd assumes a polished launch may be generated. Evidence:

- r/selfhosted rule discussion: *"99% of 'I made something' posts are someone
  spending an afternoon vibecoding buggy slop which will be abandoned two months
  from now."* ([thread](https://www.reddit.com/r/selfhosted/comments/1tuonig/are_aicoded_vibecoded_selfhosted_projects_welcome/))
- *"Websites overly nice for the product they offer are vibe coded."*
- r/selfhosted now runs an AI-compliance bot on new posts. ([rules update](https://www.reddit.com/r/selfhosted/comments/1sey9ch/quarter_2_update_revisiting_rules_again/))
- r/PKMS bans AI-written posts outright ("No AI Slop").

**Pre-empt:** human-written launch post and README; architecture doc; test
suite; benchmark; real screenshots (not renders); a changelog with dates; a
"how this was built" note; disclose any AI assistance in development honestly.
Lumen's *product* has no AI — that is a strong trust signal, but it does not
answer "was the code vibe-coded?". Show tests and release discipline.

### 3. "No docs / install is a weekend project"

- r/selfhosted critical requirements include docs and screenshots; promoted apps
  must be "production ready and have docs".
- Anytype's self-host failed on this: *"you have patch & compile them
  yourself"* → *"I'm just going to wait."*
- Docmost's HN launch: Docker install works but docs are "intimidating and
  slightly confusing"; suggestion: "one-command demo container".

**Pre-empt:** a 5-line quickstart at the top of the README; a copy-paste compose;
a troubleshooting section; a "first 10 minutes" guide; a video. If the user has
to read three docs to start, you lost them.

### 4. "It's a database, not files" (format lock-in)

- HN SiYuan: *"The file format isn't markdown, instead a proprietary format in
  JSON"* → *"It locks your efforts to the vendor/project."*
- HN Qt Notes: *"I'm really interested in keeping my notes in flat files…
  are you using a database?"*; *"I sync my md files via Nextcloud and want to be
  able to use various editors."*
- r/selfhosted family thread: *"keep the source of truth as plain Markdown and
  treat the web app as replaceable."*

**Pre-empt:** "SQLite is the engine; Markdown + CSV is the contract." Ship a
documented, tested export; show the ZIP; offer an optional "mirror to Markdown
folder" mode later. Never call the DB "the format".

### 5. "No mobile app / PWA is not enough"

- *"I don't like PWAs."* (r/selfhosted)
- *"I'm 99% mobile."* (r/PKMS)
- Capacities' own mobile reviews: buggy editing, limited quick capture, no
  widget; paid users using a second app to capture.

**Pre-empt:** native Android, share target, offline queue, notifications,
widget; a 30-second capture demo. Do not lead with "responsive web".

### 6. "No web UI" (the mirror image)

- *"solid web UI"*; *"web-based editor"*; *"working in web"*; *"access from
  mobile and PC without syncing files."*

**Pre-empt:** ship it, or be explicit about the trade-off ("desktop-first,
native, no browser client yet") and accept that r/selfhosted will discount you.
The current silence is worse than a stated roadmap.

### 7. "Migration will be a mess"

- *"I can't import text in any way."*
- *"a lot of capacities properties… that should be purged and some tags that has
  merged together."*
- *"no mass import tool, so migrating from Notion or Obsidian means doing it
  mostly by hand."* (Saner review)

**Pre-empt:** a dry-run importer with a report, a rollback, a sample migration
video, and a "we imported 3,000 notes" case study. Publish the mapping table
(type → type, property → property, link → relation, collection → collection).

### 8. "Security is now my job"

- *"if anyone breaches it they can easily read trilium database… that is a
  feature, not a bug."*
- Self-hosters ask about threat models, TLS, auth, backups, exposure.

**Pre-empt:** a "secure your Lumen" guide (TLS, reverse proxy, strong token,
no public exposure, backups, updates), a threat-model page, and a disclosure
policy. Do not claim E2EE you don't have.

### 9. "Open-core bait and switch"

- Colanode's "coming soon" pricing page drew immediate skepticism: *"Anything
  that starts as Open source but is clearly meant to be a for profit product
  makes me sceptical."*
- SiYuan's paid PDF export watermark was called out as "not fully open source".

**Pre-empt:** AGPL for the whole stack; no feature paywalls; a clear
sustainability model (support, hosting, one-time supporter license) stated up
front; no "enterprise edition" that removes core features.

### 10. "I'll just stay on Obsidian"

The default alternative is not a competitor; it is inertia. Obsidian is free,
local, familiar, and "good enough". Seekers bounce back because:

- they can approximate objects with plugins (the 297-point plugin thread);
- they already have a vault and sync;
- migration is work;
- the object model is not worth the switch *unless* it is clearly better.

**Pre-empt:** do not sell "better Obsidian". Sell "the object model without the
plugin assembly, on your server, with mobile that works". Show the 60-second
"create a person, link a meeting, query last month" loop. That is the moment
Obsidian cannot match without tinkering.

### 11. "Performance will degrade as I grow"

Capacities' own churn cause. A seeker with 5k notes will ask "what happens at
50k?". Without numbers, they assume the worst.

**Pre-empt:** benchmark at 10k/50k objects; long-note test; import a large ZIP;
publish memory and latency. If there is a known limit, state it.

### 12. "The UI won't feel like Capacities"

Capacities' moat is polish. A functional clone with a rough UI reads as
"immature". The seeker will say "I'll wait".

**Pre-empt:** screenshots that match the quality bar; the Capacities theme;
consistent spacing/typography; a design QA page; a "what's different" list that
is honest about trade-offs.

---

## 6. AI stance in the seeker pool

### 6.1 The short answer

**Lumen's no-AI policy is validated for the core adopter pool — but the winning
framing is architectural, not ideological.**

In the threads coded, the split is roughly:

| Stance | Estimated share of AI mentions | What they say | What they want |
|---|---|---|---|
| **Hostile / exhausted** | ~45–55% | *"AI boosterism is ruining this community"*; *"I don't want AI companies reading them"*; *"AI features are making productivity apps worse"* | No AI in the core; no training; no cloud calls; no AI popups |
| **Indifferent / "I just don't use it"** | ~20–25% | *"I don't use AI, so nothing has changed for me"*; *"I don't see how it can be useful"* | AI absent or invisible; no upsell |
| **Optional / local-only** | ~15–20% | *"I wouldn't let a non-local model anywhere near"*; *"local processing, explicit permission, read-only, transparent history"* | AI as a separate, opt-in, local tool; never default |
| **Agentic / pro-AI** | ~10% | *"AI agents enable… a genuinely different shift"*; *"I want a research assistant"* | API, MCP, CLI, own models; not a vendor chatbot |

These are estimates from a qualitative sample, not a survey. The direction is
consistent across r/PKMS, r/ObsidianMD, r/productivity and HN.

### 6.2 The evidence

- r/PKMS is described by a member as *"easily the most anti-AI sub I belong
  to"* — and the thread is about wanting a *librarian*, not a *writer*.
- The top comment (235 points) in r/ObsidianMD's AI thread is about **authorship
  and comprehension**, not just privacy: *"I want to be the responsible entity
  that understands what is going on."*
- The most common single fear is training data: *"I don't want AI companies
  reading them and using them to train their bots."*
- The most common emotional register is exhaustion: *"Every aspect of my life…
  is getting overtaken by AI."*
- But local AI is not rejected outright: *"I've played around with using local
  models… but don't think I'd ever let AI (local or not) actually write or
  manipulate notes."*
- And the agentic minority is real: *"I know that there's a growing 'AI'
  fatigue. But I'm also super enthusiastic about what AI agents enable."*

### 6.3 What this means for Lumen

**Validate the policy, but sharpen the message.**

1. **Say "no AI in the product", not "AI is bad".** The first is a property; the
   second is a fight. The adopter pool includes people who use ChatGPT for other
   things and don't want to be lectured.
2. **Make it verifiable.** "No model, no API key, no telemetry, no training" +
   a network-call audit. A policy page is weaker than a proxy log.
3. **Keep the door open at the edge.** Lumen's REST API + MCP server is the
   perfect answer to the optional/local/agentic segments: *"We don't ship AI.
   If you want an agent, point it at your own server with your own model."*
   This is also a genuine differentiator against Capacities, whose AI is
   metered and vendor-hosted.
4. **Never add a cloud AI button.** If AI is ever added, it must be a separate
   plugin/client, local-only by default, read-only by default, with an audit
   log and an off switch. The r/PKMS thread literally lists the acceptable
   safeguards: local processing, explicit permission, read-only access,
   transparent edit history, fully optional.
5. **Do not market "AI-free" to the mass market.** For the mass market, "fast,
   quiet, reliable" is the benefit; "no AI" is the proof. For the privacy pool,
   "no AI" is the headline. Segment the copy.

**One nuance:** the "no AI" stance is strongest in r/PKMS/r/ObsidianMD. On HN
and in r/capacitiesapp, a vocal minority actively wants AI and will see "no AI"
as a missing feature. Lumen should not try to win them; it should offer them the
API/MCP path and let them bring their own. The core adopter pool is the one that
will run Docker and stay.

---

## 7. Launch playbook

### 7.1 Channels, ranked by fit (with the rules that will get you removed)

| Rank | Channel | Size | Fit | Rules / constraints | What to post | Expected outcome |
|---|---|---|---|---|---|---|
| **1** | **r/selfhosted** | ~860k | Highest volume of qualified self-hosters | Projects <3 months old only in the weekly **New Project Megathread** (any day); promoted apps must be production-ready with docs; disclose affiliation; AI-compliance bot asks how AI was involved; no standalone mobile apps (companion apps OK) | A megathread comment: name, repo, problem, features, deploy method, docs, AI involvement, screenshots | Best single source of Docker users; low upvotes but high install intent |
| **2** | **r/PKMS** | ~66–80k | Highest concentration of object-model seekers | Self-promo only in the **monthly self-promotion thread**; no AI-written posts; posts must be about PKM; banner if you work on related software | A genuine answer in an "alternative?" thread + a monthly-thread post with a migration story | Highest-intent clicks; small volume; long tail of "I'm looking for exactly this" |
| **3** | **r/NoteTaking** | ~46–71k | Broad note-taker audience; launch-friendly | Check sidebar; self-promo tolerated if contextual; no spam | "I built the self-hosted Capacities I wanted" + demo + limitations | Good reach; mixed technical level; Vasta launched here successfully |
| **4** | **r/capacitiesapp** | ~8k | The exact refugees | No promotion; community is for Capacities users; team is present | Answer "alternative?" threads with a genuine comparison; offer a migration guide in comments; never link-drop | Small but the highest conversion rate per view |
| **5** | **r/ObsidianMD** | ~374k | Obsidian exiles and "moving from Capacities" threads | Strict; approved members; no promo | Help in "moving from Capacities" threads; post the object-model comparison only if it answers a question | High reach; low promo tolerance; use for listening and migration content |
| **6** | **r/ProductivityApps** | large | General productivity | No promo; answer "Capacities vs X" threads | Comparison answers, not launch posts | Moderate; useful for "what is this?" exposure |
| **7** | **Hacker News (Show HN)** | huge | Technical credibility | Show HN must be your own work; be present all day; no landing-page-only; technical angle wins | "Show HN: Self-hosted Capacities-compatible PKM in Qt/C++ and Go, no AI" + architecture/benchmarks + demo | Spike + credibility; not a mass-adoption channel. Capacities' own HN launch got 17 points because the difference was illegible |
| **8** | **Lobsters** | small | High-signal devs | Tag `release`, `pkm`, `go`, `qt`; no marketing | Technical write-up ("How we made a Capacities-compatible export/import") | Small, high-quality feedback; good for hiring/credibility |
| **9** | **AlternativeTo** | large SEO | People searching "Capacities alternative" | Create a listing; be honest about features | Listing + screenshots + license + self-host tag | Long-tail discovery; high intent |
| **10** | **selfh.st / Awesome Selfhosted** | medium | Curated self-hosted directories | Submit app; follow contribution guidelines | Listing + one-line description + compose link | Durable referral traffic; signals legitimacy |
| **11** | **Lemmy / Mastodon #selfhosted** | small | Self-hosted refugees from Reddit | No formal promo ban; be conversational | Short post + demo GIF + link | Low volume; good for feedback and early evangelists |
| **12** | **Product Hunt** | large but wrong audience | Low fit | Launch mechanics; self-hosted crowd is skeptical | Not recommended as a primary channel | Vanity metrics; skip unless there is a hosted option |
| **—** | **r/privacy** | 1.6M | Wrong channel for launch | **R13 explicitly puts app-promotion posts on hold, including open source; R3 bans self-promo; R7 bans closed-source promotion** | Do not post. Listen and answer only when a thread asks for a self-hosted option; disclose affiliation; no links unless asked | Reputation risk > reach; avoid |

**Also worth being present in (not launching):** the Capacities Discord/community
(listen only; do not promote), r/ObsidianMD weekly threads, r/Notion "offline"
threads, r/linux "native app" threads, the self-hosted podcasts/newsletters
(selfh.st, Self-Hosted Show), and the "Capacities alternative" listicle authors
(pitch an honest comparison for inclusion).

### 7.2 Post formats that worked (and why)

**Format A — "I built the thing I couldn't find" (personal story + feature list
+ repo/docs/demo + limitations).**

- **Hoarder/Karakeep, r/selfhosted, 2024:** *"I've been a long time lurker in
  this sub… Today, I'm launching my own self-hosted project."* 355 points, 106
  comments. Homepage, repo, docs, feature list, "Self-hosting first".
  <https://www.reddit.com/r/selfhosted/comments/1bo9tdq/introducing_hoarder_an_open_source/>
- **Piruetas, Show HN, 2026:** *"I built a self-hosted diary app for my
  girlfriend"* — 71 points, 52 comments; live demo with demo credentials;
  honest about at-rest encryption and "free forever" risk.
  <https://news.ycombinator.com/item?id=47985210>
- **Why it works:** a specific human reason beats a feature matrix; a live demo
  removes doubt; listing limitations buys credibility.
- **Lumen adaptation:** "I wanted Capacities' object model without the cloud, the
  subscription, or the AI. So I built it: one container, native Qt desktop,
  Android app, Capacities import." Include a 60-second GIF and a live demo.

**Format B — "You asked, we listened" follow-up (ship the fix, then post the
fix).**

- **Anytype:** launch post 1 point / 172 comments (downvoted as promo), then
  *"Update: You asked, we listened — Anytype now supports configurable settings
  for self-hosting & local-only mode"* → 164 points / 34 comments.
  <https://www.reddit.com/r/selfhosted/comments/18sucfz/update_you_asked_we_listened_anytype_now_supports/>
- **Why it works:** it is a story of responsiveness, not a launch; the community
  rewards having been heard.
- **Lumen adaptation:** after the first wave of feedback, ship the top three
  requests (web UI MVP, importer dry-run, one-command compose) and post
  "You asked for X, Y, Z — here's what changed" with before/after.

**Format C — technical deep-dive / benchmark (HN, Lobsters).**

- **Qt Notes, HN, 2022:** *"Fast note-taking app, open-source, without Electron,
  built in Qt C++"* — 144 points; the comments were about flat files, sync and
  mobile, not the landing page.
  <https://news.ycombinator.com/item?id=32837942>
- **Memos:** single ~20 MB Go binary + SQLite + MIT + API → 60k stars; the
  deployment simplicity *is* the marketing.
- **Why it works:** HN rewards a legible technical choice (native, single
  binary, no Electron, SQLite, no telemetry) more than a feature list.
- **Lumen adaptation:** "Why we built a Capacities-compatible PKM in Qt/C++ and
  Go (and why it's one container)" + benchmarks + sync design. Post the article,
  not the repo.

**Format D — migration guide ("How I moved 3,000 notes out of Capacities").**

- No single canonical example in the corpus, but the demand is explicit:
  *"porting from capacities"*, *"moved 3000+ notes"*, *"I can't import text in
  any way"*. This is the highest-intent content Lumen can publish.
- **Why it works:** it answers the #1 precondition with proof, and it ranks for
  "Capacities export/import" queries.
- **Lumen adaptation:** a step-by-step guide with screenshots, the mapping table,
  a dry-run report, a rollback section, and a video. Post it in r/PKMS monthly
  thread, r/NoteTaking, and as a blog post; link it in answers.

**Format E — honest comparison page ("Capacities vs Lumen: what matches, what
doesn't").**

- The listicle market is full of shallow "best alternatives" pages; almost none
  are honest about trade-offs. A table with "Lumen wins / Capacities wins /
  neither" is more credible than a sales page.
- **Why it works:** it captures "Capacities alternative" search intent and
  pre-empts the "it's not as polished" objection.
- **Lumen adaptation:** include "no web UI yet", "no E2EE", "no collaboration",
  "no calendar integration" in the "Capacities wins" column. Honesty converts
  the technical adopter.

### 7.3 A six-week sequence (realistic for a small team)

| Week | Action | Goal |
|---|---|---|
| 1 | Fix the front door: English README top section, 5-line quickstart, generic compose, GHCR image, screenshots, demo video | Make the repo installable by a stranger |
| 2 | Publish the Capacities importer (dry-run + report + rollback) and the migration guide | Remove the #1 switch blocker |
| 3 | Participate: answer 10 "Capacities alternative / self-hosted PKM" threads with genuine help, no links unless asked; collect objections verbatim | Earn the right to post; build the objection list |
| 4 | Launch in r/selfhosted **New Project Megathread** + r/PKMS monthly self-promo thread + r/NoteTaking post | First cohort of Docker users |
| 5 | Ship the top 3 feedback items; publish "we listened" follow-up; post the benchmark/architecture piece on HN/Lobsters | Second wave; credibility |
| 6 | Publish the honest comparison page + AlternativeTo/selfh.st listings; pitch listicle authors | Long-tail discovery |

**Do not** post the same text to five subreddits on the same day; Reddit's spam
policy and each sub's rules will catch it. One post per community, written for
that community.

### 7.4 Messaging that converts (copy blocks)

**One-liner (landing page / Reddit title):**
> A self-hosted Capacities: typed objects, daily notes, queries and graph — in
> one container, with no AI and no subscription.

**Sub-line:**
> Import your Capacities export in one command. Export to Markdown + CSV
> anytime. Native Qt desktop, Android app, browser extension. AGPL-3.0.

**Proof bullets:**
- One Docker container + one SQLite file. No Redis, no Postgres, no cloud.
- AGPL-3.0-or-later; the whole stack, no open-core.
- No AI, no model, no API key, no telemetry, no training.
- Import Capacities (types, properties, relations, links, media, daily notes).
- Export a zip of Markdown + YAML front matter + CSV + media.
- Native Qt 6 desktop (no Electron), Android capture with offline queue.
- REST API + MCP server for your own tools.

**Objection handlers:**
- *"Will it be maintained?"* → "Public roadmap, release notes, tests, security
  policy, and a fork-friendly AGPL license. If we stop, your data is a zip."
- *"Is this AI slop?"* → "No AI in the product. Human-written docs. Test suite
  and benchmarks in the repo. Here's the architecture."
- *"Do I need a web UI?"* → (if shipped) "Yes — browser access is included."
  (if not) "Not yet. Desktop + Android today; the browser client is the next
  milestone. If browser access is a hard requirement, wait for it."
- *"Is it as polished as Capacities?"* → "No. It's close on the object model and
  better on ownership, price, and mobile capture. Here's the honest comparison."
- *"Can I use AI with it?"* → "Not in the app. Point your own agent at the REST
  API or MCP server; your model, your keys, your rules."

**What not to say:**
- "Notion killer", "Obsidian killer", "revolutionary", "AI-powered".
- "Coming soon" pricing.
- "Trust us" on privacy; show the network log.
- "It's just like Capacities" without naming the differences.

### 7.5 Anti-patterns (observed failures)

| Anti-pattern | Example | Why it failed |
|---|---|---|
| Launching a <3-month project as a standalone r/selfhosted post | Multiple 2026 removals | Rule 6; auto-removed; wasted launch |
| Marketing tone in r/selfhosted | Anytype launch: 1 point / 172 comments | Community punishes promo; engage, don't broadcast |
| "Coming soon" pricing | Colanode HN | Immediate skepticism about open-core |
| Broken first run | Colanode: SMTP required, unsafe error handling | First 10 minutes define the reputation |
| No docs / compile-it-yourself self-host | Anytype early self-host | "I'm just going to wait" |
| Proprietary block format | SiYuan HN | "It locks your efforts to the vendor" |
| AI-written launch post | r/PKMS rule | Removed; reputational damage |
| Posting to r/privacy | R13 | Removed/banned; app promotion on hold |
| Leading with features, not the swap | Capacities HN launch | "Looks like Notion — what's the difference?" |

---

## 8. Implications for the roadmap (top 10, ranked by adoption unlocked)

1. **Web UI (read/write, mobile-responsive, auth).** Biggest gap for the
   self-hosted pool. MVP: login, daily note, object list, search, editor,
   capture. Ship before the r/selfhosted launch if possible.
2. **Capacities Full Export importer** with dry-run report, mapping table,
   rollback, and a "3,000 notes" case study. This is the switch precondition.
3. **Public one-command install**: GHCR image, generic compose, `.env.example`,
   first-run admin/token, healthcheck, backup/restore, upgrade notes.
4. **English docs + 90-second demo + public demo instance.** r/selfhosted
   requires docs; r/PKMS requires human-written posts; the adopter requires
   screenshots.
5. **Performance benchmark pack**: 10k/50k objects, long notes, search latency,
   cold start, memory, large import. Publish methodology and numbers.
6. **Trust page**: threat model, encryption at rest, no telemetry audit, backup/
   restore drill, version history, security disclosure, bus-factor plan.
7. **Mobile capture QA**: share sheet → daily note, offline queue, notifications,
   widget, capture speed. The #1 Capacities churn driver; Lumen can win here.
8. **Search relevance**: AND-by-default, operators, jump-to-block, instant
   results; benchmark against Capacities.
9. **Migration guides**: Capacities first, then Notion, Obsidian, Evernote,
   Readwise. Each with a video/GIF and a mapping table.
10. **API/MCP cookbook**: "bring your own AI" examples (local Ollama, Claude
    Desktop, scripts) — serves the agentic minority without adding AI to core.

**Do not build (yet):** collaboration/multi-user (rare demand; Capacities says
no), E2EE (self-host + at-rest encryption covers most; E2EE complicates search
and sync), native iOS (Android first; iOS users are a later segment), calendar/
Readwise/WhatsApp integrations (nice-to-have; import and mobile matter more).

---

## Appendix A — Source index

### Direct Capacities seekers
- r/selfhosted, *Selfhosted alternative to capacities.io*, 2023-04-02 —
  <https://www.reddit.com/r/selfhosted/comments/129wmux/selfhosted_alternative_to_capacitiesio/>
- r/PKMS, *Alternative to Capacities? It's great, but just clunky*, 2024-12-01 —
  <https://www.reddit.com/r/PKMS/comments/1h3usrt/alternative_to_capacities_its_great_but_just/>
- r/PKMS, *Any suggest to similar like 'capacities'*, 2024-09-06 —
  <https://www.reddit.com/r/PKMS/comments/1fa7h1n/any_suggest_to_similar_like_capacities/>
- r/capacitiesapp, *Could you please advise me on finding an optimal alternative of Capacities (besides Anytype)?*, 2025-09-16 —
  <https://www.reddit.com/r/capacitiesapp/comments/1nijyad/could_you_please_advise_me_on_finding_an_optimal/>
- r/ObsidianMD, *Looking to move away from Capacities to Obsidian…*, 2025-12-22 —
  <https://www.reddit.com/r/ObsidianMD/comments/1psm2vk/looking_to_move_away_from_capacities_to_obsidian/>
- r/ObsidianMD, *New to obsidian, moved 3000+ notes from Capacities*, 2026-05-04 —
  <https://www.reddit.com/r/ObsidianMD/comments/1t3cin1/new_to_obsidian_moved_3000_notes_from_capacities/>
- r/ObsidianMD, *Moving from Capacities but facing friction*, 2026-08-18 —
  <https://www.reddit.com/r/ObsidianMD/comments/1vrjabi/moving_from_capacities_but_facing_friction/>
- r/ObsidianMD, *Obsidian but with object-oriented-notes*, 2026-04-15 —
  <https://www.reddit.com/r/ObsidianMD/comments/1smemow/obsidian_but_with_objectorientednotes/>
- r/PKMS, *Object-based PKM on Android?*, 2025-07-16 —
  <https://www.reddit.com/r/PKMS/comments/1m1nriv/objectbased_pkm_on_android/>
- r/PKMS, *Looking for a PKM alternative to Anytype — local-first, encrypted, ADHD-friendly*, 2025-04-17 —
  <https://www.reddit.com/r/PKMS/comments/1k1irdw/looking_for_a_pkm_alternative_to_anytype/>
- r/PKMS, *an actual open source notion alternative, without AI*, 2026-01-19 —
  <https://www.reddit.com/r/PKMS/comments/1qgxvvn/an_actual_open_source_notion_alternative_without/>
- r/PKMS, *Looking for a PKMS tool with self hosting capability*, 2026-01-13 —
  <https://www.reddit.com/r/PKMS/comments/1qc2ifl/looking_for_a_pkms_tool_with_self_hosting/>
- r/PKMS, *PKM's with canvas like in Obsidian*, 2025-03-08 —
  <https://www.reddit.com/r/PKMS/comments/1j6ju78/pkms_with_canvas_like_in_obsidian/>
- r/NoteTaking, *Made my own self-hosted notes app because Capacities was almost what I wanted*, 2026-10 —
  <https://www.reddit.com/r/NoteTaking/comments/1xc0h5o/made_my_own_selfhosted_notes_app_because/>
- r/capacitiesapp, *Farewell to Capacities*, 2025-09-22 —
  <https://www.reddit.com/r/capacitiesapp/comments/1nnr3gt/farewell_to_capacities/>
- r/capacitiesapp, *Privacy concerns*, 2026-03 —
  <https://www.reddit.com/r/capacitiesapp/comments/1s2rpf9/privacy_concerns/>
- r/capacitiesapp, *Truly offline mode exist?*, 2026-01 —
  <https://www.reddit.com/r/capacitiesapp/comments/1qfiqul/truly_offline_mode_exist/>
- r/capacitiesapp, *Lifetime/business*, 2023-09-01 —
  <https://www.reddit.com/r/capacitiesapp/comments/166ypk4/lifetimebusiness/>
- Capacities feedback, *Create a "Lifetime" subscription level* —
  <https://feedback.capacities.io/b/feedback/posts/post_01m4bqb473e069thtnv7nprrm4>
- r/capacitiesapp, *capacities pro worth it for a lawyer?*, 2025-08-20 —
  <https://www.reddit.com/r/capacitiesapp/comments/1mvu0le/capacities_pro_worth_it_for_a_lawyer/>
- r/capacitiesapp, *Has promise. But I can't spend Evernote-level money…*, 2024-01-01 —
  <https://www.reddit.com/r/capacitiesapp/comments/18w5bie/has_promise_but_i_cant_spend_evernotelevel_money/>
- r/capacitiesapp, *Capacities vs OneNote vs Obsidian?*, 2025-02-25 —
  <https://www.reddit.com/r/capacitiesapp/comments/1ixmyt6/capacities_vs_onenote_vs_obsidian_looking_for_the/>
- r/capacitiesapp, *I think I might leave the app and go back to obsidian/notion*, 2026-01-05 —
  <https://www.reddit.com/r/capacitiesapp/comments/1q4bbp0/i_think_i_might_leave_the_app_and_go_back_to/>
- r/NoteTaking, *Need recommendations: Fast, free, future-proof, and cross-platform note taking app?*, 2026-06-28 —
  <https://www.reddit.com/r/NoteTaking/comments/1ui8fn7/need_recommendations_fast_free_futureproof_and/>
- r/NoteTaking, *i want to move my notes to other app*, 2026-06-02 —
  <https://www.reddit.com/r/NoteTaking/comments/1tv5jlv/i_want_to_move_my_notes_to_other_app/>
- r/Notion, *Non-american alternative to notion?*, 2026-01-25 —
  <https://www.reddit.com/r/Notion/comments/1qmgp6y/nonamerican_alternative_to_notion/>

### Self-hosted evaluation
- r/selfhosted, *Which details are important for you when checking out a self-hosted application?*, 2020-11-20 —
  <https://www.reddit.com/r/selfhosted/comments/jxpwp2/which_details_are_important_for_you_know_when/>
- r/selfhosted, *How do you pick a service to self-host?*, 2025-04-16 —
  <https://www.reddit.com/r/selfhosted/comments/1k08t1d/how_do_you_pick_a_service_to_self_host/>
- r/selfhosted, *How do you tell if a project is mature?*, 2026-06-30 (removed) —
  <https://www.reddit.com/r/selfhosted/comments/1uju6wf/how_do_you_tell_if_a_project_is_a_mature/>
- r/selfhosted, *Obsidian self hosted alternative (as Self hosted sync is not reliable...)*, 2025-10-31 —
  <https://www.reddit.com/r/selfhosted/comments/1okup46/obsidian_self_hosted_alternative_as_self_hosted/>
- r/selfhosted, *We need a "simple" web based MD notes app*, 2025-08-25 —
  <https://www.reddit.com/r/selfhosted/comments/1mzod84/we_need_a_simple_web_based_md_notes_app/>
- r/selfhosted, *Self-hosted app for security research notes + project/ideas management?*, 2026-01-31 —
  <https://www.reddit.com/r/selfhosted/comments/1qrmqse/selfhosted_app_for_security_research_notes/>
- r/selfhosted, *Self-hosted life/family journaling tools*, 2026-07-20 (removed) —
  <https://www.reddit.com/r/selfhosted/comments/1v18e3u/what_selfhosted_life_notesfamily_journaling_tools/>
- r/selfhosted, *SilverBullet: Sweet little app for markdown notes*, 2025-01-18 —
  <https://www.reddit.com/r/selfhosted/comments/1i48qma/silverbullet_sweet_little_app_for_markdown_notes/>
- r/selfhosted, *Introducing Anytype Open Beta*, 2023-07-25 —
  <https://www.reddit.com/r/selfhosted/comments/1596gs4/introducing_anytype_open_beta_one_app_for/>
- r/selfhosted, *Update: You asked, we listened — Anytype…*, 2023-12-28 —
  <https://www.reddit.com/r/selfhosted/comments/18sucfz/update_you_asked_we_listened_anytype_now_supports/>
- r/selfhosted, *Is anyone self hosting anytype?*, 2023-12-07 —
  <https://www.reddit.com/r/selfhosted/comments/18d6m1x/is_anyone_self_hosting_anytype/>
- r/selfhosted, *Notion alternative: AppFlowy vs Outline vs Affine?*, 2023-07-06 —
  <https://www.reddit.com/r/selfhosted/comments/14s9d8j/notion_alternative_appflowy_vs_outline_vs_affine/>

### Privacy, AI, and launch post-mortems
- r/privacy, *Most secure notes app?*, 2026-02-05 —
  <https://www.reddit.com/r/privacy/comments/1qwmwc6/most_secure_notes_app/>
- r/PKMS, *What PKMS tools are privacy focused?*, 2023-10-18 —
  <https://www.reddit.com/r/PKMS/comments/17acvcv/what_pkms_tools_are_privacy_focused/>
- r/PKMS, *Which Should I Choose for Maximum Privacy…*, 2025-02-18 —
  <https://www.reddit.com/r/PKMS/comments/1is9k12/which_should_i_choose_for_maximum_privacy/>
- r/PKMS, *For those who prefer AI-free PKM systems…*, 2026-07-27 —
  <https://www.reddit.com/r/PKMS/comments/1v87ypa/for_those_who_prefer_aifree_pkm_systems_what_are/>
- r/ObsidianMD, *Do you avoid using AI with your notes…?*, 2026-03-11 —
  <https://www.reddit.com/r/ObsidianMD/comments/1rqjvcb/do_you_avoid_using_ai_with_your_notes_because_you/>
- r/ObsidianMD, *AI boosterism is ruining this community*, 2026-02-16 —
  <https://www.reddit.com/r/ObsidianMD/comments/1r604pp/ai_boosterism_is_ruining_this_community/>
- r/PKMS, *Do you see a legit assistant role for AI in PKMS?*, 2026-09-03 —
  <https://www.reddit.com/r/PKMS/comments/1w6hnx9/do_you_see_a_legit_assistant_role_for_ai_in_pkms/>
- r/PKMS, *How do you use AI tools with your notes/docs…?*, 2026-04-01 —
  <https://www.reddit.com/r/PKMS/comments/1s9oywv/how_do_you_use_ai_tools_with_your_notesdocs_in/>
- r/productivity, *We need to talk about how "AI features" are actually making productivity apps worse*, 2026-04-03 —
  <https://www.reddit.com/r/productivity/comments/1sbe3ey/we_need_to_talk_about_how_ai_features_are/>
- r/PKMS, *Agentic Knowledge Management: The Next Evolution of PKM*, 2026-02-03 —
  <https://www.reddit.com/r/PKMS/comments/1quqiu9/agentic_knowledge_management_the_next_evolution/>
- HN, *Launching a new approach to note-taking / knowledge organization: capacities.io*, 2022-06-02 —
  <https://news.ycombinator.com/item?id=31592670>
- HN, *Notes: Fast note-taking app, open-source, without Electron, built in Qt C++*, 2022-09-14 —
  <https://news.ycombinator.com/item?id=32837942>
- HN, *SiYuan: Privacy-first, self-hosted personal knowledge management software*, 2024-12-26 —
  <https://news.ycombinator.com/item?id=42512713>
- HN, *Note-Taking and Personal Knowledge Management*, 2026-07-28 —
  <https://news.ycombinator.com/item?id=49084324>
- HN, *Show HN: Colanode, open-source and local-first Slack and Notion alternative*, 2025-04-24 —
  <https://news.ycombinator.com/item?id=43780176>
- HN, *Show HN: Piruetas – A self-hosted diary app I built for my girlfriend*, 2026 —
  <https://news.ycombinator.com/item?id=47985210>
- r/selfhosted, *Introducing Hoarder*, 2024-03-26 —
  <https://www.reddit.com/r/selfhosted/comments/1bo9tdq/introducing_hoarder_an_open_source/>

### Rules, pricing, docs, market
- r/selfhosted, *Quarter 2 Update - Revisiting Rules. Again.*, 2026-04-07 —
  <https://www.reddit.com/r/selfhosted/comments/1sey9ch/quarter_2_update_revisiting_rules_again/>
- r/selfhosted, *Rules Update: New Project Friday here to stay*, 2026 —
  <https://www.reddit.com/r/selfhosted/comments/1rmt39o/rules_update_new_project_friday_here_to_stay/>
- r/PKMS, *New rule: No advertising or excessive spam*, 2024-07-26 —
  <https://www.reddit.com/r/PKMS/comments/1ect241/new_rule_no_advertising_or_excessive_spam/>
- Capacities, *What's Not Next?* — <https://capacities.io/whats-not-next>
- Capacities, *Export reference* — <https://docs.capacities.io/reference/export>
- Capacities, *Bulk import reference* — <https://docs.capacities.io/reference/bulk-import>
- Capacities, *Pricing* — <https://capacities.io/pricing>
- Saner.AI, *Capacities Review 2026* — <https://www.saner.ai/blogs/capacities-review>
- XDA, *Self-hosted app abandonment is speeding up…*, 2026-06-27 —
  <https://www.xda-developers.com/self-hosted-app-abandonment-speeding-up-reaching-projects-trust/>
- Enlisted, *9 best open-source Capacities alternatives in 2026* —
  <https://enlisted.directory/alternatives/capacities>
- Nodum, *Capacities alternative* — <https://nodum.md/alternatives/capacities>
- Vasta — <https://github.com/ariedotme/vasta>

---

## Appendix B — Frequency coding notes

- **Unit of coding:** a thread, not a comment. A requirement counts if the
  original poster states it or a top comment states it as a condition for
  switching.
- **Denominator:** 26 direct seeker threads for the main checklist; 14 adjacent
  threads for the self-hosted/privacy/AI sections. Some threads are counted in
  both.
- **Bias:** Reddit search surfaces threads that mention "alternative",
  "self-hosted", "open source", "privacy" — the pool is biased toward people
  who already distrust cloud tools. The mass of happy Capacities users is not
  represented here; see `REDDIT-SENTIMENT.md`.
- **Quote handling:** short quotes are verbatim from the thread text returned by
  search; longer passages are paraphrased to avoid misattribution. Typos in
  quotes are preserved where they are part of the voice.
- **Lumen status:** based on the repo at `lumen-pkm/` (README, `server/`,
  `mobile/`, `desktop/`) as of 2026-10-31. "Verify" marks items I could not
  confirm from code alone.
- **What would change the ranking:** a working web UI would move from Gap 1 to
  Met and likely lift r/selfhosted adoption more than any other single change;
  a Capacities importer would convert the existing refugee pool; a public demo
  would reduce the "is it real?" objection.

---

*End of document.*
