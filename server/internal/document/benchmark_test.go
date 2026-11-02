package document

import (
	"encoding/json"
	"fmt"
	"strings"
	"testing"
)

// 100 KiB of text in 100 paragraphs, including Unicode and inline references.
func benchmarkDocument100KiB() Document {
	nodes := make([]Node, 100)
	for i := range nodes {
		nodes[i] = Node{ID: fmt.Sprintf("p-%03d", i), Type: "paragraph", Content: []Inline{{Type: "text", Text: strings.Repeat("a", 1024), Marks: map[string]any{"bold": true}}, {Type: "object_ref", ObjectID: "reference-object", Label: "Referencia 😀"}}}
	}
	return Document{SchemaVersion: 2, Blocks: nodes}
}

func BenchmarkDocument100KiB(b *testing.B) {
	d := benchmarkDocument100KiB()
	raw, err := json.Marshal(d)
	if err != nil {
		b.Fatal(err)
	}
	for _, name := range []string{"Normalize", "ApplyInline", "PlainText", "References", "Decode", "Encode"} {
		b.Run(name, func(b *testing.B) {
			b.SetBytes(100 * 1024)
			b.ReportAllocs()
			for b.Loop() {
				switch name {
				case "Normalize":
					_, err = Normalize(d)
				case "ApplyInline":
					_, err = Apply(d, []Command{{Type: "replace_inline", NodeID: "p-050", Content: []Inline{{Type: "text", Text: "Cambio 😀"}}}})
				case "PlainText":
					_ = PlainText(d)
				case "References":
					_ = References(d)
				case "Decode":
					_, err = Decode(raw)
				case "Encode":
					_, err = json.Marshal(d)
				}
				if err != nil {
					b.Fatal(err)
				}
			}
		})
	}
}
