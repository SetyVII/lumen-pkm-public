package main

import (
	"encoding/json"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestMarkdownToBlocksEndpoint(t *testing.T) {
	app, _, _ := makeApp(t)
	rr := api(t, app, "POST", "/v1/markdown/to-blocks", map[string]any{"markdown": "# Hi\n\n- [ ] task\n"})
	if rr.Code != 200 {
		t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
	}
	var response struct {
		Blocks []Block `json:"blocks"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &response); err != nil {
		t.Fatal(err)
	}
	if len(response.Blocks) != 2 {
		t.Fatalf("blocks = %+v", response.Blocks)
	}
	if response.Blocks[0].Type != blockHeading || response.Blocks[0].Level != 1 || response.Blocks[0].Text != "Hi" {
		t.Fatalf("heading = %+v", response.Blocks[0])
	}
	if response.Blocks[1].Type != blockTodo || !strings.HasPrefix(response.Blocks[1].Text, todoOpen) {
		t.Fatalf("todo = %+v", response.Blocks[1])
	}
	if response.Blocks[0].ID == "" || response.Blocks[1].ID == "" {
		t.Fatal("blocks without ids")
	}
}

func TestMarkdownFromBlocksEndpoint(t *testing.T) {
	app, _, _ := makeApp(t)
	blocks := []Block{
		{ID: "b1", Type: blockHeading, Text: "Hi", Level: 2, Runs: []Run{{Text: "Hi"}}},
		{ID: "b2", Type: blockBullet, Text: "one", Indent: 1, Runs: []Run{{Text: "one"}}},
		{ID: "b3", Type: blockTodo, Text: "○ task", Indent: 1, Runs: []Run{{Text: "○ "}, {Text: "task"}}},
	}
	rr := api(t, app, "POST", "/v1/markdown/from-blocks", map[string]any{"blocks": blocks})
	if rr.Code != 200 {
		t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
	}
	var response struct {
		Markdown string `json:"markdown"`
	}
	if err := json.Unmarshal(rr.Body.Bytes(), &response); err != nil {
		t.Fatal(err)
	}
	if response.Markdown != "## Hi\n\n- one\n- [ ] task\n" {
		t.Fatalf("markdown = %q", response.Markdown)
	}
}

func TestMarkdownEndpointsRoundTrip(t *testing.T) {
	app, _, _ := makeApp(t)
	source := "# Title\n\nA **bold** line with [[Wiki]] and @alice.\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n"
	first := api(t, app, "POST", "/v1/markdown/to-blocks", map[string]any{"markdown": source})
	if first.Code != 200 {
		t.Fatalf("to-blocks HTTP %d: %s", first.Code, first.Body.String())
	}
	var parsed struct {
		Blocks []Block `json:"blocks"`
	}
	if err := json.Unmarshal(first.Body.Bytes(), &parsed); err != nil {
		t.Fatal(err)
	}
	second := api(t, app, "POST", "/v1/markdown/from-blocks", map[string]any{"blocks": parsed.Blocks})
	if second.Code != 200 {
		t.Fatalf("from-blocks HTTP %d: %s", second.Code, second.Body.String())
	}
	var rendered struct {
		Markdown string `json:"markdown"`
	}
	if err := json.Unmarshal(second.Body.Bytes(), &rendered); err != nil {
		t.Fatal(err)
	}
	if rendered.Markdown != "# Title\n\nA **bold** line with [[Wiki]] and [[alice]].\n\n| a | b |\n| --- | --- |\n| 1 | 2 |\n" {
		t.Fatalf("markdown = %q", rendered.Markdown)
	}
}

func TestMarkdownEndpointsValidation(t *testing.T) {
	app, _, _ := makeApp(t)
	for _, tc := range []struct {
		name string
		path string
		body any
		want string
	}{
		{"missing markdown", "/v1/markdown/to-blocks", map[string]any{}, "Provide markdown"},
		{"null markdown", "/v1/markdown/to-blocks", map[string]any{"markdown": nil}, "Provide markdown"},
		{"wrong markdown type", "/v1/markdown/to-blocks", map[string]any{"markdown": 5}, "Invalid JSON"},
		{"missing blocks", "/v1/markdown/from-blocks", map[string]any{}, "Provide blocks"},
		{"null blocks", "/v1/markdown/from-blocks", map[string]any{"blocks": nil}, "Provide blocks"},
		{"wrong blocks type", "/v1/markdown/from-blocks", map[string]any{"blocks": "x"}, "Invalid JSON"},
	} {
		t.Run(tc.name, func(t *testing.T) {
			rr := api(t, app, "POST", tc.path, tc.body)
			if rr.Code != 400 {
				t.Fatalf("HTTP %d: %s", rr.Code, rr.Body.String())
			}
			if !strings.Contains(rr.Body.String(), tc.want) {
				t.Fatalf("body = %s, want %q", rr.Body.String(), tc.want)
			}
		})
	}
	rr := api(t, app, "POST", "/v1/markdown/to-blocks", map[string]any{"markdown": ""})
	if rr.Code != 200 || !strings.Contains(rr.Body.String(), `"blocks":[]`) {
		t.Fatalf("empty markdown: HTTP %d %s", rr.Code, rr.Body.String())
	}
}

func TestMarkdownEndpointsRequireAuth(t *testing.T) {
	app, _, _ := makeApp(t)
	for _, path := range []string{"/v1/markdown/to-blocks", "/v1/markdown/from-blocks"} {
		req := httptest.NewRequest("POST", path, strings.NewReader(`{"markdown":"x","blocks":[]}`))
		req.Header.Set("Content-Type", "application/json")
		rr := httptest.NewRecorder()
		app.Handler().ServeHTTP(rr, req)
		if rr.Code != 401 {
			t.Fatalf("%s without token: HTTP %d", path, rr.Code)
		}
	}
}
