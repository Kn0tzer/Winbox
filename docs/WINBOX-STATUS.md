# Winbox manager status (keep updated; read this first)

Main branch: `fix/winbox-chromium-20261018` in /home/ubuntu/boxedwine64. Integ build 5 (integ/winbox-20261022b HEAD 76f517d38) is merged here, deployed to /home/ubuntu/kn0tzer/winbox and pushed (see deploy notes in docs/reports/winbox-finish-20261020.md). Earlier final report: docs/reports/winbox-finish-20261020.md.

## Demo matrix on integ build 5 (2026-10-23; seconds click -> mapped, jit0/jit1, probes ran under load ~9 so absolute times are inflated)
notepad 121/117, snake 66/75, celeste 49/42, portal 46/27, gangbeasts 230/263, doom 75/79 - ALL MAP in both JIT modes. Unit suite 161/0, launcher-ui and browser.mjs pass.
Build 5 contents: Portal BC textures + menu + 2 MiB fszipurl blocks/read-ahead, Q3 INSD recover, optional shader cache (default off), x86-64 sysinfo fix (Mindustry JVM now starts), new Settings UI (folder toggle + "?" help, brief JIT note, shader cache, no WebGL/sharpness settings), simple Performance panel (fps, frame time, CPU time, GPU time), flowchart with "GL bridge" straight to WebGPU. Export fix: shader-cache.mjs was not copied to dist (now in tools/browser/build.mjs, guarded by unit test export-module-closure).

## Lane state after build 5
- Q3: reaches Q3DM1 scene via WebGPU (2 FPS first present, guest then dies). Root cause of the crash chain: Quake3e VBO path calls null qglGenProgramsARB (no ARB vertex/fragment program advertised). Pending: `r_vbo 0` launch arg + real glMultiTexCoord2fARB/glClientActiveTextureARB; CPU64 instruction-fetch fault commit 5fb4f1ad1 (lane solq3) not yet built.
- Mindustry: JVM starts, GL init reached, then black screen; main thread runs inside jvm.dll (slow vs stuck being measured).
- Gang Beasts: reaches window; lane sonnet is fixing shader/format issues for the next blocker (commits 6bdb50261, a40814158 + depth/stencil format fix).
- Boot time: no kernel wake latency found in source; opt-in wakeprofile counters 12ecdbe6e (needs a build) and fontconfig-cache ab9edc591 (no measurable win, unmerged).
- CPU32 JIT lane blocked until 27 Oct (free-tier limit).

## Open problems
1. mem2 (KMemory64 page table) ABANDONED.
2. Notepad/Snake startup 65-120 s under load: dominated by Wine service/socket startup, not kernel wake latency.
3. CPU32 JIT slower than interpreter; notepad JIT startup ~1.26-1.29x interpreter (accepted).

## Workflow speedups
- New worktree: `/home/ubuntu/scripts/winbox-wt-prep.sh <worktree>` (offline assets; local manifest is assume-unchanged, never commit it).
- Build ~15 min with `BW64_BUILD_TARGET=wasm64-mt-jit JOBS=2 BINARYEN_CORES=2`; builds serialize on /tmp/bw64-build.lock.d and wait for probes to drain.
- Wait for load with ONE call, no polling: `/home/ubuntu/scripts/winbox-wait-load.sh [max_load=5] [max_min=9]` (exits when load is low or after the timeout; then proceed and note the load). Builds and probes already queue by themselves (build lock, probe slots).
- Probe slots: web/tests/probe-slots.mjs; demo matrix `DEMO=x JIT=y OUT=... MAPPED_TIMEOUT_S=n node web/tests/demo-matrix.mjs`.
- Bisect by building a detached worktree of the suspect commit; only compare runtimes built from the same base.
- Never wait with `pgrep -f` patterns that match your own command; wait on a done-file instead.
- Agents: one narrow chunk each; bisect/build/measure by the manager directly.
