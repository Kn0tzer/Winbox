# FD-41 / FD-42 — the native suite runs end-to-end again

Lane `lane-fd41-fd42`, worktree `wt-fd41fd42`, off integration tip
`7e98ccced`. Fix commit: `dc5f96fa8`. Evidence:
`test-results/fd41fd42/` in this worktree. Host: aarch64 (the armv8
host-exception handler is the one exercised; the x86-64 handler was
patched identically and is compile-unverified on this box).

## What was wrong

### FD-41 — one test could take the whole run down

Three containment holes, all reproduced on the pristine base
before any fix:

1. **An unwind with no handler.** A guest thread's platform thread
   fetches its first decoded instruction *before* the run loop, in
   a segment with no exception handler at all, on a bare
   `std::thread` entry point. The segment mapper's "this thread
   cannot start" unwind (`throw 1`, issued after the process is
   sent SIGKILL) escaped straight into `std::terminate`. The FD-41
   window (built tests 610–621: the syscall-ABI block, then the
   32-bit clone tests) reproduced it: 3 of 10 pre-fix runs aborted
   with `terminate called after throwing an instance of 'int'`,
   rc 134, every time on the same fault — a cloned child's first
   instruction fetch faulting at its shared spin page + 2
   (`0x62001002`), the exact hazard the clone test's own comments
   describe. The run loop's catch and the scheduler's slice catch
   both handle this unwind correctly; only the pre-loop fetch was
   exposed. A second, related hole: after the run loop's handler
   clears a faulting thread's next op, the test-mode completion
   check dereferenced it (debugger evidence: repeated host SIGSEGV
   in that check under `catch throw`).
2. **The harness itself had no containment.** The test runner
   invoked each test body with no try/catch anywhere: any exception
   out of any test body terminated the suite on the harness thread.
3. **Tests share one process with everything earlier tests left
   behind.** Several tests deliberately leak guest processes and
   threads (the clone tests' spinning child is one) whose host
   platform threads keep executing guest code for the rest of the
   process lifetime. In a shared process those leftovers act
   underneath later tests: a full pre-fix run died with a host
   segfault (rc 139) in the clone region after the leftover child
   had fault-looped 26,624 delivered faults; a core dump of the
   same window shows a platform thread dying inside guest-signal
   delivery (`KThread::runSignal` → `CPU::fillFlags`) on thread
   state being torn down concurrently; another full run passed the
   clone region and then *wedged* in the tail — main thread
   spinning in a later test's JIT while a leftover platform thread
   blocked in `KProcess::removeThread`. Every implicated test
   passes standalone and in small windows; the instability belongs
   to the shared process, and its signature varies run to run
   (abort at ~616, segfault, flood at ~650, hang in the tail,
   occasionally a clean 660/0 — matching the merge gate's
   experience).

### FD-42 — an unbounded diagnostic

The host exception handler's "could not find emulation instruction
that caused exception" line was a plain, uncapped log call in both
host handlers (armv8 and x86-64). Any thread that fault-loops on an
unmapped JIT address writes it forever — the gigabytes of log this
defect is registered for.

## The fix (commit `dc5f96fa8`)

- **Platform-thread containment**
  (`source/emulation/cpu/normal/normalPlatformMultiThreaded.cpp`):
  the first-op fetch is wrapped in a catch-all; a thread that
  cannot produce a first op retires through the normal
  end-of-thread accounting (test mode: rip-sampler release +
  process thread-table removal; product: the existing delete
  path) instead of unwinding out of the thread entry point. The
  test-mode loop also retires cleanly when the run loop's handler
  has cleared the next op, instead of dereferencing it. The
  existing loud panic for "no first op and nothing went wrong" is
  preserved.
- **Harness containment** (`source/test/cpu/testCPU.cpp`): a throw
  out of a test body is caught per test — `int`, `std::exception`,
  or other — recorded as that test's failure with the diagnostic
  attached, and the suite continues with a correct final count.
- **Fork-per-test isolation for serial runs** (same file, Linux
  native test build only): each test runs in a forked child of the
  suite process — same order, same code, same assertions, the
  pre-created test context inherited copy-on-write. A child that
  crashes is recorded as that one test FAILED with the killing
  signal named; a child that cannot be forked fails its test
  loudly rather than skipping it. `BW64_TEST_NO_FORK=1` restores
  the in-process path for bisection. Rationale: every test is
  known to pass in the standalone environment; isolation gives
  each test exactly that environment and removes the cross-test
  interference class wholesale, without weakening, skipping, or
  reordering anything. The fork also retires each test's leaked
  platform threads with its address space — they cannot outlive
  the test that created them.
- **Bounded diagnostics** (`platform/linux/platformThreads-armv8.cpp`,
  `platform/linux/platformThreads-x64.cpp`): the unmapped-JIT line
  now follows the project's counting-cap standard — a `BW64-FAIL`
  line, the first 64 occurrences in full, then every 1024th,
  always carrying the cumulative count. First occurrence is always
  reported; repetitions are counted, never silently dropped and
  never unbounded.

## Proof

All runs guarded with output-file limits so no run could fill the
disk. Raw logs: `test-results/fd41fd42/`.

- Pre-fix repro: FD-41 window aborted 3/10 (rc 134,
  `terminate called`); pre-fix full run died rc 139 in the clone
  region after 615 passes with the leftover child fault-looping.
- FD-41 window post-fix: **20/20 completions**, final count every
  time, no `terminate called`. The exact child first-fetch fault
  fires in 9 of the 20 logs and is contained each time — the fix
  is exercised on the live defect path, not merely lucky timing.
- Sabotage check: a test temporarily made to throw out of its body
  is reported **FAILED** with "test threw an uncaught int exception
  out of its body (code 7)", the following tests run, and the
  final count reads "1 tests FAILED", rc 1. (Sabotage reverted;
  clean rebuild after.)
- **Full suite, three consecutive serial runs: 661/661 tests
  executed, 0 FAILED, exit code 0, ~4 minutes each, ~25 KB logs**
  (`fullfork-1/2/3.log`). The capped FD-42 line did not fire at
  all under isolation (no fault-looping leftovers exist); where it
  fired pre-isolation it carried the count (see `full-fix2.log`).
- The 62-test long window spanning the clone group and the tail
  (which segfaulted and wedged in-process): 2/2 completions,
  61 result lines for its 61 tests (indices 600–660), 0 failed.
- Fallback paths verified: `BW64_TEST_NO_FORK=1` window completes;
  the parallel path (2 workers) completes its window with the new
  harness catch in place.
- Every test still runs the same body with the same assertions in
  the same order; verdict accounting verified to flow through the
  per-test context the children inherit (all suite failure
  reporting routes through it).

## Found but not fixed here — registered as FD-43

The fork contains the damage, but the underlying product-side
defect it exposed is real and remains: guest platform threads can
outlive and execute on thread/CPU state that is being freed or
torn down by another thread (core-dump evidence:
`KThread::runSignal` → `CPU::fillFlags` host segfault; the
removeThread wedge behind leaked threads spinning in JIT code).
In the product there is no fork boundary; a long-running guest
with aggressively cloned/exited threads exercises the same
lifetime hazards. Registered in Section A as FD-43 with the
evidence pointers. (Note for the merge lane: the FD-40 fix lane
independently used the number FD-43 on its branch for a different
entry; one of the two needs renumbering when the branches
compose.)

## Not done / limits

- The x86-64 host handler edit is byte-mirrored from the armv8 one
  but not compiled on this aarch64 box.
- The 4-worker parallel suite mode had pre-existing flood/hang
  behaviour on the base before this lane (documented at the merge
  gate); the serial mode is the gate and is what this lane fixed.
  The harness catch benefits both modes.
