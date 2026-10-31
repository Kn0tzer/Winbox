# FD-40 characterization — the boot64-smoke stall (2026-10-31)

Branch `lane-fd40` (worktree `wt-fd40`), based on `lane-fd15`
tip `5f2936f4e` (the register carrying FD-40 lives there).
Question put to this lane: is the recorded `boot64-smoke`
stall an artifact of machine load, or a real defect — and if
real, what is the mechanism?

## Verdict

**A real, reproducible stall with a convicted mechanism —
not a load artifact, and not the unix-socket defect the
register originally suspected.** Machine load is at most
the trigger. The mechanism is a guest boot thread waiting
forever, on a ~30 s retry cadence, for a named object that
no component of the session ever creates. Once entered,
the state is permanent at any load: the guest issues no
other request at all.

## Method

- Built this tree's dist (`bw64-build.sh`; "Browser export
  verified") — code identical to the FD-15 tip.
- Wrote an instrumented probe (`fd40-probe.mjs`, preserved
  in this tree at `test-results/fd40-characterization/`)
  that boots the exact `boot64-smoke` session (same URL
  parameters, `WINEDEBUG=+loaddll,+server`) through the
  probe-slot system, timestamps every console line with
  wall-clock elapsed time, polls the runtime's input
  snapshot every 5 s, and records the box load average.
  Milestones and the guest's own server-request stream —
  not file activity — are the evidence.
- Compared against: the FD-15 lane's two failing captures
  (its fix build and an unmodified `c4bab5e07` baseline),
  and the passing `boot64-smoke` capture of 2026-10-26 in
  the integration tree (`test-results/suite-boot64-smoke/`,
  build `b4897e00f`).

## What the instrumented runs show

Run 1 (`test-results/fd40-run1/`, started at load 2.66,
1-min average; load rose during the run):

- Milestones: server execve at 28.1 s; `user32.dll` loaded
  by 93.9 s; the test program prints `vkboot` at 146.9 s
  and its process exits with status 0.
- At 163.4 s — after ~16 s in which the guest issues no
  request at all — one thread begins the sequence:
  open `\Sessions\1\BaseNamedObjects`, create `DBWinMutex`,
  `open_mapping(DBWIN_BUFFER)` → `OBJECT_NAME_NOT_FOUND`.
  It repeats the open every ~30.0 s (163.4, 193.4, …,
  733.9). From 163.4 s to the end of the capture (733.9 s)
  those retries are the ONLY requests the entire guest
  issues. `create_winstation` never appears.
- The thread is the session's boot thread: earlier in the
  same capture it created
  `\KernelObjects\__wineboot_event` and spawned
  `wineboot.exe --init`.

Run 2 (`test-results/fd40-run2/`, started at load 1.90):

- Echo printed at 128.7 s; the same thread pattern
  (this run's boot thread) enters the identical DBWIN
  retry at 143.9 s, again after a ~16 s guest silence
  following the last boot-process termination. Still
  looping when the capture was stopped at ~400 s.

Combined with the FD-15 lane's three failures, the
signature has reproduced 5/5 in this environment, across
three builds (FD-15 fix, unmodified baseline, this tree).

## Why the wait can never be satisfied

`DBWIN_BUFFER` is the shared section of the Windows
debug-output protocol (DBWIN): a sender creates/opens
`DBWinMutex`, then opens `DBWIN_BUFFER` to publish a
debug string. Per the documented protocol, the buffer and
its companion events exist **only while a debug listener
is running**, and a sender whose open fails is supposed
to abandon the send and continue. In every captured log —
the five failing runs and the passing one — the number of
`DBWIN_BUFFER` creations is **zero**: nothing in this
session (not the emulator's session programs, not
`deskpin.exe`, not any Wine component) ever creates it.
The boot thread's retry therefore has no owner: there is
no process whose progress could ever fulfil it. This is
the point that rules out the load explanation in its
strong form — during the wait the guest does literally
nothing else, so there is no work for a faster or quieter
machine to accelerate; ten more minutes of waiting in
run 1 changed nothing but the retry count.

The passing 2026-10-26 capture never takes this path: its
boot creates the window station (`create_winstation`,
name `WinSta0`) during initialization, before the echo
prints, and no DBWIN object is touched at all.

## What load actually does here

Entry into the wait follows a ~16 s total guest silence
after the last boot process terminates, in both
instrumented runs — the signature of a boot-side wait
expiring, after which the thread goes to report it through
the debug-output path and blocks there instead. A slow
boot plausibly causes that expiry, and every failing
capture was taken on a contended box. But run 2 started
at a 1-minute load of 1.9 and still entered the wait, and
once entered, the wait persists unchanged regardless of
load. Load is therefore recorded as the probable trigger,
not the mechanism, and "the test passes when the box is
quiet" is **not** established: the same test passing
repeatedly under previously-failing conditions was not
observed in this environment.

## The original suspicion is disproven

FD-40 as registered guessed at a unix-socket wait defect,
because the timeout dumps showed guest threads waiting in
`KUnixSocketObject::lockCond`. Those threads belong to a
service process sitting in its ordinary idle read on the
server socket; the server thread itself was in an ordinary
poll. The complete server-request stream accounts for the
non-progress without them: the only unfulfilled wait in
the session is the `DBWIN_BUFFER` open, and the socket
layer faithfully delivered everything that was sent
(the workload itself — boot, load, echo, clean exit —
all traversed it). No socket waiter was ever blocked with
data pending.

## Still open (named, not hidden)

1. What the boot thread was waiting for during the ~16 s
   silence — its report went into the DBWIN void, so its
   content is unrecoverable from these captures. Naming it
   needs a build whose debug output goes somewhere
   readable (e.g. a staged Wine with the send abandoned or
   redirected), or a listener shim that creates the DBWIN
   objects and records what arrives.
2. Whether taking this path is a regression since the
   2026-10-26 passing capture (different build) or was
   always latent behind boot speed. One passing capture
   cannot distinguish those.

## Fix levers (for whoever picks FD-40 up)

Not the emulator's socket layer. In order of directness:
the staged Wine's debug-output path should abandon the
send when no listener exists (the documented DBWIN
behaviour — a staged-payload/backport matter); or the
session should provide a minimal DBWIN listener so the
send completes (which would also make item 1 above
readable); or the boot wait that expires at ~16 s should
be found and satisfied. Any of these is verifiable by the
same instrumented boot: `create_winstation` reached, no
DBWIN retry in the request stream.

## Housekeeping

- Evidence preserved in this tree: `test-results/fd40-run1/`
  (full capture incl. per-5 s polls and load record),
  `test-results/fd40-run2/` (console capture), the probe
  driver at `test-results/fd40-characterization/fd40-probe.mjs`.
- No project code changed by this lane; the deliverable is
  the characterization and the register update.
- Generated build state stripped before finishing
  (`dist`, `web/runtime`, `project/linux/Build`, copied
  payload trees); nothing pushed; master and the live
  site untouched.
