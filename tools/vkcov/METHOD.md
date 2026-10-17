# Bridge API-Coverage Harness — Methodology

## Goal
Systematically enumerate every handler in the vk64 (Vulkan) and GL64 (OpenGL)
bridges, call each one in-browser through the PE shims with valid-ish arguments,
and produce a coverage matrix: working / slow-or-flaky / broken-or-stub, with
repros for broken handlers. Measurement only — no bridge behavior changes.

## Handler surface (observed 2026-10-08, on the box)

| Set | Source | Count | Note |
|---|---|---|---|
| vk64 implemented | `case VK64_fn_*` in `source/vulkan/vk64bridge.cpp` | 170 | All exported by `vulkan-1.dll` |
| vk64 stub-only | DLL exports without a bridge case (TAIL wrappers) | 99 | Return `VK_ERROR_FEATURE_NOT_PRESENT` (-8) by design |
| vk64 total exports | `tools/vkd3d/probe/vulkan-1.dll` | 269 | 170 + 99 |
| vk32 | `source/vulkan/vk32bridge.cpp` | 303 | Trap-only; recorded as unimplemented per task brief |
| GL64 | `case GL64_fn_*` in `source/opengl/gl64bridge.cpp` | 306 | Via opengl32 → guest libGL shim |

(The task brief's "343 handlers" for vk64 was aspirational; the actual
implemented surface is 170. The 99 stub-only exports are still tested — they
must return -8, and any deviation is a bug.)

## Test programs

### vkcov.exe (Vulkan)
Win32 C, built with `x86_64-w64-mingw32-gcc`, using Vulkan headers from
`tools/dxvk/src/include/vulkan/include` with `VK_NO_PROTOTYPES`. Loads
`vulkan-1.dll` via `LoadLibraryA` + `GetProcAddress` (269 exports).

Setup chain (each step is itself a coverage test): `vkCreateInstance` →
`vkEnumeratePhysicalDevices` → `vkCreateDevice` → `vkGetDeviceQueue` →
`vkCreateCommandPool` → `vkAllocateCommandBuffers` → `vkCreateBuffer` +
`vkAllocateMemory` + `vkBindBufferMemory` → `vkCreateImage` + memory + bind →
`vkCreateImageView` → `vkCreateSampler` → `vkCreateDescriptorSetLayout` →
`vkCreatePipelineLayout` → `vkCreateShaderModule` (hand-assembled minimal
SPIR-V) → `vkCreateRenderPass` → `vkCreateFramebuffer` →
`vkCreateDescriptorPool` → `vkAllocateDescriptorSets` → `vkCreateFence` →
`vkCreateSemaphore` → `vkCreateEvent` → `vkCreateQueryPool`.

Then per-handler tests: all 58 `vkCmd*` inside a begun command buffer, queue/
sync ops, pipeline creates, surface/swapchain (headless), the 99 stubs
(generic no-arg call, expect -8), and finally all `vkDestroy*`/`vkFree*`.

### glcov.exe (OpenGL)
Win32 C, WGL context via the `tools/rootfs64/gltest/gltri.c` pattern.
All 306 `gl*` handlers called with valid-ish args; `glGetError()` checked.

## Output protocol
Each test program prints to stderr (captured in the probe's console.log):
- `VKCOV-TRY: <name>` (or `GLCOV-TRY:`) — BEFORE each call; if the run crashes,
  the last TRY line is the repro.
- `VKCOV: <name>=<rc1>,<rc2>:<us1>:<us2>` — return codes and microseconds for
  two runs (flakiness detection).
- `vkcov: RESULT 0|1` — final marker.

## Slow threshold: 10ms per call

**Definition:** a handler is "slow" if its per-call wall time is ≥10,000µs.

**Justification:** the guest-CPU profiling lane (2026-10-08, n=211 Pong frames)
measured 48 vk64 trap invocations at ~34.5ms total, i.e. ~0.72ms average per
trap — and that includes the full record-encode path for `vkQueueSubmit`.
A simple marshalled bridge call that does no GPU work (a getter, a state
setter, a create that only allocates a host object) has no business taking
10×+ the average trap cost. Anything at or above 10ms is either doing real
work that should be async (e.g. `vkDeviceWaitIdle`, `vkQueueWaitIdle` —
expected slow, documented as such) or is a performance bug worth filing.

**Flaky definition:** `rc1 != rc2` across the two runs, or (for timing)
>10× variance — recorded but bucketed on return-code inconsistency.

## In-browser execution
`web/tests/vkcov-probe.mjs` (Playwright, modeled on `d3d12-pixels.mjs`):
serves `dist`, stages `vkcov.exe` + `vulkan-1.dll` (PE shim, app-dir wins
loader search) + `libvulkan.so.1` (guest shim), waits for the `vkboot`
sentinel, spawns with `WINEDLLOVERRIDES=vulkan-1=n;winevulkan=n`, polls logs
for `vkcov: RESULT`, saves `vkcov-results.json`.

Probe discipline: one probe at a time via `acquireProbeSlot()` from
`web/tests/probe-slots.mjs`; never touch `/tmp/bw64-probe.lock`.
