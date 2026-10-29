#!/bin/bash
# Merge the 32-bit (PE32) vkd3d-proton D3D12 payload into the shipped
# wine32.zip archive WITHOUT a full rootfs rebuild — the 32-bit mirror
# of stage-vkd3d-payloads.sh (which does the same for wine64.zip).
#
# What goes in, and why (DirectX 12 campaign, x86 vkd3d lane):
#   opt/wine-stable/lib/wine/i386-windows/d3d12.dll
#   opt/wine-stable/lib/wine/i386-windows/d3d12core.dll
#       <- the vkd3d-proton 3.1.0 PE32 build (tools/vkd3d/build.w32,
#          see tools/vkd3d/BUILD.md "Build (32-bit guests)" and
#          tools/vkd3d/build-win32.sh), replacing WineHQ wine-stable
#          10.0's bundled pair at system placement: prefix32's
#          system32.link points at /opt/wine-stable/lib/wine/i386-windows,
#          so any 32-bit D3D12 program finds them by plain LoadLibrary
#          search. Wine's own d3d12core has no DXIL frontend and cannot
#          run SM 6.x titles; the pair must move together (proton's
#          d3d12.dll is a thin loader for its own d3d12core.dll).
#          Guests select them with WINEDLLOVERRIDES=d3d12=n;d3d12core=n,
#          the same selection as the 64-bit pair.
#
# What does NOT go in: there is no PE32 bw64vulkan.dll (the bw64 PE
# Vulkan shim is a 64-bit build, tools/vkd3d/probe/bw64vulkan.dll).
# The vendored loader tries bw64vulkan.dll, then winevulkan.dll, then
# vulkan-1.dll (src/libs/d3d12core/main.c), so a 32-bit guest resolves
# WineHQ 10.0's winevulkan.dll from this same tree — a newer Vulkan
# registry than the Debian 8.0 winevulkan the 64-bit shim bypasses.
# Whether that path serves device creation is guest-verification work,
# not claimed here.
#
# Idempotent: archives whose members already match the payload bytes
# are skipped. Mirrors stage-vkd3d-payloads.sh.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="$(cd "$HERE/../.." && pwd)"
W32_DIR="opt/wine-stable/lib/wine/i386-windows"
D3D12="$REPO/tools/vkd3d/build.w32/libs/d3d12/d3d12.dll"
D3D12CORE="$REPO/tools/vkd3d/build.w32/libs/d3d12core/d3d12core.dll"
for f in "$D3D12" "$D3D12CORE"; do
    [[ -f "$f" ]] || { echo "stage-vkd3d-payloads32: missing payload $f (build it first: tools/vkd3d/build-win32.sh)" >&2; exit 2; }
done
# Arch check is load-bearing: a PE32+ (64-bit) copy is silently useless
# to a 32-bit guest (LoadLibrary fails err=126); refuse anything whose
# PE machine is not IMAGE_FILE_MACHINE_I386 (0x14c).
python3 - "$D3D12" "$D3D12CORE" <<'EOF'
import sys
for path in sys.argv[1:]:
    with open(path, 'rb') as f:
        data = f.read(4096)
    assert data[:2] == b'MZ', f'{path}: not a PE file'
    peoff = int.from_bytes(data[0x3c:0x40], 'little')
    assert data[peoff:peoff+4] == b'PE\0\0', f'{path}: no PE signature'
    machine = int.from_bytes(data[peoff+4:peoff+6], 'little')
    assert machine == 0x14c, f'{path}: machine {machine:#x}, expected 0x14c (I386) — refusing to stage a non-PE32 vkd3d payload'
EOF

payload_matches() {
    python3 - "$1" "$D3D12" "$D3D12CORE" <<'EOF'
import sys, zipfile
archive, d3d12, d3d12core = sys.argv[1:4]
want = {
    "opt/wine-stable/lib/wine/i386-windows/d3d12.dll": open(d3d12, "rb").read(),
    "opt/wine-stable/lib/wine/i386-windows/d3d12core.dll": open(d3d12core, "rb").read(),
}
with zipfile.ZipFile(archive) as z:
    names = set(z.namelist())
    ok = all(n in names and z.read(n) == data for n, data in want.items())
sys.exit(0 if ok else 1)
EOF
}

stamp="$(date -u +%Y%m%d-%H%M%S)"
for runtime in "$REPO/web/runtime" "$REPO/dist/runtime"; do
    [[ -d "$runtime" ]] || continue
    [[ ! -L "$runtime" ]] || { echo "stage-vkd3d-payloads32: refuse symlinked runtime dir $runtime" >&2; exit 2; }
    archive="$runtime/wine32.zip"
    [[ -f "$archive" ]] || { echo "stage-vkd3d-payloads32: expected $archive" >&2; exit 2; }
    if payload_matches "$archive"; then
        echo "stage-vkd3d-payloads32: $archive already carries the vkd3d32 payloads — skipping"
        continue
    fi
    backup="/home/ubuntu/backups/winbox-vkd3d32-payloads-$stamp"
    mkdir -p "$backup"
    cp "$archive" "$backup/$(basename "$(dirname "$runtime")")-$(basename "$runtime")-wine32.zip"
    TMP="$(mktemp -d)"
    mkdir -p "$TMP/payload/$W32_DIR"
    cp "$D3D12" "$TMP/payload/$W32_DIR/d3d12.dll"
    cp "$D3D12CORE" "$TMP/payload/$W32_DIR/d3d12core.dll"
    ( cd "$TMP/payload" && zip -qry9 "$TMP/payload.zip" opt )
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
    payload_matches "$archive" \
        || { echo "stage-vkd3d-payloads32: final verification failed for $archive" >&2; exit 2; }
    echo "stage-vkd3d-payloads32: vkd3d32 payloads staged in $archive (backup in $backup)"
done
