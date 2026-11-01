package main

import (
	"encoding/json"
	"sort"
	"strings"
	"sync"
	"time"
)

// ObjectMeta is the small, non-content part of an object kept in RAM. Content,
// rich text and properties stay encrypted in SQLite and are decrypted on
// demand; blobs (PDFs, images) never enter this index.
type ObjectMeta struct {
	ID            string
	TypeID        string
	Title         string
	Aliases       []string
	IsTemplate    bool
	CreatedAt     string
	UpdatedAt     string
	Revision      uint64
	Incarnation   string
	Links         []string
	PropertyLinks []string
}

func metaFromObject(o Object) ObjectMeta {
	meta := ObjectMeta{
		ID: o.ID, TypeID: o.TypeID, Title: o.Title, Aliases: objectAliases(o),
		IsTemplate: isTemplateObject(o),
		CreatedAt:  o.CreatedAt, UpdatedAt: o.UpdatedAt,
		Revision: o.Revision, Incarnation: o.Incarnation,
	}
	if o.Links != nil {
		meta.Links = append([]string{}, o.Links...)
	} else {
		meta.Links = []string{}
	}
	if len(o.PropertyLinks) > 0 {
		meta.PropertyLinks = append([]string{}, o.PropertyLinks...)
	}
	return meta
}

func cloneMeta(meta ObjectMeta) ObjectMeta {
	out := meta
	if meta.Aliases != nil {
		out.Aliases = append([]string{}, meta.Aliases...)
	}
	if meta.Links != nil {
		out.Links = append([]string{}, meta.Links...)
	}
	if meta.PropertyLinks != nil {
		out.PropertyLinks = append([]string{}, meta.PropertyLinks...)
	}
	return out
}

// memIndex keeps only metadata, types and the reverse relation map in RAM.
// SQLite remains the source of truth for every payload.
type memIndex struct {
	mu           sync.RWMutex
	objects      map[string]ObjectMeta
	ordered      []string
	byType       map[string][]string
	backlinkSets map[string]map[string]struct{}
	types        map[string]ObjectType
	titles       map[string]string
}

func newMemIndex() *memIndex {
	return &memIndex{
		objects:      map[string]ObjectMeta{},
		byType:       map[string][]string{},
		backlinkSets: map[string]map[string]struct{}{},
		types:        map[string]ObjectType{},
		titles:       map[string]string{},
	}
}

func (m *memIndex) rebuild(objects []Object, types []ObjectType) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.objects = make(map[string]ObjectMeta, len(objects))
	m.byType = map[string][]string{}
	m.backlinkSets = map[string]map[string]struct{}{}
	m.types = make(map[string]ObjectType, len(types))
	m.titles = map[string]string{}
	m.ordered = make([]string, 0, len(objects))
	for _, o := range objects {
		meta := metaFromObject(o)
		m.objects[meta.ID] = meta
		m.ordered = append(m.ordered, meta.ID)
		m.addRelationsLocked(meta)
	}
	sort.Slice(m.ordered, func(i, j int) bool {
		return metaLess(m.objects[m.ordered[i]], m.objects[m.ordered[j]])
	})
	for _, t := range types {
		m.types[t.ID] = cloneType(t)
	}
}

func (m *memIndex) getMeta(id string) (ObjectMeta, bool) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	meta, ok := m.objects[id]
	if !ok {
		return ObjectMeta{}, false
	}
	return cloneMeta(meta), true
}

func (m *memIndex) put(o Object) {
	m.putMeta(metaFromObject(o))
}

func (m *memIndex) putMeta(meta ObjectMeta) {
	m.mu.Lock()
	defer m.mu.Unlock()
	meta = cloneMeta(meta)
	if old, ok := m.objects[meta.ID]; ok {
		m.removeLocked(old)
	}
	m.objects[meta.ID] = meta
	m.addRelationsLocked(meta)
	pos := sort.Search(len(m.ordered), func(i int) bool {
		return metaLess(meta, m.objects[m.ordered[i]])
	})
	m.ordered = append(m.ordered, "")
	copy(m.ordered[pos+1:], m.ordered[pos:])
	m.ordered[pos] = meta.ID
}

func (m *memIndex) deleteObject(id string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	old, ok := m.objects[id]
	if !ok {
		return
	}
	m.removeLocked(old)
}

func (m *memIndex) putType(t ObjectType) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.types[t.ID] = cloneType(t)
}

func (m *memIndex) deleteType(id string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	delete(m.types, id)
}

func (m *memIndex) typeByID(id string) (ObjectType, bool) {
	m.mu.RLock()
	defer m.mu.RUnlock()
	t, ok := m.types[id]
	if !ok {
		return ObjectType{}, false
	}
	return cloneType(t), true
}

func (m *memIndex) listTypes() []ObjectType {
	m.mu.RLock()
	defer m.mu.RUnlock()
	out := make([]ObjectType, 0, len(m.types))
	for _, t := range m.types {
		out = append(out, cloneType(t))
	}
	sort.SliceStable(out, func(i, j int) bool { return out[i].Name < out[j].Name })
	return out
}

func (m *memIndex) idsByType(typeID string) []string {
	m.mu.RLock()
	defer m.mu.RUnlock()
	ids := append([]string(nil), m.byType[typeID]...)
	sort.Slice(ids, func(i, j int) bool {
		return metaLess(m.objects[ids[i]], m.objects[ids[j]])
	})
	return ids
}

func (m *memIndex) backlinkIDs(id string) []string {
	return m.backlinkIDsFiltered(id, true)
}

func (m *memIndex) backlinkIDsFiltered(id string, includeTemplates bool) []string {
	m.mu.RLock()
	defer m.mu.RUnlock()
	set := m.backlinkSets[id]
	if len(set) == 0 {
		return []string{}
	}
	ids := make([]string, 0, len(set))
	for source := range set {
		meta, ok := m.objects[source]
		if !ok {
			continue
		}
		if !includeTemplates && meta.IsTemplate {
			continue
		}
		ids = append(ids, source)
	}
	sort.Slice(ids, func(i, j int) bool {
		return metaLess(m.objects[ids[i]], m.objects[ids[j]])
	})
	return ids
}

func (m *memIndex) titleOwner(title string) string {
	m.mu.RLock()
	defer m.mu.RUnlock()
	return m.titles[strings.ToLower(strings.TrimSpace(title))]
}

func (m *memIndex) titleExists(title string) bool {
	return m.titleOwner(title) != ""
}

func (m *memIndex) addRelationsLocked(meta ObjectMeta) {
	if meta.TypeID != "" {
		m.byType[meta.TypeID] = append(m.byType[meta.TypeID], meta.ID)
	}
	// Template names are not note titles: they must not block creating a real
	// object with the resolved template title (including the template itself
	// when the title has no date variables).
	if title := strings.ToLower(strings.TrimSpace(meta.Title)); title != "" && !meta.IsTemplate {
		m.titles[title] = meta.ID
	}
	for _, target := range allObjectLinks(Object{Links: meta.Links, PropertyLinks: meta.PropertyLinks}) {
		if target == "" || target == meta.ID {
			continue
		}
		set := m.backlinkSets[target]
		if set == nil {
			set = map[string]struct{}{}
			m.backlinkSets[target] = set
		}
		set[meta.ID] = struct{}{}
	}
}

func (m *memIndex) removeLocked(meta ObjectMeta) {
	delete(m.objects, meta.ID)
	if list := m.byType[meta.TypeID]; len(list) > 0 {
		clean := list[:0]
		for _, id := range list {
			if id != meta.ID {
				clean = append(clean, id)
			}
		}
		if len(clean) == 0 {
			delete(m.byType, meta.TypeID)
		} else {
			m.byType[meta.TypeID] = clean
		}
	}
	if title := strings.ToLower(strings.TrimSpace(meta.Title)); title != "" && m.titles[title] == meta.ID {
		delete(m.titles, title)
	}
	for _, target := range allObjectLinks(Object{Links: meta.Links, PropertyLinks: meta.PropertyLinks}) {
		if set := m.backlinkSets[target]; set != nil {
			delete(set, meta.ID)
			if len(set) == 0 {
				delete(m.backlinkSets, target)
			}
		}
	}
	for i, id := range m.ordered {
		if id == meta.ID {
			m.ordered = append(m.ordered[:i], m.ordered[i+1:]...)
			break
		}
	}
}

func sameStrings(a, b []string) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i] != b[i] {
			return false
		}
	}
	return true
}

func metaLess(a, b ObjectMeta) bool {
	if a.UpdatedAt != b.UpdatedAt {
		ta, ea := time.Parse(time.RFC3339Nano, a.UpdatedAt)
		tb, eb := time.Parse(time.RFC3339Nano, b.UpdatedAt)
		if ea == nil && eb == nil && !ta.Equal(tb) {
			return ta.After(tb)
		}
		return a.UpdatedAt > b.UpdatedAt
	}
	return a.ID < b.ID
}

func objectLess(a, b Object) bool {
	return metaLess(metaFromObject(a), metaFromObject(b))
}

func cloneObject(o Object) Object {
	out := o
	out.Properties = cloneStringMap(o.Properties)
	if o.Links != nil {
		out.Links = append([]string{}, o.Links...)
	}
	if len(o.PropertyLinks) > 0 {
		out.PropertyLinks = append([]string{}, o.PropertyLinks...)
	} else {
		out.PropertyLinks = nil
	}
	return out
}

func cloneType(t ObjectType) ObjectType {
	out := t
	if t.Properties != nil {
		out.Properties = make([]any, len(t.Properties))
		for i, raw := range t.Properties {
			out.Properties[i] = cloneJSONValue(raw)
		}
	}
	return out
}

func cloneStringMap(in map[string]any) map[string]any {
	if in == nil {
		return nil
	}
	out := make(map[string]any, len(in))
	for key, value := range in {
		out[key] = cloneJSONValue(value)
	}
	return out
}

func cloneJSONValue(value any) any {
	switch v := value.(type) {
	case nil, string, bool, float64:
		return v
	case int:
		return float64(v)
	case int64:
		return float64(v)
	case json.Number:
		if number, err := v.Float64(); err == nil {
			return number
		}
		return v
	case map[string]any:
		return cloneStringMap(v)
	case []any:
		out := make([]any, len(v))
		for i, item := range v {
			out[i] = cloneJSONValue(item)
		}
		return out
	case []string:
		return append([]string(nil), v...)
	default:
		raw, err := json.Marshal(v)
		if err != nil {
			return v
		}
		var out any
		if json.Unmarshal(raw, &out) != nil {
			return v
		}
		return out
	}
}
