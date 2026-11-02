package main

import (
	"database/sql"
	"encoding/json"
	"fmt"
	"time"
)

// Keep retry identities indefinitely, but not a second unbounded copy of every
// document. Expired HTTP replies require reload; expired sync replies retain
// the exact acknowledged revision and cannot replay their mutation.
const maxReceiptResultBytes = 64 << 20

func (s *Store) trackReceipt(tx *sql.Tx, kind, id string, payload []byte) error {
	if kind != "document_receipt" && kind != "sync_receipt" {
		return nil
	}
	raw, err := s.decrypt(payload, kind+"/"+id)
	if err != nil {
		return err
	}
	objectID := ""
	if kind == "document_receipt" {
		var receipt DocumentReceipt
		if err = json.Unmarshal(raw, &receipt); err != nil {
			return err
		}
		objectID = receipt.Object.ID
	} else {
		var receipt storedSyncReceipt
		if err = json.Unmarshal(raw, &receipt); err != nil {
			return err
		}
		objectID = receipt.Receipt.ObjectID
	}
	_, err = tx.Exec("INSERT INTO receipt_results(kind,id,object_id,created_at,expires_at,result_bytes) VALUES(?,?,?,?,?,?) ON CONFLICT(kind,id) DO UPDATE SET result_bytes=excluded.result_bytes", kind, id, objectID, now(), time.Now().UTC().Add(7*24*time.Hour).Format("2006-01-02T15:04:05.000000000Z"), len(payload))
	return err
}

func (s *Store) pruneReceiptResults(tx *sql.Tx) error {
	var total int64
	if err := tx.QueryRow("SELECT coalesce(sum(result_bytes),0) FROM receipt_results").Scan(&total); err != nil {
		return err
	}
	for {
		var kind, id string
		var size int64
		var sealed []byte
		err := tx.QueryRow(`SELECT r.kind,r.id,r.result_bytes,d.payload FROM receipt_results r JOIN records d ON d.kind=r.kind AND d.id=r.id WHERE r.result_bytes>0 AND (r.expires_at<? OR ?>?) ORDER BY r.created_at,r.kind,r.id LIMIT 1`, time.Now().UTC().Format("2006-01-02T15:04:05.000000000Z"), total, maxReceiptResultBytes).Scan(&kind, &id, &size, &sealed)
		if err == sql.ErrNoRows {
			return nil
		}
		if err != nil {
			return err
		}
		raw, err := s.decrypt(sealed, kind+"/"+id)
		if err != nil {
			return err
		}
		var compact any
		switch kind {
		case "document_receipt":
			var receipt DocumentReceipt
			if err = json.Unmarshal(raw, &receipt); err != nil {
				return err
			}
			receipt.Expired = true
			receipt.Object = Object{ID: receipt.Object.ID, Revision: receipt.Object.Revision}
			compact = receipt
		case "sync_receipt":
			var receipt storedSyncReceipt
			if err = json.Unmarshal(raw, &receipt); err != nil {
				return err
			}
			receipt.Receipt.Object = nil
			compact = receipt
		default:
			return fmt.Errorf("unknown receipt kind %s", kind)
		}
		raw, err = json.Marshal(compact)
		if err != nil {
			return err
		}
		sealed, err = s.encrypt(raw, kind+"/"+id)
		if err != nil {
			return err
		}
		if _, err = tx.Exec("UPDATE records SET payload=? WHERE kind=? AND id=?", sealed, kind, id); err != nil {
			return err
		}
		if _, err = tx.Exec("UPDATE receipt_results SET result_bytes=0 WHERE kind=? AND id=?", kind, id); err != nil {
			return err
		}
		total -= size
	}
}

// Expiration is checked on reads too: an idle workspace must not return an old
// full snapshot merely because no later write has triggered cache pruning.
func (s *Store) receiptResultExpired(kind, id string) (bool, error) {
	var expired bool
	err := s.db.QueryRow("SELECT result_bytes=0 OR expires_at<=? FROM receipt_results WHERE kind=? AND id=?", time.Now().UTC().Format("2006-01-02T15:04:05.000000000Z"), kind, id).Scan(&expired)
	return expired, err
}

func (s *Store) syncReceipt(id string) (storedSyncReceipt, error) {
	var receipt storedSyncReceipt
	if err := s.Get("sync_receipt", id, &receipt); err != nil {
		return receipt, err
	}
	expired, err := s.receiptResultExpired("sync_receipt", id)
	if expired {
		receipt.Receipt.Object = nil
	}
	return receipt, err
}

func (s *Store) documentReceipt(id string) (DocumentReceipt, error) {
	var receipt DocumentReceipt
	if err := s.Get("document_receipt", id, &receipt); err != nil {
		return receipt, err
	}
	expired, err := s.receiptResultExpired("document_receipt", id)
	receipt.Expired = receipt.Expired || expired
	return receipt, err
}
