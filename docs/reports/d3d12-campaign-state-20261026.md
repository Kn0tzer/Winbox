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
   expansion for heap-indexed shader accesses.
2. V2 compute record + tier compute dispatch (unlocks D3D12 compute,
   UAV clears, execute-indirect).
3. godot-squash `--rendering-driver d3d12` on the mirror rig with the
   staged prefix; pin the driver banner from console.txt.
4. Implement `vkGetDeviceImageMemoryRequirements` from the existing
   image-requirements code.
5. Fold vkd3d staging into the prefix build pipeline; x86 vkd3d build.
6. Present-less chunk drain so offscreen probes (tri12) verify pixels,
   not just API success.
