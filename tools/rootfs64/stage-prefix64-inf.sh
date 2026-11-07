#!/usr/bin/env bash
# Overlay the driver .inf files into the shipped 64-bit prefix's
# C:\windows\inf\ (drive_c/windows/inf/ in the prefix zip), so the PnP
# manager can install a function driver for newly enumerated WINEBUS HID
# devices. See stage-wine-inf.sh for why the .inf files are needed at all:
# the shipped prefix has no inf/ directory because the wine zips carried
# no per-driver .inf files for wineboot to copy. The producer change is
# the .inf files in the wine zips' share/wine/ (stage-wine-inf.sh) plus a
# validation in prepare_prefixes.py; this script is the overlay path for
# the already-cut prefix64.zip, following the stage-wine64-drivers.sh
# merge pattern. Idempotent and symlink-safe (os.replace replaces the
# archive file itself, never writing through a link into the store).
# The 64-bit prefix's overlay belongs to the FD-189 lane (its prefix has
# no PlugPlay registration at all); this script covers prefix32 only.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
INF_DIR="home/username/.wine/drive_c/windows/inf"
SRCDIR="$REPO/tools/rootfs32/wine-inf"
NAMES="input.inf winebus.inf winehid.inf wineusb.inf winexinput.inf"

stamp="$(date -u +%Y%m%d-%H%M%S)"
for runtime in "$REPO/web/runtime" "$REPO/dist/runtime"; do
    [[ -d "$runtime" ]] || continue
    [[ ! -L "$runtime" ]] || { echo "stage-prefix64-inf: refuse symlinked runtime dir $runtime" >&2; exit 2; }
    archive="$runtime/prefix64.zip"
    [[ -f "$archive" ]] || { echo "stage-prefix64-inf: expected $archive" >&2; exit 2; }
    current="$(python3 - "$archive" "$SRCDIR" "$INF_DIR" <<'EOF'
import hashlib, os, sys, zipfile
archive, srcdir, infdir = sys.argv[1], sys.argv[2], sys.argv[3]
names = ["input.inf", "winebus.inf", "winehid.inf", "wineusb.inf", "winexinput.inf"]
with zipfile.ZipFile(archive) as z:
    have = set(z.namelist())
    ok = all(infdir + "/" + n in have and
             hashlib.sha256(z.read(infdir + "/" + n)).hexdigest() ==
             hashlib.sha256(open(os.path.join(srcdir, n), "rb").read()).hexdigest()
             for n in names)
    print("current" if ok else "stale")
EOF
)"
    if [[ "$current" == "current" ]]; then
        echo "stage-prefix64-inf: $archive already carries the driver .inf files -- skipping"
        continue
    fi
    backup="/home/ubuntu/winbox-project/evidence/winbox-prefix64-inf-$stamp"
    mkdir -p "$backup"
    cp "$archive" "$backup/$(basename "$(dirname "$archive")")-prefix64.zip"
    TMP="$(mktemp -d)"
    mkdir -p "$TMP/payload/$INF_DIR"
    cp "$SRCDIR"/input.inf "$SRCDIR"/winebus.inf "$SRCDIR"/winehid.inf "$SRCDIR"/wineusb.inf "$SRCDIR"/winexinput.inf "$TMP/payload/$INF_DIR/"
    ( cd "$TMP/payload" && zip -qry9 "$TMP/payload.zip" home )
    python3 - "$archive" "$TMP/payload.zip" <<'EOF'
import os, sys, zipfile
dst, src = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(dst) as zdst, zipfile.ZipFile(src) as zsrc:
    fresh = {info.filename: zsrc.read(info.filename) for info in zsrc.infolist() if not info.is_dir()}
    with zipfile.ZipFile(dst + '.merged', 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as out:
        for info in zdst.infolist():
            if info.filename not in fresh:
                out.writestr(info.filename, zdst.read(info.filename))
        for name, data in fresh.items():
            out.writestr(name, data)
os.replace(dst + '.merged', dst)
EOF
    rm -rf "$TMP"
    python3 - "$archive" "$SRCDIR" "$INF_DIR" <<'EOF'
import hashlib, os, sys, zipfile
archive, srcdir, infdir = sys.argv[1], sys.argv[2], sys.argv[3]
names = ["input.inf", "winebus.inf", "winehid.inf", "wineusb.inf", "winexinput.inf"]
with zipfile.ZipFile(archive) as z:
    have = set(z.namelist())
    bad = [n for n in names
           if infdir + "/" + n not in have
           or hashlib.sha256(z.read(infdir + "/" + n)).hexdigest() != hashlib.sha256(open(os.path.join(srcdir, n), "rb").read()).hexdigest()]
    if bad:
        raise SystemExit(f"stage-prefix64-inf: .inf verification FAILED bad={bad}")
    print(f"stage-prefix64-inf: driver .inf files verified ({len(names)} members) in {archive}")
EOF
    echo "stage-prefix64-inf: driver .inf files staged in $archive (backup in $backup)"
done
