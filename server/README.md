# Lumen server

Go API with encrypted SQLite storage (AES-256-GCM). The server decrypts objects
in memory; this is not end-to-end encryption.

## Local setup

From the repository root:

```sh
mkdir -p .build/server
(cd server && go build -o ../.build/server/lumen .)
```

Alternatively run `go build -o ../.build/server/lumen .` from `server/`.
Initialize credentials in a private directory outside the repository:

```sh
.build/server/lumen -init-secrets /path/to/private-secrets
```

Configure `LUMEN_KEY_FILE`, `LUMEN_TOKEN_FILE`, `LUMEN_DATA_DIR` and
`LUMEN_LISTEN` before starting the executable. Keep the encryption key separate
from backups. The default listener is loopback; remote access requires HTTPS
with validated server identity. Do not expose an unauthenticated HTTP listener.

Docker: build from the repository root with
`docker build -f server/Dockerfile -t lumen-pkm:local .`. The Compose example in
this directory expects locally generated files in `server/secrets/`; those files
are ignored by Git. `sandbox/compose.test.yaml` provides an isolated test server.

## API

Requests use bearer authentication. Mutations send
`X-Lumen-Document-Version: 2`. Read API discovery at `/v1/api`.
The [shared contract](../contracts/SYNC.md) defines documents, CAS writes,
idempotency, sync pages, metadata projections and property field intents.

This generation needs a coordinated client upgrade and a new document-format
library. It refuses unsupported stored objects instead of discarding their body.
Use `-fsck` to verify an encrypted store. No personal data is included.
