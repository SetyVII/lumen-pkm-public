package main

import (
	"database/sql"
	"errors"
	"strings"
)

func mediaSourceID(o Object) string {
	id, _ := o.Properties["asset_id"].(string)
	if id == "" {
		id, _ = o.Properties["image_asset_id"].(string)
	}
	return id
}

func mediaMatchesObject(o Object, asset Asset) bool {
	return mediaType(asset.MIME) == o.TypeID
}

// Offline media creation is explicit about its outstanding upload. A document
// image node still requires the actual uploaded asset before it can commit.
func (a *App) validateMediaCreation(o *Object, allowPending bool) error {
	if o.TypeID != "image" && o.TypeID != "pdf" {
		return nil
	}
	id := mediaSourceID(*o)
	var asset Asset
	err := sql.ErrNoRows
	if id != "" {
		err = a.store.Get("asset", id, &asset)
	}
	if err == nil {
		if !mediaMatchesObject(*o, asset) {
			return errors.New("Media source has the wrong file type")
		}
		if o.Properties["media_state"] == "upload_pending" {
			o.Properties["media_state"] = "ready"
		}
		return nil
	}
	if !errors.Is(err, sql.ErrNoRows) {
		return errors.New("Could not read media source")
	}
	if o.TypeID == "pdf" && o.Properties["media_state"] == "draft" {
		return nil
	}
	if allowPending && o.Properties["media_state"] == "upload_pending" && id != "" && len(id) <= 128 && !strings.ContainsAny(id, " \t\r\n") {
		return nil
	}
	return errors.New("Insert a valid media file before creating this object; use the upload flow")
}
