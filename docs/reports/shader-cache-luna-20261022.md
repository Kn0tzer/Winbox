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

Measurements used the supplied integration runtime without rebuilding it. To serve that export, the probe assembled a temporary `/tmp/winbox-shader-probe-dist` layout and reused its integration runtime files. The probe now supports a persistent Chromium profile and fixed local port so the cold and warm launches share IndexedDB; it also records the renderer's first successful submission time and avoids awaiting the entire GPU queue in the instrumentation wrapper.

Portal (`shadercache=1`) had 4 Naga translations, 10 shader-module creations, and 5 pipeline builds on the cold run. The measured totals were 249.02 ms, 0.54 ms, and 170.97 ms respectively. On the warm run, two persistent hits removed Naga translation (0 ms, 0 translations); shader-module creation took 0.38 ms across 10 creations and pipeline builds took 162.64 ms across 5 builds. `shadercache=0` measured 301.21 ms of translation, 0.46 ms of module creation, and 148.50 ms of pipeline builds. The probe-reported frame timings were 80.23 s cold, 99.39 s warm, and 72.58 s with caching disabled.

Using the probe-reported frame timing as an approximate load-time denominator, translation, module creation, and pipeline-build spans were under 0.7% in all three Portal runs. These costs are negligible relative to startup, and the available data does not show a first-frame improvement from caching. Pipeline descriptors therefore remain unwarmed; the data does not justify adding descriptor capture/reconstruction complexity.

The cold and warm Portal runs were captured before the probe stopped waiting for GPU queue completion, so their first-frame values (80.23 s and 99.39 s) are queue-completion times. The corrected cache-disabled probe measured renderer submission at 72.58 s. These are not apples-to-apples first-frame timings; a consistent cold/warm rerun stayed queued behind other heavy probes and was stopped before it acquired a slot. The shader counters are still useful for the cache decision, but no launch-time benefit is established.

None of these measurements prove a visible game frame. Portal did not verify its menu and reported zero painted pixels, although it submitted renderer frames with no rejected draws or browser errors. Treat the timings as startup diagnostics, not a user-visible launch guarantee.

Celeste's normal probe first stalled with a black canvas and no shader metrics. Adding the existing 32-bit Wine Mono overlay let Wine proceed; retrying with the x86 GL shim loaded the GL path (`stages.gl`: 34 calls, 421.61 ms in a reported interval), but the game still showed a black screenshot and zero presents through the 240 s timeout. No first frame or shader-cache metrics were produced, so a Celeste cold/warm comparison and `shadercache=0` baseline are unavailable. This probe does not measure GL shader compilation time. The run logs and screenshots are in `test-results/shadercache-celeste-cold-mono/` and `test-results/shadercache-celeste-cold-gl32/`.

Focused post-merge tests pass (33 tests across shader-cache, SPIR-V translation, metrics, and probe harness). The full unit suite has 155 passing and 3 failing tests: `binary-patch-registry`, `packaging-prefix`, and `provenance-artifacts`. The remaining failures are outside the shader-cache and probe changes.
