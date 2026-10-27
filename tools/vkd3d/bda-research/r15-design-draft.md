# R15 design draft (working note, 2026-10-27 — finalize into campaign §4.20 after the in-guest census)

## Established facts (verified, not assumed)

1. Route 0 run (dxvk-uav-r15 capture attempt 1 + §4.19): uav12 flag TRUE,
   sweep 18/18, exec stops at `spirv-capability:RuntimeDescriptorArray`
   (cap 5302) in the BRIDGE audit at vkCreateShaderModule, x4 graphics
   cases; 2 uint cases die guest-side E_INVALIDARG before compilation.
2. Standalone dxil-spirv bindless reproduction (census tool, this lane):
   ps_uav module = runtime-array<storage image> at set 3 / binding 0,
   ONE access, index = push@12 + 1 (push-constant byte offset 12,
   constant add 1). VS: no arrays, no pointers. The non-bindless
   conversion has neither 5302 nor 5347 (single image binding) — vkd3d
   does not use that mode.
3. VKD3D_SHADER_DUMP_PATH reaches vkd3d in-guest (its own instance log
   says the var is used). First capture wrote nothing because the dump
   DIRECTORY was a page-created MEMFS dir, invisible to the guest VFS
   (guest visibility needs an existing guest dir or bw64_register_file).
   Rig fixed: dump into Z:\home\username, extract by dump-name pattern.
4. Tier facts: bind groups materialize per draw from DESC_SET records
   (per (binding, arrayElement) entries); resolution today always picks
   element 0 (bindingForElement(bindings, n, 0) at every call site);
   st.pushBlock (merged push bytes) is in scope at draw time; pipeline
   WGSL is built per (pipeId + state + storageSig/border variants) and
   cached; spirvfix options flow from getPipelineFor through
   shaders.get -> toWGSL -> applySpirvFix; generated-name conventions
   parsed by scanWgslResources are precedented (bw_combined_g*_b*_v*).
5. Bridge facts: DescSet stores per-element bindings; emitDescSet runs
   at submit replay per bound set; the V2 replay walks commands in
   order (CMD_PUSH_CONST carries pcOffset/pcSize/pcBytes); Pipeline
   objects retain stage module ids and the pipeline layout id;
   vkGetBufferDeviceAddress assigns Buffer.devVa from g_nextDevVa
   (0x4000000000 + 256MiB stride) — invertible by construction.
6. Audit facts: vk64AuditSpirv rejects 5302 by name unconditionally;
   used-conditional exemptions are the established pattern (33, 4427,
   55/56, 5346/5347). The audit scan already records ptrs/arrays/vars.

## Open questions the in-guest census MUST answer

Q1. In-guest uav12 modules: set/binding of the heap array(s); array
    kind (runtime vs fixed); every access's index classification
    (push@off + const? const? other?); VS shape.
Q2. In-guest root12 modules (CBV/SRV/UAV separately): ConvertUToPtr
    provenance (push u64 pair at byte offset 2k*4?); pointee shapes
    (struct/array/scalar); access opcodes used (Load/Store/Atomic);
    any PtrToU / pointer escapes.
Q3. Which sets do in-guest vkd3d modules declare overall (for the
    root-binding group/binding assignment rule)?
Q4. NonUniform decorations / SPV_EXT_descriptor_indexing presence.
Q5. Godot: reachable at all on this tree? (bisect lane's regression
    says factory creation dies first — expect NO; then Godot census is
    explicitly pending the bisect fix.)

## Design (arrays) — spirvfix R15a `descriptor-array-specialize`

For each OpVariable whose pointee is OpTypeArray/OpTypeRuntimeArray of
a DESCRIPTOR element type (image, sampled image, sampler, texel-buffer
image, or buffer-block struct):
- Collect all OpAccessChain/OpInBoundsAccessChain uses rooted at the
  variable. Classify each site's first index with the def-use walker:
  const c  ->  signature (pushOff = NONE, add = c)
  push@off + c  ->  signature (off, c)
  anything else -> FAIL CLOSED (module untouched; audit rejection
  stands).
- Group sites by signature; one CLONE scalar variable per signature
  (same set/binding/decorations minus array-ness; NonWritable etc.
  preserved). Rewrite: chains with further indices keep the chain
  re-rooted at the clone with the array index dropped; chains with
  only the array index are deleted and their result id remapped to the
  clone variable id.
- Name each clone `bw_arr_g<set>_b<binding>_v<n>_p<pushOff|->a<add>`
  (precedent: bw_combined_*). scanWgslResources parses the suffix into
  decl fields {arrPushOff, arrAdd}; bindingForElement call sites for
  such decls resolve element = (pushOff==null ? 0 : u32(pushBlock,
  pushOff)) + add; bindGroupFingerprint includes the resolved element
  (or the push word) for those decls.
- Strip cap 5302 (and 5306-5309 if declared-and-now-unused — decide
  from census) + SPV_EXT_descriptor_indexing extension iff no
  runtime/fixed descriptor array variables remain.
- A fixed OpTypeArray of descriptors with CONSTANT indices only is
  the degenerate case (signature per constant; clones = elements).
  A fixed array indexed dynamically fails closed (cannot specialize).

## Design (root descriptors) — spirvfix R15b `root-descriptor-bda`

Per research §4.1 with the layout decision: synthesized bindings go in
the SAME set as the module's heap-array set if one exists, else set 0,
at binding = 4096 + rootIndex (rootIndex from push byte offset / 8).
Pointee handling per §4.1 steps 3-5: CBV struct -> read-only storage
(R13 layout audit applies: MatrixStride-12 / non-std430-expressible
pointees FAIL CLOSED); raw/structured -> storage (read-only or
read-write per NonWritable). Rewrite derefs per §4.1; strip
5346/5347 + extension; memory model -> Logical.
Name clones `bw_root_g<set>_b<binding>_k<rootIndex>` so the tier can
recognize root bindings: their descriptors do NOT come from DESC_SET
records but from the new V2 ROOT_DESCS state (below).

## Bridge changes

A. vk64AuditSpirv: implement the SAME classifiers (array-index +
   pointer-provenance) in the audit scan; exempt 5302 (and a used-
   5347) iff every array access / pointer in the module fits the
   servable patterns R15a/R15b implement — no wider, no narrower
   (mirror R8/Route 0 discipline; the two implementations cross-check
   via shared fixtures in the guard tests). Also verify at
   vkCreateGraphicsPipelines that no stage's synthesized root binding
   collides with a real (set,binding) declared by another stage of
   the same pipeline; collision -> loud pipeline rejection.
B. Root-descriptor runtime: Pipeline gains rootDesc[] (per rootIndex:
   kind CBV/SRV/UAV + writable) from the creation-time census. The V2
   submit replay maintains merged push bytes; when the active
   pipeline has rootDescs and the push head words changed, resolve
   each VA pair: va==0 -> null binding; va in [buf.devVa,
   buf.devVa+buf.size) -> (bufferId, va-devVa); unresolvable -> loud
   unsupported + zero binding. Emit new V2 record ROOT_DESCS
   (append-only protocol addition): per slot (bufferObjId,
   absMemOff) — the tier treats them like kind-1 descriptor entries
   for the synthesized bindings and ALSO emits BUFFER_DATA for the
   resolved ranges so content reaches the GPU.
C. Register: the 5302 row + the PhysicalStorageBuffer used-form rows
   gain the Route A carve-outs, dated, with the census scope.

## Tier changes

- vk-protocol.mjs: decode ROOT_DESCS into an op/state (st.rootDescs).
- vkwebgpu.mjs: scanWgslResources name parsing (in wgsl-utils.mjs) for
  bw_arr_* / bw_root_*; bindGroupEntries resolves bw_arr decls via
  pushBlock and bw_root decls via st.rootDescs; fingerprints cover
  both; getPipelineFor threads st-independent data only (rewrites are
  module-determined; no new pipeline key components expected).
- Null root descriptor: bind the tier's zero buffer (D3D12 reads of an
  unbound root CBV/SRV return 0; UAV writes to null are discarded —
  binding a zero/scratch buffer of adequate size approximates this;
  bound size = max block size the WGSL declares).

## Servable subset (to be confirmed by census)

- uav12 exec: YES if Q1 shows push+const indices (expected from the
  standalone shape).
- root12 CBV/SRV/UAV: YES if Q2 shows single-root-source provenance.
- Godot: UNKNOWN until reachable (Q5).

## Fail-closed list (named rejections preserved)

Dynamic index from buffer contents / phi / call; pointer stored,
loaded, or passed across OpFunctionCall; pointer base not a single
root source; offset expr containing a load; pointee layout failing
the R13 audit; >budget root descriptors; synthesized-binding
collision; array of non-descriptor element type.
