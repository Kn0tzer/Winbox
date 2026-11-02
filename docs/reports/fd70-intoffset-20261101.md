# FD-70 integer-offset root addressing (R15c) — lane record

Branch `lane-fd70int` off integ `1d932714c`, worktree
`/home/ubuntu/wt-fd70int`. Commits: `b837f16c8` (spirvfix
serving + tests), `7f409d71e` (bridge mirror + native guard),
`e47e03a6a` (citation re-pins). Nothing pushed, deployed, or
merged; master and the live site untouched.

## What was refused

The FD-70 register entry's remaining term: two real Godot vertex
modules (`testdata/spirv/godot-vs-4000ddfd359efb05.spv`, 23,440 B;
`godot-vs-ebac7e2d70a20d62.spv`, 16,304 B) whose root-parameter
loads compute their addresses in integer space — push-block base
VA plus a data-dependent byte offset (an instance-derived index x
the 176-byte element stride + a field offset), added to the base's
low half through the OpIAddCarry idiom and bitcast to a physical
pointer. R15b admits only zero-addend push roots with the index in
the access chain, so both mirrors refused the pair
(`spirv-capability-unknown:5347` in vivo, `moduleServable` false
in spirvfix). A census of both fixtures: every materialization is
an OpBitcast of a v2uint built by the carry idiom over push member
0; offsets are `index*176 + C` with C in {16,32,48,64,80,96}
(fixture 1, 7 materializations) and {16,32,48,64} plus one
zero-addend array form (fixture 2, 5 materializations); pointees
are three wrapper Block structs (struct{v4float}, struct{uint},
struct{rt-array v4float}) — the reason the R15b single-pointee
rule rejects the slot; all uses are single loads, no stores, no
atomics.

## The design (R15c, both mirrors)

A slot carrying any integer-offset materialization is served
through a **word view** instead of the typed pointee path:

- **Affine offset classification** (`r15ClassifyDynOffset` /
  `classifyDynOffset`): a 32-bit integer expression classifies as
  {terms: id -> positive coefficient, add} over constants,
  IAdd/ISub (no dynamic subtrahend), IMul by a positive constant,
  and copies. Any other 32-bit SSA value is an opaque term with
  coefficient 1 — sound because the rewrite recomputes the offset
  from the same SSA values. Coefficients cap at 0x7fffffff.
- **Address provenance**: the OpIAddCarry sum-half branch of the
  v2-half classifier accepts a dynamic addend (at least one term;
  a pure-constant addend keeps its historical classification, so
  previously served modules are unaffected); the pair classifier
  folds it into the address provenance, requiring the high half to
  be a pure carry consumer and folding constant adds accumulated
  on the low half into the offset's constant part.
- **Word cone**: every use of every materialized pointer of a
  word-view slot must be an OpLoad of a 32-bit scalar or a 2-4
  component 32-bit vector, reached through access chains and
  copies only, with every chain index folding into the affine
  offset (struct members by Offset decoration, array/runtime-array
  elements at ArrayStride with affine dynamic indices, vector
  components at 4 bytes; matrices reject). Every coefficient and
  the constant remainder of every load's total offset
  (materialization offset + cone offset) must be divisible by 4.
  A store through any cone refuses the slot, naming the loads-only
  word view.
- **Rewrite**: the slot's variable keeps the R15b name/binding
  (`bw_root_p<off>`, descriptor-heap set, binding 4096+off/8,
  NonWritable) but its type becomes a synthesized Block struct
  wrapping a runtime array of u32 (ArrayStride 4). Each recorded
  word load is replaced in place by word-index arithmetic
  (coefficient/4 x term + remainder/4), one access chain + u32
  load per word, and reassembly (direct retarget for a u32 scalar
  load, OpBitcast for float/signed scalars, per-component bitcasts
  + OpCompositeConstruct for vectors). Provenance sweep liveness
  for OpIMul is enabled only in word-view modules; OpName /
  OpMemberName labels on dropped address temporaries are removed
  (they otherwise keep the cone's OpIAddCarry alive into the
  translator, which rejects it). The entry-point interface append
  follows the same SPIR-V >= 1.4 rule as R15a; all currently
  served root modules are 1.6, so no served output changed.

The page tier needed no change: `web/vkwebgpu.mjs` /
`web/wgsl-utils.mjs` resolve `bw_root_p<off>` by name to the push
offset from the ROOT_DESCS record and never inspect the
variable's layout.

## Proof

- **Fixtures**: both take `root-descriptor-bda`; rewritten bytes
  pass spirv-val; the vendored translator converts both (WGSL
  21,302 / 14,696 chars). Eyeball check: word index
  `idx * 44 + 12` for byte offset 176*idx + 48; four word loads
  bitcast and reassembled into the vec4; read-only
  `var<storage>` binding.
- **Corpus**: snapshot over all 148 `testdata/spirv` fixtures
  (applied list + output sha256 each, baseline in
  `test-results/fd70int/corpus-baseline.json`): 146 byte-identical
  after the change; only the pair changed, each gaining exactly
  `root-descriptor-bda`. Corpus gate `web/tests/spirvfix.test.mjs`
  10/10 (expected applied list updated; the pair's REFUSED
  entries removed).
- **Boundary tests** (`spirvfix-fd70.test.mjs`, 11/11): the pair
  served (analysis wordView with 7 / 5 word loads, 5347 stripped,
  memory model back to Logical, no PSB pointer type survives);
  the Aligned-literal collision test kept; three mutations of the
  captured module refuse — stride 176 -> 174, field offset
  48 -> 46, an inserted store through a word chain (refusal names
  the loads-only word view); synthetic sweep fixtures; byte-pinned
  controls. Sabotage: disabling the dynamic branch
  (`const dyn = null`) reddens the pair tests and the store
  test's named-reason assertion; restored.
- **Mirror**: `vk64bridge.cpp` admits exactly the same class.
  Native guard in `vk64_native_test.cpp` builds the class
  synthetically: `analyzeRoot` servable for stride 176 / addend
  48 / loads-only, unservable for the three mutations; end to end
  the audit returns VK_SUCCESS with the 5347 counter unchanged
  for the admitted module and VK_ERROR_FEATURE_NOT_PRESENT with
  the counter incremented for each mutation. run-native 57 PASS
  (56 baseline + 1). The guard caught a real mirror defect during
  the lane: the first `chainAffineStep` struct branch used the
  index constant's id as the member number where the JS resolves
  the constant's value — the audit refused the synthetic module
  (and would have refused the real pair); fixed and re-proven.
  JS source pins for the mirror constructs added to the fd70
  test.
- **Citations**: the edits drifted 132 citations in
  `docs/known-gaps/vulkan.md`; re-pinned by content match (the
  audit tool's --fix applied 33; an independent content-match
  computation agreed with all 33 and supplied the rest). Audit
  back to baseline profile: 352 rows, 232 MATCH, 14
  MATCH-INHERITED, 0 DRIFT, 0 MISSING; unit test 4/4.
- **Browser build + full suite** (the shared-code lesson): clean
  staging only — `bootstrap.py --local` from the retained archive
  ("Restored and verified 13 runtime assets", `--check` exit 0),
  generated payloads copied from the integ tree, module/vendor
  symlinks to the main tree. `tools/bw64-build.sh`: "Browser
  export verified" + BUILD COMPLETE (first launch died on the
  build lock's default wait behind sibling builds, disturbing
  none; relaunched with BW64_BUILD_WAIT_SECONDS=7200 and
  completed through the queue). BUILD_INFO git_sha `e47e03a6a`,
  git_dirty_files 0, wasm sha256 `1cdc208e...`. Full JS unit
  suite on the built tree: **348 passed, 0 failed** (the unstaged
  run's three failures — provenance-artifacts,
  runtime-completeness, vk-register-citations — all resolved
  in-lane as recorded above). Evidence in
  `test-results/fd70int/`.

## Disposition

FD-70 is closed provisionally on this branch — see CL-37 in the
register (CL-35 and CL-36 are held provisionally by the FD-54
and FD-63 branches; the composing merge lane verifies and
renumbers by audit). The one proof the closing terms name that
this lane cannot supply is the pair's creations succeeding in
vivo on a shipped build; that check belongs to the deploy gate
that carries this branch, and the closure text says so.
FD-55's remaining terms (FD-72, FD-74, and the downstream
submit/swapchain behaviour) are untouched by this lane.
