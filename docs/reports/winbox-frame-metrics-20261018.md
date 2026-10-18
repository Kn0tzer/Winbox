# Winbox live frame-stage metrics: implementation report

Date: 2026-10-18. Lane 3 ("live frame-stage metrics UI") of the Winbox JIT
handoff.

- Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-frame-metrics-20261018`
- Branch: `feat/winbox-frame-metrics-20261018`
- Base commit: `64209c1c2` (unchanged lineage; no reset, no rebase)
- Implementation commit: `7867a84ce` (this report follows it)
- Contract for the other lanes: `docs/reports/winbox-metrics-contract-20261018.md`
- Nothing was pushed, deployed, or restarted. No BB agent was contacted or
  woken. No full Emscripten build was run (the integration agent owns builds;
  only focused per-file compile checks were used).

## What was built

| File | Role |
| --- | --- |
| `include/bw64metrics.h` | C ABI: `bw64_jit_stats_publish`, the stage hooks, the frame record |
| `source/util/bw64metrics.cpp` | Registry, windowing, JSON emission, JIT slot storage |
| `include/bw64jitstats.h` + `source/util/bw64jitstats.cpp` | Drop-in hook helper the JIT lanes call |
| `source/vulkan/vk64bridge.cpp` | Frame boundary hook (reuses the BW64_VKPROF timers) + vk32 present wall |
| `source/opengl/gl64bridge.cpp` | GL bridge trap wall (`gl64Bridge` / `gl32Bridge`) |
| `source/kernel/devs/devdsp.cpp` | Audio bridge write-path span + bytes |
| `source/kernel/syscall64.cpp` | Syscall/host-bridge span (GL/VK traps excluded) |
| `source/x11wire/xwirepresentSDL.cpp` | SDL present count (the GL/D3D9 display path) |
| `project/emscripten/wine64-launcher.js` | `?metrics=0` opt-out (main thread; workers cannot read the URL) |
| `web/vkwebgpu.mjs` | Page tier: pacing, translate, pipeline, JS submit, render, GPU timer |
| `web/frame-metrics.mjs` | Aggregation model + panel + app-shell binding |
| `web/app.mjs`, `web/index.html`, `web/style.css`, `web/assets/icons.svg` | Panel mount, Stats toggle, styles, icon |
| `tools/bw64metrics-selftest.cpp`, `tools/bw64metrics-fixture.sh` | Native registry selftest + fixture generator |
| `web/tests/suite/unit/frame-metrics.test.mjs` | 16 aggregation/UI tests (stub DOM) |
| `web/tests/suite/unit/frame-metrics-contract.test.mjs` | 4 writer/reader contract tests |
| `web/tests/fixtures/frame-metrics-snapshot.txt` | Real registry output (schema fixture) |
| `web/tests/frame-metrics-probe.mjs` | Real-browser probe (page tier + panel) |

The contract document is the first artifact the two JIT agents need: it defines
the publish ABI and lists the exact three call sites, so they never edit a
metrics file.

## Measured coverage (what is real today)

Measured in-process on the 64-bit guest Vulkan path, real Chromium, real
DXVK/D3D9 workload (`web/tests/frame-metrics-probe.mjs`, installed baseline wasm
+ this worktree's page tier):

```
page tier: 55 presents, 34.5 fps, 28.96 ms/frame
           submit 20.2 ms / 111 calls (JS), render total 1199.5 ms / 110 frames
           SPIR-V->WGSL 188.6 ms / 4 events, pipelines 104.0 ms / 3
           GPU execution 1.356 ms over 16 timestamp samples (measured)
           GPU verdict path: adapter timestamp-query present, zeroResolutions 0
```

Measured by the registry selftest (native, representative calls):

```
present path=vk count=30 window=503 ms · submit 0.90 ms · trapOther 1.20 ms
present body 0.30 ms · hop 0.40 ms · uploads buf 0.20 ms + img 0.50 ms
guest span 14.40 ms · syscalls 18.40 ms / 1 call · audio 2.00 ms / 3 writes / 9216 B
GL bridge 2.25 ms / 1 trap · JIT cpu64 running, 33210 entries, 81234567 retired
```

| Stage | Status | Unit / window |
| --- | --- | --- |
| Displayed FPS, frame ms | measured (page tier) | last 60 presents |
| Guest present FPS/ms | measured | ~500 ms window, 100 ms minimum |
| Guest CPU active span | measured | wall span/frame, **not** OS CPU |
| Vulkan submit / present body / other traps | measured | ms per frame window |
| Worker→page hop | measured | ms + chunks + bytes |
| Buffer / image uploads | measured | ms, subsets of submit |
| GL bridge | measured | ms per trap |
| Audio bridge | measured | ms + bytes per write |
| Syscalls / host bridge | measured | ms + calls, GL/VK traps excluded |
| SPIR-V → WGSL | measured | ms per translation **event** |
| Pipeline creation | measured | ms per pipeline |
| Submit (JS) | measured, labelled "not GPU" | ms per frame |
| Page render total (JS) | measured, overlaps everything | ms per frame |
| GPU execution | measured **only** from `timestamp-query` | ms per frame, 16-sample mean |
| CPU32 / CPU64 JIT counters | hook published, **not yet incremented** | see below |
| Wine, DXVK, VKD3D | unavailable + reason | guest code, no host boundary |
| 32-bit guest Vulkan breakdown | unavailable + reason | wall span only (`vk32.wallMs`) |

## Required CPU hooks (owner: JIT lanes 1 and 2)

No JIT file was edited by this lane. The patch is three call sites, in the file
that already owns the engine's counters:

```cpp
#include "bw64jitstats.h"
bw64jit::set_state(bw64jit::ARCH64, /*available*/ true, /*enabled*/ enabledFlag, "emitted-wasm");
bw64jit::add_blocks_emitted(bw64jit::ARCH64, 1, emitMs);
bw64jit::add_blocks_instantiated(bw64jit::ARCH64, 1, instantiateMs);
bw64jit::add_block_entered(bw64jit::ARCH64, n);      // hot path, from emitted code
bw64jit::add_instructions(bw64jit::ARCH64, n);       // hot path, from emitted code
bw64jit::add_fallback(bw64jit::ARCH64, 1);           // unsupported-op / invalidation
```

`ARCH32` for the 32-bit lane. The helper owns the worker-shared atomics and the
250 ms publish throttle, so no extra locking or timers are needed. Counters must
be incremented from emitted code, never from decoded-block or cache-lookup
counters. Until a lane calls `set_state`, the panel shows *not instrumented* for
that architecture — deliberately not `0`.

## Validation

| Check | Result |
| --- | --- |
| `node --test web/tests/suite/unit/frame-metrics.test.mjs` | 16/16 pass (status classification, staleness, honesty rules, formatters, panel reuse/updates, tier polling) |
| `node --test .../frame-metrics-contract.test.mjs` | 4/4 pass (real writer output parses, aggregates, all rows classify) |
| `tools/bw64metrics-fixture.sh` (native selftest) | 18 assertions pass |
| Full unit suite (`node web/tests/suite/runner.mjs --unit`) | 127 passed, 3 failed — all three are artifact inspections (`binary-patch-registry`, `packaging-prefix`, `provenance-artifacts`) that require a `dist/` export this worktree deliberately has no build for |
| Two existing Vulkan extraction tests | updated: their stubs now declare the new `vkTimingEnabled()` gate they extract from `vk64bridge.cpp`; both pass |
| `tsc --noEmit` (repo config, `checkJs`) | clean (after staging `web/vendor` from the pinned npm packages) |
| Focused compile check | `em++ -fsyntax-only` with the production `wasm64-mt` flag set on all five changed/added wasm TUs: `bw64metrics.cpp`, `bw64jitstats.cpp`, `vk64bridge.cpp`, `gl64bridge.cpp`, `syscall64.cpp`, `devdsp.cpp`, `xwirepresentSDL.cpp` — no errors |
| Real-browser probe `web/tests/frame-metrics-probe.mjs` | 20/20 assertions pass: page tier on the D3D9/DXVK path (see numbers above) and the panel in the app shell (22 rows before any snapshot, 18 marked unavailable, Stats toggle, real snapshot line reaching the panel, line suppressed from the log pane, 0.038 ms per `view()` over 24 rows, no page errors) |

Probe artifacts (ignored): `test-results/fm-probe-3/` (summary, phase metrics,
screenshots). The probe borrows the installed runtime binaries from
`/home/ubuntu/boxedwine64/dist` and overlays this worktree's `web/` tree in a
`/tmp` export, so it needs no rebuild.

To re-run these checks in this worktree (it has no `node_modules`):

```
ln -s /home/ubuntu/boxedwine64/node_modules node_modules
ln -s /home/ubuntu/boxedwine64/tools/browser/slang tools/browser/slang   # same for zig, disasm
node tools/browser/stage-vendor.mjs
```

## Gaps and limits (deliberate, documented, never papered over)

1. **Wine, DXVK, VKD3D are not host-visible.** They run as guest code, so their
   durations are reported `unavailable` with the reason; the panel points at
   their host-visible footprint (Vulkan trap/submit rows) instead of guessing.
2. **The JIT counters are not yet incremented** by either JIT lane, so the panel
   currently reports *not instrumented* for both architectures. That is the hook
   in this report; the JSON sections, the panel rows and the contract tests are
   already in place.
3. **GPU execution is one whole-frame number**, from timestamps bracketing the
   frame's submitted work. It is not per-draw, per-pass or per-pipeline, and it
   is unavailable when the adapter does not expose `timestamp-query`.
4. **The 32-bit guest Vulkan path has no per-trap timers**, so only its present
   count and present-to-present wall are reported; submit/trap/hop and the guest
   span are explicitly unavailable for that path (`"breakdown": false`).
5. **The legacy v1 manifest renderer** is instrumented with the same counters but
   was not exercised end-to-end in this lane (the shipped page tier uses schema
   v2).
6. **Audio is the write path**, i.e. guest write → conversion → Web Audio ring
   push. It is not an audible latency and says nothing about the guest mixer.
7. **The SDL/OpenGL present path FPS needs the rebuilt wasm**, because that
   present is counted wasm-side. Until the integration build lands, a GL program
   shows page-tier and JIT rows only.
8. **`?vkprof=1` behaviour is unchanged**: the per-frame VKPROF klog is still
   opt-in and independent; the metrics path only reuses its timers. Metrics are
   on by default, gated behind the same first-present flag (`BW64_METRICS=0` or
   `?metrics=0` disables them).
9. **Overhead**: metrics add a relaxed atomic add per accounted event and two
   clock reads per timed call (the trap timers already existed for the 64-bit
   Vulkan path). No performance benchmark was run in this lane — that belongs to
   the integration agent, per the handoff.

## Integration notes

- Cherry-pick `7867a84ce` (+ this report). There are no changes outside `source/`,
  `include/`, `web/`, `tools/` and `docs/`; no makefile edit is needed — the new
  translation units are picked up by the existing `source/util/*.cpp` glob in
  `project/emscripten/makefile`.
- After the merge and the JIT hook patch, one build is enough for the runtime
  stages to appear live; the panel needs no further wiring.
- Regenerate the contract fixture after any registry schema change:
  `tools/bw64metrics-fixture.sh`.
- `docs/reports/winbox-metrics-contract-20261018.md` is the only document the JIT
  agents need; it is self-contained (ABI, hook calls, JSON shape, semantics).