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
## 4.30 V2 compute execution: dispatches served end to end; the two guest-visible consumers stop at pre-existing walls (2026-10-28, compute lane)

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
## 4.31 x86 vkd3d build: the 32-bit D3D12 frontend exists — PE32 vkd3d-proton built, staged, pinned (2026-10-28)

The vendored vkd3d-proton 3.1.0 was built and shipped only as 64-bit
guest DLLs; a 32-bit D3D12 guest fell back to WineHQ wine-stable
10.0's bundled pair in wine32.zip (d3d12.dll 642,962 B, d3d12core.dll
61,993 B) — upstream vkd3d ~1.16, no DXIL frontend, so the entire
SM 6.x path was 64-bit-only.

### Recipe provenance

No project-local x86 recipe existed. What existed: BUILD.md's
verified x86_64 recipe (meson + vendored cross file + the
PATHCCH_NONE box workaround) and the vendored upstream cross file
`src/build-win32.txt` (i686-w64-mingw32-gcc), never used on this box.
The lane's recipe is exactly those two combined, scripted as
`tools/vkd3d/build-win32.sh` (+ a BUILD.md "Build (32-bit guests)"
section): meson setup with the vendored win32 cross file, same
workaround, same options, then ninja. Nothing in the vendored source
is modified. Toolchain was already on the box (meson 1.12.1, ninja,
i686-w64-mingw32-gcc 13, i686 widl); nothing was missing except the
build itself and every piece of staging integration.

### Build result (verified)

- meson setup OK; ninja 209/209 (same step count as the 64-bit
  build). The build script re-runs cleanly and self-verifies.
- `build.w32/libs/d3d12/d3d12.dll`: 517,273 B, sha256
  872c3d65ad3febfb3fdbd935ca2272624a0e711724dd4013001dbc9ffd3ab0e1,
  PE32 machine 0x14c, exports the full loader set
  (D3D12CreateDevice, D3D12GetInterface, serializers, debug).
- `build.w32/libs/d3d12core/d3d12core.dll`: 9,022,393 B, sha256
  71e9c174084474d84e04893f0cef9eaea9bbede43201308e46b19b01fd727600,
  PE32 machine 0x14c, exports D3D12GetInterface + D3D12SDKVersion;
  imports are exactly the 64-bit build's system set (ADVAPI32, dxgi,
  GDI32, KERNEL32, msvcrt, api-ms-win-core-path-l1-1-0, VERSION) —
  no libwinpthread/libgcc/libstdc++ DLL dependencies.
- The PE build is not byte-reproducible (linker TimeDateStamp is a
  real timestamp; the generated vkd3d_version.h embeds the enclosing
  repo's git describe — this build reads
  "rootfs-pages-1486-ge2c11c8d3"). Same as the 64-bit pair: the
  canonical bytes are the staged ones, pinned, and a rebuild re-pins
  deliberately.

### Staging integration

- `tools/rootfs64/stage-vkd3d-payloads32.sh` (new) mirrors
  stage-vkd3d-payloads.sh: PE32 arch check first (a PE32+ copy is
  silently useless to a 32-bit guest), idempotence check, backup,
  ZipInfo-preserving merge, final byte verification. It replaces the
  pair at `opt/wine-stable/lib/wine/i386-windows/` in wine32.zip —
  prefix32's system32.link target, i.e. system placement.
- Wired into `tools/rootfs64/apply-runtime-poststeps.sh` after the
  64-bit vkd3d step, so a fresh assemble cannot omit it.
- Pins: `VKD3D32_PINS` + `t_vkd3d_payloads32` in
  `tools/rootfs64/check_runtime_completeness.py`; full gate run on
  the staged set: 0 failures (new check PASS; validate_service_runtime,
  manifest-match, and all pre-existing checks PASS; 2 designed skips
  for absent fresh-worktree caches).
- Artifact manifest regenerated: the w32 provenance line and the two
  member sizes are the only changes (wine32.zip now 239,760,722 B,
  sha256 0562fb32a654e57b1de848603581f094c563a40086999cc28111b6319c41c7ec,
  1926 members).
- Deliberately NOT done in-lane: `tools/browser/runtime-assets.json`
  is not re-pinned here. Its wine32.zip entry must be re-measured on
  the canonical tree at merge time (the apply-runtime-poststeps
  re-pin flow), together with the retained-archive rebuild — the
  same step the 20261026 set is still waiting on. The merge must
  also copy the two DLLs (or build.w32) into the integ tree and
  hash-verify them against VKD3D32_PINS before staging, since build
  outputs are gitignored and rebuilds produce different bytes.
- Register: runtime.md gains one IMPLEMENTED row (the w32 payload
  row in the DirectX section).

### What remains for full 32-bit D3D12 support

- Guest verification: no 32-bit D3D12 run has been made. A PE32
  probe now exists in-tree (`tools/vkd3d/probe/tri12-32.exe`, PE32
  build of tri12.c, compiles clean against the i686 toolchain);
  it is not staged anywhere yet, and the demo path does not pass
  WINEDLLOVERRIDES for 32-bit prefixes yet.
- Vulkan path beneath it: there is no PE32 bw64vulkan.dll (the bw64
  PE shim is 64-bit only). The vendored loader tries bw64vulkan.dll,
  then winevulkan.dll, then vulkan-1.dll, so a 32-bit guest resolves
  WineHQ 10.0's winevulkan.dll — whose registry is far newer than
  the Debian 8.0 winevulkan the 64-bit shim bypasses, so the
  maintenance5/6 blocker may not exist on this path; only a guest
  run can settle whether device creation works through it and what
  the 32-bit bridge (vk32) still owes vkd3d.
- Suites: JS suite and native suite results recorded with the lane
  commit (zero new failures vs the e2c11c8d3 baseline: JS 311/0,
  native 645 OK).

## 4.32 SPIR-V fix lane: storage-image WRITES through runtime descriptor arrays specialized (spirvwrite lane, 2026-10-28)

Merged into integ/winbox-20261022b (lane-spirvwrite, from d7b053e5d;
landed after the compute merge). Follow-up to section 4.30's third
next wall: the compute lane
proved dispatch + readback and three of four cs12 variants, but its TEX
variant -- a storage-image WRITE through vkd3d's descriptor heap -- died
in naga as InvalidImage([6]) because spirvfix's R15a specialization
served runtime descriptor arrays for reads only.

**The read-only behavior, pinned.** A vkd3d descriptor heap is one
SPIR-V runtime array of storage images (element = OpTypeImage
Sampled=2) indexed by a push-constant heap index. R15a decides
servability in analyzeDescriptorArrays (tools/spirvfix/spirvfix.mjs),
mirrored instruction-for-instruction by the bridge audit's
r15::analyzeArrays (source/vulkan/vk64bridge.cpp) so the bridge admits
capability 5302 exactly when spirvfix will specialize. Two gates made
it reads-only: (1) an early "writable gate" -- a storage-image array
whose variable lacked the NonWritable decoration was classified
unservable before its index signature was even collected; (2)
per-chain pointer-use rules reject any OpStore/atomic through an array
element. With the module left unspecialized, the runtime array reached
naga, whose WebGPU lowering has no binding arrays, and conversion
failed with InvalidImage([6]) -- reachable in practice only for
compute modules, because the bridge audit early-accepts compute-only
modules (graphics modules that write a storage image are rejected
earlier, as spirv-storage-image-write; unchanged).

**The extension.** Gate (1) is removed in both classifiers; writability
is now recorded on the analysis record (spirvfix: rec.writable; the
mirror classifies by the same signature/uses rules) instead of gating.
No rewrite change was needed, for two reasons verified in the code and
by test: specializeDescriptorArrays copies the array variable's
decorations verbatim onto the scalar clone, so a writable array yields
a writable clone (NonWritable simply is not there to copy); and a real
image write (OpImageWrite) operates on the OpLoad'ed image object, not
through the element pointer, so the pointer-use analysis never counted
it as a store -- gate (2) keeps guarding exactly the shapes it should
(pointer-level stores/atomics, texel-pointer escapes), unchanged. The
page tier also needed no change: scanWgslResources carries the WGSL
decl verbatim, vkwebgpu parses the storage access (read_write) from
it, and bw_arr_* element resolution is access-agnostic. End state:
naga emits texture_storage_2d<rgba8unorm, read_write> with
textureStore for the heap write.

**Offline before/after (the merge-independent proof).** The corpus had
no writable-heap capture (the cs12 TEX module itself was never
snapshotted), so the fixture is a model mutation of a real captured
module -- testdata/spirv/d3d12-ps-uav-heap-array.spv (a genuine vkd3d
heap-table array) -- dropping its NonWritable decoration, setting a
known image format (vkd3d knows the UAV format at PSO time; an Unknown
format fails earlier at the no-format capability, a different wall),
and inserting one OpImageWrite beside its OpImageRead, ids preserved.
Before (pristine spirvfix from git): strips only, naga FAIL
InvalidImage([6]) -- the compute lane's exact error. After:
descriptor-array-specialize applies with the (writable) log marker,
naga OK, WGSL carries the read_write storage texture and textureStore.
Fixture: testdata/spirv/d3d12-ps-uav-heap-array-write.spv.

**Guards.** spirvfix-r15.test.mjs grows 15 -> 22 tests: writable census
(read fixture discriminates writable=false), the writable clone rewrite
(clone keeps set/binding, carries NO NonWritable, the OpImageWrite
survives, capability 5302 still stripped, no runtime array survives),
the naga read_write/textureStore conversion, fail-closed residues (a
second index signature and a pointer-level store and a texel-pointer
escape all still decline byte-identical), and a bridge-mirror source
pin. Sabotage-verified both directions: restoring the JS gate fails
exactly the 7 new tests; restoring the bridge gate fails exactly the
mirror pin; both files restored byte-identical afterwards.

**Probes** (slot-serialized, this lane's dist, wasm 5b3caf1c). cs12:
RESULT 1 with BUF bad=64, BUF2 bad=64, TEX bad=16 -- byte-for-byte the
compute lane's pre-serving baseline (its pre3 run): this branch has no
compute serving, so all three variants still fail on the
spirv-compute-entry notices, and crucially TEX fails at dispatch like
the others, with zero InvalidImage anywhere in the guest log (that
error can only appear once the compute lane's serving carries the
module to translation -- the merge-time check below). uav12 with the
integ-staged DLL pair: the three EXEC formats OK, R16_FLOAT stops at
the named storage-format-access:R16Float:ReadOnly rejection
(draw-pipeline-skipped x2, matching the coordinator's compute-tree
read), and R32_UINT/R8_UINT fail PSO creation -- traced to the staged
pair itself: integ's build.w64 d3d12core (md5 006c754e) predates the
section-4.29 dxbc-spirv rebuild (the uintpso lane's pair, md5 e1319ca9,
differs). Re-run with that pair: R32_UINT EXEC OK (all 16 texels,
the 4.29 shape verbatim), R8_UINT's PSO creates and its draw stops at
the named storage-format-access:R8Uint:ReadOnly wall, R16_FLOAT
unchanged -- the A/B isolates the variable to the staged DLLs and
proves this lane's edits disturb none of the typed-UAV paths. root12
(uintpso pair + the real d3dcompiler_47.dll): CASE cbv OK, RT all 16
px match [191 191 223 255]; srv/uav still stop at
spirv-capability-unknown:5347 (the queued page-tier PSB wall),
unchanged.

**Merge-time guest check (merged tree, 2026-10-28; evidence:
test-results/dxvk-spirvmerge-cs12/).** With the compute merge (section
4.30) underneath, cs12 on the merged tree's dist: TEX still fails
(bad=16) but NOT at this lane's wall -- zero InvalidImage anywhere in
the guest log, the R15a writable specialization applies in-guest, and
TEX's dispatch reaches the page tier with its full bindSets/descSet +
push + dispatch sequences. It stops at the next, already-named wall:
storage-image-write-without-format. The real dxil-spirv heap module
carries its storage image with format Unknown; naga/WGSL requires a
concrete texel format for a storage write
(UnsupportedCapability(StorageImageWriteWithoutFormat)), and nothing
yet injects the guest UAV's view format into the specialized clone.
(The offline fixture above set a known format to isolate the
writability gate, so this stacked wall was invisible offline.) The
format-injection follow-up lane is running; the same wall also gates
the meta path's storage writes once meta-bind lands. BUF bad=64 /
BUF2 bad=64 are unchanged (the vkd3d meta-bind wall), so cs12 remains
RESULT 1 until both follow-ups land; this section claims the
writability gate only.

**Suites.** JS 312 passed / 0 failed (the r15 file's growth is inside
its own count); native 647 tests, 0 FAILED, 205s single-threaded
(Build/Test/boxedwine 0 0 1). One multi-threaded run along the way
failed only the setitimer timing probe under load ~7; it passes
single-threaded on this binary, and the identical flake was recorded
the same day on the merged integ tree without this change -- not a
regression. The spirv-offline corpus gate moves 31/33 -> 32/34 (the
new fixture converts; the two pre-existing Unknown-format read
fixtures fail the OFFLINE gate identically before and after -- an
offline-harness artifact, see 4.25-era notes).

Register: vulkan.md grows one IMPLEMENTED row (367 rows in the
merged tree: 68 IMPLEMENTED / 66 REJECTED-LOUDLY / 233 NOT-A-GAP /
0 OPEN).

Probe sources: tools/vkd3d/probe/cs12.c + cs12.exe copied from the
compute lane's tree (byte-identical) so the branch carries the probe
the merge-time guest check needs.

Known residue (fail-closed, not a silent wrong answer): the shared
analysis's word scan treats any instruction word equal to the array
variable's id as a use of it, so a literal operand colliding with a
small variable id (e.g. an OpCompositeExtract index) can false-decline
an array module. Observed on an id-renumbered synthetic (var %3);
captured modules (var %14) do not collide. An operand-aware scan is
the fix when it bites.

Next walls: vkd3d meta-pipeline descriptor delivery (the cs12 BUF/BUF2
wall -- the metabind lane's brief), then sampled-texture write serving
for the formats Dawn storage rejects outright (r16float, R8_UINT),
then the storage-image format injection named above (the merge-time
cs12 TEX guest check ran at this merge and stopped at that wall --
see the merge-time paragraph).

## 4.33 Storage-image format injection: the tier resolves the written element's view format (fmtinject lane, 2026-10-28)

Merged into integ (lane-fmtinject, from 7817bd19c, the spirvwrite
tip). Follow-up to section 4.32: with writable
heap arrays specialized, the spirvwrite merge attempt carried cs12 TEX
in-guest to the page tier with full bind sequences and zero
InvalidImage -- and hit the next wall in the stack: the real heap
module declares its storage image with format Unknown, naga requires a
concrete texel format for a storage WRITE, and nothing injected the
guest UAV's view format for an array variable. The tier reported
'cannot translate the compute SPIR-V module
(UnsupportedCapability(StorageImageWriteWithoutFormat))' and skipped
both dispatches. The same wall stood behind the meta path named in
4.30's next-walls.

**The data flow, pinned.** The guest's view format never needed a new
record: vkd3d writes the UAV into the guest heap, the bridge's
descriptor updates carry each write as a V2 DESC_SET entry {binding,
dstArrayElement, count, kind=2 (image), obj=viewId}, and the page
tier's view registry resolves obj -> image -> the VIEW's format
(F.views / imageForView, which applies the view-format override) ->
the V2_STORAGE_FORMAT spv number. spirvfix R14
(injectStorageImageFormats) has always been able to inject that number
into the OpTypeImage format operand (words[7]) given a
"set:binding=spvFormat" signature, and the tier builds the signature
in storageSignatureFor. The defect was the resolution: for a
format-Unknown variable it read the entry at ELEMENT 0
(bindingForElement(..., 0)). For a direct binding that IS the element;
for a heap array the shader's element is the R15a index signature
evaluated per draw, and element 0 can hold another view -- or none.

**The failing shape, from the captured stream.** Decoding the merge
attempt's evidence chunks (integ test-results/dxvk-spirvmerge-cs12):
cs12 TEX pass 2 binds set 5 binding 0 with TWO entries -- element 0
written with a NULL view, element 1 with tex2's UAV (view format
R8G8B8A8_UNORM) -- and the module's array index signature is push@0 + 1
(descriptor-table start 0 + the UAV range's heap offset). Element-0
resolution found the null entry, produced no signature part, and R14
left the type Unknown: exactly one PIPELINE-FAILED line (pass 2's
pipeline) in the guest console, both frames' pass-2 dispatches
skipped, readback zeros, TEX bad=16. Pass 1 (signature push@0 + 0,
element 0 = its UAV) resolved under the old rule and its dispatch ran.
uav12 never bit this because its heap target sits at element 0.

**The fix (three layers, all fail-closed).** (1) spirvfix
storageImageVars: a runtime-array storage variable's entry now also
carries array:true and index -- the array's single proven index
signature from analyzeDescriptorArrays (the same analysis R15a serves
on), or null when the array is unservable or multi-signature.
Non-array entries are byte-identical to before (the coverage suite's
deepEqual pin holds). (2) New pure helper resolveStorageVarFormat
(web/wgsl-utils.mjs): given the binding's DESC_SET entries, the
variable record, the draw's push block, and a view->spv-format
callback, it returns the format ONLY when proven for the touched
element -- constant index directly; push index evaluated as
u32(push, byteOff) + add and proven only by an entry COVERING that
element with a non-null, mappable view. When the caller has no push
block, the bound image entries prove the format only if every one
carries a view and all agree on ONE format. Anything else returns
null; the signature omits the variable, the type stays Unknown, and
translation keeps the named rejection. (3) web/vkwebgpu.mjs:
storageSignatureFor delegates per-variable resolution to the helper
(direct variables keep the long-standing element-0 rule inside it);
getPipelineFor takes the draw's push block and all three draw sites
pass st.pushBlock -- the same value the bind-group builder uses for
bw_arr_* element resolution, so the injected format and the bound
clone element describe the same draw. The signature feeds the
pipeline cache key, so different proven formats key different
pipeline variants, as typed-UAV serving already did.

No bridge or record-stream change was needed (the format was already
carried, as view ids in DESC_SET entries). Read serving is untouched:
formats Dawn rejects as storage keep their dispositions (the
storage-format-access named rejections; the r16float/r8uint READ
serving path is lane-sampledtex's sampled-texture rewrite), and R14's
kind check still refuses a format whose scalar kind differs from the
image's.

**Offline proof (the definitive, merge-independent kind).** The
fixtures are the REAL captured cs12 modules, extracted from the merge
attempt's V2 chunk stream (the first frame's SHADER records):
testdata/spirv/d3d12-cs-uav-heap-write-unknown-pass1.spv (index
push@0+0) and -pass2.spv (index push@0+1; it also carries the set-3
sampled-image array). On pass 2: element-0 resolution builds an empty
signature and naga fails with
UnsupportedCapability(StorageImageWriteWithoutFormat) -- the exact
in-guest error, reproduced offline. The resolver, fed the captured
descriptor state (element 0 null, element 1 = the RGBA8 view) and the
pass's push block, yields 5:0=4, and naga accepts:
texture_storage_2d<rgba8unorm, write> with textureStore. A
kind-mismatched signature (5:0=39, R8ui against the float-typed
image) is refused by R14 and the wall stands; an element view in
Rgba8Snorm injects 5 and yields rgba8snorm (the format comes from the
element's view, not a constant).

**Guards.** New web/tests/suite/unit/vk-storage-format-inject.test.mjs
(12 tests): reflection pins on both fixtures plus the uav12 read
fixture; the resolver proof rules (pass-2 strict and fallback, pass-1,
push-value table-start selection, constant index, and the fail-closed
set -- uncovered element, null view at the element, unmappable
format, disagreeing elements without a push block, unproven index);
end-to-end naga conversions; tier wiring source pins. Sabotage-
verified at all three layers: restoring element-0 resolution in the
helper fails 6 tests; suppressing the reflection's index fails 7
(across this file and spirvfix-r15); dropping one draw site's push
block fails the wiring pin; every sabotage restored byte-identical.

**Suites.** JS 324 passed / 0 failed (the 312 branch baseline + the 12
new tests). The spirv-offline corpus gate moves 32/34 -> 32/36: both
new fixtures fail the BARE gate with the same named
StorageImageWriteWithoutFormat class as the two pre-existing uav12
heap fixtures -- the gate supplies no bound-view signature, which is
precisely the wall; with the resolver-built signature both convert
(proven above). No bridge or native code was touched (vk64bridge.cpp
untouched, so no run-native.py needed). Citations: this lane's
vkwebgpu/spirvfix line shifts re-pinned; the register citation audit
is back to 0 DRIFT / 0 MISSING (224 MATCH / 12 MATCH-INHERITED /
98 UNCHECKABLE / 7 EXEMPT).

Register: vulkan.md grows one IMPLEMENTED row (368 rows on the
merged tree: 69 IMPLEMENTED / 66 REJECTED-LOUDLY / 233 NOT-A-GAP /
0 OPEN).

**Merge-time guest check (merged tree, 2026-10-28; evidence: integ
test-results/dxvk-fmtinject-merge-cs12/).** This base (7817bd19c)
predates the compute merge, so the in-guest cs12 run belongs to the
merge. It ran on the merged tree: the format wall is GONE --
renderErrors [] (was StorageImageWriteWithoutFormat), pipelines
translated 2 / rejected 0 (was 1 / 1), pass 2 converting with the
injected format through the compute caller's no-push-block fallback
exactly as predicted below (integ's getComputePipelineFor calls
storageSignatureFor with two arguments; the new third parameter
defaults to null, and the resolver's unique-agreement fallback
resolves cs12 pass 2 there -- its bound elements are one null write
and one RGBA8 view -- so no integ-side caller change was needed).
TEX still reports bad=16, one stage later, at the next named wall:
PIPELINE-FAILED reason=group-3-non-push (dropReasons
{group-3-non-push: 1, dispatch-pipeline-skipped: 2}) -- pass 2's
module also declares its sampled-image array at descriptor SET 3,
the page tier's reserved push-constant group, and remapDeclGroups
(section 4.25) fails closed on a non-push group-3 declaration instead
of compacting it like other guest sets; translation precedes the
group remap, so this wall was invisible until the format wall fell.
This section claims the format wall only; the group-3 compaction
follow-up (lane-group3, stacked on this lane) merges next and
carries the cs12 TEX bad=0 gate. cs12 RESULT 0 additionally needs
the meta-bind delivery (BUF/BUF2) from lane-metabind.

**Gate outcome (stacked group3 merge, 2026-10-28; evidence: integ
test-results/dxvk-fmtinject-group3-merge-cs12/).** With section
4.34's compaction merged on top, the gate ran on the combined tree:
'cs12: VARIANT TEX OK (bad=0)' -- both TEX dispatches serve end to
end; zero group-3-non-push occurrences in the guest console;
dropReasons {}; renderErrors []. BUF/BUF2 still fail (bad=64) at
the meta-bind wall, so cs12 RESULT stays 1 until lane-metabind
lands.

Next walls: vkd3d meta-pipeline descriptor delivery (cs12 BUF/BUF2 --
lane-metabind), sampled-texture serving for the formats Dawn storage
rejects outright (lane-sampledtex), page-tier PSB serving
(spirv-capability-unknown:5347 -- root12 srv/uav), then the merge-time
cs12 full pass.


## 4.34 Guest descriptor set 3 compaction: the reserved push group splits (group3 lane, 2026-10-28)

Merged into integ (lane-group3, from 2612ecb66, the fmtinject tip),
stacked on section 4.33's merge. Follow-up to section 4.33: the fmtinject
merge attempt proved the format wall gone in-guest (pipelines
translated 2 / rejected 0, renderErrors []) and cs12 TEX advanced one
stage -- to the descriptor-group remap, where it died as
'PIPELINE-FAILED reason=group-3-non-push' with both dispatches skipped
(evidence: integ test-results/dxvk-fmtinject-merge-cs12/). Translation
precedes the remap, which is why this wall was invisible until the
format wall fell.

**The failing shape, from the real module.** cs12 TEX pass 2's
translated WGSL (spirvfix -> naga -> push rewrite -> resource scan,
reproduced offline from the captured fixture
testdata/spirv/d3d12-cs-uav-heap-write-unknown-pass2.spv) declares
three resources: the push block 'registers' at group 3 binding 0
(var<uniform>), the R15a clone of the sampled-image heap table
(bw_arr_g3_b0_p0_a0_, texture_2d<f32>) ALSO at group 3 binding 0, and
the storage clone (bw_arr_g5_b0_p0_a1_) at group 5. vkd3d heap-lowering
numbered the sampled table set 3 -- a real guest set, bound in the
stream like any other -- and the tier's section-4.25 remap failed
closed on any group-3 declaration that was not the push binding.

**The invariants, pinned.** Group 3 is the protocol's push group
(V2_PUSH_GROUP): the draw loop skips it in the generic bind pass and
binds the push UBO there separately when the pipeline has a push
block; bind-group construction and its fingerprint skip it likewise.
A declaration left at group 3 after the remap is therefore NEVER
bound -- so the remap's real invariant is not "group 3 is forbidden"
but "after the remap, group 3 holds only the push declaration". The
push declaration is proven by shape: group 3, binding V2_PUSH_BINDING,
addr 'uniform', created by rewritePushConstantsWGSL from the stage's
push block (hasPush). groupMap (final group -> original set) is
consulted per bound group, so the push group mapping to set 3 and a
compacted guest group also mapping to set 3 coexist without
interference; a guest declaration's bound content resolves from the
guest's original set 3 via its final group's groupMap entry.

**The fix.** New pure helper planGroupRemap (web/wgsl-utils.mjs),
following the resolveStorageVarFormat pattern of section 4.33: it
classifies each declaration, assigns final groups, and returns the
per-declaration targets, the groupMap, the @group number-rewrite map,
and -- in the split case -- the guest declarations to move by name.
Guest groups (the guest's set 3 numbered among them) take free slots
in ascending order, skipping group 3; the push declaration keeps
group 3; binding numbers within a set are unchanged. The WGSL is
rewritten in two steps: the existing number rewrite first (in the
split case it maps group 3 to itself, the push declaration's home),
then rewriteGuestGroupDeclWGSL moves each guest set-3 declaration by
its (binding, name) -- a number rewrite cannot split one @group
between two targets, and the push declaration may share the guest's
binding number (the fixture's clone sits at binding 0 too).
At merge the helper is wired into remapDeclGroups (the factored
section-4.30 home of this block): the shared function plans through
the helper, applies the number rewrite and the by-name split to every
stage holder, and keeps its binding remap, so the graphics and
compute builders share one classification; the lane's two
stage-literal wiring pins were adapted to the shared holder loop
(intent preserved). Fail-closed residues: a storage buffer at (group 3,
binding 0) stays 'group-3-non-push' -- the push slot's only legitimate
buffer occupant is the push UBO, so a buffer there in a non-push shape
is ambiguous -- and more distinct groups than the device budget stays
'group-count-limit'. A uniform buffer at the push slot with NO push
block is provably a guest buffer and compacts. Pipelines with no
guest declarations at group 3 receive exactly the pre-existing
assignment (pinned by a legacy non-regression test).

**Proof (offline; this base predates compute serving, so the in-guest
cs12 TEX check is the merge-time gate, as with 4.33).** On the real
pass-2 fixture: the plan puts the sampled clone at group 0, the
storage clone at group 1, the push declaration at group 3, groupMap
{0:3, 1:5, 3:3}; the rewritten WGSL rescans to exactly those
declarations, push intact. Pass 1 (no guest set 3) keeps its old
assignment. Guards: vk-descriptor-group3-compaction.test.mjs (9 tests:
both real fixtures, synthetic split / set-3-only / push-slot shapes,
the budget edge, legacy non-regression, tier wiring pins),
sabotage-verified three ways -- restoring the blanket group-3
rejection fails 5 of them, disabling the by-name WGSL rewrite fails
the fixture's rescan assertions, disabling push recognition fails 7.
The section-4.25 source-pin test's two pins that named the old inline
assignment moved with it to the helper. Suites: full JS suite 313
files passed / 1 failed -- the failure is provenance-artifacts'
wine32.zip dist-vs-web hash, cross-lineage only (the integ dist
carries the x86-staged archive at 955cc41c...; this branch's manifest
pins the pre-staging archive at 3da7c635..., against which
bootstrap --check passes), A/B-proven pre-existing on the pristine
tree in the same environment; zero new failures. No bridge or native
code touched (no run-native.py needed); the spirvfix corpus is
untouched (32/36 stands). Register: one new IMPLEMENTED row (369 rows on the merged tree =
70 IMPLEMENTED / 66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN);
citations 0 DRIFT / 0 MISSING on the merged tree (225 MATCH /
12 MATCH-INHERITED / 98 UNCHECKABLE / 7 EXEMPT); the new row's
planning-call citation was re-pinned at merge to the shared
function's line (vkwebgpu.mjs:1748).

**Merge-time gate (ran on the merged tree, 2026-10-28; evidence:
integ test-results/dxvk-fmtinject-group3-merge-cs12/).**
lane-fmtinject (section 4.33) and this lane merged into integ in
that order; the combined tree verifies: dist 'Browser export
verified', JS suite 316 files / 0 failed, native suite 647 / 0,
citation audit 0 DRIFT / 0 MISSING (225 MATCH / 12 MATCH-INHERITED
/ 98 UNCHECKABLE / 7 EXEMPT). The gate passed: 'cs12: VARIANT TEX
OK (bad=0 first=(-1,-1) gotR=0)' -- pass 2's pipeline builds (format
injected per 4.33, set 3 compacted per this section) and both TEX
dispatches serve; zero group-3-non-push in the guest console;
dropReasons {}; renderErrors []. BUF/BUF2 still fail (bad=64) at
the meta-bind wall, so cs12 RESULT is 1; RESULT 0 additionally
needs the meta-bind delivery (lane-metabind). Remaining known
walls after that: sampled-texture serving for the formats Dawn
storage rejects outright (lane-sampledtex), then page-tier PSB
serving (spirv-capability-unknown:5347 -- root12 srv/uav).

## 4.35 Driver-init flake instrumentation: BW64_DRVINIT step trace (2026-10-28)

§4.18 left the Godot DXGI driver-init flake as an open unknown with a
named next step: "emulator-loader instrumentation on the winex11.drv.so
init path plus repetition statistics on a quiet box." §4.27 added a
second sighting family (app processes losing the desktop-init race on
stretched boots). This section lands the instrumentation half: the
init path now has a targeted, opt-in step trace that localizes a
divergence to the exact step, so the next sighting is diagnosable
instead of anecdotal. No product behavior changes.

### The driver-init sequence, mapped

Page tier and emulator steps are ours; guest steps happen inside the
prebuilt Wine payload (no Wine source on the box) and are observable
only through emulator-visible effects (process starts, file opens, X
connects, bridge traps).

1. **Page**: runtime loads, emulator worker starts, prefix/rootfs
   staged from the runtime assets.
2. **Page**: `installVkPageTier` → `createVkRenderer2`:
   `navigator.gpu.requestAdapter()` → adapter info →
   `requestDevice()` → the `bwVk*` capability flags are published.
   TIMING-SENSITIVE: guests query these flags at device creation, so
   they must exist before the guest's `vkCreateDevice` (the recurring
   capability-publication defect class).
3. **Emulator**: session boot — `wineserver -f -p` starts as the
   persistent foreground process, then services/explorer; every start
   is a `KProcess::startProcess`.
4. **Guest** (explorer): win32u loads winex11.drv; its unix side
   X-connects (XWire accept + handshake) and creates the desktop
   window (the first CreateWindow). TIMING-SENSITIVE: desktop creation
   stretches to tens of seconds under emulation — the §4.27 race.
5. **Guest** (app): dxgi.dll → winevulkan.dll → `init_vulkan`: the
   graphics driver must be registered as the user driver, and
   winex11's unix side dlopens `libvulkan.so.1` — `LD_LIBRARY_PATH`
   decides WHICH file wins (in probes, the staged libvk64 shim .so).
   **This is the §4.18 failure point**: `init_vulkan` prints "Failed
   to load Wine graphics driver supporting Vulkan" and the app exits
   BEFORE its own X connect and BEFORE any bridge trap.
6. **Guest** (app): window creation — the app's own X connect +
   handshake + CreateWindow.
7. **Guest** (vkd3d-proton device creation): `vkCreateInstance`
   through the PE shim — the bridge's witness trap proves the shim's
   libvulkan was the one dlopened — then
   `vkEnumeratePhysicalDevices`, `vkCreateDevice`.
8. **Guest/page**: swapchain — `vkCreateWin32SurfaceKHR` (§4.19),
   first `vkQueueSubmit`/`vkQueuePresentKHR`, first frame handed to
   the page tier.

What the existing logs captured vs missed: X accepts, handshakes,
CreateWindow, `vkCreateInstance ok`, DLL opens under BW64_DLLTRACE,
and process exits were all logged — but on different channels with no
common clock, and three load-bearing steps were missing entirely:
process STARTS (boot order/timing), the `libvulkan.so` dlopen itself
(DLLTRACE's patterns do not match `.so` paths — the shim selection was
invisible), and the page tier's adapter/device timing relative to the
guest boot.

### The trace

`BW64_DRVINIT=1` enables it. The browser launcher seeds the variable
from `?drvinit=1` (`project/emscripten/wine64-launcher.js`, mirroring
`?vktrace=1`); the page can also force it via the
`bw64_drvinit_enable()` ccall — the same dual gate as BW64_VKTRACE,
whose getenv is unreachable from some pthread workers. Each step
emits ONE line on the klog channel:

```
drvinit: t=<ms> step=<name> <detail>
```

`t=` is milliseconds since the first step in the process
(steady_clock; the first call is the first process start, i.e. boot),
so process, X-server and bridge steps share one relative clock. A
failing run diffs against a healthy one step by step: the first step
present in the healthy trace but missing or late in the failing trace
is the divergence.

Steps: `proc-start` (pid, name, cmd — every process, so boot order is
explicit), `proc-exit` (pid, exe, status), `dll-open` (pid, full
path, OK/FAIL+rc — the driver-module set only: winex11.drv,
winex11.so, libvulkan.so*, winevulkan.dll, vulkan-1.dll,
bw64vulkan.dll, dxgi.dll, d3d12.dll, d3d12core.dll), `x11-accept`
(running count — the §4.18 signature is 2 accepts vs 5),
`x11-handshake` (pid), `x11-first-window` (first CreateWindow
overall = desktop up), `vk-witness`, and the first-occurrence bridge
milestones `vk-instance`, `vk-enum-devices`, `vk-device`,
`vk-surface`, `vk-submit`. Page tier: `adapter` (vendor/architecture/
description) and `device` (the three published `bwVk*` values) from
both renderers in `web/vkwebgpu.mjs`, gated on the same `?drvinit=1`
param, on the same log channel with the same step names.

Implementation: `include/drvinittrace.h` — header-only inline helpers
(cached getenv gate, steady-clock, first-occurrence flags), chosen so
no new translation unit or link symbol appears in any build (the gate
script's hand-maintained source list included). Call sites:
`source/kernel/kprocess.cpp`, `source/kernel/syscall64.cpp` (exit +
openat), `source/x11wire/xwireserver.cpp`,
`source/x11wire/xwireconnection.cpp`, `source/vulkan/vk64bridge.cpp`
(milestones + the enable ccall), `web/vkwebgpu.mjs`,
`project/emscripten/wine64-launcher.js`. Every edit is additive; when
the variable is unset each site costs one cached-flag check — zero
lines, zero behavior change.

### Exercise: repeated tri12 boots with the trace on

Three consecutive tri12 boots (`web/tests/d3d12-pixels.mjs`,
`BW_TEST_URL_PARAMS="drvinit=1"`, slot-serialized, box carrying the
other lanes' probe load throughout) plus one control boot without
the parameter, and one A/B boot of the identical probe on the
unmodified integ build. Artifacts: `test-results/dxvk-drvinit{1,2,3,
ctl}/console.log` in this lane's tree; the integ A/B verdict matches
the compute merge gate's independent finding (below).

Healthy boots (runs 1-3): guest RESULT 0, exit status 0, and the
FULL step sequence on one clock — runs 1 and 2 produced identical
55-step sequences; run 3 produced 56, the delta confined to which
boot-helper `proc-exit` lines fell inside the capture window; the
load-bearing sequence was identical in all three. Run 1's trace,
condensed (t = ms since boot):

```
t=0      proc-start pid=10 vkboot cmd        t=48    proc-start wineserver
t=271    proc-start deskpin                  ... boot helpers exit (stretched:
                                             last at t=51282 under load)
t=51384  dll-open winex11.drv (x2 paths)     t=51446 dll-open winex11.so pid=38
t=51707  x11-accept open=1                   t=51711 x11-handshake pid=38
t=53484  x11-first-window pid=38 (desktop)   ... accepts to open=5
t=61883  proc-start pid=48 tri12.exe
t=76930  dll-open d3d12.dll                  t=76969 dll-open d3d12core.dll
t=76990  dll-open dxgi.dll                   t=78520 dll-open winevulkan.dll
t=78699  vk-instance                         t=78744 vk-enum-devices
t=78950  dll-open winex11.so pid=48          t=79196 x11-accept open=6 (the app)
t=80137  dll-open bw64vulkan.dll (+ FAIL probes for the .so search paths, rc=-2)
t=81465  vk-device                           t=83601 dll-open libvulkan.so.1 pid=48
t=83688  vk-witness                          t=83849 vk-submit (vkQueueSubmit2)
t=84044  proc-exit pid=48 status=0
```

Two observations the trace makes visible for the first time: the
boot stretch is quantified (desktop window at t=53.5 s, app start at
t=61.9 s, device at t=81.5 s under load — the §4.27 race conditions,
measured), and the loader's failed search probes
(`bw64vulkan.dll.so` rc=-2) are now distinguishable from real load
failures. `vk-surface` does not appear: tri12 in this probe mode
renders offscreen and reads back, creating no Win32 surface — the
step fires on swapchain paths (uav12/root12 present modes).

Control boot (no `drvinit=1`): ZERO drvinit lines, guest RESULT 0 —
the instrumentation is fully silent when off, as designed.

Verdict artifact, resolved: the probe harness's composite
`passed:false` appears on ALL FIVE boots (3 traced, control, and the
unmodified integ A/B) alongside guest RESULT 0, because this probe
mode presents no frames for its `vk64: FRAME built` conjunct. The
compute merge gate reached the same conclusion independently
("harness composite 'passed:false' is a rig capture artifact,
present at baseline too"). Guest RESULT + exit status + the step
trace are the health signals here.

Page-tier steps: the probe harness captures the emulator/guest log
channel only — page console lines (any vkwebgpu logging, not just
drvinit) never appear in its console.log. The `adapter`/`device`
steps are pinned by the guard and fire in the page console wherever
it is visible (live site / demo harness with `?drvinit=1`).

### Suites and guards

- Guard `web/tests/suite/unit/drvinit-trace.test.mjs` (6 tests):
  pins the header gate and prefix, the launcher seeding, every C++
  call site, and the page-tier steps + gate. Sabotage-verified:
  renaming the proc-start step and breaking the launcher condition
  each turn exactly one test red.
- JS suite: **313 passed, 0 failed** (`BW_TEST_DIST` = this
  tree's dist) — the 312 baseline at 5be0b7225 plus the new guard
  file; `no-silent-failure` and `vk-register-citations` green after
  the fixes described in this section.
- Native: 645 OK, 0 FAILED, single-threaded (`Build/Test/boxedwine
  0 0 1`, 221 s) — the baseline at 5be0b7225 exactly.
- Register citations: the insertions drifted 62 citations in
  vk64bridge.cpp / vkwebgpu.mjs; re-pinned (39 via the audit's --fix,
  the rest by diff-shift and comma-list hand re-pins). Final audit:
  224 MATCH / 0 DRIFT / 0 MISSING (UNCHECKABLE 104, EXEMPT 7; the
  split differs from a pristine checkout's 221/98 because staged
  payload files present in this tree make more citations checkable).
- `no-silent-failure` fall-out, fixed at the root: the milestone
  dispatch's `default: break;` counted as an unlogged default
  (vk64bridge.cpp 20 → 21). The clause was removed (the switch is on
  a U64 fn id, not an enum) rather than raising the reviewed
  baseline.

### Flake status

NOT reproduced: 3 traced boots + 1 control + 1 integ A/B, all
guest RESULT 0 with complete step sequences, on a loaded box (the
conditions §4.27 showed stretching boots to ~10x). The flake stays
OPEN as an unknown, but it is now INSTRUMENTED: the next sighting
with `drvinit=1` localizes to the first missing or late step by
construction — §4.18's signature (2 X accepts vs 5, no bridge trap)
would present as a trace ending before `vk-instance` with the
`dll-open`/`x11-accept` gap visible, separating the loader-search
failure from the driver-registration failure from the desktop race,
which the old logs could not tell apart.

Next step for the flake itself: any future init failure run with
`?drvinit=1` (demo-matrix and probe URLs accept it via
BW_TEST_URL_PARAMS) localizes to the first missing/late step;
repetition statistics on a quiet box remain the other half of
§4.18's named follow-up.


## 4.36 fmt_bpp integer formats: the harness repair's one genuine product finding, fixed (2026-10-28, fmtbpp lane)

**The finding (inherited, pinned before it was fixed).** The vk64 harness
repair lane (lane-vk64harness) completed the native harness's format-bpp
spec table to all 48 non-BC formats `v2FormatIsSupportedImage()` accepts
and found 17 missing from it. Four were pure test rot (fmt_bpp already
served them). The other 13 were a genuine product gap: `fmt_bpp`
(`source/vulkan/vk64bridge.cpp`) had no case for R8G8_SNORM/UINT/SINT
(2 B), R16_UINT/SINT (2 B), R16G16_UINT/SINT (4 B),
R16G16B16A16_UINT/SINT (8 B), R32G32_UINT/SINT (8 B) or
R32G32B32A32_UINT/SINT (16 B). Every call landed in the loud default --
returned 4 (the true size only for the R16G16 pair) and counted
`format-bpp` -- breaking the invariant stated in that default case
("every format v2FormatIsSupportedImage() accepts is listed above") and
mis-sizing everything fmt_bpp feeds: image memory requirements
(imageDataBytes via fmt_block), vkGetImageSubresourceLayout row pitches,
swapchain/image pixBytes, and the 32-bit copy/blit/resolve pitches --
4 B instead of 16 B per texel for the RGBA32 integer pair. The harness
pinned the fallback behaviour (return 4, one count per call) so the
other 35 formats kept full-strength spec assertions until the product
fix landed.

**The fix.** All 13 cases added to fmt_bpp with their Vulkan-spec sizes;
fmt_bpp now has an explicit case for every one of the 48 non-BC supported
formats, and the loud default remains for genuinely unsupported formats
(the harness still asserts an unknown format returns 4 and counts once).
The harness's pin set is removed: the 13 formats moved into the plain
spec assertion (return value = spec size), and the block now asserts the
`format-bpp` counter does not move across the whole supported set.

**Consumer check (no compensation existed).** Every fmt_bpp call site in
the bridge was read: vk64BufferViewRange's texel alignment flooring,
both ABIs' vkGetImageSubresourceLayout, image creation / swapchain
pixBytes, the imageDataBytes helpers behind the memory-requirements
handlers, and the 32-bit clear/copy/blit/resolve paths -- all use the
returned size purely multiplicatively; none branches on the fallback
value or special-cases these formats. The page tier is independent of
fmt_bpp and already had it right: `V2_TEXTURE_UPLOAD_BPP`
(web/vk-protocol.mjs) carries the correct size for every WebGPU
counterpart of the 13 (rg8uint 2, rgba32sint 16, ...). One unrelated
observation, left untouched: the 32-bit vkCmdClearColorImage writes its
4-byte clear pattern in 4-byte steps regardless of texel size -- it uses
fmt_bpp only for the total byte count, so it neither compensated for nor
depended on the fallback.

**Proof.** run-native.py runs end-to-end green (exit 0, all 31 PASS
blocks, including the format block at its new full strength). Sabotage:
removing the single R32G32B32A32_UINT case from fmt_bpp aborts the
harness at the spec assertion (`fmt_bpp(f) == it->second`); the bridge
file was restored byte-identical (sha256 verified) and the final run is
green. Register: one IMPLEMENTED row added (merged tree: 370 = 71/66/233/0);
citation audit 0 DRIFT / 0 MISSING (227 MATCH / 12 MATCH-INHERITED /
97 UNCHECKABLE / 7 EXEMPT). The emulator native suite does not cover the
bridge (project/linux filters vk64bridge.cpp out of TEST_SOURCES, and
the changed test file is compiled only by run-native.py), so the harness
is the native check for this change. JS suite: 314 passed / 0 failed on the lane's own dist (bw64-build.sh, "Browser export verified") -- the branch baseline exactly, including vulkan-image-format-size (which compiles fmt_bpp from source) and vk-register-citations.

## 4.37 x64 vkd3d restage: the staged pair now carries the §4.29 utof fix (2026-10-28, restage lane)

§4.29 fixed the UINT pipeline wall in the vendored dxbc-spirv source
(`cad7cdf81`: `lowerConvertItoF` no longer widens U32->U64), but the
pair staged in the shipped `wine64.zip` was built before that fix —
canonical `d3d12.dll` `d77fdad5…` / `d3d12core.dll` `d2bd847a…`
(md5 `006c754e…`). The gap was proven twice independently before this
lane: the compute merge gate's baseline matrix and the spirvwrite
lane's A/B both showed uav12's R32_UINT and R8_UINT still failing
`CreateGraphicsPipelineState` with E_INVALIDARG against the staged
pair, while a rebuilt pair reproduced the §4.29 end state exactly.
The fix existed only in source; the shipped bytes never carried it.

### The rebuild

Fresh from the vendored source at `954e72aa1` via
`tools/vkd3d/BUILD.md` (meson + the `PATHCCH_NONE` box workaround,
ninja 209/209). New pair:

- `d3d12.dll`: 517,717 B, sha256 `a96b4e9d53ed0bed…`
- `d3d12core.dll`: 8,825,383 B, sha256 `3d3bfe43cbec123f…`
  (md5 `a38c3769…`)

Sizes are identical to the previous pair; hashes differ because PE
builds are not byte-reproducible (linker TimeDateStamp + the git
describe embedded in the version header — the same caveat as the
32-bit pair's build). Both are PE32+ (machine 0x8664);
`d3d12core.dll` exports exactly `D3D12GetInterface` +
`D3D12SDKVersion`, and both export sets are identical to the
uintpso lane's known-good rebuild. Behavioral equivalence, not
byte identity, is the criterion — and it is what the probes below
establish. The build output stays in `tools/vkd3d/build.w64` of the
lane tree as the merge transfer source (the staged archive is
gitignored; the merge re-stages from these bytes on the canonical
tree and verifies them against the pins below before staging).

### Probe proof, before staging (app-dir rig, built pair uploaded)

`test-results/dxvk-uav12-restage-pre/`: the case map is the §4.29
end state, case for case —

- EXEC R32_UINT **OK (all 16 texels match)** — was
  `CreateGraphicsPipelineState hr=0x80070057` against the staged pair.
- EXEC R8_UINT FAIL `mismatches=32/64` — the PSO wall is gone; the
  draw reaches the page tier and stops at the named rejection
  (`storage-format-access:R8Uint:ReadOnly` in the run log), which is
  the sampled-texture serving queue, not a pipeline failure.
- EXEC R16_FLOAT FAIL `mismatches=48/64` (named
  `storage-format-access:R16Float:ReadOnly`, unchanged).
- EXEC R8G8B8A8_UNORM / R32G32B32A32_FLOAT / R16G16B16A16_FLOAT OK;
  `flag=TRUE sweep=ALL18`.

Sanity: tri12 RESULT 0 (`dxvk-tri12-restage-pre/`); root12 CBV OK
(all 16 px `[191 191 223 255]`), srv/uav PSO `E_FAIL` on the named
`spirv-capability-unknown:5347` ×2 — the queued PSB-serving wall,
unchanged (`dxvk-root12-restage-pre/`).

### Staging and re-pins

`tools/rootfs64/stage-vkd3d-payloads.sh` merged the new pair into
`web/runtime/wine64.zip` and `dist/runtime/wine64.zip` (shim members
unchanged). `VKD3D_PINS` in `check_runtime_completeness.py` now pins
the new pair; the artifact manifest regenerated with exactly the w64
line changed; `tools/browser/runtime-assets.json` re-pins
`web/runtime/wine64.zip` at **213,105,320 B, sha256 `70d752371a…`**
(measured on this tree — replaced members carry wall-clock DOS
timestamps, so a staged archive's whole-archive hash is re-measured
per staging and is not expected to match any other run's; the member
pins + size + completeness gate are the invariants, per the x86
merge's controlled experiment). Full completeness gate: **0
failures** (`vkd3d payloads` PASS against source + shipped zip,
`manifest matches shipped zips` PASS; the one skip is the
pre-existing environmental prefix64 one). `bootstrap.py --check`
passes (13 files). Pre-staging backups:
`/home/ubuntu/backups/winbox-vkd3d64-restage-20261028-135228/` (this
lane) and the staging script's own
`/home/ubuntu/backups/winbox-vkd3d-payloads-20261028-135229/`.

### Probe proof, after staging (system placement — the shipping path)

`d3d12-system.mjs` uploads nothing; the guest resolved the pair from
`wine64.zip` system placement exactly as the demo environment does.
uav12's case map is identical to the pre-staging map, case for case
(`dxvk-uav12-restage-sys/`), and tri12 returns RESULT 0
(`dxvk-tri12-restage-sys/`). The end state survives staging.

One ordering note for future restages: the dist must be exported
*after* staging, not staged into. A first verification pass here
staged into an already-built dist and `provenance-artifacts` failed
two subtests — the two archives are merged in separate runs (their
member timestamps differ, so dist ≠ web byte-wise) and the font
provenance record anchors to the pre-staging `wine64.zip` hash.
Rebuilding the dist from the staged set made `dist/runtime/wine64.zip`
a byte copy of the staged archive and the suite green; the canonical
flow (stage → export) exists precisely to keep that invariant.

### Suites (verified on this tree)

- JS suite: **314 passed, 0 failed** (`BW_TEST_DIST=<tree>/dist`,
  `--unit`) — baseline exact at `954e72aa1`.
- Native: **647 OK, 0 FAILED** in 249 s (`Build/Test/boxedwine 0 0 1`,
  single-threaded) — baseline exact.
- Dist: `Browser export verified`; support archive unchanged
  (`bbc583c6…`).

### Next wall after this one

Unchanged queue: R8_UINT / R16_FLOAT read-path serving via the
sampled-texture rewrite (§4.26), and root12 srv/uav via page-tier
PSB serving (the 5347 rejection). The remaining vkd3d staging debt
is the 32-bit guest verification of the §4.31-x86 pair (built and
staged; never yet run in a 32-bit guest).

MERGE OUTCOME (2026-10-28, restage merge lane): merged into
integ/winbox-20261022b as merge commit 847579870 (parents 8bf268112 +
f69fbcb3a); this section was renumbered 4.31 -> 4.37 at the merge. The
canonical re-stage was performed on the merged tree under the transfer
discipline above: the pair was COPIED from wt-restage's preserved build
output (tools/vkd3d/build.w64) after both files hashed exactly to the
new pins (d3d12.dll a96b4e9d53ed0bed..., d3d12core.dll 3d3bfe43cbec...)
and was never rebuilt. Staging on the merged tree measured
web/runtime/wine64.zip at 213,105,320 bytes / sha256
8bbd57c0c7e4db20751612796ff7da3c7bd31827c7332a2d8340412cf02a53ec -- a
per-staging measurement (its DOS timestamps differ from the lane
tree's 70d752371a... for the identical member set, as the
staged-archive invariant predicts; member pins + archive size + the
completeness gate are the invariants). Follow-up commit 3b19232fc
re-pinned runtime-assets.json's wine64 entry to the merged measurement
(one line) and regenerated the artifact manifest (exactly the w64 line
changed). Gates on the merged tree: completeness 0 failures (1
environmental skip); citation audit 0 DRIFT / 0 MISSING (227 MATCH /
12 MATCH-INHERITED / 97 UNCHECKABLE / 7 EXEMPT); register unchanged at
370 = 71/66/233/0; JS suite 317/0 on the final dist; native suite
647/0; dist export verified ("Browser export verified") with
dist/runtime/wine64.zip byte-identical to the staged web archive.
Shipping-path guest proof on the merged dist (system placement from
wine64.zip; tools/vkd3d/stage-vkd3d.sh probe staging into the dist
prefix; test-results/dxvk-restage-merge-uav12-sys4 and
dxvk-restage-merge-tri12-sys2): the uav12 case map matches the lane's
post-staging map CASE FOR CASE -- EXEC OK for R8G8B8A8_UNORM,
R32G32B32A32_FLOAT, R16G16B16A16_FLOAT and R32_UINT (all 16 texels;
the section-4.29 fix now serves from the shipping archive), R16_FLOAT
FAIL mismatches=48/64 and R8_UINT FAIL mismatches=32/64 at their named
storage-format-access:<fmt>:ReadOnly rejections (the sampled serving
of lane-sampledtex is not yet merged), zero 5347 rejections,
sweep=ALL18; tri12 RESULT 0 (device created through the system-placed
pair). Two earlier probe attempts in this merge were lost to box
incidents rather than the build -- one guest run died when the disk
hit 100% mid-boot (ENOSPC in the harness's result write) and one tri12
run hit the 900 s harness timeout at load ~10 with its boot stretched
past the window; the runs above are the evidence. One suite run
mid-merge showed provenance-artifacts failing on the probe-staged
dist prefix64.zip (the subtest pins the exported prefix to the pinned
archive plus deskpin staging only); the final dist was rebuilt from
the committed tree without probe staging and the suite is 317/0 on
it, as recorded above.
## 4.38 Sampled-texture serving: read-only storage reads in r16float and r8uint (2026-10-28)

§4.26 closed the R16_FLOAT wall fail-closed and named the serving path:
read-path storage in formats Dawn rejects must be rewritten to sampled
reads. This lane (lane-sampledtex, off integ 1975bb5fa) builds that
path, for exactly the two probe-proven formats: r16float (Dawn rejects
read-only AND read-write storage, §4.26 micro-probe) and r8uint
(read-only rejection reproduced at the §4.29 end state).

### The shape, pinned layer by layer

- Guest side: a read-only UAV read (NonWritable) becomes, through
  vkd3d-proton + spirvfix (R14 injects the bound view's format into
  the storage image type; R15a specializes heap-array clones), a naga
  WGSL declaration `var NAME: texture_storage_2d<r16float,read>;`
  whose only uses are two-argument `textureLoad(NAME, coords)` and
  `textureDimensions(NAME)` — verified offline against the real
  captured module `testdata/spirv/d3d12-ps-uav-heap-array.spv` under
  the r16float signature (the clone `bw_arr_g5_b0_p0_a0_` at set 5
  binding 0). The r8uint shape is identical modulo `vec4<u32>`
  results; that fixture cannot prove it offline (its image sampled
  type is float, so R14 correctly refuses the kind-mismatched uint
  injection) — the guest runs below are the r8uint evidence.
- Page tier, before this lane: `layoutEntryFor` turned that
  declaration into a `storageTexture` bind-group-layout entry and
  Dawn rejected it at `createBindGroupLayout` — §4.26's named
  `storage-format-access` rejection.
- Why a sampled binding is the same read: the image already exists in
  the page tier as a GPUTexture that `gpuTextureFor` always creates
  with TEXTURE_BINDING usage. The sampled bind path resolves the same
  view of the same image with the same subresource range
  (`viewRangeDescriptor` is shared by both paths), and a storage read
  addresses the view's base mip — exactly what a sampled
  `textureLoad(..., 0)` reads. Component types agree: r16float
  sampled loads return vec4<f32> (half values are exactly
  representable in f32); r8uint loads return vec4<u32>. Layouts,
  fingerprints and bind groups all derive from the scanned
  declarations, so a rewritten declaration flows through the existing
  sampled machinery unchanged.

### The implementation

`rewriteReadOnlyStorageToSampled` (web/wgsl-utils.mjs), a pure
WGSL→WGSL rewrite in the style of the multisample/border rewrites,
applied by a `serveSampledStorage` helper in BOTH pipeline builders
(graphics + compute) immediately after the push-constant rewrite and
before declarations are scanned. For each storage declaration whose
format is in the served set (exactly `{r16float: f32, r8uint: u32}`),
access is exactly `read`, and dimension is exactly
`texture_storage_2d`, it proves every use of the variable:
two-argument `textureLoad` (the load gains the explicit level `, 0i`)
or `textureDimensions(var)`. The declaration is then retyped to
`texture_2d<f32>` / `texture_2d<u32>`. Servings are counted
(`stats.sampledStorageServed`) and logged per pipeline build. No
bridge change (TEXTURE_BINDING was already always granted — the
"bridge TEXTURE_BINDING usage" §4.26 anticipated was already true),
and no spirvfix change: the rewrite consumes post-spirvfix WGSL, so
it composes with lane-spirvwrite's R15a writability work without
touching it.

Fail-closed per variable, with named decline reasons in the guard:
any other use (a store, an atomic, the value passed to a call, a
load of another arity, dimensions with an argument), a
`read_write`/`write` access, a non-2d dimension, a format outside
the served set, or the storage type string appearing anywhere
besides declarations (e.g. a function parameter) leaves that
variable a storage texture, byte-identical — and the pipeline keeps
§4.26's named `storage-format-access:FORMAT:ACCESS` rejection for
it. A module with nothing served is returned byte-identical.

### The second layer: texture usage (found by the first guest run)

The first serving run built the pipeline and executed the draw —
and R16_FLOAT read back 64/64 zeros. Dawn's log named the stacked
wall underneath: `gpuTextureFor` adds STORAGE_BINDING usage for
every `V2_STORAGE_FORMAT` image, and Dawn will not create a texture
with that usage for these formats at all ("The texture usage
includes TextureUsage::StorageBinding, which is incompatible with
the format (TextureFormat::R16Float)"), so the texture, its view
and the bind group were all invalid and the frame encoding was
rejected. §4.26's run never reached texture creation (the layout
died first) — a textbook blocker stack. Fix: `gpuTextureFor`
withholds STORAGE_BINDING for `V2_STORAGE_USAGE_FORBIDDEN`
(web/vk-protocol.mjs), a set that admits a format only on Dawn's
own message as evidence: 76 (R16_SFLOAT) from the first run, and 13
(R8_UINT) from the rebuilt-pair run below, which produced the
identical message naming R8Uint (its sampled serving fired and the
case still read zeros through the invalid texture). A storage bind
of these formats was already unreachable (the layout entry is
rejected by name), so withholding the usage removes no working
path; every other format keeps the usage it has today.

### Guards

New `vk-sampled-storage-serving.test.mjs` (10 tests): the served
set pinned to exactly the two formats; both formats' rewrites;
multiple loads incl. a dimensions call nested in load coordinates;
nine decline shapes with reasons, each byte-identical; a mixed
module (served variable rewritten, unprovable variable untouched);
identity for storage-free modules; the real captured uav12 module
served end to end (spirvfix → naga → rewrite → naga WGSL
validation) while the Dawn-served rgba8unorm signature of the same
module stays on the storage path; source pins for the wiring in
both builders; and the usage-exclusion pin (the forbidden set is
exactly {76, 13} and every member is a sampled-served format).
Sabotage-verified four ways: dropping r8uint from the served set
turns 3 tests red; sanctioning textureStore uses turns the decline
test red; removing the compute builder's serving call turns the
wiring test red; removing the usage condition turns the exclusion
test red (all restored and re-verified green).
`vk-storage-format-access.test.mjs` keeps every assertion — the
rejection path is untouched — with its header updated to the new
disposition.

### Suites

- JS suite: 315 passed, 0 failed (files; BW_TEST_DIST=<tree>/dist, --unit; baseline 314 at 1975bb5fa, +1 new guard file). Native: 647 OK,
  0 FAILED, exit 0 (`Build/Test/boxedwine 0 0 1`, 229s; the diff
  touches no C++, so this is a baseline confirmation).
- Register citations: 0 DRIFT / 0 MISSING (223 MATCH / 13
  MATCH-INHERITED / 97 UNCHECKABLE / 7 EXEMPT over 340 cited rows).
  The insertions drifted 25 citations (24 in vkwebgpu.mjs, 1 in
  vk-protocol.mjs); 20 were re-pinned by the audit's --fix, 5
  adjudicated by hand to their true sites, and 7 dependents were
  followed to their primaries' new sites (the §4.26 repair).
  Recorded honestly, as in §4.26: the distribution moved +2 MATCH /
  −2 UNCHECKABLE against the 1975bb5fa baseline (220/13/99/7)
  because two 'catch'-literal rows became mechanically checkable
  when the code shifted; no test was touched to produce this.
- Register: 367 rows (68 IMPLEMENTED / 66 REJECTED-LOUDLY / 233
  NOT-A-GAP / 0 OPEN) — one new IMPLEMENTED row for the sampled
  serving; the §4.26 storage-format-access row is narrowed to the
  shapes that remain rejected.

### Guest proof (uav12, slot-serialized, rebuilt dist each time)

- Run A (staged pair, d3d12core sha d2bd847a…, pre-utof): serving
  fires for r16float; R16_FLOAT reads 64/64 zeros — the texture
  usage wall above (evidence: test-results/dxvk-20261028-stx-stagedA).
- Run A2 (staged pair, usage fix): EXEC R16_FLOAT OK (all 16
  texels); R8G8B8A8_UNORM / R32G32B32A32_FLOAT /
  R16G16B16A16_FLOAT OK as before; R32_UINT and R8_UINT still fail
  CreateGraphicsPipelineState 0x80070057 — the staged pair's known
  pre-utof wall, not this lane's (evidence:
  test-results/dxvk-20261028-stx-stagedA2).
- Run B (rebuilt pair from wt-uintpso, d3d12core md5 e1319ca9):
  R32_UINT EXEC OK; R8_UINT's serving fires but reads 64/64 zeros
  with Dawn's message naming R8Uint storage usage — the evidence
  that admitted 13 to the exclusion set (evidence:
  test-results/dxvk-20261028-stx-uintpsoB).
- Run C (rebuilt pair, both fixes): all six EXEC cases OK,
  `SUMMARY exec=ALL6`, `uav12: RESULT 0`; 4 serving lines (both
  formats). Evidence: test-results/dxvk-20261028-stx-uintpsoC.

### Process notes (kept because they cost real cycles)

- A fresh worktree's dist export fails in stages on staging this
  lane had to copy from the integ tree (DXVK build.w32/build.w64,
  rootfs64 gecko/cjk-font/openal caches, gstreamer32/64 payloads);
  `bw64-build.sh` prints BUILD COMPLETE even when the export
  crashes — "Browser export verified" from verify-dist.mjs is the
  only trustworthy signal, and two completion notices delivered to
  this lane belonged to other lanes' runs. Every claim above rests
  on artifacts read back from this tree's own test-results dirs.
- The staged-pair R8_UINT/R32_UINT PSO wall is closed on
  lane-restage's branch (rebuilt pair restaged); on the merged tree
  the Run C end state is the expected one once that merge lands.

### Next wall after this one

- None inside this lane's scope: with a current vkd3d pair, every
  uav12 EXEC case passes. The unserved remainder of the
  storage-format-access row is the fail-closed residue by design
  (writes/atomics/non-2d/other formats in Dawn-unservable storage
  shapes keep the named rejection). Adjacent standing items are
  unchanged: cs12 BUF/BUF2 await lane-metabind; the
  rendered-then-B2I'd stale page data (§4.24) and the operand-aware
  array scan remain queued.

### Merge outcome (integ, 2026-10-28)

Merged into integ/winbox-20261022b as 8f077cdd8 (this section
renumbered 4.34 -> 4.38), with one merge-fallout repair commit
4afe7ac57. Conflicts were confined to the two docs and
web/vkwebgpu.mjs (import union; the serveSampledStorage helper
inserted ahead of HEAD's getPipelineFor, whose section-4.33
pushBlock parameter is retained). Both sides verified present in
the merged files: serveSampledStorage is applied to vsR/fsR in the
graphics builder and to csR in the section-4.30 compute builder,
each time after rewritePushConstantsWGSL and before declarations
are scanned; the gpuTextureFor usage guard and the
V2_STORAGE_USAGE_FORBIDDEN set {76, 13} landed intact, alongside
the merged tree's resolveStorageVarFormat / planGroupRemap /
remapDeclGroups machinery. Gates on the merged tree: JS suite 317
passed / 1 failed on the first run -- the one failure was
vk-register-citations' coverage ceiling (UNCHECKABLE had grown
97 -> 105 against the pinned 102), a merge fallout characterized
and repaired in 4afe7ac57 (duplicate-scan dependents re-pinned to
follow their primaries; the two catch-literal rows and the
replay-skip-flag row re-pinned to their merged-tree sites; no row
text or disposition changed). Citation audit after the repair:
228 MATCH / 14 MATCH-INHERITED / 95 UNCHECKABLE / 7 EXEMPT over
344 cited rows, 0 DRIFT / 0 MISSING, and the citations guard
passes 4/4. Register: 371 rows = 72 IMPLEMENTED / 66
REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN (pipe-aware recount;
delta vs the pre-merge tree is exactly the one new IMPLEMENTED
row plus the narrowing of the storage-format-access row above).
Native suite 647/0 single-threaded; run-native.py end-to-end
green (31 PASS blocks); dist export verified ("Browser export
verified", stamp 8f077cdd8).

Guest gate on the merged dist (shipping path, d3d12-system.mjs):
NOT YET CONFIRMED -- two attempts were defeated by box
conditions, not by the build. Attempt 1 (TIMEOUT 900 s,
test-results/dxvk-sampledtex-merge-uav12): the guest completed
its Wine boot but produced no RESULT inside 15 minutes at box
load ~9-12, with a second heavy probe and a three-build convoy
sharing the machine; harness verdict is the waitForFunction
timeout. Attempt 2 (TIMEOUT 1800 s,
test-results/dxvk-sampledtex-merge-uav12b): the session lost
the explorer desktop-init race under the stretched boot
(winediag nodrv_CreateWindow, "The explorer process failed to
start" -- the section-4.27 family); uav12.exe never launched
(zero mentions in the console log) and the harness timed out at
30 minutes. Neither attempt exercised the merged serving code
in the guest, so the all-six EXEC case map on the merged tree
stands on this lane's branch proof above (Run C, rebuilt pair,
SUMMARY exec=ALL6, RESULT 0) plus section 4.37's shipping-path
proof that the archive the merge ships serves the other four
EXEC cases and held R16_FLOAT / R8_UINT at exactly the named
rejections this merge removes. Re-run the gate in a quieter
window before treating the merged-tree case map as proven.
Closing note: the dist was rebuilt at the final tip ade389f37
("Browser export verified") and the full JS suite re-run against
it: 318 passed / 0 failed (the 317-file baseline plus this
lane's vk-sampled-storage-serving guard file), superseding the
first run's 317/1 above. The guest gate remains as recorded:
outstanding, to be re-run in a quieter window.

Gate closure (psb merge, 2026-10-28): CONFIRMED. The merged
tree of section 4.39 (which contains this section's serving
code unchanged) ran uav12 in the guest via the uploading
harness with the archive's own pair (the app-dir rig's
default d3d12 pair is byte-identical to the staged canonical
pair, sha a96b4e9d / 3d3bfe43, verified against the
completeness pins): SUMMARY flag=TRUE sweep=ALL18
exec=ALL6, RESULT 0 -- EXEC OK for R8G8B8A8_UNORM,
R32G32B32A32_FLOAT, R16G16B16A16_FLOAT, R16_FLOAT, R32_UINT
and R8_UINT, all 16 texels each (evidence
test-results/dxvk-psbmerge-uav12c). Two earlier attempts in
the same session died at the harness session-ready timeout
under box load 10-15 with build convoys on the probe slot;
the control runs bracketing them (root12 and tri12, same
rig, same dist) completed normally, and the passing run
followed within the same hour as load fell -- the timeouts
were box conditions, as this section's own attempts were.

## 4.39 Root SRV/UAV serving: writable root descriptors execute end to end, with readback (2026-10-28, lane-psb)

**The pinned gap (real modules, not fixtures).** With root12's probe debt
fixed (section 4.29: genuine d3dcompiler_47.dll), its SRV and UAV cases
compile and reach the bridge as physical-storage-buffer shader code, and
both died at pipeline creation on the named rejection
`spirv-capability-unknown:5347` (PhysicalStorageBufferAddresses). The
modules were captured in-guest (VKD3D_SHADER_DUMP_PATH; fixtures
`testdata/spirv/d3d12-ps-root-srv.spv` / `d3d12-ps-root-uav.spv`):
the SRV pixel shader rebuilds the root VA from the push block with the
carry idiom (CompositeExtract both halves of a v2uint push load,
OpIAddCarry -- opcode 149 -- for the low half, CompositeConstruct,
OpBitcast) and performs ONE merged OpLoad through an
OpInBoundsAccessChain; the UAV pixel shader OpBitcasts the push-loaded
v2uint directly, chains with a RUNTIME index into a runtime array of
uint, and OpStores through it (memory operands Aligned|NonPrivatePointer).
Both arrive as ROOT descriptors (the VA is push-constant data), not heap
descriptors, so the serving vehicle is the R15b root-descriptor rewrite
(section 4.21) -- which admitted neither shape: its provenance classifier
could not see through the carry idiom (rejection: "pointer address does
not trace to a single push-block load"), and its writable policy rejected
any slot stored through (rejection: "root slot is written through its
pointer (no GPU->guest buffer writeback)") -- a premise section 4.30's
BUFFER_READBACK machinery had already removed for compute.

**The blocker stack, as it actually unfolded.** (1) Admission: extend the
provenance classifiers (spirvfix `r15ClassifyVec2Addr`, bridge mirror
`classifyRootAddr` at `vk64bridge.cpp:942`) to the carry idiom -- both
halves extracted from ONE push load of a v2uint, per-half constant
add/sub, at most one OpIAddCarry whose sum feeds the low half and whose
carry the high half consumes; any nonzero 64-bit addend stays rejected.
The writable policy inverts: a slot stored through becomes servable and
WRITABLE (no NonWritable decoration; the OpStore's Aligned operand is
stripped on re-rooting, as loads already were); a slot written through
an ATOMIC operation stays rejected by name ("atomics not served").
(2) Translation: with the module admitted and rewritten, naga 30.2.0
rejected the UAV function wholesale ("Function [0] 'main' is invalid")
over three dead OpUndef %uint dxbc-spirv emits for never-read DXIL temp
registers -- bisected on the real module against the vendored naga.
New spirvfix R16 `strip-dead-undefs` (`spirvfix.mjs:3219`) removes a
function-local OpUndef only when its id has no consumer word anywhere
in the module (fail-closed: a literal word equal to the id keeps it).
(3) Pipeline creation: Dawn refused the pipeline -- "Read-write storage
buffer binding is used with a visibility (Vertex|Fragment) that contains
Vertex". The tier merged both stages' declarations and gave every
layout entry VERTEX|FRAGMENT; bind-group-layout visibility now follows
the stages that actually declare the binding (`stageVis` on the merged
declarations, `layoutEntryFor` at `web/vkwebgpu.mjs:1705`), which is
also the honest WebGPU model for every other resource kind. A read-write
storage binding genuinely declared by a vertex shader remains
unservable and fails with Dawn's own named message.
(4) Readback correctness, two layers. The bridge marks writable root
ranges at draw record time (`vk64MarkDrawRootWrites`,
`vk64bridge.cpp:4238`, replaying the recorded pipeline + merged push
block exactly as emitRootDescs resolves them) -- but the mark's
cleanliness epoch must be measured at the draw's EXECUTION point:
vkd3d lands a resource's initial data as a vkCmdUpdateBuffer executed
earlier in the same submission's serializer walk, so a record-time
epoch snapshot counts the guest's own upload as post-draw dirt.
emitRootDescs therefore re-stamps each writable slot's mark with the
memory's current writeEpoch at submit (`vk64bridge.cpp:4696`); the
record-time mark is kept because a copy recorded later in the SAME
command buffer must still find it. And the page tier created every
storage GPUBuffer with usage CopyDst|Storage, so the deferred
V2_BUFFER_READBACK's copyBufferToBuffer failed Dawn validation
("usage (CopyDst|Storage) doesn't include CopySrc") and the bridge
timed the readback out; storage buffers now also carry COPY_SRC at both
usage computations (bind-group key builder and entry builder, which
must agree). The same COPY_SRC gap latent-affected compute readbacks
whose source buffer was created through the same path.

**Guest proof (slot-serialized, genuine d3dcompiler_47.dll).** BEFORE:
root12 cbv OK, srv + uav FAIL CreateGraphicsPipelineState 0x80004005 on
spirv-capability-unknown:5347. AFTER (evidence
test-results/dxvk-psbroot9, dist wasm a1bd43ea): root12 RESULT 0 --
cbv OK (RT 16/16 [191 191 223 255]), srv OK (RT 16/16 [255 128 64 255]),
uav OK (RT 16/16 [64 128 191 255] AND all 16 stored words match
0xA5000000+i, read back through the deferred V2_BUFFER_READBACK path).
Tier stats on the passing run: 4 pipelines translated, 0 rejected,
dropReasons {}.

**Guards.** spirvfix-r15.test.mjs extended to 22 (carry-idiom census +
rewrite, writable census + rewrite with NonWritable absent and Aligned
stripped, nonzero-vec2-addend and atomic fail-closed mutants, bridge
mirror pins); spirvfix-r16.test.mjs x5 (the real module's three dead
undefs, strip + composition with the R15b rewrite, no-fire on CBV/SRV,
used-undef kept); vk-psb-serving.test.mjs x3 (stageVis pins, COPY_SRC at
both sites, bridge marking + re-stamp pins); the REWRITES coverage list,
the vk-pipeline-rejection and vk-silent-drop-guard pins for the
layoutEntryFor signature / shared-binding comparison (visibility now
forced in the comparison, so a stage-usage difference cannot masquerade
as a kind incompatibility). Sabotage-verified: disconnecting the vec2
classifier fails exactly the 3 SRV tests; removing the atomic gate fails
exactly 1; always emitting NonWritable fails exactly 2; removing the
store Aligned strip fails exactly 1; neutering stripDeadUndefs fails
the R16 strip tests; reverting the stageVis default fails the tier pin.

**Residue / next walls (named, by design).** Atomic RMW through a root
pointer rejects loudly (atomics not served). Root address materialization
with a nonzero constant addend (scalar or vec2) rejects loudly -- folding
a proven constant into the re-rooted chains is the future fix if a real
module needs it. An OpUndef WITH consumers stays in the module (naga
rejects it at translation, loudly) -- substituting zero for a used undef
is a semantics decision no real module has forced yet. The compute
sibling of the epoch lesson is noted, not fixed here: vk64MarkComputeWrites
also snapshots at record time, so a same-list upload-then-dispatch
pattern would fail its cleanliness proof the way draws did; cs12's
proven shapes upload in earlier submissions and are unaffected.
32-bit root descriptors ride the same bridge mirror (the classifier is
ABI-shared) but have no guest proof yet -- that is lane-d3d12-32's
territory.

**Process findings worth keeping.** vkTrace() is not evidence after the
fact: its free budget is 256 trap prints and BW64_VKTRACE latches on the
first calling thread (getenv is unreliable on guest worker threads), so
trace-gated diagnostics placed past bring-up print nothing -- this
lane's first diagnostic round was invisible for exactly that reason and
the mechanism it watched was blamed unfairly for one cycle. And a
sabotage restore via `git checkout --` on UNCOMMITTED files reverts to
the base commit, not to the pre-sabotage state; the lane's files were
recovered byte-identical from the dist export (hash-verified), but the
rule stands: back up to files, not to git, when the work is uncommitted.

Merge addendum (integ merge of lane-psb, 2026-10-28): merged
as c2735d008 on top of fdcb3a79d (the section 4.38 tip).
Conflicts were confined to the two documents (the campaign
tail and the register header/rows); all code auto-merged,
and the merged regions were diffed against both parents --
the mirror pair both carry the carry-idiom extension
(spirvfix r15ClassifyVec2Addr at tools/spirvfix/spirvfix.mjs
and the bridge vec2Half / classifyRootAddr in
source/vulkan/vk64bridge.cpp), R16 strip-dead-undefs is
present, and the page tier carries stageVis visibility plus
storage COPY_SRC. Register: 372 rows = 73 IMPLEMENTED / 66
REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN (the lane's one new
IMPLEMENTED row; no disposition changed). Citations: 0
DRIFT / 0 MISSING (228 MATCH / 14 MATCH-INHERITED / 96
UNCHECKABLE / 7 EXEMPT) after re-pinning -- 48 citations
auto-fixed by the audit, 24 hand-pinned through the merge's
line map, 9 duplicate-scan dependents restored to their
primaries' sites, and the new row's citations pinned to the
merged constructs. Suites on the merged tree: JS 320 passed
/ 0 failed (318-file baseline plus spirvfix-r16 and
vk-psb-serving); native 647 / 0 single-threaded;
run-native.py end-to-end green (31 PASS blocks). Dist
rebuilt: "Browser export verified", stamp c2735d008, wasm
sha d7605626, support archive bbc583c6.
Guest gate on the merged dist (uploading harness,
test-results/dxvk-psbmerge-*): root12 RESULT 0 -- SUMMARY
cbv=OK srv=OK uav=OK, the UAV render target and all 16
stored words correct through the deferred
V2_BUFFER_READBACK (run dxvk-psbmerge-root12); uav12
RESULT 0 with all six EXEC cases OK (run dxvk-psbmerge-
uav12c; discharges the section 4.38 gate debt, see the
closure note there); tri12 RESULT 0, 10 frames
(dxvk-psbmerge-tri12); cs12 signature unchanged from the
pre-merge tree -- TEX OK (bad=0), BUF / BUF2 FAIL bad=64 at
the vkd3d meta-pipeline descriptor wall, RESULT 1 for that
reason alone (dxvk-psbmerge-cs12). That wall is the
metabind lane's, solved on its own branch and merged
separately; nothing in this merge touches it.

## 4.40 32-bit D3D12 guest verification: tri12-32.exe runs — RESULT 0 (2026-10-28, d3d12-32 lane)

Section 4.31 staged the PE32 vkd3d-proton pair and listed what was
missing: a 32-bit guest actually running DirectX 12. That run has now
happened. `tri12-32.exe` (PE32, in-tree) boots in a 32-bit guest,
creates a D3D12 device through the staged pair, renders its ten
clear frames and reports `tri12: RESULT 0` — the same verdict shape
as the 64-bit tri12 baseline in the same harness family (RESULT 0;
tri12 has no swapchain or present by design, so no chunks are
captured for either ABI).

### Resolution path (guest-side evidence, +loaddll)

- `C:\windows\system32\d3d12.dll` and `d3d12core.dll` load **native**
  — the §4.31 pair at system placement, selected by
  `WINEDLLOVERRIDES=d3d12=n;d3d12core=n` passed by the probe rig
  (probe-local configuration in the new harness
  `web/tests/d3d12-32-system.mjs`; the demo path is unchanged).
- `C:\windows\system32\winevulkan.dll` loads **builtin** — WineHQ
  10.0's PE32 winevulkan from wine32.zip. There is no PE32
  bw64vulkan shim; the path winevulkan.dll → winevulkan.so →
  libvk32 → the 32-bit bridge survives instance creation,
  enumeration and device creation (256 vk32 traps in the passing
  run). The maintenance5/6 question §4.31 left open does not bite
  on this path.
- Rig note: the 32-bit session must spawn the launcher's ARCH32
  loader (`/opt/wine-stable/bin/wine`). The Debian-layout loader
  (`/usr/lib/wine/wine`) fails to find the wine-stable NLS files and
  NULL-dereferences inside `ntdll.so` (i386-unix) during environment
  init — a rig wall, not a product wall; the never-run
  `d3d9-32bit-smoke.mjs` uses the Debian loader and would hit it.

### The wall stack, in the order the guest hit it

1. **Loader/NLS (rig)** — above; fixed in the harness.
2. **`D3D12CreateDevice` → E_INVALIDARG**: vkd3d-proton's
   device-caps gate stops at `Lacking support for single texel
   alignment`. The bridge log showed the cause:
   `UNSUPPORTED properties2-struct (sType=1000028001 …)` ×8.
   `vk32_walkProperties2` had no arm for
   `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT`
   (whose `transformFeedbackQueries` the same gate reads) and none
   for `VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES`
   (the single-texel fields). **Fix 1** adds both arms to the 32-bit
   walker, values mirroring the 64-bit arms. Layout proof is
   compile-time (i686 static asserts against the tree's own
   `vk32_guest.h`): the bridge walks the UNIX-side chain in i386
   SysV layout; the transform-feedback arm is exact under the
   `vk32_w*f` host-offset rule, but the 1.3 arm must write raw guest
   offsets — the rule would place
   `uniformTexelBufferOffsetSingleTexelAlignment` at 184, over
   `maxBufferSize`, where the verified offset is 188 (storage twin
   at 176). The arm's comment records this divergence.
3. **`D3D12CreateDevice` → E_FAIL**: with the caps gates served,
   device creation died in `vkd3d_clear_uav_ops_init` —
   `Failed to create compute pipeline 0, vr -3`. The 32-bit
   `vkCreateComputePipelines` handler validated and wrote
   `args.a[4]`, but the libvk32 shim's layout is a[3]=pCreateInfos,
   a[4]=pAllocator, a[5]=pPipelines; vkd3d passes a NULL allocator,
   so every creation "failed". The 64-bit handler uses a[5].
   **Fix 2** checks and writes a[5].
4. **RESULT 0** — `D3D12CreateDevice OK`, `render target created`,
   `10 frames cleared+submitted`, `RESULT 0`; harness verdict
   `passed: true` on two consecutive runs
   (`test-results/dxvk-d3d12-32-fix3`, `-fix4`).

Guards: `web/tests/suite/unit/vk-properties2-32.test.mjs` (4 tests,
source-pin style). Sabotage-verified: reverting the TF publication,
the 1.3 offset, or the a[5] fix individually fails exactly its own
test; the restored file passes all four and is byte-identical to the
probed build's source.

### Named residue (the stack below RESULT 0)

- **32-bit compute dispatch serving.** The 32-bit
  `vkCmdDispatch` records CMD_DISPATCH, but the shared submit
  emitter drops it loudly (`compute-dispatch`): this ABI's pipeline
  creation never records a compute stage module, so there is
  nothing to serve a dispatch with. Device creation succeeds
  because creation now works; executing 32-bit compute means
  recording stage module/layout/spec at creation on this ABI like
  the 64-bit handler does, plus the R15 census. This is the next
  wall for 32-bit D3D12 compute consumers.
- **`vkCmdBeginRendering` attachment drops** (guard at
  vk64bridge.cpp:11612). Two drops per frame are by design (tri12's
  NULL depth and stencil attachments are fed to the same lambda);
  the deduplicated counter brackets the 10-frame total at 16–31,
  so log evidence alone does not settle whether the color
  attachment also drops in some calls. The `vk32abi` rendering
  structs check out canonical. Settling it needs an instrumented
  run or a presenting probe (next item).
- **tri12draw-32.exe** (PE32 build of tri12draw.c, built by this
  lane, in-tree) passes device creation, swapchain creation, RTV
  creation and **PSO creation**, then the guest faults inside a
  libvk32 trap stub (`vk64_benign`, at its `int 0x80`) called from
  a vkd3d worker thread; the fault address (0x324B0105) sits in the
  bridge's minted 32-bit handle range, which points at a
  handle/pointer confusion in the benign-trap path. Pixel-level
  32-bit proof waits on this.
- Unmodeled properties2 sTypes observed non-fatally in the 32-bit
  chain (left as-is, device creation unaffected): 1000470001,
  1000190000, 1000545001.
- The 32-bit guest console strips spaces from guest stderr
  (`tri12:RESULT0`); the harness matches space-tolerantly. Noted,
  not fixed — it is the emulator console path, shared with all
  32-bit guests.

### Suites and register

- JS suite on this tree: 117 test files pass, 3 fail —
  `provenance-artifacts`, `binary-patch-registry`,
  `runtime-completeness` — exclusively on hand-assembled-dist
  subtests (BUILD_INFO/wasm divergence, dist↔web/runtime sync,
  the gstreamer staged tree absent from a fresh worktree). Zero
  product-code failures; the probe dist was assembled from the
  integ dist plus this lane's build (wasm sha256 `f543cd28…`)
  because the fresh worktree initially lacked the export staging
  payloads (copied since) and the shared build lane was occupied
  at close-out; the merge re-exports canonically. The new guard
  passes in-suite.
- `run-native.py` compiles the bridge edits and aborts on the same
  stale spec-census assert with the pristine base file —
  pre-existing on this base (the vk64harness repair is queued
  ahead of this lane). vk64bridge.cpp is not in the linux native
  suite's graph.
- Register: no disposition changes. The vulkan.md
  transform-feedback row's stated publication (queries TRUE, limits
  zero, features FALSE, because vkd3d's gate reads it) now holds on
  the 32-bit walker as well as the 64-bit one.
### Merge outcome (integ, 2026-10-29)

Merged as fb8f5ed5a (parents a23d1263c + 12a402fd6); the lane
section above is renumbered 4.32 -> 4.40. The only content
conflict was this report (HEAD kept wholesale, the lane section
appended); vk64bridge.cpp auto-merged and its merged delta is
byte-identical to the lane delta, verified hunk for hunk -- both
vk32 fixes survive verbatim. No payload, pin, or staged member
changed; no register disposition changes (register unchanged
372 = 73/66/233/0, diff-proven: the only register edits are two
citation re-pins, +51 across the walker insertion -- logAskedFor
9865->9916, vk64Unsupported 9866->9917, construct-verified by
direct read; citation audit 0 DRIFT / 0 MISSING, 228/14/96/7).
Suites on the merged tree: JS 321/0 (320 baseline + the lane
guard vk-properties2-32); native 647/0 single-threaded;
run-native.py end-to-end green (31 PASS blocks). Dist rebuilt:
"Browser export verified", stamp 178ee00d6, wasm sha 08d3a0d0,
support archive bbc583c6.

One merge fallout, adjudicated as test rot: run-native.py first
aborted at the harness 32-bit vkCreateComputePipelines block --
the harness packed the out-pointer at a[4], the convention the
buggy handler used. The real shim packs a[3]=pCreateInfos,
a[4]=pAllocator, a[5]=pPipelines (tools/rootfs64/libvk32/
libvk32.c, quoted in the lane fix comment), and the guest proof
above ran through that shim. The harness now packs per the shim
(a[4]=0, the NULL allocator vkd3d passes; a[5]=out), both
assertions at full strength; sabotage loop closed -- the
corrected harness aborts against the reverted (a[4]) handler and
passes against the merged one (fallout commit 8b247b79c).

Guest gate on the merged dist (slot-serialized, uploading
harness; test-results/dxvk-d3d12-32merge-*): (a) tri12-32.exe
RESULT 0, verdict passed:true -- d3d12.dll/d3d12core.dll native
from system placement, winevulkan.dll builtin, 256 vk32 traps,
the lane signature reproduced on the merged tree; (b) tri12
RESULT 0 (guest verdict; the harness composite reads
passed:false on the zero-chunk capture artifact -- tri12
presents nothing by design, as this section notes for both
ABIs); (c) uav12 SUMMARY exec=ALL6, RESULT 0 -- no regression
from the vk32 changes; (d) cs12 signature unchanged -- TEX OK
(bad=0), BUF/BUF2 FAIL bad=64 at the meta-bind wall (metabind
not yet merged), RESULT 1 for that reason alone.

PROP32 OVERLAP FLAG for that merge: lane-prop32 independently
added TRANSFORM_FEEDBACK and VULKAN_1_3 arms to
vk32_walkProperties2 (plus maintenance-5, descriptor-indexing,
and the 1.1/1.2 completions). The arms merged here are now the
integ text; the prop32 merge must reconcile per arm rather than
re-add -- in particular this lane 1.3 arm writes raw guest
offsets while prop32 built a pack(4) guest-struct mirror with
its own writers, and the two lanes guards (source-pin here,
harness block there) are the arbiters.
## 4.41 Rendered-then-copied readback: page-side buffer->image application + readback-batch queue drain (2026-10-28, lane-b2i)

The limitation section 4.24 named when readback-zeros were fixed is
closed: an image rendered into on the page tier and then overwritten
by a buffer->image copy (B2I) read back stale. Pinning the flow with a
new probe shape showed the stale answer is TWO drops compounding, not
one -- a blocker stack in miniature.

The probe (tools/vkd3d/probe/rb12.c, three new variants on the rb12
rig): RDB2I clears a render target to magenta in one submission, then
a B2I upload of a per-pixel pattern overwrites it in a second
submission, then the target is copied to a readback heap. RDB2IP is
the same with a 2x2 sub-rect upload at (1,1) (expect pattern inside
the rect, magenta outside). RDB2IS puts clear + B2I + copy in ONE
submission. Baseline on this lane's base (f47fc8606):

- RDB2I OK -- accidentally: the section-4.24 heap seed serves guest
  memory, and for a FULL overwrite the seed is the complete final
  content, so the wrong path gives the right answer.
- RDB2IS FAIL bad=48, first pixel got [255 0 255 255] (the clear
  colour), want the pattern: the render and the readback share a
  frame, the page's render target exists, and the readback resolution
  prefers it over the seed -- but the B2I was never applied page-side,
  so the pre-copy content is served. 4.24's limitation in pure form.
- RDB2IP FAIL bad=36, first pixel got [0 0 0 0], want magenta: the
  rendered content never reached the readback at all. Chunk-stream
  forensics on the captured run: 14 present batches received, exactly
  the 4 clear-only producer batches never rendered (pipeline2
  rendered=10); the readback recorded readback-unavailable (ok=false)
  and the seed -- pattern rect over zeros, the guest memory having
  never held the GPU-side clear -- was served.

Drop (1), the queue: non-readback submits never block the guest, so
the render batch is still queued when the readback batch lands behind
it; the page pump's latest-frame-wins takeLatest() then discards the
render batch unrendered (a present-pacing choice that is only safe
for frames nothing synchronous depends on). Drop (2), the copy: the
bridge executed CMD_COPY_B2I eagerly into guest memory (execCopy) and
serialized NOTHING for it -- a command buffer holding only B2I copies
was not even frame-relevant -- so the page tier could never learn the
upload happened.

Fix, fail-closed at each layer:

- Bridge (source/vulkan/vk64bridge.cpp): CMD_COPY_B2I joins the
  frame-relevance set, and its serializer case streams the copy in
  command order: the source range through the same deduped
  BUFFER_DATA path every other consumer uses, then a B2I_UPLOAD
  record (V2 0x21) naming the destination image, the source buffer +
  absolute offset, the image format, and the exact region (mip,
  baseLayer/layerCount, origin, extent) in execCopy's own
  source-layout terms (source row stride + rows per slice from
  fmt_block). A copy whose source span exceeds the per-buffer send
  cap emits nothing and is named (vk64Unsupported "b2i-page-apply");
  copies that failed in execCopy were already named there. One
  serializer serves both ABIs.
- Protocol (web/vk-protocol.mjs): V2_REC.B2I_UPLOAD 0x21 (the chunk
  parser bound moves with it; the vk-compute-records pin for "the
  next type is rejected" moves 0x21 -> 0x22). The decoder validates
  the region (non-zero, bounded extent/layers/mip, non-zero layout)
  and queues a bufferToImage op on an implicit pass. And
  V2FrameQueue.takeForPump(): when any queued batch carries readbacks
  (a guest-blocking consumer point), the pump drains the queue in
  order up to and including the first such batch; otherwise
  latest-frame-wins exactly as before.
- Page (web/vkwebgpu.mjs): the pump renders the takeForPump batch in
  order (the per-frame retry body is renderOne, unchanged). The
  bufferToImage op applies through applyBufferToImage: destination is
  the image's render target, else its materialised texture; no page
  texture counts stats.b2iNoTarget and stays seed-served (copy-only
  images, the 4.24 case). The write goes through the frame's command
  encoder (never queue.writeTexture, which would jump ahead of
  not-yet-submitted draws), staged from the frame's retained buffer
  bytes with the bridge's row layout. Declines by name: depth formats
  (b2i-apply-depth), target/image format mismatch, mip beyond the
  texture, missing or short source bytes, byte-expanding format
  conversions (packed formats; length-preserving ones like
  norm16->half convert per texel and stay exact), block-misaligned
  compressed regions.

Guest proof (uploading harness, canonical pair d77fdad5/d2bd847a,
evidence test-results/dxvk-b2i-{pre,pre2,post,sab,tri,draw}):
baseline above; post-fix rb12 RESULT 0 -- all eight variants OK
(stats: b2iApplied 4, b2iNoTarget 2 = UPLDTEX's upload, whose readback
stays seed-served by design; readback-unavailable back to exactly
UPLDTEX's two). tri12 RESULT 0 (twice; the in-tree tri12 is
clear-and-submit, its verdict is self-contained) and tri12draw
RESULT 0, 10 frames drawn+presented, centre [127,64,64] (the
first-light signature) -- the pump change does not disturb the
present path. Sabotage run (takeForPump neutered + applyBufferToImage
disabled, dist rebuilt): the exact baseline signatures return --
RDB2IS bad=48 stale magenta, RDB2IP bad=36 zeros, RDB2I OK,
pre-existing variants green, RESULT 1.

Guards: web/tests/suite/unit/vk-b2i-page-apply.test.mjs (7 tests:
wire decode + rejection shapes, bridge/protocol/page source pins,
takeForPump policy, mock-device application incl. partial region and
the decline set). Sabotage-verified: the unit file fails 3 tests
under the same two neuters used for the guest sabotage run, and the
guest run above restores the failures. The vk-compute-records chunk
pin moved as noted. Suites on this branch: JS 315 files / 0 failures
(314 at base + the new file); register 368 rows = 69 IMPLEMENTED /
66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN, citations 0 DRIFT /
0 MISSING (221/13/100/7; 205 citation numbers re-pinned by pure
delta map over this lane's three file diffs, plus the new row).
Native suite not run on this branch: the bridge is not in the linux
native build graph (its native check is the harness repair the
coordinator's merge queue carries); the wasm build compiles the
changed bridge TU and the guest probes exercise it end to end.

Residue (named, not silent): B2I spans over the 8 MiB send cap stay
guest-memory-only (b2i-page-apply); depth-image uploads decline
(b2i-apply-depth) -- a rendered depth image overwritten by B2I still
serves stale to a later sample/readback; the queue cap (3 batches)
can still drop a producer batch under heavy present traffic ahead of
a readback batch (pre-existing cap behaviour; the guest then gets
the seed + a named readback-unavailable). Next wall in this area:
none known for colour readbacks; the D3D12 campaign queue continues
at the vkd3d meta-pipeline descriptor wall (cs12 BUF/BUF2).

Merge outcome (2026-10-28, merge lane): merged as ff832946b +
citation re-pin fallout 1f0d00241. Post-merge verification: JS suite
322/0 (321 baseline + vk-b2i-page-apply), native 647/0, run-native.py
31 PASS blocks end-to-end, citation audit 0 DRIFT / 0 MISSING
(228/14/97/7 over 346 cited rows), register 373 = 74/66/233/0, dist
"Browser export verified". Guest gate: (a) rb12 RESULT 0, all eight
variants OK, page stats b2iApplied 4 / b2iNoTarget 2 matching the
branch signature exactly (evidence test-results/dxvk-b2i-merge-rb12);
(b) tri12 guest RESULT 0 (harness composite passed:false = the known
zero-chunk capture artifact, adjudicated on the guest verdict) and
tri12draw passed:true RESULT 0, centre [127,64,64]; (d) cs12
signature unchanged (TEX OK bad=0; BUF/BUF2 bad=64 at the
not-yet-merged metabind wall).

(c) uav12 FAILED -- a regression from this merge. exec=FAILED on all
six EXEC cases, RESULT 1, reproduced identically in two genuine guest
verdicts (evidence test-results/dxvk-b2i-merge-uav12 and
dxvk-b2i-merge-uav12c; one further attempt wedged in guest boot under
box load and produced no verdict). Signature: uav12's pixel shader is
a pure copy (return uav[texel]) of the pattern its own upload copy
placed in the texture, and it reads 0xFF-filled content instead
(got=255 at idx 0 ch 0 in every case; mismatch counts
48/45/48/48/32/32 of 64 words, identical across both runs);
flag=TRUE and sweep=ALL18 still pass. Page stats for the failing runs
show b2iApplied 12: the new path IS exercised by uav12 -- its per-case
upload copy leads every frame's op stream (bufferToImage first, then
the draw). The pre-merge tip 369f011c5 passed uav12 exec=ALL6 at the
d3d12-32 merge gate the same day, and lane-b2i is the only product
delta since, so the regression is attributable to this merge. The
discriminating difference from rb12 (which passes): rb12's consumer
of the uploaded image is a readback copy, uav12's is a shader load in
the SAME frame as the upload -- the applied upload evidently does not
reach the texture instance the draw samples (applyBufferToImage
resolves or creates its target before the draw binds its own;
pre-merge, the same frames were served correctly by the guest-memory
seed alone). This merge is therefore NOT gate-complete: the
section-4.41 claims themselves (rb12 rendered-then-copied readback)
stand as proven, but the merge is flagged to the coordinator for a
fix-forward lane on the upload-then-shader-load flow (or a revert).


## 4.42 uav12 regression from 4.41 fixed: the decoder's bare buffer key is the base slice, not the first range record (2026-10-29, lane-b2ifix)

The 4.41 merge-outcome addendum flagged this merge not gate-complete:
uav12 exec=FAILED on all six cases with a uniform got=255 signature.
The addendum's ranked hypothesis (H1, texture-instance shadowing --
applyBufferToImage's gpuTextureFor fallback resolving a different
texture instance than the draw binds) is REFUTED, as is the weaker H2
(stale F.buffers source record). The texture-instance discipline was
never broken: the failing run's page stats show the apply creating the
texture and the draw's bind hitting it (texMiss 12 / texHits 12,
exactly one miss + one hit per frame), and a guest-free replay of the
captured chunks through the genuine tier and real Dawn (below) shows
the bind group referencing a view of the very texture the copy wrote.
H2 falls to the wire decode: the copy's source record carries the
pattern bytes at the exact (buffer, offset) key applyBufferToImage
looks up, and the copy math is exact for all six formats.

Root cause, convicted at three independent levels. (1) The 0xFF is
the render target's CLEAR, not a fill: uav12 clears to magenta
(1,0,1,1) ("never an expected value"), and the guest's mismatch
counts match magenta exactly (48/64 with every alpha agreeing; the
page's present sample reads lit=0 / painted=16, i.e. the target holds
exactly its clear). So the draw rasterised ZERO fragments. (2) The
wire order in every failing frame: BUFFER_DATA for the shared heap
buffer at offset 65536 (the B2I upload source, 784 pattern bytes)
comes FIRST, then B2I_UPLOAD, then VERTEX_BIND, then BUFFER_DATA for
the same buffer at offset 0 (112 vertex bytes). Pre-merge, the vertex
record was the buffer's only record. vkd3d sub-allocates the upload
heap and the vertex buffer from ONE VkBuffer; the guest's list is
copy-then-draw, and section 4.41's serializer streams the copy's
source range in command order, ahead of the draw state. (3) The page
decoder kept the bare-id buffer key from the FIRST record for that id
("Keep the bare-id key (first record) for VB/IB lookups"), and the
draw path builds the vertex GPUBuffer from the bare key
(web/vkwebgpu.mjs, the vbind loop; the index paths read the same
key). Post-4.41 the bare key held the upload pattern, so the draw's
"vertex buffer" was pattern bytes: read as float positions they are
denormal garbage, the fullscreen triangle degenerates to zero area,
no fragments run, the clear survives, and the readback returns the
clear colour -- the exact guest signature, including R16_FLOAT/R8_UINT
(their sampled-storage serving is downstream of the same dead draw).
rb12 was never exposed (its consumer is a readback copy of the image,
resolved through qualified keys), tri12 neither (its vertex buffer's
buffer carries no earlier foreign range record), and descriptor
bindings were immune (they already key by (id, offset)). The index
paths shared the exposure for any offset-0 index slice of a shared
buffer; no in-tree probe exercises that shape.

Fix (web/vk-protocol.mjs only, decodeV2Frame's BUFFER_DATA case; no
wire change, no tier change, no bridge change): the bare-id key is
arbitrated to the LOWEST-offset range record for the buffer id --
which is what every bare-key consumer already assumes it holds (the
base slice) -- taking bytes and hash from that record; ties at the
lowest offset prefer the longest payload so a short descriptor range
at the base cannot starve a longer base slice. A buffer with no
low-offset record in frame keeps its only record as the bare key
(legacy behaviour preserved). Qualified (id, offset) keys are
untouched, as are applyBufferToImage, takeForPump ordering, and the
4.38/4.39 texture paths.

Guard: web/tests/suite/unit/vk-buffer-bare-key.test.mjs (5 tests) --
the uav12 frame shape (copy-source range preceding the base record),
the later-frame shape (base record byte-less: the bare key follows
the base hash, so the tier resolves the vertex buffer from its
persistent cache, not from the copy source's bytes), base-first
order unchanged, the base-offset tie, and the no-base-record
fallback. Sabotage-verified: with the arbitration reverted, exactly
the three arbitration cases fail and the two order-preserving cases
stay green; vk-b2i-page-apply (rb12's guard) is green throughout.

Verification on this branch: JS suite 323 files / 0 failures (322 at
base + the new guard); run-native.py 31 PASS blocks, 0 FAIL; register
citations 0 DRIFT / 0 MISSING (the vk-register-citations audit passes
unchanged; the edit's line growth is inside one decoder case).
Real-browser chunk replay (the genuine page tier + Dawn on the
captured failing chunks, no guest): before the fix the readback heap
bytes are the magenta clear and lit=0; after the fix the same chunks
yield the pattern bytes and lit=16. Guest gate on this branch's own
dist (uploading harness, canonical pair a96b4e9d/3d3bfe43): uav12
exec=ALL6 RESULT 0 (all six EXEC cases OK, 16/16 texels each;
b2iApplied 12 -- the 4.41 path still exercised; evidence
test-results/dxvk-b2ifix-uav12b; the first attempt died at the
session-ready wait under box load and produced no verdict), rb12
RESULT 0 with all eight variants OK (the 4.41 claim preserved),
tri12 RESULT 0, cs12 signature unchanged (TEX OK bad=0; BUF/BUF2
bad=64 at the not-yet-merged metabind wall).

Register: NO disposition change. The 4.41 row's claim (rendered-
then-copied readback served page-side) was and remains true; the
regression was a decoder keying defect in a neighbouring consumer
(the draw's vertex/index byte source), and this fix restores the
pre-merge uav12 behaviour without touching any row's disposition.
The register stays 373 = 74 IMPLEMENTED / 66 REJECTED-LOUDLY /
233 NOT-A-GAP / 0 OPEN.

Residue (named): the bare key remains a first-class fallback for
producers that emit a single range per buffer; a vertex/index slice
bound at a NONZERO offset of a shared buffer travels under its bridge
slice id (bufferSliceId), whose only record is the slice itself, so
it was never exposed to this defect. Descriptor-binding bare
fallbacks (qualified record absent) now also resolve to the base
slice instead of an arbitrary first range -- strictly more correct,
and unchanged in the case that fallback exists for.


Addendum gate item (coordinator, from the rawscan lane's unadjudicated
flag): d3d11-triangle run on this branch's fixed dist. Attempt 1 died
in bootGuest (session-ready wait, 600 s timeout) under box load ~9 --
no verdict, the known boot wedge. Attempts 2 and 3 both ran the guest
to completion and failed IDENTICALLY on the same assertion --
"vkCmdCopyBuffer must be gipa-HIT" (count 0) -- so by the addendum's
rule the signature is DETERMINISTIC, not a load artifact. But the
characterisation acquits the product path: the guest itself fully
succeeded (tri1193: D3D11CreateDevice OK FL=0x9300; shaders compiled;
LIT=8192 -- the triangle rendered; RESULT 0), and the run's console
contains zero gipa lines of any kind and exactly one copy line,
"vk64: trap vkCmdCopyBuffer2": DXVK's copy reached the bridge through
the KHR *2 entry point's trap, not through a logged
vkGetInstanceProcAddr HIT for the v1 name the test counts. The
failing assertion is a trace-log expectation about HOW the copy
entry point is resolved, not a rendering or copy failure -- the same
*2KHR-serving world section 4.24 established. No B2I_UPLOAD /
takeForPump involvement is observable (this is a DXVK/D3D11 flow, and
the gipa trace is bridge-side, upstream of everything this lane or
the 4.41 merge touched on the page). NOT fixed in this lane, per the
addendum: it is a separate determination (test expectation vs the
*2 trap path) for the coordinator. Evidence:
test-results/suite-d3d11-triangle (third run's console).

### Merge outcome (integ, 2026-10-29)

Merge commit 361f31502 (parents 6b15ff9ee + 1a713dd24) + citation
re-pin 5caf4d245. The merged web/vk-protocol.mjs delta is
byte-identical to the lane delta (diff sha256 3f10c9a2...); section
4.42 stands as numbered, no renumbering, no duplicate sections.
This merge CLOSES section 4.41's NOT-GATE-COMPLETE flag: the uav12
regression the 4.41 merge record flagged is fixed by this lane and
re-proven on the merged tree below (gate (a)); the 4.41 merge is
now gate-complete, and its claims (rb12 rendered-then-copied
readback) were never in doubt.

Suites on the merged tree: citation audit 0 DRIFT / 0 MISSING
(228 MATCH / 14 MATCH-INHERITED / 97 UNCHECKABLE / 7 EXEMPT over 346
cited rows -- the lane's decoder edit had shifted the decodeV2Frame
default case, moving that one row MATCH -> UNCHECKABLE in the raw
audit; re-pin 5caf4d245 moves its citation 1290 -> 1326 and restores
the baseline exactly); register unchanged 373 = 74 IMPLEMENTED /
66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN (pipe-aware recount);
JS unit suite 323 passed / 0 failed (runner --unit,
BW_TEST_DIST=integ dist); emulator native 647 OK / 0 FAILED;
run-native.py 31 PASS / 0 FAIL. Dist built from the merge commit
(stamp 361f31502, wasm ca4ebe1f...; "Browser export verified";
completeness gate 0 failures, 1 pre-existing environmental skip).

GUEST GATE on the merged dist (uploading harness d3d12-pixels.mjs;
canonical d3d12 pair sha-verified a96b4e9d... / 3d3bfe43... from
wt-restage build.w64; probe slot, one at a time):
(a) uav12 PASS -- SUMMARY flag=TRUE sweep=ALL18 exec=ALL6,
RESULT 0, b2iApplied 12 (the 4.41 path still exercised); the
harness composite passed:false is its clear-colour pixel check
(it demands tri12's cornflower clear of a 4x4 UAV target),
adjudicated on the guest verdict + SUMMARY. Evidence:
test-results/dxvk-b2ifixmerge-uav12.
(b) rb12 PASS -- RESULT 0, all 8 variants OK, bad=0 (BUF, CLEAR,
UPLDTEX, CONSTPS, DELAY, RDB2I, RDB2IP, RDB2IS). Evidence:
test-results/dxvk-b2ifixmerge-rb12.
(c) tri12 PASS -- guest RESULT 0, no errors, no FAIL lines; the
composite passed:false came with 0 chunks captured, the known
offscreen capture artifact, adjudicated on the guest verdict.
Evidence: test-results/dxvk-b2ifixmerge-tri12.
(d) cs12 PASS, signature unchanged -- TEX OK bad=0; BUF bad=64 and
BUF2 bad=64 at the meta-bind wall (metabind merges next); RESULT 1
for that reason alone. Evidence: test-results/dxvk-b2ifixmerge-cs12.

D3D11 DETERMINATION (coordinator, from this lane's addendum): the
d3d11-triangle browser-test failure is a STALE TEST EXPECTATION,
not a product defect. On the fixed dist the guest fully succeeds
(D3D11CreateDevice OK FL=0x9300, shaders compiled, LIT=8192
rendered, RESULT 0), the console contains zero gipa lines of any
kind, and its single buffer copy arrives through the
vkCmdCopyBuffer2 trap -- the *2KHR serving world of section 4.24 --
while the test counts a v1 vkCmdCopyBuffer gipa-HIT that no longer
occurs. No B2I_UPLOAD / takeForPump involvement is observable. Two
identical failures make it deterministic, not a load artifact. A
separate lane (lane-d3d11test) is fixing the test expectation; no
test was changed in this merge. This also adjudicates the rawscan
lane's open d3d11 flag: not a load artifact and not a b2i-merge
side effect -- a test-expectation defect.

## 4.43 Meta-bind delivery: the push-descriptor bind was swallowed by the PE shim's benign stub; storage texel buffers served end to end (2026-10-28, metabind lane + recovery)

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

## 4.44 Metabind frontier-review fixes: texel materialisation lifetime, R12S variable vetting, partial-overlap copies, draw-time write marks (2026-10-29, metabindfix lane)

An independent frontier-model review of the metabind commit (a997ea0a7)
returned 'merge with fixes': two HIGH findings (kind-6 page textures
cached by seed-content hash while dispatches write into them; the
storage-texel readback registry's missing lifetime rule), two MEDIUM
(R12S never vets uses of the image VARIABLE; no size limits on the
materialised texture), plus aliasing, partial-overlap-copy, graphics-
stage marking and a vacuous shim test. This lane adjudicated every
finding from the code before changing anything; verdicts, fixes,
guards and sabotage results below. Branch lane-metabindfix on
a997ea0a7; not merged, not pushed (the metabind merge is gated on
this lane).

Finding 1 (HIGH) -- CONFIRMED in both failure modes; the [unverified]
':nohash' leg REFUTED as a staleness vector. storageTexelTextureKey
keyed persistTexCache by view id + format + a pure bridge content
hash of the seed bytes; GPU writes never touch guest memory or the
hash, so a guest re-upload of the original bytes cache-hit a texture
holding earlier GPU writes (silent wrong reads), and an unrelated CPU
write reseeded and discarded GPU writes with no signal. The nohash
leg: the BUFFER_DATA decoder sets bytes and hash together under the
same key (bufferId:absOff), storageTexelTextureFor required the bytes
before creating, and a bind with no record degraded to the LOUD
'bind-group-storage-texel-unavailable' drop -- a missing hash could
freeze the key but could never serve stale bytes. FIX (web/vkwebgpu.mjs):
content is no longer part of kind-6 identity at all. One materialisation
state per guest view id (stexelState): identity = view id + buffer +
absolute offset + format + length; content is managed by IN-PLACE
reseed (queue.writeTexture into the same GPUTexture) so cached bind
groups stay valid. Reseed rules follow the guest stream order, judged
per submission batch: a changed bridge content hash reseeds (the CPU
write wins); an unchanged hash reseeds only when the texture was bound
in an EARLIER batch (the guest re-emitted the original bytes after the
GPU wrote -- failure A); a same-batch re-emission keeps the texture
(the bridge re-sending an unchanged record is not an upload). A view
id reused for a different range/format destroys the old texture and
clears the bind-group cache. storageTexelBind runs from BOTH the
bind-group fingerprint and bindGroupEntries, so a fingerprint-cache
hit still reseeds/registers (this is also finding 2's registration
half).

Finding 2 (HIGH) -- CONFIRMED with a correction. The registry was
never cleared (grep-verified), but neither is storageBufferRegistry,
and the bridge can defer a copy into a later submission than the
dispatch that wrote the data: renderer-lifetime persistence IS this
tier's registry lifecycle, and a per-submission clear would strand
exactly the deferred readbacks (the repeat meta clear reading zeros).
The true defects were the ones the review named second: lookup took
the FIRST bufId+range match (two views over one buffer could serve
the wrong texture), and entries were texture snapshots a replacement
could orphan. FIX: storageTexelRegistry maps view id -> the SHARED
state object (never a snapshot), so a replaced texture is unreachable;
readback resolution (pickTexelReadback) picks the freshest covering
state by last-bound batch, and several DISTINCT textures tied at the
freshest batch decline by name ('buffer-readback-texel-ambiguous')
instead of an arbitrary first match. States are destroyed in exactly
one place (view-identity change), which also drops the directory
entry by replacing the state.

Finding 3 (MEDIUM-HIGH) -- disposition: readback precedence fixed,
claim NARROWED, residue named. Within one submission batch a buffer
readback now prefers a covering texel state over a raw storage-buffer
binding of the same buffer (before, the SSBO registry was consulted
first and texel writes could be lost); a cross-batch texel entry is
still served after the SSBO tier when nothing fresher exists. The
register's texel-buffer-view row now scopes its claim to the
deferred-copy readback of the same buffer and names the residue:
other views of the buffer bound in later submissions still read guest
bytes, because GPU texel writes are not propagated into guest memory
outside the readback path.

Finding 4 (MEDIUM) -- CONFIRMED. spirvfix R12S vetted only Load/
CopyObject RESULT ids (traced); OpImageTexelPointer and OpCopyMemory
name the image VARIABLE, which was never checked, so an atomic
(InterlockedAdd on an RWBuffer) slipped through and R12S emitted
invalid SPIR-V while logging success. FIX (tools/spirvfix/spirvfix.mjs):
inside storageTexelBuffersTo2D, scan FUNCTION BODIES only (globals
excluded -- a decoration/type literal can numerically equal a variable
id, the same literal-collision class the metabind R12S fixes closed)
and refuse when any instruction other than Load/CopyObject/Variable
or a debug op names a varIds member: 'storage texel image variable
used by op N beyond OpLoad (texel-pointer/atomics are not wrappable)'.
Guard: hand-built SPIR-V fixtures in spirvfix-texel-buffer.test.mjs
(the atomic module refuses, Dim stays Buffer; the identical module
without the atomic converts); sabotage (scan disabled) turns exactly
the atomic case red.

Finding 6 (MEDIUM) -- CONFIRMED, all three legs. No maxTextureDimension2D
check (a 128M-element R32 view needs height 16384 at the 8192 wrap ->
async createTexture error invalidating the frame); no storage-format
capability check (r16float/r8uint are refused by Dawn for storage,
probe-proven in the sampledtex work); and floor-sized texel counts
dropped partial tail texels and let readback rows exceed the texture
height for non-bpp-multiple view lengths. FIX: storageTexelCreate
declines BY NAME before creating ('storage-texel-format-unmaterialisable',
'storage-texel-too-large', each reported once per view); the texel
count is ceil (tail texel materialised, zero-padded at seed); the
readback rows computation is defensively checked against the texture
height ('buffer-readback-texel-rows') so a copy can never exceed the
texture as an async encoder error.

Finding 7 (MEDIUM) -- mechanism CONFIRMED, the 'zeros' outcome
REFUTED. The bridge deferred a copy only when a GPU-write mark FULLY
CONTAINED the copy range (vk64FindGpuWrite), so a partly-overlapping
copy fell back to the record-time eager memcpy: bytes outside the
view were copied CORRECTLY (no zeros) -- the actual defect is that
the overlapped subrange silently served stale pre-dispatch bytes.
FIX (source/vulkan/vk64bridge.cpp): vk64FindGpuWriteOverlap finds the
earliest clean mark intersecting the range (cleanliness judged on the
overlap subrange), and vk64CopyRegionSplit walks each copy region
deferring maximal covered subranges as CMD_COPY_B2B_RB records and
memcpy-ing the rest eagerly at record time; both vkCmdCopyBuffer and
vkCmdCopyBuffer2/KHR route through it (containment is now the special
case of a single deferred record). Native guard: a dispatch writing
view [64,320) of a 512-byte buffer, then a whole-buffer copy, yields
exactly one deferred record {srcOff 64, size 256} with the outer
ranges already correct in guest memory at record time; sabotage
(overlap query disabled) aborts the run at the first
deferral-dependent block.

Finding 9 (LOW) -- CONFIRMED reachable, fixed by marking. In this
tree vk64MarkComputeWrites was called ONLY from the three dispatch
traps, while R12S conversion is stage-agnostic and the bridge audit
admits Dim=Buffer storage writes for any stage -- a fragment-shader
UAV write would be served but never marked, so a later copy read
stale bytes eagerly. FIX: the mark walk was extracted
(vk64ReplayBoundState / vk64MarkBoundWrites, shared with the compute
path) and vk64MarkDrawWrites now runs at the top of all six 64-bit
draw traps for graphics pipelines (it also marks descriptor-set SSBO
writes at draw time, a latent sibling of the same shape). Native
guard: a graphics pipeline with a kind-6 push binding + vkCmdDraw
defers the copy of the view range; sabotage (the vkCmdDraw call
removed) turns exactly that block red.

Finding 10 (LOW, test fix) -- the built-DLL check in
vk-push-descriptor-shim.test.mjs matched 'vkCmdPushDescriptorSet' as a
raw substring, which the KHR name also satisfies (the old DLL already
contained the core string inside the KHR one, so the check was
vacuous). Now matches NUL-terminated export strings, with a self-pin:
a buffer containing only the KHR name must NOT satisfy the core name.
Sabotage (substring matcher) turns the test red.

Finding 12 (register formatting) -- CONFIRMED and fixed: the
storage-format-access row and the metabind exports row were joined on
one physical line in docs/known-gaps/vulkan.md; they are now separate
rows. Pipe-aware recount is unchanged: 367 rows = 68 IMPLEMENTED /
66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN (both rows were already
counted). Citations re-pinned after this lane's line shifts (audit
--fix plus exact diff line-mapping for the multi-site function
citations): 0 DRIFT / 0 MISSING.

Guards added: web/tests/suite/unit/vk-storage-texel-lifetime.test.mjs
(mock-device replay: stream-order lifecycle incl. the repeat-bind and
re-upload cases cs12 cannot reach because it runs each case once;
two-views freshness + same-batch ambiguity decline; texel-over-SSBO
precedence; too-large and unmaterialisable declines; tail-texel
serving) -- 5/5, six sabotages each turning exactly one case red.
vk-storage-texel.test.mjs pins updated to the new shapes (5/5);
spirvfix-texel-buffer.test.mjs 10/10; vk-push-descriptor-shim.test.mjs
3/3. Native: run-native.py (stock) aborts at the stale fmt-bpp table
assert (vk64_native_test.cpp:501), byte-identical on pristine
a997ea0a7 -- the base predates the harness repair, as the brief
records. Diagnostic mode (a scratch copy with ONLY that assert
neutralized, the recovery-lane precedent): 24 PASS blocks, including
the metabind storage-texel block and this lane's two new blocks,
then execution aborts at the second pre-existing stale assert (the
features assert; pristine line 1357, this tree's 1470 after the
lane's insertions). Pristine diagnostic: 22 PASS blocks, the same
features abort. Sabotages: overlap query disabled -> abort at the
first deferral-dependent block; the vkCmdDraw marking call removed
-> abort exactly in the draw-marking block.

Suites + probes (verified): JS unit suite on the lane dist ('Browser export verified'):
317 passed / 0 failed (base 316/0; the +1 is the new
vk-storage-texel-lifetime guard file). Census maintenance in the
established style: vk-image-dirty-generation's raw guest-write
primitive pin 53 -> 52 (the copy split consolidated two per-handler
memcpyToGuest lines into the helper's single line; disposition note
recorded in the test). Citations 0 DRIFT / 0 MISSING (220 MATCH /
14 MATCH-INHERITED, identical to base). cs12 guest re-run (uploading
harness, this lane's dist): BUF OK (bad=0), BUF2 OK (bad=0), TEX
FAIL (bad=16, the branch-base wall), RESULT 1 for TEX alone -- the
expected branch signature, unchanged from the metabind tip.
Evidence: test-results/dxvk-metabindfix-cs12/. Cross-reference:
frontier review #2 (vk32compute) lists partially-overlapping copies
going eager/stale as a shared-with-64-bit limitation; on this branch
that limitation is closed for the 64-bit handlers by the finding-7
split (the 32-bit handlers are unchanged here -- the vk32fix lane's
scope).

Residue: cross-view aliasing of GPU texel writes (finding 3, named
in the register); the page tier still cannot serve r16float/r8uint
storage texel views (declined by name at materialisation; the r16
lane's sampled-texture rewrite is the serving path, queued
separately). MERGE NOTE for the crootmark lane: this lane extracted
the compute mark walk into vk64ReplayBoundState/vk64MarkBoundWrites
(vk64MarkComputeWrites is now a thin wrapper keeping its pipe->cs
gate); crootmark's vk64MarkComputeWritesUpTo prefix pattern should
wrap vk64MarkBoundWrites the same way at its merge.

ENVIRONMENT REPAIR (recorded for the merge lane): the pinned
wine64.zip archive for this base (runtime-assets.json sha256
b2ac0dc6..., 213105632 bytes) existed only in the metabind lane's
web/runtime, which its completion strip deleted; the GitHub
runtime-assets release now 404s. The archive was RE-STAGED on this
branch from the pinned pre-restage base (sha256 14182fe0..., preserved
in /home/ubuntu/backups/winbox-vkd3d64-restage-20261028-135228/)
merged with this tree's pinned payloads via
tools/rootfs64/stage-vkd3d-payloads.sh: member-level identity with
the lost archive is proven (same 1257 members; every non-payload
member byte-identical to the pinned base; the five payload members
byte-identical to the completeness-pinned payload sources; total size
exactly 213105632). Only the five staged members' zip timestamps
differ, so the archive hash differs: the branch's runtime-assets.json
and docs/known-gaps/runtime-artifact-manifest.md pins were updated to
the re-staged hash 47d24015... (bootstrap --check: 13 files verified,
completeness 0 failures). wine32.zip was recovered byte-exact from
/home/ubuntu/backups/winbox-vkd3d32-payloads-20261028-130037/ (hash
matches the existing pin). At the metabind merge the integ lineage's
manifest and archives govern; this repair is branch-local.

### Merge outcome (integ, 2026-10-29) -- GATE COMPLETE: cs12 RESULT 0 on integ

Merged into integ (branch integ/winbox-20261022b) as merge 8e8d6597f
(lane-metabindfix 4736ba8c9, carrying lane-metabind a997ea0a7), fallout
6fd18f716 (shim re-stage re-pins + register delta + these sections,
renumbered 4.43/4.44), repair 9e9989e0f, citation re-pin fallout
88415a446, and guard re-pin fallout cc40671d9. NOT pushed; the dist has
not been redeployed.

Convergence (hazard 1): the branch carries its own merge of
lane-compute, already in integ via 954e72aa1 (section 4.30). The
merge-base is b277cbf01 (the 4.30 commit itself); the branch delta
against it includes already-integ compute/bstring/uintpso/mem32
content with identical bytes, which auto-merged to no-ops (verified
per file: kprocess/bstring/test files, d3d12-pixels.mjs show no delta
vs HEAD). The four hand-resolved files were verified against BOTH
parents: vk64bridge.cpp (the metabindfix helper factorization --
vk64ReplayBoundState + vk64MarkBoundWrites -- is a strict superset of
integ section 4.30/4.39 inline forms, plus the new STORAGE_TEXEL_BUFFER
marking case; vk64MarkComputeWrites / vk64MarkDrawWrites /
vk64CopyRegionSplit byte-identical to the branch), spirvfix.mjs
(diff-of-diffs vs the stack delta differs ONLY in the REWRITES union
line: integ list + storage-texel-buffer-to-2d, keeping strip-dead-undefs),
vkwebgpu.mjs + wgsl-utils.mjs (the signature composition, below).

BRACE-REPAIR INCIDENT (9e9989e0f): the first merged build failed
compiling vk64bridge.cpp ("function definition is not allowed here").
The three-way merge had aligned vk64MarkDrawRootWrites final two
closing lines as the common tail of vk64CopyRegionSplit ending
(identical text, wrong owner), leaving DrawRootWrites unclosed in the
conflict region; the union resolution preserved the defect.
CopyRegionSplit itself was complete and byte-identical to the branch.
PROCESS LESSON: the merge-time extraction check that "verified"
DrawRootWrites was a FALSE POSITIVE -- its signature search matched
the FORWARD DECLARATION (line ~2218) in both files and compared the
wrong span; the build caught it. Extraction checks must search for
the definition (signature + " {"), never a bare signature string.
The repair restored the two closing lines; the repaired function is
byte-identical to its pre-merge integ text.

SIGNATURE COMPOSITION: integ storageSignatureFor (fmtinject, section
4.33) delegates format proof to resolveStorageVarFormat, whose fmtOf
historically proved formats only from kind-2 (image view) entries --
kind-6 entries returned null, which alone would have left the
meta-bind signature wall standing. Composition: resolveStorageVarFormat
takes an optional texelFormatOf callback; fmtOf proves kind 6 through
it (and the array-agreement loop admits kind 6 only with the callback);
vkwebgpu storageSignatureFor supplies it from F.bufferViews ->
V2_STORAGE_FORMAT[bv.format].spv, with the same element accuracy as
image views. Without the callback, behaviour is byte-identical to
pre-merge (the fmtinject guards 4-argument calls pin this).

SHIM RE-STAGE (hazard 4/5): stage-vkd3d-payloads.sh re-staged BOTH
web/runtime and dist/runtime wine64.zip (backups
/home/ubuntu/backups/winbox-vkd3d-payloads-20261029-071326). Both
archives now carry the merged shim sha256 ae96105b... (160,169 B)
under ALL THREE names (bw64vulkan.dll / vulkan-1.dll / winevulkan.dll),
and the canonical restaged d3d12 pair UNCHANGED (d3d12.dll a96b4e9d...,
517,717 B; d3d12core.dll 3d3bfe43..., 8,825,383 B -- provenance:
lane-restage build in wt-restage/tools/vkd3d/build.w64, section 4.37;
the branch in-commit pin moves were NOT inherited). Staged
web/runtime/wine64.zip 213,105,632 B sha256 1fc1e0ad... (member pins +
size + gate are the invariants; whole-archive hashes are not
reproducible across staging runs). Completeness gate: 0 failures,
1 pre-existing environmental skip. Dist rebuilt FROM the staged set:
stamp 9e9989e0f, wasm 2de1197c..., "Browser export verified".

REGISTER AS DELTA (hazard 2): the branch register (367 rows, old base)
was NOT taken. The stack true semantic delta (citation-normalized row
comparison) applied onto integ 373: +1 row (PE shim push-descriptor
exports family, IMPLEMENTED), the texel-buffer-view row and the
applyDescWrite texel row texts replaced with the branch served
wording, and the null-descriptor row clause-spliced (STORAGE_TEXEL_BUFFER
null views serve the zero storage texture since 4.43). Final register
(pipe-aware recount): 374 rows = 75 IMPLEMENTED / 66 REJECTED-LOUDLY /
233 NOT-A-GAP / 0 OPEN. Citations: the merge line shifts (vk64bridge
+116..+159 by region, vkwebgpu +35..+227, spirvfix +188, wgsl-utils
+10/+12) drifted 57 citations; re-pinned in 88415a446 (34 by the audit
--fix pass, 39 by hand via line-text mapping against the pre-merge
tree with definition-delta preservation, the 13 duplicate-scan rows
re-followed to their primaries, 2 catch-literal rows re-pinned to
their moved block, and the resolveStorageVarFormat row marker updated
to the composed signature). Final audit: 0 DRIFT / 0 MISSING
(228 MATCH / 14 MATCH-INHERITED / 97 UNCHECKABLE / 7 EXEMPT over 346
cited rows -- exactly the pre-merge coverage totals).

GUARD RE-PIN (cc40671d9): vk-storage-texel subtest 5 pinned the kind-6
dispatch inside vkwebgpu storageSignatureFor (the branch computed
formats inline there). In the composed shape the dispatch lives in
resolveStorageVarFormat fmtOf; the guard now pins both halves at their
composed locations, with the kind assert regex-tightened to the fmtOf
dispatch (the array-agreement loop also names kind 6 and would have
satisfied a loose pin -- caught by sabotage). Sabotage-verified both
directions: breaking the fmtOf kind-6 arm or the callback format
lookup each fail the guard; restores byte-identical. No product code
changed in this fallout.

R12S COMPOSITION NOTE (hazard 6, for the rawscan merge): metabindfix
variable-vetting scan (frontier finding 4, a function-body scan over
varIds) and rawscan consumer scan (finding 5) live in DIFFERENT
regions of storageTexelBuffersTo2D and must compose at the rawscan
merge -- neither overwrites the other. This merge brought metabindfix
version in cleanly.

SUITES on the merged tree: JS unit suite 326 passed / 0 failed
(runner --unit, BW_TEST_DIST=integ dist; 323 baseline + the stack 3
new guard files vk-storage-texel / vk-storage-texel-lifetime /
vk-push-descriptor-shim; vk-register-citations and
vk-storage-format-inject both pass); emulator native 647 OK /
0 FAILED; run-native.py 34 PASS / 0 FAIL (31 baseline + the stack 3
new blocks: storage texel push descriptor wires kind 6 marks and
reads back; partial-overlap copy splits at the GPU-write mark
boundaries; draw-time storage writes mark the view range).

GUEST GATE on the merged dist (uploading harness d3d12-pixels.mjs;
canonical pair + merged shim sha-verified in the harness provenance:
a96b4e9d... / 3d3bfe43... / ae96105b...; probe slot, one at a time;
adjudicated on guest verdicts, harness composites are the known
pixel-check artifacts):
(a) cs12 PASS -- RESULT 0: TEX OK bad=0, BUF OK bad=0, BUF2 OK bad=0
(SUMMARY buf=OK buf2=OK tex=OK). The meta-bind wall is closed on
integ; cs12 reaches its end state. Evidence:
test-results/dxvk-metabindmerge-cs12.
(b) uav12 PASS -- SUMMARY flag=TRUE sweep=ALL18 exec=ALL6, RESULT 0.
Evidence: test-results/dxvk-metabindmerge-uav12.
(c) rb12 PASS -- RESULT 0, all 8 variants OK (SUMMARY buf=OK clear=OK
upldtex=OK constps=OK delay=OK rdb2i=OK rdb2ip=OK rdb2is=OK).
Evidence: test-results/dxvk-metabindmerge-rb12.
(d) tri12 PASS -- RESULT 0. Evidence:
test-results/dxvk-metabindmerge-tri12.
(e) root12 PASS -- SUMMARY cbv=OK srv=OK uav=OK, RESULT 0. Evidence:
test-results/dxvk-metabindmerge-root12.

## 4.45 Compute readback epoch: the graphics re-stamp twin defect, fixed in the dispatch path (2026-10-28, cepoch lane)

**The defect (queued by lane-psb as the twin of its graphics fix).**
Section 4.30's compute serving marks GPU-written storage ranges at
dispatch RECORD time: `vk64MarkComputeWrites` replays the command
buffers recorded commands, resolves every storage-buffer descriptor
element, and stamps each mark with the memory's `writeEpoch` sampled at
that moment. The deferred-readback cleanliness proof
(`vk64FindGpuWrite` + `vk64MemRangeCleanSince`, consulted by the
`vkCmdCopyBuffer` handlers) defers a copy-out to a V2_BUFFER_READBACK
only if no write-log entry newer than the marks epoch touches the
range. lane-psb proved the graphics version of the trap: an initial-data
upload (`vkCmdUpdateBuffer`) recorded in the SAME submission as the
draw executes at submit time -- after the record-time snapshot -- and
its fresh epoch tick lands in the write log covering the very buffer
the draw writes, so the proof reads the upload as post-draw dirt and
refuses the deferral; the eager fallback then serves pre-draw data.
The compute path had the identical shape: a guest that uploads initial
data and dispatches in one command list, then copies out from a later
submission (the standard vkd3d readback pattern), would lose the GPU
output the same way. cs12's proven shapes upload in EARLIER submissions,
so the defect was latent, not hypothetical-by-construction only: the
marking, the upload tick (`execUpdateBuf` runs first in
vkQueueSubmits eager pass) and the proof were all read end-to-end in
`source/vulkan/vk64bridge.cpp` before the fix.

**The fix (same discipline as the graphics re-stamp, fail-closed).**
`vk64MarkComputeWrites` is now a wrapper over a prefix-bounded core,
`vk64MarkComputeWritesUpTo(cb, upto)`, that replays only the commands
before a given index (clamped); record-time callers (the three
vkCmdDispatch* handlers) are untouched and still mark the whole list.
At submit time, `serializeSubmitV2`s CMD_DISPATCH case -- after the two
loud-drop checks, before `emitRootDescs` and the V2_DISPATCH record --
calls the new `vk64RestampComputeWrites(st, dispatchIndex)`, which
re-runs the same prefix replay for that dispatch so every mark it
produced is refreshed in place (same (buffer, offset, size) key) with
the memory's CURRENT writeEpoch, exactly as `emitRootDescs` re-stamps
graphics root ranges. Record-time marking is kept (a same-list
dispatch-then-copy still defers), and the cleanliness proof is NOT
weakened: a range genuinely dirtied by an unrelated write after the
dispatch carries a newer epoch than the re-stamped mark and still
refuses the deferred readback.

**Proof.** New guard `vk-compute-epoch-restamp` (5 source pins in the
vk-psb-serving style): record path preserved at all three dispatch
handlers; the replay is prefix-bounded and clamped; the re-stamp sits
in the CMD_DISPATCH block after the loud-drop text and before the
V2_DISPATCH emit; it delegates to the shared replay and refreshes marks
in place; the proof strings (`wasMapped`, the CleanSince call) are
pinned unweakened. Sabotage-verified two ways -- deleting the re-stamp
call fails exactly the ordering pin, replacing the CleanSince check
with a stub fails exactly the proof pin -- with the bridge restored
byte-identical (sha256 re-verified) after each. run-native.py
end-to-end green (31 PASS blocks); emulator native suite 647 OK /
0 failed; JS suite 318 passed / 0 failed (317-file baseline + the new
guard). In-guest non-regression on this tree's dist (uploading harness,
restage pair a96b4e9d/3d3bfe43 + genuine d3dcompiler_47): `cs12:
VARIANT TEX OK (bad=0 first=(-1,-1) gotR=0)`, BUF/BUF2 FAIL bad=64 at
the meta-bind wall (lane-metabind, in flight), RESULT 1 for that reason
alone, dropReasons {} empty, translated 2 / rejected 0 -- the merged
trees exact signature. A same-list upload-then-dispatch in-guest proof
was NOT constructed: vkd3d-proton decides upload placement from the
D3D12 side and no raw-Vulkan guest probe exists, so the evidence for
the fixed shape is the sabotage-verified guard plus lane-psbs in-guest
proof of the identical pattern (root12 UAV readback); the first cs12
attempt additionally died in the section 4.27 desktop-init race under
box load (zero Vulkan lines logged; the app never started) and the
retry, queued behind another lanes build by the probe-slot system,
produced the signature above. Base note: the brief named b6d16b2fe;
integ had advanced to 8bf268112 (the vk64harness repair it anticipated
had landed), so the worktree was taken there. Citations: the +39/-4
bridge insertion drifted 28 register cites (+38/+41 by region); 15 rows
auto-fixed by the audit tool, 20 cites across the remaining 13 rows
hand-pinned, every target construct-verified at the audit's own hit
line; final audit 0 DRIFT / 0 MISSING (227 MATCH / 12 MATCH-INHERITED /
97 UNCHECKABLE / 7 EXEMPT, the baseline counts). Register: NO
disposition changes (370 = 71/66/233/0 stands) -- the defect was latent
and no row described it.

**Residue (named, out of scope).** On this base, compute
ROOT-descriptor dispatch writes are not marked at all:
`vk64MarkComputeWrites` covers only descriptor-set storage buffers
(the root-slot write census exists on lane-psbs branch). A guest
writing through a root descriptor in a compute dispatch and copying out
would miss the deferral entirely -- a distinct missing-mark gap, not
the epoch twin; it belongs with the psb merge or its own lane. Atomic
RMW and the other named compute rejections are unchanged.

### Merge outcome (integ, 2026-10-29) -- GATE COMPLETE

Merged as 59b1032db (parents c13c999a7 + 8edf68b9f) + citation
fallout a31c48b6c; this section renumbered 4.37 -> 4.45 at the merge.

COMPOSITION (the load-bearing judgment call). The lane's base
(8bf268112) predates the psb/metabind factoring of the marking path
into the shared vk64ReplayBoundState / vk64MarkBoundWrites walkers,
and git's 3-way merge interleaved the two shapes badly: the lane's
wrapper + vk64RestampComputeWrites landed inside the tail of
vk64CopyRegionSplit (the same identical-tail brace hazard as the
metabind merge). The bridge was therefore resolved by composing the
two proven texts in integ's factored shape, not by patching the
interleave: the prefix bound now lives in vk64MarkComputeWritesUpTo
(the lane's core, with its clamp) AND in the new
vk64ReplayBoundStateUpTo, the shared replay the core resolves
through; vk64ReplayBoundState delegates with the whole stream, so
the draw paths (vk64MarkDrawRootWrites / vk64MarkDrawWrites) are
unchanged by construction. The one-line record-time wrapper and
vk64RestampComputeWrites are the lane's verbatim text; the
serializer call is the lane's, placed after the two CMD_DISPATCH
loud-drop checks and before emitRootDescs / the V2_DISPATCH emit.
Verified by full-diff accounting against the pre-merge tree: the
merged bridge delta is exactly five hunks (registry comment, bounded
replay + delegate, bounded marking core + wrapper + restamp,
serializer call) and nothing else -- vk64CopyRegionSplit (both
64-bit copy-handler call sites), vk64MarkDrawRootWrites,
vk64MarkDrawWrites and vk64MarkBoundWrites are byte-identical to
pre-merge, checked by diff, not by name matching. The guard's pins
were additionally checked against the merged function bodies: the
'vk64MarkComputeWrites(cb);' call-site count is 3 on the merged
tree (the three vkCmdDispatch* traps), as the guard requires at
this merge.

STRUCTURE NOTE FOR THE NEXT MERGE (lane-crootmark): its
vk64MarkComputeRootWrites census call must land INSIDE
vk64MarkComputeWritesUpTo, taking that function's `upto` bound --
not in the record-time wrapper vk64MarkComputeWrites -- so a
submit-time re-stamp bounds the root census to the dispatch's own
prefix, the shape the crootmark lane closed structurally on its
branch. The vk32compute merge later raises the guard's call-site
pin from 3 to 4; this merge keeps it at 3.

REGISTER / CITATIONS: no disposition changes; integ register
unchanged at 374 = 75 IMPLEMENTED / 66 REJECTED-LOUDLY / 233
NOT-A-GAP / 0 OPEN (pipe-aware recount). Citations: the bridge
insertions drifted 28 rows; re-pinned via the audit tool's --fix
(15) plus 20 cites hand-pinned by the exact merge-delta bands
(+41 below the marking insertions, +44 below the serializer
insertion), each construct-verified at its new line; final audit
0 DRIFT / 0 MISSING (228 MATCH / 14 MATCH-INHERITED / 97
UNCHECKABLE / 7 EXEMPT over 346 cited rows -- the pre-merge
distribution exactly).

SUITES on the merged tree: JS unit suite 327 passed / 0 failed
(runner --unit, BW_TEST_DIST=integ dist; 326 baseline + the new
vk-compute-epoch-restamp guard); emulator native 647 OK / 0 FAILED;
run-native.py 34 PASS / 0 FAIL (31 baseline + the metabind stack's
3 blocks; cepoch adds no native blocks, as expected).

DIST: built from a31c48b6c (wasm 35dfc7c8...), "Browser export
verified", completeness gate 0 failures / 1 pre-existing
environmental skip. dist/runtime/wine64.zip member-verified: shim
ae96105b... under all three names (bw64vulkan.dll / vulkan-1.dll /
winevulkan.dll) and the canonical d3d12 pair a96b4e9d... /
3d3bfe43... unchanged.

GUEST GATE on the merged dist (uploading harness d3d12-pixels.mjs;
canonical pair + merged shim sha-verified in each run's provenance;
probe slot, one at a time; adjudicated on guest verdicts, harness
composites are the known pixel-check artifacts):
(a) cs12 PASS -- RESULT 0 with the full standing signature: TEX OK
bad=0, BUF OK bad=0, BUF2 OK bad=0 (SUMMARY buf=OK buf2=OK tex=OK).
Evidence: test-results/dxvk-cepochmerge-cs12.
(b) uav12 PASS -- SUMMARY flag=TRUE sweep=ALL18 exec=ALL6,
RESULT 0. Evidence: test-results/dxvk-cepochmerge-uav12.
(c) tri12 PASS -- RESULT 0, 10 frames cleared+submitted (composite
passed:false with 0 chunks captured = the known offscreen capture
artifact). Evidence: test-results/dxvk-cepochmerge-tri12.

## 4.46 Compute root-descriptor write marks: the missing-mark gap lane-cepoch named is closed (2026-10-29, lane-crootmark)

Base: psb tip 60cd0f215. The gap (§4.45 residue from
lane-cepoch, restated in its commit): vk64MarkComputeWrites marked only
DESCRIPTOR-SET storage-buffer ranges written by a dispatch. A compute
dispatch writing through a ROOT descriptor (a root UAV / storage-buffer
VA in the push block, served via the R15b re-rooting of §4.39) left no
GPU-write mark at all.

CONSEQUENCE, pinned from the code before fixing: correctness exposure,
not merely the slow path. Both copy handlers (vkCmdCopyBuffer,
vkCmdCopyBuffer2/KHR) defer a copy-out to the submit-time page readback
ONLY when vk64FindGpuWrite proves a mark; with no mark they take the
eager record-time memcpy from guest memory. A dispatch's results exist
only page-side until a deferred readback expands them, so the eager
copy silently serves the PRE-dispatch bytes, same-list dispatch->copy
and cross-submission alike. No diagnostic fires.

FIX (source/vulkan/vk64bridge.cpp, +65/-0 on this base including the
guard-driven additions below): vk64MarkComputeRootWrites, a verbatim
mirror of the graphics census vk64MarkDrawRootWrites (§4.39) -- same
replay of the recorded command stream, same push-block merge, same
VA->(buffer, offset) resolution through g_devVaRegistry (upper_bound
walk, buffer registered by vkGetBufferDeviceAddress), same bounds
checks, same mark key (buf, buf->memOff + boff, buf->size - boff,
buf->mem, mem->writeEpoch) into the same g_gpuWriteMarks store the
cleanliness proof consults. Exactly two divergences, both commented:
the predicate (the bound pipeline must be a compute pipeline,
!pipe || !pipe->cs -- a dispatch never executes a graphics pipeline)
and the invocation point: the census is called at the END of
vk64MarkComputeWrites, inside the marking core, from all three
dispatch traps' existing record-time call. Unresolvable root state
fails closed in the marking sense: a zero VA, an unknown VA and an
out-of-range VA each leave the slot unmarked, never guessed (an
unmarked range keeps the copy handlers' pre-existing eager behaviour;
the census never invents a range). Because the marks key identically,
the submit-time re-stamp that already served dispatches refreshes them
in place: emitRootDescs re-stamps the CMD_DISPATCH case's writable
root slots with the memory's current write epoch during the
serializer's in-order walk (prefix-exact by construction).

PREFIX BOUND (merge-critical): the census takes the caller's prefix
bound, vk64MarkComputeRootWrites(CmdBuf* cb, size_t upto), clamped to
the recorded list; the record-time core passes cb->cmds.size(). On
this base there is no bounded re-run of the core, so the parameter is
whole-list at every call. lane-cepoch (merging BEFORE this lane)
refactors the core into vk64MarkComputeWritesUpTo(cb, upto) +
vk64RestampComputeWrites, re-running the core at submit up to each
dispatch. MERGE NOTE: the merged tree must keep this lane's census
call inside vk64MarkComputeWritesUpTo and pass its upto argument
through (not cb->cmds.size()); the signature already takes the bound,
so the change is the one call-site argument. Without it, a submit
re-stamp would resolve end-of-list root state for every dispatch and
refresh the wrong ranges in multi-dispatch lists.

OFFSET-SPACE PIN (frontier-review follow-up, routed by the
coordinator): the metabind review flagged that vk64MarkComputeWrites
passes buf->memOff + bv->offset -- a memory-ABSOLUTE offset -- beside
the buffer handle, and that the existing native test used memOff=0 and
could not distinguish the spaces. Pinned from the code: the registry
is memory-absolute on BOTH sides. vk64FindGpuWrite's parameter is
named absOff; its containment test compares it directly against
GpuWriteMark.off, and the cleanliness leg consumes it as a memory
offset (vk64MemRangeCleanSince). Every producer passes
memOff + relative: the descriptor-set core (buf->memOff + off), both
root censuses (buf->memOff + boff), and emitRootDescs' re-stamp
(r.absOff, itself defined as buf->memOff + boff). Every consumer
passes the same space: both copy handlers query
src->memOff + r.srcOffset. The buffer handle rides along because the
registry is keyed per (buffer, absolute range); two buffers sharing
one allocation at different memOffs occupy disjoint absolute ranges.
VERDICT: the suspicion is REFUTED -- no producer or consumer was in
the wrong space, and no offset fix was needed. The genuine gap was
that no test pinned the space; see the guards. Two sibling flags from
the same review are likewise refuted on this base: (a) the mark does
NOT rely on an unresolved VK_WHOLE_SIZE -- the core resolves
b.range == 0 || b.range == ~0ULL against the buffer size (clamped by
the element offset) before marking; (b) no stale-object diagnostic
fires per dispatch for a null descriptor -- objOf logs only for
nonzero ids (!o && id), and the marking core performs no BufferView
lookup at all on this base.

FRONTIER REVIEW OF THIS DIFF (Claude Code, Opus, high effort;
advice verified against the code, per standing practice): (1)
"graphics binds after the compute bind silently drop the marks" --
REFUTED as a defect in this codebase. The bridge deliberately models
a single state stream (the vkCmdBindPipeline trap ignores the bind
point; the set-bind trap's comment states the model: guests rebind
when they switch pipeline kinds). The serializer resolves the same
last-bind state and LOUDLY drops a dispatch whose active pipeline is
not compute (vk64Unsupported "compute-dispatch"), so in the
interleaved sequence the dispatch produces no writes at all -- there
is nothing to mark, and the core's replay with its !pipe->cs return
matches the execution model exactly. A per-bind-point census would
mark writes for dispatches that never execute. (2) "an early return
may skip the census for root-only pipelines" -- the core's only early
return is the pipe check above; a root-only dispatch (no sets bound)
reaches the census, proven behaviourally by the native guard. (3)
"the census may not respect the submit-time prefix bound" -- REAL as
a post-merge hazard; closed structurally by the upto parameter
above. (4) "fail-closed wording" -- the header scopes the term to the
marking decision and states the stale-copy consequence in the same
paragraph; no change. (5) "deduplicate the two censuses" -- declined:
the draw census is shipped §4.39 code, the mirror is deliberate, and
the JS guard pins the two bodies' shared key line occurring exactly
twice.

GUARDS. Native (tools/vkd3d/tests/vk64_native_test.cpp): the block
"compute root-descriptor dispatch writes are marked, re-stamped at
submit, and later dirt still refuses deferral" -- a hand-built 96-word
compute module (push-block uint64 -> OpLoad -> OpBitcast to a
PhysicalStorageBuffer pointer -> OpStore) censused rootSlots ==
rootWritableSlots == {0}; target buffer in fresh memory, VA
registered through the real vkGetBufferDeviceAddress trap; record
bind + vkCmdPushConstants (VA = devVa+64) + dispatch; the mark is
found containing [64,192) at the memory's writeEpoch, [0,32) is
unmarked; vk64TouchMem (the same-submission upload stand-in) poisons
the record-time stamp; serializeSubmitV2 emits ROOT_DESCS (0x1D) +
DISPATCH (0x1F), moves no "compute-dispatch" unsupported count, and
the find succeeds again at the CURRENT epoch; a second touch refuses
again. Offset-space sub-cases appended in the same block: (A) the
root census repeated on a buffer bound at memOff 8192 -- the mark
lands at 8192+64, the buffer-relative [64,192) stays unmarked, the
span ahead of the VA stays unmarked, and the touch/serialize cycle
re-stamps the ABSOLUTE key in place; (B) the descriptor-set census on
a storage element (bufOff 128, range 512) of a buffer at memOff 8192,
bound via a recorded CMD_BIND_SETS -- the mark lands at 8192+128 for
exactly 512 bytes, the relative range stays unmarked, and the range
past the element stays unmarked. JS (web/tests/suite/unit/
vk-compute-root-marks.test.mjs) x5: the core calls the census after
the set marking; the census mirrors the draw census (registry
resolution, skips, the shared key line occurring exactly 2x in the
bridge); the prefix-bound pins (clamp + bounded loop); the
submit-time re-stamp reachability (emitRootDescs(csPipe) + the
absolute re-stamp line); the proof pins (wasMapped, clean-since, both
copy-handler gates) and the native block's presence incl. its offset
asserts. SABOTAGE x3, each restored byte-identical (bridge sha256
7c504173...): removing the census call re-trips the JS guard (4/5)
and aborts the native run at the block's first root-mark assert;
making the census key buffer-relative aborts at sub-case (A)'s
absolute assert -- AFTER passing the memOff=0 case, demonstrating
exactly the blindness the review flagged; making the core set-mark
buffer-relative aborts at sub-case (B)'s assert.

HARNESS STATE: run-native.py aborts on this base at the known stale
assert vk64_native_test.cpp:499 (the test's 31-entry format table vs
the bridge's wider supported set; the lane-vk64harness repair is not
in this base) -- verified to abort IDENTICALLY on pristine 60cd0f215
and on this tree, compile clean in both. The block was therefore
executed out-of-tree against lane-vk64harness's repaired test file
(766a5c7c4) carrying this lane's block, compiled with run-native.py's
exact recipe against this tree's bridge and shim: 32 PASS blocks
(the repaired suite's 31 + this one), exit code 0.

SUITES: JS suite on the lane dist (bw64-build "Browser export
verified") 317 files passed / 0 failed -- the psb base's 316 plus the
new guard file, zero new failures. Emulator native 647 tests / 0
failed (vk64bridge.cpp is not in that build graph). Citation audit
222 MATCH / 13 MATCH-INHERITED / 98 UNCHECKABLE / 7 EXEMPT, 0 DRIFT /
0 MISSING (the +4-line guard-driven delta fell inside tolerance; the
register guard passes 4/4).

GUEST: cs12 on the lane dist via the uploading harness
(web/tests/d3d12-pixels.mjs, slot-serialized), the guest pair taken
from this tree's own pin-verified wine64.zip via the harness's
D3D12_PATH/D3D12CORE_PATH overrides (d3d12.dll d77fdad5...,
d3d12core.dll d2bd847a... -- the pre-restage pair this base pins) and
the genuine d3dcompiler_47.dll af7b99be...: TEX FAIL bad=16, BUF FAIL
bad=64, BUF2 FAIL bad=64, RESULT 1 -- the psb-base signature EXACTLY
(TEX at this lineage's format wall, BUF/BUF2 at the meta-bind wall;
neither is in this lane's lineage and neither moved). No guest
compute-root probe exists -- root12's family is graphics-only -- so
the guest evidence rests on the native guard plus §4.39's in-guest
proof of the identical graphics pattern (the lane-cepoch precedent),
stated plainly.

REGISTER: no disposition change. Counts on this base stay 367 =
68 IMPLEMENTED / 66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN. The
vulkan.md header gains the dated closure note and the
compute-dispatch row's marking sentence now covers root-descriptor
ranges (naming vk64MarkComputeRootWrites) with the two guard files in
its Regression list.

RESIDUE: (a) 32-bit dispatches never record a compute stage and drop
loudly at submit -- nothing exists to mark for, unchanged. (b)
Root-pointer atomics remain classifier-rejected by name upstream, so
no mark is owed for them. (c) The draw census keeps its unbounded
replay: no bounded core re-run exists for graphics (its re-stamp path
is emitRootDescs only), so it has no prefix to honour. (d) The
multi-dispatch root re-stamp interplay is exercised by the native
guard at the registry level only; no guest probe constructs it
(vkd3d-proton controls the guest's command stream).

### Merge outcome (integ, 2026-10-29) -- GATE COMPLETE

Merged as 35c8ec3fd (parents aab45dc08 + 8da8eb2f1); the lane
section above is renumbered 4.33 -> 4.46. Fallout: caad76ee8
(citation re-pins) and dc2ea39e5 (guard re-pin). Cross-references
in the lane text were renumbered to this tree's numbering: the psb
graphics census the lane cites as section 4.32 (its number on the
lane's base) is section 4.39 here (four places), and the
section 4.37-on-branch residue reference is lane-cepoch's merged
section 4.45. The vulkan.md header note and the compute-dispatch
row's new sentence say "inside the compute marking core
(vk64MarkComputeWritesUpTo)" where the lane text said "inside
vk64MarkComputeWrites", for the composition reason below.

COMPOSITION (the upto rider, landed exactly as this section's own
MERGE NOTE prescribes): the 3-way auto-merge interleaved badly
around the marking functions -- its bridge conflict spanned the
factored core through vk64CopyRegionSplit's tail, the
identical-tail hazard for the third merge running -- so the
interleave was discarded and the proven texts composed by hand in
integ's factored shape. The merged bridge delta against the
pre-merge tree is exactly three insertions, +68/-0, verified by
full-diff accounting (three hunks, zero deletions):
vk64CopyRegionSplit (definition and both call sites),
vk64MarkDrawRootWrites, vk64MarkDrawWrites, vk64MarkBoundWrites,
vk64ReplayBoundStateUpTo and vk64RestampComputeWrites are
byte-identical to pre-merge. (1) The forward declaration sits
after vk64FindGpuWriteOverlap (on the lane's base the declaration
followed vk64FindGpuWrite directly; the metabindfix overlap
variant intervenes here). (2) The census definition sits
immediately after vk64MarkDrawRootWrites, as on the lane, and is
byte-identical to the lane's 54 lines except one comment phrase
naming the composed core (vk64MarkComputeWritesUpTo for
vk64MarkComputeWrites). (3) The census call lands INSIDE
vk64MarkComputeWritesUpTo, at the end of the bounded core after
vk64MarkBoundWrites, passing the core's own upto through --
vk64MarkComputeRootWrites(cb, upto) -- not in the record-time
wrapper and not with cb->cmds.size(). A submit-time re-stamp
(vk64RestampComputeWrites) therefore re-runs the census bounded
to each dispatch and never resolves root state recorded after
that dispatch. Placement equivalence with the lane's shape is
exact, not approximate: on the lane the call also sat behind the
core's pipe check, and the bounded core's early return fires
only when !pipe || !pipe->cs, under which the census's own
predicate returns without marking. The two commented divergences
(the !pipe || !pipe->cs predicate; the invocation point inside
the marking core) are preserved. The 'vk64MarkComputeWrites(cb);'
call-site count stays 3 on the merged tree.

GUARDS: the native root-mark block auto-merged cleanly and is
byte-identical to the lane's block (+187/-0 in
vk64_native_test.cpp); run-native.py runs 35 PASS blocks (34 +
this one) including "compute root-descriptor dispatch writes are
marked, re-stamped at submit, and later dirt still refuses
deferral". The JS guard vk-compute-root-marks.test.mjs passes 5/5
after a re-pin to the composed shape (fallout dc2ea39e5): test 1
now pins the census call inside vk64MarkComputeWritesUpTo with
its upto argument after vk64MarkBoundWrites, the wrapper's
whole-list delegation, and the re-stamp's bounded re-run; test 4
pins the mark-gated vk64CopyRegionSplit deferral gate both copy
handlers route through (the pre-metabindfix direct
vk64FindGpuWrite call shapes the branch pinned no longer exist
on this tree). Sabotage-verified: removing the census call fails
exactly test 1 (4/5, mirroring the lane's own call-removal
sabotage), bridge restored byte-identical (sha256 f2b08400...).

REGISTER / CITATIONS: no delta -- 374 = 75 IMPLEMENTED /
66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN (pipe-aware recount;
the compute-dispatch row's marking sentence and Regression list
gained the root-descriptor coverage, its disposition unchanged,
and the header gained the dated closure note). The insertions
drifted 29 cited rows; all bridge citations in register rows
were re-pinned by the exact merge-delta band map (+2 below the
declaration, +57 below the census definition, +68 below the
call; 58 rows shifted including duplicate-scan dependents),
restoring the audit to exactly its pre-merge distribution:
0 DRIFT / 0 MISSING (228 MATCH / 14 MATCH-INHERITED /
97 UNCHECKABLE / 7 EXEMPT over 346 cited rows).

SUITES (merged tree): JS unit suite 328 passed / 0 failed
(327 baseline + the new guard file; BW_TEST_DIST=integ dist);
emulator native 647 OK / 0 FAILED; run-native.py 35 PASS /
0 FAIL. Dist built from dc2ea39e5 (wasm 173d917b...), "Browser
export verified"; completeness gate 0 failures / 1 pre-existing
environmental skip; dist/runtime/wine64.zip member-verified:
shim ae96105b... under all three names (bw64vulkan.dll /
vulkan-1.dll / winevulkan.dll), canonical d3d12 pair unchanged
(a96b4e9d... / 3d3bfe43...).

GUEST GATE (merged dist, uploading harness, probe slot,
canonical pair + shim sha-verified in each run's provenance;
adjudicated on guest verdicts): (a) cs12 RESULT 0, full standing
signature -- TEX OK bad=0, BUF OK bad=0, BUF2 OK bad=0
(test-results/dxvk-crootmarkmerge-cs12); (b) uav12 exec=ALL6,
RESULT 0 (test-results/dxvk-crootmarkmerge-uav12); (c) tri12
RESULT 0 (test-results/dxvk-crootmarkmerge-tri12). As the lane
stated plainly: no guest compute-root probe exists, so the gate
proves no regression; the marking proof is the native guard plus
section 4.39's in-guest proof of the identical graphics pattern.

## 4.47 Operand-aware use scan in the R15 classifiers: literal operands are not id uses (2026-10-28, opscan lane)

**The defect (the documented §4.32 residue).** Servability of a descriptor-heap runtime array is decided by spirvfix's `analyzeDescriptorArrays`, mirrored instruction-for-instruction by the bridge audit's `r15::analyzeArrays`; both walk pointer uses through a shared helper pair (`r15PointerUses` / `r15::pointerUses`). All four scans collected a variable's (or chain pointer's) uses with the same raw idiom: any instruction word equal to the sought id counted as a use, with the hit position distinguishing the base/pointer slot from "anywhere else". SPIR-V operands are positionally typed, so a LITERAL word — a composite/vector index, a constant, an alignment parameter, a branch weight, an image-operand mask — that happens to equal a small id is misread as a pointer use and false-declines a servable array. §4.32 observed it on an id-renumbered synthetic at var %3; captured modules use ids like %14, so it had not bitten in production — a correctness landmine in the classifier pair the whole D3D12 serving stack depends on staying in lockstep.

**The fixture.** `testdata/spirv/d3d12-ps-uav-heap-array-smallid.spv` is the captured uav12 PS module with ids 3 and 14 swapped (word-exact scripted renumber, 8 words changed; the only words equal to 3 in literal positions are the six pinned exclusions: the OpMemoryModel operand, two OpTypePointer storage classes, the OpVariable storage class, an OpConstant value, and an OpCompositeExtract index). The array variable becomes %3, colliding with the module's own OpCompositeExtract literal index 3. spirv-val clean. Pre-fix the JS classifier declines it: servable=false, reason 'array variable use op81'. Control: the identical renumber with that one literal changed 3→2 classifies servable under the pre-fix code — the renumber is faithful and the literal is the sole cause.

**The fix.** One helper per side — `r15IdUseIndex` (spirvfix) and `findIdUse` (bridge r15) — returns the index of the first word that is an ID OPERAND equal to the sought id, else −1; all four call sites route through it and the downstream hit semantics (hit === first-operand index ⇔ base/pointer slot) are unchanged. The walk is operand-aware BY OPCODE, per the SPIR-V grammar: the default rule (every tail word is an id) covers the operand shapes whose operands are all ids; the special layouts are OpLine (file id only), OpExtInst (set id, literal instruction number, id operands), OpVariable (literal storage class, initializer id), OpLoad / OpStore / OpCopyMemory / OpCopyMemorySized (id operands, then MemoryAccess groups walked by mask bit: Aligned's parameter is a literal, MakePointerAvailable / MakePointerVisible carry a scope id, Volatile / Nontemporal / NonPrivatePointer carry none; words past a group still scan as ids), OpArrayLength (structure id, literal member index), OpVectorShuffle and OpCompositeExtract / OpCompositeInsert (literal index tails), OpGenericCastToPtrExplicit (literal storage class), OpLoopMerge / OpSelectionMerge (literal control mask and parameters), OpBranchConditional (literal branch weights), OpSwitch ((literal, label) pairs; literal width resolved from the selector's OpTypeInt — one word at ≤32-bit, two at 64-bit; an unresolvable width keeps the historical whole-tail scan over the pairs region), and the image ops with optional ImageOperands (a fixed leading id count per opcode, one mask literal word, then id-only parameters — every ImageOperands parameter is itself an id).

**Evidence.** Post-fix the smallid module classifies servable with signature push@0+0, and `applySpirvFix` specializes it with the identical rewrite set and clone name (`bw_arr_g5_b0_p0_a0`) as its unmutated twin. The bridge mirror agrees end-to-end at vkCreateShaderModule (run-native): the real smallid module is admitted (VK_SUCCESS, the `spirv-capability:RuntimeDescriptorArray` counter unmoved), and a hand-built minimal module whose access chain %3 collides with an OpCompositeExtract literal 3 is admitted too — that module isolates the pointerUses scan, its array-variable scan having no collision. Sabotage, all four call sites independently: reverting spirvfix's analyzeDescriptorArrays scan fails the two smallid tests; reverting r15PointerUses fails the chain-literal splice test; reverting the bridge's analyzeArrays fails the smallid native guard (the minimal-module guard still passes); reverting the bridge's pointerUses fails the minimal-module guard. Source pins in spirvfix-r15 assert both sides route through the helpers and the raw loops are gone; a 41-case table pins r15IdUseIndex's per-opcode layouts directly.

**Suites.** spirvfix-r15 27/27 (22 + 5 new). JS suite: the final full run reads 313 passed / 4 failed, where the failure set is provenance-artifacts (the branch baseline's sole failure — the documented fresh-worktree missing-dist artifact) plus fork-fd-snapshot, vk-gs-passthrough and xwire-input2, all three of which pass standalone on this tree and passed in both earlier full runs (load flakes under parallel lane builds; none touches the R15 classifiers). The two failures the change itself caused — vk-register-citations (line drift; re-pinned below) and no-silent-failure (reviewed baseline; below) — pass in the final run, so the change adds zero new failures. run-native.py end-to-end green, 33 PASS blocks (31 at base + the 2 new guards). Corpus gate 32/37 (32/36 at base): the added fixture specializes, then stops at the same by-design naga UnsupportedCapability(StorageImageReadWithoutFormat) as its unmutated twin — the deliberate fixture-addition change. Register: no disposition changes — the fix removes a false decline no register row counted. Citations: the insertions shifted vk64bridge.cpp lines below the r15 namespace, so the lane re-pinned the drifted vulkan.md citations (31 by the audit's --fix, 18 hand-pinned construct-by-construct, each content-verified against its pre-change line) — audit 228 MATCH / 12 MATCH-INHERITED / 96 UNCHECKABLE / 7 EXEMPT, 0 DRIFT / 0 MISSING (base 227/12/97/7; one row moved UNCHECKABLE → MATCH under the re-pins — coverage grew, no disposition changed). The no-silent-failure guard's reviewed baseline for vk64bridge.cpp unloggedDefault moves 20 → 22: findIdUse's two `default:` arms are operand-classification fallbacks (they select the historical all-id scan), not silent failure sites, the same reviewed-bump pattern as §4.35's fallout fix. Base note: the lane branched from the integ tip 8bf268112 (the brief's b6d16b2fe is an ancestor; the tip adds the vk64harness repair this lane's bridge verification runs on).

**Residue (named).** (1) Extension instructions whose payloads carry literals inside id-shaped operand slots — debug-info OpExtInst bodies (OpenCL.DebugInfo.100 / NonSemantic.Shader.DebugInfo.*) and vendor image/atomic extension ops — keep the historical scan and therefore decline-on-collision: fail-closed, unchanged from before. (2) An OpSwitch whose selector width cannot be resolved keeps the historical scan over its pairs region. (3) The orphan sweep's type-liveness scan answers a different question (which TYPE declarations survive specialization) and is untouched; its known sibling imprecision stays with the previously queued R5 item, out of this lane's scope.

### Merge outcome (integ, 2026-10-29) -- GATE COMPLETE

Merged as 6939c8c9e (parents 9dc56a62f + 74d38f6b3); the lane
section above is renumbered 4.37 -> 4.47. Fallout: 056e13097
(citation re-pins) and affc3c0f9 (comprehensive citation
band-map).

COMPOSITION: conflicts in five files. vulkan.md took integ's
version wholesale (the lane's re-pins were computed against its
own base's line numbers) and was re-pinned against the merged
tree in fallout. In vk64bridge.cpp the lane's findIdUse helper
landed byte-identical to the lane's text (verified by extraction
diff), followed by integ's r15::Uses line (psb's storeWrites /
atomicWrites fields kept). In spirvfix.mjs both sides' insertions
before r15PointerUses were kept verbatim -- integ's psb helpers
(r15BaseLoadId / r15Vec2Half / r15ClassifyVec2Addr) first, then
the lane's R15_IMAGE_OPS_ID_PREFIX + r15IdUseIndex -- with one
repair: git had shared r15ClassifyVec2Addr's closing brace as
common context, so each parent's region excluded it and the
first composition did not parse; the brace was restored (the
identical-tail trap in miniature, fourth merge running). The
test file is a header-comment union plus a test-append union:
22 base + 7 integ + 5 lane = 34 tests, all passing; the one
base-test name absent from the union (the R15b root-store
fail-closed test) was replaced by psb commit 60cd0f215 itself
with its served-not-rejected successor, verified by pickaxe --
integ's evolution stands. Full-diff accounting: the merged
bridge delta is the lane's exact 3 hunks (+136: helper, the
pointerUses call site, the analyzeArrays call site); the merged
spirvfix delta is the lane's exact 3 hunks (+150) modulo the
one-brace seam repair; vk64_native_test.cpp auto-merged as +101
pure insertions (the lane's 2 guard blocks).

MIRROR PARITY (the point of the merge), verified by extracting
both helpers and comparing case-by-case: the outer opcode
switches are identical (cases 8, 12, 59, 61, 62, 63, 64, 68, 79,
81, 82, 123, 246, 247, 250, 251); the image-op prefix mappings
are identical (26 entries, same three width classes); the
MemoryAccess mask bits (2/8/16) and the OpSwitch selector-width
rule match; all four call sites (spirvfix r15PointerUses +
analyzeDescriptorArrays, bridge r15::pointerUses +
r15::analyzeArrays) walk id-typed operands through the helper.
r15IdUseIndex / findIdUse keep their own names -- rawscan's
superset idOperandUseIndex is designed to coexist with them at
its later merge.

KNOWN-SILENT DECISION (explicit): the lane's reviewed baseline
bump for vk64bridge.cpp unloggedDefault (20 -> 22, for the
helper's two classification-fallback default branches) was NOT
taken. Following the rawscan lane's restructure preference,
findIdUse was restructured semantics-identically instead: the
outer switch's default block became a post-switch fall-through
(every case returns, so the fall-through is exactly the old
default body) and the inner image-op switch became a static
table, kImageOpPrefix, entry-for-entry identical to the JS
R15_IMAGE_OPS_ID_PREFIX. known-silent.json is unchanged from
pre-merge and no-silent-failure passes at full strength with
vk64bridge.cpp unloggedDefault at exactly 20.

CORPUS GATE: gate.mjs 34/39 post-merge against the pre-merge
baseline 34/38, captured before merging. The only delta is the
lane's added fixture (d3d12-ps-uav-heap-array-smallid.spv), which
applies descriptor-array-specialize and stops at the same
by-design naga bucket as its unmutated twin; all 38 pre-existing
modules' outputs are byte-identical.

CITATIONS AND REGISTER: final audit 228 MATCH / 14
MATCH-INHERITED / 97 UNCHECKABLE / 7 EXEMPT over 346 cited rows,
0 DRIFT / 0 MISSING -- the identical distribution AND identical
per-row verdicts to pre-merge (verified by a per-row verdict
diff against a scratch worktree at the pre-merge tip). Path:
audit --fix repaired 31; 28 more were hand-pinned by the exact
merge-delta band map (+127 below the bridge hunks, each landing
line content-verified); row 37's cites were band-mapped after
the shift made an unrelated default: fall inside the checker
tolerance of its unchanged cite (a coincidental UNCHECKABLE ->
MATCH, corrected back); a final occurrence-aligned pass moved
94 further citations the checker tolerates or cannot see
(including the row-403 Route A cites, which pointed into the
inserted helper table) -- all 163 citations to the two changed
files now sit at their exact post-merge lines. Register
unchanged: 374 = 75 IMPLEMENTED / 66 REJECTED-LOUDLY / 233
NOT-A-GAP / 0 OPEN (pipe-aware recount, identical pre/post).

BUILD AND SUITES: bw64-build.sh -- Browser export verified,
completeness gate 0 failures. dist/runtime/wine64.zip
(213,105,632 B) carries the shim ae96105b (160,169 B) under
bw64vulkan.dll, vulkan-1.dll and winevulkan.dll, and the
canonical pair a96b4e9d (d3d12.dll) / 3d3bfe43 (d3d12core.dll).
JS unit suite 328 passed / 0 failed (BW_TEST_DIST = the merged
dist; the lane's tests expand existing spirvfix files, so the
file total is unchanged -- the r15 file itself runs 34/34);
emulator native 647 OK / 0 FAILED; run-native 37 PASS (the 35
baseline blocks plus the lane's two: "a CompositeExtract literal
equal to the chain id is not a pointer escape" and "the small-id
heap-array module is admitted").

GUEST GATE (uploading harness, acquireProbeSlot, canonical pair
and shim sha-verified in every run's provenance): (a) cs12
RESULT 0 with the full standing signature -- VARIANT TEX OK
(bad=0), VARIANT BUF OK (bad=0), VARIANT BUF2 OK (bad=0)
(test-results/dxvk-opscanmerge-cs12); (b) uav12 SUMMARY
flag=TRUE sweep=ALL18 exec=ALL6, RESULT 0
(test-results/dxvk-opscanmerge-uav12); (c) tri12 RESULT 0
(test-results/dxvk-opscanmerge-tri12). The harness composite
passed:false in all three runs is the standing capture artifact;
the verdicts above are the guests' own, with guest exit status 0.

## 4.48 condStartWaitTime audit: KThread::sleep keeps its original deadline across wakes (2026-10-28, condwait lane)

The audit (full text: docs/reports/cond-start-wait-time-audit-20261028.md)
cleared the suspect and convicted its neighbour. condStartWaitTime
(include/kthread.h) anchors exactly two waits -- internal_poll_impl
(kpoll.cpp) and KThread::sleep (kthread.cpp) -- plus the 32-bit
signal frame's save/restore and one debug-dump read. The missing
-K_CONTINUE reset is CORRECT, proven: both dispatchers leave guest
EIP on the syscall instruction and write no result-register
sentinel, so after onExitSignal restores the saved anchor the same
syscall re-executes and must resume its ORIGINAL deadline
(SA_RESTART continuation); resetting on -K_CONTINUE would restart
the full timeout after every signal. A stale anchor's damage is
bounded: it can shorten the NEXT wait at most once. The live
defect was in sleep only: it subtracted TOTAL elapsed time from an
ALREADY-REDUCED timeout on every in-invocation wake (ms -= diff) --
the exact pattern fc6818ec1 fixed in internal_poll_impl, a pass
that touched only kpoll.cpp. Masked single-threaded; live in the
multi-threaded shipping config (tgkill kicks and KThread::signal's
wait branch wake the sleep condition WITHOUT setting startSignal,
and sleep's loop had no pending-signal check to exit on). Measured:
kicks every ~100 ms cut a 2000 ms sleep to 608 ms. Fix (sleep
only): never mutate ms; recompute diff from the anchor each pass;
terminal test diff >= ms; the condition wait takes ms - diff.
Guard: testCondStartWaitTime (source/test/process/testCondWait.cpp,
registered in BOTH TEST_ENTRIES arrays) -- the sleep-deadline case
fails pre-fix (608 ms) and passes post-fix (lane sabotage loop:
revert -> 606 ms FAIL, re-apply -> PASS); poll deadline pin;
-K_CONTINUE pin; leaked-anchor bound. One item deliberately
unchanged: sleep's loop still has no pending-signal check -- a
latency property, not a deadline defect. No register row covers
this field.

### Merge outcome (integ, 2026-10-29) -- GATE COMPLETE

Merged as b728bcee4 (parents a3406666c + 45749655c), clean ort
merge, no conflicts. This section is new in the campaign report
(the lane's record is its audit doc); numbered 4.48, no renumber
needed. COMPOSITION: the merged delta for source/kernel/kthread.cpp
and source/test/testMain.cpp is byte-identical to the lane delta
(verified by diffing the two diffs); the guard registration
survives in BOTH TEST_ENTRIES arrays on the merged tree
(testMain.cpp:146 and :801, declaration at :63), and sleep's loop
carries no added pending-signal check -- the deliberately-unchanged
item is unchanged.

CITATIONS AND REGISTER: citation audit 228 MATCH / 14
MATCH-INHERITED / 97 UNCHECKABLE / 7 EXEMPT over 346 cited rows,
0 DRIFT / 0 MISSING -- the identical distribution to pre-merge,
with no re-pins (the merge touches no cited file). Register
unchanged: no register file is in the merge delta (vulkan.md
stands at 374 = 75 IMPLEMENTED / 66 REJECTED-LOUDLY / 233
NOT-A-GAP / 0 OPEN), and kernel.md carries no condStartWaitTime
row, as the lane stated.

BUILD AND SUITES: bw64-build.sh -- Browser export verified,
completeness gate 0 failures, 1 skip (pre-existing environmental).
dist stamp b728bcee4, wasm d48af057...; dist/runtime/wine64.zip
carries the shim ae96105b (160,169 B) under bw64vulkan.dll,
vulkan-1.dll and winevulkan.dll, and the canonical pair a96b4e9d
(d3d12.dll) / 3d3bfe43 (d3d12core.dll). Emulator native suite
648 OK / 0 FAILED in 216 s (647 baseline + the lane's guard),
and the guard provably ran: "Test condStartWaitTime deadline
accounting (sleep/poll) ... OK" in the suite output. JS unit
suite 328 passed / 0 failed (BW_TEST_DIST = the merged dist);
run-native 37 PASS / 0 FAIL.

GUEST GATE (light, per the kernel-side scope; uploading harness,
acquireProbeSlot via the harness, canonical pair and shim
sha-verified in every run's provenance): (a) tri12 RESULT 0
(test-results/dxvk-condwaitmerge-tri12); (b) cs12 RESULT 0 with
the full standing signature -- SUMMARY buf=OK buf2=OK tex=OK
(test-results/dxvk-condwaitmerge-cs12). The harness composite
passed:false in both runs is the standing capture artifact; the
verdicts above are the guests' own, with guest exit status 0.

## 4.49 32-bit sysroot regeneration: the img386 producer path is rebuilt and scripted (2026-10-28, sysroot32 lane)

The one producer path the post-step elimination pass could not
re-run: apply-runtime-poststeps.sh's first step reads its payload
from /home/ubuntu/wine32-stage/img386, a deleted tree, and the
closure producer survived only as an untracked backup copy. The
lane rebuilt the path and scripted it under tools/rootfs32/
(full evidence: docs/reports/sysroot32-regeneration-20261028.md).
Forensics, byte-proven: img386 was a dpkg -x extraction of Debian
bookworm i386 packages, and the shipped glibc-rootfs32.zip is a
three-vintage hybrid -- bookworm i386 closure libraries at pinned
revisions, a bookworm font payload (libfontconfig1 2.14.1-4,
libexpat1 2.5.0-1+deb12u4), and a hand swap of Ubuntu noble
libc6 2.39-0ubuntu8's whole library directory. Deliverables:
build-img386.sh (26 sha256-pinned packages, hash-verified,
fail-closed on missing consumer paths), closure32.sh (the
backup's producer restored verbatim, IMG/OUT parameterized),
regen-glibc-rootfs32.sh (img386 -> closure -> the two proven
assembly adjustments -> the 32-bit post-steps in finisher order
-> member-by-member verification -> completeness checker).
Regeneration proof on the lane base: identical 154-member set,
150 byte-identical; checker 0 failures. Residue, named: four
glibc aux members (libanl.so.1, libnsl.so.1, libpcprofile.so,
libthread_db.so.1; 170,840 B) match no durable pinned source --
the regen fails closed on exactly those unless
--allow-glibc-aux-diff substitutes the noble-release bytes, and
prints the difference either way; none is DT_NEEDED by any guest
binary. Also named, out of scope: wine32.zip's base tree still
has no committed producer (only its post-steps are scripted).

### Merge outcome (integ, 2026-10-29) -- GATE COMPLETE

Merged as a2f4f6832 (parents 26d747b22 + 1f8bf4a5e), clean ort
merge, no conflicts. This section is new in the campaign report
(the lane's record is its regeneration report); numbered 4.49,
no renumber needed. COMPOSITION: the merged delta is
byte-identical to the lane delta (verified by diffing the two
diffs) -- 5 files, +703/-0: the three tools/rootfs32/ scripts,
the regeneration report, and the runtime.md addendum. No
product code; no staged runtime bytes change.

RE-PIN RECONCILIATION: the committed merge changes no pin.
tools/browser/runtime-assets.json carries the identical
glibc-rootfs32 entry on the lane base, the lane tip, and
pre-merge integ (3,875,770 B, sha256 32f9e512...), and the
archive present in the merged tree hashes to exactly that pin.
The lane's regenerated zip (3,877,987 B, sha256 30e88797...)
was proven against a scratch copy of the manifest re-pinned to
those values (bootstrap.py --check exit 0 there); re-pinning
is the documented publishing step (the regeneration report's
Proof section; the regen script's own closing instructions),
performed when a regenerated set is published -- it is not a
committed change of this merge, and no archive in the tree was
replaced.

REGISTER AND CITATIONS: runtime.md rows unchanged -- the
addendum is prose (table lines 93 with 14 separators, identical
pre- and post-merge); no row changed disposition, OPEN stays
0, and the standing register totals are untouched (vulkan.md
374 = 75 IMPLEMENTED / 66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0
OPEN). Citation audit: 228 MATCH / 14 MATCH-INHERITED / 97
UNCHECKABLE / 7 EXEMPT over 346 cited rows, 0 DRIFT / 0
MISSING -- the identical distribution to pre-merge, with no
re-pins (the merge touches no cited file).

CHECKS: (1) tools/browser/bootstrap.py --check exit 0,
"Runtime assets verified (13 files)"; (2)
check_runtime_completeness.py standalone: 0 failures, 1 skip
(pre-existing environmental: prefix64 built artifact not
present); (3) fail-closed re-proven on the merged tree's own
script: regen-glibc-rootfs32.sh in a scratch worktree of the
merge commit, WITHOUT --allow-glibc-aux-diff, exited 1 at the
verification step, naming exactly the four residue members
with their shipped sizes and hashes (libanl.so.1 13,736 B
sha256 909e5568...; libnsl.so.1 100,300 B sha256 567f7696...;
libpcprofile.so 13,852 B sha256 0c8181ee...;
libthread_db.so.1 42,952 B sha256 472d3516...), before the
completeness step ran. The integ tree's own archives were
never touched -- the run's reference was its own copy and the
regenerated zip stayed in the scratch worktree, since removed.

BUILD AND SUITES: bw64-build.sh -- Browser export verified,
completeness gate 0 failures / 1 skip inside the build. dist
stamp a2f4f6832, wasm d48af057... (unchanged from the pre-merge
build -- the merge carries no product code).
dist/runtime/wine64.zip member-verified: shim ae96105b...
(160,169 B) under bw64vulkan.dll, vulkan-1.dll and
winevulkan.dll; canonical pair a96b4e9d... (517,717 B) /
3d3bfe43... (8,825,383 B) unchanged. JS unit suite 328 passed
/ 0 failed (BW_TEST_DIST = merged dist). Emulator native 648
OK / 0 FAILED (222 s, single-threaded). run-native 37 PASS /
0 FAIL.

GUEST GATE: tri12 RESULT 0 -- guest verdict in
test-results/dxvk-sysroot32merge-tri12 (guest exit status 0,
no FAIL lines; provenance sha-verifies the canonical pair and
the merged shim). The harness composite passed:false with 0
chunks captured is the standing offscreen capture artifact;
the verdict above is the guest's own.


## 4.50 wine32.zip base tree: the last unproduced archive gets a provenance and a producer (2026-10-28, wine32base lane)

Until this lane, wine32.zip was the one shipped archive with no
producer: its post-steps were scripted, but the base tree (1,892
of 1,894 files) had no recorded provenance. The lane identified
the base from the bytes and scripted its regeneration (full
census, evidence, and proof:
docs/reports/wine32-base-forensics-20261028.md). The shipped
archive (1,926 members) is: the WineHQ wine-stable-i386
10.0.0.0~noble-1 deb (sha256 5ba523409...; 1,323 of 1,337 tree
files byte-identical -- the bookworm build of the same version
matches only 469 and is ruled out), whose 14 exceptions are the
d3d12 post-step pair plus the 12 patchelf'd unix modules (exactly
the noble modules with undefined __isoc23_* symbols;
patchelf --add-needed reproduces all 12 byte-for-byte); the
share/wine data (78 files, two placements) from Debian bookworm
libwine 8.0 (78/78 byte-identical); and a 71-file system-library
closure byte-attributed to bookworm i386 debs (71/71).
Deliverables: tools/rootfs32/build-wine32-base.sh and
regen-wine32-zip.sh (every input pinned URL+sha256+size,
fail-closed). Proof on the lane base: the regenerated archive is
member-identical to the shipped one (set, sizes, sha256,
1,926/1,926) against an era-matched oracle set; against current
integ exactly one non-wine32 check differs (the wine64 vkd3d
pins -- an oracle-era artifact). Residue (named, preserved): two
base members are locally built artifacts whose sources survive
nowhere -- usr/lib/wine/wineserver32 (3,794,568 B, sha256
ac990fd2...) and libisoc23shim.so (15,252 B, sha256 ef5e5696...,
two placements). The regen extracts both from the shipped
(pinned) archive and hash-verifies before use, fail-closed.

### Merge outcome (integ, 2026-10-29) -- GATE COMPLETE

Merged as d0e7a9bd9 (parents 7a8b5975d + 16480cfbd). Pre-merge
tips verified exactly (integ 7a8b5975d, lane 16480cfbd). One
content conflict: docs/known-gaps/runtime.md -- this lane and
the sysroot32 lane (merged as 4.49) both appended a provenance
addendum after the same anchor. Resolved by keeping BOTH
addenda verbatim, sysroot32's first, then this lane's; verified
from both sides: the merged file equals pre-merge integ plus
exactly the lane's runtime.md delta (+40/-1: the header scope
sentence and the addendum), and equals the lane file plus
exactly the 45-line sysroot32 addendum, 0 lines removed. The
other four files (.gitignore, the forensics report, both
scripts) are byte-identical to the lane tip; merged delta total
5 files, +774/-1, identical to the lane's own diffstat. No
product code; no staged bytes change. This section is new in
the campaign report (the lane's record is its forensics
report); numbered 4.50, no renumber needed.

REGISTER: runtime.md rows unchanged -- table lines 93 with 14
separators, identical pre- and post-merge (the lane delta is
prose only); no row changed disposition, OPEN stays 0, standing
totals untouched (runtime.md 57 rows; vulkan.md 374 = 75
IMPLEMENTED / 66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN).
Citation audit: 228 MATCH / 14 MATCH-INHERITED / 97 UNCHECKABLE
/ 7 EXEMPT over 346 cited rows, 0 DRIFT / 0 MISSING -- the
identical distribution to pre-merge, no re-pins (the merge
touches no cited file).

CHECKS: (1) tools/browser/bootstrap.py --check exit 0, "Runtime
assets verified (13 files)"; (2) check_runtime_completeness.py
standalone: 0 failures, 1 skip (pre-existing environmental:
prefix64 built artifact not present); (3) fail-closed proven
LIVE on the merged tree's own scripts, in /tmp scratch only
(the integ tree's archives were never touched; scratch since
removed): (i) an oracle dir without wine32.zip makes
regen-wine32-zip.sh exit 2 naming the missing archive before
any work; (ii) an oracle wine32.zip rebuilt with the
wineserver32 member replaced by same-length zeroed bytes (a
valid zip; member sha256 9f64bc4c...) makes build-wine32-base.sh
abort at the preserved-member step, exit 1 -- "preserved member
usr/lib/wine/wineserver32 hash mismatch: 9f64bc4c... !=
ac990fd260af..." -- and NO regenerated archive was produced.
The untampered shipped members verify against their pins
(wineserver32 ac990fd260af..., libisoc23shim.so
ef5e5696a194...), re-verified in the merged dist's wine32.zip
below. The lane's committed proof log (wt-wine32base
test-results/wine32base/regen-proof-20261028.log) shows the
full untampered regeneration: both preserved members sha256
verified, member-identical result, completeness 0 failures / 5
skips against the era-matched set (the skip profile differs
from integ's by oracle era, as the lane documented).
(4) JS unit suite 328 passed / 0 failed (BW_TEST_DIST = merged
dist). (5) Emulator native 648 OK / 0 FAILED -- the binary's
own summary "0 tests FAILED in 216s", run single-threaded,
exit 0 (the condStartWaitTime guard from 4.48 ran and passed).
(6) run-native 37 PASS / 0 FAIL (37 PASS lines, 0 FAIL lines,
exit 0). (7) Citations as above.

BUILD AND ARCHIVES: bw64-build.sh -- Browser export verified,
completeness gate 0 failures / 1 skip inside the build. Dist
stamp d0e7a9bd9, wasm d48af057... (unchanged from the pre-merge
build -- the merge carries no product code).
dist/runtime/wine64.zip member-verified: shim ae96105b...
(160,169 B) under bw64vulkan.dll, vulkan-1.dll and
winevulkan.dll; canonical pair a96b4e9d... (517,717 B) /
3d3bfe43... (8,825,383 B) unchanged. dist/runtime/wine32.zip
(1,926 members) member-verified: the PE32 pair at
opt/wine-stable/lib/wine/i386-windows -- d3d12.dll 872c3d65...
(517,273 B) / d3d12core.dll 71e9c174... (9,022,393 B); the two
preserved members at their pins (above).

GUEST GATE: (a) tri12 RESULT 0 -- guest verdict tri12Result 0,
no FAIL lines, guest exit status 0; the run's provenance
sha-verifies the canonical pair and the merged shim
(test-results/dxvk-wine32basemerge-tri12). The harness
composite passed:false with 0 chunks captured is the standing
offscreen capture artifact; the verdict above is the guest's
own. (b) tri12-32 RESULT 0 via the 32-bit system-placement
harness (arch=x86) -- harness passed:true, tri12Result 0, no
FAIL lines, and the loaddll trace proves the shipping path this
merge documents: d3d12.dll and d3d12core.dll loaded native from
C:\windows\system32 (wine32.zip system placement), winevulkan
.dll builtin, 256 vk32 traps
(test-results/dxvk-wine32basemerge-tri32).

## 4.51 32-bit vkCmdClearColorImage: the 4-byte clear pattern tiled wide texels (2026-10-28, clear32 lane)

**Question (queued observation from §4.36).** The fmtbpp lane closed
with an observation, left untouched there: the 32-bit
`vkCmdClearColorImage` writes its 4-byte clear pattern in 4-byte steps
regardless of texel size. This lane settles it: **live defect, fixed.**

**The pinned code paths (the two ABIs do not share this handler).**
The 64-bit handler (`vk64bridge.cpp`, `VK64_fn_vkCmdClearColorImage` in
`vk64Bridge`) reads the full 16-byte `VkClearColorValue` and repeats
that 16-byte pattern across `pixBytes * w * h` — exact for 16-byte
texels (the R32G32B32A32 family, whose texel IS the four 32-bit
component words). The 32-bit handler (same fn id in `vk32Bridge`) was
written separately:

```cpp
U32 pxBytes = fmt_bpp(im->format);
U32 pixel; memcpy(&pixel, col.float32, 4);
U64 total = (U64)im->w * im->h * im->d * im->layers * pxBytes;
...
for (U64 off = 0; off < total; off += 4)
    g_mem32->memcpyToGuest(va + (U32)off, &pixel, 4);
```

It uses `fmt_bpp` only for the total byte count; the pattern is the
first 4 bytes of the clear value, written unconditionally in 4-byte
steps — so a wide texel is tiled with its first component, and a
footprint not divisible by 4 is overrun (the last iteration writes a
full word at `off < total`).

**Before bytes (native harness, `tools/vkd3d/tests/run-native.py`,
real bridge + byte-addressed guest memory; 2-texel images).**

| format | clear value | bytes landed per texel (before) | spec texel |
|---|---|---|---|
| R32G32B32A32_SFLOAT (16 B) | (1.0, 0.5, 0.25, 1.0) | `00 00 80 3f` x4 = [1.0, 1.0, 1.0, 1.0] | [1.0, 0.5, 0.25, 1.0] |
| R32G32B32A32_UINT (16 B) | 0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00 | first word x4 | all four words |
| R32G32_SFLOAT (8 B) | (2.0, -1.0) | [2.0, 2.0] | [2.0, -1.0] |
| R16G16B16A16_SFLOAT (8 B) | (1.0, 0.5, 0.0, -1.0) | raw float bits of 1.0, x2 per texel | half floats 0x3c00, 0x3800, 0x0000, 0xbc00 |
| R16G16B16A16_UINT (8 B) | as above | first uint32 x2 per texel | uint16 truncation per component |
| R8G8B8A8_UNORM (4 B) | (1.0, 0.0, 0.5, 1.0) | raw float bits of 1.0 | `ff 00 80 ff` |
| R8_UNORM 3x1 (3 B footprint) | R = 1.0 | `00 00 80` + 1 byte written past the footprint | `ff ff ff`, no overrun |

Verdict: for every format whose texel is not exactly the first
component word, the guest image memory held wrong bytes, and no test
caught it because every clear exercised so far used a shape where the
defect is invisible or the bytes were never read back.

**Fix.** `vk32ClearTexel` (new, beside the other 32-bit helpers)
converts the `VkClearColorValue` into the format's own component
layout — component count and size from the format (texel size itself
still from `fmt_bpp`), float32 -> float16 for SFLOAT16 (round to
nearest, ties to even, with inf/nan/subnormal handling), UNORM/SNORM
quantization from the float components, low bits of the integer
components, and BGRA component order for B8G8R8A8 — and the handler
repeats the whole texel through a 4 KiB staging buffer (divisible by
every texel size), truncating the final write at the footprint, so
nothing is written outside it. The 64-bit handler is unchanged.

**Residue (named).** Layouts that are not a plain component vector —
the packed formats (R5G6B5, B5G6R5, B5G5R5A1, A1R5G5B5, R4G4B4A4,
B4G4R4A4, A2B10G10R10, B10G11R11) and the depth/stencil formats —
keep the historical raw fill, now truncated to the texel size and
repeated per texel instead of tiled in 4-byte steps; bit-packing a
clear value into those layouts is a separate change if a workload
ever needs it. Both handlers also still ignore the subresource
ranges and clear the whole image; that predates this lane and is
unchanged.

**Proof.** New native-harness block "Vulkan32 clear-color writes
whole texels" (9 cases: the seven rows above, a packed-format case
pinning the named raw fill and its `clear-color-layout` count, and
an R32_SFLOAT control whose behaviour is unchanged; every case
carries 0xEE sentinels on both sides of the footprint). Pre-fix the
block aborts at the first case's byte assertion; post-fix
run-native.py is end-to-end green (32 PASS blocks, 31 baseline +
this one). Sabotage: reverting only the bridge change (test kept)
aborts the harness at the same assertion; restored, green.
Citation audit re-pinned for the lines the new helper shifts
(3 rows, construct-verified): 0 DRIFT / 0 MISSING (228 MATCH /
12 MATCH-INHERITED / 97 UNCHECKABLE / 7 EXEMPT). Register: one
IMPLEMENTED row added (371 = 72/66/233/0). Emulator native suite:
647 tests, 0 FAILED. JS suite on the lane's own dist (bw64-build.sh,
"Browser export verified"): 317 passed / 0 failed -- the first run
failed only `no-silent-failure` (the layout switch's bare `default:`
counted as an unlogged default, 20 -> 21); the reviewed-baseline
bump was NOT taken -- the fallback branch now names itself through
the bridge's own accounting (`vk64Unsupported
"clear-color-layout"`), the switch needs no default arm, and the
guard passes at its original strength.

**Merge outcome (integ, 2026-10-29).** Merged as `ed7cd8520`
(parents `f066da209` + `5f9a03be4`); citation re-pin fallout
`e6b632e36`. The lane's section 4.37 is renumbered 4.51 here.
Composition verified by diff accounting: the merged
`source/vulkan/vk64bridge.cpp` delta against pre-merge integ is
line-identical to the lane delta against its base (same two hunks,
same added and removed lines) -- no `fmt_bpp` hunk rides in from the
old base, and the 64-bit clear handler (a separate implementation
in `vk64Bridge`) is byte-identical to pre-merge; the native-test
delta is likewise line-identical (+76, the guard block). The
register was applied as a delta, not taken from the branch: one
IMPLEMENTED row added with its citations re-pinned to the merged
tree (`vk32ClearTexel` at vk64bridge.cpp:9990, the 32-bit handler
at :11841), totals 375 = 76 IMPLEMENTED / 66 REJECTED-LOUDLY /
233 NOT-A-GAP / 0 OPEN. Citation audit: the +108-line helper
insertion drifted exactly the three rows the lane itself had
re-pinned (marker, once-per-name diagnostic, unimplemented 32-bit
trap); re-pinned by the exact +108 delta, construct-verified --
the fn32-trap row now cites the actual trap fallback call
(:10416); its old line had matched only by checker tolerance to an
unrelated properties2 call. Final audit 0 DRIFT / 0 MISSING
(229 MATCH / 14 MATCH-INHERITED / 97 UNCHECKABLE / 7 EXEMPT over
347 cited rows). `no-silent-failure` passes with
`known-silent.json` unchanged from pre-merge -- the lane's
no-baseline-bump resolution (the fallback names itself via
`clear-color-layout`) holds on the merged tree. Suites on the
merged tree: JS unit 328 passed / 0 failed (BW_TEST_DIST=integ
dist; the lane added no JS files); emulator native 648 OK /
0 FAILED; run-native.py 38 PASS / 0 FAIL -- 37 baseline plus the
lane's block, accounted by name ("Vulkan32 clear-color writes
whole texels (wide, half, int and UNORM formats; no footprint
overrun)"). Dist built from the re-pin commit (stamp `e6b632e36`,
wasm `1dc475a6...`), "Browser export verified", completeness
0 failures / 1 pre-existing environmental skip; wine64.zip
member-verified (shim `ae96105b...` under bw64vulkan.dll /
vulkan-1.dll / winevulkan.dll; canonical d3d12 pair `a96b4e9d...` /
`3d3bfe43...`) and wine32.zip still carries the PE32 pair
`872c3d65...` / `71e9c174...`. Guest gate on the merged dist
(probe slot, guest-verdict adjudication): tri12-32 via the 32-bit
system-placement harness (arch=x86) passed:true outright,
RESULT 0 (d3d12 pair loaded native from C:\windows\system32,
winevulkan builtin, 256 vk32 traps;
test-results/dxvk-clear32merge-tri32); tri12 RESULT 0
(test-results/dxvk-clear32merge-tri12; composite passed:false is
the standing 0-chunk capture artifact); cs12 RESULT 0 with the
full standing signature (SUMMARY buf=OK buf2=OK tex=OK;
test-results/dxvk-clear32merge-cs12). GATE COMPLETE.

## 4.52 vkCmdClearColorImage subresource ranges: both handlers cleared the whole image (2026-10-28, subclear lane)

**Question (named residue of §4.51).** The clear32 lane closed its
report with a residue it did not touch: both `vkCmdClearColorImage`
handlers ignore the `VkImageSubresourceRange` arguments and clear the
whole image — every mip level, every array layer — whatever the guest
asked for. This lane settles it: **live defect, fixed in both ABIs.**

**Before behaviour (pinned).** The 64-bit handler
(`VK64_fn_vkCmdClearColorImage` in `vk64Bridge`) never read
`args.a[4]`/`args.a[5]` (rangeCount / pRanges); it computed
`pixBytes * w * h` (falling back to the whole allocation) and repeated
the 16-byte clear colour across it. The 32-bit handler (same fn id in
`vk32Bridge`) likewise never read the ranges; after clear32 it computed
`w * h * d * layers * fmt_bpp` — mip 0 of every layer, repeated — and
repeated the converted texel across that span, which for a multi-mip
image both overruns the true footprint (clamped only by the
allocation) and still writes mips/layers the guest never named. A
guest clearing one mip or one layer silently destroyed the others'
contents.

**Layout math (reused, not reinvented).** The bridge already carries
the canonical image layout: `imageMipBytes` (per-mip byte size with
the mip shrink and block rounding), `imageLayerBytes`,
`imageDataBytes`, and `imageSubresourceOffset` (layer stride plus
preceding mips). The fix uses exactly those; `Image::pixBytes` is not
used for placement (its meaning differs by creation path).

**Fix.** Two shared helpers beside the layout math serve both ABIs.
`vkResolveClearRange` resolves one guest range against the image,
fail-closed: `VK_REMAINING_MIP_LEVELS` / `VK_REMAINING_ARRAY_LAYERS`
(and a zero count — invalid Vulkan, but what a zero-filled range
decodes to, and what the pre-existing harness guard supplies) extend
to the end of the image; a count past the end clamps to the end; a
base past the end serves nothing. The aspect mask: a range naming no
colour aspect is declined by name (`clear-color-aspect`, counted
through `vk64Unsupported`) and not cleared; a zero mask is treated as
colour. `vkClearColorFill` then fills each requested (mip, layer) at
`imageSubresourceOffset` for `imageMipBytes`, truncated at the
allocation, touching exactly those ranges (`vk64TouchMem` per
subresource), with the pattern phase continuous with the image start
so a subresource fill lands exactly the bytes a whole-image fill would
have landed there. A missing range list keeps the whole-image clear
(one synthetic REMAINING range). When the layout math has no footprint
for the image (extent fields unset), each handler falls back to its
historical whole-allocation fill, unchanged. What a texel contains is
untouched: the 64-bit 16-byte pattern and the 32-bit `vk32ClearTexel`
conversion (including the named `clear-color-layout` raw fill for
packed/depth layouts) are exactly clear32's semantics — this lane
changes only where fills land. Both handlers now also cap the range
count at 64 (the `vkCmdClearDepthStencilImage` precedent) with
`vk64FrameOverflow`.

**Proof.** New native-harness block \u201cvkCmdClearColorImage honours
subresource ranges\u201d: both ABIs over an R8G8B8A8_UNORM 4x4 image with
3 mips and 2 layers (mip sizes 64/16/4, layer stride 84, footprint
168) prefilled with 0xEE sentinels, asserting every byte inside the
cleared spans and every byte outside them (plus both footprint
neighbours). Cases per ABI: one mip + one layer, a middle mip range,
REMAINING_* from the tail, over-range counts clamping, no range list
(whole image), and a non-colour aspect declined by name with its
count; the 32-bit side adds a two-ranges-in-one-call case. Post-fix
run-native.py is end-to-end green (33 PASS blocks, 32 baseline + this
one; the clear32 block passes unmodified — its unwritten range slot
decodes to zeros, which the zero-count rule resolves to a whole-image
clear). Sabotage: forcing `vkResolveClearRange` to resolve every range
to the whole image aborts the harness at the outside-bytes assertions;
restored, green. Citation audit re-pinned for the lines the new
helpers and handler bodies shift (38 citations, occurrence-mapped and
construct-verified): 0 DRIFT / 0 MISSING. Register: one IMPLEMENTED
row added (372 = 73/66/233/0), and the clear32 row's handler citation
re-pinned with its residue sentence pointed here. Emulator native
suite: not run — the change is confined to `vk64bridge.cpp`
(outside the linux native build graph; the wasm build compiles the
TU) plus the harness and docs, and no shared header is touched. JS
suite: a full run needs a lane dist; the bootstrap was restored and
verified (13 files, completeness gate 0 failures), but the dist build
waited out a sibling lane's probe drain and then lost the build lock
to the concurrent integ build, so no lane dist exists. The dist-less
unit run's failures are all the missing-dist class (the runner's own
NOTE; 67 files failing at setup), and the four source guards that
adjudicate this change pass there: `vk-register-citations`,
`no-silent-failure`, `no-silent-drops`, `vk-shim-bridge-parity`.

**Residue (named).** Packed and depth/stencil texel layouts keep the
clear32 named raw fill (unchanged); depth/stencil *images* are
cleared by `vkCmdClearDepthStencilImage`, a separate handler whose
ranges already reach the page tier as recorded commands — out of
scope here. An image whose extent fields are unset still takes the
historical whole-allocation fill (no recorded workload creates one).

**Merge outcome (integ, 2026-10-29) -- GATE COMPLETE.** Merged as
`b0f0634b5` (parents `140701294` + `2abd3d4d9`); citation re-pin
fallout `a59d6b8b2`; the merge rider (below) landed as `b16984d2f`.
The lane's section is renumbered 4.52 here. The bridge merged
delta against pre-merge integ is exactly three hunks -- the shared
helpers (+65 lines at the layout helpers), the 64-bit handler
rewrite, the 32-bit handler rewrite -- giving exact citation band
deltas (+65 from old line 2532, +72 cumulative from 8891, +84
cumulative from 11875). The register was applied as a delta, not
taken from the branch: one IMPLEMENTED row added (helpers at
vk64bridge.cpp:2524/:2549/:2565, handlers at :8921/:11913), the
clear32 row's citations re-pinned through the insertion and its
residue sentence closed; totals 376 = 77 IMPLEMENTED /
66 REJECTED-LOUDLY / 233 NOT-A-GAP / 0 OPEN. Citation audit back
to 0 DRIFT / 0 MISSING (230 MATCH / 14 MATCH-INHERITED /
97 UNCHECKABLE / 7 EXEMPT over 348 cited rows; 17 re-pins by the
auditor's safe --fix, 16 hand-pinned by the band deltas,
construct-verified). THE RIDER: the lane's insertions broke three
existing JS tests on the integ-shaped tree (proven independently
by the packedclear64 lane on a pristine worktree at the lane tip),
and the repair is part of this merge, at full assertion strength.
vk-image-transfer and vulkan-image-format-size extract bridge
slices that now include vkResolveClearRange/vkClearColorFill, so
their stub preambles gain the ClearRangeArg mirror (plus a
vk64Unsupported stub for transfer; <vector>/U8/DeviceMemory/
vk64TouchMem declarations and Image.memOff for format-size) --
stub reversion reproduces the original ClearRangeArg compile
failures verbatim. The vk-image-dirty-generation census did not
pair the clear handlers' dirtyGen bumps with a vk64TouchMem: the
touch now happens per subresource inside vkClearColorFill. The
census's (a) pairing rule learned that delegation, pinned at the
helper's own body (`vk64TouchMem(dmem, absOff, subSize)`), and
the (f) raw-write pin moved 52 -> 53 with its disposition note
(the helper's one memcpyToGuest line, tied into the signal by the
helper touch + each handler's bump). Honesty, by scratch mutation
with the bridge restored byte-identical (sha256-checked) after
each: deleting the helper's touch turns the census red at the
helper pin; renaming the 64-bit handler's vkClearColorFill call
reproduces the original "dirtyGen bump at line 8959 lacks a
paired vk64TouchMem" failure; neutralizing an unrelated bump's
adjacent touch is still caught (line 2773). Suites on the merged
tree: JS unit 328 passed / 0 failed (BW_TEST_DIST=integ dist)
with the three repaired tests passing by name
(vk-image-dirty-generation, vk-image-transfer,
vulkan-image-format-size); emulator native 648 OK / 0 FAILED;
run-native.py 39 PASS / 0 FAIL -- 38 baseline plus the lane's
block, accounted by name ("vkCmdClearColorImage honours
subresource ranges (both ABIs; outside bytes untouched, REMAINING
and clamp)"), and the clear32 block ("Vulkan32 clear-color writes
whole texels") still passes. Dist built from the rider commit
(stamp `b16984d2f`, wasm `5a5458bafc4f...`), "Browser export
verified", completeness 0 failures / 1 pre-existing environmental
skip; wine64.zip member-verified (shim `ae96105b...` under
bw64vulkan.dll / vulkan-1.dll / winevulkan.dll; canonical d3d12
pair `a96b4e9d...` / `3d3bfe43...`) and wine32.zip carries the
PE32 pair `872c3d65...` / `71e9c174...`. Guest gate on the merged
dist (probe slot, one at a time, guest-verdict adjudication):
tri12-32 via the 32-bit system-placement harness (arch=x86)
passed:true outright, RESULT 0
(test-results/dxvk-subclearmerge-tri32); tri12 RESULT 0 with no
fail lines (test-results/dxvk-subclearmerge-tri12; composite
passed:false is the standing 0-chunk capture artifact); cs12
RESULT 0 with the full standing signature (VARIANT TEX/BUF/BUF2
all bad=0; test-results/dxvk-subclearmerge-cs12). GATE COMPLETE.
## 4.53 32-bit clear colour, packed formats: the raw fill packed nothing (2026-10-28, packedclear lane)

**Question (§4.51's named residue).** §4.51 converted the plain
component-vector formats in `vk32ClearTexel` and left the bit-packed
formats on the historical raw fill, named via `vk64Unsupported
"clear-color-layout"`: the clear value's first bytes, truncated to the
texel size. For a packed format those bytes are the raw bits of
`float32[0]` (or of `uint32[0]`), which are not the packed encoding of
any component — a clear of a packed image wrote wrong bytes for every
texel. This lane closes the packed half of the residue.

**Census (the bridge's supported set: `fmt_bpp`'s table, the
supported-image list, the tier's format map in `web/vk-protocol.mjs`).**
Fields are listed most-significant first — for packed formats the
Vulkan layout is the field order of the format name, which is also the
order the tier's own `packedUnormToRgba8` decodes and the integer
layout of WebGPU's `rgb10a2unorm` / `rg11b10ufloat`:

| format | layout (MSB -> LSB) | clear input | tier format |
|---|---|---|---|
| R4G4B4A4_UNORM_PACK16 (2) | R4 G4 B4 A4 | float32 | rgba8unorm (converted) |
| B4G4R4A4_UNORM_PACK16 (3) | B4 G4 R4 A4 | float32 | rgba8unorm (converted) |
| R5G6B5_UNORM_PACK16 (4) | R5 G6 B5 | float32 | (fmt_bpp only; not in the image list) |
| B5G6R5_UNORM_PACK16 (5) | B5 G6 R5 | float32 | rgba8unorm (converted) |
| R5G5B5A1_UNORM_PACK16 (6) | R5 G5 B5 A1 | float32 | (fmt_bpp only; see below) |
| B5G5R5A1_UNORM_PACK16 (7) | B5 G5 R5 A1 | float32 | rgba8unorm (converted) |
| A1R5G5B5_UNORM_PACK16 (8) | A1 R5 G5 B5 | float32 | rgba8unorm (converted) |
| A2B10G10R10_UNORM_PACK32 (64) | A2 B10 G10 R10 | float32 | rgb10a2unorm |
| A2B10G10R10_UINT_PACK32 (68) | A2 B10 G10 R10 | uint32 | rgb10a2uint |
| B10G11R11_UFLOAT_PACK32 (122) | B10 G11 R11 (unsigned float, 5-bit exponent, bias 15; 6-bit mantissa for the 11-bit fields, 5-bit for B) | float32 | rg11b10ufloat |

Two census defects rode along: `fmt_bpp` had **no case** for
R5G5B5A1 (it fell into the loud 4-byte default — a wrong texel size,
not just a missing conversion) and none for A2B10G10R10_UINT (the
default's 4 was the right size, with a spurious `format-bpp` note).
Both are explicit cases now. Outside the supported set entirely, and
untouched: A2R10G10B10 (58/62) and E5B9G9R9_UFLOAT_PACK32 (123)
appear in no bridge table (`fmt_bpp`, supported-image, texel-buffer),
so no clear can reach them through an image this bridge creates.

**Fix (conversion path only — where fills land is unchanged, and is
the sibling subclear lane's surface in the same handlers).**
`vk32ClearTexel` gains a packed branch before the named fallback:
UNORM fields quantize from the float components with the plain path's
round-half-up discipline (`vk32PackUnorm`; the 1-bit alpha fields are
the same formula at 1 bit, i.e. a 0.5 threshold); the UINT sibling
takes the uint32 components' low bits, masked per field; B10G11R11
fields convert through `vk32FloatToUFloat` — an exact
round-to-nearest-even conversion onto the target grid (normals,
subnormals, overflow to the inf pattern, negatives clamped to zero),
so the error is at most half a target ULP, the same guarantee as the
existing float32->float16 helper. The packed word is stored
little-endian, `texelSize` bytes. The conversion was cross-checked
against an independent exact-rational model of the UFLOAT grid on
1,234 values (boundaries, subnormals, random magnitudes): identical
encodings everywhere.

**The 64-bit handler (checked, named residue — not fixed here).** It
repeats the raw 16-byte `VkClearColorValue` pattern, so for packed
formats it is provably wrong the same way: a 2-byte packed texel gets
the low half of `float32[0]`'s raw bits, tiled with a 16-byte period;
a 4-byte packed texel gets `float32[0]`'s raw bits verbatim. No
conversion happens on that path for any packed format. The repair is
not the same change, though: the 64-bit fill loop repeats a fixed
16-byte pattern and has no per-texel conversion step to extend — it
must be restructured to repeat a converted texel, which is the fill
machinery the subclear lane owns in the same handler. Named here as
the remaining packed residue: **64-bit packed clears write the
unconverted pattern** (subclear's range work composes first; this
conversion is the follow-up on top of it).

**Residue (named, unchanged).** Depth/stencil formats (D16_UNORM,
X8_D24_UNORM_PACK32, D24_UNORM_S8_UINT, D32_SFLOAT,
D32_SFLOAT_S8_UINT) and the block-compressed formats keep the named
raw fill (`clear-color-layout`, message updated to say so): a
`VkClearColorValue` carries no depth/stencil semantics for a colour
clear, and a BC block has no per-texel layout to pack into.

**Proof.** New native-harness block "Vulkan32 clear-color packs
bit-packed formats": 12 cases over the 10 formats above (R5G5B5A1
twice for the alpha threshold, B10G11R11 three times — exact values,
a rounding case where truncation and round-to-nearest differ, and a
subnormal at exactly one R11 step), each with 0xEE footprint
sentinels, plus an assertion that no packed case touches the
`clear-color-layout` count. §4.51's block stays green; its one
packed-fallback case now pins the fallback on D16_UNORM instead of
R5G6B5 (same intent — a non-converted layout keeping the named raw
fill — on a format that still takes that path). Sabotage, both
reverted after proof: swapping the R/B fields of the R5G6B5 arm
aborts the harness at the packed block's byte assertion; making the
UFLOAT rounding truncate aborts it the same way. run-native.py
end-to-end green, 33 PASS blocks (32 at base + this one). Citation
audit: 0 DRIFT / 0 MISSING (228 MATCH / 12 MATCH-INHERITED / 97
UNCHECKABLE / 7 EXEMPT; 145 numbers re-pinned by a pure delta map over
this lane's bridge diff, construct-verified by the audit). Register:
no count change (371 = 72/66/233/0) — the §4.51 row's residue note is
narrowed to depth/stencil + block-compressed, and the header records
this closure. Emulator native suite not run: the bridge is outside
its build graph and no shared header changed. JS suite on the lane's
own dist (bw64-build.sh, "Browser export verified", support archive
bbc583c6e657): 316 passed / 1 failed — the one failure is
probe-slots, a probe-rig concurrency test whose slot-ordering
assertion is defeated by the box's concurrent lane probing (a heavy
slot was held by another tree's guest probe throughout; the failure
reproduces solo under the same load and this lane changes no JS or
probe code).
