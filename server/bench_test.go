package main

import (
	"bytes"
	"fmt"
	"path/filepath"
	"testing"
)

func BenchmarkObjectList(b *testing.B) {
	for _, size := range []int{100, 5000} {
		b.Run(fmt.Sprintf("objects_%d", size), func(b *testing.B) {
			path := filepath.Join(b.TempDir(), "bench.sqlite")
			store, err := OpenStore(path, bytes.Repeat([]byte{0x4a}, 32))
			if err != nil {
				b.Fatal(err)
			}
			defer store.Close()
			records := make([]Record, 0, size)
			for i := 0; i < size; i++ {
				o := Object{
					ID:        fmt.Sprintf("bench-%06d", i),
					TypeID:    "note",
					Title:     fmt.Sprintf("Benchmark note %d", i),
					Content:   "synthetic content for the in-memory index",
					CreatedAt: now(),
					UpdatedAt: now(),
					Properties: map[string]any{
						"tags": []any{},
					},
					Links: []string{},
				}
				records = append(records, Record{Kind: "object", ID: o.ID, Value: o})
			}
			if err := store.PutBatch(records, nil); err != nil {
				b.Fatal(err)
			}
			b.ResetTimer()
			for i := 0; i < b.N; i++ {
				_, _ = store.Objects()
			}
		})
	}
}

func BenchmarkFullBacklinks(b *testing.B) {
	path := filepath.Join(b.TempDir(), "bench-links.sqlite")
	store, err := OpenStore(path, bytes.Repeat([]byte{0x4b}, 32))
	if err != nil {
		b.Fatal(err)
	}
	defer store.Close()
	target := Object{ID: "target", TypeID: "tag", Title: "Target", CreatedAt: now(), UpdatedAt: now(), Properties: map[string]any{}, Links: []string{}}
	records := []Record{{Kind: "object", ID: target.ID, Value: target}}
	for i := 0; i < 5000; i++ {
		o := Object{
			ID:        fmt.Sprintf("source-%06d", i),
			TypeID:    "note",
			Title:     fmt.Sprintf("Source %d", i),
			CreatedAt: now(),
			UpdatedAt: now(),
			Properties: map[string]any{
				"tags": []any{"target"},
			},
			PropertyLinks: []string{"target"},
			Links:         []string{},
		}
		records = append(records, Record{Kind: "object", ID: o.ID, Value: o})
	}
	if err := store.PutBatch(records, nil); err != nil {
		b.Fatal(err)
	}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_, _ = store.Backlinks("target")
	}
}

func BenchmarkMetadataBacklinks(b *testing.B) {
	path := filepath.Join(b.TempDir(), "bench-meta-links.sqlite")
	store, err := OpenStore(path, bytes.Repeat([]byte{0x4c}, 32))
	if err != nil {
		b.Fatal(err)
	}
	defer store.Close()
	target := Object{ID: "target", TypeID: "tag", Title: "Target", CreatedAt: now(), UpdatedAt: now(), Properties: map[string]any{}, Links: []string{}}
	records := []Record{{Kind: "object", ID: target.ID, Value: target}}
	for i := 0; i < 5000; i++ {
		o := Object{
			ID: fmt.Sprintf("source-%06d", i), TypeID: "note", Title: fmt.Sprintf("Source %d", i),
			CreatedAt: now(), UpdatedAt: now(), Properties: map[string]any{},
			PropertyLinks: []string{"target"}, Links: []string{},
		}
		records = append(records, Record{Kind: "object", ID: o.ID, Value: o})
	}
	if err := store.PutBatch(records, nil); err != nil {
		b.Fatal(err)
	}
	b.ResetTimer()
	for i := 0; i < b.N; i++ {
		_ = store.BacklinkIDs("target")
	}
}
