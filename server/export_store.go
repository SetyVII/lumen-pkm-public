package main

import (
	"encoding/json"
	"fmt"
)

// exportBatchSize bounds how many decrypted payloads an export keeps in RAM at
// once. The in-memory index already holds metadata for every object; an export
// must not turn the whole workspace into a slice of full Objects.
const exportBatchSize = 50

// ObjectBatches walks every live object in id order using keyset pagination.
//
// Rows are fully read and the cursor is closed before fn runs, so fn may call
// back into the store (which uses a single SQLite connection) without
// deadlocking. Trashed objects live in the separate "trash" record kind and are
// therefore never returned: a full export is a view of live data only.
func (s *Store) ObjectBatches(batch int, fn func([]Object) error) error {
	if batch <= 0 {
		batch = exportBatchSize
	}
	last := ""
	for {
		rows, err := s.db.Query("SELECT id,payload FROM records WHERE kind='object' AND id>? ORDER BY id LIMIT ?", last, batch)
		if err != nil {
			return err
		}
		objects := make([]Object, 0, batch)
		for rows.Next() {
			var id string
			var payload []byte
			if err := rows.Scan(&id, &payload); err != nil {
				rows.Close()
				return err
			}
			raw, err := s.decrypt(payload, "object/"+id)
			if err != nil {
				rows.Close()
				return fmt.Errorf("object/%s: %w", id, err)
			}
			var o Object
			if err := json.Unmarshal(raw, &o); err != nil {
				rows.Close()
				return fmt.Errorf("object/%s: %w", id, err)
			}
			if o.ID != id {
				rows.Close()
				return fmt.Errorf("object/%s contains id %q", id, o.ID)
			}
			objects = append(objects, o)
			last = id
		}
		if err := rows.Err(); err != nil {
			rows.Close()
			return err
		}
		rows.Close()
		if len(objects) == 0 {
			return nil
		}
		if err := fn(objects); err != nil {
			return err
		}
		if len(objects) < batch {
			return nil
		}
	}
}

// AssetBatches walks attachment metadata in id order with the same bounded
// contract as ObjectBatches. The caller decrypts one blob at a time, exactly
// like GET /v1/assets/{id}/content does.
func (s *Store) AssetBatches(batch int, fn func([]Asset) error) error {
	if batch <= 0 {
		batch = exportBatchSize
	}
	last := ""
	for {
		rows, err := s.db.Query("SELECT id,payload FROM records WHERE kind='asset' AND id>? ORDER BY id LIMIT ?", last, batch)
		if err != nil {
			return err
		}
		assets := make([]Asset, 0, batch)
		for rows.Next() {
			var id string
			var payload []byte
			if err := rows.Scan(&id, &payload); err != nil {
				rows.Close()
				return err
			}
			raw, err := s.decrypt(payload, "asset/"+id)
			if err != nil {
				rows.Close()
				return fmt.Errorf("asset/%s: %w", id, err)
			}
			var asset Asset
			if err := json.Unmarshal(raw, &asset); err != nil {
				rows.Close()
				return fmt.Errorf("asset/%s: %w", id, err)
			}
			if asset.ID != id {
				rows.Close()
				return fmt.Errorf("asset/%s contains id %q", id, asset.ID)
			}
			assets = append(assets, asset)
			last = id
		}
		if err := rows.Err(); err != nil {
			rows.Close()
			return err
		}
		rows.Close()
		if len(assets) == 0 {
			return nil
		}
		if err := fn(assets); err != nil {
			return err
		}
		if len(assets) < batch {
			return nil
		}
	}
}
