package main

import (
	"encoding/json"
	"fmt"
	"io"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"syscall"
	"time"
)


const (
	chunkedUploadMaxSize = 500 << 20
	chunkedUploadMinPart = 5 << 20
	chunkedUploadMaxPart = 25 << 20
	chunkedUploadTTL     = 24 * time.Hour
	uploadDiskMargin     = 64 << 20
)

// uploadSession is the durable state of one in-flight upload. It is sealed to
// disk next to the parts, so an upload survives a server restart.
type uploadSession struct {
	ID        string    `json:"id"`
	AssetID   string    `json:"asset_id"`
	ObjectID  string    `json:"object_id,omitempty"`
	Name      string    `json:"name"`
	MIME      string    `json:"mime"`
	Size      int64     `json:"size"`
	Received  int64     `json:"received"`
	Parts     int       `json:"parts"`
	CreatedAt time.Time `json:"created_at"`
	UpdatedAt time.Time `json:"updated_at"`
}

func (a *App) uploadSessionDir(id string) string { return filepath.Join(a.uploadDir, id) }
func (a *App) uploadSessionPath(id string) string {
	return filepath.Join(a.uploadSessionDir(id), "session.bin")
}
func (a *App) uploadPartPath(id string, index int) string {
	return filepath.Join(a.uploadSessionDir(id), fmt.Sprintf("part-%06d", index))
}

// saveUploadSession persists the session metadata next to its parts, sealed
// with the store key.
func (a *App) saveUploadSession(s *uploadSession) error {
	raw, err := json.Marshal(s)
	if err != nil {
		return err
	}
	sealed, err := a.store.encrypt(raw, "upload/"+s.ID)
	if err != nil {
		return err
	}
	return os.WriteFile(a.uploadSessionPath(s.ID), sealed, 0600)
}

// loadUploadSessionLocked returns the live session, recovering it from disk
// when the server restarted mid-upload. The caller holds uploadMu.
func (a *App) loadUploadSessionLocked(id string) (*uploadSession, bool) {
	if s, ok := a.uploads[id]; ok {
		return s, true
	}
	sealed, err := os.ReadFile(a.uploadSessionPath(id))
	if err != nil {
		return nil, false
	}
	raw, err := a.store.decrypt(sealed, "upload/"+id)
	if err != nil {
		return nil, false
	}
	var s uploadSession
	if json.Unmarshal(raw, &s) != nil || s.ID != id {
		return nil, false
	}
	a.uploads[id] = &s
	return &s, true
}

// dropUploadSession removes the staging directory and the live entry.
func (a *App) dropUploadSession(id string) {
	a.uploadMu.Lock()
	delete(a.uploads, id)
	a.uploadMu.Unlock()
	_ = os.RemoveAll(a.uploadSessionDir(id))
}

// sweepUploadSessions is the TTL janitor: it runs on every chunked-upload
// request and removes the sessions whose newest activity is older than the
// TTL, whether or not the server restarted in between.
func (a *App) sweepUploadSessions() {
	a.uploadMu.Lock()
	var expired []string
	entries, err := os.ReadDir(a.uploadDir)
	if err != nil {
		a.uploadMu.Unlock()
		return
	}
	for _, entry := range entries {
		if !entry.IsDir() {
			continue
		}
		id := entry.Name()
		info, infoErr := entry.Info()
		if infoErr != nil {
			continue
		}
		age := time.Since(info.ModTime())
		if s, ok := a.loadUploadSessionLocked(id); ok {
			age = time.Since(s.UpdatedAt)
		}
		if age >= chunkedUploadTTL {
			delete(a.uploads, id)
			expired = append(expired, id)
		}
	}
	a.uploadMu.Unlock()
	for _, id := range expired {
		_ = os.RemoveAll(a.uploadSessionDir(id))
	}
}

// availableBytes reports the free space of the filesystem holding path. It is
// a best-effort guard: when the stat fails, the upload proceeds.
func availableBytes(path string) (uint64, bool) {
	var st syscall.Statfs_t
	if err := syscall.Statfs(path, &st); err != nil {
		return 0, false
	}
	return uint64(st.Bavail) * uint64(st.Bsize), true
}

type chunkedUploadInitRequest struct {
	Filename string `json:"filename"`
	MIME     string `json:"mime"`
	Size     int64  `json:"size"`
	ObjectID string `json:"object_id"`
}

// startChunkedUpload serves POST /v1/assets/upload/init. The mime allowlist is
// the same as POST /v1/assets: every upload becomes a typed media/file object.
func (a *App) startChunkedUpload(w http.ResponseWriter, r *http.Request) {
	a.sweepUploadSessions()
	var req chunkedUploadInitRequest
	if !decode(w, r, &req) {
		return
	}
	name := filepath.Base(strings.TrimSpace(req.Filename))
	if name == "" || name == "." || name == string(filepath.Separator) || name == ".." {
		jsonError(w, 400, "Provide a filename")
		return
	}
	if req.Size <= 0 {
		jsonError(w, 400, "size must be a positive number of bytes")
		return
	}
	if req.Size > chunkedUploadMaxSize {
		jsonError(w, 400, "The upload exceeds the 500 MB limit")
		return
	}
	mimeType := strings.ToLower(strings.TrimSpace(req.MIME))
	if mimeType == "" || len(mimeType) > 256 || strings.ContainsAny(mimeType, " \t\n") {
		jsonError(w, 400, "Provide a mime type")
		return
	}
	objectID := strings.TrimSpace(req.ObjectID)
	if objectID != "" {
		if _, err := a.store.Object(objectID); err != nil {
			jsonError(w, 404, "Object not found")
			return
		}
	}
	if err := os.MkdirAll(a.uploadDir, 0700); err != nil {
		jsonError(w, 500, "Could not prepare the upload directory")
		return
	}
	if free, ok := availableBytes(a.uploadDir); ok && free < uint64(req.Size)+uploadDiskMargin {
		jsonError(w, 507, "Not enough free disk space for the upload")
		return
	}
	s := &uploadSession{
		ID: newID(), AssetID: newID(), ObjectID: objectID,
		Name: name, MIME: mimeType, Size: req.Size,
		CreatedAt: time.Now().UTC(), UpdatedAt: time.Now().UTC(),
	}
	if err := os.MkdirAll(a.uploadSessionDir(s.ID), 0700); err != nil {
		jsonError(w, 500, "Could not prepare the upload directory")
		return
	}
	if err := a.saveUploadSession(s); err != nil {
		_ = os.RemoveAll(a.uploadSessionDir(s.ID))
		jsonError(w, 500, "Could not persist the upload session")
		return
	}
	a.uploadMu.Lock()
	a.uploads[s.ID] = s
	a.uploadMu.Unlock()
	jsonResponse(w, 201, map[string]any{
		"upload_id":      s.ID,
		"asset_id":       s.AssetID,
		"size":           s.Size,
		"received":       0,
		"part_min_bytes": chunkedUploadMinPart,
		"part_max_bytes": chunkedUploadMaxPart,
		"expires_at":     s.CreatedAt.Add(chunkedUploadTTL).UTC().Format(time.RFC3339),
	})
}

// uploadChunkedPart serves POST /v1/assets/upload/{upload_id}/part?index=N.
// Parts are strictly sequential and 1-based; only the final part may be
// smaller than 5 MB. The whole operation runs under uploadMu so concurrent
// requests cannot race the expected index.
func (a *App) uploadChunkedPart(w http.ResponseWriter, r *http.Request) {
	a.sweepUploadSessions()
	index, err := strconv.Atoi(r.URL.Query().Get("index"))
	if err != nil || index < 1 {
		jsonError(w, 400, "The part index must be a positive integer (?index=N, 1-based)")
		return
	}
	id := r.PathValue("upload_id")
	a.uploadMu.Lock()
	defer a.uploadMu.Unlock()
	s, ok := a.loadUploadSessionLocked(id)
	if !ok {
		jsonError(w, 404, "Unknown upload")
		return
	}
	if index != s.Parts+1 {
		jsonError(w, 400, fmt.Sprintf("Out-of-order part: expected index %d, received %d", s.Parts+1, index))
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, chunkedUploadMaxPart+1)
	data, readErr := io.ReadAll(r.Body)
	if readErr != nil || len(data) > chunkedUploadMaxPart {
		jsonError(w, 400, "A part must not exceed 25 MB")
		return
	}
	if len(data) == 0 {
		jsonError(w, 400, "A part must not be empty")
		return
	}
	if s.Received+int64(len(data)) > s.Size {
		jsonError(w, 400, "The part exceeds the declared upload size")
		return
	}
	if s.Received+int64(len(data)) < s.Size && int64(len(data)) < chunkedUploadMinPart {
		jsonError(w, 400, "Only the last part may be smaller than 5 MB")
		return
	}
	if free, ok := availableBytes(a.uploadDir); ok && free < uint64(s.Size-s.Received)+uploadDiskMargin {
		jsonError(w, 507, "Not enough free disk space for the remaining upload")
		return
	}
	sealed, sealErr := a.store.SealBlobChunk(s.AssetID, data)
	if sealErr != nil {
		jsonError(w, 500, "Could not encrypt the upload part")
		return
	}
	if err := os.WriteFile(a.uploadPartPath(id, index), sealed, 0600); err != nil {
		jsonError(w, 500, "Could not store the upload part")
		return
	}
	s.Received += int64(len(data))
	s.Parts++
	s.UpdatedAt = time.Now().UTC()
	if err := a.saveUploadSession(s); err != nil {
		jsonError(w, 500, "Could not persist the upload session")
		return
	}
	jsonResponse(w, 200, map[string]any{
		"upload_id": id,
		"received":  s.Received,
		"size":      s.Size,
		"parts":     s.Parts,
	})
}

// completeChunkedUpload serves POST /v1/assets/upload/{upload_id}/complete:
// it assembles the parts into a normal Asset with the same semantics as
// POST /v1/assets (media object creation, owner attachment metadata, mime
// sniffing of the first bytes), committed atomically.
func (a *App) completeChunkedUpload(w http.ResponseWriter, r *http.Request) {
	a.sweepUploadSessions()
	id := r.PathValue("upload_id")
	a.uploadMu.Lock()
	s, ok := a.loadUploadSessionLocked(id)
	a.uploadMu.Unlock()
	if !ok {
		jsonError(w, 404, "Unknown upload")
		return
	}
	if s.Received != s.Size {
		jsonError(w, 400, fmt.Sprintf("The upload is incomplete: %d of %d bytes received", s.Received, s.Size))
		return
	}
	head, err := a.readUploadHead(s, 512)
	if err != nil {
		jsonError(w, 500, "Could not read the uploaded parts")
		return
	}
	// POST /v1/assets sniffs the content type; the declared mime is the
	// fallback when the signature is unknown.
	mimeType := detectAssetMIME(head, s.Name, s.MIME)
	a.writeMu.Lock()
	defer a.writeMu.Unlock()
	asset := Asset{ID: s.AssetID, ObjectID: s.ObjectID, Name: s.Name, MIME: mimeType, Size: int(s.Size), CreatedAt: now()}
	records := []Record{}
	var owner Object
	hasOwner := false
	if s.ObjectID != "" {
		if err := a.store.Get("object", s.ObjectID, &owner); err != nil {
			jsonError(w, 404, "Object not found")
			return
		}
		hasOwner = true
	}
	if mediaType(asset.MIME) != "" {
		media, mediaErr := a.newMediaObject(&asset)
		if mediaErr != nil {
			jsonError(w, 400, mediaErr.Error())
			return
		}
		records = append(records, a.prepareSave(&media)...)
		asset.MediaObject = &media
		if hasOwner {
			owner.Links = append(owner.Links, media.ID)
		}
	}
	if hasOwner {
		if owner.Properties == nil {
			owner.Properties = map[string]any{}
		}
		existing, _ := owner.Properties["assets"].([]any)
		owner.Properties["assets"] = append(existing, assetProperties(asset))
		if strings.HasPrefix(mimeType, "image/") {
			owner.Properties["image_asset_id"] = asset.ID
		}
		owner.UpdatedAt = now()
		records = append(records, a.prepareSave(&owner)...)
	}
	storedAsset := asset
	storedAsset.MediaObject = nil
	records = append(records, Record{Kind: "asset", ID: asset.ID, Value: storedAsset})
	// The chunks are handed to the store as staging files: the sealed payloads
	// are read one by one inside the transaction, so the assembled attachment
	// never sits in RAM.
	chunks := make([]BlobChunk, 0, s.Parts)
	for index := 1; index <= s.Parts; index++ {
		chunks = append(chunks, BlobChunk{AssetID: s.AssetID, Seq: index - 1, Path: a.uploadPartPath(id, index)})
	}
	if err := a.store.PutBatchBlobChunks(records, nil, chunks); err != nil {
		jsonError(w, 500, "Could not persist the attachment")
		return
	}
	a.dropUploadSession(id)
	jsonResponse(w, 201, asset)
}

// readUploadHead decrypts only the beginning of the first part, for mime
// sniffing.
func (a *App) readUploadHead(s *uploadSession, n int) ([]byte, error) {
	sealed, err := os.ReadFile(a.uploadPartPath(s.ID, 1))
	if err != nil {
		return nil, err
	}
	plain, err := a.store.OpenBlobChunk(s.AssetID, sealed)
	if err != nil {
		return nil, err
	}
	if len(plain) > n {
		return plain[:n], nil
	}
	return plain, nil
}

// abortChunkedUpload serves POST /v1/assets/upload/{upload_id}/abort.
func (a *App) abortChunkedUpload(w http.ResponseWriter, r *http.Request) {
	a.sweepUploadSessions()
	id := r.PathValue("upload_id")
	a.uploadMu.Lock()
	_, ok := a.loadUploadSessionLocked(id)
	a.uploadMu.Unlock()
	if !ok {
		jsonError(w, 404, "Unknown upload")
		return
	}
	a.dropUploadSession(id)
	jsonResponse(w, 200, map[string]any{"aborted": true, "upload_id": id})
}
