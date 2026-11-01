# Templates per object type

Stage 6 of the backend parity program. Capacities reference:
[templates](https://docs.capacities.io/reference/templates) and
[date formatting in templates](https://docs.capacities.io/reference/templates/date-formatting).

## What & why

Capacities lets a user define a template for an object type, prefill its
properties and blocks, and create a new object from it. Templates can also be
the default for a type, which is how daily notes start with a predefined body.
Lumen already has typed objects, stable block IDs, a lazy journal and a
deep-copy helper for JSON values, so the smallest faithful implementation is to
make a template an ordinary object of its type with `is_template: true` rather
than introducing a second entity that would need its own editor, relations and
sync path.

The stage adds:

- a reserved `is_template` boolean property on every type;
- listing/search/backlink filtering so templates do not pollute normal views;
- `POST /v1/objects/from-template` with deep property/block copying and
  `{date:...}` substitution;
- `default_template_id` on types;
- `?apply_default_template=true` on the lazy journal endpoint.

## API

### Flag a template

Templates are created with the normal object endpoint:

```json
POST /v1/objects
{
  "type_id": "project",
  "title": "Weekly review {date:long}",
  "content": "What moved this week?",
  "properties": {
    "is_template": true,
    "status": "active",
    "due_date": "{date}",
    "blocks": [
      {"id": "template-block-1", "type": "heading", "level": 2, "text": "Wins"},
      {"id": "template-block-2", "type": "paragraph", "text": "..."}
    ]
  }
}
```

`is_template` is a normal property in the payload, not a top-level field. Absent
means `false`. The server rejects non-boolean values.

### List and search

```http
GET /v1/objects?type_id=project
GET /v1/objects?type_id=project&include_templates=true
GET /v1/search?q=review
GET /v1/search?q=review&include_templates=true
GET /v1/objects/{id}
```

Templates are excluded by default from `GET /v1/objects` and `GET /v1/search`
(the search route shares the list handler). `include_templates=true` includes
them. `GET /v1/objects/{id}` always returns a template. The same parameter is
accepted by `GET /v1/objects/{id}/backlinks`, which otherwise hides template
sources.

### Create from a template

```json
POST /v1/objects/from-template
{
  "template_id": "8417a0e2-2624-49cc-a3dc-eb40ca680d99",
  "title": "Weekly review {date:long}",
  "date": "2026-10-31"
}
```

Response `201`:

```json
{
  "id": "new-object-id",
  "type_id": "project",
  "title": "Weekly review October 31, 2026",
  "content": "What moved this week?",
  "properties": {
    "status": "active",
    "due_date": "2026-10-31",
    "blocks": [
      {"id": "fresh-block-id-1", "type": "heading", "level": 2, "text": "Wins"},
      {"id": "fresh-block-id-2", "type": "paragraph", "text": "..."}
    ]
  },
  "links": [],
  "created_at": "...",
  "updated_at": "...",
  "sync_status": "synced",
  "revision": 1
}
```

- `template_id` is required and must reference an object with
  `is_template: true`; a missing object returns `404`, a non-template `400`.
- `title` is optional. When omitted, the resolved template title is used. An
  empty template title produces an empty title. If that default title is
  already used by a normal object, the server appends ` (2)`, ` (3)`, ... so a
  fixed-title template can be applied repeatedly. An explicitly supplied title
  is never rewritten; its conflict returns `400`.
- `date` is optional (`YYYY-MM-DD`). It defaults to the server-local current
  date, matching the journal. An invalid date returns `400`.
- Properties are deep-copied. `is_template` is removed. Every block receives a
  fresh ID from `newID()` (the same UUID generator used by `POST /v1/objects`).
  Block payloads, runs, object references and attachment references are kept;
  referenced objects and assets are linked, not duplicated.
- Content and explicit links are copied. Property links are re-derived from the
  copied properties.
- The response is the created object, not the template.

### Date variables

Supported in the title and in date properties:

| Token | Example output for 2026-10-31 |
|---|---|
| `{date}` | `2026-10-31` |
| `{date:iso}` | `2026-10-31` |
| `{date:long}` | `October 31, 2026` |
| `{date:short}` | `10/31/26` |
| `{date:weekday}` | `Saturday` |

Format names are case-insensitive and surrounding spaces are ignored. Unknown
formats are left verbatim (`{date:unknown}` stays `{date:unknown}`). Multiple
variables in one string are all expanded.

A date property is a typed `date` property, or one of the built-in legacy keys
`date`, `due_date`, `*_date` and `date_*` (the built-in schemas still use
name-only definitions). A typed date property stores an ISO date, so only
`{date}` and `{date:iso}` can be used there; a formatted value such as
`{date:long}` is rejected by the normal property validator when the template is
applied. Content, `rich_text` and `blocks` are not substituted.

### Default template per type

```json
PATCH /v1/types/journal
{"default_template_id": "journal-template-id"}
```

- The object must exist, be a template, and belong to the type.
- An empty string (or JSON `null`) clears the reference.
- `GET /v1/types` returns `default_template_id` when set.
- Deleting, unflagging or changing the type of a template clears the reference
  in the same transaction, including when the change arrives through
  `/v1/sync/push`.
- `POST /v1/types` rejects `default_template_id`: a brand-new type cannot have a
  template yet.

### Journal default template

```http
GET /v1/journal/2026-10-31?apply_default_template=true
```

Daily notes are still created lazily on the first request for a date. If the
note does not exist and the `journal` type has a default template, the note is
created from that template:

- content, properties, links and blocks are copied; blocks get new IDs;
- date variables resolve to the requested day;
- the note keeps its canonical date title (`2026-10-31`) and
  `properties.date`;
- `is_template` is removed.

Applying the flag to an existing note does not reapply or merge the template.
A dangling default reference falls back to a plain note. Capture-driven journal
creation (`POST /v1/capture` with `date`) does not apply the default template;
only this endpoint does.

## Data model

- `ObjectType.DefaultTemplateID string` (`default_template_id` in JSON,
  omitted when empty). Stored in the existing encrypted `type` record; no
  schema migration.
- `is_template` lives in `Object.Properties`, so it is stored in the existing
  encrypted object payload and synced like any other property.
- `ObjectMeta.IsTemplate` is derived in the in-memory index. Titles of template
  objects are not inserted into the unique-title map, and backlink lookups can
  skip template sources.
- The reserved `is_template` definition is appended to every type at startup if
  missing and canonicalized on every type create/patch. It does not count
  toward the 64-property limit.
- No new tables, indexes or record kinds.

## How

**Why a flag instead of a `templates` table.** A template needs the editor,
properties, links, blocks, assets and sync. Reusing an object gives all of that
for free and keeps the store format stable. The cost is filtering: templates
appear in the object table, so listings, search and backlinks need an explicit
default exclusion. The sync pull still includes templates because mobile needs
them to apply templates offline.

**Why a reserved property on every type.** A type-level flag would be a second
place to keep schema state; a property keeps templates editable with the normal
object API. The server canonicalizes the definition so clients cannot redefine
it as text or remove it. Existing workspaces are normalized at startup, which is
idempotent and only writes types whose stored schema differs.

**Other pollution audits.** Besides listings, search and backlinks, capture
duplicate detection ignores `web` templates: a bookmark template that stores a
URL must not swallow a real capture of that URL. Deletion and asset cleanup still
walk templates, because a template can hold real links and attachments that must
not become dangling.

**Title namespace.** Applying a template whose title has no date variables would
always collide with the template itself if template titles participated in the
unique-title index. Templates therefore live outside the unique-title namespace:
a normal object may share a template's title, and creating from a template with
no variables succeeds. When the default title is already used by a normal
object, the server appends ` (2)`, ` (3)`, ... using the same convention as media
objects, so a fixed-title template can be applied repeatedly. An explicitly
supplied title is never rewritten; its conflict returns `400`. Clients can also
pass a title or use `{date}`. Object titles may now be empty (the desktop already
renders a placeholder), which is required for a template with no title.

**Deep copy and block IDs.** Properties are cloned with the existing
`cloneStringMap`/`cloneJSONValue` helpers. Blocks are cloned and every top-level
block map gets a new `newID()`. Runs and object references are not rewritten:
the template links existing objects instead of duplicating them, matching
Capacities. Internal block references are a follow-up (see below).

**Date substitution.** The resolver is a small regex over `{date...}` tokens; it
does not depend on the client locale. Month and weekday names are English
because the server has no locale setting yet. Dates are calendar dates parsed
with `2006-01-02`, consistent with the journal; no timezone conversion is
applied. Unknown formats are left alone so a typo never destroys the template
text. Typed date properties are validated after substitution, so a non-ISO
format fails loudly instead of storing a display string in a date field.

**Default template cleanup.** A type must never point at an object that is no
longer a template of that type. The cleanup is committed with the object change
in `updateObject`, `deleteObjectData` and the sync push paths. This avoids a
separate janitor and keeps the type and object consistent across restarts.

**Journal.** The journal is still lazy. The default template is applied only on
creation and only when the caller asks for it, so existing notes are never
rewritten. The note keeps its date title because the calendar, journal IDs and
mobile sync depend on it; the template supplies the body and properties. Capture
keeps its previous behavior to avoid surprising the share-sheet flow.

## Contracts

The following section was added to `contracts/SYNC.md`:

```md
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
- Send an empty string to clear it.
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
```

## Testing

`server/templates_test.go` covers:

- template flag on/off listing, `GET /v1/objects/{id}`, search exclusion,
  backlink exclusion (with `include_templates=true` for each) and capture
  duplicate detection ignoring web templates;
- from-template deep copy of properties, content, links and blocks, fresh unique
  block IDs, template immutability, explicit title override, repeated
  default-title suffixing, explicit-title conflicts, invalid template/date/blocks
  and empty titles;
- all date formats, case-insensitivity, multiple tokens, unknown formats left
  verbatim, typed date properties and the non-ISO rejection;
- the reserved `is_template` definition on every built-in type and on custom
  types after create/patch, plus non-boolean rejection;
- default-template validation (wrong type, non-template, missing), GET
  `/v1/types`, clearing with an empty string, and cleanup on delete, unflag,
  type change and sync push;
- journal creation from the default template (title, date property, content,
  fresh block IDs), no reapplication on an existing note, plain notes without
  the flag, invalid query values and dangling-reference fallback;
- server-local default date.

Commands:

```sh
cd server
gofmt -l .
go vet ./...
go test ./...
go test -race ./...
```

No Docker or external service is needed for these tests. For a manual check,
use the disposable sandbox (`sandbox/compose.test.yaml`, port 8879) and the
bearer token from its own secret files; never run these requests against the
personal installation.

## Client follow-ups

- **Desktop**: add a template section to object-type settings; create/edit
  templates with the normal editor; add an "apply template" picker to the new
  object flow and to empty objects; mark a template as the type default; show
  `is_template` as a boolean property; hide templates from normal lists,
  search results, relation pickers and backlinks unless the template filter is
  on; preserve `is_template: true` when saving a template (PATCH replaces the
  editable property map); render untitled objects with the existing
  "Sin título" placeholder. To populate the picker, request
  `GET /v1/objects?type_id=<type>&include_templates=true` and keep objects with
  `properties.is_template === true`.
- **Mobile**: sync already delivers templates; add a template picker and call
  `POST /v1/objects/from-template`; preserve the flag when editing a template;
  hide templates from normal lists/search; pass the calendar day as `date`.
  The picker can filter the synced objects locally by `properties.is_template`.
- **Extension**: no change; captures are normal objects and must not set
  `is_template`.

## Follow-ups

- Block references inside a template (`((block-id))`) are not remapped when
  block IDs are regenerated. A later block-reference stage should rewrite
  internal references or forbid them in templates.
- Variable queries in templates (plan item F2.3) are out of scope.
- The default template for non-journal types is only stored and validated; the
  client decides when to apply it. A future server-side "create object of type"
  endpoint could apply it automatically.
- Capacities-style custom date tokens (`YYYY-MM-DD`, `LL`, `dddd`, etc.) are
  not implemented; only the named formats above are supported. Month/weekday
  names are English until the server has a locale setting.
- Template duplication and "save an existing object as a template" are client
  workflows; no server endpoint was added.
- The 64-property type limit now excludes the reserved `is_template`
  definition. This is intentional but should be reflected in any client-side
  schema counter.
