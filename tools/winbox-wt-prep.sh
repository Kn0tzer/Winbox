#!/bin/bash
# Prepare a winbox worktree so tools/bw64-build.sh + the dist export work
# offline, consuming the shared build-material store instead of copying
# generated payloads into every worktree.
#
# Usage: winbox-wt-prep.sh <worktree>
#
# The store (/home/ubuntu/boxedwine64-shared, override with
# BW64_SHARED_STORE) holds each generated payload input exactly once:
# the pinned runtime zips and Slang assets, the DXVK and vkd3d build
# outputs, the tools/rootfs64 payload extractions (gecko, openal,
# mono32, gstreamer32/64, cjk-font) and the tools/browser tool payloads
# (zig, disasm). This script verifies the store before linking:
#   - the pinned runtime assets must match the worktree's committed
#     tools/browser/runtime-assets.json (the project's own pins);
#   - every stored file must match the store's SHA256SUMS.
# Any mismatch aborts before anything is linked. It then LINKS each
# payload into the worktree (copies 0 payload bytes) and links
# node_modules + web/vendor from the main repo, as the previous prep
# did. It does NOT overwrite tools/browser/runtime-assets.json: with
# the store's zips matching the committed pins, bootstrap.py verifies
# offline against the committed manifest, and the old local-manifest
# overwrite (stale 20261013 pins; "git checkout before committing")
# is gone.
#
# The store is read-only for consumers. If your work must MODIFY a
# payload, replace the link with a real copy first:
#   rm <worktree>/<path> && cp -a /home/ubuntu/boxedwine64-shared/<store-path> <worktree>/<path>
# A path that already exists in the worktree as real content is left
# untouched and reported, never overwritten.
set -euo pipefail
M=/home/ubuntu/boxedwine64
S=${BW64_SHARED_STORE:-/home/ubuntu/boxedwine64-shared}
W=${1:?usage: winbox-wt-prep.sh <worktree>}
[ -e "$W/.git" ] || { echo "not a worktree: $W" >&2; exit 1; }
[ -d "$S" ] || { echo "shared store missing: $S" >&2; exit 1; }
[ -f "$S/SHA256SUMS" ] || { echo "shared store has no SHA256SUMS: $S" >&2; exit 1; }

echo "== verifying store against the project's committed pins =="
python3 - "$W/tools/browser/runtime-assets.json" "$S" <<'PYEOF'
import hashlib, json, sys
from pathlib import Path
manifest = json.loads(Path(sys.argv[1]).read_text())
store = Path(sys.argv[2])
bad = []
for entry in manifest["files"]:
    p = entry["path"]
    if p.startswith("web/runtime/"):
        local = store / "web-runtime" / Path(p).name
    elif p.startswith("tools/browser/slang/"):
        local = store / "slang" / p[len("tools/browser/slang/"):]
    else:
        bad.append((p, "unmapped path")); continue
    if not local.is_file() or local.stat().st_size != entry["bytes"]:
        bad.append((p, "missing/size")); continue
    if hashlib.file_digest(local.open("rb"), "sha256").hexdigest() != entry["sha256"]:
        bad.append((p, "sha256 mismatch"))
if bad:
    for b in bad: print("PIN MISMATCH:", b)
    sys.exit(1)
print(f"pins OK ({len(manifest['files'])} files)")
PYEOF

echo "== verifying store integrity (SHA256SUMS) =="
(cd "$S" && sha256sum -c SHA256SUMS --quiet) || { echo "store integrity check FAILED; not linking anything" >&2; exit 1; }
echo "store integrity OK"

linked=0; kept=0
link_dir() { # $1 = store subpath, $2 = worktree-relative path
  local src="$S/$1" dst="$W/$2"
  [ -e "$src" ] || { echo "store is missing $1" >&2; exit 1; }
  if [ -L "$dst" ]; then
    if [ "$(readlink "$dst")" = "$src" ]; then
      echo "ok (already linked): $2"; linked=$((linked+1))
    else
      echo "WARN: $2 is a symlink to $(readlink "$dst"); left as-is"; kept=$((kept+1))
    fi
    return
  fi
  if [ -e "$dst" ]; then
    echo "WARN: $2 exists as real content; left untouched"; kept=$((kept+1)); return
  fi
  mkdir -p "$(dirname "$dst")"
  ln -s "$src" "$dst"
  echo "linked: $2 -> $src"; linked=$((linked+1))
}
link_file() { link_dir "$1" "$2"; }

# Pinned runtime zips, linked per file: web/runtime stays a real
# directory because the build writes its compiled pair into it and
# bootstrap.py may replace a zip in place (which then replaces the
# link, never the store).
mkdir -p "$W/web/runtime"
for z in "$S"/web-runtime/*.zip; do
  link_file "web-runtime/$(basename "$z")" "web/runtime/$(basename "$z")"
done
# Pinned Slang assets, per file for the same reason.
mkdir -p "$W/tools/browser/slang"
for f in slang-wasm.js slang-wasm.wasm interface.d.ts; do
  link_file "slang/$f" "tools/browser/slang/$f"
done
link_dir "slang/third-party-notices" "tools/browser/slang/third-party-notices"
# Generated payload directories (all consumers read only).
link_dir "dxvk/build.w64" "tools/dxvk/build.w64"
link_dir "dxvk/build.w32" "tools/dxvk/build.w32"
link_dir "dxvk/install" "tools/dxvk/install"
link_dir "vkd3d/build.w64" "tools/vkd3d/build.w64"
link_dir "vkd3d/build.w32" "tools/vkd3d/build.w32"
for d in gecko openal mono32 gstreamer64 gstreamer32 cjk-font; do
  link_dir "rootfs64/$d" "tools/rootfs64/$d"
done
link_dir "browser/zig" "tools/browser/zig"
link_dir "browser/disasm" "tools/browser/disasm"
# Shared dependency trees (unchanged from the previous prep).
[ -e "$W/node_modules" ] || ln -s "$M/node_modules" "$W/node_modules"
[ -e "$W/web/vendor" ] || ln -s "$M/web/vendor" "$W/web/vendor"
echo "prepared $W (linked=$linked kept-existing=$kept; copied 0 payload bytes)"
echo "note: payload paths are links into $S; to modify a payload, replace its link with a real copy first (see header)."
