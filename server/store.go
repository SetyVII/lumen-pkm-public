package main

import (
	"crypto/aes"
	"crypto/cipher"
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"sort"
	"strconv"
	"strings"

	_ "github.com/mattn/go-sqlite3"
)

// Schema 6 includes canonical documents and bounded encrypted write receipts. Older
// servers must not open the store and silently discard the new document field.
const schemaVersion = 6

type Store struct {
	db    *sql.DB
	aead  cipher.AEAD
	index *memIndex
}

type Record struct {
	Kind, ID string
	Value    any
}

type BlobRecord struct {
	ID   string
	Data []byte
}

// SyncChange is the durable, monotonic journal used by mobile clients. The
// payload itself stays encrypted; this table only exposes kind/id/sequence so a
// client can ask "what changed after N?" without scanning every record.
type SyncChange struct {
	Seq  int64  `json:"seq"`
	Kind string `json:"kind"`
	ID   string `json:"id"`
	Op   string `json:"op"`
}

// SyncOpRecord is committed in the same transaction as the data it acknowledges.
// A retry after a dropped Wi-Fi response therefore cannot duplicate a photo or
// append the same journal block twice.
type SyncOpRecord struct {
	OpID     string
	DeviceID string
	HeadSeq  int64
}

var syncKinds = map[string]bool{"object": true, "type": true, "tombstone": true, "asset": true}

func syncKind(kind string) bool { return syncKinds[kind] }

func recordSyncChange(tx *sql.Tx, kind, id, op string) error {
	if !syncKind(kind) {
		return nil
	}
	_, err := tx.Exec("INSERT INTO sync_changes(kind,id,op,changed_at) VALUES(?,?,?,?)", kind, id, op, now())
	return err
}

// BlobChunk is one sealed piece of a large attachment. Payload is produced
// by SealBlobChunk with the same "asset/<id>" AAD as a whole blob; Path is an
// alternative staging source whose file holds the sealed payload (used by the
// chunked upload so the assembled bytes never sit in RAM before commit).
type BlobChunk struct {
	AssetID string
	Seq     int
	Payload []byte
	Path    string
}

// SealBlobChunk encrypts one piece of an attachment before it is committed.
// Sealing happens while the part is being received, so the staging file and
// the final database row carry ciphertext only.
func (s *Store) SealBlobChunk(assetID string, data []byte) ([]byte, error) {
	return s.encrypt(data, "asset/"+assetID)
}

// OpenBlobChunk decrypts one stored piece of an attachment.
func (s *Store) OpenBlobChunk(assetID string, payload []byte) ([]byte, error) {
	return s.decrypt(payload, "asset/"+assetID)
}

// BlobChunkCount returns how many pieces of an attachment are stored. Zero
// means the blob is stored whole (or does not exist).
func (s *Store) BlobChunkCount(assetID string) (int, error) {
	var count int
	if err := s.db.QueryRow("SELECT count(*) FROM blob_chunks WHERE asset_id=?", assetID).Scan(&count); err != nil {
		return 0, err
	}
	return count, nil
}

// BlobChunk returns one decrypted piece of an attachment, by sequence.
func (s *Store) BlobChunk(assetID string, seq int) ([]byte, error) {
	var payload []byte
	if err := s.db.QueryRow("SELECT payload FROM blob_chunks WHERE asset_id=? AND seq=?", assetID, seq).Scan(&payload); err != nil {
		return nil, err
	}
	return s.decrypt(payload, "asset/"+assetID)
}

func (s *Store) PutBatch(records []Record, deletes [][2]string) error {
	return s.putBatch(records, deletes, nil, nil)
}

// PutBatchBlobs commits encrypted attachment bytes, metadata, object changes and
// their journal entries together. A failed upload cannot leave a half-linked blob.
func (s *Store) PutBatchBlobs(records []Record, deletes [][2]string, blobs []BlobRecord) error {
	return s.putBatch(records, deletes, blobs, nil)
}

// PutBatchBlobChunks commits records together with the pre-sealed pieces of
// one large attachment. The chunked upload flow uses it so asset metadata,
// the owner object and every piece commit atomically while the plaintext
// never has to be assembled in RAM.
func (s *Store) PutBatchBlobChunks(records []Record, deletes [][2]string, chunks []BlobChunk) error {
	return s.putBatch(records, deletes, nil, chunks)
}

func (s *Store) putBatch(records []Record, deletes [][2]string, blobs []BlobRecord, chunks []BlobChunk) error {
	type sealedRecord struct {
		kind, id string
		payload  []byte
	}
	sealed := make([]sealedRecord, 0, len(records))
	syncOps := make([]SyncOpRecord, 0)
	versions := make([]VersionRecord, 0)
	for _, record := range records {
		if record.Kind == "object" {
			switch value := record.Value.(type) {
			case *Object:
				if err := canonicalizeStoredObject(value); err != nil {
					return err
				}
			case Object:
				if err := canonicalizeStoredObject(&value); err != nil {
					return err
				}
				record.Value = value
			default:
				return errors.New("object record requires Object")
			}
		}
		switch record.Kind {
		case "sync_op":
			if op, ok := record.Value.(SyncOpRecord); ok {
				syncOps = append(syncOps, op)
			}
			continue
		case versionRecordKind:
			// Snapshots live in their own table and must never be written to
			// records or recorded in sync_changes: mobile clients do not sync
			// version history.
			version, ok := record.Value.(VersionRecord)
			if !ok {
				return errors.New("version record requires a VersionRecord value")
			}
			versions = append(versions, version)
			continue
		}
		raw, err := json.Marshal(record.Value)
		if err != nil {
			return err
		}
		payload, err := s.encrypt(raw, record.Kind+"/"+record.ID)
		if err != nil {
			return err
		}
		sealed = append(sealed, sealedRecord{kind: record.Kind, id: record.ID, payload: payload})
	}
	sealedBlobs := make([]sealedRecord, 0, len(blobs))
	for _, blob := range blobs {
		payload, err := s.encrypt(blob.Data, "asset/"+blob.ID)
		if err != nil {
			return err
		}
		sealedBlobs = append(sealedBlobs, sealedRecord{id: blob.ID, payload: payload})
	}
	sealedVersions, err := s.sealVersions(versions)
	if err != nil {
		return err
	}
	// JSON encoding, attachment encryption and snapshot encryption happen
	// before taking SQLite's single writer connection, keeping the commit
	// critical section short.
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	for _, record := range sealed {
		if _, err = tx.Exec("INSERT INTO records(kind,id,payload) VALUES(?,?,?) ON CONFLICT(kind,id) DO UPDATE SET payload=excluded.payload", record.kind, record.id, record.payload); err != nil {
			return err
		}
		if err = recordSyncChange(tx, record.kind, record.id, "put"); err != nil {
			return err
		}
		if err = s.trackReceipt(tx, record.kind, record.id, record.payload); err != nil {
			return err
		}
	}
	for _, record := range deletes {
		if record[0] == "receipts" {
			if _, err = tx.Exec("DELETE FROM records WHERE (kind,id) IN (SELECT kind,id FROM receipt_results WHERE object_id=?)", record[1]); err != nil {
				return err
			}
			if _, err = tx.Exec("DELETE FROM receipt_results WHERE object_id=?", record[1]); err != nil {
				return err
			}
			continue
		}
		if record[0] == "versions" {
			// Version snapshots live in their own table and are server-side
			// only. Permanent purge must delete them with the object; a
			// recreated fixed-id journal must not inherit old history.
			if _, err = tx.Exec("DELETE FROM object_versions WHERE object_id=?", record[1]); err != nil {
				return err
			}
			continue
		}
		if record[0] == "blob" {
			if _, err = tx.Exec("DELETE FROM blobs WHERE id=?", record[1]); err != nil {
				return err
			}
			// A chunked attachment has no whole-blob row; its pieces live in
			// blob_chunks. Both shapes disappear together.
			if _, err = tx.Exec("DELETE FROM blob_chunks WHERE asset_id=?", record[1]); err != nil {
				return err
			}
			continue
		}
		if _, err = tx.Exec("DELETE FROM records WHERE kind=? AND id=?", record[0], record[1]); err != nil {
			return err
		}
		if err = recordSyncChange(tx, record[0], record[1], "delete"); err != nil {
			return err
		}
	}
	for _, blob := range sealedBlobs {
		if _, err = tx.Exec("INSERT INTO blobs(id,payload) VALUES(?,?)", blob.id, blob.payload); err != nil {
			return err
		}
	}
	for _, chunk := range chunks {
		payload := chunk.Payload
		if len(payload) == 0 && chunk.Path != "" {
			// Sealed staging file: read it right before the insert so at most
			// one part is in RAM at a time.
			payload, err = os.ReadFile(chunk.Path)
			if err != nil {
				return err
			}
		}
		if _, err = tx.Exec("INSERT INTO blob_chunks(asset_id,seq,payload) VALUES(?,?,?)", chunk.AssetID, chunk.Seq, payload); err != nil {
			return err
		}
	}
	if err = s.pruneReceiptResults(tx); err != nil {
		return err
	}
	if err = putVersions(tx, sealedVersions); err != nil {
		return err
	}
	for _, op := range syncOps {
		if _, err = tx.Exec("INSERT OR IGNORE INTO sync_ops(op_id,device_id,applied_at,head_seq) VALUES(?,?,?,?)", op.OpID, op.DeviceID, now(), op.HeadSeq); err != nil {
			return err
		}
	}
	if err = tx.Commit(); err != nil {
		return err
	}
	s.applyIndex(records, deletes)
	return nil
}

func newID() string {
	b := make([]byte, 16)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	b[6] = (b[6] & 0x0f) | 0x40
	b[8] = (b[8] & 0x3f) | 0x80
	h := hex.EncodeToString(b)
	return h[:8] + "-" + h[8:12] + "-" + h[12:16] + "-" + h[16:20] + "-" + h[20:]
}

func OpenStore(path string, key []byte) (*Store, error) {
	if len(key) != 32 {
		return nil, errors.New("encryption key must be exactly 32 bytes")
	}
	if err := os.MkdirAll(filepath.Dir(path), 0700); err != nil {
		return nil, err
	}
	block, err := aes.NewCipher(key)
	if err != nil {
		return nil, err
	}
	aead, err := cipher.NewGCM(block)
	if err != nil {
		return nil, err
	}
	db, err := sql.Open("sqlite3", path+"?_busy_timeout=5000&_journal_mode=WAL&_synchronous=FULL&_foreign_keys=on&_txlock=immediate")
	if err != nil {
		return nil, err
	}
	db.SetMaxOpenConns(1)
	s := &Store{db: db, aead: aead, index: newMemIndex()}
	_, err = db.Exec(`CREATE TABLE IF NOT EXISTS records(kind TEXT NOT NULL,id TEXT NOT NULL,payload BLOB NOT NULL,PRIMARY KEY(kind,id));
		CREATE TABLE IF NOT EXISTS blobs(id TEXT PRIMARY KEY,payload BLOB NOT NULL);
		CREATE TABLE IF NOT EXISTS blob_chunks(asset_id TEXT NOT NULL,seq INTEGER NOT NULL,payload BLOB NOT NULL,PRIMARY KEY(asset_id,seq));
		CREATE TABLE IF NOT EXISTS sync_changes(seq INTEGER PRIMARY KEY AUTOINCREMENT,kind TEXT NOT NULL,id TEXT NOT NULL,op TEXT NOT NULL,changed_at TEXT NOT NULL);
		CREATE INDEX IF NOT EXISTS sync_changes_lookup ON sync_changes(kind,id,seq);
		CREATE TABLE IF NOT EXISTS receipt_results(kind TEXT NOT NULL,id TEXT NOT NULL,object_id TEXT NOT NULL,created_at TEXT NOT NULL,expires_at TEXT NOT NULL,result_bytes INTEGER NOT NULL,PRIMARY KEY(kind,id));
 CREATE INDEX IF NOT EXISTS receipt_results_age ON receipt_results(created_at);
 CREATE TABLE IF NOT EXISTS sync_ops(op_id TEXT PRIMARY KEY,device_id TEXT NOT NULL,applied_at TEXT NOT NULL,head_seq INTEGER NOT NULL);
		CREATE TABLE IF NOT EXISTS object_versions(object_id TEXT NOT NULL,revision INTEGER NOT NULL,captured_at TEXT NOT NULL,payload BLOB NOT NULL,PRIMARY KEY(object_id,revision));
		CREATE TABLE IF NOT EXISTS meta(key TEXT PRIMARY KEY,value TEXT NOT NULL);`)
	if err != nil {
		db.Close()
		return nil, err
	}
	// Authenticate the key before serving. A different key must never silently open an empty workspace.
	var payload []byte
	err = db.QueryRow("SELECT payload FROM records WHERE kind='system' AND id='key-check'").Scan(&payload)
	if errors.Is(err, sql.ErrNoRows) {
		err = s.Put("system", "key-check", map[string]string{"value": "lumen-v1"})
	} else if err == nil {
		_, err = s.decrypt(payload, "system/key-check")
	}
	if err != nil {
		db.Close()
		return nil, fmt.Errorf("cannot authenticate workspace key: %w", err)
	}
	if err = s.checkSchemaVersion(); err != nil {
		db.Close()
		return nil, err
	}
	// Baseline first: a legacy workspace may have an empty sync_changes table.
	// The versioned migrations then append only the changes they really make.
	if err = s.ensureSyncBaseline(); err != nil {
		db.Close()
		return nil, err
	}
	if err = s.migrate(); err != nil {
		db.Close()
		return nil, err
	}
	if err = s.rebuildIndex(); err != nil {
		db.Close()
		return nil, err
	}
	if err = os.Chmod(path, 0600); err != nil {
		db.Close()
		return nil, err
	}
	return s, nil
}

func (s *Store) checkSchemaVersion() error {
	var raw string
	err := s.db.QueryRow("SELECT value FROM meta WHERE key='schema_version'").Scan(&raw)
	if errors.Is(err, sql.ErrNoRows) {
		return nil
	}
	if err != nil {
		return err
	}
	version, err := strconv.Atoi(raw)
	if err != nil {
		return fmt.Errorf("invalid schema_version %q: %w", raw, err)
	}
	if version > schemaVersion {
		return fmt.Errorf("workspace schema version %d is newer than supported %d", version, schemaVersion)
	}
	return nil
}

func (s *Store) migrate() error {
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	version := 0
	var raw string
	err = tx.QueryRow("SELECT value FROM meta WHERE key='schema_version'").Scan(&raw)
	if err == nil {
		version, err = strconv.Atoi(raw)
		if err != nil {
			return fmt.Errorf("invalid schema_version %q: %w", raw, err)
		}
	} else if !errors.Is(err, sql.ErrNoRows) {
		return err
	}
	if version > schemaVersion {
		return fmt.Errorf("workspace schema version %d is newer than supported %d", version, schemaVersion)
	}
	if version < 1 {
		if err = s.migrateLegacyTarea(tx); err != nil {
			return err
		}
	}
	if version < 2 {
		if err = s.migrateV2(tx); err != nil {
			return err
		}
	}
	// Version 3 adds the "trash" record kind. The generic records table needs no
	// DDL; the bump only stops a pre-trash binary from opening a workspace whose
	// trash payloads it would silently ignore. Version 4 adds the blob_chunks
	// side table for chunked uploads (created by the OpenStore DDL).
	if _, err = tx.Exec("INSERT INTO meta(key,value) VALUES('schema_version',?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", strconv.Itoa(schemaVersion)); err != nil {
		return err
	}
	return tx.Commit()
}

// migrateLegacyTarea moves the old ad-hoc "Tarea" type migration into the
// versioned migration runner. It is intentionally a no-op on current data.
func (s *Store) migrateLegacyTarea(tx *sql.Tx) error {
	legacy := map[string]bool{}
	rows, err := tx.Query("SELECT id,payload FROM records WHERE kind='type'")
	if err != nil {
		return err
	}
	for rows.Next() {
		var id string
		var payload []byte
		if err = rows.Scan(&id, &payload); err != nil {
			rows.Close()
			return err
		}
		raw, err := s.decrypt(payload, "type/"+id)
		if err != nil {
			rows.Close()
			return err
		}
		var typ ObjectType
		if json.Unmarshal(raw, &typ) != nil {
			continue
		}
		if typ.ID != "task" && strings.EqualFold(strings.TrimSpace(typ.Name), "Tarea") {
			legacy[id] = true
		}
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return err
	}
	rows.Close()
	if len(legacy) == 0 {
		return nil
	}
	rows, err = tx.Query("SELECT id,payload FROM records WHERE kind='object'")
	if err != nil {
		return err
	}
	for rows.Next() {
		var id string
		var payload []byte
		if err = rows.Scan(&id, &payload); err != nil {
			rows.Close()
			return err
		}
		raw, err := s.decrypt(payload, "object/"+id)
		if err != nil {
			rows.Close()
			return err
		}
		var o Object
		if json.Unmarshal(raw, &o) != nil || !legacy[o.TypeID] {
			continue
		}
		o.TypeID = "task"
		if o.Properties == nil {
			o.Properties = map[string]any{}
		}
		if _, ok := o.Properties["status"]; !ok {
			o.Properties["status"] = "todo"
		}
		raw, err = json.Marshal(o)
		if err != nil {
			rows.Close()
			return err
		}
		ciphertext, err := s.encrypt(raw, "object/"+id)
		if err != nil {
			rows.Close()
			return err
		}
		if _, err = tx.Exec("UPDATE records SET payload=? WHERE kind='object' AND id=?", ciphertext, id); err != nil {
			rows.Close()
			return err
		}
		if err = recordSyncChange(tx, "object", id, "put"); err != nil {
			rows.Close()
			return err
		}
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return err
	}
	rows.Close()
	for id := range legacy {
		if _, err = tx.Exec("DELETE FROM records WHERE kind='type' AND id=?", id); err != nil {
			return err
		}
		if err = recordSyncChange(tx, "type", id, "delete"); err != nil {
			return err
		}
	}
	return nil
}

// migrateV2 removes the Logseq outbox and the exporter state that used to live
// inside the encrypted document. It deliberately does not append sync_changes:
// these are not user edits and mobile clients must not see false changes.
func (s *Store) migrateV2(tx *sql.Tx) error {
	if _, err := tx.Exec("DELETE FROM records WHERE kind='outbox'"); err != nil {
		return err
	}
	rows, err := tx.Query("SELECT id,payload FROM records WHERE kind='object'")
	if err != nil {
		return err
	}
	type rewrite struct {
		id      string
		payload []byte
	}
	var rewrites []rewrite
	for rows.Next() {
		var id string
		var payload []byte
		if err = rows.Scan(&id, &payload); err != nil {
			rows.Close()
			return err
		}
		raw, err := s.decrypt(payload, "object/"+id)
		if err != nil {
			rows.Close()
			return err
		}
		var o Object
		if json.Unmarshal(raw, &o) != nil {
			continue
		}
		if o.SyncStatus == "synced" && o.SyncError == "" {
			continue
		}
		o.SyncStatus, o.SyncError = "synced", ""
		raw, err = json.Marshal(o)
		if err != nil {
			rows.Close()
			return err
		}
		ciphertext, err := s.encrypt(raw, "object/"+id)
		if err != nil {
			rows.Close()
			return err
		}
		rewrites = append(rewrites, rewrite{id: id, payload: ciphertext})
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return err
	}
	rows.Close()
	for _, item := range rewrites {
		if _, err = tx.Exec("UPDATE records SET payload=? WHERE kind='object' AND id=?", item.payload, item.id); err != nil {
			return err
		}
	}
	// Assets used to embed a full copy of their media object. Keep the stable
	// media_object_id and drop the stale snapshot.
	rows, err = tx.Query("SELECT id,payload FROM records WHERE kind='asset'")
	if err != nil {
		return err
	}
	rewrites = nil
	for rows.Next() {
		var id string
		var payload []byte
		if err = rows.Scan(&id, &payload); err != nil {
			rows.Close()
			return err
		}
		raw, err := s.decrypt(payload, "asset/"+id)
		if err != nil {
			rows.Close()
			return err
		}
		var asset Asset
		if json.Unmarshal(raw, &asset) != nil || asset.MediaObject == nil {
			continue
		}
		asset.MediaObject = nil
		raw, err = json.Marshal(asset)
		if err != nil {
			rows.Close()
			return err
		}
		ciphertext, err := s.encrypt(raw, "asset/"+id)
		if err != nil {
			rows.Close()
			return err
		}
		rewrites = append(rewrites, rewrite{id: id, payload: ciphertext})
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return err
	}
	rows.Close()
	for _, item := range rewrites {
		if _, err = tx.Exec("UPDATE records SET payload=? WHERE kind='asset' AND id=?", item.payload, item.id); err != nil {
			return err
		}
	}
	return nil
}

func (s *Store) applyIndex(records []Record, deletes [][2]string) {
	if s.index == nil {
		return
	}
	for _, record := range deletes {
		switch record[0] {
		case "object":
			s.index.deleteObject(record[1])
		case "type":
			s.index.deleteType(record[1])
		}
	}
	for _, record := range records {
		if record.Kind == "tombstone" {
			s.index.deleteObject(record.ID)
		}
	}
	for _, record := range records {
		switch value := record.Value.(type) {
		case Object:
			if record.Kind == "object" {
				s.index.put(value)
			}
		case *Object:
			if record.Kind == "object" && value != nil {
				s.index.put(*value)
			}
		case ObjectType:
			if record.Kind == "type" {
				s.index.putType(value)
			}
		case *ObjectType:
			if record.Kind == "type" && value != nil {
				s.index.putType(*value)
			}
		}
	}
}

// ensureSyncBaseline gives pre-existing workspaces a starting sequence. It is a
// one-time migration: new writes append their own change rows transactionally.
func (s *Store) ensureSyncBaseline() error {
	// Once the schema is current, the baseline is complete. On a legacy or new
	// workspace, fill only the records that do not have a journal entry yet.
	var version string
	if err := s.db.QueryRow("SELECT value FROM meta WHERE key='schema_version'").Scan(&version); err == nil && version == strconv.Itoa(schemaVersion) {
		return nil
	}
	rows, err := s.db.Query("SELECT kind,id FROM records WHERE kind IN ('object','type','tombstone','asset') ORDER BY kind,id")
	if err != nil {
		return err
	}
	defer rows.Close()
	type pair struct{ kind, id string }
	var pairs []pair
	for rows.Next() {
		var p pair
		if err := rows.Scan(&p.kind, &p.id); err != nil {
			return err
		}
		pairs = append(pairs, p)
	}
	if err := rows.Err(); err != nil {
		return err
	}
	if len(pairs) == 0 {
		return nil
	}
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	for _, p := range pairs {
		var exists int
		err := tx.QueryRow("SELECT 1 FROM sync_changes WHERE kind=? AND id=? LIMIT 1", p.kind, p.id).Scan(&exists)
		if errors.Is(err, sql.ErrNoRows) {
			if err = recordSyncChange(tx, p.kind, p.id, "put"); err != nil {
				return err
			}
		} else if err != nil {
			return err
		}
	}
	return tx.Commit()
}

func (s *Store) encrypt(data []byte, aad string) ([]byte, error) {
	nonce := make([]byte, s.aead.NonceSize())
	if _, err := rand.Read(nonce); err != nil {
		return nil, err
	}
	return s.aead.Seal(nonce, nonce, data, []byte(aad)), nil
}
func (s *Store) decrypt(data []byte, aad string) ([]byte, error) {
	n := s.aead.NonceSize()
	if len(data) < n {
		return nil, errors.New("invalid encrypted record")
	}
	return s.aead.Open(nil, data[:n], data[n:], []byte(aad))
}
func (s *Store) Put(kind, id string, value any) error {
	return s.PutBatch([]Record{{Kind: kind, ID: id, Value: value}}, nil)
}
func (s *Store) Get(kind, id string, out any) error {
	var data []byte
	if err := s.db.QueryRow("SELECT payload FROM records WHERE kind=? AND id=?", kind, id).Scan(&data); err != nil {
		return err
	}
	raw, err := s.decrypt(data, kind+"/"+id)
	if err != nil {
		return err
	}
	return json.Unmarshal(raw, out)
}
func (s *Store) List(kind string) ([][]byte, error) {
	rows, err := s.db.Query("SELECT id,payload FROM records WHERE kind=? ORDER BY id", kind)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := [][]byte{}
	for rows.Next() {
		var id string
		var data []byte
		if err = rows.Scan(&id, &data); err != nil {
			return nil, err
		}
		raw, e := s.decrypt(data, kind+"/"+id)
		if e != nil {
			return nil, e
		}
		out = append(out, raw)
	}
	return out, rows.Err()
}

func (s *Store) Count(kind string) (int, error) {
	var count int
	err := s.db.QueryRow("SELECT count(*) FROM records WHERE kind=?", kind).Scan(&count)
	return count, err
}
func (s *Store) Delete(kind, id string) error {
	tx, err := s.db.Begin()
	if err != nil {
		return err
	}
	defer tx.Rollback()
	if _, err = tx.Exec("DELETE FROM records WHERE kind=? AND id=?", kind, id); err != nil {
		return err
	}
	if err = recordSyncChange(tx, kind, id, "delete"); err != nil {
		return err
	}
	if err = tx.Commit(); err != nil {
		return err
	}
	s.applyIndex(nil, [][2]string{{kind, id}})
	return nil
}

// Objects decrypts the full payloads on demand. The in-memory index only
// contains metadata, so a large library does not pin all text in RAM.
func (s *Store) Objects() ([]Object, error) {
	rows, err := s.List("object")
	if err != nil {
		return nil, err
	}
	out := make([]Object, 0, len(rows))
	for _, raw := range rows {
		var o Object
		if err = json.Unmarshal(raw, &o); err != nil {
			return nil, err
		}
		out = append(out, o)
	}
	sort.SliceStable(out, func(i, j int) bool { return objectLess(out[i], out[j]) })
	return out, nil
}

func (s *Store) ObjectsByType(typeID string) ([]Object, error) {
	ids := s.index.idsByType(typeID)
	out := make([]Object, 0, len(ids))
	for _, id := range ids {
		o, err := s.Object(id)
		if err != nil {
			return nil, err
		}
		out = append(out, o)
	}
	return out, nil
}

func (s *Store) ObjectCount() int {
	var count int
	if err := s.db.QueryRow("SELECT count(*) FROM records WHERE kind='object'").Scan(&count); err != nil {
		return 0
	}
	return count
}

func (s *Store) Object(id string) (Object, error) {
	var o Object
	if err := s.Get("object", id, &o); err != nil {
		return Object{}, err
	}
	if o.ID != id {
		return Object{}, fmt.Errorf("object/%s contains id %q", id, o.ID)
	}
	return o, nil
}

// Has reports whether a record exists without decrypting its payload. It is
// used for existence checks on hot paths such as link validation.
func (s *Store) Has(kind, id string) bool {
	var one int
	return s.db.QueryRow("SELECT 1 FROM records WHERE kind=? AND id=?", kind, id).Scan(&one) == nil
}

// TrashedObject reads one encrypted trash payload.
func (s *Store) TrashedObject(id string) (trashedObject, error) {
	var t trashedObject
	err := s.Get("trash", id, &t)
	return t, err
}

// TrashedObjects decrypts every trash payload. Trash is expected to be small;
// payloads are never kept in the in-memory index.
func (s *Store) TrashedObjects() ([]trashedObject, error) {
	rows, err := s.List("trash")
	if err != nil {
		return nil, err
	}
	out := make([]trashedObject, 0, len(rows))
	for _, raw := range rows {
		var t trashedObject
		if err = json.Unmarshal(raw, &t); err != nil {
			return nil, err
		}
		out = append(out, t)
	}
	return out, nil
}

// Assets decrypts every attachment metadata record.
func (s *Store) Assets() ([]Asset, error) {
	rows, err := s.List("asset")
	if err != nil {
		return nil, err
	}
	out := make([]Asset, 0, len(rows))
	for _, raw := range rows {
		var asset Asset
		if err = json.Unmarshal(raw, &asset); err != nil {
			return nil, err
		}
		out = append(out, asset)
	}
	return out, nil
}

func (s *Store) Types() []ObjectType { return s.index.listTypes() }

func (s *Store) Type(id string) (ObjectType, error) {
	t, ok := s.index.typeByID(id)
	if !ok {
		return ObjectType{}, sql.ErrNoRows
	}
	return t, nil
}

func (s *Store) Backlinks(id string) ([]Object, error) {
	return s.BacklinksFiltered(id, true)
}

// BacklinksFiltered excludes template sources by default so a template that
// references a tag or person does not pollute that object's normal backlinks.
func (s *Store) BacklinksFiltered(id string, includeTemplates bool) ([]Object, error) {
	ids := s.index.backlinkIDsFiltered(id, includeTemplates)
	out := make([]Object, 0, len(ids))
	for _, source := range ids {
		o, err := s.Object(source)
		if err != nil {
			return nil, err
		}
		out = append(out, o)
	}
	return out, nil
}

func (s *Store) BacklinkIDs(id string) []string { return s.index.backlinkIDs(id) }
func (s *Store) TitleOwner(title string) string { return s.index.titleOwner(title) }
func (s *Store) TitleExists(title string) bool  { return s.index.titleExists(title) }

func (s *Store) HeadSeq() (int64, error) {
	var seq int64
	err := s.db.QueryRow("SELECT COALESCE(MAX(seq),0) FROM sync_changes").Scan(&seq)
	return seq, err
}

// ChangesSince collapses multiple writes to the same record and returns the
// latest operation for each. Tombstones and assets are included so a client can
// reconcile deletions and media metadata without a full snapshot.
func (s *Store) ChangesSince(seq int64) ([]SyncChange, error) {
	rows, err := s.db.Query(`SELECT c.seq,c.kind,c.id,c.op FROM sync_changes c
		JOIN (SELECT kind,id,MAX(seq) AS seq FROM sync_changes
			WHERE seq>? AND kind IN ('object','type','tombstone','asset')
			GROUP BY kind,id) latest ON latest.seq=c.seq
		ORDER BY c.seq`, seq)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	out := []SyncChange{}
	for rows.Next() {
		var c SyncChange
		if err := rows.Scan(&c.Seq, &c.Kind, &c.ID, &c.Op); err != nil {
			return nil, err
		}
		out = append(out, c)
	}
	return out, rows.Err()
}

func (s *Store) SyncOpApplied(opID string) (bool, error) {
	var exists int
	err := s.db.QueryRow("SELECT 1 FROM sync_ops WHERE op_id=?", opID).Scan(&exists)
	if errors.Is(err, sql.ErrNoRows) {
		return false, nil
	}
	return err == nil, err
}

func (s *Store) PutSyncOp(opID, deviceID string, headSeq int64) error {
	_, err := s.db.Exec("INSERT OR IGNORE INTO sync_ops(op_id,device_id,applied_at,head_seq) VALUES(?,?,?,?)", opID, deviceID, now(), headSeq)
	return err
}
func (s *Store) PutBlob(id string, data []byte) error {
	encrypted, err := s.encrypt(data, "asset/"+id)
	if err != nil {
		return err
	}
	_, err = s.db.Exec("INSERT INTO blobs(id,payload) VALUES(?,?)", id, encrypted)
	return err
}
func (s *Store) Blob(id string) ([]byte, error) {
	var data []byte
	err := s.db.QueryRow("SELECT payload FROM blobs WHERE id=?", id).Scan(&data)
	if errors.Is(err, sql.ErrNoRows) {
		// Large attachments are stored as sealed pieces instead of one row.
		return s.assembleChunkedBlob(id)
	}
	if err != nil {
		return nil, err
	}
	return s.decrypt(data, "asset/"+id)
}

// assembleChunkedBlob decrypts and concatenates the pieces of a chunked
// attachment in sequence order.
func (s *Store) assembleChunkedBlob(id string) ([]byte, error) {
	rows, err := s.db.Query("SELECT payload FROM blob_chunks WHERE asset_id=? ORDER BY seq", id)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	var out []byte
	for rows.Next() {
		var payload []byte
		if err = rows.Scan(&payload); err != nil {
			return nil, err
		}
		plain, err := s.decrypt(payload, "asset/"+id)
		if err != nil {
			return nil, err
		}
		out = append(out, plain...)
	}
	if err = rows.Err(); err != nil {
		return nil, err
	}
	if out == nil {
		return nil, sql.ErrNoRows
	}
	return out, nil
}
func (s *Store) Close() error { return s.db.Close() }

// Fsck verifies the durable invariants and, when the in-memory index exists,
// that it matches the encrypted rows. It intentionally decrypts every record:
// this is a maintenance command, not a request path.
func (s *Store) Fsck() error {
	objects := map[string]Object{}
	tombstones := map[string]tombstone{}
	trashed := map[string]trashedObject{}
	assets := map[string]bool{}
	types := map[string]ObjectType{}

	rows, err := s.db.Query("SELECT kind,id,payload FROM records ORDER BY kind,id")
	if err != nil {
		return err
	}
	for rows.Next() {
		var kind, id string
		var payload []byte
		if err = rows.Scan(&kind, &id, &payload); err != nil {
			rows.Close()
			return err
		}
		raw, err := s.decrypt(payload, kind+"/"+id)
		if err != nil {
			rows.Close()
			return fmt.Errorf("%s/%s: %w", kind, id, err)
		}
		switch kind {
		case "object":
			var o Object
			if err = json.Unmarshal(raw, &o); err != nil {
				rows.Close()
				return fmt.Errorf("object/%s: %w", id, err)
			}
			if o.ID != id {
				rows.Close()
				return fmt.Errorf("object/%s contains id %q", id, o.ID)
			}
			objects[id] = o
		case "type":
			var t ObjectType
			if err = json.Unmarshal(raw, &t); err != nil {
				rows.Close()
				return fmt.Errorf("type/%s: %w", id, err)
			}
			if t.ID != id {
				rows.Close()
				return fmt.Errorf("type/%s contains id %q", id, t.ID)
			}
			types[id] = t
		case "tombstone":
			var t tombstone
			if err = json.Unmarshal(raw, &t); err != nil {
				rows.Close()
				return fmt.Errorf("tombstone/%s: %w", id, err)
			}
			if t.ID != id {
				rows.Close()
				return fmt.Errorf("tombstone/%s contains id %q", id, t.ID)
			}
			tombstones[id] = t
		case "trash":
			var t trashedObject
			if err = json.Unmarshal(raw, &t); err != nil {
				rows.Close()
				return fmt.Errorf("trash/%s: %w", id, err)
			}
			if t.Object.ID != id {
				rows.Close()
				return fmt.Errorf("trash/%s contains id %q", id, t.Object.ID)
			}
			if t.DeletedAt == "" {
				rows.Close()
				return fmt.Errorf("trash/%s has no deleted_at", id)
			}
			trashed[id] = t
		case "asset":
			var a Asset
			if err = json.Unmarshal(raw, &a); err != nil {
				rows.Close()
				return fmt.Errorf("asset/%s: %w", id, err)
			}
			if a.ID != id {
				rows.Close()
				return fmt.Errorf("asset/%s contains id %q", id, a.ID)
			}
			assets[id] = true
		case "outbox":
			rows.Close()
			return fmt.Errorf("outbox/%s must not exist after the Logseq removal", id)
		case "system":
			// key-check and server-id are opaque metadata.
		case "document_receipt":
			var receipt DocumentReceipt
			if err := json.Unmarshal(raw, &receipt); err != nil || len(receipt.Digest) != 64 || receipt.Object.ID == "" || receipt.Object.Revision == 0 {
				rows.Close()
				return fmt.Errorf("invalid document receipt %s", id)
			}
		case "sync_receipt":
			var receipt storedSyncReceipt
			if err := json.Unmarshal(raw, &receipt); err != nil || len(receipt.Digest) != 64 || receipt.Receipt.OpID != id {
				rows.Close()
				return fmt.Errorf("invalid sync receipt %s", id)
			}
		default:
			rows.Close()
			return fmt.Errorf("unknown record kind %q", kind)
		}
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return err
	}
	rows.Close()

	blobs := map[string]bool{}
	rows, err = s.db.Query("SELECT id,payload FROM blobs ORDER BY id")
	if err != nil {
		return err
	}
	for rows.Next() {
		var id string
		var payload []byte
		if err = rows.Scan(&id, &payload); err != nil {
			rows.Close()
			return err
		}
		if _, err = s.decrypt(payload, "asset/"+id); err != nil {
			rows.Close()
			return fmt.Errorf("blob/%s: %w", id, err)
		}
		blobs[id] = true
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return err
	}
	rows.Close()

	// Chunked attachments store sealed pieces in blob_chunks. Every piece
	// must decrypt with its asset AAD and belong to a known asset.
	chunked := map[string]bool{}
	rows, err = s.db.Query("SELECT asset_id,payload FROM blob_chunks ORDER BY asset_id,seq")
	if err != nil {
		return err
	}
	for rows.Next() {
		var assetID string
		var payload []byte
		if err = rows.Scan(&assetID, &payload); err != nil {
			rows.Close()
			return err
		}
		if _, err = s.decrypt(payload, "asset/"+assetID); err != nil {
			rows.Close()
			return fmt.Errorf("blob_chunk/%s: %w", assetID, err)
		}
		chunked[assetID] = true
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return err
	}
	rows.Close()

	for id := range assets {
		if !blobs[id] && !chunked[id] {
			return fmt.Errorf("asset/%s has no blob", id)
		}
	}
	for id := range blobs {
		if !assets[id] {
			return fmt.Errorf("blob/%s has no asset", id)
		}
	}
	for id := range chunked {
		if !assets[id] {
			return fmt.Errorf("blob_chunks/%s has no asset", id)
		}
	}
	versionsByObject := map[string]int{}
	rows, err = s.db.Query("SELECT object_id,revision,captured_at,payload FROM object_versions ORDER BY object_id,revision")
	if err != nil {
		return err
	}
	for rows.Next() {
		var objectID, capturedAt string
		var revision int64
		var payload []byte
		if err = rows.Scan(&objectID, &revision, &capturedAt, &payload); err != nil {
			rows.Close()
			return err
		}
		version, err := s.decodeVersion(objectID, uint64(revision), payload)
		if err != nil {
			rows.Close()
			return fmt.Errorf("version/%s/%d: %w", objectID, revision, err)
		}
		if version.CapturedAt != capturedAt {
			rows.Close()
			return fmt.Errorf("version/%s/%d captured_at does not match its payload", objectID, revision)
		}
		versionsByObject[objectID]++
	}
	if err = rows.Err(); err != nil {
		rows.Close()
		return err
	}
	rows.Close()
	for objectID, count := range versionsByObject {
		if count > maxObjectVersions {
			return fmt.Errorf("object/%s has %d versions (cap %d)", objectID, count, maxObjectVersions)
		}
	}
	for id := range objects {
		var changes int
		if err = s.db.QueryRow("SELECT count(*) FROM sync_changes WHERE kind='object' AND id=?", id).Scan(&changes); err != nil {
			return err
		}
		if changes == 0 {
			return fmt.Errorf("object/%s has no sync_changes entry", id)
		}
	}
	for id, o := range objects {
		if o.Document == nil {
			return fmt.Errorf("object/%s has no canonical document", id)
		}
		expected := cloneObject(o)
		if err := canonicalizeStoredObject(&expected); err != nil {
			return fmt.Errorf("object/%s document: %w", id, err)
		}
		if expected.Content != o.Content || !slices.Equal(expected.DocumentLinks, o.DocumentLinks) {
			return fmt.Errorf("object/%s document projections differ from canonical content", id)
		}
		// Valid object identities may precede their targets during offline sync.
		// Check the relation/index shape without treating arrival order as damage.
		for _, target := range append(append([]string{}, o.Links...), o.PropertyLinks...) {
			if target == id {
				return fmt.Errorf("object/%s links to itself", id)
			}
			if !validObjectReferenceID(target) {
				return fmt.Errorf("object/%s links to invalid object identity %q", id, target)
			}
		}
	}
	for id, t := range trashed {
		if _, live := objects[id]; live {
			return fmt.Errorf("trash/%s is also a live object", id)
		}
		marker, ok := tombstones[id]
		if !ok {
			return fmt.Errorf("trash/%s has no tombstone", id)
		}
		if marker.Revision <= t.Object.Revision {
			return fmt.Errorf("trash/%s tombstone revision %d is not newer than object revision %d", id, marker.Revision, t.Object.Revision)
		}
	}
	if s.index != nil {
		for id, o := range objects {
			indexed, ok := s.index.getMeta(id)
			if !ok || !reflect.DeepEqual(indexed, metaFromObject(o)) {
				return fmt.Errorf("memIndex differs from disk for object %s", id)
			}
		}
		for id, t := range types {
			indexed, ok := s.index.typeByID(id)
			if !ok || !reflect.DeepEqual(indexed, t) {
				return fmt.Errorf("memIndex differs from disk for type %s", id)
			}
		}
		for id, o := range objects {
			for _, target := range allObjectLinks(o) {
				if _, ok := s.index.backlinkSets[target][id]; !ok {
					return fmt.Errorf("memIndex is missing backlink %s -> %s", id, target)
				}
			}
		}
		for target, sources := range s.index.backlinkSets {
			for source := range sources {
				o, ok := objects[source]
				if !ok || !containsString(allObjectLinks(o), target) {
					return fmt.Errorf("memIndex has stale backlink %s -> %s", source, target)
				}
			}
		}
	}
	return nil
}

func containsString(values []string, target string) bool {
	for _, value := range values {
		if value == target {
			return true
		}
	}
	return false
}
