# Port Plan: PR #151 WASM JIT → boxedwine64 `cpu64`

Analysis date: 2026-03-09. Sources: local fork `/home/ubuntu/boxedwine64` @ working tree,
and `danoon2/Boxedwine` PR #151 ("introduce a wasm jit"), head `9c3190ffbaf29b41c08553dc696b94dde9ff26b4`,
base `master`, **closed, merged=no**, 153 commits, 90 files, +62,779 / −1,808.

---

## 0. Correction to the task premise (read first)

Three things differ from how the task is framed:

1. **There is no standalone "WASM JIT interpreter" in PR #151 to port onto `cpu64`.** The PR adds
   `source/emulation/cpu/wasm/jitWasmCodeGen.{h,cpp}` (+14,292 / +1,327) which is a **backend of the
   existing `JitCodeGen` virtual interface**, one class `JitWasmCodeGen : public JitSSE`, sibling to the
   existing `jitX86CodeGen` (x32) and `jitArmV8CodeGen`. The block/decode/call graph is inherited from
   `jit/jit.cpp` + `jit/jitCodeGen.cpp` and is *not* reimplemented.
2. **"Merged" means module-merge, not source-merge.** `wasmModuleMerger.cpp` (+266) concatenates many
   single-function wasm modules into one bounded group; `wasmJitBatchPolicy.cpp` (+220) decides when a
   batch flushes. Both are orthogonal to codegen.
3. **`/home/ubuntu/boxedwine64/patch.diff` (897 lines, 9 files) is already a slice of PR #151's interface
   refactor** — `IfLessThan2` → `IfLessThan(..., bool isSigned)`, plus `IfGreaterThan`/`IfGreaterThanOrEqual`
   signed/unsigned, and `Jit::setImulOverflowFlags`. Part of the prerequisite interface churn is already
   present in this fork. Do not re-apply it.

So the port is: **make `JitCodeGen`/`Jit` express 64-bit guest state, then add `JitWasmCodeGen64`
and hook it into `cpu64`'s block loop.**

---

## 1. Fork `cpu64` interpreter structure (mapped)

`source/emulation/cpu/cpu64.cpp` (5,995 lines) is a **hand-written block interpreter with its own IR**,
*not* a `DecodedOp`/`ops.h` consumer:

| Symbol | Line | Role |
| --- | --- | --- |
| `CPU64::run()` | 5449 | outer loop; env-gated tracer (`BOXEDWINE64_TRACE_FROM/TO`), wild-jump RIP ring (`BW64_WILDJUMP`), `while (!yield)` |
| `CPU64::step()` | 613 | per-insn entry (the slow path the JIT must supersede) |
| `CPU64::decodeModRMRecipe()` | 5580 | decodes ModRM into a re-materializable `BRecipe` |
| `CPU64::buildBlock(U64 startRip, BBlock&)` | 5654 | **the block builder** — fills `BBlock.recs[]` of `BRec` (`BK_*` kinds), capped at `BBLOCK_MAX_RECS`; bails on `lock`/`rep`/`osize16` |
| `CPU64::execBlock(const BBlock&)` | 5792 | **the block executor** — `switch (rec.kind)` over `BK_MOV_RM_R`, `BK_ALU_RM_R`, `BK_ALU_RM_IMM`, … calls `resolveRecipe`/`loadRM`/`storeRM`/`runAlu` |
| `CPU64::tryBlockStep()` | 5914 | block fast-path attempt |
| `CPU64::runBounded(U64)` | 5954 | test harness entry |
| `cpu64DumpOpProfile()` | 546 | **existing op profiler** — free phase-0 baseline data |
| `fetchDword/fetchQword`, `push64/pop64`, `readReg8/writeReg8` | 116–146 | memory model: **64-bit guest addresses**, `U64` |

Memory model: `KMemory64* memory` member; guest memory is **not** shadow-paged onto linear memory the way
`JitCodeGen::readHost` assumes. Register file `reg[]` with `u32`/`u64` accessors (REX.W selects width).
Helpers `flagsAdd/flagsSub/flagsLogic`, `maskFor(U32 width)`, `evalCC(U8)`.

Related: `include/cpu64.h` (329 L), `source/emulation/cpu/common/cpu.h` (544 L — the 32-bit `CPU`),
`decoder.h` (1,667 L, `DecodedOp`-based, 32-bit path), `source/emulation/cpu/normal/normalCPU.cpp`
(already carries `BOXEDWINE_DIRECT_NORMAL_DISPATCH` with `MUSTTAIL return` arms explicitly commented
"lets WASM emit direct return_call instructions").

Fork has **no x86-64 codegen backend at all** (`jit/x32/`, `jit/armv8/` only) and only 8 `BOXEDWINE_64`
mentions across `jit/` — the 64-bit JIT interface is essentially unexercised.

---

## 2. PR #151 architecture (what we are porting)

### 2.1 Codegen: `jitWasmCodeGen.h` (+1,327) — the contract

Doc comment (lines 20–30):
> "One WASM module per compiled JIT block. Module imports Emscripten linear memory + C++ helper
> functions. Exports a single `execute` function: `(param cpu_ptr i32)`… The resulting function export
> is added to Emscripten's `wasmTable` and its table index is stored in `DecodedOp::pfnJitCode`.
> Subsequent calls go through `wasmStartJITOp()`."

Local layout (constants, lines 199–225):

```
WASM_CPU_LOCAL       = 0    // param: CPU* as i32 offset into linear memory
WASM_RELOC_LOCAL     = 1    // param: per-activation relocation slot array (nested guest calls safe)
WASM_GP_LOCAL_BASE   = 2    // 8 slots: eax..edi
WASM_SEG_LOCAL_BASE  = 10   // cs,ds,ss,es segment-address slots
WASM_TMP_LOCAL_BASE  = 14,  WASM_TMP_LOCAL_COUNT = 32
WASM_I64_SCRATCH     = 46   // i64 scratch for imulRRI/imulRR overflow only
WASM_F64_LOCAL_BASE  = 47,  count 8 (shared JitFPU)
WASM_V128_LOCAL_BASE = 55,  count 12 (reserved, MMX/SIMD later)
WASM_DIRECT_LOOP_BUDGET_LOCAL = 67
```

**`RegPtr` here is a wasm local index, not a machine register** — that is the key inversion versus asmjit.
Register state is kept in wasm locals for the block's duration, flushed to linear memory via `cpu_ptr`
at block exit / any helper call.

Memory: an **inline TLB fast path** (`emitAligned inline load; else call helper`) backed by page→RAM-offset
arrays living in `KMemoryData`, `BOXEDWINE_WASM_JIT`-gated, ~8 MB total. Slow path imports C++ helpers
through the function table: `m_helperReadMemIdx`, `m_helperWriteMemIdx`,
`m_helperEmulateSingleOpIdx`, `m_helperGetNextOpIdx`, `m_helperSyncToHostIdx` (header lines 1027–1031).

Compilation/instantiation is out-of-process-free but JS-mediated:
`boxedwine_wasm_instantiate_runtime_batch(bytes, size, importFns, importCount, entryCount, outputSlots)`
→ `int* outputSlots` of wasmTable indices; plus a release path to free a block from `wasmTable`.

Flag/state handling is inherited wholesale from `Jit`/`JitSSE` (`lazyFlags`). Unsupported constructs are
explicitly enumerated in the header (xadd mem8/16, cmpxchg mem8/16, cmpxchg8b, bswap32, 16-bit imul, etc.)
— **read lines 84–190 before estimating scope; that list is the real "not implemented" ledger.**

### 2.2 Emitter: `wasmEmitter.{h,cpp}` (+503/+482) — *reusable nearly as-is*

Self-contained wasm binary writer: sections (Custom/Type/Import/Function/Export/Code), value types
incl. `V128`, ~200 opcodes, full SIMD surface (`emitSimdOp`, `emitI8x16Shuffle`, `emitV128Const`),
control flow with **branch hints** (`WASM_BRANCH_HINT` — the `else` opcode the engine reads),
imports (memory/function/global), and `finalize()` → `std::vector<U8>`. **Port cost ≈ 0 LOC** — this file
is architecture-independent.

### 2.3 Cache pipeline (JS/offline, +3,700 LOC) — *deferrable*

- `boxedwine-wasm-jit-module-broker.js` (+1,834), `-cache-pipeline.mjs` (+1,502) with `--flat` and
  "piped" grouping modes, `-cache-analyze.mjs` (+649), `-cache-verify-merged.mjs` (+261)
- `binaryen_js.js` (+31,518) vendored Binaryen build for offline optimise/verify
- Emscripten shell UI (+795) with `?jit-record=true` AOT zip export; IndexedDB persistence
- makefile targets `jit`, `multiThreadedJit`; test target `testJit`

### 2.4 Merging

- `wasmModuleMerger.{h,cpp}`: parses Type/Import/Code sections of N single-function modules and emits one
  module with them side by side (shared imports, multiple exports). ULEB/LULEB-aware, bounds-checked.
- `wasmJitBatchPolicy.{h,cpp}`: `WasmJitBatchKey{KMemory*, U32 mappedFileKey}`;
  `WasmJitFlushReason{BlockCount, ByteCount, PendingHits, ProcessBytes}`;
  limits `maxBlocks=64, maxBatchBytes=512KB, urgentPendingHits=8, maxProcessOpenBytes=4MB`;
  `cancelMemory(KMemory*)` is the invalidation hook.

### 2.5 Tests shipped (use as port gates)

`source/test/cpu/testWasmJitModuleBroker.cpp` (+2,616), `testWasmJitBatch.cpp` (+1,718),
`testXchg.cpp` (+1,135 — emuSingleOp path), plus `testCPU.cpp`/`testMain.cpp`/`makefile`/`Jenkinsfile` wiring.

---

## 3. Port surface

### 3.1 What `JitCodeGen` expects vs. what `cpu64` has

`JitCodeGen` (`jit/jitCodeGen.h`, 207 L, **byte-identical to the fork's copy except two hunks**) is
`public Jit` with ~180 pure-virtual backend hooks. `JitWasmCodeGen` implements ~250 of them
(add/ sub/ and/ or/ xor/ shifts/ rotates/ shld/shrd/ mul/div/ string ops/ FPU hooks/ MMX-SSE / direct_*).
It consumes `DecodedOp*` from `decoder.h`.

| Component | `cpu64` today | Needed | Est. LOC |
| --- | --- | --- | --- |
| **Emitter** | — | port `wasmEmitter.*` verbatim | **~1,000 (0 new)** |
| **Block decoder bridge** | `buildBlock` → `BBlock`/`BRec` | *either* emit `BBlock` directly (new backend, smaller) *or* make `cpu64` build `DecodedOp` chains so `jit.cpp` drives it. Bridge both: `BRec → DecodedOp` adapter | **600–1,200** |
| **Codegen backend** | — | `JitWasmCodeGen64` from `JitWasmCodeGen` + 64-bit deltas | **~9,000–11,000** (of 14,292) |
| **Block loop / cache** | `tryBlockStep`, no cache | `jit.cpp` block cache, `pfnJitCode` slot, `wasmTable` free, lazy-flag init/teardown | **~1,200** (mostly reuse) |
| **Merging** | — | `wasmModuleMerger.*` | **290 (0 new)** |
| **Batch policy** | — | `wasmJitBatchPolicy.*` | **282 (0 new)** |
| **Cache pipeline (JS)** | — | optional, phase 5 | **~3,700** |
| **Inline TLB / `KMemoryData`** | `KMemory64`, no shadow TLB | new `KMemoryData`-equivalent, 64-bit page maps | **~500** |
| **Host helpers** | none | ~12 imported C++ helpers (`readMem/writeMem/emulateSingleOp/getNextOp/syncToHost/…`) incl. `#[no_mangle]`/`EM_JS` glue | **~400** |
| **Interface 64-bit lift** | `jit.h` 8 `BOXEDWINE_64` refs | widen `RegPtr`/`DYN_PTR_SIZE`/`U32 value` immediates to 64-bit where required; add missing virt hooks | **~800–1,500** |

### 3.2 The three riskiest 32→64 deltas

1. **Pointer width — the dominant risk.** The wasm backend's entire value model is `WasmType::I32`
   (`cpu_ptr i32`, `RegPtr` = local index, `emitI32Const`). `WASM_I64_SCRATCH` is the *only* i64 in the
   design, used solely for `imul` overflow tracking — 64-bit values are never materialised. `cpu64`'s
   guest state is 64-bit end to end: `U64` regs, `U64` effective addresses, `fetchQword`. Porting means
   `RegPtr`/locals become i64 across ~250 overrides, every `emitI32*` pair becomes `emitI64*`, and the
   8-GP-local model must become 16 GPRs. A genuine *wasm32 memory64* build would fix this at the cost of
   dropping every engine/browser that lacks memory64; the pragmatic route is **wasm32 + i64 value
   semantics + a 64-bit shadow TLB**, which must be proven correct before anything else moves.
2. **Inline TLB vs. shadow paging.** PR #151 inlines a fast path over `KMemoryData` page→RAM-offset
   arrays sized for 32-bit guest space. `cpu64` uses `KMemory64` with real 64-bit guest address spaces and
   a different page representation. Either mirror the array layout (cost: 2× pages, and correctness
   obligations on every mmap/munmap/prot path — the PR already carries a 148/-46 `kmemory.cpp` and
   64/-3 `kprocess.cpp` delta) or fall back to helper-only memory, which forfeits most of the win.
3. **Relocations / re-activation.** The design deliberately uses *parameters* (`WASM_RELOC_LOCAL`)
   rather than a module global, because nested guest execution (signal delivery inside a helper,
   scheduler switch inside a blocking syscall) must not clobber an outer activation. In `cpu64`,
   64-bit guest pointers widen the relocation payload and any `pfnJitCode`-slot bookkeeping; multi-threaded
   emscripten shares one `wasmTable` across workers, so table-index ownership and `cancelMemory`
   invalidation become load-bearing. Getting this wrong is silent memory corruption, not a crash.

Secondary (non-blocking but real): signed/unsigned widening in the already-landed
`IfLessThan(..., isSigned)` interface; SIMD `V128` locals are only *reserved* upstream (MMX/SSE will
fall back to `emulateSingleOp`); branch hints are emitted but explicitly "not actually doing anything
useful yet".

---

## 4. Phased milestones (each with a measurable gate)

**Phase 0 — Baseline & scaffolding (no codegen).** Build with `BOXEDWINE_WASM_JIT` off/on; land
`wasmEmitter.*` + a unit test that emits a module and validates it via the vendored Binaryen.
*Gate:* emitter test passes; `cpu64DumpOpProfile` run on Cinebench records the interpreter baseline
(instructions/sec) written to `docs/` for later speedup claims.

**Phase 1 — 64-bit value model.** Widen `RegPtr`/locals/params to i64, 8→16 GPR locals, add
`emitI64*` load/store/arith; prove correctness with a *non-optimizing* mode where every guest op still
goes through `emulateSingleOp`, so only the state plumbing is new.
*Gate:* full `source/test/cpu/*` suite green **under the WASM build** (an arithmetic fixture:
`testAdd/testSub/testShift/testCmp/testMov/testXchg`), and end-to-end `Wasm64RunElf` on a native fixture
binary reproduces the `cpu64` interpreter's result bit-for-bit.

**Phase 2 — Arithmetic codegen + inline TLB.** Implement the `add/sub/and/or/xor/shl/shr/sar/neg/not/mov`
backend overrides, plus the `KMemoryData`-equivalent inline TLB fast path and the imported read/write
helpers.
*Gate:* **native fixture probe passes** (arithmetic+memory fixture matches interpreter, byte-exact) **AND
measured speedup >1.5× on that same fixture vs. the Phase 0 baseline.** This is the go/no-go for the
whole project — if the inline TLB doesn't pay off, stop here rather than proceeding.

**Phase 3 — Flags, branches, control flow.** Lazy-flag plumbing, `fillFlags`, `getCondition`,
`IfCondition`, `blockNext1/2`, `blockExit`, `jumpEip`; port `getJumpConditionFromOp` 64-bit targets.
*Gate:* `testJmp/testLoop/testSet/testCMov/testCmp` green; control-flow-heavy fixture matches interpreter;
speedup >2× on Phase-2 fixture retained (no regression from flag materialisation).

**Phase 4 — Block cache, invalidation, merging.** `jit.cpp` block cache on `BBlock`, `pfnJitCode` slot,
`wasmTable` alloc/free, `WasmJitBatchPolicy` (start with only `BlockCount`/`ByteCount` reasons) and
`wasmModuleMerger` standalone mode.
*Gate:* long-running fixture shows cache hit ratio >90% after warmup, memory growth flat over 10 min
(no table leak), and `cancelMemory` correctly invalidates across an `mmap`/`munmap` fixture.

**Phase 5 — Full op coverage.** `mul/div/imul/shld/shrd/rcl/rcr`, `setcc`/`cmov`, string ops, `push/pop`,
segments, FPU via shared `JitFPU`, then MMX/SSE (V128 locals — expect `emulateSingleOp` fallback first).
*Gate:* full test suite green; **Cinebench end-to-end speedup ≥3× vs. interpreter**; no op falls back to
`emulateSingleOp` more than 5% of dynamic retired instructions (measured by the existing
`BOXEDWINE_WASM_JIT_PROFILE` per-helper counters).

**Phase 6 — Offline cache pipeline (optional).** `boxedwine-wasm-jit-cache-pipeline.mjs` `--flat` and
piped modes, AOT zip export, IndexedDB persistence, `binaryen_js.js` vendoring, `testWasmJitBatch` +
`testWasmJitModuleBroker`.
*Gate:* recorded zip replayed into a second run reproduces identical output **and** measurably beats the
online-compiled cache; `-cache-verify-merged` passes on a merged zip.

**Phase 7 — Multi-threaded.** Share `wasmTable` across emscripten workers; per-activation relocation
safety under signal/scheduler nesting.
*Gate:* `multiThreadedJit` build passes a threaded fixture with relocation params verified distinct
across concurrent activations (assert via instrumentation, not by inspection).

---

## 5. Open questions for the requester

- Is the target a **wasm32 + i64-semantics** build (portable, more work) or a **wasm memory64** build
  (simpler codegen, narrow browser support)? This decision alone swings Phase 1 by ~1,000 LOC and is the
  precondition for everything else.
- Should we build on `cpu64`'s `BBlock`/`BRec` IR (deviates from `DecodedOp`, smaller backend) or force
  `cpu64` onto `DecodedOp` (maximal PR reuse, larger Phase-1 bridge)? Recommend the former with a thin
  adapter.
- Is `patch.diff` already-applied work intended to stay, or was it staged for a different effort?
- Are the PR's new tests (`testWasmJitBatch`, `testWasmJitModuleBroker`) to be ported as-is, or
  rewritten against `cpu64`?


## 6. Decoder/cache framework status (committed on `lane-jit`)

The earlier lane called this framework "Phase 1". That label is separate
from the original Phase 1 in §4, which requires a wasm 64-bit value model
and its full execution gates. Completing the decoder/cache framework did
not complete that original milestone. See §7 for the current verified state.

**Scope delivered.** JIT decoder/cache framework for `CPU64`: a self-contained
x86-64 decoder + classifier (`Jit64Op` IR), a block cache keyed on
`startRip` + page generations, and a runtime flag — all behind `BW64_JIT=1`,
default behavior unchanged. CPU64 does not dispatch emitted wasm yet: a
"compiled block" in this framework is a validated + ranked copy of the decode plan with a per-record
fast/fallback dispatch plan. `jit64EmitPlanText()` serializes that plan in a
stable text form the phase-2 wasm emitter will consume.

**Files (all in the `lane-jit` worktree):**

| File | Role |
| --- | --- |
| `include/jit64.h` | Public API: `Jit64Op`/`Jit64Block`, `jit64DecodeOne`, `jit64Classify`, `jit64CompileStream`, `Jit64BlockCache`, `jit64Enabled()`, `jit64EmitPlanText` |
| `source/emulation/cpu/jit64.cpp` | Decoder (prefix/REX/ModRM/SIB scan), classifier, stream compiler, 512-slot open-addressed block cache |
| `source/emulation/cpu/tests/jit64_tests.cpp` | Native unit tests (`g++ -std=c++17 -DBOXEDWINE_GUEST_X64=1`); **113 passed, 0 failed** |
| `source/emulation/cpu/tests/profile_opcodes.py` | Profile evidence generator (capstone; 64-bit PEs only) |
| `scripts/build-jit-wasm.sh` | Native, emitted-module, and wasm execution gates; verified with the SDK at `/opt/emsdk` (see §7) |
| `include/cpu64.h` / `source/emulation/cpu/cpu64.cpp` | `CPU64::tryJitStep()` hook wired into `run()`/`runBounded()` ahead of `tryBlockStep()`; `Jit64State` owned per-CPU |
| `project/emscripten/makefile` | `EMULATION_SOURCES` prunes `source/emulation/cpu/tests/` (own `main()`) from the emcc glob; `jit64.cpp` auto-globbed, `BOXEDWINE_GUEST_X64`-gated |

**Design (per §3.1 "recommend the former with a thin adapter").** Built on
`cpu64`'s own `BBlock`/`BRec` IR, not the 32-bit `DecodedOp` pipeline.
`tryJitStep()` compiles the JIT plan for the block at RIP, then **validates it
against the interpreter's `buildBlock`** (record count + every instruction
length must agree — decoder-drift guard) and executes via the interpreter's
`execBlock`, so semantics are bit-identical by construction. **Whole-block
fallback rule:** if any record in the block classifies fallback (LOCK/REP/
`66h` prefixes, 8-bit ALU forms, SYSCALL, unknown opcodes), the entire block
stays on the interpreter path. Cache validity reuses the `BOXEDWINE_BLOCK_EXEC`
page-generation contract (`blockPageRegister`/`blockPageGenOf`).

**Top-20 opcode profile (evidence).** Static disassembly of 83,376
instructions from real x86-64 guest binaries in this repo —
`tools/rootfs64/games/doom.exe` (59.9k), `tetris`/`snake`, the `gltest` suite
(`glcube`/`gltri`/`msgloop`/`sleepprobe`), `tools/dxvk/tri9/tri9.exe` — via
`profile_opcodes.py` (capstone linear sweep; opcode counted *after*
legacy/REX prefixes; NOP padding `90`/`0F1F` — 8,384 insns — excluded from
ranking; PUSH/POP register rows and `MOV r,imm` grouped as classes
`50..57`/`58..5F`/`B8..BF`). `HxD64.exe` was excluded: its `.text` is
dominated by zero padding / non-code patterns a linear sweep cannot
distinguish (26% decoded as opcode `00`). doom.exe alone yields 59.9k insns
vs the 62.4k cited in the original analysis, corroborating the method.
Reproduced 2026-10-05 on the lane host (capstone 5.0.7).

Top-20 opcodes in the JIT fast set (share of all 83,376 instructions):

| # | opcode | share | # | opcode | share |
| --- | --- | --- | --- | --- | --- |
| 1 | `89` MOV r/m,r | 11.2% | 11 | `C1` SHIFT r/m,imm8 | 2.3% |
| 2 | `8B` MOV r,r/m | 10.1% | 12 | `E9` JMP rel32 | 2.2% |
| 3 | `83` ALU r/m,imm8 | 6.7% | 13 | `74` JZ rel8 | 2.0% |
| 4 | `E8` CALL rel32 | 6.0% | 14 | `39` CMP r/m,r | 2.0% |
| 5 | `8D` LEA | 5.4% | 15 | `0FB6` MOVZX r,r/m8 | 1.9% |
| 6 | `58..5F` POP r64 | 3.6% | 16 | `C7` MOV r/m,imm32 | 1.8% |
| 7 | `B8..BF` MOV r,imm | 3.1% | 17 | `75` JNZ rel8 | 1.7% |
| 8 | `85` TEST r/m,r | 3.0% | 18 | `C3` RET | 1.7% |
| 9 | `50..57` PUSH r64 | 2.8% | 19 | `0F84` JZ rel32 | 1.2% |
| 10 | `31` XOR r/m,r | 2.4% | 20 | `01` ADD r/m,r | 1.2% |

The phase-1 fast set covers **80.2% of padding-excluded static
instructions** (top-20 rows above: 72.2% of all 83,376 instructions).
By raw byte rank, positions 19–20 are `FF` (INC/DEC/indirect call, 1.7%) and
8-bit `00` (1.4%), which are correctly *not* in the fast set; `0F84` and `01`
are the next-fastest fast opcodes, hence table rows 19/20. `0F85`/`29`
remain fast in the classifier but fell just outside this corpus's top-20, so
they carry no coverage-table row. The authoritative table is
`jit64CoveredOpcodes()` in `source/emulation/cpu/jit64.cpp`; the unit test
asserts its rank order against this profile.

**Verification (all on the lane host):**

```
g++ -std=c++17 -Wall -Wextra -DBOXEDWINE_GUEST_X64=1 -Iinclude \
    source/emulation/cpu/jit64.cpp source/emulation/cpu/tests/jit64_tests.cpp \
    -o /tmp/jit64test/jit64_tests && /tmp/jit64test/jit64_tests
# -> 113 passed, 0 failed, zero warnings
python3 source/emulation/cpu/tests/profile_opcodes.py <64-bit exe>...
```

**Still deferred:** broader wasm opcode emission, runtime module
instantiation/table-index glue, the inline-TLB fast path, and interpreter/JIT
timing gates on the selftest/runelf fixtures. The initial standalone MOV
emitter and verified execution gates are described below.

## 7. Verified continuation, 2026-10-05

The build driver now runs the real JIT framework under Emscripten 6.0.9
and Node 22.22.3. Earlier driver settings omitted `BOXEDWINE_BLOCK_EXEC`
and `BOXEDWINE_BLOCK_CACHE_INFRA`; with those settings, `BW64_JIT=1`
could pass an output comparison while the JIT never ran. The corrected
driver uses an isolated build directory, enables both macros, limits make
to two jobs by default, handles the repository's ESM/CommonJS boundary,
and forwards only the two JIT test controls into the guest environment.
The JIT run must report nonzero executed blocks and instructions.

Two execution regressions were demonstrated failing before their fixes:
`runBounded(1)` ran extra records from a decoded block, and a store patching
a later instruction in the same block still ran the old immediate. Block
execution now honors the remaining instruction budget and rechecks page
generations after each record. The full wasm smoke suite passes in both
modes: **258 passed, 0 failed**, with **30 JIT blocks / 76 instructions**
observed in the shared program cases. Test output matches byte-for-byte
after removing the one JIT statistics line. The native decoder/cache suite
also passes **113/113**. This is correctness evidence, not a speedup result.

`include/jit64wasm.h` and `source/emulation/cpu/jit64wasm.cpp` add the first
standalone binary emitter. Its `execute(state_ptr: i32)` imports
`env.memory`; its staging ABI contains sixteen i64 GPRs, an i64 guest RIP,
and preserved flags. The ABI is separate from CPU64's C++ object layout.
The supported subset is register-direct MOV and immediate MOV at 32/64-bit
operand widths. The emitter rejects the entire block for memory operands,
MOVZX, flag-changing operations, unsupported prefixes, and other kinds.
CPU64 still executes through `execBlock`; it does not instantiate or call
these modules yet. No wasm table ownership or threaded imports are installed.

The emitted modules pass **22 Node validation/execution tests**, covering
all sixteen registers, high halves, signed LEB boundary values, both
register MOV encodings, 32-bit zero extension, C7 sign extension, RIP
wraparound, flags preservation, and whole-block rejection. The RED emitter
stub produced 15 positive-case failures; the implemented emitter passes
all 22. The binary encoding uses the baseline instruction forms in the
[WebAssembly instruction specification](https://webassembly.github.io/spec/core/binary/instructions.html).

Run the gates with:

```sh
source /opt/emsdk/emsdk_env.sh
JOBS=2 BUILD_TMP=/tmp/bw64-jit-build bash scripts/build-jit-wasm.sh
# --selftest-only runs native decoder tests and standalone wasm-module tests
# without rebuilding the Emscripten host.
```

Progress against the **original** milestones is estimated by delivered
components, not elapsed time. None of the original milestone gates is
complete yet:

| Original milestone | Estimate | Remaining gate / major missing component |
| --- | ---: | --- |
| 0: baseline and scaffolding | ~50% | Cinebench baseline, original on/off configuration gate, and vendored Binaryen validation |
| 1: 64-bit value model | ~30% | State bridge API + tests landed; still missing runtime instantiation glue, full wasm arithmetic suites and ELF equivalence |
| 2: arithmetic and inline TLB | ~20% | Register ALU codegen landed; still missing guest-memory codegen, helpers/TLB and measured >1.5× fixture speedup |
| 3: flags and control flow | ~10% | Eager ALU flags landed and interpreter-exact; still missing branches/control flow and the original speed gates |
| 4: cache, invalidation and merging | ~15% | Wasm table ownership, module merging/batch policy and sustained hit/leak/invalidation gates |
| 5: full op coverage | ~10% | ALU+MOV covered; still missing shifts, IMUL, string ops, FPU/SIMD and the Cinebench/dynamic-fallback gates |
| 6: offline cache pipeline | 0% | Recording, persistence, replay and merged-cache validation |
| 7: multithreaded JIT | 0% | Shared table/activation ownership and concurrent relocation fixture |

## 8. Verified continuation, 2026-10-05 (ALU + state bridge)

`include/jit64wasm.h` gains the dependency-free state bridge
`jit64WasmStateInit`/`jit64WasmStateRead` (plain GPR/RIP/RFLAGS triple <->
staging struct; `cpu64.cpp` will pass `cpu->reg[i].u64` / `cpu->rip` /
`cpu->rflags` once the runtime instantiation glue exists). Native harness:
**151 passed, 0 failed** (113 phase-1 + 38 new marshal checks), zero
warnings; `scripts/build-jit-wasm.sh` links `jit64wasm.cpp` into the gate.

The emitter now covers register-direct ALU: ADD/ADC/SUB/SBB/AND/OR/XOR/CMP/
TEST (`/0`..`/7`, r/m-r, r-r/m, r/m-imm and acc-imm forms, 32/64-bit) with
eager, interpreter-exact flag computation. Flags mirror `cpu64.cpp`
`flagsAdd`/`flagsSub`/`flagsLogic` bit-for-bit (CF/OF/AF formulas, SZP on the
masked result, AF cleared for logic ops, non-written RFLAGS bits preserved);
a new i32 RFLAGS local is loaded/stored around the existing GPR/RIP traffic.
Two emitter bugs were demonstrated failing before their fixes: `orFlagBit`
used `i64.and` (0x83) instead of `i64.or` (0x84), and the ALU result was
stored straight to guest memory where the final store-back clobbered it with
the stale GPR local (results now publish through the GPR local, so later ops
in the block observe them). MOVZX stays rejected via an explicit `sub != 0`
check on the MOV kinds.

Node suite: **43 passed, 0 failed** (26 previous + 17 new), including a JS
reference model of the interpreter flag formulas and 15 ALU execution tests
(operand directions, imm8/imm32/acc forms, 32-bit zero-extension + 32-bit
flags, CMP/TEST writing no register, ADC/SBB carry-in, extended registers,
RFLAGS preservation, mixed MOV+ALU blocks). Whole-block rejection still pins
the boundaries: empty blocks, guest-memory operands, MOVZX, 16-bit operand
size, REP prefix, shifts, IMUL, >24 ops.

Still deferred: runtime module instantiation/table-index glue
(`boxedwine_wasm_instantiate_runtime_batch`), the inline-TLB fast path,
shifts/IMUL/string-op/FPU emission, and interpreter/JIT timing gates.

## 9. Verified continuation, 2026-10-05 (table-index glue + tryJitStep wiring)

The runtime instantiation glue is implemented as a single-module
`boxedwine_wasm_instantiate_runtime_batch`-shaped API in `include/jit64wasm.h`
(batch/merge remain phase-4 work): `jit64WasmInstantiate(bytes, len)` returns
a table index (>= 0) or -1; `jit64WasmCall(index, statePtr)` invokes the
table entry's `execute(statePtr)`; `jit64WasmRelease(index)` frees a slot for
reuse. Under `__EMSCRIPTEN__` these are real declarations implemented with
synchronous `WebAssembly.Module`/`WebAssembly.Instance` in the new
`source/emulation/cpu/jit64wasm_runtime.cpp` (EM_JS; imports only
`env.memory`, satisfied with the host's own `wasmMemory`, so the state
pointer is an ordinary wasm32 linear-memory address). Natively they are
inline no-ops (`instantiate` returns -1, `jit64WasmAvailable()` is false), so
`cpu64.cpp` and the section-GC lifetime test link without the runtime TU.

A live emscripten probe (hand-built `execute` module storing `0x34` to
`gpr[0]`) verified the full path: instantiate -> index 0, call writes
through the imported memory to the host `Jit64WasmState`, release + re-
instantiate reuses slot 0, malformed bytes return -1. The probe caught one
real bug: `Module.HEAPU8` is not exported by default (`EXPORTED_RUNTIME_METHODS`
abort) — the glue now copies via `wasmMemory.buffer` instead, no build-flag
change needed.

`CPU64::tryJitStep()` (cpu64.cpp/h) now prefers the wasm vehicle on cache
hits when the whole block fits the caller's budget (`maxInsn >= n`, since the
module always runs the full block and advances RIP): marshal GPRs/RIP/RFLAGS
via `jit64WasmStateInit`, `jit64WasmCall`, `jit64WasmStateRead`, unmarshal
back. Emission rejection is cached (`wasmIndex == -2`, never retried);
transient instantiate failures fall through to `execBlock` and retry next
entry. Soundness: emitted blocks are register-only (the emitter rejects any
memory operand), so the page-generation check at cache lookup suffices — no
mid-block invalidation rechecks. Everything is behind `BW64_JIT`; the
interpreter stays the fallback. `Jit64Block::wasmIndex` (-1 untried / >= 0
live / -2 rejected) is owned by `Jit64BlockCache`: slot overwrite,
`invalidate()` and `clear()` release live modules. New `Jit64State::wasmBlocks`
/`wasmInsns` counters; the selftest prints a `WASM64:` sentinel under
`BW64_JIT_VERIFY=1` (fails if zero) and the driver strips it from the
interpreter-vs-JIT diff like the existing `JIT64:` line.

Native harness: **159 passed, 0 failed** (151 + 8 new glue/cache-lifecycle
tests), zero warnings. `test-jit-lifetime.sh` passes in both modes.
`scripts/build-jit-wasm.sh` runs the emscripten selftest with
`BW64_JIT=1 BW64_JIT_VERIFY=1` and asserts a nonzero `WASM64:` sentinel; the
`BW64_JIT=1` smoke must stay byte-identical to the interpreter baseline.

## 10. Verified continuation, 2026-10-05 (SHL/SHR/SAR emission)

The emitter now covers register-direct SHL/SHR/SAR (`0xC1` `/4`, `/5`,
`/7`; `/6` aliases `/4`; 32/64-bit) with eager, interpreter-exact flags
mirroring `cpu64.cpp` `doShift`: count == 0 is a pure no-op (value and all
flags untouched, nothing emitted); otherwise CF is the last bit shifted
out, OF is defined only for count == 1 (SHL: MSB(result) XOR CF; SHR: old
MSB; SAR: 0), and SZP is set from the result. AF is NOT touched by shifts
(shift merge mask `0x8C5`, unlike the ALU `0x8D5`), and all other RFLAGS
bits are preserved. Rotates (ROL/ROR/RCL/RCR, `/0`..`/3`) stay explicitly
rejected, as do memory shifts; the whole-block rejection boundary is
unchanged otherwise.

Two emitter bugs were demonstrated failing before their fixes:
`i64.shr_s` (`0x87`) and `i64.shr_u` (`0x88`) were swapped throughout
(validation passes either way, semantics silently wrong — the pre-existing
PF fold in `emitFlags` had the same latent swap, benign there since the
folded value is < 256); and the SAR 32-bit path pushed the shift amount
before sign-extension, scrambling the operand stack (`shr_s` shifted by
the sign-extended value instead of the count).

Native harness: **173 passed, 0 failed** (159 + 14 new emission
accept/reject checks), zero warnings. Node suite: **58 passed, 0 failed**
(43 + 15 new), including a JS reference model of `doShift` and execution
tests covering CF/OF edges, 32-bit width + zero-extension, count-0 no-op,
AF preservation, `/6` alias, extended registers, and a mixed shift+ALU
block. Selftest **258 passed, 0 failed**; `BW64_JIT=1` smoke stays
byte-identical with `WASM64: blocks=21 insns=49` (was 20/47 — shift ops in
the smoke workload now jitted).

Still deferred: FPU emission -- now decided: x87 stays interpreter-only by design (see section 12).

### 2026-10-05: rotates + IMUL (d853f18)

Rotate emission (`emitRotate`, 0xC1 /0../3, 32/64-bit): ROL/ROR via
single-rotl/rotr; RCL/RCR via a wasm `block`/`loop` with loop-carried
carry in L_P and counter in L_B. Flags: CF = last bit rotated out; OF
only for count==1 (ROL/RCL: MSB^CF; ROR/RCR: MSB^MSB<<1); SZP/AF
untouched (interpreter's `doShift` skips `setSZP` for rotates); count==0
is a flag no-op (32-bit still zero-extends the destination, matching
the interpreter's `setU32`). OF is *preserved* (not cleared) when
count!=1, via a count-dependent merge mask (0x801 for count==1, 0x001
otherwise).

IMUL emission (`emitImul`, two-operand 0F AF, register-direct only):
sign-extended operands; 32-bit uses exact 64-bit product with overflow
iff `res != sext32(res)`; 64-bit uses a 128-bit product via 32-bit limbs
(`hi_u = t3 + s_hi + m_hi`, provably no intermediate wrap), signed
correction `signed_hi = hi_u - a_s*b - b_s*a (mod 2^64)`, overflow iff
`signed_hi != shr_s(lo,63)`. CF|OF both set on overflow, both cleared
otherwise; all other flags preserved. One-operand (F6/F7 /5) and
three-operand (69/6B) IMUL stay interpreter-only.

Three emitter bugs were demonstrated failing before their fixes via
differential fuzzing against JS reference models of the interpreter
formulas (640 cases: values + flags): (1) 64-bit IMUL overflow flag
never set — `i64.and` used instead of `i64.mul` to scale the 0/1
overflow bit into the 0x801 mask (`1 & 0x801 == 1`); (2) rotate OF
cleared instead of preserved for count!=1 (fixed via the
count-dependent mask); (3) 32-bit shift/rotate-by-0 did not zero-extend
the destination (emitter left the high 32 bits intact; interpreter
does `setU32`).

Native harness: **182 passed, 0 failed** (173 + 9 new emission
accept/reject checks). Node suite: **68 passed, 0 failed** (58 + 10
new), including JS reference models of `doShift` rotates and
`BK_IMUL_R_RM` and execution tests covering CF/OF edges, RCL/RCR carry
threading, 32-bit width, count-0 no-op, AF/SZP preservation, and a mixed
imul+rol block. Selftest **258 passed, 0 failed**; `BW64_JIT=1` smoke
stays byte-identical with `WASM64: blocks=21 insns=49`.

Per-block instantiation overhead (Node v22, 200 iterations, 1-op module):
`WebAssembly.compile` 40 us + `instantiate` 8 us = **48 us per block**,
vs `execute()` 162 ns per call (~300x). Instantiation amortizes after
~300 executions of a cached block; cold/single-shot blocks are a net
loss. The block cache already amortizes this for hot code; phase-4
batching/merging is worth it if the cold-start path matters.

## 11. Design: string-op emission (memory-touching), 2026-10-05

String ops (MOVS/STOS/CMPS/SCAS, A4-A7/AA/AB/AE/AF) are the first emitter
group that touches guest memory. Design decisions, made before emitting:

**Memory access path: imported host helpers, not raw linear memory.**
The emitted module gains two function imports alongside `env.memory`:
`env.memRead(addrLo:i32, addrHi:i32, size:i32, retPtr:i32)` and
`env.memWrite(addrLo:i32, addrHi:i32, size:i32, valLo:i32, valHi:i32)`.
All params are i32 (no BigInt dependence); the 64-bit read result is
written to 8 bytes at `retPtr` (a new `U64 memScratch` at struct offset
144; struct grows 144 -> 152). The JS glue calls exported C++
`jit64MemRead`/`jit64MemWrite`, which dispatch to the exact same
`KMemory64::readb/readw/readd/readq` and `writeb/writew/writed/writeq`
the interpreter uses. Fault/commit semantics are therefore identical by
construction: unmapped reads return 0, writes commit on demand, and the
write slow path calls `noteGuestWrite` (block pages are barred from the
DTLB fast path, so code-page writes always bump generations).

**Routing to the right KMemory64:** a `thread_local KMemory64*` set by
`CPU64::tryWasmExec` around `jit64WasmCall` (wasm execution is fully
synchronous; the module cannot re-enter the CPU, so the thread-local is
always correct when an import runs). Chosen over a token table or a
second `execute()` param because a 64-bit host pointer cannot cross the
JS boundary without precision loss, and the thread-local needs no ABI
change to the instantiate path.

**Soundness vs the "register-only" argument (§9):** emitted blocks are no
longer register-only. The within-block staleness this introduces (a
string op writing the blocks own code page, later ops in the same
## 11. Design: string-op emission (memory-touching), 2026-10-05

String ops (MOVS/STOS/CMPS/SCAS, A4-A7/AA/AB/AE/AF) are the first emitter
group that touches guest memory. Design decisions, made before emitting:

**Memory access path: imported host helpers, not raw linear memory.**
The emitted module gains two function imports alongside `env.memory`:
`env.memRead(addrLo:i32, addrHi:i32, size:i32, retPtr:i32)` and
`env.memWrite(addrLo:i32, addrHi:i32, size:i32, valLo:i32, valHi:i32)`.
All params are i32 (no BigInt dependence); the 64-bit read result is
written to 8 bytes at `retPtr` (a new `U64 memScratch` at struct offset
144; struct grows 144 -> 152). The JS glue calls exported C++
`jit64MemRead`/`jit64MemWrite`, which dispatch to the exact same
`KMemory64::readb/readw/readd/readq` and `writeb/writew/writed/writeq`
the interpreter uses. Fault/commit semantics are therefore identical by
construction: unmapped reads return 0, writes commit on demand, and the
write slow path calls `noteGuestWrite` (block pages are barred from the
DTLB fast path, so code-page writes always bump generations).

**Routing to the right KMemory64:** a `thread_local KMemory64*` set by
`CPU64::tryWasmExec` around `jit64WasmCall` (wasm execution is fully
synchronous; the module cannot re-enter the CPU, so the thread-local is
always correct when an import runs). Chosen over a token table or a
second `execute()` param because a 64-bit host pointer cannot cross the
JS boundary without precision loss, and the thread-local needs no ABI
change to the instantiate path.

**Soundness vs the "register-only" argument (section 9):** emitted blocks
are no longer register-only. The within-block staleness this introduces
(a string op writing the block's own code page, later ops in the same
module executing the pre-write snapshot) is exactly the staleness the
interpreter's `execBlock` already accepts (it executes pre-decoded
records without per-record rechecks). The generation check at block
entry plus `noteGuestWrite` on the write path (next entry rebuilds)
preserves the existing invalidation contract. No new invalidation
machinery is needed.

**Interpreter equivalence:** `dsp_43`/`dsp_44` are factored into
`CPU64::runStringOp(sub, size, rep, asize32)`, called from the
single-step dispatcher AND from `execBlock`'s new `BK_STRING` case.
Single source of truth; the 258-test selftest guards the refactor.
`buildBlock` learns `BK_STRING` (string ops are exempted from the
REP/0x66 break; LOCK+string still breaks, matching the JIT decoder's
rejection). The `tryJitStep` cross-check (`bb.n == n`, length agreement)
then works for string-op blocks.

**Decoder:** new `J64_STRING` kind (sub 0=MOVS,1=STOS,2=CMPS,3=SCAS;
size 1/2/4; new `rep` and `asize32` fields on `Jit64Op`). A4-A7, AA, AB,
AE, AF decoded; A8/A9 (TEST) and AC/AD (LODS, unimplemented by the
interpreter) excluded; LOCK excluded (conservative: falls back to the
interpreter, which ignores it). Segment prefixes ignored, matching the
interpreter (dsp_43/dsp_44 never consult p.seg). REX.W ignored for size
(string ops use opSize only).

**Flags:** CMPS/SCAS reuse the verified `emitFlags` SUB path
(sub=5, sb per width). MOVS/STOS touch no flags. DF (bit 10) selects
plus/minus size step; RSI/RDI update is full 64-bit wrapping (NOT masked
under 0x67 -- only the effective address is masked), matching the
interpreter.

**Performance note:** per-iteration host calls (2 per MOVS iteration)
are slower than the interpreter's C++ loop for huge REPs. Correctness
first; a bulk memCopy helper is the obvious follow-up if timing gates
flag it. The 48us instantiation cost (section 10) dominates for cold
blocks anyway.

### 2026-10-05: 8-bit ALU, 16-bit forms, MOVZX/MOVSX, 1-op/3-op IMUL

8-bit ALU emission (00-3D forms 0/2/4, 80 /0../7, 84/85, A8/A9, C0 /4../7):
exact partial-register semantics via `byteRegParts`/`emitReadByte`/
`emitWriteByte`/`emitReadWord`/`emitWriteWord`. The REX rule is critical:
without REX, indices 4..7 are AH/BH/CH/DH (phys-4, shift 8); with any REX
they are SPL/BPL/SIL/DIL (low bytes). Transcribed from the interpreter's
`readReg8`/`writeReg8`. 8-bit writes do inline RMW and return 0xFF.

16-bit forms (0x66 prefix): ALU/shift/rotate with 16-bit flag widths.
`setU16` preserves the upper 48 bits (RMW via `emitWriteWord`). The
0x66 prefix is ignored for 8-bit forms (matches interpreter).

MOVZX/MOVSX emission (`emitMovx`, 0F B6/B7/BE/BF): 8->16/32/64 and
16->32/64. Flags untouched (interpreter leaves them). 16-bit dest does
RMW; 32-bit zero-extends; 64-bit writes full.

One-operand IMUL (`emitImul1`, F6/F7 /5): rDX:rAX (EDX:EAX, DX:AX, AX
for smaller). 8-bit writes AX (interpreter `setU16`). 64-bit uses the
32-bit limb method with sign correction; the signed hi is saved to L_T1
before the overflow check (it was consumed by `i64.ne`). CF/OF set iff
the high half doesn't match sign-extension of the low half.

Three-operand IMUL (`emitImul3`, 69/6B): r = sext(r/m) * sext(imm).
6B takes imm8 (sign-extended); 69 takes imm16 under 0x66 else imm32.
Overflow iff the full product doesn't fit the width.

Bugs caught pre-commit via differential fuzzing (1440 cases):
- 6B operand size: used imm8 width as operand size (should be opSize).
- 64-bit IMUL1: stray `[sext]` left on stack (validation error).
- 64-bit IMUL1: wrote hi to local 4 (RSP) not 3 (RDX).
- 64-bit IMUL1: used hi_u not signed_hi for RDX (L_P holds unsigned).
- Test bugs: wrong srcSize for MOVX, wrong GPR index for RDX (3 vs 2),
  8-bit opcodes in 16-bit fuzz, missing byteAccess for high-byte sources.

Gates: native 278/0, Node wasm 140/0 (1440 fuzz), selftest 258/0,
smoke byte-identical (WASM64 blocks=20).

### 11.8 Pre-existing i32Const bug (found by string-op testing, 2026-10-05)
i32Const used unsigned LEB128 for the i32.const immediate, but the wasm spec
requires SIGNED LEB128. Values with bit 6 set in the final payload byte decode
as negative (0x40 encodes as one byte 0x40, decodes to -64). Affected all prior
uses with such constants (F_ALU_MASK=0x8D5 in emitFlags, F_SHIFT_MASK, rotMask,
F_CFOF_MASK). Fixed to proper signed LEB128, mirroring i64Const. The string-op
REP early-exit (i32.const 0x40 for ZF) exposed it: the br_if never fired until
the fix. All gates green after the fix (native 212, Node 81 incl. 300-case fuzz).

## 12. Design: x87 FPU stays interpreter-only (decision, 2026-10-05)

**Verdict: no wasm emission for x87. The D8-DF opcodes decode to an explicit
`J64_FPU` kind that always classifies `JIT64_FALLBACK`; `jit64CompileStream`
truncates the block there, so the fast prefix still jits and the x87 op
single-steps on the interpreter. This is a deliberate design boundary, not a
gap: emitting x87 as wasm f64 is provably wrong, and emitting it correctly is
provably pointless.**

### The four design questions, answered

1. **x87 register-stack model.** The interpreter keeps `FPU::regs[9]` as
   `extFloat80_t` (80-bit extended, explicit integer bit), plus a
   `regCache[9]` double fast-path, `isRegCached[9]`, `tags[9]`, `top`,
   `cw`, `sw`, `round` (`source/emulation/cpu/common/fpu.{h,cpp}`). Any
   wasm model would have to shadow all of it (the dual representation is
   load-bearing: `FLD_I64` never populates the cache, `FCHS` has a
   zero special-case on the cached path, compares read the double view).

2. **Rounding mode.** `KSystem::useF64 = false` for `BOXEDWINE_GUEST_X64`
   (`source/kernel/ksystem.cpp:146`); the comment there is explicit: the
   64-bit guest mandates the full SoftFloat 80-bit path (glibc
   `__printf_fp` spills true `long double` via `FSTP m80`). Arithmetic
   (`FADD/FSUB/FMUL/FDIV` and the R-variants) is `extF80_add/sub/mul/div`
   under `softfloat_roundingMode` from CW bits 10-11. wasm `f64.add` is
   round-to-nearest-even on 53-bit mantissas -- a different operation.

3. **Precision control / status word / tags.** PC bits are honored inside
   the softfloat conversions; C0/C1/C2/C3 are written by compares
   (`cpu64_fpu_set_c012_3`), `FCHS`/`FABS` (C1=0), `FTST`, `FXAM`;
   `FNSTSW`/`FNSTCW` expose `sw`/`cw` to the guest. An emitter would have
   to track all of it eagerly -- there is no lazy corner to hide in,
   because the guest can observe the status word after any x87 op.

4. **Bit-exactness.** Disproven for f64 emission by construction (witness
   below). sNaN/qNaN: the interpreter does not distinguish them in
   compares (`std::isnan` on the double view); arithmetic NaN propagation
   follows Berkeley SoftFloat (8086-SSE specialization). Any f64 emitter
   would inherit the host engines NaN propagation instead.

## 12. Design: x87 FPU stays interpreter-only (decision, 2026-10-05)

**Verdict: no wasm emission for x87. The D8-DF opcodes decode to an explicit
`J64_FPU` kind that always classifies `JIT64_FALLBACK`; `jit64CompileStream`
truncates the block there, so the fast prefix still jits and the x87 op
single-steps on the interpreter. This is a deliberate design boundary, not a
gap: emitting x87 as wasm f64 is provably wrong, and emitting it correctly is
provably pointless.**

### The four design questions, answered

1. **x87 register-stack model.** The interpreter keeps `FPU::regs[9]` as
   `extFloat80_t` (80-bit extended, explicit integer bit), plus a
   `regCache[9]` double fast-path, `isRegCached[9]`, `tags[9]`, `top`,
   `cw`, `sw`, `round` (`source/emulation/cpu/common/fpu.{h,cpp}`). Any
   wasm model would have to shadow all of it (the dual representation is
   load-bearing: `FLD_I64` never populates the cache, `FCHS` has a
   zero special-case on the cached path, compares read the double view).

2. **Rounding mode.** `KSystem::useF64 = false` for `BOXEDWINE_GUEST_X64`
   (`source/kernel/ksystem.cpp:146`); the comment there is explicit: the
   64-bit guest mandates the full SoftFloat 80-bit path (glibc
   `__printf_fp` spills true `long double` via `FSTP m80`). Arithmetic
   (`FADD/FSUB/FMUL/FDIV` and the R-variants) is `extF80_add/sub/mul/div`
   under `softfloat_roundingMode` from CW bits 10-11. wasm `f64.add` is
   round-to-nearest-even on 53-bit mantissas -- a different operation.

3. **Precision control / status word / tags.** PC bits are honored inside
   the softfloat conversions; C0/C1/C2/C3 are written by compares
   (`cpu64_fpu_set_c012_3`), `FCHS`/`FABS` (C1=0), `FTST`, `FXAM`;
   `FNSTSW`/`FNSTCW` expose `sw`/`cw` to the guest. An emitter would have
   to track all of it eagerly -- there is no lazy corner to hide in,
   because the guest can observe the status word after any x87 op.

4. **Bit-exactness.** Disproven for f64 emission by construction (witness
   below). sNaN/qNaN: the interpreter does not distinguish them in
   compares (`std::isnan` on the double view); arithmetic NaN propagation
   follows Berkeley SoftFloat (8086-SSE specialization). Any f64 emitter
   would inherit the host engine's NaN propagation instead.

### Divergence witness (repo SoftFloat vs wasm f64.add)

`a = 1.0`, `b = 0x1.0000000000002p-53` (= 2^-53 + 2^-70, exactly
representable). True sum S = 1 + 2^-53 + 2^-70 sits 2^-70 above the
double midpoint M = 1 + 2^-53:

- wasm `f64.add`: correctly-rounded direct sum. S > M rounds up to
  `1 + 2^-52` (`0x3ff0000000000001`).
- interpreter: `f64_to_extF80` (exact), `extF80_add` rounds S to the
  64-bit mantissa grid -> lands exactly on M, `extF80_to_f64`
  round-nearest-even on the exact midpoint -> ties-to-even -> `1.0`
  (`0x3ff0000000000000`).

Verified with the repo's own SoftFloat sources (`/tmp/dblround.cpp`
repro): `via80 = 0x3ff0000000000000`, `direct = 0x3ff0000000000001`,
`DIVERGE: YES`. Double rounding is not a theoretical concern; it fires
on ordinary-looking inputs.

### Decision matrix

| Approach | Bit-exact | Faster than interp | Effort |
|---|---|---|---|
| Interpreter-only (chosen) | yes (by construction) | n/a (baseline) | ~0 |
| Host-import softfloat per op | yes | no -- wasm->host call per op is slower than the interpreter's direct C++ call | medium |
| f64 in wasm | **no (witness above)** | yes | medium |
| softfloat-in-wasm | yes | maybe | huge (port ~10k lines of SoftFloat into the emitter) |

### Frequency (capstone linear sweep, 2026-10-05)

doom.exe: 1/59,899 (0.0017%); tetris.exe: 1/3,128 (0.032%);
tri9.exe: 21/8,287 (0.25%, CRT/printf paths). x87 is statistically
negligible in 64-bit code; real FP work is SSE/SSE2 (separate,
still-deferred V128/MXCSR project). There is no hot x87 loop to win.

### Boundary mechanics

- `J64_FPU` (`jit64.h`): decoded from D8-DF via `scanModRM` for exact
  length; `jit64Classify` leaves it `JIT64_FALLBACK` (default arm).
- `jit64CompileStream` truncates at `J64_FPU` (does not include it), so a
  block's fast prefix still jits; the x87 op and everything after run on
  the interpreter's single-step path (`dsp_59`), exactly as before this
  change. `buildBlock`/`execBlock` never see x87 either (both break at
  unsupported opcodes), so the `tryJitStep` cross-check is unaffected.
- The wasm emitter's default arm rejects unknown kinds, so even a future
  accidental inclusion degrades to whole-block rejection, never to silent
  f64 miscompilation. Native test `x87NeverJitted` pins the boundary.


## 13. Design: memory-operand ALU emission, 2026-10-05

Memory-operand ALU (`op [mem], reg` / `op reg, [mem]` / `op [mem], imm`,
sizes 1/2/4/8) is the highest-value remaining emitter group: real code is
dominated by r/m-with-memory forms (the phase-1 profile ranks `89`, `8B`,
`83`, `85`, `31`, `39`, `01` in the top 20). Design decisions, made before
emitting:

### 1. Effective-address computation: decode-time EA description, runtime i64 math

`scanModRM` (jit64.cpp) only computes ModRM/SIB/disp *lengths*. The emitter
needs the full addressing form, so the decoder gains a `decodeEA` helper
that mirrors `CPU64::decodeModRM` (cpu64.cpp:196) field-for-field and fills
a new `Jit64MemEA` struct on `Jit64Op` (only meaningful when `isMem`):

- `ripRel` + `ripRelTarget`: mod=0,rm=5. The target is materialized at
  decode time as `modrmGuestAddr + 5 + trailingImm + (S64)disp32`, where
  `modrmGuestAddr = ripBase + opOff + 1`. In production `ripBase` is the
  real block RIP (tryJitStep); the Node fixture uses ripBase=0, so
  RIP-relative tests must expect the fixture-base target. The interpreter
  quirk is transcribed exactly: RIP-relative returns *before* the 0x67
  mask and *before* FS/GS handling (cpu64.cpp:214-222), so `asize32` and
  `seg` are ignored for RIP-relative.
- `baseReg`/`idxReg` (0-15, 0xFF = none), `scale` (0-3), `disp` (S64,
  sign-extended): SIB decoded per the interpreter -- idxF==4 with no
  REX.X means "no index" (with REX.X it is R12); baseF==5 with mod==0
  means disp32-only (no base). mod=1: disp8; mod=2 (or SIB-no-base):
  disp32. Non-SIB base = rm | REX.B.
- `asize32` (0x67): EA masked to 32 bits *after* base+index+disp wrap
  (mod 2^64), exactly as the interpreter does.
- `seg` (0, 0x64 FS, 0x65 GS): recorded; the emitter rejects seg != 0
  (interpreter-only). ES/CS/SS/DS overrides are no-ops in long mode and
  never recorded (matching `consumePrefixes`).

Runtime EA in wasm (all i64, wrapping): `base + (idx << scale) + disp`,
then `& 0xFFFFFFFF` under 0x67. Base/index come from the GPR locals;
RIP-relative is a single `i64.const`. FS/GS bases live on the CPU object,
not in `Jit64WasmState`, which is why seg-prefixed forms stay rejected.

### 2. Read-modify-write via the string-op helpers; fault ordering

The emitted code calls the existing `env.memRead`/`env.memWrite` imports
(`emitMemRead`/`emitMemWrite`), which reach `KMemory64::readb/w/d/q` and
`writeb/w/d/q` -- the exact accessors the interpreter `loadRM`/`storeRM`
use. Fault/commit semantics are identical by construction:
`KMemory64::readb` returns 0 for unmapped pages (kmemory64.cpp:690, no
throw); writes commit on demand and funnel through `noteGuestWrite`, so
the block-page generation contract from section 11 applies unchanged.

Fault ordering matches the interpreter `dsp_6`/`dsp_7` sequence:
`loadRM` (read) -> `runAlu` (compute + flags) -> `storeRM` (write). The
emitter does memRead -> ALU compute + `emitFlags` -> memWrite in the same
order. Since reads cannot throw, the only observable ordering is
read-before-write, which both paths share. (A write to an unmapped page
commits it -- same as the interpreter.)

### 3. LOCK prefix: stays interpreter-only (existing boundary)

`jit64Classify` already returns FALLBACK for `hasLockOrRep` on non-string
ops, so `lock add/or/and/... [mem], reg` never classifies FAST and the
interpreter keeps its `cpu64AtomicLockFor` serialization (glibc futex /
lock words). No new atomicity machinery in the emitter; the boundary is
unchanged.

### 4. CMP/TEST first, then RMW, then immediate-to-memory

- `J64_TEST_RM_R` (84/85) and CMP (sub==7): read-only. memRead -> flags
  via the AND (TEST) / SUB (CMP) path -> no writeback. Simplest; done first.
- `J64_ALU_RM_R` forms 0/1 (`op [mem], reg`): memRead -> L_A, reg ->
  L_B, compute, memWrite.
- `J64_ALU_R_RM` forms 2/3 (`op reg, [mem]`): reg -> L_A, memRead ->
  L_B, compute, publish L_R to the dest reg (8/16-bit via the existing
  inline RMW helpers, 32/64 via the caller GPR-local publish).
- `J64_ALU_RM_IMM` (80/81/83, dest always r/m): memRead -> L_A,
  decode-time imm -> L_B, compute, memWrite. (80 is 8-bit-only; 83 is
  imm8 sign-extended; 81 is imm16/32/64 per size -- all already in
  `op.imm` from the existing decoder.)

The ALU compute core (`emitAluCompute`: L_A/L_B in, L_R + `emitFlags`
out) and the ADC/SBB carry fold are factored out of `emitAlu` so the
register and memory paths share them. No flag-semantics duplication.

### Soundness notes

- The `tryJitStep` cross-check (`bb.n == n`, per-record lengths) is
  unaffected: the decoder produces the same op boundaries; only new
  fields are added to `Jit64Op`. `buildBlock`/`execBlock` already handle
  memory ALU (existing interpreter records), so no BKind changes.
- Within-block staleness (a memory ALU writing the block own code
  page) is the accepted `execBlock` staleness from section 11.
- The emitter validates EA fields defensively (`seg == 0`, reg indices
  < 16 or 0xFF, scale <= 3) even though classify already filtered LOCK;
  a malformed op degrades to whole-block rejection, never to a wrong EA.

## 14. Hardening checklist: BoxedWine wasm-JIT bug catalog (2026-10-05)

Upstream BoxedWine (danoon2/Boxedwine, master) ships a real, active wasm JIT
that independently converged on our exact architecture (module-per-block,
wasmTable-index dispatch, Emscripten memory + helper imports, lazy flags).
Its commit history is a catalog of the failure modes of this architecture.
Each item below is triaged against OUR design (eager flags, fixed locals,
single-module-per-block). Items marked CHECK need verification; items marked
FUTURE are phase-4 work.

### 14.1 Cross-block lazy-flag preservation

Upstream uses lazy flags: flag computation is deferred, and a bug let a
stale CF leak across a block boundary into Wine's heap manager, corrupting
allocations. OUR STATUS: NOT APPLICABLE BY DESIGN. We compute all flags
eagerly per instruction (emitFlags, emitAluCompute) and merge them into
the RFLAGS local before the next op emits. There is no deferred flag state
to leak. The x87 decision (section 12) stands: no lazy evaluation anywhere.

### 14.2 Scratch-pool exhaustion

Upstream allocates scratch wasm locals from a pool; pool exhaustion reused
a live scratch, silently corrupting a branch decision. OUR STATUS: NOT
APPLICABLE BY DESIGN, WITH A CONSTRAINT. Our scratch locals are statically
assigned (L_A/L_B/L_R/L_F/L_P/L_T1/L_T2/L_STEP/L_CNT, see the jit64wasm.cpp
header comment). No pool, no exhaustion. CONSTRAINT: any new emitter that
needs scratch MUST take it from the fixed assignment and document the
liveness (which locals are live at each point); never introduce a dynamic
pool.

### 14.3 Block-overlap invalidation

Upstream's cache could return a stale block when a newly compiled block
overlapped an existing one (jump into the middle), executing the wrong code.
OUR STATUS: CHECK. Our block cache is keyed on startRip + page generations
(section 3.1), and tryJitStep cross-checks bb.n == n with per-record
lengths. The case to verify: a block whose bytes overlap (but do not start
at) a cached block's range after the underlying page is rewritten -- the
page generation bump must invalidate. Add a targeted test: compile block at
RIP X, rewrite the page so a new block starts at X+2 overlapping the old
range, and assert the old entry is never entered.

### 14.4 Module batching -- Firefox OOM at ~20k modules

Upstream emitted one wasm module per JIT block; Firefox ran out of memory
around 20,000 modules and they had to batch multiple blocks per module
(wasmModuleMerger, wasmJitBatchPolicy). OUR STATUS: FUTURE (phase 4).
We currently emit one module per block and instantiate synchronously. The
measured instantiation cost is ~48us/block (section 10); a module-count
ceiling has not been hit in testing, but any long-running workload will
eventually need batching. When batching lands, the policy to port is
theirs: batch cold blocks, keep hot blocks solo for faster compile.

## 15. Design: table-driven emitter pivot (2026-10-05, project decision)

Hand-rolling one wasm emitter per instruction group does not scale, and
copy-pasting an existing x86-64 to wasm emitter is not viable (survey:
v86 is 32-bit-only; CheerpX closed; Remill needs LLVM in-browser; FEX has
no wasm backend; QEMU wasm-TCG experimental + GPL; BoxedWine's own wasm
JIT is 32-bit-only and itself hand-rolled per group). v86 -- the most mature
project in this space -- does not hand-write emitters either: it generates
them from a semantics table (gen/x86_table.js). We do the same.

### 15.1 What the table covers

The table (source/emulation/cpu/jit64table.mjs) drives the EMITTER
(jit64wasm.cpp per-group emit functions), not the decoder
(jit64DecodeOne stays hand-written; it is complete for the proven groups
and its gates are green). Each table entry:

- func: generated C++ function name (e.g. emitAluGen).
- template: composition pattern in the generator (alu, shift,
  rotate, imul2, imul1, imul3, movx, string).
- kinds: the Jit64Kind values the emitter handles.
- decode: decode patterns (opcode, ModRM.reg extension, prefixes
  REX/0x66/0x67, REX.W/B) -- documentation of the contract plus the
  corpus schema for the byte-identical test.
- operands: operand spec per role (sizes 8/16/32/64; reg-direct with
  the AH/BH/CH/DH-vs-REX byte rule; ModRM r/m reg-or-memory; immediates).
- flags: flag behavior (which flags are written, result width, what is
  preserved).
- params: template-specific varying parts (e.g. for alu: per-kind
  dest/srcB selection, isTest expression).

New coverage becomes table rows (+ a new template only when the
composition is truly novel), not hand-written functions.

### 15.2 Generator decision

GENERATE C++ into source/emulation/cpu/jit64gen.inc, included into
jit64wasm.cpp's anonymous namespace. The generator is
scripts/gen-jit64.mjs (Node, matching the repo's JS tooling); the table
is a JS module (v86's x86_table.js pattern). The .inc is checked in;
regeneration must be byte-clean (verified by script).

Alternatives considered and rejected:
- Separate jit64gen.cpp TU: the generated functions need the
  anonymous-namespace primitives (uleb, i32Const, emitFlags,
  emitAluCompute, emitEA, operand accessors, ...); a separate TU
  would force moving ~15 functions out of the anonymous namespace --
  wider churn for no benefit.
- Runtime-interpreted table (emitter reads rows at block-compile time):
  branchier emission path, harder to review, and no C++-level
  byte-identical guarantee.
- Micro-op IR (fully declarative compute sequences): a bigger design
  than the task calls for; templates with parameters capture the real
  variation (operand wiring, flag modes) at lower risk.

### 15.3 The byte-identical bar (non-negotiable)

Migration is one group at a time: generate the group's emitter, then prove
the generated wasm bytes are BYTE-IDENTICAL to the hand-written emitter's
output over a broad corpus (random valid Jit64Ops per group +
decode-driven ops via jit64DecodeOne on random bytes), via
source/emulation/cpu/tests/jit64_gen_tests.cpp (native, wired into
scripts/build-jit-wasm.sh). Only then is the hand-written emitter
replaced. Per-op identity implies block identity (the dispatch mapping is
unchanged); the full gates (native/wasm/selftest/smoke/fuzz) then prove
the integrated behavior.
