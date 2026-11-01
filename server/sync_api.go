package main

import (
	"context"
	"database/sql"
	"encoding/base64"
	"errors"
	"fmt"
	"net/http"
	"reflect"
	"sort"
	"strings"
	"time"
)

const syncProtocolVersion = "lumen-sync/1"

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

type syncPushResponse struct {
	HeadSeq   int64          `json:"head_seq"`
	Applied   []string       `json:"applied"`
	Conflicts []syncConflict `json:"conflicts,omitempty"`
	Errors    []syncOpError  `json:"errors,omitempty"`
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

func (a *App) syncPull(w http.ResponseWriter, r *http.Request) {
	var req syncPullRequest
	if !decode(w, r, &req) {
		return
	}
	if req.SinceSeq < 0 {
		req.SinceSeq = 0
	}
	if req.MediaSinceDate != "" {
		if d, err := time.Parse("2006-01-02", req.MediaSinceDate); err != nil || d.Format("2006-01-02") != req.MediaSinceDate {
			jsonError(w, 400, "media_since_date must be YYYY-MM-DD")
			return
		}
	}
	head, err := a.store.HeadSeq()
	if err != nil {
		jsonError(w, 500, "Could not read sync sequence")
		return
	}
	changes, err := a.store.ChangesSince(req.SinceSeq)
	if err != nil {
		jsonError(w, 500, "Could not read sync changes")
		return
	}
	objects := map[string]Object{}
	types := map[string]ObjectType{}
	tombstones := map[string]tombstone{}
	assets := map[string]Asset{}
	deleted := map[string]syncDeleted{}
	for _, change := range changes {
		key := change.Kind + "/" + change.ID
		if change.Op == "delete" {
			deleted[key] = syncDeleted{Kind: change.Kind, ID: change.ID, Seq: change.Seq}
			continue
		}
		switch change.Kind {
		case "object":
			if o, err := a.store.Object(change.ID); err == nil {
				objects[o.ID] = o
			} else {
				deleted[key] = syncDeleted{Kind: change.Kind, ID: change.ID, Seq: change.Seq}
			}
		case "type":
			if t, err := a.store.Type(change.ID); err == nil {
				types[t.ID] = t
			} else {
				deleted[key] = syncDeleted{Kind: change.Kind, ID: change.ID, Seq: change.Seq}
			}
		case "tombstone":
			var t tombstone
			if a.store.Get("tombstone", change.ID, &t) == nil {
				tombstones[t.ID] = t
			}
		case "asset":
			var asset Asset
			if a.store.Get("asset", change.ID, &asset) == nil {
				assets[asset.ID] = asset
			} else {
				deleted[key] = syncDeleted{Kind: change.Kind, ID: change.ID, Seq: change.Seq}
			}
		}
	}
	resp := syncPullResponse{HeadSeq: head, Objects: []Object{}, Types: []ObjectType{}, Tombstones: []tombstone{}, Assets: []Asset{}, Deleted: []syncDeleted{}}
	for _, o := range objects {
		resp.Objects = append(resp.Objects, o)
	}
	for _, t := range types {
		resp.Types = append(resp.Types, t)
	}
	for _, t := range tombstones {
		resp.Tombstones = append(resp.Tombstones, t)
	}
	for _, asset := range assets {
		resp.Assets = append(resp.Assets, asset)
	}
	for _, d := range deleted {
		resp.Deleted = append(resp.Deleted, d)
	}
	sort.Slice(resp.Objects, func(i, j int) bool { return resp.Objects[i].ID < resp.Objects[j].ID })
	sort.Slice(resp.Types, func(i, j int) bool { return resp.Types[i].ID < resp.Types[j].ID })
	sort.Slice(resp.Tombstones, func(i, j int) bool { return resp.Tombstones[i].ID < resp.Tombstones[j].ID })
	sort.Slice(resp.Assets, func(i, j int) bool { return resp.Assets[i].ID < resp.Assets[j].ID })
	sort.Slice(resp.Deleted, func(i, j int) bool {
		if resp.Deleted[i].Kind != resp.Deleted[j].Kind {
			return resp.Deleted[i].Kind < resp.Deleted[j].Kind
		}
		return resp.Deleted[i].ID < resp.Deleted[j].ID
	})
	jsonResponse(w, 200, resp)
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
			a.writeMu.Unlock()
			resp.Applied = append(resp.Applied, op.OpID)
			continue
		}
		records, deletes, blobs, conflict, opErr := a.applySyncOperation(r.Context(), op)
		if opErr == nil {
			head, _ := a.store.HeadSeq()
			records = append(records, Record{Kind: "sync_op", Value: SyncOpRecord{OpID: op.OpID, DeviceID: req.DeviceID, HeadSeq: head}})
			opErr = a.store.PutBatchBlobs(records, deletes, blobs)
		}
		a.writeMu.Unlock()
		if opErr != nil {
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

// validateSyncObject validates a mobile operation like the direct API, except
// that relations whose target does not exist on the server yet are dropped
// instead of failing the whole operation: a push batch can create the target
// in a later operation, phones can be offline for days, and a relation must
// never block its object forever. Tombstoned targets are already filtered by
// filterTombstonedReferences; these are targets that never landed here.
func (a *App) validateSyncObject(o *Object) error {
	a.dropUnresolvableRelations(o)
	if err := a.validateObject(o); err == nil {
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
					return a.validateObject(o)
				}
			}
		}
	}
	return a.validateObject(o)
}

func (a *App) dropUnresolvableRelations(o *Object) {
	if len(o.Links) > 0 {
		clean := make([]string, 0, len(o.Links))
		for _, id := range o.Links {
			if id == o.ID {
				continue
			}
			// Live targets always stay; trashed targets stay restorable
			// (parity trash semantics); only purged and never-landed
			// targets are dropped.
			if _, err := a.store.Object(id); err != nil && !a.store.Has("trash", id) {
				continue
			}
			clean = append(clean, id)
		}
		o.Links = clean
	}
	typ, err := a.store.Type(o.TypeID)
	if err != nil {
		return
	}
	for _, raw := range typ.Properties {
		schema, ok := raw.(map[string]any)
		if !ok || schema["type"] != "object" {
			continue
		}
		key := schemaKey(schema)
		value, exists := o.Properties[key]
		if !exists || value == nil || value == "" {
			continue
		}
		switch v := value.(type) {
		case string:
			if v != o.ID {
				if _, err := a.store.Object(v); err != nil && !a.store.Has("trash", v) {
					delete(o.Properties, key)
				}
			}
		case []any:
			clean := make([]any, 0, len(v))
			for _, entry := range v {
				id, ok := entry.(string)
				if !ok {
					continue
				}
				if id != o.ID {
					// Live targets always stay; trashed targets stay
					// restorable (parity trash semantics); only purged and
					// never-landed targets are dropped.
					if _, err := a.store.Object(id); err != nil && !a.store.Has("trash", id) {
						continue
					}
				}
				clean = append(clean, id)
			}
			if len(clean) == 0 {
				delete(o.Properties, key)
			} else {
				o.Properties[key] = clean
			}
		}
	}
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
			if a.isPurged(id) {
				continue
			}
			clean = append(clean, id)
		}
		o.Links = clean
	}
	typ, err := a.store.Type(o.TypeID)
	if err != nil {
		return nil
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
			if a.isPurged(v) {
				delete(o.Properties, key)
			}
		case []any:
			clean := make([]any, 0, len(v))
			for _, entry := range v {
				id, ok := entry.(string)
				if ok && a.isPurged(id) {
					continue
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
		if err := a.validateSyncObject(&incoming); err != nil {
			return nil, nil, nil, nil, err
		}
		return a.prepareSave(&incoming), saveCleanup(incoming.ID), nil, nil, nil
	}
	_ = a.filterPurgedReferences(&existing)

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
