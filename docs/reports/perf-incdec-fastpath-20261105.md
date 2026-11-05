# Performance lane: FF/FE INC/DEC fast path (J64_INCDEC) — 2026-11-05

Branch `lane-perf2` on integ tip `4006ddb58` (batch 7). One change:
the 64-bit fast engine (WASM JIT) now compiles the FF /0 INC, FF /1 DEC,
FE /0 INC and FE /1 DEC read-modify-write instructions (operand sizes
1/2/4/8, register and memory forms) instead of refusing every block
that contains one.

## Why this target

A block compiles only if EVERY instruction in it is fast-path
eligible; one ineligible instruction makes the whole block fall back
to the interpreter on every entry, permanently (per page generation).
The October reject census on the shipped build counted 49% of block
entries falling back, with the FF group the largest single reject
family by distinct block (363), and the coverage table's own note
recorded that `ff 05` (INC dword ptr [rip+disp]) "accounts for most
of the remaining FF census weight" — FF /0 and /1 were deliberately
left interpreter-served when the FF CALL/JMP round landed. INC/DEC
are also among the most common instructions in compiled code
(loop counters, reference counts, string lengths).

## The change

- `include/jit64.h`: new op kind `J64_INCDEC = 38`.
- `source/emulation/cpu/jit64.cpp`: decoder accepts FF groups 0/1
  (size from prefixes) and FE groups 0/1 (size 1); classifier declines
  LOCK/REP forms, FS/GS-segment memory forms (pre-existing generic
  guards) and the size-1 register-direct AH/BH/CH/DH encodings
  without REX (conservative, mirroring the C6 precedent);
  `jit64BlockMayStore` reports memory forms; coverage table gains
  rows 38/39.
- `source/emulation/cpu/jit64wasm.cpp`: hand-written `emitIncDec`
  (mirroring the DIV/IDIV precedent). Flags are computed exactly as
  ADD/SUB of 1 for OF/SF/ZF/AF/PF, and CF is PRESERVED, as the
  architecture requires. `emitFlags` gained a merge-mask parameter;
  INCDEC merges with `F_INCDEC_MASK = 0x8D4` (the ALU mask without
  CF). Publishing is inline, like the other ALU kinds; the kind is
  in the width-1/2/4/8 group, the memory-form whitelist, the
  acceptance switch and `jit64OpWritesMemory` (so self-modifying
  code invalidation treats these stores exactly like ALU stores).

### A bug this lane's own tests caught before any guest ran

The first emitter version cleared CF from the clear-mask but merged
the *computed* flag word whole, so a computed carry leaked into
RFLAGS (INC of 0xFF wrongly set CF; DEC of 0x00 wrongly set CF).
The differential tests flagged it; the fix ANDs the computed flag
word with the merge mask before merging (a no-op for every existing
full-mask caller). Standalone repro after the fix: DEC byte of 0x00
with CF clear yields rflags 0x296 (was 0x297); INC byte of 0xFF
yields 0x256; DEC dword yields 0x296.

## Correctness evidence

- Native decoder/plan tests (`jit64_tests`): 873 passed, 0 failed,
  including a new `testIncDec` (decode sizes/forms incl. RIP-relative
  `ff 05`, FE forms, negative groups /2+ and far forms, plan checks
  incl. LOCK fallback, FS-memory fallback, AH decline, SPL-with-REX
  fast path, mid-block stream flow, emit-ok at all sizes, mayStore)
  and the updated canonical-loop expectation (the whole loop body is
  now one compiled block, n=4).
- Emitted-code differential suite (`jit64_wasm_tests.mjs`, full run
  via the native fixture binary): 314 passed, 0 failed. Includes a
  new INCDEC edge suite (overflow at sign boundaries at 8/64-bit,
  CF preserved in both directions, memory forms, zero-extension,
  SPL form) checked against a JS reference model of the interpreter,
  plus a 600-case differential fuzz over random
  kind/size/value/flags.
- Generated-emitter gate (`jit64_gen_tests`): byte-identical
  (failures=0).
- JIT lifetime tests (`scripts/test-jit-lifetime.sh`): both modes
  pass.
- Native main suite (`make test`, `boxedwine 0 0 1`): 686 tests,
  0 FAILED (standing size).
- Native x64 suite (`make x64-test`): 111 tests, 0 FAILED
  (standing size).
- JS unit suite: 353 passed, 0 failed (standing size). Legacy JS:
  115 passed, 0 failed (standing size).
- Guest fixture battery (below): checksum identical across the
  interpreter and the fast engine, on both builds, in every run —
  the fast path does not change results.
- NOT covered: the block-invalidation differential tests
  (`jit64_invalidate_tests.cpp`) could not be run — the project's
  own gate script fails to link them on the integ tip for reasons
  unrelated to this change; filed as FD-166. INCDEC stores flow
  through the same page-generation / noteGuestWrite machinery as
  the existing ALU r/m fast paths (`jit64OpWritesMemory` +
  `jit64BlockMayStore`, both unit-tested above).

## Measurements

Method: guest fixture `tools/guest-fixtures/incprobe64.c`
(registered as `incprobe64-pe`), a battery of explicit INC/DEC
assembly at every width in register and memory forms — including
flag capture folded into a checksum and CF preservation proven by a
following ADC — followed by a timing loop of 8 explicit INC/DEC
instructions, 100,000 passes per sample, 4 samples per boot, timed
with QueryPerformanceCounter inside the guest. Driven by
`web/tests/suite/browser/incprobe-bench.test.mjs`, which boots a
fresh session per engine mode and (following `jit64.test.mjs`)
waits for Wine prefix initialization to exit before spawning the
fixture. Warm median = median of samples 1..3. Machine load
recorded per run; the box was shared with other lanes throughout.

Before (baseline dist, wasm sha256 d7917c91…, clean integ tip):

| run | mode | samples (ms) | warm median |
| --- | --- | --- | --- |
| B1 | interpreter | 273, 654, 310, 294 | 310 |
| B1 | fast engine | 559, 491, 417, 623 | 491 |
| B2 | interpreter | 1011, 413, 546, 634 | 546 |
| B2 | fast engine | 2066, 640, 1814, 958 | 958 |

Before, the fast engine is 1.6–1.8× SLOWER than the interpreter on
this loop (every block containing an INC/DEC fell back). B2's
fast-engine boot counters: 77,390,848 blocks entered, 35,537,240
fallbacks (45.9%), 3,126 blocks emitted.

After (rebuilt dist with this change, wasm sha256 9d52c281…):

| run | mode | samples (ms) | warm median |
| --- | --- | --- | --- |
| A1 | interpreter | 188, 287, 206, 487 | 287 |
| A1 | fast engine | 68, 54, 89, 134 | 89 |
| A2 | fast engine | 134, 432, 102, 102 | 102 |
| A2 | interpreter | 237, 269, 601, 298 | 298 |

After, the fast engine runs the loop 2.9–3.2× faster than the
interpreter in the same run, and 4.8–10.8× faster than the fast
engine did before the change (the range spans the noisiest and
quietest before/after pairings; the two canonical same-day pairs
give 491→102 and 958→89). Absolute medians move ±~2× with machine
load on this shared box; the within-run engine ratio and the
direction of every pairing agree. Checksum `51d907cb02447577`
identical in every sample, mode and build.

Real demo program — Doom via the project's demo matrix
(`DEMO=doom JIT=1`, same invocation both times):

| | before (load ~11–25) | after (load ~5–10) |
| --- | --- | --- |
| stage reached | mapped, overlay cleared | mapped, overlay cleared |
| frames presented | 13 | 132 |
| click-to-mapped | 504.9 s | 258.0 s |
| guest instructions retired (final METRICS) | 545,897,420 | 829,784,777 |
| blocks entered / fallbacks | 134,654,824 / 62,457,367 (46.4%) | 210,339,693 / 101,780,003 (48.4%) |
| final screenshot | gameplay, 20,833 distinct colors | gameplay (inputs applied), 28,803 distinct colors |

Read honestly: Doom still reaches gameplay exactly as before and
presents ten times as many frames in the measurement window, but
the two runs had different machine load and Doom's session-wide
fallback share is dominated by code outside this op family, so
Doom is corroboration, not the controlled measurement. The
controlled measurement is the fixture bench above.

Portal smoke on the after build (same demo-matrix invocation,
`DEMO=portal JIT=1`): stage "mapped", overlay cleared,
click-to-mapped 44.1 s, 197 frames presented, visible frame at
78.9 s with 38,871 distinct colors (non-dark fraction 0.985), and
the input sequence (escape, mouse) rendered through the final
screenshot at 451 s (31,843 distinct colors). Portal reaches the
state it reached before this change (menu and in-game rendering);
no Portal before-run was repeated for timing, per the lane rule
against burning shared capacity on extra full-game boots — the
before-state is the integration record's (batch-7: Portal reaches
its menu on the integ tip this lane branched from).

## Harness findings recorded for other lanes

- `jitbench.test.mjs` spawns fixtures immediately after session
  ready, while Wine prefix initialization (`wineboot --init`) is
  still running; on 2026-11-05 that wedged the fixture's wineserver
  handshake in 5 of 6 attempts (the fixture's wine64 blocks in a
  socket read with empty queues and never reaches main; the
  standard `jitbench64` fixture wedged identically, so it is not
  fixture-specific). `jit64.test.mjs`'s pattern — wait for the
  `wineboot.exe --init` exit line before spawning — succeeded
  reliably; `incprobe-bench.test.mjs` follows it. This looks like
  one concrete mechanism inside FD-115's wedged-start family.
- `bw64_jit_stats_json_text()` truncates at 3072 bytes; filed as
  FD-165.
- The invalidation gate's link rot; filed as FD-166.

## Files

- `include/jit64.h`, `source/emulation/cpu/jit64.cpp`,
  `source/emulation/cpu/jit64wasm.cpp` — the fast path.
- `source/emulation/cpu/tests/jit64_tests.cpp`,
  `source/emulation/cpu/tests/jit64_wasm_tests.mjs` — tests.
- `tools/guest-fixtures/incprobe64.c` — guest fixture (new).
- `web/tests/suite/lib/guest-fixtures.mjs`,
  `web/tests/suite/browser/jitbench.test.mjs` — fixture registered
  with the existing bench.
- `web/tests/suite/browser/incprobe-bench.test.mjs` — the
  before/after bench (new).
