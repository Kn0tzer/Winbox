# CPU JIT statistics

The combined runtime reports separate `cpu32` and `cpu64` objects through
`bw64_jit_stats_json_text()`. The historical `bw64` prefix does not restrict the
contents to 64-bit guests. Runtime selection is `jit=0|1`, forwarded to host
`BW32_JIT` and `BW64_JIT` before their cached environment reads.

`bw64_jit_stats(char*, int)` and `bw64_jit_stats_json(char*, int)` provide buffer
interfaces. Callers must check the return value before parsing; an undersized
buffer is not a valid partial document. The text convenience export uses a
thread-local buffer which callers should copy immediately.

## Execution evidence

| Field | Meaning |
| --- | --- |
| `available`, `enabled`, `engine` | Build capability, requested state, and selected engine. Capability does not prove execution. |
| `blocksEmitted`, `blocksInstantiated` | Modules generated and installed. These are compilation counts. |
| `blocksEntered` | Entries incremented inside generated modules. |
| CPU64 `insnsEmitted` | Historical name for instructions retired inside generated code, not the number of instructions compiled. |
| CPU64 `rawEnteredCounter` | Sum of the module-written entry counter; agrees with host-counted entries. |
| CPU32 `instructionsRetired` | Completed native nonbranch instructions, excluding interpreter fallback. |
| CPU32 `retiredCounting`, `retiredScope` | Whether optional retirement counting is enabled and what it covers. `jitstats=1` enables it and adds hot-path overhead. |

Both sections can be available while only one architecture executes. A 32-bit
Chromium run cannot supply CPU64 execution or performance evidence.

CPU32 x87 operations use the interpreter when the runtime selects 80-bit mode;
they cannot contribute to the generated-code retirement count. Integer and SSE
operations still compile. Double-cache builds can emit x87 instructions.

## Costs and fallback

CPU64 `emitUs`/`emitMs` measure bytecode generation; `instantiateUs` measures
module construction and table installation. These are cumulative cold-path wall
times, not frame times. CPU32 `emitMs` spans its translation call, which includes
installation work; its separately reported `instantiateMs` overlaps that span.
Do not add overlapping spans into a CPU total.

CPU64 `coldSkipped` counts hotness deferrals. `emitRejected` and
`instantiateFailed` identify rejected compilation attempts. `fallbacks` and
`fallbackInsns` are fallback-attempt counters; they are not exact retired
instruction counts. `earlyExits` and `limitExits` count generated blocks that
stopped for changed code or an instruction budget.

CPU32's `fallbacks` object distinguishes `emulateSingleOp`, `slotMiss`,
`installFailure`, and `disabled`. The `emulateSingleOp` counter records emitted
fallback sites, not the dynamic number of times those sites execute.

## Sampling limits

CPU64 owners publish snapshots under a mutex; readers do not inspect live CPU
counters. Retired CPUs retain their final totals. CPU32 generated counters use
shared atomic storage. Sampling is cumulative and may lag an active worker.

CPU64 `mode` is a worker-local diagnostic. A UI-thread sample can report `idle`
while other workers execute table-installed modules; use the entry and retirement
counters as execution evidence. `js-array` is no longer an execution fallback:
failed table installation returns to the interpreter.

CPU64 `cacheLookups`, `cacheHits`, and `cacheInserts` are meaningful for
`jit64StatsJson(state, ...)` on a specific CPU. The current process aggregate
does not publish those cache counters; its zero values must not be interpreted
as a measured hit rate.

GPU execution, Wine, DXVK, VKD3D and frame-stage durations are separate metrics.
These CPU statistics do not measure those layers.
