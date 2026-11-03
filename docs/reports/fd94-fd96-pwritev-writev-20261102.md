# FD-94 + FD-96 — pwritev/pwritev2 wiring on both dispatchers; writev partial-error return shape

Lane `lane-fd94-96` (worktree `wt-fd94-96`), off integration tip
`cc1d89ab3`, 2026-11-02. The two entries were filed on branch
`lane-fd92-93` (tip `1f92d1ecf`) and carried onto this branch
verbatim (commit `05f4380f5`) before any work. Neither is merged;
nothing here is pushed or deployed.

## What was wrong

- **FD-94.** The positioned vectored writes answered "not
  implemented" everywhere: on the 64-bit dispatcher pwritev (296)
  and pwritev2 (328) existed only as name-table strings — no
  `X64_SYS` define, no dispatch case, so both returned -ENOSYS;
  on the 32-bit side the syscall table slots 334 (pwritev) and
  378 (pwritev2) were null.
- **FD-96.** `KObject::writev` — the 32-bit vectored-write walk —
  returned a failing segment's error result even when earlier
  segments had already written bytes. The host returns the bytes.

## Host behaviour measured (box kernel 6.17.0-1020-oracle)

Reference programs and outputs in `test-results/fd94-96/`
(`hostref-pwritev.c`, `hostref-pwritev2b.c`, `hostref-pwritev2c.c`,
`hostref-off-order.c`; the filing lane's `hostref-write2.c` for
writev). The load-bearing answers:

- pwritev validation order: a negative offset is EINVAL ahead of
  the descriptor check (bad fd + offset -1 → EINVAL; bad fd +
  iovcnt -1 + offset 0 → EBADF; bad fd + iovcnt -1 + offset -1 →
  EINVAL). iovcnt outside [0, IOV_MAX] is EINVAL; a null iovec is
  EFAULT with iovcnt 1 and returns 0 with iovcnt 0; iovcnt 0 on a
  bad fd is still EBADF, and with offset -1 on a good fd is
  EINVAL (pwritev has no position mode).
- A good first segment (3 bytes) followed by a bad segment buffer
  returns 3; a bad first segment is EFAULT. A 3-segment pwritev
  raises exactly one IN_MODIFY on the host.
- pwritev2: offset -1 is the file-position mode — no EINVAL, the
  write lands at the current position and advances it (and the
  iovcnt/iovec validation still applies in that mode). Offset -2
  is EINVAL ahead of both the descriptor and flags checks. The
  descriptor check precedes the flags check (bad fd + bad flag →
  EBADF; good fd + flag 0x40 → EOPNOTSUPP). The defined RWF mask
  is 0x3f: HIPRI/DSYNC/SYNC/APPEND bits and 0x20 (NOAPPEND) are
  all accepted — wider than the read side's measured 0x1f.
  NOWAIT (0x08) answers EOPNOTSUPP on this host's /tmp
  filesystem; that is a per-filesystem capability answer, and the
  emulator accepts the flag, following the preadv2 precedent
  recorded under FD-80. RWF_APPEND ignores the offset and writes
  at end of file; with offset -1 it appends and the position
  follows to the new end of file; with an explicit offset the
  position does not move.
- writev: a good first segment (3 bytes) and a bad second returns
  3 (the filing lane's measurement).

## The fix (commit `d528d792b`)

- 64-bit: `sys_pwritev64` in `source/kernel/syscall64.cpp`
  dispatches 296/328 over the existing `sys_pwrite64` core, one
  segment at a time at successive offsets, in the measured
  validation order (offset → descriptor → flags → iovcnt →
  iovec). The position mode seeks to position+total after a
  successful walk; RWF_APPEND takes the file length as its base.
  Each segment's buffer is range-checked before its write (see
  FD-97 below). A later segment's failure returns the bytes
  already written; a short write ends the walk.
- 32-bit: `KProcess::pwritev` (declared in `include/kprocess.h`,
  defined in `source/kernel/kprocess.cpp`) walks
  `KProcess::pwrite64` per segment with the same order and shape;
  `syscall_pwritev`/`syscall_pwritev2` in
  `source/kernel/syscall.cpp` fill table slots 334/378, taking
  the offset as ARG4 | ARG5<<32 and flags from ARG6.
- FD-96: `KObject::writev` returns the accumulated count when a
  later segment fails after bytes were written, and the error
  only when nothing was written. Its sole caller is
  `KProcess::writev`, so the change is contained to the 32-bit
  writev (146); the short-write accumulate-and-continue behaviour
  is unchanged. The stale "TODO file pointer" warning is replaced
  by one stating the partial count is returned.

One deliberate divergence, inherited from the base: on a
read-only descriptor the positioned writes answer -EINVAL (the
`sys_pwrite64`/`KProcess::pwrite64` house answer) where the host
answers EBADF. That divergence is the write family's and is
FD-93's registered territory on the filing lane — exactly as
FD-92's test pins the read side's house answer — so this lane's
tests pin -EINVAL and do not move it.

## Tests (commits `c060e0613`, corrections `f9a8fdeef`, `84e5feca7`)

- `testPwritev64Syscalls` (x64 suite; registered in both test
  lists, compiled into the x64-list build): 3 segments at offset
  2 write in order without moving the position and raise exactly
  one watcher-visible IN_MODIFY; zero-length segments are
  skipped; a bad second segment returns the first segment's
  bytes; a bad first segment is -EFAULT; the full measured
  validation battery for 296 and 328; the position mode; both
  RWF_APPEND shapes; the read-only house answer; pos_h honoured
  (offset 2^32 lands past 4 GiB, verified by file length and a
  guest pread64 readback).
- `testPwritev32Dispatch` (main suite): the same shapes through
  the real int 0x80 dispatch for 334/378, including a hand-driven
  null-iovec case and the position/APPEND modes.
- `testWritevPartialError` (main suite): the writev walk over a
  KEvent whose counter admits one 8-byte segment and refuses the
  next — the partial return (8, counter advanced by the first
  segment only), the first-segment-fails boundary (-EAGAIN), and
  the all-good shape.

RED on the base, from the preserved logs
(`test-results/fd94-96/red-x64-pwritev.log`,
`red-main-pwritev32-writevpartial.log`): every pwritev/pwritev2
call answered -38 (-ENOSYS, with the dispatcher's own
"unimplemented syscall #296/#328" lines), and writev returned
-11 where 8 was owed. GREEN on the fix: the three tests pass;
full suites serial, 1 thread — main 670/670, x64 70/70.

Two test-authoring corrections are in the lane history and worth
recording: an added `<fcntl.h>` include broke the x64 build
(glibc's AT_FDCWD macro collides with an existing test's
constant), so the pos_h verification uses the descriptor's
length plus a guest pread64 readback instead of host file calls;
and pread64 takes its offset in one 64-bit register, unlike the
vectored calls' lo/hi split — the first readback attempt passed
the split form and read the file's zero-filled start, which the
test caught.

## Browser build and JS suite

Shared code changed (syscall64.cpp, syscall.cpp, kprocess.cpp,
kobject.cpp), so the lane gate includes the browser build and the
JS unit suite. The browser build prints 'Browser export verified'
and '[bw64-build] BUILD COMPLETE'
(`test-results/fd94-96/build-browser3.log`) — on its third run:
the first two stopped in dependency staging on generated payload
inputs this fresh tree did not have (tools/dxvk/build.w32 and
build.w64, then tools/rootfs64/gstreamer32 and gstreamer64),
copied from the integration tree, after which staging and
verification completed; the wasm compile itself (which includes
the changed kernel files) succeeded on the first run. JS unit:
348 passed, 0 failed. The first run failed no-silent-failure (the baseline bump below, since committed) and probe-slots (a timing failure under box contention; it passed on both re-runs, including the full green run).

The silent-failure scanner's per-file count for syscall64.cpp
rose 115 → 118 (unloggedErrno) with the new dispatch function.
Reviewed: the counted returns are sys_pwritev64's guest-validation
errno answers — the same class the FD-80 and FD-92 reviews
accepted for the preadv side — so the baseline in
web/tests/suite/known-silent.json is bumped to 118 with this lane
(commit `6e8984829`).
No other touched file appears in the scanner's table. Note for
the merge audit: lane-fd92-93 bumped the same cell 115 → 116 for
its readv work, so on the composed tree the scanner should read
119 and the baseline should settle there.

## Also filed here

- **FD-97** — the 64-bit single-call write paths fabricate zero
  bytes for an unreadable guest buffer instead of answering
  EFAULT: `KMemory64::memcpyFromGuest` zero-fills its destination
  when the guest range fails its access check, and `sys_write64`
  / `sys_pwrite64` copy through it before writing. FD-94's
  vectored paths range-check each segment and do not have the
  trait; the single-call paths keep it for the owning lane.

## Deliberately not touched

- The 32-bit preadv/preadv2 slots (333/377) are also null on this
  base; they are the read family's, not this filing's.
- FD-95 (the read family's bad-fd/negative-offset composition)
  stays with the merge lane, as its filing states.
- FD-93's wrong-mode answers live on the filing lane's branch,
  not in this base; this lane's pwritev inherits the base's house
  answers there, as recorded above.

## Evidence

`test-results/fd94-96/`: host reference programs and outputs,
RED logs for all three tests, GREEN logs, the full-suite logs,
and the build logs. Register: FD-94 and FD-96 closed in place
(CL-49, CL-50 provisional), FD-97 filed.
