# Explicit paged catalog

`server/catalog.go` implements the read-only `catalog/v1` DTO. The metadata-only
path selects at most 128 objects under the index read lock and never decrypts
payloads. Requested properties/excerpts fetch only the selected page, one object
at a time. Existing full-object endpoints retain their complete Document2 data.

Tests cover 140-object metadata paging with a deliberately unreadable payload
(proving no body reads), explicit excerpt reads, Unicode excerpts, property
omission flags, full-source preservation, invalid parameters and an 8 MiB page
budget with a cursor that resumes before the omitted next entry. Catalog schema
and fixtures live in `contracts/`.

The next native-client change must be explicit: adopt metadata pages for global
navigation, fetch full documents when opening editors, evict closed full-object
bodies within a bounded cache, and use property-field intents for quick actions.
Current Qt refresh/poll/views still retain the old full-object cache. Switching
that cache to partial properties without auditing replacement writes would lose
unknown properties; this implementation does not make that unsafe substitution.

Catalog `q` searches metadata only. No-match block search remains linear, and
catalog pages are discovery views rather than a transactional sync snapshot.
No new service, plaintext FTS database or unbounded plaintext body cache is used.
