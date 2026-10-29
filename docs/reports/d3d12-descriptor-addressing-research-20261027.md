# DirectX 12 descriptor addressing (BDA / SPIR-V cap 5347) — research verdict

Lane: descriptor-addressing research (scratch branch `lane-vkd3d-bda` off
`lane-vkd3d` @ c52f39d4e). Date: 2026-10-27. All source citations are to the
vendored vkd3d-proton 3.1.0 in `tools/vkd3d/src` (pin faf71e79…) and to this
repo's bridge/tier code, unless marked [web].

**Question.** vkd3d-proton compiles every descriptor-using D3D12 shader to
SPIR-V declaring capability 5347 (PhysicalStorageBufferAddresses); the bridge
honestly reports `bufferDeviceAddress = VK_FALSE` and its module audit
rejects cap 5347 at `vkCreateShaderModule`, so no descriptor-using D3D12
pipeline has ever been created in this stack (campaign report §4.17). What
route, if any, serves these shaders over WebGPU?

## 0. Verdict (ranked)

1. **Route 0 — "declared but unused" audit exemption (DO FIRST, tiny).**
   The 5347 in the modules that block us today is a *module-wide header
   artifact*, not code: vkd3d switches dxil-spirv into the physical
   addressing model for the whole module whenever the shader interface
   contains *any* raw-VA binding or root descriptor, and the capability is
   emitted even when the module contains **zero** pointer dereferences
   (census, §3). spirvfix **R8 already strips exactly this case** in the
   page tier (`strip-unused-physical-storage-buffer`), but the module never
   reaches the tier: the bridge audit rejects 5347 first. The audit already
   has the "declared but unused → serve" precedent for caps 33
   (CullDistance), 4427 (DrawParameters) and 55/56 (storage-image
   read/write-without-format). Add the same treatment for 5346/5347: skip
   the rejection iff the module contains no `OpConvertUToPtr` /
   `OpConvertPtrToU` and no `OpTypePointer` with storage class
   PhysicalStorageBuffer (5349) — byte-for-byte the used-test R8 applies.
   This unblocks the entire class of table-only modules whose pointers are
   never dereferenced — including uav12's typed-UAV execution, the current
   campaign wall — with **no new serving machinery**. Modules that *do*
   dereference keep rejecting, loudly, at the same named token.
2. **Route A — root-descriptor rewrite (THE main route for real games).**
   A new spirvfix rule (R15) rewrites dxil-spirv's root-descriptor pointer
   pattern into accesses on **dedicated per-root-descriptor bindings**,
   materialized per draw by the bridge+tier from the push-constant address
   words, resolved through the bridge's own synthetic VA table (the bridge
   already assigns every buffer's device address itself:
   `vk64bridge.cpp:707`, 256 MiB stride from 0x4000000000). Root
   descriptors are few (the D3D12 root-signature cost budget is 64 DWORDs
   total and each root descriptor costs 2; each names exactly one buffer —
   D3D12 root descriptors are **buffer-only** by API design), their
   addresses arrive per draw in push constants the tier already receives,
   and dxil-spirv's pointer codegen for them is a narrow, compiler-fixed
   pattern (§4). This is vkd3d's own non-raw-VA serving model (push
   descriptors → real bindings) re-implemented one layer down, where
   vkd3d can't be persuaded to emit it.
3. **Route C — single-arena storage buffer (FALLBACK, not now).** Serve
   all BDA traffic as offsets into one giant storage buffer holding copies
   of every VA-visible guest buffer. This is the only route that covers
   pointers that *escape* (stored into memory, read back by other shaders)
   without descriptor arrays, because the decode
   `slot = addr >> 28, offset = addr & 0xFFFFFFF` works in-shader against a
   single binding. It costs a ground-up change to the tier's memory model
   plus a hard ceiling: one binding window is capped by
   `maxStorageBufferBindingSize` (128 MiB spec default [web]; desktop
   Chromium adapters commonly report 1–2 GiB when higher limits are
   requested, but it is adapter-dependent and never guaranteed), so two
   live buffers farther apart than the window in one draw cannot both be
   served. Defer unless a target program provably needs escaping pointers.
4. **Route B — steer vkd3d out of raw-VA emission (DEAD as a whole; one
   partial lever, not recommended).** Proven at vendored source (§2.3):
   `VKD3D_RAW_VA_ROOT_DESCRIPTOR_SRV_UAV` is set unconditionally; the full
   config-flag list (`include/private/config_flag_decl.h`) contains no
   opt-out (only `force_raw_va_cbv`, which adds *more* raw VA). The
   descriptor-heap path (`VK_EXT_descriptor_heap` +
   `VKD3D_CONFIG=descriptor_heap`) still emits BDA for root descriptors
   whose alignment fails its SSBO test and for its raw heap view. Wine's
   upstream vkd3d avoids BDA but has no DXIL frontend — useless for SM 6.x.
   One partial lever exists: RAW_VA for **CBV** is skipped when the Vulkan
   vendorID is NVIDIA (0x10DE) or Qualcomm, falling back to push
   descriptors. The bridge currently reports a synthetic vendorID
   (0x1A2B3C4D, `vk64bridge.cpp:4357`). Spoofing NVIDIA would flip root
   CBVs — the commonest root kind — to machinery the tier already serves,
   but it is a false device advertisement with cross-stack blast radius:
   the same physical-device answer serves DXVK (D3D11) programs, which
   have their own vendor-keyed paths, and vkd3d keys further behavior off
   vendorID (device.c:1878, resource.c:6960, plus the exe-keyed workaround
   table). Not recommended; recorded so nobody re-derives it as a shortcut.

**Scope honesty.** Routes 0+A serve root descriptors and
declared-but-unused modules. They do **not** serve *heap-indexed* accesses
that dxil-spirv emits as runtime-sized descriptor arrays with non-uniform
indices (caps 5302/5306–5309, already audit-rejected under their own
names) — that is the separate binding-array wall named in campaign §6
item 1, second half. Which of the two walls a given real program hits
first is an empirical question per program (§6).

## 1. How 5347 gets into a module (mechanism, source-proven)

Three facts combine:

**(a) The bindless state sets the flags.** `vkd3d_bindless_state_get_bindless_flags`
(state.c:7820) sets `flags |= VKD3D_RAW_VA_ROOT_DESCRIPTOR_SRV_UAV`
**unconditionally** (comment: "We must use root SRV and UAV due to
alignment requirements for 16-bit storage"). The CBV sibling is set when
`FORCE_RAW_VA_CBV` is configured or vendor ∉ {NVIDIA, Qualcomm}
(state.c:7870). The descriptor-heap init path sets both flags too
(state.c:8186).

**(b) Every root signature with a UAV descriptor-table range carries a
raw-VA binding anyway.** For each SRV/UAV table range, root-signature
construction adds an auxiliary binding flagged `RAW_VA | AUX_BUFFER`
pointing at `raw_va_aux_buffer_binding` (state.c:805) — "Use raw VA for
both RTAS and UAV counters" — whether or not the shader uses counters.

**(c) The converter switch is module-wide and presence-based.**
`vkd3d_shader_compile_dxil` (dxil.c:1572, 1584) counts interface bindings
with the RAW_VA flag and passes `bda = (raw_va_binding_count ||
num_root_descriptors)` into `DXIL_SPV_OPTION_PHYSICAL_STORAGE_BUFFER`
(dxil.c:859). dxil-spirv then calls
`emit_entry_point(..., physical_storage=true, ...)`, which unconditionally
emits `OpCapability PhysicalStorageBufferAddresses`, the
`SPV_KHR_physical_storage_buffer` extension, and
`OpMemoryModel PhysicalStorageBuffer64` (spirv_module.cpp:405–417) —
**before any shader body is examined**. Pointer *instructions* are only
emitted if a resource actually resolves to a BDA binding during remap.

The remap (`dxil_remap_inner`, dxil.c:183) is where BDA is actually
*used*: a binding flagged as a root descriptor maps to
`DXIL_SPV_VULKAN_DESCRIPTOR_TYPE_BUFFER_DEVICE_ADDRESS` with a
`root_constant_index`, unless `VKD3D_SHADER_INTERFACE_HEAP_LOWERING`
applies (descriptor-heap mode only) **and** the binding's alignment
passes (`min_ssbo_alignment <= d3d_binding->alignment`), in which case it
maps to a plain SSBO/UBO descriptor. The fallback comment is verbatim:
*"In some cases we cannot use SSBOs due to alignment requirements. Just
fallback to good old BDA."*

**Consequence:** cap 5347 in a module proves only that the *root
signature* contained a UAV table range, an RTAS/counter-capable range, or
a root descriptor. It does not prove the shader dereferences a pointer.
That distinction is the whole opportunity.

## 2. Route B post-mortem (steering vkd3d)

- **Config flags:** full list read (`config_flag_decl.h`, 76 flags). The
  only raw-VA flag is `force_raw_va_cbv` (forces CBV raw VA *on*). No
  `no_raw_va`, no descriptor-model selector. `descriptor_heap` opts into
  the EXT_descriptor_heap path (device.c:144 requires the extension
  advertised *and* the config).
- **Descriptor-heap path does not remove BDA:** heap lowering maps root
  descriptors to SSBO/UBO only when the alignment test passes
  (dxil.c:188–209); the raw descriptor heap view meta-descriptor is
  itself BDA-typed in the inline-redzone configuration
  (`DXIL_SPV_META_DESCRIPTOR_KIND_UBO_CONTAINING_BDA`, dxil.c:1446), and
  heap init sets both RAW_VA flags (state.c:8186). Serving this path
  would additionally require the bridge to implement
  VK_EXT_descriptor_heap (descriptor heaps as buffers, mapping structs) —
  a large new surface that still ends at pointers.
- **Feature advertisement can't help:** the flags in (a) are not gated on
  any Vulkan feature the bridge controls, except via vendorID (see
  verdict item 4 for why that's a bad trade). vkd3d doesn't even require
  the Vulkan `bufferDeviceAddress` *feature* to be true for this codegen —
  it forces the two capture-replay/multi-device sub-fields to VK_FALSE
  (device.c:2535) and proceeds; the addresses it consumes come from
  `vkGetBufferDeviceAddress`, which the bridge already answers with
  synthetic values.
- **Other versions:** the RAW_VA design is vkd3d-proton's long-standing
  performance architecture (root descriptors as push-constant addresses);
  no release offers a descriptor-indexed root-descriptor mode outside the
  alignment-gated heap-lowering above. Wine upstream vkd3d uses push
  descriptors for root descriptors but translates DXBC/HLSL only — no
  DXIL, so no SM 6.x games. Not a route.

## 3. Census: what real vkd3d modules actually contain

Ground truth from uav12 (the Typed-UAV probe, `tools/vkd3d/probe/uav12.c`):
root signature = **one descriptor table, one UAV range (u1), no root
descriptors**; shaders = pass-through VS + PS doing typed UAV loads of 6
format classes.

**(i) Real guest run** (`test-results/dxvk-uav-after/console.log`,
campaign §4.17): the *only* capability rejections in the entire run are
`spirv-capability-unknown:5347` (×4). The audit collects **every**
OpCapability in the module (vk64bridge.cpp, audit scan `case 17`) and
logs each rejection by name — so the real modules declare **no**
RuntimeDescriptorArray / NonUniform-indexing capabilities. 5347 is the
sole blocker for this class.

**(ii) Faithful host reproduction.** The guest-compiled DXBC blobs were
recovered from the bisect lane's `uav12dump` console output (base64 dump;
vs 1020 B exact, ps_uav 732 B exact, ps_plain truncated by log wrap and
unused). dxil-spirv was built standalone from the vendored subproject and
run on `ps_uav.dxbc` in bindless mode (vkd3d's model). Result
(`bda-research/ps_uav_bindless.spv`, spirv-val clean):

- Declares `PhysicalStorageBufferAddresses` + PhysicalStorageBuffer64
  memory model — and **`OpConvertUToPtr`: 0, `OpConvertPtrToU`: 0,
  PhysicalStorageBuffer pointer types: 0, pointer loads/stores: 0.**
- The UAV access is an ordinary descriptor access: heap index loaded from
  push constants, `OpAccessChain` into the image array, `OpLoad` of the
  image handle, typed image read. (The standalone's generic bindless mode
  also declares RuntimeDescriptorArray; the real in-guest module did not
  — a remapper delta: vkd3d presents this single-range table as a fixed
  binding where the standalone presents a global heap array. The
  prototype lane should confirm with `VKD3D_SHADER_DUMP_PATH` captures;
  either way the *pointer* census is the same: none.)
- The VS converts identically: 5347 declared, zero pointer ops.

**(iii) R8 already serves this exact module shape.** Running the tree's
own spirvfix on the reproduction (`bda-research/r8test.mjs`):
`strip-unused-physical-storage-buffer` fires, capability + extension
removed, memory model rewritten to Logical, output passes `spirv-val`.
R8's test header (web/tests/suite/unit/spirvfix-r8.test.mjs, from the
2026-10-09 scene12 probe) records the same finding from real vkd3d
output: *"when a descriptor table is present, the whole pipeline gets the
PhysicalStorageBuffer64 memory model"* for shaders that never use
physical pointers. **The serving code exists; only the audit ordering
blocks it.**

**(iv) Root-descriptor modules (the Route A target).** No root-descriptor
module was capturable host-side (uav12 has none; a guest dump is the
prototype lane's first step), so the pattern is pinned from dxil-spirv's
converter source instead. It is narrow by construction:

- Root descriptors are **buffers only** — the standalone converter hard-
  errors otherwise ("BDA root descriptors must be raw buffers or
  structured buffers"); D3D12 root descriptors cannot name textures.
- dxil-spirv models each BDA pointee as a Block struct wrapping a single
  member (scalar/vector/runtime-array payload), creating one
  PhysicalStorageBuffer pointer type per (pointee, writability/alignment
  meta) — `physical_pointer_entries`, dxil_converter.cpp:1029, 3404,
  4322.
- The address is an integer expression whose only non-constant input is
  the root-constant u64 at that binding's `root_constant_index`
  (two push-constant words per root descriptor, occupying the head of
  the push block — "Root constants come after root descriptors",
  dxil.c:1580). Address arithmetic (byte offsets for structured/raw
  accesses) is integer math *before* `OpConvertUToPtr`; dereferences are
  `OpAccessChain` + `OpLoad`/`OpStore`/`OpAtomic*` with `Aligned`
  operands through the converted pointer.
- Pointers are SSA values: dxil-spirv never stores a root-descriptor
  pointer into memory, never writes one into a buffer, and never passes
  one across a function boundary except as the immediate operand of an
  access chain. `OpConvertPtrToU` appears only for null/equality idioms
  and for recomputing addresses within the same expression tree.

Patterns a rewriter must therefore handle are exactly: base-from-root-
constant, + integer offset expression, ConvertUToPtr, AccessChain/
PtrAccessChain with u32-expressible indices, Load/Store/Atomic at the
leaf, and PtrToU round-trips *within* the same expression DAG. Anything
else (pointer loaded from memory, pointer as function-call argument to a
non-inline helper, pointer stored, pointer whose base is a heap-array
load) is outside dxil-spirv's root-descriptor codegen and can fail
closed without losing real shaders.

## 4. Route A design (implementation depth)

**Goal:** serve modules whose physical pointers all derive from root
descriptors, by giving each root descriptor a real descriptor binding —
the same serving model vkd3d itself uses when it is *not* in raw-VA mode
(state.c:536–548 counts them as push descriptors then).

### 4.1 spirvfix R15: `root-descriptor-bda` (page tier, at module translation)

Input: module + the pipeline's root-descriptor map (see 4.3). Steps:

1. Census physical pointers exactly as R8 does; if none, R8's path
   applies (nothing to do). If any exist, continue; any step failing →
   return module untouched (the audit's loud rejection stands).
2. Build the def-use graph of every `OpConvertUToPtr` result. For each,
   walk its address operand backwards: it must bottom out at exactly one
   *root address source* — an `OpLoad`/`OpAccessChain` chain reading the
   u64 at push-constant word offset `2k` (root descriptor *k*), possibly
   with `OpIAdd`/`OpISub` of an integer expression that does **not**
   itself contain a pointer or a memory load. Record (k, offset-expr).
3. For each root descriptor *k*, synthesize one descriptor variable in a
   reserved set (the tier already reserves group 3 for the push-constant
   UBO; use a new reserved group, e.g. group 4 is taken by maxBindGroups=4
   [web] — so the root bindings must live **inside an existing set**:
   append them to the push-constant set's layout as extra bindings, or
   repack — an open layout detail for the prototype; the tier already
   rewrites pipeline layouts and WGSL for push constants
   (`rewritePushConstantsWGSL`), so there is precedent machinery).
   Binding kind from the pointee: CBV-shaped struct → read-only storage
   (uniform demotion is R13's shape; a CBV read through a storage binding
   is byte-identical under std430 for the layouts dxil-spirv emits for
   root CBVs — but see the alignment caveat in §5); raw/structured buffer
   → storage buffer variable whose block is the same pointee struct.
4. Rewrite each dereference: `OpAccessChain(psb-ptr, idx…)` → the same
   chain rooted at binding *k*'s variable, with the recorded byte-offset
   expression folded in as the chain's leading index arithmetic exactly
   as dxil-spirv would have done for an SSBO binding (it emits identical
   chains for SSBO-mapped resources — the heap-lowering branch produces
   them). Loads/stores/atomics keep their opcodes and memory operands
   minus `Aligned` (bindings carry their own alignment contract; where
   the pointee struct's layout requires it, keep the chain shape
   identical so naga's layout checks see the SSBO form it already
   accepts for DXVK/vkd3d heap SSBOs).
5. Delete the now-dead address computation, `OpConvertUToPtr`s, physical
   pointer types; strip cap 5346/5347 + extension; memory model →
   Logical (as R8). Validate the result structurally (spirv-val in the
   test harness; the tier's existing translation is the runtime check).

Fail-closed boundaries (each keeps today's named rejection):
pointer provenance not a single root source; offset expression containing
a load (address computed from buffer contents — the escaping case);
pointer stored to memory or passed across an `OpFunctionCall`; more root
descriptors than the layout budget; pointee layouts that are
scalar-block-layout-shaped (the known MatrixStride-12 hazard — naga
silently misreads those in *any* address space; R15 must run the same
layout audit R13's hazard note demands, or reject strided-matrix
pointees).

### 4.2 Bridge: VA resolution at command-record time

- The bridge already owns the VA space: `vkGetBufferDeviceAddress`
  assigns `Buffer.devVa` from `g_nextDevVa` (0x4000000000, +256 MiB per
  buffer, vk64bridge.cpp:5704) — a pure function the bridge can invert:
  `va → (Buffer*, va − devVa)`.
- At `vkCreateGraphicsPipelines`/`vkCreateComputePipelines`, run the same
  census as R15 over the stage modules (the audit already parses them)
  and store on the pipeline object: `rootDescCount` and per-slot kind
  (CBV / SRV / UAV + writable). If any stage's pointers don't fit the
  Route A pattern, mark the pipeline unservable — creation still
  *succeeds* only if R15's rewrite is expected to serve it; otherwise
  keep today's behavior (module creation already rejected it — i.e.,
  Route 0/A change the *audit* to admit modules the census proves
  servable, with the census result carried on the shader object so
  pipeline creation and the tier agree).
- At `vkCmdPushConstants` record time (and root-descriptor state is only
  ever delivered this way in raw-VA mode — vkd3d pushes the VA pairs as
  the head words of the push block), the bridge resolves words
  `[0 .. 2*rootDescCount)` of the *merged* push state for the bound
  pipeline at each draw/dispatch record: each VA → (buffer id, offset);
  VA 0 → null descriptor (D3D12 allows unbound root descriptors; reads
  must return 0 — serve with a zero-filled dummy buffer binding, the
  tier's existing null-descriptor machinery). Emit a V2 side record
  `ROOT_DESCRIPTORS` alongside the push-constant record the tier
  already consumes for its group-3 UBO.

### 4.3 Page tier: materialization

`vkwebgpu.mjs` already materializes descriptor sets per draw from the V2
stream and already consumes push-constant bytes per draw. Add: for
pipelines flagged with root descriptors, maintain the extra bindings of
§4.1 step 3; on each draw, bind (buffer, offset, range) from the
`ROOT_DESCRIPTORS` record into those slots (range = buffer size −
offset, matching Vulkan SSBO/UBO descriptor semantics; dynamic-offset
bind groups where the layout allows, else a cached bind group per
distinct tuple — root state changes are per-draw but tuple repetition is
high in practice). No new WebGPU features are needed: storage/uniform
buffer bindings with offsets are core; the only limit pressure is
`maxBindGroups` (4, spec [web]) — hence the layout note in step 3 — and
per-stage storage/uniform counts (8 storage / 12 uniform per stage spec
defaults [web]), which the prototype must budget against vkd3d's sets.

### 4.4 The aux buffer (UAV counters / RTAS) — explicitly staged later

Counter accesses go through the `raw_va_aux_buffer_binding`: the shader
loads a counter *address* that vkd3d computed as aux-base + per-descriptor
offset. The aux base is a bridge buffer like any other (it has a devVa),
so the same machinery *can* serve it as one more dedicated binding once
the address provenance is "aux descriptor word" instead of "root
constant word". RTAS root/heap entries have no WebGPU analogue at all
and stay rejected under the ray-tracing rows regardless. Counters are
Phase 2 of Route A, gated on a census module that actually uses them
(RWStructuredBuffer with counter) — do not build it speculatively.

## 5. Route C assessment (arena)

- *Mechanism:* bridge assigns VAs densely (it controls the allocator —
  shrink the 256 MiB stride to size-aligned packing); tier keeps one
  arena GPUBuffer; every buffer that ever receives a devVa is mirrored
  into the arena at its VA offset (the tier already re-uploads dirty
  buffer contents per submit via its content-hash model, so mirroring
  rides that path); R15-variant rewrites every physical load/store to
  `arena[(addr − arenaBase) >> 2 …]` word accesses on one storage
  binding typed as a runtime u32 array, with per-pointee vector/scalar
  reconstruction.
- *Why not now:* (1) the binding window cap (§0.3) turns "which buffers
  are live together" into a correctness cliff instead of a non-issue;
  (2) every buffer binding in the tier (vertex/index/uniform included)
  must stay coherent with its arena mirror — the tier's current model
  gives each guest buffer its own GPUBuffer and content stream, so this
  is a memory-model rewrite, not an addition; (3) typed accesses
  (structured structs, matrices) through a u32 arena need full
  load/store decomposition in the rewriter — strictly more rewrite
  surface than Route A for the common cases. Its unique coverage
  (escaping pointers) is unproven to occur in the target programs
  (Godot's D3D12 backend uses conventional root signatures + tables).
- Chromium reality check [web]: spec defaults are maxBufferSize
  256 MiB / maxStorageBufferBindingSize 128 MiB; adapters may report
  far more and the tier already requests a 1 GiB maxBufferSize (SM 6.0
  lane), but binding-size headroom is per-adapter and the arena must
  fit the *binding*, not the buffer.

## 6. What others do (survey)

- **No shipped Vulkan→WebGPU layer exists besides this project's**, and
  no general SPIR-V BDA→descriptor rewriter exists in SPIRV-Tools,
  SPIRV-Cross, Tint, or naga: SPIRV-Cross lowers `buffer_reference` only
  to native GLSL/MSL pointer syntax; Tint and naga reject the
  PhysicalStorageBuffer addressing model outright (which is why R8's
  strip is load-bearing today). Emulators that generate BDA (e.g.
  shadPS4's recompiler [web]) target native Vulkan hosts where BDA is
  real. The rewrite in Route A is greenfield — but bounded, because the
  input dialect is one compiler's (dxil-spirv's) fixed codegen patterns,
  not arbitrary BDA.
- **Dawn/wgpu implement WebGPU *on* Vulkan** (reverse direction) using
  descriptor indexing; their internal binding-array use is not exposed
  to web pages: browser WebGPU has no binding arrays of buffers,
  textures, or samplers in the shipped API surface (bind group entries
  are single resources; WGSL has no resource-array type for pages)
  [web]. Any design requiring shader-visible descriptor arrays is a
  dead end in the browser, full stop — this is also why the heap wall
  (§0, scope) needs the tier's materialization tricks rather than a
  direct mapping.
- **vkd3d's own fallback is the design template:** when not raw-VA, root
  descriptors are push descriptors counted in the root signature cost
  model (state.c:536–558) — i.e., "few, fixed, per-draw-materialized
  bindings" is the upstream-blessed serving shape. Route A copies it.

## 7. Ranked recommendation + prototype plan (one lane)

**Recommendation: Route 0 now, Route A next, Route C shelved, Route B
closed.** Route 0 is a one-function audit change with an existing
serving proof (R8) behind it and immediately converts the Typed-UAV
grant (§4.17) into executed typed-UAV loads. Route A is the only route
that serves the root-descriptor traffic real D3D12 games generate, fits
the tier's existing per-draw materialization, and needs no false
advertisements and no new WebGPU capabilities.

**Prototype plan (sized for one lane):**

1. **Capture ground truth.** Guest runs with `VKD3D_SHADER_DUMP_PATH`
   (vkd3d-shader dumps every compiled module): uav12 (expect: 5347
   declared, pointer-op census zero — confirming §3 and resolving the
   RuntimeDescriptorArray remapper delta), a new micro-probe with a
   root CBV + a root ByteAddressBuffer SRV + a root RWStructuredBuffer
   UAV (census the real R15 input patterns before writing the rule),
   and godot-squash far enough to dump its first descriptor-using
   modules (which wall — 5347-unused, root-BDA, or heap-arrays — does
   Godot actually hit?).
2. **Route 0.** Audit: add physical-pointer used-scan (ops 120/121 +
   OpTypePointer sc 5349 — the audit's scan already records pointer
   storage classes); skip 5346/5347 rejection iff unused, mirroring the
   33/4427 pattern; register row + unit guard with a *used*-pointer
   module that must still reject (sabotage pair). Verify: uav12 AFTER
   run — expect PSO creation to succeed and EXEC readbacks to run
   (RESULT 0 for the read cases the tier serves); first-light
   regression unchanged.
3. **R15 for root CBV only** (read-only, struct pointee — the commonest
   root kind): rewrite + bridge rootDescCount plumbing + tier
   materialization for one slot kind; guard tests on the captured
   modules; verify with the micro-probe (values read back match).
4. **R15 for root SRV/UAV** (raw/structured, incl. stores + atomics on
   UAV): same machinery, writable storage bindings; verify with the
   micro-probe; then a Godot re-run to find the next wall honestly.
5. Only if a captured real module shows counters: aux-buffer binding
   (§4.4). Only if one shows escaping pointers: reopen Route C with
   that evidence in hand.

**Risks named up front:** the push-block layout assumption (root VA
words at the head, 2 per root descriptor) is a dxil-spirv/vkd3d
invariant verified in source (dxil.c:1572–1592) but must be asserted in
the guard tests against captured modules; CBV-through-storage layout
equivalence holds for std430-expressible CBV layouts and inherits the
standing MatrixStride-12 silent-misread hazard — R15 must layout-audit
pointees and fail closed on the hazardous shapes rather than serve them
wrong.

## Appendix: evidence inventory (this lane)

- Vendored source citations: state.c:536–600, 780–830, 7820–7920,
  8150–8260; dxil.c:135–300, 796–870, 1436–1470, 1540–1660, 1810–1860;
  dxil_spirv.cpp (subproject):392–570, 1095–1100; spirv_module.cpp:
  405–417; dxil_converter.cpp:1029, 3404, 4322; config_flag_decl.h
  (full); device.c:144, 2535.
- Repo citations: vk64bridge.cpp:707 (VA allocator), 5704
  (vkGetBufferDeviceAddress), 4357 (vendorID), audit scan + capability
  loop (~540–650, incl. 33/4427/55/56 precedents and the 5302–5309 array
  rejections); tools/spirvfix/spirvfix.mjs R8
  (`stripUnusedPhysicalStorageBuffer`, line 1225); web/vkwebgpu.mjs +
  vk-protocol.mjs (push-constant UBO group 3, per-draw set
  materialization).
- Census artifacts (scratch tree `bda-research/`, kept with this
  report): `vs.dxbc`, `ps_uav.dxbc` (guest-compiled blobs recovered
  from the uav12dump console log), `ps_uav_bindless.spv/.spvasm`
  (dxil-spirv standalone, built from the vendored subproject),
  `ps_uav_fixed.spv` (post-R8, spirv-val clean), `r8test.mjs`.
- Real-run evidence: `test-results/dxvk-uav-after/console.log`
  (wt-vkd3d) — 4 capability rejections, all `spirv-capability-unknown:5347`.
