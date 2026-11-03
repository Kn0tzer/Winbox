# FD-92 — spirvfix splice ordering: synthesized definitions now land behind the definitions they reference

Lane `lane-fd92` (worktree `/home/ubuntu/wt-fd92`), base integ
`cc1d89ab3`. Fix commit `89ac92ff9` (`tools/spirvfix/spirvfix.mjs` +
`web/tests/suite/unit/spirvfix-fd92.test.mjs`). The FD-92 entry was
carried onto this branch's register from `lane-fd88` (tip `e7381c300`),
where the FD-88 lane filed it; it is closed here with provisional
closure CL-45.

## The defect

spirvfix rules splice synthesized type/constant instructions into the
type section at fixed anchors (the first global variable, the first
constant) without regard to what the synthesized instructions
reference. SPIR-V requires a definition to precede its uses, but
nothing groups the type section: a valid module may define a type
after its first global variable. The captured 369,400-byte Godot
module (`godot-fs-f2b738c259a79b4a.spv`, committed on `lane-fd87` at
`testdata/spirv/`) defines OpTypeBool (id 54) at instruction 333,
past its first global variable at instruction 299. When
`lower-mid-shader-discard` lowered the module's helper queries, it
spliced its synthesized Private and Function pointer-to-bool types at
the first-variable anchor — ahead of the OpTypeBool they reference —
and the fixed module failed the standard validator.

## Reproduction on this base

- Raw module: `spirv-val` clean.
- Base fixup output: `spirv-val` fails — `error: line 301: Operand
  '54[%bool]' requires a previous definition` on
  `%_ptr_Private_54 = OpTypePointer Private %bool`, the filed error
  verbatim (the FD-88 lane measured the same output shape on
  lane-fd87's end state).
- The translator never reaches the defect: the vendored naga refuses
  the module at the capability stage
  (`UnsupportedCapability(SampledImageArrayDynamicIndexing)`, FD-88's
  territory) on both the base and the fixed output.

## The fix

New helper `defsSplicePos(mod, adds, anchor)` in
`tools/spirvfix/spirvfix.mjs`: the earliest position at or after the
rule's anchor that follows every module definition the synthesized
instructions reference. References are identified grammar-aware via
the existing `idOperandUseIndex` (so literal words equal to small ids
cannot false-trigger), plus a constant's `resultType`, which is its
type word. Ids the synthesized block itself defines are the caller's
to order and do not constrain the position. The position only ever
moves later, and never needs to pass the first function — every
referenced definition precedes it in a valid module — so output that
was valid before is byte-identical.

Applied at the three splice sites whose anchors sit inside the type
section:

- `lower-mid-shader-discard` (the measured defect): its bool type,
  bool constants and pointer-to-bool types previously went in as
  three separate splices (before the first constant / first
  variable); they now move as one dependency-ordered block placed by
  `defsSplicePos`.
- `strip-point-builtins`: its synthesized zero constants.
- The texel-pointer rewrite (R12A): its `addTypes`, which previously
  relied on the comment-level assumption that "globals follow all
  types in a valid module" — the assumption this defect disproves.
  Boundary, stated exactly: each referenced element type precedes the
  converted variable whose own pointer chain names it, so the block
  follows every definition it references without passing the
  variables that consume it; the block can pass an earlier converted
  variable only when a module interleaves fresh type definitions
  between two converted variables, a shape no corpus module carries
  (and one the old anchor answered with a use-before-definition of
  the type instead — invalid either way, never a mistranslation).

The remaining splice sites needed no change: `r15SpliceAdditions`
and the R1/R15 families splice at the first-function anchor, which
every module definition precedes by construction; the
specialized-binding-split twins splice before a variable whose own
pointer type they reuse, so their operand precedes the splice point
by the module's own validity.

## Proof

All figures re-read from preserved outputs in `test-results/fd92/`.

- **Validator, captured module**: the fixed output validates
  cleanly under `spirv-val` (SPIRV-Tools v2025.1). The raw module is
  clean; the base output fails with the filed error.
- **Meaning unchanged**: the fixed output is a pure reordering of the
  base output — instruction word-multisets identical (18,352
  instructions, headers equal; compared word-level via `parseSpirv`,
  since `spirv-dis`'s friendly names for the pointer types change
  with their position). No instruction was added, removed or altered;
  no id changed; the applied rewrite set is unchanged.
- **Corpus stays green**: `applySpirvFix` run over the full fixture
  corpus (72 modules: `testdata/spirv/*.spv` + `dxvk-live/*.spv`)
  before and after the fix — every output byte-identical, every
  applied set unchanged. The `spirv-val` sweep over the outputs is
  unchanged too: 63 clean, and the same 9 `atomic-texel-*` outputs
  invalid for a pre-existing, unrelated reason (device-scope atomics
  without the VulkanMemoryModelDeviceScopeKHR capability declaration
  — a capability-declaration matter, not an ordering one; untouched
  by this lane). Corpus gate `web/tests/spirvfix.test.mjs`: 10/10
  with its expectation tables untouched.
- **Guard test** `web/tests/suite/unit/spirvfix-fd92.test.mjs` builds
  the defect's shape in miniature (a global variable before
  OpTypeBool, a helper query, a non-entry discard — both
  pointer-to-bool types synthesized): RED on the base (`spirv-val` on
  the base output: `ID '7[%bool]' has not been defined`; the test's
  ordering assertions fail), GREEN with the fix; the raw and fixed
  minimal modules are both `spirv-val` clean.
- **Full JS surface** (`node tools/run-tests.mjs`): legacy 115
  tests, 110 pass — the 4 failures are `web/tests/vkwebgpu-v2.test.mjs`
  (FD-64's standing set on this base), byte-identical on the pristine
  base by A/B. Suite unit: 198 files pass including `spirvfix-fd92`;
  the single failing file, `provenance-artifacts`, fails identically
  on the pristine base (it requires a built `dist/` export, which a
  lane tree does not carry). No test result is attributable to this
  change.

## Deliberately not done

- The captured module is **not** enrolled in `testdata/spirv/`: the
  corpus gate's idempotency test requires a second fixup pass to
  apply nothing, and this module is not a fixpoint — a second pass
  re-applies `lower-mid-shader-discard`, on the pristine base exactly
  as with this fix (the recorded rule-ordering interaction, cf. the
  gate's own note on `tri9.frag`). Enrolling it would fail the gate
  for a pre-existing reason unrelated to FD-92. The module itself is
  committed on `lane-fd87`; this lane's guard is the minimal-module
  test above, and the validator proof over the captured module is
  preserved in `test-results/fd92/`
  (`godot-fs-f2b738c259a79b4a.spv`, `fixed-base.spv`,
  `fixed-fd92.spv`, disassemblies and sweep manifests).
- No browser build: the change is confined to the shader pre-pass,
  which the corpus gate and unit suites exercise through the same
  entry point the page uses; nothing in the emulator or the bridge
  changed.

## Tree staging notes

The worktree was staged as the gate standard prescribes for this
base: `tools/browser/bootstrap.py --local` from the retained
`runtime-assets-20261101` archive (this base's manifest pins that
release; the 20261102 re-pin lives on `lane-fd65`), restoring 13
runtime assets, plus the `web/vendor` and `node_modules` symlinks to
the main repo, matching the integ tree's arrangement. The staged
runtime assets and both symlinks were removed again at lane end
(generated state stripped per disk discipline); the tracked tree
carries only the fix, the guard test, the register and this record,
and the evidence in `test-results/fd92/` stays.
