# Canonical document v2

Frozen contract on 2026-11-02. Every object owns `document`, including images,
PDFs, dates, tags and other types. A media asset is not the document: its notes
use the same document/editor as any other object. No Qt HTML or Android-specific
format is canonical. `content` is a server-derived plain-text projection.

## Wire format

`contracts/document.schema.json` defines `Document {schema_version:2, blocks}`.
Every block has `id`, `type`, optional `attrs`, `content` and `children`.
Inline spans use `type` and appropriate fields (`text`, `href`, `object_id`,
`block_id`, `label`, `latex`, `marks`, `attrs`). Missing optional arrays/maps
mean empty. Canonical empty document is `{"schema_version":2,"blocks":[]}`.

Unknown structural keys, node types and marks are rejected by strict Decode;
unknown JSON-compatible `attrs` are retained. This deliberately supports extension
metadata without letting another client silently flatten an unknown node.

| Node | Required/recognized attrs | Body |
|---|---|---|
| paragraph | alignment left/center/right/justify | inline + optional child blocks |
| heading | level 1..6 | inline + optional child blocks |
| list_item | kind bullet/decimal/alpha/roman/task; task requires checked; optional start, case lower/upper | inline + child blocks |
| quote | alignment/style attrs | inline + child blocks |
| group | style attrs | child blocks |
| toggle | expanded boolean | inline title + child blocks |
| code | source string, optional language | atomic |
| math | source string | atomic |
| divider | — | atomic |
| image | asset_id; optional object_id,width,height,alignment,caption,fit contain/cover | atomic; caption is explicit text attr |
| object_embed | object_id, view inline/link_block/small_card/wide_card/embed; optional block_id for a live block embed | atomic |
| columns | — | 1..8 column nodes |
| column | optional positive width fraction | child blocks; only inside columns |
| table | columns: [{id,width}] | table_row nodes |
| table_row | optional header boolean | table_cell nodes; only inside table |
| table_cell | alignment/style attrs | paragraphs; only inside table_row |

Table columns are identified definitions, not child blocks. Width is positive
logical pixels; layout-column width is a positive relative weight. Every row
must have exactly the table's column count. A table has >=1 row and 1..100
columns, with at most 10000 cells. Table column IDs and all node IDs share the
same document namespace. Table-cell paragraphs do not require a second editor.

Known foreground/background attrs and inline color/highlight marks are hex
`#RRGGBB` or `#RRGGBBAA`. Marks bold/italic/underline/strike/code are boolean;
point_size is finite and in (0,256]. Unknown attrs must be safe JSON data,
not executable CSS/HTML. IDs are nonempty, at most 128 UTF-8 bytes, without
whitespace/control characters. Limits: 8 MiB JSON, 20000 nodes/column IDs and
32 nested containers. A node never carries an `indent` or marker glyph state.

## Inline semantics

- `text`: text and optional marks; `break`: an in-block newline.
- `link`: text, href and optional marks; absolute HTTP(S) or mailto target.
- `object_ref`: object_id, optional label/marks.
- `block_ref`: object_id AND block_id, optional label/marks.
- `math`: latex source. Formula images are derived by the client at its DPR.
- `temporal`: attrs.kind is date or datetime, attrs.start is required,
  attrs.end optional. Date uses YYYY-MM-DD; datetime uses RFC3339 with explicit
  offset and a required valid IANA attrs.timezone. End must not precede start.

Object/asset existence is checked by the service, not the pure domain. The
projection retains reference labels, math source, dates, code and captions.
References returns provenance node_id and object/block/asset/url/temporal kinds,
including text inside table cells and nested/hidden containers.

## Atomic commands

`contracts/document-commands.schema.json` is an array of <=1000 commands.
The service envelope adds base_revision/op_id; these are not document state.

| type | Payload |
|---|---|
| insert | node, parent_id (empty=root), position |
| replace | node_id, node retaining that identity |
| replace_inline | node_id, content |
| set_attrs | node_id, attrs patch; null removes a key |
| delete/move/copy | node_ids or node_id; move/copy add parent_id,position |
| wrap | node_id, node (new empty container) |
| unwrap | node_id; group, quote or column with no inline title |
| flatten_columns | node_id of columns; splice content left-to-right |
| table_insert_row | node_id table, node complete table_row, position |
| table_delete_row/table_move_row | node_id table, parent_id identifies row; move adds position |
| table_insert_column | node_id table, column_id new identity,width,position |
| table_delete_column/table_move_column/table_set_width | node_id table,column_id; move adds position, width sets width |
| table_sort | node_id table,column_id,direction asc/desc,numeric optional |

Move/copy/delete normalize a selection to its roots, so selecting parent and
child processes that subtree only once. Document order wins over selection
click order. Move positions are evaluated AFTER removal from the destination.
Moving a parent into itself/descendants is rejected. Copy retains references
to their original targets while regenerating copied node/column identities.
`new_ids` optionally maps old IDs to deterministic replacement IDs; absent IDs
are generated with cryptographic randomness. On table column insertion keys
`<row-id>/cell` and `<row-id>/paragraph` supply deterministic new IDs.

Each command's result must be valid; batches do not permit temporarily invalid
trees. The whole batch returns an independent result only after all commands
succeed, with no input or command mutation on either success or failure.
Sorting preserves entire rows, keeps header rows at the beginning, is stable
for ties and places empty cells last in both directions. Text sorting is
case-insensitive lexicographic; numeric sorting rejects nonnumeric/nonfinite
nonempty cells instead of guessing. Removing the last row/column is rejected;
remove the whole table to remove that structure.

## Writers and responsibilities

Inventory to integrate: object create/PATCH, document writes, block routes,
REST Markdown/append, sync put/append, MCP, capture/upload/promote, templates,
convert/merge, version restore and import/export. All must use the shared domain
before store commit; a field on Object is insufficient by itself.

Go package: `server/internal/document`. Public functions: Empty, Decode,
Normalize, Validate, Apply, PlainText, References, Walk, Find. No HTTP, SQL,
widget or store dependency. UI selection, cursor and open windows stay client
state. Shared document attrs never include derived bitmaps.

Fixtures under `contracts/fixtures/document-v2/*.document.json` are direct
wire documents. `document-commands/*.json` has input,commands and either
expected or error_contains. Go tests read the same corpus available to clients.

Markdown ingestion uses `append` (append `node` to `parent_id` or root) and
`append_section` (heading `node` with entry `children`; append entries to the
matching root heading before the next heading of equal or higher rank, or
create the heading). These operations compute positions inside the atomic
reducer, so a retry carries the same request even after a successful append.
Ingestion IDs derive from `op_id` and node paths. HTTP Markdown/journal append
and MCP body mutations require `base_revision` and `op_id`, sharing document
receipts and conflict handling with direct editor commands.
