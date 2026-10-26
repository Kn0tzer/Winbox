#!/bin/bash
# stage-vkd3d.sh — stage the vkd3d-proton bring-up payload into dist/runtime/prefix64.zip.
#
# What goes in, and why it goes THERE (mirrors tools/dxvk/stage-dxvk.sh):
#   tri12.exe + d3d12.dll + d3d12core.dll at BOTH home/username/ (= Z:\home\username\,
#   the spawn working dir) and home/username/.wine/drive_c/ (= C:\).
#   The DLLs sit NEXT TO the app (not in system32): wine's DLL search checks
#   the app directory first, so with WINEDLLOVERRIDES=d3d12=n;d3d12core=n the
#   vkd3d-proton build loads without touching the shared system32 (which is a
#   .link into the wine install and must stay pristine). d3d12core.dll has no
#   Wine builtin counterpart in system32 search order problems either way —
#   app-dir placement is what vkd3d-proton's own setup script does (it copies
#   both DLLs into the prefix's system32; app-dir is the least invasive
#   equivalent for this tree).
#
# Build the DLLs first (see tools/vkd3d/BUILD.md):
#   meson setup build.w64 src --cross-file src/build-win64.txt \
#     --buildtype release -Denable_tests=false -Denable_extras=false
#   ninja -C build.w64
# (this box's mingw pathcch.h lacks PATHCCH_NONE; configure with
#  CFLAGS=-DPATHCCH_NONE=0x00000000 CXXFLAGS=-DPATHCCH_NONE=0x00000000)
#
# Usage: tools/vkd3d/stage-vkd3d.sh [PREFIX_ZIP]
#   default PREFIX_ZIP = dist/runtime/prefix64.zip
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
ZIP="${1:-$REPO/dist/runtime/prefix64.zip}"
D3D12="$HERE/build.w64/libs/d3d12/d3d12.dll"
D3D12CORE="$HERE/build.w64/libs/d3d12core/d3d12core.dll"
TRI12="$HERE/probe/tri12.exe"

[ -f "$ZIP" ]       || { echo "error: $ZIP missing (build the dist first: tools/bw64-build.sh)" >&2; exit 1; }
[ -f "$D3D12" ]     || { echo "error: $D3D12 missing (build vkd3d first, see tools/vkd3d/BUILD.md)" >&2; exit 1; }
[ -f "$D3D12CORE" ] || { echo "error: $D3D12CORE missing (build vkd3d first, see tools/vkd3d/BUILD.md)" >&2; exit 1; }
[ -f "$TRI12" ]     || { echo "error: $TRI12 missing" >&2; exit 1; }

STAGE="$(mktemp -d)"
trap 'rm -rf "$STAGE"' EXIT
mkdir -p "$STAGE/home/username" "$STAGE/home/username/.wine/drive_c"
cp "$TRI12" "$D3D12" "$D3D12CORE" "$STAGE/home/username/"
cp "$TRI12" "$D3D12" "$D3D12CORE" "$STAGE/home/username/.wine/drive_c/"
( cd "$STAGE" && zip -q "$ZIP" home/username/tri12.exe home/username/d3d12.dll home/username/d3d12core.dll \
    home/username/.wine/drive_c/tri12.exe home/username/.wine/drive_c/d3d12.dll \
    home/username/.wine/drive_c/d3d12core.dll )
echo "--- $ZIP (vkd3d payload) ---"
unzip -l "$ZIP" | grep -E "tri12|d3d12" || true
echo "=== staged. WINEDLLOVERRIDES=d3d12=n;d3d12core=n selects the native dlls at runtime ==="
