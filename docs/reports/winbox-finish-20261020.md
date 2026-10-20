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
