#!/usr/bin/env bash
# Merge the FD-188 drivers/ mirror into the shipped wine64.zip archives:
# copy every x86_64-windows/*.sys member into x86_64-windows/drivers/
# (byte copies of the same builtins), WITHOUT a full rootfs rebuild.
#
# Why: Wine's driver loader (ntoskrnl load_driver) opens every driver by
# the absolute path C:\windows\system32\drivers\<name>.sys. The shipped
# prefix redirects system32 at the filesystem level to the Wine lib dir
# (drive_c/windows/system32.link -> .../x86_64-windows), which carries
# the .sys files but no drivers/ subdirectory -- so the loader answered
# STATUS_DLL_NOT_FOUND (c0000135) for every driver and ZwLoadDriver failed
# with c0000142 (FD-188). Wine's own prefix creation writes drivers\*.sys
# placeholders, but this project's prefix build deliberately ships only
# the registry + the system32.link, so the placeholders never arrive.
# With the mirror in place the loader finds the file, and its
# builtin-replacement machinery loads the canonical builtin and associates
# the Unix .so by basename, so DriverEntry runs.
#
# The canonical path is build-wine64-zip.sh (container staging), which
# performs exactly these copies; this script is the merge-script path
# for use when the container build is unavailable -- on this ARM64 host
# the linux/amd64 container's binfmt/QEMU layer crashes during staging
# (x86_64-binfmt-P: QEMU internal SIGSEGV, 2026-10-25), and the docker
# image boxedwine64/wine64-debian:bookworm is absent. Idempotent: an
# archive whose drivers/ members already equal their siblings is skipped.
# The mirror is derived from the archive's own .sys members, so no other
# input is needed. See tools/rootfs64/stage-wine8-backports.sh for the
# same merge pattern.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
WIN64_DIR="usr/lib/x86_64-linux-gnu/wine/x86_64-windows"
DRIVERS_DIR="$WIN64_DIR/drivers"

stamp="$(date -u +%Y%m%d-%H%M%S)"
for runtime in "$REPO/web/runtime" "$REPO/dist/runtime"; do
    [[ -d "$runtime" ]] || continue
    [[ ! -L "$runtime" ]] || { echo "stage-wine64-drivers: refuse symlinked runtime dir $runtime" >&2; exit 2; }
    archive="$runtime/wine64.zip"
    [[ -f "$archive" ]] || { echo "stage-wine64-drivers: expected $archive" >&2; exit 2; }
    # Skip only when every drivers/ member already equals its sibling.
    current="$(python3 - "$archive" "$WIN64_DIR" "$DRIVERS_DIR" <<'EOF'
import hashlib, sys, zipfile
archive, win, drv = sys.argv[1], sys.argv[2], sys.argv[3]
with zipfile.ZipFile(archive) as z:
    names = set(z.namelist())
    sysnames = sorted(n for n in names if n.startswith(win + "/") and n.endswith(".sys") and "/" not in n[len(win) + 1:])
    if not sysnames:
        print("empty")
    elif all(drv + "/" + n.rsplit("/", 1)[-1] in names and
             hashlib.sha256(z.read(drv + "/" + n.rsplit("/", 1)[-1])).hexdigest() ==
             hashlib.sha256(z.read(n)).hexdigest() for n in sysnames):
        print("current")
    else:
        print("stale")
EOF
)"
    if [[ "$current" == "current" ]]; then
        echo "stage-wine64-drivers: $archive already carries the drivers/ mirror -- skipping"
        continue
    fi
    [[ "$current" == "stale" ]] || { echo "stage-wine64-drivers: no .sys files in $archive ($current)" >&2; exit 2; }
    backup="/home/ubuntu/winbox-project/evidence/winbox-wine64-drivers-$stamp"
    mkdir -p "$backup"
    cp "$archive" "$backup/$(basename "$runtime")-wine64.zip"
    TMP="$(mktemp -d)"
    mkdir -p "$TMP/payload/$DRIVERS_DIR"
    python3 - "$archive" "$TMP/payload/$DRIVERS_DIR" <<'EOF'
import sys, zipfile
archive, dest = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(archive) as z:
    for n in z.namelist():
        if n.startswith("usr/lib/x86_64-linux-gnu/wine/x86_64-windows/") and n.endswith(".sys") and "/" not in n[len("usr/lib/x86_64-linux-gnu/wine/x86_64-windows/"):]:
            open(dest + "/" + n.rsplit("/", 1)[-1], "wb").write(z.read(n))
print("drivers/ payload staged from the archive's own .sys members")
EOF
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
    # Final verification: the mirror is exact.
    python3 - "$archive" "$WIN64_DIR" "$DRIVERS_DIR" <<'EOF'
import hashlib, sys, zipfile
archive, win, drv = sys.argv[1], sys.argv[2], sys.argv[3]
with zipfile.ZipFile(archive) as z:
    names = set(z.namelist())
    sysnames = sorted(n for n in names if n.startswith(win + "/") and n.endswith(".sys") and "/" not in n[len(win) + 1:])
    bad = [n for n in sysnames
           if drv + "/" + n.rsplit("/", 1)[-1] not in names
           or hashlib.sha256(z.read(drv + "/" + n.rsplit("/", 1)[-1])).hexdigest() != hashlib.sha256(z.read(n)).hexdigest()]
    siblings = {s.rsplit("/", 1)[-1] for s in sysnames}
    extra = [n for n in names if n.startswith(drv + "/") and not n.endswith("/") and n.rsplit("/", 1)[-1] not in siblings]
    if bad or extra:
        raise SystemExit(f"stage-wine64-drivers: mirror verification FAILED bad={bad} extra={extra}")
    print(f"stage-wine64-drivers: drivers/ mirror verified ({len(sysnames)} members) in {archive}")
EOF
    echo "stage-wine64-drivers: drivers/ mirror staged in $archive (backup in $backup)"
done
