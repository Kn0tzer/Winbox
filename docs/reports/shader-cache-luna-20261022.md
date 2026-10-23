# Optional persistent shader cache

## Implemented

- Added `web/shader-cache.mjs`, an IndexedDB store keyed by Naga 30.2.0 and schema 1. It stores WGSL text and metadata, caps entries at 32 MiB, evicts least recently used shaders, tracks per-installation shader manifests, and degrades to an empty cache on storage failures.
- Added the opt-in `shadercache` setting, defaulting to Off, persisted with the existing launcher settings. Launches pass `shadercache=0|1`. Settings also has a clear-cache action.
- GLSL conversions use a SHA-256 source/options key and can reuse persisted WGSL. Vulkan SPIR-V entries use stage, module hash:size, topology, and specialization options. Cache hits are loaded into memory; app shader manifests warm WebGPU shader modules and request compilation info during idle startup work.
- Added shader-module creation counters to both Vulkan renderers and exposed them with translation and pipeline counters in frame metrics.
- Pipeline descriptors are not persisted or replayed. The current paths contain device-owned bind-group layouts and state that the store cannot safely reconstruct, so warmup skips pipelines.

## Verification

Focused tests passed: `shader-cache`, `shader-naga-wgsl-spirv`, and `frame-metrics` (22 tests total, including fake IndexedDB and fake-device warmup coverage). JavaScript syntax checks passed.

The full unit suite completed with 149 passing and 9 failing tests. Failures were in `binary-patch-registry`, `memory64-backing-lifetime`, `packaging-prefix`, `poll-deadline`, `provenance-artifacts`, `v2-image-blit`, `vulkan-depth-bias`, `vulkan-query-results`, and `vulkan-sampler`. These failures are outside the files changed for this feature; they appear to reflect the prepared worktree/build state and need a clean baseline comparison.

## Runtime measurements

No cold-versus-warm measurements are recorded yet. The requested Portal game probe was started with the integration runtime and `shadercache=1`, but stayed queued behind existing heavy probes and did not reach the browser. I stopped that queued process after the queue remained occupied. The supplied integration export currently has no `web/runtime/index.html`, which may also prevent this `game-probe.mjs` version from serving it as-is. Celeste and GL probes have not run. Consequently, there are no cold/warm numbers, shader-time fractions, or time-to-first-frame comparisons to report.

The timing counters now report milliseconds and event counts from the WebGPU Vulkan page tier. They do not yet instrument the GL WebGPU pipeline or provide a load-time denominator. Pipeline creation remains synchronous and is measured as the renderer's pipeline-build span, including validation-scope completion.
