package main

import (
	"reflect"
	"testing"
)

func TestPropertyIntentsPreserveUnfetchedFieldsAndCanonicalBody(t *testing.T) {
	a, _, _ := makeApp(t)
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Catalog-backed edit", "document": documentFixture("Keep my complete body"), "properties": map[string]any{"private_extension": map[string]any{"retained": true}, "obsolete": "remove", "summary": "old"}}))
	saved := objectFrom(t, api(t, a, "PATCH", "/v1/objects/"+o.ID, map[string]any{"base_revision": o.Revision, "op_id": "field-property-update", "property_updates": map[string]any{"summary": "new"}, "remove_properties": []string{"obsolete"}}))
	if !reflect.DeepEqual(saved.Document, o.Document) || !reflect.DeepEqual(saved.Properties["private_extension"], o.Properties["private_extension"]) || saved.Properties["summary"] != "new" {
		t.Fatal("field update replaced unfetched data")
	}
	if _, exists := saved.Properties["obsolete"]; exists {
		t.Fatal("requested property removal ignored")
	}
	replay := objectFrom(t, api(t, a, "PATCH", "/v1/objects/"+o.ID, map[string]any{"base_revision": o.Revision, "op_id": "field-property-update", "property_updates": map[string]any{"summary": "new"}, "remove_properties": []string{"obsolete"}}))
	if replay.Revision != saved.Revision {
		t.Fatal("property intent replay duplicated history")
	}
	if r := api(t, a, "PATCH", "/v1/objects/"+o.ID, map[string]any{"base_revision": o.Revision, "property_updates": map[string]any{"summary": "stale"}}); r.Code != 409 {
		t.Fatal("property field update bypassed CAS")
	}
}

func TestPropertyIntentsRejectAmbiguityAndManagedValuesAtomically(t *testing.T) {
	a, _, _ := makeApp(t)
	o := objectFrom(t, api(t, a, "POST", "/v1/objects", map[string]any{"type_id": "note", "title": "Atomic field update", "properties": map[string]any{"summary": "original"}}))
	for name, fields := range map[string]map[string]any{
		"replacement-and-intents": {"properties": map[string]any{}, "property_updates": map[string]any{"summary": "discarded"}},
		"set-and-remove":          {"property_updates": map[string]any{"summary": "discarded"}, "remove_properties": []string{"summary"}},
		"managed":                 {"property_updates": map[string]any{"summary": "discarded", "reader_content": "forged"}},
		"legacy":                  {"property_updates": map[string]any{"blocks": []any{}}},
		"nonobject":               {"property_updates": []any{}},
		"null":                    {"property_updates": nil},
		"invalid-cover":           {"property_updates": map[string]any{"image_asset_id": "missing-image"}},
	} {
		t.Run(name, func(t *testing.T) {
			fields["base_revision"] = o.Revision
			r := api(t, a, "PATCH", "/v1/objects/"+o.ID, fields)
			if r.Code != 400 {
				t.Fatalf("bad field intent accepted: %d %s", r.Code, r.Body.String())
			}
			current, _ := a.store.Object(o.ID)
			if current.Revision != o.Revision || !reflect.DeepEqual(current.Properties, o.Properties) {
				t.Fatal("invalid property batch partially committed")
			}
		})
	}
}
