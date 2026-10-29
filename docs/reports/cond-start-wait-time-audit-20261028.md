# condStartWaitTime audit — the shared poll/sleep deadline anchor (2026-10-28)

Lane: lane-condwait, base `8bf268112`. Question settled: is
`KThread::condStartWaitTime` — the one deadline anchor shared by
`internal_poll_impl` and `KThread::sleep`, and not reset on the
`-K_CONTINUE` exit — a live busy-spin / wrong-wait defect?

**Verdict, in one line:** the missing reset on `-K_CONTINUE` is
BENIGN-BY-DESIGN (it is what makes signal-restarted waits keep their
original deadline, and every terminal exit resets the anchor, so a
leaked anchor can shorten at most one later wait — no persistent spin
is possible). The audit did convict one LIVE defect in the same
lifecycle: `KThread::sleep` double-counted elapsed time after
non-terminal wakes and returned progressively early. That defect is
fixed here, with a regression guard.

## 1. The field and its lifecycle (pinned from the code)

`U32 condStartWaitTime` (`include/kthread.h:223`, public, per-KThread,
initialised 0; 0 means "no wait in progress"). It stores the
`KSystem::getMilliesSinceStart()` timestamp of the current wait's
first pass. Only two functions use it as a deadline anchor:

- `internal_poll_impl` (`source/kernel/kpoll.cpp:37`), behind
  `internal_poll` (poll/ppoll/select) and `internal_epoll_poll`
  (epoll_wait via `KEPoll::wait`).
- `KThread::sleep` (`source/kernel/kthread.cpp:2158`), behind
  `KThread::nanoSleep` (nanosleep / clock_nanosleep, both widths) and
  `kselect`'s no-fd delegation (`kpoll.cpp:270`).

Plus two mechanical uses: the 32-bit signal frame saves/restores it
(`KThread::runSignal` pushes it, `onExitSignal` pops it back —
`kthread.cpp:1904/1916` and `1804`, with the design stated in the
comment at `kthread.cpp:1841`: "interrupted and condStartWaitTime are
pushed because syscall's during the signal will clobber them"), and a
debug dump reads it (`kprocess.cpp:4275`).

### Exit-by-exit reset table

`internal_poll_impl` (all exits; line numbers at base):

| Exit | Line | Resets? |
|---|---|---|
| ready (`result > 0`) | 128–131 | yes |
| `timeout == 0` | 157–161 | yes |
| interrupted flag → `-K_EINTR` | 162–166 | yes |
| deadline reached (`elapsed >= timeout`) | 171–176 | yes |
| cross-thread tgkill interrupt → `-K_EINTR` | 210–213 | yes |
| epoll-restart, loop top → `return 0` + `restartRequested` | 52–57 | **no — by design** (comment at 49–51: "without resetting the wait deadline so it can poll the new state") |
| epoll-restart, after wait → `return 0` + `restartRequested` | 193–198 | **no — by design** (same) |
| `terminating` → `-K_EINTR` | 200–202 | no (thread is being destroyed; the field dies with it) |
| `startSignal` → `-K_CONTINUE` | 203–206 | **no — by design** (§2) |

`KThread::sleep`:

| Exit | Line | Resets? |
|---|---|---|
| short sleep (`ms <= NUMBER_OF_MILLIES_TO_SPIN_FOR_WAIT`) → host nanosleep | 2159–2161 | field untouched (never read on this path) |
| deadline reached | 2166–2170 | yes |
| `terminating` → `-K_EINTR` | 2179–2181 | no (thread is being destroyed) |
| `startSignal` → `-K_CONTINUE` | 2182–2185 | **no — by design** (§2) |
| single-threaded builds: the wait itself returns `-K_WAIT` out of the function (`BOXEDWINE_CONDITION_WAIT_TIMEOUT` is a `return` in that configuration, `source/util/synchronization.h:149`) | 2176 | no — the anchor is the ONLY record of the deadline across the scheduler re-entry, exactly like `-K_CONTINUE` |

`KEPoll::wait` (`kepoll.cpp:275`) loops internally on both
epoll-restart returns and never exposes them to the guest; its loop
exits only through the terminal/`-K_CONTINUE`/`-K_EINTR` returns
above.

## 2. Why the `-K_CONTINUE` non-reset is correct (the headline suspect)

`-K_CONTINUE` (998) means "a signal handler must run before this
syscall completes". Both dispatchers honour the same contract
(`syscall.cpp:2613`, `syscall64.cpp:6553`): the sentinel is NOT
written to the guest result register and the guest instruction
pointer is NOT advanced, so after the handler returns (through
`onExitSignal`, which restores the saved anchor and the saved
`interrupted` flag) the very same syscall instruction re-executes
with its original arguments.

For that re-execution to be a Linux-correct restart, the wait must
resume against its ORIGINAL deadline:

- With `SA_RESTART`, the re-entered wait continues the same timeout;
  total elapsed time includes the handler's duration because the
  anchor predates it.
- Without `SA_RESTART`, `runSignal` saved `interrupted = 1` for the
  blocked wait; the re-entered poll observes it at its loop top and
  returns `-K_EINTR` — resetting the anchor on that exit.
- A nested wait inside the handler clobbers the anchor freely; the
  frame restore overwrites the clobber with the outer wait's anchor
  (nested signals nest the frames LIFO, so the pairing is exact).

Resetting the anchor on the `-K_CONTINUE` exit would therefore be
the bug: every delivered signal would restart the full timeout, and
a guest receiving periodic signals (Wine's SIGUSR1 thread-suspension
traffic is the standing example) could starve a wait indefinitely —
the actual busy-spin shape in this design space. The same argument
covers the single-threaded `-K_WAIT` re-entry and the epoll-restart
returns: in all three, the anchor is the deadline's only surviving
record, deliberately.

The 64-bit guest path interrupts blocked waits primarily with
`-K_EINTR` semantics instead (`interruptBlockedSyscall`, poll exit at
`kpoll.cpp:210`, which resets); where a 64-bit thread does take the
`startSignal`/`-K_CONTINUE` exit, the same dispatcher contract
applies.

## 3. The abandonment residue — bounded at one shortened wait

The anchor can outlive its wait if the restart is abandoned: a
signal handler that never returns through sigreturn (longjmp out of
the handler, `execve` from the handler — the `KThread` object and its
anchor survive an exec — or thread exit from the handler, which is
harmless). The next wait on that thread, of either kind, then
computes its elapsed time against the stale start and can return
early — in the worst case immediately.

It cannot spin: every terminal exit of both waits resets the anchor
(§1 table), so the first wait that observes the stale value consumes
and clears it. The worst case is exactly ONE shortened (possibly
zero-length) wait per abandonment, after which waits are exact
again. Native scenario 4 of the guard test pins this bound with a
10-second-stale anchor: the first `sleep(300)` returns at once, the
second sleeps its full 300 ms. Accepted as residue: the abandonment
paths already leave larger signal state unrestored (`inSignal`,
`sigMask`), and no guest has been observed to hit this.

## 4. The LIVE defect: `KThread::sleep` double-counted elapsed time

Pre-fix `KThread::sleep` (base `kthread.cpp:2162–2177`):

```cpp
while (true) {
    if (!this->condStartWaitTime) {
        this->condStartWaitTime = KSystem::getMilliesSinceStart();
    } else {
        U32 diff = KSystem::getMilliesSinceStart()-this->condStartWaitTime;
        if (diff>ms) { this->condStartWaitTime = 0; return 0; }
        ms-=diff;                       // <-- mutates the timeout basis
    }
    BOXEDWINE_CONDITION_WAIT_TIMEOUT(this->sleepCond, ms);
    ...
}
```

`diff` is the TOTAL elapsed time since the anchor, but after the
first wake it is subtracted from an ALREADY-REDUCED `ms`. Each
further in-invocation wake compounds the error: with a wake every
100 ms, a 2000 ms sleep returns after ~600 ms (traced in the guard
test, scenario 1). In the single-threaded configuration this could
not bite (each scheduler re-entry restarts the function with the
guest's original `ms`); in multi-threaded builds — the shipping
configuration — the loop iterates in one invocation.

Non-terminal wakes of `sleepCond` are real: `sleepCond` is never
signalled by name anywhere, but while a thread is parked on it,
`thread->waitingCond == sleepCond`, and the generic cross-thread
kicks signal exactly that condition without setting `startSignal` —
the tgkill delivery kick (`syscall64.cpp:5035`, "kick the condition
it sleeps on; its wait loop sees pendingSignals…": poll's loop does
check, sleep's loop has no pending-signal check and simply re-waits),
the wait-branch of `KThread::signal` (`kthread.cpp:246–250`), and
plain `std::condition_variable` spurious wakes. A thread that is the
target of repeated cross-thread signals while in a long
`nanosleep` (Wine's `NtDelayExecution` is the standing sleeper) had
its sleeps cut progressively short.

`internal_poll_impl` was already fixed for exactly this pattern by
`fc6818ec1` ("Preserve poll deadlines across wakeups and consecutive
timed waits"), whose code comment states the rule: "Keep the
original deadline. Subtracting total elapsed time from an
already-reduced timeout returns early after spurious wakes."
(`kpoll.cpp:180–181`.) That pass touched only `kpoll.cpp`;
`KThread::sleep` kept the defective form. This lane closes it.

## 5. The fix

`source/kernel/kthread.cpp`, `KThread::sleep` only: never mutate
`ms`. Each pass recomputes `diff` from the anchor and waits
`ms - diff`; the terminal test is `diff >= ms`, matching
`internal_poll_impl`'s `elapsed >= timeout` (the old `diff > ms`
form, combined with the mutated basis, additionally waited a
zero-length pass at the exact boundary). Behaviour is unchanged for
the single-wake and re-entry cases; the `-K_CONTINUE` and
`terminating` exits are deliberately untouched (§2).

## 6. Proof — native guard `testCondStartWaitTime`

`source/test/process/testCondWait.cpp`, registered in both
`TEST_ENTRIES` arrays (`source/test/testMain.cpp`) as "Test
condStartWaitTime deadline accounting (sleep/poll)". It drives the
real `KThread::sleep` / `internal_poll` on the test context's thread,
parking it on its real conditions and waking it with the same
`waitingCond` kick the tgkill path uses:

1. **Sleep deadline (regression guard):** `sleep(2000)` woken every
   ~100 ms ×8 with non-terminal kicks must still take ≥1400 ms
   (expected ~2000 ms). Pre-fix it returns at ~600 ms → test FAILS;
   post-fix PASS. Sabotage-verified by reverting the fix hunk.
2. **Poll deadline (pin):** the same treatment of a 0-fd
   `internal_poll(2000)` — passes pre- and post-fix; pins the pattern
   sleep was aligned to.
3. **`-K_CONTINUE` pin:** a signal-interrupted poll returns
   `-K_CONTINUE` with the anchor preserved (age-checked), and a
   re-entered wait whose timeout the preserved deadline has already
   covered returns immediately and resets the anchor.
4. **Leaked-anchor bound (characterization):** with the anchor
   pre-set 10 s in the past, the first `sleep(300)` returns at once
   and consumes the anchor; the second sleeps the full 300 ms.

Results (native suite, `Build/Test/boxedwine 0 0 1`, single worker,
multi-threaded build — the shipping configuration):

- Pre-fix (test added, product code untouched): the guard FAILS on
  scenario 1 only — `sleep(2000)` returned after **608 ms** under the
  kick train. Scenarios 2–4 pass on the unmodified code, empirically
  confirming the poll deadline pattern, the `-K_CONTINUE` anchor
  preservation (§2), and the one-wait leak bound (§3).
- Post-fix: the guard passes; the full suite reports **648 tests,
  0 FAILED** (baseline 647 + this guard).
- Sabotage loop: reverting only the `kthread.cpp` hunk returns the
  guard to FAILED (**606 ms**, same signature); re-applying the fix
  restores PASS. The final full-suite run above is on the fixed tree.
  (A first full-suite attempt was cut off externally one verdict
  short of the end — 647 verdicts printed, 0 failures, the missing
  UD2-with-SIG_DFL verdict passes standalone — and was re-run
  detached to completion for the numbers above.)

## 7. What was NOT changed, deliberately

- No reset added on any `-K_CONTINUE`, epoll-restart, or `-K_WAIT`
  path (§2 — adding one would restart timeouts after signals).
- The `terminating` → `-K_EINTR` exits still do not reset: the
  thread is being destroyed and the field dies with it.
- The shared-field design itself is kept: with the save/restore
  discipline of §2 it is sound, and splitting the field would churn
  the signal-frame layout to defend only the §3 abandonment residue,
  which is already bounded at one wait.
- `KThread::sleep` still has no pending-signal check in its loop
  (unlike poll's `interruptBlockedSyscall`): a kicked sleeping
  thread re-waits for its remaining deadline and the signal is
  delivered at the next slice. That is a delivery-latency property,
  not a deadline defect; after this fix the re-wait is at least for
  the correct remaining time. Recorded here so it is a known,
  chosen behaviour rather than an oversight.
