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
# It also prepares the machine-wide compiler cache (see the compiler-cache
# section at the bottom): one ccache directory shared by every worktree.
#
# Finally, if the store holds a build seed for the worktree's exact HEAD
# commit, it installs the seed as the worktree's emulator build directory
# (see the build-seed section at the bottom), so the copy's first build
# only recompiles what changed after that commit instead of compiling
# every file from scratch. A seed is the verified build output of one
# exact commit, published by tools/winbox-publish-build-seed.sh.
# The store is read-only for consumers. If your work must MODIFY a
# payload, replace the link with a real copy first:
#   rm <worktree>/<path> && cp -a /home/ubuntu/boxedwine64-shared/<store-path> <worktree>/<path>
# A path that already exists in the worktree as real content is left
# untouched and reported, never overwritten.
set -euo pipefail
M=/home/ubuntu/winbox-project/repo/boxedwine64
S=${BW64_SHARED_STORE:-/home/ubuntu/winbox-project/store}
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
# --- Compiler cache ---------------------------------------------------------
# The project makefiles route compiles through ccache when it is installed on
# PATH (project/emscripten/makefile, project/linux/makefile). The cache is
# one directory shared by every worktree on this box, so a source file
# compiled in any copy is reused by all the others while its inputs and
# flags are unchanged. This step creates the directory if missing and
# asserts the size cap + compression in the cache's own config file. The cap
# is sized for this box's disk while space is tight; raise it with
#   CCACHE_DIR=/home/ubuntu/winbox-project/cache/ccache ccache --set-config max_size=<size>
# when there is room.
CACHE_DIR=/home/ubuntu/winbox-project/cache/ccache
if command -v ccache >/dev/null 2>&1; then
  mkdir -p "$CACHE_DIR"
  chmod 0777 "$CACHE_DIR" 2>/dev/null || true
  CCACHE_DIR="$CACHE_DIR" ccache --set-config max_size=2.0G >/dev/null 2>&1 || true
  CCACHE_DIR="$CACHE_DIR" ccache --set-config compression=true >/dev/null 2>&1 || true
  echo "compiler cache: $CACHE_DIR (max_size=$(CCACHE_DIR="$CACHE_DIR" ccache --get-config max_size 2>/dev/null || echo '?'), compression=$(CCACHE_DIR="$CACHE_DIR" ccache --get-config compression 2>/dev/null || echo '?'))"
else
  echo "compiler cache: ccache not installed; builds compile everything (install the ccache package to enable sharing)"
fi

# --- Build seed (incremental first build) ----------------------------------
# The store verification above has already covered the seed's bytes (it is
# part of SHA256SUMS). The seed is still verified again here on its own
# terms — its recorded commit must equal this worktree's HEAD and its
# program file must match the recorded hash — before anything is
# installed. Seeding is advisory: on any mismatch or problem the worktree
# is left unseeded, which is exactly how a fresh copy behaves today. Only
# the standard browser build (project/emscripten/Build/Wasm64Mt) is
# seeded; other targets still build from scratch on first use.
seed_note=""
seed_build() {
  local head seed_dir dst built_in old_mirror new_mirror w_abs f
  head=$(git -C "$W" rev-parse HEAD 2>/dev/null) || { seed_note="seed: cannot read worktree HEAD; skipped"; return 0; }
  w_abs=$(readlink -f "$W") || { seed_note="seed: cannot resolve worktree path; skipped"; return 0; }
  seed_dir="$S/build-seed/$head"
  dst="$w_abs/project/emscripten/Build/Wasm64Mt"
  if [ ! -d "$seed_dir" ]; then seed_note="seed: no build seed stored for ${head:0:9}; first build will compile from scratch"; return 0; fi
  if [ -e "$dst" ]; then seed_note="seed: build directory already exists; left untouched"; return 0; fi
  case "$w_abs" in *'|'*|*'&'*) seed_note="seed: worktree path contains a character the path rewrite cannot handle; skipped"; return 0;; esac
  built_in=$(python3 - "$seed_dir/SEED.json" "$seed_dir/Wasm64Mt/boxedwine64.wasm" "$head" <<'PYEOF'
import hashlib, json, sys
from pathlib import Path
try:
    meta = json.loads(Path(sys.argv[1]).read_text())
except Exception:
    sys.exit(1)
if meta.get("git_sha") != sys.argv[3] or not meta.get("built_in"):
    sys.exit(1)
wasm = Path(sys.argv[2])
if not wasm.is_file() or hashlib.file_digest(wasm.open("rb"), "sha256").hexdigest() != meta.get("wasm_sha256"):
    sys.exit(1)
print(meta["built_in"])
PYEOF
  ) || { seed_note="seed: stored seed for ${head:0:9} failed verification; skipped"; return 0; }
  case "$built_in" in *'|'*|*'&'*) seed_note="seed: seed origin path contains a character the path rewrite cannot handle; skipped"; return 0;; esac
  mkdir -p "$(dirname "$dst")"
  if ! cp -a "$seed_dir/Wasm64Mt" "$dst"; then
    seed_note="seed: copying the seed failed; worktree left unseeded"; rm -rf "$dst"; return 0
  fi
  # Object files live under Build/Wasm64Mt/<origin tree's absolute path>/,
  # mirroring the source tree, and the generated dependency files name
  # that same prefix. Move the mirrored subtree to this worktree's path
  # and rewrite the prefix in the dependency files; every other path in
  # them is relative to the tree or a system path valid on this machine.
  old_mirror="${built_in#/}"; new_mirror="${w_abs#/}"
  if [ "$old_mirror" != "$new_mirror" ]; then
    mkdir -p "$dst/$(dirname "$new_mirror")"
    if ! mv "$dst/$old_mirror" "$dst/$new_mirror"; then
      seed_note="seed: relocating the seed failed; worktree left unseeded"; rm -rf "$dst"; return 0
    fi
    if ! find "$dst" -name '*.d' -exec sed -i "s|$built_in|$w_abs|g" {} +; then
      seed_note="seed: rewriting dependency paths failed; worktree left unseeded"; rm -rf "$dst"; return 0
    fi
  fi
  # Seeded files must look newer than this copy's freshly checked-out
  # sources, with the link outputs newest of all, so the first build
  # treats the seed as up to date and only recompiles real changes.
  if ! find "$dst" -type f -exec touch {} +; then
    seed_note="seed: refreshing seed timestamps failed; worktree left unseeded"; rm -rf "$dst"; return 0
  fi
  for f in boxedwine64.html boxedwine64.js boxedwine64.wasm boxedwine64.worker.js; do
    if [ -f "$dst/$f" ]; then touch -d '+2 seconds' "$dst/$f"; fi
  done
  seed_note="seed: installed verified build seed for ${head:0:9} (built in $built_in); first build will only recompile changes"
  return 0
}
seed_build || true
if [ -n "$seed_note" ]; then echo "$seed_note"; fi

echo "prepared $W (linked=$linked kept-existing=$kept; copied 0 payload bytes)"
echo "note: payload paths are links into $S; to modify a payload, replace its link with a real copy first (see header)."
