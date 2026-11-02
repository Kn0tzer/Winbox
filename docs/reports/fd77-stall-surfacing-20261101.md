# FD-77 remainder — stall surfacing in the default session

Lane `lane-fd77-stall-surface`, branched off integ `1d932714c`. FD-77's
freeze is fixed and closed (CL-34); its register entry and the FD-77
report both recorded one debt still owed: in a default (unarmed)
session, a boot that produces no window and no output sat silent
indefinitely. This lane builds that surfacing. Launcher-only change:
`project/emscripten/wine64-launcher.js`, plus tests. No C++ changed;
no readiness rule weakened.

## What was built

A stall watchdog inside the launcher, active only in default
sessions. It is off when `?stallwatch=0`, when `?bw64diag=threads`,
or when `?guestdiag` is an integer in 1..300 — armed diagnostic rigs
behave exactly as before.

- In a default session the launcher now arms the (already shipped,
  read-only) guest thread dump for its own use and, while no window
  has mapped, takes dumps itself: the first 90 s after the runtime
  starts, then every 60 s. `?stallwatchdelay` / `?stallwatchinterval`
  override the cadence in ms (minimum 250) for test rigs.
- A session is reported only when **all** of these hold together:
  the existing FD-47 desktop handshake is complete **as tracked by
  the existing output observer** (plausible `deskpin: READY`, at
  least five accepted X11 client connections, explorer opened — the
  watchdog reads that state, never writes it); no `XWire: first
  window mapped` has been seen; one process shows `running` ≥ 3 and
  `min_noprogress_ms` ≥ 60,000 in **two consecutive** dumps; and the
  dump's proxy-fetch footer shows **zero fetches in flight** in both.
  A fetch in flight is an explained wait (the FD-12 amplifier shape)
  and resets all counts; a dump whose footer never arrives counts
  nothing; a process that progresses or disappears resets its own
  count; a dump that returns "log lock held" (the dump's never-block
  contract) is a skipped sample, not evidence.
- On report, the held per-thread dump lines are flushed into the
  session log, followed by exactly one machine-readable line —
  `BW64-FAIL component=launcher.session reason=booted-no-window-no-progress pid=<pid> running=<n> min_noprogress_ms=<n> stall_dumps=2 detail=…`
  — and the session ends through the app's existing error surface:
  "The program stopped", the plain-language explanation ("Windows
  started, but this program never opened a window and stopped making
  progress, so the session is reported as failed. The session log is
  preserved — open Show logs for the captured evidence, or retry."),
  with Retry and Show-logs offered and the log pane preserved. The
  report fires at most once; the timers stop on report or on the
  first mapped window.
- Evidence hygiene: during the watchdog's own dumps only the summary
  lines (begin/end/proc/proxyfetch/ramalloc) reach the session log;
  per-thread lines are held back and flushed in full only ahead of a
  report, so a healthy slow boot's log is not flooded.

The bar is deliberately conservative, because the failure surface is
terminal: a false report against a slow start is the worse error.
Healthy Chromium boots map a window in roughly 33–71 s from click,
before the first dump. Boots that take minutes to map (Mindustry
class, window at ~296–384 s) or that execute shader work for
thousands of seconds without mapping (Godot) keep their threads
executing, so their no-progress readings stay small and cannot
convict. Detection lands at the second consecutive qualifying dump:
with the default cadence the earliest possible report is the ~150 s
dump, and only if the first dump already shows a full 60 s of no
progress; for an FD-77-shaped freeze completing ~78 s after runtime
start, successive dumps read ≈12/72/132 s of no-progress, so the
report lands at the ~210 s dump.

## Proof

- **Unit tests** — new `web/tests/suite/unit/stall-watchdog.test.mjs`
  (8 tests in the guest-diagnostics extraction-and-VM pattern):
  arming per session kind; the shaped park convicts exactly once
  with the FAIL line and the held evidence flushed ahead of it;
  slow-but-alive never alarms; a fetch in flight resets; pre-handshake
  stalls and a `running=2` process never convict; armed/opted-out
  sessions never start and never hold lines; a lock-held dump is a
  skipped sample; wiring assertions (including that the probe rigs'
  string-patch marker line stays byte-identical). **Red 8/8 on the
  unpatched launcher, green 8/8 on the patched one.**
- **Full JS unit suite on the built tree: 349 passed / 0 failed**
  (the integ baseline's 348 test files + the new one). This includes
  `cpu64-control-trace`, which VM-runs the runtime initializer
  without the session section; the watchdog's start call is guarded
  (`typeof stallWatchStart === "function"`) so that rig is
  unaffected, and `guest-diagnostics`' env/timer slices are untouched
  by construction (verified by the suite itself).
- **Build**: `tools/bw64-build.sh` completed — "Browser export
  verified", "BUILD COMPLETE"; completeness 0 failures, 1 standing
  skip; support pack `c4dbe35986a5`. The built wasm is byte-identical
  to the previously shipped build's (`1d46405a…`), as expected: no
  C++ changed. The built `dist/runtime/wine64-launcher.js` carries
  the final guarded watchdog code (verified by direct grep).
- **Shaped-session browser probe** — `web/tests/stall-watchdog-probe.mjs`
  runs the real site and the real launcher from the built dist with
  only the wasm glue replaced by a shaped guest, through the probe
  slot. All three scenarios pass (`test-results/fd77-stall-surfacing/summary.json`):
  1. "Booted, windowless, no-progress session is reported as failed;
     the FAIL line and the held dump evidence are preserved in the
     session log" — the page shows "The program stopped" with the
     plain-language failure text (screenshot:
     `test-results/fd77-stall-surfacing/reported-failure.png`), the
     log carries the `BW64-FAIL … reason=booted-no-window-no-progress`
     line with `min_noprogress_ms=90000`, the flushed
     `BW_THREAD_DUMP pid=42 tid=…` evidence, and conviction took at
     least two dumps.
  2. "Slow-but-alive start raises no alarm: progressing dumps, a
     fetch in flight, and a single stalled dump all pass; mapping
     and painting complete startup normally".
  3. "A stall before the desktop handshake completes is not
     reported — the session has not booted yet".
  Rig notes, for the next lane: the app shell validates the support
  archive with a HEAD request (content-length + accept-ranges), so
  stubbing zips breaks startup before any launch; and the cadence
  override must reach the runtime frame on a 302 redirect —
  `route.continue({url})` left the frame's final URL unchanged and
  the watchdog silently stayed on its default cadence (found by the
  debug rig's frame-URL read, not by guessing).
- **Healthy path on the built dist**: `web/tests/startup.test.mjs`
  passes 5/5 — its cases include "long silent startup is possible
  and is not a timeout failure". `web/tests/launcher-ui.mjs` fails
  at its line-31 demo-card count (15 cards rendered, 8 asserted):
  a pre-existing inconsistency at the branch point between
  `web/demos.mjs` (15 demos, rendered unconditionally by
  `web/app.mjs`) and the test's expectation, independent of this
  lane's change — the test replaces the runtime page with a
  fixture, so the changed launcher script never loads. Filed on
  the register as **FD-82**; log in
  `test-results/fd77-stall-surfacing/healthy.log`.

## Register

FD-77's entry gains an update block recording this surfacing as
built, with its gates and proof; FD-77 stays CLOSED (CL-34) — the
register debt the entry carried is discharged, no new closure number
is filed by this lane. Section B gains this lane's row. One further
defect found along the way is filed as FD-82 (the launcher-ui
demo-card count mismatch, above) — found, not fixed, as it is
outside this lane's change.

## Limits, stated plainly

- A freeze that keeps any one thread of the watched process
  executing will not be caught: the conviction needs the process's
  *slowest* running thread to show a full minute without an executed
  guest operation, twice in a row. That is the price of never
  false-reporting a slow start, and it is the same evidence the
  register's readiness rules already trust.
- Detection is dump-paced, not instant: with the default cadence a
  report lands minutes after the freeze completes (≈150 s at the
  very earliest, ≈210 s for the FD-77 shape). The alternative —
  reporting on a single dump or a shorter no-progress bar — is
  exactly the false-ready direction this project's history warns
  against.
