#!/usr/bin/env bash
# Add the staged i386 GStreamer plugin set to the 32-bit runtime archive.
# Mirrors stage-alsa32.sh; payload is produced by prepare-gstreamer.py.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
RUNTIME="${1:-$REPO/web/runtime}"
[[ -d "$RUNTIME" ]] || { echo "stage-gstreamer: runtime directory missing: $RUNTIME" >&2; exit 2; }
[[ ! -L "$RUNTIME" ]] || { echo "stage-gstreamer: refuse to update a symlinked runtime directory" >&2; exit 2; }
RUNTIME="$(cd "$RUNTIME" && pwd)"
WINE32="$RUNTIME/wine32.zip"
SOURCE="$HERE/gstreamer32"
[[ -f "$WINE32" ]] || { echo "stage-gstreamer: expected wine32.zip in $RUNTIME" >&2; exit 2; }
[[ -d "$SOURCE/usr/lib/i386-linux-gnu/gstreamer-1.0" ]] || {
    echo "stage-gstreamer: missing $SOURCE payload — run prepare-gstreamer.py" >&2
    exit 2
}

# Merge the plugin set into a copy of wine32.zip (python merge keeps entry
# names flat; re-running the script never duplicates entries).
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT
cp "$WINE32" "$TMP/wine32.zip"
( cd "$SOURCE" && zip -qry9 "$TMP/payload.zip" usr/lib )
python3 - "$TMP/wine32.zip" "$TMP/payload.zip" <<'EOF'
import os, sys, zipfile
dst, src = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst) as zdst, zipfile.ZipFile(src) as zsrc:
    names = set(zdst.namelist())
    with zipfile.ZipFile(dst + '.merged', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for info in zdst.infolist():
            out.writestr(info.filename, zdst.read(info.filename))
        for info in zsrc.infolist():
            if info.filename not in names:
                out.writestr(info.filename, zsrc.read(info.filename))
os.replace(dst + '.merged', dst)
EOF
mv "$TMP/wine32.zip" "$WINE32"

unzip -Z1 "$WINE32" | grep -qx 'usr/lib/i386-linux-gnu/gstreamer-1.0/libgstlibav.so' \
    || { echo "stage-gstreamer: final verification failed" >&2; exit 2; }
echo "stage-gstreamer: i386 GStreamer plugin set staged in $WINE32"
