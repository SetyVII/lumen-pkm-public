package main

import (
	"errors"
	"fmt"
	"strings"
)

const (
	aliasesPropertyKey  = "aliases"
	maxAliasesPerObject = 50
	maxAliasLength      = 200
)

// reservedPropertyKey reports whether a type schema may redefine a key the
// server owns as a default property. Type schemas are not rejected for
// declaring it (that would break existing workspaces), but the server keeps
// the final word on its shape.
func reservedPropertyKey(key string) bool {
	return key == aliasesPropertyKey
}

// normalizeAliases validates and canonicalizes properties["aliases"] in place.
// It trims surrounding spaces, drops empty entries and deduplicates
// case-insensitively keeping the first spelling. An empty result removes the
// key so "no aliases" has a single representation. Limits are enforced instead
// of silently truncating: losing a user-provided name is worse than a 400.
func normalizeAliases(properties map[string]any) error {
	if properties == nil {
		return nil
	}
	raw, exists := properties[aliasesPropertyKey]
	if !exists {
		return nil
	}
	values, err := aliasStrings(raw)
	if err != nil {
		return err
	}
	// Keep the JSON-native []any shape: every list property decoded from a
	// request or from disk looks like this, and the no-op PATCH check compares
	// property maps with reflect.DeepEqual.
	out := make([]any, 0, len(values))
	seen := make(map[string]bool, len(values))
	for _, value := range values {
		alias := strings.TrimSpace(value)
		if alias == "" {
			continue
		}
		if len([]rune(alias)) > maxAliasLength {
			return fmt.Errorf("Each alias supports up to %d characters", maxAliasLength)
		}
		key := strings.ToLower(alias)
		if seen[key] {
			continue
		}
		seen[key] = true
		out = append(out, alias)
	}
	if len(out) > maxAliasesPerObject {
		return fmt.Errorf("An object supports up to %d aliases", maxAliasesPerObject)
	}
	if len(out) == 0 {
		delete(properties, aliasesPropertyKey)
		return nil
	}
	properties[aliasesPropertyKey] = out
	return nil
}

// aliasStrings decodes the stored alias list. A bare string is rejected on
// purpose: accepting it would hide a client that confuses aliases with a text
// property, and the response would not round-trip.
func aliasStrings(raw any) ([]string, error) {
	switch value := raw.(type) {
	case nil:
		return nil, nil
	case []string:
		return append([]string(nil), value...), nil
	case []any:
		out := make([]string, 0, len(value))
		for _, entry := range value {
			text, ok := entry.(string)
			if !ok {
				return nil, errors.New("Property aliases must be an array of strings")
			}
			out = append(out, text)
		}
		return out, nil
	default:
		return nil, errors.New("Property aliases must be an array of strings")
	}
}

// objectAliases returns the stored aliases for read paths. The write path has
// already normalized them; this only tolerates legacy or hand-edited payloads.
func objectAliases(o Object) []string {
	values, err := aliasStrings(o.Properties[aliasesPropertyKey])
	if err != nil || len(values) == 0 {
		return nil
	}
	out := make([]string, 0, len(values))
	for _, value := range values {
		if value = strings.TrimSpace(value); value != "" {
			out = append(out, value)
		}
	}
	return out
}

// matchAlias returns the first stored alias containing the already lowercased
// query, or "" when no alias matches.
func matchAlias(o Object, query string) string {
	for _, alias := range objectAliases(o) {
		if strings.Contains(strings.ToLower(alias), query) {
			return alias
		}
	}
	return ""
}

// objectMatchesQuery is the shared text filter for GET /v1/objects and
// GET /v1/search: case-insensitive substring over title, content and aliases.
func objectMatchesQuery(o Object, query string) bool {
	if strings.Contains(strings.ToLower(o.Title+" "+o.Content), query) {
		return true
	}
	// Weblink reader articles are searchable like any other body text.
	if reader, _ := o.Properties["reader_content"].(string); reader != "" {
		if strings.Contains(strings.ToLower(reader), query) {
			return true
		}
	}
	return matchAlias(o, query) != ""
}
