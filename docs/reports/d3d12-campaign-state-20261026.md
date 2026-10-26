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
   addressing (cap 5347, item 1) still wait behind it.
6. Present-less chunk drain so offscreen probes (tri12) verify pixels,
   not just API success.

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
