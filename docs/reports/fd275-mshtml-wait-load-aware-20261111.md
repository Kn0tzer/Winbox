# FD-275 — the supporting-programs check's web-page stage waits 360 s, too short under sustained load

## The defect, in plain terms

The supporting-programs check opens a web page inside the emulator (Wine's
built-in browser engine, Gecko) and waits for the page test to finish. The
wait is a fixed 360 seconds. Under sustained machine load the page test takes
longer than 360 seconds, so the check fails even though nothing is broken. It
failed three times in the batch-17 deploy's live checks (2026-11-11): live
batch 17, then identically on the batch-16 rollback, then on a live re-run —
so it is not a regression in the new build. The product code is byte-identical
to batch 16.

## Investigation

The check (`web/tests/suite/browser/common-dependencies.test.mjs`) boots the
guest, stages the `mshtml-document.exe` fixture, spawns it, and waits for the
line `MSHTML: RESULT 0`. FD-273 had raised that wait from 180 s to 360 s on
measurements up to load 12–14 (the stage then took 230 s); its calibration
note claimed 360 s would cover load ~20. Under sustained load 9–16 the stage
needs more than 360 s — the fixed wait broke again within a day of FD-273
landing, in the batch-17 deploy.

Stage times measured (spawn to `MSHTML: RESULT 0`):

| run | box load | stage time |
|---|---|---|
| FD-273 discovery probe, PASS | 8–9.5 | 130.2 s |
| FD-273 proof run, PASS | 12–14 | 230.1 s |
| batch-17 deploy live, FAIL | 15.54 | > 360 s (never arrived) |
| batch-17 deploy A/B batch 16, FAIL | 16.31→10.79 | > 360 s (never arrived) |
| batch-17 deploy live re-run, FAIL | 9.18→12.81 | > 360 s (never arrived) |
| FD-275 proof run2, MSHTML stage PASS | 15.38 at spawn | **593.7 s** (covered by the 900 s load-scaled wait; the old 360 s wait would have failed) |

In all three failing runs the guest was alive and progressing (console kept
growing, METRICS heartbeats ticking, stack dumps showing the ordinary
blocking-socket-read wait) — the wait ran out, not the guest.

## The fix (test-only)

`web/tests/suite/browser/common-dependencies.test.mjs`:

- The MSHTML wait is now scaled with the 1-minute load average at spawn
  time: 60 s per unit of load, floored at the FD-273 360 s and capped at
  900 s so the test budget still fits. The load and the chosen wait are
  logged, and the measured stage time is logged, so each run records its own
  calibration. This breaks the raise-again cycle: a fixed wait keeps breaking
  as load rises; the scaled wait grows with the load that slows the stage.
- The test's `timeoutMs` goes from 1,500,000 ms to 2,400,000 ms so the total
  budget (boot up to 600 s in the deploy band + MSHTML wait up to 900 s +
  OpenAL wait up to 600 s + audio 30 s + staging overhead) still fits. The
  runner kills the test after `timeoutMs` + 60 s.

No product code changed; the OpenAL wait is load-scaled too (see below).

The OpenAL stage turned out to need the same treatment. Proof run2 (MSHTML
stage passing, 593.7 s) went on to fail on the OpenAL stage: `Guest output
/OPENAL: RESULT 0/ not seen within 180000ms`, the guest alive and
progressing, no OPENAL output at all in 180 s. At load ~21 the OpenAL startup
stage needs more than the fixed 180 s — the same defect one stage later
(FD-273's proof measured it at 46.9 s at load 12–14). So the OpenAL wait is
scaled with the same rule (60 s per load unit, floored at 180 s, capped at
600 s — a smaller cap, the stage is lighter than Gecko startup), and the
stage time is logged the same way. The test's `timeoutMs` is 2,400,000 ms to
cover the worst case (boot ~600 s in the deploy band + MSHTML up to 900 s +
OpenAL up to 600 s + audio 30 s + overhead).

## Proof

- **Proof run1** (fixed 600 s boot wait, old test): failed at the boot wait
  (`page.waitForFunction: Timeout 600000ms exceeded` in `bootGuest`, total
  611,560 ms, load ~21–27) — the guest never became ready, so the run never
  reached the MSHTML stage. That is FD-274's defect, not this one.
- **Proof run2** (boot wait temporarily raised to 1800 s as a control,
  MSHTML wait load-scaled, OpenAL still fixed 180 s): boot took ~930 s at
  load ~15–25; the MSHTML stage took **593.7 s** at 1-min load 15.38, inside
  the chosen 900 s wait — the old 360 s wait would have failed. The run then
  failed on the OpenAL stage (180 s wait, guest alive, no output), which is
  what extended the fix to OpenAL.
- **Proof run3** (both waits load-scaled, timeoutMs 2,400,000 ms; same boot
  control): **PASS common-dependencies (1,270,776 ms)**, `SUITE RESULT: 1
  passed, 0 failed`, exit 0. Boot took ~11.5 min at load ~11–20; the MSHTML
  stage took **498.8 s** at 1-min load 21.94 (chosen wait 900 s); the OpenAL
  stage took **92.4 s** at 1-min load 19.17 (chosen wait 600 s). Loads
  through the run stayed at or above 12 from the boot on, and the two stages
  ran at load ~19–22 — the band where the old fixed waits failed. One
  startup flake on the first run3 attempt (the browser server's
  DevToolsActivePort file missing at setup, a harness race before any test
  code ran); the re-run started cleanly, so the flake is recorded, not fixed
  here.

All three runs used the live batch-17 dist (wasm `bcd9d500…`), no product
change. The lane's own dist build from the fix commit verified the export
("Browser export verified", wasm `bcd9d500…` byte-identical to batch 16).
Local JS unit suite on the built tree: **354 passed, 0 failed**.

## Light checks

Queue gate pipeline #95 on the fix commit `9b4133944`, read from the
preserved step logs (`checks/evidence/pipeline-95/queue-logs/`, steps
811–819), in the standing acceptance shape:

- 811 verify-source: mounted commit `9b4133944…` exactly
- 813 citations: 352 rows, 0 drift (MATCH 232, MATCH-INHERITED 14,
  UNCHECKABLE 99, EXEMPT 7)
- 814 js-unit: **352 passed, 2 failed** — exactly the two known bare-checkout
  artifacts (`provenance-artifacts`, `runtime-completeness`)
- 815 js-legacy: 0 fail
- 816 native-suites: **main 706/0 in 377 s; x64 121/0 in 31 s**
- 817 run-native: 0
- 819 verdict: only `js-unit.exit` is 1; every other exit is 0

Pipeline status "failure" driven solely by the js-unit exit — the standing
shape, same as the batch-17 gate #94.

## Interaction with FD-274 (sibling lane)

The sibling lane is raising the boot wait (`bootGuest`, `probe-base.mjs`)
because a fresh 64-bit boot alone exceeds 600 s at the current load. This
lane's proof run1 failed exactly there (boot wait, never reaching MSHTML),
so proof runs 2 and 3 ran with the boot wait temporarily raised to 1800 s as
a control, mirroring the sibling's discovery shape; the control was reverted
before commit and is not part of this fix. The two fixes touch different
files (this lane: only `common-dependencies.test.mjs`; the sibling:
`probe-base.mjs` and its own test), so no merge conflict is expected. One
budget note for composition: if FD-274 lands a boot wait above 600 s, this
test's `timeoutMs` (2,400,000 ms) still covers the measured worst case —
boot ~930 s + MSHTML up to 900 s + OpenAL up to 600 s + audio 30 s +
overhead — with margin.
