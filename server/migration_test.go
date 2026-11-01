package main

import (
	"bytes"
	"database/sql"
	"encoding/json"
	"path/filepath"
	"strconv"
	"testing"
)

// Los tipos integrados ganan al reabrir las propiedades por defecto añadidas
// después de crear el almacén: un almacén anterior a las etiquetas recibe la
// propiedad tags en journal sin perder las definiciones del usuario.
func TestReopenAddsNewDefaultPropertiesToBuiltInTypes(t *testing.T) {
	path := filepath.Join(t.TempDir(), "legacy-types.sqlite")
	key := bytes.Repeat([]byte{0x32}, 32)
	store, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	legacy := ObjectType{
		ID: "journal", Name: "Diario", Icon: "view-calendar-day", Color: "#89a093",
		Properties: []any{"date"},
	}
	if err := store.Put("type", legacy.ID, legacy); err != nil {
		t.Fatal(err)
	}
	if err := store.Close(); err != nil {
		t.Fatal(err)
	}

	reopened, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	defer reopened.Close()
	app, err := NewApp(reopened, testToken, nil)
	if err != nil {
		t.Fatal(err)
	}
	rr := api(t, app, "GET", "/v1/types", nil)
	if rr.Code != 200 {
		t.Fatalf("HTTP %d listing types: %s", rr.Code, rr.Body.String())
	}
	var types []ObjectType
	if err := json.Unmarshal(rr.Body.Bytes(), &types); err != nil {
		t.Fatal(err)
	}
	for _, typ := range types {
		if typ.ID != "journal" {
			continue
		}
		keys := map[string]bool{}
		for _, raw := range typ.Properties {
			switch v := raw.(type) {
			case string:
				keys[v] = true
			case map[string]any:
				keys[v["key"].(string)] = true
			}
		}
		if !keys["date"] || !keys["tags"] {
			t.Fatalf("journal did not gain tags after reopen: %#v", typ.Properties)
		}
		return
	}
	t.Fatal("journal type missing from /v1/types")
}

func TestV1MigrationRemovesOutboxAndExporterState(t *testing.T) {
	path := filepath.Join(t.TempDir(), "legacy.sqlite")
	key := bytes.Repeat([]byte{0x31}, 32)
	store, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	legacy := Object{
		ID: "legacy-object", TypeID: "note", Title: "Legacy", Content: "old",
		Properties: map[string]any{}, Links: []string{},
		CreatedAt: now(), UpdatedAt: now(), SyncStatus: "pending", SyncError: "old error",
	}
	if err := store.Put("object", legacy.ID, legacy); err != nil {
		t.Fatal(err)
	}
	if err := store.Close(); err != nil {
		t.Fatal(err)
	}

	db, err := sql.Open("sqlite3", path)
	if err != nil {
		t.Fatal(err)
	}
	if _, err = db.Exec("DELETE FROM meta WHERE key='schema_version'"); err != nil {
		t.Fatal(err)
	}
	if _, err = db.Exec("INSERT INTO records(kind,id,payload) VALUES('outbox','legacy-object',?)", []byte("legacy-outbox")); err != nil {
		t.Fatal(err)
	}
	if err = db.Close(); err != nil {
		t.Fatal(err)
	}

	reopened, err := OpenStore(path, key)
	if err != nil {
		t.Fatal(err)
	}
	defer reopened.Close()
	var got Object
	if err = reopened.Get("object", legacy.ID, &got); err != nil {
		t.Fatal(err)
	}
	if got.SyncStatus != "synced" || got.SyncError != "" {
		t.Fatalf("migration left exporter state: %+v", got)
	}
	rows, err := reopened.List("outbox")
	if err != nil || len(rows) != 0 {
		t.Fatalf("migration left outbox rows: %d, %v", len(rows), err)
	}
	if err = reopened.Fsck(); err != nil {
		t.Fatal(err)
	}
	var version string
	if err = reopened.db.QueryRow("SELECT value FROM meta WHERE key='schema_version'").Scan(&version); err != nil || version != strconv.Itoa(schemaVersion) {
		t.Fatalf("schema_version = %q, %v", version, err)
	}
	// A newer database must be refused instead of being migrated backwards.
	if _, err = reopened.db.Exec("UPDATE meta SET value='99' WHERE key='schema_version'"); err != nil {
		t.Fatal(err)
	}
	if _, err = OpenStore(path, key); err == nil {
		t.Fatal("newer schema version was accepted")
	}
}
