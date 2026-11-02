package main

import (
	"encoding/json"
	"strings"
	"testing"
)

func createCollection(t *testing.T, app *App, title, itemType string, items []string) Object {
	t.Helper()
	properties := map[string]any{"item_type_id": itemType}
	if items != nil {
		properties["items"] = items
	}
	return objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{
		"type_id": "collection", "title": title, "properties": properties,
	}))
}

func storedCollectionItemIDs(t *testing.T, o Object) []string {
	t.Helper()
	ids, err := collectionItemIDs(o.Properties["items"])
	if err != nil {
		t.Fatalf("invalid stored items: %v", err)
	}
	return ids
}

func TestCollectionTypeCreationAndValidation(t *testing.T) {
	app, _, _ := makeApp(t)

	types := api(t, app, "GET", "/v1/types", nil)
	if types.Code != 200 || !strings.Contains(types.Body.String(), `"id":"collection"`) {
		t.Fatalf("default types do not expose collection: %s", types.Body.String())
	}
	var listed []ObjectType
	if err := json.Unmarshal(types.Body.Bytes(), &listed); err != nil {
		t.Fatal(err)
	}
	var collectionType *ObjectType
	for i := range listed {
		if listed[i].ID == "collection" {
			collectionType = &listed[i]
		}
	}
	if collectionType == nil || collectionType.Name != "Collection" {
		t.Fatalf("collection type missing or misnamed: %+v", collectionType)
	}
	foundItemType, foundItems := false, false
	for _, raw := range collectionType.Properties {
		schema, ok := raw.(map[string]any)
		if !ok {
			continue
		}
		switch schemaKey(schema) {
		case "item_type_id":
			foundItemType = schema["type"] == "text"
		case "items":
			foundItems = schema["type"] == "object" && schema["multiple"] == true
		}
	}
	if !foundItemType || !foundItems {
		t.Fatalf("collection schema is incomplete: %+v", collectionType.Properties)
	}

	one := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Collection book one"}))
	two := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Collection book two"}))
	person := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "person", "title": "Collection person"}))

	collection := createCollection(t, app, "Holiday books", "book", []string{one.ID, two.ID})
	if collection.TypeID != "collection" || collection.Properties["item_type_id"] != "book" {
		t.Fatalf("collection identity was not stored: %+v", collection)
	}
	if ids := storedCollectionItemIDs(t, collection); !sameStrings(ids, []string{one.ID, two.ID}) {
		t.Fatalf("item order was not preserved: %v", ids)
	}
	if !sameStrings(collection.PropertyLinks, []string{one.ID, two.ID}) {
		t.Fatalf("items did not become property links: %v", collection.PropertyLinks)
	}

	renamed := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+collection.ID, map[string]any{
		"title": "Holiday reading",
		"properties": map[string]any{
			"item_type_id": "book", "items": []string{one.ID, two.ID}, "summary": "Curated by hand",
		},
	}))
	if renamed.Title != "Holiday reading" || renamed.Properties["summary"] != "Curated by hand" ||
		renamed.Revision != collection.Revision+1 || !sameStrings(storedCollectionItemIDs(t, renamed), []string{one.ID, two.ID}) {
		t.Fatalf("title/description patch damaged the collection: %+v", renamed)
	}

	if rr := api(t, app, "PATCH", "/v1/objects/"+collection.ID, map[string]any{
		"properties": map[string]any{"item_type_id": "person", "items": []string{one.ID}},
	}); rr.Code != 400 {
		t.Fatalf("item_type_id was mutable: %d %s", rr.Code, rr.Body.String())
	}
	same := objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+collection.ID, map[string]any{
		"properties": map[string]any{"item_type_id": "book", "items": []string{one.ID, two.ID}, "summary": "Curated by hand"},
	}))
	if same.Revision != renamed.Revision {
		t.Fatalf("no-op collection patch bumped revision %d -> %d", renamed.Revision, same.Revision)
	}

	for name, properties := range map[string]map[string]any{
		"missing item type":  {"items": []string{}},
		"unknown item type":  {"item_type_id": "missing-type"},
		"nested collection":  {"item_type_id": "collection"},
		"wrong item type":    {"item_type_id": "book", "items": []string{person.ID}},
		"missing item":       {"item_type_id": "book", "items": []string{"missing-object-id"}},
		"duplicate item":     {"item_type_id": "book", "items": []string{one.ID, one.ID}},
		"non-string item id": {"item_type_id": "book", "items": []any{42}},
	} {
		rr := api(t, app, "POST", "/v1/objects", map[string]any{
			"type_id": "collection", "title": "Rejected " + name, "properties": properties,
		})
		if rr.Code != 400 {
			t.Fatalf("%s was accepted: %d %s", name, rr.Code, rr.Body.String())
		}
	}

	empty := createCollection(t, app, "Empty shelf", "book", nil)
	if ids := storedCollectionItemIDs(t, empty); len(ids) != 0 {
		t.Fatalf("empty collection gained items: %v", ids)
	}
	if !strings.Contains(api(t, app, "GET", "/v1/objects?type_id=collection", nil).Body.String(), "Empty shelf") {
		t.Fatal("collections are not listed by type")
	}
}

func TestCollectionTypeSchemaIsProtected(t *testing.T) {
	app, _, _ := makeApp(t)
	for name, properties := range map[string][]any{
		"missing items": {
			map[string]any{"key": "item_type_id", "name": "Item type", "type": "text"},
		},
		"items not a relation": {
			map[string]any{"key": "item_type_id", "name": "Item type", "type": "text"},
			map[string]any{"key": "items", "name": "Items", "type": "text"},
		},
		"missing item type": {
			map[string]any{"key": "items", "name": "Items", "type": "object", "multiple": true},
		},
	} {
		if rr := api(t, app, "PATCH", "/v1/types/collection", map[string]any{"properties": properties}); rr.Code != 400 {
			t.Fatalf("%s was accepted: %d %s", name, rr.Code, rr.Body.String())
		}
	}
	if rr := api(t, app, "PATCH", "/v1/types/collection", map[string]any{"name": "Collections", "color": "#123456"}); rr.Code != 200 {
		t.Fatalf("renaming the collection type failed: %s", rr.Body.String())
	}
}

func TestCollectionItemEndpoints(t *testing.T) {
	app, _, _ := makeApp(t)
	makeBook := func(title string) Object {
		return objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": title}))
	}
	a, b, c, d := makeBook("Item A"), makeBook("Item B"), makeBook("Item C"), makeBook("Item D")
	person := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "person", "title": "Item person"}))
	collection := createCollection(t, app, "Reading queue", "book", nil)
	base := "/v1/collections/" + collection.ID + "/items"

	collection = objectFrom(t, api(t, app, "POST", base, map[string]any{"object_id": a.ID}))
	if ids := storedCollectionItemIDs(t, collection); !sameStrings(ids, []string{a.ID}) {
		t.Fatalf("append failed: %v", ids)
	}
	collection = objectFrom(t, api(t, app, "POST", base, map[string]any{"object_id": c.ID, "position": 0}))
	collection = objectFrom(t, api(t, app, "POST", base, map[string]any{"object_id": b.ID, "position": 1}))
	if ids := storedCollectionItemIDs(t, collection); !sameStrings(ids, []string{c.ID, b.ID, a.ID}) {
		t.Fatalf("insert did not preserve order: %v", ids)
	}
	collection = objectFrom(t, api(t, app, "POST", base, map[string]any{"object_id": d.ID, "position": 3}))
	if ids := storedCollectionItemIDs(t, collection); !sameStrings(ids, []string{c.ID, b.ID, a.ID, d.ID}) {
		t.Fatalf("position at the end was not an append: %v", ids)
	}

	for name, body := range map[string]map[string]any{
		"duplicate":         {"object_id": b.ID},
		"wrong type":        {"object_id": person.ID},
		"missing object":    {"object_id": "missing-object-id"},
		"negative position": {"object_id": person.ID, "position": -1},
		"position too high": {"object_id": person.ID, "position": 99},
		"empty object id":   {"object_id": "  "},
	} {
		want := 400
		if name == "duplicate" {
			want = 409
		}
		if rr := api(t, app, "POST", base, body); rr.Code != want {
			t.Fatalf("%s returned %d: %s", name, rr.Code, rr.Body.String())
		}
	}

	resolved := api(t, app, "GET", base, nil)
	var listed collectionItemsResponse
	if err := json.Unmarshal(resolved.Body.Bytes(), &listed); err != nil {
		t.Fatal(err)
	}
	if listed.CollectionID != collection.ID || listed.ItemTypeID != "book" ||
		!sameStrings(listed.ItemIDs, []string{c.ID, b.ID, a.ID, d.ID}) || len(listed.Items) != 4 {
		t.Fatalf("resolved items are inconsistent: %+v", listed)
	}
	for i, want := range []string{c.ID, b.ID, a.ID, d.ID} {
		if listed.Items[i].ID != want || listed.Items[i].TypeID != "book" {
			t.Fatalf("resolved item %d is wrong: %+v", i, listed.Items[i])
		}
	}

	collection = objectFrom(t, api(t, app, "PUT", base, map[string]any{"items": []string{a.ID, d.ID, c.ID, b.ID}}))
	if ids := storedCollectionItemIDs(t, collection); !sameStrings(ids, []string{a.ID, d.ID, c.ID, b.ID}) {
		t.Fatalf("replace did not reorder: %v", ids)
	}
	revision := collection.Revision
	same := objectFrom(t, api(t, app, "PUT", base, map[string]any{"items": []string{a.ID, d.ID, c.ID, b.ID}}))
	if same.Revision != revision {
		t.Fatalf("identical replace bumped revision %d -> %d", revision, same.Revision)
	}
	for name, body := range map[string]any{
		"duplicate":  map[string]any{"items": []string{a.ID, a.ID}},
		"wrong type": map[string]any{"items": []string{person.ID}},
		"missing":    map[string]any{"items": []string{"missing-object-id"}},
		"no items":   map[string]any{},
	} {
		if rr := api(t, app, "PUT", base, body); rr.Code != 400 {
			t.Fatalf("replace with %s returned %d: %s", name, rr.Code, rr.Body.String())
		}
	}

	collection = objectFrom(t, api(t, app, "DELETE", base+"/"+d.ID, nil))
	if ids := storedCollectionItemIDs(t, collection); !sameStrings(ids, []string{a.ID, c.ID, b.ID}) {
		t.Fatalf("remove did not preserve the rest of the order: %v", ids)
	}
	if rr := api(t, app, "DELETE", base+"/"+d.ID, nil); rr.Code != 404 {
		t.Fatalf("removing an absent item returned %d", rr.Code)
	}

	// Reordering through the generic PATCH is also supported; properties is an
	// explicit replacement document, so the client resends item_type_id too.
	collection = objectFrom(t, api(t, app, "PATCH", "/v1/objects/"+collection.ID, map[string]any{
		"properties": map[string]any{"item_type_id": "book", "items": []string{b.ID, a.ID, c.ID}},
	}))
	if ids := storedCollectionItemIDs(t, collection); !sameStrings(ids, []string{b.ID, a.ID, c.ID}) {
		t.Fatalf("PATCH reorder failed: %v", ids)
	}

	if rr := api(t, app, "GET", "/v1/collections/"+a.ID+"/items", nil); rr.Code != 400 {
		t.Fatalf("items of a non-collection returned %d", rr.Code)
	}
	if rr := api(t, app, "GET", "/v1/collections/missing/items", nil); rr.Code != 404 {
		t.Fatalf("items of a missing collection returned %d", rr.Code)
	}
}

func TestCollectionObjectDeletionCleansItems(t *testing.T) {
	app, _, _ := makeApp(t)
	one := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Deletion one"}))
	two := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Deletion two"}))
	three := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Deletion three"}))
	first := createCollection(t, app, "First list", "book", []string{one.ID, two.ID, three.ID})
	second := createCollection(t, app, "Second list", "book", []string{two.ID})

	// Deleting moves the object to the trash. The stored `items` array keeps
	// the id so a restore can bring the membership back (trash semantics);
	// the collection read paths must hide it in the meantime.
	if rr := api(t, app, "DELETE", "/v1/objects/"+two.ID, nil); rr.Code != 200 {
		t.Fatalf("delete failed: %s", rr.Body.String())
	}
	first = objectFrom(t, api(t, app, "GET", "/v1/objects/"+first.ID, nil))
	if ids := storedCollectionItemIDs(t, first); !sameStrings(ids, []string{one.ID, two.ID, three.ID}) {
		t.Fatalf("trash must preserve stored items for restore: %v", ids)
	}
	rr := api(t, app, "GET", "/v1/collections/"+first.ID+"/items", nil)
	var listed struct {
		ItemIDs []string `json:"item_ids"`
		Items   []Object `json:"items"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &listed); err != nil {
		t.Fatalf("items read failed: %s", rr.Body.String())
	}
	visibleIDs := make([]string, 0, len(listed.Items))
	for _, o := range listed.Items {
		visibleIDs = append(visibleIDs, o.ID)
	}
	if !sameStrings(visibleIDs, []string{one.ID, three.ID}) {
		t.Fatalf("trashed item is visible to reads: %v", visibleIDs)
	}
	if rr := api(t, app, "GET", "/v1/objects/"+two.ID+"/backlinks?type_id=collection", nil); rr.Code != 404 {
		t.Fatalf("trashed object advertises backlinks: %d %s", rr.Code, rr.Body.String())
	}

	// Restoring the object revives its collection memberships.
	if rr := api(t, app, "POST", "/v1/objects/"+two.ID+"/restore", nil); rr.Code != 200 {
		t.Fatalf("restore failed: %s", rr.Body.String())
	}
	rr = api(t, app, "GET", "/v1/collections/"+first.ID+"/items", nil)
	listed.Items = nil
	if err := json.Unmarshal(rr.Body.Bytes(), &listed); err != nil {
		t.Fatalf("items read failed: %s", rr.Body.String())
	}
	visibleIDs = visibleIDs[:0]
	for _, o := range listed.Items {
		visibleIDs = append(visibleIDs, o.ID)
	}
	if !sameStrings(visibleIDs, []string{one.ID, two.ID, three.ID}) {
		t.Fatalf("restore did not revive membership: %v", visibleIDs)
	}

	// Trashing again and purging runs the transactional cleanup: the stored
	// arrays lose the id everywhere and property links follow.
	if rr := api(t, app, "DELETE", "/v1/objects/"+two.ID, nil); rr.Code != 200 {
		t.Fatalf("second delete failed: %s", rr.Body.String())
	}
	if rr := api(t, app, "DELETE", "/v1/trash/"+two.ID, nil); rr.Code != 200 {
		t.Fatalf("purge failed: %s", rr.Body.String())
	}
	first = objectFrom(t, api(t, app, "GET", "/v1/objects/"+first.ID, nil))
	second = objectFrom(t, api(t, app, "GET", "/v1/objects/"+second.ID, nil))
	if ids := storedCollectionItemIDs(t, first); !sameStrings(ids, []string{one.ID, three.ID}) {
		t.Fatalf("purge corrupted the first collection: %v", ids)
	}
	if ids := storedCollectionItemIDs(t, second); len(ids) != 0 {
		t.Fatalf("purge left a dangling item: %v", ids)
	}
	if !sameStrings(first.PropertyLinks, []string{one.ID, three.ID}) {
		t.Fatalf("purge left stale property links: %v", first.PropertyLinks)
	}

	var containing []Object
	rr = api(t, app, "GET", "/v1/objects/"+one.ID+"/backlinks?type_id=collection", nil)
	if err := json.Unmarshal(rr.Body.Bytes(), &containing); err != nil || len(containing) != 1 || containing[0].ID != first.ID {
		t.Fatalf("collections containing an object were not listed: %s", rr.Body.String())
	}
	if rr := api(t, app, "GET", "/v1/objects/"+two.ID+"/backlinks?type_id=collection", nil); strings.TrimSpace(rr.Body.String()) != "[]" {
		t.Fatalf("purged object still advertises collections: %s", rr.Body.String())
	}
}

func TestCollectionSyncPushCannotChangeItemType(t *testing.T) {
	app, _, _ := makeApp(t)
	book := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Sync collection book"}))
	collection := createCollection(t, app, "Sync collection", "book", []string{book.ID})
	push := map[string]any{
		"device_id": "phone-collection-test", "base_seq": 0,
		"operations": []any{map[string]any{
			"op_id": "op-collection-item-type-1", "action": "put", "base_revision": collection.Revision,
			"object": map[string]any{
				"id": collection.ID, "type_id": "collection", "title": collection.Title, "document": collection.Document,
				"properties": map[string]any{"item_type_id": "person", "items": []string{}},
				"links":      []string{}, "created_at": collection.CreatedAt, "updated_at": now(),
			},
		}},
	}
	resp := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", push))
	if len(resp.Errors) != 1 || !strings.Contains(resp.Errors[0].Error, "item_type_id") {
		t.Fatalf("sync push changed item_type_id: %+v", resp)
	}
	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+collection.ID, nil))
	if got.Properties["item_type_id"] != "book" || !sameStrings(storedCollectionItemIDs(t, got), []string{book.ID}) {
		t.Fatalf("rejected push damaged the collection: %+v", got)
	}
}

func TestCollectionOrderSurvivesRestart(t *testing.T) {
	app, path, key := makeApp(t)
	one := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Restart one"}))
	two := objectFrom(t, api(t, app, "POST", "/v1/objects", map[string]any{"type_id": "book", "title": "Restart two"}))
	collection := createCollection(t, app, "Restart list", "book", []string{two.ID, one.ID})

	if err := app.store.Close(); err != nil {
		t.Fatal(err)
	}
	store, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	app.store = store

	got := objectFrom(t, api(t, app, "GET", "/v1/objects/"+collection.ID, nil))
	if ids := storedCollectionItemIDs(t, got); !sameStrings(ids, []string{two.ID, one.ID}) {
		t.Fatalf("stored order changed across restart: %v", ids)
	}
	rr := api(t, app, "GET", "/v1/collections/"+collection.ID+"/items", nil)
	var listed collectionItemsResponse
	if err := json.Unmarshal(rr.Body.Bytes(), &listed); err != nil {
		t.Fatal(err)
	}
	if !sameStrings(listed.ItemIDs, []string{two.ID, one.ID}) || len(listed.Items) != 2 ||
		listed.Items[0].ID != two.ID || listed.Items[1].ID != one.ID {
		t.Fatalf("resolved order changed across restart: %+v", listed)
	}
}
