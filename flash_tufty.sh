#!/usr/bin/env bash
set -euo pipefail
project_dir="$(cd "$(dirname "$0")" && pwd)"
case "${1:-}" in
    "") exec "$project_dir/tools/build.sh" ;;
    --flash) test "$#" -eq 1 || { echo "Usage: $0 [--flash]" >&2; exit 2; } ;;
    --help|-h) echo "Usage: $0 [--flash] (default: build only; --flash requires BOOTSEL and USB-capable picotool)"; exit 0 ;;
    *) echo "Usage: $0 [--flash]" >&2; exit 2 ;;
esac
command -v picotool >/dev/null || { echo "Install picotool with USB support first" >&2; exit 1; }
"$project_dir/tools/build.sh"
uf2_file="${TUFTY_ARTIFACT_DIR:-$project_dir/dist}/tufty-nes-flash.uf2"
picotool info
picotool load "$uf2_file"
picotool verify "$uf2_file"
picotool reboot
