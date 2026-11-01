package main

import (
	"context"
	"net/url"
	"strings"
	"time"
)

// Shared capture pipeline pieces. POST /v1/capture and the developer API's
// POST /v1/objects/from-url must clean, deduplicate, fetch and link exactly
// the same way, so the steps live here and both endpoints call them. The
// fetch itself always happens through the SSRF-guarded secureFetcher.

// normalizeCaptureURL cleans a shared link exactly like POST /v1/capture:
// tracking parameters are stripped, then a reddit share link (/s/...) is
// resolved to its canonical post URL. Shared with the from-url endpoint so
// the two entry points deduplicate identically.
func (a *App) normalizeCaptureURL(ctx context.Context, raw string) (string, error) {
	cleaned, err := cleanURL(raw)
	if err != nil {
		return "", err
	}
	u, err := safeURL(cleaned)
	if err != nil {
		return "", err
	}
	if isRedditHost(u.Hostname()) && strings.Contains(u.Path, "/s/") {
		if resolved, resolveErr := a.fetcher.resolve(ctx, cleaned); resolveErr == nil {
			if canonical, cleanErr := cleanURL(resolved); cleanErr == nil {
				if safe, safeErr := safeURL(canonical); safeErr == nil {
					return safe.String(), nil
				}
			}
		}
	}
	return cleaned, nil
}

// findWebObjectByURL returns the live non-template web object that already
// saved this URL, if any. It is the dedupe shared by /v1/capture (which
// reports the hit as `duplicate: true`) and the developer from-url endpoint
// (which reports it as `existing: true`).
func (a *App) findWebObjectByURL(rawURL string) (Object, bool) {
	objects, err := a.objects()
	if err != nil {
		return Object{}, false
	}
	for _, existing := range objects {
		if existing.TypeID != "web" || isTemplateObject(existing) {
			continue
		}
		existingURL, _ := existing.Properties["url"].(string)
		if existingURL != "" && sameCleanURL(existingURL, rawURL) {
			return existing, true
		}
	}
	return Object{}, false
}

// capturePreview fetches the OpenGraph preview, the reader article and the
// preview image for a cleaned URL — the exact fetch pipeline of POST
// /v1/capture: reddit posts go through the three-step embed fallback,
// everything else through the plain preview fetch. The SSRF guards, redirect
// policy, size caps and the single-fetch reader extraction all live in the
// secureFetcher; this helper is the only way either endpoint reaches the
// network, so the fetch logic is never duplicated.
func (a *App) capturePreview(ctx context.Context, rawURL string) (Preview, error) {
	if u, parseErr := url.Parse(rawURL); parseErr == nil && isRedditHost(u.Hostname()) {
		return a.fetcher.PreviewReddit(ctx, rawURL)
	}
	return a.fetcher.Preview(ctx, rawURL)
}

// linkJournalObject ensures the journal for date exists (the same lazy
// semantics as GET /v1/journal/{date}) and links objectID to it without
// duplicating an existing relation. It returns the journal id. The caller
// holds writeMu.
func (a *App) linkJournalObject(ctx context.Context, date, objectID string) (string, error) {
	journal, err := a.ensureJournal(ctx, date, false)
	if err != nil {
		return "", err
	}
	for _, id := range journal.Links {
		if id == objectID {
			return journal.ID, nil
		}
	}
	journal.Links = append(journal.Links, objectID)
	journal.UpdatedAt = now()
	if err := a.saveObject(ctx, &journal); err != nil {
		return "", err
	}
	return journal.ID, nil
}

// todayJournalDate is the extension flow's journal target: the server-local
// current day, as a YYYY-MM-DD calendar date.
func todayJournalDate() string {
	return time.Now().Format("2006-01-02")
}
