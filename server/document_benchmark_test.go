package main

import (
	"bytes"
	"context"
	"fmt"
	"path/filepath"
	"strings"
	"testing"

	"lumen/server/internal/document"
)

// Measures the actual encrypted SQLite commit/history/receipt/sync/index path.
// The disposable catalog has 1000 objects; no personal installation is opened.
func BenchmarkCanonicalCommit100KiBCatalog1000(b *testing.B) {
	store, err := OpenStore(filepath.Join(b.TempDir(), "benchmark.sqlite"), bytes.Repeat([]byte{0x5a}, 32))
	if err != nil {
		b.Fatal(err)
	}
	b.Cleanup(func() { _ = store.Close() })
	app, err := NewApp(store, testToken, nil)
	if err != nil {
		b.Fatal(err)
	}
	records := []Record{}
	for i := range 1000 {
		d := document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "text", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Catalog entry"}}}}}
		o := Object{ID: fmt.Sprintf("catalog-%04d", i), TypeID: "note", Title: fmt.Sprintf("Catalog %d", i), Document: &d, Properties: map[string]any{}, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
		records = append(records, app.prepareSave(&o)...)
	}
	if err = store.PutBatch(records, nil); err != nil {
		b.Fatal(err)
	}
	nodes := make([]document.Node, 100)
	for i := range nodes {
		nodes[i] = document.Node{ID: fmt.Sprintf("p-%03d", i), Type: "paragraph", Content: []document.Inline{{Type: "text", Text: strings.Repeat("a", 1024)}}}
	}
	d := document.Document{SchemaVersion: 2, Blocks: nodes}
	o := Object{ID: "benchmark-document", TypeID: "note", Title: "Benchmark", Document: &d, Properties: map[string]any{}, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
	if err = app.saveObject(context.Background(), &o); err != nil {
		b.Fatal(err)
	}
	b.SetBytes(100 * 1024)
	b.ReportAllocs()
	b.ResetTimer()
	for i := 0; b.Loop(); i++ {
		app.writeMu.Lock()
		o, err = app.commitDocument(o.ID, o.Revision, fmt.Sprintf("benchmark-operation-%09d", i), nil, []document.Command{{Type: "replace_inline", NodeID: "p-050", Content: []document.Inline{{Type: "text", Text: strings.Repeat("a", 1020) + fmt.Sprintf("%04d", i%10000)}}}})
		app.writeMu.Unlock()
		if err != nil {
			b.Fatal(err)
		}
	}
}
