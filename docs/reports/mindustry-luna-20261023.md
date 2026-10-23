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
