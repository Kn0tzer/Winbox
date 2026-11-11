# FD-272 — the live browser-program check loses its test-browser tab

Filed 2026-11-10, Medium. Record written 2026-11-10.

## What was wrong, in plain words

The check that opens the program in a test browser lost the browser tab in
3 of 8 runs during the batch-15 + FD-271 deploy's live checks. The program
inside never faulted — the test browser itself ran out of memory and its tab
died. The check then reported a failure of the program when nothing about
the program had failed, so a ship decision had to be adjudicated by hand.

## Mechanism, measured

- The three crashed runs (deploy evidence
  `test-results/deploy-batch15-fd271-20261110/chromium-consumer-b1, -b6, -b8`):
  b1 died in `page.evaluate` during metrics sampling after a full 437 s run;
  b6 died in `page.screenshot` (~3 min in); b8 died in `page.evaluate`
  (~3 min in). All three are test-browser renderer deaths; every preserved
  console shows 0 guest fault lines.
- The box was under extreme memory pressure throughout the deploy (swap
  11G/11G used, 97M free; RAM 19G/23G used at the b8 crash). No OOM-killer
  event appears in dmesg for the deploy window (the retained ring covers
  Nov 9 09:20 → Nov 10 20:26, so the absence is meaningful, not a rotated
  log). The most plausible mechanism, stated as analysis: with swap full,
  the renderer's attempt to commit more memory fails inside Chromium's
  allocator and the renderer dies on its own — no kernel OOM kill needed.
- `/dev/shm` is 12G, 1% used — not the cause. `--disable-dev-shm-usage` is
  already passed to the test browser. No coredumps recorded for the deploy
  window.
- Measured browser-tree memory (baseline run): renderer RSS ~2.85 GiB
  (stable), whole browser tree ~3.65–3.74 GiB peak. The wasm guest memory
  commits lazily as the guest touches pages.

## The fix (`web/tests/demo-matrix.mjs` only — test driver, no product code)

1. **Memory gate.** Before each attempt, `waitForMemoryGate()` reads
   `/proc/meminfo` and requires MemAvailable + SwapFree ≥ 5120 MiB
   (measured peak 3.7 GiB + margin). Overridable with
   `BW_MEMORY_GATE_NEED_MB` / `BW_MEMORY_GATE_WAIT_MS` (wait up to 5 min,
   polling every 15 s). On timeout it returns false and the driver exits 3
   without launching a browser.
2. **Retry.** Up to 2 attempts (`FD272_MAX_ATTEMPTS = 2`). `main()` acquires
   the probe slot once and holds it across attempts. An error matching the
   renderer-death family (`Target crashed` | `Target page, context or
   browser has been closed` | `Browser has been closed` | `Page closed`)
   preserves the attempt's `console.txt` / `log.txt` as
   `console-attempt-N.txt` / `log-attempt-N.txt` and relaunches once on a
   fresh browser. A second renderer death sets exit code 3 (environment
   failure, never a product verdict). Any other error rethrows, unchanged
   behaviour.
3. **Crash race.** The mapped wait's `TimeoutError` path swallowed a
   renderer-only death: the wait keeps polling a destroyed execution
   context until its own 900 s timeout (proven by the fixed4 run below).
   The driver now races the mapped wait against the page `crash` event and
   fails fast with `Target crashed`; a crash flag is also checked in the
   timeout branch. The raced promises each carry a no-op catch so a late
   rejection cannot crash the driver process.

## Proof (all figures read from the preserved logs, not from summaries)

- **Baseline, unfixed driver, normal conditions**
  (`test-results/fd272-baseline/`): stage `mapped`, overlay cleared,
  5 frames, 1,273,748 lit px, pageErrors 0, DRIVER_EXIT 0.
- **Fail-before, unfixed driver, browser tree capped at 2G RAM + 0 swap**
  via a `systemd-run` scope (`test-results/fd272-constrained-unfixed/`):
  dmesg `Memory cgroup out of memory: Killed process 3584905 (chrome)`
  (anon-rss 1.8G); driver exit 1 with `page.waitForFunction: Target page,
  context or browser has been closed`. Guest console ends at 25.4 s, alive,
  0 fault lines.
- **Retry trigger, fixed driver, same scope**
  (`test-results/fd272-constrained-fixed/`): attempt 1's renderer OOM-killed
  at 21:02:03; the driver logged `BW64-ENV renderer died on attempt 1 of 2
  (page.waitForFunction: Target page, context or browser has been closed);
  relaunching once`; attempt-1 console preserved (80,035 B). The run then
  ended DRIVER_EXIT 137 (SIGKILL): the scope cap also covered the node
  driver, so the retry's renderer hit the 2G cap again and systemd killed
  the whole scope including the driver. Harness artifact, not a fix
  failure — in production the driver is not in a memory-capped cgroup.
  The scope approach was abandoned after this run.
- **Crash-race gap, fixed driver, cap applied late**
  (`test-results/fd272-constrained-fixed4/`): the renderer was OOM-killed
  in the cap cgroup (dmesg, `oom_memcg=/fd272-browser-cap`) but the driver
  burned the full 900 s mapped wait and reported `not-mapped`, exit 0, no
  retry. This proved the mapped wait swallows a renderer-only death, and
  motivated the crash race (part 3 above).
- **Pass-after, fixed driver with the crash race, cap from launch**
  (`test-results/fd272-constrained-fixed5/`): the renderer was OOM-killed
  in the cap cgroup (dmesg 21:52:38, `oom_memcg=/fd272-browser-cap`,
  chrome pid 3660736, anon-rss ~2G); the driver relaunched within ~31 s
  (attempt-1 console renamed 21:53:09); attempt 2 completed mapped,
  overlay cleared, 1,273,748 lit px, 7 frames, pageErrors 0,
  DRIVER_EXIT 0. Attempt-1 console preserved
  (`console-attempt-1.txt`, 103,261 B, last line [37.2s], guest alive
  until the kill) with **0 guest fault lines**.
- **No-regression, fixed driver, normal conditions:** `fd272-constrained-fixed2`
  and `fd272-constrained-fixed3` each completed in a single attempt,
  mapped, overlay cleared, 1,273,748 lit px, pageErrors 0, exit 0.
  (fixed3's cap was applied after the renderer had finished growing and
  did not kill it — see the notes below.)
- **Gate trip:** `BW_MEMORY_GATE_NEED_MB=999999 BW_MEMORY_GATE_WAIT_MS=0`
  → exit 3 immediately, `BW64-ENV memory gate: only 18659 MiB free after
  0s, need 999999 MiB — not starting the browser`, no browser launched.

The fix touches only the test driver, so no rebuild was needed: the lane's
`dist/` (BUILD_INFO git_sha `fd0120f4e`, wasm `05bc6962…`) is byte-identical
to the live build. `dist/demos.mjs` was patched dist-only during the runs
to point the games archive at the local mirror
(`http://127.0.0.1:8901/`, the live URL is cross-origin and the live Caddy
config sends no CORS header) and was restored from
`/tmp/fd272-demos.mjs.orig` before committing — the fix commit contains only
the driver change, this record, and the register closure.

## Notes and harness lessons

- Moving an already-grown renderer into a small cgroup does NOT kill it
  (fixed3: 9 PIDs moved at ~2.8G into a 2G cap, no OOM, run completed
  normally) — the cap only bites on new allocations. The cap must be
  applied while the renderer is still growing.
- The mover that applies the cap must be scoped to the driver's own
  browser tree (descendant check on the node driver's PID, anchored pgrep
  `^node web/tests/demo-matrix`); an unanchored pgrep matched the launcher
  bash and the first mover watched a dead PID. The mover script is
  `/tmp/fd272-mover.py`; it moves only processes descended from the driver
  PID, so other tenants' browsers are never touched.
- The mover's first version died instantly because `< /dev/null` on its
  nohup line starved `python3 -` of its script stdin; the script now lives
  in a file.
- Content written through the SSH heredoc channel mangled once (the
  crash-race edit's third anchor failed its count check); the fix was
  applied from a file copied with scp, and the count checks caught the
  failed attempt before anything was written.
- Disk during this lane: 1.5G free at the start of the proof series, dipped
  to 867M, recovered to 3.0G — flagged to the coordinator; the FD-273
  lane's build needs ~1G.
- The cap cgroup `/sys/fs/cgroup/fd272-browser-cap` (memory.max=2G,
  memory.swap.max=0) is removed at closeout.

## Not changed

No product code. The live Caddy CORS item (no `Access-Control-Allow-Origin`
on `/games/*`) is separate and untouched. FD-273 remains open.

## Light checks — pipeline #88 (2026-11-10)

Submitted as `submit-check lane-fd272` on tip `6e7b24431`. Pipeline status
"failure" is driven solely by the js-unit exit, the standing acceptance
shape. Step verdicts read from the preserved results
(`checks/evidence/pipeline-88/results/`):

- verify-source: mounted commit `6e7b24431` exactly, read-only mount
  enforced, tree pristine.
- citations: exit 0, 352 rows, 0 drifts.
- js-unit: 352 passed, 2 failed — exactly `provenance-artifacts` and
  `runtime-completeness`, the two known bare-checkout artifacts.
- js-legacy: exit 0.
- native-main: 706 tests, 0 FAILED (255 s).
- native-x64: 121 tests, 0 FAILED (25 s).
- run-native: exit 0.

## Closure

FD-272 CLOSED 2026-11-10. Fix commit `bc0ed729e`. CL number settled at composition.
