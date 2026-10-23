# Winbox finish pass, 2026-10-20

Ten lane branches were merged (shaders, cpu32, cpu64, render, ui, jitoff, gamepad, profile, jit64speed, gangbeasts) and a merged WASM build was exercised with `web/tests/demo-matrix.mjs`. Nothing was pushed or deployed.

## Demo matrix on the merged build

| Demo | JIT | Result |
| --- | --- | --- |
| Notepad | off | mapped |
| Notepad | on | not mapped (Wine64 exits status 1 during startup; reproduced twice) |
| Explorer | on | mapped, desktop rendered |
| Snake | on | not mapped |
| Doom | off | mapped, rendering |
| Doom | on | not mapped |
| Celeste | on | mapped, title screen rendering |
| Chromium | on | mapped, typed input reaches the browser |
| Portal | on | mapped, screen stays black (no first frame) |
| Gang Beasts | on | not mapped |

## Known problems
- **JIT-on regression on the merged build.** Notepad, Doom, snake and Gang Beasts fail to map with `jit=1`; Explorer, Celeste and Chromium work. The cause is not bisected. Treat JIT as opt-in and off by default.
- Portal reaches mapped but renders no frame (conformance d3d9 13/15, d3d11 11/12).
- Gang Beasts is unverified after the epoll fix.
- Gamepad end-to-end test fails.
- JIT speedups are partial: jit32 3.3x on jitbench32 but slower on mixed x86; jit64 baseline 1.93x.
- Unit suite: 7 failures from the integration run, mostly dist-artifact checks, not triaged.
- Local builds need the main repo's runtime-assets manifest and ignored DXVK/vkd3d outputs; the pinned release download returns 404.
- Profiling covers Doom with JIT on only.

## Update 2026-10-23: integ build 5 shipped
- Matrix (click to mapped, jit0/jit1, s): notepad 121/117, snake 66/75, celeste 49/42, portal 46/27, gangbeasts 230/263, doom 75/79. All map in both modes. Unit 161/0; launcher-ui and browser.mjs pass. Times were taken under shared load (~9).
- User-facing changes: folder mode moved from the home page into Settings as a toggle with a "?" help; JIT setting help is two sentences; WebGPU/WebGL and sharpness settings removed; Performance panel shows fps, frame time, CPU time, GPU time; flowchart renamed "GL subset" to "GL bridge" and dropped the WebGL2 path.
- Fixes: x86-64 sysinfo reports memory (Mindustry JVM heap check), export now includes shader-cache.mjs, vk-page-lifetime fixture no longer sets the removed #backend setting.
- Open: Q3 (null qglGenProgramsARB, VBO path), Mindustry black screen after GL init, Gang Beasts shader fixes, startup time dominated by Wine service waits.

## Update 2026-10-23 (later): integ builds 7 and 8
- Matrix 12/12 mapped on builds 6, 7, 8 (build 8 jit0/jit1 s: notepad 61/104, snake 52/55, celeste 24/24, portal 26/28, gangbeasts 179/214, doom 51/122). Unit suite 172 on build 7, 173 + one stub fix on build 8; launcher-ui and browser.mjs pass. Not deployed (no user-visible change vs build 5).
- Mindustry (build 7, ctrace=1): no bad control transfer or hs_err; stalls silently after the JVM RAM message with zero presents. Q3: loading screen of Q3DM1. Gang Beasts: dynamic raster state, event2, clear-depth-stencil added; black render targets remain; pass-readback diagnostic in progress. Wake-latency counters added (opt-in) for the Wine startup investigation.

## Update 2026-10-23: first-frame overlay and GL readback (build 11, deployed)
- **Site showed "Waiting for the first frame" forever.** Portal commit 8b54d991f dropped the runtime-iframe canvas fallback from the launcher's `visibleFrame` check, so Wine programs that draw into the runtime `#canvas` never cleared the overlay. Restored in fa2d04c01 with a source-level unit test.
- **Celeste (and any programmable GL game) drew black.** The WebGPU GL layer only mirrored fixed-function draws on the CPU, and `glReadPixels` on the default framebuffer served that CPU shadow to the X11 present sink. Present now copies the colour target to a staging buffer and `readPixels` serves the last completed readback (about one swap of lag). Commit 7bc865fe9 plus `gl-webgpu-readback.test.mjs`. Celeste's starfield now renders; its title-logo glyphs are still glitched.
- **Why the matrix missed it.** "Mapped" only meant Wine logged a window. `demo-matrix.mjs` now also fails when `#loading`/`#launchProgress` is still visible after mapping. The `nonblankFraction` pixel read cannot see WebGPU canvases (Portal and Gang Beasts read 0 while their screenshots show real frames), so judge those by screenshot.
- Matrix i10 on the real export: 12/12 mapped, overlay cleared in all 12; unit 177/177; launcher-ui passes.
