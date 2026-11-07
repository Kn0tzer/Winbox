#!/usr/bin/env bash
# Merge the FD-188 driver .inf files into the shipped wine zips' data
# directories (share/wine/), so a real wineboot --init installs them into
# a prefix's C:\windows\inf\ via wine.inf's [InfFiles] section.
#
# Why: the PnP manager installs a function driver for a newly enumerated
# device by searching C:\windows\inf\*.inf for a compatible driver
# (SetupDiBuildDriverInfoList, SPDIT_COMPATDRIVER). The shipped wine zips
# carry only wine.inf in share/wine/ -- no per-driver .inf files -- so a
# real wineboot had nothing to copy, the shipped prefixes have no inf/
# directory at all, and the install fails with "Unable to install a
# function driver" for every WINEBUS HID device even though the bus driver
# itself now loads (FD-188 drivers/ mirror). Without the function driver
# (winehid.sys) no HID device appears and the FD-24 witness sees no
# controller. The .inf files are the upstream Wine sources, version-matched
# per width (Wine 8.0 for wine32, Wine 10.0 for wine64; the two vintages'
# files are byte-identical except input.inf, which differs by 6 bytes).
#
# The canonical path is the wine-zip build scripts, which must copy these
# files into share/wine/ at build time (build-wine32-base.sh,
# build-wine64-zip.sh); this script is the merge-script path for the
# already-cut zips, following the stage-wine64-drivers.sh pattern.
# Idempotent: an archive whose share/wine/*.inf members already equal the
# tree sources is skipped. Symlink-safe: the merge replaces the archive
# file itself (os.replace), never writing through a link into the store.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"

stamp="$(date -u +%Y%m%d-%H%M%S)"
stage_one() {
    local archive="$1" srcdir="$2"; shift 2
    local placement
    for placement in "$@"; do
        :
    done
    # Skip only when every .inf member already equals its tree source.
    current="$(python3 - "$archive" "$srcdir" "$@" <<'EOF'
import hashlib, os, sys, zipfile
archive, srcdir = sys.argv[1], sys.argv[2]
placements = sys.argv[3:]
names = ["input.inf", "winebus.inf", "winehid.inf", "wineusb.inf", "winexinput.inf"]
with zipfile.ZipFile(archive) as z:
    have = set(z.namelist())
    ok = True
    for pl in placements:
        for n in names:
            member = pl + "/" + n
            src = os.path.join(srcdir, n)
            if member not in have or hashlib.sha256(z.read(member)).hexdigest() != hashlib.sha256(open(src, "rb").read()).hexdigest():
                ok = False
    print("current" if ok else "stale")
EOF
)"
    if [[ "$current" == "current" ]]; then
        echo "stage-wine-inf: $archive already carries the driver .inf files -- skipping"
        return 0
    fi
    backup="/home/ubuntu/winbox-project/evidence/winbox-wine-inf-$stamp"
    mkdir -p "$backup"
    cp "$archive" "$backup/$(basename "$(dirname "$archive")")-$(basename "$archive")"
    TMP="$(mktemp -d)"
    mkdir -p "$TMP/payload"
    python3 - "$archive" "$srcdir" "$TMP/payload" "$@" <<'EOF'
import os, shutil, sys, zipfile
archive, srcdir, dest = sys.argv[1], sys.argv[2], sys.argv[3]
placements = sys.argv[4:]
names = ["input.inf", "winebus.inf", "winehid.inf", "wineusb.inf", "winexinput.inf"]
for pl in placements:
    d = os.path.join(dest, pl)
    os.makedirs(d, exist_ok=True)
    for n in names:
        shutil.copyfile(os.path.join(srcdir, n), os.path.join(d, n))
print("driver .inf payload staged from the tree sources")
EOF
    ( cd "$TMP/payload" && zip -qry9 "$TMP/payload.zip" $(ls) )
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
    python3 - "$archive" "$srcdir" "$@" <<'EOF'
import hashlib, os, sys, zipfile
archive, srcdir = sys.argv[1], sys.argv[2]
placements = sys.argv[3:]
names = ["input.inf", "winebus.inf", "winehid.inf", "wineusb.inf", "winexinput.inf"]
with zipfile.ZipFile(archive) as z:
    have = set(z.namelist())
    bad = []
    for pl in placements:
        for n in names:
            member = pl + "/" + n
            src = os.path.join(srcdir, n)
            if member not in have or hashlib.sha256(z.read(member)).hexdigest() != hashlib.sha256(open(src, "rb").read()).hexdigest():
                bad.append(member)
    if bad:
        raise SystemExit(f"stage-wine-inf: .inf verification FAILED bad={bad}")
    print(f"stage-wine-inf: driver .inf files verified ({len(placements) * len(names)} members) in {archive}")
EOF
    echo "stage-wine-inf: driver .inf files staged in $archive (backup in $backup)"
}

for runtime in "$REPO/web/runtime" "$REPO/dist/runtime"; do
    [[ -d "$runtime" ]] || continue
    [[ ! -L "$runtime" ]] || { echo "stage-wine-inf: refuse symlinked runtime dir $runtime" >&2; exit 2; }
    if [[ -f "$runtime/wine32.zip" ]]; then
        stage_one "$runtime/wine32.zip" "$REPO/tools/rootfs32/wine-inf" "opt/wine-stable/share/wine" "usr/share/wine"
    fi
    if [[ -f "$runtime/wine64.zip" ]]; then
        stage_one "$runtime/wine64.zip" "$REPO/tools/rootfs64/wine-inf" "usr/share/wine"
    fi
done
