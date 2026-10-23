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
