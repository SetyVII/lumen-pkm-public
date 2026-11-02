package main

import (
	"encoding/json"
	"fmt"
)

// Startup must not retain every decrypted document while constructing an index
// whose final shape contains only metadata. Stream one payload at a time, then
// sort the small metadata snapshot once instead of incrementally sorting bodies.
func (s *Store) rebuildIndex() error {
	metas := []ObjectMeta{}
	rows, err := s.db.Query("SELECT id,payload FROM records WHERE kind='object' ORDER BY id")
	if err != nil {
		return err
	}
	for rows.Next() {
		var id string
		var sealed []byte
		if err = rows.Scan(&id, &sealed); err != nil {
			rows.Close()
			return err
		}
		raw, err := s.decrypt(sealed, "object/"+id)
		if err != nil {
			rows.Close()
			return err
		}
		var o Object
		if err = json.Unmarshal(raw, &o); err != nil {
			rows.Close()
			return err
		}
		if o.ID != id || o.Document == nil {
			rows.Close()
			return fmt.Errorf("object/%s requires a canonical Document 2; use a clean workspace for this coordinated upgrade", id)
		}
		metas = append(metas, metaFromObject(o))
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return err
	}
	rows.Close()
	types := []ObjectType{}
	rows, err = s.db.Query("SELECT id,payload FROM records WHERE kind='type' ORDER BY id")
	if err != nil {
		return err
	}
	for rows.Next() {
		var id string
		var sealed []byte
		if err = rows.Scan(&id, &sealed); err != nil {
			rows.Close()
			return err
		}
		raw, err := s.decrypt(sealed, "type/"+id)
		if err != nil {
			rows.Close()
			return err
		}
		var t ObjectType
		if err = json.Unmarshal(raw, &t); err != nil {
			rows.Close()
			return err
		}
		if t.ID != id {
			rows.Close()
			return fmt.Errorf("type/%s contains identity %q", id, t.ID)
		}
		types = append(types, t)
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return err
	}
	rows.Close()
	s.index.rebuildMetadata(metas, types)
	return nil
}
