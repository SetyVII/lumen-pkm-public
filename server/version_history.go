package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"lumen/server/internal/document"
	"strconv"
	"strings"
)

const maxObjectVersions = 20

// versionRecordKind marks an in-memory record as a snapshot. PutBatch
// intercepts it; it is never a valid row kind in the records table.
const versionRecordKind = "version"

// ObjectVersion is the immutable snapshot of an object as it was before a
// revision bump. It lives in the object_versions side table, encrypted with
// AES-256-GCM and associated with its object_id/revision pair.
type ObjectVersion struct {
	Document      *document.Document `json:"document,omitempty"`
	DocumentLinks []string           `json:"document_links,omitempty"`
	ObjectID      string             `json:"object_id"`
	Revision      uint64             `json:"revision"`
	TypeID        string             `json:"type_id"`
	Title         string             `json:"title"`
	Content       string             `json:"content"`
	Properties    map[string]any     `json:"properties"`
	Links         []string           `json:"links"`
	UpdatedAt     string             `json:"updated_at"`
	CapturedAt    string             `json:"captured_at"`
}

// VersionRecord is how prepareSave hands a snapshot to the store. PutBatch
// intercepts it before the generic record path, so snapshots never enter the
// records table and therefore never reach the sync journal.
type VersionRecord struct {
	ObjectID string
	Revision uint64
	Value    ObjectVersion
}

func versionAAD(objectID string, revision uint64) string {
	return "version/" + objectID + "/" + strconv.FormatUint(revision, 10)
}

func snapshotFromObject(o Object) ObjectVersion {
	properties := cloneStringMap(o.Properties)
	if properties == nil {
		properties = map[string]any{}
	}
	return ObjectVersion{
		ObjectID:      o.ID,
		Document:      cloneDocument(o.Document),
		DocumentLinks: append([]string(nil), o.DocumentLinks...),
		Revision:      o.Revision,
		TypeID:        o.TypeID,
		Title:         o.Title,
		Content:       o.Content,
		Properties:    properties,
		Links:         append([]string{}, o.Links...),
		UpdatedAt:     o.UpdatedAt,
		CapturedAt:    now(),
	}
}

// versionTimestamp is the moment the snapshotted state was last current. Legacy
// objects without updated_at fall back to the capture time.
func versionTimestamp(v ObjectVersion) string {
	if v.UpdatedAt != "" {
		return v.UpdatedAt
	}
	return v.CapturedAt
}

func versionWordCount(content string) int { return len(strings.Fields(content)) }
func versionBytes(content string) int     { return len(content) }

func (s *Store) decodeVersion(objectID string, revision uint64, payload []byte) (ObjectVersion, error) {
	raw, err := s.decrypt(payload, versionAAD(objectID, revision))
	if err != nil {
		return ObjectVersion{}, err
	}
	var v ObjectVersion
	if err = json.Unmarshal(raw, &v); err != nil {
		return ObjectVersion{}, err
	}
	if v.ObjectID != objectID || v.Revision != revision {
		return ObjectVersion{}, fmt.Errorf("version/%s/%d contains %s/%d", objectID, revision, v.ObjectID, v.Revision)
	}
	return v, nil
}

// ListVersions returns the stored snapshots newest first. The cap keeps this
// list small enough to decrypt on demand; no version payload is cached in RAM.
func (s *Store) ListVersions(objectID string) ([]ObjectVersion, error) {
	rows, err := s.db.Query("SELECT revision,payload FROM object_versions WHERE object_id=? ORDER BY revision DESC", objectID)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := []ObjectVersion{}
	for rows.Next() {
		var revision int64
		var payload []byte
		if err = rows.Scan(&revision, &payload); err != nil {
			return nil, err
		}
		v, err := s.decodeVersion(objectID, uint64(revision), payload)
		if err != nil {
			return nil, err
		}
		out = append(out, v)
	}
	return out, rows.Err()
}

func (s *Store) Version(objectID string, revision uint64) (ObjectVersion, error) {
	var payload []byte
	if err := s.db.QueryRow("SELECT payload FROM object_versions WHERE object_id=? AND revision=?", objectID, revision).Scan(&payload); err != nil {
		return ObjectVersion{}, err
	}
	return s.decodeVersion(objectID, revision, payload)
}

type sealedVersion struct {
	objectID   string
	revision   uint64
	capturedAt string
	payload    []byte
}

// sealVersions serializes and encrypts snapshots before SQLite's single writer
// connection is taken, keeping the commit critical section short.
func (s *Store) sealVersions(versions []VersionRecord) ([]sealedVersion, error) {
	out := make([]sealedVersion, 0, len(versions))
	for _, version := range versions {
		if version.Value.ObjectID != version.ObjectID || version.Value.Revision != version.Revision {
			return nil, fmt.Errorf("version record identity mismatch: %s/%d vs %s/%d", version.ObjectID, version.Revision, version.Value.ObjectID, version.Value.Revision)
		}
		capturedAt := version.Value.CapturedAt
		if capturedAt == "" {
			capturedAt = now()
			version.Value.CapturedAt = capturedAt
		}
		raw, err := json.Marshal(version.Value)
		if err != nil {
			return nil, err
		}
		payload, err := s.encrypt(raw, versionAAD(version.ObjectID, version.Revision))
		if err != nil {
			return nil, err
		}
		out = append(out, sealedVersion{objectID: version.ObjectID, revision: version.Revision, capturedAt: capturedAt, payload: payload})
	}
	return out, nil
}

// putVersions writes all snapshots and prunes the oldest ones for each object
// inside the caller's transaction. Pruning after every insert would do the same
// work twice when one batch carries several snapshots for one object.
func putVersions(tx *sql.Tx, versions []sealedVersion) error {
	objects := map[string]bool{}
	for _, version := range versions {
		if _, err := tx.Exec("INSERT INTO object_versions(object_id,revision,captured_at,payload) VALUES(?,?,?,?) ON CONFLICT(object_id,revision) DO UPDATE SET captured_at=excluded.captured_at,payload=excluded.payload", version.objectID, version.revision, version.capturedAt, version.payload); err != nil {
			return err
		}
		objects[version.objectID] = true
	}
	for objectID := range objects {
		if _, err := tx.Exec(`DELETE FROM object_versions WHERE object_id=? AND revision NOT IN (
			SELECT revision FROM object_versions WHERE object_id=? ORDER BY revision DESC LIMIT ?)`, objectID, objectID, maxObjectVersions); err != nil {
			return err
		}
	}
	return nil
}
