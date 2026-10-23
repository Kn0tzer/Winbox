# Mindustry WebGPU GL investigation, 2026-10-23

## Scope and source state

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-solhang-20261021`

Branch: `lane/winbox-solhang-20261021`

I fast-forwarded this worktree to `integ/winbox-20261022b` (`3d0ecbc24`). This brings the integration build's browser JavaScript into the worktree while leaving the integration worktree and WASM untouched. The GLSL precision/conditional and interface-location fixes from `c5bad39fe` and `1e9f3c653` are present in `web/gl-webgpu.mjs` and their Naga-backed regression fixtures are in `web/tests/suite/unit/gl-shader-compat.test.mjs`.

The supplied integration runtime exists at `/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022/web/runtime`. For the probe, `WINE_DIST_DIR` must point to its parent `/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022/web`, because the script appends `runtime/`. It is not a runtime build made from this branch, and no WASM build was run.

The probe previously omitted the `jit` launch parameter. `tools/bench/gl-game-probe.mjs` now accepts `JIT=0|1`, defaults to `0`, validates the value, records it in the summary, and forwards it to the runtime URL.

## Preserved probe evidence

The prior capture in `/tmp/winbox-finish/r3/work-luna-hang/mindustry-shader-dump/verify3` reached the first window after 402.1 seconds. Its summary reports `webgpuActive: true`, no WebGPU recovery, and no page errors. The GL bridge logged `host GL up (WebGPU)`. The filtered console contains no shader link/preparation error. It does contain Emscripten's warning that immediate-mode emulation is limited.

The capture `/tmp/winbox-finish/r3/work-luna-hang/mindustry-shader-dump/verify3/mapped-plus-60s.png` is black. Its sample has the normal guest and GL canvases, and the console shows subsequent GL activity, so successful WebGPU initialization alone does not produce the menu. The earlier shader-dump runs and their initial `UnexpectedNewLine`, duplicate-location, and `ElseOutsideOfBlock` failures are documented in `guest-hangs-luna-20261022.md`; the precision/location regression tests now pass.

Focused verification on the merged source:

```text
node --test web/tests/suite/unit/gl-shader-compat.test.mjs
11 passed, 0 failed
```

## Restored-runtime reruns

The game was restored at `/tmp/winbox-finish/r2/work-glwgpu/mindustry/Mindustry/Mindustry.exe`. I assembled a temporary probe root under `/tmp/winbox-finish/r4/dist-stage`: top-level web modules are symlinks to this worktree's `web/`; runtime binaries are symlinks to the supplied integration `web/runtime`; the required page entrypoint and support/dependency files come from the integration distribution. The staged WASM SHA-256 matches `BUILD_INFO.json`: `6c2fbf7e32eab884e4b2dea362a1c7aa98ced64f7c0cd94447e3d3b5993cf5ed`. No WASM build was run and the integration worktree was not edited.

Both requested runs completed with the same game zip and runtime build:

| JIT | First window | WebGPU GL active | Outcome |
| --- | ---: | --- | --- |
| 0 | no, 480 s timeout | no | OpenJDK exits during initialization: `Too small maximum heap` |
| 1 | no, 480 s timeout | no | OpenJDK exits during initialization: `Too small maximum heap` |

Evidence:

- JIT=0: `/tmp/winbox-finish/r4/mindustry-jit0/summary.json`, `console.log`, `timeout.png`
- JIT=1: `/tmp/winbox-finish/r4/mindustry-jit1/summary.json`, `console.log`, `timeout.png`
- Both logs show the Java VM startup error before any GL context, shader compilation, or `host GL up (WebGPU)` message. There is no shader failure or WebGL2 recovery in these runs.

The likely blocker is the x86-64 guest memory report. `source/kernel/syscall64.cpp`, `X64_SYS_sysinfo` (case 99), returns success after zero-filling all 112 bytes, so `totalram`, `freeram`, and `mem_unit` are zero. In contrast, the 32-bit `KSystem::sysinfo` reports 1 GiB total and 768 MiB free, and `/proc/meminfo` reports 1 GiB total. OpenJDK now aborts with “Too small maximum heap” on the x64 path, consistent with a zero physical-memory report. The current logs do not trace the syscall itself, so this is a strong root-cause candidate rather than direct syscall-level proof.

This C++ fix needs a WASM rebuild, which was explicitly excluded. I did not modify it. The required follow-up is to populate the x86-64 `sysinfo` fields with a nonzero, consistent memory total/free value and `mem_unit`, add focused coverage, rebuild the WASM runtime, then rerun both modes. Until that is done, the GL menu failure cannot be evaluated on the supplied runtime.

## Build 5 reruns (2026-10-23)

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-mindustry-build5-20261023`

Branch: `lane/winbox-mindustry-build5-20261023`, based on integration HEAD `336572461556d787b9ad997d36f88ccfa0430a41`.

The supplied build-5 `web/runtime` and `dist/runtime` agree on WASM SHA-256 `3a6bd71f8778c0e1e266844363d8078ce5333fc1f4d6de9a761ad1ec4bb2a5d0`, and both BUILD_INFO files identify HEAD `336572461`. No WASM build was run and the integration worktree was not modified. The earlier zero-memory sysinfo startup blocker is resolved: Mindustry starts Java 25.0.1 and reaches its OpenGL initialization logs.

### Probe staging correction

The first build-5 JIT=0 and JIT=1 runs mapped the window but initialized WebGL2. Those runs were not valid WebGPU checks: `winbox-wt-prep.sh` had left an old `web/runtime/wine64-launcher.js` in the new worktree, and the initial temporary stage preferred that copy. It does not set `BW64_GL_BACKEND`. Build 5's fresh `dist/runtime/wine64-launcher.js` does set the WebGPU default. I corrected only the temporary stage under `/tmp/winbox-finish/r5/dist-stage` to use that build artifact; the stage still serves this branch's current `web/` modules, including `gl-webgpu.mjs`.

### Corrected WebGPU results

| JIT | First window | WebGPU active | Captures | Result |
| --- | ---: | --- | --- | --- |
| 0 | 319.7 s | yes | +60 s | Black; no page errors, bridge recovery, shader link failure, or WebGPU failure |
| 1 | 371.4 s | yes | +60, +120, +240 s | All black; no page errors, bridge recovery, shader link failure, or WebGPU failure |
| 0, `GL_TRACE=400` | 252.4 s | yes | +60 s | Black; sample reports zero GL calls and zero presents |

The JIT=1 console reaches `[I] [GL] Version: OpenGL 3.2.0`, `[I] [GL] Using OpenGL 3 API`, `[I] [JAVA] Version: 25.0.1`, and `[I] [RAM] Available: 259.5 MB`. Its +120 and +240 second samples continue reporting CPU64 instructions and syscalls, but zero GL calls and zero presents. No shader is submitted to the WebGPU translator in these captures. The guest-reported GL version string includes `WebGL2`, but the host bridge independently confirms `host GL up (WebGPU)` and the probe summary remains `webgpuActive: true`; there was no fallback.

The bounded `GL_TRACE=400` run emitted no `gl64: call fnId` trace. `gl64bridge.cpp` documents that `getenv` can be unavailable on guest worker threads in this pthread build, so the silence is not evidence that no calls happened before the sampled interval. The bridge stage counters provide the interval-specific evidence: zero GL calls/presents at the JIT=0 trace capture and at JIT=1 +120/+240.

### Current blocker and evidence

The precision macro, conditional, and interface-location fixes from `c5bad39fe` and `1e9f3c653` are present in this branch. The build-5 black frame is not currently attributable to GLSL-to-WGSL translation: after Java reports OpenGL initialization, the captured later samples contain no GL traffic, and the WebGPU failure list is empty. The next blocker is a guest-side stall after Java/GL initialization and before the first rendered frame. The available probe output does not identify the guest thread or call site responsible. Resolving that will require a guest-thread/CPU diagnostic (likely C++ instrumentation and a rebuilt WASM), which was outside the no-rebuild runtime verification requested here. No C++ change or renderer workaround was made without evidence.

Evidence:

- Initial mis-staged JIT=0/JIT=1 runs: `/tmp/winbox-finish/r5/mindustry-jit0/` and `/tmp/winbox-finish/r5/mindustry-jit1/` (WebGL2; not used to assess the WebGPU path).
- Corrected JIT=0: `/tmp/winbox-finish/r5/mindustry-jit0-corrected/summary.json`, `console.log`, `mapped-plus-60s.png`.
- Corrected JIT=1: `/tmp/winbox-finish/r5/mindustry-jit1-corrected/summary.json`, `console.log`, and `mapped-plus-{60,120,240}s.png`.
- Bounded GL trace: `/tmp/winbox-finish/r5/mindustry-jit0-gltrace/summary.json`, `console.log`, `mapped-plus-60s.png`.
