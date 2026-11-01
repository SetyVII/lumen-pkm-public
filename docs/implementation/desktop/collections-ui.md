# Desktop stage: collections UI (D2)

Status: implemented on `desktop/collections-ui` (2026-10-31). Desktop-only
stage of the Capacities parity program; the server, `contracts/` and the
mobile/extension clients are untouched. The backend contract is the one
documented in `docs/implementation/backend/collections.md` and
`contracts/SYNC.md` §Collections.

## What & why

In Capacities, a collection is a hand-curated, ordered list of objects **of one
type**; it is deliberately not a tag, a label or a query
(<https://docs.capacities.io/reference/collections>,
<https://docs.capacities.io/reference/organizational-structures>). Lumen's
backend models a collection as an ordinary object of the default type
`collection` with two properties: `item_type_id` (the one type it holds,
immutable) and `items` (an ordered object relation). This stage surfaces that
model in the desktop client without introducing a second list engine: the
collection page reuses the existing `LumenViews`/`LumenQuery` machinery, so the
six presentations, filters, search and saved views keep working on a curated
list.

## UI flows

### Type page: collection chips

Every type page (except the synthetic `collection` type) renders a chip row
under the title, before the object view:

1. **All** — the first chip; returns to the unfiltered type view.
2. One chip per collection of the type, labelled `Name · item count` and
   ordered by title (id as tie-break). The chip tooltip repeats the count and
   says the order is curated.
3. **+** — opens the new-collection dialog. The item type is fixed to the
   current type; only the name is asked. On success the collection page opens.

Clicking a collection chip opens the collection page (not a filtered type
page), so the curated order and the collection's own title/actions are
available. Right-clicking a chip offers Open, Add objects… and Delete
collection. Dragging a card from the type view onto a chip appends that object
to the collection (`application/x-lumen-object`, the mime type `NativeCard`
already produces).

### Collection page

The page shows the item type badge, an editable collection title (the normal
document-title save path), the item count, **Add objects**, a "…" menu (Rename,
Show all `<type>` objects, Delete collection) and the shared view widget.

- The view is scoped to `item_type_id` and carries `ids` = `properties.items`
  plus `order: "manual"`, so it renders exactly the curated list in the stored
  order. Filters and search run inside that list; choosing an explicit sort in
  the view toolbar overrides the curated order until the sort is cleared.
- The empty state's "New" action is remapped to "Add objects" (the collection
  already exists; the useful action is adding existing objects).
- Right-clicking an object row/card offers **Move up**, **Move down** and
  **Remove from collection**. Move actions rewrite the whole ordered list with
  `PUT /v1/collections/{id}/items`; remove uses the item delete helper and
  keeps the order of the remaining items.

### Add objects picker

**Add objects** opens a dialog with a search field and a checkable list of
objects of the collection's type, excluding objects already in the collection.
Confirming appends the selected objects in list order through
`POST /v1/collections/{id}/items` (one request per object, sequentially). The
picker follows the existing object-picker pattern (search + list + OK/Cancel)
and never offers another type.

### Object row context menu

Object rows and cards in the type view and saved-query views have a context
menu:

- **Add to collection…** lists the collections of the object's type. A
  collection that already contains the object is shown checked and disabled.
  If the type has no collections, the submenu offers **New collection…**.
- **New collection…** creates the collection and immediately adds the object
  that opened the menu, then opens the collection page.
- Collection objects do not get the submenu: the backend forbids nested
  collections.

### Object page: Collections section

A non-journal object page shows a **Collections** section above the incoming
links. It lists the collections that contain the object as clickable chips.
The local workspace cache paints the section immediately; the authoritative
lookup is `GET /v1/objects/{id}/backlinks?type_id=collection`, per the
contract's recommended query. The panel hides itself when the object is in no
collection.

### Sidebar

Each type row is followed by its collections as indented rows with the item
count, highlighted when that collection page is active. The type context menu
can hide/show its collection rows for the session. This follows the
saved-queries pattern (a section row plus indented entries) without adding a
second navigation model.

The synthetic `collection` type is **not** listed as a type page in the
sidebar, the new-object type picker, the new-type "copy properties from"
picker or the command palette's "Create ·" list. Collections are surfaced under
their item type, which is how Capacities presents them; the type still exists
in the data model and `typeFor("collection")` supplies its icon/color.

### Empty and error states

- A type with no collections shows only **All** and **+**.
- A collection with no items shows the shared empty state plus the Add objects
  action.
- `409` (already in the collection) and `400` (wrong type, missing/trashed
  object, invalid position) are shown as the app's non-blocking notice
  (`error()`), never as a modal failure. The curated list is not changed until
  the server confirms a mutation.
- `404` from the remove helper means "not in the collection" per the contract;
  the client re-reads the collection and treats it as already removed.

## API calls used

All calls use the existing bearer-token `Api` client.

| Flow | Call |
|---|---|
| Create collection | `POST /v1/objects` with `type_id: "collection"`, `title`, `properties: {item_type_id, items: []}` |
| Rename / save title | `PATCH /v1/objects/{id}` through the normal `saveCurrent` path (full properties, `base_revision`) |
| Delete collection | `DELETE /v1/objects/{id}` (the backend's trash semantics) |
| Add one object | `POST /v1/collections/{id}/items` `{object_id}` |
| Reorder | `PUT /v1/collections/{id}/items` `{items: [...]}` |
| Remove one object | `DELETE /v1/collections/{id}/items/{objectId}` |
| Containing collections | `GET /v1/objects/{id}/backlinks?type_id=collection` |
| Candidate objects | the workspace cache loaded from `GET /v1/objects`; the contract's `GET /v1/objects?type_id=<item_type_id>` is the server-side equivalent |

The client does not call `GET /v1/collections/{id}/items` for normal rendering:
the desktop already holds every object in memory, and `properties.items` is the
authoritative order, so resolving the IDs against the cache avoids a round trip
and keeps the page responsive. The resolved endpoint remains the contract's
source of truth for clients without a full object cache.

## Order handling

- `properties.items` is the only ordering source. `property_links`/backlinks
  are a derived set and are never used to order a collection.
- The view spec uses `order: "manual"` plus `ids`. `LumenQuery::evaluate`
  ranks matching objects by their index in `ids`; an explicit `sort` array
  wins until the user clears it, because choosing a sort is a deliberate view
  decision. Objects that match the filter but are not in `ids` cannot appear
  (the `ids` filter runs first).
- Add appends (the item helper's omitted `position`), move up/down rewrites the
  whole array, and remove preserves the order of the remaining IDs.
- The presentation/sort/filter choices of a collection view are remembered per
  collection in `collectionSpecs` (`collection:<id>`), separately from the
  type page spec, so returning to a collection restores its view without
  leaking its `ids` into the type page.
- If the user is editing the collection title when an item mutation returns,
  the client merges the server's `items` and `revision` into the local object
  before the next save, so a title save cannot overwrite the item change. A
  concurrent external edit still goes through the app's normal
  `base_revision` conflict flow (409 keeps the local version and asks the user
  to review).
- Remote item changes arrive through the normal object sync/poll path; the
  open collection view re-reads `properties.items` and re-applies the manual
  scope.

## Hygiene fix included: stale page text widgets

While exercising the create-collection flow, the type page occasionally
inherited `dirty = true` from the journal page that had just been replaced.
`resetBody()` hid the outgoing `RichEditor`/`DocumentTitle` and scheduled them
for deletion, but their `textChanged` connections to `MainWindow` stayed alive
until the deferred delete ran; a queued signal could therefore call
`markDirty()` on the new page. `canLeave()` then deferred the collection
navigation behind a save that had nothing to save, and the page never opened.
The fix is two lines of hygiene: `resetBody()` disconnects the outgoing editor
and title from the window before dropping the pointers, and the type/query page
resets `dirty` after it is built (it is a read-only view until the user edits a
property or saves a view). This is the same stale-widget class as the 0.6
`clearLayout` SIGSEGV fix in `block-handle-parity.md`, caught by the
collections test running 40 consecutive times.

## Testing

`desktop/tests/collections_test.cpp` runs entirely offscreen against
`desktop/tests/fake_api.hpp`, an in-process loopback `QTcpServer` that mirrors
the backend collection validation (item type, existence, duplicates, 409/400,
ordered `PUT`, item delete, backlinks `type_id` filter, delete cleanup). It
covers:

- type page chip row: All + + on an empty type, no collection chips;
- create dialog: fixed item type, POST body, page opens, empty count;
- add picker: filtered to the type, excludes members, appends in insertion
  order;
- curated order: append, move up/down through the context menu, PUT body,
  remove preserving the remaining order, DELETE path;
- chip counts, chip click, All chip returning to the type view, manual order
  marker;
- 409 duplicate and 400 wrong-type notices without changing the list;
- object page Collections section and the
  `backlinks?type_id=collection` lookup, chip navigation;
- object-row "Add to collection…" and "New collection…" (creates and adds the
  source object);
- sidebar rows with counts, navigation, and delete cleanup.

Commands (docker image `lumen-qt-make:latest`, always `--parallel 2`):

```sh
cmake --preset test -S desktop
cmake --build .build/desktop-test --parallel 2 \
  --target lumen-views lumen-collections lumen-calendar lumen-properties \
           lumen-editor-media lumen-graph lumen-code-math lumen-tasks \
           lumen-editor-blocks
QT_QPA_PLATFORM=offscreen .build/desktop-test/lumen-collections
# ... and the other eight targets
```

All nine offscreen targets are green. `lumen-collections` is also green under
the `asan` preset (`-fsanitize=address,undefined`, leaks disabled), which
guards the page rebuild and the deferred widget deletion paths.

## Follow-ups

- **Drag reorder inside the collection view.** This stage ships the
  context-menu Move up/down path (the scope allowed either). Dragging rows in
  the list/table presentation and persisting with `PUT .../items` is the next
  step; the backend primitive is already there.
- **Live chip/sidebar counts.** `pollWorkspace` refreshes the open collection
  view, but chip rows and sidebar counts rebuild on the next navigation or
  mutation. A cheap diff of collection titles/item counts could refresh them
  in place without rebuilding the whole sidebar.
- **Shared object picker.** The Add objects dialog follows the existing
  picker pattern but is local to this stage; extracting a reusable picker
  would let "link object", "add to collection" and future multi-select flows
  share search, keyboard handling and empty states.
- **Multi-select add from the type view.** The context menu adds one object at
  a time. A selection action could call the same sequential add queue.
- **Collection type page / all-collections overview.** The synthetic
  `collection` type is hidden from navigation by design; if a global
  "all collections" view is wanted later, it should be a dedicated page rather
  than reusing the type page (whose "+" would have no fixed item type).
- **Collection covers/icons.** The backend has no cover/icon property for
  collections; the UI uses the collection type icon. Adding a cover would be a
  backend/contract change, out of scope here.
