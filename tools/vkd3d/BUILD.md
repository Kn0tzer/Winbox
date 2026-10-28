# vkd3d-proton build and guest staging

## Pin

**vkd3d-proton 3.1.0**, vendored at `tools/vkd3d/src`
(HansKristian-Work/vkd3d-proton @ `faf71e79b9b7e7ac6b7294a3c0b62f33d85e90ad`;
exact subproject pins in `tools/vkd3d/SOURCES.json`). Chosen over Wine's
bundled vkd3d (~1.16) because upstream vkd3d has no DXIL frontend and D3D12
apps ship SM 6.x DXIL; vkd3d-proton vendors dxil-spirv (+ dxbc-spirv for
legacy DXBC) statically into `d3d12core.dll`. See
`docs/reports/d3d12-campaign-state-20261026.md` for the route analysis.

## Build (x86_64 guests)

```sh
# from tools/vkd3d/
CFLAGS=-DPATHCCH_NONE=0x00000000 CXXFLAGS=-DPATHCCH_NONE=0x00000000 \
meson setup build.w64 src --cross-file src/build-win64.txt \
  --buildtype release -Denable_tests=false -Denable_extras=false
ninja -C build.w64
# DLLs: build.w64/libs/d3d12/d3d12.dll (loader, ~518 KB)
#       build.w64/libs/d3d12core/d3d12core.dll (implementation, ~8.8 MB)
```

The `PATHCCH_NONE` define is a box-local toolchain workaround: this box's
mingw-w64 `pathcch.h` declares `PathCchCombineEx` but not the
`PATHCCH_NONE` (0x0) flag constant that vkd3d-proton 3.1.0's
`libs/vkd3d-common/platform.c` uses. No vendored source is modified.

Verified 2026-10-26: meson setup OK (345 targets), ninja 209/209, 
`d3d12core.dll` exports `D3D12GetInterface` + `D3D12SDKVersion`.

## Build (32-bit guests)

```sh
# from tools/vkd3d/
tools/vkd3d/build-win32.sh
```

The script runs the 64-bit recipe against the vendored upstream cross
file `src/build-win32.txt` (`i686-w64-mingw32-gcc`), same
`PATHCCH_NONE` workaround, same options:

```sh
CFLAGS=-DPATHCCH_NONE=0x00000000 CXXFLAGS=-DPATHCCH_NONE=0x00000000 \
meson setup build.w32 src --cross-file src/build-win32.txt \
  --buildtype release -Denable_tests=false -Denable_extras=false
ninja -C build.w32
# DLLs: build.w32/libs/d3d12/d3d12.dll (loader, PE32, 517,273 B)
#       build.w32/libs/d3d12core/d3d12core.dll (implementation, PE32, 9,022,393 B)
```

Verified 2026-10-28: meson setup OK, ninja 209/209; both outputs are
PE32 (machine 0x14c); `d3d12core.dll` exports `D3D12GetInterface` +
`D3D12SDKVersion` and imports only the same system DLLs as the 64-bit
build (ADVAPI32, dxgi, GDI32, KERNEL32, msvcrt,
api-ms-win-core-path-l1-1-0, VERSION — no mingw runtime DLLs). The
script re-checks architecture and exports on every run. As with the
64-bit pair, the PE build is not byte-reproducible (linker timestamp;
the generated version header embeds the enclosing repo's git
describe), so the canonical bytes are the staged ones, pinned below.

Staging: `tools/rootfs64/stage-vkd3d-payloads32.sh` merges the pair
into `web/runtime/wine32.zip` at
`opt/wine-stable/lib/wine/i386-windows/` (prefix32's `system32.link`
target), replacing WineHQ wine-stable 10.0's bundled pair; the step is
wired into `tools/rootfs64/apply-runtime-poststeps.sh`, and the bytes
are pinned in `tools/rootfs64/check_runtime_completeness.py`
(`VKD3D32_PINS`). Guests select the pair with
`WINEDLLOVERRIDES=d3d12=n;d3d12core=n`, as on the 64-bit side.

There is no PE32 `bw64vulkan.dll`: the bw64 PE Vulkan shim is a
64-bit build. The vendored loader tries `bw64vulkan.dll`, then
`winevulkan.dll`, then `vulkan-1.dll` (`src/libs/d3d12core/main.c`),
so a 32-bit guest resolves WineHQ 10.0's `winevulkan.dll` from the
same tree. Guest-side device creation on this pair is NOT yet
verified — no 32-bit D3D12 probe run exists;
`tools/vkd3d/probe/tri12-32.exe` (PE32 build of tri12.c,
`i686-w64-mingw32-gcc -O2 -o tri12-32.exe tri12.c -lole32`) is in-tree
for that follow-up.

## What the guest needs (see stage-vkd3d.sh)

- `d3d12.dll` + `d3d12core.dll` next to the app, with
  `WINEDLLOVERRIDES=d3d12=n;d3d12core=n` so wine loads the vkd3d-proton
  build instead of Wine 10.0's bundled d3d12 (which has no vkd3d beneath
  it in this tree).
- The Vulkan chain beneath it: guest `vulkan-1.dll`/`winevulkan.dll` =
  `tools/vkd3d/probe/bw64vulkan.dll` (PE shim) + `libvulkan.so.1`
  (`tools/rootfs64/libvk64/`) on `LD_LIBRARY_PATH`, reaching the
  vk64bridge traps. `web/tests/d3d12-pixels.mjs` stages exactly this set.

## Verify the guest path

```sh
TAG=d3d12-check WINE_DIST_DIR=dist \
  DXGI_PATH=tools/dxvk/build.w64/src/dxgi/dxgi.dll \
  node web/tests/d3d12-pixels.mjs        # clear-only probe (tri12.exe)
TRI12DRAW_PATH=tools/vkd3d/probe/tri12draw.exe \
  node web/tests/d3d12-pixels.mjs        # triangle variant
```

`stage-vkd3d.sh` puts the same payload into `dist/runtime/prefix64.zip`
so a guest spawned from the prefix finds it without test-harness uploads.
