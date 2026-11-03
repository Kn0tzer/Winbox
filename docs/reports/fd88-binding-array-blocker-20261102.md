# FD-88 — the blocker is the browser's bind-group API, not the translation stack

Lane `lane-fd88` (worktree `/home/ubuntu/wt-fd88`, off integ
`cc1d89ab3`), 2026-11-02. FD-88 narrowed to its proven blocker; no
product code changed. A second defect found on the way is filed as
FD-92. Evidence: `test-results/fd88/`; minimal fixtures committed as
`testdata/spirv/fd88-*`.

## The claim under test

FD-88 (filed by the FD-87 lane) records the 3D sampled-image runtime
array at set 3 / binding 0 of the captured 369,400-byte Godot module
(`godot-fs-f2b738c259a79b4a.spv`, variable `%23`) as unservable
because two of its seven access chains add a term loaded through a
root-descriptor pointer (`>> 16` / `& 0xFFFF` of a GPU-memory load),
making the element index data-dependent. The register named the
gating exactly: "true binding arrays in the translation stack. The
translator (naga) and WGSL have no runtime-sized texture arrays."

That gating does not survive measurement. Link by link:

## Link 1 — the translator serves the class (naga 30.0.1, vendored)

Eight hand-assembled fixtures in the captured module's own form —
runtime or fixed array of 3D sampled images at set 3 / binding 0, a
separate runtime sampler array, `OpSampledImage` — varying the array
form (runtime / fixed-4), the index form (constant / push-block load
/ storage load reduced by `>> 16` & `0xFFFF`), and the capability
set. All are spirv-val clean. Results (`test-results/fd88/matrix.log`):

- Every fixture **declaring** capability 29
  (SampledImageArrayDynamicIndexing) is refused by the translator
  with `UnsupportedCapability(SampledImageArrayDynamicIndexing)` —
  regardless of array form or index form.
- The **identical** data-dependent runtime-array module **without**
  the declaration (fixture `l_rt_data_nocap29`) translates. The
  emitted WGSL is correct, not a stub: `enable wgpu_binding_array;`,
  `@group(3) @binding(0) var global: binding_array<texture_3d<f32>>;`,
  and the sample call carries the exact data-dependent index
  expression, `textureSample(global[((_e13 >> bitcast<u32>(16u)) &
  65535u)], …)`. The push-indexed form (fixture `n_rt_push_nocap29`)
  likewise. So the translator's refusal is a capability-table entry,
  not a missing feature: the construct underneath is fully served.
- spirvfix behaves as the register describes: servable forms
  specialize to scalar clones; the data-dependent form is left
  untouched with its refusal (fixture `h_rt_data`).

On the captured module itself: with the cap-29 declaration removed
(word-level excision of exactly the 2-word `OpCapability`
instruction; everything else byte-identical) and one unrelated
mis-ordering in the fixup's output repaired (FD-92, below), the
translator accepts the module's entire type/global section — all
seven chains into `%23` included — and its next refusal is
`UnsupportedInstruction(Function, IAddCarry)`. Cone analysis over
the fixed module (`test-results/fd88/cones.mjs`): the module
contains exactly one `OpIAddCarry`, and **none** of the seven `%23`
index cones contains it; it belongs to a root-address cone
(FD-70/R15 territory, which production fixup rewrites with a
pipeline signature — the FD-87 probes, and this one, run the fixup
with an empty signature). The FD-72 helper-invocation refusal
stands behind that. Neither is FD-88's construct.

## Link 2 — Tint parses the shader form (Chromium 145)

Raw WebGPU probes in the project's own Chromium (Playwright
chromium-1208, HeadlessChrome/145.0.0.0, SwiftShader adapter,
probe-slot discipline; `test-results/fd88/webgpu-binding-array-probe.log`,
`bindgroup-array-probe.log`):

- Naga's verbatim output is rejected for its `enable
  wgpu_binding_array;` directive ("expected extension — possible
  values: clip_distances, dual_source_blending, f16,
  primitive_index, subgroups"). The directive is a wgpu-native gate;
  Chrome's Tint does not know the name. Dropping the line is a
  tier-side rewrite.
- The unsized form is rejected: "'binding_array' requires 2
  template arguments". Chrome's WGSL requires the sized form
  `binding_array<texture_3d<f32>, N>` — the size must come from the
  guest layout's descriptor count, substituted by the tier.
- `binding_array<sampler, N>` is rejected ("element type must be a
  sampled texture type"). Sampler arrays have no WebGPU form; in
  the real chain this costs nothing, because the module's sampler
  heap array is specialized into scalar clones by spirvfix (its
  chains are push-derived).
- With those two adjustments the fragment module **compiles**
  (shaderModule: OK) at texture counts 4, 1024 and 65536.

## Link 3 — the proven blocker: bind groups in Chromium 145

- `createBindGroupLayout` accepts a texture entry with `count` 2, 4,
  1024, 65536 without error — but the count is not honoured:
  pipeline validation against a count-2 layout reports "Binding
  type in the shader is a binding_array with 2 elements but the
  layout only provides 1 elements".
- `createBindGroup` with an array of texture views as the entry
  resource **throws synchronously** at WebIDL conversion: "Failed
  to read the 'entries' property … Failed to read the 'resource'
  property … Failed to read the 'buffer' property from
  'GPUBufferBinding': Required member is undefined" — the
  `GPUBindingResource` union in this build has no array member.
- The alternate spelling — one scalar entry per element — is
  rejected by validation: "binding index 0 already used by a
  previous entry".
- (Curiosity, not a path: a single scalar view against a count-2
  layout passes this build's validation; the remaining elements
  would be unbound.)

So in the project's serving browser there is **no way to populate
an arrayed texture binding**: shader and layout objects exist, but
no bind group can ever satisfy them. That is the single external
blocker, and it is a property of Chromium 145's WebGPU
implementation (Blink WebIDL + Dawn), not of the translation stack.

## The project's own links are shaped and ready

- spirvfix's `analyzeDescriptorArrays` already isolates exactly
  this class (an array is unservable *only* because a chain's index
  is unclassifiable, with all other uses audited: no escapes, no
  stores, no texel forms). The serving rule, when the browser lands,
  is a declaration strip for that verdict alone — plus the audit
  mirror admitting capability 29 under the same verdict
  (`vk64bridge.cpp`'s capability loop and `analyzeArrays`).
- The tier's WGSL scanner already records array counts; its
  bind-group builder already assembles per-element view arrays and
  pushes `{binding, resource: views}` for `arrayCount > 1` — in
  spec form, i.e. it would reach the same browser throw. Its
  missing pieces are the two named in Link 2: sizing the unsized
  declarations from the guest layout count carried in the pipeline
  signature, dropping the `enable` directive, and a
  `layoutEntryFor` arm carrying the count (today it returns null
  for binding-array types and the pipeline fails `decl-kind`).

## Why no code changed

Both mirrors currently refuse the class by name at module creation
(`spirv-capability:SampledImageArrayDynamicIndexing` /
`spirv-capability:RuntimeDescriptorArray`), failing closed with
`VK_ERROR_FEATURE_NOT_PRESENT`. Admitting the module today would
move the failure to pipeline/draw time in the browser — an uncaught
throw or a dropped draw — which is strictly worse and would break
the campaign's no-silent-failure rule. The refusals stay until a
serving browser honours arrayed texture bindings; FD-88's register
entry now carries the revised closing terms and stays OPEN,
re-gated on the browser.

## FD-92 — found on the way (filed, not fixed)

spirvfix's end-state output for the captured module
(`test-results/fd87/godot-369400-d4da162f-fixed2.spv`) is not
spirv-val clean: "Operand '54[%bool]' requires a previous
definition". The fixup's synthesized Private/Function
pointer-to-bool types (ids 17439/17440) sit at instruction 299;
`OpTypeBool` (id 54) is defined at instruction 333. The raw module
is clean. The FD-87 lane never saw it because the translator
refuses that module at the capability stage, before type parsing
reaches the mis-ordered types, and its spirv-val check ran on the
sibling fixture. Any strict consumer of the fixed module —
including the translator the day FD-88's and FD-72's refusals lift
— rejects it. Filed as FD-92 with closing terms (splice placement
must respect dependencies on later-defined module types; proof =
spirv-val on this module's fixed output, corpus unchanged).

## Evidence index (`test-results/fd88/`)

- `asm/` + committed `testdata/spirv/fd88-*` — the eight fixtures
  (`.spvasm` sources, `fd88-gen.py` generator, `.spv` binaries).
- `matrix.mjs` / `matrix.log` — translator × fixup matrix over the
  fixtures; `dump-wgsl.mjs` — the emitted WGSL for the served forms.
  (WGSL dumps also inline in this record's Link 1.)
- `captured-naga*.mjs/.log`, `cones.mjs`, `id54.mjs` — the captured
  module isolation: capability strip, the FD-92 repair
  (`godot-369400-fixed2-repaired.spv`, spirv-val clean), cone
  analysis placing the lone `OpIAddCarry` outside all seven array
  chains.
- `webgpu-binding-array-probe.mjs/.log`,
  `webgpu-bindgroup-array-probe.mjs` / `bindgroup-array-probe.log`
  — the Chromium probes (naga-verbatim WGSL variants; the isolated
  bind-group contract: layout, pipeline validation, array resource,
  duplicate entries, single view).
- `spirvfix-fd87.mjs`, `reg-fd87.md` — working copies of the FD-87
  lane's fixup and register, the state FD-88 was filed against
  (this lane's base `cc1d89ab3` predates that branch; FD-88's entry
  is carried onto this register with its narrowing, flagged for the
  composing merge).
