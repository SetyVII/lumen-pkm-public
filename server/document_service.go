package main

import (
	"crypto/sha256"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"reflect"
	"strings"

	"lumen/server/internal/document"
)

// DocumentReceipt is encrypted and committed with its object and version.
// It records the actual reply, so retries do not acknowledge a later writer.
type DocumentReceipt struct {
	Expired bool   `json:"expired,omitempty"`
	Digest  string `json:"digest"`
	Object  Object `json:"object"`
}

func documentRequestDigest(id string, payload any) string {
	raw, _ := json.Marshal(struct {
		ID      string
		Payload any
	}{id, payload})
	sum := sha256.Sum256(raw)
	return hex.EncodeToString(sum[:])
}

func cloneDocument(d *document.Document) *document.Document {
	if d == nil {
		return nil
	}
	raw, err := json.Marshal(d)
	if err != nil {
		return nil
	}
	var out document.Document
	if json.Unmarshal(raw, &out) != nil {
		return nil
	}
	return &out
}

func (a *App) normalizeObjectDocument(o *Object) error {
	if o.Document == nil {
		imported, err := canonicalDocumentFromLegacy(*o)
		if err != nil {
			return fmt.Errorf("document import: %w", err)
		}
		o.Document = &imported
	}
	normalized, err := document.Normalize(*o.Document)
	if err != nil {
		return fmt.Errorf("document: %w", err)
	}
	links := []string{}
	for _, ref := range document.References(normalized) {
		if ref.ObjectID != "" && ref.ObjectID != o.ID && !containsString(links, ref.ObjectID) {
			// Missing sources remain visibly unresolved. They may arrive in the next
			// offline sync operation; preserving identity is better than deleting it.
			links = append(links, ref.ObjectID)
		}
		if ref.AssetID != "" {
			var asset Asset
			if err := a.store.Get("asset", ref.AssetID, &asset); err != nil {
				return fmt.Errorf("document: asset %s does not exist", ref.AssetID)
			}
			if !strings.HasPrefix(asset.MIME, "image/") {
				return fmt.Errorf("document: asset %s is not an image", ref.AssetID)
			}
		}
	}
	o.Document = &normalized
	o.DocumentLinks = links
	o.Content = document.PlainText(normalized)
	delete(o.Properties, "blocks")
	delete(o.Properties, "rich_text")
	return nil
}

// Last storage boundary for server-created objects (journals, tasks, media).
// It also prevents a producer bypassing HTTP validation from persisting an
// obsolete Qt representation as a second source of truth.
func canonicalizeStoredObject(o *Object) error {
	d, err := canonicalDocumentFromLegacy(*o)
	if err != nil {
		return err
	}
	o.Document = &d
	o.Content = document.PlainText(d)
	o.DocumentLinks = nil
	for _, ref := range document.References(d) {
		if ref.ObjectID != "" && ref.ObjectID != o.ID && !containsString(o.DocumentLinks, ref.ObjectID) {
			o.DocumentLinks = append(o.DocumentLinks, ref.ObjectID)
		}
	}
	delete(o.Properties, "blocks")
	delete(o.Properties, "rich_text")
	return nil
}

// Must be called under writeMu. No HTTP handler or sync worker can bypass
// the same document validator, revision check and transactional receipt.
func (a *App) commitDocument(id string, base uint64, opID string, next *document.Document, commands []document.Command) (Object, error) {
	if len(opID) < 8 || len(opID) > 128 || strings.TrimSpace(opID) != opID {
		return Object{}, &httpError{400, "op_id must contain 8..128 non-padding characters"}
	}
	if base == 0 {
		return Object{}, &httpError{400, "base_revision is required"}
	}
	payload, _ := json.Marshal(struct {
		ID       string
		Base     uint64
		Document *document.Document
		Commands []document.Command
	}{id, base, next, commands})
	sum := sha256.Sum256(payload)
	digest := hex.EncodeToString(sum[:])
	if receipt, err := a.store.documentReceipt(opID); err == nil {
		if receipt.Digest != digest {
			return Object{}, &httpError{409, "op_id was already used with a different request"}
		}
		if _, err := a.store.Object(receipt.Object.ID); err != nil {
			return Object{}, errNotFoundHTTP("Object not found")
		}
		if receipt.Expired {
			return Object{}, &httpError{410, "Operation was committed; receipt expired. Reload the object instead of replaying the edit"}
		}
		return cloneObject(receipt.Object), nil
	} else if !errors.Is(err, sql.ErrNoRows) { // SQL errors must not be treated as missing receipts.
		return Object{}, errPersistHTTP("Could not read document receipt")
	}
	current, err := a.store.Object(id)
	if err != nil {
		return Object{}, errNotFoundHTTP("Object not found")
	}
	if current.Revision != base {
		return current, &httpError{409, "conflict"}
	}
	if next == nil {
		if current.Document == nil {
			return current, &httpError{409, "Object has no canonical document"}
		}
		applied, err := document.Apply(*current.Document, commands)
		if err != nil {
			return current, &httpError{400, err.Error()}
		}
		next = &applied
	}
	updated := cloneObject(current)
	updated.Document = cloneDocument(next)
	// A body-only edit preserves current metadata, including unresolved offline
	// relationships. Do not revalidate unrelated property constraints here.
	if err := a.normalizeObjectDocument(&updated); err != nil {
		return current, &httpError{400, err.Error()}
	}
	records := []Record{}
	if !reflect.DeepEqual(current.Document, updated.Document) || !sameStrings(current.DocumentLinks, updated.DocumentLinks) {
		updated.UpdatedAt = now()
		records = a.prepareSave(&updated)
	}
	records = append(records, Record{Kind: "document_receipt", ID: opID, Value: DocumentReceipt{Digest: digest, Object: updated}})
	if err := a.store.PutBatch(records, nil); err != nil {
		return current, errPersistHTTP("Could not persist document")
	}
	return updated, nil
}
