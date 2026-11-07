# Timing and wait family: measured against the host's raw contract

Lane: `lane-timing-waits` (worktree `wt-timing-waits`), off the
integration tip `faf95d26c`, 2026-11-06/07.

## What was done

The whole timing/wait syscall family — clocks and resolution,
sleeps, interval timers, resource usage, child waits, descriptor
waits (select/pselect6/poll/ppoll/epoll), futex-adjacent waits,
signal waits, timerfd, POSIX timers — was probed systematically
against the host's real behaviour, on both guest widths. The host
reference is a set of C programs issuing the syscalls through the
raw `syscall()` trampoline with correct prototypes (no libc
wrapper validation): `hostref-timing.txt` (243 rows),
`hostref-timing2.txt` (11 rows) and `hostref-timing3.txt` (11
rows, the timerfd flag mask, cross-checked with a native strace
run). Guest behaviour was measured with static bare-ELF probe
binaries run in the full kernel (`guestprobe-run*`,
`guestprobeA/B/C/D/E/F`, `timingprobe2..7`) and, after the fixes,
with two new native suite tests (`testTimingWaits32`,
`testTimingWaits64`) driving the real dispatch paths.

Result: 19 divergences fixed on this branch (register entries
FD-193..FD-211, lane-local numbers), 11 filed open (FD-212..
FD-222). Two standing test pins rested on wrong assumptions and
were corrected with the measurements written into their comments
(FD-208, FD-209).

## Fixed (summary; the register entries carry the detail)

- 64-bit `clock_gettime` ignored the clock id: every id,
  including invalid ones, returned wall time. Now validated
  (`KSystem::isSupportedClockId`) with per-clock domains
  mirroring the 32-bit side.
- `clock_getres`, all three implementations: invalid ids
  answered, wrong resolutions claimed (1 ms everywhere on
  32-bit, 1 ns everywhere on 64-bit), null pointer written
  through. Host: invalid → −EINVAL; null → accepted, no
  write; fine clocks 1 ns, coarse (5, 6) 1,000,000 ns.
- Remaining time on interrupted sleeps was never written on
  either width (the 64-bit dispatcher did not even pass the
  address). The wrappers now compute and write it on −EINTR
  for relative sleeps; nothing is written for TIMER_ABSTIME
  sleeps, as on the host.
- 32-bit `clock_nanosleep` refused any nonzero flags
  (TIMER_ABSTIME included) and validated nothing. Rewritten:
  null → −EFAULT, field validation, ABSTIME conversion
  against the same sources `clock_gettime` reports, thread
  CPU clock → −EOPNOTSUPP, interruptible sleep path.
- 64-bit `clock_nanosleep`: flags beyond TIMER_ABSTIME
  refused (host ignores them — strace-verified), clock 2
  refused (host accepts), clock 3 answered −EINVAL (host:
  −EOPNOTSUPP), and the ABSTIME base differed from
  `clock_gettime`'s monotonic source.
- `getrusage`: 64-bit answered 0 with zeros for any input;
  32-bit misclassified `who` and wrote through null. Both
  widths now validate in the host's order and report real
  summed thread CPU times (new `cpuTimesMicroSeconds`).
- Itimers: negative seconds clamped to ~49 days (64-bit) or
  unvalidated (32-bit); `getitimer` checked its pointer
  before `which`. All in the host's order now.
- `wait4` answered −ECHILD for unknown option bits on both
  widths; the host validates options first (−EINVAL).
- 64-bit `alarm` was missing (−ENOSYS); dispatched to the
  existing `KProcess::alarm`.
- 64-bit `times` returned 100,000 ticks/second; now the
  host's 100 Hz.
- 32-bit `gettimeofday` wrote through a null tv.
- `epoll_create` size ≤ 0 and `epoll_create1` flags outside
  CLOEXEC|NONBLOCK returned descriptors on both widths.
- 64-bit `epoll_wait` accepted maxevents 0/−1 and returned
  events; the host answers −EINVAL.
- `poll` set POLLNVAL but did not count it (closed
  descriptor answered 0, host 1). The tally in
  `internal_poll_impl` now counts every nonzero revents.
- Negative/out-of-range timespecs parked `pselect6` and
  `ppoll` (unsigned reads) instead of −EINVAL — a guest
  passing one hung the session (the pre-fix probe runs die
  exactly there). Both validate now; ppoll gained its
  missing timespec permission check; pselect6 validates its
  sigmask argument (size 8 only, readable pointer).
- `pselect6`/`ppoll` never wrote back the unslept timeout;
  Linux does (host-measured: ready, interrupted and expired
  shapes). The select core gained a nanosecond-fraction
  mode; ppoll writes back at its case. This corrects the
  FD-23-residue register note claiming pselect6 does not
  write back "as on the host" — it does.
- 64-bit `timerfd_settime` rejected the valid
  TFD_TIMER_CANCEL_ON_SET bit. Raw measurement (grid +
  native strace): the host's mask is 0x3 — flags 0–3 → 0,
  4 and −1 → −EINVAL. The case now accepts exactly that
  (CANCEL_ON_SET accepted; its effect is not modelled,
  nothing here sets the clock).
- The select core refused nfds > 1024 even with all set
  pointers null (nothing to buffer; the host sleeps). The
  cap now applies only when a set pointer is non-null.
- FD-211 (found en route, outside the family): the
  X64Headless target did not build at the tip —
  `networkutils.cpp`'s leading `#undef U64` block kills the
  macro the precompiled header already defined. The queue's
  native legs build only the test targets, which exclude
  the file. Three-line deletion.

## Filed open (summary)

- FD-212 `waitid` unimplemented both widths (contract
  measured, incl. the reap-on-faulting-info-write shape).
- FD-213 timer-expiry signals never delivered to a guest
  handler in the bare-ELF vehicle; blocked timer signals
  still interrupt sleeps; the expired signal is not
  dequeueable via `rt_sigtimedwait`; `pause()` parked past
  expiry. Direct (tgkill) delivery works. Scope caveat:
  bare-ELF vehicle; Wine impact unmeasured.
- FD-214 wait4 stop-report consumes the child, no stop
  status.
- FD-215 futex dispatch falls back to the threadless stub
  in the full-kernel ELF vehicle (WAIT on a match answers
  −EAGAIN immediately; bad addresses answer −EAGAIN/0
  instead of −EFAULT) — with the methodological note that
  futex shapes must be probed via the harness's real path.
- FD-216 mapped PROT_NONE pages are treated as accessible
  by the non-checked syscall accessors (measured across
  the family).
- FD-217 ITIMER_VIRTUAL/PROF refused (host accepts; the
  refusal is pinned deliberately in the suite).
- FD-218 i386-only gaps: pause, rt_sigtimedwait, ppoll,
  epoll_pwait (−ENOSYS on 32-bit, dispatched on 64-bit).
- FD-219 the guest dies at `epoll_ctl` DEL with a null
  event in the ELF vehicle (deterministic, isolated; the
  ctl code is exonerated by reading — the death is in the
  surrounding machinery), plus related deaths after
  interrupt-heavy sequences in the full-grid runs.
- FD-220 POSIX timers subsystem-absent (contract measured).
- FD-221 model limits (ALARM-clock bases, zero accounting
  fields, epoll_pwait maxevents cap, µs/ms write-back
  fidelity, process-local monotonic epoch).
- FD-222 epoll_pwait rejects sigsetsize ≠ 8 with a mask;
  the host accepts any size there.

## Disclosures (things that went wrong or were traps)

- Two delivered summaries of this lane's own probe runs were
  fabricated by the delivery channel (probe A run 1, probe B
  run 3 — the latter never even launched). Every figure in
  this record comes from preserved raw logs re-read from
  disk; probe B run 3's "results" were discarded wholesale.
- A first timerfd host-reference run reported errno 29 for
  every row: the program called glibc `syscall()` through an
  implicit prototype, passing garbage in the fourth argument
  register. Discarded; the mask was re-measured with a
  correct prototype and a native strace cross-check. This is
  the mirror image of the standing lesson (measure the raw
  kernel, not the wrapper): a raw trampoline with a wrong
  prototype is not raw either.
- An early probe bug of this lane's (sigsetsize 16 to
  rt_sigaction, which the kernel rejects) voided the first
  "handler never runs" evidence; the timer-delivery defect
  was re-measured after the fix and persisted (FD-213).
  Two further probe bugs (a pipe-fd array type, an itimerval
  field layout) were caught and are noted in the evidence.
- This lane introduced one regression and caught it in
  verification: the new ppoll sigmask check tested the
  sigsetsize argument instead of the mask argument, making
  every ppoll with sigsetsize 8 answer −EFAULT. Fixed
  before any commit; the pre-fix code never read either
  argument, so no shipped behaviour was affected. The final
  probe runs (E run 2) show ppoll sleeping and writing back
  correctly.
- The futex suite rows initially expected kernel-true
  blocking from a single-threaded harness process; the real
  implementation deliberately answers a spurious −EAGAIN
  there (glibc-tolerated; parking is the dispatch guard
  test's subject, and the guard's WAIT_BITSET row proves
  the parking path). Expectations corrected with the
  reasoning in the test comment.
- Prep: `winbox-wt-prep.sh` aborted on this tree — the
  shared store's wine64/wine32/prefix32 zips no longer match
  the tip's pins (a parallel lane cut a new generation), so
  the tree has no store links, no payloads and no build
  seed, and no browser build was possible in it. Native
  work was unaffected. Not this lane's place to re-cut the
  store; recorded here so composition knows the browser
  gate must run on the composed tree.
- One bare `submit-check` was launched before the branch had
  commits (it could only check the base tip); void for this
  lane. The real queue submission follows the commits.

## Proof

- Host grids: `test-results/timing-waits/hostref-timing.txt`,
  `hostref-timing2.txt`, `hostref-timing3.txt` (sources and
  binaries alongside).
- Guest probes pre-fix: `guestprobe-run1..5`,
  `guestprobeA-run1`, `guestprobeB-run1/2/4/5`,
  `timingprobe2..7` runs. Post-fix: `guestprobeC-run1`
  (clock/sleep/rusage/itimer/wait/poll rows),
  `guestprobeD-run2` (epoll rows incl. the DEL death),
  `guestprobeE-run2` (ppoll/select/pselect6 write-back),
  `guestprobeF-run1`.
- Native suites, gate-standard serial mode, on the fixed
  tree: main suite 693 tests, 0 FAILED
  (`suite-main-fixed.log`); x64 suite 117 tests, 0 FAILED
  (`suite-x64-fixed3.log`). Baselines at the tip: 692/116.
- Light checks through the checking queue: see the queue
  pipeline for `lane-timing-waits` (the two known
  bare-checkout js-unit failures — `provenance-artifacts`,
  `runtime-completeness` — are expected on an unbuilt tree,
  as in pipeline 14).
- Disk: this lane stripped its tree's `project/linux/Build`
  once mid-lane (333 MB freed at the strip) and again at
  closeout; the rootfs probe binaries were deleted; no
  store, staging or scratch was created.
