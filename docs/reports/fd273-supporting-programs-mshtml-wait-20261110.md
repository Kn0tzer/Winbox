# FD-273 — the supporting-programs check fails on its web-page test under load

## The defect, in plain terms

The supporting-programs check opens a web page inside the emulator (Wine's
built-in browser engine, Gecko) and waits 180 seconds for the page test to
finish. Under heavy machine load the page test takes longer than 180 seconds
to finish, so the check fails — even though nothing is broken. It failed
three times in the batch-15 + FD-271 deploy's live checks (2026-11-10): twice
on the new build, then identically on the previous build, so it is not a
regression in the new build.

## Investigation

The check (`web/tests/suite/browser/common-dependencies.test.mjs`) boots the
guest, stages the `mshtml-document.exe` fixture, spawns it, and waits up to
180 s for the line `MSHTML: RESULT 0`. The fixture itself loads a small HTML
document through Wine Gecko and reads back its title and body text; its own
ready-state poll is capped at 10 s, so the long stage is Gecko starting up
(DLL loads, engine initialisation), not the document load.

Stage times, measured from the runs' own `bw64: METRICS` heartbeats (one per
~0.5 s of guest time):

| run | box load | boot to spawn | spawn to MSHTML RESULT |
|---|---|---|---|
| batch-15 gate, PASS (428 s total) | gate load | ~241 s | ~135 s |
| discovery probe, PASS | 8–9.5 | 356 s | **130.2 s** |
| deploy live, FAIL (590 s total) | 8.9 → 14.9 | ~380 s | **> 180 s (never arrived)** |
| deploy rerun1, FAIL (573 s total) | 5.5 → 13.3 | ~380 s | **> 180 s (never arrived)** |
| deploy A/B batch 14, FAIL (671 s total) | 14.2 → 12.0 | ~480 s | **> 180 s (never arrived)** |
| proof run, PASS (504.6 s total) | 12–14 | 221 s | **230.1 s** |

The stage time grows with load: ~130 s at load ~9, over 180 s at load 12–15,
230 s at load 12–14. The wait is simply too short for the load the check
runs under.

### What the socket-wait stack dump does and does not mean

Each failing run's tail shows a guest thread `WAITING
KUnixSocketObject::lockCond`, in a blocking `recvfrom` on a socket. That is
the ordinary state of a thread doing a blocking socket read — the snapshot at
one instant, not evidence of a stall. The same suspicion was disproved for a
different wait in the FD-15 entry: threads in that wait are in their ordinary
idle read. The guest in these runs was alive and progressing throughout: in
the discovery probe the console kept growing every 30 s sample (4,507 lines
at spawn, 6,733 at RESULT) and the METRICS heartbeats kept ticking.

## Decisive experiments

Both runs used the shipped live build (wasm `05bc6962…`), no product change.

1. **Discovery probe** (`test-results/fd273-proof/fd273-discovery.mjs`,
   600 s wait): boot ready 356.4 s, spawned 360.3 s, `MSHTML: RESULT 0` at
   490.5 s — stage 130.2 s — with the guest emitting new console lines
   continuously. Evidence: `test-results/fd273-proof/discovery-run1.log`,
   `discovery-timings.json`,
   `test-results/suite-fd273-discovery/console.log`.
2. **Proof run** (the full `common-dependencies` test with the fixed 360 s
   wait, at load 12–14 — the band where the old wait failed three times):
   `PASS common-dependencies (504647ms)`. The stage took 230.1 s — over the
   old 180 s wait, inside the new 360 s wait. The OpenAL stage took 46.9 s,
   well inside its unchanged 180 s wait. Evidence:
   `test-results/fd273-proof/proof-run1.log`,
   `test-results/suite-common-dependencies/console.log`.

## The fix (test-only, commit `de2d751cc`)

`web/tests/suite/browser/common-dependencies.test.mjs`:

- The MSHTML `waitForLog` goes from 180,000 ms to 360,000 ms, with a comment
  recording the calibration.
- The test's `timeoutMs` goes from 1,200,000 ms to 1,500,000 ms, so the total
  budget (boot + 360 s + OpenAL 180 s + audio 30 s + overhead) still fits
  under load.

Calibration: 360 s covers ~2.7x the 130 s stage measured at load ~9, and on
the load scaling observed (130 s at load ~9, 230 s at load ~13) it covers
load ~20. No product code changed; the OpenAL wait is unchanged.

## Light checks

Pipeline #87 on the fix commit `de2d751cc` finished in the standing acceptance shape: verify-source names `de2d751cc` exactly, citations exit 0 (352 rows, 0 drifts), js-legacy 0 fail, js-unit 352 passed / 2 failed (only the two known bare-checkout artifacts), native-main 706 tests 0 FAILED, native-x64 121 tests 0 FAILED, run-native exit 0. The pipeline's overall "failure" status is driven solely by the js-unit exit.
