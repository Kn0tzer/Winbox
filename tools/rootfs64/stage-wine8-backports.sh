#!/usr/bin/env bash
# Merge the Wine 8.0 loader-backport DLLs (tools/rootfs64/
# wine8-backports-x86_64/: user32, kernelbase, kernel32, iphlpapi, win32u,
# msvcp140, setupapi —
# rebuilt from the pinned wine-8.0 source with the wine8-*.patch
# backports) into the shipped wine64.zip archives, REPLACING the Debian
# wine 8.0 members, WITHOUT a full rootfs rebuild.
#
# The canonical path is build-wine64-zip.sh (container staging), which
# performs exactly these copies; this script is the merge-script path
# for use when the container build is unavailable — on this ARM64 host
# the linux/amd64 container's binfmt/QEMU layer crashes during staging
# (x86_64-binfmt-P: QEMU internal SIGSEGV, 2026-10-25). Idempotent: an
# archive whose members already hash to the pinned backport bytes is
# skipped. See wine8-backports-x86_64/README.md for provenance.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WIN64_DIR="usr/lib/x86_64-linux-gnu/wine/x86_64-windows"
UNIX_DIR="usr/lib/x86_64-linux-gnu/wine/x86_64-unix"
DLLS=(user32.dll kernelbase.dll kernel32.dll iphlpapi.dll win32u.dll msvcp140.dll setupapi.dll ucrtbase.dll msvcrt.dll msvcr70.dll msvcr71.dll msvcr80.dll msvcr90.dll msvcr100.dll msvcr110.dll msvcr120.dll)
for dll in "${DLLS[@]}"; do
    [[ -f "$HERE/wine8-backports-x86_64/$dll" ]] || { echo "stage-wine8-backports: missing $HERE/wine8-backports-x86_64/$dll — run tools/rootfs64/build-wine8-backports.sh" >&2; exit 2; }
done
# The unix half of win32u ships with its PE half: the desktop-wait
# patch (FD-01) lives in win32u.so, and staging the DLL alone ships
# the fix in no binary (FD-43).
[[ -f "$HERE/wine8-backports-x86_64/win32u.so" ]] || { echo "stage-wine8-backports: missing $HERE/wine8-backports-x86_64/win32u.so — run tools/rootfs64/build-wine8-backports.sh" >&2; exit 2; }

stamp="$(date -u +%Y%m%d-%H%M%S)"
for runtime in "$REPO/web/runtime" "$REPO/dist/runtime"; do
    [[ -d "$runtime" ]] || continue
    [[ ! -L "$runtime" ]] || { echo "stage-wine8-backports: refuse symlinked runtime dir $runtime" >&2; exit 2; }
    archive="$runtime/wine64.zip"
    [[ -f "$archive" ]] || { echo "stage-wine8-backports: expected $archive" >&2; exit 2; }
    # Skip only when every member already carries the pinned bytes.
    current=1
    so_hash="$(python3 - "$archive" "$UNIX_DIR/win32u.so" <<'EOF'
import hashlib, sys, zipfile
try:
    with zipfile.ZipFile(sys.argv[1]) as z:
        print(hashlib.sha256(z.read(sys.argv[2])).hexdigest())
except KeyError:
    print("absent")
EOF
)"
    [[ "$so_hash" == "$(sha256sum "$HERE/wine8-backports-x86_64/win32u.so" | awk '{print $1}')" ]] || current=0
    for dll in "${DLLS[@]}"; do
        member_hash="$(python3 - "$archive" "$WIN64_DIR/$dll" <<'EOF'
import hashlib, sys, zipfile
try:
    with zipfile.ZipFile(sys.argv[1]) as z:
        print(hashlib.sha256(z.read(sys.argv[2])).hexdigest())
except KeyError:
    print("absent")
EOF
)"
        want_hash="$(sha256sum "$HERE/wine8-backports-x86_64/$dll" | awk '{print $1}')"
        [[ "$member_hash" == "$want_hash" ]] || { current=0; break; }
    done
    if [[ "$current" == "1" ]]; then
        echo "stage-wine8-backports: $archive already carries the backport DLLs — skipping"
        continue
    fi
    backup="/home/ubuntu/winbox-project/evidence/winbox-wine8-backports-$stamp"
    mkdir -p "$backup"
    cp "$archive" "$backup/$(basename "$runtime")-wine64.zip"
    TMP="$(mktemp -d)"
    mkdir -p "$TMP/payload/$WIN64_DIR" "$TMP/payload/$UNIX_DIR"
    for dll in "${DLLS[@]}"; do
        cp "$HERE/wine8-backports-x86_64/$dll" "$TMP/payload/$WIN64_DIR/"
    done
    cp "$HERE/wine8-backports-x86_64/win32u.so" "$TMP/payload/$UNIX_DIR/"
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
    for dll in "${DLLS[@]}"; do
        member_hash="$(python3 - "$archive" "$WIN64_DIR/$dll" <<'EOF'
import hashlib, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    print(hashlib.sha256(z.read(sys.argv[2])).hexdigest())
EOF
)"
        want_hash="$(sha256sum "$HERE/wine8-backports-x86_64/$dll" | awk '{print $1}')"
        [[ "$member_hash" == "$want_hash" ]] || { echo "stage-wine8-backports: final verification failed for $dll in $archive" >&2; exit 2; }
    done
    so_hash="$(python3 - "$archive" "$UNIX_DIR/win32u.so" <<'EOF'
import hashlib, sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    print(hashlib.sha256(z.read(sys.argv[2])).hexdigest())
EOF
)"
    [[ "$so_hash" == "$(sha256sum "$HERE/wine8-backports-x86_64/win32u.so" | awk '{print $1}')" ]] || { echo "stage-wine8-backports: final verification failed for win32u.so in $archive" >&2; exit 2; }
    echo "stage-wine8-backports: backport DLLs staged in $archive (backup in $backup)"
done
