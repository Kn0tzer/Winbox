# CPU core cleanup / logging / error recovery — lane record

Lane: `lane-clean-cpu`, worktree `/home/ubuntu/wt-clean-cpu`, based on
integ tip `538f03db5`. Area: the CPU emulation core — the 64-bit
interpreter (`cpu64.cpp`), the 64-bit JIT (`jit64.cpp`, `jit64wasm.cpp`,
`jit64wasm_runtime.cpp`), and the shared instruction decoder
(`decoder.cpp`, used by both cores' tooling and the 32-bit interpreter's
trace path).

Standard followed: the pathfinder lane's
`docs/failure-logging-and-recovery.md` (on `lane-cleanlog`) and the
clean-up skill. Note: the skill path in the wave brief
(`/root/.bb/runtime/global-skills/<hash>/skills/clean-up/SKILL.md`) no
longer exists on the box; the skill was read at its current location,
`/root/.bb/skills/clean-up/SKILL.md` (same 4364-byte file the
coordinator verified on 2026-10-30).

## Changes (4 commits)

1. **`8d855d936` cpu64: log synchronous instruction faults; route FX
   #GP sites through the fault funnel.**
   `CPU64::step()`'s `faultAtInstruction` delivered #UD/#GP faults
   (UD2, the privileged-opcode group, far CALL/JMP, the
   unimplemented-opcode tail) with **no log line at all** — a guest
   dying there was indistinguishable from a hang, the same blind-spot
   class as the dead-start memory faults. It now emits a
   counting-capped `BW64-FAIL guest-insnfault` line
   (pid/sig/trap/rip/delivered/count; first 64, then every 1024th),
   and always logs when delivery fell back to signalling the process.
   The FXRSTOR and LDMXCSR #GP sites open-coded the identical
   deliver-or-signal pattern; they now call the funnel — same signal,
   same trap, same fault address (the instruction start in both
   forms), same yield behavior — and gain the line.

2. **`41db0191e` decoder: instruction-log formatters degrade on
   unknown width instead of panicking.** The nine operand-formatting
   helpers behind `DecodedOp::log` ended in `kpanic_fmt` for any
   operand width they do not print. Width is a static log-table
   constant, and the table **already** routes a width-64 entry
   (Cvtsd2si) to `logRE`, which prints 8/16/32 only — so tracing that
   one instruction in a debug build killed the emulator from inside a
   text formatter. (Verified by parsing the table against each
   helper's handled set: exactly one live mismatch today; the other
   eight panic branches are the same trap waiting for the next
   64/128-width entry — the table already uses widths 64 and 128 with
   other helpers.) All nine now funnel to `logUnknownWidth`: a
   counting-capped `BW64-FAIL decoder-log` line (fn/name/width/count)
   plus a placeholder in the trace text. Handled widths format
   exactly as before. decoder.cpp is CRLF-terminated; the edit was
   made in binary mode so the diff stays at the changed lines.

3. **`71a459a37` jit64: instantiation failures log their reason and
   get a BW64-FAIL line.** When the wasm engine refused a JIT module,
   the EM_JS catch in `jit64wasm_runtime.cpp` discarded the exception
   — the only place the reason (compile vs link vs OOM) exists — and
   the C++ caller silently fell back to the interpreter: a broken
   engine looked exactly like a working one, only slower. The catch
   now logs the reason (counting-capped: first 8, then every 1024th)
   before returning -1, and `CPU64::tryWasmExec` emits a capped
   `BW64-FAIL jit-instantiate` line (rip/module-bytes/outcome/count).
   Fallback behavior unchanged.

4. **`c0bcdb399` cpu64: BW64-FAIL token on the unimplemented-opcode
   line.** The handler was already loud (full byte dump, then SIGILL)
   but its line lacked the token, so a `BW64-FAIL` sweep missed it.
   Prefix added; consumers match on the `CPU64: unimpl opcode` /
   `unimpl opcode` substrings (web/tests baldi-d3d9-prep,
   scratch-dxvk, scratch-vk, scratch-baldi), both preserved.

## Audited, deliberately unchanged

- `CPU64::deliverGuestMemoryFault` / `validateInstructionFetch`
  (cpu64.cpp) — the dead-start blind spot, **already fixed by the
  pathfinder lane** on `lane-cleanlog`. Not duplicated here; editing
  the same functions would only collide at merge time.
- cpu64.cpp UD2 site's `if (bw64RecoverEnabled())
  bw64RecoverNoteSigillFallback();` — the callee is a statistics
  counter for the recovery feature (it feeds
  `bw64_recover_stats_json_text`), not failure reporting. The fault
  itself is delivered unconditionally and, per change 1, now logged.
- cpu64.cpp unimplemented-opcode multi-line
  `if (bw64RecoverEnabled()) { ... }` block — per-site audit result:
  the failure's reporting is unconditional below the gate (the
  unimpl-opcode line + SIGILL delivery); the gated block reports only
  the gated skip *action*, which does not happen when the gate is
  off. Gate left as designed. (This closes the cpu64 entry in the
  pathfinder census.)
- Instruction-skip recovery (`BW64_RECOVER`) stays opt-in — the
  pattern document's deliberate exception. Untouched.
- JIT emit-rejection path (`jit64EmitWasm` returning false) — a
  by-design degrade for unsupported op streams: the refusal is
  recorded on the entry (-2), in the negative cache, in the
  reject-site registry, and in the `emitRejected` counter that
  surfaces in `BW64JITSTATS` and the metrics publish. A per-block
  log line would be startup noise for a designed path. The emitter
  itself has no panics/aborts (67 decline-by-return sites, all data
  to the caller). No change.
- Engine selection (`jit64SelectEngine` WASM→INTERP fallback when the
  wasm engine is unavailable) — designed degrade; the selected engine
  is reported in the JIT stats JSON / metrics surface, which is the
  "is it actually running" evidence. No change.
- `CPU64::execWasmBlock` — read in full; fault publication and
  checkpoint replacement are carefully handled (K-M3), counters
  updated on every path. No change.
- #DE divide-by-zero path (cpu64.cpp) — already logs values and
  outcome richly, including the no-handler termination. No change.
- `DecodedOp::dealloc` double-free `kpanic` (decoder.cpp) —
  `_DEBUG`-only guard for a violated internal invariant (allocator
  corruption). The legitimate panic class; left.
- `jit64_wasm_mem_shared_impl` EM_JS catch returning 0 — a
  capability probe where an exception *is* the "not shared" answer,
  not a swallowed failure. Left.
- `cpu64RunElf` `readFileAll` — dev harness helper; its caller
  prints the failure and exits nonzero. Left.
- `bw64RecoverLogOnce`'s reporting gate (`source/util/log.cpp`) —
  outside this area and already changed by the pathfinder lane. Not
  duplicated.

## Handed off (evidence-backed, not fixed here)

- **32-bit interpreter op handlers panic on guest input**
  (`source/emulation/cpu/ops.h`): the twelve INS-string handlers are
  bare `kpanic("INSB/INSW/INSD on port %X not implemented")`
  (ops.h:3708–3753), and `int99` kpanics on an unregistered callback
  index (ops.h:3339) — both reachable by guest execution, both kill
  the whole emulator. No degrade was invented here: the 32-bit core
  has no port-IO convention to degrade *to* (there are no single
  IN/OUT op implementations at all), so the right answer (0xFF fill
  vs #GP delivery) is a 32-bit-core design decision for that lane.
- `normal/normal_other.h:262,287` mirror the two cpu64 recover-gate
  sites audited above; the same conclusions likely apply — for the
  32-bit lane to confirm in its own context.
- `CPU64::raiseSyncFault` (`source/kernel/syscall64.cpp:3173`) logs
  nothing itself. Deliveries routed through `step()`'s funnel now
  log at the funnel; direct kernel-side callers (e.g.
  syscall64.cpp:5152) remain unlogged at the delivery point — kernel
  lane's call.
- Pre-existing: the `-D_DEBUG` Linux compile of decoder.cpp is
  broken at baseline (`translateToLockInstruction` switch vs
  `-Werror=switch`), verified byte-identical failure on the pristine
  integ-tip file. Untouched by this lane.

## Verification

- Per-TU compiles with the project's exact native test flags
  (`make -n test` command line): `cpu64.cpp` clean after changes 1,
  3, 4; `decoder.cpp` clean after change 2.
- `jit64wasm_runtime.cpp`: `em++ -fsyntax-only` with the project's
  real emscripten flags (from `make -n wasm64-mt`) — clean; the
  modified EM_JS body extracted and passed `node --check`.
- Post-fix census re-run: the only `kpanic` left in decoder.cpp is
  the deliberate `_DEBUG` invariant guard above.
- Native suite, single worker, on the final tree (full build
  BUILD_RC=0): 648 tests, 0 FAILED, exit 0 (327s) - matches the
  648/0 baseline reported for this base by the pathfinder and
  kernelgate lanes.
- Build history note: the first full build attempt died at 113
  objects with 'cc1plus: Terminated signal' - the compiler
  processes were killed externally on the loaded box, not a
  compile error; the restarted build completed clean and is the
  build the suite ran on.
- Parallel-run caveat: three runs at the harness default of 4
  workers died with a host SIGSEGV (exit 139) within the first
  few tests, in the 32-bit KThread::seg_mapper fault-dump path
  (source/kernel/kthread.cpp - not modified by this lane), with
  the completed-test count varying between runs (2, 2, 3). The
  bridges lane recorded the same exit-139 behavior for parallel
  runs on this box, and the kernelgate lane's 648/0 was also
  single-worker; the single-worker run above is the comparable
  result. The 4-worker crash was not A/B-tested against a
  pristine-tip binary.
- The `_DEBUG` decoder compile failure was A/B-proven pre-existing
  (pristine file, identical errors), not introduced by change 2.

## Risk handback

- Load-bearing safety fact: fault-delivery semantics are unchanged —
  same signals, trap numbers, fault addresses, and yield behavior;
  the additions are log lines and one env-gated diagnostic dump the
  funnel already produced for other #GP sites. Evidence: the commit
  diffs (the FX sites' old bodies are line-for-line the funnel's
  logic) plus the native suite result above.
- Log volume: every new hot-path line is counting-capped. The single
  uncapped variant (`delivered=0`) fires only when no guest handler
  is installed, which ends the thread (yield + process signal) — it
  cannot repeat in a loop from the same instruction stream.
- The decoder placeholder alters trace text only for cases that
  previously crashed the process; handled-width output is
  byte-identical.
