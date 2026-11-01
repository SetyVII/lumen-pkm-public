package main

import (
	"archive/zip"
	"context"
	"encoding/csv"
	"encoding/json"
	"fmt"
	"io"
	"math"
	"mime"
	"net/http"
	"os"
	"path/filepath"
	"sort"
	"strconv"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"
)

// Full space export (backend parity stage 11).
//
// The export is a portable snapshot, not a backup: the encrypted SQLite store
// remains the source of truth, version history and the sync journal are
// server-side only, and a concurrent edit may be reflected in some files but
// not others. The ZIP is built in a private temporary file and streamed only
// after it is complete, so a store error never reaches the client as a
// truncated archive.
const (
	exportSchemaVersion  = 1
	exportFormat         = "lumen-export"
	exportGenerator      = "lumen-server"
	exportSlugLimit      = 60
	exportAssetNameLimit = 80
)

// exportOptions is the JSON body of POST /v1/export. Pointers distinguish
// "omitted" from "explicitly false" so the defaults stay include=true.
type exportOptions struct {
	TypeIDs          []string `json:"type_ids"`
	IncludeAssets    *bool    `json:"include_assets"`
	IncludeTemplates *bool    `json:"include_templates"`
	Q                string   `json:"q"`
}

// exportSettings is the validated form used by the exporter.
type exportSettings struct {
	TypeIDs          []string
	TypeFilter       map[string]bool
	IncludeAssets    bool
	IncludeTemplates bool
	Q                string
}

// exportObject is the metadata the exporter keeps for every selected object.
// It is deliberately small: ids, type, title and the two path components.
type exportObject struct {
	ID       string
	TypeID   string
	Title    string
	Folder   string
	Filename string
}

// exportSelection is the result of the first bounded pass over the store.
// Objects holds metadata only; bodies are decrypted again in the emission pass.
type exportSelection struct {
	objects          map[string]*exportObject
	counts           map[string]int
	propertyKeys     map[string]map[string]bool
	referencedAssets map[string]bool
	referencedMedia  map[string]bool
}

type exportAssetEntry struct {
	ID            string `json:"id"`
	Path          string `json:"path"`
	Name          string `json:"name"`
	MIME          string `json:"mime"`
	Size          int    `json:"size"`
	ObjectID      string `json:"object_id,omitempty"`
	MediaObjectID string `json:"media_object_id,omitempty"`
}

type exportTypeEntry struct {
	ID     string `json:"id"`
	Name   string `json:"name"`
	Folder string `json:"folder"`
	CSV    string `json:"csv,omitempty"`
	Count  int    `json:"count"`
}

type exportManifestOptions struct {
	TypeIDs          []string `json:"type_ids,omitempty"`
	IncludeAssets    bool     `json:"include_assets"`
	IncludeTemplates bool     `json:"include_templates"`
	Q                string   `json:"q,omitempty"`
}

type exportManifest struct {
	SchemaVersion int                   `json:"schema_version"`
	Format        string                `json:"format"`
	Generator     string                `json:"generator"`
	ExportedAt    string                `json:"exported_at"`
	Options       exportManifestOptions `json:"options"`
	Counts        map[string]int        `json:"counts"`
	ObjectCount   int                   `json:"object_count"`
	AssetCount    int                   `json:"asset_count"`
	TemplateCount int                   `json:"template_count"`
	Types         []exportTypeEntry     `json:"types"`
	Assets        []exportAssetEntry    `json:"assets"`
}

// exportLinkContext carries the lookup tables used to rewrite object links and
// image runs while a body is rendered.
type exportLinkContext struct {
	app        *App
	objects    map[string]*exportObject
	assetPaths map[string]string
	mediaAsset map[string]string
}

func (c *exportLinkContext) relativePath(target *exportObject) string {
	return "../" + target.Folder + "/" + target.Filename
}

func (c *exportLinkContext) imagePath(id string) (string, bool) {
	if path, ok := c.assetPaths[id]; ok {
		return "../" + path, true
	}
	if assetID, ok := c.mediaAsset[id]; ok {
		if path, ok := c.assetPaths[assetID]; ok {
			return "../" + path, true
		}
	}
	return "", false
}

func (c *exportLinkContext) titleOf(id string) string {
	if meta, ok := c.app.store.index.getMeta(id); ok {
		return meta.Title
	}
	return ""
}

// normalizeExportOptions validates type ids and applies the documented
// defaults. Unknown type ids are a client error: silently exporting an empty
// ZIP would hide a typo.
func (a *App) normalizeExportOptions(opts exportOptions) (exportSettings, error) {
	settings := exportSettings{
		IncludeAssets:    true,
		IncludeTemplates: true,
		Q:                strings.TrimSpace(opts.Q),
	}
	if opts.IncludeAssets != nil {
		settings.IncludeAssets = *opts.IncludeAssets
	}
	if opts.IncludeTemplates != nil {
		settings.IncludeTemplates = *opts.IncludeTemplates
	}
	typesByID := make(map[string]ObjectType)
	for _, typ := range a.store.Types() {
		typesByID[typ.ID] = typ
	}
	seen := map[string]bool{}
	for _, raw := range opts.TypeIDs {
		for _, part := range strings.Split(raw, ",") {
			id := strings.TrimSpace(part)
			if id == "" {
				continue
			}
			if _, ok := typesByID[id]; !ok {
				return settings, fmt.Errorf("unknown type id %q", id)
			}
			if seen[id] {
				continue
			}
			seen[id] = true
			settings.TypeIDs = append(settings.TypeIDs, id)
		}
	}
	sort.Strings(settings.TypeIDs)
	if len(settings.TypeIDs) > 0 {
		settings.TypeFilter = make(map[string]bool, len(settings.TypeIDs))
		for _, id := range settings.TypeIDs {
			settings.TypeFilter[id] = true
		}
	}
	return settings, nil
}

// exportSpace builds the ZIP in a temporary file, then streams it with a
// Content-Length. The temporary file keeps a mid-export store error from
// producing a 200 response with a truncated archive; it is created with 0600
// and removed on every path.
func (a *App) exportSpace(w http.ResponseWriter, r *http.Request, opts exportOptions) {
	settings, err := a.normalizeExportOptions(opts)
	if err != nil {
		jsonError(w, 400, err.Error())
		return
	}
	tmp, err := os.CreateTemp("", "lumen-export-*.zip")
	if err != nil {
		jsonError(w, 500, "Could not create a temporary export file")
		return
	}
	name := tmp.Name()
	defer os.Remove(name)
	defer tmp.Close()
	if err := a.writeExportZip(r.Context(), tmp, settings); err != nil {
		if r.Context().Err() != nil {
			return
		}
		jsonError(w, 500, "Could not build the export")
		return
	}
	if _, err := tmp.Seek(0, io.SeekStart); err != nil {
		jsonError(w, 500, "Could not read the export")
		return
	}
	info, err := tmp.Stat()
	if err != nil {
		jsonError(w, 500, "Could not read the export")
		return
	}
	// The server-wide WriteTimeout is 45s, which a large space can exceed.
	// Extend the deadline for this response only; ResponseRecorder-based tests
	// simply report ErrNotSupported and keep the default.
	_ = http.NewResponseController(w).SetWriteDeadline(time.Now().Add(30 * time.Minute))
	w.Header().Set("Content-Type", "application/zip")
	w.Header().Set("Content-Disposition", mime.FormatMediaType("attachment", map[string]string{
		"filename": "lumen-export-" + time.Now().Format("20060102-150405") + ".zip",
	}))
	w.Header().Set("Content-Length", strconv.FormatInt(info.Size(), 10))
	w.WriteHeader(http.StatusOK)
	_, _ = io.Copy(w, tmp)
}

func (a *App) writeExportZip(ctx context.Context, out io.Writer, settings exportSettings) error {
	types := a.sortedExportTypes()
	typesByID := make(map[string]ObjectType, len(types))
	for _, typ := range types {
		typesByID[typ.ID] = typ
	}
	folders := exportTypeFolders(types)
	selection, err := a.selectExportObjects(ctx, settings, typesByID, folders)
	if err != nil {
		return err
	}
	zw := zip.NewWriter(out)
	buildErr := a.buildExportZip(ctx, zw, settings, types, folders, selection)
	closeErr := zw.Close()
	if buildErr != nil {
		return buildErr
	}
	return closeErr
}

func (a *App) sortedExportTypes() []ObjectType {
	types := a.store.Types()
	sort.SliceStable(types, func(i, j int) bool { return types[i].ID < types[j].ID })
	return types
}

func (a *App) buildExportZip(ctx context.Context, zw *zip.Writer, settings exportSettings, types []ObjectType, folders map[string]string, selection *exportSelection) error {
	if err := writeZipJSON(zw, "types.json", types); err != nil {
		return err
	}
	assetPaths, mediaAsset, assetEntries, err := a.writeExportAssets(ctx, zw, selection, settings.IncludeAssets)
	if err != nil {
		return err
	}
	csvDir, err := os.MkdirTemp("", "lumen-export-csv-*")
	if err != nil {
		return err
	}
	defer os.RemoveAll(csvDir)
	csvs, err := createExportCSVs(csvDir, selection, folders)
	if err != nil {
		return err
	}
	written, templates, err := a.writeExportObjects(ctx, zw, selection, assetPaths, mediaAsset, csvs)
	if err != nil {
		closeExportCSVs(csvs)
		return err
	}
	if err := addExportCSVs(zw, csvs, written); err != nil {
		return err
	}
	manifest := buildExportManifest(settings, types, folders, written, templates, assetEntries)
	return writeZipJSON(zw, "manifest.json", manifest)
}

// selectExportObjects is the first bounded pass. It decrypts every live object
// once to apply the filters, but keeps only metadata for the selected ones.
func (a *App) selectExportObjects(ctx context.Context, settings exportSettings, typesByID map[string]ObjectType, folders map[string]string) (*exportSelection, error) {
	selection := &exportSelection{
		objects:          map[string]*exportObject{},
		counts:           map[string]int{},
		propertyKeys:     map[string]map[string]bool{},
		referencedAssets: map[string]bool{},
		referencedMedia:  map[string]bool{},
	}
	// Seed CSV columns from the type schema so a declared property keeps its
	// column even before the first object that uses it is seen.
	for id, typ := range typesByID {
		set := map[string]bool{}
		for _, key := range schemaPropertyKeys(typ) {
			set[key] = true
		}
		if len(set) > 0 {
			selection.propertyKeys[id] = set
		}
	}
	q := strings.ToLower(settings.Q)
	err := a.store.ObjectBatches(exportBatchSize, func(batch []Object) error {
		if err := ctx.Err(); err != nil {
			return err
		}
		for _, o := range batch {
			if settings.TypeFilter != nil && !settings.TypeFilter[o.TypeID] {
				continue
			}
			if !settings.IncludeTemplates && isTemplateObject(o) {
				continue
			}
			if q != "" && !objectMatchesQuery(o, q) {
				continue
			}
			folder := folders[o.TypeID]
			if folder == "" {
				// A dangling type id (the store was edited outside the API)
				// must not make its objects disappear from the export.
				folder = exportFallbackFolder(o.TypeID)
			}
			meta := &exportObject{
				ID:       o.ID,
				TypeID:   o.TypeID,
				Title:    o.Title,
				Folder:   folder,
				Filename: exportObjectFilename(o),
			}
			selection.objects[o.ID] = meta
			selection.counts[o.TypeID]++
			if selection.propertyKeys[o.TypeID] == nil {
				selection.propertyKeys[o.TypeID] = map[string]bool{}
			}
			for key := range o.Properties {
				if exportCSVExcludedKey(key) {
					continue
				}
				selection.propertyKeys[o.TypeID][key] = true
			}
			collectExportAssetRefs(selection, o)
		}
		return nil
	})
	if err != nil {
		return nil, err
	}
	return selection, nil
}

func (s *exportSelection) includeAsset(asset Asset) bool {
	if asset.ObjectID != "" && s.objects[asset.ObjectID] != nil {
		return true
	}
	if asset.MediaObjectID != "" && s.objects[asset.MediaObjectID] != nil {
		return true
	}
	if s.referencedAssets[asset.ID] {
		return true
	}
	if asset.MediaObjectID != "" && s.referencedMedia[asset.MediaObjectID] {
		return true
	}
	return false
}

// collectExportAssetRefs records the attachment ids an exported object points
// at. The asset pass uses these sets so an image embedded in a note is still
// exported when the image object's own type is filtered out.
func collectExportAssetRefs(selection *exportSelection, o Object) {
	if id, ok := o.Properties["asset_id"].(string); ok && id != "" {
		selection.referencedAssets[id] = true
	}
	if id, ok := o.Properties["image_asset_id"].(string); ok && id != "" {
		selection.referencedAssets[id] = true
	}
	if assets, ok := o.Properties["assets"].([]any); ok {
		for _, raw := range assets {
			entry, ok := raw.(map[string]any)
			if !ok {
				continue
			}
			if id, _ := entry["id"].(string); id != "" {
				selection.referencedAssets[id] = true
			}
			if id, _ := entry["media_object_id"].(string); id != "" {
				selection.referencedMedia[id] = true
			}
		}
	}
	if raw, ok := o.Properties["blocks"]; ok && raw != nil {
		if blocks, err := decodeObjectBlocks(raw); err == nil {
			collectBlockAssetRefs(selection, blocks)
		}
	}
}

func collectBlockAssetRefs(selection *exportSelection, blocks []Block) {
	for _, block := range blocks {
		collectBlockAssetRefs(selection, block.Children)
		for _, run := range block.Runs {
			collectRunAssetRefs(selection, run)
		}
		for _, row := range block.Rows {
			for _, cell := range row {
				for _, run := range cell.Runs {
					collectRunAssetRefs(selection, run)
				}
			}
		}
		for _, column := range block.Columns {
			collectBlockAssetRefs(selection, column)
		}
	}
}

func collectRunAssetRefs(selection *exportSelection, run Run) {
	for _, name := range []string{run.ImageName, run.Href} {
		if strings.HasPrefix(name, "lumen://image/") {
			id := strings.TrimPrefix(name, "lumen://image/")
			if id != "" {
				selection.referencedAssets[id] = true
				selection.referencedMedia[id] = true
			}
		}
	}
	// A link to a media object (image or PDF) should carry its asset even when
	// the media object's type is excluded by a filter.
	if target := objectRunTarget(run); target != "" {
		selection.referencedMedia[target] = true
	}
}

// writeExportAssets decrypts and writes one blob at a time, following the same
// path as GET /v1/assets/{id}/content. Images and PDFs are already compressed,
// so they are stored without Deflate.
func (a *App) writeExportAssets(ctx context.Context, zw *zip.Writer, selection *exportSelection, include bool) (map[string]string, map[string]string, []exportAssetEntry, error) {
	assetPaths := map[string]string{}
	mediaAsset := map[string]string{}
	entries := []exportAssetEntry{}
	if !include {
		return assetPaths, mediaAsset, entries, nil
	}
	err := a.store.AssetBatches(exportBatchSize, func(batch []Asset) error {
		if err := ctx.Err(); err != nil {
			return err
		}
		for _, asset := range batch {
			if !selection.includeAsset(asset) {
				continue
			}
			data, err := a.store.Blob(asset.ID)
			if err != nil {
				return fmt.Errorf("asset %s: %w", asset.ID, err)
			}
			path := "assets/" + exportAssetFilename(asset)
			if err := writeZipBytes(zw, path, data, zip.Store); err != nil {
				return err
			}
			assetPaths[asset.ID] = path
			if asset.MediaObjectID != "" {
				mediaAsset[asset.MediaObjectID] = asset.ID
			}
			entries = append(entries, exportAssetEntry{
				ID: asset.ID, Path: path, Name: asset.Name, MIME: asset.MIME,
				Size: asset.Size, ObjectID: asset.ObjectID,
				MediaObjectID: asset.MediaObjectID,
			})
		}
		return nil
	})
	return assetPaths, mediaAsset, entries, err
}

// writeExportObjects is the second bounded pass. It renders the bodies that
// were selected in the first pass and appends CSV rows to per-type temporary
// files, so the CSV table never has to fit in RAM.
func (a *App) writeExportObjects(ctx context.Context, zw *zip.Writer, selection *exportSelection, assetPaths, mediaAsset map[string]string, csvs map[string]*exportCSV) (map[string]int, int, error) {
	written := map[string]int{}
	templates := 0
	linkCtx := &exportLinkContext{
		app: a, objects: selection.objects,
		assetPaths: assetPaths, mediaAsset: mediaAsset,
	}
	err := a.store.ObjectBatches(exportBatchSize, func(batch []Object) error {
		if err := ctx.Err(); err != nil {
			return err
		}
		for _, o := range batch {
			meta := selection.objects[o.ID]
			if meta == nil {
				continue
			}
			body := buildExportBody(o, linkCtx)
			content := buildExportFrontMatter(o)
			if body != "" {
				content += "\n" + body
			}
			if err := writeZipBytes(zw, meta.Folder+"/"+meta.Filename, []byte(content), zip.Deflate); err != nil {
				return err
			}
			written[meta.TypeID]++
			if isTemplateObject(o) {
				templates++
			}
			if csv := csvs[meta.TypeID]; csv != nil {
				if err := csv.writeRow(o); err != nil {
					return err
				}
			}
		}
		return nil
	})
	return written, templates, err
}

// exportCSV streams one type's table into a private temporary file. The header
// is written when the type is selected; rows arrive during the emission pass.
type exportCSV struct {
	file   *os.File
	writer *csv.Writer
	keys   []string
	path   string
}

func createExportCSVs(dir string, selection *exportSelection, folders map[string]string) (map[string]*exportCSV, error) {
	out := map[string]*exportCSV{}
	for typeID, count := range selection.counts {
		if count == 0 {
			continue
		}
		folder := folders[typeID]
		if folder == "" {
			folder = exportFallbackFolder(typeID)
		}
		keys := sortedStringSet(selection.propertyKeys[typeID])
		file, err := os.CreateTemp(dir, "*.csv")
		if err != nil {
			closeExportCSVs(out)
			return nil, err
		}
		writer := csv.NewWriter(file)
		header := append([]string{"id", "title"}, keys...)
		header = append(header, "is_template")
		if err := writer.Write(header); err != nil {
			file.Close()
			closeExportCSVs(out)
			return nil, err
		}
		out[typeID] = &exportCSV{file: file, writer: writer, keys: keys, path: folder + ".csv"}
	}
	return out, nil
}

func (c *exportCSV) writeRow(o Object) error {
	row := make([]string, 0, len(c.keys)+3)
	row = append(row, o.ID, o.Title)
	for _, key := range c.keys {
		row = append(row, csvPropertyValue(o.Properties[key]))
	}
	row = append(row, strconv.FormatBool(isTemplateObject(o)))
	return c.writer.Write(row)
}

func closeExportCSVs(csvs map[string]*exportCSV) {
	for _, c := range csvs {
		c.writer.Flush()
		_ = c.file.Close()
	}
}

func addExportCSVs(zw *zip.Writer, csvs map[string]*exportCSV, written map[string]int) error {
	ids := make([]string, 0, len(csvs))
	for id := range csvs {
		ids = append(ids, id)
	}
	sort.Strings(ids)
	// Close every file before adding any entry so an error on one type cannot
	// leak the descriptors of the remaining types.
	var firstErr error
	for _, id := range ids {
		c := csvs[id]
		c.writer.Flush()
		if err := c.writer.Error(); err != nil && firstErr == nil {
			firstErr = err
		}
		if err := c.file.Close(); err != nil && firstErr == nil {
			firstErr = err
		}
	}
	if firstErr != nil {
		return firstErr
	}
	for _, id := range ids {
		if written[id] == 0 {
			continue
		}
		c := csvs[id]
		file, err := os.Open(c.file.Name())
		if err != nil {
			return err
		}
		header := &zip.FileHeader{Name: c.path, Method: zip.Deflate, Modified: time.Now()}
		entry, err := zw.CreateHeader(header)
		if err != nil {
			file.Close()
			return err
		}
		_, err = io.Copy(entry, file)
		file.Close()
		if err != nil {
			return err
		}
	}
	return nil
}

func buildExportManifest(settings exportSettings, types []ObjectType, folders map[string]string, written map[string]int, templates int, assets []exportAssetEntry) exportManifest {
	counts := make(map[string]int, len(types))
	typeEntries := make([]exportTypeEntry, 0, len(types))
	objectCount := 0
	for _, typ := range types {
		count := written[typ.ID]
		counts[typ.ID] = count
		objectCount += count
		folder := folders[typ.ID]
		csvName := ""
		if count > 0 {
			csvName = folder + ".csv"
		}
		typeEntries = append(typeEntries, exportTypeEntry{
			ID: typ.ID, Name: typ.Name, Folder: folder, CSV: csvName, Count: count,
		})
	}
	for typeID, count := range written {
		if _, ok := counts[typeID]; ok {
			continue
		}
		counts[typeID] = count
		objectCount += count
		folder := folders[typeID]
		if folder == "" {
			folder = exportFallbackFolder(typeID)
		}
		typeEntries = append(typeEntries, exportTypeEntry{
			ID: typeID, Name: typeID, Folder: folder, CSV: folder + ".csv", Count: count,
		})
	}
	sort.Slice(typeEntries, func(i, j int) bool { return typeEntries[i].ID < typeEntries[j].ID })
	sort.Slice(assets, func(i, j int) bool { return assets[i].ID < assets[j].ID })
	if assets == nil {
		assets = []exportAssetEntry{}
	}
	return exportManifest{
		SchemaVersion: exportSchemaVersion,
		Format:        exportFormat,
		Generator:     exportGenerator,
		ExportedAt:    now(),
		Options: exportManifestOptions{
			TypeIDs:          settings.TypeIDs,
			IncludeAssets:    settings.IncludeAssets,
			IncludeTemplates: settings.IncludeTemplates,
			Q:                settings.Q,
		},
		Counts:        counts,
		ObjectCount:   objectCount,
		AssetCount:    len(assets),
		TemplateCount: templates,
		Types:         typeEntries,
		Assets:        assets,
	}
}

func writeZipJSON(zw *zip.Writer, name string, value any) error {
	data, err := json.MarshalIndent(value, "", "  ")
	if err != nil {
		return err
	}
	data = append(data, '\n')
	return writeZipBytes(zw, name, data, zip.Deflate)
}

func writeZipBytes(zw *zip.Writer, name string, data []byte, method uint16) error {
	header := &zip.FileHeader{Name: name, Method: method, Modified: time.Now()}
	entry, err := zw.CreateHeader(header)
	if err != nil {
		return err
	}
	_, err = entry.Write(data)
	return err
}

// ---------------------------------------------------------------------------
// Markdown body and link rewriting
// ---------------------------------------------------------------------------

func buildExportBody(o Object, ctx *exportLinkContext) string {
	if raw, ok := o.Properties["blocks"]; ok && raw != nil {
		if blocks, err := decodeObjectBlocks(raw); err == nil && len(blocks) > 0 {
			for i := range blocks {
				rewriteExportBlock(&blocks[i], ctx)
			}
			return BlocksToMarkdown(blocks)
		}
	}
	if strings.TrimSpace(o.Content) != "" {
		return o.Content
	}
	// Objects written by older clients may only have the editor HTML snapshot.
	// Plain text is not lossless, but an empty body would lose the note.
	if rich, ok := o.Properties["rich_text"].(string); ok {
		text := strings.TrimSpace(richTextPlain(rich))
		if text != "" {
			return strings.Join(strings.Fields(text), " ")
		}
	}
	return ""
}

func decodeObjectBlocks(raw any) ([]Block, error) {
	encoded, err := json.Marshal(raw)
	if err != nil {
		return nil, err
	}
	var blocks []Block
	if err := json.Unmarshal(encoded, &blocks); err != nil {
		return nil, err
	}
	return blocks, nil
}

func rewriteExportBlock(block *Block, ctx *exportLinkContext) {
	for i := range block.Children {
		rewriteExportBlock(&block.Children[i], ctx)
	}
	rewriteExportRuns(block.Runs, ctx)
	for i := range block.Rows {
		for j := range block.Rows[i] {
			rewriteExportRuns(block.Rows[i][j].Runs, ctx)
		}
	}
	for i := range block.Columns {
		for j := range block.Columns[i] {
			rewriteExportBlock(&block.Columns[i][j], ctx)
		}
	}
}

func rewriteExportRuns(runs []Run, ctx *exportLinkContext) {
	for i := range runs {
		run := &runs[i]
		if run.Type == "image" || strings.HasPrefix(run.ImageName, "lumen://image/") {
			rewriteExportImageRun(run, ctx)
			continue
		}
		target := objectRunTarget(*run)
		if target == "" {
			if strings.HasPrefix(run.Href, objectHrefPrefix) {
				// An unresolved wikilink has no id to point at. Keep its title
				// as plain text instead of emitting a broken lumen:// link.
				run.Href = ""
				run.ObjectID = ""
				run.TypeID = ""
			}
			continue
		}
		if exported, ok := ctx.objects[target]; ok {
			run.Href = ctx.relativePath(exported)
			run.ObjectID = ""
			run.TypeID = ""
			if strings.TrimSpace(run.Text) == "" {
				run.Text = exported.Title
			}
			continue
		}
		title := strings.TrimSpace(run.Text)
		if title == "" {
			title = ctx.titleOf(target)
		}
		run.Href = ""
		run.ObjectID = ""
		run.TypeID = ""
		run.Text = degradedLinkText(title, target)
	}
}

func objectRunTarget(run Run) string {
	if run.ObjectID != "" {
		return run.ObjectID
	}
	if strings.HasPrefix(run.Href, objectHrefPrefix) {
		return strings.TrimPrefix(run.Href, objectHrefPrefix)
	}
	return ""
}

func rewriteExportImageRun(run *Run, ctx *exportLinkContext) {
	name := run.ImageName
	if name == "" && strings.HasPrefix(run.Href, "lumen://image/") {
		name = run.Href
	}
	if !strings.HasPrefix(name, "lumen://image/") {
		return
	}
	id := strings.TrimPrefix(name, "lumen://image/")
	if path, ok := ctx.imagePath(id); ok {
		run.ImageName = path
		return
	}
	run.Type = ""
	run.ImageName = ""
	run.Href = ""
	run.ObjectID = ""
	run.TypeID = ""
	run.Text = "image (id: " + id + ")"
}

func degradedLinkText(title, id string) string {
	if title == "" {
		return "id: " + id
	}
	return title + " (id: " + id + ")"
}

// ---------------------------------------------------------------------------
// YAML front matter
// ---------------------------------------------------------------------------

func buildExportFrontMatter(o Object) string {
	var b strings.Builder
	b.WriteString("---\n")
	writeYAMLField(&b, 0, "id", o.ID)
	writeYAMLField(&b, 0, "type_id", o.TypeID)
	writeYAMLField(&b, 0, "title", o.Title)
	writeYAMLField(&b, 0, "created_at", o.CreatedAt)
	writeYAMLField(&b, 0, "updated_at", o.UpdatedAt)
	writeYAMLField(&b, 0, "revision", o.Revision)
	if isTemplateObject(o) {
		writeYAMLField(&b, 0, "is_template", true)
	}
	if len(o.Links) > 0 {
		writeYAMLField(&b, 0, "links", o.Links)
	}
	properties := frontMatterProperties(o)
	if len(properties) > 0 {
		writeYAMLField(&b, 0, "properties", properties)
	}
	b.WriteString("---\n")
	return b.String()
}

func frontMatterProperties(o Object) map[string]any {
	properties := make(map[string]any, len(o.Properties))
	for key, value := range o.Properties {
		switch key {
		case "blocks", "rich_text", "is_template":
			// blocks are the body, rich_text is an editor snapshot, and
			// is_template is emitted as a top-level flag when true.
			continue
		}
		properties[key] = value
	}
	return properties
}

func writeYAMLField(b *strings.Builder, indent int, key string, value any) {
	b.WriteString(strings.Repeat(" ", indent))
	b.WriteString(yamlKey(key))
	b.WriteString(":")
	writeYAMLValue(b, indent, value)
}

func writeYAMLValue(b *strings.Builder, indent int, value any) {
	switch v := value.(type) {
	case map[string]any:
		if len(v) == 0 {
			b.WriteString(" {}\n")
			return
		}
		b.WriteString("\n")
		writeYAMLMap(b, indent+2, v)
	case []any:
		if len(v) == 0 {
			b.WriteString(" []\n")
			return
		}
		b.WriteString("\n")
		writeYAMLSeq(b, indent+2, v)
	case []string:
		if len(v) == 0 {
			b.WriteString(" []\n")
			return
		}
		b.WriteString("\n")
		writeYAMLSeqStrings(b, indent+2, v)
	default:
		b.WriteString(" ")
		b.WriteString(yamlScalar(v))
		b.WriteString("\n")
	}
}

func writeYAMLMap(b *strings.Builder, indent int, values map[string]any) {
	keys := make([]string, 0, len(values))
	for key := range values {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	for _, key := range keys {
		writeYAMLField(b, indent, key, values[key])
	}
}

func writeYAMLSeq(b *strings.Builder, indent int, values []any) {
	for _, item := range values {
		b.WriteString(strings.Repeat(" ", indent))
		b.WriteString("-")
		switch v := item.(type) {
		case map[string]any:
			if len(v) == 0 {
				b.WriteString(" {}\n")
				continue
			}
			b.WriteString("\n")
			writeYAMLMap(b, indent+2, v)
		case []any:
			if len(v) == 0 {
				b.WriteString(" []\n")
				continue
			}
			b.WriteString("\n")
			writeYAMLSeq(b, indent+2, v)
		case []string:
			if len(v) == 0 {
				b.WriteString(" []\n")
				continue
			}
			b.WriteString("\n")
			writeYAMLSeqStrings(b, indent+2, v)
		default:
			b.WriteString(" ")
			b.WriteString(yamlScalar(v))
			b.WriteString("\n")
		}
	}
}

func writeYAMLSeqStrings(b *strings.Builder, indent int, values []string) {
	for _, item := range values {
		b.WriteString(strings.Repeat(" ", indent))
		b.WriteString("- ")
		b.WriteString(yamlScalar(item))
		b.WriteString("\n")
	}
}

func yamlKey(key string) string {
	if yamlPlainSafe(key) {
		return key
	}
	raw, _ := json.Marshal(key)
	return string(raw)
}

func yamlScalar(value any) string {
	switch v := value.(type) {
	case nil:
		return "null"
	case string:
		if yamlPlainSafe(v) {
			return v
		}
		raw, _ := json.Marshal(v)
		return string(raw)
	case bool:
		return strconv.FormatBool(v)
	case float64:
		if math.IsNaN(v) || math.IsInf(v, 0) {
			return "null"
		}
		return strconv.FormatFloat(v, 'g', -1, 64)
	case float32:
		f := float64(v)
		if math.IsNaN(f) || math.IsInf(f, 0) {
			return "null"
		}
		return strconv.FormatFloat(f, 'g', -1, 64)
	case int:
		return strconv.Itoa(v)
	case int64:
		return strconv.FormatInt(v, 10)
	case uint64:
		return strconv.FormatUint(v, 10)
	case json.Number:
		return v.String()
	default:
		raw, err := json.Marshal(v)
		if err != nil {
			return "null"
		}
		return string(raw)
	}
}

// yamlPlainSafe is deliberately conservative: anything that could be read as a
// YAML scalar of another type, a comment, a flow collection or a block marker
// is emitted as a JSON-quoted string. JSON strings are valid YAML
// double-quoted scalars, so no YAML dependency is needed.
func yamlPlainSafe(value string) bool {
	if value == "" || strings.TrimSpace(value) != value {
		return false
	}
	if strings.ContainsAny(value, "\n\r\t") {
		return false
	}
	if strings.ContainsAny(value, ":#,[]{}&*!|>'\"%@`") {
		return false
	}
	if strings.HasPrefix(value, "-") || strings.HasPrefix(value, "?") || strings.HasPrefix(value, "+") || strings.HasPrefix(value, ".") {
		return false
	}
	switch strings.ToLower(value) {
	case "true", "false", "null", "~", "yes", "no", "on", "off":
		return false
	}
	if _, err := strconv.ParseFloat(value, 64); err == nil {
		return false
	}
	first, _ := utf8.DecodeRuneInString(value)
	if unicode.IsDigit(first) {
		// Dates and numeric-looking strings keep their string type.
		return false
	}
	for _, r := range value {
		if r < 0x20 || r == 0x7f {
			return false
		}
	}
	return true
}

// ---------------------------------------------------------------------------
// CSV values
// ---------------------------------------------------------------------------

func schemaPropertyKeys(typ ObjectType) []string {
	keys := map[string]bool{}
	for _, raw := range typ.Properties {
		key := ""
		switch schema := raw.(type) {
		case string:
			key = schema
		case map[string]any:
			key = schemaKey(schema)
		}
		if key == "" || exportCSVExcludedKey(key) {
			continue
		}
		keys[key] = true
	}
	return sortedStringSet(keys)
}

func exportCSVExcludedKey(key string) bool {
	switch key {
	case "blocks", "rich_text", "is_template", "id", "title":
		return true
	}
	return false
}

func sortedStringSet(set map[string]bool) []string {
	keys := make([]string, 0, len(set))
	for key := range set {
		keys = append(keys, key)
	}
	sort.Strings(keys)
	return keys
}

func csvPropertyValue(raw any) string {
	switch v := raw.(type) {
	case nil:
		return ""
	case string:
		return v
	case bool:
		return strconv.FormatBool(v)
	case float64:
		if math.IsNaN(v) || math.IsInf(v, 0) {
			return ""
		}
		return strconv.FormatFloat(v, 'f', -1, 64)
	case float32:
		f := float64(v)
		if math.IsNaN(f) || math.IsInf(f, 0) {
			return ""
		}
		return strconv.FormatFloat(f, 'f', -1, 64)
	case int:
		return strconv.Itoa(v)
	case int64:
		return strconv.FormatInt(v, 10)
	case uint64:
		return strconv.FormatUint(v, 10)
	case json.Number:
		return v.String()
	case []string:
		return strings.Join(v, ";")
	case []any:
		parts := make([]string, 0, len(v))
		for _, item := range v {
			parts = append(parts, csvListItem(item))
		}
		return strings.Join(parts, ";")
	case map[string]any:
		if start, ok := v["start"].(string); ok {
			if end, ok := v["end"].(string); ok && end != "" {
				return start + ".." + end
			}
			return start
		}
		return compactJSON(v)
	case map[string]string:
		if start, ok := v["start"]; ok {
			if end, ok := v["end"]; ok && end != "" {
				return start + ".." + end
			}
			return start
		}
		return compactJSON(v)
	default:
		return fmt.Sprint(v)
	}
}

func csvListItem(raw any) string {
	switch v := raw.(type) {
	case nil:
		return ""
	case string:
		return v
	case bool:
		return strconv.FormatBool(v)
	case float64:
		if math.IsNaN(v) || math.IsInf(v, 0) {
			return ""
		}
		return strconv.FormatFloat(v, 'f', -1, 64)
	case map[string]any:
		// Object relations are plain ids. Asset metadata is a map; compact JSON
		// keeps every field instead of silently dropping name/mime/size.
		return compactJSON(v)
	default:
		return fmt.Sprint(v)
	}
}

func compactJSON(value any) string {
	raw, err := json.Marshal(value)
	if err != nil {
		return fmt.Sprint(value)
	}
	return string(raw)
}

// ---------------------------------------------------------------------------
// Stable folder, object and asset names
// ---------------------------------------------------------------------------

// exportTypeFolders assigns one stable folder per type. Built-in type ids are
// already English slugs, so they are pluralized as English (note -> notes,
// query -> queries, person -> persons). Custom types use a UUID id, so their
// folder comes from the display name with Spanish pluralization (the UI
// language). Collisions get a numeric suffix; "assets" is reserved.
func exportTypeFolders(types []ObjectType) map[string]string {
	folders := map[string]string{}
	used := map[string]bool{}
	sorted := append([]ObjectType(nil), types...)
	sort.SliceStable(sorted, func(i, j int) bool { return sorted[i].ID < sorted[j].ID })
	for _, typ := range sorted {
		base := typ.ID
		english := exportSlugID(base)
		if !english {
			base = typ.Name
		}
		slug := slugify(base)
		if slug == "" {
			slug = "type-" + shortExportID(typ.ID)
			english = true
		}
		folder := pluralizeEnglish(slug)
		if !english {
			folder = pluralizeSpanish(slug)
		}
		if folder == "" {
			folder = "type-" + shortExportID(typ.ID)
		}
		if folder == "assets" {
			folder = "assets-type"
		}
		candidate := folder
		for suffix := 2; used[candidate]; suffix++ {
			candidate = folder + "-" + strconv.Itoa(suffix)
		}
		used[candidate] = true
		folders[typ.ID] = candidate
	}
	return folders
}

func exportFallbackFolder(typeID string) string {
	slug := slugify(typeID)
	if slug == "" {
		slug = "type-" + shortExportID(typeID)
	}
	return slug + "-objects"
}

func exportSlugID(id string) bool {
	if id == "" {
		return false
	}
	for i, r := range id {
		if i == 0 {
			if r < 'a' || r > 'z' {
				return false
			}
			continue
		}
		if (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9') || r == '_' {
			continue
		}
		return false
	}
	return true
}

func shortExportID(id string) string {
	safe := safeFilenameComponent(id)
	if len(safe) > 8 {
		safe = safe[:8]
	}
	if safe == "" {
		return "type"
	}
	return safe
}

func pluralizeEnglish(slug string) string {
	if strings.HasSuffix(slug, "y") && len(slug) > 1 {
		previous := slug[len(slug)-2]
		if !strings.ContainsRune("aeiou", rune(previous)) {
			return slug[:len(slug)-1] + "ies"
		}
	}
	if strings.HasSuffix(slug, "s") || strings.HasSuffix(slug, "x") || strings.HasSuffix(slug, "z") ||
		strings.HasSuffix(slug, "ch") || strings.HasSuffix(slug, "sh") {
		return slug + "es"
	}
	return slug + "s"
}

func pluralizeSpanish(slug string) string {
	if slug == "" {
		return ""
	}
	if strings.HasSuffix(slug, "z") {
		return slug[:len(slug)-1] + "ces"
	}
	last := slug[len(slug)-1]
	if strings.ContainsRune("aeiou", rune(last)) {
		return slug + "s"
	}
	return slug + "es"
}

func exportObjectFilename(o Object) string {
	slug := slugify(o.Title)
	if slug == "" {
		slug = "untitled"
	}
	return slug + "-" + safeFilenameComponent(o.ID) + ".md"
}

func safeFilenameComponent(value string) string {
	var b strings.Builder
	for _, r := range value {
		if (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') ||
			r == '-' || r == '_' || r == '.' {
			b.WriteRune(r)
			continue
		}
		b.WriteByte('-')
	}
	out := strings.Trim(b.String(), ".-")
	if out == "" {
		return "object"
	}
	return out
}

func exportAssetFilename(asset Asset) string {
	name := sanitizeAssetName(asset.Name)
	if name == "" {
		name = "asset"
	}
	return asset.ID + "-" + name
}

func sanitizeAssetName(name string) string {
	name = filepath.Base(strings.TrimSpace(name))
	var b strings.Builder
	lastDash := false
	for _, r := range name {
		switch {
		case (r >= 'a' && r <= 'z') || (r >= 'A' && r <= 'Z') || (r >= '0' && r <= '9') ||
			r == '.' || r == '_' || r == '-':
			b.WriteRune(r)
			lastDash = false
		case unicode.IsLetter(r) || unicode.IsDigit(r):
			b.WriteRune(r)
			lastDash = false
		default:
			if !lastDash && b.Len() > 0 {
				b.WriteByte('-')
				lastDash = true
			}
		}
	}
	out := strings.Trim(b.String(), ".-")
	// A separator right before the extension reads badly ("a-b-c-.pdf").
	out = strings.ReplaceAll(out, "-.", ".")
	if runes := []rune(out); len(runes) > exportAssetNameLimit {
		out = strings.Trim(string(runes[:exportAssetNameLimit]), ".-")
	}
	return out
}

var exportSlugReplacer = strings.NewReplacer(
	"á", "a", "à", "a", "ä", "a", "â", "a", "ã", "a", "å", "a",
	"é", "e", "è", "e", "ë", "e", "ê", "e",
	"í", "i", "ì", "i", "ï", "i", "î", "i",
	"ó", "o", "ò", "o", "ö", "o", "ô", "o", "õ", "o", "ø", "o",
	"ú", "u", "ù", "u", "ü", "u", "û", "u",
	"ñ", "n", "ç", "c", "ý", "y", "ÿ", "y",
	"æ", "ae", "œ", "oe", "ß", "ss", "đ", "d", "ł", "l",
	"ś", "s", "ź", "z", "ż", "z", "ć", "c", "ń", "n",
	"ő", "o", "ű", "u",
)

func slugify(value string) string {
	value = strings.ToLower(strings.TrimSpace(value))
	value = exportSlugReplacer.Replace(value)
	var b strings.Builder
	lastDash := false
	for _, r := range value {
		switch {
		case (r >= 'a' && r <= 'z') || (r >= '0' && r <= '9'):
			b.WriteRune(r)
			lastDash = false
		case unicode.IsLetter(r) || unicode.IsDigit(r):
			b.WriteRune(r)
			lastDash = false
		default:
			if !lastDash && b.Len() > 0 {
				b.WriteByte('-')
				lastDash = true
			}
		}
	}
	out := strings.Trim(b.String(), "-")
	if runes := []rune(out); len(runes) > exportSlugLimit {
		out = strings.Trim(string(runes[:exportSlugLimit]), "-")
	}
	return out
}
