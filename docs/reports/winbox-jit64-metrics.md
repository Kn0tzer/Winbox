# CPU64 JIT metrics contract (Lane 2 proposal)

Status: **proposed by the CPU64 JIT lane**; the frame-metrics lane had not yet
published a contract when this was written, so this document is the interface
the CPU64 side actually implements. If the metrics lane lands a different
schema, this one is the adapter point — nothing else in the JIT depends on it.

Everything here is read-only instrumentation. No counter changes JIT behaviour.

## Entry points

| Symbol | Where | Purpose |
| --- | --- | --- |
| `jit64StatsJson(const Jit64State*, char* buf, U32 size)` | `source/emulation/cpu/jit64.cpp` | One JSON object for one CPU's JIT state. Returns bytes written (excluding NUL) or `0` when the buffer is too small (never a truncated document). |
| `jit64GlobalStatsJson(char* buf, U32 size)` | `source/emulation/cpu/cpu64.cpp` | Process-wide aggregate over every CPU64 that ran with the JIT enabled (relaxed atomics; safe from any thread). |
| `bw64_jit_stats(char* buf, int size)` | `source/emulation/cpu/jit64wasm_runtime.cpp` (`extern "C"`, exported) | Emscripten/JS bridge to `jit64GlobalStatsJson`. Returns the byte count or `0`. Call it from the viewer/panel; there is no per-frame cost beyond the formatting. |
| `CPU64::jit()` | `include/cpu64.h` | The `Jit64State*` for one CPU (null until `BW64_JIT=1` has been used). |

The panel should call the `bw64_jit_stats` export (or `jit64GlobalStatsJson`)
and treat `0` as "stats unavailable" rather than parsing a partial document.

## Schema

```json
{"cpu64":{
  "available":1,          // emitted-module engine usable in this build
  "enabled":1,            // BW64_JIT=1
  "engine":"wasm",        // "wasm" | "interp" | "off"
  "mode":"table",         // "table" | "js-array" | "idle" | "unavailable"
  "hotThreshold":8,
  "blocksCompiled":120,   // hot blocks decoded + emitted + cached
  "blocksEmitted":120,    // == blocksInstantiated (kept for clarity)
  "blocksInstantiated":120,
  "blocksEntered":5400,   // emitted-module entries (EMITTED CODE counter)
  "insnsEmitted":84000,   // guest instructions retired inside emitted modules
  "rawEnteredCounter":5400,// sum of Jit64WasmState::blocksEntered over calls
  "earlyExits":0,         // modules that left at a page-generation change
  "limitExits":12,        // modules that left at the instruction bound
  "emitRejected":4,       // blocks the emitter refused (stay interpreted)
  "instantiateFailed":0,
  "fallbacks":930,        // cold entries left to the interpreter
  "coldSkipped":900,      // of those, skipped by the hotness counter
  "emitUs":41000,         // cumulative emit+instantiate time (cold path only)
  "emitMs":41.000,
  "fallbackInsns":930,
  "cacheLookups":54000,
  "cacheHits":53100,
  "cacheInserts":120},
 "cpu32":{"available":0,"enabled":0,"engine":"unavailable",
          "reason":"32-bit JIT not compiled in this target"}}
```

In the 32-bit build the `cpu32` section reports the legacy `BOXEDWINE_JIT`
status instead (same keys, `engine":"boxedwine-jit"`); the 64-bit target has no
CPU32 code linked, so it reports unavailable with a reason. The `cpu64` section
is always present when this build contains `BOXEDWINE_GUEST_X64`.

## Provenance of each number (the honest part)

* `blocksEntered`, `insnsEmitted`, `rawEnteredCounter` are **values the emitted
  modules wrote themselves**: the generated prologue increments
  `Jit64WasmState::blocksEntered`, and the epilogue (or an early exit) stores
  `Jit64WasmState::execCount`. The host only reads them back and sums them. A
  decoded-block cache hit cannot produce them, which is precisely why they are
  the evidence used by `scripts/build-jit-wasm.sh` and by the self-test's
  `WASM64:` line. `blocksEntered` (host, one per call) is printed next to
  `rawEnteredCounter` so the agreement between them is checkable.
* `blocksCompiled`/`blocksEmitted`/`blocksInstantiated` are host-side cold-path
  counters: work done, not work executed. Do not present them as speed.
* `emitUs`/`emitMs` are host wall-clock microseconds spent in
  `jit64EmitWasm` + `jit64WasmInstantiate` on the cold path only. This is
  *compile* time, not frame time, and it is what a cold-start regression would
  show up in. Interpreter entries never add to it.
* `fallbacks`/`coldSkipped`/`emitRejected`/`instantiateFailed` count blocks that
  went to the interpreter. `coldSkipped` ⊆ `fallbacks` (hotness deferral).
* `cacheLookups`/`cacheHits`/`cacheInserts` are `Jit64BlockCache` counters,
  including the *unvalidated* lookups the invalidation tests perform; they are
  not a hit rate for `blocksEntered`.
* `earlyExits` means "a guest store invalidated one of the block's code pages
  and the module left at the next instruction" — the self-modifying-code exit
  that keeps JIT semantics identical to `CPU64::execBlock`. A rising value is
  evidence of correct invalidation, not of failure.
* `limitExits` means the entry's instruction budget (used by `runBounded`) was
  reached. Production (`run()`) passes an unbounded budget, so this stays 0
  there.
* `mode` reports the call path for emitted modules: `table` = the module's
  function lives in the host `WebAssembly.Table` and C++ reaches it through a
  table index (requires `-sALLOW_TABLE_GROWTH`, added by `BW64_JIT_BUILD=1`);
  `js-array` = the build could not grow the table, so each block entry crosses
  JavaScript once. `js-array` is correct but slower — a visible signal that a
  build flag is missing.

## Environment controls (host `getenv`, read once and cached)

| Variable | Values | Meaning |
| --- | --- | --- |
| `BW64_JIT` | `1` | Enable the CPU64 JIT (anything else = off, deployed default). |
| `BW64_JIT_ENGINE` | `wasm` \| `interp` \| `off` | Force an engine. `interp` selects the phase-1 interpreter vehicle, which is the same-binary A/B that proves the emitted path is what changed. |
| `BW64_JIT_HOT` | integer ≥ 0 | Entries before a block is emitted (default 8, `0` = compile on first entry). |
| `BW64_JIT_VERIFY` | `1` | Cross-check every emitted block's decode against the interpreter's `BBlock` builder and require the self-test's emitted-code evidence. |

These are **host** knobs: they are read by the compiled module, so the guest
environment cannot switch engines. `scripts/build-jit-wasm.sh` forwards them
into the Emscripten `ENV` in its `preRun` shim for exactly that reason.

## Missing / not applicable for CPU64

* No GPU, frame, present or shader data here — those belong to the frame-metrics
  panel.
* No per-instruction or per-block tracing hooks: enabling them would defeat the
  measurement. `jit64EmitPlanText()` (native/diagnostic builds only) dumps a
  block's decoded plan when a test needs to explain a fallback.
* No CPU32 numbers in the 64-bit target (not linked).