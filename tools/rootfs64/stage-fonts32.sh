#!/usr/bin/env bash
# Add the DejaVu Bold/Mono faces to the 32-bit glibc rootfs archives.
# Mirrors stage-gstreamer.sh. Until now glibc-rootfs32.zip carried exactly
# one face (DejaVuSans.ttf, staged by tools/rootfs32/stage-fonts.py), so
# every bold/mono request on the 32-bit guest synthesized or fell back to
# the regular face. Payload lives in fonts32/ next to this script
# (fonts-dejavu-core, see fonts32/README.md for provenance + hashes).
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SOURCE="$HERE/fonts32"
[[ -d "$SOURCE" ]] || { echo "stage-fonts32: missing $SOURCE" >&2; exit 2; }
for face in DejaVuSans-Bold.ttf DejaVuSansMono.ttf DejaVuSansMono-Bold.ttf; do
    [[ -f "$SOURCE/$face" ]] || { echo "stage-fonts32: missing payload $face" >&2; exit 2; }
done

stamp="$(date -u +%Y%m%d-%H%M%S)"
for runtime in "$REPO/web/runtime" "$REPO/dist/runtime"; do
    [[ -d "$runtime" ]] || continue
    [[ ! -L "$runtime" ]] || { echo "stage-fonts32: refuse symlinked runtime dir $runtime" >&2; exit 2; }
    archive="$runtime/glibc-rootfs32.zip"
    [[ -f "$archive" ]] || { echo "stage-fonts32: expected $archive" >&2; exit 2; }
    if unzip -Z1 "$archive" | grep -qx 'usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf'; then
        echo "stage-fonts32: $archive already carries the Mono face — skipping"
        continue
    fi
    backup="/home/ubuntu/backups/winbox-fonts32-$stamp"
    mkdir -p "$backup"
    cp "$archive" "$backup/$(basename "$runtime")-glibc-rootfs32.zip"
    TMP="$(mktemp -d)"
    ( cd "$SOURCE" && zip -qry9 "$TMP/payload.zip" . -i '*.ttf' )
    # payload paths must land under usr/share/fonts/truetype/dejavu/
    python3 - "$archive" "$TMP/payload.zip" <<'EOF'
import os, sys, zipfile
dst, src = sys.argv[1], sys.argv[2]
prefix = 'usr/share/fonts/truetype/dejavu/'
with zipfile.ZipFile(dst) as zdst, zipfile.ZipFile(src) as zsrc:
    names = set(zdst.namelist())
    with zipfile.ZipFile(dst + '.merged', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for info in zdst.infolist():
            out.writestr(info.filename, zdst.read(info.filename))
        for info in zsrc.infolist():
            target = prefix + info.filename
            if target not in names:
                out.writestr(target, zsrc.read(info.filename))
os.replace(dst + '.merged', dst)
EOF
    rm -rf "$TMP"
    unzip -Z1 "$archive" | grep -qx 'usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf' \
        || { echo "stage-fonts32: final verification failed for $archive" >&2; exit 2; }
    echo "stage-fonts32: DejaVu Bold/Mono faces staged in $archive (backup in $backup)"
done
