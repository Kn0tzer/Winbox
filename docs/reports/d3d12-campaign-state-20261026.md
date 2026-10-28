# DirectX 12 campaign — state report (2026-10-26)

Lane: `lane-vkd3d`, worktree `/home/ubuntu/wt-vkd3d`, base `integ/winbox-20261022b`
@ `b4897e00f` (tree verified clean at lane start). All work on the SSH box.
End goal: a real DirectX 12 program creating a device and drawing in the
browser: guest d3d12 → vkd3d → Vulkan bridge (vk64bridge/vkwebgpu) → WebGPU.

## 1. Inventory (evidence-based)

**In-tree before this lane:**
- `tools/vkd3d/` — Oct 2026 feasibility docs (FEASIBILITY.md, CURRENT_STATUS.md,
  PROBE_PLAN.md, ICD_WORK_ITEMS.md, SOURCES.json), probe programs
  (`probe/tri12.c/.exe` clear-only, `tri12draw.c/.exe` triangle, `scene12`,
  `d3dconf12`), the PE Vulkan shim `probe/bw64vulkan.dll`, and the vendored
  **vkd3d-proton 3.1.0** source at `tools/vkd3d/src` (SOURCES.json pins
  `faf71e79b9b7e7ac6b7294a3c0b62f33d85e90ad`).
- Prebuilt proton DLLs existed only as untracked artifacts in *other*
  worktrees (Oct 13 build of the older 3.0.1 pin). None in this worktree.
- The staged `wine64.zip` carries Wine 10.0's bundled `d3d12.dll` (588,985 B)
  and `d3d12core.dll` (70,654 B) with **no vkd3d underneath** — device
  creation had nothing real to call; `d3d12` dlloverrides were illusory.
- DXVK 2.4.1 is built/staged for d3d9/d3d11/dxgi (`tools/dxvk/BUILD.md`,
  `stage-dxvk.sh`): DLLs are placed next to the app inside
  `dist/runtime/prefix64.zip` and selected with `WINEDLLOVERRIDES=<dll>=n`.
  This is the staging pattern vkd3d now follows (`tools/vkd3d/stage-vkd3d.sh`).
- Bridge Vulkan caps (from `source/vulkan/vk64bridge.cpp` source, not docs):
  ICD reports `VK_API_VERSION_1_3`; handlers exist for synchronization2,
  dynamic rendering, push descriptors (synthetic push sets), descriptor
  update templates, `vkCmdBindIndexBuffer2`, `vkCmdPushConstants2KHR`.
- Prior probe result (2026-10-05, `tools/vkd3d/probe/PROBE_RESULT.md`):
  `tri12.exe` failed `D3D12CreateDevice hr=0x80070057` (E_INVALIDARG) with
  vkd3d-proton logging `Lacking support for VK_EXT_vertex_attribute_divisor`;
  `vkCreateDevice` was never reached, though the whole ICD query path
  (instance/physical-device/properties/features/extensions) worked.

## 2. Route decision

**vkd3d-proton (pinned: the vendored 3.1.0), NOT Wine's bundled vkd3d.**
- Upstream Wine vkd3d (~1.16) translates HLSL/DXBC → SPIR-V only and has
  **no DXIL frontend**; D3D12 applications ship SM 6.x DXIL. vkd3d-proton
  vendors dxil-spirv (DXIL → SPIR-V) and dxbc-spirv statically into
  `d3d12core.dll`.
- Load chain: app → `d3d12.dll` (proton loader) → `d3d12core.dll` (proton)
  → `vulkan-1.dll` (bw64vulkan PE shim) → `libvulkan.so.1` (ELF shim) →
  vk64 traps → vk64bridge → page tier (naga SPIR-V → WGSL) → WebGPU.
- vkd3d requires Vulkan 1.3 minimum; the bridge reports 1.3. The remaining
  distance is a finite list of device-creation gates inside
  `vkd3d_init_device_caps` + `vkd3d_bindless_state_init` (see §5).
- Shader path check: dxil-spirv emits the `OpSampledImage` form naga
  handles; the known naga `InvalidId(40)` gap is on a construct
  dxil-spirv does not emit (per FEASIBILITY.md).

## 3. Build outcome

Fresh cross-build of the vendored 3.1.0 in this worktree
(`tools/vkd3d/BUILD.md` has the recipe):
- meson setup OK (345 targets; dxil-spirv subproject configured),
  ninja **209/209, DONE_RC=0** on the second attempt.
- First attempt failed on exactly one error:
  `libs/vkd3d-common/platform.c:285: 'PATHCCH_NONE' undeclared` — this box's
  mingw-w64 `pathcch.h` declares `PathCchCombineEx` but not the
  `PATHCCH_NONE` (0x0) flag constant. Fixed by configuring with
  `CFLAGS=-DPATHCCH_NONE=0x00000000 CXXFLAGS=-DPATHCCH_NONE=0x00000000`;
  no vendored source modified.
- Products: `build.w64/libs/d3d12/d3d12.dll` **517,717 B**
  sha256 `d77fdad5d3a1426287cec7e642ca8e43f1f52c2f54c7de3bb24a64174ec2284a`;
  `build.w64/libs/d3d12core/d3d12core.dll` **8,825,383 B**
  sha256 `d2bd847a7d564fb64aee51461998b0014767b73a3b4f0ec46a977702da0076b3`,
  exports `D3D12GetInterface`, `D3D12SDKVersion` (objdump-verified).

## 4. Probe outcomes

Harness: `web/tests/d3d12-pixels.mjs` (probe-slot serialized; serves a dist,
uploads the DLL set into the guest app dir, spawns under
`WINEDLLOVERRIDES=d3d12=n;d3d12core=n;dxgi=n;vulkan-1=n;winevulkan=n`,
`BW64_VKTRACE=2`, `BW64_VKFRAME=1`; verdict requires RESULT 0 + rendered
frames + >1000 cornflower pixels for the clear probe).

### 4.1 Baseline — merged tree (integ dist @ b4897e00f) + fresh DLLs
`test-results/dxvk-d3d12-baseline1/`. The harness provenance pins the exact
DLLs above. Result: **FAIL, exactly at the first vkd3d device gate**:
- `tri12: FAIL D3D12CreateDevice hr=0x80070057`
- `err:vkd3d-proton:vkd3d_init_device_caps: Lacking support for VK_EXT_vertex_attribute_divisor.`
- 0 frames built, 0 chunks. Same signature as the 2026-10-05 probe —
  reproducible baseline on the merged tree.

### 4.2 Bridge patch #1 — the advertised-capability gates
Audit-driven changes to `source/vulkan/vk64bridge.cpp` +
`source/vulkan/vk-capabilities.json` (the manifest test forces every
advertised bit to carry an `implemented` row; the patch moved rows
accordingly) and `web/vk-protocol.mjs`:

- Enumerate `VK_KHR_push_descriptor`, `VK_EXT_vertex_attribute_divisor`,
  `VK_EXT_transform_feedback` (handlers/features behind them exist; TF
  stream-out entry points stay absent and its feature bits stay FALSE).
- `fillVertexAttributeDivisor`: both bits TRUE (divisor != 1 still rejects
  loudly at pipeline audit).
- `fillMaintenance5/6`: bits TRUE (`vkCmdBindIndexBuffer2(KHR)` /
  `vkCmdPushConstants2KHR` implemented; vkd3d never calls the *2 descriptor
  entry points — grep-verified).
- `fillVulkan12Features`: `samplerMirrorClampToEdge` TRUE (page tier serves
  mode 4 as mirror-repeat, exact on [-1, 2]; `ADDR[4]` in vk-protocol.mjs),
  plus the bindless gate bits `descriptorIndexing`,
  `shaderStorageTexelBufferArrayNonUniformIndexing`,
  `shaderStorageImageArrayNonUniformIndexing`,
  `descriptorBindingVariableDescriptorCount` (state.c:8343 gate; sets are
  materialized at submit; shader-side array indexing still rejects loudly
  in the SPIR-V audit).
- `fillVulkan13Features`: `synchronization2`, `dynamicRendering` TRUE
  (handlers exist; the bits previously read FALSE only due to zero-fill).
- Transform-feedback properties arm: `transformFeedbackQueries` TRUE
  (the exact property the vkd3d gate reads); stream-out limits stay 0.
- Push-descriptor properties arm added to BOTH properties walkers
  (`maxPushDescriptors = 64`; the 32-bit arm uses the vk32 field-writer
  idiom after a first version with the wrong ABI was caught pre-build).

Probe `test-results/dxvk-d3d12-patch1/`: divisor gate **passed**; new
first failure one gate later:
`err:vkd3d-proton:vkd3d_init_device_caps: maintenance5 and/or maintenance6 not supported...`
Root cause found in the vendored source (device.c:1455): vkd3d only
*chains* the maintenance5/6 feature structs when the **extensions**
`VK_KHR_maintenance5/6` are enumerated — the feature bits alone are
invisible to it.

### 4.3 Bridge patch #2 — enumerate VK_KHR_maintenance5/6
Probe `test-results/dxvk-d3d12-patch2/`: **all `vkd3d_init_device_caps`
gates pass, and bindless state init passes.** Device creation reached
vkd3d's meta ops and failed at:
`err:vkd3d-proton:vkd3d_meta_ops_common_init: Failed to create shader modules, vr -8`
(VK_ERROR_FEATURE_NOT_PRESENT) → `D3D12CreateDevice hr=0x80004005` (E_FAIL).
Bridge log: one module rejected for SPIR-V capability `Geometry` —
vkd3d's meta ops create a fullscreen **geometry shader**
(`gs_fullscreen`, meta.c:1512) unless the device advertises
`shaderOutputViewportIndex` + `shaderOutputLayer`, in which case it uses
the `vs_fullscreen_layer` vertex shader instead (meta.c:1501).

### 4.4 Bridge patches #3 + #4 — the viewport/layer meta path
- `fillVulkan12Features`: `shaderOutputViewportIndex`,
  `shaderOutputLayer` TRUE (+ manifest rows).
- SPIR-V audit: capabilities 5345 (ShaderViewportIndexLayerEXT), 68
  (ShaderLayer), 69 (ShaderViewportIndex) pass the audit (the first probe
  after patch #3 rejected on the core form, cap 69 — patch #4 completed
  the set and added all three to the audit's exemption allowlist;
  `vk-spirv-rejections.test.mjs`'s pinned exemption set was updated to
  match, per that test's own re-disposition rule).
Geometry capability itself still rejects loudly (real geometry shaders
remain a browser ceiling).

Probe `test-results/dxvk-d3d12-patch4/`: meta common init **passed**;
device creation advanced to the next eager subsystem:
`err:vkd3d-proton:vkd3d_clear_uav_ops_init: Failed to create compute pipeline 0, vr -8`
with bridge rejections `spirv-compute-entry`, `spirv-storage-image`,
and two unknown capabilities in the compute module. vkd3d eagerly
builds compute pipelines for D3D12 ClearUnorderedAccessView at device
init; the bridge (a deliberate Phase-2 disposition) refused compute
shader modules and compute pipelines outright because the V2 protocol
has no dispatch execution.

### 4.5 Bridge patch #5 — compute creation succeeds, dispatch stays loud
Disposition change (recorded here and in the code comments; the native
test's two pinned blocks and the known-gaps register rows are updated
to match):
- `vk64AuditSpirv`: a module whose entry points are compute-only is
  accepted (logged as `spirv-compute-entry`, not rejected). The
  capability/storage/input-attachment/SSBO-write checks protect the
  graphics translation path and no longer apply to modules that never
  reach it. Mixed graphics+compute modules keep the full audit.
- `vkCreateComputePipelines` (both ABIs): creates real Pipeline
  objects (64-bit records stage module + layout) and returns
  VK_SUCCESS instead of VK_ERROR_FEATURE_NOT_PRESENT.
- Execution is unchanged: `vkCmdDispatch` records CMD_DISPATCH, and
  the submit emitter drops it with the loud `compute-dispatch`
  vk64Unsupported line (guarded by `vk-silent-drop-guard.test.mjs`).
  D3D12 compute and UAV clears therefore *create* but do not *produce
  results* — the honest limit until the V2 protocol grows a compute
  record (see §5).

## 5. Blocker stack

### Servable by us (bridge gaps, with evidence)
_(gates list updated after the patched probe)_

### Buildable (packaging/build work)
- vkd3d staging into `prefix64.zip`: script written
  (`tools/vkd3d/stage-vkd3d.sh`, mirrors `stage-dxvk.sh`); staged proof
  pending the patched-dist probe.
- 32-bit (x86) vkd3d build: the vendored tree ships `build-win32.txt`;
  not yet attempted (x64 first, per campaign order).

### Browser ceiling (WebGPU limits, established — not re-litigated)
- No geometry/tessellation stages in WebGPU → the highest D3D12 feature
  levels are unreachable; basic D3D12 (device, queues, swapchain, root
  signatures, VS/PS PSOs, compute) does not need them.
- Shader-side descriptor arrays: vkd3d's bindless model indexes descriptor
  heap arrays in shaders; the page tier has no WGSL binding arrays, so
  shaders declaring the descriptor-array indexing capabilities
  (SPIR-V caps 5302–5309) are rejected loudly at `vkCreateShaderModule`
  (`vk64AuditSpirv`) until the page tier grows an expansion strategy.
  D3D12 clears (vkCmdClearColorImage path) and descriptor-free shaders
  are unaffected.

## 6. Next steps
_(updated at lane end)_

### 4.6 Fifth probe (patch 5): compute disposition works; copy_image is next

`dxvk-d3d12-patch5`: the clear-UAV ops init (dozens of compute pipelines)
now completes — 69 compute modules accepted with the loud
`spirv-compute-entry` line each. Device creation proceeded to
`vkd3d_copy_image_ops_init`, whose GRAPHICS shader modules were rejected
(vr -8): one for SPIR-V capability Geometry (the copy variants pair the
fullscreen VS with `gs_fullscreen` for layered copies) and one for
SampleRateShading (the MSAA copy fragment variants). Meta shader census
(source-level): 37 `.comp`, 14 `.frag`, 3 `.vert`, exactly 1 `.geom`
(`gs_fullscreen`) — the entire remaining meta surface is known.

### 4.7 Patch 6: geometry and sample-rate leave the creation path too

Same disposition as patch 5, applied to the graphics side:

- SPIR-V capabilities 2 (Geometry) and 35 (SampleRateShading) pass module
  creation. Geometry's consumers are pipelines, and pipelines are gated at
  submit (below); sample-rate fragment variants are gated by translation
  in the page tier (loud there), while the fixed-function
  `sampleShadingEnable` pipeline state still rejects at creation.
- `vk64AuditGraphicsPipeline` logs `geometry-stage` but no longer rejects;
  both pipeline handlers record the geometry module in a new `Pipeline.gs`
  field. Tessellation/task/mesh stages still reject at creation (the known
  browser ceiling; vkd3d meta ops never use them).
- At submit, CMD_DRAW / CMD_DRAW_INDEXED under a pipeline with `gs` set are
  dropped with the loud `geometry-stage-draw` line; other draws in the same
  submission are unaffected.

Pinned tests updated in the same change: `vk-spirv-rejections` exemption
set += {2, 35}; `vk-pipeline-rejection` moves `geometry-stage` out of the
reject list and pins the recorded-not-rejected wording; the native test's
geometry block now expects VK_SUCCESS + a live handle. JS suite for the
touched areas: 31/31. Native harness: passes both compute blocks and the
geometry block; still aborts only at the pre-existing environmental
textureCompressionBC assertion (identical on the pristine integ tree).

### 4.8 Sixth probe (patch 6): DEVICE CREATED — but the submits were hollow

`dxvk-d3d12-patch6` is the campaign's first device creation:

```
tri12: D3D12CreateDevice OK
tri12: render target created
tri12: 10 frames cleared+submitted
tri12: RESULT 0
```

Every vkd3d device-init subsystem now initializes. But the page side
captured **0 frames / 0 chunks**, and the console shows why: the bridge
rejected vkd3d's queue submissions wholesale —
`UNSUPPORTED timeline-semaphore (submit2 timeline signal is not supported;
VK_ERROR_FEATURE_NOT_PRESENT) in vkQueueSubmit2` (99 times), with vkd3d
logging `vkd3d_memory_transfer_queue_flush_locked: Failed to submit command
buffer, vr -8`. vkd3d bundles a timeline-semaphore signal (its D3D12 fence)
into every queue submit; the bridge's submit paths returned an error
before serializing anything, so the guest API surface reported success
while no GPU work crossed. tri12's RESULT 0 asserts API success only (its
source verifies no readback), so this is API-level first light, not
rendering — recorded here as exactly that.

### 4.9 Patch 7: timeline semaphore emulation in the submit paths

The host-side half of timeline support already existed and was honest:
`Semaphore.counter` seeded from `VkSemaphoreTypeCreateInfo.initialValue`,
`vkGetSemaphoreCounterValue` reads it, `vkWaitSemaphores` compares against
it (VK_TIMEOUT when unreached, never fabricating), `vkSignalSemaphore`
bumps it monotonically — and a pinned test forbids wait paths from
inventing signal values. The missing half was the submit paths. Patch 7
completes the model in all four (v1/v2 × 64/32-bit):

- Timeline signal in a submit: raises the semaphore counter
  (monotonic) when the batch is accepted — no rejection.
- Timeline wait in a submit: satisfied when an earlier submission already
  signalled the value; if unreached, it logs `timeline-semaphore-wait` and
  the submission proceeds (submissions execute in order; the counter only
  ever moves in signal paths, so nothing is fabricated).
- Binary semaphore semantics unchanged (the swapchain/DXVK contract, still
  pinned by `vk-binary-semaphore.test.mjs`, whose two assertions pinning
  the old timeline rejection strings were updated to the new disposition;
  `vk-timeline-semaphore.test.mjs` gains a submit-side test).

Native harness: compiles clean, still aborts only at the pre-existing
environmental textureCompressionBC assertion.

### 4.10 Seventh probe (patch 7): submits land; the draw probe finds the next gate

With timeline signals emulated, the patch-7 run shows **zero**
timeline-semaphore rejections and zero vkd3d submit errors: queue batches
are accepted and serialized. tri12 (offscreen, no present) still shows no
page-side frames — its clears go through dynamic rendering, and the
chunk-drain question for present-less sessions remains open (see §5).

The draw probe (`tri12draw`, window + DXGI swapchain + triangle) then
advanced the frontier three steps in three runs:

1. `CreateRootSignature` failed with E_INVALIDARG, silently. With
   `VKD3D_DEBUG=trace` injected through the launcher's JSON `env` URL
   parameter, vkd3d named it: *"Root signature requires 6 descriptor sets,
   but device only supports 3."* vkd3d's legacy bindless layout always
   builds six sets (heap, samplers, push-UBO and bookkeeping); the bridge
   advertised `maxBoundDescriptorSets = 3` and both pipeline-layout
   handlers rejected layouts with more than three sets.
2. **Patch 8** raised the ceiling to the bridge's own record capacity:
   `maxBoundDescriptorSets = VK64_MAX_SETS` (8) and both
   `vkCreatePipelineLayout` handlers record up to 8 sets. What a shader may
   *use* stays gated where it always was — the page tier builds bind
   groups from shader declarations only (groups 0–2 plus the push group)
   and fails higher declarations loudly (`group-out-of-range` in
   web/vkwebgpu.mjs). The register rows that called the old arrangement
   NOT-A-GAP are re-dispositioned in this campaign's register addendum.
   Root signature creation then **succeeded**.
3. `CreateGraphicsPipelineState` failed with E_FAIL: the bridge rejected
   the converted pixel shader's SPIR-V module for capability **4466
   (SignedZeroInfNanPreserve)**, which dxbc-spirv emits for D3D12's IEEE
   float semantics. **Patch 9** passes 4466 at module creation (a
   float-preservation execution mode: no control-flow or memory effect;
   naga remains the translation gate).

Also observed, not yet blocking: `vkGetDeviceImageMemoryRequirements` is
an unimplemented entry point (returns FEATURE_NOT_PRESENT); vkd3d calls
it during resource creation probing and currently tolerates the refusal.
It is implementable from the bridge's existing image-requirements logic
and is listed in §5.

### 4.11 Box disk incident (2026-10-26, during this lane)

The box disk reached **100% full (512M free)** mid-campaign, killing one
dist export mid-zip. This lane freed its own corrupt dist (1.0G) and
rebuilt; the disk later recovered to ~18G free through cleanup outside
this lane. The large consumers are pre-existing (docker 18G, backups 15G,
kn0tzer 24G). Flagged for the coordinator: builds and probes box-wide are
one full disk away from silent corruption — the build script prints
BUILD COMPLETE only after its export verifies, but an export killed
mid-zip leaves a dist that must not be trusted.

### 4.12 Eighth probe onward: FIRST LIGHT — a D3D12 triangle in the browser

`dxvk-d3d12-draw6` (build with patches 1–9), verdict **`passed: true`**:

- Guest: `D3D12CreateDevice OK` → window → DXGI swapchain → RTVs →
  root signature → `PSO created` → `10 frames drawn+presented` →
  `RESULT 0`.
- Page: 21 V2 chunks captured; 10 frames received at 480×360, each with
  one draw (`bindPipe,viewport,scissor,vbind,bindPipe,draw`); canvas
  pixel analysis: **208,556 triangle pixels**, center pixel
  **[127, 64, 64]** — the RGB vertex-colored triangle's centroid blend on
  the dark-blue clear. No tier render errors.

This is the campaign goal at probe scale: guest D3D12 → vkd3d-proton →
vk64bridge → WebGPU, drawn and presented in the browser.

**Staging proof** (`dxvk-d3d12-staged1`): after `tools/vkd3d/stage-vkd3d.sh`
put tri12.exe + d3d12.dll + d3d12core.dll into `dist/runtime/prefix64.zip`
(`home/username/` and `home/username/.wine/drive_c/`), the staged harness
variant — which uploads NONE of those files — ran the guest to
`D3D12CreateDevice OK` / `RESULT 0` purely from the prefix, with
`WINEDLLOVERRIDES=d3d12=n;d3d12core=n`.

### 4.13 The complex scene (scene12): two more walls, one fixed, one mapped

scene12 (3 PSOs, depth buffer, checkerboard SRV texture, descriptor-table
root signature, 60 draws/frame) fails at `CreateGraphicsPipelineState(0)`
(E_FAIL). Diagnosis, in order:

1. `maxBufferSize of 0`: vkd3d refused a 512-byte constant buffer because
   the bridge's Vulkan 1.3 properties arm left `maxBufferSize` at 0.
   **Patch 10** models `maxBufferSize = 2^30` (matching
   maxStorageBufferRange) and subgroup sizes 32/32 (matching the
   SubgroupProperties arm). Verified: the refusal is gone from the
   scene4 run.
2. What remains is exactly one rejection, twice (both guest shaders):
   SPIR-V capability **5347 (PhysicalStorageBufferAddresses)**. The same
   shader bytecode compiles cleanly under an empty root signature
   (tri12draw) but vkd3d's conversion emits buffer-address modules when
   the root signature carries a descriptor table — the addressing model
   comes from the root-signature interface, not from the shader body
   (the pixel shader touches no resources). Our bridge advertises
   `bufferDeviceAddress = VK_FALSE` and no BDA extension, and
   vkd3d-proton 3.1 has no config steer away from this emission.
   WebGPU has no raw GPU-pointer model, so these modules reject loudly
   at creation. This is the campaign's next wall (§5).

Also logged in scene runs, non-fatal: `vkGetDeviceImageMemoryRequirements`
unimplemented (vkd3d tolerates the refusal today), and one
`texel-buffer-storage` descriptor-write rejection during heap setup.

## 5. Blocker stack (as of 2026-10-26, after patches 1–10)

**Servable by us (bridge/tier work, no ceiling involved):**
- D3D12 **compute execution**: compute pipelines now create; dispatches
  are loud-dropped (`compute-dispatch`). Serving them needs a V2 compute
  record + a tier compute path. This also unlocks vkd3d's UAV clears and
  indirect-draw helpers for real games.
- **Descriptor-addressed graphics shaders** (the scene12 wall): vkd3d
  emits PhysicalStorageBufferAddresses modules for descriptor-table root
  signatures. Two research routes: (a) find/force vkd3d's non-BDA
  interface (descriptor-indexing path) — but graphics modules using
  non-uniform descriptor indexing (capabilities 5301–5310, 5340s) are
  likewise rejected today, so the tier needs a binding-array story
  either way; (b) a tier descriptor-array expansion: rewrite heap-indexed
  accesses into WebGPU binding arrays sized to the heap. This is THE
  blocker for real D3D12 games (all use descriptor heaps + tables).
- `vkGetDeviceImageMemoryRequirements`: implementable from the existing
  image-requirements logic; vkd3d calls it on resource paths.
- Offscreen/present-less chunk delivery (tri12's clears serialize but no
  frames are captured page-side without a present): the readback path
  needs the same drain the present path gets. Affects verification, not
  presented apps.
- 32-bit properties walker lacks the transform-feedback and Vulkan 1.3
  properties arms added on the 64-bit side; maintenance6 and
  vertex-attribute-divisor *properties* arms are unmodeled on both
  (fields read 0; vkd3d clamps conservatively — cosmetic caps, not
  gates, in all probes so far).
- x86 vkd3d build (`src/build-win32.txt` exists; unattempted — x64 was
  the campaign scope).

**Buildable (packaging/process):**
- Prefix staging is proven for the probe payload; a production staging
  step should fold the vkd3d DLLs into the standard prefix build (the
  DXVK pattern) rather than a post-hoc zip edit, and decide the
  system32-vs-app-dir placement for arbitrary games (app-dir only works
  when the DLLs sit next to each exe; arbitrary D3D12 games need the
  prefix's system32 or a launcher overlay).
- The local-mirror rig for archive demos (127.0.0.1:8901) plus a
  dlloverrides/overlay channel is the known recipe for running
  godot-squash with `--rendering-driver d3d12`; its driver banner is the
  next real-program evidence to collect.

**Browser ceiling (WebGPU limits, established — not re-litigated):**
- No geometry/tessellation stages: geometry-stage pipelines record but
  their draws drop loudly at submit; tessellation/task/mesh reject at
  creation. Highest D3D12 feature levels stay unreachable; basic D3D12
  (what this campaign proves) does not need them.
- Raw buffer device addresses in shaders (see above): no WebGPU
  equivalent exists; only the descriptor-indexing *translation* route
  (tier work) can route around vkd3d's BDA emission.
- Layered rendering via VS-written gl_Layer: single-layer draws are
  served (the viewport/layer feature bits are advertised); genuinely
  layered draws reject loudly in the tier.

## 6. Next steps

1. Descriptor-addressing research lane: determine whether vkd3d can be
   steered to its descriptor-indexing interface by feature/extension
   advertisement alone; in parallel, design the tier's binding-array
   expansion for heap-indexed shader accesses. Section 4.17 PROVES
   this item gates even the simplest typed-UAV execution (vkd3d emits
   cap 5347 for every descriptor-using shader; no config opt-out), not
   just full games - it is the D3D12 campaign's critical path.
2. V2 compute record + tier compute dispatch (unlocks D3D12 compute,
   UAV clears, execute-indirect).
3. DONE (lane 2, section 4.15): godot-squash `--rendering-driver d3d12`
   re-run on the restaged environment - the maintenance5/6 gate passes
   and D3D12 device creation completes; the run now stops one layer
   deeper, at vkd3d's shader-model 6.0 gate (next item).
4. Implement `vkGetDeviceImageMemoryRequirements` from the existing
   image-requirements code. (Godot's run calls it during device init
   and tolerates the FEATURE_NOT_PRESENT answer today; it is not the
   current blocker, but it is on the real-program path now.)
5. DONE in part (lane 2): vkd3d staging is folded into the standard
   pipeline (build-wine64-zip.sh staging block, stage-vkd3d-payloads.sh
   merge path, apply-runtime-poststeps.sh, completeness pins). Still
   open: x86 vkd3d build.
7. DONE (SM 6.0 caps lane, section 4.16): shader model 6.0 is granted.
   The gate needed two conjuncts, not one: uniformBufferStandardLayout
   (honest half of the layout bit; scalarBlockLayout stays FALSE — its
   shapes match no WGSL layout) served by spirvfix R13's
   uniform-block-to-storage demotion, AND shaderInt16 (section 4.15
   wrongly recorded it as already served). Guest A/B: highest shader
   model 0x51 -> 0x60. Godot's D3D12 driver now passes the
   shader-model sweep and stops at the next capability check, Typed
   UAV Load Additional Formats (a D3D12_OPTIONS format question —
   follow-on lane); shader compilation (DXIL) and descriptor
   addressing (cap 5347, item 1) still wait behind it. FOLLOW-ON DONE
   (section 4.17): Typed UAV Load Additional Formats is now granted
   (guest A/B: flag FALSE -> TRUE, servable sweep 15/18 -> 18/18);
   actual typed-UAV execution is blocked by item 1's cap-5347 wall,
   proven at the vendored-source level.
6. Present-less chunk drain so offscreen probes (tri12) verify pixels,
   not just API success.
8. RESOLVED (see 4.18): bisect the guest-winevulkan-path
   regression on the 4.17 dist - tri12draw dies on a winevulkan
   loader_thunks assert during swapchain init and Godot's DXGI factory
   creation fails through DXVK (VK_KHR_surface reported unsupported),
   while the direct-ICD probes (tri12, uav12) work on the same dist
   and the guest archives are byte-identical to the reference tar.
   Clean HEAD-code dist vs 4.17 dist A/B is the first step; see 4.17.

## 4.14 Real program: godot-squash --rendering-driver d3d12 (2026-10-26, post-commit runs)

Six demo-matrix attempts against the final dist (build #13). Attempts 1-3 were
rig failures, recorded for the next runner: (1) the demo archive is fetched
from the production origin, which emits no CORS headers - the local mirror
rig (127.0.0.1:8901, CORS *) only helps if the SERVED demos list points at
it (dist/demos.mjs archiveBase; the gamezip URL param is overridden by the
launcher and does NOT work); (2) demo-matrix waits on the shared build lock
and its wait can time out under other lanes' builds.

Attempt 4 (mirror fixed): the engine BOOTED - banner "Godot Engine
v4.7.2.stable.official", stage=mapped at 206.6s - and entered its D3D12
driver, failing at load: ERROR: Parameter "lib_d3d12" is null.
(d3d12.dll not found: demos run with working dir home/username/apps/, the
game archive's extract root; the prefix staging of section 4.12 lands in
home/username/, one level up, and demos have no app-dir copy.)

Attempt 5 (+ overlay zip with d3d12.dll/d3d12core.dll at
home/username/apps/, served from the mirror as vkd3d-overlay.zip, mounted
via ?overlay=&overlaysize= - the same channel the Portal DXVK probe uses):
lib_d3d12 error GONE, DLL loaded; new failure one layer deeper:
ERROR: D3D12CreateDevice failed with error 0x80070057.
(_initialize_device, rendering_device_driver_d3d12.cpp:6025.)

Attempt 6 (same + VKD3D_DEBUG=trace): vkd3d-proton names the gate verbatim:
"err:vkd3d-proton:vkd3d_init_device_caps: maintenance5 and/or maintenance6
not supported by this implementation. This is required for correct
operation." That is this lane's patch-2 gate. CONCLUSION: the demo
environment's Vulkan chain (Wine builtin winevulkan + the libvulkan.so.1
staged inside the pre-campaign support/rootfs archives) predates ALL ten
bridge patches of this lane; the d3d12 probe harness bypasses those
archives (it uploads the worktree's fresh shim and .so), which is why
the probes pass and the demo does not. Step 5 therefore reduces to a
PACKAGING restage: rebuild the rootfs/support staging from the patched
tree (the pipeline that produces tools/rootfs64/libvk64 and the support
archive), after which the demo should meet the same device caps the
probes verified. No new bridge code is implicated by this failure.
framesPresented stayed 0 in all Godot attempts; the engine fell back
after the D3D12 error (screenshots show a static ~5.6k-lit-px screen).

## 4.15 Lane 2: demo-environment restage + Godot re-run (2026-10-26)

Mission: close the gap section 4.14 named - the demo environment's
Vulkan chain predated the bridge patches, so Godot met the
maintenance5/6 gate while the probes (which upload fresh binaries)
passed. Outcome: the gate is gone in the demo environment, D3D12
device creation completes inside Godot, and the run stops at a new,
precisely identified layer (shader-model 6.0 caps, below). No bridge
code changed in this lane; everything is packaging plus one harness.

### Root cause, refined (two layers, both proven on the box)

Lane 1's framing ("stale rootfs/support archives") was half the story.
Hash archaeology: the guest ELF shim in the support archive
(libvulkan.so.1, md5 0e39a3f4...) is byte-identical to the current
tools/rootfs64 build - probes and demos share it. The stale link is
the PE side: demos resolve Vulkan through Wine 8.0's builtin
winevulkan, whose conversion tables (Vulkan 1.3.239 registry) know
only VK_KHR_maintenance1/2 - the bridge's patched caps never reach
vkd3d. And the decisive detail, read from the vendored source
(tools/vkd3d/src/libs/d3d12core/main.c, load_modules_once): the
Boxedwine64 patch makes vkd3d try loader DLLs in the order
"bw64vulkan.dll", "winevulkan.dll", "vulkan-1.dll". Probes pass
because they upload bw64vulkan.dll next to the exe (first choice
hits); demos fell through to winevulkan because no bw64vulkan.dll
existed anywhere in the demo environment. Experiments S1-S3 confirmed
the negative space: replacing system vulkan-1.dll with the shim, a
user.reg DllOverrides entry, and a WINEDLLOVERRIDES vulkan-1=n all
left the gate standing (the shim was third in line and never reached).
The fix is therefore additive and fleet-safe: stage bw64vulkan.dll
under its own name at system placement.

### What was staged (wine64.zip, web/runtime + dist/runtime)

- usr/lib/x86_64-linux-gnu/wine/x86_64-windows/bw64vulkan.dll
  = tools/vkd3d/probe/bw64vulkan.dll (159,504 B, sha256 8084b4de...).
  The prefix's system32 is a .link into this tree, so plain
  LoadLibrary search finds it - no app-dir copies, no overrides.
- d3d12.dll (517,717 B, sha256 d77fdad5...) and d3d12core.dll
  (8,825,383 B, sha256 d2bd847a...) replaced by the vkd3d-proton
  3.1.0 build at the same system placement; d3d12core's previous
  provenance (winehq10 backport) is retired - the pair moves together.
- vulkan-1.dll deliberately UNTOUCHED (Wine's 83,473 B forwarder,
  sha256 f7d151a8...): with bw64vulkan.dll present vkd3d never falls
  through to it, and DXVK demos keep the exact winevulkan path they
  were verified on. (An interim vulkan-1.dll swap was staged, tested
  in S1-S3, and restored byte-exact from backup.)

### Pipeline encoding (a clean build reproduces this state)

- tools/rootfs64/stage-vkd3d-payloads.sh (new): merge-script staging
  of the three members into web/runtime + dist/runtime wine64.zip;
  idempotent, byte-verified, backups under /home/ubuntu/backups.
- tools/rootfs64/build-wine64-zip.sh: preflight now hard-fails when
  any of the three payload files is missing, and the staging step
  copies them into the wine tree (canonical container path).
- tools/rootfs64/check_runtime_completeness.py: new VKD3D_PINS +
  'vkd3d payloads (source + shipped zip)' check; winehq10 pin set
  drops d3d12core. Gate result on the shipped set: 0 failures.
- tools/rootfs64/apply-runtime-poststeps.sh runs the vkd3d step;
  re-run on the finished set exits 0 ("already complete").
- tools/vkd3d/stage-vkd3d.sh now stages only tri12.exe into the
  prefix (system placement carries the DLLs) and strips any legacy
  app-dir d3d12 copies.
- tools/browser/runtime-assets.json re-pinned (wine64.zip plus a
  container-only re-zip drift of the two 32-bit archives - member
  CRCs verified identical); retained tar rebuilt as
  browser-runtime-20261026.tar (512,153,600 B, sha256 78f3d7f2...)
  in /home/ubuntu/backups/winbox-runtime-assets-20261026/; bootstrap
  verifies 13 files. (Uploading the tar to the GitHub release
  remains user-gated, as with the 20261025 set.)
- Final export via bw64-build.sh --export-only: "Browser export
  verified: 34 .mjs syntax files; ... dependency archive
  108f781c5c8d verified" and the in-export completeness run passes
  the vkd3d check.

### Probe verdicts

- New harness web/tests/d3d12-system.mjs: the staged draw probe with
  NOTHING uploaded and no LD_LIBRARY_PATH - the demo's exact
  resolution path. S1-S3 (pre-discovery configurations): FAIL at the
  maintenance gate. S4 (bw64vulkan.dll staged): tri12 RESULT 0,
  "D3D12CreateDevice OK", 10 frames cleared+submitted. (S4's printed
  verdict was false only because the harness's failure regex matched
  a benign vkd3d INFO line from vkd3d_init_device_caps about EXT_dgc;
  the regex was narrowed to the real failure signatures.) S5 with the
  fixed verdict: passed:true.
- First-light regression on the final dist (d3d12-pixels.mjs,
  run test-results/dxvk-d3d12-draw-restage1): passed:true,
  208,556 triangle pixels, center [127, 64, 64] - identical to
  lane 1's first-light numbers. The restage did not regress the
  proven path.

### Godot re-run (test-results/demo-matrix/godot-squash-jit0-20261026T203028)

DEMO=godot-squash, JIT=0, no overlay zip (system placement replaces
it), WINEDLLOVERRIDES=d3d12=n;d3d12core=n, VKD3D_DEBUG=trace.
Engine mapped at 168.6s. The maintenance5/6 gate line is ABSENT.
vkd3d-proton runs inside the game (instance + device extension
checks, vkd3d_create_vk_device at 161.1s, format-property probing at
164.4s): D3D12 device creation completes - the exact step that
returned 0x80070057 in every lane-1 attempt. The run then stops at
the NEXT layer, named by Godot at 165.3s:

  ERROR: No support for any of the suitable shader models (6.0-6.6)
  has been found.
  ERROR: Failed to initialize driver for device.
  WARNING: Your video card drivers seem not to support Direct3D 12,
  switching to Vulkan.  (177.9s; the Vulkan fallback presented 0
  frames in the run window, demo verdict FAIL on display only)

Root cause, from the vendored source: vkd3d grants shader model 6.0
only if (device.c, the d3d12_caps shader-model gate) subgroup props,
scalar block layout, and shaderInt16 all hold. Subgroups
(size 32, ops/stages all-ones) and shaderInt16 are served; the layout
conjunct is not: vkd3d chains only VkPhysicalDeviceVulkan12Features
(device.c:1238) and reads its scalarBlockLayout /
uniformBufferStandardLayout fields, and the bridge's
fillVulkan12Features (vk64bridge.cpp:4498) sets neither - while the
standalone VkPhysicalDeviceScalarBlockLayoutFeatures filler,
fillScalarBlockLayout (vk64bridge.cpp:4494), is an empty stub.
max_shader_model therefore stays below 6.0 and Godot's 6.6-to-6.0
CheckFeatureSupport sweep fails every entry. Fixing it is a bridge
caps decision (is scalar block layout honestly servable by the page
tier's buffer layout rules?) - section 6 item 7. Also observed on
this path: vkGetDeviceImageMemoryRequirements is called during
device init and its FEATURE_NOT_PRESENT answer is tolerated today
(section 6 item 4); it is not this blocker.

framesPresented stayed 0, as in lane 1 - but the campaign state
moved: the D3D12 wall for real programs is no longer packaging; it
is one named capability bit away from driver initialization, with
shader compilation (DXIL) and descriptor addressing (cap 5347)
behind it.

## 4.16 SM 6.0 caps lane: shader model 6.0 granted; Godot stops one layer deeper, at Typed UAV Load Additional Formats (2026-10-26)

Section 6 item 7, executed. Correction to section 4.15 first: it says
the gate's subgroup and shaderInt16 conjuncts "are served". Subgroups
are; **shaderInt16 was not** — `fillCoreFeatures` never set it and the
capability manifest listed it under `notAdvertised`. The gate had TWO
missing conjuncts, not one; the first build of this lane (layout bit
only) still reported SM 5.1 in the guest, which is how the second was
found. The gate (vendored device.c, `d3d12_device_caps_init_shader_model`):
subgroupSize >= 4 + required subgroup ops/stages +
(`scalarBlockLayout` OR `uniformBufferStandardLayout`) + `shaderInt16`,
all read from the chains the bridge fills.

### Layout honesty (the layout conjunct)

Proven with hand-built SPIR-V shapes run through the tier's own path
(spirvfix + vendored naga, the same conversion the page tier performs),
not by argument:

- WGSL's storage address-space layout IS std430, byte for byte; WGSL's
  uniform layout is not — array strides, struct/array member alignment
  and struct sizes there round up to 16 (matrices excepted: their stride
  rule is the vector rule in both spaces). A std430-packed uniform block
  — exactly dxil-spv's D3D12 cbuffer form, a wrapper struct around a
  tightly-strided array whose element type is the shader's widest access
  (float -> stride 4, vec2 -> stride 8) — is rejected by naga as
  `var<uniform>` ("The array stride 4 is not a multiple of the required
  alignment 16") while being exactly representable one address space
  over. The page tier derives bind-group layout entries and GPU buffer
  usage from the converted WGSL declaration, so a storage demotion
  propagates end to end with no page-tier change.
- Scalar packing is NOT servable in any WGSL address space: vec3 arrays
  at stride 12 and MatrixStride-12 matrices match neither WGSL layout
  (WGSL fixes vec3 array stride at 16 and mat3x3 column stride at 16).
  One shape is worse than rejected: a mat3 block at MatrixStride 12 is
  CONVERTED SILENTLY WRONG by naga today (columns read at stride 16
  from stride-12 data). That hazard pre-dates this lane, is named here
  for the register, and is not a shape the descriptor-mode translators
  emit; a module-creation layout audit is the fix shape, not attempted
  here.
- In dxil-spv the `SCALAR_BLOCK_LAYOUT` compile option's only codegen
  effect is NPOT-vec3 vectorization of buffer loads — i.e. advertising
  scalar layout would switch the translator INTO the inexpressible
  shapes. `uniformBufferStandardLayout` has no codegen consumer at all;
  its only consumer in vkd3d is the SM gate itself.

Grant: `uniformBufferStandardLayout = VK_TRUE` in the monolithic
Vulkan 1.2 features struct AND the standalone features struct, both
ABIs (new `fillUniformBufferStandardLayout` + walker arms; the guest
headers lack the struct, so the bridge carries exact-layout mirrors,
24 B / 12 B, static_asserted). `scalarBlockLayout` stays VK_FALSE.

Serving: spirvfix R13 `uniform-block-to-storage`. A DescriptorSet-bound
Uniform block variable whose decorated layout is storage-expressible
but not uniform-expressible is demoted to a read-only StorageBuffer
variable (storage-class flip on the variable and every pointer type its
access chains produce, twins inserted before the variable; variable-level
NonWritable so naga emits `var<storage>` read — legal in every stage —
not `var<storage, read_write>`). The checker requires decorated
ArrayStride/MatrixStride/member Offset to EQUAL the WGSL-computed
values under each layout (equality, not >=: a stride-16 decoration over
a stride-4 type is a different silent-misread shape, also proven in the
matrix). Fail-closed: bool members, runtime arrays in uniform blocks,
spec-constant lengths, shared structs (pre-1.3), or a block pointer
escaping the access-chain trace (function call, store, phi, select)
all refuse the demotion and leave the module to its previous loud
outcome. `spirvfix-r13.test.mjs`: 11 tests over 9 fixtures (flip set,
uniform-expressible controls, scalar negatives, phi-escape refusal,
decoration sabotage); spirvfix corpus gate stays 22/22.

### shaderInt16 (the second conjunct)

`shaderInt16 = VK_TRUE` in `fillCoreFeatures`, manifest moved from
`notAdvertised` to implemented. Boundary, recorded in the register row:
vkd3d's only other consumers of the bit are conjunctive on
shaderFloat16 / storageBuffer16BitAccess / shaderInt64 + 8-bit storage
(all honestly FALSE here), so the bit selects no 16-bit code path by
itself, and vkd3d-shader treats Int16 as a hard requirement of its
translator. A module that actually declares the Int16 capability still
fails loudly at `vkCreateShaderModule` (`spirv-capability:Int16`):
browser WebGPU has no 16-bit integer type and naga can only emit the
non-standard `enable wgpu_int16` (proven on a hand-built Int16 module:
correct native-i16 WGSL, unusable in a browser). The bit grants the
shader MODEL — SM 6.0 translation for shaders that do not use native
16-bit integer ops, which the tier serves end to end.

### Verification

- Guest A/B (`tools/vkd3d/probe/sm12.c`: D3D12CreateDevice +
  CheckFeatureSupport(D3D12_FEATURE_SHADER_MODEL) asking down from 6.6,
  run via web/tests/d3d12-system.mjs, system placement): BEFORE —
  highest shader model 0x51, RESULT 1 (test-results/dxvk-sm12-before);
  layout-bit-only build — still 0x51; AFTER both grants — **highest
  shader model 0x60, RESULT 0** (test-results/dxvk-sm12-after2).
- Godot re-run (DEMO=godot-squash, JIT=0, local games mirror;
  test-results/demo-matrix/godot-squash-jit0-20261026T222709):
  vkd3d-proton runs in-game, D3D12 device creation completes, and the
  shader-model sweep PASSES — the "No support for any of the suitable
  shader models (6.0-6.6)" error is absent. Driver init proceeds into
  `_check_capabilities` and stops at the NEXT named requirement, 181.2s
  into the run, verbatim:

    ERROR: No support for Typed UAV Load Additional Formats has been found.
       at: _check_capabilities (drivers/d3d12/rendering_device_driver_d3d12.cpp:6166)
    ERROR: Condition "err != OK" is true. Returning: ERR_CANT_CREATE
       at: initialize (drivers/d3d12/rendering_device_driver_d3d12.cpp:6402)
    ERROR: Failed to initialize driver for device.

  Then the engine falls back to Vulkan (0 frames presented, as in prior
  runs). The wall behind the SM gate is therefore a FORMAT capability —
  D3D12_OPTIONS TypedUAVLoadAdditionalFormats, which vkd3d derives from
  typed-load format support beyond the core set — not shader compilation
  and not the cap-5347 descriptor wall; those still wait behind it for a
  program that gets past this check. vkGetDeviceImageMemoryRequirements
  was called during init and tolerated again (section 6 item 4).
- First-light regression: the staged draw probe is pixel-identical —
  passed, 208,556 triangle pixels, center [127, 64, 64]
  (test-results/dxvk-sm6-draw2).
- JS unit suite (`node web/tests/suite/runner.mjs --unit`,
  BW_TEST_DIST=/home/ubuntu/wt-vkd3d/dist): 286 passed / 8 failed. All
  8 failures reproduce at pristine HEAD 0b82f40b6 against the same dist
  (packaging-prefix, provenance-artifacts, v2-draw-contract,
  vk-image-dirty-generation, vk-register-citations, vk-sampler-rejection,
  vk32-properties, vulkan-sampler): zero regressions from this lane, and
  two improved (vk-register-citations 2/2 -> 3/1, provenance-artifacts
  2/6 -> 7/1 in per-file subtests). The one remaining citations failure
  is the pre-existing coverage pin (uncheckable rows 109 vs ceiling
  102 — already the HEAD state).
- Register: 353 rows = 49 IMPLEMENTED / 71 REJECTED-LOUDLY / 233
  NOT-A-GAP / 0 OPEN, with one new row for this grant. This lane also
  re-pinned the register's rotted citations in this tree (the audit tool
  reports 0 DRIFT / 0 MISSING over 326 cited rows; 43 rows moved from
  DRIFT/MISSING to MATCH, including the transform-feedback row, whose
  disposition text was corrected to the current code: the extension
  string and transformFeedbackQueries are advertised for vkd3d's device
  gate, all stream-out bits/limits stay off).


## 4.17 Typed-UAV-Load lane: Typed UAV Load Additional Formats granted; execution proven blocked by the cap-5347 descriptor wall; a winevulkan-path regression found (2026-10-27)

The follow-on lane from 4.16 item 7, verified end to end on lane-vkd3d
(dist built 2026-10-26T23:34:45Z, wasm sha 0461611f4486167b...; JS unit
suite 287 passed / 8 failed, the same 8 pre-existing failures as the
c3dc7f43a baseline - zero new).

### The capability and the grant

Godot's stop was D3D12_OPTIONS TypedUAVLoadAdditionalFormats. vkd3d
derives it per format: for each of its 18 additional formats
(d3d12_device_determine_additional_typed_uav_support) the format's
VkFormatProperties2 feature flags (carried in the flags2 chain word)
must include VK_FORMAT_FEATURE_2_STORAGE_IMAGE_BIT plus
VK_FORMAT_FEATURE_2_STORAGE_READ_WITHOUT_FORMAT_BIT (bit 0x80000000) -
the latter is what says a shader may load the UAV without declaring
the format (DXIL typed loads). The bridge advertised storage-image for
only part of the set and READ_WITHOUT_FORMAT for none, so the answer
was FALSE with 15 of 18 formats unservable.

The grant, all layers traced and each one load-bearing:

- Bridge (source/vulkan/vk64bridge.cpp): a 30-format servable set
  (every format the page tier's texture-format tables can bind as a
  storage texture, incl. 17 integer formats added to those tables)
  receives STORAGE_IMAGE + STORAGE_READ_WITHOUT_FORMAT in the
  optimal/linear/buffer arms of BOTH format-properties walkers (64-bit
  and 32-bit property paths);
  fillCoreFeatures.shaderStorageImageReadWithoutFormat = VK_TRUE
  (line 4420). WRITE without format stays FALSE - the tier cannot
  serve storage writes (no GPU->guest readback), and the audit still
  rejects any module that writes a storage image, now under its own
  token spirv-storage-image-write (vk64bridge.cpp:626); read-only
  storage modules pass the audit, and capabilities 55/56
  (StorageImageRead/WriteWithoutFormat declarations) are exempt when
  the module does not write.
- spirvfix R14 (storage-image-format): a DXIL typed-load module
  declares its storage image type with ImageFormat Unknown; R14
  injects the bound view's concrete format from the module's binding
  map, fail-closed and kind-checked (a non-2D view or an unsampled
  kind rejects rather than guessing).
- Page tier (web/vkwebgpu.mjs, web/vk-protocol.mjs): the create-view
  record now carries the image's storage format (V2_STORAGE_FORMAT,
  vk-protocol.mjs:373); storage bindings create real texture views
  with STORAGE_BINDING usage instead of falling back to sampled
  views, so a typed-load bind is servable at draw time, not just
  advertised at query time.

### Guest A/B (probe tools/vkd3d/probe/uav12.c, new)

BEFORE (runs dxvk-uav-before{,2,3,4}, HEAD code):
TypedUAVLoadAdditionalFormats = FALSE; 15 of 18 sweep formats
UAV_TYPED_LOAD=NO; all pipeline creations fail; RESULT 1.
AFTER (run dxvk-uav-after, dist above): flag = TRUE; all 18 sweep
formats UAV_TYPED_LOAD=YES (Support2 bit 0x40 on every case). The 18,
as Vulkan formats: 100, 98, 99, 109, 107, 108, 97, 95, 96, 37, 41,
42, 76, 74, 75, 9, 13, 14 - pinned by unit test
vk-storage-format-mirror.test.mjs (D3D12 additional set == bridge
servable set).

### Execution: two gates, the second is the campaign's wall

Phase 3 of the probe executes typed loads for 6 format classes
(R8G8B8A8_UNORM, R32G32B32A32_FLOAT, R16G16B16A16_FLOAT, R16_FLOAT,
R32_UINT, R8_UINT) with per-texel readback checks. RESULT is still 1:
execution does not complete. Two distinct gates were isolated:

1. Probe-side (FIXED): vkd3d-proton silently rejects (E_INVALIDARG,
   no warning even at VKD3D_DEBUG=warn) any graphics PSO whose vertex
   shader's input signature declares only SV_VertexID. Wine's
   in-guest D3DCompile is exonerated (the blobs parse as well-formed
   DXBC). The probe now uses a pass-through VS with declared
   POSITION/COLOR inputs, a real vertex buffer, and a matching input
   layout; with that, all six cases advance to gate 2.
2. The wall: a graphics PSO whose root signature carries a UAV
   descriptor table fails creation with E_FAIL, and the console shows
   exactly one bridge rejection per case immediately before it:
   spirv-capability-unknown:5347 (VK_ERROR_FEATURE_NOT_PRESENT) in
   vkCreateShaderModule. Vendored-source mechanism:
   vkd3d_bindless_state_get_bindless_flags
   (tools/vkd3d/src/libs/vkd3d/state.c) sets
   VKD3D_RAW_VA_ROOT_DESCRIPTOR_SRV_UAV unconditionally - EVERY
   descriptor-using D3D12 shader is compiled to SPIR-V declaring
   capability 5347 (PhysicalStorageBufferAddresses) - and
   config_flag_decl.h offers no opt-out flag. The bridge's rejection
   is honest (bufferDeviceAddress is VK_FALSE; WebGPU has no
   equivalent). Shaders under an empty root signature (tri12draw's)
   never declare it, which is why first light worked while no
   descriptor-carrying PSO ever has in this stack. Conclusion: item 1
   of section 6 (descriptor addressing) gates even the simplest
   typed-UAV load, not just real games; the flag grant above is real
   and necessary, and execution waits on that research.

### Godot and first light: a regression on this dist, not isolated

demo-matrix godot-squash JIT=0 on this dist (run
godot-squash-jit0-20261027T014448): the console contains ZERO
occurrences of the Typed UAV Load Additional Formats stop - that gate
is passed. But driver init stopped EARLIER than the HEAD-era runs:
the DXGI factory creation goes through the prefix's DXVK dxgi.dll,
whose Vulkan instance creation reports "Required Vulkan extension
VK_KHR_surface not supported", and Godot returns ERR_CANT_CREATE at
_create_dxgi_factory. The HEAD-era runs reached vkd3d device
creation on the same guest archives: wine64.zip on this dist is
entry-for-entry identical to the reference runtime tar (1257
entries; bw64vulkan.dll, the vkd3d pair, winevulkan.dll, vulkan-1.dll
all same sizes), prefix64.zip differs only by the staged probe exes,
and the bridge's static instance-extension list still advertises
VK_KHR_surface. The same dist also regresses first light: tri12draw
now dies during dxgi_vk_swap_chain_init on a winevulkan assertion
(loader_thunks.c:3097, !status) after back-buffer creation, where
HEAD-era runs presented the 208,556 px triangle. Both failing paths
traverse the guest winevulkan thunk layer; the direct-ICD probes
(tri12, uav12) are unaffected on the same dist. Cause NOT isolated -
it sits in this lane's runtime-code delta or its interaction with
the thunk path. First light is therefore UNVERIFIED (regressed) on
this dist, and a clean-dist A/B bisect is the campaign's top
follow-up (section 6, item 8). This lane's commit proceeds on its own
evidence: the capability grant is guest-verified by uav12, the unit
suite shows zero new failures, and the register (below) records the
execution block honestly.

### Register

docs/known-gaps/vulkan.md: 354 rows = 51 IMPLEMENTED / 71
REJECTED-LOUDLY / 232 NOT-A-GAP / 0 OPEN. The storage-image module
row is narrowed to writers (REJECTED-LOUDLY, spirv-storage-image-
write); the two storage-image binding rows are re-dispositioned to
the implemented read serve; one new row records this grant and its
execution block. The implementation's insertions had drifted 68
citations; all re-pinned mechanically (git-diff line mapping,
exact-site duplicates re-pinned with their primaries): citation
audit 0 DRIFT / 0 MISSING over 327 cited rows, checked coverage
back at the HEAD baseline (the citations suite test fails only its
pre-existing coverage-ceiling subtest, byte-identical message to
pristine HEAD).

## 4.18 Winevulkan-path bisect: both 4.17 symptoms acquit the change set - one pre-existing system-placement gap, one intermittent guest driver-init flake (2026-10-27)

Method: clean-dist A/B. Parent c3dc7f43a built fresh in this tree
(its WASM is bit-identical to the SM6-era build, sha 2c19eb44, the
same hash the HEAD-era Godot runs record); the c52f39d4e dist was
rebuilt the same way. Page-tier subsets were swapped with
--export-only rebuilds (all four page files; then vkwebgpu+spirvfix
vs vk-protocol splits). Godot verdicts are per-run guest-console
evidence (init_vulkan error count / vkd3d_create_vk_device count /
bridge trap count) under WINEDEBUG=+vulkan (one run also +loaddll).

Finding 1 - the tri12draw system-placement assert PRE-DATES the
change set; it is not a regression. Disassembly of the dist's own
guest winevulkan.dll places the loader_thunks.c:3097 assert site
inside vkCreateWin32SurfaceKHR (unix call 0x130): the unix side
returns a failing NTSTATUS before any bridge trap fires. The parent
dist fails d3d12-system.mjs tri12draw with the identical assert
(test-results/dxvk-bisect-system-parent3), and the c52 dist fails it
too (dxvk-draw-final{,2}, dxvk-bisect-system-cur). The HEAD-era
first-light evidence was always the UNSTAGED probe, which uploads
bw64vulkan.dll as vulkan-1.dll and bypasses winevulkan entirely;
the only system-placement runs at HEAD were tri12.exe, which is
clear-only and never creates a surface or swapchain. The 4.17 lane's
new prefix staging of tri12draw.exe was the first exercise of the
system-placement swapchain path - it uncovered a gap it did not
cause. On the unstaged path the c52 dist still presents the pinned
first light exactly: 208,556 px, center [127,64,64]
(test-results/dxvk-bisect-unstaged-cur). NEW FOLLOW-UP (not a
regression): the system-placement vkCreateWin32SurfaceKHR unix call
fails deterministically in the probe session; demo-path games create
DXVK swapchains through the same guest call successfully in the
launcher context, so the defect may be specific to the probe
session's window setup. Root cause not isolated in this lane.

Finding 2 - the Godot DXGI/init_vulkan failure is INTERMITTENT and
acquits the change set. Run table (demo-matrix, godot-squash, JIT=0):
c52f39d4e dist: FAIL, FAIL, PASS. Parent c3dc7f43a dist: PASS
(stops at the Typed-UAV line, the 4.16-era wall). c52 WASM + parent
page tier: PASS. c52 WASM + parent vkwebgpu/spirvfix + c52
vk-protocol: FAIL, then PASS on the identical dist. In passing runs
on the c52 dist Godot clears DXGI factory creation and D3D12 device
creation (vkd3d_create_vk_device x14), passes the SM 6.0 sweep and
the Typed-UAV gate, and stops at the cap-5347 wall in
vkCreateShaderModule (test-results/demo-matrix/godot-bisect-c52again:
829 bridge traps, stage mapped) - exactly the section 6 item 1 wall.
In failing runs the guest prints err:vulkan:init_vulkan "Failed to
load Wine graphics driver supporting Vulkan", the Godot process
never connects to the X server (2 XWire accepts in the run vs 5 in
passing runs) and never issues a bridge trap; the bridge's instance
and physical-device answers are fully static, so neither the page
tier nor the bridge can gate them. Failing runs cluster on slow
guest boots under heavy multi-lane box load (the guest reached its
Vulkan stage at 285-570 s vs 136-215 s in passing runs). No file in
c3dc7f43a -> c52f39d4e is convicted. Section 4.17's regression
framing is corrected: first light was UNVERIFIED on that dist (now
verified, above), and the Godot stop was a flake sighting, not a
delta effect. The Typed-UAV grant stands as committed: on the final
rebuilt c52 dist, uav12 reports TypedUAVLoadAdditionalFormats =
TRUE with the sweep 18/18 YES (test-results/dxvk-bisect-uav-final),
and the JS suite is 288 passed / 7 failed against the 287/8
baseline in this tree's config - the same pre-existing failure set,
zero new (packaging-prefix, v2-draw-contract,
vk-image-dirty-generation, vk-register-citations,
vk-sampler-rejection, vk32-properties, vulkan-sampler).

Remaining suspect for the flake: guest winex11.drv unix-side init
(driver registration / libvulkan dlopen) racing session startup
under load. It matches the BDA lane's system-probe desktop timeout
(their dxvk-uav-route0-explorerfail run). Next evidence would be
emulator-loader instrumentation on the winex11.drv.so init path
plus repetition statistics on a quiet box. It is not a blocker for
the D3D12 stack and no register disposition changes.

Section 6 item 8: CLOSED by this section (no code change).

## 4.19 System-placement surface lane: the draw-probe abort was a Vulkan handle-domain collision at Wine dxgi's surface factory — fixed by resolving every PE Vulkan name to the bw64 shim (2026-10-27)

§4.18 left one scoped follow-up: the system-placement draw probe (tri12draw via d3d12-system.mjs) aborts deterministically at Wine's loader_thunks.c:3097 assert inside vkCreateWin32SurfaceKHR (unix call 0x130), before any bridge trap, while the unstaged path renders first light and demo-path DXVK swapchains work. §4.18's named suspect — probe-session window setup — is ACQUITTED; the failure is a product gap in the system-placement configuration itself, and it is now FIXED and verified.

ROOT CAUSE (verified at instruction level; this lane's runs test-results/dxvk-surface-repro1/, dxvk-surface-diag1/, dxvk-surface-diag2/, dxvk-surf-diag-module2/ on the pre-fix dist):

1. The vendored vkd3d-proton's loader tries bw64vulkan.dll FIRST (Boxedwine64 patch, tools/vkd3d/src/libs/d3d12core/main.c) to bypass Wine winevulkan's extension filtering (its tables predate VK_KHR_maintenance5/6). vkd3d's VkInstance is therefore a BRIDGE handle: an opaque synthetic id (observed 0x564b0102), not a pointer to guest memory.
2. Wine's builtin dxgi.dll implements the D3D12 swapchain surface factory by LoadLibrary'ing winevulkan.dll BY NAME. Proven by a WINEDEBUG=+module loader trace of the failing run (dxvk-surf-diag-module2): the tri12draw process loads bw64vulkan.dll and winevulkan.dll, and never loads vulkan-1.dll at all. (An earlier strings-based reading blamed vulkan-1.dll; the loader trace is definitive. The unstaged probe's own comment — "dxgi LoadLibraryA("vulkan-1.dll") must resolve to our PE shim" — describes DXVK's dxgi.dll, which the unstaged probe uploads; system placement uses Wine's dxgi, which names winevulkan.dll.)
3. winevulkan's unix handler (wine_vkCreateWin32SurfaceKHR, winevulkan.so file offset 0x1b300) treats the instance as a pointer to its own wrapper struct; its first field read (mov 0x8(%rdi),%r12) dereferences handle+8. With a bridge handle that address (observed 0x564b010a) is unmapped. WINEDEBUG=+seh shows handle_syscall_fault code=c0000005 addr=0x564b010a at that exact instruction (winevulkan.so+0x1b314); Wine's fault handling converts the access violation to STATUS_ACCESS_VIOLATION returned to the PE thunk, whose assert (!status, loader_thunks.c:3097; unix call 0x130 verified by static disassembly of the shipped winevulkan.dll) aborts the process. The window, its flags, its visibility, and the HWND in the create-info are never reached — tri12draw's window creation is textbook WS_OVERLAPPEDWINDOW|WS_VISIBLE, and the fault precedes any read of the create-info. A preceding vkGetInstanceProcAddr("vkCreateWin32SurfaceKHR") faults the same way (winevulkan.so+0x703b, same +8 read); the gipa name-table fallback still returns the thunk, which is why the observable sequence is "thunk64_vkCreateWin32SurfaceKHR traced, then assert, nothing in between".

WHY THE OTHER PATHS WORK: the unstaged probe (d3d12-pixels.mjs) uploads DXVK's dxgi.dll plus the bw64 PE shim under the names bw64vulkan.dll and vulkan-1.dll in the app dir — one handle domain (the bridge) serves everything. Demo-path games and Godot resolve their Vulkan through Wine's loader into the winevulkan domain, whose ICD underneath is the same bridge. Only the system-placement D3D12 combination (vkd3d on bw64vulkan.dll + Wine dxgi on winevulkan.dll) mixed domains.

FIX (pipeline staging only — no bridge, vkd3d, or Wine code changes): stage the bw64 PE shim under ALL THREE PE Vulkan names in the wine tree (usr/lib/x86_64-linux-gnu/wine/x86_64-windows/{bw64vulkan.dll, vulkan-1.dll, winevulkan.dll}), so every PE Vulkan client at system placement resolves to one handle domain, the bridge. The shim was originally built AS vulkan-1.dll (tools/vkd3d/probe/VULKAN1_DLL_README.md: gen_vk64pe.py emits vulkan-1.dll, copied to bw64vulkan.dll; byte-identical), so this restores its native naming rather than inventing a new one. The 32-bit tree is untouched. Safety scan: no shipped Wine PE statically imports winevulkan.dll or vulkan-1.dll (objdump import scan of the likely consumers, 2026-10-27); consumption is dynamic only, and the only dynamic consumer in the D3D12 flow is dxgi's factory. Landed in tools/rootfs64/stage-vkd3d-payloads.sh (five payload members + idempotence check), tools/rootfs64/build-wine64-zip.sh (canonical staging + rationale), tools/rootfs64/check_runtime_completeness.py (VKD3D_PINS gains vulkan-1.dll and winevulkan.dll, both pinned to the shim sha256 8084b4de88fcfd89771ecb6bc09cd9bc4f4c70b2a962756b8b2fd6767b9825db — a future archive that drifts back to Wine's copies fails the completeness check), and docs/known-gaps/runtime-artifact-manifest.md regenerated (gen_runtime_manifest.py). Both this tree's runtime archives (web/runtime + dist/runtime wine64.zip) were restaged with the script (pre-change archives backed up under /home/ubuntu/backups/winbox-vkd3d-payloads-20261027-*).

DIST-STATE NOTE: the staging script rewrites both runtime archives in place, which breaks the byte-identity between dist/runtime and web/runtime wine64.zip that the provenance guard asserts (member contents stay identical; zip metadata differs per pass), and the font/desktop-pin provenance records are written against the exported archive hash. After restaging, this lane restored the canonical export state exactly as tools/browser/build.mjs produces it — dist archive byte-copied from web/runtime, then tools/browser/stage-wine32-fonts.py and tools/browser/stage-desktop-pin.py re-run against dist/runtime — and removed the probe exes this lane's repro had staged into dist prefix64.zip via tools/vkd3d/stage-vkd3d.sh (that staging is a per-run step for the d3d12 probes, not tree state). All probe verdicts below were gathered on archives whose member contents are identical to the final state.

VERIFICATION (fixed dist, this tree):
- tri12draw SYSTEM placement (the target): PASS — summary passed:true, RESULT 0, guest exit 0, bridge log "vkCreateWin32SurfaceKHR -> surface id minted" + "vkCreateSwapchainKHR 480x360 fmt=44 nimg=3", 10 frames drawn+presented, canvas analysis trianglePx 208,556 / center [127, 64, 64] — the pinned first-light signature, now at system placement (test-results/dxvk-surf-fix3-tri12draw/). Before the fix the same probe aborted at the loader_thunks assert (test-results/dxvk-surface-repro1/).
- Unstaged first light (d3d12-pixels.mjs tri12draw): PASS, 208,556 px / center [127, 64, 64] — unchanged (test-results/dxvk-surf-fix3-unstaged/).
- uav12 system placement: SUMMARY flag=TRUE sweep=ALL18 exec=FAILED, with the exec failures at spirv-capability-unknown:5347 in vkCreateShaderModule — the §4.17/§4.18 baseline signature on this tree (the Typed-UAV grant is undisturbed; the 5347 wall itself is addressed on the lane-vkd3d-bda branch by Route 0, not here) (test-results/dxvk-surf-fix3-uav12/).
- tri12 system placement (clear-only): PASS, RESULT 0 (test-results/dxvk-surf-fix3-tri12/).
- Godot (demo-matrix, DEMO=godot-squash JIT=1): PASS on retry — window mapped (366 s under heavy multi-lane load), frames presented, full input phase completed; the guest log shows its Vulkan loader finding vkGetInstanceProcAddr in winevulkan.dll (now the shim) and vkd3d-proton device caps initialising (test-results/demo-matrix/godot-squash-jit1-20261027T134536/). The first attempt showed the §4.18-documented load flake in its exact signature (2 XWire accepts, "Failed to create Windows OS window / all display drivers failed", 19 bridge traps, process exit 1 — test-results/demo-matrix/godot-squash-jit1-20261027T132538/): a display-layer failure upstream of any Vulkan call, not a loader effect.
- Portal (demo-matrix, DEMO=portal JIT=1, 32-bit control): mapped (62.7 s), presents logged, full input phase, 0 page errors (test-results/demo-matrix/portal-jit1-20261027T142531/). The 32-bit tree is untouched by the fix; this validates the restaged dist as a whole.
- Gang Beasts (demo-matrix, DEMO=gangbeasts JIT=1): NOT VERIFIED in this lane — the run wedged with the campaign's documented early-boot contention signature (0 DXVK gipa lines, stall before Vulkan enumeration) at box load 16–26 with two other lanes' guests/builds active (test-results/demo-matrix/gangbeasts-jit1-20261027T140834/). DXVK never loaded, so this run exercised nothing about the change; a GB verdict needs the lane-boundary window the campaign already queues for the GB menu run. The DXVK-class route itself (a non-Wine dxgi resolving Vulkan through the wine tree) is covered by the Godot and tri12draw evidence above.
- Runtime completeness checker (tools/rootfs64/check_runtime_completeness.py): 0 failures, 1 pre-existing skip; the vkd3d-payloads test passes with the new pins, and the manifest test passes after regeneration.
- JS unit suite (node web/tests/suite/runner.mjs --unit): 288 passed / 7 failed — the failing set is exactly the §4.18 baseline set (packaging-prefix, v2-draw-contract, vk-image-dirty-generation, vk-register-citations, vk-sampler-rejection, vk32-properties, vulkan-sampler). Zero new failures. (An intermediate run showed provenance-artifacts failing for the dist-state reasons in the note above; it passes 8/8 after the canonical export state was restored.)

One un-reproduced observation, recorded for honesty: the first post-staging tri12draw run (test-results/dxvk-surf-fix-tri12draw/, staging round 1, when only vulkan-1.dll had been flipped) failed at LoadLibrary(d3d12.dll) err=193 with no loader diagnostics; the identical load succeeded in the next run (dxvk-surf-diag-module2) and in every run since. No archive-content explanation exists (a full member-hash diff of that archive against its backup shows vulkan-1.dll as the ONLY changed member, and d3d12.dll's import chain touches neither vulkan-1.dll nor winevulkan.dll). Treated as a one-off load flake of the family §4.18 documented, not as a staging effect; it has not recurred.

REGISTER: docs/known-gaps/vulkan.md gains one real-workload row (IMPLEMENTED) for the loader-domain collision at the DXGI surface boundary. Counts 354 -> 355 rows (52 IMPLEMENTED / 71 REJECTED-LOUDLY / 232 NOT-A-GAP / 0 OPEN).


## 4.20 Route 0: the cap-5347 wall was a header artifact — declared-but-unused PhysicalStorageBufferAddresses now passes the audit; execution stops at the descriptor-array wall (2026-10-27)

Route 0 from the BDA research report
(`docs/reports/d3d12-descriptor-addressing-research-20261027.md`) is
landed. The bridge's SPIR-V audit (`vk64AuditSpirv`) now dispositions
capabilities 5346/5347 (PhysicalStorageBufferAddresses) by use,
following the 33/4427 precedent in the same loop: when the module
contains no OpConvertUToPtr / OpConvertPtrToU (ops 120/121) and no
OpTypePointer with storage class 5349 (PhysicalStorageBuffer), the
two capabilities are skipped instead of rejected, so spirvfix R8 —
which already strips exactly that form — can run. A module with real
pointer use still rejects at creation under the same named token,
`spirv-capability-unknown:5347` (5346/5347 remain absent from the
name table and from the unknown-branch exemption list; the skip is a
separate use-conditional exemption in the capability loop).

Exactness audit against R8 (the brief's hard requirement — the audit
must accept precisely the modules R8 can strip): the audit's
predicate is {no op 120} ∧ {no op 121} ∧ {no OpTypePointer with
words[2] == 5349 in raw-word layout}. R8's predicate in parsed form
is {no op 120/121} ∧ {no OpTypePointer with storage class 5349}.
Auditing R8's implementation line by line found a LATENT DEFECT:
R8's pointer arm tested `i.words[0] === 5349` — the OpTypePointer
RESULT ID — instead of `i.words[1]`, the storage-class operand
(every other TypePointer consumer in spirvfix.mjs uses words[1] /
words[2] correctly). R8's own test fixture encoded the same wrong
operand order ([5349, 10, 11] as [storageClass, id, pointee]), so
the defect was invisible to its test. Consequences of the old code:
a module whose physical pointer type had an id other than 5349
would have its capability stripped while the physical pointer type
remained (naga would then fail at translation — loud, but a
different failure than designed), and a module with an innocent type
whose id happened to be 5349 would be refused the strip. Both are
now fixed: `tools/spirvfix/spirvfix.mjs` tests words[1], the
fixture's operand order is corrected, and the audit implements the
corrected, semantic predicate — audit and R8 accept exactly the same
modules. Guards: `spirvfix-r8.test.mjs` gains an operand-index pin
(a pointer type whose RESULT ID is 5349 is not physical use), and
`vk-spirv-rejections.test.mjs` gains (a) source pins for both halves
of the predicate, the exemption line, and the R8 mirror; (b) a census
test on the reproduced real module
(`testdata/spirv/d3d12-ps-uav-5347-unused.spv`, copied from the
research census — declares 5346/5347, zero pointer ops; after R8 the
capabilities are gone and the memory model is Logical); (c) a
sabotage pair whose pointer-only and convert-only sub-cases each
block the strip alone. Sabotage evidence: deleting the bridge
exemption, inverting its condition, deleting the op-120/121 scan, or
moving the audit's storage-class read to the result-id word each
turn the guard red (1 failing test each); reverting R8 to words[0]
turns 3 tests red across the two files. Note the census fixture also
declares RuntimeDescriptorArray (5302) and DenormFlushToZero (4465),
which naga rejects for unrelated reasons (a standalone dxil-spirv
reproduction delta) — the guards assert the census and the R8 strip,
not end-to-end translation of that file.

Guest proof (rebuilt dist, "Browser export verified"; BUILD_INFO
git_sha 1d40f018c + this lane's uncommitted-at-build-time changes,
support archive c58f20bb880a; probe exes staged with
tools/vkd3d/stage-vkd3d.sh). uav12
(`test-results/dxvk-uav-route0/console.log`), BEFORE → AFTER against
the section 4.17 run in wt-vkd3d: TypedUAVLoadAdditionalFormats =
TRUE (unchanged), sweep 18/18 UAV_TYPED_LOAD=YES (unchanged),
`spirv-capability-unknown:5347` lines 4 → 0. The execution landing
point, verbatim: the four graphics EXEC cases still fail
CreateGraphicsPipelineState with hr=0x80004005, but the rejection
behind it is now `vk64: UNSUPPORTED spirv-capability:
RuntimeDescriptorArray` (x4 in vkCreateShaderModule) — the modules
pass 5347 and stop at capability 5302, the runtime-descriptor-array
wall the research report explicitly scoped OUT of Routes 0+A
(browser WebGPU has no binding arrays of any kind). The two uint
EXEC cases are unchanged (hr=0x80070057, vkd3d-side E_INVALIDARG
before shader compilation). SUMMARY stays flag=TRUE sweep=ALL18
exec=FAILED, RESULT 1 — Route 0 removes the header-artifact wall; it
does not, and by design cannot, serve descriptor arrays. First
light: tri12 on the same dist passes, RESULT 0 (run
test-results/dxvk-tri12-route0/). Three earlier uav12 attempts on
this dist failed for infrastructure reasons only and are preserved
(dxvk-uav-route0-timeout1 / -explorerfail: guest Wine explorer
failed to start under box saturation, the documented wedge class;
dxvk-uav-route0-nostage: probe exe not yet staged into the prefix,
c0000135 — staging is a post-export step this fresh worktree had
never had run). JS suite (this tree,
BW_TEST_DIST=/home/ubuntu/wt-vkd3d-bda/dist, runner.mjs --unit):
288 passed / 7 failed — the baseline failure set at c52f39d4e
(287/8, verified from two independent logs) minus
provenance-artifacts, which passes here; zero new failures. The
vk-register-citations failure is byte-identical in kind to
pristine HEAD (only its pre-existing coverage-ceiling subtest).

Tree-preparation notes for this worktree (it had never been built
in): deps installed from the local pnpm store; DXVK w32+w64 built
in-tree per tools/dxvk/BUILD.md with the sibling tree's exact meson
options; the GStreamer payloads were copied from the sibling tree
and verified against their pinned sha256 closure
(prepare-gstreamer.py --check: "staged and verified"); the runtime
manifest was regenerated (one line: the support archive's hash,
from the locally built DXVK DLLs, same 3543 entries).

### Register

docs/known-gaps/vulkan.md: counts unchanged, 354 rows = 51
IMPLEMENTED / 71 REJECTED-LOUDLY / 232 NOT-A-GAP / 0 OPEN (recount
mechanical; no rows added — the Route 0 carve-out extends the
existing capability-audit row, the catch-all row's carve-out list,
and the Typed-UAV row's outcome sentence, which now records the
section 4.20 landing point). The bridge insertions drifted 49
citations; all re-pinned (tool fix pass + git-diff line mapping for
multi-citation rows): citation audit 0 DRIFT / 0 MISSING over 327
cited rows, UNCHECKABLE 110 as at HEAD.

### What this lane did not do

Route A / spirvfix R15 (root-descriptor rewrite) is the follow-on
lane's work, as are the VKD3D_SHADER_DUMP_PATH captures. tri12draw
was not run: the winevulkan-path swapchain regression from section
4.17 item 8 belongs to the bisect lane in wt-vkd3d; nothing in this
lane's change touches that path (the audit change only widens which
modules reach the tier, and tri12's direct path verifies clean).
The RuntimeDescriptorArray wall this run exposed as the next stop
is a separate scope (no binding arrays exist in browser WebGPU);
serving it needs bindless emulation work beyond Routes 0+A as
researched.

## 4.21 Route A: descriptor addressing served for the provable subset — runtime-array specialization (R15a) and read-only root descriptors (R15b) (2026-10-27)

Route A continuation lane (wt-vkd3d-bda, branch lane-vkd3d-bda, on top of
88bed0bcf / section 4.20). The research design (descriptor-addressing
research report section 4) is finalized here against REAL guest-module
captures, then implemented CBV-first. Two of the research assumptions did
not survive contact with the captures; both corrections are recorded
below because they change the mechanism, not just the constants.

### Captures (rig web/tests/d3d12-dump.mjs, fixed by the prior lane)

- uav12 on this tree reproduces the section 4.20 landing point exactly:
  Typed-UAV flag TRUE, sweep 18/18, exec fails with exactly 4x
  "UNSUPPORTED spirv-capability:RuntimeDescriptorArray" at
  vkCreateShaderModule. Dumps extracted this time (run
  test-results/dxvk-uav-r15b/): 1 root signature + 4 DXBC/SPV pairs
  (1 VS + 3 PS; the 6 EXEC cases share modules by shader source).
  Case attribution, read off the guest log: the 4 float-format cases
  (R8G8B8A8_UNORM, R32G32B32A32_FLOAT, R16G16B16A16_FLOAT, R16_FLOAT)
  reach the bridge and take the 4x 5302 rejections. The 2 uint cases
  (R32_UINT, R8_UINT) fail GUEST-side with E_INVALIDARG at
  CreateGraphicsPipelineState before any vkCreateShaderModule -- a
  vkd3d/guest-side wall independent of Route A; they remain blocked
  after this lane and are reported per-case in the verification table.
- root12 (new micro-probe tools/vkd3d/probe/root12.c: separate root CBV /
  root SRV / root UAV cases, readback-verified): the CBV case compiles
  and fails at PSO creation with exactly 1x
  spirv-capability-unknown:5347 (run test-results/dxvk-root-r15b/;
  module 39f7fd6a1a8077f3.spv). The SRV and UAV cases cannot be compiled
  in-guest at all: Wine 10's builtin d3dcompiler_47 HLSL front end
  rejects EVERY buffer-object declaration form. Proven by a dedicated
  syntax probe (hlslprobe, 7 candidates, one boot): ByteAddressBuffer
  and RWByteAddressBuffer are lexed as unknown NEW_IDENTIFIER;
  Buffer<T> hits a keyword with no grammar production at ps_5_0 AND
  ps_5_1; StructuredBuffer / RWStructuredBuffer are unknown identifiers.
  Texture objects compile fine (that is how uav12 builds), buffer
  objects do not exist in this compiler. A genuine Microsoft
  d3dcompiler_47.dll exists on the box only as a 32-bit build (Chromium
  109 portable, inside chromium.zip; a 64-bit guest gets LoadLibrary
  err=126) and no dxc exists anywhere on the box, so guest-side SRV/UAV
  root captures are blocked on tooling, not on the design. The R15b
  code path is kind-agnostic (only the pointee shape and the writable
  verdict differ); SRV guest-verification stays queued behind a working
  in-guest HLSL buffer compiler or Godot's own DXIL modules (below).

### Census verdicts (census.mjs, extended this lane with the shipped R15 classifiers)

uav12 PS modules (all three; entry PS, set 5 = the UAV heap): a runtime
array of 2D storage images (Unknown format, NonWritable) at set 5 /
binding 0; exactly ONE OpAccessChain access each; the index is a load of
push-constant byte offset 0, addend 0. NOTE: the push offset is 0, not
the offset 12 / addend 1 the standalone reproduction (and the earlier
census) showed -- vkd3d's push layout for THIS root signature puts the
heap index word first. Caps declared: 1, 55 (read-without-format), 4466,
5302, 5345, 5347 (declared-unused). No 5306-5309, no NonUniform
decorations. The VS module is shared with root12 and carries no arrays
and no pointers.

root12 CBV PS (39f7fd6a1a8077f3.spv) -- the root-descriptor shape DIFFERS
from the research section 4.1 assumption in the load-bearing detail:
there is NO OpConvertUToPtr. The VA is loaded from the push block as a
vec2<u32> (push struct { vec2<u32> }, chain + load) and materialized by
OpBitcast (op 124) into ptr<PhysicalStorageBuffer, struct>. Derefs are
OpInBoundsAccessChain with constant member/element indices rooted at
the bitcast result, then OpLoad with an Aligned 16 operand; one fresh
bitcast per load site (2 materializations, same slot). Pointee:
struct { array<vec4<f32>, 4096> } decorated Block, member Offset 0 --
the fixed 4096-element array is dxbc-spirv's unknown-size CBV
convention. No pointer escapes, no stores, no calls. Caps 1/4466/5345/
5347. The R15b classifier therefore treats BOTH materialization forms
as roots: OpConvertUToPtr (the DXIL/dxil-spirv form SM6 games take)
and OpBitcast-to-PSB-pointer of a push-traced integer value. (Grammar
note for the mirror: OpConvertPtrToU is op 117, OpPtrCastToGeneric is
121; the inherited Route 0 used-test names the pair 120/121 -- harmless
there because it only widens a skip, but R15's classifier uses the
grammar-correct numbers.)

### Godot reachability (this tree, demo-matrix, test-results/godot-r15/)

Godot IS reachable -- the section 4.17 winevulkan-path failure did not
reproduce (the bisect lane's verdict, section 4.18, acquits the
Typed-UAV change set: that failure was an intermittent load flake in
guest X-server connection, and the system-placement surface assert is
pre-existing). On this tree Godot creates its DXGI factory and D3D12
device, passes the SM 6.0 sweep and the Typed-UAV gate, and its early
graphics shader modules are ACCEPTED by the bridge (zero 5347 and zero
5302 rejections in a 2,402-line console). It stops during
RenderingDevice setup, before any present, at: CreateQueryHeap failed
0x80004005 x2; 7x spirv-compute-entry rejections (the V2 compute
execution wall, separate scope); 18x texel-buffer-storage; image-format
rejections; and one module each rejected for capability 46 and
capability 64. No VKD3D_SHADER_DUMP_PATH census of Godot modules was
taken (the demo-matrix path has no dump hook); what its heap/root
modules look like stays unknown until it passes the compute/query
walls -- Route A's classifiers will meet them at the audit either way,
serving or rejecting by the same rules as the fixtures below.

### The design as implemented

R15a descriptor-array-specialize (spirvfix): an array variable is
servable iff its pointee is a runtime array of a descriptor element
type (image/sampler/sampled image), every body use is a single-index
access chain, all index expressions classify to ONE signature
({const v} or {push@byteOff + add} through loads of the push block,
copies, bitcasts, constant adds/subs), the element pointers never
escape and are never stored through, and -- for storage images -- the
variable is NonWritable. The rewrite deletes the array variable and
installs ONE scalar clone at the same set/binding named
bw_arr_g<set>_b<binding>_p<pushOff>_a<add> (or _c<value>); each access
chain becomes an OpCopyObject of the clone. The tier resolves the
element at bind time from the draw's merged push block and binds
bindingForElement(set, binding, element); the resolved entry feeds the
bind-group fingerprint, so a changed index re-keys the group. If no
runtime array survives, cap 5302 is stripped. R15a runs BEFORE R14 in
applySpirvFix; storageImageVars also unwraps one array level so the
tier's format signature (computed from the original bytes) names the
array's set/binding and R14 can inject the bound view's format into the
clone's image type.

R15b root-descriptor-bda (spirvfix): a module is servable iff EVERY
physical-storage-buffer pointer materialization (OpConvertUToPtr, or
OpBitcast whose result type is a PSB pointer) traces through
r15ClassifyExpr to a push-block load at an 8-aligned byte offset with
zero addend; no materialization's pointer cone contains a store or an
atomic RMW (read-only scope, below); no pointer escapes its cone
(calls, stores of the pointer, OpConvertPtrToU, phi/select,
OpPtrAccessChain, bitcast reinterprets); each slot (grouped by push
byte offset) has ONE pointee type whose decorated layout is byte-exact
in WGSL storage layout (the R13 measure rules plus a runtime-array tail
member); and every PSB pointer type in the module belongs to a servable
cone or is unreferenced. The rewrite synthesizes one read-only
StorageBuffer variable per slot, named bw_root_p<pushByteOff>, in the
module's heap-array set (when all its runtime-array variables share one
set) else set 0, at binding 4096 + pushByteOff/8; re-roots every cone
chain at it with StorageBuffer pointer-type twins; strips the Aligned
operand from re-rooted loads (the audited layout carries the contract;
a VA's offset inside its buffer does not guarantee the operand's
alignment); flips pointee structs Block -> BufferBlock pre-SPIR-V 1.3
only; deletes the materializations and their now-dead provenance cone;
and strips caps 5346/5347, the SPV_KHR_physical_storage_buffer
extension, and the PhysicalStorageBuffer64 addressing model. Root slots
are keyed by PUSH BYTE OFFSET, not root-parameter index: vkd3d's push
layout interleaves root constants, table words and root descriptors in
root-signature order, so offset is the only key every layer can compute
independently (spirvfix from the module, the bridge from its own census
of the same module, the tier from the name).

Read-only scope (deliberate): the page tier has no GPU->guest BUFFER
writeback -- image readbacks exist (V2 READBACK records), buffer
writebacks do not; that is the same wall the audit already names
spirv-storage-buffer-write. A root UAV store served without writeback
would silently lose the guest's data, so writable root pointers keep
the loud 5347 rejection until a buffer-writeback path exists. Root CBV
and root SRV (both read-only) are the served kinds.

Bridge mirror (vk64bridge.cpp): vk64AuditSpirv implements the same two
classifiers over the raw words. Cap 5302 is skipped when every runtime
array in the module is R15a-servable; caps 5346/5347 are skipped when
Route 0's unused-test passes (unchanged) OR every materialization is
R15b-servable; the trailing writable-storage checks are unchanged
(root modules carry no storage variables, so they never trip them).
Anything else keeps the existing named rejections. At pipeline
creation the bridge censuses the pipeline's modules into a root-slot
table (push byte offsets); the serializer tracks the merged push block
across the command stream and, before any draw/dispatch whose pipeline
has root slots, resolves each slot's u64 VA -- VA 0 or an unregistered
VA emits a zero-length resolution (tier binds its zero buffer; D3D12
null-descriptor semantics: reads return 0) -- otherwise VA -> (buffer,
offset) via the devVa registry (g_nextDevVa, 256 MiB stride) and emits
a V2 ROOT_DESCS record (new append-only type 0x1D: count, then per slot
{pushOff u32, buffer-object id u64, absolute memory offset u64, length
u64}) plus BUFFER_DATA for the range (the same deduped content path as
descriptor buffer bindings).

Tier (web/vkwebgpu.mjs + web/vk-protocol.mjs + web/wgsl-utils.mjs):
ROOT_DESCS decodes into the per-draw state (st.rootDescs, keyed by push
offset). scanWgslResources parses bw_arr_* names into a per-decl array
signature and bw_root_* names into a root push offset. At bind time a
bw_arr decl resolves its element from st.pushBlock and uses that
element's DESC_SET entry; a bw_root decl binds the ROOT_DESCS
resolution for its offset through the normal kind-1 buffer path
(content-addressed GPU buffer, padded to the declaration's minimum
binding size -- the CBV pointee's fixed 4096-element array makes the
minimum 64 KiB; bytes past the guest's bound range read as zero, which
matches D3D12's out-of-range robustness for a slot the app sized).

Fail-closed list (both implementations, cross-checked by the guard
fixtures): index/address expression containing a load that is not a
push-block chain load; dynamic index from buffer contents, a phi, or a
call result; two distinct signatures on one array variable; a writable
storage-image array; a root pointer that is stored through, atomically
RMW'd, passed to a call, stored as a value, converted back to an
integer, selected/phi'd, or advanced with OpPtrAccessChain; an address
addend other than a folded constant (and nonzero addends are rejected
for root slots in this lane); a base that is not a single root source;
a pointee layout failing the storage measure (including bool members,
mis-strided matrices, scalar-packed arrays); a pointee struct shared
with a non-PSB pointer type; a PSB pointer type used outside a root
cone; more root slots than the synthesized binding space admits
(collision with a module-owned binding at 4096 + pushByteOff/8 in the
chosen set rejects).

### Implementation and verification (2026-10-27, route-a lane)

Implemented as designed above: spirvfix R15a/R15b (tools/spirvfix/
spirvfix.mjs) with the exported analyzers shared by the rewrites, the
census, and the guards; the bridge mirror classifiers (namespace r15 in
source/vulkan/vk64bridge.cpp) wired into the capability audit, the
graphics-pipeline root-slot census, the devVa registry, and the
serializer's ROOT_DESCS emission; the V2 record (web/vk-protocol.mjs);
the tier resolution (web/vkwebgpu.mjs, web/wgsl-utils.mjs). Three
defects were found and fixed en route, each pinned by a guard or a
fixture: (1) the bridge mirror's decoration parse required an operand
word, so operand-less decorations (Block, BufferBlock, NonWritable)
were silently dropped -- the mirror rejected every real module while
the JS analyzer accepted it; caught by running the mirror classifiers
standalone against the captured fixtures. (2) Both orphan sweeps
counted header/directive words as references, so a capability NUMBER
equal to a type id (Int64 = 11 = the array type id in
d3d12-ps-uav-heap-array-int64.spv) kept the dead runtime-array type
alive forever; both sweeps now skip header/directive instructions
identically. (3) The synthesized-binding collision check this section
specified was missing from both classifiers; it is now implemented in
both and guard-tested. A fourth, older defect fell out of the uav12
verification: the PE Vulkan shim's export table
(tools/vkd3d/probe/exports.txt) routed vkGetDeviceImageMemoryRequirements
to the unimplemented stub even though both bridge ABIs implement it
(fn 917); the entry now carries the real id and the shim DLLs were
rebuilt from the corrected table (the bridge's answers were already
correct -- only the guest-side table was stale). The guest runs below
used the rebuilt DLL swapped into the dist's wine64.zip. The fix was
then restaged properly, following the pipeline's own re-pin
discipline: the payload pin in
tools/rootfs64/check_runtime_completeness.py now carries the rebuilt
DLL's hash, the new bytes were merged into both shipped wine64.zip
copies, docs/known-gaps/runtime-artifact-manifest.md was regenerated,
tools/browser/runtime-assets.json was re-pinned to the merged archive,
and the local runtime-assets tar in the backups tree was regenerated
to match (the pre-R15 tar is kept beside it as .pre-r15). A final
rebuild (same wasm stamp 33f47805) verifies the whole chain at
export: the completeness checker reports 0 failures.

Guards: web/tests/suite/unit/spirvfix-r15.test.mjs (14 tests) covers
both real-fixture positives, the tier/protocol contracts, the bridge
mirror source pins, and fail-closed mutations of the REAL modules (a
store through the root pointer, a pointer-to-integer escape, a
re-rooted provenance chain, a second array index signature, a
module-owned binding colliding with the synthesized slot). Sabotage:
breaking the provenance classifier, the signature-count check, or the
bridge's audit skip lines turns the guard red (9 tests); restoring
returns it green. The REWRITES coverage pin and the reviewed
silent-failure baseline were updated for the new code (four new
unlogged switch defaults, all in the pure mirror classifiers; the
audit logs each rejection once at the capability site).

Guest verification on the rebuilt dist (build stamp 33f47805,
test-results/dxvk-uav-r15f/, dxvk-uav-r15h/, dxvk-root-r15g/,
dxvk-tri12-r15f/):
- uav12: flag=TRUE, sweep=ALL18 (held). RuntimeDescriptorArray
  rejections 4 -> 0; the four float EXEC cases now pass
  CreateGraphicsPipelineState (previously FAIL hr=0x80004005) and
  texture creation (16 vkCreateImage, all vr=0), then stop inside
  vkd3d's committed-resource creation before any command list is
  submitted (no bridge rejection is logged; the guest prints no
  per-case line for these exits). The two uint EXEC cases are
  unchanged: guest-side E_INVALIDARG at CreateGraphicsPipelineState.
  SUMMARY exec=FAILED, RESULT 1 -- the exec wall moved from shader
  creation to resource creation; the remaining stop is the general
  D3D12 execution-completion work, not descriptor addressing.
- root12: CASE cbv PSO OK -- the root-CBV pipeline state creates for
  the first time (previously rejected at the bridge,
  spirv-capability-unknown:5347; this run has zero spirv-capability
  lines). The case then stops at CreateCommittedResource (render
  target readback) hr=0x8007000E -- the same committed-resource wall
  as uav12. The srv and uav cases remain blocked at D3DCompile by
  Wine 10's builtin HLSL compiler (the hlslprobe evidence above);
  their shader shapes follow the identical R15b path and remain a
  follow-up for a guest-side DXIL capture (e.g. Godot's).
- tri12 first light: RESULT 0, rig verdict passed -- no regression.
- Godot (godot-squash) on this tree: reachable as in the design
  section (device + SM 6.0 + Typed-UAV gates pass, early modules
  accepted, zero 5346/5347/5302 rejections); it stops at its
  pre-existing compute/query-heap walls, unchanged by this lane.

JS suite (this tree, final dist): 289 passed / 7 failed of 296 files
(baseline at 88bed0bcf: 288/7 of 295; the added file is the R15
guard, which passes). The seven failures are the baseline set: five
source-level baseline failures (v2-draw-contract,
vk-image-dirty-generation, vk-sampler-rejection, vk32-properties,
vulkan-sampler -- each reproduced identically on pristine 88bed0bcf
sources), vk-register-citations failing only its pre-existing
coverage-ceiling subtest, and packaging-prefix failing only its
shipped-archive ELF subtest (a property of the pinned runtime asset,
identical from the pristine tree). The dist-state failures seen
mid-lane (provenance-artifacts, runtime-completeness) pass on the
final dist after the re-pin chain above. Zero new failures.
Register: docs/known-gaps/vulkan.md stays 354 rows = 51 IMPLEMENTED /
71 REJECTED-LOUDLY / 232 NOT-A-GAP / 0 OPEN (no verdicts changed; the
5302 and 5346/5347 rows gained the dated Route A carve-outs above).
Citations re-pinned mechanically for every line this lane's insertions
shifted (audit: 0 DRIFT / 0 MISSING; the one citation pointing into
the rewritten bindGroupEntries call sites was re-pinned by call-site
ordinal).
## 4.22 Module-layout audit lane: the §4.16 silent-misconversion hazard closed — array-hidden and padded layouts now reject loudly as `spirv-block-layout` (2026-10-27)

§4.16 named the hazard and deferred the fix: "a mat3 block at
MatrixStride 12 is CONVERTED SILENTLY WRONG by naga today (columns read
at stride 16 from stride-12 data)... a module-creation layout audit is
the fix shape, not attempted here." This lane built the modules,
mapped exactly which shapes are silent, and landed the audit.

### The empirical map (vendored naga 30.2.0; every module spirv-val clean)

§4.16's claim needs one refinement, proven by fixture: the BARE-member
shape is not silent in this naga build. A mat3 struct member decorated
MatrixStride 12 — uniform, storage, or push-constant — is rejected
(`UnsupportedMatrixStride { stride: 12, columns: 3, rows: 3 }`), as are
mat3x2 members at stride 12 (WGSL computes 8), member Offsets that
disagree with the computed layout, and under-sized array strides. The
silent class is everything naga never compares for equality:

- MatrixStride on matrices that are ELEMENTS of OpTypeArray /
  OpTypeRuntimeArray: array<mat3> at MatrixStride 12 or 20, array<mat2x3>
  and array<mat4x3> at 12, runtime-array<mat3> at 12 — all convert.
- ArrayStride LARGER than the WGSL stride (padded arrays): array<f32> at
  32 in a uniform block (WGSL 16), at 16 in a storage block (WGSL 4) —
  the emitted WGSL is literally `array<f32, 4>`; the padding is dropped
  without a word.

Value-level proof on a fixture the tree already carried: the R13 corpus
module `testdata/spirv/r13/ubo-mat3arr-stride12.spv` reads element
[0][1].z; the guest wrote it at byte 1*12 + 2*4 = 20; the converted
WGSL `array<mat3x3<f32>, 1>` in `var<uniform>` reads byte 1*16 + 2*4 =
24. Wrong data, zero diagnostics — and the array-wrapped form is the
realistic one: §4.16 itself records that dxil-spirv's D3D12 cbuffer
form is a wrapper struct around a tightly-strided array. Real-compiler
grounding: glslang with GL_EXT_scalar_block_layout emits exactly
ArrayStride 36 + MatrixStride 12 + ColMajor for
`layout(scalar) uniform B { mat3 m[1]; }` — the decoration set the
fixtures carry is a real compiler's output shape, not an invention.
(The fully-scalar module happens to be caught downstream by naga's
array-stride alignment check; the audit names it precisely regardless.)

### The audit

One layout model, shared: R13's decoration collection and measure()
walker were refactored into `createLayoutModel` in
`tools/spirvfix/spirvfix.mjs`; R13 and the new `auditBlockLayouts`
consume the same model, so the rewriter and the auditor cannot drift.
For every block variable (Uniform Block, StorageBuffer Block /
BufferBlock, PushConstant Block; descriptor arrays of blocks unwrapped)
the audit returns one of three verdicts:

- ok — every decorated Offset / ArrayStride / MatrixStride EQUALS the
  WGSL value for the variable's final address space;
- reject — a proven inequality, with a trace detail naming the
  decoration, the decorated value, the WGSL value, and the block/member
  (`spirv-block-layout: block %4 member 0: ArrayStride 32 on array<f32, 4>
  (WGSL stride 16)`);
- unknown — a shape the model does not cover (bool members,
  spec-constant array lengths before folding, a block member with no
  Offset decoration). No verdict is issued and the module keeps its
  previous outcome: the audit rejects only on proof, because the
  audit-stricter-than-the-tier defect class is as bad as the reverse.

Enforcement sits in the page tier at translation, in `web/shader.mjs`
immediately after `applySpirvFix` returns — so storage classes are
FINAL when the audit walks (R13 demotions already applied; each block
is judged in the address space it will actually be translated into, by
construction rather than by convention). The first reject verdict
throws `spirv-block-layout: ...`; the existing passthrough catch
rethrows only that token (infrastructure failures keep the old
warn-and-pass degradation), and the shader cache logs it loudly
(`vk64: page tier cannot translate the <stage> SPIR-V module
(spirv-block-layout: ...) — frame skipped`). No frame is drawn from a
misconverted module. Deliberately not built: a duplicate walker in the
bridge audit — the bridge sees the raw module (pre-R13), so a bridge
copy would judge the wrong layout and re-introduce the drift the shared
model exists to prevent; the tier translation is the gate, the same
precedent as capability 35's disposition. `scalarBlockLayout` stays
FALSE; nothing about the SM 6.0 grant changes.

### Guards and corpus

`web/tests/suite/unit/spirvfix-layout-audit.test.mjs` (34 tests) over 16
new fixtures in `testdata/spirv/layout/` (spirv-val clean, .spvasm
alongside): the silent set pinned as CONVERTING at naga while the audit
rejects; loud shapes, servable shapes (mat3 at stride 16, mat3x2 at 8,
std430 vec4 runtime arrays, scalar push blocks, spec-constant arrays)
pinned ok; both ordering pins (a stride-4 float block rejects judged as
uniform and passes post-R13-demotion; a spec-constant-length block is
unknown raw and ok post-fold); decoration sabotage pairs in BOTH
directions (12→16 flips a reject to ok; 16→12 and ArrayStride 16→32
flip ok to reject). Sabotage of the implementation itself: disabling
the shader.mjs throw reds 11 tests; disabling the auditor's reject path
reds 26. Corpus gate over all of testdata/spirv: 52 modules, 48 block
variables, 45 ok, 0 unknown, exactly 3 rejects — the R13 fixtures
ubo-mat3arr-stride12 (the hazard), ubo-vec3-stride12 (scalar; naga-loud
today, now named), and ubo-pointer-phi (R13 refuses its demotion over a
pointer-through-OpPhi, so its stride-4 block still reaches naga as a
uniform block; the audit names the same unservable outcome instead of
naga's bare stride error). No false positives anywhere in the corpus;
R13's own suite stays 11/11 and the spirvfix corpus gate 22/22 after
the shared-model refactor.

REGISTER: docs/known-gaps/vulkan.md gains one row (REJECTED-LOUDLY) for
the unservable block-layout shapes at `tools/spirvfix/spirvfix.mjs:2150`
+ `web/shader.mjs:50`. Counts 355 -> 356 rows (52 IMPLEMENTED / 72
REJECTED-LOUDLY / 232 NOT-A-GAP / 0 OPEN). The row's citation verifies
as MATCH under tools/vk-register-citation-audit.py (checked coverage
211 -> 212). Pre-existing, not this lane's: the register's UNCHECKABLE
count (109) exceeds the citation test's pinned ceiling (102) on the
pristine tree already — rows added by the Typed-UAV and surface lanes
after the re-pin baseline; that test fails identically before and after
this lane's change and wants a re-pin decision at merge.

MERGED NUMBERING (D3D12 stack merge, 2026-10-27): the two lane trees collided on section 4.19/4.20 -- the vkd3d tree used 4.19 for the system-placement surface fix and 4.20 for the module-layout audit, while the vkd3d-bda tree used 4.19 for Route 0 and 4.20 for Route A. This merged report renumbers to one sequence: 4.18 bisect, 4.19 surface fix, 4.20 Route 0, 4.21 Route A, 4.22 layout audit. References inside each section were updated to the merged numbers (the vkd3d tree's layout-audit references now read 4.22; the bda tree's Route 0 / Route A references now read 4.20 / 4.21). SETTLEMENTS RECORDED BY THIS MERGE: (a) the SPIR-V capability exemption union is {4464-4468, 5345, 68, 69} with 5254 kept as a named rejection, and the audit's used-conditioned carve-outs for 5302 (mirror classifier: every runtime array specialized away) and 5346/5347 (mirror classifier: module provably servable) from Route 0/Route A; (b) the vkd3d tree ADOPTS the GB lane's geometry-shader passthrough serve (integ's position: modules pass, GS pipelines created and recorded, served at submit only on a structural layer-passthrough proof plus a strip-equivalent fragment stage, all other GS draws loud-dropped and counted); (c) compute-entry decision: the vkd3d disposition is adopted -- compute-only modules are accepted at creation so vkd3d-proton device creation succeeds (it creates internal compute pipelines during device creation and treats refusal as device-fatal), and dispatches are loud-dropped at submit as `compute-dispatch`; mixed compute+graphics modules still reject, and the cap/storage/SSBO checks still apply to graphics modules. The integ blanket-compute-rejection guards were updated to assert this behavior, not deleted.

## 4.23 Committed-resource wall: the bridge advertised no HOST_CACHED memory type — small READBACK / WRITE_BACK allocations failed E_OUTOFMEMORY (2026-10-28)

CHARACTERIZATION (probe: tools/vkd3d/probe/res12.c, no shaders, no
pipelines, no draws — D3D12CreateDevice plus a CreateCommittedResource
matrix). Pre-fix, on the merged build (wasm 60c5705a): default buffers
(64K, 1M), upload buffer + Map round-trip, default render-target and
UAV textures, custom WRITE_COMBINE buffer, and GPU_UPLOAD buffer all
pass; readback-buffer-1k FAILS hr=0x8007000E and custom-writeback-buffer
FAILS hr=0x8007000E; readback-buffer-4m PASSES with a Map round-trip.
RESULT 1. The same wall is what stopped root12's CBV case (readback
buffer for its 4x4 render target) and uav12's float EXEC cases after
Route A: they pass CreateGraphicsPipelineState, create their textures,
then stop inside vkd3d committed-resource creation with no bridge
rejection logged.

ROOT CAUSE (source-proven, then guest-proven). vkd3d-proton's
vkd3d_select_memory_flags (tools/vkd3d/src/libs/vkd3d/memory.c) requires
HOST_VISIBLE | HOST_CACHED for READBACK heaps and for CUSTOM heaps with
WRITE_BACK page property. The bridge's fillMemoryProperties
(source/vulkan/vk64bridge.cpp) advertised four types and NO type
carried HOST_CACHED: type 0 DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT
(2 GiB device heap), type 1 DEVICE_LOCAL, types 2/3 HOST_VISIBLE |
HOST_COHERENT (4 GiB host heap). The guest's own BW64OOM instrumentation
confirms the advertised model in vivo (memtype[0] flags=0x7,
memtype[1]=0x1, memtype[2]=memtype[3]=0x6) and the exact failure:
readback-1k reaches vkd3d_suballocate_memory with type_flags=0xa,
req_mask=0, opt_mask=0, then tries a new 16M chunk with base_mask=0 and
fails. UPLOAD works (HOST_VISIBLE | HOST_COHERENT is served by types
2/3), DEFAULT works (DEVICE_LOCAL, type 1), GPU_UPLOAD works (type 0) —
HOST_CACHED was the ONLY missing flag.

THE SIZE SPLIT (why 1K fails and 4M passes). vkd3d's allocator has two
tiers: allocations that fit the chunk regime go through suballocation,
which uses the required-flags mask (empty here → E_OUTOFMEMORY); large
allocations go direct with the domain type mask (base_mask=0xd) and
RELAX the flags (the 4M case retries with type_flags=0x2, dropping
HOST_CACHED) and land on type 2. So the wall bites exactly the regime
real cases live in: root12's 784-byte readback and uav12's small
readbacks are all suballocation-regime. The 64K granularity rounding is
visible in the trace (the 1K resource requests a 64K allocation).

FIX (one flag, two types, no special-casing). fillMemoryProperties now
advertises HOST_VISIBLE | HOST_COHERENT | HOST_CACHED on the host-only
types 2 and 3; the DEVICE_LOCAL-only type and the HVV type are
untouched. The flag is honest, not a lie about coherency: the memory is
ordinary emulator RAM (host-cacheable) and bridge-managed coherent, so
CACHED is a property superset of what vkd3d requires with the COHERENT
bit unchanged — the same combination real UMA hardware advertises.
Scope note, verified by a fresh read (not assumed): the 64-bit handler
(vk64Bridge) and the Properties2 handler both call fillMemoryProperties,
so both ABIs of the 64-bit answer carry the fix; the 32-bit handler
(vk32Bridge) hand-writes its own memory-properties answer and does NOT
call fillMemoryProperties, so whether the 32-bit answer needs the same
addition is a separate, unverified question for a 32-bit lane — this
fix covers the 64-bit path only, and no 32-bit code was touched.

POST-FIX PROOF (rebuilt dist, wasm 1a3edfe6, "Browser export
verified"). res12: RESULT 0 — every case passes, including
readback-1k + Map round-trip and custom-writeback (both previously
0x8007000E). In-guest mechanism: memtype[2]=memtype[3]=0xe, and the
readback suballocation now computes req_mask=0xc, opt_mask=0xc with a
new chunk on a cached type. root12: the CBV case passes its PSO and its
readback buffer now CREATES (the 784-byte heap_type=3 allocation goes
through with req_mask=0xc) — the case then reports RT mismatch bad=64
first px got=[0 0 0 0] want=[191 191 223 255]. uav12: format sweep still
ALL18, and the four float EXEC cases now pass resource creation and
complete their execution, but read back zeros (mismatches 64/64 or
63/64, got=0). The two UINT EXEC cases fail earlier, at
CreateGraphicsPipelineState hr=0x80070057 — a separate, pre-existing
PSO-creation wall, not the resource wall (PSO creation precedes
resource creation in the exec path, so those cases never reached it).

NEXT WALL (stated, not solved here). Execution-to-readback data
delivery returns zeros: command lists complete without error, but the
readback buffers contain all zeros — root12 CBV (64/64 pixels) and
uav12 float EXEC (63–64/64) agree. The stage is NOT yet pinned (draw /
dispatch execution vs copy vs readback synchronization); no rejection
in either log explains the zeros, and the console shows no draw
rejection. Second, separate wall: uav12 UINT EXEC PSO creation
E_INVALIDARG. Third, separate probe debt: root12's srv/uav cases fail at
D3DCompile (E5000 syntax errors in the probe's own HLSL strings),
independent of the resource wall.

GUARDS + SUITES + REGISTER. Guard: web/tests/suite/unit/
vk-memory-properties.test.mjs (2 tests pinning the advertised answer in
source; sabotage-verified — removing the HOST_CACHED token from the arm
fails the first test, restoring it passes 2/2). JS suite 305/0 (baseline
304/0 at 58b4b779e + the new guard, zero new failures). Native suite: make -C project/linux test exits 0; the __TEST binary single-threaded run reports 644 tests OK, 0 tests FAILED in 205s (the trailing fault output is the suite's designed final crash-path test). jit64_tests / jit64_wasm_tests not re-run: the change touches only the wasm-only bridge file, which the native build does not compile (the test binary came up up-to-date with no recompile), so those CPU gates are unaffected by this change.
REGISTER: docs/known-gaps/vulkan.md gains one IMPLEMENTED row at
source/vulkan/vk64bridge.cpp:5606 (fillMemoryProperties); counts
360 -> 361 rows (62 IMPLEMENTED / 66 REJECTED-LOUDLY / 233 NOT-A-GAP /
0 OPEN). The +13-line insertion shifted later vk64bridge.cpp citations;
all 13 drifted rows were re-pinned mechanically (audit --fix for the
three single-citation marker rows, +13 hand re-pin for the rest, each
verified against the audit's own nearest occurrence); final audit:
0 DRIFT / 0 MISSING (217 MATCH, 12 MATCH-INHERITED, 98 UNCHECKABLE,
7 EXEMPT).

## 4.24 Readback-zeros wall: three stages, not one — the *2KHR copy handlers never parsed their Info2 structs, readback rows were copied tight, and copy-only textures had no page target (2026-10-28)

CHARACTERIZATION (probe: tools/vkd3d/probe/rb12.c, new, five
variants with ONE variable changed per variant on the same
device/queue/fence: BUF = upload-to-default-to-readback buffer copies
only; CLEAR = clear-only render target, no draw; UPLDTEX =
upload-filled texture copied to a render target, then read back
(buffer-to-image and image-to-buffer copies, no draw); CONSTPS =
clear plus a constant-color fullscreen draw; DELAY = CLEAR repeated,
then a 2-second sleep and a second Map). Pre-fix, on the
committed-resource-fixed dist (wasm 1a3edfe6): BUF FAIL bad=64
(first mismatch at byte 0, got 0x00 want 0x10); CLEAR FAIL bad=48,
nonzero=0/784; UPLDTEX FAIL bad=62, nonzero=0/784; CONSTPS PSO OK
then FAIL bad=64, nonzero=0/784; DELAY FAIL on both reads (the
sleep changes nothing, so fence/map timing is not the gap). The
same zeros are what stopped root12's CBV case (64/64 pixels,
first px got=[0 0 0 0] want=[191 191 223 255]) and uav12's four
float EXEC cases (63-64/64 mismatches, got=0) after §4.23. The
bridge log's only lead was two DROPPED guest commands in
vkCmdCopyBuffer2 (guard `!src || !dst || !src->mem ||
!dst->mem`) — the copy handlers, not the draw or the fence.

ROOT CAUSE 1 (source-proven, then guest-proven): the "shim
unpacks Info2" assumption is false. The five 64-bit *2KHR copy
handlers read src/dst/count/regions from trap args as if the PE
shim unpacked the Info2 struct into v1-style args. The shim
never does that: tools/vkd3d/probe/gen_vk64pe.py generates
normal trap stubs as verbatim positional args, with exactly one
special case (vkCmdSetViewportWithCount / vkCmdSetScissorWithCount
insert first=0). The ABI header was split-brained (CopyBuffer2
already said (cmdBuf, pCopyBufferInfo*); the other four said
(unpacked)) and exports.txt stub arity was wrong for four of
the five (CopyBuffer2/CopyImage2/CopyBufferToImage2 1 instead
of 2, CopyImageToBuffer2 5 instead of 2). Consequences:
CopyBuffer2/CopyImage2/CopyBufferToImage2 lost the struct
pointer (args.a[1..]=0) and DROPPED; CopyImageToBuffer2 read
its region count from caller garbage and recorded zero copies,
so setupReadbacks found nothing and no readback ran, silently;
CopyBufferToImage2 recorded zero regions, so the upload never
landed, silently. vkd3d-proton calls the *2 variants
exclusively for copies, which is why DXVK's v1 paths worked
while every D3D12 readback returned zeros. The other 2KHR
handlers (SetEvent2, BeginRenderPass2, EndRenderPass2,
PipelineBarrier2, PushConstants2, BindIndexBuffer2,
BindVertexBuffers2) take verbatim scalar args or parse struct
pointers and are correct; the 32-bit CopyBuffer2 handler
already parses its struct pointer and does not share this
defect.

FIX 1. Each handler now reads its Info2 struct from args.a[1]
with rd64/rd32, drops loudly on a null struct pointer, and
keeps the nreg > 1024 guard (now guarding a real count).
Correct layouts, compiler-verified with offsetof against the
vendored header: CopyBufferInfo2 src@16 dst@24 count@32
regions@40; CopyImageToBufferInfo2 srcImage@16 dstBuffer@32
count@40 regions@48; CopyBufferToImageInfo2 srcBuffer@16
dstImage@24 count@36 regions@40; CopyImageInfo2 srcImage@16
dstImage@32 count@44 regions@48; BlitImageInfo2 srcImage@16
dstImage@32 count@44 regions@48 filter@56. (First attempt got
three offsets wrong by assuming a 4-byte pad after every
4-byte enum field; the rb12-3 buffer-to-image nreg > 1024 drop
exposed it. Padding exists only when the next field needs
stricter alignment.) The shim was rebuilt (GCC 13-win32,
-static-libgcc is required or Wine lacks libgcc_s_seh-1.dll)
and all three probe DLLs are byte-identical (sha 73f880f6...),
re-staged under all three PE names and re-pinned in
check_runtime_completeness.py. The build's runtime-assets
bootstrap 404 (the 20261026 release tar was never uploaded)
was resolved by the same deliberate re-pin pattern: the staged
wine64.zip entry in tools/browser/runtime-assets.json now
carries its new hash/bytes; no file was downloaded from the
external release URL.

ROOT CAUSE 2 (guest-proven by the rb12-2 strided pattern):
readback row pitch dropped. setupReadbacks dropped the copy
region's bufferRowLength and allocated a tight heap (w*h*4);
completeReadbacks copied one tight block to the guest; the
page tier also packs tight rows into the heap. The guest
(D3D12) reads rows RowPitch apart (256 bytes for a 4x4
RGBA8 image), so row 0 was correct and rows 1-3 read as zeros
at strided offsets — exactly rb12-2's CLEAR bad=36 (12 pixels
x 3 nonzero channels) and CONSTPS bad=48 (12 pixels x 4),
first mismatch at pixel 4 (first pixel of row 1), with the
full 4x4 image correct contiguously in the HEX dump.

FIX 2 (bridge only, no protocol or page change). The pending
record gains pitch: pend.pitch = rb.rowLen ? rb.rowLen * 4 :
rb.w * 4 (bufferRowLength is in texels; 0 = tightly packed
per Vulkan semantics). completeReadbacks copies per row from
heapPtr + y*tightRow to dstBase + y*pitch, bounds-clamped
per row; when pitch == tightRow the output bytes are
identical to the old single-copy behavior. The v1 I2B
handlers also store rowLen, so v1 readbacks gain the fix too
(with rowLen=0 behavior unchanged).

ROOT CAUSE 3 (guest-proven by the rb12-4 bridge log):
copy-only textures have no page target. UPLDTEX's render
target is never rendered and never sampled — only a
buffer-to-image destination then an image-to-buffer source —
so the page tier's readback lookup (targets.get('img:' +
srcImage) || 'imgd:' + srcImage) finds nothing, pushes
readback-unavailable, never touches the heap, and still
notifies the flag: zeros with no error. The bridge log
proves the upload itself executes (execCopy B2I ok, img
fmt=37 4x4) and that submit order puts that data in image
guest memory before setupReadbacks runs. The bridge does NOT
send buffer-to-image copies to the page (the "page handles
the copy" comment was stale); it handles them eagerly.

FIX 3 (bridge only). setupReadbacks seeds the heap from the
source image's guest memory after zeroing it and before
pushing the pending: look up the Image and its DeviceMemory,
require non-negative origins within the image extent, copy
tight rows from mm->va + im->memOff +
imageSubresourceOffset(*im, 0, 0) + oy*imgRow + ox*4
(mip 0 / layer 0, matching the page path; the I2B record
carries no mip/layer). Any lookup or bounds failure leaves
the zeroed heap and the page path proceeds exactly as
before. The seed survives exactly when the page has no
target, because the page overwrites the heap whenever it
has data. KNOWN LIMITATION (not solved here):
rendered-then-overwritten-by-buffer-to-image images read
stale page data; that needs page-side buffer-to-image
application, a bigger change.

POST-FIX PROOF (rebuilt dist, wasm 7075d8e0, "Browser export
verified"). rb12-5: BUF OK, CLEAR OK, UPLDTEX OK, CONSTPS
OK, DELAY OK (first=OK second=OK), SUMMARY all OK, RESULT 0.
The staged experiment table: rb12-1 (pre-fix) all FAIL;
rb12-2 (Info2 fix) BUF OK, CLEAR/CONSTPS/DELAY strided-FAIL,
UPLDTEX FAIL; rb12-3 (pitch fix) all OK except UPLDTEX;
rb12-4 (offset correction) same as rb12-3 with the
buffer-to-image drop gone; rb12-5 (prefill) all OK. The
original probes were re-run on the same dist to confirm the
fix generalizes: root12 CBV now FAILS with got=[255 0 255
255] (the magenta CLEAR) instead of zeros, bad=48; uav12
float EXEC now FAILS with mismatches=48/64, first px ch=0
got=255 (the clear) instead of 63-64/64 zeros. Both readbacks
now return real page data instead of zeros — the readback
mechanism is fixed; what stops them now is draw execution
(see next walls). Unchanged and out of scope: uav12 UINT
EXEC still fails at CreateGraphicsPipelineState
hr=0x80070057; root12 srv/uav still fail at D3DCompile
(E5000 in the probe's own HLSL strings).

NEXT WALLS (stated, not solved here). First: root12's CBV
draw is skipped at the page tier — the page cannot translate
the fragment SPIR-V module (error:
UnsupportedExtension("SPV_KHR_physical_storage_buffer")),
dropReasons shader-translation:1, draw-pipeline-skipped:2.
That capability comes from Route A's R15b root-descriptor
rewrite (push-block pointers become storage buffers), so
every D3D12 shader that reads a root descriptor currently
fails page translation. Second: uav12's EXEC draws are
skipped at the page tier for descriptor-group resolution
(dropReasons group-out-of-range:2/4,
draw-pipeline-skipped:4/8; the draws use descriptor tables,
bindSets/descSet, not root descriptors) — a page-tier
descriptor-group issue distinct from the first wall. Third:
the rendered-then-buffer-to-image staleness limitation from
Fix 3 above. None of these is a readback defect; the
readback path now delivers whatever the page actually
rendered (here, the clear).

GUARDS + SUITES + REGISTER. Guards:
web/tests/suite/unit/vk-copy2-info-structs.test.mjs (7
tests pinning the five Info2 layouts, the shim arity 2 for
all ten entry points, and v1 handlers untouched;
sabotage-verified both directions) and
web/tests/suite/unit/vk-readback-pitch.test.mjs (5 tests:
pending pitch, setupReadbacks pitch assignment,
completeReadbacks strided copy, prefill seed, prefill
bounds; sabotage-verified). The 5 new drop sites moved the
VK64_DROP_IF census pin 242 -> 247 (188 return-form + 59
flow-control; the readback-completion guard was reworded,
same continue site) with matching updates to the register
row and the macro-audit report addendum. JS suite 307/0
(baseline 305/0 at d39dec846 + the 2 new files, zero new
failures). Native suite: make -C project/linux test exits
0; the test binary single-threaded run reports 644 tests
OK, 0 tests FAILED in 198s (trailing fault output is the
designed final crash-path test). jit64_tests /
jit64_wasm_tests not re-run: the change touches only the
wasm-only bridge file, which the native build does not
compile (the test binary came up up-to-date with no
recompile), so those CPU gates are unaffected.
REGISTER: docs/known-gaps/vulkan.md gains three
IMPLEMENTED rows (Info2 parsing, readback pitch,
copy-only prefill); counts 361 -> 364 rows (65
IMPLEMENTED / 66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0
OPEN). The +93-line bridge insertion shifted later
citations; all 26 drifted rows were re-pinned (audit
--fix for the 13 single-citation marker/token rows,
diff-shift re-pin for the 13 func/multi-citation rows,
each verified against the shifted line — the audit's
nearest occurrence is unreliable for common function
names, where a neighboring call site can be nearer than
the moved true site); final audit: 0 DRIFT / 0 MISSING
(220 MATCH, 12 MATCH-INHERITED, 98 UNCHECKABLE, 7
EXEMPT).
## 4.25 Page-tier draw-execution walls: PSB strip, binding cap, WGSL name mismatch, group remap (2026-10-28)

With readback fixed (§4.24), both original probes stopped at DRAW
EXECUTION in the page tier, not at data delivery. Four distinct
defects stood between the probes and their first drawn pixel, found
in order — each one hid the next. All four are page-tier / tooling
fixes; no bridge, spirvfix-scheme, or probe change was needed.

### Wall 1 — root12 CBV: page could not translate fragment SPIR-V

Signature (reproduced from §4.24 evidence before changing anything):
`page tier cannot translate the fragment SPIR-V module
(error: UnsupportedExtension("SPV_KHR_physical_storage_buffer")`,
`draw-pipeline-skipped (shader-translation)`, CBV case returning the
magenta clear instead of the CBV-computed color.

Root cause: NOT the translator and NOT the R15b rewrite logic — a
one-word decoding bug in the R15b strip itself
(`tools/spirvfix/spirvfix.mjs`). `parseSpirv` stores operands only, so
an OpExtension string starts at `words[0]`; the strip decoded it with
`decodeStr` (the OpName offset, `words[1]`), yielding
`KHR_physical_storage_buffer`, which never equaled the full name — the
filter never removed the extension. Naga rejects the OpExtension
instruction itself regardless of capability use, so every
root-descriptor module failed page translation. (R8's own PSB strip
already decoded from `words[0]` correctly; only the R15b strip was
wrong. The older R15b tests passed because they checked capabilities
and the re-rooted variable, never the extension list — the same
audit-stricter-than-the-tier class as R11/R8.)

Fix (one line): the R15b strip now decodes with
`new TextDecoder().decode(new Uint8Array(strOf(i, 0)))`, mirroring R8.
Verified in Node on the captured root12 fragment module (byte-identical
to fixture `testdata/spirv/d3d12-ps-root-cbv.spv`): raw has the PSB
extension + cap 5347; fixed has neither, caps [1, 5345], `bw_root_p0`
intact, module re-serializes.

Guard: `spirvfix-r15.test.mjs` gains "R15b rewrite: the PSB extension
is stripped with the PSB capability" (fixture-based, behavioral).
Sabotage-verified: restoring `decodeStr` turns the new test red while
the older R15b tests stay green.

### Wall 1b — root12 CBV: re-rooted binding 4096 exceeds the layout cap

With translation fixed, the micro-probe (`TAG=root12-fix`) showed no
`shader-translation` drop — and a new failure: `pipeline-rejected:
[Invalid BindGroupLayout (unlabeled)] is invalid`, with Dawn's full
reason in the console: "On entries[0]: binding number (4096) exceeds
the maxBindingsPerBindGroup limit (1000)." R15b re-roots at binding
`4096 + pushByteOff/8` precisely so it (almost) never collides with a
module-owned binding — but 4096 exceeds Dawn's binding-number cap, so
the layout itself was invalid. The group remap (§4.25 wall 2) is
identity for this pipeline (0→0, 3→3), so this wall is independent
of it.

Fix (page tier only, `web/vkwebgpu.mjs` `getPipelineFor`, after the
group remap): per remapped group, compact bindings that exceed the
device's `maxBindingsPerBindGroup` (default 1000; the page tier does
not request it, so the default applies) into 0,1,2,… in sorted order,
rewrite the WGSL `@binding` annotations to match (a second pass that
changes only the binding axis), and record `origBinding` on each
remapped decl so draw-time set resolution (`bindFor` in both builders,
`samplerIdForDecl`) still keys off the original binding. The push
group is skipped (binding 0 stays the protocol's push slot); a group
needing more bindings than the cap fails `binding-count-limit`.
A spirvfix-side scheme change was rejected: the bridge mirrors the
4096 scheme for its collision audit (`vk64bridge.cpp`), so changing
it would need an identical bridge change plus a wasm rebuild, for a
fix that is general (any high binding) when done in the page tier.
`origBinding` is deliberately a new field, not `sourceBinding` (which
is set only for split combined samplers and read only by
`samplerIdForDecl`).

Guards: new `vk-descriptor-binding-remap.test.mjs` (2 tests: a
behavioral mock-render test driving the real pipeline builder — the
re-rooted binding must land at layout binding 0, push intact at
group 3 — plus a source-pattern test for the cap, the fail-closed
reason, and the `origBinding` lookups). Sabotage-verified: raising
the cap to 1000000 (remap skipped) turns both tests red.

### Wall 1c — root12 CBV: the draw rendered black — naga's trailing underscore broke R15 name resolution

With the layout fixed, the micro-probe (`TAG=root12-fix2`) showed an
empty `dropReasons` and a pipeline that builds — but the CBV case
still failed, now with `got=[0 0 0 0]` (all 16 pixels zero) instead
of magenta. The diagnosis, in order: the per-pass readback diagnostic
(`?bwPassReadback=1`) proved the GPU render target itself is constant
black after a pass that cleared and drew once with no skips — so the
draw wrote black, and the §4.24 readback path was exonerated (it
faithfully delivered the GPU's black pixels). A temporary page-tier
diagnostic then proved the re-rooted bind never resolved at all: the
bind-group builder's root branch never fired. The captured module's
WGSL, scanned with the page tier's own `scanWgslResources`, gave the
reason: naga emits the re-rooted variable as `bw_root_p0_` (trailing
underscore), but the scan's root regex required an exact
`bw_root_p(\d+)` match — so `rootOff` was never set, bind resolution
fell through to the (empty) descriptor-table branch, and the
"missing uniform data" path silently substituted a ZERO buffer. No
drop, no error, no counter: a valid bind group holding zeros, which
the shader dutifully read as gMul=gAdd=0. The same defect affected
the R15a clone regex (`bw_arr_g5_b0_p0_a0_` scanned with no `arrSig`);
for the uav12 EXEC cases the fallback accidentally resolves the
correct single element, so their behavior is unchanged (verified by
re-probe below). The combined-sampler regex already tolerated a
suffix (`(?:_\d*)?`) — only the root and array regexes assumed
SPIR-V names at a WGSL boundary.

Fix (two regexes, `web/wgsl-utils.mjs` `scanWgslResources`): accept
naga's optional trailing underscore —
`/^bw_root_p(\d+)_?$/` and
`/^bw_arr_g(\d+)_b(\d+)_(?:p(\d+)_a(-?\d+)|c(-?\d+))_?$/`.
Bare SPIR-V names still match.

Guard: new `vk-wgsl-r15-names.test.mjs` (4 tests: suffixed root name
resolves push offset 0; suffixed array name resolves its push
signature; bare names still resolve; unrelated names gain no
metadata). Sabotage-verified: restoring the exact-match regexes
turns the two suffixed-name tests red while the other two stay green.

### Wall 2 — uav12 EXEC: descriptor-group resolution failure

Signature: `group-out-of-range` (set 5) at pipeline build;
`draw-pipeline-skipped` for all EXEC draws. The EXEC shaders declare a
runtime descriptor array of storage images at set 5, binding 0 (the R15a
specialization target class); the bridge faithfully records DESC_SET
for sets 1–5, so set 5 is genuinely bound, not stale.

Root cause: the V2 pipeline builder gated groups to 0–2 + push 3,
encoding the WebGPU default `maxBindGroups = 4`. Guest Vulkan set
numbers are virtual (vkd3d heap-lowering tables, bindless sets) and
routinely exceed that budget. Raising the gate alone would only move
the failure to pipeline-layout validation on a 4-group adapter.

Fix (page tier only, `web/vkwebgpu.mjs` `getPipelineFor`): compact a
pipeline's non-push groups into the device's bind-group budget (push
stays group 3), rewrite the WGSL `@group` annotations to match, and
record a remapped→original map on the pipe record so draw-time set
lookups (`bindGroupEntries`, `bindGroupFingerprint`, sampler resolution)
still resolve the guest's original set index. Fail-closed preserved:
a pipeline needing more groups than the device offers fails
`group-count-limit`; a non-push declaration at group 3 fails
`group-3-non-push`. A spirvfix-side set change alone cannot work —
draw-time binding resolution keys off the original set index — and a
bridge-side remap would corrupt the V2 protocol's meaning.

Guards: new `vk-descriptor-group-remap.test.mjs` (3 tests: remap
present + old gate gone; draw-time lookups use original indices; both
fail-closed reasons present). Sabotage-verified: dropping the push
groupMap assignment turns the first test red.

### Suite fallout (all honest, no test weakened)

- `v2-replay-state.test.mjs` pinned the OLD group numbering (groups
  1/2/3); under the remap its two uniform buffers bind at 0/1 and push
  stays at 3. Assertions updated to the remapped indices with a comment;
  the test still asserts both buffers' exact sizes, push at 3, and push
  bytes [1,2].
- `vk-capabilities.test.mjs`: stale gate comment updated to the remap,
  and a stale test name ("tier-gated") renamed to "tier-remapped".
- `vk-register-citations`: each `web/vkwebgpu.mjs` edit shifted later
  citations. Two re-pin cycles (group remap, then binding remap), each
  16 mechanical rows (`audit.py --fix`) + 14 manual rows (ambiguous
  primaries, re-pinned to their fail/definition/call lines, plus
  orphaned duplicates re-pinned to their primary's new site so
  inheritance holds). Final audit: 0 DRIFT / 0 MISSING (220 MATCH, 13
  MATCH-INHERITED, 97 UNCHECKABLE, 7 EXEMPT). The register's
  descriptor-set prose bullet already described the remap behavior;
  no table rows were added or removed, so the count is unchanged:
  364 rows (65 IMPLEMENTED / 66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN).
- A transient `provenance-artifacts` failure during this lane was a
  probe-staging artifact, not a code regression: staging probe exes
  into `dist/runtime/prefix64.zip` makes its namelist differ from the
  pinned `web/runtime` copy by exactly those 8 entries. The suite
  result below is on the unstaged dist; staging is re-applied only
  for probe runs and removed after.

### Suites + probes (verified)

- JS suite: 310/310 files, 0 failed, config
  `BW_TEST_DIST=<tree>/dist`, `--unit` (baseline 307 at 0d835b123;
  +1 group-remap file, +1 binding-remap file, +1 r15-names file).
- Native: 644 OK, 0 FAILED, exit 0, 202s (`Build/Test/boxedwine
  0 0 1`); the diff touches no C++, so the binary is byte-identical
  to baseline and this run is a baseline confirmation. Trailing fault
  output is the designed final crash-path test.
- Micro-probes (slot-serialized, rebuilt dist, "Browser export
  verified" each time; staged probes removed after):
  - root12 CBV (`TAG=root12-fix3`): `CASE cbv OK (RT all 16 px match
    [191 191 223 255])`, zero drop/translation/group errors. The only
    remaining root12 failures are the known out-of-scope probe debt
    (`FAIL D3DCompile(srv-ps/uav-ps, ps_5_0)` — the probe's own HLSL
    syntax errors).
  - uav12 EXEC (`TAG=uav12-fix3`): case results IDENTICAL to the
    pre-fix baseline — R8G8B8A8_UNORM OK, R32G32B32A32_FLOAT OK,
    R16G16B16A16_FLOAT OK (all 16 texels match); R16_FLOAT FAIL with
    the same signature (see next wall); R32_UINT / R8_UINT FAIL
    `CreateGraphicsPipelineState hr=0x80070057` (out of scope, queued
    separately). No `group-out-of-range` remains anywhere.

### Next wall after these two

- **uav12 EXEC R16_FLOAT: Dawn rejects a read-only storage texture in
  r16float** — "Texture format TextureFormat::R16Float does not
  support storage texture access StorageTextureAccess::ReadOnly."
  The shader only reads (NonWritable), so the layout must be
  read-only as declared; the other EXEC formats (rgba8unorm, float32,
  float16x4) build fine, proving this is format-specific, not
  structural. Candidate narrow fix, UNPROVEN: upgrade
  shader-declared + layout access to `read-write` for NonWritable
  storage textures whose format rejects read-only — it needs probe
  evidence that Dawn accepts r16float + read-write. If that proves
  invalid, the honest alternatives are a sampled-texture rewrite path
  (large: spirvfix/page-tier WGSL rewrite + bridge TEXTURE_BINDING
  usage) or a loud fail-closed reason for read-only storage formats
  Dawn rejects.
- Still open, unchanged: rendered-then-B2I'd stale page data (§4.24
  known limitation — needs page-side B2I application); uav12 UINT
  EXEC PSO E_INVALIDARG (out of scope); root12 srv/uav D3DCompile
  probe debt (out of scope).

## 4.26 R16_FLOAT storage: the read-write candidate is refuted by probe, replaced with a named fail-closed reason (2026-10-28)

With draw execution fixed (§4.25), uav12 EXEC passes R8G8B8A8_UNORM,
R32G32B32A32_FLOAT, and R16G16B16A16_FLOAT (16 texels each) but R16_FLOAT
still fails. Reproduced on the §4.25 tip before changing anything:
`uav12: EXEC R16_FLOAT FAIL mismatches=48/64 first idx=0 ch=0 got=255
want=16`, with Dawn's "Texture format TextureFormat::R16Float does not
support storage texture access StorageTextureAccess::ReadOnly." The
shader only reads (a NonWritable UAV), so the layout must be read-only
as declared; the other EXEC formats build fine, proving this is
format-specific, not structural.

### The candidate, tested with probe evidence first — REFUTED

The §4.25 report's candidate was to upgrade shader-declared + layout
access to `read-write` for NonWritable storage textures whose format
rejects read-only. It was implemented (a page-tier rewrite applied to
both the WGSL declarations and the scanned decl types, so layout and
shader agree) and micro-probed — and the probe refuted it: with the
upgrade active, the rejection did not go away, it moved. Dawn now
reported "does not support storage texture access
StorageTextureAccess::ReadWrite." Dawn rejects r16float for BOTH
read-only and read-write storage access, so no access rewrite can serve
read-path storage in that format (write-only cannot serve a shader that
only reads). The upgrade was reverted, not left in the tree: a
proven-useless transformation must not ship, and it would only have
changed which rejection message the same failure produces.

Development note, kept because it is a silent-failure trap of its own:
the first form of the rewrite helper built its regex with `new RegExp`
from a string, and a JS string literal swallows the backslashes of `\w`
and `\s` — the regex could never match, with no error anywhere. The
helper was rewritten with a regex literal, and the guard pinned the
behavior (sabotage-verified: restoring the string form turns the new
test red). Any future regex built from a string needs a test that
executes it, not just constructs it.

### The fail-closed replacement

The validation error scope now opens BEFORE bind-group-layout creation:
Dawn reports a format/access rejection at `createBindGroupLayout`, and
outside a scope that error escaped as an `uncaptured` warning instead of
a named pipeline failure. At the pop, a rejection matching
`parseStorageTextureAccessRejection` fails with the named reason
`storage-format-access:FORMAT:ACCESS` plus the guest-original
group/binding (from `sourceGroup` / `origBinding` where remapped). The
generic `pipeline-rejected` path is unchanged for every other failure.

Guards: new `vk-storage-format-access.test.mjs` (the parse helper on
both Dawn messages plus non-matches; source-pattern pins for the scope
placement, the named reason, and the upgrade's absence).
Sabotage-verified: breaking the parse regex turns the unit test red.

### Suites + probes (verified)

- JS suite: 311/310 files, 0 failed, config
  `BW_TEST_DIST=<tree>/dist`, `--unit` (baseline 310 at b95ce0e51;
  +1 guard file, replacing the reverted upgrade's guard file).
- Native: 644 OK, 0 FAILED, exit 0, 203s (`Build/Test/boxedwine
  0 0 1`); the diff touches no C++, so this is a baseline confirmation.
  Trailing fault output is the designed final crash-path test.
- Register citations: 0 DRIFT / 0 MISSING (221 MATCH, 13 INHERITED, 97
  UNCHECKABLE, 7 EXEMPT). The re-pin cycles' side effect is recorded
  honestly: moving primaries without their dependents dropped
  MATCH-INHERITED 13→5 and pushed UNCHECKABLE 97→105, over the test's
  pinned ceiling of 102. It was repaired without touching the test:
  the 8 dependents were followed to their primaries' new sites
  (248→1943, 255→1970, 258→2016, 264→2314), restoring 13/97 exactly.
  One new REJECTED-LOUDLY row covers this wall, so the register is 365
  rows (65 IMPLEMENTED / 67 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN).
- Micro-probe (slot-serialized, rebuilt dist, "Browser export
  verified"; staged probes removed after): uav12 EXEC R16_FLOAT fails
  with reason `storage-format-access:R16Float:ReadOnly [@group(5)
  @binding(0)]`, zero `uncaptured` warnings for the rejection, and
  dropReasons `storage-format-access:1` (the generic
  `pipeline-rejected` is gone for this case). All other EXEC formats
  unchanged (R8/R32F/R16x4 OK; R32_UINT/R8_UINT still E_INVALIDARG at
  CreateGraphicsPipelineState, out of scope).

### Next wall after this one

- **uav12 EXEC R16_FLOAT, serving path: sampled-texture rewrite** —
  read-path storage in formats Dawn rejects must be rewritten to
  sampled reads (spirvfix/page-tier WGSL rewrite + bridge
  TEXTURE_BINDING usage). Large; queued separately, not built here.
- Still open, unchanged: rendered-then-B2I'd stale page data (§4.24
  known limitation — needs page-side B2I application); uav12 UINT
  EXEC PSO E_INVALIDARG (out of scope); root12 srv/uav D3DCompile
  probe debt (out of scope).

## 4.27 Session teardown at ~166 s: guest-side desktop-init race — no emulator defect isolated; BString::contains NUL blindness fixed en route (2026-10-28)

### The defect, as briefed

Minimal and real guests alike sometimes die at ~166 s with one signature:
the session tears down (services/explorer exit, imm32 exit_group), only
2 XWire clients ever connect (healthy runs show 5), and the guest never
maps a window. It hit a shipped-build Doom run twice (box loads 5.2 and
2.94), a Gang Beasts menu run, and the tiny Snake demo in the rig-fix
lane's live check. Brief: reproduce with a minimal guest, pin the
mechanism, fix product code if it pins there, otherwise prove a
rig/environment cause with the same rigor. Minimal guests only; no game
boots.

### Reproduction (this lane)

Snake on the shipped build (c01fdb666, the build Doom died on) reproduced
the full signature three times:

- **snake-c** — pid10 exit_group status=1 at 211.8 s page time
  (guestdiag=15 thread dumps running; explorer exits 220.5 s, session
  silent after).
- **snake-e** — pid10 dead at 295.9 s, with the wineserver's complete
  syscall trace captured (65,606 lines, BW64_TRACEPID=14).
- **snake-g** — pid10 dead at 163.7 s, with the app process's own
  complete syscall trace (BW64_TRACEPID=10).

Controls: two current-tip runs healthy (snake-a idle; snake-d under
contention, mapped at 234.0 s — a slower boot than any dying run's), and
one shipped-build run healthy (snake-f, mapped 162.4 s). The failure is
stochastic, and it has not reproduced at the current tip in this lane.
Gang Beasts is excluded from the family on its own log's evidence: the
game itself crashed (UnityCrashHandler64.exe opened at 259.3 s) roughly
300 s before its session unwound; only the downstream cascade is shared.

### What the traces pin

- **There is no fixed ~166 s deadline.** App-process death times across
  all evidence: 133.3 / 134.0 / 135.7 / 154.6 s (2026-10-27 runs) and
  163.7 / 211.8 / 295.9 s (this lane). The "~166 s" was when the cascade
  finished in the original runs, not a timer expiring.
- **The app process dies by its own decision, at the end of GUI init.**
  pid10 is the session anchor: its exit is the first failure event in
  every failing run; explorer and services unwind after it. In snake-g's
  trace its life is: a 25.2 s blocked connect to the wineserver, a
  77.3 s boot wait on the server socket, a 22.7 s wait spanning
  explorer's desktop creation (explorer needed >53 s from start to X
  connect), then 7.4 s of dense, promptly answered server round-trips
  (fd-passing recvmsg, file stats, closes), a final burst of completed
  round-trips inside the last 0.1 s, close, exit_group(1). In snake-c's
  dumps the same endgame shows the thread inside a win32u call
  (syscall 0x105c) with a freetype burst in its final sample. Nothing
  kills it: no signal, no trap, no unanswered request.
- **The wineserver is not the killer.** In snake-e's trace the server is
  alive and answering in the death second itself (replies on the app's
  channel at 295.815 / 295.858 / 295.862 s, a registry-file flush just
  before), and it reaps the app's fds (epoll_ctl DEL + closes at
  295.919-296.037 s) only after the app exits. Cause and effect run
  app-first.
- **The cascade is Wine's own session unwind.** With the app gone, the
  winstation desktop loses its last user; explorer exits ~2.6 s after
  creating the desktop window (SHAPE print) — Wine's close-desktop
  timeout in server/winstation.c (remove_desktop_user arms a 1 s
  close_timeout when desktop users drop to the desktop window itself).
  Vanilla Wine carries the same winstation_init / explorer /
  close_timeout race family (Wine bug 59393).
- **Cleared suspects.** (1) The wineserver's perpetual 16 ms epoll
  timeout: present identically in the healthy tip run snake-d (dumps
  show timeout=0x10 throughout, occasionally 3-5 ms) — the server's own
  cadence in this environment; it costs CPU but does not kill. (2) Box
  CPU load: deaths at load 2.94; snake-b stayed healthy under 4 added
  load hogs; snake-d survived heavier contention on the tip. (Boots are
  not box-CPU-bound: guest boot time did not move under added load.)
  (3) The M18 bare-wineserver race: its -p fix is present in every
  failing log, and this defect's wineserver demonstrably outlives the
  app and orchestrates the unwind. (4) The fd9/fd7 two-channel wait
  choreography: identical shape in healthy runs (snake-d spent 51-231 s
  in the same waits and mapped).

### Verdict

The fatal hop is inside the prebuilt guest Wine payload (win32u /
wineserver are payload binaries; there is no Wine source tree on the
box) losing its own desktop-init race when the emulated boot stretches
that race from ~1 s natively to tens of seconds: a startup call chain in
the app returns failure after its desktop wait resolves against a
desktop explorer is still creating, and the app exits status 1 instead
of ever mapping a window. No emulator defect was isolated — every
emulator-side suspect was checked against traces and cleared (the
socket layer delivered every reply; every wait returned; the server
never stalled). Accordingly **no product fix is shipped for the
teardown**: a speculative pacing patch would be exactly the wrong move.
The practical mitigation already happened incidentally — the current
tip's boot is much faster (wineboot exit 55.5 s vs 92-121 s in the
failing runs), which shrinks the race window; both tip runs here were
healthy. If it recurs at the tip, the next instrument is decoding
wineserver request opcodes from guest memory at the failing wait (the
tracer logs syscall entries only, not request buffers). The wasm-trap
question (the JIT-improved build's Doom run dying at 345.9 s with a
memory-access-out-of-bounds RuntimeError) was not reached: no run in
this lane hit a trap — every death was a clean guest exit_group — and
no game was booted for it. It stays open.

### Found en route, fixed: BString::contains stopped at the first NUL byte

The diagnosis leaned on BW64_TRACE_EXE and got zero lines while
BW64_TRACEPID produced 65k. The source gave up two separate facts:

1. **contains() was NUL-blind (fixed here).** BString::contains used
   strstr/strcasestr, which stop at the first NUL byte. BString is a
   length-carrying byte string and embedded-NUL instances are real
   (appendAfterNull builds them), so a needle past the first NUL could
   never match — a silent no-op for any such caller, in the same
   silent-disable trap class as the stale-ABI JIT disables. Fix:
   length-aware search over length() bytes in both overloads; the
   BString overload now searches with the needle's own length. NUL-free
   behavior, including the empty-needle and empty-haystack edges, is
   unchanged.
2. **The tracer was not in fact broken (recorded, not fixed).**
   KProcess::commandLine is built with BString::join("\0", args)
   (kprocess.cpp), but join's const char* delimiter is measured with
   strlen, so the NUL delimiter contributes zero bytes and argv lands
   concatenated with no separator at all — runtime proof in this lane's
   own logs: cmd='/usr/lib/wine/wine64C:\\windows\\system32\\wineboot.exe--init'.
   Substring matching still works on that string, so TRACE_EXE stands.
   Making commandLine truly NUL-joined needs a consumer audit first
   (exit/dump logging prints it through c_str() and would truncate at
   the first NUL); the call site should move to the BString-delimiter
   join overload with an explicit 1-byte NUL delimiter after that
   audit. (For the record: BW64_TRACE_EXE was also never actually set
   in the snake-e run — this lane's scratch-launcher patch placed the
   assignment as the body of a braceless if(). The 65k-line pid14 trace
   from that run came from the unconditional TRACEPID statement and is
   unaffected.)

### Guards + suites (verified)

- New native guard **testBStringContains**
  (source/test/util/testBString.cpp; Build/Test table index 601):
  needles in argv[1]/argv[2] past real NUL separators, ignoreCase past
  a NUL, a needle spanning a NUL through the BString overload,
  absent-needle and segment-boundary negatives, NUL-free regression
  cases. Sabotage-verified: against the original contains it fails at
  "contains must find a needle past the first embedded NUL (argv[1])".
- Native Build/Test suite (single-threaded, `boxedwine 0 0 1`):
  **645 tests, 0 FAILED** (644 baseline + the new guard). X64Test
  suite: **49/0**, identical to a pristine-baseline run. (An earlier
  4-threaded X64Test run showed a guest-topology FAILED and a teardown
  crash; both vanish single-threaded on pristine and fixed builds
  alike — harness-threading artifacts under load, not product
  failures.)
- JS suite: **311 passed, 0 failed**.
- Evidence: test-results/teardown/{snake-a,snake-b,snake-c,snake-d,
  snake-e-traced,snake-f-traced10,snake-g-traced10} in this tree
  (console JSONL + progress logs; snake-e/g include the full syscall
  traces cited above).

### Still open after this lane

- Teardown recurrence watch at the tip (see verdict); wineserver
  request-opcode decoding if it recurs.
- commandLine NUL-join consumer audit + call-site fix (above).
- Wasm-trap question (Doom B, 345.9 s) — untouched, needs a run that
  reaches it; no game boot authorized for it.
- BString::indexOf/lastIndexOf share the strstr pattern family with
  the old contains; not audited here — recorded as a sibling to check
  before any caller relies on them for NUL-containing strings.
## 4.28 32-bit memory properties: the HOST_CACHED advertisement gap exists but is NOT-A-GAP — DXVK treats the flag as optional (2026-10-28)

The §4.23 lane fixed the 64-bit answer and explicitly left the 32-bit
path untouched: the 32-bit handler in `vk32Bridge` hand-writes its own
memory-properties answer and never calls `fillMemoryProperties`. This
lane (mem32) determined whether 32-bit guests hit the same wall.

### Field-by-field: the two answers

| Field | 64-bit (`fillMemoryProperties`, post-§4.23) | 32-bit (hand-written; identical in the v1 and Properties2 arms) |
|---|---|---|
| Heaps | 2: 2 GiB DEVICE_LOCAL; 4 GiB flags 0 | 1: 4 GiB DEVICE_LOCAL |
| Types | 4 | 1 |
| Type 0 | heap 0, DEVICE_LOCAL \| HOST_VISIBLE \| HOST_COHERENT | heap 0, DEVICE_LOCAL \| HOST_VISIBLE \| HOST_COHERENT (0x0007) |
| Types 1–3 | DEVICE_LOCAL-only; HV \| HC \| HOST_CACHED; HV \| HC \| HOST_CACHED | — |
| Any HOST_CACHED type | yes (types 2, 3) | **no** |

So the advertisement gap exists identically: no 32-bit type carries
HOST_CACHED (0x0008). (The 32-bit handler also does not zero the guest
struct first; it writes only the count / type-0 / heap-0 fields.
Consumers read only what the counts advertise, so the unwritten tail
is inert.)

### Why it is not a wall: the consumer requirement differs

The 64-bit wall existed because vkd3d-proton's requirement is HARD:
`vkd3d_select_memory_flags` computes a required mask
(HOST_VISIBLE | HOST_CACHED for READBACK and CUSTOM WRITE_BACK heaps)
and the suballocation path has no fallback that relaxes it — empty
required mask, E_OUTOFMEMORY (§4.23, res12 guest proof).

The 32-bit consumers are DXVK (D3D9/D3D11), built from the vendored
source at `tools/dxvk/src` (pinned in `tools/dxvk/SOURCES.json`).
DXVK does request HOST_CACHED — D3D11_USAGE_STAGING buffers and
textures, CPU-read DEFAULT buffers, D3D9 staging buffers and mapped
texture buffers all ask for HOST_VISIBLE | HOST_COHERENT |
HOST_CACHED. But every one of those allocations flows through the
single `DxvkMemoryAllocator::alloc`, and that allocator classifies
the flag as a preference: `optionalFlags = DEVICE_LOCAL_BIT |
HOST_CACHED_BIT`, and when no type satisfies the full request it
strips the optional flags and retries (`src/dxvk/dxvk_memory.cpp`,
the alloc fallback; HOST_CACHED's only occurrence in DXVK's core).
The stripped request reduces to HOST_VISIBLE | HOST_COHERENT, which
the 32-bit answer's type 0 satisfies. The adapter passes the
properties through with no existence check, and the one behavioral
read of the bit (`d3d11_context_imm.cpp`, a Map heuristic: "Uncached
reads can be so slow that a GPU sync may actually be faster") is a
performance heuristic, not a requirement — and on this bridge the
underlying store is the same emulator RAM either way, so the
heuristic has no cost here either.

Real workloads corroborate: 32-bit Portal (DXVK D3D9) and 32-bit
Unigine Tropics (DXVK D3D11) already run on this exact answer,
staging allocations included — a hard requirement would have thrown
`DxvkError("DxvkMemoryAllocator: Memory allocation failed")` at the
first staging buffer.

### Verdict

NOT-A-GAP. The flag is absent from the 32-bit advertisement, but no
32-bit consumer in the shipped configuration requires it, so no
allocation can fail the way the 64-bit ones did. No product code
changed. The honest residual: a hypothetical future 32-bit consumer
with vkd3d-style hard selection (none ships here — Wine's wined3d
backends are overridden by DXVK per the staging scripts, and there
is no 32-bit D3D12 target) would need the flag; the guard pins both
sides of the verdict so that day is caught by the suite, not by a
guest failure.

### Guard

`vk-memory-properties.test.mjs` gains two tests, sabotage-verified
in both directions: the 32-bit advertised answer is pinned in source
(both handler arms: type count 1, type 0 = 0x0007, heap count 1,
4 GiB DEVICE_LOCAL heap), and the verdict's premise is pinned in the
vendored DXVK source (optionalFlags contains HOST_CACHED and the
strip-and-retry line is present). Removing HOST_CACHED from DXVK's
optionalFlags turns the premise test red and forces re-review of
this verdict; changing the 32-bit answer (0x0007 → 0x000f) turns the
shape test red.

### Suites (verified)

- JS suite: 311 passed, 0 failed (`BW_TEST_DIST=<tree>/dist`,
  `--unit`) — the baseline count at abdca70e9. The guard file keeps
  its name, so the file count is unchanged; its test count goes
  2 → 4.
- Native: 644 tests, 0 FAILED, single-threaded
  (`Build/Test/boxedwine 0 0 1`, 229s; the trailing fault output is
  the suite's designed final crash-path test). No C++ touched —
  baseline confirmation.
- Register: 366 rows = 65/67/234/0 (+1 NOT-A-GAP); citation audit
  0 DRIFT / 0 MISSING (221 MATCH, 13 MATCH-INHERITED, 98
  UNCHECKABLE, 7 EXEMPT).
- Dist: rebuilt in-tree; "Browser export verified"; the support
  archive is byte-identical to the checked-in pin (bbc583c6…).

## 4.29 UINT-PSO + probe debt: the Int64 was a utof workaround; root12's real compiler lands on the audit's PSB rejection (2026-10-28)

Two unrelated-looking failures remained on the micro-probes: uav12's
UINT EXEC cases (R32_UINT, R8_UINT) failed at
CreateGraphicsPipelineState with E_INVALIDARG while every float format
passed, and root12's srv/uav cases failed at D3DCompile with E5000.
Both are now pinned to their roots; one is fixed outright, the other is
fixed as a probe and lands exactly on the next designed wall.

### A. UINT EXEC: the rejection point is three layers from the symptom

Baselines reproduced on this lane's tree before changing anything:
`uav12: EXEC R32_UINT FAIL (CreateGraphicsPipelineState hr=0x80070057)`
and the R8_UINT twin, with float formats OK and flag=TRUE /
sweep=ALL18. A bisect probe (uav12x) isolated the trigger: float4,
float and SINT typed-UAV loads create their PSO; the UINT load fails
with an empty root signature too and passes when merely declared, so
the load itself -- not the table, not the declaration -- is the
trigger, and an SRV texture uint load fails the same way. Under
WINEDEBUG the failure names itself:

`warn:vkd3d-proton:d3d12_device_validate_shader_meta: Attempting to
use Int64 operations in shader 14eaf51505d31a3e, but this is not
supported.`

The chain, each link probe- or source-verified:

1. Guest vkd3d-proton compiles the PS, then
   `d3d12_device_validate_shader_meta` (libs/vkd3d/device.c) rejects
   it because the compiled SPIR-V declares `SpvCapabilityInt64` while
   `options1.Int64ShaderOps` is false. fmt12 confirms the guest view:
   `Int64ShaderOps=0`, `TypedUAVLoadAdditionalFormats=1` (all 18
   formats report Support2 UAV_TYPED_LOAD, so the without-format
   compile extension was NOT the issue). No Vulkan call is ever made
   for the PS: the bridge log shows the pipeline layouts and the VS
   module, then the failure.
2. The bridge honestly reports `shaderInt64 = VK_FALSE`: WebGPU has
   no 64-bit shader integers, so the validator is doing its job
   against a real platform limit -- IF the shader really used Int64.
3. It does not. A diagnostic census patched into the vendored meta
   extractor for one build (since reverted) counted exactly ONE
   OpTypeInt-64 in the 1624-byte module, and a word-level decode of
   the dumped module showed the only consumer: dxbc-spirv outlines
   every int-to-float conversion into a `cvt_*` helper function, and
   inside `cvt_u32_f32` sits `OpUConvert u32 -> u64` followed by
   `OpConvertUToF`. The image load itself is a clean OpImageRead;
   the 64-bit value is touched by nothing else.
4. The widening is `ArithmeticPass::lowerConvertItoF` in vendored
   dxbc-spirv, and its own comment says why: "Nvidia is broken as of
   580.76.05 and will treat UtoF inputs as signed with our code for
   some reason. Cast to the next largest integer type to work around
   the issue." Every HLSL `utof` (here `float(v)` on the loaded uint)
   therefore drags Int64 into the module. SINT passes because the
   signed conversion needs no such workaround; float loads pass
   because they contain no conversion at all. The "UINT" framing was
   a symptom -- any SM5 shader converting u32 to f32 hit this wall.

The fix is at the pinned root, in the vendored converter
(`ir_pass_arithmetic.cpp`): drop the U32->U64 widening, keeping the
U16->U32 widening (contained in 32-bit types). The vendored
converter's only target is this project's own WebGPU ICD, which is
not the broken Nvidia driver; `OpConvertUToF` on a U32 input is
spec-correct there (round-to-nearest-even, identical to D3D utof
semantics). A comment in the patch records the reasoning so a
re-vendor does not silently restore it.

After the fix (vkd3d rebuilt in-lane, rig pointed at the lane's
build.w64): `uav12: EXEC R32_UINT OK (all 16 texels match)` -- PSO
creation, draw, typed load and readback all serve end to end.
R8_UINT's PSO failure is gone too; it now draws and lands on the
NEXT layer down, the section 4.26 wall's twin: the page tier fails
its pipeline with the named reason
`storage-format-access:R8Uint:ReadOnly [@group(5) @binding(0)]`
(core WebGPU has no read-only storage for r8uint), so the probe
reports value mismatches (32/64) against the un-drawn target. That
is a genuine platform limit, already named and loud, with the same
queued serving path as R16_FLOAT (sampled-texture rewrite) -- not a
regression and not this lane's scope.

Guard: `vkd3d-utof-int64.test.mjs` pins the patch at source level
(marker comment present; no eU64 anywhere in lowerConvertItoF;
U16->U32 widening and the direct ConvertItoF intact).
Sabotage-verified: restoring the widening turns 2 of its 4 tests
red; the fix returns 4/4. Root-cause guard for the behavior itself
is uav12's R32_UINT EXEC line above.

### B. root12 probe debt: three probe defects, then the designed wall

The srv/uav cases never tested the product: they died inside the
probe's own D3DCompile calls. Three stacked probe defects, each
characterized before fixing:

1. **No buffer-typed HLSL parses under Wine's compiler at all.**
   The hlsl12x sweep compiled ten forms against Wine 10's builtin
   d3dcompiler_47 (vkd3d-shader HLSL): `Buffer`, `RWBuffer`,
   `StructuredBuffer`, `RWStructuredBuffer`, `ByteAddressBuffer`,
   `RWByteAddressBuffer` ALL fail E5000 at 1:1 (KW_BUFFER /
   NEW_IDENTIFIER); controls (cbuffer, RWTexture2D) pass. The
   earlier flavor swap from ByteAddressBuffer to Buffer<uint> could
   never have worked. The fix is the arrangement
   tools/vkd3d/stage-vkd3d.sh already documents for the demo path:
   a genuine Microsoft d3dcompiler_47.dll beside the probe, selected
   with `WINEDLLOVERRIDES=d3dcompiler_47=n`. The copy previously on
   the box is a 32-bit build (LoadLibrary err=126 in a 64-bit
   guest); a genuine PE32+ copy (4,916,728 bytes, sha256
   af7b99be1b8770c0e4d18e43b04e81d11bdeb667fa6b07ade7a88f4c5676bf9a)
   was taken from Chrome-for-Testing 131 win64's ANGLE
   redistributable and sits untracked at
   tools/vkd3d/probe/d3dcompiler_47.dll, as the stage script
   prescribes. The rig gained the matching opt-in:
   `D3DCOMPILER_PATH` in web/tests/d3d12-pixels.mjs stages the DLL
   and adds the override, inert when unset.
2. **The UAV register was illegal.** Under the genuine compiler the
   uav case failed with FXC error X4509: UAV registers share the
   output namespace in a pixel shader, so with a render target bound
   the UAV must start at u1. root12 now declares `register(u1)` and
   the root signature matches (uav12's u1 was already correct).
3. **The buffer flavor was wrong for root descriptors.** Typed
   flavors then compiled but dxil-spirv refused the conversion:
   "BDA root descriptors must be raw buffers, structured buffers or
   RTAS" -> E_INVALIDARG at PSO creation. Root descriptors are raw
   GPU VAs; the cases now use the original raw forms
   (`ByteAddressBuffer` + Load, `RWByteAddressBuffer` + Store),
   which is also what real root-descriptor shaders look like.

With all three fixed, both cases compile, convert, and reach the
bridge -- and hit the wall the R15 lane predicted in root12's own
header comment: the bridge module audit rejects the srv PS (and the
uav PS identically) with
`UNSUPPORTED spirv-capability-unknown:5347` at vkCreateShaderModule,
surfacing as E_FAIL at CreateGraphicsPipelineState. 5347 is
PhysicalStorageBufferAddresses, and the audit's Route-0 rule passes
it only when declared-but-unused; a root-descriptor shader genuinely
dereferences a physical pointer, and serving that in the page tier
(the spirvfix PSB rewrite at draw time) is the queued R15-serving
work, not probe debt. The cases now prove the whole path up to that
named, loud, designed rejection; the CBV case is unchanged
(`root12: CASE cbv OK`, all 16 px match), re-verified against the
lane's fixed vkd3d build as a regression check for part A.

Diagnostic probes committed with this lane: uav12x (PSO bisect
matrix + DXBC dump), hlsl12x (compiler-form sweep), fmt12
(guest-visible OPTIONS/OPTIONS1 + the 18-format Support2 table
behind TypedUAVLoadAdditionalFormats). The WINEDEBUG runner copy
used for diagnosis was scratch and is not committed.

### Suites + probes (verified)

- JS suite: 312 passed, 0 failed (`BW_TEST_DIST=<tree>/dist`,
  `--unit`; baseline 311 at abdca70e9, +1 guard file).
- Native: 644 OK, 0 FAILED in 282s (Build/Test/boxedwine 0 0 1;
  baseline 644 OK at abdca70e9; the diff touches no first-party C++,
  so this is a baseline confirmation. Trailing crash-path output is
  the designed final test.)
- Register: no row cites the vendored converter or the utof
  behavior; no citation re-pins needed (audit inputs unchanged).
- Micro-probes (slot-serialized): uav12 baseline vs fixed as quoted
  above; root12 in four stages (Wine-compiler E5000 pair -> genuine
  compiler: srv PSO E_INVALIDARG + uav X4509 -> register fix: both
  PSO E_INVALIDARG (typed-BDA refusal) -> raw flavors: both PSO
  E_FAIL on the audit's named 5347 rejection).

### Next wall after this one

- **Root SRV/UAV serving: genuine PSB dereference in the page
  tier** -- the audit's 5347 rejection is designed and loud; lifting
  it means spirvfix rewriting physical-pointer derefs to the
  4096+ root storage buffers for real root-descriptor modules.
  root12 srv/uav are the ready-made probes for that lane.
- **R8_UINT EXEC serving** joins R16_FLOAT on the sampled-texture
  rewrite queue (read-path storage in formats core WebGPU cannot
  bind read-only); its PSO wall is gone, only the page-tier
  format/access wall remains.
- SM6/DXIL utof conversions were not audited for an equivalent
  widening (the Nvidia-workaround comment exists only in the DXBC
  converter); a DXIL shader with u32->f32 conversion on this device
  deserves one probe before assuming symmetry.

### 4.30 V2 compute execution: dispatches served end to end; the two guest-visible consumers stop at pre-existing walls (2026-10-28, compute lane)

**Drop characterization (the "compute-entry" disposition, three drop points).**
Compute-only SPIR-V modules were accepted at creation (the audit's
`spirv-compute-entry` early-accept), but a dispatch died at three successive
gates: (1) record time in the 64-bit bridge — `vkCmdBindDescriptorSets` and
`vkCmdPushDescriptorSetKHR` rejected any bind point other than GRAPHICS
(`compute-descriptor-sets`); (2) submit time in the bridge emitter —
`serializeSubmitV2`'s CMD_DISPATCH case loud-dropped every dispatch
(`compute-dispatch`: "no V2 record; results are not produced"); (3) the page
tier had no compute records at all: the bridge V2Rec enum ended at
ROOT_DESCS (0x1D), `parseV2Chunk` rejected higher types, the SHADER record
decode threw for stage > 1, and no compute pipeline or dispatch decode
existed. Two adjacent data-flow defects mattered for serving: dispatch
recording dropped vkCmdDispatchBase's baseY/baseZ, and
`vkCmdCopyBuffer`/`vkCmdCopyBuffer2` executed an eager CPU memcpy at record
time — a copy out of a buffer a queued dispatch would later write could
never see the dispatch's results.

**Implementation.** Bridge (`vk64bridge.cpp`, +303/-29): new V2 records
PIPELINE_COMPUTE (0x1E), DISPATCH (0x1F), BUFFER_READBACK (0x20);
`vkCreateComputePipelines` captures specialization constants and the R15
root census, mirroring graphics; both bind gates accept GRAPHICS||COMPUTE
(residual rejection renamed `descriptor-bind-point`); dispatch handlers
record baseY/baseZ and, before recording, mark the storage-buffer ranges
the bound pipeline may write (replaying the command buffer's bound sets,
dynamic offsets included); a CopyBuffer/CopyBuffer2 whose source range
intersects a mark — with no bridge-observed write since (write-epoch proof)
— defers to a pending page readback instead of the eager memcpy, and
completion writes the page-produced bytes into guest memory with the
destination buffer's dirtyGen bump + vk64TouchMem (a new bump/write pair,
census-pinned). Protocol (`vk-protocol.mjs`): the three records decode
fail-closed (readback sizes range-checked 1..64MB; SHADER stage bound now
2). Page (`vkwebgpu.mjs`): compute pipelines are built by
`getComputePipelineFor`, which reuses the graphics path's machinery — the
§4.25 group/binding remap block was factored verbatim into
`remapDeclGroups` and is now shared; bind-group construction, the
fingerprint/persist cache, push-constant handling, §4.26's
storage-format-access naming, and the error-scope-before-layouts
discipline are all the graphics code paths. Replay executes real compute
passes (`beginComputePass`/`dispatchWorkgroups`); buffer readbacks copy
out of a registry of buffers actually bound as storage. Residual loud
drops, all named: dispatch with no compute pipeline or a non-zero base
group (`compute-dispatch`), a compute dispatch recorded into a render pass
(`compute-in-render-pass`), untranslatable modules (pipeline-creation
failure), 32-bit compute (never recorded on that ABI).

**Micro-probe (cs12, tools/vkd3d/probe/cs12.c).** Three variants. Before
the lane (pre3, baseline dist): BUF FAIL (all zeros), BUF2 FAIL, TEX FAIL —
the console shows the drops (`compute-dispatch` x53+, `compute-descriptor-sets`
x65+). Two characterization findings shaped the probe: vkd3d's clear-UAV
builtin takes a vkCmdFillBuffer fast path whenever the byte pattern repeats
every 4 bytes, so only differing-component multi-byte clears reach the meta
compute dispatch at all; and the in-guest d3dcompiler SM5 front-end accepts
no structured-buffer types, no thread-ID semantics, and only unrolled
constant-index texture writes with pure decimal literals. After the lane:
the record path is proven in the decoded chunk stream (meta frames decode
as bindComputePipe/push/dispatch; guest frames as full
bindSets/descSet/dispatch sequences) and the page tier's serving is proven
against a recording mock device replaying a real decoded guest frame
(compute pipeline built with COMPUTE-visibility storage texture, dispatch
issued, readback copy taken from the bound texture). The three variants
still fail in the guest, each at a pre-existing wall characterized to root
cause:

- **BUF/BUF2 (vkd3d's own meta clears)** stop at descriptor delivery for
  meta pipelines: their submit chunks carry SHADER + PIPELINE_COMPUTE +
  PUSH + DISPATCH but no descriptor-set records at all, although the meta
  shader needs exactly one descriptor (its push-descriptor bind never
  reaches the V2 stream), and the texel-buffer storage image then also
  misses its format injection (`StorageImageWriteWithoutFormat` at
  translation). Why the meta bind never enters the stream is unresolved —
  candidates are a bind-only preamble submit or a heap-set bind the
  emitter drops; it is a vkd3d-internal path, not guest-visible API
  behavior.
- **TEX (guest CS through descriptor-heap tables)** stops at the standing
  storage-image-write wall, now root-caused: vkd3d lowers a descriptor
  heap to a SPIR-V runtime array of storage images indexed by a
  push-constant heap index; spirvfix R15a specializes such arrays only for
  READ accesses (the array variable must be NonWritable), so an image
  WRITE through the array survives to naga, which rejects it
  (`InvalidImage([6])`, offline-reproduced through spirvfix + naga
  directly). The wall is stage-independent — it equally blocks fragment
  shaders writing UAVs through heap tables; graphics EXEC probes only
  passed because they write through render attachments. Guest root
  descriptors are not an escape: the vendored vkd3d rejects a compute PSO
  whose root signature carries a root UAV with E_INVALIDARG
  (probe-verified), so heap tables are the only guest compute shape here.

**Guards and suites.** New: `vk-compute-records.test.mjs` (5 tests —
record decode incl. fail-closed bounds) and
`vk-compute-serving-guard.test.mjs` (6 tests — source pins across bridge,
protocol, page), plus a native extension in `vk64_native_test.cpp`
(compute pipeline creation emits PIPELINE_COMPUTE; a bound dispatch emits
DISPATCH with its group counts and drops nothing; a non-zero-base dispatch
drops loudly and emits none). Census maintenance, all sanctioned by the
pins' own rules: VK64_DROP_IF sites 247 -> 250, dirtyGen bumps 20 -> 21,
guest-write primitive lines 52 -> 53 (register addendum). JS suite: 313 passed / 0 failed (311 baseline test files at the branch
point + the 2 new files above; the 5 pre-existing guards whose pins this
lane's changes move — drop-macro census, dirty-generation census,
pipeline-rejection extractor, spirv-rejections phrase, register
citations — were updated per their own maintenance rules and pass).
Native: the emulator test binary builds and exits 0; `run-native.py` is
broken at the branch baseline in this environment (its format-bpp table
assert fires identically on the pristine integ tree, and a later features
assert behind it) — the new native block was verified by executing it in a
diagnostic copy with only the pre-existing assert neutralized; it passes
and the failure resumes at the pre-existing assert downstream.

**Next walls, in order.** (1) spirvfix: storage-image writes through
runtime descriptor arrays (extend R15a specialization to writes) — this
unlocks guest compute through heaps AND heap-bound fragment UAV writes.
(2) vkd3d meta-pipeline descriptor delivery (why push-descriptor binds
for meta clears never enter the V2 stream) — unlocks differing-component
UAV buffer clears. (3) Sampled-texture serving path for r16float/R8_UINT
storage reads (already queued from §4.26 and the UINT lane).

### 4.31 Meta-bind delivery: the push-descriptor bind was swallowed by the PE shim's benign stub; storage texel buffers served end to end (2026-10-28, metabind lane + recovery)

**The wall, as inherited.** Section 4.30 left cs12's BUF/BUF2 variants
failing bad=64: vkd3d-proton's ClearUnorderedAccessView on a typed
texel-buffer UAV whose components differ cannot take its vkCmdFillBuffer
fast path, so it runs an internal meta compute pipeline
(cs_clear_uav_buffer_uint/float) that binds a STORAGE_TEXEL_BUFFER with
vkCmdPushDescriptorSetKHR and dispatches. The dispatch reached the page
tier with no descriptor set at all and was skipped, so the readback
served stale zeros. The wall turned out to be three stacked defects;
each layer's fix is necessary and none is sufficient alone.

**Layer 1 — the bind never left the guest.** The bridge has trapped
vkCmdPushDescriptorSetKHR (ABI 914) and
vkCmdPushDescriptorSetWithTemplateKHR (ABI 915) since the
push-descriptor work, and the unix shim (libvk64.c) exports both. The
PE shim did not: tools/vkd3d/probe/exports.txt carried
`vkCmdPushDescriptorSet 1 1` (the fnId-1 unimplemented stub) and no KHR
entries at all, and gen_vk64pe.py's vk64_resolve answers any name
missing from the table with vk64_benign, a do-nothing stub. vkd3d
therefore called a no-op for every meta push bind. Proven by
instrumenting the bridge: during BUF/BUF2, recordBindSets was never
called on the meta command buffer (5 recorded commands, 1 dispatch, 0
bind-sets), while the same buffer later carried TEX's ten heap binds
normally. Fix: exports.txt now traps the whole family
(vkCmdPushDescriptorSet + KHR at 914/6 args,
vkCmdPushDescriptorSetWithTemplate + KHR at 915/5), the shim was
regenerated and rebuilt per VULKAN1_DLL_README.md (sha256
ae96105b161f436711f5c7781263a4751c4893bef19c12a4078b42e0615a8c89; the
three staged copies stay byte-identical), the completeness pins moved
to the new hash (the two d3d12 pair pins were also re-pinned to the
restaged pair this tree already carries, a96b4e9d/3d3bfe43, matching
the integ re-pin), both shipped wine64.zip archives were restaged with
stage-vkd3d-payloads.sh, runtime-assets.json was re-pinned and the
runtime-artifact manifest regenerated. Guard:
vk-push-descriptor-shim.test.mjs (sabotage-verified: deleting the KHR
entry turns it red).

**Layer 2 — storage texel buffers served (the original lane's stack).**
Once the bind arrives it names a descriptor shape the stack rejected at
every tier. Bridge (vk64bridge.cpp): applyDescWrite's
STORAGE_TEXEL_BUFFER case records the view instead of logging
texel-buffer-storage; the V2 serializer gains wire kind 6 for the type,
sharing kind 5's V2_BUFFER_VIEW emission and view-range data flow;
vk64MarkComputeWrites marks a storage-texel binding's view range
GPU-written at dispatch record time, so a later copy defers to
V2_BUFFER_READBACK instead of reading stale guest bytes. spirvfix gains
R12S storageTexelBuffersTo2D: an OpTypeImage with Dim=Buffer, Sampled=2
is wrapped in the R12 2D layout (8192 wide, (i mod 8192, i div 8192));
only OpImageWrite/OpImageRead consumers are admitted, anything else
refuses the conversion whole. Page tier (vkwebgpu.mjs): a
storageTexelRegistry materialises a kind-6 view as a 2D storage texture
in the R12S layout, seeded verbatim from guest bytes (fail-closed to a
loud bind drop when the format or bytes are unknown); a null view id
serves the zero storage texture; and the readback resolution gains a
storage-texel fallback that copies the covered rows out of the storage
texture (with a srcSkip offset) so the guest receives byte-exact
written data. Storage texel stays UNADVERTISED in the format feature
bits — serving rides the descriptor path guests already take. Guards:
vk-storage-texel.test.mjs (wire decode + per-hop source pins), the
descriptor-type census moving STORAGE_TEXEL_BUFFER from class-rejected
to zero-served, and a native record-level block driving
vkCmdPushDescriptorSetKHR with a storage-texel write through kind-6
wiring, the dispatch write mark, and the deferred readback (verified in
a diagnostic harness copy: this branch's run-native.py aborts at a
pre-existing stale format-bpp assert, identical on pristine HEAD, the
same rot the vk64harness lane repaired on integ).

**Layer 3 — two over-broad refusals inside R12S (found by the
after-probe split: BUF2 cleared, BUF still bad=64).** Feeding the
captured guest modules through spirvfix offline isolated two vetting
defects, both id/value confusions. (1) The consumer scan treated every
instruction's operands as potential value uses, so the push block's
OpMemberDecorate Offset literal (35) "consumed" the image — the uint
module's image load is also numbered %35 (the float twin's is %36,
which is why only the uint clear refused). Metadata instructions
(capabilities, entry points, execution modes, source/debug records,
annotations) carry literals and global ids, never runtime values; the
scan now skips that family, and every instruction that can actually
consume a value stays vetted. (2) The write/read operand check refused
any instruction longer than the bare form; the uint write carries a
flag-only ZeroExtend image-operand mask (8192, no id operands). The
check now parses the mask and refuses only masks carrying id operands
(Lod/Grad/offsets/Sample/MinLod/scope ids), whose coordinate semantics
the 2D wrap does not rewrite; flag-only masks (NonPrivateTexel,
VolatileTexel, SignExtend, ZeroExtend, Nontemporal) pass through
verbatim. Regression: the captured cs_clear_uav_buffer_uint module is
pinned as a fixture in spirvfix-texel-buffer.test.mjs (converts and
naga-accepts with the view signature), plus a word-patched
Lod-operand variant that must still refuse; both fixes are
sabotage-verified against that fixture (unskipping OpMemberDecorate or
refusing every masked write each turn exactly that test red).

**Probe (cs12, uploading harness).** Before (this branch's baseline,
test-results/dxvk-cs12-meta-before3): BUF FAIL bad=64, BUF2 FAIL
bad=64, TEX FAIL bad=16. With only Layer 1+2 in the dist
(dxvk-dxvk-cs12-meta-after4): BUF FAIL bad=64, BUF2 OK — the split that
isolated Layer 3. After all three layers
(dxvk-dxvk-cs12-meta-after5): **BUF OK (bad=0), BUF2 OK (bad=0)**,
TEX FAIL bad=16, RESULT 1. TEX's failure is pre-existing on this
branch base — the spirvwrite/fmtinject/group-3 work it needs is not on
this branch (TEX is green on integ); RESULT 0 is expected once this
lane merges into the integ stack. Page stats for after5: both meta
modules translated, 4 meta dispatches executed, the ImageBuffer
translation failure gone; remaining translation failures are TEX's two
known module walls. tri12 spot check on the rebuilt shim: attempted once and starved at the harness timeout under box load (test-results/dxvk-dxvk-tri12-metabind-check: the guest never got past Wine startup, zero chunks captured -- the same starvation signature as the two cs12 timeouts that preceded the successful long-budget run; no verdict either way). The rebuilt shim's behavioural evidence is the cs12 full-path proof above (same loader, same DLL, device creation plus meta and heap traffic), the export-table guard, and the 316/316 suite. JS suite: 316 passed / 0 failed (the pre-recovery run on this branch was 313 passed / 2 failed: the spirvfix REWRITES census missed the new rule and the register citations had drifted with the lane's line shifts; both closed by this lane's bookkeeping, and the new vk-push-descriptor-shim file accounts for the +1 file). Provenance-artifacts and runtime-completeness, which failed mid-recovery against the interrupted export and the stale pins, pass against the final dist. Register: the texel rows' stale
storage-rejection text updated, one new IMPLEMENTED row for the PE-shim
push family, closing addendum below the compute addendum; 367 rows =
68 IMPLEMENTED / 66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN, citation
audit 0 DRIFT / 0 MISSING after re-pinning the citations the lane's
line shifts moved (plus the two spirvfix citations the Layer-3 edits
moved).
