# MCP server — MCP over HTTP for AI agents

Stage 13 (final backend stage) of the backend parity program
(`docs/tracking/BACKEND-PARITY-STRUCTURE.md`, spec #13). Branch:
`backend/mcp-server`. Depends on the merged stages 1–12, and composes the
api-v2 developer surface (`docs/implementation/backend/api-v2.md`).

## What & why

Capacities ships an MCP server
(<https://docs.capacities.io/developer/model-context-protocol>) so AI tools —
Claude, ChatGPT, Cursor and custom clients — can search, read and edit a
user's space over the Model Context Protocol instead of hand-crafted HTTP
calls. Lumen now exposes the same capability at **`http://<host>:<port>/mcp`**:
a streamable-HTTP MCP server built on the **official Go SDK**
(`github.com/modelcontextprotocol/go-sdk` v1.8.0, MIT per its source headers;
catalog entry in `docs/tracking/REUSE-CATALOG.md`), mounted on the existing
`net/http` mux behind the same bearer token. No JSON-RPC 2.0 and no streamable
transport is hand-rolled: `mcp.NewServer`, the generic `mcp.AddTool` (typed
input/output with JSON-schema inference) and `mcp.NewStreamableHTTPHandler`
provide the protocol. The SDK requires Go ≥ 1.25, which matches `go.mod`.

Every tool is a thin wrapper over the internal functions of the earlier
stages, so the MCP surface and the REST API cannot drift. The tool list is
modeled on Capacities' (their names in parentheses where ours differ):

| Capacities tool | Lumen tool | Scope |
|---|---|---|
| `search` | `search` | `api:read` |
| `getObjectContent` | `read_object` | `api:read` |
| `readObjectBlocks` | `read_object_blocks` | `api:read` |
| `listObjectsByObjectType` | `list_by_type` | `api:read` |
| `listObjectsByTag` | `list_by_tag` | `api:read` |
| `listObjectsByCollection` | `list_by_collection` | `api:read` |
| `getObjectTypeShape` | `get_type_shape` | `api:read` |
| `createObjectViaMd` / `createPage` | `create_object` | `api:write` |
| `appendMdToObject` | `append_md_to_object` | `api:write` |
| `saveToDailyNote` | `append_to_daily_note` | `api:write` |
| `addBlock` | `add_block` | `api:write` |
| `updateBlock` | `update_block` | `api:write` |
| `deleteBlock` | `delete_block` | `api:write` |
| `createTask` | `create_task` | `api:write` |

Not ported (deliberate): `createObjectLink` (Lumen has no public object URLs),
`uploadFile` (agents cannot fetch arbitrary URLs from the server; the
SSRF-guarded capture pipeline stays behind `POST /v1/objects/from-url`),
`deleteObject` (no agent-initiated deletion in this stage — deleting is
destructive and cheap to add later once a trash tool is wanted),
`updateObjectViaMD` (PATCH already composes it; a follow-up). Never exposed
as tools: the sync endpoints, token management and the trash purge-all. Read
tools exclude trashed objects and templates, exactly like the REST API.

## Endpoint and transport

- **URL**: `POST http://<host>:<port>/mcp` (the daily compose listens on
  `127.0.0.1:8765`). Streamable HTTP per the MCP spec — **not** the older
  SSE-only transport; do not configure a separate `/sse` endpoint.
- **Auth**: `Authorization: Bearer <token>` — the same token file as the whole
  API (`LUMEN_TOKEN_FILE`).
- **Sessions**: SDK defaults — stateful sessions with `Mcp-Session-Id`; a
  standalone SSE `GET` stream is available and `DELETE` terminates the
  session. The server never sends server-initiated messages today, so
  clients that skip the SSE stream lose nothing. Requests are capped at
  8 MB (the Markdown endpoints' limit); the handler keeps the SDK's DNS
  rebinding protection.
- **Tool errors are in-band**: validation problems return `isError: true`
  with a self-correcting message, not protocol errors, so the model can fix
  its arguments.

## Tool catalog

Input fields are JSON-schema-typed through the SDK's struct binding
(the `jsonschema` struct tags carry the per-field descriptions); the SDK
validates arguments before the handler runs. "Underlying function" is the
same code the REST endpoint calls — never an HTTP round-trip.

| Tool | Input (required in bold) | Underlying function | Scope |
|---|---|---|---|
| `search` | **query**, type_id?, limit? (1-200, default 50) | `matchesSearchQuery` (the GET /v1/search matcher) + `store.Objects`/`ObjectsByType` | `api:read` |
| `read_object` | **object_id** | `store.Object` (the GET /v1/objects/{id} assembly) + `BlocksToMarkdown` | `api:read` |
| `read_object_blocks` | **object_id** | `objectBlocks` (stored `properties.blocks`) + `BlocksToMarkdown` | `api:read` |
| `list_by_type` | **type_id**, limit?, cursor? | `store.ObjectsByType` + `pageWindowParams` (the REST keyset cursor) | `api:read` |
| `list_by_tag` | **tag_name** | title index (`store.TitleOwner`) + tag-relation scan | `api:read` |
| `list_by_collection` | **collection_id** | `collectionItemIDs` + `store.Object` (GET /v1/collections/{id}/items rules) | `api:read` |
| `get_type_shape` | **type_id** | `store.Type` (the GET /v1/types entry) | `api:read` |
| `create_object` | **type_id**, title?, markdown?, tags?, properties? | `createObjectFromMarkdownCore` (POST /v1/objects/markdown) | `api:write` |
| `append_md_to_object` | **object_id**, **markdown**, position? | `appendObjectMarkdownCore` (POST /v1/objects/{id}/append-markdown) | `api:write` |
| `append_to_daily_note` | **markdown**, date? (default today) | `appendJournalMarkdownCore` (POST /v1/journal/{date}/append) | `api:write` |
| `add_block` | **object_id**, **block**, position? | block-array insert per convert-merge rules + `saveObject` | `api:write` |
| `update_block` | **object_id**, **block_id**, **block** | block replace (stored id kept) + `saveObject` | `api:write` |
| `delete_block` | **object_id**, **block_id** | block removal + `saveObject` | `api:write` |
| `create_task` | **title**, markdown?, due_date?, priority?, context_object_id?, recurrence?, tags? | `createObjectFromMarkdownCore` with `type_id: task` | `api:write` |

### Tool semantics worth knowing

- **`search`** answers with id, type, title, `matched_alias` and a 280-rune
  content snippet (Capacities returns the same idea); call `read_object` for
  the full payload. It is not paginated — narrow with `type_id`.
- **`list_by_type`** cursors are byte-identical to the REST
  `X-Lumen-Next-Cursor` (tests pin the interchangeability), so an agent can
  continue a REST listing or vice versa.
- **`list_by_tag`** takes the tag **name** (chosen over the id because agents
  and users refer to tags by name; the server resolves it through the unique
  title index). Discover names with `list_by_type type_id=tag`.
- **`create_object`** enforces exactly the POST /v1/objects/markdown contract:
  `properties.blocks`, `properties.rich_text` and `properties.tags` are
  rejected (they are derived), `type_id: collection` and
  `properties.is_template: true` are rejected, tags are names resolved
  get-or-create.
- **`create_task`** sets `status: "todo"`, validates `due_date` as YYYY-MM-DD,
  stores `priority` and `context_id` as (untyped) task properties and passes
  `recurrence` through the recurring-tasks validation
  (`validateTaskRecurrence`), so completing the task through
  `POST /v1/tasks/{id}/complete` spawns the next occurrence normally.
- **Block tools** (`add_block`, `update_block`, `delete_block`) follow the
  convert-merge representation rules: an object with blocks accepts inserts
  and keeps its content mirror recomputed **only while content mirrors the
  blocks** (legacy objects with arbitrary `content` never lose text); an
  object without any text gains the block representation; a plain-text
  object refuses blocks (a block array is authoritative in the editor and
  would hide the plain body — the tool tells the agent to use
  `append_md_to_object`). Agent-supplied blocks are normalized to the stored
  shape: server-assigned ids, `text` recomputed as the concatenation of the
  runs, todo/toggle markers (○/✔, ▶/▼) added when missing, heading levels
  clamped to 1-4, list nesting to 1-8. A columns block accepts non-container
  blocks per column (the SDK's schema inference rejects recursive types);
  deeper layouts remain available through Markdown `::: columns`. Every
  write is a normal object save: revision bump, version-history snapshot,
  mobile sync journal.
- **`read_object_blocks`** ships no output schema (the `Block` type is
  recursive); its JSON shape is `{object_id, blocks, markdown}`.

## Auth: why we deviate from Capacities' OAuth 2.1

Capacities' MCP server sits behind OAuth 2.1 + PKCE with Dynamic Client
Registration, refresh tokens and consent screens because it is a multi-tenant
SaaS: many users must authorize many third-party apps against many spaces.
Lumen is **single-user and self-hosted** with exactly one deployment token
(`LUMEN_TOKEN_FILE`); there is no third party to authorize and no tenant
boundary to enforce. An OAuth dance would add client registration, token
storage and an authorization endpoint without producing any isolation the
deployment does not already have — the same rationale as the api-v2 scope
model (`docs/implementation/backend/api-v2.md`, "Scope model: why not
OAuth 2.1").

What Lumen adopts instead is the **scoping** idea, per tool:

- an **unscoped token** (the default) has full access to every tool;
- **`api:read`** (via `LUMEN_TOKEN_SCOPES`) answers the seven read tools and
  rejects the seven write tools in-band with
  `This token only has the api:read scope; this request modifies data and
  requires api:write`;
- **`api:write`** implies read and unlocks everything.

Because one POST URL hosts both classes of tools, `scopes.go` exempts `/mcp`
from the method-based middleware decision and each tool checks its own scope
(`requireMCPWriteScope`). The `DELETE /mcp` teardown stays allowed for any
valid token: it ends a session, it mutates no data. The middleware still
enforces the bearer token itself, so MCP is never anonymous.

## Creation limit

Capacities enforces a per-user creation quota (10,000 objects, growing). For
a single-user self-hosted server a durable quota adds state without much
benefit; what an operator does want is a runaway agent loop being bounded.
Lumen caps **how many objects a single tool call may create at 20** — the main
object plus every get-or-created tag (`requireMCPCreationBudget`). The rule
over-approximates (existing tags are not created) to stay predictable, is
reported in `GET /v1/api` as `limits.mcp_max_objects_per_call`, and the tool
descriptions tell the model to send ≤19 tags.

## Connecting Claude / Cursor / a custom client

Any MCP client that speaks the streamable HTTP transport and custom bearer
headers works:

- **Endpoint**: `http://<host>:<port>/mcp` (e.g. `http://127.0.0.1:8765/mcp`
  for the daily compose; `https://` when `LUMEN_TLS_CERT`/`LUMEN_TLS_KEY` are
  configured).
- **Header**: `Authorization: Bearer <content of the token file>`.
- **Claude (custom connectors)**: add a connector with the endpoint URL and
  the bearer token; only HTTPS endpoints are accepted there, so front the
  server with its TLS configuration or a local reverse proxy.
- **Cursor**: configure a streamable-HTTP MCP server with
  `--header "Authorization: Bearer <token>"`.
- **Custom clients**: use the official SDKs or follow the Capacities guide's
  streamable-transport steps minus the OAuth discovery/registration — Lumen
  answers a plain `initialize` with a bearer header. Set
  `LUMEN_TOKEN_SCOPES=api:read` for a search-and-read-only agent.

Client requirements come from the transport: POST JSON-RPC 2.0 messages,
honor the `Mcp-Session-Id` response header on subsequent calls, tolerate the
response being `text/event-stream` or `application/json`, and treat tool
results with `isError: true` as model-facing failures. The standalone SSE
GET stream is optional (the server sends nothing on it today).

## Data model

None. No new record kinds, no schema bump, no fixtures: the tools read and
write the same encrypted objects through the same save path. The only new
state is in-memory: one `*mcp.Server` and its streamable handler per `App`
(`mcpOnce`/`mcpHandler`), holding the SDK session bookkeeping.

## Contracts

The following section was added to `contracts/SYNC.md`:

```markdown
## MCP server (AI agents)

Implemented 2026-10-31 (backend stage `mcp-server`). `POST /mcp` exposes the
workspace to MCP clients (Claude/Cursor-style) over the streamable HTTP
transport of the Model Context Protocol (JSON-RPC 2.0), built on the official
Go SDK (github.com/modelcontextprotocol/go-sdk, MIT) —
mcp.NewStreamableHTTPHandler mounted on the existing mux. Sessions follow the
SDK defaults: stateful with Mcp-Session-Id, an optional standalone SSE GET
(the server never pushes), DELETE to terminate. Request bodies are capped at
8 MB like the Markdown endpoints.

- Auth: the same bearer token as the whole API. Capacities uses OAuth 2.1 +
  PKCE because it is a multi-tenant SaaS authorizing third-party apps; Lumen
  is single-user and self-hosted with one deployment token, so OAuth would
  add ceremony without isolation — the token itself is the authorization.
  LUMEN_TOKEN_SCOPES applies per tool: api:read answers the read tools,
  api:write unlocks the write tools, an unscoped token keeps full access.
  Because one URL hosts both classes, /mcp bypasses the method-based scope
  middleware and each tool enforces its own scope; DELETE /mcp (session
  teardown, no data change) stays allowed for any valid token. Tool errors
  are returned in-band (isError: true with a self-correcting message), never
  as protocol errors.
- Tools (thin wrappers over the same internals as the REST API; read tools
  exclude trashed objects and templates): search (query, type_id?, limit?),
  read_object (object_id), read_object_blocks (object_id), list_by_type
  (type_id, limit?, cursor?), list_by_tag (tag_name), list_by_collection
  (collection_id), get_type_shape (type_id) — api:read; create_object
  (type_id, title?, markdown?, tags?, properties?), append_md_to_object
  (object_id, markdown, position?), append_to_daily_note (markdown, date?),
  add_block (object_id, block, position?), update_block (object_id, block_id,
  block), delete_block (object_id, block_id), create_task (title, markdown?,
  due_date?, priority?, context_object_id?, recurrence?, tags?) — api:write.
- create_object enforces the exact POST /v1/objects/markdown contract
  (properties.blocks/rich_text/tags rejected; collections and templates
  rejected; tags resolved get-or-create). create_task sets status: todo and
  passes recurrence through the recurring-task validation, so
  /v1/tasks/{id}/complete spawns occurrences normally. The block tools follow
  the convert-merge representation rules and normalize agent blocks to the
  stored shape (server-assigned ids, text = concatenation of runs,
  todo/toggle markers, heading levels 1-4, list indent 1-8; a plain-text
  object refuses blocks).
- Creation limit: one tool call creates at most 20 objects (the main object
  plus get-or-created tags); reported in GET /v1/api as
  limits.mcp_max_objects_per_call.
- Cursors: list_by_type cursors are the same keyset cursors as the REST
  X-Lumen-Next-Cursor; a cursor from one surface works on the other.
- Never exposed: the sync endpoints, token management and the trash
  purge-all. Every write is a normal object save (revision bump, version
  snapshot, mobile journal). No new record kinds, no schema change, no
  fixtures: GET /v1/api lists the endpoint as mcp_server with scope api:write
  (the full toolset's requirement; api:read still initializes and runs the
  read tools).
```

## Testing

`cd server && go test -race ./...`, `go vet ./...` and `gofmt -l .` are
clean. New coverage in `server/mcp_server_test.go` runs the SDK's own client
(`mcp.NewClient` + `StreamableClientTransport` with a bearer RoundTripper)
against the full handler stack on an `httptest` server:

- transport auth: missing/wrong/malformed Authorization on a raw
  initialize POST → 401 with the version header;
- handshake: `initialize` negotiates a protocol version, serverInfo
  `lumen/1.0.0`; `tools/list` returns the 14 tools, every one with a
  description, an object input schema and the readOnlyHint on read tools;
- read tools against REST-seeded fixtures (book with aliases, tagged book,
  markdown note, collection, template, trashed object): title/alias/reader
  matching, snippet, full-object read, blocks+markdown, cursor walk and
  REST↔MCP cursor interchangeability, template/trash exclusion everywhere,
  error messages for unknown types, bad cursors, missing tags, non-collection
  ids and trashed objects;
- write tools verified through the store and the regular API: create (blocks,
  content mirror, get-or-created tags), append with position, daily-note
  lazy creation (default today and explicit date), the POST
  /v1/objects/markdown rejections, block add/update/delete (marker
  normalization, level clamping, stable ids, mirror recomputation,
  plain-text refusal, version-history snapshots, emptying the body), task
  creation (defaults, recurrence validation) and recurrence spawning the next
  occurrence through `/v1/tasks/{id}/complete`;
- the creation limit (20 tags refused, 19 accepted with dedup) and the
  api:read/api:write scope matrix (read-only token initializes and reads,
  every write tool rejected in-band, store count unchanged).

The pre-existing suite is untouched: the api-v2 handlers were refactored into
shared cores (`createObjectFromMarkdownCore`,
`appendObjectMarkdownCore`, `appendJournalMarkdownCore`) with the exact
status-code mapping they always had (`httpError` + `writeCoreError`).

## Client follow-ups

- **Desktop**: none required — the MCP surface is server-side. A future
  "AI panel" would be a separate, explicitly opt-in integration (product
  policy keeps AI features out of the clients for now).
- **Mobile**: unaffected — sync stays full-trust and is not exposed over MCP.
- **Extension**: unaffected.
- **Ops**: `ops/personal` may later document a scoped read-only token for a
  hosted agent; nothing in the compose files changes.

## Follow-ups

- `deleteObject` / `restoreObject` tools when an agent should manage the
  trash (Capacities has `deleteObject`; Lumen's trash makes it reversible, so
  exposing it is low-risk but needs a product decision).
- `updateObjectViaMD`-style property updates (frontmatter-driven PATCH).
- Resolve `[[wikilinks]]` inside agent Markdown to object ids (depends on the
  markdown-converter follow-up).
- Session housekeeping: idle MCP sessions are never closed (SDK default);
  revisit `StreamableHTTPOptions.SessionTimeout` if long-lived agents pile
  up, and note the server's 45 s `WriteTimeout` bounds the standalone SSE
  stream (harmless today: nothing is ever pushed, clients reconnect).
- A `/v1/objects/{id}/blocks` REST endpoint should the desktop want
  block-level editing over HTTP too (today the block operations exist only
  as MCP tools).
