package main

import (
	"errors"
	"net/http"
	"strings"
)

// Token scopes for the developer API (backend stage api-v2).
//
// Capacities (developers.capacities.io) issues per-space access tokens with
// api:read/api:write scopes on top of OAuth 2.1 + PKCE because it is a
// multi-tenant SaaS that must mediate between many users, apps and spaces.
// Lumen is single-user and self-hosted with exactly one bearer token
// (LUMEN_TOKEN_FILE), so there is no third party to authorize: the scopes are
// a deployment-level restriction of that one token, configured with the
// LUMEN_TOKEN_SCOPES environment variable. An unconfigured variable keeps the
// token at full access — today's behavior — so no existing client breaks.
// OAuth 2.1 would add an authorization-code dance, client registration and
// refresh-token storage without any isolation benefit here; when Lumen ever
// grows multi-user spaces, that decision should be revisited.

const (
	scopeAPIRead  = "api:read"
	scopeAPIWrite = "api:write"
)

// apiScopes is the effective capability set of the configured token.
// api:write implies api:read.
type apiScopes struct {
	read  bool
	write bool
}

// fullAccess reports the default, unrestricted configuration.
func (s apiScopes) fullAccess() bool { return !s.read && !s.write }

// readOnlyPostPaths are POST endpoints that never mutate the store: the pure
// Markdown conversions and the ZIP export. They stay reachable with an
// api:read token, mirroring the read-only tooling of the Capacities API.
var readOnlyPostPaths = map[string]bool{
	"/v1/markdown/to-blocks":   true,
	"/v1/markdown/from-blocks": true,
	"/v1/export":               true,
}

var errReadOnlyToken = errors.New("This token only has the api:read scope; this request modifies data and requires api:write")

// parseTokenScopes validates a LUMEN_TOKEN_SCOPES value: empty means full
// access (backward compatible), otherwise a comma-separated list of api:read
// and/or api:write. api:write implies api:read. Unknown values are an error
// instead of being silently ignored: a typo must never grant more access
// than the operator intended.
func parseTokenScopes(raw string) (apiScopes, []string, error) {
	var scopes apiScopes
	names := []string{}
	for _, entry := range strings.Split(raw, ",") {
		entry = strings.ToLower(strings.TrimSpace(entry))
		if entry == "" {
			continue
		}
		switch entry {
		case scopeAPIRead:
			scopes.read = true
		case scopeAPIWrite:
			scopes.read = true
			scopes.write = true
		default:
			return apiScopes{}, nil, errors.New("unknown token scope \"" + entry + "\" (supported: api:read, api:write)")
		}
		if !containsString(names, entry) {
			names = append(names, entry)
		}
	}
	if scopes.write && !containsString(names, scopeAPIRead) {
		names = append([]string{scopeAPIRead}, names...)
	}
	if len(names) == 0 {
		names = []string{"full_access"}
	}
	return scopes, names, nil
}

// SetScopes restricts the single bearer token to the configured API scopes.
// It is called once at startup (before the HTTP handler serves requests) from
// LUMEN_TOKEN_SCOPES.
func (a *App) SetScopes(raw string) error {
	scopes, names, err := parseTokenScopes(raw)
	if err != nil {
		return err
	}
	a.scopes = scopes
	a.scopeNames = names
	return nil
}

// authorizeScope enforces the token scopes for one request.
//
// Sync endpoints are deliberately full-trust and bypass the scope check: the
// mobile client is a first-party app sharing the one deployment token, and
// scoping the token for scripting must never break the phone's push/pull.
//
// /mcp also bypasses this check, but for the opposite reason: the MCP endpoint
// hosts read AND write tools behind one POST URL, so a method-based decision
// is impossible here. The MCP layer (mcp_server.go) enforces the scopes per
// tool instead: read tools answer for a token with only api:read, write
// tools reject the call with the same message this middleware uses.
func (a *App) authorizeScope(r *http.Request) error {
	if a.scopes.fullAccess() {
		return nil
	}
	if strings.HasPrefix(r.URL.Path, "/v1/sync") {
		return nil
	}
	if r.URL.Path == "/mcp" {
		return nil
	}
	switch r.Method {
	case http.MethodGet, http.MethodHead, http.MethodOptions:
		return nil
	}
	if readOnlyPostPaths[r.URL.Path] {
		return nil
	}
	if a.scopes.write {
		return nil
	}
	return errReadOnlyToken
}
