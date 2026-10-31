# FD-44 — guest platform threads outliving the thread state they execute on: fixed

Lane `lane-fd44`, worktree `wt-fd44`, off integ `f3fd57d58`.
Evidence: `test-results/fd44/` in this worktree.

Commits:

- `76407729e` — the regression test (red proof captured against this
  exact phase-1 code; see below).
- `d52d839ac` — the fix.
- `84d430548`, `03f259dc1` — test-only robustness fixes for phase 2
  (sanitizer-speed scheduling; phase 2 now parks victims on a real
  condition wait).

## What was wrong

`KThread` lifetime was raw-pointer based. `KProcess::createThread`
puts a bare `KThread*` in the process thread table; the only product
free is `delete thread` inside `KProcess::deleteThread`. Other host
threads fetch the pointer from the table and use it with no lifetime
guarantee:

- `KProcess::iterateThreads` (used by `KProcess::signalProcess`)
  snapshots thread ids, then passes each raw pointer to a callback
  that runs the target's signal machinery (`runSignals`) — while the
  target's own platform thread can concurrently retire it.
- `KSystem::tgkill` fetches a raw pointer and calls
  `KThread::signal()`, which holds the target's conditions, can run
  `runSignal` on the target cross-thread, and with `wait=true` waits
  up to 1000 ms on the target's handler-end condition.
- The same escape shape at MT `terminateOtherThread`, syscall64
  `tgkill` (cross-thread block), `rt_tgsigqueueinfo`,
  `pidfd_send_signal`, and the `bw64_dump_guest_threads` diagnostic
  dump (`tryGetThreadById`).

This is the mechanism behind the FD-41 lane's captured core: a
platform thread dying inside `KThread::runSignal` ->
`CPU::fillFlags` on concurrently torn-down state.

## Proof the defect is real (red)

New suite test `testThreadLifetimeTeardown` (registered in both
`TEST_ENTRIES` arrays; index 605 in the 32-bit array). Phase 1 races
`signalProcess` + `KSystem::tgkill` deliveries against victims
retiring through the product teardown sequence
(`terminateCurrentThread` + `deleteThread`), with the main thread
recycling thread-state allocations; victims complete delivered
signals the way sigreturn does (`onExitSignal` bookkeeping) so
delivery keeps taking the frame-building path. Phase 2 is the
`terminateOtherThread` control.

In a plain build the use-after-free touches land in intact freed
memory and stay benign under glibc (20+ plain runs passed pre-fix,
including with `MALLOC_PERTURB_`/`MALLOC_CHECK_`); the detector is an
AddressSanitizer-instrumented build of the lifetime translation
units (kthread/kprocess/ksystem/syscall/MT-platform/cpu + the test).
Pre-fix it fails immediately (`asan-prefix-run1.log`):

- read of size 8 in `KThread::signal`, `kthread.cpp:205` (the
  target's `sigWaitCond` member), from the tgkill sender thread;
- the 1760-byte `KThread` was freed by `KProcess::deleteThread`,
  `kprocess.cpp:410` (`delete thread`), on the victim's platform
  thread.

## The fix (`d52d839ac`)

`KThread` carries an atomic reference count starting at 1 — the
table/creator's reference. `deleteThread` releases that reference
instead of deleting; the object (and the CPU it owns) is destroyed
when the last reference drains. Deferred destruction is safe because
`deleteThread` always runs `cleanup()` first, whose first pass
disarms the memory-touching teardown (clear_child_tid, robust list),
so the destructor's second pass touches only the thread's own
objects.

Every table escape that dereferences the thread beyond the lookup
now takes the reference under the table lock:

- New `KProcess::getThreadByIdRef` / `getThreadRef` /
  `tryGetThreadByIdRef` and `KSystem::getThreadByIdRef` /
  `tryGetThreadByIdRef`.
- `iterateThreads` holds the reference across its callback;
  `signalProcess` additionally skips threads already in teardown
  (the rule the tgkill path already applied; `terminating` is
  sticky, never cleared).
- `KSystem::tgkill`, MT `terminateOtherThread`, syscall64 `tgkill`,
  `rt_tgsigqueueinfo`, `pidfd_send_signal`, and the thread dump hold
  references across their uses (the dump uses the try-lock variant,
  preserving its never-blocks design).

Direct `delete` remains only where the reference was never shared:
the `currentCpu` `tmpThread` in `cpu.cpp` and the tests'
recycled-threads harness.

## Proof the fix works (green)

- ASan build of the same units, post-fix: the test passes with zero
  AddressSanitizer reports (`asan-postfix-run3.log`); the earlier
  post-fix ASan runs 1-2 also showed zero reports (their only
  failures were the phase-2 test-harness issue fixed by
  `03f259dc1`).
- Plain build: the new test passes repeatedly
  (`postfix-run*.log`, `postfix2-run*.log`).
- Full native suite, serial: 662/662, 0 FAILED, full completion in
  357 s (`fullsuite-postfix.log`).
- x64-test suite: 59/59, 0 FAILED (`x64suite-postfix.log`).
- Phase 1 of the test is byte-identical between the red-proof commit
  and HEAD (the later test commits touch only phase 2), so the
  red/green pair measures the same race.

## Found on the way (registered, not fixed here)

- **FD-47** — the RIP sampler's background thread dereferences raw
  published `CPU`/`CPU64` pointers (registers, `thread->process`,
  guest memory) with no lifetime hold. Diagnostic mode only
  (`BW64_RIPSAMPLE`), same defect class.
- The FD-44 entry's wedge (a leaked thread blocked in
  `KProcess::removeThread` while the main thread spun in JIT code)
  was not reproduced anywhere in this lane's runs; it is consistent
  with a downstream symptom of executing on freed state (a platform
  thread can wedge anywhere, including a teardown condition wait).
  If it recurs on a tree carrying this fix, it is an independent
  defect.

## Residuals and limits

- **FD-48** — cross-process delivery does not pin the target's `KProcess`: a
  delivery holds the `KThread` (and so its CPU), but a frame build
  also touches the process's shared `KMemory`, whose lifetime is the
  process's. No failure was observed from this window (deliveries
  drain quickly and product process teardown waits out thread
  removal — an ASan build with the memory unit instrumented ran the
  teardown race clean twice, `asan-kmem-run1/2.log`), and pinning
  process lifetime through every delivery path is a larger change;
  registered as FD-48 so the next lane starts from the fact.
- With deferred destruction, `~KThread` (and its exit callbacks) can
  run on the last releasing host thread rather than the thread's own
  platform thread. No ordering assumption on the platform thread was
  found in the destructor's work (it deletes the CPU and the thread's
  own objects after cleanup disarmed shared state).
- x86-64 host signal/handler paths inside the changed files are
  compile-unverified on this aarch64 box, as with FD-41/FD-42 (the
  syscall64 changes were reviewed against the 32-bit mirror paths;
  the x64-test suite builds the guest-x64 flavour natively here).
- No dist/JS build on this lane: the change is native kernel code
  only; the batch merge gate should run the JS/dist gate as it did
  for FD-23.
