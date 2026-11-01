# FD-23 — Undispatched-syscall census, disposition pass, and the three implementations

**Lane:** `lane-fd23-syscalls` (worktree `wt-fd23`), off integration
tip `f3fd57d58`. **Date:** 2026-10-31. Nothing pushed, nothing
deployed; the live site and master were not touched.

## Summary

The register's FD-23 said 168 of 363 named x86-64 syscalls had no
dispatch case, 223 i386 table slots were null, and 22 i386 numbers
fell off the end of the table — counted, but never dispositioned, and
never compared against what guests actually ask for. This lane did
both halves:

1. **The census tool now classifies three ways** (implemented /
   deliberately stubbed with a logged `-ENOSYS` / absent), on both
   ABIs, so "has a case" can no longer hide a stub.
2. **The dynamic census** — mining the emulator's own
   once-per-number "unimplemented syscall" log lines across the
   preserved run logs on the box — shows what guest software
   actually issues. One call dominates everything else combined:
   `rseq`.
3. **Three genuinely-needed calls are implemented and proven**:
   `epoll_pwait` (281), `accept4` (288), and `dup3` (292) on x86-64.
   Implementing `accept4` exposed a separate silent-wrong-answer
   defect (registered as FD-46): the existing 32-bit `accept4` had
   been silently dropping `SOCK_CLOEXEC` since it was written,
   because the flag was tested in the wrong encoding. The fix is
   centralised in `kaccept`, so both ABIs are corrected by it.
4. The rest of the measured set is dispositioned below with
   reasons. Everything not implemented refuses with a guest-visible
   `-ENOSYS` and a host-side log line — the fallback path the
   dispatch-guard test pins — never a silent success.

## Method

**Static.** `tools/conformance/syscall_census.py` (upgraded on this
branch) extracts, mechanically: for x86-64, the in-file v6.12 name
table vs the dispatch switch in `source/kernel/syscall64.cpp`,
classifying a case whose whole body is the central
`unimplementedSyscall64()` call as *stubbed*, and a case that reaches
it only on a fallback path as *conditional*; for i386, the
`unistd_32.h` list vs the positional `syscallFunc[]` table in
`source/kernel/syscall.cpp`, classifying a named handler whose whole
body is `return -K_ENOSYS;` as *stubbed*. Re-run cost: seconds.
Command:

```
python3 tools/conformance/syscall_census.py --source-dir . \
  --unistd32 /usr/x86_64-linux-gnu/include/asm/unistd_32.h \
  --out test-results/fd23-census
```

**Dynamic.** On x86-64, an undispatched (or stubbed) number logs
exactly once per emulator process:
`ksyscall64: unimplemented syscall #N (name)`. Grepping that line
across the preserved test-result logs on the box therefore counts
*processes that asked*, per number — a lower bound on real demand,
from real workloads (Wine boots, demo sweeps, gate runs), not from
a table. The i386 side has no equivalent always-on log (its
fallback logs at debug level only); the dynamic census is x86-64
only, which is also where the measured workloads run.

## Static results

x86-64, named surface 363:

| | before | after this branch |
| --- | --- | --- |
| implemented | 193 | **196** |
| stubbed (case present, logged `-ENOSYS`) | 2 — `select` (23), `rseq` (334) | 2 (unchanged) |
| conditional `-ENOSYS` fallback inside a real case | 5 — `clone`, `fork`, `vfork`, `sync_file_range`, `clone3` | 5 (unchanged) |
| absent (no case; switch-default `-ENOSYS`) | 168 | **165** |

i386, named surface 451, table slots 440: implemented 205,
stubbed 1 (`clone3`, 435), absent (null slot) 223, beyond the table
22 (440–461; the caller bounds-checks and answers `-ENOSYS`, so
these refuse honestly without a slot). Unchanged by this branch —
the x86-64 side is where the measured guest demand is; the i386
dispositions below are recorded for the queue.

## Dynamic results — what guests actually issue

Across the integration tree's preserved run logs:

| number | name | processes that asked |
| --- | --- | --- |
| 334 | `rseq` | 218 |
| 9999 | (deliberately invalid probe) | 1 |
| 56 | `clone` (conditional fallback path) | 1 |
| 438 | `pidfd_getfd` | 1 |
| 435 | `clone3` (conditional fallback path) | 1 |
| 425/426/427 | `io_uring_setup/enter/register` | 1 each |
| 23 | `select` | 1 |

Across five more lane trees' logs (firstframe, firstvisit,
broadconf, fd15, fd40fix): `rseq` 55, nothing else. Total: **273
processes asked for `rseq`; every other undispatched number was
asked for at most once, and the single-occurrence set comes from
one log — the CPU64 self-test, a program that probes syscall
availability on purpose** (it also probes the impossible #9999).

Two honest conclusions:

- `rseq` is the only undispatched call real workloads issue at
  scale. Every one of those 273 processes continued running —
  glibc asks for `rseq` once at startup and falls back cleanly on
  `-ENOSYS`.
- `epoll_pwait` / `accept4` / `dup3` never appear in the mined
  logs. That is expected, not exculpatory: the mined workloads are
  Wine boots, and Wine's server and DLL unix sides call the older
  variants directly (`epoll_wait`, `accept`, `dup2` — all
  dispatched). The three implemented calls are the ones behind the
  *public* glibc functions (`epoll_pwait()`, `accept4()`,
  `dup3()`) that any other guest binary — a game runtime, a JVM, a
  statically-linked tool — issues as its first choice. The register
  named them "modern calls real libraries issue" before this lane;
  the census now states precisely which evidence supports them
  (spec-side + API-side) and which does not (no observed hits yet).

## Implemented on this branch

All three are x86-64 dispatch cases in
`source/kernel/syscall64.cpp`, proven by new tests in
`source/test/process/testSyscallCensus64.cpp` that drive the real
dispatcher (`ksyscall64`) and **fail before / pass after** (below).

- **`epoll_pwait` (281)** shares the `epoll_pwait2` (441) case body
  — the two calls have identical argument layout except the
  timeout (plain milliseconds vs a timespec pointer), so the
  sigset-size validation, the signal-mask swap scoped to the wait,
  and the event bounce are literally the same code. A negative
  timeout waits forever, per Linux.
- **`dup3` (292)** is `KProcess::dup2` plus the two Linux
  differences: `oldfd == newfd` is `-EINVAL` (dup2 no-ops), any
  flag other than `O_CLOEXEC` is `-EINVAL`, and `O_CLOEXEC` sets
  `FD_CLOEXEC` on the fresh descriptor (dup2 already creates it
  with descriptor flags cleared).
- **`accept4` (288)** mirrors the `accept` case and passes the
  guest flags to `kaccept` — see FD-46 for what that required.

## FD-46 — the defect found on the way (fixed here)

Every socket `accept` implementation consumed the flags word in an
*internal* encoding — non-blocking as `K_O_NONBLOCK`, close-on-exec
as the `FD_CLOEXEC` **bit (value 1)**. The syscall layer passed the
guest's `accept4` flags through untranslated, and in the guest ABI
`SOCK_CLOEXEC` is `O_CLOEXEC` (**value 0x80000**). The bit test
therefore never matched: **32-bit `accept4` with `SOCK_CLOEXEC`
returned a descriptor without close-on-exec, silently** — the
FD-15 class of defect (a confident answer that is wrong), in the
kernel. Non-blocking happened to work only because
`SOCK_NONBLOCK == O_NONBLOCK == K_O_NONBLOCK` by value.

Fix: `kaccept` (`source/kernel/ksocket.cpp`) now validates the
guest flags (`-EINVAL` for anything outside
`SOCK_NONBLOCK | SOCK_CLOEXEC`) and translates them to the internal
encoding at that single entry point. Plain `accept` passes 0 and
is bit-for-bit unaffected; both `accept4` entry points (the
existing 32-bit one and the new 64-bit one) share the corrected
semantics. The new `accept4` test pins the behaviour end-to-end:
unknown flags are refused *before* the pending connection is
consumed, `SOCK_CLOEXEC` lands as `FD_CLOEXEC` on the accepted
descriptor, `SOCK_NONBLOCK` yields a non-blocking socket, and a
flags-0 accept yields a plain descriptor.

## Dispositions for the rest of the measured / modern set

- **`rseq` (334) — `-ENOSYS` is the correct answer today, on
  purpose.** A registration that pins the per-thread `cpu_id` to
  the machine's single CPU is easy; the honest part is the rest
  of the contract — when a signal is delivered while the thread
  is inside an rseq critical section, the kernel must redirect
  execution to the section's abort handler. Without that, a
  signal arriving mid-section would let the handler run and
  return *into* a half-completed per-cpu update: silently wrong,
  strictly worse than the current loud refusal, and exactly the
  failure shape this campaign keeps convicting. glibc's fallback
  is proven by the dynamic census (273 asks, zero resulting
  failures observed). A real implementation must land the
  abort-on-signal check in the 64-bit signal-delivery path first;
  that is recorded in FD-23 as the remaining work, with this
  reasoning.
- **`select` (23) — deliberate stub; route recorded.** glibc's
  `select()` issues `pselect6` (270, dispatched); only raw-syscall
  callers reach 23 (one observed hit, from the probing self-test).
  The honest implementation extracts the `pselect6` case body into
  a shared core (it is currently a single large case) and adapts
  timeval→timespec plus the remaining-time write-back. Left as a
  logged stub rather than a rushed refactor of a working case.
- **`io_uring_*` (425–427) — `-ENOSYS` is correct.** A subsystem,
  not a call; liburing-based guests probe and fall back by design
  (the one observed ask was the self-test probe).
- **`pidfd_getfd` (438) — open.** One probe hit, no real consumer
  yet. Needs cross-process descriptor duplication with
  ptrace-style permission checks; recorded in FD-23 with the
  evidence bar (a real consumer, or a second independent ask).
- **`futex_waitv` (449, both ABIs) — open.** No observed asks.
  Needs multi-futex waiting over the existing futex machinery;
  recorded in FD-23.
- **Conditional fallbacks** (`clone`/`fork`/`vfork` when the
  process has no 64-bit address space, `clone3` sub-paths,
  `sync_file_range`): engineering refusals inside working cases,
  each already logged; the one observed `clone` hit came from the
  self-test. No action.
- **i386 beyond-table numbers (440–461)** refuse honestly via the
  caller's bounds check. No table extension needed; noted so the
  census's "beyond table" count is not misread as a silent hole.
- **The remaining absent numbers** (165 x86-64, 223 i386) are
  overwhelmingly legacy surface (oldstat-family, `uselib`,
  `_sysctl`, …) plus calls whose honest answer in a single-user
  guest is a refusal the *dispatched* neighbours already give
  (e.g. the credential/mount families are refused where they are
  implemented). FD-23 stays open for a per-number disposition of
  the long tail; this lane dispositioned the measured set and the
  modern-API set, which is where silent loss could actually bite.

## Proof

New tests (registered in `source/test/testMain.cpp`):
`testEpollPwaitSyscall`, `testDup3Syscall`, `testAccept4Syscall`.

- **Before** (tests + census only, no implementation): the x64
  suite logs `ksyscall64: unimplemented syscall #281
  (epoll_pwait)` and `#292 (dup3)` from the new tests, and all
  three tests FAIL. (`test-results/fd23-runs/fd23-x64test-before.log`)
- **After**: full x64 suite in the gate-standard serial mode —
  **61/61, "0 tests FAILED"**, including the three new tests, the
  dispatch-guard test (the central logged `-ENOSYS` fallback is
  intact), and the socket/epoll neighbours.
  (`test-results/fd23-runs/fd23-x64test-serial.log`)
- **Census re-run after**: x86-64 implemented 193 → 196, absent
  168 → 165; 281/288/292 no longer listed absent.
  (`test-results/fd23-census/syscall-census.json`)

Two disclosures:

- A 4-worker parallel run of the suite on this branch crashed in
  a late clone-area test under box load ~13, and the "guest CPU
  topology" test failed in parallel mode in *both* the before and
  after runs (it compares two reads of free memory while sibling
  workers allocate; it passes serially). Serial mode is the
  project's gate standard (it is what produced the 661/661 and
  58/58 merge-gate results), and in serial mode this branch is
  fully green. The parallel-mode behaviour predates this branch's
  production changes (the topology failure is in the before log)
  and was not chased here.
- The main 661-test suite was not rebuilt on this lane: the box
  disk sat at ~2.4 GB free with several lanes mid-flight, and the
  merge gate re-runs the full gate per the batching rule. The one
  shared-code change, `kaccept`, is the identity for the only
  flags value its pre-existing callers pass (0), and its socket
  neighbours are covered by the x64 suite above. Flagged here so
  the merge lane runs the full native gate with eyes open.

## Register actions on this branch

- **FD-23** — updated in place: disposition pass recorded
  (three-way census + dynamic census), the three implementations
  named, and the entry narrowed to what genuinely remains: the
  long-tail per-number disposition, `rseq` (with the
  abort-on-signal prerequisite), `select` (shared-core route),
  `pidfd_getfd`, `futex_waitv`. Entry stays **open**.
- **FD-46** — new entry (the `accept4` `SOCK_CLOEXEC` silent
  drop), recorded as found and fixed on this branch, with a
  section-B row; it closes when the branch lands and is re-proven
  at the merge gate. Numbering note: FD-45 was already taken by
  the FD-39 lane's find on its own branch; if any other parallel
  lane has also claimed FD-46, the merge lane owns renumbering,
  as in previous rounds.
