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
