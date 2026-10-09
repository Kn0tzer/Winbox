# cpu-diff methodology

## Environment note (verified 2026-10-09)

The task assumed the SSH box is x86_64. It is not: `uname -m` on
129.213.44.0 reports **aarch64**, and the default `g++` targets ARM64.
There is no x86-64 hardware on this box. The harness adapts:

- **Ground truth**: x86-64 snippets execute under `qemu-x86_64-static`
  11.1.1 (user-mode). QEMU's x86-64 TCG implements full architectural
  semantics for the integer ALU, x87, and SSE instructions tested here;
  only architecturally *defined* flags/results are compared, so
  implementation-defined details (e.g. NaN payload propagation) cannot
  produce false positives.
- **Timing**: native timing is unavailable (no x86-64 hardware; qemu TCG
  timing is not cycle-meaningful). Timing is emulator-only, reported as
  ns per instruction, with an emulator-relative slow threshold (below).

## What it does

For each test case, the harness assembles a short x86-64 snippet from
hand-encoded bytes (deterministic; the exact bytes appear in the report),
then executes it twice:

1. **Natively (qemu-x86_64)** — `runner/runner.elf`, a static x86-64 ELF,
   runs under qemu. For each case it forks an isolated child; the child
   mmaps a stack, a scratch page, and the snippet (+`ret`), then calls a
   hand-written asm trampoline (`native_run`) that loads all 16 GPRs +
   rflags from the test's initial state, `call`s the snippet, and captures
   all GPRs + rflags via `pushfq`. The trampoline saves/restores
   callee-saved registers (rbx/rbp/r12-r15) and the red zone. A crash or
   5 s timeout in the child is reported as a native-runner failure, never
   blamed on the emulator. Cases are batched (64 per qemu invocation).
2. **In the emulator** — `CPU64` driven directly in-process (same pattern as
   `source/emulation/cpu/cpu64SelfTest.cpp`): `KMemory64`, code at
   `0x400000`, snippet terminated with `0xCC` (INT3 sets `yield=true`
   without touching registers), stack page at `0x7FF000`, scratch page at
   `0x900000` (16-byte aligned).

Because stack/scratch live at different numeric addresses on the two
sides, the differ compares register *values* (all GPR inputs are
identical) and scratch *contents*; rsp is checked per-side for
stack-balance, and rbx (which holds the scratch address for FPU/SSE
tests) is checked per-side for preservation. Each case runs 3× in the
emulator; differing results across runs are flagged *inconsistent*.

## What is compared

- All 16 GPRs (rsp per-side balance; rbx per-side preservation when it
  holds the scratch address).
- Only **architecturally defined flags** per instruction (Intel SDM), e.g.
  AF is excluded for shifts, OF excluded for multi-bit shifts, CF excluded
  for inc/dec (but CF *preservation* is checked separately), no flags for
  div/idiv/mov/lea/bswap. Undefined-flag bits are never compared.
- For FPU/SSE: the bytes stored to the scratch page (8 or 16 bytes).

## Timing

Timing uses a loop (`mov r15d, N; L: <setup+insn>; dec r15d; jnz L`,
N = 200000) executed in the emulator only. Reported cost =
(full loop − setup-only loop) / N, min of 2 runs, in ns per instruction.
This measures the **interpreter** (`CPU64::step`), not the JIT — numbers
are relative, useful for finding per-instruction implementation overhead,
not absolute guest performance.

## "Slow" threshold (chosen from the data)

Slow = emulator ns/insn above **4 × the median ns/insn of the set**.
Justification: with no native hardware for a ratio, the threshold must be
emulator-relative; 4× median identifies genuine per-instruction outliers
(e.g. an instruction that takes an expensive helper path) instead of
flagging the interpreter's typical cost. The median and threshold are
printed per set at the top of the run and recorded in the report.
"Inconsistent" = emulator results differ across the 3 runs (or vs native
intermittently).

## Buckets (per instruction set, per instruction form)

1. **total** — instruction forms tested (`name/form`, e.g. `add/r64,imm8`).
2. **fully passing** — every case matches native, stable, fast.
3. **passing but slow or inconsistent** — correct results, but above the
   slow threshold or flaky across runs.
4. **fully broken** — any case with wrong regs/flags/memory vs native, an
   emulator fault, or failure to reach the INT3 terminator.

Priority: broken > inconsistent > slow > passing. Raw per-case data
(instruction, bytes, native vs emulator state) is in `report.json`; the
per-form table in `REPORT.md` makes every bucket auditable.

## Scope and limits

- Pass 1 (155 forms): full integer ALU (mov/push/pop/add/sub/adc/sbb/and/or/
  xor/cmp/test/inc/dec/neg/not/lea/shifts/rotates/shld/shrd/mul/imul/div/
  idiv/xchg/xadd/bswap/movsx/movzx/cbw-cqo/setcc/cmovcc), x87 FPU starter
  (fld1/fldz/fldpi/fldl2e, faddp/fsubp/fsubrp/fmulp/fdivp/fdivrp, fxch),
  SSE starter (movaps/movups/movdqa/movdqu, addps/subps/mulps/divps/
  maxps/minps/sqrtps, andps/andnps/orps/xorps, paddd/psubd/paddq/psubq/
  pmullw/pcmpeqd/pand/por/pxor).
- Pass 2 (2026-10-09, +12 forms): x87 constants (fldl2t/fldl2e/fldlg2/fldln2)
  and transcendental (f2xm1/fyl2x/fptan/fpatan/fxtract/fprem/frndint/fscale).
  Note: fyl2xp1 has 1-ULP precision difference vs x86 80-bit (documented,
  dispatch uncommitted).
- Not covered yet: string ops (emulator implements MOVS/STOS/LODS/SCAS/CMPS
  but harness lacks RSI/RDI per-side infrastructure), control flow (jcc/
  loops/calls), segment overrides, privileged/system instructions, most of
  SSE2+ integer SIMD, FPU environment (fldcw/fstenv).
- AVX/AVX2: intentionally unsupported. Emulator clears AVX bit in CPUID
  (cpu64.cpp:2852) so glibc selects non-AVX paths. Documented, not a bug.
- MMX: advertised in CPUID (MMX=1) but no instruction handlers implemented
  (0F 6E/6F/7E/7F unhandled). Known gap; MMX is deprecated (superseded by
  SSE) and rarely used by modern games.
- The harness never modifies emulator behavior; test-only stubs in
  `harness.cpp` cover symbols differential snippets cannot reach
  (`ksyscall64`, `KSystem::getSystemTimeAsMicroSeconds`,
  `KProcess::signalProcess`, `KThread::runningThread`,
  `CPU64::raiseSyncFault`/`deliverPendingSignals`, `klog`).

## Bug fixes applied during development (2026-10-09)

The harness found two genuine emulator bugs that were trivial and verifiable;
both were fixed in the working tree (source/emulation/cpu/cpu64.cpp) with
before/after harness evidence. See BUGS.md for details. The fixes are NOT
committed - they await review.

1. IMUL r64,r64 CF/OF (signed-overflow UB miscompilation): fixed by computing
   the 64-bit product in __int128 before truncating to S64.
   Before: 3/12 cases failed. After: 12/12 pass.
2. FLD1/FLDZ double-push: cpu64.cpp called fpu.PREP_PUSH() before fpu.FLD1() /
   fpu.FLDZ(), but those methods push internally.
   Before: fpu 2/11 pass. After: fpu 9/11 pass.

Remaining failures (ADC/SBB flags, FLDPI/FLDL2E unimplemented, SQRTPS NaN
payload) are documented in BUGS.md as reported-not-fixed.
