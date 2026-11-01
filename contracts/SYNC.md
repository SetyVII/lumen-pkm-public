# API, almacén y sincronización móvil

Implementación: 2026-10-30. El servidor Go es la fuente de verdad de un almacén
SQLite propio, cifrado con AES-256-GCM. Logseq ya no forma parte de la
arquitectura: no hay CLI, worker, outbox de exportación ni grafo en claro.

## Guardado

- `POST/PATCH /v1/objects`, diario y upload responden después de confirmar el
  objeto, sus relaciones derivadas, el journal móvil y los blobs en una sola
  transacción SQLite. La respuesta incluye `sync_status: "synced"` por
  compatibilidad con clientes anteriores.
- SQLite usa WAL, `synchronous=FULL` y `BEGIN IMMEDIATE`: el acuse HTTP implica
  que los datos están en disco.
- `revision` es monotónica por objeto y la asigna el servidor. `PATCH` acepta
  `base_revision` opcional; si está presente y no coincide, devuelve 409 con
  `{"error":"conflict","conflict":true,"current":<objeto>}`. Omitirlo mantiene
  el comportamiento anterior.
- El índice en memoria contiene **solo metadatos**: id, tipo, título, fechas,
  revisión, enlaces y backlinks inversos. El contenido, las propiedades y los
  blobs siguen cifrados en SQLite y se descifran bajo demanda. Un PDF nunca se
  mantiene en RAM entre peticiones.
- El índice se actualiza solo después de un `COMMIT`; un fallo de escritura no
  cambia lo que leen las peticiones.
- `GET /v1/types` y los backlinks usan el índice. Las fichas y los listados
  descifran los payloads que van a devolver; no se guarda una copia permanente
  de todo el texto en RAM.
- `PATCH` idéntico al objeto actual devuelve 200 sin incrementar `revision` ni
  añadir un `sync_changes` falso.
- `properties` en PATCH es un reemplazo explícito de propiedades editables;
  omitir una clave la elimina. Se preserva únicamente la metadata de adjuntos
  gestionada por upload/promote. Si cambia `content`, se descartan `blocks` y
  `rich_text` anteriores salvo que el payload proporcione explícitamente cada
  representación.

## Propiedades y relaciones

`PATCH /v1/types/{id}` permite `name`, `icon`, `color` y `properties`. No cambia
ID ni borra notas. Las definiciones siguen siendo **por tipo**: `status` de
`book`, `project`, `task` y `web` son independientes. Al cambiar una propiedad
`object`, los `PropertyLinks` de los objetos de ese tipo se recalculan en la
misma transacción.

Una propiedad `object` admite ID string o array de IDs, comprueba existencia y
`target_type`, y genera `property_links`. Estos IDs se mantienen separados de
`links` explícitos: quitar la última mención no elimina una relación de
propiedad. Backlinks y referencias usan la unión deduplicada. Eliminar un
destino limpia las propiedades tipadas además de las relaciones explícitas.

Los tipos estándar de contenido incluyen `tags` (`type: "object"`,
`target_type: "tag"`, múltiple): lo llevan todos los tipos integrados salvo
`tag` mismo, incluidos `journal` (nota diaria), `person`, `task`, `image`,
`pdf` y `query`; `NewApp` añade la definición al reabrir almacenes anteriores.
El tipo estándar `video` existe junto a
`podcast` y `audio`; no implica un reproductor nuevo.

`summary` es la descripción editable y visible del objeto. En enlaces web, el
servidor guarda ahí la descripción del preview; `og_description` queda como
metadata interna y no se muestra como propiedad. Los clientes aceptan
`description` como alias heredado al leer, pero escriben `summary`; al editar la
descripción se elimina `description` para no duplicar la fila.

## Aliases

`aliases` is a default property available on every object, independent of its
type schema. It lives inside the existing `properties` map as an array of
strings, the same shape `tags` uses for its object IDs, so the store schema and
the sync payload do not change:

```json
{"properties": {"aliases": ["HP1", "The Stone"]}}
```

- `POST /v1/objects` and `PATCH /v1/objects/{id}` accept `properties.aliases`.
  `properties` remains a full replacement document: omitting the key removes the
  aliases, and an empty array also removes the key (objects without aliases have
  no `aliases` key).
- `GET /v1/objects` and `GET /v1/objects/{id}` return the stored array.
- The server trims each alias, drops empty entries and deduplicates
  case-insensitively keeping the first spelling. Limits: 50 aliases per object,
  200 characters each; violations return 400 instead of truncating.
- `GET /v1/search?q=` matches titles, content and aliases (case-insensitive
  substring). A result that matched only through an alias carries
  `"matched_alias": "<stored alias>"`; title or content matches omit the field.
  The response is still a JSON array of objects, so the change is additive.
  `GET /v1/objects?q=` uses the same matcher without `matched_alias`.
- Aliases are not globally unique (titles are): two objects may share one and
  search returns both.
- Sync carries aliases inside the object payload unchanged. Conflict merges
  union list properties, so aliases are unioned and then normalized by the
  server.

## Collections

`collection` is a default object type: a curated, ordered list of objects of
exactly one type (Capacities: <https://docs.capacities.io/reference/collections>).
Collections are ordinary objects, so they are created, renamed, read and deleted
with the generic object API. The type adds two properties:

- `item_type_id` (`text`): ID of the single type whose objects the collection
  holds. It must reference an existing type, cannot be `collection` (no nested
  collections) and is immutable after creation; `PATCH` and mobile sync reject a
  different value.
- `items` (`object` relation, multiple): ordered array of object IDs. The array
  order is authoritative; `property_links` and backlinks expose the same IDs as
  a derived set, not as a second ordering.
- `tags` is inherited like every other content type.

The built-in `collection` type keeps these two structural properties:
`PATCH /v1/types/collection` may rename labels, icon, color or the tags
property, but removing `item_type_id`/`items` or changing their kinds returns
`400`.

Invariants enforced on create, `PATCH`, capture and sync push:

- every item exists, is not trashed and has `type_id == item_type_id`;
- an object appears at most once per collection;
- a collection cannot contain itself or another collection;
- `items` is always stored as a JSON array (`[]` when empty).

Deleting an object removes it from every collection in the same SQLite
transaction (the existing backlink cleanup rewrites `items`) while preserving
the order of the remaining items. Reads skip an ID that no longer resolves or
is trashed as defense in depth, but the stored property is authoritative.

### Endpoints

All routes require the bearer token and are additive to the existing object API.
Create with `POST /v1/objects` (`type_id: "collection"`, `properties.item_type_id`,
optional `properties.items`); rename or describe with `PATCH /v1/objects/{id}`.

`POST /v1/collections/{id}/items`

```json
{"object_id":"<id>","position":0}
```

`object_id` is required. `position` is an optional 0-based index; omitted or
equal to the current length appends. Returns `200` with the updated collection
object. Errors: `400` for a wrong-type, nonexistent or trashed item, an invalid
position or an invalid body; `404` when the collection does not exist; `409`
when the item is already in the collection.

`PUT /v1/collections/{id}/items`

```json
{"items":["<id-1>","<id-2>"]}
```

Replaces the full ordered list; this is the reorder primitive for drag-and-drop.
`items` is required and `[]` clears the collection. The same validation as
creation applies. Returns `200` with the updated collection; an identical list
is a no-op and does not bump `revision`.

`DELETE /v1/collections/{id}/items/{object_id}`

Removes one item and preserves the order of the rest. Returns `200` with the
updated collection; `404` when the object is not in the collection (clients may
treat that as already removed).

`GET /v1/collections/{id}/items`

```json
{"collection_id":"<id>","item_type_id":"book","item_ids":["<id-1>","<id-2>"],"items":[{...},{...}]}
```

Resolves the stored order to full objects in one request. `item_ids` is the
stored order; `items` skips IDs that no longer resolve or are trashed.

### Listing collections that contain an object

Items are property links, so backlinks already flow. Use the optional `type_id`
filter:

```sh
GET /v1/objects/<object-id>/backlinks?type_id=collection
```

The response is the list of collection objects containing that object. Without
the filter the endpoint keeps returning every backlink (notes, journals, other
relations). To list the candidate objects of one type use
`GET /v1/objects?type_id=<item_type_id>`; to read a collection's contents in
order use `GET /v1/collections/{id}/items`.

### Sync

Collections are ordinary objects in `sync_changes`; item changes are object
`put`s. `properties.items` is an ordered list of IDs, so a mobile conflict merge
unions the lists as ordered sets (server order first, incoming-only IDs appended)
and deduplicates. `item_type_id` is immutable on the server: a push that changes
it is rejected with an operation error. Deleting an item on any client goes
through the normal delete path and cleans every collection.

## Date properties with an optional end

A `date` property accepts the legacy plain string and the extended
`{start,end}` object. Both forms coexist; clients must accept both when
reading.

```json
{"properties": {"date": "2026-10-31"}}
{"properties": {"period": {"start": "2026-10-01", "end": "2026-10-05"}}}
```

- `start` is required and uses `YYYY-MM-DD`.
- `end` is optional, uses `YYYY-MM-DD`, and must be on or after `start`.
- A plain string is equivalent to `{start: <date>}`: a single day with no end.
- A missing or empty `end` means "no end". `end` equal to `start` is allowed
  and is still an explicit end.
- The server validates both forms on create, PATCH and mobile sync push.
- No schema version bump and no index change: the value stays inside the
  encrypted `properties` payload. Existing fixtures contain no date values, so
  they do not change.
- Time of day is not part of the format yet (follow-up).

## Object list filters

`GET /v1/objects` and `GET /v1/search` (they share the handler) accept these
query parameters. Every filter is ANDed with the others. Unknown parameters are
ignored so older servers tolerate newer clients; a malformed value for a known
parameter returns HTTP 400 with `{"error":"..."}`. Repeated scalar parameters
use the first value; repeated `*_includes`, `*_type` and `collection` values are
ANDed.

| Parameter | Value | Matches |
|---|---|---|
| `type_id` | type id | objects of that type (unchanged) |
| `q` | text | case-insensitive substring of title and plain content (unchanged) |
| `linked_count` | `N`, `min,max`, `min,`, `,max` | number of distinct outgoing relations: explicit `links` plus `property_links` |
| `linked_includes` | object id, repeatable | links to every listed object |
| `linked_type` | type id, repeatable | links to at least one object of every listed type |
| `backlinked_count` | `N`, `min,max`, `min,`, `,max` | number of distinct objects that link to this one (deduped union) |
| `backlinked_includes` | object id, repeatable | is linked from every listed object |
| `backlinked_type` | type id, repeatable | is linked from at least one object of every listed type |
| `content_empty` | `true` or `false` | `true`: no text in blocks, rich_text or content; `false`: some text |
| `collection` | collection object id, repeatable | listed in the `items` property of every listed collection |
| `props.<name>.from` | `YYYY-MM-DD` | the date range ends on or after this day |
| `props.<name>.to` | `YYYY-MM-DD` | the date range starts on or before this day |
| `props.<name>.has_end` | `true` or `false` | `true`: the date has an explicit end; `false`: single date |

Examples:

```http
GET /v1/objects?type_id=note&linked_count=0
GET /v1/objects?type_id=note&linked_count=2,5&linked_type=person
GET /v1/objects?linked_includes=ID1&linked_includes=ID2
GET /v1/objects?backlinked_count=0&type_id=note
GET /v1/objects?backlinked_type=project&backlinked_includes=PROJECT_ID
GET /v1/objects?content_empty=true
GET /v1/objects?collection=COLLECTION_ID
GET /v1/objects?props.due_date.from=2026-10-01&props.due_date.to=2026-10-31
GET /v1/objects?props.period.has_end=true
```

Date range filters are inclusive and use overlap semantics: an object matches
when its date range `[start,end]` intersects `[from,to]`. A single date is the
range `[date,date]`. An object without the property, or with a non-date value,
does not match. `props.<name>` may contain dots; the operation is split from
the right. `from` later than `to` is rejected.

`content_empty` follows the editor precedence: a non-empty `blocks` array is
authoritative, otherwise a `rich_text` string, otherwise `content`. Markup that
renders no words (`<p></p>`, `<p>&nbsp;</p>`) counts as empty.

The `collection` filter requires an existing object of type `collection` with
an `items` object relation. A missing id or an object of another type returns
HTTP 400. Collections themselves are not part of their own item list. The
filter selects membership only: results keep the normal listing order (most
recently updated first). Use the collections endpoints for the curated order.

## Recurring tasks (backend parity stage 8)

*English section added by the backend parity program; the rest of this file
predates it.*

A task object may carry an optional `properties.recurrence` object. Absent or
`null` means a normal task, so every pre-existing task stays valid without a
migration. The server validates the rule strictly on every task write
(`POST/PATCH /v1/objects`, sync push) and rejects invalid rules with 400.
`recurrence` is reserved for `task` objects: a non-task object with a non-null
`recurrence` is rejected instead of silently ignoring the rule.

| Field | Type | Rules |
|---|---|---|
| `mode` | string | `daily`, `weekly`, `monthly` or `every_n_days` (lowercase, required) |
| `n` | integer | Only for `every_n_days`; 1..3660 |
| `weekdays` | array of integers | Only for `weekly`; 1=Monday..7=Sunday, no repeats, at most 7 |

- A recurring task must also have a valid `due_date` (`YYYY-MM-DD`). The date is
  required because the next occurrence is computed from it; a rule without a
  schedule would be uncompletable.
- `weekly` without `weekdays` advances seven days. An explicit empty
  `weekdays` array is rejected.
- `weekly` with `weekdays` picks the next listed weekday strictly after the due
  date; a listed weekday equal to the due date advances a full week. The server
  stores `weekdays` sorted ascending.
- `monthly` keeps the same calendar day in the next month and clamps to the
  last day when the target month is shorter (`2026-01-31` -> `2026-02-28`,
  `2028-01-31` -> `2028-02-29`). Clamping is applied to each occurrence's own
  due date, so a clamped series can drift (`Jan 31 -> Feb 28 -> Mar 28`).
- `every_n_days` adds `n` calendar days.
- Dates are calendar dates (`YYYY-MM-DD`), not instants: no timezone is involved
  in the math. Timestamps (`completed_at`, `created_at`) keep the server's
  existing UTC format.

### `POST /v1/tasks/{id}/complete`

Marks the task done by setting `properties.status` to `"done"` and records
`properties.completed_at`. The desktop already treats `"done"` and `"completed"`
as closed. When the task has a `recurrence`, the server also creates the next
occurrence as a new object in the same SQLite transaction.

Request: no body. Response `200`:

```json
{
  "task": { "...": "the completed task, with occurrences and next_occurrence_id" },
  "next": { "...": "the new occurrence, or null" },
  "next_date": "2026-11-01",
  "created": true
}
```

- `next_date` is omitted for non-recurring tasks.
- `created` is `false` when the call did not spawn a new occurrence.
- The next due date is computed from the task's `due_date`, never from the
  completion time. An overdue task therefore advances exactly one period per
  completion and may still be overdue (catch-up policy: the series keeps its
  cadence and no occurrence is silently skipped).
- The next occurrence copies the task definition: title (with the occurrence
  date appended, see below), `priority`, `context_id`, typed relations
  (`links`/`property_links`), `tags`, `recurrence` and other definition
  properties. It starts with `status: "todo"`, the new `due_date`, no content,
  no `blocks`/`rich_text`, no attachments, no `occurrences`, no
  `completed_at` and no `next_occurrence_id`.
- Lumen keeps titles unique per workspace, so the next occurrence is titled
  `<base> (YYYY-MM-DD)`, where `<base>` is the parent title without a trailing
  `(YYYY-MM-DD)` or `(YYYY-MM-DD #N)` suffix. If that title is taken, the
  discriminator goes inside the parentheses: `<base> (YYYY-MM-DD #2)`, `#3`...
- The completed task keeps its content and notes. It receives one entry in
  `properties.occurrences` (a list property, not a text block, so the note body
  is untouched):

```json
{
  "completed_at": "2026-10-31T12:34:56.000000000Z",
  "scheduled_for": "2026-10-30",
  "next_due": "2026-10-31",
  "next_occurrence_id": "<next occurrence id>"
}
```

- `properties.next_occurrence_id` on the completed task remembers the spawned
  occurrence. `properties.previous_occurrence_id` on the new occurrence points
  back to the completed one. Both are server-managed metadata, not object links,
  so they do not create backlinks.
- The log keeps the most recent 1000 entries; older entries are pruned so a
  long-running daily task cannot grow the encrypted record without bound.
- Idempotency: completing an already completed task returns `200` with the task
  unchanged and `created: false` (never 409); a retried request cannot spawn a
  second occurrence. A task reopened after completion and completed again marks
  itself done again but does not spawn or log a second occurrence. The
  `next_occurrence_id` pointer is the guard: even if the recorded child was
  deleted, the parent does not fork the series.
- Completing a recurring task by PATCHing `properties.status` to `done` is a
  manual completion: it does not spawn the next occurrence. Clients must call
  this endpoint to advance the series.
- Non-task objects return 400; unknown ids return 404.
- The completed task and the new occurrence are committed together and produce
  two `object` entries in the mobile sync journal.

## Templates

Templates are ordinary objects of their type with `properties.is_template: true`.
`is_template` is a reserved boolean property present on every type (default
false; absent means false). The server canonicalizes its definition on type
create/patch and on startup, so clients cannot redefine it.

- `GET /v1/objects` and `GET /v1/search` exclude templates by default. Add
  `include_templates=true` to include them. `GET /v1/objects/{id}` always
  returns a template.
- `GET /v1/objects/{id}/backlinks` also excludes template sources by default;
  `include_templates=true` includes them. Deletion still cleans references from
  templates, so no dangling links remain.
- Template titles are not part of the unique-title namespace. A normal object
  may use the same title as a template; two normal objects still cannot share a
  title. Object titles may be empty (clients render a placeholder).
- Mobile sync (`/v1/sync/pull`) includes templates: clients need them to apply
  templates offline. Clients must preserve `properties.is_template` when they
  edit a template; `PATCH` replaces the whole editable property map.

### Create from template

`POST /v1/objects/from-template`

```json
{"template_id":"...","title":"Weekly {date:long}","date":"2026-10-31"}
```

- `template_id` is required and must reference an object with
  `is_template: true`.
- `title` is optional. When omitted, the resolved template title is used; an
  empty template title produces an empty title. If that default title is
  already used by a normal object, the server appends ` (2)`, ` (3)`, ... so a
  fixed-title template can be applied repeatedly. An explicitly supplied title
  is never rewritten; its conflict returns `400`.
- `date` is optional (YYYY-MM-DD). It defaults to the server-local current date,
  matching journal semantics.
- The new object has the template's type, content, links, properties and blocks.
  Properties are deep-copied. Every block receives a new ID from the same
  generator as `POST /v1/objects`; block payloads, runs and object references are
  preserved. Referenced objects and attachments are linked, not duplicated.
- `is_template` is removed from the new object.
- The response is `201` with the created object.

Date variables are expanded in the title and in date properties. Supported
forms: `{date}` and `{date:iso}` (YYYY-MM-DD), `{date:long}`
("October 31, 2026"), `{date:short}` ("10/31/26"), `{date:weekday}`
("Saturday"). Format names are case-insensitive. Unknown formats are left
verbatim. Date properties are typed `date` properties plus the built-in legacy
keys `date`, `due_date`, `*_date` and `date_*`; a typed date property must
resolve to ISO (`{date}` or `{date:iso}`) because the stored value is a date.
Content, `rich_text` and `blocks` are not substituted.

### Default template per type

`PATCH /v1/types/{id}` accepts `default_template_id`:

```json
{"default_template_id":"..."}
```

- The object must exist, have `is_template: true`, and belong to that type.
- Send an empty string (or JSON `null`) to clear it.
- `GET /v1/types` returns `default_template_id` when set.
- Deleting, unflagging or changing the type of a template clears the reference
  in the same transaction. Mobile sync applies the same cleanup.
- `POST /v1/types` rejects `default_template_id` because a new type cannot have
  a template yet.

### Journal default template

`GET /v1/journal/{date}?apply_default_template=true`

Daily notes are still created lazily on the first request for a date. When the
note does not exist and the `journal` type has a default template, the note is
created from that template: content, properties, links and blocks are copied,
blocks get new IDs, and date variables resolve to the requested day. The note
keeps its canonical date title and `properties.date`. Applying the flag to an
existing note does not reapply or merge the template. A dangling default
reference falls back to a plain note. Capture-driven journal creation does not
apply the default template; only this endpoint does.

## Objetos de medio

`POST /v1/assets` conserva multipart `object_id` + `file` y la respuesta Asset:

```json
{"id":"blob-id","object_id":"owner-id","name":"libro.pdf","mime":"application/pdf","size":42,"created_at":"...","media_object_id":"media-id"}
```

- Imagen o PDF crea un objeto reutilizable. Tiene `properties.asset_id`,
  `file_name`, `mime`, `size`, `assets` y, para imagen, `image_asset_id`.
- Con origen, éste conserva el adjunto y recibe un link al medio. El medio guarda
  `source_object_id` y sus backlinks muestran el origen.
- Sin `object_id` sólo crea el objeto medio y el blob; no genera una nota
  fantasma. Otro MIME sin origen devuelve 400.
- Bytes cifrados, metadata, objetos y journal se confirman juntos. No se escriben
  archivos temporales descifrados.
- Al borrar un objeto medio se eliminan sus assets y blobs y se limpian las
  referencias. Al borrar una nota, los medios promovidos se conservan y los
  adjuntos exclusivos no promovidos se eliminan.
- `Asset.MediaObject` no se duplica dentro del registro persistido; se resuelve
  por `media_object_id` en la respuesta.

## Weblink reader content

`/v1/capture` and `POST /v1/objects/{id}/refresh-reader` extract a readable
article from a public HTML page and store it on `web` objects as
`properties.reader_content` (Markdown string). Extraction is deterministic and
local: it reuses the HTML fetch already governed by the preview SSRF guards
(public `http`/`https` only, ports 80/443, DNS validation, redirect validation,
2 MB HTML cap). It never calls an external reader service and never uses AI.

- `reader_content` is capped at 500 KiB (512000 bytes) and truncated on a UTF-8
  boundary.
- When the page has no readable article, or the preview fetch fails,
  `reader_content` is `""` and the capture still succeeds. A client-supplied
  preview or `fetch_preview=false` skips the fetch, so the property is absent
  until `refresh-reader` runs.
- `favicon` (string URL) is set on `web` objects when Readability finds a PNG
  favicon. It is additive metadata for the pending favicon work (F4.6).
- Both properties are server-managed. `PATCH /v1/objects/{id}` restores the
  stored values when `properties` omits them, and ignores client-supplied
  replacements; `POST /v1/objects` strips them for `web` objects. `capture` and
  `refresh-reader` are the only supported writers.

### Search

`GET /v1/search?q=...` (and `GET /v1/objects?q=...`) matches the query against
`title`, `content` and `properties.reader_content` with the same
case-insensitive substring rule. There is no weighting: a title hit and a
reader hit rank the same. Results keep the existing order (`updated_at`
descending, then `id` ascending), so the behavior is deterministic. The
in-memory index remains metadata-only; `reader_content` is decrypted on demand
during the scan, so a 500 KiB article is never pinned in RAM.

### Refresh endpoint

`POST /v1/objects/{id}/refresh-reader` re-fetches the saved URL and replaces
`reader_content` (and `favicon` when non-empty) on an existing `web` object.
No request body is needed. The fetch happens before the write lock; the object
is re-read under the lock, so concurrent edits are preserved.

```json
POST /v1/objects/01J.../refresh-reader
Authorization: Bearer <token>

200 OK
{
  "id": "01J...",
  "type_id": "web",
  "title": "The Quiet Web",
  "properties": {
    "url": "https://example.com/quiet-web",
    "reader_content": "# The Quiet Web\n\n...",
    "favicon": "https://example.com/favicon.png"
  },
  "revision": 4
}
```

- `404` if the object does not exist or its type is not `web`.
- `400` if the web object has no `url`.
- `409` with `{"error":"conflict","conflict":true,"current":<object>}` if the
  URL changed while the page was being fetched.
- `502` if the page cannot be fetched or is not HTML; the previous
  `reader_content` is kept.

`reader_content` is a normal property in sync payloads; clients may ignore it.
The fixtures are unchanged: `reader_content` and `favicon` are optional
server-generated properties, not required for object validity.

## Sincronización móvil (Lumen Android)

El servidor mantiene `sync_changes`: un journal monotónico por `seq` con
`kind/id/op`. Los cambios de `object`, `type`, `tombstone` y `asset` se escriben
en la misma transacción SQLite que el dato cifrado. Los clientes no descargan
toda la base: piden el delta desde su última secuencia.

### Endpoints

- `GET /v1/sync/status` → `{server_id, head_seq, pending_logseq, protocol,
  tls_fingerprint?}`. `pending_logseq` se mantiene en `0` por compatibilidad.
- `POST /v1/sync/pull` con `{since_seq, media_since_date}` → `{head_seq, objects,
  types, tombstones, assets, deleted}`. `deleted` cubre tipos/assets borrados;
  `tombstones` conserva los borrados de objetos.
- `POST /v1/sync/push` con `{device_id, base_seq, operations}`. Cada operación
  lleva `op_id` (8..128 caracteres), `action` (`put`, `delete`, `asset`),
  `base_revision`, `append`, `client_time`, `object` y/o `asset`.
- La respuesta es `{head_seq, applied, conflicts?, errors?}`. `applied` incluye
  también operaciones ya aplicadas: reintentar tras perder la respuesta Wi-Fi no
  duplica nada. El registro `sync_ops` se confirma en la misma transacción.
- `POST /v1/sync` se conserva como no-op de compatibilidad y devuelve `/health`.

### Tolerancia en el push (0.6.1)

Un push no debe bloquearse por relaciones que todavía no resuelven. Antes de
validar cada objeto, el servidor descarta los enlaces cuyo destino no existe
todavía (nunca se creó o quedó tombstoned) y los valores de propiedades
`object` que apunten a IDs no resueltos, de la misma forma en que ya se
filtraban las referencias a tombstones. Un objeto no puede quedar bloqueado
para siempre por un enlace que aún no puede satisfacerse.

`POST /v1/types` acepta un `id` opcional (recortado, 4..64 caracteres, sin
espacios) para anclar el ID del tipo: necesario cuando un teléfono se
re-empareja contra un servidor nuevo y sus objetos ya referencian el tipo. Si
el ID ya existe se devuelve 200 con la definición almacenada y sin cambios
(idempotente); colisión con otro ID devuelve 409.

### Merge

- **Diario** (`type_id=journal`, `append=true`): concatena el contenido móvil al
  final, une `links` y `assets` sin duplicar y conserva la portada existente si
  el móvil no envía otra.
- **Conflicto normal** (`base_revision < server.revision`): la versión del
  servidor queda como principal y el contenido móvil se añade como
  `> [Edición desde móvil · HH:MM]`. Las propiedades escalares divergentes se
  guardan en `properties.mobile_conflict`; las listas de IDs se fusionan como
  conjuntos.
- **Tareas**: gana el `updated_at` más reciente. Una tarea móvil más antigua
  devuelve `task_kept_server_newer` sin modificar el servidor.
- **Título duplicado**: un put móvil cuyo título ya pertenece a otro objeto
  (típico: el objeto-medio creado por el servidor desde el mismo adjunto) se
  desambigua como `"<título> (2)"` en vez de rechazar la operación; el móvil
  no puede preguntar a nadie en mitad de un push.
- **Tombstones**: un `put` para un ID borrado se ignora con
  `ignored_tombstoned` si `deleted.Revision >= base_revision`; la recreación
  explícita de un diario con ID fijo sigue permitida. Los enlaces y propiedades
  `object` que apuntan a un tombstone se filtran antes de validar.
- **Assets**: `action=asset` acepta `data_base64` (máximo 20 MB), imagen o PDF.
  `create_media_object=true` crea el objeto `image`/`pdf` reutilizable; si hay
  `object_id`, adjunta metadata y `image_asset_id` al propietario.

### Cliente Android

- `applyPull` no sobrescribe un objeto con outbox pendiente. Un tombstone o
  `deleted` elimina también su outbox para evitar resurrecciones.
- Tras un push correcto, `confirmPush` borra las operaciones aplicadas y marca
  los objetos como `synced` en la misma transacción local, sin esperar al pull.
- El móvil sigue enviando `base_revision`; el servidor lo usa para detectar
  ediciones concurrentes.
- Antes de subir, el móvil ordena la outbox para que un objeto preceda a las
  operaciones que lo enlazan: el servidor tolera los enlaces sin resolver, y
  este orden evita perderlos en el primer pase. Los ciclos se envían tal cual
  y el servidor rompe el empate descartando una dirección.
- Al empezar cada sync, el móvil registra en el servidor los tipos
  personalizados locales que falten (`POST /v1/types` con `id` anclado); sin
  esto, un re-emparejamiento dejaría huérfanos sus objetos.
- Los mensajes de sync conservan el aviso de operaciones rechazadas
  («El servidor rechazó N operación(es): <motivo>») y no lo tapan con el
  resumen final.
- "Reenviar todo al servidor" re-encola todos los objetos y medios sellados
  locales para migrar a un servidor recién emparejado; los puts ya vigentes
  responden con no-op y lo divergente se fusiona conservando ambas versiones.

### Medios y disparadores

El móvil descarga todas las miniaturas OG (también las de objetos `web` cuyo
nombre de archivo no contiene `og-` o `preview`), las fotos de los últimos 90 días y
los PDFs/fotos marcados con pin. El resto queda como metadata y se pide bajo
demanda. `tools/scripts/usb-sync.sh` publica `adb reverse tcp:8765 tcp:8765`; la app
prueba `127.0.0.1:8765` antes que Wi-Fi. En LAN, `NsdManager` busca
`_lumen-sync._tcp.local.` y el servidor lo anuncia con `LUMEN_MDNS=1` (o cuando
el bind no es loopback). TLS es opcional; `tools/scripts/generate-tls.sh` crea un
certificado local y `tools/scripts/pairing-qr.sh` incluye su huella SHA-256 en el QR.

Los archivos de medios del teléfono se sellan en reposo (`SecureMedia`):
AES-256-GCM con la clave maestra del espacio, contenedor idéntico al de los
payloads de la base (`nonce || ciphertext || tag`) y AAD que liga cada fichero
a su nombre, de modo que no se puede intercambiar un medio por otro. El texto
claro existe solo en RAM mientras se muestra o sube, y brevemente en el caché
privado al compartir; ese caché se limpia al arrancar. Ficheros escritos por
versiones anteriores a 0.6.1 estaban en claro: siguen siendo legibles y se
re-sellan en el sitio la primera vez que se leen con la app desbloqueada.

## Trash (soft delete)

Implemented 2026-10-31 (backend stage `trash`). `DELETE /v1/objects/{id}` no
longer destroys the payload: it moves the object to the trash. The tombstone and
its anti-resurrection rules are unchanged; the difference is that the payload is
kept in a separate encrypted record (`kind=trash`, id = object id) until the
object is restored or purged. The tombstone stays small because it is part of
the mobile sync payload; the trash payload is never synced.

### Lifecycle

- **Trash** (`DELETE /v1/objects/{id}`): writes the tombstone
  (`revision = object.revision + 1`, `deleted_at`), writes the `trash` record
  with the full object and the same `deleted_at`, and removes the live object
  row in one transaction. Response:
  `{"deleted":true,"trashed":true,"id":"...","deleted_at":"...","sync_status":"synced"}`.
- **Restore** (`POST /v1/objects/{id}/restore`): writes the object again with a
  fresh revision (`max(object.revision, tombstone.revision) + 1`), keeps the
  original `incarnation`, and deletes the tombstone and the trash record in the
  same transaction. `404` when the id is not in the trash (including purged
  ids). If another live object took the title while this one was trashed, the
  restored title gets a numeric suffix (`Title (2)`).
- **Purge** (`DELETE /v1/trash/{id}`): today's hard delete. Removes the trash
  record, cleans inbound `links`/`properties` references, deletes or reassigns
  attachments and blobs, and **keeps the tombstone**. `404` when the id is not
  in the trash.
- **Empty** (`POST /v1/trash/empty[?type_id=...]`): purges every trashed object,
  optionally restricted to one type. Response `{"purged":N}`. Entries are
  purged one transaction at a time; a crash leaves the rest recoverable.

`deleted_at` is stored on both the tombstone and the trash record so a future
janitor can implement retention (Capacities uses 30 days). This stage does not
auto-purge.

### Listing and exclusions

- `GET /v1/trash?q=&type_id=` returns metadata only:
  `[{"id","type_id","title","deleted_at"}]`, ordered by `deleted_at` descending
  (ties by id). `q` matches title or content, case-insensitively.
- A trashed object is not a live object: it is absent from `GET /v1/objects`,
  `GET /v1/search`, `GET /v1/objects/{id}` (404), backlinks as a source, and
  `GET /v1/objects/{id}/backlinks` (404). The in-memory index keeps only live
  objects, so no extra filters are needed.
- Inbound references are **not** rewritten when an object is trashed. Other
  objects keep the id in `links`/object properties, so a restore brings the
  backlinks back intact. The target is simply not resolvable until then.
  Clients should render such references as "in trash" (follow-up). Purge is the
  operation that cleans references, exactly as the old hard delete did.
- While trashed, the object's attachments and blobs are kept encrypted so a
  restore does not lose covers or files. Purge deletes exclusive attachments and
  reassigns promoted media objects, as before.

### Mobile sync

- Trash writes the same journal entries as the old hard delete: `object` delete
  plus `tombstone` put. A phone sees the deletion on its next pull and removes
  the object locally; it never sees the trash payload (the `trash` record kind
  is not a sync kind).
- Restore writes `object` put plus `tombstone` delete. The object is delivered
  again with a newer revision; `deleted` contains
  `{"kind":"tombstone","id":...}` so clients can drop their local tombstone row
  (the current Android client ignores that kind; harmless but a follow-up).
- Anti-resurrection is unchanged: a `put` for a trashed or purged id with
  `base_revision <= tombstone.revision` is ignored with `ignored_tombstoned`.
  Fixed journal ids keep the explicit-recreation exception.
- A mobile `delete` operation moves the object to the trash too.
- Mobile pushes keep references to trashed objects (they are restorable) and
  strip references to purged objects, so a stale outbox never gets stuck and a
  later purge still cleans the relation on the server.
- Restoring from mobile is a follow-up: the phone can pull the restored object
  but has no trash UI or restore operation yet.

### Storage

- The trash payload is a normal encrypted record (`kind=trash`,
  AAD `trash/<id>`), not part of the tombstone. Tombstones are synced to every
  device and must stay small; embedding a multi-megabyte document would inflate
  every pull and keep the payload forever. The side record is temporary and is
  deleted by restore or purge.
- Schema version 3: the generic `records` table needs no DDL, but the bump
  prevents a pre-trash binary from opening a workspace whose trash payloads it
  would ignore.
- `Fsck` verifies that every trash record has a matching tombstone with a newer
  revision and that no id is live and trashed at the same time.

## Object version history (server-side only)

Every successful object change that bumps `revision` stores a snapshot of the
**previous** state in the `object_versions` side table, keyed by
`(object_id, revision)`. The snapshot contains the previous `content`,
`properties`, `links`, `title`, `type_id` and `updated_at`. It is encrypted with
AES-256-GCM like every other payload, with associated data
`version/<object_id>/<revision>`.

- The server keeps at most **20 snapshots per object**. The oldest rows are
  pruned in the same transaction as the new snapshot, so the cap is a durable
  invariant (`Fsck` verifies it). The current state is never stored as a
  snapshot; `GET /v1/objects/{id}` is the current revision.
- Snapshots are created by the central save path, so they cover `PATCH`, journal
  creation/linking, capture, asset upload/promote, type-schema relation
  recomputation, deletion cleanup and mobile `sync/push` merges. Creating an
  object does not snapshot (there is no previous state), and a `PATCH` that
  changes nothing returns 200 without a new revision or snapshot.
- A snapshot row never enters `records` or `sync_changes`. It is **server-side
  only**: `POST /v1/sync/pull` and the push protocol never carry history. A
  restore is a normal object `put`, so mobile clients receive the restored
  content like any other `PATCH` and keep their own single current copy.
- Objects created before revisions existed may have a snapshot at revision `0`;
  it is listed and restorable like any other revision.

### Endpoints

- `GET /v1/objects/{id}/versions` → newest first:
  `[{"revision": 7, "timestamp": "...", "word_count": 184, "bytes": 1120}, ...]`.
  `timestamp` is the `updated_at` of that state; `word_count` counts
  whitespace-separated words and `bytes` is the UTF-8 length of `content`.
- `GET /v1/objects/{id}/versions/{revision}` → the full snapshot
  (`object_id`, `revision`, `timestamp`, `type_id`, `title`, `content`,
  `properties`, `links`, `word_count`, `bytes`). The current revision is not a
  snapshot and returns 404.
- `POST /v1/objects/{id}/restore/{revision}` with optional
  `{"base_revision": N}` → restores `content`, `properties` and `links` from the
  snapshot as a **new** revision and returns the updated object. It never deletes
  history: the pre-restore state is snapshotted by the restore write itself.
  `title` and `type_id` are preserved (titles are unique workspace-wide and type
  conversion is a separate operation). Links or object properties that point to
  objects deleted since the snapshot are dropped instead of failing the restore.
- `base_revision` follows `PATCH` semantics: omitted or `0` disables CAS; a
  mismatch returns `409` with
  `{"error":"conflict","conflict":true,"current":<object>}`. Unknown
  object/version → 404; invalid revision/body → 400.

Restoring a deleted or trashed object is out of scope: the trash stage owns
restoring the object itself, and its restore will compose with this history
because snapshots are keyed by object ID and revision. Permanent purge deletes
the object's `object_versions` rows in the same transaction as the trash payload
and the reference cleanup, so a purged object leaves no history behind.

## Markdown and blocks

The server converts between Markdown and the block schema the desktop editor
persists in `properties.blocks` (`RichEditor::structuredBlocks` in
`desktop/src/editor/lumen_editor.cpp`). Markdown is the compact wire format
for scripts, bulk import and export; block JSON remains the lossless
representation. Conversion is pure: it never reads the store and never
resolves object titles to ids.

### Block schema

```json
{
  "id": "6f1d2c0a-3b7e-4c55-9a10-2d8e4f6b7c01",
  "type": "heading",
  "text": "Title",
  "level": 1,
  "runs": [{"text": "Title"}]
}
```

| Field | Type | Meaning |
|---|---|---|
| `id` | string | UUID, stable per block |
| `type` | string | `paragraph`, `small`, `heading`, `bullet_list`, `numbered_list`, `alphabetic_list`, `quote`, `todo`, `toggle`, `group`, `code`, `math`, `divider`, `embed`, `table`*, `columns`* |
| `text` | string | concatenation of `runs[].text`; code bodies use U+2028 between lines; math and divider blocks use U+FFFC |
| `level` | int | heading level 1-4 (omitted otherwise) |
| `indent` | int | list nesting 1-8, 1 = top level (omitted otherwise) |
| `language` | string | code fence language (omitted when absent) |
| `runs` | array | inline spans, always present (`[]` for table/columns) |
| `align`* | []string | table column alignment: `left`, `center`, `right` or `""` |
| `rows`* | [][]BlockCell | table cells; the first row is the GFM header row |
| `columns`* | [][]Block | two-column layout; each entry is a column of blocks |
| `children` | []Block | editable body of a `group` or `toggle`; nested blocks retain IDs |

`*` fields are additive converter extensions. Android and the Qt editor now
read and write `rows`/`align` and `columns` as structured data. Qt uses native
QTextTable frames; Android stacks layout columns in reading order on narrow
screens. Table cells contain `runs` (not child blocks); layout columns contain
arrays of full blocks. Both preserve block IDs and nested image references.

A run is one inline span:

```json
{"text": "docs", "bold": true, "href": "https://example.com"}
```

| Field | Meaning |
|---|---|
| `text` | visible text (always present) |
| `bold`, `italic`, `underline`, `strike`, `code` | formatting flags; absent means false |
| `highlight`* | `==highlight==`; absent means false (editor ignores it) |
| `href`, `type_id`, `object_id` | link target; object links use `lumen://object/<id>` |
| `type` | `math` or `image` for embedded runs |
| `latex`, `display` | math source and display flag |
| `image_name`, `width`, `height` | editor image resource metadata |
| `reference_icon`, `point_size` | visual metadata written by the editor |

Invariants:

- `text` equals the concatenation of `runs[].text`.
- Todo blocks start with `○ ` (open) or `✔ ` (done); toggle blocks start with
  `▶ ` (collapsed) or `▼ ` (expanded). The triangle is document state,
  shared by desktop and Android through the existing object/sync APIs.
  A toggle's `text`/`runs` contain only its title (including the marker);
  `children: Block[]` contains the editable body in reading order. Children
  may include toggles, images, lists, tables, columns or other block types.
  Collapsing changes the marker and never deletes the children. Legacy
  title-only toggles remain readable and gain an empty editable body.
  The title and children retain their own stable block IDs.
- In-block line breaks are U+2028 (`QChar::LineSeparator`), matching the
  editor. The converter also accepts `\n` on input.
- Math and divider blocks carry U+FFFC as `text` plus a run with `type` set.
- Unresolved object links use `href: "lumen://object/"`, no `object_id`, and
  the title in `text`. Resolving titles to ids is a follow-up.

El editor móvil usa `properties.blocks` como fuente del documento cuando está
presente. Representa las imágenes en la posición de sus runs `type: image`
(`image_name: lumen://image/<id>`), aunque no estén en `links`. Muestra viñetas,
encabezados, formato de runs y divisores. Al editar reconcilia el tramo cambiado:
conserva los bloques vecinos, IDs, metadatos, sangría, estilos y dimensiones de
imágenes; envía los bloques actualizados y `rich_text: null` para invalidar sólo
el HTML obsoleto. `content` es el espejo textual canónico de esos bloques.
El Markdown nuevo se convierte a bloques del mismo contrato. Las menciones
usan IDs; las menciones heredadas por título sólo se resuelven sin ambigüedad.
Fixtures: `fixtures/mobile-editor.json` y `fixtures/native-editor.json`.
Las ediciones estructurales del móvil actualizan `blocks` directamente: una
celda conserva runs y claves adicionales de las celdas vecinas; añadir/eliminar
filas mantiene `align` consistente. Columnas y grupos conservan sus hijos.
El espejo `content` incluye el texto de las celdas y de las columnas para
búsqueda offline. Las relaciones de imágenes/menciones se extraen recursivamente;
eliminar un bloque elimina sus relaciones derivadas, manteniendo relaciones
externas y `property_links`. Fixture común: `fixtures/native-structures.json`.

### Saved views (desktop and Android)

Saved views are ordinary synchronised objects with `type_id: "query"` and
`properties.view` containing the existing desktop LumenQuery spec. Android
uses the same grammar locally, including pending local edits; no new query
language or database is introduced.

```json
{"type_id":"query","title":"Por revisar","properties":{"view":{
  "type_id":"web","search":"","filter":{"mode":"and","rules":[
    {"field":"properties.status","op":"in","value":["Por revisar","Para revisar"]}
  ]},"sort":[{"field":"updated_at","direction":"desc"}]}}}
```

A spec supports `type_id`, `search` (title/plain content/aliases), `ids`,
`include_journals`, `order: "manual"`, nested `filter` groups (`mode: and/or`,
`rules`), multi-field `sort` and a positive `limit` applied after ordering.
Rules use `field`, `op` and `value`: `equals`, `not_equals`, `in`, `contains`,
`not_contains`, `starts_with`, `ends_with`, `empty`, `not_empty`, `exists`,
`before`, `after`, `lt`, `gt`, `lte`, `gte`. Fields include object fields,
`properties.<key>`, union `links` and derived `backlinks`. Unknown operations
retain the existing desktop `contains` fallback. With no explicit sort,
manual ids order wins, otherwise updated_at descending with id as tie breaker.
Unknown presentation/configuration keys survive mobile updates. Android shows
results as a list/overview and honours `group_by`; desktop-specific renderers
(kanban, calendar, etc.) are retained in the spec, not reproduced on Android.
Shared conformance cases: `fixtures/saved-views.json` (Qt and Android).


### Markdown mapping

| Markdown | Block |
|---|---|
| `#` .. `####` | `heading` with `level` 1-4; `#####`/`######` clamp to 4 |
| plain paragraph | `paragraph` |
| `-`, `*`, `+` items | `bullet_list`; nesting becomes `indent` |
| `1.` / `1)` items | `numbered_list` |
| `a.` / `A)` items | `alphabetic_list` (Lumen extension) |
| `> quote` | `quote` |
| `>> group` | `group` (Lumen extension; `> >` stays a quote) |
| `- [ ]` / `- [x]` / bare `[ ]` | `todo` with the `○`/`✔` marker |
| `▶ text` / `▼ text` | `toggle` |
| `<small>text</small>` | `small` |
| fenced code with info string | `code` with `language` |
| `$$...$$` | `math` block (`display: true`) |
| `$...$` | math run (`display: false`) |
| `---`, `***`, `___` | `divider` |
| GFM table | `table` with `rows` and `align` |
| `::: columns` / `::: column` | `columns` (Lumen extension) |
| `**b**`, `*i*`, `` `c` ``, `~~s~~`, `==h==`, `<u>u</u>` | run flags |
| `[text](url)`, `<url>`, bare URL | link run |
| `[[Title]]`, `@Name` | object-link run with the title and no id |
| `![alt](url)` | link run (no image import in this stage) |

### Canonical output

`BlocksToMarkdown` is deterministic:

- Blocks are separated by one blank line; list items by one newline. A change
  of list family (bullet/todo, numbered, alphabetic) at the same or a
  shallower indent starts a new list.
- Ordered markers are normalized to `1.`; alphabetic markers to `a.`.
- Nested items are indented by the parent's content column (`- ` = 2,
  `1. ` = 3, `- [ ] ` = 6), so nested lists survive a round-trip.
- Code fences grow to contain any backtick run in the body; the language is
  the first info word.
- Text is escaped with backslashes for `\`, `` ` ``, `*`, `[`, `]`, `$`, `<`,
  `_` (unless intraword), doubled `~`/`=`, `|` inside tables, and block
  markers at the start of a line.
- Object links render as `[[Title]]`; ids are not serialized.
- U+FFFC placeholders are dropped; U+2028 becomes a hard break in paragraphs.
- Blocks that render to nothing are skipped.

### Endpoints

```http
POST /v1/markdown/to-blocks
Authorization: Bearer <token>
Content-Type: application/json

{"markdown": "# Title\n\n- [ ] task\n"}
```

```json
{"blocks": [
  {"id": "...", "type": "heading", "text": "Title", "level": 1, "runs": [{"text": "Title"}]},
  {"id": "...", "type": "todo", "text": "○ task", "indent": 1, "runs": [{"text": "○ "}, {"text": "task"}]}
]}
```

```http
POST /v1/markdown/from-blocks
Authorization: Bearer <token>
Content-Type: application/json

{"blocks": [{"id": "...", "type": "heading", "text": "Title", "level": 1, "runs": [{"text": "Title"}]}]}
```

```json
{"markdown": "# Title\n"}
```

- Both endpoints require the bearer token, accept up to 8 MB of JSON and are
  additive: they do not modify objects, ids or the sync journal.
- Missing or invalid `markdown`/`blocks` returns 400; invalid UTF-8 in
  `markdown` returns 400.
- Empty input returns `{"blocks": []}` or `{"markdown": ""}`.

### Known limits

- Wikilink and mention titles are not resolved to object ids (follow-up).
- YAML front matter is not parsed; a leading `---` becomes a divider.
- Images become links; importing them as image objects is a follow-up.
- Footnotes and definition lists are not enabled (goldmark extensions).
- Reference links are resolved by goldmark and inlined on output.
- HTML blocks and inline HTML are kept as literal text (except `<u>`).
- List items are single-line; a loose item's extra paragraphs become separate
  paragraph blocks.
- `alphabetic_list`, `group`, `toggle`, `small` and `::: columns` are Lumen
  extensions; other Markdown renderers show them as text.
- `rich_text` HTML is not generated; clients keep their own snapshot.

## Convert and merge

Implemented 2026-10-31 (backend stage `convert-merge`). Two object lifecycle
operations from Capacities
(<https://docs.capacities.io/reference/content-types#type-conversions>,
<https://docs.capacities.io/reference/content-types#merge-two-objects>). Neither
changes the stored object format: both are normal object writes, so the sync
payload, the in-memory index and the mobile journal keep their existing shapes.

### Convert

`POST /v1/objects/{id}/convert`

```json
{"target_type_id": "book", "property_map": {"author_name": "author"}, "base_revision": 7}
```

- The object keeps its id, title, content, links, creation time and
  incarnation. `type_id` and `properties` change, the revision is bumped, and
  one `object` put reaches the sync journal.
- `base_revision` is optional and follows the PATCH CAS contract: a mismatch
  returns `409 {"error":"conflict","conflict":true,"current":<object>}`.
- Converting to the type the object already has, or to an unknown type, is
  `400`. A missing or trashed object is `404`.
- `property_map` moves a source property to a differently named target
  property. Both keys must be declared by the corresponding type (a source key
  may also be an existing hidden property). `aliases` cannot be remapped.
  Explicit mappings win over the automatic same-name rule; a second source
  mapped onto an already filled target is written into the content.
- Compatibility: `text`→`text`, `number`→`number`, `date`→`date`,
  `boolean`→`boolean`, `select`→`select` (when the target options admit the
  value), `object`→`object` (when every id resolves and satisfies the target
  `target_type`; a list needs a multiple target or exactly one id). Legacy
  untyped properties accept any value; a typed target accepts a value whose
  JSON shape matches. Cross-kind moves never happen automatically.
- Unmapped source properties whose name and kind are compatible with a target
  property stay automatically.
- Every source property value that cannot move (no compatible target,
  incompatible kind, blocked mapping, target already filled) is appended to the
  content as one line per property, `Property <name>: <value>`, after a blank
  line. Object relations are rendered by title (falling back to the id); lists
  are joined with `, `. When anything is appended, `properties.blocks` and
  `properties.rich_text` are dropped so no client renders a stale body.
- `aliases` is a default property and always travels with the object. `tags`
  only survives as a relation when the target type declares a compatible
  `tags`; otherwise the tag names are written into the content.
- Hidden keys declared by neither type (`summary`, `assets`, `image_asset_id`,
  `rich_text`, `mobile_conflict`, ...) are carried over untouched.
- `links` and the object's inbound backlinks are unchanged. Property links are
  recomputed for the new type, and the index moves the object to the new type.
  The one exception is collection membership: a collection holds exactly one
  type, so an object that no longer matches a collection's `item_type_id` is
  removed from that collection in the same transaction (the collection revision
  is bumped and its change reaches the sync journal). Keeping the stale item
  would violate the collection invariant and make every later collection write
  fail validation.

### Merge

`POST /v1/objects/{id}/merge`

```json
{"target_id": "8f1c2a44-0d3e-4b6a-9c11-2f6f0f6a1b21", "base_revision": 4, "target_base_revision": 9}
```

- The object in the URL is the **source**: it is merged into `target_id` and
  moved to the trash. The response is the updated target.
- Both objects must be live and of the same type (`400` otherwise). Source and
  target must differ. Merging an already trashed source is `400`; an unknown
  source is `404`; an unknown target is `400`.
- `base_revision` guards the source (the path object, like PATCH);
  `target_base_revision` optionally guards the target. A mismatch returns the
  PATCH `409` shape with the current object.
- Target property values win. Empty target properties (missing, null, empty
  string, empty list) take the source value. Declared multiple relations
  (`tags`, any `multiple: true` object property) and `aliases` are unioned with
  `mergeStringSlices`; a single-value property stays as it is.
- The source body is appended to the target body after a blank line. When both
  objects carry `properties.blocks`, the source blocks are appended and their
  ids are preserved (a colliding source id gets a fresh id); `rich_text` is
  dropped. When only one side has blocks, both `blocks` and `rich_text` are
  dropped so the plain-text `content` stays canonical.
- Attachments owned by the source are reassigned to the target and their
  metadata is unioned. An asset that is the source media object itself (merging
  two images or PDFs) stays with the source.
- The target adopts the source's explicit links. Every live object whose
  `links` or declared object properties point at the source is rewritten to the
  target in the same transaction, with a revision bump and an `object` put in
  the sync journal. `source_object_id` is rewritten too. Hidden keys that are
  not declared relations are not rewritten. Trashed objects are frozen
  snapshots: a reference they hold to the source is not rewritten, and
  restoring both later keeps that reference (the source is restorable).
- The source is trashed exactly like `DELETE /v1/objects/{id}`: tombstone
  (`revision = source.revision + 1`), trash payload, `object` delete and
  `tombstone` put in the journal. It stays restorable; restoring it brings back
  its own content and properties as a duplicate, while the links now point at
  the target (so it has no backlinks).
- The whole merge is one SQLite transaction: target, referrers, assets,
  tombstone and trash commit together.

## Full space export

Implemented 2026-10-31 (backend stage `full-export`). Capacities exports a
space as a ZIP of Markdown files with front matter, assets and a manifest
(<https://docs.capacities.io/reference/export>). Lumen exposes the same idea as
two additive endpoints. The export is a portable snapshot for migration and
inspection, not a backup of the encrypted store: version history, the sync
journal and the trash payload are server-side only and are not included.

### Endpoints

`GET /v1/export` and `POST /v1/export` both require the bearer token and return
`application/zip` with
`Content-Disposition: attachment; filename="lumen-export-YYYYMMDD-HHMMSS.zip"`.
The ZIP is built in a private temporary file and streamed with
`Content-Length`; a store error returns JSON before any archive bytes are sent.

`GET` options are query parameters; `POST` accepts the same options as JSON
(an empty body means all defaults):

```json
{
  "type_ids": ["note", "book"],
  "include_assets": true,
  "include_templates": true,
  "q": "reading"
}
```

| Option | Default | Meaning |
|---|---|---|
| `type_ids` | all types | Only objects of these type ids. Repeated and comma-separated values are accepted; unknown ids return 400. |
| `include_assets` | `true` | Write attachment bytes under `assets/`. When false, image runs degrade to text and `manifest.asset_count` is 0; the front matter keeps asset ids/metadata, only the bytes are omitted. |
| `include_templates` | `true` | Include objects with `properties.is_template: true`. |
| `q` | empty | Case-insensitive substring over title, content, `reader_content` and aliases, using the same matcher as `GET /v1/objects`/`GET /v1/search`. The value is trimmed; empty or whitespace-only means no filter. |

The export is a best-effort snapshot, not a transaction: each object is read
atomically, but a concurrent edit may be reflected in some files and not
others. Use the encrypted store backup for point-in-time recovery.

### ZIP layout

```
manifest.json
types.json
assets/<asset-id>-<human-readable-name>
<type-folder>/<slug>-<object-id>.md
<type-folder>.csv
```

- `<type-folder>` is the pluralized, slugified type name, stable for the same
  type. Built-in type ids are English slugs (`note` -> `notes`, `query` ->
  `queries`, `person` -> `persons`); custom types use the display name with
  Spanish pluralization because their ids are UUIDs. Collisions get `-2`,
  `-3`; `assets` is reserved (`assets-type`).
- Object files are `<slug>-<id>.md`, not title-only: titles are unique only
  for normal objects, and a template may share a title with a normal object.
  The full id makes every filename unambiguous; an empty title becomes
  `untitled`.
- A type with no exported objects has no folder and no CSV; it is still listed
  in `manifest.json` and `types.json`.
- `types.json` is the full type registry (id, name, icon, color, properties,
  `default_template_id`) so an importer can recreate schemas. It is written
  even for a filtered export.
- `manifest.json` contains `schema_version` (1), `format`
  (`lumen-export`), `generator`, `exported_at`, effective `options`,
  `counts` per type, `object_count`, `asset_count`, `template_count`, the
  `types` table (id, name, folder, csv, count) and the `assets` table (id,
  path, name, mime, size, object_id, media_object_id).

### Markdown files

Each `.md` starts with YAML front matter:

```yaml
---
id: 0f7c...
type_id: book
title: Deep Work
created_at: "2026-10-31T09:00:00.000000000Z"
updated_at: "2026-10-31T09:00:00.000000000Z"
revision: 3
is_template: true
links:
  - 5d2e...
properties:
  aliases:
    - DW
  author: Cal Newport
  tags:
    - 8a1f...
---
# Deep Work
```

- `is_template` is emitted only when true. `links` is the explicit link array
  (ids); `property_links` is derived and omitted.
- `properties` contains every stored property except `blocks` (rendered as
  the body), `rich_text` (editor HTML snapshot) and `is_template` (top-level).
  `aliases`, `tags`, `reader_content`, `assets`, `image_asset_id`,
  `asset_id`, date ranges and recurrence objects are all preserved.
- The body is `BlocksToMarkdown(properties.blocks)` when the block array is
  non-empty. Otherwise it is `content`; if that is empty too, a plain-text
  extraction of `rich_text` is used so an old object does not export an empty
  body.
- YAML strings are quoted conservatively (JSON-quoted when they contain
  punctuation, newlines, reserved words or numeric-looking values). No YAML
  dependency is added; JSON strings are valid YAML double-quoted scalars.

### Link rewriting

- A block run with `href: "lumen://object/<id>"` or `object_id: <id>` becomes
  `[Title](../<type-folder>/<slug>-<id>.md)` when the target is exported.
- A target that is not exported (trashed, filtered out by `type_ids`/`q`, or
  excluded by `include_templates=false`) becomes plain text:
  `Title (id: <id>)`, or `id: <id>` when no title is available. No link points
  at a file that is not in the ZIP.
- A run with `href: "lumen://object/"` and no id (an unresolved wikilink)
  becomes its plain title.
- An image run with `image_name: "lumen://image/<id>"` becomes
  `![](../assets/<asset-id>-<name>)` when the media object's asset is
  exported. Otherwise it becomes `image (id: <id>)`.
- External links, `reader_content` links and `content` fallback text are left
  unchanged. Property relations and explicit `links` stay ids in the front
  matter and CSV; only block-model links become file links.
- Table cells and `columns` blocks are rewritten recursively.

### CSV files

`<type-folder>.csv` has `id`, `title`, one column per property (the type
schema keys plus any extra stored keys, sorted) and `is_template`:

- dates are ISO (`YYYY-MM-DD`); ranges are `start..end`;
- lists are `;`-joined; object relations are `;`-joined id lists;
- maps (recurrence, view, asset metadata) are compact JSON;
- missing values are empty; booleans are `true`/`false`;
- `blocks`, `rich_text`, `is_template`, `id` and `title` are not property
  columns (the first two are not tabular; the last three have dedicated
  columns).

### What is and is not included

Included: live objects, templates by default, explicit links, typed and
untyped properties, aliases, tags, reader content, asset metadata and bytes,
type schemas, one CSV per type with objects, and the manifest.

Excluded: trashed objects (they stay restorable through `/v1/trash`; a later
`export_trash` option can add them), version history (`object_versions` is
server-side only), the sync journal (`sync_changes`, `sync_ops`), tombstones,
and internal fields (`incarnation`, `sync_status`, `sync_error`,
`property_links`). Assets owned only by trashed objects are not exported.

### Memory and streaming

The exporter never loads the workspace payloads into RAM. It keeps the
metadata index that the server already maintains, then makes two bounded
passes over `records` with keyset pagination (`exportBatchSize = 50`): one
to select objects and collect CSV columns/asset references, one to render
bodies. Asset blobs are decrypted one at a time, exactly like
`GET /v1/assets/{id}/content`.
CSV rows go to private temporary files per type; the ZIP is written to a
0600 temporary file and streamed only after it is complete. The temporary
files are removed on every path.

Errors: `400` for invalid JSON/options or an unknown type id; `401` without a
bearer token; `500` if a store payload cannot be read or the ZIP cannot be
built. The export does not change objects, revisions or the sync journal.

## API v2 (developer surface)

Implemented 2026-10-31 (backend stage `api-v2`). Modeled on the Capacities
developer API (<https://developers.capacities.io>), composing the earlier
parity stages into endpoints for scripts and agents. **Everything in this
section is additive**: no existing endpoint, parameter or response shape
changed.

### Token scopes

Lumen is single-user and self-hosted with one bearer token
(`LUMEN_TOKEN_FILE`), so there is no OAuth 2.1 authorization framework as in
Capacities' multi-tenant SaaS; what Lumen adopts is the scoping idea. The
optional **`LUMEN_TOKEN_SCOPES`** environment variable restricts the token:

- unset/empty: **full access** (today's behavior, the default);
- `api:read`: GET/HEAD everywhere, plus the POST endpoints that never mutate
  the store (`/v1/markdown/to-blocks`, `/v1/markdown/from-blocks`,
  `/v1/export`); any write returns `403 {"error":"This token only has the
  api:read scope; this request modifies data and requires api:write"}`;
- `api:write`: read plus writes (implies `api:read`);
- unknown values fail at startup instead of silently granting more access.

The sync endpoints (`/v1/sync/*`) are **full-trust and bypass scopes**: the
Android client is a first-party app sharing the deployment token, and scoping
the token for scripting must never break mobile push/pull.

### Version header

`X-Lumen-Api-Version: 1.0.0` is echoed on **every** response (including
`/health` and error responses). Requests may send the header; an absent or
unknown value is never a 400 — the server always answers with the version it
speaks. The header is in the CORS allow and expose lists, so the extension
can send and read it.

### `POST /v1/objects/from-url`

```json
{"url": "https://example.com/a?utm_source=n", "title": "…", "tags": ["reading"],
 "notes_markdown": "# Notes\n\n- [ ] summarize", "journal_link": true}
```

Reuses the exact `/v1/capture` pipeline (URL cleaning, SSRF-guarded preview +
reader fetch, duplicate detection over the cleaned URL). `title` wins over
`og:title`, which wins over the hostname. `tags` are names resolved
get-or-create to `tag` objects (400 when a name is owned by a non-tag
object); the tag objects commit with the web object. `notes_markdown`
becomes `properties.blocks` + the plain-text `content` on a new object and
is appended when the URL already exists. `journal_link: true` links today's
journal and adds `journal_id` to the response.

- Fresh capture: `201` with the object plus `"existing": false` (and
  `journal_id` when linked).
- Duplicate: `200` with the existing object plus `"existing": true`; notes
  are appended and tags unioned before the response.
- Invalid/private/unsupported URLs: `400`.

### `POST /v1/objects/markdown`

```json
{"type_id": "note", "title": "…", "markdown": "# Title\n\n- [ ] task",
 "tags": ["inbox"], "properties": {"priority": "high"}}
```

Creates an object with `properties.blocks` from `MarkdownToBlocks` and
`content` as the plain-text mirror. `properties.blocks`, `properties.rich_text`
and `properties.tags` are owned by the endpoint (400 when supplied);
`tags` ride the top-level array. `type_id: "collection"` is rejected (use the
collections endpoints) and so is `properties.is_template: true` (templates
are created through `POST /v1/objects`). Response: `201` with the created
object.

### `POST /v1/objects/{id}/append-markdown`

```json
{"markdown": "## Section\n\nAdded.", "position": 0}
```

Appends Markdown with the convert-merge representation rules:

- an object carrying a non-empty `properties.blocks` gets the new blocks
  appended (or inserted at `position`, 0-based, `0..len(blocks)`, else 400)
  and loses `rich_text`;
- an object with no text anywhere gains the block representation;
- a plain-text object (content or `rich_text`, no blocks) never gains
  blocks — the block array is authoritative in the editor and would hide
  the plain body; only `content` is appended and `rich_text` is dropped;
  `position` is ignored for plain-text objects.

The plain-text `content` is always extended. The write is a normal object
save: revision bump, version snapshot and a mobile sync-journal entry.
Unknown id → 404; empty/whitespace `markdown` → 400.

### `POST /v1/journal/{date}/append`

```json
{"markdown": "- walked the dog", "section": "Log"}
```

Get-or-create with the existing lazy journal semantics (no default-template
application), then append as above. With `section`, the content goes under a
level-2 heading of that name — a `heading` block (level 2) inserted before
the next same-or-higher-level heading in the block representation, or a
`## <name>` line inserted before the next `#` line in a plain-text note —
creating the heading at the end when missing. Invalid date or empty
`markdown` → 400. Response: `200` with the updated journal object.

### Chunked uploads

For files above the 50 MB limit of `POST /v1/assets` (unchanged):

| Endpoint | Body | Response |
|---|---|---|
| `POST /v1/assets/upload/init` | `{"filename", "mime", "size", "object_id"?}` | `201` `{upload_id, asset_id, size, received, part_min_bytes, part_max_bytes, expires_at}` |
| `POST /v1/assets/upload/{upload_id}/part?index=N` | raw part bytes | `200` `{upload_id, received, size, parts}` |
| `POST /v1/assets/upload/{upload_id}/complete` | none | `201` standard `Asset` |
| `POST /v1/assets/upload/{upload_id}/abort` | none | `200` `{aborted: true}` |

- Parts are sequential, 1-based (`index` must equal the expected next part,
  else 400 out-of-order), 1 B..25 MB; **only the final part may be smaller
  than 5 MB**; the running total may never exceed the declared `size`.
- Total size limit: **500 MB**. The mime allowlist is the same as
  `POST /v1/assets` (standalone uploads must be image/PDF; attached uploads
  accept any type); the final mime is sniffed from the first part's bytes,
  falling back to the declared mime for unknown signatures.
- `complete` produces a normal `Asset` with the same semantics as
  `POST /v1/assets` (media object for images/PDFs, owner attachment
  metadata); an incomplete upload is 400.
- Sessions live **outside the encrypted store**, staged in
  `$TMPDIR/lumen-uploads/<id>/` (override: `LUMEN_UPLOAD_DIR`, 0700): each
  part is sealed with the store key on arrival (AAD `asset/<asset-id>`) and
  the session metadata is sealed too (AAD `upload/<id>`), so the staging
  directory never holds plaintext, survives a restart, and is removed by
  `complete`, `abort` or the **24 h TTL janitor** that sweeps on every
  upload request. A disk-space guard rejects requests with `507` when the
  remaining bytes plus a 64 MB margin do not fit.

Storage: large attachments are persisted as sealed rows of the new
`blob_chunks` table (schema version 4) instead of one `blobs` row. `Fsck`
verifies the chunk invariants; blob deletion removes both shapes;
`GET /v1/assets/{id}/content` streams chunked attachments piece by piece, so
a multi-hundred-MB file never sits decrypted in RAM.

### Pagination

`GET /v1/objects` and `GET /v1/search` accept:

| Parameter | Rules |
|---|---|
| `limit` | integer 1..200, default 50; outside the range → 400 |
| `cursor` | opaque, produced by the `X-Lumen-Next-Cursor` response header; malformed → 400 |

The response **stays a JSON array**; when more results exist the response
carries **`X-Lumen-Next-Cursor`** (absent on the last page, exposed to
browsers through CORS). Pagination runs **after every filter** (type, query,
relational filters, `include_templates`), so it composes with them all. The
cursor is a keyset over the display order (`updated_at` descending, then
`id`): unchanged objects are never skipped or repeated, an edited object may
shift (recency order, last-write-wins), and a cursor whose anchor object
disappeared still resolves by position.

**Backward compatibility:** a request with neither `limit` nor `cursor`
keeps the full list and no header — the default of 50 applies only once a
client opts into pagination, so existing clients never lose data.

### `GET /v1/api`

Discovery document consumed by tooling (stage 13 mounts MCP on top of it):

```json
{
  "version": "1.0.0",
  "token_scopes": ["full_access"],
  "limits": {"max_small_asset_bytes": 52428800, "max_chunked_asset_bytes": 524288000,
             "min_chunk_part_bytes": 5242880, "max_chunk_part_bytes": 26214400,
             "upload_session_ttl_hours": 24, "default_page_limit": 50, "max_page_limit": 200},
  "endpoints": [{"name": "…", "method": "POST", "path": "/v1/objects/from-url", "scopes": "api:write"}]
}
```

`token_scopes` is the effective set of the current token. Endpoint `scopes`:
`public` (no token), `api:read`, `api:write` (implies read), `full` (the
first-party sync protocol, not scope-governed).

## MCP server (AI agents)

Implemented 2026-10-31 (backend stage `mcp-server`). `POST /mcp` exposes the
workspace to MCP clients (Claude/Cursor-style) over the **streamable HTTP
transport** of the Model Context Protocol (JSON-RPC 2.0), built on the
official Go SDK (`github.com/modelcontextprotocol/go-sdk`, MIT) —
`mcp.NewStreamableHTTPHandler` mounted on the existing mux. Sessions follow
the SDK defaults: stateful with `Mcp-Session-Id`, an optional standalone SSE
`GET` (the server never pushes), `DELETE` to terminate. Request bodies are
capped at 8 MB like the Markdown endpoints.

- **Auth**: the same bearer token as the whole API. Capacities uses OAuth 2.1
  + PKCE because it is a multi-tenant SaaS authorizing third-party apps;
  Lumen is single-user and self-hosted with one deployment token, so OAuth
  would add ceremony without isolation — the token itself is the
  authorization. `LUMEN_TOKEN_SCOPES` applies **per tool**: `api:read` answers
  the read tools, `api:write` unlocks the write tools, an unscoped token keeps
  full access. Because one URL hosts both classes, `/mcp` bypasses the
  method-based scope middleware and each tool enforces its own scope;
  `DELETE /mcp` (session teardown, no data change) stays allowed for any valid
  token. Tool errors are returned in-band (`isError: true` with a
  self-correcting message), never as protocol errors.
- **Tools** (thin wrappers over the same internals as the REST API; read
  tools exclude trashed objects and templates):

| Tool | Input | Scope |
|---|---|---|
| `search` | query, type_id?, limit? (1-200, default 50) | api:read |
| `read_object` | object_id | api:read |
| `read_object_blocks` | object_id | api:read |
| `list_by_type` | type_id, limit?, cursor? | api:read |
| `list_by_tag` | tag_name (exact tag title) | api:read |
| `list_by_collection` | collection_id | api:read |
| `get_type_shape` | type_id | api:read |
| `create_object` | type_id, title?, markdown?, tags?, properties? | api:write |
| `append_md_to_object` | object_id, markdown, position? | api:write |
| `append_to_daily_note` | markdown, date? (default today) | api:write |
| `add_block` | object_id, block, position? | api:write |
| `update_block` | object_id, block_id, block | api:write |
| `delete_block` | object_id, block_id | api:write |
| `create_task` | title, markdown?, due_date?, priority?, context_object_id?, recurrence?, tags? | api:write |

- `create_object` enforces the exact `POST /v1/objects/markdown` contract
  (properties.blocks/rich_text/tags rejected; collections and templates
  rejected; tags resolved get-or-create). `create_task` sets `status: todo`
  and passes `recurrence` through the recurring-task validation, so
  `/v1/tasks/{id}/complete` spawns occurrences normally. The block tools
  follow the convert-merge representation rules and normalize agent blocks to
  the stored shape (server-assigned ids, text = concatenation of runs,
  todo/toggle markers, heading levels 1-4, list indent 1-8; a plain-text
  object refuses blocks).
- **Creation limit**: one tool call creates at most **20 objects** (the main
  object plus get-or-created tags); the limit is reported in `GET /v1/api` as
  `limits.mcp_max_objects_per_call`.
- **Cursors**: `list_by_type` cursors are the same keyset cursors as the REST
  `X-Lumen-Next-Cursor`; a cursor from one surface works on the other.
- **Never exposed**: the sync endpoints, token management and the trash
  purge-all. Every write is a normal object save (revision bump, version
  snapshot, mobile journal). No new record kinds, no schema change, no
  fixtures: `GET /v1/api` lists the endpoint as `mcp_server` with scope
  `api:write` (the full toolset's requirement; `api:read` still initializes
  and runs the read tools).

## Verificación

`go test -race ./...` cubre persistencia cifrada, reinicio, CAS opcional,
borrado O(k), assets/blobs, `Fsck`, tombstones, enlaces muertos, merge de
listas e historial de versiones. `go vet ./...` revisa el paquete.

```sh
cd server
go test -race ./...
go vet ./...
```

### Compatibilidad de esquemas en Android (2026-11-01)

Android conserva `property_links` y representa las propiedades recibidas por
tipo, incluidas definiciones heredadas como strings. Relaciones por ID y texto
libre no se intercambian. El catálogo recibido prevalece sobre los defaults de
primer arranque; un tipo personalizado se registra antes de enviar sus objetos.
Los aliases de revisión se agrupan en filtros, pero los valores guardados deben
pertenecer a las opciones del esquema del tipo `web`.

Los deltas append de diario que incluyen `properties.blocks` añaden sólo esos
bloques al JSON existente; no reemplazan su snapshot. Invalidan `rich_text`
obsoleto y se identifican por `op_id`. Imágenes distintas con el mismo texto
U+FFFC se mantienen como operaciones distintas, también en el mismo minuto.
El protocolo no cambia. La regresión está en `server/document_formats_test.go`.

Fixtures y QA: `fixtures/mobile-object-schemas.json` y
`../docs/qa/2026-11-01-mobile-objects/README.md`.


## Backend blocks, media, recurrence and habits (2026-11-01)

This additive contract is exercised by `fixtures/backend-power.json`.
All endpoints require the existing bearer token and API scopes. Writes use the
same encrypted store, version snapshots and mobile change journal as objects.

- Blocks: `GET /v1/objects/{id}/blocks`, `POST /v1/objects/{id}/blocks`,
  `PATCH/DELETE /v1/objects/{id}/blocks/{block_id}`. Mutation bodies contain
  `base_revision` (required), `block` for POST/PATCH, and optional zero-based
  `position` for POST. IDs survive edits; POST assigns fresh subtree IDs.
  Nested `children` and `columns` participate in lookup. GET `/v1/blocks?q=...`
  searches block text (bounded `limit`, optional `object_id`). GET
  `/v1/blocks/{block_id}` resolves the current source, and
  `/v1/blocks/{block_id}/references` lists incoming references. Optional
  `object_id` disambiguates legacy duplicate block IDs; ambiguous lookup is 409.
  References use existing run `href: lumen://block/<id>` links; an `embed`
  block with this run is a transclusion. Resolution reads current source
  content, never a copied snapshot. Missing/deleted targets are 404. Cyclic
  references remain links; the server never recursively expands them.
- New uploads/promotions recognize `audio`, `video` and generic `file` objects
  alongside image/PDF. Generic files download as attachments with `nosniff`;
  no content is executed. Small and chunked uploads use the same MIME
  classification. Existing attachments are not migrated automatically.
- Recurrence additionally accepts `yearly`, `weekdays`, `last_day`,
  `last_weekday` (last Mon-Fri of a month; public holidays are not inferred).
  Leap-year dates clamp to the target month. POST `/v1/tasks/{id}/skip` and
  `/excuse` finalize an occurrence without marking it done, record its outcome
  and atomically create at most one successor. Retries cannot fork a series.
- A built-in `habit` object stores `habit_schedule` (ISO weekdays 1..7,
  default all days), `habit_target` (positive number, default 1),
  `habit_start` (YYYY-MM-DD) and `habit_entries` (date-keyed numeric values).
  PUT `/v1/habits/{id}/entries/{date}` with `{value,base_revision}` replaces
  a day's count (not increments); DELETE removes it. `base_revision` is
  required. GET `/v1/habits/{id}/stats?as_of=YYYY-MM-DD` returns completed/
  scheduled days, current/best streak and totals up to the explicit date.
  Unscheduled days do not break streaks; an unfinished as_of day does not
  break yesterday's streak. Entries outside the configured schedule/start,
  negative/nonfinite values and more than 10000 entries are rejected.
  Habit state is ordinary encrypted object data and survives sync/export/
  trash/restore. No scheduler, external service or notifications are added.

Client follow-up: render transclusions, media players, recurrence controls and
habit dashboards in desktop/mobile; API availability alone is not UI parity.
