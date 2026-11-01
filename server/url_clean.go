package main

import (
	"errors"
	"net/url"
	"strings"
)

var trackingParams = map[string]bool{
	"fbclid": true, "gclid": true, "si": true, "igshid": true, "ref": true,
	"feature": true, "mc_cid": true, "mc_eid": true, "access_token": true,
	"token": true, "api_key": true, "apikey": true, "authorization": true,
	"password": true, "secret": true,
}

func trackingParam(key string) bool {
	key = strings.ToLower(strings.TrimSpace(key))
	return trackingParams[key] || strings.HasPrefix(key, "utm_")
}

// cleanURL is deliberately shared by capture, duplicate detection and clients.
// It removes tracking noise, normalizes short YouTube/Reddit links and sorts the
// query so two spellings of the same public link compare equal.
func cleanURL(raw string) (string, error) {
	raw = strings.TrimSpace(raw)
	if raw == "" || len(raw) > 8192 {
		return "", errors.New("URL is empty or too long")
	}
	u, err := url.Parse(raw)
	if err != nil {
		return "", errors.New("invalid URL")
	}
	u.Scheme = strings.ToLower(u.Scheme)
	if u.Scheme != "http" && u.Scheme != "https" {
		return "", errors.New("URL must use http or https")
	}
	if u.User != nil || u.Hostname() == "" {
		return "", errors.New("credentials and invalid hosts are not allowed")
	}
	u.Host = strings.ToLower(u.Host)
	host := strings.ToLower(u.Hostname())
	if (u.Scheme == "http" && u.Port() == "80") || (u.Scheme == "https" && u.Port() == "443") {
		if strings.Contains(host, ":") {
			u.Host = "[" + host + "]"
		} else {
			u.Host = host
		}
	}
	u.Fragment = ""

	q := u.Query()
	for key := range q {
		if trackingParam(key) {
			q.Del(key)
		}
	}

	switch host {
	case "youtu.be":
		if id := strings.Trim(u.Path, "/"); id != "" {
			u.Host, u.Path = "www.youtube.com", "/watch"
			q.Set("v", id)
		}
	case "youtube.com", "www.youtube.com", "m.youtube.com":
		if strings.HasPrefix(u.Path, "/shorts/") {
			if id := strings.Trim(strings.TrimPrefix(u.Path, "/shorts/"), "/"); id != "" {
				u.Path = "/watch"
				q.Set("v", id)
			}
		}
	case "redd.it":
		if id := strings.Trim(u.Path, "/"); id != "" {
			u.Host, u.Path = "www.reddit.com", "/comments/"+id
		}
	}

	u.RawQuery = q.Encode()
	return u.String(), nil
}

func sameCleanURL(a, b string) bool {
	ca, errA := cleanURL(a)
	cb, errB := cleanURL(b)
	return errA == nil && errB == nil && ca == cb
}
