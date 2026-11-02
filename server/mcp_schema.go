package main

import (
	"encoding/json"
	"reflect"

	"github.com/google/jsonschema-go/jsonschema"
	"lumen/server/internal/document"
)

// mcpCanonicalOutputSchema uses the SDK's own schema inference for envelopes
// and ordinary fields, overriding only the recursive canonical document type.
// The schema literal is generated from contracts/document.schema.json.
func mcpCanonicalOutputSchema[T any]() *jsonschema.Schema {
	var canonical jsonschema.Schema
	if err := json.Unmarshal([]byte(document.SchemaJSON), &canonical); err != nil {
		panic(err)
	}
	options := &jsonschema.ForOptions{TypeSchemas: map[reflect.Type]*jsonschema.Schema{
		reflect.TypeFor[document.Document](): &canonical,
		// Legacy tool Blocks are a lossy, recursive Markdown projection. The
		// authoritative document alongside them always has the strict schema.
		reflect.TypeFor[Block](): {Type: "object", Description: "Reduced Markdown projection; use document for lossless structure.", AdditionalProperties: &jsonschema.Schema{}},
	}}
	object, err := jsonschema.For[Object](options)
	if err != nil {
		panic(err)
	}
	// Stored objects universally have an editor; omitempty only permits transient
	// Go objects during construction, not incomplete MCP output.
	object.Required = append(object.Required, "document")
	options.TypeSchemas[reflect.TypeFor[Object]()] = object
	schema, err := jsonschema.For[T](options)
	if err != nil {
		panic(err)
	}
	return schema
}
