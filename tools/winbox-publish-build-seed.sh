#!/bin/bash
# Publish a verified build as the shared store's build seed for its commit.
#
# Usage: winbox-publish-build-seed.sh <built-worktree>
#
# A build seed is the emulator build directory (project/emscripten/Build/
# Wasm64Mt) produced by a from-scratch build of one exact commit, stored
# in the shared build-material store under build-seed/<commit>/. The
# worktree preparation step (tools/winbox-wt-prep.sh) installs the seed
# into any fresh worktree created at that commit, so that copy's first
# build only recompiles later changes instead of every file.
#
# Publishing is deliberately strict; every check must pass or nothing is
# stored:
#   - the worktree's tracked files are unmodified (the untracked payload
#     links and test results a prepared worktree always carries do not
#     count);
#   - Build/Wasm64Mt/BUILD_INFO.json names the worktree's HEAD commit;
#   - the program file's size and SHA-256 match BUILD_INFO.json.
# The store's SHA256SUMS is then refreshed for the build-seed entries, so
# the preparation step's store verification covers the seed like any
# other stored material. Publishing again for the same commit replaces
# its seed. Only the standard browser build is seeded.
set -euo pipefail
S=${BW64_SHARED_STORE:-/home/ubuntu/boxedwine64-shared}
W=${1:?usage: winbox-publish-build-seed.sh <built-worktree>}
W=$(readlink -f "$W")
[ -d "$S" ] || { echo "shared store missing: $S" >&2; exit 1; }
[ -f "$S/SHA256SUMS" ] || { echo "shared store has no SHA256SUMS: $S" >&2; exit 1; }
B="$W/project/emscripten/Build/Wasm64Mt"
[ -f "$B/BUILD_INFO.json" ] || { echo "no BUILD_INFO.json in $B; run tools/bw64-build.sh first" >&2; exit 1; }
[ -f "$B/boxedwine64.wasm" ] || { echo "no boxedwine64.wasm in $B" >&2; exit 1; }
if [ -n "$(git -C "$W" status --porcelain --untracked-files=no)" ]; then
  echo "worktree has modified tracked files; a seed must come from a clean build of one commit:" >&2
  git -C "$W" status --porcelain --untracked-files=no >&2
  exit 1
fi
HEAD_SHA=$(git -C "$W" rev-parse HEAD)
read -r WASM_SHA WASM_BYTES OBJECTS < <(python3 - "$B" "$HEAD_SHA" <<'PYEOF'
import hashlib, json, sys
from pathlib import Path
build = Path(sys.argv[1])
info = json.loads((build / "BUILD_INFO.json").read_text())
if info.get("git_sha") != sys.argv[2]:
    sys.exit("BUILD_INFO git_sha does not match worktree HEAD")
wasm = build / "boxedwine64.wasm"
digest = hashlib.file_digest(wasm.open("rb"), "sha256").hexdigest()
if digest != info.get("wasm_sha256") or wasm.stat().st_size != info.get("wasm_bytes"):
    sys.exit("boxedwine64.wasm does not match BUILD_INFO.json")
print(digest, wasm.stat().st_size, sum(1 for _ in build.rglob("*.o")))
PYEOF
)
[ -n "$WASM_SHA" ] || { echo "could not verify the build in $B" >&2; exit 1; }
mkdir -p "$S/build-seed"
rm -rf "$S"/build-seed/.publish-*
DEST="$S/build-seed/$HEAD_SHA"
TMP="$S/build-seed/.publish-$HEAD_SHA-$$"
mkdir -p "$TMP"
cp -a "$B" "$TMP/Wasm64Mt"
python3 - "$TMP/SEED.json" "$HEAD_SHA" "$W" "$WASM_SHA" "$WASM_BYTES" "$OBJECTS" <<'PYEOF'
import json, sys
from datetime import datetime, timezone
from pathlib import Path
meta = {
    "git_sha": sys.argv[2],
    "built_in": sys.argv[3],
    "wasm_sha256": sys.argv[4],
    "wasm_bytes": int(sys.argv[5]),
    "objects": int(sys.argv[6]),
    "published_at_utc": datetime.now(timezone.utc).isoformat(),
}
Path(sys.argv[1]).write_text(json.dumps(meta, indent=2) + "\n")
PYEOF
rm -rf "$DEST"
mv "$TMP" "$DEST"
# Refresh the store sums for the build-seed entries; every other entry is
# kept byte-for-byte, in the store's existing "<sha256>  ./<path>" format.
grep -v '  \./build-seed/' "$S/SHA256SUMS" > "$S/SHA256SUMS.tmp" || true
(cd "$S" && find ./build-seed -type f | LC_ALL=C sort | xargs sha256sum) >> "$S/SHA256SUMS.tmp"
mv "$S/SHA256SUMS.tmp" "$S/SHA256SUMS"
echo "published build seed for $HEAD_SHA (objects=$OBJECTS wasm_sha256=$WASM_SHA)"
echo "fresh worktrees at this commit will start from the seed; see tools/winbox-wt-prep.sh"
