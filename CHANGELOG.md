# Document 2

- Shared structured documents for all object types, including media annotations.
- Atomic versioned edits, encrypted history, idempotent receipts and explicit conflicts.
- Bounded sync pages, metadata catalog and field-level property updates.
- Native Qt editor with tables, images, dates, block references and structured clipboard.
- Android persistence, outbox recovery and coordinated protocol support.

This is a coordinated breaking change: document schema 2, sync protocol 2 and
storage schema 6. Use matching clients and a new library. Existing APK downloads
are historical builds; compile current sources for this protocol.

Advanced editor interactions and long-session memory profiling remain ongoing.
