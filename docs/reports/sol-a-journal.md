# Lane A investigation journal

## 2026-10-25: inherited state and hypothesis ledger

Scope: existing branch `lane/winbox-p2kernel-20261023`, HEAD `da7e5f5f5`. No branch creation, push, deployment, other-worktree writes, or stash operations authorized.

Read handoff v2 and gate runbook. Source wins over reports. Inherited uncommitted K-M4 patch archived as `/tmp/winbox-finish/work-sol-a/inherited-km4.patch`; not yet verified. Build check reports BUILD IDLE; probe slots empty; load 0.39 at inspection. Permanent opc Chromium processes are unrelated (runbook solo discipline), so no processes were killed.

| Hypothesis | Evidence | Current status / next discrimination |
| --- | --- | --- |
| K-M4 FP/SSE lost across signal delivery | HEAD frame fpregs pointer is null; partial patch serializes legacy FXSAVE but ignores invalid restore | Add fail-first native CPU64 delivery/return tests, including edits and nested delivery; audit format against Linux/glibc |
| Signal stack alignment / nesting corrupts handlers | Source retSlot is frameBase+128, frameBase 16-aligned; both delivery paths always pick altstack top | New tests must assert RSP modulo 16 = 8 and disjoint nested frames |
| K-M3 permission checks missing on execution paths | Source ALREADY has k64CheckGuestAccess and interpreter execution context (contradicts broad register wording); tryJitStep runs outside that context | Audit JIT helpers and instruction fault rollback; test safepoint read, stack write, cross-page cases |
| K-M2 shared indirect target corruption | Handoff evidence only; not reproduced by this lane | Preserve as OPEN; correlate branch trace after proven CPU fixes |
| Q3 epoll spin | Handoff evidence only | Inspect readiness/edge/wake-one paths and add minimal reproducer before patching |
| LOCK overlap corruption | Previous committed exact-oracle stress tests; not rerun yet | Do not reopen without contradictory evidence |

ABI primary sources inspected: Linux v6.12 arch/x86/kernel/signal.c (get_sigframe, handle_signal), arch/x86/kernel/fpu/signal.c, glibc 2.40 sysdeps/unix/sysv/linux/x86/sys/ucontext.h. Linux handler entry uses aligned stack minus 8 and does not reset stack top for nested altstack delivery. Glibc mcontext fpregs is a pointer; the kernel need not place state at glibc __fpregs_mem. No claim of browser success or completed fix yet.

## Verification log

No suites run yet. All original game failures remain unverified on this lane revision.
