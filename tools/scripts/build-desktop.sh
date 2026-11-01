#!/usr/bin/env bash
# Compila el escritorio en .build/desktop-release. No sobrescribe un binario en
# uso: para publicar, usa --install (copia a releases/<versión>/lumen).
set -euo pipefail
script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
project_root=$(cd -- "$script_dir/../.." && pwd)
workspace_root=$(cd -- "$project_root/.." && pwd)
build_dir="$project_root/.build/desktop-release"

if command -v cmake >/dev/null 2>&1; then
  cmake --preset release -S "$project_root/desktop"
  cmake --build "$build_dir" --parallel 2
else
  docker run --rm -u "$(id -u):$(id -g)" \
    -v "$workspace_root:/work" -w /work/lumen-pkm \
    lumen-qt-make:latest bash -lc 'cmake --preset release -S desktop && cmake --build .build/desktop-release --parallel 2'
fi

if [[ "${1:-}" == "--install" ]]; then
  dest="$workspace_root/releases/0.6/lumen"
  mkdir -p "$(dirname -- "$dest")"
  tmp="$dest.tmp.$$"
  cp -- "$build_dir/lumen" "$tmp"
  chmod 0755 "$tmp"
  mv -f -- "$tmp" "$dest"
  printf 'Instalado: %s\n' "$dest"
else
  printf 'Ejecutable: %s\n' "$build_dir/lumen"
fi
