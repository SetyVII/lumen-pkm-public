package main

import (
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"net/http"
	"net/url"
	"strconv"
	"strings"
)

// registerExportRoutes mounts the full space export. Keeping the routes in
// their own file keeps the stage diff on main.go additive.
func (a *App) registerExportRoutes(mux *http.ServeMux) {
	mux.HandleFunc("GET /v1/export", a.exportGet)
	mux.HandleFunc("POST /v1/export", a.exportPost)
}

func (a *App) exportGet(w http.ResponseWriter, r *http.Request) {
	opts, err := exportOptionsFromQuery(r.URL.Query())
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	a.exportSpace(w, r, opts)
}

func (a *App) exportPost(w http.ResponseWriter, r *http.Request) {
	opts, ok := decodeExportOptions(w, r)
	if !ok {
		return
	}
	a.exportSpace(w, r, opts)
}

// exportOptionsFromQuery mirrors the POST body for GET clients. Repeated
// type_ids and comma-separated values are both accepted.
func exportOptionsFromQuery(values url.Values) (exportOptions, error) {
	opts := exportOptions{TypeIDs: values["type_ids"], Q: values.Get("q")}
	if raw := strings.TrimSpace(values.Get("include_assets")); raw != "" {
		value, err := strconv.ParseBool(raw)
		if err != nil {
			return opts, fmt.Errorf("include_assets must be a boolean")
		}
		opts.IncludeAssets = &value
	}
	if raw := strings.TrimSpace(values.Get("include_templates")); raw != "" {
		value, err := strconv.ParseBool(raw)
		if err != nil {
			return opts, fmt.Errorf("include_templates must be a boolean")
		}
		opts.IncludeTemplates = &value
	}
	return opts, nil
}

// decodeExportOptions accepts an empty body as "all defaults", which makes
// `curl -X POST /v1/export` useful without a JSON payload.
func decodeExportOptions(w http.ResponseWriter, r *http.Request) (exportOptions, bool) {
	var opts exportOptions
	r.Body = http.MaxBytesReader(w, r.Body, 1<<20)
	decoder := json.NewDecoder(r.Body)
	err := decoder.Decode(&opts)
	if err != nil && !errors.Is(err, io.EOF) {
		jsonError(w, 400, "Invalid JSON or export options exceed 1 MB")
		return opts, false
	}
	if err == nil {
		var extra any
		if decoder.Decode(&extra) != io.EOF {
			jsonError(w, 400, "Only one JSON document is allowed")
			return opts, false
		}
	}
	return opts, true
}
