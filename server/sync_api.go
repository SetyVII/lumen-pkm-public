package main

import (
	"context"
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"encoding/json"

	"encoding/base64"
	"errors"
	"fmt"
	"lumen/server/internal/document"
	"net/http"
	"reflect"
	"strings"
	"time"
)

const syncProtocolVersion = "lumen-sync/2"

type syncStatusResponse struct {
	ServerID       string `json:"server_id"`
	HeadSeq        int64  `json:"head_seq"`
	PendingLogseq  int    `json:"pending_logseq"`
	Protocol       string `json:"protocol"`
	TLSFingerprint string `json:"tls_fingerprint,omitempty"`
}

type syncPullRequest struct {
	SinceSeq       int64  `json:"since_seq"`
	MediaSinceDate string `json:"media_since_date"`
}

type syncDeleted struct {
	Kind string `json:"kind"`
	ID   string `json:"id"`
	Seq  int64  `json:"seq"`
}

type syncPullResponse struct {
	HeadSeq    int64         `json:"head_seq"`
	NextSeq    int64         `json:"next_seq"`
	HasMore    bool          `json:"has_more"`
	Objects    []Object      `json:"objects"`
	Types      []ObjectType  `json:"types"`
	Tombstones []tombstone   `json:"tombstones"`
	Assets     []Asset       `json:"assets"`
	Deleted    []syncDeleted `json:"deleted"`
}

type syncAsset struct {
	ID                string `json:"id"`
	ObjectID          string `json:"object_id"`
	Name              string `json:"name"`
	MIME              string `json:"mime"`
	CreatedAt         string `json:"created_at"`
	DataBase64        string `json:"data_base64"`
	CreateMediaObject bool   `json:"create_media_object"`
}

type syncOperation struct {
	OpID         string     `json:"op_id"`
	Action       string     `json:"action"`
	ObjectID     string     `json:"object_id,omitempty"`
	BaseRevision uint64     `json:"base_revision,omitempty"`
	Append       bool       `json:"append,omitempty"`
	ClientTime   string     `json:"client_time,omitempty"`
	Object       *Object    `json:"object,omitempty"`
	Asset        *syncAsset `json:"asset,omitempty"`
}

type syncPushRequest struct {
	DeviceID   string          `json:"device_id"`
	BaseSeq    int64           `json:"base_seq"`
	Operations []syncOperation `json:"operations"`
}

type syncConflict struct {
	OpID       string `json:"op_id"`
	ObjectID   string `json:"object_id"`
	Resolution string `json:"resolution"`
}

type syncOpError struct {
	OpID  string `json:"op_id"`
	Error string `json:"error"`
}

type syncPushReceipt struct {
	ReferenceIssues       []referenceTypeIssue `json:"reference_issues,omitempty"`
	ReferenceIssuesCursor string               `json:"reference_issues_cursor,omitempty"`
	OpID                  string               `json:"op_id"`
	ObjectID              string               `json:"object_id"`
	Revision              uint64               `json:"revision"`
	PreviousRevision      uint64               `json:"previous_revision"`
	Object                *Object              `json:"object,omitempty"`
	Digest                string               `json:"-"`
}
type storedSyncReceipt struct {
	Receipt syncPushReceipt `json:"receipt"`
	Digest  string          `json:"digest"`
}
type syncPushResponse struct {
	Receipts  []syncPushReceipt `json:"receipts,omitempty"`
	HeadSeq   int64             `json:"head_seq"`
	Applied   []string          `json:"applied"`
	Conflicts []syncConflict    `json:"conflicts,omitempty"`
	Errors    []syncOpError     `json:"errors,omitempty"`
}

func (a *App) syncStatus(w http.ResponseWriter, r *http.Request) {
	head, err := a.store.HeadSeq()
	if err != nil {
		jsonError(w, 500, "Could not read sync sequence")
		return
	}
	jsonResponse(w, 200, syncStatusResponse{
		ServerID:       a.serverID,
		HeadSeq:        head,
		PendingLogseq:  0,
		Protocol:       syncProtocolVersion,
		TLSFingerprint: a.tlsFingerprint,
	})
}

func (a *App) syncPush(w http.ResponseWriter, r *http.Request) {
	var req syncPushRequest
	if !decodeLimit(w, r, &req, 64<<20) {
		return
	}
	req.DeviceID = strings.TrimSpace(req.DeviceID)
	if req.DeviceID == "" || len(req.DeviceID) > 200 {
		jsonError(w, 400, "device_id is required")
		return
	}
	if len(req.Operations) > 500 {
		jsonError(w, 400, "At most 500 operations are allowed per push")
		return
	}
	resp := syncPushResponse{Applied: []string{}, Conflicts: []syncConflict{}, Errors: []syncOpError{}}
	for _, op := range req.Operations {
		op.OpID = strings.TrimSpace(op.OpID)
		if len(op.OpID) < 8 || len(op.OpID) > 128 {
			resp.Errors = append(resp.Errors, syncOpError{OpID: op.OpID, Error: "op_id must contain 8..128 characters"})
			continue
		}
		rawOp, _ := json.Marshal(op)
		sum := sha256.Sum256(rawOp)
		digest := hex.EncodeToString(sum[:])
		if stored, receiptErr := a.store.syncReceipt(op.OpID); receiptErr == nil {
			if stored.Digest != digest {
				resp.Errors = append(resp.Errors, syncOpError{OpID: op.OpID, Error: "op_id was used with different payload"})
				continue
			}
			resp.Applied = append(resp.Applied, op.OpID)
			resp.Receipts = append(resp.Receipts, stored.Receipt)
			continue
		} else if !errors.Is(receiptErr, sql.ErrNoRows) {
			resp.Errors = append(resp.Errors, syncOpError{OpID: op.OpID, Error: "could not read receipt"})
			continue
		}
		applied, err := a.store.SyncOpApplied(op.OpID)
		if err != nil {
			resp.Errors = append(resp.Errors, syncOpError{OpID: op.OpID, Error: "could not read idempotency record"})
			continue
		}
		if applied {
			resp.Applied = append(resp.Applied, op.OpID)
			continue
		}
		a.writeMu.Lock()
		if again, againErr := a.store.SyncOpApplied(op.OpID); againErr == nil && again {
			prior, receiptErr := a.store.syncReceipt(op.OpID)
			a.writeMu.Unlock()
			if receiptErr == nil && prior.Digest != digest {
				resp.Errors = append(resp.Errors, syncOpError{OpID: op.OpID, Error: "op_id was used with different payload"})
				continue
			}
			resp.Applied = append(resp.Applied, op.OpID)
			if receiptErr == nil {
				resp.Receipts = append(resp.Receipts, prior.Receipt)
			}
			continue
		}
		records, deletes, blobs, conflict, opErr := a.applySyncOperation(r.Context(), op)
		if opErr == nil {
			head, _ := a.store.HeadSeq()
			receipt := syncPushReceipt{OpID: op.OpID, ObjectID: op.ObjectID}
			for _, record := range records {
				if obj, ok := record.Value.(*Object); ok && record.Kind == "object" {
					copy := cloneObject(*obj)
					receipt.Object = &copy
					receipt.ObjectID = obj.ID
					receipt.Revision = obj.Revision
					if previous, err := a.store.Object(obj.ID); err == nil {
						receipt.PreviousRevision = previous.Revision
					} else if !errors.Is(err, sql.ErrNoRows) {
						opErr = errors.New("Could not read previous object revision")
						break
					}
					break
				}
			}
			if receipt.ObjectID == "" && op.Object != nil {
				if obj, e := a.store.Object(op.Object.ID); e == nil {
					receipt.ObjectID = obj.ID
					receipt.Revision = obj.Revision
					receipt.Object = &obj
				}
			}
			if opErr == nil && receipt.Object != nil && receipt.Revision == 1 {
				receipt.ReferenceIssues, receipt.ReferenceIssuesCursor, opErr = a.referenceTypeIssues(*receipt.Object, "")
			}
			if opErr == nil {
				records = append(records, Record{Kind: "sync_receipt", ID: op.OpID, Value: storedSyncReceipt{receipt, digest}})
				resp.Receipts = append(resp.Receipts, receipt)
				records = append(records, Record{Kind: "sync_op", Value: SyncOpRecord{OpID: op.OpID, DeviceID: req.DeviceID, HeadSeq: head}})
				opErr = a.store.PutBatchBlobs(records, deletes, blobs)
			}
		}
		a.writeMu.Unlock()
		if opErr != nil {
			if len(resp.Receipts) > 0 && resp.Receipts[len(resp.Receipts)-1].OpID == op.OpID {
				resp.Receipts = resp.Receipts[:len(resp.Receipts)-1]
			}
			resp.Errors = append(resp.Errors, syncOpError{OpID: op.OpID, Error: truncate(opErr.Error(), 300)})
			continue
		}
		resp.Applied = append(resp.Applied, op.OpID)
		if conflict != nil {
			resp.Conflicts = append(resp.Conflicts, *conflict)
		}
	}
	head, _ := a.store.HeadSeq()
	resp.HeadSeq = head
	jsonResponse(w, 200, resp)
}

func (a *App) applySyncOperation(ctx context.Context, op syncOperation) ([]Record, [][2]string, []BlobRecord, *syncConflict, error) {
	switch op.Action {
	case "put":
		return a.applySyncPut(ctx, op)
	case "delete":
		return a.applySyncDelete(ctx, op)
	case "asset":
		return a.applySyncAsset(ctx, op)
	default:
		return nil, nil, nil, nil, errors.New("action must be put, delete or asset")
	}
}

func mergeLinks(existing, incoming []string) []string {
	seen := map[string]bool{}
	out := []string{}
	for _, list := range [][]string{existing, incoming} {
		for _, id := range list {
			id = strings.TrimSpace(id)
			if id == "" || seen[id] {
				continue
			}
			seen[id] = true
			out = append(out, id)
		}
	}
	return out
}

func mergeProperties(existing, incoming map[string]any) map[string]any {
	out := map[string]any{}
	for key, value := range existing {
		out[key] = value
	}
	for key, value := range incoming {
		out[key] = value
	}
	return out
}

func mergeAssets(existing, incoming map[string]any) []any {
	out := []any{}
	seen := map[string]bool{}
	for _, source := range []map[string]any{existing, incoming} {
		raw, _ := source["assets"].([]any)
		for _, item := range raw {
			value, ok := item.(map[string]any)
			if !ok {
				continue
			}
			id, _ := value["id"].(string)
			if id == "" || seen[id] {
				continue
			}
			seen[id] = true
			out = append(out, value)
		}
	}
	return out
}

// isDeleted reports whether id has a tombstone: the object is either in the
// trash or permanently purged. Read paths that must hide deleted content
// regardless of restorability (for example collection items) use this.
func (a *App) isDeleted(id string) bool {
	return a.store.Has("tombstone", id)
}

// isPurged reports whether a target is permanently gone: it has a tombstone
// but no trash payload. Trashed objects keep their references because restore
// must be able to bring them back; purged ones can never resolve again.
func (a *App) isPurged(id string) bool {
	if a.store.Has("trash", id) {
		return false
	}
	var deleted tombstone
	return a.store.Get("tombstone", id, &deleted) == nil
}

// validateSyncObject preserves pending offline relations while validating their
// formats and every target whose live/trash type is already known. Only purged
// references are removed, by filterPurgedReferences before validation.
func (a *App) validateSyncObject(o *Object) error {
	if err := a.validateObjectReferences(o, true); err == nil {
		return nil
	}
	// A mobile put cannot ask the user to rename anything. When the only
	// problem is a title already owned by another object — typically a media
	// object the server created from the same capture — the title is
	// disambiguated instead of blocking the outbox forever.
	if owner := a.store.TitleOwner(o.Title); owner != "" && owner != o.ID {
		base := strings.TrimSpace(o.Title)
		if base != "" {
			for suffix := 2; suffix <= 100; suffix++ {
				candidate := fmt.Sprintf("%s (%d)", base, suffix)
				if holder := a.store.TitleOwner(candidate); holder == "" || holder == o.ID {
					o.Title = candidate
					return a.validateObjectReferences(o, true)
				}
			}
		}
	}
	return a.validateObjectReferences(o, true)
}

// filterPurgedReferences drops relations to objects that were permanently
// deleted on the server. A phone can be offline for days; rejecting the whole
// operation because a tag disappeared would leave its outbox stuck forever.
// References to trashed objects are kept: they are restorable, and a later
// purge cleans them through the reverse index.
func (a *App) filterPurgedReferences(o *Object) error {
	if len(o.Links) > 0 {
		clean := make([]string, 0, len(o.Links))
		for _, id := range o.Links {
			purged, err := a.purgedReference(id)
			if err != nil {
				return err
			}
			if purged {
				continue
			}
			clean = append(clean, id)
		}
		o.Links = clean
	}
	if o.Document != nil {
		purgedIDs := map[string]string{}
		checked := map[string]bool{}
		for _, ref := range document.References(*o.Document) {
			if ref.ObjectID == "" || checked[ref.ObjectID] {
				continue
			}
			checked[ref.ObjectID] = true
			purged, err := a.purgedReference(ref.ObjectID)
			if err != nil {
				return err
			}
			if purged {
				purgedIDs[ref.ObjectID] = ""
			}
		}
		removeCanonicalReferences(o, purgedIDs, nil)
	}

	typ, err := a.store.Type(o.TypeID)
	if err != nil {
		if errors.Is(err, sql.ErrNoRows) {
			return nil
		}
		return err
	}
	for _, raw := range typ.Properties {
		schema, ok := raw.(map[string]any)
		if !ok || schema["type"] != "object" {
			continue
		}
		key := schemaKey(schema)
		value, exists := o.Properties[key]
		if !exists {
			continue
		}
		switch v := value.(type) {
		case string:
			purged, err := a.purgedReference(v)
			if err != nil {
				return err
			}
			if purged {
				delete(o.Properties, key)
			}
		case []any:
			clean := make([]any, 0, len(v))
			for _, entry := range v {
				id, ok := entry.(string)
				if ok {
					purged, err := a.purgedReference(id)
					if err != nil {
						return err
					}
					if purged {
						continue
					}
				}
				clean = append(clean, entry)
			}
			if len(clean) == 0 {
				delete(o.Properties, key)
			} else {
				o.Properties[key] = clean
			}
		}
	}
	return nil
}

func toStrings(value any) ([]string, bool) {
	switch v := value.(type) {
	case []string:
		return append([]string(nil), v...), true
	case []any:
		out := make([]string, 0, len(v))
		for _, entry := range v {
			id, ok := entry.(string)
			if !ok {
				return nil, false
			}
			out = append(out, id)
		}
		return out, true
	default:
		return nil, false
	}
}

func mergeStringSlices(current, incoming any) (any, bool) {
	left, ok := toStrings(current)
	if !ok {
		return nil, false
	}
	right, ok := toStrings(incoming)
	if !ok {
		return nil, false
	}
	seen := map[string]bool{}
	out := make([]any, 0, len(left)+len(right))
	for _, group := range [][]string{left, right} {
		for _, id := range group {
			if id == "" || seen[id] {
				continue
			}
			seen[id] = true
			out = append(out, id)
		}
	}
	return out, true
}

func appendMobileContent(existing, incoming Object, stamp time.Time) string {
	body := strings.TrimSpace(incoming.Content)
	if body == "" {
		// A journal delta may only add links, a cover or properties. Do not
		// manufacture a text block for it.
		return existing.Content
	}
	marker := fmt.Sprintf("> [Edición desde móvil · %s]", stamp.Format("15:04"))
	if incoming.Properties["blocks"] == nil && strings.Contains(existing.Content, marker) && strings.Contains(existing.Content, body) {
		return existing.Content
	}
	if strings.TrimSpace(existing.Content) == "" {
		return body
	}
	return strings.TrimRight(existing.Content, "\n") + "\n\n" + marker + "\n" + body
}

func newerTimestamp(incoming, fallback string) time.Time {
	for _, value := range []string{incoming, fallback} {
		if value == "" {
			continue
		}
		if parsed, err := time.Parse(time.RFC3339Nano, value); err == nil {
			return parsed
		}
	}
	return time.Now().UTC()
}

func (a *App) applySyncPut(ctx context.Context, op syncOperation) ([]Record, [][2]string, []BlobRecord, *syncConflict, error) {
	if op.Object == nil {
		return nil, nil, nil, nil, errors.New("put requires object")
	}
	incoming := *op.Object
	if incoming.ID == "" {
		incoming.ID = newID()
	}
	if !validObjectReferenceID(incoming.ID) {
		return nil, nil, nil, nil, errors.New("Object identity is invalid")
	}
	if incoming.TypeID == "" {
		incoming.TypeID = "note"
	}
	if incoming.CreatedAt == "" {
		incoming.CreatedAt = now()
	}
	if incoming.UpdatedAt == "" {
		incoming.UpdatedAt = now()
	}
	if err := a.filterPurgedReferences(&incoming); err != nil {
		return nil, nil, nil, nil, err
	}
	var existing Object
	err := a.store.Get("object", incoming.ID, &existing)
	if err != nil && !errors.Is(err, sql.ErrNoRows) {
		return nil, nil, nil, nil, err
	}
	if errors.Is(err, sql.ErrNoRows) {
		var deleted tombstone
		if a.store.Get("tombstone", incoming.ID, &deleted) == nil {
			journal := incoming.TypeID == "journal" && strings.HasPrefix(incoming.ID, "journal-")
			if !journal && deleted.Revision >= op.BaseRevision {
				return nil, nil, nil, &syncConflict{OpID: op.OpID, ObjectID: incoming.ID, Resolution: "ignored_tombstoned"}, nil
			}
		}
		incoming.Revision = 0
		incoming.Incarnation = ""
		incoming.SyncStatus, incoming.SyncError = "synced", ""
		if err := a.validateMediaCreation(&incoming, true); err != nil {
			return nil, nil, nil, nil, err
		}
		if err := a.validateSyncObject(&incoming); err != nil {
			return nil, nil, nil, nil, err
		}
		return a.prepareSave(&incoming), saveCleanup(incoming.ID), nil, nil, nil
	}
	if err := a.filterPurgedReferences(&existing); err != nil {
		return nil, nil, nil, nil, err
	}

	if incoming.Document != nil || existing.Document != nil {
		if incoming.Document == nil {
			return nil, nil, nil, nil, errors.New("canonical document is required")
		}
		base := op.BaseRevision
		if base == 0 {
			base = incoming.Revision
		}
		if !op.Append && base != existing.Revision {
			return nil, nil, nil, nil, fmt.Errorf("document_conflict: expected revision %d; current %d", base, existing.Revision)
		}
		merged := cloneObject(incoming)
		merged.CreatedAt = existing.CreatedAt
		merged.Incarnation = existing.Incarnation
		merged.Revision = existing.Revision
		if op.Append {
			if incoming.TypeID != "journal" {
				return nil, nil, nil, nil, errors.New("document append requires journal")
			}
			if existing.Document == nil {
				return nil, nil, nil, nil, errors.New("journal has no canonical document")
			}
			joined := *cloneDocument(existing.Document)
			joined.Blocks = append(joined.Blocks, incoming.Document.Blocks...)
			merged = cloneObject(existing)
			merged.Document = &joined
			merged.Properties = mergeProperties(existing.Properties, incoming.Properties)
			merged.Links = mergeLinks(existing.Links, incoming.Links)
		}
		if err := a.validateSyncObject(&merged); err != nil {
			return nil, nil, nil, nil, err
		}
		if reflect.DeepEqual(existing.Document, merged.Document) && reflect.DeepEqual(existing.Properties, merged.Properties) && existing.Title == merged.Title && existing.TypeID == merged.TypeID && sameStrings(existing.Links, merged.Links) {
			return nil, nil, nil, nil, nil
		}
		merged.UpdatedAt = now()
		records := a.prepareSave(&merged)
		records = append(records, a.templateDefaultCleanup(existing, merged)...)
		return records, saveCleanup(merged.ID), nil, nil, nil
	}
	base := op.BaseRevision
	if base == 0 {
		base = incoming.Revision
	}
	if incoming.TypeID == "journal" && op.Append {
		merged := existing
		merged.Properties = map[string]any{}
		for key, value := range existing.Properties {
			merged.Properties[key] = value
		}
		merged.Content = appendMobileContent(existing, incoming, newerTimestamp(op.ClientTime, incoming.UpdatedAt))
		merged.Links = mergeLinks(existing.Links, incoming.Links)
		if merged.Properties == nil {
			merged.Properties = map[string]any{}
		}
		merged.Properties["assets"] = mergeAssets(existing.Properties, incoming.Properties)
		if incoming.Properties != nil {
			if cover, ok := incoming.Properties["image_asset_id"]; ok {
				if current, exists := merged.Properties["image_asset_id"]; !exists || current == nil || current == "" {
					merged.Properties["image_asset_id"] = cover
				}
			}
			for key, value := range incoming.Properties {
				if key == "assets" || key == "image_asset_id" {
					continue
				}
				if _, exists := merged.Properties[key]; !exists {
					merged.Properties[key] = value
				}
			}
		}
		if err := mergeMobileJournalBlocks(existing, incoming, &merged, newerTimestamp(op.ClientTime, incoming.UpdatedAt)); err != nil {
			return nil, nil, nil, nil, err
		}
		merged.UpdatedAt = now()
		if err := a.validateSyncObject(&merged); err != nil {
			return nil, nil, nil, nil, err
		}
		return a.prepareSave(&merged), saveCleanup(merged.ID), nil, nil, nil
	}

	if existing.TypeID == "task" || incoming.TypeID == "task" {
		incomingTime := newerTimestamp(incoming.UpdatedAt, op.ClientTime)
		existingTime := newerTimestamp(existing.UpdatedAt, "")
		if incomingTime.After(existingTime) {
			merged := existing
			if strings.TrimSpace(incoming.Title) != "" {
				merged.Title = incoming.Title
			}
			merged.Content = incoming.Content
			merged.Properties = mergeProperties(existing.Properties, incoming.Properties)
			merged.Links = mergeLinks(existing.Links, incoming.Links)
			merged.UpdatedAt = incomingTime.UTC().Format("2006-01-02T15:04:05.000000000Z")
			if err := a.validateSyncObject(&merged); err != nil {
				return nil, nil, nil, nil, err
			}
			conflict := &syncConflict{OpID: op.OpID, ObjectID: merged.ID, Resolution: "task_latest_updated_at"}
			records := a.prepareSave(&merged)
			records = append(records, a.templateDefaultCleanup(existing, merged)...)
			return records, saveCleanup(merged.ID), nil, conflict, nil
		}
		conflict := &syncConflict{OpID: op.OpID, ObjectID: existing.ID, Resolution: "task_kept_server_newer"}
		return nil, nil, nil, conflict, nil
	}

	if base > 0 && base < existing.Revision {
		merged := existing
		merged.Content = appendMobileContent(existing, incoming, newerTimestamp(op.ClientTime, incoming.UpdatedAt))
		merged.Links = mergeLinks(existing.Links, incoming.Links)
		if merged.Properties == nil {
			merged.Properties = map[string]any{}
		}
		merged.Properties["assets"] = mergeAssets(existing.Properties, incoming.Properties)
		conflictValues := map[string]any{}
		for key, value := range incoming.Properties {
			if key == "assets" {
				continue
			}
			if current, exists := merged.Properties[key]; !exists {
				merged.Properties[key] = value
			} else if !reflect.DeepEqual(current, value) {
				if mergedList, ok := mergeStringSlices(current, value); ok {
					merged.Properties[key] = mergedList
				} else {
					conflictValues[key] = value
				}
			}
		}
		if len(conflictValues) > 0 {
			history, _ := merged.Properties["mobile_conflict"].(map[string]any)
			if history == nil {
				history = map[string]any{}
			}
			history[now()] = conflictValues
			merged.Properties["mobile_conflict"] = history
		}
		merged.UpdatedAt = now()
		if err := a.validateSyncObject(&merged); err != nil {
			return nil, nil, nil, nil, err
		}
		conflict := &syncConflict{OpID: op.OpID, ObjectID: merged.ID, Resolution: "server_kept_mobile_appended"}
		return a.prepareSave(&merged), saveCleanup(merged.ID), nil, conflict, nil
	}

	merged := incoming
	merged.ID = existing.ID
	merged.CreatedAt = existing.CreatedAt
	merged.Incarnation = existing.Incarnation
	merged.Revision = existing.Revision
	merged.Properties = mergeProperties(existing.Properties, incoming.Properties)
	merged.Links = mergeLinks(existing.Links, incoming.Links)
	merged.UpdatedAt = now()
	if err := a.validateSyncObject(&merged); err != nil {
		return nil, nil, nil, nil, err
	}
	records := a.prepareSave(&merged)
	records = append(records, a.templateDefaultCleanup(existing, merged)...)
	return records, saveCleanup(merged.ID), nil, nil, nil
}

func (a *App) applySyncDelete(ctx context.Context, op syncOperation) ([]Record, [][2]string, []BlobRecord, *syncConflict, error) {
	id := strings.TrimSpace(op.ObjectID)
	if id == "" && op.Object != nil {
		id = op.Object.ID
	}
	if id == "" {
		return nil, nil, nil, nil, errors.New("delete requires object_id")
	}
	// A mobile delete moves the object to the trash exactly like the HTTP
	// delete: the phone still receives the tombstone on its next pull.
	records, deletes, _, err := a.trashObjectData(id)
	if errors.Is(err, sql.ErrNoRows) {
		return nil, nil, nil, nil, nil
	}
	if err != nil {
		return nil, nil, nil, nil, err
	}
	return records, deletes, nil, nil, nil
}

func (a *App) applySyncAsset(ctx context.Context, op syncOperation) ([]Record, [][2]string, []BlobRecord, *syncConflict, error) {
	if op.Asset == nil {
		return nil, nil, nil, nil, errors.New("asset requires asset")
	}
	data, err := base64.StdEncoding.DecodeString(op.Asset.DataBase64)
	if err != nil || len(data) == 0 {
		return nil, nil, nil, nil, errors.New("asset data_base64 is invalid")
	}
	if len(data) > 20<<20 {
		return nil, nil, nil, nil, errors.New("asset exceeds 20 MB")
	}
	mimeType := detectAssetMIME(data, op.Asset.Name, op.Asset.MIME)
	id := strings.TrimSpace(op.Asset.ID)
	if id == "" {
		id = newID()
	}
	asset := Asset{
		ID:        id,
		ObjectID:  strings.TrimSpace(op.Asset.ObjectID),
		Name:      strings.TrimSpace(op.Asset.Name),
		MIME:      mimeType,
		Size:      len(data),
		CreatedAt: op.Asset.CreatedAt,
	}
	if asset.Name == "" {
		asset.Name = "asset"
	}
	if asset.CreatedAt == "" {
		asset.CreatedAt = now()
	}
	var previous Asset
	if err := a.store.Get("asset", asset.ID, &previous); err == nil {
		sameOwner := previous.ObjectID == asset.ObjectID || (asset.ObjectID == "" && op.Asset.CreateMediaObject && previous.ObjectID == previous.MediaObjectID)
		if previous.Size != len(data) || !sameOwner {
			return nil, nil, nil, nil, errors.New("Asset identity is immutable; upload a different file under a new asset_id")
		}
		storedBytes, err := a.store.Blob(asset.ID)
		if err != nil {
			return nil, nil, nil, nil, errors.New("Could not read existing asset")
		}
		if sha256.Sum256(storedBytes) != sha256.Sum256(data) {
			return nil, nil, nil, nil, errors.New("Asset identity is immutable; upload a different file under a new asset_id")
		}
		asset = previous
		// A resend of already committed media is a no-op, including history.
		if !op.Asset.CreateMediaObject || asset.MediaObjectID != "" {
			return nil, nil, nil, nil, nil
		}
	} else if !errors.Is(err, sql.ErrNoRows) {
		return nil, nil, nil, nil, errors.New("Could not read existing asset")
	}
	records := []Record{}
	var media Object
	if op.Asset.CreateMediaObject {
		media, err = a.newMediaObject(&asset)
		if err != nil {
			return nil, nil, nil, nil, err
		}
		records = append(records, a.prepareSave(&media)...)
	}
	if asset.ObjectID != "" {
		if owner, err := a.store.Object(asset.ObjectID); err == nil {
			if mediaSourceID(owner) == asset.ID && (owner.TypeID == "image" || owner.TypeID == "pdf") {
				if !mediaMatchesObject(owner, asset) {
					return nil, nil, nil, nil, errors.New("Media source has the wrong file type")
				}
				owner.Properties["media_state"] = "ready"
			}
			if owner.Properties == nil {
				owner.Properties = map[string]any{}
			}
			existing, _ := owner.Properties["assets"].([]any)
			found := false
			for i, raw := range existing {
				if item, ok := raw.(map[string]any); ok && item["id"] == asset.ID {
					existing[i] = assetProperties(asset)
					found = true
				}
			}
			if !found {
				existing = append(existing, assetProperties(asset))
			}
			owner.Properties["assets"] = existing
			if strings.HasPrefix(asset.MIME, "image/") {
				if _, exists := owner.Properties["image_asset_id"]; !exists {
					owner.Properties["image_asset_id"] = asset.ID
				}
			}
			if media.ID != "" {
				owner.Links = mergeLinks(owner.Links, []string{media.ID})
			}
			owner.UpdatedAt = now()
			records = append(records, a.prepareSave(&owner)...)
		}
	}
	records = append(records, Record{Kind: "asset", ID: asset.ID, Value: asset})
	return records, nil, []BlobRecord{{ID: asset.ID, Data: data}}, nil, nil
}
