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

## K-M4 first red/green cycle

`make -C project/linux x64-test -j2` built the new tests against HEAD production source after archiving/reverting the inherited patch. `boxedwine 27 3 1`: 3 tests FAILED, with missing FP images, RSP alignment/DF errors, and nested altstack frame corruption. Logs: `km4-red-build.log`, `km4-red.log` in the scratch directory. An earlier attempted invocation before compilation completed ran ZERO tests against the stale binary; it is not evidence.

Reused the inherited FXSAVE refactor; replaced the signal layout/return handling. New implementation keeps restorer/ucontext/siginfo in Linux rt_sigframe order, publishes a separate aligned FP image (glibc's inline __fpregs_mem is NOT a kernel placement contract), reserves 1152 bytes, preserves nested altstack frames/red zones, clears handler DF/RF/TF and FP state, and rejects unreadable/invalid-MXCSR FP restore instead of silently resuming. Focused `boxedwine 27 3 1`: 3 OK / 0 FAILED (`km4-green.log`). No full WASM or game proof yet.

A full x64 invocation overlapped sibling probe PID 1609945 (gb2). The solo precheck printed it but the shell continued; this was a lane error. The run is non-authoritative and will be repeated solo. It reported 29 OK / 1 FAILED: the source guard pins the OLD siginfo expression and the obsolete PAGE_SIZE spelling from before da7e5f5f5. Updated the expected expressions without deleting assertions; real queued-signal and clone3 ABI tests still required. Added async FP snapshot/null-pointer semantics coverage. No claim that concurrency caused these source guard failures.

## Native harness abort: previous environmental explanation refuted

Solo fresh-shell native suite aborted with system_error EPERM. Strace showed no denied syscall; GDB catch-throw reached the FIRST CPU test, inside JitCodeGen::doJIT, so this was not a startup/scheduler failure. Preserved raw trace and debugger logs in scratch. Installed strace for this diagnosis (authorized system dependency).

Control: rebuilt ONLY jitCodeGen.cpp.o with debug info; first CPU test then passed. Old/new object disassemblies show CPU::memory load offsets 768 and 864 respectively. The 96-byte discrepancy matches BOXEDWINE_JIT_ARMV8's storedRegs[12]. This proves incompatible compiled layouts were linked; the source-level lock was not the defect. The makefile tracks headers but not compiler flags.

Added native-build-flags.test.mjs: actual make/g++ tiny CPU-layout fixture, unchanged source; switch ABI_WORDS 8 -> 104 -> 8, require the 96-byte size change and reverse, and require unchanged flags to reuse the object. Pre-fix: 0 passed / 1 failed (observed delta 0, expected 96), native-flags-red.log. Added per-build flag fingerprints whose old stamps are removed on configuration switches. Full clean rebuilds and suite reruns remain required. No register closure yet.

Signal full x64 solo result (before the build-cache fix): 31 OK / 0 FAILED, km4-x64-solo.log. First three signal tests had 3 FAILED before / 3 OK after; async/null case additionally passes. Full native and WASM still pending.

## Consistent native rebuild and next precise-fault probes

After adding the configuration dependency, make rebuilt all 400 existing native objects. A fresh-shell solo `project/linux/Build/Test/boxedwine 0 0 1` completed: **632 OK / 0 FAILED** (native32-config-clean-solo.log), including the SIG_DFL subprocess. No test was removed/weakened. This resolves the formerly unexplained EPERM with build evidence. Raw evidence copied to `/home/ubuntu/backups/winbox-sol-a-20261025/evidence/`.

The signal implementation's earlier solo x64 run was **31 OK / 0 FAILED**. The full JS unit suite is now running against this lane's dist; no result yet. Full WASM and game probes still pending.

Prepared (not yet compiled/run) testMemoryFault64.cpp cases for load destination preservation, RMW flags, PUSH RSP, cross-page XMM state, and cached-block poll-page revocation. The old data-protection self-test only asserts a fault/address and successful permission transitions, not interrupted state. Code proves interpreter helpers continue mutating state after recordGuestMemoryFault; deliverGuestMemoryFault only rewinds RIP. JIT block dispatch occurs before k64SetGuestAccessCPU, and emitted imports have no independent protection gate. These are concrete path gaps, but runtime red results still pending.

Found verification coverage gap: x64-test previously compiled neither BOXEDWINE_BLOCK_EXEC nor BOXEDWINE_BLOCK_CACHE_INFRA; its BW64_JIT=1 runs did not enter block dispatch. Enabled those definitions in the pending makefile change; the new poll test requires nonzero blocksExecuted when JIT is enabled. Earlier 31/31 remains valid signal/interpreter evidence only.

Probe staging: durable Mindustry zip has 109 entries, exe `home/username/apps/Mindustry.exe`; Q3 zip has 37, exe `home/username/apps/quake3e.x64.exe`. Existing tools/bench/gl-game-probe.mjs accepts REUSE_GAME_ZIP=1, GAME_ZIP, WINE_DIST_DIR, CAPTURE_SECONDS and forward-input capture, so no page/demo edits needed. Added CONTROL_TRACE/GUEST_PROFILE controls to it; node --check passes. Set TMPDIR to the lane scratch directory when probing.

Further source findings to test, not fixed: sigaltstack reports stored flags without deriving SS_ONSTACK, allows changing active stack, and accepts AUTODISARM without implementing delivery/return state; clone64 inherits the parent's altstack via cloneRegistersFrom even for CLONE_VM without CLONE_VFORK. Linux v6.12 kernel/fork.c lines 2277-2281 explicitly sas_ss_reset in that combination. These fit the existing clone/signal residuals and require regression tests before implementation. No claim that they caused Mindustry.
