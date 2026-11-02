package main

import (
	"archive/zip"
	"bytes"
	"encoding/csv"
	"encoding/json"
	"fmt"
	"io"
	"lumen/server/internal/document"
	"mime/multipart"
	"net/http"
	"net/http/httptest"
	"path"
	"reflect"
	"regexp"
	"sort"
	"strings"
	"testing"
)

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

func exportEntries(t *testing.T, app *App, method, target string, body any) map[string][]byte {
	t.Helper()
	rr := api(t, app, method, target, body)
	if rr.Code != http.StatusOK {
		t.Fatalf("export HTTP %d: %s", rr.Code, rr.Body.String())
	}
	if got := rr.Header().Get("Content-Type"); got != "application/zip" {
		t.Fatalf("export Content-Type = %q", got)
	}
	if disposition := rr.Header().Get("Content-Disposition"); !strings.HasPrefix(disposition, "attachment;") {
		t.Fatalf("export Content-Disposition = %q", disposition)
	}
	return unzipEntries(t, rr.Body.Bytes())
}

func unzipEntries(t *testing.T, data []byte) map[string][]byte {
	t.Helper()
	reader, err := zip.NewReader(bytes.NewReader(data), int64(len(data)))
	if err != nil {
		t.Fatal(err)
	}
	entries := map[string][]byte{}
	for _, file := range reader.File {
		rc, err := file.Open()
		if err != nil {
			t.Fatal(err)
		}
		content, err := io.ReadAll(rc)
		rc.Close()
		if err != nil {
			t.Fatal(err)
		}
		entries[file.Name] = content
	}
	return entries
}

func entryNames(entries map[string][]byte) []string {
	names := make([]string, 0, len(entries))
	for name := range entries {
		names = append(names, name)
	}
	sort.Strings(names)
	return names
}

func mustEntry(t *testing.T, entries map[string][]byte, name string) []byte {
	t.Helper()
	content, ok := entries[name]
	if !ok {
		t.Fatalf("missing ZIP entry %q; entries: %v", name, entryNames(entries))
	}
	return content
}

func typeFolder(t *testing.T, app *App, typeID string) string {
	t.Helper()
	folder := exportTypeFolders(app.store.Types())[typeID]
	if folder == "" {
		t.Fatalf("no export folder for type %q", typeID)
	}
	return folder
}

func objectEntry(t *testing.T, entries map[string][]byte, app *App, o Object) []byte {
	t.Helper()
	return mustEntry(t, entries, typeFolder(t, app, o.TypeID)+"/"+exportObjectFilename(o))
}

func frontMatterBody(t *testing.T, content []byte) (string, string) {
	t.Helper()
	text := string(content)
	if !strings.HasPrefix(text, "---\n") {
		t.Fatalf("missing front matter: %q", text)
	}
	rest := text[len("---\n"):]
	index := strings.Index(rest, "\n---\n")
	if index < 0 {
		t.Fatalf("unterminated front matter: %q", text)
	}
	return rest[:index], rest[index+len("\n---\n"):]
}

func manifestFrom(t *testing.T, entries map[string][]byte) exportManifest {
	t.Helper()
	var manifest exportManifest
	if err := json.Unmarshal(mustEntry(t, entries, "manifest.json"), &manifest); err != nil {
		t.Fatal(err)
	}
	return manifest
}

func csvTable(t *testing.T, entries map[string][]byte, name string) [][]string {
	t.Helper()
	reader := csv.NewReader(bytes.NewReader(mustEntry(t, entries, name)))
	rows, err := reader.ReadAll()
	if err != nil {
		t.Fatal(err)
	}
	return rows
}

func csvColumn(t *testing.T, rows [][]string, name string) int {
	t.Helper()
	if len(rows) == 0 {
		t.Fatalf("empty CSV")
	}
	for index, cell := range rows[0] {
		if cell == name {
			return index
		}
	}
	t.Fatalf("column %q not found in %v", name, rows[0])
	return -1
}

func csvRow(t *testing.T, rows [][]string, id string) []string {
	t.Helper()
	for _, row := range rows[1:] {
		if len(row) > 0 && row[0] == id {
			return row
		}
	}
	t.Fatalf("row %q not found in %v", id, rows)
	return nil
}

func uploadTestAsset(t *testing.T, app *App, objectID, name string, data []byte) Asset {
	t.Helper()
	body := bytes.NewBuffer(nil)
	writer := multipart.NewWriter(body)
	if objectID != "" {
		if err := writer.WriteField("object_id", objectID); err != nil {
			t.Fatal(err)
		}
	}
	part, err := writer.CreateFormFile("file", name)
	if err != nil {
		t.Fatal(err)
	}
	if _, err := part.Write(data); err != nil {
		t.Fatal(err)
	}
	if err := writer.Close(); err != nil {
		t.Fatal(err)
	}
	req := httptest.NewRequest("POST", "/v1/assets", body)
	req.Header.Set("Authorization", "Bearer "+testToken)
	req.Header.Set("Content-Type", writer.FormDataContentType())
	req.Header.Set("X-Lumen-Document-Version", "2")
	rr := httptest.NewRecorder()
	app.Handler().ServeHTTP(rr, req)
	if rr.Code != http.StatusCreated {
		t.Fatalf("asset upload HTTP %d: %s", rr.Code, rr.Body.String())
	}
	var asset Asset
	if err := json.Unmarshal(rr.Body.Bytes(), &asset); err != nil {
		t.Fatal(err)
	}
	return asset
}

// seedExportNotes writes valid objects directly in one transaction. It keeps
// the large-space test fast without bypassing the encryption or sync journal.
func seedExportNotes(t *testing.T, app *App, count int) []Object {
	t.Helper()
	objects := make([]Object, 0, count)
	records := make([]Record, 0, count)
	for i := 0; i < count; i++ {
		o := Object{
			ID: newID(), TypeID: "note", Title: fmt.Sprintf("Seeded note %03d", i),
			Properties: map[string]any{}, Links: []string{},
			CreatedAt: now(), UpdatedAt: now(), Revision: 1,
		}
		objects = append(objects, o)
		records = append(records, Record{Kind: "object", ID: o.ID, Value: o})
	}
	if err := app.store.PutBatch(records, nil); err != nil {
		t.Fatal(err)
	}
	return objects
}

var exportMarkdownLink = regexp.MustCompile(`!?\[[^\]]*\]\(([^)]+)\)`)

func assertRelativeLinksResolve(t *testing.T, entries map[string][]byte) {
	t.Helper()
	for name, content := range entries {
		if !strings.HasSuffix(name, ".md") {
			continue
		}
		for _, match := range exportMarkdownLink.FindAllStringSubmatch(string(content), -1) {
			target := match[1]
			if target == "" || strings.HasPrefix(target, "http://") || strings.HasPrefix(target, "https://") ||
				strings.HasPrefix(target, "mailto:") || strings.HasPrefix(target, "lumen://") ||
				strings.HasPrefix(target, "#") || strings.HasPrefix(target, "/") {
				continue
			}
			if strings.HasPrefix(target, "<") && strings.HasSuffix(target, ">") {
				target = strings.TrimSuffix(strings.TrimPrefix(target, "<"), ">")
			}
			resolved := path.Clean(path.Join(path.Dir(name), target))
			if _, ok := entries[resolved]; !ok {
				t.Fatalf("%s links to missing ZIP entry %q (from %q)", name, resolved, target)
			}
		}
	}
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

func TestExportRoundTrip(t *testing.T) {
	app, _, _ := makeApp(t)
	tag := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "tag", "title": "Go"}))
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Linked Note",
		"properties": map[string]any{"aliases": []string{"LN"}, "reader_content": "line one\nline two"},
	}))
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "book", "title": "Deep Work",
		"properties": map[string]any{
			"author": "Cal Newport", "rating": 5.0,
			"tags": []string{tag.ID}, "aliases": []string{"DW"},
			"period": map[string]any{"start": "2026-10-01", "end": "2026-10-05"},
		},
	}))
	png := []byte{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 0x00, 0x00, 0x00, 0x00}
	asset := uploadTestAsset(t, app, book.ID, "cover.png", png)
	if asset.MediaObjectID == "" {
		t.Fatal("upload did not create a reusable media object")
	}
	book = objectFrom(t, api(t, app, "GET", "/v1/objects/"+book.ID, nil))
	bookDoc := document.Document{SchemaVersion: 2, Blocks: []document.Node{
		{ID: newID(), Type: "heading", Attrs: map[string]any{"level": 1}, Content: []document.Inline{{Type: "text", Text: "Deep Work"}}},
		{ID: newID(), Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "See "}, {Type: "object_ref", ObjectID: note.ID, Label: "Linked Note"}}},
		{ID: newID(), Type: "image", Attrs: map[string]any{"asset_id": asset.ID, "object_id": asset.MediaObjectID, "width": 10, "height": 10}},
	}}
	book = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+book.ID, documentPatchPayload(t, app, book.ID, map[string]any{"document": bookDoc})))

	template := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "book", "title": "Book Template",
		"properties": map[string]any{"is_template": true, "author": "Template Author"},
	}))
	trashed := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Trashed Note"}))
	if rr := api(t, app, "DELETE", "/v1/objects/"+trashed.ID, nil); rr.Code != http.StatusOK {
		t.Fatalf("trash HTTP %d: %s", rr.Code, rr.Body.String())
	}

	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	manifest := manifestFrom(t, entries)
	if manifest.SchemaVersion != exportSchemaVersion || manifest.Format != exportFormat {
		t.Fatalf("unexpected manifest header: %+v", manifest)
	}
	// note, book, template, tag and the image media object created by upload.
	if manifest.ObjectCount != 5 || manifest.AssetCount != 1 || manifest.TemplateCount != 1 {
		t.Fatalf("unexpected manifest counts: %+v", manifest)
	}
	if manifest.Counts["note"] != 1 || manifest.Counts["book"] != 2 || manifest.Counts["tag"] != 1 || manifest.Counts["image"] != 1 {
		t.Fatalf("unexpected per-type counts: %v", manifest.Counts)
	}

	bookContent := objectEntry(t, entries, app, book)
	front, body := frontMatterBody(t, bookContent)
	if !strings.Contains(front, "id:") || !strings.Contains(front, book.ID) || !strings.Contains(front, "type_id: book") {
		t.Fatalf("front matter lost identity:\n%s", front)
	}
	if !strings.Contains(front, "title: Deep Work") {
		t.Fatalf("front matter lost title:\n%s", front)
	}
	if !strings.Contains(front, fmt.Sprintf("revision: %d", book.Revision)) {
		t.Fatalf("front matter lost revision:\n%s", front)
	}
	if !strings.Contains(front, "properties:") || !strings.Contains(front, "  author: Cal Newport") {
		t.Fatalf("front matter lost properties:\n%s", front)
	}
	if !strings.Contains(front, "  aliases:") || !strings.Contains(front, "    - DW") {
		t.Fatalf("front matter lost aliases:\n%s", front)
	}
	if !strings.Contains(front, "  tags:") || !strings.Contains(front, tag.ID) {
		t.Fatalf("front matter lost tags:\n%s", front)
	}
	if !strings.Contains(front, "  period:") || !strings.Contains(front, `    start: "2026-10-01"`) {
		t.Fatalf("front matter lost the date range:\n%s", front)
	}
	if strings.Contains(front, "is_template:") {
		t.Fatalf("normal book marked as a template:\n%s", front)
	}
	if !strings.Contains(front, "links:") || !strings.Contains(front, asset.MediaObjectID) {
		t.Fatalf("front matter lost explicit links:\n%s", front)
	}
	if !strings.Contains(body, "# Deep Work") {
		t.Fatalf("body lost the heading:\n%s", body)
	}
	if !strings.Contains(body, "[Linked Note](../notes/"+exportObjectFilename(note)+")") {
		t.Fatalf("object link was not rewritten:\n%s", body)
	}
	if !strings.Contains(body, "![](../assets/"+exportAssetFilename(asset)+")") {
		t.Fatalf("image run was not rewritten:\n%s", body)
	}

	noteFront, _ := frontMatterBody(t, objectEntry(t, entries, app, note))
	if !strings.Contains(noteFront, `reader_content: "line one\nline two"`) {
		t.Fatalf("reader_content not preserved as a YAML string:\n%s", noteFront)
	}
	if !strings.Contains(noteFront, "  aliases:") || !strings.Contains(noteFront, "    - LN") {
		t.Fatalf("note aliases missing:\n%s", noteFront)
	}

	templateFront, _ := frontMatterBody(t, objectEntry(t, entries, app, template))
	if !strings.Contains(templateFront, "is_template: true") {
		t.Fatalf("template flag missing:\n%s", templateFront)
	}

	if _, ok := entries[typeFolder(t, app, "note")+"/"+exportObjectFilename(trashed)]; ok {
		t.Fatal("trashed object was exported")
	}

	assetPath := "assets/" + exportAssetFilename(asset)
	if !bytes.Equal(mustEntry(t, entries, assetPath), png) {
		t.Fatal("exported asset bytes differ from the uploaded bytes")
	}
	if len(manifest.Assets) != 1 || manifest.Assets[0].Path != assetPath ||
		manifest.Assets[0].MediaObjectID != asset.MediaObjectID {
		t.Fatalf("manifest asset entry is wrong: %+v", manifest.Assets)
	}

	bookRows := csvTable(t, entries, typeFolder(t, app, "book")+".csv")
	aliasesColumn := csvColumn(t, bookRows, "aliases")
	tagsColumn := csvColumn(t, bookRows, "tags")
	periodColumn := csvColumn(t, bookRows, "period")
	templateColumn := csvColumn(t, bookRows, "is_template")
	bookRow := csvRow(t, bookRows, book.ID)
	if bookRow[aliasesColumn] != "DW" || bookRow[tagsColumn] != tag.ID ||
		bookRow[periodColumn] != "2026-10-01..2026-10-05" || bookRow[templateColumn] != "false" {
		t.Fatalf("unexpected book CSV row: %v", bookRow)
	}
	templateRow := csvRow(t, bookRows, template.ID)
	if templateRow[templateColumn] != "true" {
		t.Fatalf("template CSV row is not flagged: %v", templateRow)
	}

	noteRows := csvTable(t, entries, typeFolder(t, app, "note")+".csv")
	readerColumn := csvColumn(t, noteRows, "reader_content")
	noteRow := csvRow(t, noteRows, note.ID)
	if noteRow[readerColumn] != "line one\nline two" {
		t.Fatalf("reader_content lost in CSV: %q", noteRow[readerColumn])
	}

	var types []ObjectType
	if err := json.Unmarshal(mustEntry(t, entries, "types.json"), &types); err != nil {
		t.Fatal(err)
	}
	var noteSchema *ObjectType
	for i := range types {
		if types[i].ID == "note" {
			noteSchema = &types[i]
		}
	}
	if noteSchema == nil {
		t.Fatal("types.json does not describe the note type")
	}
	schemaKeys := map[string]bool{}
	for _, raw := range noteSchema.Properties {
		switch schema := raw.(type) {
		case string:
			schemaKeys[schema] = true
		case map[string]any:
			schemaKeys[schemaKey(schema)] = true
		}
	}
	if !schemaKeys["tags"] || !schemaKeys["is_template"] {
		t.Fatalf("types.json lost the note schema: %v", schemaKeys)
	}

	assertRelativeLinksResolve(t, entries)
}

func TestExportFiltersAndDegradation(t *testing.T) {
	app, _, _ := makeApp(t)
	alpha := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Alpha Unique"}))
	beta := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Beta"}))
	linker := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "book", "title": "Linker",
		"properties": map[string]any{"blocks": []any{
			map[string]any{"id": newID(), "type": "paragraph", "text": "Beta", "runs": []any{
				map[string]any{"text": "Beta", "href": "lumen://object/" + beta.ID, "object_id": beta.ID, "type_id": "note"},
			}},
		}},
	}))

	entries := exportEntries(t, app, "GET", "/v1/export?type_ids=note&q=unique", nil)
	manifest := manifestFrom(t, entries)
	if manifest.ObjectCount != 1 {
		t.Fatalf("filtered export count = %d", manifest.ObjectCount)
	}
	if !reflect.DeepEqual(manifest.Options.TypeIDs, []string{"note"}) || manifest.Options.Q != "unique" ||
		!manifest.Options.IncludeAssets || !manifest.Options.IncludeTemplates {
		t.Fatalf("manifest options do not echo the request: %+v", manifest.Options)
	}
	if _, ok := entries[typeFolder(t, app, "note")+"/"+exportObjectFilename(alpha)]; !ok {
		t.Fatal("matching note missing from filtered export")
	}
	if _, ok := entries[typeFolder(t, app, "note")+"/"+exportObjectFilename(beta)]; ok {
		t.Fatal("non-matching note exported")
	}
	if _, ok := entries[typeFolder(t, app, "book")+"/"+exportObjectFilename(linker)]; ok {
		t.Fatal("book exported despite type_ids=note")
	}

	entries = exportEntries(t, app, "GET", "/v1/export?type_ids=book", nil)
	_, body := frontMatterBody(t, objectEntry(t, entries, app, linker))
	if !strings.Contains(body, "Beta (id: "+beta.ID+")") {
		t.Fatalf("filtered-out link did not degrade to plain text:\n%s", body)
	}
	if strings.Contains(body, "../notes/") {
		t.Fatalf("filtered-out link still points at a missing file:\n%s", body)
	}

	entries = exportEntries(t, app, "POST", "/v1/export", map[string]any{"type_ids": []string{"note"}, "q": "beta"})
	if manifest := manifestFrom(t, entries); manifest.ObjectCount != 1 {
		t.Fatalf("POST filtered export count = %d", manifest.ObjectCount)
	}
	if _, ok := entries[typeFolder(t, app, "note")+"/"+exportObjectFilename(beta)]; !ok {
		t.Fatal("POST q filter missed the matching note")
	}

	if rr := api(t, app, "GET", "/v1/export?type_ids=does-not-exist", nil); rr.Code != http.StatusBadRequest {
		t.Fatalf("unknown type id returned %d: %s", rr.Code, rr.Body.String())
	}
}

func TestExportTemplatesAndTrash(t *testing.T) {
	app, _, _ := makeApp(t)
	template := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Note Template",
		"properties": map[string]any{"is_template": true},
	}))
	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	if _, ok := entries[typeFolder(t, app, "note")+"/"+exportObjectFilename(template)]; !ok {
		t.Fatal("template missing from the default export")
	}
	front, _ := frontMatterBody(t, objectEntry(t, entries, app, template))
	if !strings.Contains(front, "is_template: true") {
		t.Fatalf("template front matter missing the flag:\n%s", front)
	}
	if manifest := manifestFrom(t, entries); manifest.TemplateCount != 1 {
		t.Fatalf("template count = %d", manifest.TemplateCount)
	}

	entries = exportEntries(t, app, "GET", "/v1/export?include_templates=false", nil)
	if _, ok := entries[typeFolder(t, app, "note")+"/"+exportObjectFilename(template)]; ok {
		t.Fatal("template exported despite include_templates=false")
	}
	if manifest := manifestFrom(t, entries); manifest.TemplateCount != 0 {
		t.Fatalf("template count with include_templates=false = %d", manifest.TemplateCount)
	}

	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Trash Me"}))
	if rr := api(t, app, "DELETE", "/v1/objects/"+note.ID, nil); rr.Code != http.StatusOK {
		t.Fatalf("trash HTTP %d: %s", rr.Code, rr.Body.String())
	}
	entries = exportEntries(t, app, "GET", "/v1/export", nil)
	if _, ok := entries[typeFolder(t, app, "note")+"/"+exportObjectFilename(note)]; ok {
		t.Fatal("trashed object exported")
	}
	if rr := api(t, app, "POST", "/v1/objects/"+note.ID+"/restore", nil); rr.Code != http.StatusOK {
		t.Fatalf("restore HTTP %d: %s", rr.Code, rr.Body.String())
	}
	entries = exportEntries(t, app, "GET", "/v1/export", nil)
	if _, ok := entries[typeFolder(t, app, "note")+"/"+exportObjectFilename(note)]; !ok {
		t.Fatal("restored object missing from the export")
	}
}

func TestExportAssetsOptionAndBytes(t *testing.T) {
	app, _, _ := makeApp(t)
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Illustrated"}))
	png := []byte{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a, 1, 2, 3, 4}
	asset := uploadTestAsset(t, app, book.ID, "figure.png", png)
	book = objectFrom(t, api(t, app, "GET", "/v1/objects/"+book.ID, nil))
	imageDoc := document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: newID(), Type: "image", Attrs: map[string]any{"asset_id": asset.ID, "object_id": asset.MediaObjectID}}}}
	book = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+book.ID, documentPatchPayload(t, app, book.ID, map[string]any{"document": imageDoc})))

	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	assetPath := "assets/" + exportAssetFilename(asset)
	if !bytes.Equal(mustEntry(t, entries, assetPath), png) {
		t.Fatal("asset bytes changed during export")
	}
	_, body := frontMatterBody(t, objectEntry(t, entries, app, book))
	if !strings.Contains(body, "![](../"+assetPath+")") {
		t.Fatalf("image run does not point at the asset:\n%s", body)
	}

	entries = exportEntries(t, app, "GET", "/v1/export?include_assets=false", nil)
	for name := range entries {
		if strings.HasPrefix(name, "assets/") {
			t.Fatalf("asset exported despite include_assets=false: %s", name)
		}
	}
	manifest := manifestFrom(t, entries)
	if manifest.AssetCount != 0 || manifest.Options.IncludeAssets {
		t.Fatalf("include_assets=false not reflected in the manifest: %+v", manifest)
	}
	_, body = frontMatterBody(t, objectEntry(t, entries, app, book))
	if !strings.Contains(body, "image (id: "+asset.MediaObjectID+")") {
		t.Fatalf("excluded image did not degrade to text:\n%s", body)
	}
	if strings.Contains(body, "../assets/") {
		t.Fatalf("excluded image still points at a missing asset:\n%s", body)
	}
}

func TestExportPDFAsset(t *testing.T) {
	app, _, _ := makeApp(t)
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Paper"}))
	pdf := []byte("%PDF-1.4\nexport me\n")
	asset := uploadTestAsset(t, app, book.ID, "paper.pdf", pdf)
	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	assetPath := "assets/" + exportAssetFilename(asset)
	if !bytes.Equal(mustEntry(t, entries, assetPath), pdf) {
		t.Fatal("PDF asset bytes changed during export")
	}
	media := objectFrom(t, api(t, app, "GET", "/v1/objects/"+asset.MediaObjectID, nil))
	front, body := frontMatterBody(t, objectEntry(t, entries, app, media))
	if !strings.Contains(front, "asset_id:") || !strings.Contains(front, asset.ID) {
		t.Fatalf("PDF object lost its asset reference:\n%s", front)
	}
	if strings.TrimSpace(body) != "" {
		t.Fatalf("PDF object should have no Markdown body, got %q", body)
	}
}

func TestExportEmptySpace(t *testing.T) {
	app, _, _ := makeApp(t)
	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	if names := entryNames(entries); !reflect.DeepEqual(names, []string{"manifest.json", "types.json"}) {
		t.Fatalf("empty export entries = %v", names)
	}
	manifest := manifestFrom(t, entries)
	if manifest.ObjectCount != 0 || manifest.AssetCount != 0 || manifest.TemplateCount != 0 {
		t.Fatalf("empty export counts = %+v", manifest)
	}
	if manifest.Counts["note"] != 0 {
		t.Fatalf("empty export note count = %d", manifest.Counts["note"])
	}
	var types []ObjectType
	if err := json.Unmarshal(mustEntry(t, entries, "types.json"), &types); err != nil {
		t.Fatal(err)
	}
	if len(types) == 0 {
		t.Fatal("types.json is empty")
	}
}

func TestExportLargeSpace(t *testing.T) {
	app, _, _ := makeApp(t)
	seedExportNotes(t, app, 300)
	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	manifest := manifestFrom(t, entries)
	if manifest.ObjectCount != 300 {
		t.Fatalf("large export object count = %d", manifest.ObjectCount)
	}
	folder := typeFolder(t, app, "note")
	count := 0
	for name := range entries {
		if strings.HasPrefix(name, folder+"/") && strings.HasSuffix(name, ".md") {
			count++
		}
	}
	if count != 300 {
		t.Fatalf("large export wrote %d Markdown files", count)
	}
	rows := csvTable(t, entries, folder+".csv")
	if len(rows) != 301 {
		t.Fatalf("large export CSV has %d rows", len(rows))
	}
}

func TestExportStoreBatches(t *testing.T) {
	app, _, _ := makeApp(t)
	seeded := seedExportNotes(t, app, 5)
	var ids []string
	if err := app.store.ObjectBatches(2, func(batch []Object) error {
		for _, o := range batch {
			ids = append(ids, o.ID)
		}
		return nil
	}); err != nil {
		t.Fatal(err)
	}
	got := append([]string(nil), ids...)
	sort.Strings(got)
	want := make([]string, 0, len(seeded))
	for _, o := range seeded {
		want = append(want, o.ID)
	}
	sort.Strings(want)
	if !reflect.DeepEqual(got, want) {
		t.Fatalf("ObjectBatches returned %v, want %v", got, want)
	}

	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Asset owner"}))
	asset := uploadTestAsset(t, app, book.ID, "a.png", []byte{0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a})
	var assets []Asset
	if err := app.store.AssetBatches(1, func(batch []Asset) error {
		assets = append(assets, batch...)
		return nil
	}); err != nil {
		t.Fatal(err)
	}
	if len(assets) != 1 || assets[0].ID != asset.ID {
		t.Fatalf("AssetBatches returned %+v", assets)
	}
}

func TestExportAuth(t *testing.T) {
	app, _, _ := makeApp(t)
	req := httptest.NewRequest("GET", "/v1/export", nil)
	rr := httptest.NewRecorder()
	app.Handler().ServeHTTP(rr, req)
	if rr.Code != http.StatusUnauthorized {
		t.Fatalf("export without a bearer token returned %d", rr.Code)
	}
}

func TestExportYAMLFrontMatterEscaping(t *testing.T) {
	app, _, _ := makeApp(t)
	note := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note",
		"title":   `A: B # C "quoted"`,
		"properties": map[string]any{
			"reader_content": "line1\nline2",
			"aliases":        []string{"A;B", "true"},
			"date":           "2026-10-31",
		},
	}))
	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	front, _ := frontMatterBody(t, objectEntry(t, entries, app, note))
	if !strings.Contains(front, `title: "A: B # C \"quoted\""`) {
		t.Fatalf("title was not quoted safely:\n%s", front)
	}
	if !strings.Contains(front, `reader_content: "line1\nline2"`) {
		t.Fatalf("multiline reader_content was not quoted safely:\n%s", front)
	}
	if !strings.Contains(front, `- "true"`) {
		t.Fatalf("reserved alias was not quoted:\n%s", front)
	}
	if !strings.Contains(front, `- A;B`) {
		t.Fatalf("plain alias was over-quoted:\n%s", front)
	}
	if !strings.Contains(front, `date: "2026-10-31"`) {
		t.Fatalf("date string was not preserved:\n%s", front)
	}
}

func TestExportCSVValues(t *testing.T) {
	cases := []struct {
		name string
		raw  any
		want string
	}{
		{"date range", map[string]any{"start": "2026-10-01", "end": "2026-10-05"}, "2026-10-01..2026-10-05"},
		{"single date", "2026-10-01", "2026-10-01"},
		{"ids", []any{"a", "b"}, "a;b"},
		{"bool", true, "true"},
		{"number", 5.0, "5"},
		{"assets", []any{map[string]any{"id": "x"}}, `{"id":"x"}`},
		{"recurrence", map[string]any{"mode": "daily"}, `{"mode":"daily"}`},
		{"nil", nil, ""},
	}
	for _, tc := range cases {
		if got := csvPropertyValue(tc.raw); got != tc.want {
			t.Errorf("%s: got %q, want %q", tc.name, got, tc.want)
		}
	}
}

func TestExportFolderAndFilenamePolicy(t *testing.T) {
	types := []ObjectType{
		{ID: "note", Name: "Nota"}, {ID: "book", Name: "Libro"}, {ID: "person", Name: "Persona"},
		{ID: "query", Name: "Consulta"}, {ID: "collection", Name: "Collection"},
		{ID: "a0f7c1f0-6a4e-4f2a-9b8e-3d1c2a5b7e90", Name: "Receta"},
		{ID: "b0f7c1f0-6a4e-4f2a-9b8e-3d1c2a5b7e90", Name: "Receta"},
		{ID: "asset", Name: "Asset"},
	}
	folders := exportTypeFolders(types)
	wants := map[string]string{
		"note": "notes", "book": "books", "person": "persons", "query": "queries", "collection": "collections",
		"a0f7c1f0-6a4e-4f2a-9b8e-3d1c2a5b7e90": "recetas",
		"b0f7c1f0-6a4e-4f2a-9b8e-3d1c2a5b7e90": "recetas-2",
		"asset":                                "assets-type",
	}
	for id, want := range wants {
		if folders[id] != want {
			t.Errorf("folder for %s = %q, want %q", id, folders[id], want)
		}
	}

	if got := exportObjectFilename(Object{ID: "abc-123", Title: "Héllo / World: A Test"}); got != "hello-world-a-test-abc-123.md" {
		t.Errorf("object filename = %q", got)
	}
	if got := exportObjectFilename(Object{ID: "abc-123"}); got != "untitled-abc-123.md" {
		t.Errorf("empty-title filename = %q", got)
	}
	if exportObjectFilename(Object{ID: "id-1", Title: "Same"}) == exportObjectFilename(Object{ID: "id-2", Title: "Same"}) {
		t.Error("same-title objects must not collide")
	}
	if got := exportAssetFilename(Asset{ID: "asset-1", Name: "../secret/cover.png"}); got != "asset-1-cover.png" {
		t.Errorf("asset filename = %q", got)
	}
	if got := exportAssetFilename(Asset{ID: "asset-1", Name: "a b:c?.pdf"}); got != "asset-1-a-b-c.pdf" {
		t.Errorf("sanitized asset filename = %q", got)
	}
	if got := exportAssetFilename(Asset{ID: "asset-1"}); got != "asset-1-asset" {
		t.Errorf("empty asset filename = %q", got)
	}
}

func TestExportGETAndPOSTEquivalent(t *testing.T) {
	app, _, _ := makeApp(t)
	objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "One"}))
	getEntries := exportEntries(t, app, "GET", "/v1/export", nil)
	postEntries := exportEntries(t, app, "POST", "/v1/export", map[string]any{})
	if !reflect.DeepEqual(entryNames(getEntries), entryNames(postEntries)) {
		t.Fatalf("GET and POST produced different entries:\nGET  %v\nPOST %v", entryNames(getEntries), entryNames(postEntries))
	}
}

func TestExportCanonicalRichMarksAndPlainText(t *testing.T) {
	app, _, _ := makeApp(t)
	rich := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Rich Only",
		"document": document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: newID(), Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Hello "}, {Type: "text", Text: "world", Marks: map[string]any{"bold": true}}}}}},
	}))
	plain := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Plain Only", "document": document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: newID(), Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Plain body"}}}}},
	}))
	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	_, richBody := frontMatterBody(t, objectEntry(t, entries, app, rich))
	if strings.TrimSpace(richBody) != "Hello **world**" {
		t.Fatalf("rich_text fallback = %q", richBody)
	}
	_, plainBody := frontMatterBody(t, objectEntry(t, entries, app, plain))
	if strings.TrimSpace(plainBody) != "Plain body" {
		t.Fatalf("content fallback = %q", plainBody)
	}
}

func TestExportTableAndColumnLinks(t *testing.T) {
	app, _, _ := makeApp(t)
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Target"}))
	refParagraph := func(label string) document.Node {
		return document.Node{ID: newID(), Type: "paragraph", Content: []document.Inline{{Type: "object_ref", ObjectID: target.ID, Label: label}}}
	}
	ownerDoc := document.Document{SchemaVersion: 2, Blocks: []document.Node{
		{ID: newID(), Type: "table", Attrs: map[string]any{"columns": []any{map[string]any{"id": newID(), "width": 180}}}, Children: []document.Node{{ID: newID(), Type: "table_row", Children: []document.Node{{ID: newID(), Type: "table_cell", Children: []document.Node{refParagraph("Target")}}}}}},
		{ID: newID(), Type: "columns", Children: []document.Node{{ID: newID(), Type: "column", Attrs: map[string]any{"width": 1}, Children: []document.Node{refParagraph("Nested")}}}},
	}}
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Nested Links", "document": ownerDoc}))

	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	_, body := frontMatterBody(t, objectEntry(t, entries, app, owner))
	link := "../notes/" + exportObjectFilename(target)
	if strings.Count(body, "("+link+")") < 2 {
		t.Fatalf("table/column links were not rewritten:\n%s", body)
	}
}

func TestExportLinkToTrashedObjectDegrades(t *testing.T) {
	app, _, _ := makeApp(t)
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Gone"}))
	owner := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Owner",
		"properties": map[string]any{"blocks": []any{
			map[string]any{"id": newID(), "type": "paragraph", "text": "Gone", "runs": []any{
				map[string]any{"text": "Gone", "href": "lumen://object/" + target.ID, "object_id": target.ID},
			}},
		}},
	}))
	if rr := api(t, app, "DELETE", "/v1/objects/"+target.ID, nil); rr.Code != http.StatusOK {
		t.Fatalf("trash HTTP %d: %s", rr.Code, rr.Body.String())
	}
	entries := exportEntries(t, app, "GET", "/v1/export", nil)
	_, body := frontMatterBody(t, objectEntry(t, entries, app, owner))
	if !strings.Contains(body, "Gone (id: "+target.ID+")") {
		t.Fatalf("trashed link did not degrade:\n%s", body)
	}
	if strings.Contains(body, "../notes/") {
		t.Fatalf("trashed link still points at a missing file:\n%s", body)
	}
}
