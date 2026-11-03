# FD-05 — file-descriptor table exhaustion returns EMFILE

Lane `lane-fd05` (worktree `/home/ubuntu/wt-fd05`, off integ
`cc1d89ab3`). Evidence: `test-results/fd05/`.

## The defect

`KProcess::getNextFileDescriptorHandle` ended in a `kpanic_fmt`
when its scan found no free slot at or below the
`MAX_NUMBER_OF_FILES` (0x4000) ceiling — the same ceiling the guest
is told is its `RLIMIT_NOFILE`. A guest that opened (or dup'd, or
otherwise created) 16,384 descriptors therefore killed the whole
emulator: `kpanic` exits the process. This was the register's one
known remaining guest-reachable emulator-kill in the
kernel-subsystems area.

## The audit — the filed estimate did not survive it

The register estimated that answering EMFILE "needs the allocation
contract changed across ~50 call sites whose callers mostly
dereference the result immediately". Measured against the tree:

- `getNextFileDescriptorHandle` has exactly **one** caller:
  `KProcess::allocFileDescriptor` (auto-allocation case only).
- `allocFileDescriptor` has **22 production call sites**. Two of
  them — `pidfd_open` (syscall64.cpp) and
  `createPidFdForProcess` (kpidfd.h) — already implemented the
  null-result contract (`result ? result->handle : -K_EMFILE`).
  They were unreachable only because the panic fired first; the
  intended contract existed and the panic contradicted it.
  `testInotify`'s call sites null-check the result the same way.
- The remaining 20 sites each dereferenced or ignored the result;
  each is individually small and is listed in the fix table below.
- Direct table installs exist in exactly two places:
  `allocFileDescriptor` itself, and the fork table copy, which
  re-installs the descriptor `allocFileDescriptor` just created at
  an explicit handle taken from the parent's snapshot (it cannot
  consult the ceiling and cannot fail).

So the contract changed at the single choke point instead of by a
50-site rewrite: the scan returns `MAX_NUMBER_OF_FILES` as its
"none available" sentinel, `allocFileDescriptor` turns that into a
null result, and every caller propagates the error its syscall
owes. Exhaustion also logs one line per process
(`BW64-FAIL fd-table-exhausted process=%s outcome=returned-EMFILE`)
— not silent, and a guest retry loop cannot flood the log.

## The fix, per call site

| Site | Answer on exhaustion |
| --- | --- |
| `openFileDescriptor` (open/openat/creat, both ABIs) | `-EMFILE` |
| `KProcess::dup` | `-EMFILE` |
| `memfd_create` | `-EMFILE` |
| `epollcreate`, `inotify_init1`, `timerfd_create` | `-EMFILE` |
| `eventfd2` (kevent.cpp), `signalfd4` (ksignal.cpp), `sys_signalfd64` | `-EMFILE` |
| `ksocket` (unix / netlink / inet) | `-EMFILE` |
| `fcntl` `F_DUPFD`, `F_DUPFD_CLOEXEC` | `-EMFILE` when the range is full; `-EINVAL` when the minimum is at/past the ceiling (the host's distinction) |
| `dup2` (and `dup3` through it) | `-EBADF` for a target at/past the ceiling — explicit targets can no longer grow the table past the advertised limit |
| `ksocketpairFds` (socketpair, and pipe/pipe2 which are built on it) | the failing call's error; atomicity preserved — the first descriptor is closed when the second cannot be allocated |
| unix `accept` | `-EMFILE`; the pending connection is requeued (push-front + signal), so the connector keeps waiting as on Linux |
| `recvmsg` SCM_RIGHTS | installs what fits, breaks; the rest are marked `MSG_CTRUNC` by the existing count check and dropped with the message — the close Linux performs for undelivered descriptors |
| native loopback `accept` | pre-check answers `-EMFILE` before dequeueing; on a lost race for the final slot the dequeued connection is requeued and signalled |
| native host `accept` | pre-check answers `-EMFILE` before `::accept` consumes a connection; on a lost race the consumed host descriptor is closed and `-EMFILE` returned |

`inotify_init1` also registers its instance only after the
descriptor exists, so a failed creation registers nothing (the
registration previously preceded allocation).

## Characterized residuals — none of them a crash

- The native accepts answer EMFILE on entry when the table is
  already full, rather than after a connection becomes pending
  (Linux defers the answer until it has a connection to give).
  The unix accept keeps its blocking shape: it waits for a
  connection first and only then reaches allocation. No
  guest-visible state differs; only the timing of the answer in
  the table-already-full case.
- Internal explicit-handle allocations (fork's table copy, stdio
  setup at handles 0–2) bypass the ceiling by construction and
  can only carry handles the process already legally holds; with
  dup2 bounded, no guest action can mint an over-ceiling handle
  for them to copy.
- A no-fork (in-process) x64 suite run still segfaults at the
  clone3 test, with or without this lane's test in the run
  (verified by re-running with the test skipped: same crash site).
  That is the FD-41 harness-contamination class the fork-per-test
  harness exists to isolate; the gate-standard forked runs are
  fully green. Not an FD-05 effect; no new entry filed, the class
  is already on record with FD-41.

## Proof

New test `testFdTableExhaustion` (`source/test/process/`,
registered in both suites) builds a real file in a temporary root,
drives the process to the ceiling through `dup`, then checks at
the full table: direct allocation returns null; epoll, timerfd,
inotify and open all answer `-EMFILE`; `F_DUPFD` answers `-EMFILE`
at minimum 0 and `-EINVAL` at the ceiling; `dup2` answers
`-EBADF` at the ceiling value and succeeds at the top legal slot;
freeing one slot admits exactly one more descriptor at exactly
that handle; and after draining, the table is fully usable. The
test tracks every handle it creates and closes exactly those, so
the shared suite context is left as found.

- RED (unfixed tree, logs `red-main.log`, `red-x64.log`,
  `red-x64-nofork.log`): the fill hit the panic —
  `KProcess::getNextFileDescriptorHandle  ran out of file handles`.
  In the forked harness the test's child died at that line and the
  test was each suite's single failure (main 668 OK + 1 FAILED,
  x64 69 OK + 1 FAILED). With fork isolation off, the suite
  process itself died at the test, panic line last, exit 1.
- GREEN (logs `green-main.log`, `green-x64.log`): native serial
  main **669/669** and x64 **70/70**, 0 FAILED (baselines 668 and
  69; +1 is the new test in each), the test OK in both, exactly
  one `BW64-FAIL fd-table-exhausted` line per run, suite times in
  line with baseline (252 s / 23 s).
- Browser build on the fixed tree (log `browser-build.log`):
  staged only through the clean bootstrap path (13 assets
  restored and verified against the retained archive), BUILD
  COMPLETE with "Browser export verified", wasm `ea3bdaa0…`,
  support archive unchanged (`support-c4dbe35986a5…`). The kernel
  change is shared with the browser build; it compiles and
  exports there.
- JS unit suite (log `js-unit2.log`): **348 passed, 0 failed**.
  The first run (log `js-unit.log`) failed three source-slice
  tests — `fs-file-create`, `browser-network`,
  `unix-connect-handoff` — with `'K_EMFILE' was not declared in
  this scope`: those tests extract the real
  `openFileDescriptor` / `ksocket` / unix `accept` source into
  mock harnesses whose errno tables predated the new returns.
  Adding `K_EMFILE=24` to the three mocks closed them; the tests
  then pass with the edited functions exercised inside their
  harnesses (concurrent O_CREAT still publishes one shared inode;
  the blocking connect retry still completes on accept). All
  three pass on the base tree and failed only on the missing
  constant — an A/B run against integ confirmed the attribution.
- JS legacy suite (log `js-legacy.log`): 110 pass / 4 fail, the
  standing FD-64 set on this base (that lane's fix is on its own
  branch).

## Files

- Fix: `include/kprocess.h`,
  `source/kernel/kprocess.cpp`, `source/kernel/kevent.cpp`,
  `source/kernel/ksignal.cpp`, `source/kernel/ksocket.cpp`,
  `source/kernel/kunixsocket.cpp`,
  `source/kernel/knativesocket.cpp`,
  `source/kernel/syscall64.cpp`; mock-contract updates in
  `web/tests/suite/unit/fs-file-create.test.mjs`,
  `web/tests/suite/unit/browser-network.test.mjs`,
  `web/tests/suite/unit/unix-connect-handoff.test.mjs`.
- Test: `source/test/process/testFdTableExhaustion.cpp`,
  registration in `source/test/testMain.cpp`.
- Register: FD-05 closed, CL-44 filed provisionally (the
  composing merge verifies and renumbers by audit; CL-40..CL-43
  are held provisionally by parallel branches).
