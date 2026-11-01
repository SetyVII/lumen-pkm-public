package main

// collectionItemIDs resolves the ordered items of a collection object.
//
// A collection is an object of type "collection" whose `items` property is an
// object relation (a single id or a list of ids). The collections stage owns
// the type definition and the mutation endpoints; list filters only need this
// read helper. Keeping the lookup in one function lets the merge with
// backend/collections replace it with the collections helper without touching
// the filter parser or the relation matcher.
func (a *App) collectionItemIDs(id string) ([]string, error) {
	o, err := a.store.Object(id)
	if err != nil {
		return nil, invalidFilter("collection %q does not exist", id)
	}
	if o.TypeID != "collection" {
		return nil, invalidFilter("object %q is not a collection", id)
	}
	return collectionItemIDs(o.Properties["items"])
}
