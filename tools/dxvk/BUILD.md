# DXVK build and guest staging

## Pin

**DXVK v2.4.1** (`doitsujin/dxvk` tag `v2.4.1`, commit `0cf0578`).
The source and header snapshots are vendored ordinary files; exact base pins
are in `SOURCES.json`. No newer-meson problem: `meson.build` wants `>= 0.58`, this box
has meson 1.12.1 (pip) + ninja 1.13.2 + glslangValidator 15.1.0 (apt
`glslang-tools`), so **no version pin-down was needed** — 2.4.1 builds as-is.

## Why the stock cross file does not work here

Upstream `src/build-win64.txt` names the bare `x86_64-w64-mingw32-*` tools.
On this box those resolve to the **win32-threads** flavor (`Thread model:
win32`), and DXVK does not build with win32 threads (upstream README "Build
troubleshooting": `std::cv_status has not been declared`). The installed
`-posix` suffixed tools are the same GCC 13 otherwise, so
`cross-win64-posix.txt` (this dir) pins those explicitly.

## Vendored headers (DXVK's own mechanism)

`src/meson.build` checks `./include/vulkan/include` and `./include/spirv/include`
first and only errors when they are absent. The source snapshots include them there:

- `src/include/vulkan` — KhronosGroup/Vulkan-Headers `v1.3.290`
- `src/include/spirv` — KhronosGroup/SPIRV-Headers `vulkan-sdk-1.3.290.0`

## Build

```sh
# from tools/dxvk/
meson setup --cross-file cross-win64-posix.txt --buildtype release \
  -Denable_dxgi=true -Denable_d3d8=false -Denable_d3d9=true \
  -Denable_d3d10=true -Denable_d3d11=true \
  --prefix / src build.w64
nice -n 19 ninja -C build.w64
# DLLs: build.w64/src/{d3d9,d3d10,d3d11,dxgi}/
```

Rebuilds are incremental (`build.w64/` is kept in-tree, gitignored like the
generated files).

## What the guest needs (see stage-dxvk.sh)

- `d3d9.dll` next to the app (`C:\tri9.exe` dir + `Z:\home\username\`), with
  `WINEDLLOVERRIDES=d3d9=n` so wine takes the native one over its builtin
  wined3d-based d3d9.
- `tri9/` — minimal D3D9 app (Clear + DrawIndexedPrimitive, exits 0 after
  70 frames, `tri9: RESULT` on stderr).

## Verify the guest path

Use `stage-dxvk.sh` or `stage-dxvk32.sh` for DLL placement and the guest
conformance programs in `probe/`, `tri9/` and `tools/d3dtests/`.
`web/tests/d3d-conformance.mjs` records per-operation browser evidence;
`docs/reports/d3d-conformance-MATRIX.md` is the historical result, including
presentation failures. Repeat the build with `cross-win32-posix.txt` and
`build.w32` for 32-bit DLLs; D3D9 and D3D11 are enabled in the saved 32-bit
configuration, while its D3D10 DLL build remains unverified.
