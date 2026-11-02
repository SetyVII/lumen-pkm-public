package main

import (
	"encoding/json"
	"fmt"
	"os"
	"reflect"
	"strings"
	"testing"

	"lumen/server/internal/document"
)

func pushPendingFixture(t *testing.T, app *App, opID string, o Object) syncPushResponse {
	t.Helper()
	return decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "pending-reference-device", "base_seq": 0, "operations": []any{map[string]any{"op_id": opID, "action": "put", "base_revision": 0, "object": o}}}))
}

func TestSyncPreservesOfflineCyclesAndTypedPendingRelations(t *testing.T) {
	app, _, _ := makeApp(t)
	schema := []any{map[string]any{"key": "related", "type": "object", "target_type": "note", "multiple": true}}
	if rr := api(t, app, "PATCH", "/v1/types/note", map[string]any{"properties": schema}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	bdoc := documentFixture("B body")
	bdoc.Blocks[0].Content = append(bdoc.Blocks[0].Content, document.Inline{Type: "object_ref", ObjectID: "offline-cycle-a", Label: "A"})
	b := Object{ID: "offline-cycle-b", TypeID: "note", Title: "Offline B", Document: &bdoc, Links: []string{"offline-cycle-a"}, Properties: map[string]any{"related": []any{"offline-cycle-a"}}}
	first := pushPendingFixture(t, app, "offline-cycle-b-operation", b)
	if len(first.Errors) != 0 || len(first.Applied) != 1 {
		t.Fatalf("first offline cycle operation rejected: %+v", first)
	}
	stored, _ := app.store.Object(b.ID)
	if !sameStrings(stored.Links, []string{"offline-cycle-a"}) || !sameStrings(stored.PropertyLinks, []string{"offline-cycle-a"}) || !sameStrings(stored.DocumentLinks, []string{"offline-cycle-a"}) {
		t.Fatalf("pending references lost: %+v", stored)
	}
	values, ok := stored.Properties["related"].([]any)
	if !ok || len(values) != 1 || values[0] != "offline-cycle-a" {
		t.Fatal("pending typed property lost")
	}
	adoc := documentFixture("A body")
	a := Object{ID: "offline-cycle-a", TypeID: "note", Title: "Offline A", Document: &adoc, Links: []string{b.ID}, Properties: map[string]any{"related": []any{b.ID}}}
	second := pushPendingFixture(t, app, "offline-cycle-a-operation", a)
	if len(second.Errors) != 0 || len(second.Applied) != 1 {
		t.Fatalf("reverse cycle operation rejected %+v", second)
	}
	backlinks := decodeBody[[]Object](t, api(t, app, "GET", "/v1/objects/"+a.ID+"/backlinks", nil))
	if len(backlinks) != 1 || backlinks[0].ID != b.ID {
		t.Fatalf("pending backlink did not resolve: %+v", backlinks)
	}
}

func TestSyncPendingReferencesStillValidateKnownTypesAndSyntax(t *testing.T) {
	app, _, _ := makeApp(t)
	if rr := api(t, app, "PATCH", "/v1/types/note", map[string]any{"properties": []any{map[string]any{"key": "related", "type": "object", "target_type": "note"}}}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Known wrong type"}))
	d := documentFixture("Text")
	for _, tt := range []struct {
		name      string
		links     []string
		property  any
		errorText string
	}{
		{"known-type", nil, book.ID, "requires objects of type note"},
		{"invalid-property-id", nil, "has whitespace", "valid object value"},
		{"invalid-link-id", []string{"bad identity"}, nil, "identity is invalid"},
		{"invalid-property-value", nil, []any{123}, "valid object value"},
	} {
		t.Run(tt.name, func(t *testing.T) {
			o := Object{ID: "pending-invalid-" + tt.name, TypeID: "note", Title: tt.name, Document: &d, Links: tt.links, Properties: map[string]any{"related": tt.property}}
			response := pushPendingFixture(t, app, "operation-"+tt.name, o)
			if len(response.Errors) != 1 || !strings.Contains(response.Errors[0].Error, tt.errorText) {
				t.Fatalf("invalid relation accepted or wrong error %+v", response)
			}
			if app.store.Has("object", o.ID) {
				t.Fatal("invalid relation persisted")
			}
		})
	}
	if rr := api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Strict direct API", "document": d, "links": []string{"missing-direct-reference"}}); rr.Code != 400 {
		t.Fatalf("direct API accepted missing link %d", rr.Code)
	}
}

func TestSyncReferenceReadFailureDoesNotDropRelation(t *testing.T) {
	app, _, _ := makeApp(t)
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Corruption target"}))
	var payload []byte
	if err := app.store.db.QueryRow("SELECT payload FROM records WHERE kind='object' AND id=?", target.ID).Scan(&payload); err != nil {
		t.Fatal(err)
	}
	defer func() {
		_, _ = app.store.db.Exec("UPDATE records SET payload=? WHERE kind='object' AND id=?", payload, target.ID)
	}()
	if _, err := app.store.db.Exec("UPDATE records SET payload=? WHERE kind='object' AND id=?", []byte("broken ciphertext"), target.ID); err != nil {
		t.Fatal(err)
	}
	d := documentFixture("Text")
	source := Object{ID: "source-read-error", TypeID: "note", Title: "Read failure", Document: &d, Links: []string{target.ID}, Properties: map[string]any{}}
	if err := app.validateSyncObject(&source); err == nil {
		t.Fatal("database/decryption failure treated as pending missing target")
	}
	if len(source.Links) != 1 || source.Links[0] != target.ID {
		t.Fatal("read error silently dropped relation")
	}
}

func TestSyncFiltersPurgedCanonicalReferencesPreservingLabels(t *testing.T) {
	app, _, _ := makeApp(t)
	target := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Purged offline target"}))
	for _, path := range []string{"/v1/objects/" + target.ID, "/v1/trash/" + target.ID} {
		if rr := api(t, app, "DELETE", path, nil); rr.Code != 200 {
			t.Fatal(rr.Body.String())
		}
	}
	d := documentFixture("Survives")
	d.Blocks[0].Content = append(d.Blocks[0].Content, document.Inline{Type: "block_ref", ObjectID: target.ID, BlockID: "missing-block", Label: "Readable old reference"})
	source := Object{ID: "stale-canonical-reference", TypeID: "note", Title: "Stale canonical reference", Document: &d, Links: []string{target.ID}, Properties: map[string]any{}}
	response := pushPendingFixture(t, app, "stale-canonical-reference-op", source)
	if len(response.Errors) != 0 || len(response.Applied) != 1 {
		t.Fatalf("purged ref blocked sync %+v", response)
	}
	stored, err := app.store.Object(source.ID)
	if err != nil {
		t.Fatal(err)
	}
	if len(stored.Links) != 0 || len(stored.DocumentLinks) != 0 || len(document.References(*stored.Document)) != 0 || !strings.Contains(stored.Content, "Readable old reference") {
		t.Fatal("purged canonical reference resurrected or text lost")
	}
}

func TestSyncPendingWrongTypeArrivalReportsThenClearsIssue(t *testing.T) {
	app, _, _ := makeApp(t)
	if rr := api(t, app, "PATCH", "/v1/types/note", map[string]any{"properties": []any{map[string]any{"key": "related", "type": "object", "target_type": "note"}}}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	d := documentFixture("Keep source document untouched")
	source := Object{ID: "pending-typed-origin", TypeID: "note", Title: "Pending typed origin", Document: &d, Properties: map[string]any{"related": "arriving-wrong-type"}, Links: []string{}}
	first := pushPendingFixture(t, app, "pending-typed-origin-op", source)
	if len(first.Errors) != 0 {
		t.Fatalf("unknown target rejected %+v", first)
	}
	before, _ := app.store.Object(source.ID)
	targetDoc := documentFixture("Target")
	target := Object{ID: "arriving-wrong-type", TypeID: "book", Title: "Arriving target", Document: &targetDoc, Properties: map[string]any{}, Links: []string{}}
	arrived := pushPendingFixture(t, app, "arriving-wrong-type-op", target)
	if len(arrived.Errors) != 0 || len(arrived.Receipts) != 1 || len(arrived.Receipts[0].ReferenceIssues) != 1 {
		t.Fatalf("wrong arrival silently accepted %+v", arrived)
	}
	issue := arrived.Receipts[0].ReferenceIssues[0]
	if issue.SourceObjectID != source.ID || issue.PropertyKey != "related" || issue.ExpectedType != "note" || issue.ActualType != "book" {
		t.Fatalf("incorrect diagnostic %+v", issue)
	}
	report := decodeBody[struct {
		Issues     []referenceTypeIssue `json:"issues"`
		NextCursor string               `json:"next_cursor"`
	}](t, api(t, app, "GET", "/v1/objects/"+target.ID+"/reference-issues", nil))
	if len(report.Issues) != 1 || report.NextCursor != "" {
		t.Fatalf("missing read diagnostic %+v", report)
	}
	fixtureRaw, err := os.ReadFile("../contracts/fixtures/reference-issues/type-resolution.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture map[string]any
	if err = json.Unmarshal(fixtureRaw, &fixture); err != nil {
		t.Fatal(err)
	}
	reportJSON, _ := json.Marshal(map[string]any{"object_id": target.ID, "issues": report.Issues, "next_cursor": report.NextCursor})
	var actualReport any
	_ = json.Unmarshal(reportJSON, &actualReport)
	if !reflect.DeepEqual(actualReport, fixture["expected_report"]) {
		t.Fatal("reference issue wire report diverged from cross-client fixture")
	}

	after, _ := app.store.Object(source.ID)
	if !reflect.DeepEqual(before, after) {
		t.Fatal("diagnostic rewrote source data")
	}
	objectFrom(t, api(t, app, "POST", "/v1/objects/"+target.ID+"/convert", map[string]any{"target_type_id": "note"}))
	report = decodeBody[struct {
		Issues     []referenceTypeIssue `json:"issues"`
		NextCursor string               `json:"next_cursor"`
	}](t, api(t, app, "GET", "/v1/objects/"+target.ID+"/reference-issues", nil))
	if len(report.Issues) != 0 {
		t.Fatalf("corrected target still reported %+v", report)
	}
	if rr := api(t, app, "GET", "/v1/objects/"+target.ID+"/reference-issues?cursor=invalid!", nil); rr.Code != 400 {
		t.Fatal("invalid issue cursor accepted")
	}
}

func TestReferenceTypeIssuesAreBoundedAndPaginateWithinOneSource(t *testing.T) {
	app, _, _ := makeApp(t)
	schemas := []any{}
	properties := map[string]any{}
	for i := range 51 {
		key := fmt.Sprintf("relation-%03d", i)
		schemas = append(schemas, map[string]any{"key": key, "type": "object", "target_type": "note"})
		properties[key] = "paged-arriving-target"
	}
	if rr := api(t, app, "PATCH", "/v1/types/note", map[string]any{"properties": schemas}); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	d := documentFixture("Source remains intact")
	for i := range 4 {
		source := Object{ID: fmt.Sprintf("reference-page-source-%d", i), TypeID: "note", Title: fmt.Sprintf("Reference page source %d", i), Document: &d, Properties: properties, Links: []string{}}
		if pushed := pushPendingFixture(t, app, fmt.Sprintf("reference-page-source-op-%d", i), source); len(pushed.Errors) != 0 {
			t.Fatal(pushed.Errors)
		}
	}
	targetDoc := documentFixture("Target")
	target := Object{ID: "paged-arriving-target", TypeID: "book", Title: "Paged arriving target", Document: &targetDoc, Properties: map[string]any{}, Links: []string{}}
	pushed := pushPendingFixture(t, app, "reference-page-target-op", target)
	if len(pushed.Errors) != 0 || len(pushed.Receipts) != 1 {
		t.Fatal(pushed)
	}
	receipt := pushed.Receipts[0]
	if len(receipt.ReferenceIssues) != referenceIssuePageSize || receipt.ReferenceIssuesCursor == "" {
		t.Fatalf("unbounded or truncated without cursor %+v", receipt)
	}
	page := decodeBody[struct {
		Issues     []referenceTypeIssue `json:"issues"`
		NextCursor string               `json:"next_cursor"`
	}](t, api(t, app, "GET", "/v1/objects/"+target.ID+"/reference-issues?cursor="+receipt.ReferenceIssuesCursor, nil))
	if len(page.Issues) != 4 || page.Issues[0].PropertyKey != "relation-047" || page.NextCursor != "" {
		t.Fatalf("pagination lost a source property %+v", page)
	}
	objectFrom(t, api(t, app, "POST", "/v1/objects/"+target.ID+"/convert", map[string]any{"target_type_id": "note"}))
}
