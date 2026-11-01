# Reddit & Community Sentiment on Capacities (and PKM generally)

**Research agent B — parallel intelligence sweep**
**Date:** 2026-10-31
**Scope:** real-user sentiment about Capacities.io, plus the PKM market it lives in.
**Audience:** Lumen product/engineering (self-hosted, offline-friendly, no-AI, privacy-first Capacities clone).

---

## Method and caveats

Sources actually read (not just search snippets):

- **r/capacitiesapp** — 33 threads, **534 comments** pulled from the Reddit comment API/PullPush (threads dated 2025–2026), plus two 2024 churn threads read via the web embed. Also the subreddit's top-100 posts of the year (titles, dates, post bodies via RSS).
- **r/PKMS** — 4 threads (`1mj0tlm`, `1hpas5i`, `1u3ayg1`, `1qfv3y5`).
- **r/Notion** — `129edd8` ("Anyone has tried Capacities.io?").
- **r/ObsidianMD** — `1psm2vk` ("Looking to move away from Capacities…"), `1vrjabi` ("Moving from Capacities but facing friction").
- **r/NoteTaking** — `1xc0h5o` ("Made my own self-hosted notes app because Capacities was almost what I wanted").
- **Apple App Store** — the 40 most recent US reviews (Jul 2024 – Aug 2026). Average **3.50/5**; distribution 5★:14, 4★:6, 3★:12, 2★:2, 1★:6.
- **Hacker News** — Algolia search for `capacities.io`, launch thread (2022), "Capacities" thread (2023), Tolaria thread (2026).
- **Capacities official** — pricing page, "What's Not Next?" page, release posts.

Caveats:

- Reddit is self-selected: happy users are under-represented in complaint threads, unhappy users over-represented. The App Store sample is a useful counterweight (3.5 average, not a disaster).
- r/capacitiesapp is small and enthusiast-heavy. Cross-sub threads (Obsidian/Notion/PKMS) are where churn reasoning shows up.
- Keyword frequency counts are **indicative, not statistical**. Where I say "most repeated", it is based on reading every thread plus a keyword pass over the 534 comments.
- Quotes are short and paraphrased where possible; links go to the thread, not necessarily the exact comment.
- Dates matter: Capacities shipped a lot in 2025–2026 (tasks, recurring tasks, search 2.0/3.0, mobile 2.0, whiteboards, version history). Some complaints below are already fixed; they still explain user psychology and trust.

---

## 0. TL;DR — the ten things that matter for Lumen

1. **The moat is the object model + daily note, not AI.** Users describe it as fitting "how my brain organized information" and as "structure without system design". This is the thing to protect.
2. **Mobile is the #1 grievance by a wide margin.** 24 of 40 recent App Store reviews mention the phone/tablet app, most of them negatively. It is the main reason people keep a second app.
3. **Pricing resentment is about *what* is paywalled, not the absolute price.** Queries, tasks and API are seen as "foundational" and their gating feels punitive.
4. **Trust is the real currency.** Data-loss stories, sync duplicates, delayed backups and missing version history do more churn damage than any missing feature.
5. **Users ask for reminders, not more AI.** "Tasks without notifications are just notes with dates" is the sentence that best captures the task-management gap.
6. **Offline is "good enough", not "offline-first".** There is no local-only mode and no way to disable sync; this blocks corporate/regulated users.
7. **Self-hosting demand is niche but real — and already has a direct OSS competitor.** A user built *Vasta*, an open-source self-hosted Capacities alternative, in Oct 2026.
8. **People leave for Obsidian when they want local files, Claude Code, canvas, or speed at scale.** They come back to Capacities for less tinkering. The trade-off is power vs friction.
9. **Search relevance and setup friction are quiet churn drivers.** Both contradict the marketing ("just works"), and both are fixable cheaply.
10. **Delight is still possible.** The whiteboard launch (Oct 2026) produced genuine euphoria after years of "no whiteboards ever". Long-held wishes, delivered well, convert skeptics.

---

## (a) What users LOVE — keep doing / make sure Lumen nails

### a.1 The object model (typed objects, properties, collections, backlinks)

This is the most consistently praised thing in the entire corpus. It is described as a different mental model, not a feature.

- "Objects just totally changed my life. Organizing and systemising is finally possible for me." — [What keeps you coming back?](https://www.reddit.com/r/capacitiesapp/comments/1p7isuj/what_keeps_you_coming_back/)
- A long-time Obsidian user explains the key difference: in Obsidian a template is a static mold; in Capacities an object type is **dynamic and persistent**, and property changes propagate to every instance. He calls this the reason he stopped trying to "turn Obsidian into Capacities". — [Why did you choose Capacities over Notion or Obsidian?](https://www.reddit.com/r/capacitiesapp/comments/1wqllus/why_did_you_choose_capacities_over_notion_or/)
- "Everything just connects, it's really just that simple." — [What keeps you coming back?](https://www.reddit.com/r/capacitiesapp/comments/1p7isuj/what_keeps_you_coming_back/)
- Users repeatedly say the object model fits how they think better than folders, tags-only, or Notion databases. The r/PKMS thread ["Capacities is awesome and super easy to use"](https://www.reddit.com/r/PKMS/comments/1mj0tlm/capacities_is_awesome_and_super_easy_to_use/) is built around exactly this.

**Lumen implication:** object types, properties, collections, backlinks and "create object from anywhere" are table stakes. Do not simplify them away in the name of onboarding. The *persistence* of schema (change the type, all instances update) is the differentiator users can articulate.

### a.2 The daily note as universal inbox

The second most loved pattern. It is the onboarding ramp and the retention loop at once.

- "My daily note is my inbox. I dump things there first, then organize them later." — [What keeps you coming back?](https://www.reddit.com/r/capacitiesapp/comments/1p7isuj/what_keeps_you_coming_back/)
- A user with ADHD says letting thoughts spill onto the page, then adding tags/links/types as they go, lets the notes organize themselves. — [App Store review, 2025-05-28](https://apps.apple.com/us/app/capacities-notes-pkm/id1670188548)
- "The Daily Notes feature is always there, ready to go — no setup required." — [r/PKMS, Leaning to stick with Capacities](https://www.reddit.com/r/PKMS/comments/1hpas5i/leaning_to_stick_with_capacities_a_journey_of/)

**Lumen implication:** the daily note must be the default landing surface, one keystroke away, and must accept any block type without asking for a destination. The "dump first, structure later" loop is the product.

### a.3 Low friction / low tinkering vs Notion and Obsidian

This is the emotional core of the "why Capacities" answer. Users are not buying features; they are buying *not having to build a system*.

- "Obsidian is incredibly capable… Capacities feels different. It's calmer. Less impressive on paper, but easier to live in." — [After all, why do you prefer Capacities?](https://www.reddit.com/r/capacitiesapp/comments/1qvo2bg/after_all_why_do_you_prefer_capacities/)
- "On paper, Obsidian wins question one almost every time. In real life, Capacities often wins question two" (question two = "how does it behave on an average Tuesday"). Same thread.
- "I spent too much time messing with Obsidian plugins and settings and not enough time making notes." — [Why did you choose Capacities…](https://www.reddit.com/r/capacitiesapp/comments/1wqllus/why_did_you_choose_capacities_over_notion_or/)
- "I don't feel that kind of pressure, as it just works and I don't have to set up my system in advance." — [I think I might leave the app…](https://www.reddit.com/r/capacitiesapp/comments/1q4bbp0/i_think_i_might_leave_the_app_and_go_back_to/)

**Lumen implication:** "no setup" is a promise, not a feature. Every new setting, template gallery or advanced panel risks breaking it. Defaults matter more than options.

### a.4 Speed relative to Notion, and a calm, beautiful UI

- "It also just loads SO MUCH FASTER than Notion, which has been almost unusably slow on my end." — [r/Notion thread](https://www.reddit.com/r/Notion/comments/129edd8/anyone_has_tried_capacitiesio/)
- "Gosh, Capacities is beautiful" is a whole thread title ([link](https://www.reddit.com/r/capacitiesapp/comments/1tdusr7/gosh_capacities_is_beautiful/)). Commenters mention it "fuels my creative brain" and that it is European, with "friendly and sustainable principles".
- Multiple users call the UI "calm" and say it reduces cognitive load compared with Notion's blank-canvas freedom.

**Lumen implication:** visual calm is a retention feature. A Qt desktop app can win here if typography, spacing and block chrome are disciplined. Do not add dashboard clutter.

### a.5 Tags, labels and queries as lightweight dashboards

- Users on the free tier use tags as a poor-man's dashboard; Pro users combine queries with a customized sidebar. — [How to organise and maximise…](https://www.reddit.com/r/capacitiesapp/comments/1so78sw/how_to_organise_and_maximise_the_use_of/)
- "The relatively recent ability to add labels to objects and then query has taken it to the next level for me." — [Looking at using Capacities](https://www.reddit.com/r/capacitiesapp/comments/1ra2k8d/looking_at_using_capacities/)
- Queries embedded in templates (e.g. last 10 meetings for a project) are a recurring "this is the killer feature" story. — [Business PM setup](https://www.reddit.com/r/capacitiesapp/comments/1qdk214/how_i_use_capacities_for_business_project/)

**Lumen implication:** inline queries in templates and object pages are high-leverage. They turn a note app into a personal database without a database UI.

### a.6 Tasks tied to context (post-Dec 2025)

Once tasks shipped, the "context" angle — a task attached to a person, project, meeting or AC unit — became a differentiator.

- "Being able to attach a task to an object is awesome cuz it provides so much context…" — [How is task management in Capacities?](https://www.reddit.com/r/capacitiesapp/comments/1u95g59/how_is_task_management_in_capacities/)
- "I have finally moved to Capacities… I very much like the current system and queries and multiple possible views." — [Thoughts a week after tasks](https://www.reddit.com/r/capacitiesapp/comments/1prio55/what_are_your_thoughts_a_week_after_the/)
- Recurring tasks (May 2026) converted several holdouts: "Love the new Recurring Tasks — this is amazing!" ([link](https://www.reddit.com/r/capacitiesapp/comments/1t5zegx/love_the_new_recurring_tasks_this_is_amazing/)).

**Lumen implication:** context links on tasks are more valuable than a Todoist clone. The daily note should surface "today" tasks and the object page should show its tasks.

### a.7 Capture integrations (WhatsApp, Telegram, email, Readwise, Kindle, calendar, Raycast, API)

- WhatsApp/Telegram/email capture is repeatedly cited as the reason mobile weakness is tolerable. — [Mobile app thread](https://www.reddit.com/r/capacitiesapp/comments/1sh6gxk/the_mobile_app_makes_me_want_to_rip_my_hair_out/)
- Readwise and Kindle integrations are "game changers" for researchers. — [Love the recent updates](https://www.reddit.com/r/capacitiesapp/comments/1wwao65/love_the_recent_updates_and_workflow_capabilities/)
- Raycast is "huge" for a litigation attorney: "I don't know how one would use Capacities for note taking without it." — [Business PM setup](https://www.reddit.com/r/capacitiesapp/comments/1qdk214/how_i_use_capacities_for_business_project/)
- The API thread is small but positive: "The API is awesome" ([link](https://www.reddit.com/r/capacitiesapp/comments/1xie98u/the_api_is_awesome/)).

**Lumen implication:** the browser extension and Android share target are Lumen's capture story. They must be *reliable* (see pain #1) and should support "capture to daily note" without opening the app.

### a.8 Whiteboards and global graph — long-held wishes that landed well

- Whiteboards shipped 2026-10-26. Reaction: "I knew it. I f****** knew it. You can't make me more happy than that." and "STFU!!! Respectfully, though. I am shocked and delighted!" — [Whiteboards thread](https://www.reddit.com/r/capacitiesapp/comments/1xfvzwn/surprise_whiteboards_in_capacities_out_now/)
- Global graph (Oct 2026) also got a warm reception; former Obsidian users say they love it. — [Global graph thread](https://www.reddit.com/r/capacitiesapp/comments/1x5jec6/capacities_just_casually_added_global_graph_view/)

**Lumen implication:** canvas/whiteboard is now expected in the "Capacities parity" set. If Lumen does not have it, expect it in every comparison. A simple, object-aware canvas beats a full Excalidraw clone.

### a.9 Export and backup story (as reassurance, not as a feature)

A surprising number of "why I stay" posts mention export/backup as the thing that defuses lock-in anxiety.

- "Capacities has fantastic export options (Markdown, Word, CSV, PDF) and an automated, scheduled local Markdown backup feature… I have never felt locked in." — [Why did you choose Capacities…](https://www.reddit.com/r/capacitiesapp/comments/1wqllus/why_did_you_choose_capacities_over_notion_or/)
- Free plan explicitly advertises "Export everything, always" ([pricing](https://capacities.io/pricing)).

**Lumen implication:** Lumen should be *better* here, not merely equal: one-click full export, scheduled local backups, and an export that preserves links/relations. This is a cheap trust win against a cloud competitor.

### a.10 The team and the community

- "They seem genuinely a nice company worthy of supporting." — [What keeps you coming back?](https://www.reddit.com/r/capacitiesapp/comments/1p7isuj/what_keeps_you_coming_back/)
- "Great communication from the creators' team… it develops month by month." — [After all, why do you prefer Capacities?](https://www.reddit.com/r/capacitiesapp/comments/1qvo2bg/after_all_why_do_you_prefer_capacities/)
- The co-founder replies directly in r/Notion and r/capacitiesapp threads. This is visible and appreciated.

**Lumen implication:** a small team that ships and answers is a moat against Notion. Keep release notes and a public roadmap; users read them.

---

## (b) Pain points, ranked by frequency

Ranking is based on how often a theme appears as a *complaint* across the 33 r/capacitiesapp threads, 5 cross-subreddit threads and 40 App Store reviews. Approximate counts are given where useful; they are not statistically rigorous.

### 1. Mobile app quality and parity — the dominant complaint

**Evidence:** 24 of 40 recent App Store reviews mention the phone/tablet app, most negatively; at least 5 r/capacitiesapp threads are primarily or partly about it.

What users say:

- "The mobile app makes me want to rip my hair out." — thread title ([link](https://www.reddit.com/r/capacitiesapp/comments/1sh6gxk/the_mobile_app_makes_me_want_to_rip_my_hair_out/)).
- "The Android app is almost unusable, not even for a quick capture with the sync problems." — [An idea to make the mobile app better](https://www.reddit.com/r/capacitiesapp/comments/1pipvm1/an_idea_to_make_the_mobile_app_better/).
- "Borderline unusable" (Android), "bug-ridden mess" (iOS), "viewer-centric" — multiple threads and reviews.
- Specific gaps: cannot create/edit object types on phone; share sheet fails ~60% of the time; no "Add to Daily Note" Shortcuts action; no widgets; search opens the object but not the block; iPad Stage Manager layout breaks; text selection and tables are clunky; app freezes and needs restart.
- "I have to get on my iPad or computer in order to create or edit an object… functions available on each device are notably inconsistent." — [App Store, 2025-08-30](https://apps.apple.com/us/app/capacities-notes-pkm/id1670188548)

Why it matters: mobile is where capture happens. Every failure pushes users to Apple Notes/Google Keep/WhatsApp, and some never come back. Capacities shipped "Mobile App 2.0" in Aug 2026, but sentiment in Oct 2026 is still negative.

**Lumen implication:** this is the single biggest opportunity. A Qt desktop + native Android + browser extension can win on *capture reliability* even if feature parity lags. Prioritize: share target, quick capture to daily note, offline queue, search-that-jumps-to-block, widgets.

### 2. Pricing and the shape of the paywall

**Evidence:** ~10% of comments touch price; 4 dedicated threads; multiple App Store reviews.

What users say:

- Pro is **$9.99/mo**; Believer from **$12.49/mo**; the AI "Plus" add-on is **$8.33/mo billed annually** ([pricing](https://capacities.io/pricing)). The App Store 1★ review from 2025 cites **$199/year**.
- "Hardly cheap." / "It's $10 US! That's an amazing value" — the community is split. — [Is Capacities Pro worth it?](https://www.reddit.com/r/capacitiesapp/comments/1r9r9s5/is_capacities_pro_worth_it/)
- "I like how organized everything is… but does that justify an over $100 subscription? I also don't need tons of space." — [Black Friday thread](https://www.reddit.com/r/capacitiesapp/comments/1p4egqh/will_there_be_a_black_friday_deal/)
- The harshest review: "Dishonest Pricing… $199/year? No way. It's still in development. No student discount. It doesn't even have an import or export tool." — [App Store, 2025-06-15](https://apps.apple.com/us/app/capacities-notes-pkm/id1670188548)
- The specific resentment is **what** is gated: queries, tasks, API and AI are seen as foundational. "They paywall queries whereas it's a foundational feature. Same for the tasks probably." — [I think I might leave…](https://www.reddit.com/r/capacitiesapp/comments/1q4bbp0/i_think_i_might_leave_the_app_and_go_back_to/)
- No Black Friday deal was a "big miss" for several users; they asked for a tier between Basic and Pro. — [Black Friday thread](https://www.reddit.com/r/capacitiesapp/comments/1p4egqh/will_there_be_a_black_friday_deal/)
- Regional pricing exists but is inconsistent; Brazilian and other non-US users say it is expensive in local currency. — [I love Capacities](https://www.reddit.com/r/capacitiesapp/comments/1phhb9e/i_love_capacities/)

**Lumen implication:** Lumen's self-hosted model can undercut this, but "free" is not automatically a win — users worry about sustainability. A clear "pay once / host yourself / no per-seat" story, plus a generous free tier with *queries and tasks included*, is a direct wedge. Avoid gating "foundational" primitives.

### 3. Sync, data integrity and backups — the trust killer

**Evidence:** at least 6 threads about lost content, plus App Store login/lockout reviews.

What users say:

- "My Workspace is Gone." — thread title; the team asked the user to email support. ([link](https://www.reddit.com/r/capacitiesapp/comments/1v4p5pf/my_workspace_is_gone/))
- A toggle-block bug wiped page content on click; the user posted a video and said "I've completely lost trust in the app." The team apologized and recommended a manual full export. — [I lost all the content of one of my pages](https://www.reddit.com/r/capacitiesapp/comments/1ovyumq/i_lost_all_the_content_of_one_of_my_pages_for_no/)
- Sync duplicates: edits on mobile left old data behind; desktop changes did not appear on phone until app data was cleared. — [Why I was excited…](https://www.reddit.com/r/capacitiesapp/comments/1ftk8cy/why_i_was_excited_about_capacities_but_stopped/)
- Backups are server-generated, can take 12+ hours, arrive by email, and had broken media links. Same thread.
- No version history was a years-long complaint; it is now listed as a Pro feature, but the trust scar remains. — [Page version history feedback link cited in thread](https://www.reddit.com/r/capacitiesapp/comments/1ftk8cy/why_i_was_excited_about_capacities_but_stopped/)
- "This is why the lack of versioning is such a weakness… reliability is so much more important than pace of feature development." — [Lost content thread](https://www.reddit.com/r/capacitiesapp/comments/1ovyumq/i_lost_all_the_content_of_one_of_my_pages_for_no/)
- Login lockout: "I've been unable to login for months… support and reliability isn't there. Don't keep any info in here that you're not willing to lose." — [App Store, 2025-08-15](https://apps.apple.com/us/app/capacities-notes-pkm/id1670188548)

**Lumen implication:** this is where a self-hosted product can be *categorically* better. Local-first storage, automatic local snapshots, per-object version history, and a visible "your data is on your disk" indicator are not nice-to-haves; they are the pitch. Make restore a first-class UI, not a support ticket.

### 4. Performance at scale

**Evidence:** 3 dedicated threads + recurring comments; App Store mentions of freezing/slowness.

What users say:

- "Capacities performance broken. Any ideas?" — 26 MB export, frequent freezes, buttons stop working. — [thread](https://www.reddit.com/r/capacitiesapp/comments/1uijicx/capacities_performance_broken_any_ideas/)
- "Persistent Slowness": creating a property, letters appear ~30 seconds later; scrolling slow in large object types. — [thread](https://www.reddit.com/r/capacitiesapp/comments/1viev7w/persistent_slowness/)
- Long notes (>5k words) and pages with many backlinks slow down; memory consumption 2–3× Obsidian; memory leak reported. — [Why I was excited…](https://www.reddit.com/r/capacitiesapp/comments/1ftk8cy/why_i_was_excited_about_capacities_but_stopped/)
- "It got slow as my notes grew" is a stated reason for leaving to Obsidian. — [I think I might leave…](https://www.reddit.com/r/capacitiesapp/comments/1q4bbp0/i_think_i_might_leave_the_app_and_go_back_to/)
- One user's org blocked Capacities because of the AI integration, and they went back to Anytype: "It's so much faster." — [Performance thread](https://www.reddit.com/r/capacitiesapp/comments/1uijicx/capacities_performance_broken_any_ideas/)

**Lumen implication:** Qt + local DB should be a performance advantage. Publish benchmarks (10k notes, 5k-word pages, 100 backlinks) and treat "no spinner" as a feature. Large-note editing is where Capacities is weakest.

### 5. Missing task essentials (reminders, quick add, carry-over)

**Evidence:** tasks are the most-discussed feature cluster (~18% of comments mention tasks/reminders); multiple threads.

What users say:

- "Tasks without notifications are just notes with dates." — [How is task management…](https://www.reddit.com/r/capacitiesapp/comments/1u95g59/how_is_task_management_in_capacities/)
- "Until it gets push notifications on mobile, it can't compete with any dedicated task app." — [Thoughts a week after tasks](https://www.reddit.com/r/capacitiesapp/comments/1prio55/what_are_your_thoughts_a_week_after_the/)
- Wanted: reminders, quick add from anywhere, task templates, "someday/later" status, carry-over of unfinished tasks, deadlines (shipped), recurring (shipped), time tracking (unlikely).
- "When Reminders come I can officially cancel my dedicated to-do app subscription." — [Recurring tasks thread](https://www.reddit.com/r/capacitiesapp/comments/1t5zegx/love_the_new_recurring_tasks_this_is_amazing/)
- Reminders were still not shipped as of Oct 2026: ["When are reminders being released"](https://www.reddit.com/r/capacitiesapp/comments/1x0y70/when_are_reminders_being_released/).

**Lumen implication:** Lumen can leapfrog by shipping *reliable local notifications* (desktop + Android) and a global quick-add. This is a concrete, low-cost differentiator against Capacities' biggest functional gap.

### 6. AI-first roadmap, MCP and API limits

**Evidence:** ~11% of comments mention AI; 3 dedicated threads; a steady trickle of "I left for Claude Code" posts.

What users say:

- "I don't give a crap about AI integration. Tasks is worth the price of admission for me." — [Is Capacities Pro worth it?](https://www.reddit.com/r/capacitiesapp/comments/1r9r9s5/is_capacities_pro_worth_it/)
- "Am I crazy for being really disappointed with the MCP approach?" — thread title. The MCP has a hard cap on creating object instances, no delete, and is not meant to automate knowledge work. — [thread](https://www.reddit.com/r/capacitiesapp/comments/1t6u9nh/am_i_crazy_for_being_really_disappointed_with_the/)
- "Loved Capacities for a year+ but switched to Obsidian 4–5 weeks ago for this exact reason… the ability to do almost anything with Claude/Codex far outweighs the downside." — same thread.
- "Tools like Claude Code and Claude Cowork are putting PKM apps in danger. Capacities needs a full API." — [thread](https://www.reddit.com/r/capacitiesapp/comments/1qiwlw8/tools_like_claude_code_and_claude_cowork_are/)
- API 2.0 was "in progress" in May 2026; users are impatient. — [API 2.0 thread](https://www.reddit.com/r/capacitiesapp/comments/1tq66fb/api_20_in_progress/)

**Lumen implication:** Lumen's "no AI" stance is a *feature* for a large segment (privacy, cost, focus). But the API/MCP crowd is real and influential. A clean local REST API + CLI + MCP adapter (opt-in, no cloud) would capture the "agentic PKM" refugees without adding AI to the core.

### 7. Search relevance and navigation

**Evidence:** ~10% of comments; Search 2.0/3.0 threads; still complaints after the rewrite.

What users say:

- Search defaults to OR, not AND: searching a software name + "contract" returned every note containing either term. Users want AND default or relevance ranking. — [Search 2.0 thread](https://www.reddit.com/r/capacitiesapp/comments/1qof9wx/search_20_is_here_and_much_more/)
- On mobile, tapping a result opens the object but does not scroll to the block; no find-in-page. — [Mobile thread](https://www.reddit.com/r/capacitiesapp/comments/1sh6gxk/the_mobile_app_makes_me_want_to_rip_my_hair_out/)
- Users say the phone version is mainly for search and quick entry, so its weaknesses hurt disproportionately. — [Mobile thread](https://www.reddit.com/r/capacitiesapp/comments/1sh6gxk/the_mobile_app_makes_me_want_to_rip_my_hair_out/)
- Capacities rebuilt search twice in 2026 (2.0 in Jan, 3.0 in Apr), which shows the team knows.

**Lumen implication:** search is a trust surface. Implement AND-by-default with quoted phrases/operators, jump-to-block with highlight, and instant local results. This is cheap and highly visible.

### 8. Export, ownership and E2EE

**Evidence:** ~2% of comments, but high emotional weight; official "What's Not Next" page.

What users say:

- "I didn't like not owning my data." — [I think I might leave…](https://www.reddit.com/r/capacitiesapp/comments/1q4bbp0/i_think_i_might_leave_the_app_and_go_back_to/)
- Export does not preserve links between objects well; backups had broken media links. — [Preserving links when exporting](https://www.reddit.com/r/capacitiesapp/comments/1on9zgd/preserving_links_between_objects_when_exporting/) and [Why I was excited…](https://www.reddit.com/r/capacitiesapp/comments/1ftk8cy/why_i_was_excited_about_capacities_but_stopped/)
- "Obsidian is E2EE and Capacities only has encryption in transit… Obsidian has personal data sovereignty using .md files." — [After all, why do you prefer Capacities?](https://www.reddit.com/r/capacitiesapp/comments/1qvo2bg/after_all_why_do_you_prefer_capacities/)
- Capacities officially says **full E2EE will not be added** and self-hosting is not an option ([What's Not Next](https://capacities.io/roadmap/whats-not-next)).

**Lumen implication:** this is Lumen's home turf. Say it plainly on the landing page: your server, your disk, no E2EE trade-off because there is no cloud. Export must preserve links and attachments.

### 9. Setup complexity and UI clunkiness

**Evidence:** a persistent minority report, often from users who *want* to love the app.

What users say:

- "The UI feels like I need a second job just to set it up." — [2 months in…](https://www.reddit.com/r/capacitiesapp/comments/1s6qhhb/2_months_in_with_capacities_great_object/)
- "I get stuck in the 'is this thing set up properly?' phase before I even begin working." — [I think I might leave…](https://www.reddit.com/r/capacitiesapp/comments/1q4bbp0/i_think_i_might_leave_the_app_and_go_back_to/)
- "The navigation just feels a bit too much like work. Not effortless enough." — [2 months in…](https://www.reddit.com/r/capacitiesapp/comments/1s6qhhb/2_months_in_with_capacities_great_object/)
- It took one user "almost two years" to feel comfortable; another says onboarding emails were the turning point. — [How to learn Capacities](https://www.reddit.com/r/capacitiesapp/comments/1xa1fiy/what_is_the_best_way_to_learn_capacities/) and [Looking at using Capacities](https://www.reddit.com/r/capacitiesapp/comments/1ra2k8d/looking_at_using_capacities/)
- Block editing is a common friction: paragraph vs block, copying, mobile selection. — [Write paragraphs not blocks](https://www.reddit.com/r/capacitiesapp/comments/1py2xmh/write_paragraphs_not_blocks/)

**Lumen implication:** the object model is powerful but the first 30 minutes decide retention. Invest in an opinionated starter space, in-context tips, and a "just start writing" path that never asks for schema. Also: a real paragraph block / clean copy-paste behavior.

### 10. Customization limits

**Evidence:** 2 dedicated threads + recurring comments.

What users say:

- "It is time Capacities let us customize built-in objects." — [thread](https://www.reddit.com/r/capacitiesapp/comments/1ryo3p9/it_is_time_capacities_let_us_customize_builtin/)
- Can't reuse custom object types across spaces; must recreate them manually per space (a litigation attorney with one space per case calls this a dealbreaker). — [Business PM setup](https://www.reddit.com/r/capacitiesapp/comments/1qdk214/how_i_use_capacities_for_business_project/)
- No object inheritance, no formulas/rollups (officially not planned). — [What's Not Next](https://capacities.io/roadmap/whats-not-next)
- Users want to hide default objects they never use (e.g. Page) to reduce accidental creation. — [Customize built-in objects](https://www.reddit.com/r/capacitiesapp/comments/1ryo3p9/it_is_time_capacities_let_us_customize_builtin/)

**Lumen implication:** allow extending built-in types (add properties, hide, alias) without forking them. Cross-space type sharing is a cheap win. Formulas can wait.

### 11. Collaboration is absent (and officially not planned)

- Capacities: "Collaboration & Team plans… on hold" ([What's Not Next](https://capacities.io/roadmap/whats-not-next)).
- Users mostly accept it; the few who need it use Notion. One attorney: "Capacities is non-collaborative for me. I've never shared a note and frankly don't know how I would." — [Business PM setup](https://www.reddit.com/r/capacitiesapp/comments/1qdk214/how_i_use_capacities_for_business_project/)
- View-only sharing exists on the free plan, but real-time co-editing does not.

**Lumen implication:** do not chase real-time collaboration. A read-only share link and a static export are enough for the target user.

### 12. Support responsiveness

- Several App Store reviews describe unanswered emails, Discord-only support, and multi-month login lockouts.
- Reddit threads usually get a team reply, which softens the blow, but email support is the weak link.
- "I sent a bug a day ago and still have not received any response… Such things are unacceptable for serious services." — [Why I was excited…](https://www.reddit.com/r/capacitiesapp/comments/1ftk8cy/why_i_was_excited_about_capacities_but_stopped/)

**Lumen implication:** self-hosted users will debug more themselves, but a public issue tracker + fast release cadence is a trust signal. Do not rely on Discord as the only support channel.

### 13. Long-tail bugs that erode confidence

- Spell check duplicates words "every time" — [thread](https://www.reddit.com/r/capacitiesapp/comments/1u3ac89/automatic_spell_check_repeats_the_word_twice/).
- Android: deleting a link repastes it — [thread](https://www.reddit.com/r/capacitiesapp/comments/1qmg4za/bug_where_the_link_repastes_itself_when_trying_to/).
- Windows file links broke after an update; users must copy-paste paths manually — [MCP thread comment](https://www.reddit.com/r/capacitiesapp/comments/1t6u9nh/am_i_crazy_for_being_really_disappointed_with_the/).
- iOS: cannot create object types; iPad window resize broken; share sheet broken for months.

**Lumen implication:** a public "known issues" page and a fast patch cadence turn these from trust killers into proof of responsiveness.

---

## (c) Wishlist — what users ask for, why, and where

Ordered roughly by frequency × intensity.

| # | Wish | Why users want it | Source |
|---|------|-------------------|--------|
| 1 | **Reminders / push notifications** | Tasks are useless for time-sensitive items without an alert; users keep Todoist/TickTick just for this | [Tasks thread](https://www.reddit.com/r/capacitiesapp/comments/1u95g59/how_is_task_management_in_capacities/), [Recurring tasks](https://www.reddit.com/r/capacitiesapp/comments/1t5zegx/love_the_new_recurring_tasks_this_is_amazing/), [Reminders thread](https://www.reddit.com/r/capacitiesapp/comments/1x0y70/when_are_reminders_being_released/) |
| 2 | **True offline-first / local-only mode; disable sync** | Corporate/regulated users cannot upload notes to a cloud; "offline support" still syncs when online | [Truly offline mode?](https://www.reddit.com/r/capacitiesapp/comments/1qfiqul/truly_offline_mode_exist/), [Looking at using Capacities](https://www.reddit.com/r/capacitiesapp/comments/1ra2k8d/looking_at_using_capacities/) |
| 3 | **Self-hosting + E2EE** | Data sovereignty, GDPR, sensitive work; team says no to both | [Privacy concerns](https://www.reddit.com/r/capacitiesapp/comments/1s2rpf9/privacy_concerns/), [What's Not Next](https://capacities.io/roadmap/whats-not-next) |
| 4 | **Full CRUD API / MCP** | Claude Code/Codex workflows; agents that can create, update and delete objects | [MCP disappointment](https://www.reddit.com/r/capacitiesapp/comments/1t6u9nh/am_i_crazy_for_being_really_disappointed_with_the/), [API 2.0](https://www.reddit.com/r/capacitiesapp/comments/1tq66fb/api_20_in_progress/) |
| 5 | **Mobile capture parity** | Share sheet, widgets, Shortcuts, quick add, create/edit object types on phone | [Mobile thread](https://www.reddit.com/r/capacitiesapp/comments/1sh6gxk/the_mobile_app_makes_me_want_to_rip_my_hair_out/), [iOS widgets](https://www.reddit.com/r/capacitiesapp/comments/1tzj3ax/are_ios_mobile_widgets_on_the_roadmap/) |
| 6 | **Whiteboards / canvas** | Visual thinking, diagrams, spatial mapping; was a top request for years (shipped Oct 2026) | [Whiteboards](https://www.reddit.com/r/capacitiesapp/comments/1xfvzwn/surprise_whiteboards_in_capacities_out_now/) |
| 7 | **Version history** | Fear of accidental deletion; audit trail; "mandatory for any serious app" (now Pro) | [Lost content](https://www.reddit.com/r/capacitiesapp/comments/1ovyumq/i_lost_all_the_content_of_one_of_my_pages_for_no/) |
| 8 | **Task depth**: templates, "someday", carry-over, deadlines, time tracking | Daily planning needs more than a list; users want to drop Todoist entirely | [Tasks thread](https://www.reddit.com/r/capacitiesapp/comments/1prio55/what_are_your_thoughts_a_week_after_the/), [Task management via MCP](https://www.reddit.com/r/capacitiesapp/comments/1t2zv3r/task_management_via_mcp_ticktick_replacement/) |
| 9 | **Customize built-in objects / object inheritance / cross-space types** | Built-ins are too rigid; recreating types per space is tedious | [Customize built-ins](https://www.reddit.com/r/capacitiesapp/comments/1ryo3p9/it_is_time_capacities_let_us_customize_builtin/) |
| 10 | **Formulas / rollups** | Notion-style computed properties; officially not planned | [What's Not Next](https://capacities.io/roadmap/whats-not-next) |
| 11 | **Search operators / AND default** | Precision; OR search is noisy | [Search 2.0](https://www.reddit.com/r/capacitiesapp/comments/1qof9wx/search_20_is_here_and_much_more/) |
| 12 | **Export that preserves links/relations; import from Notion/Obsidian/Evernote** | Portability and migration; bulk import shipped Jun 2026 | [Export links](https://www.reddit.com/r/capacitiesapp/comments/1on9zgd/preserving_links_between_objects_when_exporting/), [Bulk import release](https://www.reddit.com/r/capacitiesapp/comments/1tts96o/new_release_bulk_import_is_here_for_everyone/) |
| 13 | **Cheaper mid tier / student / regional / lifetime** | Price sensitivity; students and non-US users churn | [Black Friday](https://www.reddit.com/r/capacitiesapp/comments/1p4egqh/will_there_be_a_black_friday_deal/), [I love Capacities](https://www.reddit.com/r/capacitiesapp/comments/1phhb9e/i_love_capacities/) |
| 14 | **Collaboration / teams** | Shared projects; officially not planned | [What's Not Next](https://capacities.io/roadmap/whats-not-next) |
| 15 | **iPad handwriting, flashcards/spaced repetition, RTL, plugins** | Niche but vocal; all officially "not next" | [What's Not Next](https://capacities.io/roadmap/whats-not-next), [Whiteboards comments](https://www.reddit.com/r/capacitiesapp/comments/1xfvzwn/surprise_whiteboards_in_capacities_out_now/) |
| 16 | **Offline web-page archiving / reader mode** | Read-later users want the article preserved, not just a link | [Love the recent updates](https://www.reddit.com/r/capacitiesapp/comments/1wwao65/love_the_recent_updates_and_workflow_capabilities/) |
| 17 | **PDF annotations / highlights** | Researchers want to annotate in-app, not just attach PDFs | [Annotations feature?](https://www.reddit.com/r/capacitiesapp/comments/1takbs8/would_other_people_use_an_annotations_feature/) |
| 18 | **Calendar drag-and-drop / time blocking** | Daily planning; current calendar is read-only-ish | [Tasks thread](https://www.reddit.com/r/capacitiesapp/comments/1u95g59/how_is_task_management_in_capacities/) |

---

## (d) Competitor trade-offs — why people choose something else (and why they come back)

The recurring axis is **power vs friction**, with a second axis **ownership vs maintenance**.

| Tool | What pulls people there (vs Capacities) | What pulls them back to Capacities | Typical user |
|------|----------------------------------------|-----------------------------------|--------------|
| **Obsidian** | Local Markdown files, E2EE, plugin ecosystem, Canvas/Excalidraw, free, Claude Code/Codex can read/write the vault, speed at scale | Too much tinkering, plugin maintenance, sync setup/cost, mobile and sync need configuration, no native objects/daily-note loop | Technical users, developers, "local-first" advocates, AI tinkerers |
| **Notion** | Real databases, formulas/rollups, collaboration, integrations, maturity, templates | Slow, bloated, setup rabbit hole, cloud-only, "identity crisis", not individual-first | Teams, database-heavy users, people who share work |
| **Anytype** | Local-first + E2EE + object model + free; "more secure than Notion"; one user found it faster than Capacities | Steeper learning curve, less polished, smaller ecosystem, different editor | Privacy/self-host crowd, org-blocked users |
| **Tana** | Supertags, outliner flexibility, powerful navigation | Poor search, weak mobile, complexity, pivot to business users | Outliner nerds, GTD power users |
| **Logseq** | Outliner, local files, journals, free | Weaker editor/UX, less structure, smaller ecosystem | Outliner/journalers |
| **Craft** | Beautiful native apps, excellent iOS, polished editing | Different philosophy, less object/database depth, subscription | Apple-first users, writers |
| **Heptabase** | Whiteboard + PDF annotation, visual research | Less note/object depth; Capacities "better if you don't need whiteboard" | Visual researchers |
| **Mem** | AI-first, low-friction capture | Limited multi-object structure, AI-centric | AI-native note takers |
| **Vasta (OSS)** | Self-hosted, local-first, open source (AGPL), Capacities importer, one container + SQLite | Alpha, no native mobile app, no Markdown files yet, single maintainer | Self-hosters, OSS advocates, Capacities refugees |
| **Apple Notes / Google Keep** | Zero friction, perfect mobile, free | No structure, no links, no PKM | People who gave up on PKM |

Key quotes:

- **Obsidian → Capacities:** "I spent too much time working *on* Obsidian rather than *in* it." — [Why did you choose Capacities…](https://www.reddit.com/r/capacitiesapp/comments/1wqllus/why_did_you_choose_capacities_over_notion_or/)
- **Capacities → Obsidian:** "Once Obsidian released Bases, there was no justification to continue paying for smart queries in an increasingly slow app." — [I think I might leave…](https://www.reddit.com/r/capacitiesapp/comments/1q4bbp0/i_think_i_might_leave_the_app_and_go_back_to/)
- **Capacities → Obsidian (AI):** "The ability to do almost anything with Claude/Codex far outweighs the downside." — [MCP thread](https://www.reddit.com/r/capacitiesapp/comments/1t6u9nh/am_i_crazy_for_being_really_disappointed_with_the/)
- **Notion → Capacities:** "Notion targets companies. Capacities targets individuals. I feel like I'm the ideal user when I use Capacities but making do when I use Notion." — [Why did you choose Capacities…](https://www.reddit.com/r/capacitiesapp/comments/1wqllus/why_did_you_choose_capacities_over_notion_or/)
- **Capacities → Anytype:** "My org blocked it due to the AI integration, so back to Anytype I go. It's so much faster." — [Performance thread](https://www.reddit.com/r/capacitiesapp/comments/1uijicx/capacities_performance_broken_any_ideas/)
- **Obsidian user tempted by Capacities:** "I love the idea of local files and plain text, but found myself doing a lot of work using properties and Bases to essentially recreate Objects." — [What keeps you coming back?](https://www.reddit.com/r/capacitiesapp/comments/1p7isuj/what_keeps_you_coming_back/)

**The trade-off in one sentence:** Capacities wins the *average Tuesday*; Obsidian wins the *edge case* and the *ownership argument*; Notion wins the *database and the team*. Lumen can win by being Capacities-easy **and** Obsidian-owned, without Obsidian's setup tax.

---

## (e) Workflow archetypes — the use cases that drive retention

These are the stories users tell when they are not complaining. They are the retention engine.

### e.1 ADHD "dump first, structure later"

The most emotionally intense segment. Capacities is described as "ADHD-friendly" because it removes the blank-page/setup paralysis.

- Daily note as inbox; objects emerge later; tags as dashboards.
- "Significant or multiple friction points can be fatal to my productivity." — [After all, why do you prefer Capacities?](https://www.reddit.com/r/capacitiesapp/comments/1qvo2bg/after_all_why_do_you_prefer_capacities/)
- A 36-year-old recently diagnosed with ADHD calls it "one of the most promising, ADHD-friendly tools I've used" and plans to consolidate Readwise + Todoist into it. — [Love the recent updates](https://www.reddit.com/r/capacitiesapp/comments/1wwao65/love_the_recent_updates_and_workflow_capabilities/)
- Setup itself is "a big part of the fun" for some ADHD users — a double-edged sword: they enjoy building the system and may never use it. — [I love the idea…](https://www.reddit.com/r/capacitiesapp/comments/1tbg3nt/i_love_the_idea_of_capacities_but_i_cant_find/)

**Lumen needs:** instant capture, zero mandatory fields, forgiving structure, visible "today" surface, no modal dialogs.

### e.2 Student / researcher

- Courses, papers, authors, highlights, PDFs, Readwise/Kindle imports, Zettelkasten.
- Aliases are useful for researchers who refer to authors by surname. — [Search 2.0](https://www.reddit.com/r/capacitiesapp/comments/1qof9wx/search_20_is_here_and_much_more/)
- Students want a single place for assignments, notes and sources; reminders are the missing piece. — [Recurring tasks](https://www.reddit.com/r/capacitiesapp/comments/1t5zegx/love_the_new_recurring_tasks_this_is_amazing/)
- Price sensitivity is highest here; student discount (now 40% off annual Pro) matters.

**Lumen needs:** PDF/highlight ingestion, source/author objects, citation-friendly export, cheap/free tier.

### e.3 Consultant / PM / engineering manager

- Projects, phases, meetings, people, companies, tasks, queries, calendar sync, Raycast.
- "Projects at the center, Meetings, Tasks, and Pages for all else." — [Business PM setup](https://www.reddit.com/r/capacitiesapp/comments/1qdk214/how_i_use_capacities_for_business_project/)
- Meeting templates with embedded queries ("last 10 meetings for this project") are a killer pattern.
- They often keep Jira/Confluence for the team and use Capacities as their private brain.

**Lumen needs:** meeting objects, people/company objects, project dashboards, calendar import, quick capture from email/calendar.

### e.4 Lawyer / regulated knowledge worker

- Matter pages, meeting notes, research notes, document links (Hookmark), tasks.
- "Whenever I'm going to work on a matter, I generally turn to its page in Capacities first." — [Business PM setup](https://www.reddit.com/r/capacitiesapp/comments/1qdk214/how_i_use_capacities_for_business_project/)
- They need confidentiality and often cannot use cloud/AI; this is a natural Lumen user.
- Pain: recreating object types per space; no collaboration.

**Lumen needs:** local encryption at rest, per-space isolation, audit/version history, no AI, reliable export.

### e.5 Journaler / reflector

- Daily notes, long-form blocks, images, tags as mood/theme dashboards, "created today" review.
- "80% of my use case is journaling and archiving… I like arranging things in a way that looks good to me." — [Looking at using Capacities](https://www.reddit.com/r/capacitiesapp/comments/1ra2k8d/looking_at_using_capacities/)
- A user realized they had written daily notes for six months without noticing. — [Gosh, Capacities is beautiful](https://www.reddit.com/r/capacitiesapp/comments/1tdusr7/gosh_capacities_is_beautiful/)

**Lumen needs:** beautiful typography, image layout, date navigation, streaks/forgiveness, export to Markdown.

### e.6 GTD / task person

- Daily note + tasks + recurring + queries; often hybrid with Todoist/TickTick/Things for reminders.
- "Tasks are done when you are concentrating… Reminders are something that distracts you." — [GTD thread](https://www.reddit.com/r/capacitiesapp/comments/1qyc5jx/how_i_use_gtd_in_capacities/)
- The hybrid is stable but fragile: one missing reminder keeps a second subscription alive.

**Lumen needs:** GTD views (inbox/next/waiting/someday), reliable notifications, quick add, carry-over.

### e.7 Read-later / highlight collector

- Readwise, Kindle, web clipper, WhatsApp/Telegram links, screenshots.
- "Capacities saves my highlights and I can view them separately… I don't miss Readwise." — [Love the recent updates](https://www.reddit.com/r/capacitiesapp/comments/1wwao65/love_the_recent_updates_and_workflow_capabilities/)
- Pain: no offline web-page archiving; link rot; mobile share sheet.

**Lumen needs:** browser extension that saves clean article content + highlights locally, not just a URL.

### e.8 Life logger / creative cataloger

- Wine notes, songs, albums, workouts, RPG sessions, sports logs, recipes.
- "I track my work, my life, my workouts, my RPG sessions, the books I read, everything." — [Looking at using Capacities](https://www.reddit.com/r/capacitiesapp/comments/1ra2k8d/looking_at_using_capacities/)
- This is where custom objects shine and where users become evangelists.

**Lumen needs:** easy custom types, image/cover support, collections, fun icons/colors.

### e.9 Hybrid analog + digital

- Paper for thinking, Capacities for archive; "Do you hybrid?" thread. — [link](https://www.reddit.com/r/capacitiesapp/comments/1xbrbp9/do_you_hybrid_ie_use_an_analog_system_with/)
- Implication: capture must be fast enough to beat a notebook, and review must be pleasant enough to justify transcription.

### e.10 What users pair Capacities with (the "second app" map)

This is the clearest signal of where Capacities leaves a gap. Almost every serious user runs at least one other tool.

| Need | What they pair with | Why Capacities doesn't cover it |
|------|--------------------|----------------------------------|
| Reminders / notifications | Todoist, TickTick, Things, Sunsama, Google Tasks, Apple Reminders | No push notifications/reminders for tasks (as of Oct 2026) |
| Read-later / highlights | Readwise Reader, Kindle, Raindrop, MyMind, Capture | No offline article archiving; web clipper saves links, not clean article content |
| Quick mobile capture | Apple Notes, Google Keep, WhatsApp, Telegram, email | Mobile share sheet is unreliable; no widgets/Shortcuts |
| Calendar / time blocking | Google Calendar, Outlook, Apple Calendar | Calendar sync is one-way-ish; no drag-and-drop time blocking |
| File/document links | Hookmark (macOS), Finder/Explorer | No native file-system linking |
| AI / agents | Claude, ChatGPT, Claude Code, Codex, MCP clients | MCP/API limited; no local AI |
| Second brain / archive | Obsidian, Notion, Anytype | Different strengths: local files, databases, E2EE |
| Task management | TickTick, Todoist, Things | Task depth (templates, someday, carry-over, time tracking) |
| Screenshots / visual references | Raindrop, MyMind, Apple Photos | Image object exists but capture/display is weak |

**Lumen implication:** each row is a potential "replace one subscription" story. The highest-value consolidations are reminders (replace Todoist), read-later (replace Readwise), and quick capture (replace Apple Notes). Ship those three well and the "second app" map shrinks dramatically.

---

## (f) Self-hosting and privacy sentiment in PKM communities

### f.1 The direct evidence: people ask, Capacities says no

In March 2026 a user posted detailed privacy concerns in r/capacitiesapp: always-online requirement, data stored in app data, no E2EE, unclear audit evidence, and a request for **self-hosted data storage**. The co-founder answered directly:

- "If you don't want your data to be backed up to a server for synchronization across devices, we do not recommend using it."
- "Self-hosting is not an option at the moment, but you can also create a ticket for that."
- On E2EE: the official docs explain why they decided against it; "full end to end encryption" is on the **"will not be added"** list.
- The team also confirmed search is local and they do not use ElasticSearch/third parties, and that they do not train on user data.

Source: [Privacy concerns](https://www.reddit.com/r/capacitiesapp/comments/1s2rpf9/privacy_concerns/) and [What's Not Next](https://capacities.io/roadmap/whats-not-next).

The user's reply is the clearest statement of the unmet need: they clarified they meant self-hosting the data storage/server, not AI, and argued that the app's privacy posture was misleading to uninformed users. The thread is worth reading in full.

### f.2 The strongest signal: someone built a self-hosted Capacities clone

Oct 2026, r/NoteTaking: **"Made my own self-hosted notes app because Capacities was almost what I wanted"** ([thread](https://www.reddit.com/r/NoteTaking/comments/1xc0h5o/made_my_own_selfhosted_notes_app_because/)). The project is **Vasta** ([GitHub](https://github.com/ariedotme/vasta)):

- "Open-source, self-hosted alternative to Capacities and Notion: notes, tasks and daily notes as typed objects. Local-first, works offline."
- One container + one SQLite file; Capacities importer; browser extension, Telegram, desktop shortcut; AGPL-3.0; 40 stars, 39 open issues at time of reading.
- Its README explicitly contrasts Capacities ("closed source, your notes live on its servers") with Vasta ("runs on your computer or your server").

This is simultaneously **validation** (the niche is real and someone else sees it) and **competition** (a direct OSS alternative with a Capacities importer). Lumen's advantages: native Qt desktop, native Android, Go server, browser extension, no AI, and a more mature parity feature set. Vasta's advantage: already open source, one-container simplicity, and an importer.

### f.3 Work/cloud restrictions are a recurring churn trigger

- "Can't upload work notes to the cloud" → user wants true offline-first for work; recommended Anytype. — [Looking at using Capacities](https://www.reddit.com/r/capacitiesapp/comments/1ra2k8d/looking_at_using_capacities/)
- "My org blocked it due to the AI integration, so back to Anytype." — [Performance thread](https://www.reddit.com/r/capacitiesapp/comments/1uijicx/capacities_performance_broken_any_ideas/)
- A consultant notes that Capacities is often *allowed* by client IT **because it is German**, but still needs per-client approval. — [Business PM setup](https://www.reddit.com/r/capacitiesapp/comments/1qdk214/how_i_use_capacities_for_business_project/)

This is the wedge: regulated/enterprise-adjacent users cannot use cloud PKM, and "German company" is not enough — they need local control.

### f.4 Privacy is a reason people stay on Obsidian

- "Obsidian is E2EE and Capacities only has encryption in transit. Obsidian has personal data sovereignty using .md files." — [After all, why do you prefer Capacities?](https://www.reddit.com/r/capacitiesapp/comments/1qvo2bg/after_all_why_do_you_prefer_capacities/)
- "The more I understand about potential backdoor vulnerabilities by community plugins, the more I get concerned about Obsidian" — the counter-argument, showing the trade-off is not one-sided. Same thread.
- Obsidian users who move to Capacities cite EU/GDPR and "not funding US corps" as reasons; they are not indifferent to privacy, they are trading it for convenience.

### f.5 How big is the self-host demand?

Honest assessment:

- **Small in absolute numbers, high in intent.** The privacy thread, the Vasta launch and the work-restriction comments are a thin but consistent stream, not a mass movement.
- **High-value users:** technical, willing to run Docker, often influencers in their circles, and they evangelize.
- **The mass market still chooses convenience.** Most Capacities users accept the cloud; they complain about price and mobile, not E2EE.
- **The wedge is "no cloud, no AI, no subscription" for people who already self-host** (Homelab, r/selfhosted, r/DataHoarder) plus regulated workers. Do not expect this to be the majority of Lumen's users; expect it to be the reason the first 1,000 users trust you.

**Lumen positioning that matches the evidence:**

1. "Your notes live on your server, in a database you control and can back up yourself." (Directly answers the privacy thread.)
2. "No AI, no training, no third-party search." (Directly answers the AI/cloud anxiety.)
3. "Import your Capacities space in one step; export to Markdown with links intact." (Directly answers lock-in and competes with Vasta.)
4. "Mobile capture that works offline and syncs when you're back." (Directly answers the #1 complaint.)
5. "Self-host it yourself — no per-seat subscription, no cloud lock-in." (Directly answers pricing fatigue, without promising a hosted plan Lumen may not offer.)

---

## Appendix A — Source index

### r/capacitiesapp (selected; all read in full or via top comments)

| Thread | Date | Why it matters |
|--------|------|----------------|
| [Why I was excited about Capacities but stopped using it](https://www.reddit.com/r/capacitiesapp/comments/1ftk8cy/why_i_was_excited_about_capacities_but_stopped/) | 2024-10 | The canonical churn post: sync, backups, version history, performance, pricing |
| [I used Capacities for a month](https://www.reddit.com/r/capacitiesapp/comments/1h417p0/i_used_capacities_for_a_month_here_are_my_thoughts/) | 2024-12 | Positive counterpoint: low friction, mobile/tablet, link saving |
| [After all, why do you prefer Capacities?](https://www.reddit.com/r/capacitiesapp/comments/1qvo2bg/after_all_why_do_you_prefer_capacities/) | 2026-02 | Best single thread on power vs friction |
| [Why did you choose Capacities over Notion or Obsidian?](https://www.reddit.com/r/capacitiesapp/comments/1wqllus/why_did_you_choose_capacities_over_notion_or/) | 2026-09 | Competitor trade-offs, object persistence, export reassurance |
| [I think I might leave the app and go back to Obsidian/Notion](https://www.reddit.com/r/capacitiesapp/comments/1q4bbp0/i_think_i_might_leave_the_app_and_go_back_to/) | 2026-01 | Churn reasons: slow, paywalled queries, ownership, no whiteboards |
| [The mobile app makes me want to rip my hair out](https://www.reddit.com/r/capacitiesapp/comments/1sh6gxk/the_mobile_app_makes_me_want_to_rip_my_hair_out/) | 2026-04 | Mobile capture failures, search, share sheet |
| [An idea to make the mobile app better](https://www.reddit.com/r/capacitiesapp/comments/1pipvm1/an_idea_to_make_the_mobile_app_better/) | 2025-12 | Android "almost unusable"; parity expectations |
| [2 months in: great object philosophy, clunky UI, & the Claude hole](https://www.reddit.com/r/capacitiesapp/comments/1s6qhhb/2_months_in_with_capacities_great_object/) | 2026-03 | UI friction + AI/Claude Code gap |
| [Capacities gets almost everything right — except one major detail](https://www.reddit.com/r/capacitiesapp/comments/1piq3ls/capacities_gest_almost_everything_exactly_right/) | 2025-12 | Collections vs queries confusion; paywall blind spot |
| [The free version is not OCD-friendly](https://www.reddit.com/r/capacitiesapp/comments/1qux5x6/the_free_version_of_capacities_is_not_ocdfriendly/) | 2026-02 | Free-tier limits; icons; "pay" backlash |
| [Is Capacities Pro worth it?](https://www.reddit.com/r/capacitiesapp/comments/1r9r9s5/is_capacities_pro_worth_it/) | 2026-02 | Price/value split; "I don't care about AI" |
| [Will there be a Black Friday deal?](https://www.reddit.com/r/capacitiesapp/comments/1p4egqh/will_there_be_a_black_friday_deal/) | 2025-11 | Pricing rigidity; mid-tier request |
| [Privacy concerns](https://www.reddit.com/r/capacitiesapp/comments/1s2rpf9/privacy_concerns/) | 2026-03 | Self-hosting/E2EE; team's definitive answer |
| [Truly offline mode exist?](https://www.reddit.com/r/capacitiesapp/comments/1qfiqul/truly_offline_mode_exist/) | 2026-01 | No local-only mode; sync cannot be disabled |
| [My Workspace is Gone](https://www.reddit.com/r/capacitiesapp/comments/1v4p5pf/my_workspace_is_gone/) | 2026-07 | Data-loss trust event |
| [I lost all the content of one of my pages](https://www.reddit.com/r/capacitiesapp/comments/1ovyumq/i_lost_all_the_content_of_one_of_my_pages_for_no/) | 2025-11 | Toggle-block data loss; versioning demand |
| [Disappointed after losing files, advice on switching](https://www.reddit.com/r/capacitiesapp/comments/1vxuo70/disappointed_after_losing_files_advice_on/) | 2026-08 | Churn to Obsidian; plugin workarounds |
| [Capacities performance broken](https://www.reddit.com/r/capacitiesapp/comments/1uijicx/capacities_performance_broken_any_ideas/) | 2026-06 | Freezes, 26 MB export, org blocked AI |
| [Persistent Slowness](https://www.reddit.com/r/capacitiesapp/comments/1viev7w/persistent_slowness/) | 2026-08 | Property creation lag, scrolling |
| [Am I crazy for being really disappointed with the MCP approach?](https://www.reddit.com/r/capacitiesapp/comments/1t6u9nh/am_i_crazy_for_being_really_disappointed_with_the/) | 2026-05 | API/MCP limits; Claude Code churn |
| [Tools like Claude Code are putting PKM apps in danger](https://www.reddit.com/r/capacitiesapp/comments/1qiwlw8/tools_like_claude_code_and_claude_cowork_are/) | 2026-01 | Strategic AI threat; API demand |
| [It is time Capacities let us customize built-in objects](https://www.reddit.com/r/capacitiesapp/comments/1ryo3p9/it_is_time_capacities_let_us_customize_builtin/) | 2026-03 | Customization limits |
| [What keeps you coming back?](https://www.reddit.com/r/capacitiesapp/comments/1p7isuj/what_keeps_you_coming_back/) | 2025-11 | Pure "loved features" thread |
| [How is task management in Capacities?](https://www.reddit.com/r/capacitiesapp/comments/1u95g59/how_is_task_management_in_capacities/) | 2026-06 | Tasks love + reminders gap |
| [Thoughts a week after tasks](https://www.reddit.com/r/capacitiesapp/comments/1prio55/what_are_your_thoughts_a_week_after_the/) | 2025-12 | Task wishlist: recurring, someday, carry-over |
| [How I use GTD in Capacities](https://www.reddit.com/r/capacitiesapp/comments/1qyc5jx/how_i_use_gtd_in_capacities/) | 2026-02 | GTD workflow, tasks vs reminders philosophy |
| [How I use Capacities for business PM](https://www.reddit.com/r/capacitiesapp/comments/1qdk214/how_i_use_capacities_for_business_project/) | 2026-01 | PM/lawyer workflows, cross-space types, Raycast |
| [How I use Capacities for task/project management + Zettelkasten](https://www.reddit.com/r/capacitiesapp/comments/1q6k372/how_i_use_capacities_for_task_and_project/) | 2026-01 | Power-user workflow, long-note performance |
| [Looking at using Capacities](https://www.reddit.com/r/capacitiesapp/comments/1ra2k8d/looking_at_using_capacities/) | 2026-02 | Onboarding, journaling, work-cloud restriction |
| [I love the idea but can't find enough references](https://www.reddit.com/r/capacitiesapp/comments/1tbg3nt/i_love_the_idea_of_capacities_but_i_cant_find/) | 2026-05 | ADHD setup culture; example object systems |
| [How to organise and maximise](https://www.reddit.com/r/capacitiesapp/comments/1so78sw/how_to_organise_and_maximise_the_use_of/) | 2026-04 | Micro-spaces, queries, sidebar |
| [Search 2.0 is here](https://www.reddit.com/r/capacitiesapp/comments/1qof9wx/search_20_is_here_and_much_more/) | 2026-01 | Search relevance, aliases, E2EE question |
| [Whiteboards out now](https://www.reddit.com/r/capacitiesapp/comments/1xfvzwn/surprise_whiteboards_in_capacities_out_now/) | 2026-10 | Delight; handwriting request |
| [Global graph view](https://www.reddit.com/r/capacitiesapp/comments/1x5jec6/capacities_just_casually_added_global_graph_view/) | 2026-10 | Graph sentiment |
| [Love the recent updates and workflow capabilities](https://www.reddit.com/r/capacitiesapp/comments/1wwao65/love_the_recent_updates_and_workflow_capabilities/) | 2026-10 | ADHD, PDF/voice, Readwise replacement |
| [Love the new recurring tasks](https://www.reddit.com/r/capacitiesapp/comments/1t5zegx/love_the_new_recurring_tasks_this_is_amazing/) | 2026-05 | Reminders demand |
| [For those who use the free version](https://www.reddit.com/r/capacitiesapp/comments/1xdnoum/for_those_who_use_the_free_version_of_capacities/) | 2026-10 | Free tier is generous; low Pro conversion |
| [Gosh, Capacities is beautiful](https://www.reddit.com/r/capacitiesapp/comments/1tdusr7/gosh_capacities_is_beautiful/) | 2026-05 | Emotional attachment, EU values |
| [App literally changed my life](https://www.reddit.com/r/capacitiesapp/comments/1olbxuy/app_literally_changed_my_life/) | 2025-11 | Evangelism; Android complaint |
| [I love Capacities](https://www.reddit.com/r/capacitiesapp/comments/1phhb9e/i_love_capacities/) | 2025-12 | Pricing/regional pricing pain inside a love post |

### Cross-subreddit

- r/PKMS: [Capacities is awesome and super easy to use](https://www.reddit.com/r/PKMS/comments/1mj0tlm/capacities_is_awesome_and_super_easy_to_use/) (2025-08)
- r/PKMS: [Leaning to stick with Capacities](https://www.reddit.com/r/PKMS/comments/1hpas5i/leaning_to_stick_with_capacities_a_journey_of/) (2024-12)
- r/PKMS: [What are your thoughts on Capacities?](https://www.reddit.com/r/PKMS/comments/1u3ayg1/what_are_your_thoughts_on_capacities/) (2026-06)
- r/PKMS: [Thoughts about Capacities](https://www.reddit.com/r/PKMS/comments/1qfv3y5/thoughts_about_capacities/) (2026-01)
- r/Notion: [Anyone has tried Capacities.io?](https://www.reddit.com/r/Notion/comments/129edd8/anyone_has_tried_capacitiesio/) (2023)
- r/ObsidianMD: [Looking to move away from Capacities to Obsidian](https://www.reddit.com/r/ObsidianMD/comments/1psm2vk/looking_to_move_away_from_capacities_to_obsidian/) (2025-12)
- r/ObsidianMD: [Moving from Capacities but facing friction](https://www.reddit.com/r/ObsidianMD/comments/1vrjabi/moving_from_capacities_but_facing_friction/) (2026-08)
- r/NoteTaking: [Made my own self-hosted notes app because Capacities was almost what I wanted](https://www.reddit.com/r/NoteTaking/comments/1xc0h5o/made_my_own_selfhosted_notes_app_because/) (2026-10)

### Non-Reddit

- Apple App Store, Capacities – Notes & PKM, 40 most recent US reviews: <https://apps.apple.com/us/app/capacities-notes-pkm/id1670188548>
- Hacker News launch thread (2022): <https://news.ycombinator.com/item?id=31604728>
- Hacker News "Capacities" (2023): <https://news.ycombinator.com/item?id=37384962>
- Hacker News Tolaria comparison (2026): <https://news.ycombinator.com/item?id=47886775>
- Capacities pricing: <https://capacities.io/pricing>
- Capacities "What's Not Next?": <https://capacities.io/roadmap/whats-not-next>
- Vasta (OSS self-hosted Capacities alternative): <https://github.com/ariedotme/vasta>

---

## Appendix B — Quick sentiment scoreboard

| Dimension | Signal | Confidence |
|-----------|--------|------------|
| Overall satisfaction (App Store, recent 40) | 3.50/5 average; 20/40 are 4–5★, 20/40 are 1–3★ | Medium (small sample, US only) |
| Desktop/web satisfaction | High; "life-changing", "best PKM" | High |
| Mobile satisfaction | Low; 24/40 reviews mention the mobile app, mostly negatively | High |
| Pricing sentiment | Polarized; value complaints concentrated on paywalled queries/tasks | High |
| Trust/reliability | Fragile; data-loss stories are rare but devastating | Medium |
| AI sentiment | Mostly indifferent-to-negative in this community; a vocal pro-AI minority | Medium |
| Self-host demand | Niche, high-intent, already has an OSS competitor | Medium |
| Switching volatility | High; many users run 2–3 PKM apps in parallel | High |

---

## Appendix C — What this means for Lumen's next phase (actionable summary)

**Must-win (parity + trust):**

1. **Mobile capture reliability** — share target, quick capture to daily note, offline queue, search-to-block. This is the #1 complaint and Lumen's best chance to differentiate.
2. **Local-first data safety** — automatic snapshots, per-object version history, one-click restore, visible backup status. Turn Capacities' weakest area into Lumen's strongest.
3. **Search that behaves** — AND-by-default, operators, instant local results, jump-to-block.
4. **Tasks with real reminders** — desktop + Android notifications, quick add, carry-over, "someday". This alone can replace Todoist for many users.
5. **Export/import that preserves links** — plus a Capacities importer (Vasta already has one; do not cede this).

**Should-win (differentiation):**

6. **Object model depth** — persistent types, collections, cross-space types, customizable built-ins, inline queries in templates.
7. **Daily note as the product** — default surface, zero-friction capture, review loop ("created today", weekly review).
8. **Whiteboard/canvas** — now table stakes after Capacities shipped it; a simple object-aware canvas is enough.
9. **Local API + CLI + optional MCP adapter** — capture the Claude Code/agentic crowd without adding AI to the core.
10. **Privacy story made concrete** — "your server, your files, no AI, no telemetry", with a backup/restore demo and a security page.

**Do-not-chase:**

- Real-time collaboration/teams (officially not planned by Capacities; low demand in this corpus).
- Formulas/rollups and object inheritance (power-user niche; high complexity).
- Handwriting, flashcards, RTL, plugins (all explicitly "not next" for Capacities; low pull).
- AI features in the core (the no-AI stance is a feature; keep AI as an optional external integration).

**Positioning sentence backed by the evidence:**

> "Capacities without the cloud, the subscription, or the mobile bugs: your objects, your daily notes, your server."

---

*End of report. All links verified at time of writing (2026-10-31). Reddit links may require login; thread IDs are stable.*
