#!/usr/bin/env bash
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$script_dir/../.." && pwd)        # lumen-pkm/
workspace_root=$(cd -- "$project_root/.." && pwd)       # ${LUMEN_WORKSPACE}/

export LUMEN_API_URL=${LUMEN_API_URL:-http://127.0.0.1:8765}
token_path=${LUMEN_TOKEN_FILE:-$workspace_root/ops/personal/secrets/client.token}
if [[ -z ${LUMEN_TOKEN:-} && -r "$token_path" ]]; then
  LUMEN_TOKEN=$(cat -- "$token_path")
  export LUMEN_TOKEN
fi

bin=${LUMEN_BIN:-}
if [[ -z $bin ]]; then
  for candidate in \
    "$project_root/.build/desktop-release/lumen" \
    "$workspace_root/releases/0.6/lumen"; do
    if [[ -x $candidate ]]; then bin=$candidate; break; fi
  done
fi
if [[ -z $bin || ! -x $bin ]]; then
  printf 'No hay ejecutable. Compila con: cmake --preset release -S %s/desktop && cmake --build %s/.build/desktop-release\n' \
    "$project_root" "$project_root" >&2
  exit 1
fi
exec "$bin" "$@"
