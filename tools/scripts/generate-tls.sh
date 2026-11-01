#!/usr/bin/env bash
# Create a local TLS certificate for Lumen Mobile. Use the LAN IP or hostname
# that the phone will connect to. Existing files are never overwritten.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)"   # raíz del workspace
OUT="${LUMEN_TLS_DIR:-${ROOT}/ops/personal/secrets}"
HOST="${1:-}"

if [[ -z "$HOST" ]]; then
  echo "Usage: $0 <lan-ip-or-hostname>" >&2
  exit 1
fi
if [[ -e "$OUT/tls.crt" || -e "$OUT/tls.key" ]]; then
  echo "Refusing to overwrite existing $OUT/tls.crt or tls.key" >&2
  exit 1
fi
mkdir -p "$OUT"
chmod 700 "$OUT"

openssl req -x509 -newkey rsa:3072 -sha256 -days 825 -nodes \
  -keyout "$OUT/tls.key" -out "$OUT/tls.crt" \
  -subj "/CN=${HOST}" \
  -addext "subjectAltName=DNS:${HOST},IP:${HOST}" >/dev/null 2>&1
chmod 600 "$OUT/tls.key"

echo "Created $OUT/tls.crt and $OUT/tls.key"
echo "Start the server with:"
echo "  LUMEN_LISTEN=0.0.0.0:8765 LUMEN_TLS_CERT=$OUT/tls.crt LUMEN_TLS_KEY=$OUT/tls.key LUMEN_MDNS=1 ..."
echo "Pairing fingerprint:"
openssl x509 -in "$OUT/tls.crt" -noout -fingerprint -sha256
