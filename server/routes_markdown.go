package main

import (
	"lumen/server/internal/document"
	"net/http"
)

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
	d, e := canonicalDocumentFromBlocks(blocks)
	if e != nil {
		jsonError(w, 400, e.Error())
		return
	}
	jsonResponse(w, 200, map[string]any{"document": d, "blocks": blocks})
}

func (a *App) markdownFromBlocks(w http.ResponseWriter, r *http.Request) {
	var request struct {
		Blocks   *[]Block           `json:"blocks"`
		Document *document.Document `json:"document"`
	}
	if !decodeLimit(w, r, &request, 8<<20) {
		return
	}
	if request.Document != nil {
		d, e := document.Normalize(*request.Document)
		if e != nil {
			jsonError(w, 400, e.Error())
			return
		}
		jsonResponse(w, 200, map[string]any{"markdown": BlocksToMarkdown(documentMarkdownBlocks(d))})
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
