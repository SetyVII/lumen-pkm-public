package main

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"io"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"net/url"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"
)

const testToken = "only-tests-012345678901234567890123456789"

func makeApp(t *testing.T) (*App, string, []byte) {
	t.Helper()
	path := filepath.Join(t.TempDir(), "cache.sqlite")
	key := bytes.Repeat([]byte{0x8e}, 32)
	store, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	app, err := NewApp(store, testToken, []string{"chrome-extension://allowed"})
	if err != nil {
		t.Fatal(err)
	}
	t.Cleanup(func() {
		if err := app.store.Fsck(); err != nil {
			t.Errorf("fsck after test: %v", err)
		}
		_ = app.store.Close()
	})
	return app, path, key
}
func api(t *testing.T, a *App, method, path string, body any) *httptest.ResponseRecorder {
	t.Helper()
	var reader io.Reader
	if body != nil {
		raw, err := json.Marshal(body)
		if err != nil {
			t.Fatal(err)
		}
		reader = bytes.NewReader(raw)
	}
	req := httptest.NewRequest(method, path, reader)
	req.Header.Set("Authorization", "Bearer "+testToken)
	req.Header.Set("Content-Type", "application/json")
	rr := httptest.NewRecorder()
	a.Handler().ServeHTTP(rr, req)
	return rr
}
func objectFrom(t *testing.T, rr *httptest.ResponseRecorder) Object {
	t.Helper()
	if rr.Code < 200 || rr.Code >= 300 {
		t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
	}
	var o Object
	if err := json.Unmarshal(rr.Body.Bytes(), &o); err != nil {
		t.Fatal(err)
	}
	return o
}

func TestAPIObjectsRelationsJournalPersistenceAndEncryption(t *testing.T) {
	app, path, key := makeApp(t)
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "SECRET_BOOK_SENTINEL_8412", "content": "Study notes", "properties": map[string]any{"rich_text": "<p><b>Study notes</b></p>"}}))
	if book.SyncStatus != "synced" || book.Revision != 1 {
		t.Fatalf("HTTP must acknowledge a durable local save: %+v", book)
	}
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-09", nil))
	journal = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+journal.ID, map[string]any{"content": "I read the book", "links": []string{book.ID}}))
	backlinks := api(t, app, "GET", "/v1/objects/"+book.ID+"/backlinks", nil)
	var linked []Object
	if err := json.Unmarshal(backlinks.Body.Bytes(), &linked); err != nil {
		t.Fatal(err)
	}
	if len(linked) != 1 || linked[0].ID != journal.ID {
		t.Fatal("backlinks were not persisted")
	}
	search := api(t, app, "GET", "/v1/search?q=SECRET_BOOK", nil)
	if !strings.Contains(search.Body.String(), book.ID) {
		t.Fatal("search missed saved title")
	}
	if err := app.store.Close(); err != nil {
		t.Fatal(err)
	}
	files, _ := filepath.Glob(path + "*")
	for _, file := range files {
		data, err := os.ReadFile(file)
		if err != nil {
			t.Fatal(err)
		}
		if bytes.Contains(data, []byte(book.Title)) || bytes.Contains(data, []byte("Study notes")) {
			t.Fatalf("plaintext leaked into %s", file)
		}
	}
	reopened, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	app.store = reopened
	restored := objectFrom(t, api(t, app, "GET", "/v1/objects/"+book.ID, nil))
	if restored.Title != book.Title || restored.Properties["rich_text"] != book.Properties["rich_text"] {
		t.Fatal("restore lost content or formatting")
	}
	if wrong, err := OpenStore(path, bytes.Repeat([]byte{9}, 32)); err == nil {
		wrong.Close()
		t.Fatal("different encryption key silently opened workspace")
	}
}

func TestObjectSurvivesRestartWithoutOutbox(t *testing.T) {
	app, path, key := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "idea", "title": "Offline idea"}))
	if o.SyncStatus != "synced" || o.SyncError != "" {
		t.Fatal("a local save must be durable and synced without an exporter")
	}
	health := api(t, app, "GET", "/health", nil)
	if !strings.Contains(health.Body.String(), `"pending":0`) {
		t.Fatal(health.Body.String())
	}
	app.store.Close()
	store, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	app.store = store
	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+o.ID, nil))
	if got.SyncStatus != "synced" || got.Title != o.Title {
		t.Fatal("object did not survive restart")
	}
	rows, err := app.store.List("outbox")
	if err != nil || len(rows) != 0 {
		t.Fatal("outbox must not exist in the local-only store")
	}
}

func TestTrashKeepsReferencesUntilPurge(t *testing.T) {
	app, _, _ := makeApp(t)
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Delete target"}))
	journal := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-09", nil))
	objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+journal.ID, map[string]any{"links": []string{book.ID}}))
	result := api(t, app, "DELETE", "/v1/objects/"+book.ID, nil)
	if result.Code != 200 {
		t.Fatal(result.Body.String())
	}
	kept := objectFrom(t, api(t, app, "GET", "/v1/objects/"+journal.ID, nil))
	if len(kept.Links) != 1 || kept.Links[0] != book.ID || kept.SyncStatus != "synced" {
		t.Fatalf("trash dropped a restorable reference: %+v", kept.Links)
	}
	if purged := api(t, app, "DELETE", "/v1/trash/"+book.ID, nil); purged.Code != 200 {
		t.Fatal(purged.Body.String())
	}
	clean := objectFrom(t, api(t, app, "GET", "/v1/objects/"+journal.ID, nil))
	if len(clean.Links) != 0 || clean.SyncStatus != "synced" {
		t.Fatalf("purge left a dangling reference: %+v", clean)
	}
}

func TestAuthenticationAndExactCORS(t *testing.T) {
	app, _, _ := makeApp(t)
	for _, path := range []string{"/v1/types", "/v1/objects", "/v1/assets/anything/content"} {
		rr := httptest.NewRecorder()
		app.Handler().ServeHTTP(rr, httptest.NewRequest("GET", path, nil))
		if rr.Code != 401 {
			t.Fatalf("unauthenticated %s returned %d", path, rr.Code)
		}
		if rr.Header().Get("Cache-Control") != "no-store" {
			t.Fatal("missing no-store")
		}
	}
	for origin, want := range map[string]int{"chrome-extension://allowed": 204, "https://evil.example": 403} {
		req := httptest.NewRequest("OPTIONS", "/v1/capture", nil)
		req.Header.Set("Origin", origin)
		rr := httptest.NewRecorder()
		app.Handler().ServeHTTP(rr, req)
		if rr.Code != want {
			t.Fatalf("origin %s returned %d", origin, rr.Code)
		}
		if want == 204 && rr.Header().Get("Access-Control-Allow-Origin") != origin {
			t.Fatal("CORS origin not exact")
		}
	}
}

func TestAssetEncryptionAndAuthenticatedMemoryDownload(t *testing.T) {
	app, path, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Attachment"}))
	body := bytes.NewBuffer(nil)
	writer := multipart.NewWriter(body)
	_ = writer.WriteField("object_id", o.ID)
	part, err := writer.CreateFormFile("file", "notes.pdf")
	if err != nil {
		t.Fatal(err)
	}
	sentinel := []byte("%PDF-1.4\nATTACHMENT_SECRET_SENTINEL_0912\n")
	_, _ = part.Write(sentinel)
	writer.Close()
	req := httptest.NewRequest("POST", "/v1/assets", body)
	req.Header.Set("Authorization", "Bearer "+testToken)
	req.Header.Set("Content-Type", writer.FormDataContentType())
	rr := httptest.NewRecorder()
	app.Handler().ServeHTTP(rr, req)
	if rr.Code != 201 {
		t.Fatalf("upload: %s", rr.Body.String())
	}
	var asset Asset
	json.Unmarshal(rr.Body.Bytes(), &asset)
	if asset.MediaObjectID == "" {
		t.Fatal("PDF upload did not expose its reusable media object")
	}
	media := objectFrom(t, api(t, app, "GET", "/v1/objects/"+asset.MediaObjectID, nil))
	owner := objectFrom(t, api(t, app, "GET", "/v1/objects/"+o.ID, nil))
	if media.TypeID != "pdf" || media.Properties["asset_id"] != asset.ID || len(owner.Links) != 1 || owner.Links[0] != media.ID {
		t.Fatal("media ID, blob ID and owner relation were confused")
	}
	listed := api(t, app, "GET", "/v1/assets?object_id="+media.ID, nil)
	if !strings.Contains(listed.Body.String(), asset.ID) {
		t.Fatal("reusable medium could not resolve its attachment")
	}
	download := api(t, app, "GET", "/v1/assets/"+asset.ID+"/content", nil)
	if !bytes.Equal(download.Body.Bytes(), sentinel) {
		t.Fatal("asset bytes changed")
	}
	if download.Header().Get("Cache-Control") != "no-store" {
		t.Fatal("asset can be cached")
	}
	app.store.db.Exec("PRAGMA wal_checkpoint(TRUNCATE)")
	data, _ := os.ReadFile(path)
	if bytes.Contains(data, sentinel) {
		t.Fatal("asset stored plaintext")
	}
}

func TestConcurrentCreatesAndCaptureJournal(t *testing.T) {
	app, _, _ := makeApp(t)
	var wg sync.WaitGroup
	for i := 0; i < 12; i++ {
		wg.Add(1)
		go func(index int) {
			defer wg.Done()
			o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": fmt.Sprintf("Concurrent %d", index)}))
			if o.ID == "" {
				t.Error("missing id")
			}
		}(i)
	}
	wg.Wait()
	objects, err := app.objects()
	if err != nil || len(objects) != 12 {
		t.Fatalf("lost concurrent save: %d, %v", len(objects), err)
	}
	o := objectFrom(t, api(t, app, "POST", "/v1/capture", map[string]any{"selection": "Read carefully", "title": "Captured text", "date": "2026-10-09"}))
	j := objectFrom(t, api(t, app, "GET", "/v1/journal/2026-10-09", nil))
	if len(j.Links) != 1 || j.Links[0] != o.ID {
		t.Fatal("capture did not link journal")
	}
	bad := api(t, app, "POST", "/v1/capture", map[string]any{"selection": "Bad date", "date": "2026-99-99"})
	if bad.Code != 400 {
		t.Fatal("invalid date accepted")
	}
}

func TestSSRFURLDNSAndRedirectGuards(t *testing.T) {
	for _, raw := range []string{"file:///etc/passwd", "http://127.0.0.1/", "https://user:secret@example.com", "http://169.254.169.254/latest/meta-data/", "http://[::1]/", "http://[::ffff:127.0.0.1]/", "http://10.1.2.3/", "https://example.com:8443/", "http://100.64.0.1/", "http://198.18.0.1/"} {
		if _, err := safeURL(raw); err == nil {
			t.Errorf("unsafe URL accepted: %s", raw)
		}
	}
	for _, raw := range []string{"https://example.com/article", "http://8.8.8.8/"} {
		if _, err := safeURL(raw); err != nil {
			t.Error(err)
		}
	}
	for _, ip := range []string{"127.0.0.1", "192.168.0.1", "fe80::1", "2001:db8::1", "::ffff:10.0.0.1"} {
		if publicIP(netip.MustParseAddr(ip)) {
			t.Errorf("unsafe IP accepted %s", ip)
		}
	}
	f := newSecureFetcher()
	if _, _, _, err := f.fetch(context.Background(), "http://localhost/", 100, "html"); err == nil {
		t.Fatal("localhost DNS was not blocked")
	}
	u, _ := url.Parse("http://127.0.0.1/private")
	if err := f.client.CheckRedirect(&http.Request{URL: u}, []*http.Request{{}}); err == nil {
		t.Fatal("private redirect accepted")
	}
}

func TestOpenGraphParsing(t *testing.T) {
	p := parsePreview(strings.NewReader(`<html><head><title>Fallback</title><meta property="og:title" content="A &amp; B"><meta property="og:description" content="Read &amp; learn"><meta property="og:image" content="/image.png"><meta property="og:site_name" content="Library"></head></html>`))
	if p.Title != "A & B" || p.Description != "Read & learn" || p.Image != "/image.png" || p.SiteName != "Library" {
		t.Fatalf("invalid parsed preview: %+v", p)
	}
}

func TestAtomicAddLinksPreservesConcurrentRelationsAndDocument(t *testing.T) {
	app, _, _ := makeApp(t)
	original := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Original journal", "content": "Latest text", "properties": map[string]any{"rich_text": "<p>Latest text</p>"}}))
	ids := make([]string, 8)
	for i := range ids {
		ids[i] = objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": fmt.Sprintf("Link target %d", i)})).ID
	}
	var wg sync.WaitGroup
	for _, id := range ids {
		wg.Add(1)
		go func(id string) {
			defer wg.Done()
			rr := api(t, app, "PATCH", "/v1/objects/"+original.ID, map[string]any{"add_links": []string{id, id}})
			if rr.Code != 200 {
				t.Errorf("add relation: HTTP %d", rr.Code)
			}
		}(id)
	}
	wg.Wait()
	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+original.ID, nil))
	if len(got.Links) != len(ids) || got.Title != original.Title || got.Content != original.Content || got.Properties["rich_text"] != original.Properties["rich_text"] {
		t.Fatal("atomic relation union overwrote current content or lost concurrent links")
	}
	bad := api(t, app, "PATCH", "/v1/objects/"+original.ID, map[string]any{"add_links": []string{"missing-id"}})
	if bad.Code != 400 {
		t.Fatal("missing linked object accepted")
	}
}
