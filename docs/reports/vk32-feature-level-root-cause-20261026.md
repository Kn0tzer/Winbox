# 32-bit bridge feature level 0 — root cause and fix (2026-10-26)

Lane: `lane-vk32fl` (worktree `/home/ubuntu/wt-vk32fl`, base integ `b4897e00f`).

## Symptom

Every DXVK D3D11/D3D10 device creation against the Vulkan bridge failed with:

```
info:  D3D11InternalCreateDevice: Maximum supported feature level: 0
err:   D3D11InternalCreateDevice: Minimum required feature level D3D_FEATURE_LEVEL_10_1 not supported
```

First observed on the 32-bit Unigine Tropics benchmark (steering experiments,
`test-results/steering/tropics-*` in the integ worktree), where it blocked all
32-bit DXVK D3D10/D3D11 paths.

## Root cause

Not a 32-bit struct-layout bug. The 32-bit and 64-bit bridges share the same
feature fill functions and mirrored Features2/Properties2 walkers (a unit
test pins walker parity), and the values they return are identical for both
guest architectures.

The actual cause is a contract break between the bridge and the vendored
DXVK (v2.4.1, `tools/dxvk`):

1. DXVK's device creation is all-or-nothing. `D3D11Device::GetDeviceFeatures`
   (tools/dxvk/src/src/d3d11/d3d11_device.cpp) hard-requires 20 Vulkan
   features (literal `VK_TRUE` assignments); `D3D11Device::GetMaxFeatureLevel`
   returns `D3D_FEATURE_LEVEL()` = 0 if the adapter lacks even one of them
   (`DxvkAdapter::checkFeatureSupport`).
2. Commit `70fe78535` (2026-10-23, "vulkan: truthful capability manifest +
   diff test, 32-bit feature parity, SPIR-V and sampler audit") cut the
   bridge's advertised feature set down to only end-to-end-implemented
   features. That removed 11 of DXVK's 20 baseline requirements from the
   advertised set:
   - core: `dualSrcBlend`, `fillModeNonSolid`, `geometryShader`,
     `multiViewport`, `occlusionQueryPrecise`, `sampleRateShading`,
     `shaderCullDistance`, `shaderImageGatherExtended`
   - `VkPhysicalDeviceVulkan12Features.samplerMirrorClampToEdge`
   - `VkPhysicalDeviceTransformFeedbackFeaturesEXT.transformFeedback` /
     `.geometryStreams` (the fill function was emptied entirely)
3. With any one of them reported FALSE, DXVK computes maximum feature
   level 0 and refuses to create any D3D11/D3D10 device — on BOTH guest
   architectures, since the fills are shared. The last pre-regression
   evidence (Gang Beasts, 64-bit, reporting `D3D_FEATURE_LEVEL_11_0`) is
   dated 2026-10-23 00:51–05:51, before the manifest commit entered the
   build lineage; no successful DXVK D3D11 device creation exists on any
   build containing it. The breakage was masked on 64-bit by the loader64
   PT_GNU_RELRO regression (guests did not start at all) and surfaced on
   32-bit first via Tropics after the merge.

Verified mechanically: the set difference between DXVK's hard-required
features and the bridge's advertised set (`source/vulkan/vk-capabilities.json`)
is exactly the 11 fields above.

Why "advertise them anyway" was not the fix: the bridge's reports are
honest — the WebGPU page tier has no geometry/tessellation stage, no
transform feedback, no dual-source blending, no wireframe, one viewport,
no occlusion-query counting, no cull distance, and no
mirror-clamp-to-edge sampler mode. `web/tests/suite/unit/vk-capabilities.test.mjs`
(the guard the manifest commit added) forbids advertising what is not
implemented, including a dedicated pin keeping `occlusionQueryPrecise`
unadvertised. Making the bridge lie again would re-break that contract.

## Fix

Vendored DXVK, `D3D11Device::GetDeviceFeatures`: the 11 unsatisfiable
baseline entries are now mirrored from the adapter's reported support
(`enabled.x = supported.x`) — the idiom the same function already uses for
its optional features — instead of being hard-required. Consequences:

- `checkFeatureSupport` passes: the requirement set is a subset of the
  reported set by construction.
- `D3D11DeviceFeatures::GetMaxFeatureLevel` caps the device at the level
  the honestly reported features sustain: **10_1**, because
  `fragmentStoresAndAtomics` and `tessellationShader` remain FALSE.
- An application that actually uses an unsupported feature (a geometry
  shader, stream-out, dual-source blending, ...) fails at that use, in the
  open, instead of every application failing at device creation.

The bridge's feature reporting is unchanged; DXVK D3D9 (separate caps
path) and vkd3d are untouched.

## Regression guard

`web/tests/suite/unit/vk-dxvk-baseline.test.mjs` (JS unit suite) pins the
contract from both sides:

1. Every feature DXVK hard-requires (`= VK_TRUE` in `GetDeviceFeatures`)
   must be advertised by the bridge (present in `vk-capabilities.json`).
2. The 11 fields above must stay mirrored from `supported.*` in DXVK and
   must never become hard requirements again.

Sabotage-verified: flipping `geometryShader` back to `VK_TRUE` in DXVK
fails both tests; deleting a manifest row for a still-hard-required field
(`fillVulkan13Features.shaderDemoteToHelperInvocation`) fails the first.
Restored state: guard 2/2 pass, `vk-capabilities.test.mjs` 11/11 pass.

## Second half of the defect (found by the first verification round)

The DXVK baseline mirror alone did NOT lift the feature level (verified
in-browser: patched support pack mounted, DXVK still reported maximum
feature level 0). One hard-required field remains genuinely servable but
was reported unsupported at query time: textureCompressionBC. The bridge
fills it from a page global (vk64bridge.cpp `v2WebGpuBcSupported()` reads
`globalThis.bwVkTextureCompressionBC`), which was only published by the
renderer tier (web/vkwebgpu.mjs `createVkRenderer*`) when it constructs
its WebGPU device — at first frame. Guests query device features at
D3D11 device creation, BEFORE any frame, so the flag read as unset and
BC as unsupported, and `DxvkAdapter::checkFeatureSupport` failed the
whole baseline. The WebGPU adapter itself exposes
'texture-compression-bc' (verified in the same headless Chromium/
SwiftShader the probes use). Same defect class the Portal lane fixed on
its branch for the sibling depth32float-stencil8 flag.

Fix (commit eddb4368d): web/runtime.html now publishes the
adapter-derived capability flags (bwVkTextureCompressionBC,
bwVkDepth32FloatStencil8, bwVkMaxVertexBuffers) at page load, before
the guest boots — the same values the renderer later derives from its
device. The guard gains a third test pinning this pre-boot publication
(sabotage-verified: removing the block fails the test; restored suite:
vk-dxvk-baseline 3/3 + vk-capabilities 11/11 = 14/14).

## Verification

All runs: demo-matrix harness from the integ worktree,
`DEMO=unigine-tropics JIT=1`, dist = this lane's scratch dist
(identical to integ's except the noted archives/runtime page).

- tropics-baseline-f — committed config (`-video_app direct3d10`):
  dies at `dxgi.dll.DXGID3D10CreateDevice, aborting` (defect #2, the
  staged DXVK dxgi export gap), exactly as the steering lane recorded.
- tropics-d3d11-f — direct3d11 args, pre-fix support pack: DXVK logs
  `Maximum supported feature level: 0` + `Minimum required feature
  level D3D_FEATURE_LEVEL_9_1 not supported` (×2). Defect reproduced.
- tropics-d3d11-g — PATCHED DXVK d3d11.dll in the support pack, runtime
  page unchanged: STILL feature level 0. Proved the DXVK half of the
  fix necessary-but-insufficient and led to the BC flag-timing cause.
- tropics-d3d11-h — patched DXVK + pre-boot flag publication:
  `[31.2s] D3D11InternalCreateDevice: Maximum supported feature level:
  D3D_FEATURE_LEVEL_10_1` and `Using feature level
  D3D_FEATURE_LEVEL_10_1`. The device is created; the engine proceeds
  (swapchain extension negotiated, state-cache init, DXGI device
  queries, main loop alive for the full run). 10_1 is the honest
  ceiling: the ladder's 11_0 rung needs fragmentStoresAndAtomics +
  tessellationShader, both genuinely unservable over WebGPU.
  Presented frames were still 0 in this run: after device creation the
  guest stalls in the 32-bit present path — the vkAcquireNextImageKHR
  signalling defect the Portal lane already fixed in 4adb27962, which
  is on the current integ tip (b32fc06c6) but not in this lane's base.
  Re-run Tropics on the merged tree to see frames.

Guard config: `node --test web/tests/suite/unit/vk-dxvk-baseline.test.mjs
web/tests/suite/unit/vk-capabilities.test.mjs` → 14 tests, 14 pass
(vk-dxvk-baseline 3/3, vk-capabilities 11/11). All three guard tests
sabotage-verified during development (DXVK VK_TRUE flip, manifest row
deletion, runtime.html block removal each fail exactly the right test).

## Merge notes (coordinator)

- Integ has moved to b32fc06c6 since this lane branched (b4897e00f).
  Its runtime.html already publishes the same adapter flags pre-boot
  (4adb27962, slightly different expression) — at merge, keep ONE
  implementation; the guard accepts both shapes (0598e63c9).
- The DXVK patch + guard (379a402f3) do not overlap the portal work.
- Effective restage at merge: rebuild DXVK w32+w64 from the patched
  source, re-run tools/browser/stage-dependencies.py, ship the new
  support-*.zip + dist/runtime/dependencies.json pin.

## Which archive actually serves DXVK (staging correction)

The guest does NOT load d3d11.dll from prefix32.zip's home/username copy
(that staging path, tools/dxvk/stage-dxvk32.sh, targets the spawn-dir
search order and does not govern the system DLL path). The effective
copies live in the SUPPORT archive, layered over wine32.zip at
`opt/wine-stable/lib/wine/i386-windows/d3d11.dll` (32-bit) and
`usr/lib/x86_64-linux-gnu/wine/x86_64-windows/d3d11.dll` (64-bit).
tools/browser/stage-dependencies.py builds that pack from
`tools/dxvk/build.w{32,64}/src/{d3d9,d3d11,dxgi}/*.dll` and names it
`support-<sha256>.zip`, pinned in dist/runtime/dependencies.json
(archive/bytes/sha256 — the only pin). So the merge-time restage for this
fix is: rebuild DXVK both arches from the patched source, re-run
stage-dependencies.py, and ship the new support archive + its
dependencies.json. (The prefix32.zip re-pin commit on this branch keeps
the secondary staging consistent but is not the effective one.)
For the verification runs in this lane, the support archive in the
scratch dist was rebuilt surgically: identical entries except the two
d3d11.dll files replaced with the patched builds, dependencies.json
re-pinned to the new hash (support-35fd94625f2d…).

Probe mechanics worth recording (learned the hard way, several runs lost):
the demo-matrix harness serves the dist itself, and the shell will not
launch a demo archive until a HEAD probe succeeds against a hardcoded
base in dist/demos.mjs: `archiveBase = 'http://127.0.0.1:8901/games/'`
(demoParameters). If that probe 404s, the shell shows "files are
unavailable", the runtime never launches the guest, and the run idles to
the mapping timeout with zero metrics. The 8901 mirror
(/tmp/games-mirror.mjs) originally mapped the games dir at the ROOT
(serving /<file>, which is what the runtime's later gamezip fetch uses),
so /games/<file> 404'd. This lane patched the mirror additively to also
strip a leading `games/` prefix and restarted it; both paths now serve.
Separately, the runtime fetches the archive from
`http://127.0.0.1:8901/unigine-tropics.zip` (root path) — the gamezip
override used by the steering lane is therefore not needed for Tropics
on the current dist, though it is harmless.
Also: demo-matrix acquires the probe slot BEFORE waiting for build-idle,
while bw64-build.sh waits for probe slots — a probe started during a
build can deadlock against it (both wait; bounded by their timeouts).
And the harness writes console.txt/summary.json only at the very end;
an OUT dir must be preserved before any cleanup logic runs.

## Follow-on: builtin D3D10 arm

Run tropics-builtin-f (`dlloverrides=dxgi=b;d3d10core=b`, direct3d10
args): the guest boots and the Vulkan instance enumerates fine, but the
engine logs `Loading "dxgi.dll"... D3D10Wrapper::init(): can't load
"dxgi.dll" library` after an earlier `ERROR: Could Not Get Primary
Adapter Handle`, and falls back to a non-rendering UI (0 presented
frames). Verdict: NO — Wine's builtin D3D10 stack does not serve the
benchmark in this environment; the builtin dxgi.dll fails to load at
all (WINEDEBUG=-all hides the loader's reason; root-causing that is a
separate thread). The DXVK D3D10 path remains blocked by the staged
dxgi.dll's missing DXGID3D10CreateDevice export (defect #2) — a DXVK
packaging/export question, not a bridge one.
