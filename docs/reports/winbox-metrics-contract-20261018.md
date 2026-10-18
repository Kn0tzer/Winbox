# Winbox frame metrics: CPU/JIT instrumentation contract

Status: **implemented and frozen 2026-10-18** for the frame-metrics lane. Other
lanes implement the publish call described here; they do not edit the metrics
worktree. The panel and aggregation live in `web/frame-metrics.mjs`, the wasm-side
registry in `source/util/bw64metrics.cpp`, the drop-in JIT helper in
`include/bw64jitstats.h` + `source/util/bw64jitstats.cpp`, and the native selftest
in `tools/bw64metrics-selftest.cpp`. Both sides parse this contract, and
`web/tests/suite/unit/frame-metrics-contract.test.mjs` fails if they drift.

## What the panel measures

| Stage | Source | Unit | Status |
| --- | --- | --- | --- |
| FPS, frame ms | page tier present pacing (`vw/vkwebgpu.mjs`) | per second / per present | measured |
| Guest present FPS | vk64 present-to-present wall | per second, 500 ms window | measured |
| CPU active span | vk64 present-to-present wall minus submit/present/other traps | ms/frame | measured (wall span, not OS CPU) |
| Vulkan submit, present body, other traps | vk64 trap timers | ms/frame | measured |
| Worker→page hop | chunk hop timers | ms/frame + chunks/bytes | measured |
| Buffer / image uploads | subsets of submit | ms/frame | measured (never added to submit) |
| GL bridge | `gl64Bridge` / `gl32Bridge` trap wall | ms/event | measured |
| Audio bridge | `DevDsp::writeNative` wall | ms/event + bytes | measured |
| Syscalls / host bridge | 64-bit syscall dispatch wall (GL/VK traps excluded) | ms/frame + calls | measured |
| SPIR-V → WGSL | page tier translate wall | ms/**event** | measured |
| Pipeline creation | page tier `createRenderPipeline` wall | ms/event | measured |
| Submit (JS) | page tier `queue.submit` wall | ms/frame | measured, labelled "not GPU" |
| Page render total (JS) | whole page-tier frame replay | ms/frame | measured, overlaps everything |
| GPU execution | WebGPU `timestamp-query` around the frame | ms/frame | measured when the adapter supports it, otherwise unavailable with the reason |
| CPU/JIT | this contract | counters + µs | owner-implemented |
| Wine, DXVK, VKD3D | not host-visible | — | unavailable + reason, always |

`measured` = a real span/event was observed. `inactive` = instrumented, zero
events in the window. `unavailable` = this build/backend cannot report it (reason
shown). `stale` = the program is running but the wasm snapshot stopped arriving
(>3 s); the last real values stay on screen. Submit time is never reported as GPU
execution and guest-layer duration is never inferred from bridge time.

## Publish API (C ABI, `include/bw64metrics.h`)

```c
typedef struct Bw64JitStats {
    uint32_t structSize;              /* sizeof(Bw64JitStats) — checked */
    const char* engine;               /* static lifetime, e.g. "emitted-wasm" */
    uint32_t available;               /* 1 = an engine published statistics */
    uint32_t enabled;                 /* 1 = dispatching into emitted code now */
    uint64_t blocksEmitted;           /* emitted, not cache lookups */
    uint64_t blocksInstantiated;      /* module/instance actually ready to enter */
    uint64_t blocksEntered;           /* entries into emitted code */
    uint64_t instructionsRetired;     /* incremented inside emitted code */
    uint64_t fallbacks;               /* unsupported-op / invalidation fallbacks */
    uint64_t emitMicroseconds;
    uint64_t instantiateMicroseconds;
} Bw64JitStats;

void bw64_jit_stats_publish(int arch, const Bw64JitStats* stats);  /* arch 32 or 64 */
```

Thread-safe, non-blocking, copies into atomics. Values are **monotonic
counters**, not window deltas. `engine` must be a static string (sanitised to a
JSON-safe subset on publish).

## Required hook patch (exact)

JIT owners do **not** touch the metrics files. Include the helper and call it:

```cpp
#include "bw64jitstats.h"

// once, when the engine is created/disabled:
bw64jit::set_state(bw64jit::ARCH64, /*available*/ true, /*enabled*/ jitEnabled, "emitted-wasm");

// after emitting one block / instantiating one module (real measured ms):
bw64jit::add_blocks_emitted(bw64jit::ARCH64, 1, emitMs);
bw64jit::add_blocks_instantiated(bw64jit::ARCH64, 1, instantiateMs);

// hot path, incremented FROM EMITTED CODE (one relaxed atomic add each):
bw64jit::add_block_entered(bw64jit::ARCH64, n);
bw64jit::add_instructions(bw64jit::ARCH64, n);

// unsupported-op / invalidation fallback:
bw64jit::add_fallback(bw64jit::ARCH64, 1);
```

Rules the panel depends on:

1. `blocksEntered` / `instructionsRetired` are incremented **from emitted code**,
   never from decoded-block or cache-lookup counters.
2. Counters are worker-shared: the helper owns the atomics, so a JIT owner needs
   no locking of its own. Call from any guest worker thread.
3. `available=false` (or a `*_publish` call with a NULL/mismatched struct) means
   "no statistics from this engine"; the panel then shows *not instrumented*, and
   never `0` blocks. `available=true, enabled=false` means "compiled in, off" and
   the panel shows *disabled*.
4. Call `set_state()` at engine creation even when the JIT ends up disabled.
5. Publishing is throttled to 250 ms inside the helper; no caller needs a timer.
   The registry reads the last published values at its own ~500 ms emission, so a
   stopped publisher shows as `stale`, never as zero.
6. Do not time guest execution through this API: `guestMs` comes from the frame
   boundary. These counters are for engine attribution only.

## Emitted snapshot

Every ~500 ms, when a frame or a bridge stage was active, the runtime logs:

```
bw64: METRICS {"v":1,...}
```

Forwarded by the existing `print` hook, parsed in `web/app.mjs`, and deliberately
kept out of the visible log pane. Shape (abridged, with the numbers a real run
produced on the D3D9/DXVK path — see the lane report for the full sample):

```json
{
  "v": 1, "uptimeMs": 41230, "winMs": 503.0, "arch": "x86-64", "enabled": true,
  "present": { "vk": 30, "vk32": 0, "sdl": 0, "count": 30, "fps": 59.6, "frameMs": 16.8, "path": "vk" },
  "vk": { "supported": true, "frames": 30, "wallMs": 512.0, "submitMs": 9.3,
          "trapOtherMs": 12.1, "presentMs": 3.6, "hopMs": 4.2, "bufMs": 2.1,
          "imgMs": 5.0, "guestMs": 482.8, "trapCalls": 912,
          "hopChunks": 120, "hopBytes": 8123456 },
  "vk32": { "supported": true, "frames": 0, "wallMs": 0.0, "breakdown": false,
            "reason": "32-bit guest Vulkan presents have no per-trap timers; ..." },
  "stages": {
    "syscall": { "supported": true, "calls": 1203, "ms": 18.4 },
    "gl":      { "supported": true, "calls": 0,    "ms": 0.0 },
    "audio":   { "supported": true, "calls": 22,   "ms": 1.7, "bytes": 65536 }
  },
  "jit": {
    "cpu32": { "supported": false, "available": false, "engine": null, "enabled": false, "...": 0 },
    "cpu64": { "supported": true, "available": true, "engine": "emitted-wasm", "enabled": true,
               "blocksEmitted": 912, "blocksInstantiated": 904, "blocksEntered": 33210,
               "instructionsRetired": 81234567, "fallbacks": 12, "emitUs": 8120, "instantiateUs": 2450 }
  },
  "unavailable": [ { "id": "wine", "reason": "..." }, { "id": "dxvk", "reason": "..." } ]
}
```

Units: milliseconds, bytes, monotonic counts. `fps`/`frameMs` are `null` for a
window shorter than 100 ms (frame 1) — a rate from a 0 ms window would be
fabricated. `guestMs` is a **wall span** inside the present-to-present window.
`bufMs`/`imgMs` are subsets of `submitMs`. Inclusive stages overlap and must not
be summed. `supported` describes the build, `available`/`enabled` the JIT hook.

## Page-tier snapshot (JS, merged by the panel)

`window.bwVkTier.metrics()`:

```js
{ v1: Snapshot|null, v2: { presents, windowMs, fps, frameMs,
    gpu: { status:'measured'|'inactive'|'unavailable', ms, lastMs, samples, reason },
    translate: { ms, count }, pipeline: { ms, count },
    submit: { ms, count }, render: { ms, count }, skipped, unsupported } | null,
  queue: { size, dropped }, received: { chunks, frames },
  rendered: { v1, v2 }, rejected }
```

GPU execution comes only from `writeTimestamp` around the frame's command
encoder, resolved asynchronously (never blocking the render pump). Without the
adapter's `timestamp-query` feature, or when a backend resolves both timestamps
to 0, the status is `unavailable`/`inactive` with that exact reason and the panel
shows `--`. `submit.ms` is the JS `queue.submit` call and is labelled "not GPU".