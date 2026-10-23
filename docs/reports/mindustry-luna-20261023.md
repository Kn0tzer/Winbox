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

## Remaining blocker

I could not run the requested fresh `jit=0` and `jit=1` probes. The probe's default Mindustry directory and the previous Mindustry zip path no longer exist. A search of `/tmp` and the usual `/home/ubuntu` locations found no `Mindustry.exe` or Mindustry-named archive. The preserved `verify3` artifact is diagnostic evidence only; it is not a substitute for the requested two runs.

Without a current game archive and a fresh shader dump from the black-screen run, the remaining rendering failure is not localized to a GLSL translation rule or a specific WebGPU GL feature. I made no speculative renderer change and added no Mindustry-specific behavior. The menu is not verified; the report therefore records an incomplete result rather than claiming success.

## Next step

Restore/provide the same Mindustry game directory or archive used for the earlier probe. Then run the existing `tools/bench/gl-game-probe.mjs` against the integration runtime with `WINE_DIST_DIR=/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022/web`, using the worktree's `web/` sources and `JIT=0` followed by `JIT=1`. Preserve the failing shader source, console, and screenshots before making the next general GL/WebGPU change.
