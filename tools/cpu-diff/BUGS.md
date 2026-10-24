# CPU Differential Harness — Bug Findings

Generated 2026-10-09 by tools/cpu-diff. Each bug was found by differential
testing (native x86-64 via qemu-x86_64 vs CPU64 emulator) and verified with
minimal repros.

## Fixed (applied to working tree, NOT committed — awaiting review)

### 1. IMUL r64,r64 (0F AF) — CF/OF never set on signed overflow
**File:** source/emulation/cpu/cpu64.cpp (0F AF handler)
**Bytes:** `48 0F AF C3` (imul rax, rbx)
**Expected:** CF=1, OF=1 when signed product overflows 64 bits
**Actual:** CF=0, OF=0 always
**Root cause:** `S64 r = sa * sb` is signed-integer-overflow UB. GCC assumes
the overflow cannot happen and folds `(__int128)sa*sb != (__int128)r` to false.
**Fix:** Compute the 64-bit product in `__int128` first, then truncate to S64
(well-defined), then compare.
**Before:** 3/12 imul r64,r64 cases failed (flags 0x801 vs 0x0)
**After:** 12/12 pass

### 2. FLD1 (D9 E8) / FLDZ (D9 EE) — double push
**File:** source/emulation/cpu/cpu64.cpp (D9 E8 / D9 EE handlers)
**Bytes:** `D9 E8` (fld1), `D9 EE` (fldz)
**Expected:** Push one value; TOP decreases by 1
**Actual:** TOP decreases by 2; the extra slot contains garbage
**Root cause:** cpu64.cpp called `fpu.PREP_PUSH()` before `fpu.FLD1()`/`fpu.FLDZ()`,
but FPU::FLD1/FPU::FLDZ already call PREP_PUSH internally.
**Fix:** Removed the redundant `fpu.PREP_PUSH()` calls.
**Before:** fpu set 2/11 pass (faddp/fsubp/etc. read garbage registers)
**After:** fpu set 9/11 pass

## Reported (not fixed)

### 3. ADC / SBB — AF (and sometimes CF/OF) wrong when carry/borrow-in is set
**File:** source/emulation/cpu/cpu64.cpp, CPU64::runAlu (cases 2, 3)
**Bytes:** `48 11 D8` (adc rax, rbx), `48 19 D8` (sbb rax, rbx), plus imm8/imm32 forms
**Expected vs actual (examples):**
- adc r64,r64: native flags 0x297 vs emu 0x286 (AF missing)
- adc r64,r64: native 0x11 vs emu 0x801 (AF missing, OF spuriously set)
- sbb r64,r64: native 0x810 vs emu 0x0 (AF+OF missing)
**Root cause:** `flagsAdd(rflags, lhs, rhs + cIn, ...)` — the `rhs + cIn` can wrap
(e.g. rhs=0xFF..FF, cIn=1 → 0), so the flag computation sees the wrong operand.
AF/CF/OF must be computed from the full `lhs + rhs + cIn` (or `lhs - rhs - cIn`).
**Fix shape:** Rework runAlu cases 2/3 to use `unsigned __int128` for the full
operation and derive CF/AF/OF from it. Non-trivial; not attempted.

### 4. FLDPI (D9 EB) / FLDL2E (D9 EC) — dispatch present (historical report)
**Bytes:** `D9 EB`, `D9 EC`
**Expected:** Push pi / log2(e) onto FPU stack
**Original actual:** Emulator stopped (unhandled opcode); did not reach INT3 terminator
**Status (source review, 2026-10-24):** the cpu64.cpp dispatch exists
(`source/emulation/cpu/cpu64.cpp:7226-7230`: `FLDL2E` and `FLDPI` call
`fpu.FLDL2E()` / `fpu.FLDPI()`). The cpu-diff vector for these bytes has not been
re-run since, so the pass/fail status here is unverified until the harness is run.

### 5. FXCH (D9 C9) — was blocked, now passes
**Bytes:** `D9 C9`
**Note:** Initially reported as broken, but the failure was caused by FLDPI
(unimplemented) in the test vector. After changing the test to avoid FLDPI,
FXCH passes. Not an emulator bug.

### 6. SQRTPS (0F 51) — NaN payload differs from x86
**Bytes:** `0F 51 C1` (sqrtps xmm0, xmm1)
**Expected (x86):** 0xFFC00000 (indefinite QNaN) for negative input
**Actual:** 0x7FC00000 (host default QNaN from std::sqrtf on ARM)
**Root cause:** Emulator uses `std::sqrtf()` which produces the host's NaN;
x86 mandates the indefinite QNaN (0xFFC00000) for sqrt of negative.
**Severity:** Minor — result is still NaN, only the payload differs.
**Fix shape:** Check for negative input (other than -0.0) and return 0xFFC00000
explicitly. Not attempted (NaN propagation has many edge cases).

## Test-vector notes
- fsubp/fmulp/fdivp test vectors originally used FLDPI (unimplemented); changed
  to use FLD1/FLDZ so the actual fop is tested.
- The `slow_or_inconsistent` bucket is 0 for all sets: no instruction exceeds 4x
  the set median emulator time, and all 3 emulator runs per case agree.
