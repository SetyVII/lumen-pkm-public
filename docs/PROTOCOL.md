# Data protocol

Every object owns a structured document, independently of its binary assets.
[Document semantics](architecture/document-v2.md), the JSON schemas and fixtures
in `contracts/` define node types, inline marks, temporal values and commands.

[HTTP/sync contracts](../contracts/SYNC.md) describe durable CAS writes,
idempotent retries, bounded pull pages and field-level metadata changes.
[Catalog](implementation/backend/catalog.md) documents read-only projections.

Documents retain state and stable identities across server, Qt and Android.
Plain text, thumbnails and rendered formula images are derived representations.
Local cursor selection and local undo are distinct from persistent object history.
