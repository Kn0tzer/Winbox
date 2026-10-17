#!/bin/bash
# stage-dxvk32.sh — stage 32-bit DXVK DLLs into dist/runtime/prefix32.zip.
#
# Mirrors stage-dxvk.sh (64-bit): DLLs go at home/username/ (= Z:\home\username\,
# the spawn working dir) so wine's DLL search finds them next to the app.
# With WINEDLLOVERRIDES=d3d9=n;d3d11=n;dxgi=n the native DXVK builds load
# instead of Wine's builtin wined3d-based implementations.
#
# Usage: tools/dxvk/stage-dxvk32.sh [PREFIX_ZIP]
#   default PREFIX_ZIP = dist/runtime/prefix32.zip (and web/runtime/prefix32.zip)
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
D3D9="$HERE/build.w32/src/d3d9/d3d9.dll"
D3D11="$HERE/build.w32/src/d3d11/d3d11.dll"
DXGI="$HERE/build.w32/src/dxgi/dxgi.dll"

for f in "$D3D9" "$D3D11" "$DXGI"; do
  [ -f "$f" ] || { echo "error: $f missing (build 32-bit DXVK first)" >&2; exit 1; }
done

stage_one() {
  local ZIP="$1"
  [ -f "$ZIP" ] || { echo "error: $ZIP missing" >&2; exit 1; }
  local STAGE="$(mktemp -d)"
  trap 'rm -rf ""' EXIT
  mkdir -p "$STAGE/home/username"
  cp "$D3D9" "$D3D11" "$DXGI" "$STAGE/home/username/"
  ( cd "$STAGE" && zip -q "$ZIP" home/username/d3d9.dll home/username/d3d11.dll home/username/dxgi.dll )
  trap - EXIT
  rm -rf "$STAGE"
  echo "--- $ZIP (dxvk32 payload) ---"
  unzip -l "$ZIP" | grep -E "d3d9.dll|d3d11.dll|dxgi.dll" | grep "home/username/" || true
}

if [ $# -ge 1 ]; then
  stage_one "$1"
else
  stage_one "$REPO/dist/runtime/prefix32.zip"
  stage_one "$REPO/web/runtime/prefix32.zip"
fi
echo "=== staged. WINEDLLOVERRIDES=d3d9=n;d3d11=n;dxgi=n selects native dlls at runtime ==="
