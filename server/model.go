package main

import (
	"lumen/server/internal/document"
	"time"
)

type Object struct {
	Document      *document.Document `json:"document,omitempty"`
	DocumentLinks []string           `json:"document_links,omitempty"`
	ID            string             `json:"id"`
	TypeID        string             `json:"type_id"`
	Title         string             `json:"title"`
	Content       string             `json:"content"`
	Properties    map[string]any     `json:"properties"`
	Links         []string           `json:"links"`
	PropertyLinks []string           `json:"property_links,omitempty"`
	CreatedAt     string             `json:"created_at"`
	UpdatedAt     string             `json:"updated_at"`
	SyncStatus    string             `json:"sync_status,omitempty"`
	SyncError     string             `json:"sync_error,omitempty"`
	Revision      uint64             `json:"revision,omitempty"`
	Incarnation   string             `json:"incarnation,omitempty"`
}

type ObjectType struct {
	ID                string `json:"id"`
	Name              string `json:"name"`
	Icon              string `json:"icon"`
	Color             string `json:"color"`
	Properties        []any  `json:"properties"`
	DefaultTemplateID string `json:"default_template_id,omitempty"`
}

type Asset struct {
	ID            string  `json:"id"`
	ObjectID      string  `json:"object_id"`
	Name          string  `json:"name"`
	MIME          string  `json:"mime"`
	Size          int     `json:"size"`
	CreatedAt     string  `json:"created_at"`
	MediaObjectID string  `json:"media_object_id,omitempty"`
	MediaObject   *Object `json:"media_object,omitempty"`
}

func now() string { return time.Now().UTC().Format("2006-01-02T15:04:05.000000000Z") }

// tagsProperty is a typed relation shared by content types. Definitions remain
// per type: adding this map to a type does not merge unrelated schemas such as
// the different "status" properties used by book, project, task and web.
var tagsProperty = map[string]any{
	"key":         "tags",
	"name":        "Etiquetas",
	"type":        "object",
	"target_type": "tag",
	"multiple":    true,
}

// isTemplateProperty is a reserved boolean present on every type. Templates are
// ordinary objects of their type with this flag set; absent means false.
var isTemplateProperty = map[string]any{
	"key":     "is_template",
	"name":    "Plantilla",
	"type":    "boolean",
	"default": false,
}

func withTemplate(properties ...any) []any {
	out := make([]any, 0, len(properties)+1)
	out = append(out, properties...)
	out = append(out, isTemplateProperty)
	return out
}

// collectionProperties hold a curated, ordered list of objects of exactly one
// type. `item_type_id` is validated and made immutable by validateCollection;
// `items` rides on the same object-relation machinery as tags, so every item
// also becomes a property link and therefore a backlink.
var collectionProperties = []any{
	map[string]any{"key": "item_type_id", "name": "Item type", "type": "text"},
	map[string]any{"key": "items", "name": "Items", "type": "object", "multiple": true},
}

func withTags(properties ...any) []any {
	return withTemplate(append(properties, tagsProperty)...)
}

var defaultTypes = []ObjectType{
	{ID: "habit", Name: "Habit", Icon: "circle-check", Color: "#89a093", Properties: withTags("habit_start", "habit_schedule", "habit_target", "habit_entries")},
	{ID: "note", Name: "Nota", Icon: "text-x-generic", Color: "#6d8d9b", Properties: withTags()},
	{ID: "page", Name: "Página", Icon: "file-text", Color: "#6d8d9b", Properties: withTags()},
	{ID: "atomic", Name: "Nota atómica", Icon: "file-text", Color: "#d97706", Properties: withTags()},
	{ID: "idea", Name: "Idea", Icon: "dialog-information", Color: "#98a37a", Properties: withTags()},
	{ID: "book", Name: "Libro", Icon: "accessories-dictionary", Color: "#b19160", Properties: withTags("author", "rating", "recommended_by", "medium", "status")},
	{ID: "person", Name: "Persona", Icon: "avatar-default", Color: "#aa7985", Properties: withTags()},
	{ID: "project", Name: "Proyecto", Icon: "folder", Color: "#7c90b9", Properties: withTags("status")},
	{ID: "task", Name: "Tarea", Icon: "circle-check", Color: "#e07a5f", Properties: withTags("status", "due_date", "context_id")},
	{ID: "quote", Name: "Cita", Icon: "quote", Color: "#8b5cf6", Properties: withTags("author", "source")},
	{ID: "tag", Name: "Etiqueta", Icon: "tag", Color: "#64748b", Properties: withTemplate()},
	{ID: "podcast", Name: "Podcast", Icon: "microphone", Color: "#a855f7", Properties: withTags("host", "url")},
	{ID: "tweet", Name: "Tweet", Icon: "brand-twitter", Color: "#38bdf8", Properties: withTags("author", "url")},
	{ID: "audio", Name: "Audio", Icon: "music", Color: "#f43f5e", Properties: withTags("duration", "mime")},
	{ID: "file", Name: "Archivo", Icon: "file", Color: "#94a3b8", Properties: withTags("mime")},
	{ID: "web", Name: "Enlace web", Icon: "web-browser", Color: "#7e9f88", Properties: withTags("url", "status", "category", "topic")},
	{ID: "video", Name: "Vídeo", Icon: "video-x-generic", Color: "#c2410c", Properties: withTags("url", "duration", "mime")},
	// Daily notes are content too: tagging a journal must produce the same tag
	// relation (and therefore the same tag-page/backlink behavior) as any other
	// content type. The property is added to existing workspaces by the
	// additive startup normalization in NewApp.
	{ID: "journal", Name: "Diario", Icon: "view-calendar-day", Color: "#89a093", Properties: withTags("date")},
	{ID: "image", Name: "Imagen", Icon: "image-x-generic", Color: "#a48baf", Properties: withTags("mime")},
	{ID: "pdf", Name: "PDF", Icon: "application-pdf", Color: "#b9877d", Properties: withTags("mime")},
	{ID: "query", Name: "Consulta", Icon: "edit-find", Color: "#7b91ae", Properties: withTags("view")},
	{ID: "collection", Name: "Collection", Icon: "view-list-details", Color: "#8f7fb5", Properties: withTags(collectionProperties...)},
}
