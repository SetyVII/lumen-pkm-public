#!/usr/bin/env bash
# Map the Lumen API port over USB. The phone then uses 127.0.0.1:8765 with the
# exact same HTTP client as Wi-Fi, even when no Wi-Fi network is available.
set -euo pipefail

PORT="${LUMEN_PORT:-8765}"

if ! command -v adb >/dev/null 2>&1; then
  echo "adb is not installed. Install Android platform-tools." >&2
  exit 1
fi

if [[ "${1:-}" == "--remove" ]]; then
  adb reverse --remove "tcp:${PORT}" || true
  echo "USB sync mapping removed."
  exit 0
fi

STATE="$(adb get-state 2>/dev/null || true)"
if [[ "$STATE" != "device" ]]; then
  echo "No Android device authorized. Connect the phone by USB and accept the debugging prompt." >&2
  exit 1
fi

adb reverse "tcp:${PORT}" "tcp:${PORT}"
echo "USB sync ready: phone 127.0.0.1:${PORT} -> PC 127.0.0.1:${PORT}"
echo "In Lumen Mobile, pair/use http://127.0.0.1:${PORT}."
echo "Keep this mapping while the cable is connected; remove it with: $0 --remove"
