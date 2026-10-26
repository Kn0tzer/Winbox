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
  The original FYL2XP1 vector used x=1, outside Intel's defined operand
  range. Its differing result was an invalid comparison, not evidence of
  a 1-ULP precision difference. The current vectors use valid operands.
- Not covered yet: string ops (emulator implements MOVS/STOS/LODS/SCAS/CMPS
  but harness lacks RSI/RDI per-side infrastructure), control flow (jcc/
  loops/calls), segment overrides, privileged/system instructions, most of
  SSE2+ integer SIMD, FPU environment (fldcw/fstenv).
- AVX/AVX2/FMA/BMI: not implemented. The emulator reports no AVX, so
  guests do not see it, but VEX-encoded instructions are still not
  executed. Source facts: CPUID leaf 1 ECX omits OSXSAVE (bit 27) and AVX
  (bit 28); leaf 7 reports no BMI1, AVX2 or BMI2 (`cpu64.cpp:3220-3262`);
  XGETBV(0) returns `0x3`, x87+SSE state only (`cpu64.cpp:4906-4916`). VEX
  prefixes C4/C5 have no handler and go to the `unhandled` path
  (`cpu64.cpp:1225`, `:7531`).
  - Affected software, by how it selects code:
    1. Code that runs VEX instructions with no CPUID check: builds for
       x86-64-v3 or with `-mavx`/`-mavx2`/`-mfma`/`-mbmi2` across the
       whole program, and AVX intrinsics without runtime dispatch. These
       reach the unhandled VEX path.
    2. Runtime-dispatched code: glibc IFUNC selection for memcpy, strlen,
       and libm variants, and any library that queries CPUID. It sees no
       AVX and takes its SSE path. That is correct and is what the
       differential harness exercises.
    3. Baseline x86-64 and x86-64-v2 builds (SSE4.2/POPCNT) do not use
       VEX and are not affected.
  - Silent-skip risk: on the Emscripten build `BW64_RECOVER` defaults on
    (`source/util/log.cpp:60-70`), so the unhandled path skips the
    instruction and the guest continues. The per-RIP cap
    (`recoverFaultsAtRip < 3`, `cpu64.cpp:7613`) resets at the start of
    every step whose RIP differs from the last one (`cpu64.cpp:979-981`).
    A skip always advances RIP, so the cap cannot stop this path. Native
    builds default recover off and stop at the unimplemented-opcode
    bail-out. A skipped VEX instruction leaves its destination unchanged
    and logs at most one `recover: skipped` line per RIP.
  - Detection: `tools/cpu-diff/avx-probe.c` (build line in its header).
    It prints `cpuid.1.ecx.osxsave`, `cpuid.1.ecx.avx`,
    `cpuid.7.0.ebx.{bmi1,avx2,bmi2}`, `xgetbv0.eax` when OSXSAVE is set,
    and `vex.vmovd`, which is `executed`, `SIGILL`, or `NOT_EXECUTED`.
    `NOT_EXECUTED` is the skip case: the destination register is checked,
    because a skipped instruction raises no signal. Measured under
    `qemu-x86_64` (2026-10-24): default CPU model gives AVX/AVX2 present
    and `vex.vmovd=executed`; `-cpu max,avx=off,avx2=off` gives
    `vex.vmovd=SIGILL`; a copy with the VEX bytes replaced by NOPs gives
    `NOT_EXECUTED`. Expected emulator output, from source and not yet run
    on CPU64: `cpuid.1.ecx.avx=0`, `cpuid.7.0.ebx.avx2=0`, and
    `vex.vmovd` of `SIGILL` (native recover off) or `NOT_EXECUTED`
    (Emscripten recover on). The `cpuid` set in `harness.cpp` also checks
    the emulator's CPUID values against the advertised feature set.
  - Owner decision, not made here: implement AVX, keep CPUID clear and
    make unhandled VEX fail closed on every build, or leave it as is.
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

## Verified checkpoint — 2026-10-14

The current harness runs 1,560 cases covering 167 instruction forms.
All 118 integer, 23 x87 and 26 SSE forms pass the native/QEMU comparison.
The historical failure statements above describe the 2026-10-09 run.

FYL2XP1 now includes valid positive/negative quarter inputs, tiny positive
and negative epsilon, and negative zero. Exact stored-result comparisons
remain in place. The valid tiny and signed-zero cases failed before the
production FPU switched from `log(1+x)` to `log1p(x)`, and pass afterward.
The native CPU64 harness checks those three cases in both its cached-f64
and extended-register paths.

The operand restriction comes from [Intel SDM Volume 2A, FYL2XP1,
page 3-425](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2a-manual.pdf).
An out-of-range result is undefined; the harness must not require QEMU's
particular NaN for that input. The baseline now uses zero, and the new valid
inputs provide the numerical regression coverage.

The runner checks source/header freshness before using a cached executable.
The CLI returns nonzero for wrong results, an unstable emulator result, or
a failed native reference run; timing outliers remain reported separately.
