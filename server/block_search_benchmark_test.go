package main

import (
	"bytes"
	"fmt"
	"net/http/httptest"
	"path/filepath"
	"strings"
	"testing"

	"lumen/server/internal/document"
)

func BenchmarkBlockSearchCatalog(b *testing.B) {
	for _, size := range []int{1000, 10000} {
		b.Run(fmt.Sprintf("Objects%d", size), func(b *testing.B) {
			store, err := OpenStore(filepath.Join(b.TempDir(), "search.sqlite"), bytes.Repeat([]byte{0x6b}, 32))
			if err != nil {
				b.Fatal(err)
			}
			b.Cleanup(func() { _ = store.Close() })
			app, err := NewApp(store, testToken, nil)
			if err != nil {
				b.Fatal(err)
			}
			records := []Record{}
			for i := range size {
				d := document.Document{SchemaVersion: 2, Blocks: []document.Node{{ID: "p", Type: "paragraph", Content: []document.Inline{{Type: "text", Text: "Catalog entry " + strings.Repeat("a", 1024)}}}}}
				o := Object{ID: fmt.Sprintf("catalog-%05d", i), TypeID: "note", Title: fmt.Sprintf("Catalog %d", i), Document: &d, Properties: map[string]any{}, Links: []string{}, CreatedAt: now(), UpdatedAt: now()}
				records = append(records, app.prepareSave(&o)...)
				if len(records) >= 100 {
					if err = store.PutBatch(records, nil); err != nil {
						b.Fatal(err)
					}
					records = nil
				}
			}
			if len(records) > 0 {
				if err = store.PutBatch(records, nil); err != nil {
					b.Fatal(err)
				}
			}
			for _, mode := range []struct{ name, path string }{{"Matches", "/v1/blocks?q=Catalog&limit=50"}, {"Miss", "/v1/blocks?q=no-match-identifier&limit=50"}, {"ObjectScope", fmt.Sprintf("/v1/blocks?q=Catalog&object_id=catalog-%05d", size-1)}} {
				b.Run(mode.name, func(b *testing.B) {
					b.ReportAllocs()
					for b.Loop() {
						request := httptest.NewRequest("GET", mode.path, nil)
						response := httptest.NewRecorder()
						app.searchBlocks(response, request)
						if response.Code != 200 {
							b.Fatalf("search failed %d %s", response.Code, response.Body.String())
						}
					}
				})
			}
		})
	}
}
