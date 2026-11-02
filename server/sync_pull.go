package main

import (
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"net/http"
	"sort"
	"time"
)

const maxSyncPullRecords = 128
const maxSyncPullBytes = 8 << 20

// Bound record identities before decrypting payloads. The captured head keeps a
// finite page even while another client keeps appending changes.
func (s *Store) changesPage(since, head int64) ([]SyncChange, error) {
	rows, err := s.db.Query(`SELECT c.seq,c.kind,c.id,c.op FROM sync_changes c
 JOIN (SELECT MAX(seq) AS seq FROM sync_changes WHERE seq>? AND seq<=?
 AND kind IN ('object','type','tombstone','asset') GROUP BY kind,id) latest ON latest.seq=c.seq
 ORDER BY c.seq LIMIT ?`, since, head, maxSyncPullRecords+1)
	if err != nil {
		return nil, err
	}
	defer rows.Close()
	changes := []SyncChange{}
	for rows.Next() {
		var c SyncChange
		if err := rows.Scan(&c.Seq, &c.Kind, &c.ID, &c.Op); err != nil {
			return nil, err
		}
		changes = append(changes, c)
	}
	return changes, rows.Err()
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
	changes, err := a.store.changesPage(req.SinceSeq, head)
	if err != nil {
		jsonError(w, 500, "Could not read sync changes")
		return
	}
	resp := syncPullResponse{HeadSeq: head, NextSeq: req.SinceSeq, Objects: []Object{}, Types: []ObjectType{}, Tombstones: []tombstone{}, Assets: []Asset{}, Deleted: []syncDeleted{}}
	bytes, processed := 0, 0
	for _, change := range changes {
		if processed == maxSyncPullRecords {
			resp.HasMore = true
			break
		}
		var value any
		deleted := change.Op == "delete"
		var readErr error
		if !deleted {
			switch change.Kind {
			case "object":
				var o Object
				o, readErr = a.store.Object(change.ID)
				value = o
			case "type":
				var t ObjectType
				t, readErr = a.store.Type(change.ID)
				value = t
			case "tombstone":
				var t tombstone
				readErr = a.store.Get("tombstone", change.ID, &t)
				value = t
			case "asset":
				var asset Asset
				readErr = a.store.Get("asset", change.ID, &asset)
				value = asset
			}
			if errors.Is(readErr, sql.ErrNoRows) {
				deleted = true
			} else if readErr != nil {
				jsonError(w, 500, "Could not read sync payload")
				return
			}
		}
		if deleted {
			value = syncDeleted{Kind: change.Kind, ID: change.ID, Seq: change.Seq}
		}
		encoded, err := json.Marshal(value)
		if err != nil {
			jsonError(w, 500, "Could not encode sync payload")
			return
		}
		// A single valid large object is delivered whole, so the cursor can always
		// progress. Subsequent records wait for the next page instead of truncating.
		if processed > 0 && bytes+len(encoded) > maxSyncPullBytes {
			resp.HasMore = true
			break
		}
		bytes += len(encoded)
		processed++
		resp.NextSeq = change.Seq
		switch v := value.(type) {
		case Object:
			resp.Objects = append(resp.Objects, v)
		case ObjectType:
			resp.Types = append(resp.Types, v)
		case tombstone:
			resp.Tombstones = append(resp.Tombstones, v)
		case Asset:
			resp.Assets = append(resp.Assets, v)
		case syncDeleted:
			resp.Deleted = append(resp.Deleted, v)
		default:
			jsonError(w, 500, fmt.Sprintf("Unsupported sync payload %s", change.Kind))
			return
		}
	}
	if !resp.HasMore {
		resp.NextSeq = head
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
