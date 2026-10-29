#!/bin/bash
# build-win32.sh — reproducible 32-bit (PE32) build of the vendored
# vkd3d-proton 3.1.0 (tools/vkd3d/src, pin in tools/vkd3d/SOURCES.json),
# the i686 counterpart of the x86_64 build documented in BUILD.md.
#
# Output: build.w32/libs/d3d12/d3d12.dll       (loader, PE32)
#         build.w32/libs/d3d12core/d3d12core.dll (implementation, PE32)
# staged into wine32.zip by tools/rootfs64/stage-vkd3d-payloads32.sh.
#
# Uses the vendored upstream cross file src/build-win32.txt unchanged
# (i686-w64-mingw32-gcc). The PATHCCH_NONE define is the same box-local
# toolchain workaround as the 64-bit build (this box's mingw-w64
# pathcch.h declares PathCchCombineEx but not the PATHCCH_NONE flag
# constant vkd3d-proton 3.1.0 uses). No vendored source is modified.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

command -v meson >/dev/null || { echo "build-win32: meson not found" >&2; exit 2; }
command -v ninja >/dev/null || { echo "build-win32: ninja not found" >&2; exit 2; }
command -v i686-w64-mingw32-gcc >/dev/null || { echo "build-win32: i686-w64-mingw32-gcc not found (install gcc-mingw-w64-i686)" >&2; exit 2; }

export CFLAGS=-DPATHCCH_NONE=0x00000000
export CXXFLAGS=-DPATHCCH_NONE=0x00000000
if [ -f build.w32/build.ninja ]; then
    meson setup --reconfigure build.w32 src --cross-file src/build-win32.txt \
        --buildtype release -Denable_tests=false -Denable_extras=false
else
    meson setup build.w32 src --cross-file src/build-win32.txt \
        --buildtype release -Denable_tests=false -Denable_extras=false
fi
ninja -C build.w32

# Fail loudly if the outputs are not genuine PE32 vkd3d-proton DLLs:
# machine type must be IMAGE_FILE_MACHINE_I386 (0x14c) and d3d12core
# must export the D3D12 entry points (the check BUILD.md records for
# the 64-bit pair).
python3 - <<'PYEOF'
def pe_machine(path):
    with open(path, 'rb') as f:
        data = f.read(4096)
    assert data[:2] == b'MZ', f'{path}: not a PE file'
    peoff = int.from_bytes(data[0x3c:0x40], 'little')
    assert data[peoff:peoff+4] == b'PE\0\0', f'{path}: no PE signature'
    return int.from_bytes(data[peoff+4:peoff+6], 'little')
for path in ('build.w32/libs/d3d12/d3d12.dll',
             'build.w32/libs/d3d12core/d3d12core.dll'):
    machine = pe_machine(path)
    assert machine == 0x14c, f'{path}: machine {machine:#x}, expected 0x14c (I386)'
    print(f'{path}: PE32 (machine 0x14c)')
PYEOF
i686-w64-mingw32-objdump -p build.w32/libs/d3d12core/d3d12core.dll \
    | grep -E "D3D12GetInterface|D3D12SDKVersion" >/dev/null \
    || { echo "build-win32: d3d12core.dll lacks D3D12GetInterface/D3D12SDKVersion exports" >&2; exit 1; }
echo "--- built. sha256: ---"
sha256sum build.w32/libs/d3d12/d3d12.dll build.w32/libs/d3d12core/d3d12core.dll
