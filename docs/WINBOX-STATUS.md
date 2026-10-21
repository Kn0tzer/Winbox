# Winbox manager status (keep updated; read this first)

Main branch: `fix/winbox-chromium-20261018` in /home/ubuntu/boxedwine64 (local only, unpushed, undeployed).
Last merged: glsh, recover, gb, loopback sockets, WebGPU GL (HEAD 723b3a056 builds and maps notepad jit=0 in 43 s). Earlier final report: docs/reports/winbox-finish-20261020.md.

## Demo matrix (older runtime built 2026-10-21 03:01; seconds click -> mapped, jit0/jit1)
notepad 40/54, snake 29/fail, doom fail/45, celeste 22/53, portal 25/47, gangbeasts not mapped.
JIT=1 startup is ~1.3-2.3x slower than JIT=0; the earlier "JIT breaks Wine startup" bug is fixed (jitfix), snake and doom still have one failing mode each.

## Open problems
1. mem2 branch `lane/winbox-mem2-20261021` (a84238b6c, KMemory64 open-addressed page table, native +18% page stride): DO NOT MERGE. Bisected: main without it maps notepad; with it Wine stalls after start.exe and never maps. Lane thr_2y9kiyujhs asked to fix.
2. Q3 draws nothing (client arrays), Mindustry stuck at 82% (errno 5 loop), Gang Beasts no swapchain: Sol lanes solq3/solhang in flight.
3. jitpar lane (JIT correctness/startup, snake jit=1 root cause): round 4 pending.
4. Portal blank screenshot; CPU32 JIT slower than interpreter; 7 unit failures untriaged; CPU32 recovery test LabelAlreadyBound (test reordered last).
5. Codex plan hit 429 rate limits until about 00:50 UTC 2026-10-22 (resetsAtMs 1792630237000); all lanes paused until then.

## Workflow speedups
- New worktree: `/home/ubuntu/scripts/winbox-wt-prep.sh <worktree>` (offline assets; local manifest is assume-unchanged, never commit it).
- Build ~15 min with `BW64_BUILD_TARGET=wasm64-mt-jit JOBS=2 BINARYEN_CORES=2`; builds serialize on /tmp/bw64-build.lock.d and wait for probes to drain.
- Probe slots: web/tests/probe-slots.mjs; demo matrix `DEMO=x JIT=y OUT=... MAPPED_TIMEOUT_S=n node web/tests/demo-matrix.mjs`.
- Bisect by building a detached worktree of the suspect commit; only compare runtimes built from the same base.
- Never wait with `pgrep -f` patterns that match your own command; wait on a done-file instead.
- Agents: one narrow chunk each; bisect/build/measure by the manager directly.
