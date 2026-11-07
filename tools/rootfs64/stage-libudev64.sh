#!/usr/bin/env bash
# Merge libudev.so.1 (tools/rootfs64/libudev1-x86_64/, the genuine
# Debian bookworm libudev1 binary) into the shipped wine64.zip
# archives. winebus's udev backend dlopens libudev.so.1 to enumerate
# evdev devices; the 64-bit runtime never carried it, so the backend
# stayed dead and no evdev device (the game controller) ever became
# a guest device on the 64-bit side (FD-195). The 32-bit runtime
# ships its i386 libudev the same way, as a regular file under its
# SONAME.
#
# The canonical path is build-wine64-zip.sh (container staging; its
# image recipe now installs libudev1 as well); this script is the
# merge-script path for use when the container build is unavailable,
# following stage-wine8-backports.sh. Idempotent: an archive whose
# member already hashes to the pinned bytes is skipped. See
# libudev1-x86_64/README.md for provenance.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
MEMBER="usr/lib/x86_64-linux-gnu/libudev.so.1"
SRC="$HERE/libudev1-x86_64/libudev.so.1"
[[ -f "$SRC" ]] || { echo "stage-libudev64: missing $SRC" >&2; exit 2; }
want_hash="$(sha256sum "$SRC" | awk '{print $1}')"

stamp="$(date -u +%Y%m%d-%H%M%S)"
for runtime in "$REPO/web/runtime" "$REPO/dist/runtime"; do
    [[ -d "$runtime" ]] || continue
    [[ ! -L "$runtime" ]] || { echo "stage-libudev64: refuse symlinked runtime dir $runtime" >&2; exit 2; }
    archive="$runtime/wine64.zip"
    [[ -f "$archive" ]] || { echo "stage-libudev64: expected $archive" >&2; exit 2; }
    member_hash="$(python3 - "$archive" "$MEMBER" <<'EOF'
import hashlib, sys, zipfile
try:
    with zipfile.ZipFile(sys.argv[1]) as z:
        print(hashlib.sha256(z.read(sys.argv[2])).hexdigest())
except KeyError:
    print("absent")
EOF
)"
    if [[ "$member_hash" == "$want_hash" ]]; then
        echo "stage-libudev64: $archive already carries libudev.so.1 — skipping"
        continue
    fi
    backup="/home/ubuntu/winbox-project/evidence/winbox-libudev64-$stamp"
    mkdir -p "$backup"
    cp "$archive" "$backup/$(basename "$runtime")-wine64.zip"
    TMP="$(mktemp -d)"
    mkdir -p "$TMP/payload/usr/lib/x86_64-linux-gnu"
    cp "$SRC" "$TMP/payload/$MEMBER"
    ( cd "$TMP/payload" && zip -qry9 "$TMP/payload.zip" usr )
    python3 - "$archive" "$TMP/payload.zip" <<'EOF'
import os, sys, zipfile
dst, src = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst) as zdst, zipfile.ZipFile(src) as zsrc:
    fresh = {info.filename: zsrc.read(info.filename) for info in zsrc.infolist()}
    with zipfile.ZipFile(dst + '.merged', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for info in zdst.infolist():
            if info.filename not in fresh:
                out.writestr(info, zdst.read(info.filename))  # keep ZipInfo: symlink bits (external_attr), dates, modes
        for name, data in fresh.items():
            out.writestr(name, data)
os.replace(dst + '.merged', dst)
EOF
    rm -rf "$TMP"
    member_hash="$(python3 - "$archive" "$MEMBER" <<'EOF'
import hashlib, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    print(hashlib.sha256(z.read(sys.argv[2])).hexdigest())
EOF
)"
    [[ "$member_hash" == "$want_hash" ]] || { echo "stage-libudev64: final verification failed for $MEMBER in $archive" >&2; exit 2; }
    echo "stage-libudev64: libudev.so.1 staged in $archive (backup in $backup)"
done
