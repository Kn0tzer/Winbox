# Cleanup wave — 32-bit fault delivery (2026-10-30)

Branch `lane-cleanlog32` (worktree `wt-cleanlog32`), base integ tip
`538f03db5`. Follows the pathfinder's pattern record
(`docs/failure-logging-and-recovery.md` on `lane-cleanlog`): one
greppable `BW64-FAIL key=value` line per failure, the recovery ladder,
reporting never gated. This lane closes the pathfinder census item
"32-bit CPU fault delivery: not yet audited for the same
silent-delivery pattern."

## Audit findings (evidence before changes)

Every 32-bit fault-delivery site was read in full. State before:

| Site | Delivery | Logging | No-handler outcome |
|---|---|---|---|
| `KThread::seg_mapper` (kthread.cpp) | SIGSEGV SEGV_MAPERR if a guest handler exists, else `KMemory::logPageFault` | ad-hoc `SEG_MAPPER`/`SEG_STATE`/`SEG_SEGS`/`SEG_MOD`/`SEG_TEB`/`SEG_STACK` debug dump, first 10 faults only, then silent | `logPageFault` ends in `kpanic("pf")` — emulator-wide `exit(1)` in the default configuration |
| `KThread::seg_access` (kthread.cpp) | SIGSEGV SEGV_ACCERR, same shape | `SEG_ACCESS` line, first 10 only | same `kpanic` |
| `CPU::prepareException` (cpu/common/cpu.cpp) | #GP unconditional; divide/#NP/#SS only with a guest handler; **#BOUND had no branch at all** | none, in any branch | stack walk + `kpanic` — a handler-less divide or a BOUND instruction killed the emulator |
| `CPU::prepareFpuException` (cpu/common/cpu.cpp) | with a guest handler only | none | stack walk + `kpanic` |
| `KThread::signalTrap` (kthread.cpp) | SIGTRAP | none on delivery | `kpanic` at the default disposition — after calling `getNextOp()`, which can itself throw from inside fault handling (the nested-throw crash pattern), under a message naming the wrong instruction class ("Illegal instruction" for a breakpoint) |
| `KThread::signalIllegalInstruction` (kthread.cpp) | SIGILL via runSignal | none | runSignal already applied the default disposition correctly |
| `normal_intO` (normal/normalCPU.cpp) | SIGSEGV trap 4, unconditional | none | runSignal handled disposition |

Key mechanism fact: `KThread::runSignal` already implements the
honest default disposition (SIGKILL the guest process; ignore if the
guest asked). The panics existed because callers bypassed or pre-empted
it. `kpanic` in the default configuration prints and calls `exit(1)`
(util/log.cpp `internal_kpanic`); only with the opt-in recovery flag
does it degrade to killing the guest process.

## Changes (4 commits)

1. **b864f33ab — kthread32 fault delivery.** New shared helper
   `bw64LogGuestFault32` (kthread.cpp, declared in include/kthread.h):
   one `BW64-FAIL guest-<kind>` line with pid, tid, fault address,
   EIP, error, `delivered=` and a counting cap identical to the 64-bit
   side (first 64, then every 1024th; undelivered always logs).
   - `seg_mapper`/`seg_access`: the Portal-era SEG_* scaffolding
     (~80 lines of first-10-only register/segment/stack dumps) is
     deleted and replaced by the standard line on both the delivered
     and undelivered paths. The no-handler path keeps the
     `logPageFault` post-mortem dump, then ends the guest process
     (`process->signal(K_SIGKILL)`, the disposition runSignal applies
     to a default SIGSEGV) and unwinds the thread with `throw 1` —
     the unwind the scheduler already catches for recovery-mode
     panics. `KMemory::logPageFault` no longer ends in `kpanic`.
   - `signalTrap`: the default-disposition `kpanic` (and its
     `getNextOp()` call) is deleted; delivery goes through runSignal
     for every disposition, and is logged (`trap32`).
   - `signalIllegalInstruction`: logged (`ill32`); semantics unchanged.
2. **2b9b625d0 — cpu32 exceptions.** `prepareException` and
   `prepareFpuException` rewritten so every recognized code fills the
   same sigInfo values as before and is delivered through runSignal
   unconditionally; every delivery logs (`exception32`). #BOUND joins
   its segment-fault siblings as SIGSEGV trap 5 instead of panicking
   the emulator. Only an unrecognized exception code — an internal
   bug, not guest input — still panics, and it logs first.
3. **73e8916d5 — normal_intO** logs its delivery (`exception32`).
   Semantics unchanged.
4. **55f137b04 — regression test** `testFaultDelivery32`
   (source/test/process/testFaultDelivery32.cpp, registered in
   testMain.cpp): faults throwaway 32-bit processes three ways
   (divide via `prepareException`, INT3 via `signalTrap`, unmapped
   read via `seg_mapper`) with all dispositions at default and asserts
   the guest process is ended (and, for the page fault, that the
   faulting thread unwinds with throw 1).

Net diff of the three code commits: deletions exceed insertions —
the removed debug scaffolding outweighs the added logging.

## Verification

- All four touched translation units pass a syntax check with the
  test build's exact flags before the full build.
- Full native build: RC=0. **Full native suite, serial: 649 tests,
  0 FAILED** (648 baseline + the new test), including
  "Test 32-bit fault delivery without a guest handler ... OK".
  During the run the new lines fire from real guest code paths
  (e.g. `BW64-FAIL guest-exception32 pid=10 tid=11 ... delivered=1`).
- **Sabotage proof:** restoring a `kpanic` on signalTrap's
  default-disposition path makes the test binary die inside
  `testFaultDelivery32` (exit 1, panic line printed, no OK).
  Reverted; the test passes again on the restored build. On the
  pre-cleanup code all three test cases take a `kpanic` path, so the
  check cannot pass vacuously.
- Known-gaps audit (`tools/known-gaps-audit.mjs --check`): clean —
  every UNOWNED hit registered or allowlisted.
- JS unit suite not run: this lane's changes are native-only and the
  tree has no `dist/` export of its own (the two dist-dependent JS
  tests cannot pass on any tree without one — same accounting as the
  pathfinder lane). Nothing in `web/` was touched.

## Deliberately left alone

- **sigInfo contents and runSignal arguments** at every site are
  byte-for-byte the pre-change values (including quirks like
  `prepareFpuException` delivering through the SIGSEGV slot and
  `normal_intO` reporting an INTO as SIGSEGV trap 4). This lane is
  about visibility and disposition, not re-litigating signal
  semantics; changing those needs its own evidence.
- **The on-demand mapping** of the Wine syscall_frame page at the top
  of `seg_mapper` (a silent successful recovery): it returns before
  delivery by design and is not a failure path.
- **The unrecognized-code panic** in `prepareException`: an
  unrecognized exception code is a violated internal invariant, which
  is what panics are for (pattern doc, ladder rung 5). It now logs
  first.
- **64-bit fault delivery** (pathfinder's area, already at standard)
  and the separate free-function `signalIllegalInstruction` used by
  the 64-bit opcode table in ops.h — outside this lane's area.
- `KThread::modify_ldt`'s invalid-index `kpanic`s (same file, found
  while reading): guest input reaching an internal-invariant panic
  is the syscall lane's pattern, and the pathfinder already converted
  the syscall-layer sites; these two are in the thread object, not
  the syscall dispatch. Flagged here for the coordinator rather than
  widened into this lane.

## Residual risks

- The no-handler page-fault path now throws `throw 1` even when
  `seg_mapper` was called with `throwException=false` (previously it
  could not return either — `kpanic` never returns — so no caller
  could have been relying on a return; the throw matches the
  recovery-mode behavior those callers already tolerate).
- Behavior change visible to guests: a handler-less faulting 32-bit
  process now dies by SIGKILL where the emulator previously exited.
  Any guest that somehow depended on the emulator dying is depending
  on a bug.
