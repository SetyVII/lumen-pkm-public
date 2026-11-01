package main

import (
	"context"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"strings"
	"sync"
	"sync/atomic"
	"testing"
	"time"
	"unicode/utf8"
)

// readerTestFetcher keeps the production SSRF policy (safeURL) but dials the
// httptest listener for any public-looking address. Tests therefore exercise
// the real capture pipeline without weakening the guard or touching the network.
func readerTestFetcher(server *httptest.Server) *secureFetcher {
	return &secureFetcher{client: &http.Client{
		Transport: &http.Transport{
			DialContext: func(ctx context.Context, network, _ string) (net.Conn, error) {
				var dialer net.Dialer
				return dialer.DialContext(ctx, "tcp", server.Listener.Addr().String())
			},
		},
		Timeout: 5 * time.Second,
	}}
}

func readerArticlePage(extra string) string {
	return `<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <title>The Quiet Web</title>
  <meta property="og:title" content="The Quiet Web">
  <meta property="og:description" content="A field guide to reading slowly.">
  <link rel="icon" type="image/png" sizes="32x32" href="/favicon.png">
  <script>window.tracker = "HEAD_SCRIPT_TOKEN";</script>
  <style>.hidden { display: none; }</style>
</head>
<body>
  <header class="site-header"><a href="/">Home</a><nav><a href="/about">About</a></nav></header>
  <nav class="breadcrumbs"><a href="/">Home</a> / Essays</nav>
  <main>
    <article class="post">
      <header>
        <h1>The Quiet Web</h1>
        <p class="byline">By Ada Reader</p>
      </header>
      <p>The web was once a place for <strong>slow reading</strong> and <em>careful thought</em>. See <a href="/notes">the notes</a>.</p>
      <h2>Three habits</h2>
      <ul>
        <li>Read one article at a time.</li>
        <li>Take notes by hand.
          <ul><li>Keep them short.</li></ul>
        </li>
      </ul>
      <blockquote><p>Attention is the rarest form of generosity.</p></blockquote>
      <pre><code class="language-go">fmt.Println("READER_CODE_TOKEN")</code></pre>
      <p>Search finds <mark>ZEBRA_READER_TOKEN_9271</mark> in this paragraph.</p>
      ` + extra + `
      <figure>
        <img src="/cover.png" alt="A quiet desk">
        <figcaption>A quiet desk.</figcaption>
      </figure>
      <table>
        <tr><th>Habit</th><th>Benefit</th></tr>
        <tr><td>Reading</td><td>Focus</td></tr>
      </table>
    </article>
  </main>
  <aside class="sidebar"><p>SIDEBAR_TOKEN</p></aside>
  <footer><p>FOOTER_TOKEN</p></footer>
  <div class="ad-container"><p>AD_TOKEN</p></div>
  <script>document.write("BODY_SCRIPT_TOKEN")</script>
</body>
</html>`
}

func TestExtractReaderContentStripsChromeAndKeepsMarkdown(t *testing.T) {
	base, _ := url.Parse("http://8.8.8.8/article")
	got := extractReaderContent([]byte(readerArticlePage(`<p>Extra paragraph.</p>`)), base)
	for _, want := range []string{
		"# The Quiet Web",
		"By Ada Reader",
		"**slow reading**",
		"*careful thought*",
		"[the notes](http://8.8.8.8/notes)",
		"## Three habits",
		"- Read one article at a time.",
		"- Take notes by hand.",
		"  - Keep them short.",
		"> Attention is the rarest form of generosity.",
		"```go\nfmt.Println(\"READER_CODE_TOKEN\")\n```",
		"ZEBRA_READER_TOKEN_9271",
		"![A quiet desk](http://8.8.8.8/cover.png)",
		"| Habit | Benefit |",
	} {
		if !strings.Contains(got, want) {
			t.Errorf("reader content is missing %q:\n%s", want, got)
		}
	}
	for _, unwanted := range []string{
		"HEAD_SCRIPT_TOKEN", "BODY_SCRIPT_TOKEN", "SIDEBAR_TOKEN", "FOOTER_TOKEN",
		"AD_TOKEN", "About", "Essays", "display: none",
	} {
		if strings.Contains(got, unwanted) {
			t.Errorf("reader content kept boilerplate %q:\n%s", unwanted, got)
		}
	}
}

func TestExtractReaderContentPrefersLargestArticleAndRoleMain(t *testing.T) {
	page := `<html><body>
		<article><h2>Teaser</h2><p>SHORT_TOKEN</p></article>
		<article><h2>Feature</h2><p>` + strings.Repeat("Long form prose. ", 40) + `LONG_TOKEN</p></article>
	</body></html>`
	got := extractReaderContent([]byte(page), nil)
	if !strings.Contains(got, "LONG_TOKEN") || strings.Contains(got, "SHORT_TOKEN") {
		t.Fatalf("largest article was not preferred:\n%s", got)
	}
	roleMain := `<html><body><div role="main"><p>ROLE_MAIN_TOKEN</p></div></body></html>`
	if got := extractReaderContent([]byte(roleMain), nil); !strings.Contains(got, "ROLE_MAIN_TOKEN") {
		t.Fatalf("role=main was ignored:\n%s", got)
	}
}

func TestExtractReaderContentDegradesGracefully(t *testing.T) {
	cases := map[string]string{
		"empty":      "",
		"chrome":     `<html><body><nav>Menu</nav><script>alert(1)</script><footer>Footer</footer></body></html>`,
		"whitespace": `<html><body>   <div>  </div> </body></html>`,
	}
	for name, page := range cases {
		if got := extractReaderContent([]byte(page), nil); got != "" {
			t.Errorf("%s page produced %q, want empty", name, got)
		}
	}
	malformed := `<html><body><article><p>Hello <b>world`
	if got := extractReaderContent([]byte(malformed), nil); !strings.Contains(got, "Hello **world**") {
		t.Fatalf("malformed HTML lost text: %q", got)
	}
}

func TestExtractReaderContentTruncationCap(t *testing.T) {
	page := `<html><body><article><p>` + strings.Repeat("é", 300*1024) + `</p></article></body></html>`
	got := extractReaderContent([]byte(page), nil)
	if len(got) > readerContentLimit {
		t.Fatalf("reader content exceeds cap: %d > %d", len(got), readerContentLimit)
	}
	if len(got) < readerContentLimit-4 {
		t.Fatalf("reader content was truncated too early: %d", len(got))
	}
	if !utf8.ValidString(got) {
		t.Fatal("truncation split a UTF-8 rune")
	}
}

func TestCaptureStoresReaderContentAndSearchFindsIt(t *testing.T) {
	var requests atomic.Int32
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests.Add(1)
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		_, _ = w.Write([]byte(readerArticlePage(`<p>Extra paragraph.</p>`)))
	}))
	defer server.Close()
	app, _, _ := makeApp(t)
	app.fetcher = readerTestFetcher(server)

	o := objectFrom(t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": "http://8.8.8.8/article", "title": "Saved link",
	}))
	if o.TypeID != "web" {
		t.Fatalf("capture type = %q", o.TypeID)
	}
	reader, _ := o.Properties["reader_content"].(string)
	if !strings.Contains(reader, "# The Quiet Web") || !strings.Contains(reader, "ZEBRA_READER_TOKEN_9271") {
		t.Fatalf("capture did not store the article:\n%s", reader)
	}
	if strings.Contains(reader, "SIDEBAR_TOKEN") || strings.Contains(reader, "BODY_SCRIPT_TOKEN") {
		t.Fatalf("capture stored boilerplate:\n%s", reader)
	}
	if o.Properties["favicon"] != "http://8.8.8.8/favicon.png" {
		t.Fatalf("capture favicon = %v", o.Properties["favicon"])
	}
	if requests.Load() != 1 {
		t.Fatalf("capture fetched the page %d times, want 1", requests.Load())
	}

	search := api(t, app, "GET", "/v1/search?q=zebra_reader_token_9271", nil)
	if !strings.Contains(search.Body.String(), o.ID) {
		t.Fatal("search did not match reader_content")
	}
	chrome := api(t, app, "GET", "/v1/search?q=sidebar_token", nil)
	if strings.Contains(chrome.Body.String(), o.ID) {
		t.Fatal("search matched text that extraction had stripped")
	}
}

func TestCaptureFailureStoresEmptyReaderContent(t *testing.T) {
	notFound := httptest.NewServer(http.NotFoundHandler())
	defer notFound.Close()
	app, _, _ := makeApp(t)
	app.fetcher = readerTestFetcher(notFound)
	o := objectFrom(t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": "http://8.8.8.8/missing", "title": "Missing page",
	}))
	if reader, _ := o.Properties["reader_content"].(string); reader != "" {
		t.Fatalf("404 page stored reader content: %q", reader)
	}
	if o.Properties["preview_status"] != "unavailable" {
		t.Fatalf("404 page preview status = %v", o.Properties["preview_status"])
	}

	empty := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
	}))
	defer empty.Close()
	app.fetcher = readerTestFetcher(empty)
	o = objectFrom(t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": "http://8.8.8.8/empty", "title": "Empty page",
	}))
	if reader, _ := o.Properties["reader_content"].(string); reader != "" {
		t.Fatalf("empty page stored reader content: %q", reader)
	}
}

func TestPatchPreservesReaderContent(t *testing.T) {
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		_, _ = w.Write([]byte(readerArticlePage("")))
	}))
	defer server.Close()
	app, _, _ := makeApp(t)
	app.fetcher = readerTestFetcher(server)
	o := objectFrom(t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": "http://8.8.8.8/article", "title": "Saved link",
	}))
	before, _ := o.Properties["reader_content"].(string)
	if o.Properties["favicon"] != "http://8.8.8.8/favicon.png" {
		t.Fatalf("capture favicon = %v", o.Properties["favicon"])
	}

	patched := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{
		"properties": map[string]any{"url": "http://8.8.8.8/article", "status": "Leído"},
	}))
	if got, _ := patched.Properties["reader_content"].(string); got != before {
		t.Fatal("PATCH without reader_content erased the article")
	}
	if patched.Properties["favicon"] != "http://8.8.8.8/favicon.png" {
		t.Fatal("PATCH without favicon erased the favicon")
	}
	forged := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+o.ID, map[string]any{
		"properties": map[string]any{"url": "http://8.8.8.8/article", "reader_content": "forged"},
	}))
	if got, _ := forged.Properties["reader_content"].(string); got != before {
		t.Fatal("PATCH was allowed to forge reader_content")
	}
}

func TestCreateWebObjectStripsReaderMetadata(t *testing.T) {
	app, _, _ := makeApp(t)
	o := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "web", "title": "Imported link",
		"properties": map[string]any{
			"url":            "http://8.8.8.8/article",
			"reader_content": "forged",
			"favicon":        "http://8.8.8.8/forged.png",
		},
	}))
	if _, ok := o.Properties["reader_content"]; ok {
		t.Fatal("create accepted a client-supplied reader_content")
	}
	if _, ok := o.Properties["favicon"]; ok {
		t.Fatal("create accepted a client-supplied favicon")
	}
}

func TestRefreshReaderUpdatesContentAndBumpsRevision(t *testing.T) {
	var mu sync.Mutex
	page := readerArticlePage(`<p>FIRST_VERSION_TOKEN</p>`)
	var requests atomic.Int32
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requests.Add(1)
		mu.Lock()
		body := page
		mu.Unlock()
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		_, _ = w.Write([]byte(body))
	}))
	defer server.Close()
	app, _, _ := makeApp(t)
	app.fetcher = readerTestFetcher(server)
	o := objectFrom(t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": "http://8.8.8.8/article", "title": "Saved link",
	}))
	if reader, _ := o.Properties["reader_content"].(string); !strings.Contains(reader, "FIRST_VERSION_TOKEN") {
		t.Fatalf("capture version missing: %q", reader)
	}

	mu.Lock()
	page = readerArticlePage(`<p>SECOND_VERSION_TOKEN</p>`)
	mu.Unlock()
	refreshed := objectFrom(t, api(t, app, "POST", "/v1/objects/"+o.ID+"/refresh-reader", nil))
	reader, _ := refreshed.Properties["reader_content"].(string)
	if !strings.Contains(reader, "SECOND_VERSION_TOKEN") || strings.Contains(reader, "FIRST_VERSION_TOKEN") {
		t.Fatalf("refresh did not replace the article:\n%s", reader)
	}
	if refreshed.Revision <= o.Revision {
		t.Fatalf("refresh revision %d did not advance from %d", refreshed.Revision, o.Revision)
	}
	if refreshed.Properties["favicon"] != "http://8.8.8.8/favicon.png" {
		t.Fatalf("refresh favicon = %v", refreshed.Properties["favicon"])
	}
	if requests.Load() != 2 {
		t.Fatalf("refresh made %d requests, want 2 total", requests.Load())
	}

	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Not a link",
	}))
	if rr := api(t, app, "POST", "/v1/objects/"+note.ID+"/refresh-reader", nil); rr.Code != 404 {
		t.Fatalf("non-web refresh = HTTP %d, want 404", rr.Code)
	}
	if rr := api(t, app, "POST", "/v1/objects/missing/refresh-reader", nil); rr.Code != 404 {
		t.Fatalf("missing refresh = HTTP %d, want 404", rr.Code)
	}
	noURL := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "web", "title": "No URL",
	}))
	if rr := api(t, app, "POST", "/v1/objects/"+noURL.ID+"/refresh-reader", nil); rr.Code != 400 {
		t.Fatalf("web link without URL = HTTP %d, want 400", rr.Code)
	}
}

func TestRefreshReaderFailureKeepsPreviousContent(t *testing.T) {
	var fail atomic.Bool
	server := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if fail.Load() {
			http.Error(w, "down", http.StatusInternalServerError)
			return
		}
		w.Header().Set("Content-Type", "text/html; charset=utf-8")
		_, _ = w.Write([]byte(readerArticlePage("")))
	}))
	defer server.Close()
	app, _, _ := makeApp(t)
	app.fetcher = readerTestFetcher(server)
	o := objectFrom(t, api(t, app, "POST", "/v1/capture", map[string]any{
		"url": "http://8.8.8.8/article", "title": "Saved link",
	}))
	before, _ := o.Properties["reader_content"].(string)
	fail.Store(true)
	if rr := api(t, app, "POST", "/v1/objects/"+o.ID+"/refresh-reader", nil); rr.Code != 502 {
		t.Fatalf("failed refresh = HTTP %d, want 502", rr.Code)
	}
	after := objectFrom(t, api(t, app, "GET", "/v1/objects/"+o.ID, nil))
	if got, _ := after.Properties["reader_content"].(string); got != before {
		t.Fatal("failed refresh erased the previous article")
	}
}
