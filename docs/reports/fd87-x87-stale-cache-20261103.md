# FD-87 — x87 loads left a stale FPU double-cache entry authoritative (lane-fd87loss, 2026-11-03)

**Status: FIXED on `lane-fd87loss`. Fixing commit `dd6a6855d`. Register entry FD-87 closed
(CL-55 provisional; the merge audit settles numbering). New filing from this lane: FD-109
(native test binaries crash under their default parallel workers; serial runs pass).**

## What was filed vs what was true

FD-87 was filed by lane-fd85pay as "64-bit guest writes interleaved with repeated
cross-module calls are lost to later reads" — the convicted blocker behind FD-85's silent
audio. The filing's evidence was real (buffers filled across repeated `sin` calls read
back as zeros; the shipped OpenAL tone fixture produced silence), but the mechanism was
misattributed to memory serving. The memory layer is innocent, proven by direct trace
(below). The guest's *stored values themselves* were already wrong: they were computed
by an x87 `fsin` that read a stale register.

## Conviction path (each step eliminated a layer)

1. **Reproduction.** The carried fixtures (fpprobe6/fpprobe7) reproduce exactly in a
   browser build of the integration base: post-loop arrays all `0.0000`, accumulator
   `0.0000` against the true `184.1776`.
2. **Memory exonerated.** With `BW64_BSSDIAG` watching the array's page, every store
   lands in the one committed buffer (never decommitted/adopted during the fixture's
   life), the argument array provably holds the written values, and the `sin` loop's
   stores already carry `0.0` — the value is wrong *before* the store. Readbacks match
   the stored (wrong) value, which is why in-loop verification passed vacuously.
3. **Shape split.** Per-iteration syscalls (Sleep/QPC) mask the defect; plain calls
   don't. Pointer calls to msvcrt's `sin` (pure SSE) are always correct; the fixtures'
   own `sin` is *not* msvcrt's — mingw `-lm` links a static wrapper
   (`fld`/`fsin`/`fstp` through `__sinl_internal`, whose core is a single `fsin`).
   The 80-bit path (`fldt`/`fstpt`, `sinl`) is immune; the double path is not.
4. **The tell.** A bare hand-written loop `fldl; fsin; fstpl` (no calls at all)
   reproduces, and fpprobe21 printed the sequence it produces across iterations:
   `0.47942553860420301, 0.46126955503318068, 0.4450853368470909, ...` — the
   iterated sine, sin of the *previous result*, converging toward the Dottie number.
   An FNSAVE dump after a fresh load showed the exact correct 80-bit state
   (`[3ffe:8000000000000000]` for the argument, `[3ffd:f57743a2582f8000]` after
   `fsin`), so loads and the FPU arithmetic core are faithful; the wrongness enters
   through the register's cached-double shadow.
5. **Root cause.** In `source/emulation/cpu/common/fpu.cpp`, the FPU keeps two
   representations per register: SoftFloat `regs[]` (80-bit) and a double `regCache[]`
   shadow gated by `isRegCached[]`. With `KSystem::useF64 == false` (the 64-bit
   guest), `FPU::FLD_F32` / `FLD_F64` / `FLD_I16` / `FLD_I32` wrote the loaded value
   into `regs[]` but **did not clear `isRegCached[store_to]`**. `FPU::getF64()`
   (reached from the `FSTP` handlers and other double readers) sets that flag and
   leaves its cached double behind, so the next load into the same physical slot
   kept the stale cached value authoritative: `FSIN` and every other cache-honoring
   op computed on the previous occupant of the slot. `FLD_I64`, `FLD_F80`, `LD80`
   and the constant loaders already cleared the flag; the four memory-load entry
   points were the only ones that didn't. This explains every observed property:
   first call correct, tight loops degrading, `FINIT` (which memsets the flags)
   and FPU-churning detours (printf, syscall paths) masking it, pointer-called
   SSE `sin` immune, 32-bit stack (`useF64 == true`, coherent flag discipline)
   unaffected.

## The fix

Commit `dd6a6855d`: each of the four load entry points now clears
`isRegCached[store_to] = false;` on its 80-bit path — four added lines in
`source/emulation/cpu/common/fpu.cpp`, matching the pattern the neighboring loaders
already used. No other product file changed.

## Proof (evidence in `test-results/fd87loss/` of the lane worktree)

- **Fixtures, browser build of the fixed tree** (`build-browser-fix.log`: "Browser
  export verified"): fpprobe6 final `acc=184.1776 (true 184.1776)`; fpprobe7
  post-loop `sinv[100]=0.8415 sinv[500]=-0.9589 sinv[999]=-0.5356`, `bad=0 bad2=0`;
  fpprobe8 all five sections `acc=184.1776` incl. the previously-lost A/B/D and the
  A recheck; fpprobe9 zero-count `1` at every checkpoint; fpprobe10
  `main-before=1 worker=1 main-after=1`; fpprobe16 direct vs pointer sections agree
  (`0.909297` / `0.479426`); fpprobe19 X/Y/Z all `0.4794255386`; fpprobe20 SC
  `ratio=1` for all five arguments at iteration 500; fpprobe21 loop returns
  `0.47942553860420301` on every one of 10 iterations (`fix1/`, `fix2/` consoles).
- **Native suites, run serially** (the suite binaries' default parallel workers
  crash on this box on base and fixed builds alike — filed as FD-109):
  X64Test `0 tests FAILED`, EXIT=0 (`suite-x64test-serial.log`); main Test
  `0 tests FAILED`, EXIT=0 (`suite-main-serial.log`).
- **The filed closing consequence**: the shipped common-dependencies test's audio
  stage passes with its checks unloosened — verified on the composed state,
  because the stage also needs FD-65's payload (Gecko/fonts) and FD-85's test-side
  capture fix, which live on sibling branches: scratch tree at the batch-3
  integration tip `caadab5ad` + `dd6a6855d` cherry-picked, bootstrapped from the
  retained `runtime-assets-20261103` archive (dist `wine64.zip` = `be4d4956…`,
  the FD-65 restage), build "Browser export verified", then
  `node web/tests/suite/runner.mjs --browser --filter=common-dependencies` →
  **PASS common-dependencies, SUITE RESULT: 1 passed, 0 failed** (`OPENAL: RESULT 0`)
  (`commondeps-verify.log`). A control run of the same test on this lane's own
  base-pinned dist (pre-FD-65 payload `53d77f33…`) fails at the Gecko stage as
  expected, confirming the payload dependency is FD-65's, not this fix's.

## Notes for the merge audit

- The FD-87 entry text above the closure still carries the filing lane's
  "writes are lost" framing; the closure paragraph records the correction. The
  number collision noted in the entry stands (this is the third FD-87 claim);
  settlement is the audit's, per the standing rule.
- CL-55 is provisional, claimed after CL-53/CL-54 (lane-fd99-100).
- FD-109's number is likewise provisional.
- The fixture sources fpprobe8–fpprobe21 and the probe harness
  (`web/tests/fpprobe-probe.mjs`) are committed on this branch (`36aa29c12`) as
  the conviction record; they are diagnostics, not suite tests.
