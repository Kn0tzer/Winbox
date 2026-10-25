#!/usr/bin/env bash
# Merge the winehq10 backport DLLs + the GStreamer plugin set into the
# shipped wine64.zip archives WITHOUT a full rootfs rebuild.
#
# The canonical path is build-wine64-zip.sh (container staging), which
# performs exactly these copies; this script is the wine64 analogue of
# stage-gstreamer.sh (which does the same payload merge for wine32.zip)
# for use when the container build is unavailable — on this ARM64 host
# the linux/amd64 container's binfmt/QEMU layer crashes during staging
# (x86_64-binfmt-P: QEMU internal SIGSEGV, 2026-10-25). Idempotent:
# archives that already carry the payloads are skipped.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WIN64_DIR="usr/lib/x86_64-linux-gnu/wine/x86_64-windows"
for dll in d3d12core.dll msmpeg2vdec.dll msvcp140_codecvt_ids.dll; do
    [[ -f "$HERE/winehq10-x86_64/$dll" ]] || { echo "stage-wine64-payloads: missing $HERE/winehq10-x86_64/$dll" >&2; exit 2; }
done
[[ -d "$HERE/gstreamer64/usr/lib/x86_64-linux-gnu/gstreamer-1.0" ]] || {
    echo "stage-wine64-payloads: missing gstreamer64 payload — run prepare-gstreamer.py" >&2; exit 2; }

stamp="$(date -u +%Y%m%d-%H%M%S)"
for runtime in "$REPO/web/runtime" "$REPO/dist/runtime"; do
    [[ -d "$runtime" ]] || continue
    [[ ! -L "$runtime" ]] || { echo "stage-wine64-payloads: refuse symlinked runtime dir $runtime" >&2; exit 2; }
    archive="$runtime/wine64.zip"
    [[ -f "$archive" ]] || { echo "stage-wine64-payloads: expected $archive" >&2; exit 2; }
    if unzip -Z1 "$archive" | grep -qx 'usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstlibav.so' \
       && unzip -Z1 "$archive" | grep -qx "$WIN64_DIR/d3d12core.dll" \
       && unzip -Z1 "$archive" | grep -qx "$WIN64_DIR/msvcp140_codecvt_ids.dll"; then
        echo "stage-wine64-payloads: $archive already carries DLLs + plugins — skipping"
        continue
    fi
    backup="/home/ubuntu/backups/winbox-wine64-payloads-$stamp"
    mkdir -p "$backup"
    cp "$archive" "$backup/$(basename "$runtime")-wine64.zip"
    TMP="$(mktemp -d)"
    mkdir -p "$TMP/payload/$WIN64_DIR"
    cp "$HERE/winehq10-x86_64/d3d12core.dll" "$HERE/winehq10-x86_64/msmpeg2vdec.dll" "$HERE/winehq10-x86_64/msvcp140_codecvt_ids.dll" "$TMP/payload/$WIN64_DIR/"
    cp -R "$HERE/gstreamer64/usr" "$TMP/payload/"
    ( cd "$TMP/payload" && zip -qry9 "$TMP/payload.zip" usr )
    python3 - "$archive" "$TMP/payload.zip" <<'EOF'
import os, sys, zipfile
dst, src = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst) as zdst, zipfile.ZipFile(src) as zsrc:
    fresh = {info.filename: zsrc.read(info.filename) for info in zsrc.infolist()}
    with zipfile.ZipFile(dst + '.merged', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for info in zdst.infolist():
            if info.filename not in fresh:
                out.writestr(info.filename, zdst.read(info.filename))
        for name, data in fresh.items():
            out.writestr(name, data)
os.replace(dst + '.merged', dst)
EOF
    rm -rf "$TMP"
    unzip -Z1 "$archive" | grep -qx 'usr/lib/x86_64-linux-gnu/gstreamer-1.0/libgstlibav.so' \
        || { echo "stage-wine64-payloads: final verification failed for $archive" >&2; exit 2; }
    unzip -Z1 "$archive" | grep -qx "$WIN64_DIR/msmpeg2vdec.dll" \
        || { echo "stage-wine64-payloads: final verification failed for $archive" >&2; exit 2; }
    unzip -Z1 "$archive" | grep -qx "$WIN64_DIR/msvcp140_codecvt_ids.dll" \
        || { echo "stage-wine64-payloads: final verification failed for $archive" >&2; exit 2; }
    echo "stage-wine64-payloads: DLLs + GStreamer plugins staged in $archive (backup in $backup)"
done
