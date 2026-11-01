package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"image"
	_ "image/gif"
	_ "image/jpeg"
	_ "image/png"
	"io"
	"net"
	"net/http"
	"net/netip"
	"net/url"
	"regexp"
	"strings"
	"time"

	_ "golang.org/x/image/webp"
	"golang.org/x/net/html"
)

type Preview struct {
	Title         string `json:"title"`
	Description   string `json:"description"`
	SiteName      string `json:"site_name"`
	Image         string `json:"image,omitempty"`
	ImageData     []byte `json:"-"`
	ImageMIME     string `json:"-"`
	ReaderContent string `json:"-"`
	Favicon       string `json:"-"`
}

var deniedNetworks = []netip.Prefix{
	netip.MustParsePrefix("0.0.0.0/8"), netip.MustParsePrefix("10.0.0.0/8"), netip.MustParsePrefix("100.64.0.0/10"), netip.MustParsePrefix("127.0.0.0/8"), netip.MustParsePrefix("169.254.0.0/16"), netip.MustParsePrefix("172.16.0.0/12"), netip.MustParsePrefix("192.0.0.0/24"), netip.MustParsePrefix("192.0.2.0/24"), netip.MustParsePrefix("192.88.99.0/24"), netip.MustParsePrefix("192.168.0.0/16"), netip.MustParsePrefix("198.18.0.0/15"), netip.MustParsePrefix("198.51.100.0/24"), netip.MustParsePrefix("203.0.113.0/24"), netip.MustParsePrefix("224.0.0.0/4"), netip.MustParsePrefix("240.0.0.0/4"), netip.MustParsePrefix("::/96"), netip.MustParsePrefix("64:ff9b::/96"), netip.MustParsePrefix("100::/64"), netip.MustParsePrefix("2001:db8::/32"), netip.MustParsePrefix("2001::/23"), netip.MustParsePrefix("2002::/16"), netip.MustParsePrefix("3fff::/20"), netip.MustParsePrefix("fc00::/7"), netip.MustParsePrefix("fe80::/10"), netip.MustParsePrefix("ff00::/8"),
}

func publicIP(addr netip.Addr) bool {
	addr = addr.Unmap()
	if addr.Is6() && !netip.MustParsePrefix("2000::/3").Contains(addr) {
		return false
	}
	if !addr.IsGlobalUnicast() {
		return false
	}
	for _, n := range deniedNetworks {
		if n.Contains(addr) {
			return false
		}
	}
	return true
}
func safeURL(raw string) (*url.URL, error) {
	if len(raw) > 8192 {
		return nil, errors.New("URL too long")
	}
	u, err := url.Parse(raw)
	if err != nil {
		return nil, errors.New("invalid URL")
	}
	if u.Scheme != "https" && u.Scheme != "http" {
		return nil, errors.New("URL must use http or https")
	}
	if u.User != nil || u.Hostname() == "" || strings.Contains(u.Hostname(), "%") {
		return nil, errors.New("credentials and invalid hosts are not allowed")
	}
	if p := u.Port(); p != "" && p != "80" && p != "443" {
		return nil, errors.New("only public web ports 80 and 443 are allowed")
	}
	if ip, err := netip.ParseAddr(u.Hostname()); err == nil && !publicIP(ip) {
		return nil, errors.New("private or reserved address is blocked")
	}
	u.Fragment = ""
	return u, nil
}

type secureFetcher struct{ client *http.Client }

func newSecureFetcher() *secureFetcher {
	dialer := &net.Dialer{Timeout: 4 * time.Second}
	transport := &http.Transport{Proxy: nil, ForceAttemptHTTP2: true, MaxIdleConns: 5, IdleConnTimeout: 20 * time.Second, TLSHandshakeTimeout: 4 * time.Second, ResponseHeaderTimeout: 5 * time.Second, MaxResponseHeaderBytes: 64 << 10}
	transport.DialContext = func(ctx context.Context, network, address string) (net.Conn, error) {
		host, port, err := net.SplitHostPort(address)
		if err != nil {
			return nil, err
		}
		ips, err := net.DefaultResolver.LookupNetIP(ctx, "ip", host)
		if err != nil {
			return nil, err
		}
		if len(ips) == 0 {
			return nil, errors.New("host has no addresses")
		}
		for _, ip := range ips {
			if !publicIP(ip) {
				return nil, errors.New("host resolves to a private or reserved address")
			}
		}
		var last error
		for _, ip := range ips {
			conn, e := dialer.DialContext(ctx, network, net.JoinHostPort(ip.String(), port))
			if e == nil {
				return conn, nil
			}
			last = e
		}
		return nil, last
	}
	client := &http.Client{Transport: transport, Timeout: 10 * time.Second, CheckRedirect: func(req *http.Request, via []*http.Request) error {
		if len(via) >= 4 {
			return errors.New("too many redirects")
		}
		_, err := safeURL(req.URL.String())
		return err
	}}
	return &secureFetcher{client: client}
}
func (f *secureFetcher) fetch(ctx context.Context, raw string, max int64, kind string) ([]byte, string, *url.URL, error) {
	return f.fetchWithHeaders(ctx, raw, max, kind, nil)
}

func (f *secureFetcher) fetchWithHeaders(ctx context.Context, raw string, max int64, kind string, headers map[string]string) ([]byte, string, *url.URL, error) {
	u, err := safeURL(raw)
	if err != nil {
		return nil, "", nil, err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, u.String(), nil)
	if err != nil {
		return nil, "", nil, err
	}
	req.Header.Set("User-Agent", "LumenPreview/0.1 (+public-metadata-only)")
	if kind == "image" {
		req.Header.Set("Accept", "image/jpeg,image/png,image/webp,image/gif")
	} else {
		req.Header.Set("Accept", "text/html,application/xhtml+xml")
	}
	for key, value := range headers {
		req.Header.Set(key, value)
	}
	response, err := f.client.Do(req)
	if err != nil {
		return nil, "", nil, err
	}
	defer response.Body.Close()
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return nil, "", nil, fmt.Errorf("web source returned HTTP %d", response.StatusCode)
	}
	mime := strings.ToLower(strings.TrimSpace(strings.Split(response.Header.Get("Content-Type"), ";")[0]))
	if kind == "image" {
		if mime != "image/jpeg" && mime != "image/png" && mime != "image/webp" && mime != "image/gif" {
			return nil, "", nil, errors.New("preview image must be PNG, JPEG, WebP or GIF")
		}
	} else if mime != "text/html" && mime != "application/xhtml+xml" {
		return nil, "", nil, errors.New("source is not an HTML page")
	}
	if response.ContentLength > max {
		return nil, "", nil, errors.New("preview exceeds size limit")
	}
	body, err := io.ReadAll(io.LimitReader(response.Body, max+1))
	if err != nil {
		return nil, "", nil, err
	}
	if int64(len(body)) > max {
		return nil, "", nil, errors.New("preview exceeds size limit")
	}
	if kind == "image" {
		detected := http.DetectContentType(body)
		if detected != mime && !(mime == "image/webp" && detected == "application/octet-stream") {
			return nil, "", nil, errors.New("image content does not match MIME")
		}
		config, _, err := image.DecodeConfig(bytes.NewReader(body))
		if err != nil || config.Width <= 0 || config.Height <= 0 || config.Width > 8192 || config.Height > 8192 || int64(config.Width)*int64(config.Height) > 16_000_000 {
			return nil, "", nil, errors.New("preview image dimensions are invalid or too large")
		}
	}
	return body, mime, response.Request.URL, nil
}
func (f *secureFetcher) fetchRaw(ctx context.Context, raw string, max int64, headers map[string]string) ([]byte, string, *url.URL, error) {
	u, err := safeURL(raw)
	if err != nil {
		return nil, "", nil, err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, u.String(), nil)
	if err != nil {
		return nil, "", nil, err
	}
	req.Header.Set("User-Agent", "LumenPreview/0.1 (+public-metadata-only)")
	for key, value := range headers {
		req.Header.Set(key, value)
	}
	response, err := f.client.Do(req)
	if err != nil {
		return nil, "", nil, err
	}
	defer response.Body.Close()
	if response.StatusCode < 200 || response.StatusCode >= 300 {
		return nil, "", nil, fmt.Errorf("web source returned HTTP %d", response.StatusCode)
	}
	if response.ContentLength > max {
		return nil, "", nil, errors.New("preview exceeds size limit")
	}
	body, err := io.ReadAll(io.LimitReader(response.Body, max+1))
	if err != nil {
		return nil, "", nil, err
	}
	if int64(len(body)) > max {
		return nil, "", nil, errors.New("preview exceeds size limit")
	}
	return body, strings.ToLower(strings.TrimSpace(strings.Split(response.Header.Get("Content-Type"), ";")[0])), response.Request.URL, nil
}

func browserHeaders() map[string]string {
	return map[string]string{
		"User-Agent":      "Mozilla/5.0 (compatible; open-graph-scraper/6.0.0; +https://www.npmjs.com/package/open-graph-scraper)",
		"Accept":          "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
		"Accept-Language": "en-US",
		"Cache-Control":   "max-age=0",
	}
}

func redditEmbedHeaders() map[string]string {
	return map[string]string{
		"User-Agent":      "Discordbot/2.0",
		"Accept":          "text/html,application/xhtml+xml,application/xml;q=0.9,*/*;q=0.8",
		"Accept-Language": "en-US",
		"Cache-Control":   "max-age=0",
	}
}

func (f *secureFetcher) resolve(ctx context.Context, raw string) (string, error) {
	u, err := safeURL(raw)
	if err != nil {
		return "", err
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, u.String(), nil)
	if err != nil {
		return "", err
	}
	for key, value := range browserHeaders() {
		req.Header.Set(key, value)
	}
	response, err := f.client.Do(req)
	if err != nil {
		return "", err
	}
	defer response.Body.Close()
	return response.Request.URL.String(), nil
}

func isRedditHost(host string) bool {
	host = strings.ToLower(host)
	return host == "reddit.com" || host == "www.reddit.com" || strings.HasSuffix(host, ".reddit.com") || host == "redd.it"
}

func redditPostID(raw string) string {
	u, err := url.Parse(raw)
	if err != nil {
		return ""
	}
	segments := strings.Split(strings.Trim(u.Path, "/"), "/")
	for i, segment := range segments {
		if segment == "comments" && i+1 < len(segments) {
			if regexp.MustCompile(`^[A-Za-z0-9_]+$`).MatchString(segments[i+1]) {
				return segments[i+1]
			}
		}
	}
	if u.Hostname() == "redd.it" && len(segments) == 1 {
		return segments[0]
	}
	return ""
}

func toVxRedditURL(raw string) string {
	u, err := url.Parse(raw)
	if err != nil {
		return raw
	}
	host := strings.ToLower(u.Hostname())
	switch {
	case host == "reddit.com":
		u.Host = "vxreddit.com"
	case host == "www.reddit.com":
		u.Host = "www.vxreddit.com"
	case strings.HasSuffix(host, ".reddit.com"):
		u.Host = strings.Replace(host, "reddit.com", "vxreddit.com", 1)
	default:
		return raw
	}
	return u.String()
}

func isBlockedTitle(title string) bool {
	normalized := strings.ToLower(strings.TrimSpace(title))
	if normalized == "" || normalized == "shared link" || normalized == "reddit" || normalized == "instagram" || normalized == "facebook" {
		return true
	}
	for _, blocked := range []string{"please wait for verification", "cloudflare", "attention required", "internal server error", "500 internal", "404 not found", "login", "log in", "error"} {
		if strings.Contains(normalized, blocked) {
			return true
		}
	}
	return false
}

func mergePreview(primary, candidate Preview) Preview {
	if candidate.Title != "" && !isBlockedTitle(candidate.Title) {
		primary.Title = candidate.Title
	}
	if primary.Description == "" {
		primary.Description = candidate.Description
	}
	if primary.SiteName == "" {
		primary.SiteName = candidate.SiteName
	}
	if primary.Image == "" {
		primary.Image = candidate.Image
	}
	return primary
}

type arcticPost struct {
	Title               string `json:"title"`
	Selftext            string `json:"selftext"`
	URL                 string `json:"url"`
	URLOverriddenByDest string `json:"url_overridden_by_dest"`
	Thumbnail           string `json:"thumbnail"`
	Preview             struct {
		Images []struct {
			Source struct {
				URL string `json:"url"`
			} `json:"source"`
		} `json:"images"`
	} `json:"preview"`
}

func arcticImage(post arcticPost) string {
	clean := func(value string) string { return strings.TrimSpace(strings.ReplaceAll(value, "&amp;", "&")) }
	isImage := func(value string) bool {
		lower := strings.ToLower(value)
		return strings.Contains(lower, "i.redd.it") || strings.Contains(lower, "preview.redd.it") ||
			strings.HasSuffix(lower, ".jpg") || strings.HasSuffix(lower, ".jpeg") ||
			strings.HasSuffix(lower, ".png") || strings.HasSuffix(lower, ".webp") || strings.HasSuffix(lower, ".gif")
	}
	for _, candidate := range []string{post.URLOverriddenByDest, post.URL} {
		if candidate = clean(candidate); candidate != "" && isImage(candidate) {
			return candidate
		}
	}
	if len(post.Preview.Images) > 0 {
		if source := clean(post.Preview.Images[0].Source.URL); source != "" {
			return source
		}
	}
	if thumbnail := clean(post.Thumbnail); strings.HasPrefix(thumbnail, "http") {
		return thumbnail
	}
	return ""
}

func (f *secureFetcher) arcticShiftPreview(ctx context.Context, raw string) Preview {
	id := redditPostID(raw)
	if id == "" {
		return Preview{}
	}
	archiveURL := "https://arctic-shift.photon-reddit.com/api/posts/ids?ids=" + url.QueryEscape(id)
	body, _, _, err := f.fetchRaw(ctx, archiveURL, 2<<20, map[string]string{
		"Accept":     "application/json",
		"User-Agent": "LumenMobile/0.1 (+public-metadata-only)",
	})
	if err != nil {
		return Preview{}
	}
	var payload struct {
		Data []arcticPost `json:"data"`
	}
	if json.Unmarshal(body, &payload) != nil || len(payload.Data) == 0 {
		return Preview{}
	}
	post := payload.Data[0]
	title := strings.TrimSpace(post.Title)
	if isBlockedTitle(title) {
		return Preview{}
	}
	return Preview{Title: truncate(title, 500), Description: truncate(strings.TrimSpace(post.Selftext), 2000), SiteName: "reddit.com", Image: arcticImage(post)}
}

// PreviewReddit ports the three-step pipeline used by Share-To-Thymer:
// direct OG, vxreddit embed metadata, then the Arctic Shift archive.
func (f *secureFetcher) PreviewReddit(ctx context.Context, raw string) (Preview, error) {
	resolved, err := f.resolve(ctx, raw)
	if err == nil && resolved != "" {
		raw = resolved
	}
	preview := Preview{}
	var readerBody []byte
	var readerURL *url.URL
	if body, _, finalURL, err := f.fetchWithHeaders(ctx, raw, 2<<20, "html", browserHeaders()); err == nil {
		preview = parsePreview(strings.NewReader(string(body)))
		readerBody, readerURL = body, finalURL
	}
	if body, _, finalURL, err := f.fetchWithHeaders(ctx, toVxRedditURL(raw), 2<<20, "html", redditEmbedHeaders()); err == nil {
		preview = mergePreview(preview, parsePreview(strings.NewReader(string(body))))
		if len(readerBody) == 0 {
			readerBody, readerURL = body, finalURL
		}
	}
	if preview.Image == "" {
		preview = mergePreview(preview, f.arcticShiftPreview(ctx, raw))
	}
	if preview.Title == "" || isBlockedTitle(preview.Title) {
		preview.Title = "Reddit"
	}
	if preview.Image != "" {
		imageURL, e := url.Parse(preview.Image)
		if e == nil {
			full := imageURL
			if !imageURL.IsAbs() {
				if base, bErr := url.Parse(raw); bErr == nil {
					full = base.ResolveReference(imageURL)
				}
			}
			if data, mime, _, fErr := f.fetch(ctx, full.String(), 5<<20, "image"); fErr == nil {
				preview.ImageData, preview.ImageMIME = data, mime
			}
		}
	}
	previewArticle := extractReaderArticle(readerBody, readerURL)
	preview.ReaderContent = previewArticle.Content
	preview.Favicon = previewArticle.Favicon
	preview.Image = ""
	return preview, nil
}

func (f *secureFetcher) Preview(ctx context.Context, raw string) (Preview, error) {
	body, _, finalURL, err := f.fetch(ctx, raw, 2<<20, "html")
	if err != nil {
		return Preview{}, err
	}
	p := parsePreview(strings.NewReader(string(body)))
	article := extractReaderArticle(body, finalURL)
	p.ReaderContent = article.Content
	p.Favicon = article.Favicon
	if p.Image != "" {
		imageURL, e := url.Parse(p.Image)
		if e == nil {
			full := finalURL.ResolveReference(imageURL).String()
			data, mime, _, e := f.fetch(ctx, full, 5<<20, "image")
			if e == nil {
				p.ImageData = data
				p.ImageMIME = mime
			}
		}
	}
	p.Image = ""
	return p, nil
}

// Reader fetches one public HTML page and returns its readable article as
// Markdown plus its favicon. It reuses the preview fetcher, so the SSRF guards,
// redirect policy, MIME check and size cap are identical to capture. It is used
// by refresh-reader, which must not fetch the page twice.
func (f *secureFetcher) Reader(ctx context.Context, raw string) (readerArticle, error) {
	body, _, finalURL, err := f.fetch(ctx, raw, 2<<20, "html")
	if err != nil {
		return readerArticle{}, err
	}
	return extractReaderArticle(body, finalURL), nil
}
func parsePreview(r io.Reader) Preview {
	z := html.NewTokenizer(r)
	meta := map[string]string{}
	title := strings.Builder{}
	inTitle := false
	for {
		tt := z.Next()
		if tt == html.ErrorToken {
			break
		}
		switch tt {
		case html.StartTagToken, html.SelfClosingTagToken:
			t := z.Token()
			if t.Data == "title" {
				inTitle = true
			}
			if t.Data == "meta" {
				attrs := map[string]string{}
				for _, a := range t.Attr {
					attrs[strings.ToLower(a.Key)] = a.Val
				}
				key := strings.ToLower(attrs["property"])
				if key == "" {
					key = strings.ToLower(attrs["name"])
				}
				if _, exists := meta[key]; !exists {
					meta[key] = attrs["content"]
				}
			}
		case html.EndTagToken:
			if z.Token().Data == "title" {
				inTitle = false
			}
		case html.TextToken:
			if inTitle && title.Len() < 2048 {
				title.Write(z.Text())
			}
		}
	}
	first := func(keys ...string) string {
		for _, k := range keys {
			if v := strings.TrimSpace(meta[k]); v != "" {
				return v
			}
		}
		return ""
	}
	t := first("og:title", "twitter:title")
	if t == "" {
		t = strings.TrimSpace(title.String())
	}
	return Preview{Title: truncate(t, 500), Description: truncate(first("og:description", "twitter:description", "description"), 2000), SiteName: truncate(first("og:site_name"), 200), Image: first("og:image:secure_url", "og:image", "twitter:image")}
}
func truncate(s string, max int) string {
	r := []rune(s)
	if len(r) > max {
		return string(r[:max])
	}
	return s
}
