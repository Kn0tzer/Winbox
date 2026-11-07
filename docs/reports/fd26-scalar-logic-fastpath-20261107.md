# Performance lane: scalar SSE moves + packed FP logic fast path (J64_SSE_MOVSCALAR / J64_SSE_LOGIC128) - 2026-11-07

Lane `lane-fd26` on integration tip `faf95d26c`. FD-26 stays
OPEN; this lane closes two more of its reject families and
files FD-195 / FD-196 (lane-local numbers).

## Why this target

A block compiles only if EVERY instruction in it is fast-path
eligible; one declined op makes the whole block (up to 24 ops)
fall back to the interpreter permanently. After batch 9 the
remaining declined SSE families were the scalar moves, the
packed logic ops, and scalar FP arithmetic. A static capstone
sweep (linear sweep of executable sections) of the shipped
wine64 DLLs found the scalar forms the only declined SSE
families present there in quantity: ntdll.dll (103,665
instructions) carries 184 scalar moves and 467 scalar FP
arithmetic instructions; user32.dll (139,259) carries 96 and
217; kernel32.dll and win32u.dll carry almost none. MOVSD /
MOVSS are how x86-64 code loads, stores and spills every
scalar double / float, XORPS is the XMM-zeroing idiom and
ANDPD is the fabs mask in hot FP code - the blocks these
families poison are exactly the FP/data-movement blocks worth
compiling.

Scalar FP *arithmetic* was deliberately left out of scope:
wasm f64 NaN-payload and min/max semantics differ from the
interpreter's, so it is not bit-exact by construction. Moves
and bitwise logic are: no flags, no traps, no rounding.

## The change

Commits `b29af281c` (engine + tests), `a4b21e8b6` (guest
fixture + bench), `148142414` (fixture timing fix, below).

- `include/jit64.h`: new kinds `J64_SSE_LOGIC128 = 39`
  (sub = the 0F opcode byte 0x54/0x55/0x56/0x57, size 16) and
  `J64_SSE_MOVSCALAR = 40` (sub 0 = MOVSD load, 1 = MOVSD
  store, 2 = MOVSS load, 3 = MOVSS store, 4 = MOVQ load;
  size 8 or 4).
- `jit64.cpp`: decoder branches - the scalar forms are carved
  out before the existing SSE branch (F2/F3 on 0F 10/11, and
  F3 0F 7E with no 0x66 for the MOVQ load; the 66-prefixed
  0F 7E stays the MOVD store, and 66+F3 stays undecoded);
  the logic forms require REP == 0 (the 0x66 PD forms are the
  same bit op). Classifier cases pin each sub to its exact
  prefix/size pairing. `jit64BlockMayStore` reports exactly
  the two scalar memory stores. Coverage table 40 -> 42 rows.
- `jit64wasm.cpp`: write classification, width groups,
  memory-form helper group, per-op validation, and two
  emitters. Semantics mirror the interpreter's blocks in
  cpu64.cpp field for field: register-form scalar moves
  preserve the destination's upper bits EXCEPT the MOVQ load
  (high qword always zeroed); memory scalar loads zero
  everything above the scalar; scalar stores write only the
  scalar's bytes; ANDN is ~dst & src; a scalar load publishes
  through a temp register so a denied read leaves the
  destination untouched; the logic source is staged before
  anything publishes, like PXOR.
- Tests: `jit64_tests.cpp` (decode/classify for every form,
  REX.W/REX.R behaviour, stacked-prefix and LOCK refusals,
  may-store, forged-op emission refusals, coverage table) and
  `jit64_wasm_tests.mjs` (five targeted tests covering each
  form's upper-bit rule, a 500-iteration differential fuzz
  against a JS model transcribed from the interpreter blocks,
  and fault tests for a denied scalar load, a denied logic
  memory source and a denied scalar store).

## Correctness evidence

- Native gate, phase 1 (`scripts/build-jit-wasm.sh
  --selftest-only`): 919 passed / 0 failed on this branch;
  a pristine checkout of the tip run the same day gives the
  standing 873 / 0. Generated emitters byte-identical.
- Wasm fixtures (`jit64_wasm_tests.mjs` against the native
  fixture binary): 323 / 0 (tip figure 314; the nine new
  tests are numbers 270-278 in the run).
- Sabotage sensitivity: with both new kinds decoded as
  J64_UNKNOWN, the phase-1 suite goes to 903 passed / 16
  failed, and all 16 failures are the new checks (decode,
  classify, may-store, must-emit). Restored afterwards.
- JS unit suite: 353 passed / 1 failed; the one failure is
  provenance-artifacts, which needs a dist/ in the tree -
  re-run after this tree's build it passes (354 / 0 on the
  built tree).
- The gate's section-14.3 invalidation stage does not build
  at the tip (FD-195, pre-existing - the pristine checkout
  fails identically, so the fixture stage above was run
  standalone exactly as the script runs it). The emulator's
  native main / x64 suites were not run by this lane; they
  are the checking queue's arm, and this change is confined
  to jit64 plus its own differential suites above.

## Measurements

Protocol: `sseprobe64.exe` (battery + two timing loops,
100,000 passes per sample, 8 ops per pass) under the suite
bench `sseprobe-bench`; a fresh browser session per engine
mode, modes 0,1,1,0 (0 = interpreter, 1 = fast engine); the
statistic is the bench's warm median of samples 1..3. The
battery checksum was identical in all eight sessions
(`4df7127a5e3dd7a6`), as were the loop checksums (scalar
`81e005d65f44701d`, logic `6d3db8206051a387`) - same results,
only the speed changed. Evidence:
`test-results/suite-sseprobe-bench/bench-before.json` and
`bench-after.json` (full samples + build provenance), plus
both console logs in the same directory.

Warm medians (ms):

| build | loop | interpreter sessions | fast-engine sessions |
| --- | --- | --- | --- |
| tip `faf95d26c`, wasm `cd9dc8fd...` | scalar | 408 / 489 | 967 / 416 |
| tip | logic | 246 / 219 | 357 / 330 |
| this branch, wasm `4950e00e...` | scalar | 293 / 433 | 34 / 40 |
| this branch | logic | 221 / 205 | 82 / 34 |

On the tip the fast engine was at parity-or-worse on these
loops (their blocks fell back whole). On this branch, the
within-run ratio of fast engine to interpreter is 8.6x and
10.8x for the scalar loop and 2.7x and 6.0x for the logic
loop. Load honesty: the box was shared throughout and the
BEFORE run carried heavier competing load (its interpreter
medians sit ~1.2-1.4x above the AFTER run's and its bench
took 868 s wall against 382 s) - the within-run ratios are
the controlled figures, and even cross-build worst-to-best
(tip's best fast median 416 -> this branch's worst 40) is a
10x improvement on the scalar loop. Interpreter medians are
unchanged across builds within load noise, as expected: the
interpreter did not change.

Scope honesty: the loops are tight loops over exactly the
newly compiled families - they measure the effect's ceiling
on such blocks, not a whole-program speedup. The
whole-program gain is bounded by how much hot code sat in
blocks these families poisoned; the static sweep says the
scalar forms are the declined SSE families that actually
occur in the shipped DLLs.

### The fixture's own timing anomaly (FD-196)

The fixture originally printed its millisecond figure as a
double. On the tip build, the first sample in each process
printed sane (504 ms, 530 ms) and every later sample printed
~1e66-1e68 garbage - in interpreter and fast-engine sessions
alike - while all checksums stayed correct. The fixture now
computes milliseconds in integer math and prints the raw
QPC delta (`ticks=`, frequency 10 MHz) on every line, so the
figures above can be re-derived from the logs. Which FP step
is wrong was not isolated; filed as FD-196 with the killed
run's console preserved at
`test-results/fd26/console-fp-garbage-partial.log`.

## Harness findings recorded for other lanes

- FD-195: the invalidation gate's fixed source list no
  longer links at the tip (missing ksystem.cpp /
  fsopennode.cpp / kthread.cpp definitions). Evidence:
  `test-results/fd26/inv_build_interp.log` (pristine run).
- FD-196: the FP print anomaly above.
- Shared-store desync: since 2026-11-07 ~03:27 UTC the
  shared store's runtime zips match no committed manifest
  (wine64 `1b725073...`, wine32 `aa2bb9d6...`, prefix32
  `68164e56...`, prefix64 `197251244...` - the last matches
  lane-fd189's manifest only), so a fresh prep off the tip
  fails PIN MISMATCH everywhere. This lane prepped both
  bench trees against a private store assembled in /tmp
  from the retained archive
  `backups/winbox-runtime-assets-20261106/browser-runtime-20261106.tar`
  (all six zips match the tip's pins) and deleted it at
  closeout. The coordinator was already diagnosing the
  desync in parallel.
- The companion static sweep of two large game binaries
  (Godot / Unity) did not complete - the sweep process died
  silently three times under box load. The family ranking
  above rests on the completed Wine-DLL sweep.

## Files

- Engine: `include/jit64.h`, `source/emulation/cpu/jit64.cpp`,
  `source/emulation/cpu/jit64wasm.cpp`
- Tests: `source/emulation/cpu/tests/jit64_tests.cpp`,
  `source/emulation/cpu/tests/jit64_wasm_tests.mjs`
- Bench: `tools/guest-fixtures/sseprobe64.c`,
  `web/tests/suite/browser/sseprobe-bench.test.mjs`,
  `web/tests/suite/lib/guest-fixtures.mjs`
- Register: FD-26 progress note, FD-195, FD-196, section-B
  row for `lane-fd26`.
