package main

import (
	"encoding/json"
	"fmt"
	"strings"
	"time"
)

// Plain-text API edits must not retain a stale richer representation that a
// client would prefer when rendering. Explicitly supplied native formats remain
// the client's current representation; absent formats are invalidated per key.
func invalidateImplicitFormats(o *Object, previousContent string, patch map[string]json.RawMessage) {
	if _, present := patch["content"]; !present || o.Content == previousContent {
		return
	}
	var explicit map[string]json.RawMessage
	if raw, present := patch["properties"]; present {
		_ = json.Unmarshal(raw, &explicit) // the main decoder already validated it
	}
	for _, key := range []string{"blocks", "rich_text"} {
		if _, provided := explicit[key]; !provided {
			delete(o.Properties, key)
		}
	}
}

// Journal deltas carry only new native blocks, never a replacement snapshot.
// Keep existing JSON maps verbatim so image dimensions and future metadata survive.
func mergeMobileJournalBlocks(existing, incoming Object, merged *Object, stamp time.Time) error {
	if merged.Content == existing.Content || strings.TrimSpace(incoming.Content) == "" {
		return nil
	}
	oldRaw, oldPresent := existing.Properties["blocks"]
	newRaw, newPresent := incoming.Properties["blocks"]
	if (!oldPresent || oldRaw == nil) && (!newPresent || newRaw == nil) {
		return nil
	}
	decode := func(raw any, fallback string) ([]any, error) {
		if raw != nil {
			data, err := json.Marshal(raw)
			if err != nil {
				return nil, err
			}
			var blocks []any
			if err := json.Unmarshal(data, &blocks); err != nil {
				return nil, fmt.Errorf("invalid journal blocks: %w", err)
			}
			return blocks, nil
		}
		if strings.TrimSpace(fallback) == "" {
			return nil, nil
		}
		parsed, err := MarkdownToBlocks(fallback)
		if err != nil {
			return nil, err
		}
		data, err := json.Marshal(parsed)
		if err != nil {
			return nil, err
		}
		var blocks []any
		err = json.Unmarshal(data, &blocks)
		return blocks, err
	}
	prior, err := decode(oldRaw, existing.Content)
	if err != nil {
		return err
	}
	added, err := decode(newRaw, incoming.Content)
	if err != nil {
		return err
	}
	if len(prior) > 0 {
		marker := fmt.Sprintf("> [Edición desde móvil · %s]", stamp.Format("15:04"))
		for _, text := range []string{"", marker} {
			prior = append(prior, map[string]any{"id": newID(), "type": "paragraph", "text": text, "runs": []any{map[string]any{"text": text}}})
		}
	}
	merged.Properties["blocks"] = append(prior, added...)
	delete(merged.Properties, "rich_text")
	return nil
}
