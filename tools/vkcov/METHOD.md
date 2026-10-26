# Bridge API-Coverage Harness — Methodology

## Goal
Systematically enumerate every handler in the vk64 (Vulkan) and GL64 (OpenGL)
bridges, call each one in-browser through the PE shims with valid-ish arguments,
and produce a coverage matrix: working / slow-or-flaky / broken-or-stub, with
repros for broken handlers. Measurement only — no bridge behavior changes.

## Handler surface (re-measured 2026-10-24 from the sources; ABI ids run to 918)

| Set | Source | Count | Note |
|---|---|---|---|
| vk64 bridge cases | `case VK64_fn_*` before `vk32Bridge` in `source/vulkan/vk64bridge.cpp` | 204 | x86-64 guest ABI |
| vk32 bridge cases | `case VK64_fn_*` after `vk32Bridge` in the same file | 207 | i386 guest ABI. Not trap-only: it has real handlers for the same ids (see `docs/known-gaps/vulkan.md` for the shim/bridge parity guard) |
| ICD shim entries | `E()` bridged / `S()` shim-local / `TAIL()` loud stub in `tools/rootfs64/libvk{64,32}/*.c` | 251 / 14 / 40 per arch | Identical per arch (`vk-shim-bridge-parity.test.mjs`); a `TAIL` returns -8 and logs `fn:<name>` |
| `vulkan-1.dll` exports used by `vkcov.exe` | `tools/vkd3d/probe/exports.txt` | 269 (measured 2026-10-08) | Historic Windows-side list; it contains invented names that no real application can call, which the ICD intentionally leaves as `TAIL` stubs (register: NOT-A-GAP) |
| GL64 | `case GL64_fn_*` in `source/opengl/gl64bridge.cpp` | 306 | Via opengl32 -> guest libGL shim |

There is no `source/vulkan/vk32bridge.cpp`; both ABIs are served by `vk64bridge.cpp`.
The `TAIL` stubs are still tested: they must return -8 and be loud, and any other
behaviour is a bug. The original "stub-only 99 / implemented 170" split of the
Windows export list predates the 32-bit parity work, the shim-local answers
(`S()`), and the extended-dynamic-state ids; do not use it to size remaining work -
use the register (`docs/known-gaps/vulkan.md`).

## Test programs

### vkcov.exe (Vulkan)
Win32 C, built with `x86_64-w64-mingw32-gcc`, using Vulkan headers from
`tools/dxvk/src/include/vulkan/include` with `VK_NO_PROTOTYPES`. Loads
`vulkan-1.dll` via `LoadLibraryA` + `GetProcAddress` (269 exports at the time of writing; see Handler surface).

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
sync ops, pipeline creates, surface/swapchain (headless), the stub-only exports
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
