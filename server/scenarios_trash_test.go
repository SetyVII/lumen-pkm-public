package main

// User journey: delete a person by mistake, notice the page and the collection
// still reference it, restore it, and finally purge it on purpose. This
// combines trash, collections, backlinks, search and reference cleanup.

import (
	"testing"
)

func TestScenarioDeleteRegretPurgeJourney(t *testing.T) {
	app, _, _ := makeApp(t)

	person := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "person", "title": "Grace Hopper", "content": "Compiler pioneer",
	}))
	person = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+person.ID, map[string]any{
		"content": "Compiler pioneer and COBOL advocate",
	}))
	if versions := versionSummaries(t, app, person.ID); len(versions) != 1 {
		t.Fatalf("fixture did not create version history: %+v", versions)
	}
	page := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "note", "title": "Compiler notes", "content": "Grace invented the compiler",
		"links": []string{person.ID},
	}))
	collection := createCollection(t, app, "People I quote", "person", []string{person.ID})

	// Step 1: delete the person. The page keeps the stored reference so a
	// restore can bring the relation back; reads hide the target.
	scenarioStatus(t, api(t, app, "DELETE", "/v1/objects/"+person.ID, nil), 200)
	page = scenarioGet(t, app, page.ID)
	if !scenarioContainsID(page.Links, person.ID) {
		t.Fatalf("trash rewrote the inbound reference: %v", page.Links)
	}
	scenarioStatus(t, api(t, app, "GET", "/v1/objects/"+person.ID, nil), 404)
	scenarioStatus(t, api(t, app, "GET", "/v1/objects/"+person.ID+"/backlinks", nil), 404)
	if ids := scenarioSearchIDs(t, app, "Grace Hopper"); scenarioContainsID(ids, person.ID) {
		t.Fatalf("trashed person is still searchable: %v", ids)
	}

	// Step 2: the collection read path hides the trashed item while the stored
	// order keeps it for the restore.
	items := decodeBody[collectionItemsResponse](t, api(t, app, "GET", "/v1/collections/"+collection.ID+"/items", nil))
	if !scenarioContainsID(items.ItemIDs, person.ID) {
		t.Fatalf("trash dropped the stored collection item: %v", items.ItemIDs)
	}
	if len(items.Items) != 0 {
		t.Fatalf("trashed person is visible in the collection: %+v", items.Items)
	}
	if hits := queryObjects(t, app, "type_id=person&collection="+collection.ID); len(hits) != 0 {
		t.Fatalf("collection filter returned a trashed item: %v", objectIDs(hits))
	}

	// Step 3: restore. Everything comes back: the page backlink, the
	// collection membership and search.
	restored := objectFrom(t, api(t, app, "POST", "/v1/objects/"+person.ID+"/restore", nil))
	if restored.Title != "Grace Hopper" || restored.Content != "Compiler pioneer and COBOL advocate" {
		t.Fatalf("restore lost the person: %+v", restored)
	}
	if versions := versionSummaries(t, app, person.ID); len(versions) != 1 {
		t.Fatalf("restore lost the version history: %+v", versions)
	}
	backlinks := scenarioBacklinks(t, app, person.ID)
	if len(backlinks) != 2 || !scenarioContainsID(objectIDs(backlinks), page.ID) || !scenarioContainsID(objectIDs(backlinks), collection.ID) {
		t.Fatalf("restore did not bring every backlink back: %+v", backlinks)
	}
	items = decodeBody[collectionItemsResponse](t, api(t, app, "GET", "/v1/collections/"+collection.ID+"/items", nil))
	if len(items.Items) != 1 || items.Items[0].ID != person.ID {
		t.Fatalf("restore did not revive the collection membership: %+v", items)
	}
	if ids := scenarioSearchIDs(t, app, "Grace Hopper"); !scenarioContainsID(ids, person.ID) {
		t.Fatalf("restore did not bring the person back to search: %v", ids)
	}

	// Step 4: delete again, then purge permanently. This time the references
	// are cleaned for real.
	scenarioStatus(t, api(t, app, "DELETE", "/v1/objects/"+person.ID, nil), 200)
	scenarioStatus(t, api(t, app, "DELETE", "/v1/trash/"+person.ID, nil), 200)
	page = scenarioGet(t, app, page.ID)
	if scenarioContainsID(page.Links, person.ID) || scenarioContainsID(page.PropertyLinks, person.ID) {
		t.Fatalf("purge left a dangling reference: links=%v property_links=%v", page.Links, page.PropertyLinks)
	}
	collection = scenarioGet(t, app, collection.ID)
	stored, err := collectionItemIDs(collection.Properties["items"])
	if err != nil {
		t.Fatal(err)
	}
	if len(stored) != 0 {
		t.Fatalf("purge left the person in the collection: %v", stored)
	}
	if ids := scenarioSearchIDs(t, app, "Grace Hopper"); scenarioContainsID(ids, person.ID) {
		t.Fatalf("purged person is still searchable: %v", ids)
	}
	var versionCount int
	if err := app.store.db.QueryRow("SELECT count(*) FROM object_versions WHERE object_id=?", person.ID).Scan(&versionCount); err != nil {
		t.Fatal(err)
	}
	if versionCount != 0 {
		t.Fatalf("purge left %d version snapshots behind", versionCount)
	}
	scenarioStatus(t, api(t, app, "POST", "/v1/objects/"+person.ID+"/restore", nil), 404)
}
