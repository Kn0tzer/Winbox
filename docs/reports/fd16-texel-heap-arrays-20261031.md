# FD-16 — descriptor-heap texel/sampled runtime arrays rejected at shader creation

Lane `lane-fd16` (worktree `wt-fd16`), off integration tip
`f3fd57d58`. Fix commits: `9d7b86428` (spirvfix + tier +
fixtures/tests) and `e7377591b` (bridge audit mirror + native
guards + citation re-pin). Register: FD-16 closed — see CL-13;
FD-50 and FD-49 opened by this lane (below).

## What was broken

A D3D12 guest reaches a `Buffer<T>` SRV through a descriptor heap:
vkd3d-proton emits a runtime-sized array of sampled images over a
Dim=Buffer image type, indexed by root constants. Two classifiers
have to agree that shape is servable — the bridge audit
(`vk64AuditSpirv` in `source/vulkan/vk64bridge.cpp`) at
vkCreateShaderModule time, and the SPIR-V→WGSL translator
(spirvfix + vendored naga) in the page. Both refused it, so
pipeline creation failed with `0x80004005` and the Godot demo
never produced a first frame.

The refusal had two layers. The first layer was visible from
synthetic fixtures; the second only appeared when the fixed
build was run against the real program and its actual shader
modules were captured and replayed through the translator
(`test-results/fd16/godot-capture/`, 27 dumped modules; the
failing PSO modules are `7ad22adecec4515c.spv` and
`b01f14e5fa9b27f6.spv`).

Layer 1 (composition and classification):

- spirvfix ran the texel-buffer conversion (R12) before
  descriptor-array specialization (R15a) and never again after,
  so a specialized clone — by then an ordinary direct texel
  variable — was never converted, and naga rejected the module
  with `UnsupportedCapability(SampledBuffer)`.
- The bridge audit admitted capabilities 46/47 via a servability
  test that only recognized a *direct* UniformConstant variable;
  the same type reached through an array variable read as
  unservable and was rejected by name.
- Three further defects in the same machinery, each a silent
  wrong answer rather than a refusal: when a direct texel
  variable and a texel heap array shared one image type, R12
  traced the array's loads onto the direct variable (fetches
  would have read the wrong resource); R1's combined-sampler
  split treated deferred texel variables as ordinary combined
  samplers; R15a left a dangling `OpName` for removed array
  variables (spirv-val-invalid output), and R12 left a dangling
  `OpCopyObject` of a converted variable after specialization.

Layer 2 (the real modules' shapes):

- **Multiple index signatures per heap base.** The real heap
  variable is indexed by five access chains carrying four–five
  distinct root-constant signatures (two heap bases, with
  constant offsets). R15a refused any array with ≠1 signature,
  and the audit mirrored the refusal.
- **Bare-image texel form.** The real modules load the bare
  `OpTypeImage` (Dim=Buffer, Sampled=1) and fetch directly —
  no `OpTypeSampledImage` wrapper, no sampler. R12 and the
  audit only recognized the wrapped form.
- **Coherent clones.** Storage clones inherited the array
  variable's `Coherent` decoration; naga rejects a clone
  variable carrying it ("global variable is invalid").
- **Orphan-sweep literal collision.** The dead-type sweep
  counted every operand word as an id reference, so an
  `OpConstant` whose *value* equalled the runtime-array type's
  id kept the dead type (and capability 5302) alive forever;
  the bridge's settlement simulation had the same bug.
- **Binding-slot conflicts.** Five clones cannot share one
  (set, binding) in WGSL — naga rejects the module ("bindings
  conflict"). Clones past the first need synthetic slots in
  the same set, with the tier resolving them against the guest
  heap binding.
- **Pre-1.4 interface listing.** The specializer appended
  clones to the entry-point interface unconditionally;
  spirv-val rejects UniformConstant variables in a pre-1.4
  interface list, and the real modules are SPIR-V 1.3.

## The fix

spirvfix (`tools/spirvfix/spirvfix.mjs`):

- R15a serves an array when **every** access chain classifies
  ({constant} or {root-constant member + constant addend});
  one scalar clone per distinct signature. Clone 1 keeps the
  guest (set, binding); clones 2..n take fresh slots in the
  same set past its current maximum binding. Clone decorations
  drop `Coherent`. Entry-point listing is version-aware:
  ≥1.4 replace-or-append, <1.4 the list is left untouched.
  Removed variables lose their `OpName`.
- A post-specialization R12-family pass converts the clones
  exactly like direct texel variables. R12 recognizes the
  bare-image form (in-place conversion, no split), attributes
  loads through `OpCopyObject` roots, vets pointer copies, and
  excludes array-shared types per translation form (a type
  used by both a direct variable and an array is converted
  only once it is no longer array-shared — the mixed fixture
  pins that the two fetches keep their own resources). R1 no
  longer splits Dim=Buffer sampled images.
- The orphan sweep's liveness scan treats the constant family
  (ops 41–46, 50–52) as carrying no id references in its
  operand words.

Bridge audit (`source/vulkan/vk64bridge.cpp`) — the mirror,
changed instruction-for-instruction with the above:
`vk64TexelBufferServable` now takes the R15 servable-array set;
array variables over a converted type must all be in that set;
load attribution accepts a copy root in the direct variables
or a single-index chain over a servable array variable; bare
image variables are admitted; `analyzeArrays` accepts any
number of classifiable signatures; the skip-5302 settlement
simulation excludes constant operand words the same way. One
deliberate divergence is documented in the code: spirvfix's
array-sharing exclusion counts array *types*, the mirror
counts array *variables*; a module in the gap still fails
loudly at naga, never silently.

Tier (`web/wgsl-utils.mjs`): a `bw_arr` declaration whose
group matches now always carries its `arrSig`, and a
declaration sitting on a synthetic slot records
`origBinding` = the guest binding from its name, which the
tier's texture/sampler resolution already prefers.

## Proof

- **Fixtures** (`testdata/spirv/`, hand-authored in the
  vkd3d-proton heap idiom, spirv-val clean; sources in
  `testdata/spirv/src/`): heap array, mixed direct+heap over
  one shared type, two arrays, storage array, query-consumer
  refusal, unclassifiable-index refusal, the multi-signature
  bare-image module distilled from the captured Godot compute
  shader (SPIR-V 1.3), and the coherent storage array.
- **JS tests**: new suite `spirvfix-texel-heap.test.mjs`
  (11 tests) pins servability, per-signature clones, synthetic
  slots, `arrSig`/`origBinding` resolution, widened
  `textureLoad` coordinates, the 1.3 interface rule, the
  Coherent drop, and the constant-collision sweep. Two R15a
  tests that pinned the old one-signature refusal were
  re-purposed to pin the new per-signature contract at the
  same rigor (the refusal direction stays pinned by the
  unclassifiable-index and store/escape cases). Combined run
  over the five SPIR-V suites: 88 pass / 0 fail. Sabotage:
  with spirvfix stashed, the new suite goes 2 pass / 6 fail.
- **Native guards** (`tools/vkd3d/tests/vk64_native_test.cpp`):
  the same fixture bytes embedded; heap/mixed/two/storage and
  multi-signature modules must return `VK_SUCCESS` with the
  SampledBuffer/ImageBuffer/RuntimeDescriptorArray rejection
  counters unchanged; the query and bad-index modules must be
  refused with exactly the named counter increments. Run via
  the FD-50 scratch method (see below): RC=0, 52 PASS lines
  including both FD-16 guards. Sabotage: with the bridge patch
  stashed, the guard fails at the first heap module.
- **Full JS unit suite**: 340 passed / 1 failed, the failure
  being `vk-register-citations` — caused by this lane's bridge
  edit shifting cited line numbers, and repaired by re-pinning
  `docs/known-gaps/vulkan.md` through the exact old→new line
  map of the lane's own diff (93 rows moved, zero citations
  inside changed hunks; audit DRIFT 0, guard 4/4). Final
  recount on the finished branch: **341 passed, 0 failed**.
- **Real captured modules**: all five texel-heap modules from
  the Godot capture translate through naga after the fix
  (before: `UnsupportedCapability(SampledBuffer)` /
  `RuntimeDescriptorArray`). The fixed `7ad22adece` module is
  spirv-val clean under vulkan1.3, carries five texel clones
  at set 2 bindings 0–4 with the four synthetic-slot clones
  resolving to guest binding 0, and its entry-point interface
  is untouched.
- **Product path**: dist rebuilt from this branch ("Browser
  export verified", "BUILD COMPLETE"; dependency archive
  `de592964f772`) and the Godot demo run through the real
  launcher (`test-results/fd16/godot-proof5/`, firstframe
  capture harness). Before/after on the guest's own stream —
  before (layer-1 dist, `godot-proof/`): one
  `spirv-capability:SampledBuffer` and one
  `spirv-capability:RuntimeDescriptorArray` rejection, then
  `ERROR: Create(Graphics)PipelineState failed with error
  0x80004005`. After: **zero** capability rejections, zero
  occurrences of "failed" in the whole console, 61
  `vkCreateShaderModule` calls, the same 25 module dumps
  extracted, and vkd3d-proton's pipeline-library disk cache
  flushing with wakeup counters up to 106 — the game's
  pipeline set compiling through instead of dying at the
  first pipeline. The guest was still active when the
  harness ended the session at 410 s by design.

## Staging composition (lane-local)

The dist's wine64.zip was staged from the live base container
with the project's own `stage-wine8-backports.sh`: member audit
1257 members, name set identical to base, exactly one content
difference — win32u.dll → `49b87438…`, i.e. guest-identical to
the container pinned on the integration branch. The zip bytes
differ from that pin (the staging script recompresses with
run-time stamps), so `tools/browser/runtime-assets.json` is
re-pinned lane-locally (209,678,123 B, sha256 `4c9850e9…`) and
the runtime artifact manifest regenerated — the same
composition the FD-40 lane committed and the FD-41 merge
re-pinned again to its own reproduction. The merge lane should
expect to re-pin once more.

## Registered by this lane, not fixed here

- **FD-50** — the native test harness aborts at the fn-921
  `vkGetPipelineCacheData` assertions on the pristine
  integration tip (the bridge writes through a guest address
  the harness's bounded memory model rejects), so
  `run-native.py` never reaches the sections after it —
  including the SPIR-V audit guards. Verified as pre-existing:
  identical abort on unmodified `f3fd57d58`.
- **FD-49** — five captured Godot modules fail translation
  with `UnsupportedCapability(StorageImageReadWithoutFormat)`
  and one with `InvalidStorageImageWithoutFormat`: storage
  images used without a format qualifier, a different class
  from FD-16 (no capabilities 46/47 involved). In the
  fixed-dist run the guest submitted the same modules again
  (they are among the proof5 dumps) without any failure
  surfacing in the console — vkd3d creates the shader
  modules and defers the rest; whether a pipeline using one
  of them fails later, or the page tier rejects them at
  translation time when a pipeline is actually built, is
  exactly what FD-49 leaves to measure.

## Disclosures

- The first dist built from this branch (layer 1 only) still
  failed the Godot run at the same PSO; that failure is what
  convicted the layer-2 shapes above. The closure rests on
  the rebuilt dist and the captured-module evidence, not on
  the first build.
- The demo-matrix harness (which samples presents and
  display pixels) never got a clean run at the fixed dist:
  two attempts dead-booted with the frozen-metrics
  signature of the FD-01 start family (one SDL present,
  guest clock frozen), and a third was killed by this lane
  after a 17-minute boot crawl under heavy cross-lane load
  (load average 12 on a 4-CPU box; the batch-3 merge gate
  was building and testing concurrently, and the
  probe-slot/build-lock interlocks serialized most of the
  afternoon). The product-path claim above therefore rests
  on the capture-harness run's guest stream — the same
  stream that convicted the defect — plus the
  captured-module translations, not on a presented-frame
  count. A presented-frame confirmation through
  demo-matrix remains a reasonable merge-gate check.
