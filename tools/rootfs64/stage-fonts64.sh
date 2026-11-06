#!/usr/bin/env bash
# Add the DejaVu faces to the 64-bit glibc rootfs archives. Mirrors
# stage-fonts32.sh. Until now glibc-rootfs64.zip carried no font faces
# at all, so 64-bit font enumeration found no usable text font and
# Wine Gecko aborted at startup ("unable to find a usable font
# (serif)", FD-65). Payload lives in fonts64/ next to this script
# (fonts-dejavu-core, see fonts64/README.md for provenance + hashes);
# the Serif pair is the face class whose absence Gecko names.
set -euo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
SOURCE="$HERE/fonts64"
[[ -d "$SOURCE" ]] || { echo "stage-fonts64: missing $SOURCE" >&2; exit 2; }
for face in DejaVuSans.ttf DejaVuSans-Bold.ttf DejaVuSansMono.ttf DejaVuSansMono-Bold.ttf DejaVuSerif.ttf DejaVuSerif-Bold.ttf copyright; do
    [[ -f "$SOURCE/$face" ]] || { echo "stage-fonts64: missing payload $face" >&2; exit 2; }
done

stamp="$(date -u +%Y%m%d-%H%M%S)"
for runtime in "$REPO/web/runtime" "$REPO/dist/runtime"; do
    [[ -d "$runtime" ]] || continue
    [[ ! -L "$runtime" ]] || { echo "stage-fonts64: refuse symlinked runtime dir $runtime" >&2; exit 2; }
    archive="$runtime/glibc-rootfs64.zip"
    [[ -f "$archive" ]] || { echo "stage-fonts64: expected $archive" >&2; exit 2; }
    if unzip -Z1 "$archive" | grep -qx 'usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf'; then
        echo "stage-fonts64: $archive already carries the Serif face — skipping"
        continue
    fi
    backup="/home/ubuntu/winbox-project/evidence/winbox-fonts64-$stamp"
    mkdir -p "$backup"
    cp "$archive" "$backup/$(basename "$runtime")-glibc-rootfs64.zip"
    python3 - "$archive" "$SOURCE" <<'EOF'
import os, sys, zipfile
dst, source = sys.argv[1], sys.argv[2]
faces = ('DejaVuSans.ttf', 'DejaVuSans-Bold.ttf', 'DejaVuSansMono.ttf',
         'DejaVuSansMono-Bold.ttf', 'DejaVuSerif.ttf', 'DejaVuSerif-Bold.ttf')
payload = {'usr/share/fonts/truetype/dejavu/' + name:
           open(os.path.join(source, name), 'rb').read() for name in faces}
payload['usr/share/doc/fonts-dejavu-core/copyright'] = \
    open(os.path.join(source, 'copyright'), 'rb').read()
with zipfile.ZipFile(dst) as zdst:
    names = set(zdst.namelist())
    with zipfile.ZipFile(dst + '.merged', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for info in zdst.infolist():
            out.writestr(info.filename, zdst.read(info.filename))
        for target, data in payload.items():
            if target not in names:
                out.writestr(target, data)
os.replace(dst + '.merged', dst)
EOF
    unzip -Z1 "$archive" | grep -qx 'usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf' \
        || { echo "stage-fonts64: final verification failed for $archive" >&2; exit 2; }
    echo "stage-fonts64: DejaVu faces staged in $archive (backup in $backup)"
done
