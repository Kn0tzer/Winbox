# CPU differential coverage expansion — 2026-11-07

Lane `lane-cpudiff` off integration tip `faf95d26c`. Fix commit
`35889ff29`. All numbers below are measured from the harness's own
preserved reports (`report.json` in each evidence directory), never
from console summaries.

## The instrument was rotted; repairs came first

At the integration tip the CPU differential harness
(`tools/cpu-diff/`) did not run at all:

1. **Link rot.** The build failed with 11 undefined symbols against
   current product code (split-lock mutex, FsOpenNode page-note
   methods, `KSystem::guestCpuCount()`, the sync-recovery and
   profiler gates, the current-thread registry lookup). Repaired
   with faithful stubs in the harness's existing stub section;
   `guestCpuCount()` returns 2, the documented single source for
   the machine's topology surfaces.
2. **Fault-model rot.** The harness's `raiseSyncFault` stub threw
   unconditionally. Product commit `066fd18ec` routes INT3 through
   the synchronous-fault path, so the stub misclassified the INT3
   snippet terminator and the first full run failed 7,539 of 7,567
   cases with spurious sync faults. The product's own fallback —
   yield at the faulting RIP when no handler takes the fault — is
   what the stub now mirrors: it records the fault and reports it
   undelivered, and `runEmu` classifies a recorded fault that is
   not the terminator as the case's faulted result.
3. **Case rot.** The string-op builder was commented out (its
   stated blocker, per-side RSI/RDI handling, exists). Inside it:
   `stosb rep` lacked `skipRsiRdi`; `lodsb rep` encoded its LEA
   with the wrong source register (`0x36` for `0x33`); and
   `lodsb rep` never set `checkMem`, so the native runner never
   patched the scratch pointer and the native child always
   faulted on the unmapped emulator address — that case had never
   actually run. All fixed.
4. **One stale expectation.** The CPUID case expected a 1-vCPU
   leaf-1 EBX; the machine definition is 2 vCPUs. Test-side fix.

Also added: `--filter <substring>` (run only matching `name/form`
cases) — single-case debugging in a suite of thousands.

## Measured coverage

| | forms | cases |
|---|---|---|
| Repaired baseline (`test-results/cpu-differential-repaired/`) | 4,409 | 7,567 |
| Final (`test-results/cpu-differential/`) | 4,494 | 7,920 |
| Delta | +85 | +353 |

Final run: every set fully passing, `slow_or_inconsistent = 0`
everywhere — fpu 47, int 292, mmx 623, sse 110, sse41 610,
sse42 2,482, ssse3 330 forms. (METHOD.md's 2026-10-14 "167 forms"
checkpoint used a different form granularity and is not
comparable.)

Added, all in the `int` set (207 → 292 forms):

- **Control flow (47 forms, 305 cases):** all 16 jcc conditions in
  rel8 and rel32, each under 8 flag scenarios chosen so every
  condition is exercised taken and not-taken; jmp rel8/rel32/r64;
  call rel32/r64; ret imm16 (the per-side RSP-balance check catches
  a wrong pop count); loop/loope/loopne; jrcxz and jecxz including
  the RCX = 2^32 split (jrcxz not taken, jecxz taken);
  pushfq/popfq round-trips under a defined-bits mask; lahf; sahf
  observed through setcc.
- **String widths and repeat modes (35 forms, 45 cases):**
  MOVS/STOS/LODS/SCAS/CMPS at byte/word/dword/qword under REP;
  REPE early-exit and run-out variants for SCAS/CMPS; REPNE SCAS
  (match found / absent) and REPNE CMPS; DF=1 REP MOVS at byte and
  qword widths; single-step forms at byte and qword widths.
- **Re-enabled (3 forms):** the original `movsb/stosb/lodsb rep`
  cases, repaired per item 3 above.

## The one product divergence, fixed

**REP LODS executed exactly one iteration** in the 64-bit
interpreter (dsp_62), under a comment asserting REP has no effect
on LODS. Hardware repeats the load until the count register
reaches zero, leaving the last element in the accumulator. The
new `lods rep-{b,w,d,q}` cases showed it at all four widths
(native RCX 3→0 and last element vs emulator RCX unchanged and
first element). The handler now repeats with the same count
handling and K-M3 partial-progress fault protocol as the
neighbouring MOVS/STOS and CMPS/SCAS handlers. Filed as FD-193
(lane-local number) with a progress note on FD-25. Proof: the
final full run above, 7,920/7,920 cases passing including the
four REP LODS widths; intermediate state in
`test-results/cpu-differential-expanded/` (expansion before the
fix: exactly those four forms broken).

No other divergence was found: every other new and old form
matches the native reference, including jecxz's address-size
behaviour and the popfq flag round-trips.

## Notes for whoever runs this next

- Snippets must return with DF=0: the native runner's
  result-copying epilogue never clears DF, so a DF=1 return
  corrupts the runner child's own copies. The 0xED7 popfq case
  ends with CLD for this reason. Harness property, not an
  emulator divergence.
- Object files in `tools/cpu-diff/build/` use path-mangled names
  for sources outside the harness directory; recompiling a single
  product source to a hand-chosen object name duplicates symbols
  at link time. Let `build.sh` name them, or match its scheme.
- When rebuilding one object, check the compiler's exit code
  without truncating its output: a failed compile followed by a
  relink silently tests the stale object.
