package main

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"strings"
	"testing"
)

func TestMediaClassificationAcrossUploadAndSync(t *testing.T) {
	fixtures := []struct {
		name, kind string
		data       []byte
	}{
		{"song.mp3", "audio", []byte{'I', 'D', '3', 4, 0, 0, 0, 0, 0, 0, 0xff, 0xfb}},
		{"recording.flac", "audio", append([]byte("fLaC"), bytes.Repeat([]byte{0}, 20)...)},
		{"clip.mp4", "video", []byte{0, 0, 0, 24, 'f', 't', 'y', 'p', 'm', 'p', '4', '2', 0, 0, 0, 0, 'm', 'p', '4', '2', 0, 0, 0, 0}},
		{"page.html", "file", []byte("<!DOCTYPE html><script>unsafe()</script>")},
		{"lying.png", "file", []byte("plain text with an image filename")},
	}
	for _, f := range fixtures {
		t.Run(f.name, func(t *testing.T) {
			app, _, _ := makeApp(t)
			uploaded := uploadFixture(t, app, "", f.name, f.data)
			if uploaded.MediaObject == nil || uploaded.MediaObject.TypeID != f.kind {
				t.Fatalf("wrong upload kind: %+v", uploaded)
			}
			download := api(t, app, "GET", "/v1/assets/"+uploaded.ID+"/content", nil)
			if download.Code != 200 || !bytes.Equal(download.Body.Bytes(), f.data) || download.Header().Get("X-Content-Type-Options") != "nosniff" {
				t.Fatal("download changed bytes or omitted nosniff")
			}
			if f.kind == "file" && !strings.HasPrefix(download.Header().Get("Content-Disposition"), "attachment;") {
				t.Fatal("generic file served inline")
			}
			synced := decodeBody[syncPushResponse](t, api(t, app, "POST", "/v1/sync/push", map[string]any{"device_id": "new-media-test", "operations": []any{map[string]any{"op_id": "sync-" + f.name, "action": "asset", "asset": map[string]any{"id": newID(), "name": f.name, "mime": "image/png", "data_base64": base64.StdEncoding.EncodeToString(f.data), "create_media_object": true}}}}))
			if len(synced.Errors) != 0 || len(synced.Applied) != 1 {
				t.Fatalf("sync media rejected: %+v", synced)
			}
			objects, err := app.store.Objects()
			if err != nil {
				t.Fatal(err)
			}
			if len(objects) != 2 || objects[0].TypeID != f.kind || objects[1].TypeID != f.kind {
				t.Fatalf("sync/upload classification diverged: %+v", objects)
			}
		})
	}
}

func TestChunkedAudioAndFileRecognition(t *testing.T) {
	app, _, _ := makeUploadApp(t)
	data := append([]byte("fLaC"), bytes.Repeat([]byte{0}, 100)...)
	init, _ := chunkedInit(t, app, map[string]any{"filename": "recording.flac", "mime": "audio/flac", "size": len(data)})
	id := init["upload_id"].(string)
	if rr := uploadPart(t, app, id, 1, data); rr.Code != 200 {
		t.Fatal(rr.Body.String())
	}
	rr := api(t, app, "POST", "/v1/assets/upload/"+id+"/complete", nil)
	if rr.Code != 201 {
		t.Fatal(rr.Body.String())
	}
	var asset Asset
	if err := json.Unmarshal(rr.Body.Bytes(), &asset); err != nil {
		t.Fatal(err)
	}
	if asset.MediaObject == nil || asset.MediaObject.TypeID != "audio" || asset.MIME != "audio/flac" {
		t.Fatalf("chunked audio misclassified: %+v", asset)
	}
	if rr := api(t, app, "GET", "/v1/assets/"+asset.ID+"/content", nil); !bytes.Equal(rr.Body.Bytes(), data) {
		t.Fatal("chunked audio changed bytes")
	}
}
