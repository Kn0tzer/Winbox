# Bridge API Coverage — deliverable index

Four-bucket coverage matrices per API set, from in-browser probes (2026-10-09).

- `matrix-vk64.md` — vk64 bridge (Vulkan), 269 DLL exports tested in-browser via `tools/vkcov/vkcov.exe`.
- `matrix-gl64.md` — GL64 bridge (OpenGL), 306 handlers tested in-browser via `tools/glcov/glcov.exe`.
- `matrix-vk32.md` — vk32 bridge (32-bit Vulkan): static record (in-browser testing blocked on 32-bit wineboot).
- `SUMMARY.md` — four-bucket rollup per set with slow-threshold justification.

Bucket definitions (see `tools/vkcov/METHOD.md`):
1. total tested
2. fully working (rc==0 / glerr==0, <10ms, consistent across 2 runs)
3. slow or flaky (>=10ms per call, or inconsistent rc/err across runs)
4. broken or stub (nonzero rc/err, stub -8, missing proc, or crash)

Slow threshold: 10,000us per call. Justification: guest-CPU profiling (2026-10-08,
n=211 Pong frames) measured 48 vk64 traps at ~34.5ms total (~0.72ms avg per trap,
including full record-encode for vkQueueSubmit). A marshalled bridge call doing
no GPU work has no business taking 10x+ the average trap cost.
