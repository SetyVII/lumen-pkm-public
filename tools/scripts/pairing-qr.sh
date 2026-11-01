#!/usr/bin/env bash
# Print a pairing URI (and a terminal QR when qrencode is installed) for the
# Lumen Android app. The token is read from the private secret file and is never
# written to the repository.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"   # raíz del workspace
API="${LUMEN_API_URL:-http://127.0.0.1:8765}"
TOKEN_FILE="${LUMEN_TOKEN_FILE:-${ROOT}/ops/personal/secrets/client.token}"
CERT_FILE="${LUMEN_TLS_CERT:-}"

if [[ ! -r "$TOKEN_FILE" ]]; then
  echo "Token file not readable: $TOKEN_FILE" >&2
  echo "Set LUMEN_TOKEN_FILE to your private client token." >&2
  exit 1
fi

TOKEN="$(tr -d '\r\n' < "$TOKEN_FILE")"
FINGERPRINT=""
if [[ -n "$CERT_FILE" && -r "$CERT_FILE" ]]; then
  FINGERPRINT="$(openssl x509 -in "$CERT_FILE" -noout -fingerprint -sha256 2>/dev/null | sed 's/^.*=//; s/://g' | tr '[:upper:]' '[:lower:]')"
fi

URI="$(python3 - "$API" "$TOKEN" "$FINGERPRINT" <<'PY'
import sys, urllib.parse
api, token, fingerprint = sys.argv[1:4]
params = {"api": api.rstrip("/"), "token": token}
if fingerprint:
    params["fp"] = fingerprint
print("lumen://pair?" + urllib.parse.urlencode(params))
PY
)"

echo "$URI"
if command -v qrencode >/dev/null 2>&1; then
  qrencode -t ANSIUTF8 "$URI"
else
  echo "Install qrencode to render this URI as a terminal QR: qrencode -t ANSIUTF8 '<uri>'"
fi
