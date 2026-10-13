# D3D9 Pong Guest Frame Breakdown — Methodology

## Goal
Split the ~276ms D3D9 Pong guest frame (baseline: `test-results/frametime-pong2/`,
mean 276.1ms) into measurable components so optimization work targets the
right hotspot.

## Approach: additive, env-gated wasm instrumentation (no behavior change)

Two new instruments, both off unless env vars are set (zero cost otherwise):

### 1. `BW64_VKPROF=1` — wall-clock timers in `source/vulkan/vk64bridge.cpp`
Frame defined as present-exit → present-exit (`VK64_fn_vkQueuePresentKHR`).
All timestamps are `emscripten_get_now()` on the guest worker thread.
Components are non-overlapping; `wall = submit + trapOther + present + guest`
verified to close to <0.001ms.

| Component | What it measures |
|---|---|
| `wall` | Present-exit to present-exit (whole frame, wasm side) |
| `submit` | `vkQueueSubmit`/`2`/`2KHR` handler wall (cmdbuf walk, `serializeSubmitV2` record encode, `readGuestRange` copies, `fnv1a64` hashing, chunk emit) |
| `bufdata` | `emitBufferData` portion of submit (+ bytes hashed / bytes sent) |
| `imgdata` | `emitImageData` portion of submit (+ bytes hashed / bytes sent) |
| `trapOther` | Every OTHER vk64 trap handler via RAII timer (`vkCmd*` recording, `vkAcquireNextImageKHR`, …) |
| `present` | `vkQueuePresentKHR` handler wall (struct reads + `hopV2ToPage`) |
| `hop` | `MAIN_THREAD_EM_ASM` chunk hops inside `hopV2ToPage` (chunks, bytes) |
| `guest` | `wall − submit − trapOther − present` = pure CPU64 interpreter time |

One `vk64: VKPROF …` klog line per presented frame.

### 2. `BW64_RIPSAMPLE_PROFILE=1` — instruction histogram in `ripSampler.cpp` + `cpu64.cpp`
`CPU64::run()` samples `(pid, rip, insnDelta)` every 64k instructions into a
per-(pid, module) histogram (module resolution reuses the existing
`g_modRanges` table populated at mmap/PE-load time). At each present the
histogram is dumped (`RIPPROF …` klog lines) and reset, giving a per-frame
module split of the interpreter time. Overhead: one cached-bool test per 64k
insns when off; one mutex + table scan per 64k insns when on.

### 3. Page side (`web/tests/frametime-profile.mjs`, measurement only)
- Guest frame completions via MORE-bit in chunk headers (same as
  `frametime-harness.mjs`).
- Per-chunk page-tier decode time (wall around `tier.chunk()`), separating
  page decode from the wasm-side EM_ASM hop.

### Env plumbing (important gotcha)
Wasm-side `getenv()` reads the JS `ENV` object populated at page load from URL
params (`wine64-launcher.js`), NOT the `bw64_spawn` env (which only reaches the
guest process). The probe passes `?vkprof=1&ripsampleprof=1`; the two param →
ENV mappings were added to the `dist-profile` copy of `wine64-launcher.js`
only (shared `dist/` untouched).

## Build
Instrumented build kept separate from shared `dist/`:
- `dist-profile/` = full copy of `dist/`, with `boxedwine64.wasm`/`.js`
  overwritten from the instrumented build and the launcher patch above.
- The 23:23 UTC recovery build already contained the pass-1 instrumentation;
  a later incremental build added pass-2 (call counts, ser2/queue, top traps)
  — pass-2 probe runs were flaky (spawn/liveness flakes under load), so the
  numbers below are from the pass-1 run (`pong2`), which is complete.

## Results (test-results/frametime-profile-pong2/)
- 211 VKPROF frames (60 warmup + 150 measured for page-side; wasm stats cover all 211)
- `summary.json`: aggregated stats; `breakdown.json`: per-frame records;
  `console.log`: raw klog; `frametimes.json`: page-side timestamps.

## Caveats
- `wall` (wasm, 226.9ms mean) vs page-side guest frame (255.4ms): the wasm
  stat covers all 211 frames including warmup, the page stat covers the 150
  measured frames; warmup frames are faster. Apples-to-apples re-runs should
  align windows.
- Headless SwiftShader: relative numbers, not absolute device performance.
- The `bad/stale handle` klog flood is benign (present in working runs too).
