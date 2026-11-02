# FD-87 — the Godot second-wave sibling shader classes

Lane `lane-fd87` off integ `cc1d89ab3`. Register: FD-87 closed
(provisional CL-42), FD-88 filed, FD-78 updated, FD-70 pointer note.
Evidence: `test-results/fd87/`.

## The end state reached

Every refusal class carried by the two sibling modules that killed
the Godot demo's second-wave pipeline creations on the shipped build
(`9e37e5640` / wasm `d631d254`) is now either genuinely served in the
fixup layer — with the bridge audit mirror admitting exactly the
same class — or characterized on the register with its gating
named. No class remains an unexplained refusal string.

The two modules, byte-identified against the deploy proof console
(`test-results/deploy-fd54-63-70-7980-20261102/godot-proof/console.txt`)
by size + the bridge's djb2 `blob_hash` and harvested from the demo
capture set:

- 369,400 B, djb2 `d4da162f9cbdb948` →
  `testdata/spirv/godot-fs-f2b738c259a79b4a.spv`
- 2,340 B, djb2 `fe166085df72b93f` →
  `testdata/spirv/godot-fs-f1b4e0027d98e2e4.spv`

Both are committed as corpus fixtures.

## What each class was, and its disposition

**Capability 29 (the 369,400 B module).** 29 is
SampledImageArrayDynamicIndexing; the bridge had no name-table case
for it (`spirv-capability-unknown:29`) and naga rejects the bare
declaration outright. Served: spirvfix R19 strips the declaration
when no image/sampled-image array survives the earlier rewrites;
the audit names the capability and admits it exactly when every
used image/sampled-image runtime array is R15a-servable and no
fixed-size image array exists. On this module the declaration
correctly survives — it rides on FD-88's array — and is now refused
by name at both layers.

**SampledBuffer + RuntimeDescriptorArray (the 2,340 B module).**
Entirely dead descriptors. The module's body reads only push
constants and inputs; its three descriptor variables (texel-buffer
runtime array, SSBO-offsets block, bindless-CBV array) are
referenced by no instruction (verified by raw-word scan of the
disassembly). Served end to end: spirvfix R18
(`strip-dead-descriptors`) deletes zero-use descriptor variables,
their decorations, ≥1.4 entry-point interface entries and the types
they orphan (iterated operand-aware sweep, ExtInst- and
leading-literal-bounded); R12X then drops the stray texel
capabilities and R18 the RuntimeDescriptorArray declaration. Fixed
form: capabilities `{Shader}` only, no descriptor variables, naga
translates, spirv-val clean. The bridge mirror treats zero-use
descriptor array variables as dead in the array verdict, in the
skip-5302 simulation, and in both texel mirrors.

**SampledBuffer (the 369,400 B module).** Not a translator problem:
the descriptor-array orphan sweep's liveness scan counted the
OpExtInst instruction-number word as an id reference. This module's
GLSL.std.450 instruction numbers (13/14/29/30) equal the ids of its
specialized-away runtime-array/pointer types, so those types never
died, the texel image type stayed array-owned, and the
post-specialization texel conversion (R12) declined its clones —
silently. This is FD-78's ExtInst position, now proven live on a
captured module (FD-78's entry is updated; it stays open for its
remaining positions and sibling sweeps). With the word bounded in
spirvfix's sweep and in the bridge's skip-5302 simulation, the
conversion runs: all 508 OpImageFetch coordinates widened,
SampledBuffer gone from the fixed module.

**RuntimeDescriptorArray remainder (the 369,400 B module) → FD-88.**
The 3D image array at set 3 / binding 0 has seven access chains:
five with push-constant-derived indices (servable — the same
module's `%15`/`%19`/`%31`/`%42` arrays and its sibling chains
specialize), two whose index adds a term loaded through a
root-descriptor (BDA) pointer (`& 0xFFFF`, `>> 16` of a GPU-memory
load). Data-dependent indices cannot be specialized; per-array
fail-closed leaves the whole array (and its five servable chains)
unserved, keeping RuntimeDescriptorArray and, as its passenger,
capability 29. Gating: true binding arrays in the translation
stack — naga/WGSL have no runtime-sized texture arrays; a
heap-sized fixed array + whole-heap binding would be a page-tier
feature, not a fixup. Filed as FD-88 with closing terms. Boundary
verified on both implementations: fixed-size image arrays are also
outside the specializer's universe (runtime arrays only) and keep
the named refusal (native guard + JS probe).

**Helper invocation (the 369,400 B module).** FD-72's
translator-gated class, unchanged; accounted, not re-derived.

## Changes

`tools/spirvfix/spirvfix.mjs`
- R18 `stripDeadDescriptors` (+ operand-aware liveness helper),
  wired first in `applySpirvFix`.
- R19 `stripStraySampledImageArrayDynamicIndexing`, wired after the
  specialized-binding split.
- Orphan sweep in `specializeDescriptorArrays`: the OpExtInst
  instruction-number word is bounded (FD-78 position).
- Both slugs added to `REWRITES`.

`source/vulkan/vk64bridge.cpp` (mirror)
- Name table: capability 29 = `SampledImageArrayDynamicIndexing`.
- `analyzeArrays`: `imageDynIndexServable` verdict; zero-use
  descriptor array variables treated as dead (excluded from the
  verdict, retired in the skip-5302 simulation, whose scan bounds
  the ExtInst word the same way).
- `vk64RawVarDead`; both texel mirrors no longer gate on dead array
  variables.
- Capability loop: 29 admitted when `imageDynIndexServable`.

Tests: `web/tests/suite/unit/spirvfix-fd87.test.mjs` (5 tests);
corpus EXPECTED/REFUSED updates in `web/tests/spirvfix.test.mjs`;
three native guards in `tools/vkd3d/tests/vk64_native_test.cpp`.

## Measured results

- 2,340 B fixture: fixed capabilities `{1}` (Shader), zero runtime
  arrays, zero descriptor variables; naga translates; spirv-val OK.
- 369,400 B fixture: `texel-buffer-to-2d` applies (508 fetches
  widened); fixed capabilities `1,29,50,51,63,64,5302` — SampledBuffer
  and PhysicalStorageBufferAddresses gone; 29 + 5302 remain as
  FD-88's pair; naga refuses with
  `UnsupportedCapability(SampledImageArrayDynamicIndexing)`, the
  exact characterized remainder.
- Corpus (`web/tests/spirvfix.test.mjs`): green over all 74 modules
  (72 existing + 2 new). R18 fires on 10 pre-existing modules; every
  stripped variable was independently verified dead (zero
  non-metadata references; raw-word scan where the disassembler
  prints friendly names). Consequences recorded in the affected
  expectations: R18 subsumes prune-specialized-aliases on five
  godot-cs fixtures and prune-dead-storage-image-types on two (the
  prune rule is now corpus-unexercised but still unit-covered);
  clone slot numbering in the multisig texel test shifts because
  the dead UBO at binding 3 no longer occupies a slot; the R14
  coverage fixtures gained a genuine use (their storage image was
  header-only and unreferenced — exactly what R18 removes); the
  empty-shader storage test now accepts either pruning rule.
  Idempotency: the two new fixtures join the standing
  demote/discard re-lowering exclusion (the page applies exactly
  one pass), same class as dxvk-live/tri9.
- Native: `tools/vkd3d/tests/run-native.py` 60 PASS (57 standing +
  3 new FD-87 guards: cap-29 admitted/refused-by-name in both
  unservable forms; dead descriptors gate neither capability; the
  ExtInst sweep position).
- JS unit suite: 351 passed / 0 failed.
- Citations: 352 rows, 0 drift (33 re-pinned by the audit's `--fix`,
  20 multi-citation rows re-pinned through this change's exact line
  map, +55/+54 lines by region).
- Browser build: `tools/bw64-build.sh` — "Browser export verified",
  wasm `10c1a10b`, BUILD COMPLETE on the lane tree.

## Observations carried, not filed

- The deploy proof run recorded one delivered guest memory fault
  (`BW64-FAIL guest-memfault pid=10 addr=0x2140bf26c
  rip=0x140301916 err=4 si_code=1 delivered=1 count=1` at 2,116.8 s,
  ~42 s after the pipeline failures; the run continued to its
  deadline). Fixture work did not reproduce it; it is recorded in
  FD-87's register entry as observed and unattributed.
- Pre-existing, not a regression (present in pre-patch spirvfix
  output too): spirv-val rejects the big module's fixed output over
  a forward-referenced private pointer type
  (`%_ptr_Private_54 = OpTypePointer Private %bool` before `%bool`);
  naga, the corpus arbiter, accepts the shape. The 2,340 B fixed
  output passes spirv-val cleanly.

## Still owed

- CL-42's in-vivo clause: the two pipelines' creations succeeding
  on a shipped build — belongs to the deploy gate carrying the
  composing merge; for these pipelines it now gates only on FD-88
  and FD-72.
- FD-88 (data-dependent descriptor-array indexing) and FD-72
  (helper invocation) remain open on their named gating.
