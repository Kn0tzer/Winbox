# FD-266 — silent after containment: a fault-loop-killed start never restarts

Filed by the batch-12 deploy lane, 2026-11-08 (register line 11281, Medium).
Fixed on branch `lane-fd266` (commits `715983582` + `d01c3b84e`); register
closed as CL-192. This record covers the defect, the first fix and why it
failed live, the corrected fix, and the live proof.

## The defect

In the batch-12 live acceptance the consumer browser program hit the
FD-205 fault loop three times (runs a1, a2, b3 — identical signature: a
guest memory fault, address `0xfffffd08`, instruction `0x10584384`, 65
fault lines each). Each time the containment refused delivery at count
130 (`delivered=0`) and the faulting guest process ended, exactly as
designed — but the session then sat silent until the 900-second timeout:
the preserved consoles carry zero `restart` lines, their last entries
are the lazy-fetch lines at about 23–26 seconds, and the timeout
screenshots are nearly dark with 0 frames. In the previous release the
same loop died differently (a worker error killed the page), so this
silent-after-containment shape is new to batch 12. The baseline consumer
series stands at 5/6 mapped. The underlying loop's trigger remains the
open question behind the containment; this defect is the session's silence
after containment, not the loop's trigger.

Closing terms (register): a session whose guest process dies via
containment either restarts the guest or reports the failure, proven by
a live consumer run that hits the loop and then either maps or fails
loudly instead of timing out silently.

## Mechanism (read from the code)

There is one launcher file, `project/emscripten/wine64-launcher.js`,
serving both widths. The stall watchdog's `stallWatchReportFaultLoop`
reports the loop (a `BW64-FAIL ... reason=prehandshake-fault-loop` line
plus a `window.bwRuntimeError` message, which the app shell surfaces),
but it scheduled no restart. The §4.68 auto-restart
(`observeSessionHealth` → `restartSessionBoot`) fires only on the single
signal "The explorer process failed to start", and the consumer driver
waits only for the log to contain 'first window mapped' and never checks
for errors — so the report went nowhere and the run timed out. The fault
loop is a fresh draw each boot (it recurs in only a minority of boots),
so a restart is the same class of remedy as the doom signal.

## The first fix, and why it failed live (commit 715983582)

The first fix refactored the restart scheduling in the self-healing block
into one shared function `scheduleSessionBootRestart(reason)` and called
it from `stallWatchReportFaultLoop`. Unit tests: 23/23 pass, and the
three new assertions fail on the base launcher (RED verified).

Live, it did not restart. Two live-intercept runs (g5, g6 — evidence in
`test-results/fd266-proof/live-intercept-v1/`) hit the identical loop (65
memfault lines, report + containment at count 129) and then timed out at
900 seconds with `restart=false` and the launcher served once. The
diagnosis, proven by a dedicated interception check
(`test-results/fd266-proof/intercept-verify.mjs`, result in
`intercept-verify-result.json`): the runtime iframe DOES receive the
fixed launcher (a fetch from inside the frame returns a file
byte-identical to the fixed launcher), and the scheduler's guards pass
by inspection — but demos launch with `session=0`
(`web/launch.mjs:53` hardcodes `session: '0'` in `runtimeParameters`),
so `SESSION` is false and the scheduler's `!SESSION` guard silently
returns. In addition, `bw64_session_ready()` goes true as soon as the
session context is captured (about 6.8 s in, long before any window
maps), so `sessionReady()` is also true in non-session mode and would
veto the restart even with the `SESSION` guard removed. Both guards
were inherited from the doom path, whose proven §4.68 behaviour is on
the suite page path (which does not pass `session=0`); on the
demo/consumer path the whole restart mechanism was inert.

## The corrected fix (commit d01c3b84e)

The fault-loop report now calls the shared scheduler forced:
`scheduleSessionBootRestart("prehandshake fault loop", { force: true })`.
A forced restart is gated only on "no window mapped yet"
(`stallWatchMapped`, owned by the watchdog section) and on the once-only
flag; it skips the `SESSION` and `sessionReady()` guards, which do not
apply to a start that never mapped. `restartSessionBoot` takes the same
flag plus the reason and re-checks the mapped gate when its 20-second
confirm timer fires, so a window that maps during the confirm window
abandons the restart. The doom path stays unforced, keeping its proven
§4.68 guards, and its final-attempt message is byte-identical. The
no-progress report (`stallWatchReportPrehandshake`) keeps its
report-only shape; its trigger is a stall, not a containment death.

Unit tests: 27/27 pass (`stall-watchdog.test.mjs` + `runtime-session-
restart.test.mjs`, including four new forced-path tests: schedules
outside a session or once ready, gated on no window mapped, navigates
outside a session, abandoned if a window maps during the confirm
window). The four new assertions fail on the v1 launcher (RED verified).

## Proof — live-intercept run g3, 2026-11-08 ~23:15 UTC

Method: the probe (`test-results/fd266-proof/live-intercept-probe.mjs`)
runs the chromium consumer demo against the LIVE site, serving this
lane's fixed launcher to the test browser in place of the shipped one
(Playwright route interception, test browser only). Everything else —
wasm, game archives, page origin, fetch timing — is the live site
exactly as the batch-12 deploy saw it. The live site itself is
untouched. The probe stops early once a run proves the fix.

Run g3 (evidence `test-results/fd266-proof/live-intercept/chromium-
live-g3/`, console 3,955 lines, summary `proof: true`), read from the
preserved console:

- `[19.5s] error: BW64-FAIL ... reason=prehandshake-fault-loop pid=78
  tid=81 fault_lines=32 eip=0x10584384 addr=0xfffffd08` — the loop, the
  exact deploy signature.
- `[19.5s] error: session: prehandshake fault loop (boot attempt 1);
  scheduling a clean session restart` — the forced scheduler fired.
- `[19.5s] log: BW64-FAIL fault-loop-contained pid=78 tid=81 sig=11
  eip=0x10584384 addr=0xfffffd08 count=129` — containment ended the
  faulting guest process.
- `[39.5s] log: session: restarting session boot (attempt 2)` — exactly
  the 20-second confirm delay later, the restart navigated to a fresh
  boot (`bwretry=1`).
- `[75.7s] log: XWire: first window mapped (present sink=SDL)` — the
  fresh boot mapped. Stage `mapped`, `tMapped` 75.7 s, 65 memfault
  lines, launcher served twice (one per boot), 0 page errors, 0 HTTP
  errors.

The probe's full series: g1, g2 mapped cleanly (~37 s each); g3 is the
proof run above; the probe stopped there. The v1 series (6 runs) is
preserved in `live-intercept-v1/` (g5, g6 are the two loop-hit runs
whose silence diagnosed the v1 failure).

## Local consumer series (staging calibration, not the proof)

Six local runs against a staged proof dist (shipped batch-12 wasm,
sha256 `62f6123e…`, plus the fixed launcher) all mapped cleanly
(~33–36 s, 0 memfault lines): the loop never drew locally, so those
runs do not prove the fix. The loop is a guest-side race whose odds
shift with archive-fetch timing (the local fetch is far faster than the
live host's), which is why the proof was taken live instead. A notepad
smoke run against the staged dist mapped (119.4 s, 2,766 console
lines), proving the staging boots. The staged dist's `demos.mjs`
differs from shipped by one line only: `archiveBase` is the page
origin's `/games/` (the live games host's CORS blocks cross-origin
fetch from a local test server, and a relative base breaks `new URL()`;
the zip served is the box's own copy, sha256 `6420689e…`,
468,348,892 bytes, identical to the live host's file). Two staging
failures are preserved as evidence: the CORS attempt (fetch blocked)
and the bad-base attempt (relative base, empty console). The
integration tree's `dist/` is a stale Nov 6 build (wasm `79a94826…`)
and was not used.

## Checking-service light checks

Checking-queue pipeline 66 (commit `360598698`, the record +
register-close tip) adjudicated clean in the standard shape:
citations exit 0 (352 rows, 232 MATCH / 14 MATCH-INHERITED, 0 drift);
js-unit 352 passed / 2 failed — the known bare-checkout pair
(`provenance-artifacts`, `runtime-completeness`); js-legacy exit 0
(0 fail); native-main 706 tests, 0 FAILED; native-x64 121 tests, 0
FAILED; run-native exit 0. Pipeline status "failure" is the standing
shape, driven solely by js-unit's known pair. Evidence:
`winbox-project/checks/evidence/pipeline-66/` (step logs; the
publish-results step could not upload to MinIO — `XMinioStorageFull`,
the store's disk threshold tripped — so the full per-step result files
were not archived, only the harvested step summaries, which carry the
figures above).

The full JS unit suite run locally on this tree gave the same count
(352 passed, 2 failed) with a different second failure:
`binary-patch-registry` instead of `runtime-completeness`. That local
failure is a staging artifact, not a regression: the test compares
`dist/runtime/BUILD_INFO.json` against `web/runtime/BUILD_INFO.json`;
this tree's staged `dist/` is symlinked to the live batch-12 build
while `web/runtime/BUILD_INFO.json` is absent (the tree never ran a
browser build), so the comparison throws. In the queue's bare checkout
the test skips (no `dist/`), and pipeline 66 shows the known pair.

## Follow-ups (noted, not done)

- The no-progress report (`stallWatchReportPrehandshake`) is a
  candidate for the same forced-restart treatment; its trigger is a
  stall, not a containment death, so it was left report-only.
- The §4.68 doom restart is inert on the demo path (`session=0`) by
  the same guards; its proven behaviour is on the suite path. Changing
  that is out of scope for this lane.
- The underlying fault-loop trigger remains the open question behind
  FD-205's containment.
