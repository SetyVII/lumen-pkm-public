package main

import (
	"bytes"
	"mime"
	"net/http"
	"path/filepath"
	"strings"
)

// File names and declared types are hints only when the signature is unknown.
// In particular a renamed HTML/text file must not become an image or PDF.
func detectAssetMIME(data []byte, name, declared string) string {
	detected := http.DetectContentType(data)
	if bytes.HasPrefix(data, []byte("fLaC")) {
		return "audio/flac"
	}
	if strings.HasPrefix(detected, "application/ogg") {
		if bytes.Contains(data, []byte("theora")) {
			return "video/ogg"
		}
		return "audio/ogg"
	}
	if detected != "application/octet-stream" {
		return detected
	}
	for _, hint := range []string{declared, mime.TypeByExtension(strings.ToLower(filepath.Ext(name)))} {
		if kind, _, err := mime.ParseMediaType(hint); err == nil && kind != "application/octet-stream" && !strings.HasPrefix(kind, "image/") && kind != "application/pdf" {
			return kind
		}
	}
	return detected
}

func mediaType(mimeType string) string {
	kind, _, err := mime.ParseMediaType(mimeType)
	if err != nil || kind == "" {
		return "file"
	}
	switch {
	case kind == "application/pdf":
		return "pdf"
	case strings.HasPrefix(kind, "image/"):
		return "image"
	case strings.HasPrefix(kind, "audio/"):
		return "audio"
	case strings.HasPrefix(kind, "video/"):
		return "video"
	default:
		return "file"
	}
}
