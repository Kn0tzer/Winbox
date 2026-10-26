#!/bin/bash
# stage-vkd3d.sh — stage the vkd3d-proton bring-up PROBE payload into
# dist/runtime/prefix64.zip: tri12.exe only.
#
# tri12.exe goes at BOTH home/username/ (= Z:\home\username\, the spawn
# working dir) and home/username/.wine/drive_c/ (= C:\), mirroring
# tools/dxvk/stage-dxvk.sh.
#
# The DLLs are NOT staged here any more. Since the 2026-10-26 restage,
# d3d12.dll + d3d12core.dll (the vkd3d-proton build) and the bw64
# vulkan-1.dll PE shim ship at SYSTEM PLACEMENT inside wine64.zip's
# x86_64-windows tree (the prefix's system32 is a .link into that tree),
# staged by tools/rootfs64/stage-vkd3d-payloads.sh and, canonically, by
# tools/rootfs64/build-wine64-zip.sh. Guests resolve them with
# WINEDLLOVERRIDES=d3d12=n;d3d12core=n — no app-dir copies, which is
# what lets arbitrary D3D12 programs (not just probes staged next to
# their DLLs) create devices in the demo environment.
#
# Usage: tools/vkd3d/stage-vkd3d.sh [PREFIX_ZIP]
#   default PREFIX_ZIP = dist/runtime/prefix64.zip
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
ZIP="${1:-$REPO/dist/runtime/prefix64.zip}"
TRI12="$HERE/probe/tri12.exe"

[ -f "$ZIP" ]   || { echo "error: $ZIP missing (build the dist first: tools/bw64-build.sh)" >&2; exit 1; }
[ -f "$TRI12" ] || { echo "error: $TRI12 missing" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/home/username" "$STAGE/home/username/.wine/drive_c"
cp "$TRI12" "$STAGE/home/username/"
cp "$TRI12" "$STAGE/home/username/.wine/drive_c/"
( cd "$STAGE" && zip -q "$ZIP" home/username/tri12.exe \
    home/username/.wine/drive_c/tri12.exe )
# Drop any pre-restage app-dir d3d12 copies: resolution must come from
# wine64.zip system placement so the staged probe proves the same path
# demos use.
zip -q -d "$ZIP" home/username/d3d12.dll home/username/d3d12core.dll \
    home/username/.wine/drive_c/d3d12.dll home/username/.wine/drive_c/d3d12core.dll 2>/dev/null || true
echo "--- $ZIP (vkd3d probe payload) ---"
unzip -l "$ZIP" | grep -E "tri12|d3d12" || true
echo "=== staged. d3d12.dll/d3d12core.dll resolve from wine64.zip system placement; WINEDLLOVERRIDES=d3d12=n;d3d12core=n selects them ==="
