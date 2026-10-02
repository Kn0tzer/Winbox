# Browser Wine experiment

## Architecture and trust boundaries

The upstream revision is dee2ff3. The local fork is outside the web root. Only a build export is served at /wine. No Windows binary executes on the host: all guest code runs in Boxedwine's WASM interpreter. Folder imports and archives stay in browser storage. Imported paths are validated before writes; archive symlinks, encrypted entries, traversal, case collisions, excessive counts and expansion are rejected. The runtime is an iframe reset between launches, which owns its own Wine prefix, workers and canvas.

## Ordered vertical slices

1. Build upstream cleanly (M): `source /opt/emsdk/emsdk_env.sh; make -C project/emscripten wasm64-mt`. Acceptance: linked JS/WASM, CRC-tested rootfs. Fix emcc/em++ link regression. No upstream pushes.
2. Launcher/storage (L): folder/drop/ZIP → persistent package → EXE picker → iframe boot. Separate browser-only IndexedDB namespaces for each installation. Saves include registry and app profile, export/restore; patch ZIP is a checked overlay, not an invented universal binary patcher. Acceptance: hostile-path tests; import actual PE + sibling assets; guest accesses assets and writes save; reload preserves save.
3. Graphics foundation (L): collect supported real guest fixed-function GL commands at swap; render them with a WebGPU/WGSL pipeline, with explicitly bounded API support and WebGL fallback. Acceptance: animated Windows glcube frames from guest, not an unrelated JavaScript cube. Direct3D continues through upstream wined3d/WebGL until a complete state/shader translator exists; Vulkan is not falsely advertised.
4. Shader foundation (M): Naga WASM GLSL/SPIR-V → validated WGSL. Investigate real HLSL compiler rather than regex transpilation. Acceptance: translated vertex/fragment shaders compile with browser GPU validation; rejected syntax returns diagnostics. This is shader tooling, not full DirectX/Vulkan compatibility.
5. E2E/deploy (M): Playwright headless Chromium screenshots, console/network evidence, GUI input/save/upload/patch/export/restore, GL animation, exact live URL test. Add only /wine route and headers, validate Caddy before reload. Preserve unrelated services.

## Boundaries

Do not claim complete until the user's full modern-game goal is actually verified. A working GUI/import/3D foundation is a useful checkpoint, not universal compatibility. No GitHub fork/commit/push without unambiguous authorization. Independent reviewer unavailable; record self-review and executable adversarial tests instead.

## Verification status (2026-10-02 continuation)

- Deployed at kn0tzer.work.gd/wine (dist export + COOP/COEP headers, Caddy validated + reloaded). Live smoke: crossOriginIsolated, GLSL→WGSL, WASM threads + WebGPU detected.
- `node --test web/tests/files.test.mjs web/tests/graphics.test.mjs`: 10/10 pass.
- `node web/tests/browser.mjs`: GLSL+HLSL→WGSL, real Notepad window mapped, real guest OpenGL→WebGPU frames, zero page errors.
- `node web/tests/upload.mjs`: folder import → save count 1 → reload persistence (count 2) → export → patch overlay (count 3, patched asset) → browser restart with same profile → restore backup → run (count 3, original asset), zero page errors.
- Known limitation: 4+ consecutive Wine boots in a single browser process can hang (worker/renderer accumulation); the upload test restarts the browser before restore, matching a returning-user session. Fresh boots are unaffected.
