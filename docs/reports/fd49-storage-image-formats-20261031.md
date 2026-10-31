# FD-49 — storage images without a format: the multi-type heap class, served

Lane: `lane-fd49` (worktree `wt-fd49`), off integration tip `51d0aff98`.
Fix commit: `df68b0d92` (register row in the follow-up commit names it).
Evidence: `test-results/fd49/` (probes + the captured corpus copy);
suite: `web/tests/suite/unit/vk-storage-format-multi-type.test.mjs`
over fixtures in `testdata/spirv/godot-cs-*.spv` (the captured modules
themselves, copied from `test-results/fd16/godot-proof5/`).

## The class

Six captured Godot compute modules (vkd3d-proton shader dumps from the
demo run) failed translation because they use storage images whose
SPIR-V type declares Image Format Unknown — the format is a property of
the bound view, not the shader. The bridge audit admits compute-only
modules deliberately (the `spirv-compute-entry` carve-out in
`vk64AuditSpirv`): dispatches are served by the V2 compute records and
translation at the page tier is the fail-closed gate. So the whole
defect lived in the page tier, and it was three stacked defects, not
one:

1. **One format per binding.** Five modules declare vkd3d's descriptor
   heap at set 5 / binding 0 with TWO storage image types — a float
   type (read plane) and an int type (write plane) — as runtime
   arrays. R14's signature (`options.storageFormats`) was keyed by
   `set:binding` alone, so it could carry only one view format; the
   other type necessarily stayed Unknown, and injecting the carried
   format into both is barred by R14's own scalar-kind guard. Proven
   on the pristine tree: `'5:0=4'` formats the float type and leaves
   the int type Unknown; `'5:0=27'` the mirror image; both end at
   `UnsupportedCapability(StorageImageReadWithoutFormat)`.
2. **Two clones, one binding.** After R15a specialization, the two
   accessed arrays are scalar clones at the same binding with
   different types. With per-type formats hand-injected, naga
   validation rejects that shape outright: "Bindings for [3] conflict
   with other resource". A WebGPU bind group likewise has one entry
   per binding — the shape is inexpressible as-is.
3. **Dead declarations poison an empty shader.** `e1ca507c`'s entry
   point is a bare return; it declares the same two heap arrays and
   never references them. `prune-specialized-aliases` removes the
   variables, but the orphaned OpTypeImage declarations remain, and
   the translator processes type declarations whether or not a
   variable uses them: `InvalidStorageImageWithoutFormat`.

## The fix (per shape, decided by what the stack can express)

Serve all three shapes; refuse nothing that was refused before, and
keep every refusal that remains fail-closed:

- **Per-type format resolution.** `storageImageVars` now reports a
  `typeKey` per storage variable — the image type's identity apart
  from its format (sampled scalar kind+width, Dim, Depth, Arrayed, MS,
  access qualifier), computed by one helper (`storageTypeKey`) shared
  by the reflection and the injector. The tier's new
  `buildStorageSignature` (wgsl-utils) groups resolutions by binding:
  single-type bindings keep the exact legacy `set:binding=fmt`
  entries; multi-type bindings emit `set:binding@<typeKey>=fmt` per
  type, only when every resolving variable of that type agrees
  (unresolving variables abstain; disagreement omits the type, which
  stays Unknown and fails loudly). R14's injector accepts the typed
  form (it wins over a plain entry for its type); the per-variable
  agreement rule and the scalar-kind guard are unchanged. No plain
  entries are emitted for a multi-type binding, so no type can pick
  up a fallback format that was resolved for a different type.
- **Specialized-binding split** (spirvfix, after R14 — the signature
  is keyed by origin bindings, so nothing may renumber before
  injection consumes it): when specialization leaves two `bw_arr_*`
  clones at one set/binding, clones past the keeper move to fresh
  bindings above the set's maximum. The clone name keeps the origin
  set/binding and index signature; `scanWgslResources` now records
  that origin as the declaration's `origBinding` when the declaration
  sits elsewhere, so bind-time resolution reads the guest's entries
  for the origin binding exactly as before (and the binding-remap in
  vkwebgpu no longer overwrites a scan-recorded `origBinding`).
  Original (non-clone) variables are never renumbered.
- **Dead-declaration prune** (spirvfix, after R14):
  `prune-dead-storage-image-types` removes a format-less storage
  image type — plus its equally unreferenced array/pointer wrappers —
  only when nothing references it (liveness accounting mirrors the
  R15a orphan sweep). If the prune removes the module's last
  format-less storage type, stray 55/56 capability declarations are
  stripped under the same condition R14 uses. A referenced type is
  never touched: unresolved formats keep failing loudly.

The C++ bridge needed no change: its audit already admits this class
by design (compute carve-out) and mirrors R14's contract; the
graphics-path storage-write refusal is a separate, already-precise
guest-visible rejection (`VK_ERROR_FEATURE_NOT_PRESENT` at module
creation) and is untouched.

## Proof

New suite `vk-storage-format-multi-type.test.mjs` (10 tests) over the
six captured modules themselves, driven through the tier's real path
(reflection → `resolveStorageVarFormat` against a synthetic heap
built from each module's own index signatures → `buildStorageSignature`
→ `applySpirvFix` → vendored naga):

- All five read/write modules translate; each yields two storage
  textures at distinct bindings with the resolved formats and access
  modes (e.g. `21fc48e3`: `texture_storage_2d<rgba8unorm,read>` at
  binding 0, `texture_storage_2d<rg8sint,write>` at binding 1, the
  latter with `origBinding 0` and push signature +3).
- The empty shader prunes and translates with no storage declarations.
- Fail-closed sabotage set: legacy one-format signature, partial typed
  signature, and kind-swapped typed signature all still fail with the
  WithoutFormat rejection; with no signature the live types are never
  pruned and translation fails; a fabricated disagreement between two
  resolving variables omits the type's entry.
- Fail-before: on the pristine integration tree the same suite fails
  (the per-type machinery does not exist), and the pre-fix probes over
  the corpus record the exact rejections —
  `UnsupportedCapability(StorageImageReadWithoutFormat)` ×5 and
  `InvalidStorageImageWithoutFormat` ×1 (`test-results/fd49/`,
  probe-replay/probe2/probe3).
- One existing expectation was extended, not weakened:
  `spirvfix-coverage`'s R14 reflection test now also pins the new
  `typeKey` field, and its REWRITES list gains the two new rules.

Full JS unit suite on this branch: `339 passed, 4 failed — provenance-artifacts and runtime-completeness audit the built dist payloads, which this worktree deliberately never built (both fail on their dist-existence checks in any dist-less tree, untouched by this change); vk-descriptor-binding-remap and vk-register-citations were broken BY this change (a source-shape pin and four line-number citations the new code drifted), were fixed in the same commit, and re-verified green standalone (2/2 and 4/4). Log: `test-results/fd49/unit-suite.log``.

## Measured impact on the affected program

From the captured run that produced the corpus (Godot "Squash the
Creeps" demo on FD-16's fixed dist, `test-results/fd16/godot-proof5/
console.txt`): the guest submitted all six modules at ~385 s and the
bridge accepted every one at `vkCreateShaderModule` (the compute
carve-out admissions are logged); vkd3d's pipeline-library thread
created PSOs through the run and flushed its cache cleanly; there is
no pipeline failure and no translation failure anywhere in the
console, and the demo proceeded to present (first window mapped at
320 s, `final.png` captured). So in the captured run **nothing
user-visible was blocked**: vkd3d creates the shader modules eagerly
and defers the rest, and no dispatch in that run reached the tier's
translation of these modules (the console carries no tier translation
log, so a dispatch cannot be proven absent — only that no failure
surfaced and the program ran).

The class is latent, not cosmetic: these are real renderer compute
passes (typed storage reads and writes). On the pre-fix stack, the
first dispatch that builds a pipeline on any of them fails
translation at the tier, permanently and counted, and that dispatch's
work is dropped — the guest is never told (the Vulkan calls all
succeeded). After this fix, such a dispatch translates and runs.

## Scope notes

- Serving is bounded by the same per-format limits as the rest of the
  storage path: a bound view whose format Dawn will not serve as a
  storage texture still fails loudly at pipeline build (the §4.26
  machinery), now with the correct type's format in hand rather than
  a blanket WithoutFormat rejection.
- The merge lane should note: FD-49's open entry lives in
  `docs/defect-register.md` on `lane-fd16`'s branch; this branch adds
  the Section B row. Renumbering map is unaffected (FD-49 keeps its
  number, per the coordinator's composition plan).
