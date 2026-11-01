package main

import "net/http"

// registerMarkdownRoutes mounts the Markdown <-> blocks utilities. They are
// pure conversions: no store access, no side effects.
func (a *App) registerMarkdownRoutes(mux *http.ServeMux) {
	mux.HandleFunc("POST /v1/markdown/to-blocks", a.markdownToBlocks)
	mux.HandleFunc("POST /v1/markdown/from-blocks", a.markdownFromBlocks)
}

func (a *App) markdownToBlocks(w http.ResponseWriter, r *http.Request) {
	var request struct {
		Markdown *string `json:"markdown"`
	}
	if !decodeLimit(w, r, &request, 8<<20) {
		return
	}
	if request.Markdown == nil {
		jsonError(w, 400, "Provide markdown")
		return
	}
	blocks, err := MarkdownToBlocks(*request.Markdown)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	jsonResponse(w, 200, struct {
		Blocks []Block `json:"blocks"`
	}{blocks})
}

func (a *App) markdownFromBlocks(w http.ResponseWriter, r *http.Request) {
	var request struct {
		Blocks *[]Block `json:"blocks"`
	}
	if !decodeLimit(w, r, &request, 8<<20) {
		return
	}
	if request.Blocks == nil {
		jsonError(w, 400, "Provide blocks")
		return
	}
	jsonResponse(w, 200, struct {
		Markdown string `json:"markdown"`
	}{BlocksToMarkdown(*request.Blocks)})
}
