package main

import "net/http"

// registerMCPRoutes mounts the MCP streamable endpoint at /mcp (backend parity
// stage mcp-server). The App middleware (App.Handler in main.go) has already
// applied the bearer-token check by the time requests arrive here, and
// scopes.go exempted /mcp from the method-based scope decision because one
// URL hosts read and write tools; the per-tool enforcement lives in
// mcp_server.go.
func (a *App) registerMCPRoutes(mux *http.ServeMux) {
	mux.Handle(mcpEndpointPath, a.mcpHTTPHandler())
}
