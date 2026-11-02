package document

import (
	"encoding/json"
	"fmt"
	"github.com/google/jsonschema-go/jsonschema"
	"math"
	"net/url"
	"os"
	"path/filepath"
	"reflect"
	"strings"
	"testing"
)

const fixtureRoot = "../../../contracts/fixtures/"

func readDoc(t *testing.T, name string) Document {
	t.Helper()
	b, e := os.ReadFile(fixtureRoot + "document-v2/" + name + ".document.json")
	if e != nil {
		t.Fatal(e)
	}
	d, e := Decode(b)
	if e != nil {
		t.Fatal(e)
	}
	return d
}
func same(t *testing.T, a, b Document) {
	t.Helper()
	a, e := Normalize(a)
	if e != nil {
		t.Fatal(e)
	}
	b, e = Normalize(b)
	if e != nil {
		t.Fatal(e)
	}
	if !reflect.DeepEqual(a, b) {
		x, _ := json.Marshal(a)
		y, _ := json.Marshal(b)
		t.Fatalf("documents differ\ngot %s\nwant %s", x, y)
	}
}
func TestCorpusDocuments(t *testing.T) {
	paths, e := filepath.Glob(fixtureRoot + "document-v2/*.document.json")
	if e != nil || len(paths) < 2 {
		t.Fatal("missing document corpus", e)
	}
	for _, p := range paths {
		t.Run(filepath.Base(p), func(t *testing.T) {
			b, e := os.ReadFile(p)
			if e != nil {
				t.Fatal(e)
			}
			d, e := Decode(b)
			if e != nil {
				t.Fatal(e)
			}
			n, e := Normalize(d)
			if e != nil {
				t.Fatal(e)
			}
			same(t, d, n)
			wire, e := json.Marshal(n)
			if e != nil {
				t.Fatal(e)
			}
			r, e := Decode(wire)
			if e != nil {
				t.Fatal(e)
			}
			same(t, d, r)
		})
	}
}
func TestCorpusCommands(t *testing.T) {
	paths, e := filepath.Glob(fixtureRoot + "document-commands/*.json")
	if e != nil || len(paths) < 10 {
		t.Fatal("missing command corpus", e)
	}
	for _, p := range paths {
		t.Run(filepath.Base(p), func(t *testing.T) {
			var c struct {
				Input    Document  `json:"input"`
				Commands []Command `json:"commands"`
				Expected Document  `json:"expected"`
				Error    string    `json:"error_contains"`
			}
			b, e := os.ReadFile(p)
			if e != nil {
				t.Fatal(e)
			}
			if e = json.Unmarshal(b, &c); e != nil {
				t.Fatal(e)
			}
			before, _ := json.Marshal(c.Input)
			cmdBefore, _ := json.Marshal(c.Commands)
			got, e := Apply(c.Input, c.Commands)
			if c.Error != "" {
				if e == nil || !strings.Contains(e.Error(), c.Error) {
					t.Fatalf("expected %q, got %v", c.Error, e)
				}
			} else {
				if e != nil {
					t.Fatal(e)
				}
				same(t, got, c.Expected)
			}
			after, _ := json.Marshal(c.Input)
			if string(before) != string(after) {
				t.Fatal("input mutated")
			}
			cmdAfter, _ := json.Marshal(c.Commands)
			if string(cmdBefore) != string(cmdAfter) {
				t.Fatal("commands mutated")
			}
		})
	}
}
func TestNestedProjectionAndReferences(t *testing.T) {
	d := readDoc(t, "rich")
	s := PlainText(d)
	for _, v := range []string{"Hola 👩🏽‍💻, café", "Oculto, conservado", "Nombre\tValor", "Ana\t10", "Luis\t2", "\\frac{1}{2}", "2026-11-02T02:17:00+01:00", "Izquierda\nDerecha"} {
		if !strings.Contains(s, v) {
			t.Errorf("missing projection %q", v)
		}
	}
	refs := References(d)
	if len(refs) != 7 {
		t.Fatalf("refs=%+v", refs)
	}
	n, ok := Find(d, "cell-text-2-1")
	if !ok || nodeText(n) != "2" {
		t.Fatal("nested cell lookup failed")
	}
	if _, ok = Find(d, "missing"); ok {
		t.Fatal("unexpected node")
	}
}
func TestValidationRejectsBrokenSemantics(t *testing.T) {
	base := readDoc(t, "rich")
	tests := map[string]func(*Document){
		"version":           func(d *Document) { d.SchemaVersion = 1 },
		"duplicate":         func(d *Document) { d.Blocks[1].ID = d.Blocks[0].ID },
		"unknown type":      func(d *Document) { d.Blocks[0].Type = "html" },
		"invalid marks":     func(d *Document) { d.Blocks[0].Content[0].Marks["bold"] = "yes" },
		"invalid color":     func(d *Document) { d.Blocks[0].Content[0].Marks["color"] = "url(file:/etc/passwd)" },
		"invalid date":      func(d *Document) { d.Blocks[12].Content[0].Attrs["start"] = "2026-02-30" },
		"inverted interval": func(d *Document) { d.Blocks[12].Content[0].Attrs["end"] = "2020-01-01" },
		"unknown zone":      func(d *Document) { d.Blocks[0].Content[6].Attrs["timezone"] = "No/Such_Zone" },
		"atomic image":      func(d *Document) { d.Blocks[8].Content = []Inline{{Type: "text", Text: "cannot type here"}} },
		"no asset":          func(d *Document) { delete(d.Blocks[8].Attrs, "asset_id") },
		"broken cell count": func(d *Document) { d.Blocks[11].Children[1].Children = nil },
		"structural root":   func(d *Document) { d.Blocks = []Node{{ID: "c", Type: "column"}} },
		"nonfinite":         func(d *Document) { d.Blocks[8].Attrs["width"] = math.Inf(1) },
	}
	for name, change := range tests {
		t.Run(name, func(t *testing.T) {
			d, e := Normalize(base)
			if e != nil {
				t.Fatal(e)
			}
			change(&d)
			if _, e = Normalize(d); e == nil {
				t.Fatal("accepted broken document")
			}
		})
	}
}
func TestUnknownAttributesSurviveButStructureIsStrict(t *testing.T) {
	d := readDoc(t, "rich")
	if d.Blocks[0].Attrs["future_semantics"] == nil {
		t.Fatal("extension stripped")
	}
	if _, e := Decode([]byte(`{"schema_version":2,"blocks":[],"html":"oops"}`)); e == nil {
		t.Fatal("unknown root field silently dropped")
	}
	if _, e := Decode([]byte(`{"schema_version":2,"blocks":[]} {}`)); e == nil {
		t.Fatal("accepted trailing JSON")
	}
}
func TestTableColumnInsertDeleteRoundTrip(t *testing.T) {
	d := readDoc(t, "rich")
	table := d.Blocks[11]
	one := Document{Version, []Node{table}}
	insert := Command{Type: "table_insert_column", NodeID: table.ID, ColumnID: "extra-column", Width: 120, Position: 1}
	out, e := Apply(one, []Command{insert})
	if e != nil {
		t.Fatal(e)
	}
	if len(out.Blocks[0].Children[0].Children) != 3 {
		t.Fatal("missing inserted cells")
	}
	out, e = Apply(out, []Command{{Type: "table_delete_column", NodeID: table.ID, ColumnID: "extra-column"}})
	if e != nil {
		t.Fatal(e)
	}
	same(t, out, one)
}
func FuzzDecodeNeverPanics(f *testing.F) {
	b, _ := os.ReadFile(fixtureRoot + "document-v2/rich.document.json")
	f.Add(b)
	f.Add([]byte(`{"schema_version":2,"blocks":[]}`))
	f.Fuzz(func(t *testing.T, b []byte) {
		d, e := Decode(b)
		if e == nil {
			if _, e = Normalize(d); e != nil {
				t.Fatal(e)
			}
		}
	})
}

func TestPublishedSchemasMatchCorpus(t *testing.T) {
	load := func(name string) *jsonschema.Schema {
		t.Helper()
		b, e := os.ReadFile("../../../contracts/" + name)
		if e != nil {
			t.Fatal(e)
		}
		var s jsonschema.Schema
		if e = json.Unmarshal(b, &s); e != nil {
			t.Fatal(e)
		}
		return &s
	}
	schema := load("document.schema.json")
	resolved, e := schema.Resolve(nil)
	if e != nil {
		t.Fatal(e)
	}
	paths, _ := filepath.Glob(fixtureRoot + "document-v2/*.document.json")
	for _, p := range paths {
		b, _ := os.ReadFile(p)
		var v any
		if e = json.Unmarshal(b, &v); e != nil {
			t.Fatal(e)
		}
		if e = resolved.Validate(v); e != nil {
			t.Fatalf("%s: %v", p, e)
		}
	}
	commands := load("document-commands.schema.json")
	cr, e := commands.Resolve(&jsonschema.ResolveOptions{Loader: func(u *url.URL) (*jsonschema.Schema, error) {
		if u.Path != "/contracts/document.schema.json" {
			return nil, fmt.Errorf("unexpected schema URI: %s", u)
		}
		return schema, nil
	}})
	if e != nil {
		t.Fatal(e)
	}
	paths, _ = filepath.Glob(fixtureRoot + "document-commands/*.json")
	for _, p := range paths {
		b, _ := os.ReadFile(p)
		var v map[string]any
		_ = json.Unmarshal(b, &v)
		if e = cr.Validate(v["commands"]); e != nil {
			// Zero width is an intentionally invalid command and must fail schema/domain.
			if !strings.Contains(p, "reject-zero-width") {
				t.Fatalf("%s: %v", p, e)
			}
		}
	}
}

func TestAppendSectionUsesCurrentTreeAndPreservesIDs(t *testing.T) {
	d := Document{SchemaVersion: 2, Blocks: []Node{{ID: "h", Type: "heading", Attrs: map[string]any{"level": 2}, Content: []Inline{{Type: "text", Text: "Log"}}}, {ID: "p", Type: "paragraph"}, {ID: "other", Type: "heading", Attrs: map[string]any{"level": 2}, Content: []Inline{{Type: "text", Text: "Other"}}}}}
	heading := Node{ID: "unused-heading", Type: "heading", Attrs: map[string]any{"level": 2}, Content: []Inline{{Type: "text", Text: "Log"}}, Children: []Node{{ID: "entry", Type: "paragraph", Content: []Inline{{Type: "text", Text: "new"}}}}}
	out, err := Apply(d, []Command{{Type: "append_section", Node: &heading}})
	if err != nil {
		t.Fatal(err)
	}
	if len(out.Blocks) != 4 || out.Blocks[0].ID != "h" || out.Blocks[2].ID != "entry" || out.Blocks[3].ID != "other" {
		t.Fatalf("wrong section insertion %+v", out)
	}
	if len(d.Blocks) != 3 {
		t.Fatal("mutated input")
	}
}

func TestGeneratedSchemaMatchesSourceContract(t *testing.T) {
	source, err := os.ReadFile("../../../contracts/document.schema.json")
	if err != nil {
		t.Fatal(err)
	}
	var expected, actual any
	if err = json.Unmarshal(source, &expected); err != nil {
		t.Fatal(err)
	}
	if err = json.Unmarshal([]byte(SchemaJSON), &actual); err != nil {
		t.Fatal(err)
	}
	if !reflect.DeepEqual(expected, actual) {
		t.Fatal("generated schema stale; run python3 tools/scripts/generate-document-schema.py")
	}
}

func TestCommandDecodingRejectsUnknownStructure(t *testing.T) {
	for _, raw := range []string{
		`{"type":"insert","typo":true}`,
		`{"type":"insert","node":{"id":"p","type":"paragraph","typo":true}}`,
		`{"type":"replace_inline","node_id":"p","content":[{"type":"text","text":"text","typo":true}]}`,
		`{"node_id":"p"}`,
		string([]byte{'{', '"', 't', 'y', 'p', 'e', '"', ':', '"', 0xff, '"', '}'}),
	} {
		var c Command
		if err := json.Unmarshal([]byte(raw), &c); err == nil {
			t.Errorf("accepted invalid command %q", raw)
		}
	}
	var c Command
	if err := json.Unmarshal([]byte(`{"type":"set_attrs","node_id":"p","attrs":{"extension":{"future":true}}}`), &c); err != nil {
		t.Fatal("rejected extension attrs", err)
	}
}

func TestCommandBatchLimitsAreAtomic(t *testing.T) {
	d := Document{SchemaVersion: 2, Blocks: []Node{{ID: "p", Type: "paragraph"}}}
	oversized := strings.Repeat("x", MaxBytes)
	if _, err := Apply(d, []Command{{Type: "set_attrs", NodeID: "p", Attrs: map[string]any{"oversized": oversized}}}); err == nil {
		t.Fatal("accepted oversized command batch")
	}
	ids := make([]string, MaxNodes+1)
	if _, err := Apply(d, []Command{{Type: "delete", NodeIDs: ids}}); err == nil {
		t.Fatal("accepted oversized selection")
	}
	if len(d.Blocks) != 1 || d.Blocks[0].Attrs != nil {
		t.Fatal("failed batch mutated input")
	}
}

func TestClipboardEnvelopeSchemaAndInteropFixture(t *testing.T) {
	schemaBytes, err := os.ReadFile("../../../contracts/clipboard.schema.json")
	if err != nil {
		t.Fatal(err)
	}
	var schema, canonical jsonschema.Schema
	if err = json.Unmarshal(schemaBytes, &schema); err != nil {
		t.Fatal(err)
	}
	if err = json.Unmarshal([]byte(SchemaJSON), &canonical); err != nil {
		t.Fatal(err)
	}
	resolved, err := schema.Resolve(&jsonschema.ResolveOptions{Loader: func(u *url.URL) (*jsonschema.Schema, error) {
		if u.Path != "/contracts/document.schema.json" {
			return nil, fmt.Errorf("unexpected schema %s", u)
		}
		return &canonical, nil
	}})
	if err != nil {
		t.Fatal(err)
	}
	raw, err := os.ReadFile(fixtureRoot + "clipboard-v1/internal-and-external-reference.json")
	if err != nil {
		t.Fatal(err)
	}
	var fixture map[string]any
	if err = json.Unmarshal(raw, &fixture); err != nil {
		t.Fatal(err)
	}
	clipboard := fixture["clipboard"].(map[string]any)
	if err = resolved.Validate(clipboard); err != nil {
		t.Fatal(err)
	}
	for _, value := range []any{clipboard["document"], fixture["target_document"], fixture["expected_document"]} {
		encoded, _ := json.Marshal(value)
		if _, err = Decode(encoded); err != nil {
			t.Fatal(err)
		}
	}
	clipboard["clipboard_version"] = 2
	if err = resolved.Validate(clipboard); err == nil {
		t.Fatal("unsupported clipboard version accepted")
	}
	delete(clipboard, "clipboard_version")
	if err = resolved.Validate(clipboard); err == nil {
		t.Fatal("framed clipboard missing version accepted")
	}
}
