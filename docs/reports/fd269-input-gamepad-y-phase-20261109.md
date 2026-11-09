# FD-269 — input-gamepad Y-phase wait takes the correct line

## What the defect was

The input-gamepad suite test's left-stick Y phase waited for the first
post-mark XInput state line carrying any `ly` value. The guest witness
prints its current state more than once per state (each state change
produces a repeat of the line a moment later), and console delivery can
lag the phase mark — so a repeat of the full-up line (`ly=32767`,
printed before the stick moved down) could land after the mark, match
the any-value pattern, and fail the sign check even though the guest
was correct. Proven at the batch-14 gate, 2026-11-09: the test failed
at the same assertion in three runs (in-suite plus two focused
re-runs, box load 5–11 throughout), each time quoting an `ly=32767`
line, while every run's preserved console contains the required down
state `lx=32767 ly=-32767` delivered in the same phase. The test file
itself documents this in-flight-line hazard at its trigger phase and
hardens that phase by waiting for a line evidencing the press itself;
the Y phase's pattern accepted any ly value and was not hardened.
Test-harness defect only; no guest behavior was wrong.

## The fix

Commit `b4c51b2d9` on branch `lane-fd269`, one change in
`web/tests/suite/browser/input-gamepad.test.mjs`: the Y-down wait now
builds its pattern from the measured up value, accepting only a line
whose `ly` has the opposite sign — a line evidencing the down state
itself. With the up value positive (as measured: `ly=32767`), the wait
is `/XINPUT slot=0 STATE .* ly=-\d+ /`; the pattern is built the
other way round if the up value is ever negative. The existing
sign-and-magnitude check on the returned line is unchanged. A comment
records the hazard, mirroring the trigger phase's hardening note.

## Proof

Build on the lane tree: `tools/bw64-build.sh`, "Browser export
verified" (`test-results/fd269/build.log`), wasm `b01c7dc6de6bc9aa…`.

Focused runs of `node web/tests/suite/runner.mjs --browser
--filter=input-gamepad` on the fixed build, each running both guest
architectures:

- First proof attempt: killed before testing — the probe-slot wait was
  starved by a two-build relay from sibling lanes (the build-pending
  flag blocks probe starts while it names a live pid); nothing had
  been tested and no evidence was lost. The restarted first run then
  FAILED at the button-A phase on the 64-bit arm (`buttons=0x1000`
  never arrived within 60 s, box load 15.2); the 32-bit arm in that
  same run passed all phases including Y-down. Adjudicated a load
  artifact, not FD-269: the failure precedes the changed code, the
  guest printed its resting state and then stalled, and the shape is
  the same guest-starved-under-load collapse the batch-14 gate
  recorded for memory-shared. Preserved:
  `test-results/fd269/run1.log` + `test-results/fd269/run1-evidence/`.
- Run 2: **PASS input-gamepad (482,599 ms)** — box load peaked at
  19.0 during the run.
- Run 3: **PASS input-gamepad (313,496 ms)** — box load peaked at
  13.05 during the run.
- Run 4: **PASS input-gamepad (412,823 ms)** — box load peaked at
  16.04 during the run.

Three consecutive passes, both architectures each time, all under
load above the gate's 5–11 range. The down state `ly=-32767` is in
every passing run's preserved console on both architectures
(`test-results/fd269/run2-evidence/`, `run3-evidence/`,
`run4-evidence/`).

Checking queue pipeline #73 on the fix commit `b4c51b2d9` (verify-source
names the exact commit, tree pristine): citations exit 0, js-legacy
clean, native-main 706 tests / 0 FAILED, native-x64 121 tests /
0 FAILED, run-native pass, js-unit 352 passed / 2 failed (only the
two known bare-checkout artifacts, `provenance-artifacts` and
`runtime-completeness`) — the standing acceptance shape; the
pipeline's overall "failure" status is driven solely by those two
known failures, as usual.

## Notes

- The X phase and Y-up phase waits share the same any-value pattern
  shape and could in principle match a stale in-flight line; neither
  has ever failed (their stale candidates are resting/zero lines
  printed once and already consumed before the mark). Left as an
  observation, not filed — no failure observed.
- The first proof attempt re-exposed the build-pending probe
  starvation (two sibling builds relayed; a probe start waits while
  build-pending names a live pid). No action — the known
  infrastructure finding stands.
