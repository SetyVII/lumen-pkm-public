package main

import (
	"errors"
	"strings"
)

// Object merge (Capacities: reference/content-types#merge-two-objects).
//
// The object in the URL is the SOURCE: it is merged into the TARGET and then
// moved to the trash. The target keeps its identity, title and non-empty
// property values; empty properties are filled from the source, list
// properties are unioned, the source body is appended to the target body, and
// every reference to the source is rewritten to the target. The source is
// never hard-deleted: a restore brings back a (degraded) duplicate.

type mergeRequest struct {
	TargetID           string `json:"target_id"`
	BaseRevision       uint64 `json:"base_revision"`
	TargetBaseRevision uint64 `json:"target_base_revision"`
}

// rewriteReferenceList replaces from with to, drops empty/self references and
// deduplicates while keeping the original order.
func rewriteReferenceList(ids []string, from, to, self string) ([]string, bool) {
	out := make([]string, 0, len(ids))
	seen := make(map[string]bool, len(ids))
	changed := false
	for _, id := range ids {
		if id == from {
			id = to
			changed = true
		}
		if id == "" || id == self || seen[id] {
			changed = true
			continue
		}
		seen[id] = true
		out = append(out, id)
	}
	return out, changed
}

// rewriteObjectPropertyReferences rewrites the declared object relations of
// one object, plus the source_object_id provenance key. Hidden keys are left
// alone: only relations the index knows about are rewritten.
func rewriteObjectPropertyReferences(o *Object, schemas map[string]propertySchema, from, to, self string) bool {
	if o.Properties == nil {
		return false
	}
	changed := false
	for key, schema := range schemas {
		if schema.Kind != "object" {
			continue
		}
		value, exists := o.Properties[key]
		if !exists {
			continue
		}
		switch typed := value.(type) {
		case string:
			if typed != from {
				continue
			}
			changed = true
			if to == self {
				delete(o.Properties, key)
			} else {
				o.Properties[key] = to
			}
		case []string:
			rewritten, listChanged := rewriteReferenceList(typed, from, to, self)
			if !listChanged {
				continue
			}
			changed = true
			if len(rewritten) == 0 {
				delete(o.Properties, key)
				continue
			}
			list := make([]any, 0, len(rewritten))
			for _, id := range rewritten {
				list = append(list, id)
			}
			o.Properties[key] = list
		case []any:
			ids, ok := objectIDList(typed)
			if !ok {
				continue
			}
			rewritten, listChanged := rewriteReferenceList(ids, from, to, self)
			if !listChanged {
				continue
			}
			changed = true
			if len(rewritten) == 0 {
				delete(o.Properties, key)
				continue
			}
			list := make([]any, 0, len(rewritten))
			for _, id := range rewritten {
				list = append(list, id)
			}
			o.Properties[key] = list
		}
	}
	if id, ok := o.Properties["source_object_id"].(string); ok && id == from {
		changed = true
		if to == self {
			delete(o.Properties, "source_object_id")
		} else {
			o.Properties["source_object_id"] = to
		}
	}
	return changed
}

// appendBlockLists appends the source blocks to the target blocks. Block ids
// are client-owned and are preserved; a source id that already exists in the
// target (only possible after a duplicated object or a client bug) is replaced
// with a fresh id so block-level edits stay unambiguous.
func appendBlockLists(target, source []any) []any {
	out := make([]any, 0, len(target)+len(source))
	seen := make(map[string]bool, len(target)+len(source))
	for _, item := range target {
		out = append(out, cloneJSONValue(item))
		if entry, ok := item.(map[string]any); ok {
			if id, _ := entry["id"].(string); id != "" {
				seen[id] = true
			}
		}
	}
	for _, item := range source {
		entry, ok := item.(map[string]any)
		if !ok {
			out = append(out, cloneJSONValue(item))
			continue
		}
		clone, ok := cloneJSONValue(entry).(map[string]any)
		if !ok {
			out = append(out, item)
			continue
		}
		if id, _ := clone["id"].(string); id != "" {
			if seen[id] {
				id = newID()
				clone["id"] = id
			}
			seen[id] = true
		}
		out = append(out, clone)
	}
	return out
}

// unionRelationValues unions two object-relation values, normalizing a single
// id string to a one-element list first. It reuses mergeStringSlices so the
// semantics stay identical to the mobile conflict merge.
func unionRelationValues(current, incoming any) (any, bool) {
	left, ok := objectIDList(current)
	if !ok {
		return nil, false
	}
	right, ok := objectIDList(incoming)
	if !ok {
		return nil, false
	}
	return mergeStringSlices(left, right)
}

func assetList(value any) ([]any, bool) {
	if value == nil {
		return nil, true
	}
	list, ok := value.([]any)
	return list, ok
}

// mergeObjectData builds the whole merge transaction: the updated target, the
// rewritten referrers, the reassigned attachments, and the trash records for
// the source. Nothing is persisted here; the caller holds writeMu and commits
// every record in one PutBatch so the merge is atomic.
func (a *App) mergeObjectData(source, target Object) (Object, []Record, [][2]string, error) {
	typ, err := a.store.Type(target.TypeID)
	if err != nil {
		return target, nil, nil, errors.New("Unknown object type")
	}
	schemas := propertySchemasOf(typ)

	merged := cloneObject(target)
	if merged.Properties == nil {
		merged.Properties = map[string]any{}
	}

	// Attachments owned by the source move to the target. An asset that *is*
	// the source media object (when merging two images or PDFs) stays with the
	// source in the trash, so purging the source cannot delete the target's
	// data.
	assets, err := a.store.Assets()
	if err != nil {
		return target, nil, nil, err
	}
	movedAssets := map[string]bool{}
	assetRecords := []Record{}
	for _, asset := range assets {
		if asset.ObjectID != source.ID || asset.MediaObjectID == source.ID {
			continue
		}
		asset.ObjectID = target.ID
		movedAssets[asset.ID] = true
		assetRecords = append(assetRecords, Record{Kind: "asset", ID: asset.ID, Value: asset})
	}

	// aliases is a default property: union it like any other list.
	if value, ok := source.Properties[aliasesPropertyKey]; ok {
		if current, exists := merged.Properties[aliasesPropertyKey]; exists {
			if union, ok := mergeStringSlices(current, value); ok {
				merged.Properties[aliasesPropertyKey] = union
			}
		} else {
			merged.Properties[aliasesPropertyKey] = cloneJSONValue(value)
		}
	}

	// Attachment metadata: keep the target list and add the entries whose asset
	// was just reassigned. A malformed target value is preserved untouched.
	if targetAssets, ok := assetList(merged.Properties["assets"]); ok {
		union := make([]any, 0, len(targetAssets))
		seen := map[string]bool{}
		for _, item := range targetAssets {
			if entry, ok := item.(map[string]any); ok {
				if id, _ := entry["id"].(string); id != "" {
					if seen[id] {
						continue
					}
					seen[id] = true
				}
			}
			union = append(union, item)
		}
		if sourceAssets, ok := assetList(source.Properties["assets"]); ok {
			for _, item := range sourceAssets {
				entry, ok := item.(map[string]any)
				if !ok {
					continue
				}
				id, _ := entry["id"].(string)
				if id == "" || seen[id] || !movedAssets[id] {
					continue
				}
				seen[id] = true
				union = append(union, cloneJSONValue(entry))
			}
		}
		if len(union) > 0 {
			merged.Properties["assets"] = union
		} else {
			delete(merged.Properties, "assets")
		}
	}

	for _, key := range sortedStringKeys(source.Properties) {
		switch key {
		case aliasesPropertyKey, "assets", "blocks", "rich_text", "image_asset_id", "asset_id", "source_object_id":
			continue
		}
		value := source.Properties[key]
		if isEmptyPropertyValue(value) {
			continue
		}
		current, exists := merged.Properties[key]
		if exists && !isEmptyPropertyValue(current) {
			// The target keeps its value. Only declared multi-value relations
			// are unioned; a single value stays as it is.
			if schema, declared := schemas[key]; declared && schema.Kind == "object" && schema.Multiple {
				if union, ok := unionRelationValues(current, value); ok {
					merged.Properties[key] = union
				}
			}
			continue
		}
		merged.Properties[key] = cloneJSONValue(value)
	}

	// Single-value managed keys: the target keeps its own; an empty target
	// value is filled only when the referenced asset moved with the merge.
	for _, key := range []string{"image_asset_id", "asset_id"} {
		if current, exists := merged.Properties[key]; exists && !isEmptyPropertyValue(current) {
			continue
		}
		value, exists := source.Properties[key]
		if !exists || isEmptyPropertyValue(value) {
			continue
		}
		id, _ := value.(string)
		if id == "" || !movedAssets[id] {
			continue
		}
		merged.Properties[key] = id
	}
	if current, exists := merged.Properties["source_object_id"]; !exists || isEmptyPropertyValue(current) {
		if value, exists := source.Properties["source_object_id"]; exists && !isEmptyPropertyValue(value) {
			merged.Properties["source_object_id"] = cloneJSONValue(value)
		}
	}

	// Blocks are an optional richer representation of the body. They are
	// appended only when both objects have them; otherwise they are dropped so
	// no client can render a stale view that hides the appended text. The
	// plain-text content is always canonical.
	contentChanged := strings.TrimSpace(source.Content) != ""
	blocksMerged := false
	if targetBlocks, ok := merged.Properties["blocks"].([]any); ok {
		if sourceBlocks, ok := source.Properties["blocks"].([]any); ok && len(sourceBlocks) > 0 {
			merged.Properties["blocks"] = appendBlockLists(targetBlocks, sourceBlocks)
			blocksMerged = true
		}
	}
	if contentChanged && !blocksMerged {
		delete(merged.Properties, "blocks")
	}
	if contentChanged || blocksMerged {
		delete(merged.Properties, "rich_text")
	}
	if contentChanged {
		body := strings.Trim(source.Content, "\n")
		if strings.TrimSpace(merged.Content) == "" {
			merged.Content = body
		} else {
			merged.Content = strings.TrimRight(merged.Content, "\n") + "\n\n" + body
		}
	}

	// The target keeps its outgoing links and adopts the source's, then its own
	// references to the source become self-references and are dropped.
	merged.Links, _ = rewriteReferenceList(mergeLinks(merged.Links, source.Links), source.ID, target.ID, target.ID)
	rewriteObjectPropertyReferences(&merged, schemas, source.ID, target.ID, target.ID)

	// Rewrite every live object that pointed at the source. Same-type
	// enforcement guarantees the target still satisfies the referrer's
	// target_type, so validation cannot reject the rewritten relation.
	referrerIDs := a.store.BacklinkIDs(source.ID)
	others := make([]Object, 0, len(referrerIDs))
	for _, referrerID := range referrerIDs {
		if referrerID == source.ID || referrerID == target.ID {
			continue
		}
		other, err := a.store.Object(referrerID)
		if err != nil {
			continue
		}
		otherSchemas := schemas
		if other.TypeID != target.TypeID {
			otherType, err := a.store.Type(other.TypeID)
			if err != nil {
				continue
			}
			otherSchemas = propertySchemasOf(otherType)
		}
		links, linksChanged := rewriteReferenceList(other.Links, source.ID, target.ID, other.ID)
		propertiesChanged := rewriteObjectPropertyReferences(&other, otherSchemas, source.ID, target.ID, other.ID)
		if !linksChanged && !propertiesChanged {
			continue
		}
		if linksChanged {
			other.Links = links
		}
		other.UpdatedAt = now()
		others = append(others, other)
	}

	if err := a.validateObject(&merged); err != nil {
		return target, nil, nil, err
	}
	for i := range others {
		if err := a.validateObject(&others[i]); err != nil {
			return target, nil, nil, err
		}
	}

	records := a.prepareSave(&merged)
	for i := range others {
		records = append(records, a.prepareSave(&others[i])...)
	}
	records = append(records, assetRecords...)
	tombRecords, deletes, _, err := a.trashObjectData(source.ID)
	if err != nil {
		return target, nil, nil, err
	}
	records = append(records, tombRecords...)
	return merged, records, deletes, nil
}
