# FD-115 + FD-165 — pre-handshake silent starts now surface as reported failures

Lane `lane-fd115`, 2026-11-05, based on the integration tip
`4006ddb58` (batch 7). Record of the lane that set out to close
FD-115 and, in doing so, captured and closed a second silent
pre-handshake shape, filed as FD-165.

## The problem, in plain terms

A first-time start of a Windows program in the browser could sit
forever showing a loading page: the session never finished
starting, and nothing ever told the user it had failed. FD-115
filed the one captured instance (2026-11-02, shipped build
`8ac504abf`): before the desktop handshake completed, one
winedevice-host guest thread looped on delivered memory faults
(at least 1,024 of them) and the start waited silently for the
whole 900 s observation window. The launcher's stall watchdog
(FD-77) reports stalls only *after* the handshake, so this shape
had no surfacing. FD-115's closing terms allowed either fixing
the fault's mechanism plus a clean first-visit series, or
surfacing a pre-handshake stall of this shape as a reported
failure.

## What this lane did

### Re-read of the filed instance

The preserved console (684 lines, in the integration tree at
`test-results/deploy-fd66-65-85-fd77surface-20261102/live-x86/boot-1/console.log`)
shows the filed signature exactly: guest thread pid=71 tid=76,
first fault a read at addr=eip=`0x10584384`, then write faults at
addr=`0xfffffd08`, counts 1-64 printed consecutively plus the
1024th line; closing dumps show the thread still `running` at
rip=`0x10584384` with `noprogress_ms` at 780018; the handshake
never completed. The fault counter in `BW64-FAIL
guest-memfault32` lines is session-global and rate-limited
(first 64 lines always printed, then every 1024th), and every
healthy boot console on record carries zero BW64-FAIL lines, so
a per-thread count of printed delivered-fault lines separates
this shape from any healthy one by a wide margin.

**The fault's mechanism was not identified.** The console
carries no mechanism, the filing claimed none, and the shape did
not recur in any of this lane's 89 measured starts. The entry
therefore closes on its surfacing term, and this record does not
claim a fix to the fault itself.

### The FD-115 fix

Commit `de0144c72` (`project/emscripten/wine64-launcher.js`):
before the desktop handshake completes, the stall watchdog now
counts printed delivered-fault lines
(`BW64-FAIL guest-memfault32 ... delivered=1`) per guest thread;
at 32 lines from one thread the start is reported as failed —
`BW64-FAIL component=launcher.session
reason=prehandshake-fault-loop` on the session log, the held
dump evidence flushed ahead of it, and the user-facing failure
("Windows did not finish starting: a background service thread
faulted over and over before the desktop was ready, so this
start is reported as failed instead of waiting forever."). The
slate clears at the handshake: post-handshake faults belong to
the running session (a single delivered fault there was observed
in a run that continued and finished), and faults spread across
threads never convict. Probe scenario commit `4ce0dc424`.

### A second silent shape, captured live: FD-165

The lane's reproduction series against the live shipped batch-7
build (public route, the filing's identical protocol; the site
was serving build `4006ddb58` / wasm `c0af957a` per the same
day's deploy verification) produced, at boot 10 of 20, a start
that consumed its whole 900 s window without reaching ready and
without any failure being surfaced — but with **no fault lines
at all**. Its preserved console (2,373 lines,
`test-results/fd115/repro-live-batch7/boot-10/console.log`):
0 BW64-FAIL lines, 0 guest-memfault32 lines; explorer.exe was
opened (twice) but no deskpin READY line ever printed; across
the console's 14 thread dumps, the session's three earliest processes
(pids 10, 12, 14) each show one running thread whose
`min_noprogress_ms` grows from 0 to 780078 — no guest progress
for the final 780 s — their step-rings cycling the same emulator
steps, while all 25 other dumped threads sat blocked in socket
reads (`wait=KUnixSocketObject::lockCond`), no fetch in flight,
no memory-mutex owner. Neither FD-77's post-handshake rule nor
the FD-115 fault rule can see this shape. It was filed as
FD-165. (The driver's result line for this boot reports
`memfaults:4`; a census of the preserved console finds no line
matching the driver's pattern — the counter runs over a wider
stream than the preserved console. The console census, 0, is the
figure used here.) **No mechanism is claimed for the underlying
guest stall**; none is visible in the console.

### The FD-165 fix

Commit `3a4d59793` (same file): the stall watchdog gains a
pre-handshake dump rule. Before the handshake, when *every*
process in a complete thread dump (footer seen, no lazy fetch
in flight) has made no guest progress for 300,000 ms, and the
next dump shows the same, the start is reported as failed —
`reason=prehandshake-no-progress`, same evidence-preserving
shape, user-facing text "Windows did not finish starting: the
session stopped making progress before the desktop was ready,
so this start is reported as failed instead of waiting
forever." One progressing process anywhere, or a fetch in
flight, resets the count. The 300 s threshold sits far past
every healthy start on record: the slowest certified boot in
this lane's series took 199 s of wall time at box load 38 while
its dumps still showed `min_noprogress_ms` of 0 — progress, not
wall time, is what the rule measures. Probe scenario commit
`b7f5d4003`.

## Proof

Unit tests (`web/tests/suite/unit/stall-watchdog.test.mjs`,
which extracts the watchdog from the launcher source):

- FD-115: the fault-loop tests failed on the base launcher and
  pass with the fix (11/11 at that commit).
- FD-165: the three new tests fail on the base launcher
  (verified against the launcher as committed at `4ce0dc424`:
  11 pass, exactly the 3 FD-165 tests fail) and pass with the
  fix — 14/14.
- Full JS unit suite on the final build: **353 passed, 0
  failed**; `npm test` (legacy tests + unit suite): **ALL TESTS
  PASSED**.

Shaped-session probe (`web/tests/stall-watchdog-probe.mjs`,
real launcher from the built dist, stubbed glue): **5/5
scenarios pass** on the final build — the FD-77 booted stall,
the slow-but-alive start, the FD-77-scale pre-handshake stall
(no report), the FD-115 fault loop (reported at the threshold,
not before; cross-thread bursts ignored), and the FD-165
no-progress shape (reported once every process is stuck; one
progressing process keeps it unreported). Screenshots and
`summary.json` in `test-results/fd77-stall-surfacing/`.

Build: `tools/bw64-build.sh` BUILD COMPLETE, "Browser export
verified"; final dist BUILD_INFO git_sha `b7f5d4003`,
git_dirty_files 0, wasm `c0af957a` — unchanged from batch 7,
as both fixes are launcher-JS only.

First-visit series (all x86, fresh browser profile per boot,
bwretry=2, 900 s budget, FD-47 classification, handle `0x10052`
on every certified boot, 0 restarts and 0 rescues everywhere):

| series | build | attempted | measured | certified | other |
|---|---|---|---|---|---|
| repro (public route) | shipped batch-7 `4006ddb58` | 20 | 19 | 18 | 1 = FD-165 boot 10 |
| fixed | FD-115 fix only, `4ce0dc424` | 35 | 34 | 34 | — |
| final | both fixes, `b7f5d4003` | 36 | 36 | 36 | — |

Medians of certified-ready: repro 58.9 s, fixed 51.4 s, final
40.3 s (box load ranged 5-38 across the runs). Console censuses:
every preserved console of the fixed and final series (35 and
36) has **0 BW64-FAIL lines and 0 guest-memfault32 lines**; the
repro consoles likewise, including boot 10's. The unmeasured
boots never started a guest: the harness timed out waiting for
a probe slot on the shared box while sibling lanes' builds held
the probe queue (fixed-series boot 29, repro boot 15; each is
recorded as a harness error in its driver log and excluded from
the measured counts).

Tallies after this lane: the FD-115 fault-loop shape stands at
1 wedged in 54 measured shipped-build starts; the FD-165 shape
at 1 in 19 measured shipped-build starts (this lane's series is
its only sample so far). On a build carrying the fixes, either
shape now ends as a reported failure instead of a silent wait.

## Lane notes

- The first build attempt's process disappeared after the
  compile stage (13:46) leaving no error text and no dist; the
  compiled outputs were intact, and an immediate incremental
  rebuild completed and verified normally. No cause was
  established and no defect is filed on it; both build logs are
  in the evidence directory.
- Native suites were not run: the change is launcher-JS only
  and touches no native code (the wasm hash is unchanged).

## Files

- Fixes: `de0144c72` (FD-115), `3a4d59793` (FD-165) —
  `project/emscripten/wine64-launcher.js`,
  `web/tests/suite/unit/stall-watchdog.test.mjs`.
- Probe: `4ce0dc424`, `b7f5d4003` —
  `web/tests/stall-watchdog-probe.mjs`.
- Evidence: `test-results/fd115/` (series `repro-live-batch7/`,
  `fixed-series/`, `final-series/`, driver copies and logs,
  build logs, suite logs) and
  `test-results/fd77-stall-surfacing/` (probe summary and
  screenshots).
- Register: FD-115 closed as CL-119, FD-165 filed and closed as
  CL-120; Section B row `lane-fd115`.
