# FD-38 enablement park — mechanism found and removed (2026-10-31)

Lane: `lane-fd38park`, worktree `/home/ubuntu/wt-fd38park`, off integ tip
`fe4cfc265`. Evidence: `test-results/fd38park/`.

## End state reached

`KProcess::inotify_init1` succeeds in the shipped emulator code — the
ENOSYS gate is gone — with the event semantics from the sibling FD-38
lane intact, and the Chromium consumer starts and presents through the
real website path with delivery live, repeatedly (site rig, below).
The sibling semantics commit is carried as cherry-pick `04b32c1ac`
(duplicate of `lane-fd38`'s `0ddb6f31a` — the merge lane drops one at
composition, the FD-02 precedent). The park fix is commit
`1e8151d2a`.

## The mechanism (proven, not inferred)

An ABBA lock-order deadlock between the poll scan and the inotify
notify sweep. It can only exist once an inotify instance exists, which
is why the refusal made it unreachable and why every non-inotify part
of the system was unaffected.

- A thread blocked in `poll`/`select` scans its watch set while
  holding its poll condition's mutex (`internal_poll_impl` in
  kpoll.cpp holds `thread->pollCond` for the whole scan). For each
  watched object it calls `waitForEvents(...)` and then
  `isReadReady()`.
- `KInotify::isReadReady()` took the instance's own wait-condition
  mutex. Lock edge: **pollCond → waitCond**.
- File activity in any process funnels into `KInotify::sweepAll` →
  `sweep`, which holds the instance wait condition while queueing and
  then calls `BoxedWineCondition::signalAll(waitCond)` *inside* the
  lock. `signalAll` locks each registered parent condition's mutex
  before signaling it. Lock edge: **waitCond → pollCond**.
- When a polling thread (the guest wineserver, which polls its
  inotify fd) meets a sweeping thread (any guest process opening,
  closing, writing or removing a file), each holds one mutex and
  blocks on the other. Both host threads park in futex waits at ~0
  CPU; the sweeping thread keeps holding the instance lock, so every
  later sweep in every process queues behind it and guest file
  activity freezes session-wide. The guest log simply stops — the
  exact signature the FD-38 bisect isolated (log frozen, CPU ≈ 0,
  nothing lost on the wire: the two queued rm_watch events in the
  traced run were read back correctly before the freeze).

Proof, all three layers:

1. **Bisect (FD-38 lane, inherited)**: enablement alone on pristine
   machinery parks (B1); enablement + forced non-blocking reads still
   parks (B2); refusal intact passes. The read path and the event
   stream were never the problem.
2. **Minimal reproduction (this lane)**: `testInotifyPollNotifyStress`
   composes the production pieces exactly — one thread runs the real
   poll-scan lock sequence against a registered instance (registering
   its poll condition via `KInotify::waitForEvents`), the other calls
   the production funnels `KInotify::notifyOpened/notifyClosed`. On
   the pre-fix tree it wedges and never returns.
3. **The wedged process, captured** (`stress-red2-gdb.txt`): one
   worker blocked in `pthread_mutex_lock` from
   `KInotify::isReadReady()`; the other blocked in
   `pthread_mutex_lock` from `BoxedWineCondition::signalAll()` ←
   `KInotify::sweep` ← `KInotify::sweepAll` ← `KInotify::fileEvent`;
   the main test thread parked in the join. Two different futex words,
   each thread holding one and waiting for the other.

Why nothing else hits this: the only lock-free-readiness outlier was
inotify. The unix socket object's `isReadReady` is deliberately
lock-free (its critical-section line is commented out in
kunixsocket.cpp) — sockets are polled constantly in the shipped
product and never gave a poller a lock edge. The blocking-read wake
path (`removeWatch`, the condition `wait()` parent's re-signal)
shares the sweep's waitCond → pollCond edge, but with the scan no
longer acquiring waitCond, no cycle can close through any of them.

## The fix

At the readiness probe — the one place that closed the cycle:

- `KInotify::isReadReady()` is now lock-free: it returns an atomic
  mirror of the queue state (`pendingNonEmpty`), the same pattern the
  socket object uses. The mirror is maintained under the wait
  condition at every queue mutation: set in both `queueLocked` push
  paths (normal event and overflow record), cleared in `readNative`
  when a drain empties the queue. Coalescing and full-queue early
  returns never change emptiness, so the mirror cannot drift from
  `!pendingEvents.empty()`.
- `KProcess::inotify_init1`: the early `return -K_ENOSYS` is deleted;
  the implementation that already existed below it (flag validation →
  EINVAL, instance construction, registration, fd allocation) is now
  live. Its comment states the new truth and points at the stress pin.
- The three pins that asserted the refusal now assert the enabled
  contract: x64 syscall test (EINVAL for invalid flags, a real
  descriptor for valid ones), the KProcess-API test (init1 returns a
  descriptor that takes a watch through `inotify_add_watch`), and the
  x64 dispatch guard (same contract through the full syscall layer,
  for both `inotify_init1` and `inotify_init`).

No test was weakened or deleted: every refusal assertion was replaced
by a strictly stronger enabled-behavior assertion, and the stress pin
was added.

## Proof

- Stress pin: pre-fix run wedges (gdb stacks above); post-fix it
  completes — 2 × 200,000 iterations of scan + notify composition —
  in well under a second (`stress-green.log`).
- Native suites, this branch: main flavour **663/663** (662 base + the
  new stress test), x64 flavour **62/62**, both full serial
  (`native-full-main.log`, `native-full-x64.log`).
- JS suite: **344 passed / 0 failed** (`js-suite2.log`).
- Dist: bw64-build.sh completed with its built-in verification
  ("Browser export verified"); completeness **0 failures, 1 standing skip (post-build bootstrap --check; the build verifies the export itself as its final step)**
  (`dist-build2.log`; the first attempt, `dist-build.log`, stopped at the runtime-asset bootstrap before this tree payload zips were staged from the sibling trees).
- **Site rig (the consumer proof)**: the FD-38 lane's driver, fresh
  profile per run, serving this lane's dist: **3/3 fresh-profile runs window-mapped+frame (57-66 s each), against the bisect baseline where every enablement form parked**
  (`site-rig-*/`). With delivery live, the Chromium session starts and
  presents repeatedly — the exact runs that parked in B1/B2 form on
  the pre-fix machinery.

## For the merge lane

- Branch composition: integ `fe4cfc265` + `04b32c1ac` (cherry-pick of
  `lane-fd38`'s `0ddb6f31a`; drop at composition when `lane-fd38`
  itself merges — the contained pin files were resolved `--ours`, so
  nothing else overlaps) + `1e8151d2a` (the park fix + flipped
  pins + stress test) + `the register/record commit carrying this file` (register + this
  record).
- FD-38 is closed by this lane: closure row **CL-18** (CL-17 is
  reserved for FD-01's closure row, which the deploy record assigns
  to the next merge batch; renumber at composition if that changes).
- Touch map vs the semantics work: this lane changes
  `source/kernel/kinotify.cpp` (readiness probe + queue-mirror
  stores — the semantics commit touched the sweep/queue logic in the
  same file, sequentially, no overlap in behavior),
  `include/kinotify.h` (one member), `source/kernel/kprocess.cpp`
  (the gate), and test files. The semantics commit's behavior is
  untouched and its rig stays green (the full suites above include
  it).
