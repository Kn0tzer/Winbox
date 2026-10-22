# Winbox manager status (keep updated; read this first)

Main branch: `fix/winbox-chromium-20261018` in /home/ubuntu/boxedwine64 (local only, unpushed, undeployed).
Last merged: glsh, recover, gb, loopback sockets, WebGPU GL (HEAD 723b3a056 builds and maps notepad jit=0 in 43 s). Earlier final report: docs/reports/winbox-finish-20261020.md.

## Demo matrix on integ build 2026-10-22 (main + solhang + solq3 commits; seconds click -> mapped, jit0/jit1)
notepad 56/61, snake 47/54, doom 47/54, celeste 22/52, portal 25/47 - all mapped. gangbeasts not mapped (either setting).
Merged: solhang socket/loopback fixes, solq3 WebGPU fixed-function menus and framebuffer mip fix. Q3 and Mindustry not re-verified end to end.

## Open problems
1. mem2 branch `lane/winbox-mem2-20261021` (KMemory64 open-addressed page table): ABANDONED. After a hash-mixer fix (b74b64436) native random reads are -13.6% and 8 MiB stride -2.7% vs main, notepad unverified (build-lock timeouts), snake took 225 s. Not worth more effort; main stays without it.
2. Q3 draws nothing (client arrays), Mindustry stuck at 82% (errno 5 loop), Gang Beasts no swapchain: Luna takeover lanes in flight.
3. jitpar lane (JIT correctness/startup, snake jit=1 root cause): round 4 pending.
4. Portal blank screenshot; CPU32 JIT slower than interpreter; CPU32 recovery test LabelAlreadyBound (test reordered last).
5. Unit suite: 150/151 pass. Only `native-socket-safety` fails (3 subtests): its extracted-source stubs lack the new loopback members (loopbackStream, loopbackDatagram, ensureLoopbackBound, ...). Hand to solhang after its socket work lands, since it is editing the same functions. Fixed here: build-lock, browser-network, cpu64-opcode-coverage, xwire-focus (stale tests, not product bugs).
6. Codex is back (Luna only; Sol access gone). Sol lanes stopped; Luna lanes thr_km7i552v9i (Q3) and thr_aeh57tei89 (Mindustry/Gang Beasts hangs + native-socket-safety stubs) took over their worktrees. jitpar (thr_5annabscvc) and mem2 (thr_2y9kiyujhs) resumed.

## Workflow speedups
- New worktree: `/home/ubuntu/scripts/winbox-wt-prep.sh <worktree>` (offline assets; local manifest is assume-unchanged, never commit it).
- Build ~15 min with `BW64_BUILD_TARGET=wasm64-mt-jit JOBS=2 BINARYEN_CORES=2`; builds serialize on /tmp/bw64-build.lock.d and wait for probes to drain.
- Wait for load with ONE call, no polling: `/home/ubuntu/scripts/winbox-wait-load.sh [max_load=5] [max_min=9]` (exits when load is low or after the timeout; then proceed and note the load). Builds and probes already queue by themselves (build lock, probe slots).
- Probe slots: web/tests/probe-slots.mjs; demo matrix `DEMO=x JIT=y OUT=... MAPPED_TIMEOUT_S=n node web/tests/demo-matrix.mjs`.
- Bisect by building a detached worktree of the suspect commit; only compare runtimes built from the same base.
- Never wait with `pgrep -f` patterns that match your own command; wait on a done-file instead.
- Agents: one narrow chunk each; bisect/build/measure by the manager directly.
