# FD-70 — the second-wave Godot modules: demand proven, two false refusal layers fixed, boundary restated

Lane `lane-fd70` (worktree `wt-fd70`), forked from integration tip
`38085a142`. Evidence under `test-results/fd70/`. Register actions:
FD-70's entry rewritten by update block (stays open, terms restated),
FD-55 annotated, FD-78 filed provisionally for the residual class.

## Outcome

Of the three outcomes the lane could land, the result is the third —
boundary proven with primary evidence — with a genuine fix landed on
the way: two of the three layers that refused this pair of modules
were a single defect in the descriptor-array orphan sweep, present in
both mirrors (spirvfix and the bridge audit's simulation), and are
fixed and guarded on this branch. The remaining layer is the one the
entry now states exactly: physical-storage-buffer addressing in its
used form, over addresses computed in integer space. The pair is
still refused, loudly, for that single true reason.

## The pair is genuinely demanded (artifact hypothesis disproven)

The FD-62 and FD-75 lanes' runs never submitted these modules, which
left open the possibility that they were an artifact of the
renderer's uninitialized state. They are not. On the integration
build the lane forked from (wasm `04e73f59`, byte-identical to the
live site), a standard demo-path capture run with vkd3d shader dumps
enabled (`cap-long1`, deadline 2,700 s) submitted both modules and
saw them refused at creation: 23,440 B (djb2 `28a2394720432fda`) at
2,556.7 s; 16,304 B (djb2 `3eccbc1e60e9f7a4`) at 2,557.6 s and
2,558.5 s — three of the run's five `Create(Graphics)PipelineState
0x80004005` failures, the other two being FD-72's module at
360.6/361.3 s. That is diag2's shape exactly. The earlier runs'
1,500 s deadlines ended before the engine's scene-shader wave
arrives; the wave tracks guest progress, not wall time (in the
post-fix run below it arrived at ~2,089 s in a faster-booting
session). Fixtures harvested from the run's dumps and re-verified
against the refusal log's size+hash:
`testdata/spirv/godot-vs-4000ddfd359efb05.spv`,
`testdata/spirv/godot-vs-ebac7e2d70a20d62.spv`.

Capture note: the stock warm-capture driver does not forward
`BW_TEST_URL_PARAMS` to the guest (the forwarding lives only in the
demo-matrix main flow), so vkd3d dump env vars never arrive through
it — the FD-55 pipeline lane hit the same gap and used an
uncommitted variant. This lane commits its variant
(`web/tests/godot-warm-capture-fd70.mjs`, the stock driver plus the
forwarding init-script) so the next capture lane starts from
committed tooling.

## What the modules actually are

Both are vertex-stage modules from the same vkd3d code path, same
shape: a push-constant root block whose first member is a 64-bit
root VA; a descriptor image runtime array (set 2 / binding 0, texel
images); two buffer-content runtime arrays (the SSBO-offsets block
and the bindless CBV heap — FD-61's class, exempt by design); and
physical-pointer loads. The descriptor array is R15a-servable in
both (one load signature: push member + constant offset), and
spirvfix does specialize it — the register's filed claim that "the
descriptor array is not specializable" was wrong. What gates the
modules is the physical addressing, plus the sweep defect below.

## The sweep defect (mechanism, proven on the bytes)

The descriptor-array orphan sweep decides whether capability 5302
can be stripped by scanning for surviving references to each
specialized-away type. Both mirrors' scans count every non-constant
operand word as an id reference. In each module, exactly one OpLoad
— through a zero-addend root pointer — carries a MemoryAccess group
whose Aligned literal is 16, and the descriptor array's type id is
%16. The literal therefore "references" the type forever: the type
never dies, its pointer type never dies, the capability stays. Two
consequences, one per mirror: spirvfix keeps the module
half-served — and the still-array-owned image type makes the
post-specialization texel pass decline the specialized clone, so the
output also keeps SampledBuffer and the translator rejects the
module for that; the bridge audit's skip5302 simulation refuses the
module for RuntimeDescriptorArray outright.

Proven by a keeper census run under the sweeps' exact rules on the
captured bytes (`keeper-scan.mjs` in the evidence dir): the only
words keeping %16 alive are its pointer type's definition and the
load's Aligned word; patching that one literal to a non-id value in
a replica flips the simulation's verdict, and three admitted control
modules show no such keeper. The codebase already knew this class in
the small — the sweeps skip the constant family with a comment
citing a captured module where an OpConstant value equaled a type
id, and the operand-aware walker's memory-access grammar exists for
the same reason on the rewrite side — but the sweeps' own scans were
never given the same treatment.

A second position in the same class surfaced while building the
guard fixture: OpTypeInt / OpTypeFloat width words (32, in a module
whose swept type id is 32) are also counted as references. Same fix.

## The fix

Both mirrors, same rule: when scanning a candidate-removed set for
references, bound the MemoryAccess operand group of OpLoad /
OpStore / OpCopyMemory / OpCopyMemorySized at the group start (group
contents name constants — alignment, scope ids — never the types
the sweep deletes), and skip the literal-only OpTypeInt /
OpTypeFloat instructions. Comments in both places name the captured
modules and the rule.

Blast radius: a byte-level snapshot of spirvfix output over all 145
pre-existing `testdata/spirv` fixtures, before and after — only the
pair's output changed.

## Guards and gates

- JS unit `web/tests/suite/unit/spirvfix-fd70.test.mjs` (7 tests):
  the pair (applied rewrites, surviving capabilities, served outputs
  rejected by the translator for `PhysicalStorageBufferAddresses`
  and not for SampledBuffer), a literal-value control (the colliding
  Aligned literal patched to 999 yields identical output), two
  hand-built fixtures whose swept type id collides with an Aligned
  literal and with type widths, a negative whose array index is
  unclassifiable (keeps its named refusal), and byte pins on three
  previously admitted Godot modules. Sabotage: with the spirvfix fix
  reverted, 5/7 fail; restored, 7/7.
- Native guard in `tools/vkd3d/tests/vk64_native_test.cpp` (fixtures
  transcribed from `testdata/spirv/src/fd70-aligned-literal-*.spvasm`):
  colliding and twin modules create with the RuntimeDescriptorArray
  counter unmoved; the negative refuses by name with the counter +1.
  Sabotage: with the bridge fix reverted, the harness aborts at this
  guard's first assertion (`module(alignedLiteralSweep) ==
  VK_SUCCESS`); with the fix, `run-native.py` completes at 56 PASS
  (the previous 55 plus this guard).
- Full JS unit suite on the lane tree: 348/348 entries (the 347
  baseline entries plus this lane's test file; its 7 cases also pass
  standalone, and 5/7 go red with the spirvfix fix reverted). The
  first post-fix suite run read 347/348 from its preserved log: the
  bridge edit's +17 lines had drifted 51 mechanically checked
  citations in `docs/known-gaps/vulkan.md` and the citation guard
  failed on exactly that. All 51 citations are re-pinned on this
  branch (33 by the audit tool's `--fix`, 18 hand-pinned to the
  constructs' true lines); the citation audit is back to its exact
  baseline profile (352 rows, 232 MATCH, 14 inherited, 0 DRIFT),
  and the final suite run is green end to end with dist present
  (evidence `test-results/fd70/js-unit-final2.log`).
- Dist rebuild: "Browser export verified", dependency archive
  `c4dbe35986a5` verified, wasm `16f43514`.

## In vivo after the fix

Same demo path, same 2,700 s deadline, fixed build (`cap-long2`):
FD-72's module refused twice by name at 253.0/253.5 s (unchanged);
the pair refused at 2,088.3 s, 2,089.3 s, 2,090.4 s with
`spirv-capability-unknown:5347` as the only logged reason —
RuntimeDescriptorArray refusal count across the run: 3 → 0. Five
pipeline-creation E_FAILs, unchanged: the pair still fails creation,
now for the true reason only. Submissions and the downstream
no-swapchain shape are unchanged (49 `vkQueueSubmit2` lines vs 44).

## The boundary, restated

The pair's root-descriptor loads compute their addresses in integer
space: push-constant base VA plus a data-dependent offset
(instance-derived index × 176-byte stride + 48-byte field offset,
via OpIAddCarry), bitcast to a physical pointer, one load each. R15b
serves a root cone only when the address traces to a single
push-block load with a zero addend and the element index rides the
access chain; the analyzers decline this form by name on both
fixtures (`moduleServable=false`). One materialization per module
does match the R15b array-wrapper form, but the verdict is
per-module. With the false layers removed, spirvfix's served outputs
pass `spirv-val` (vulkan1.1), and the vendored translator's
remaining rejection is `UnsupportedCapability(PhysicalStorageBufferAddresses)`
— FD-72's shape: the capability is used, and nothing sound rewrites
it away. Serving it means a new design (re-rooting a data-dependent
byte offset as typed accesses over a byte view of the resolved root
buffer, with a bridge binding form and a readback story to match).
A partial version would risk the worst outcome this stack can
produce — a shader silently reading the wrong data — so the refusal
stands, by name, until that design exists. FD-70 stays open on
those terms; the two fixtures are the design's specification.

## Filed alongside

FD-78 (provisional): the same raw-word liveness idiom survives in
two sibling sweeps and at further literal operand positions the
operand-aware walker already documents (ExtInst instruction number,
VectorShuffle components, CompositeExtract/Insert indices, OpSwitch
literals, OpLine words, OpFunction/OpVariable leading literals,
OpArrayLength index). No captured module is known to be gated by
those positions; the class is proven live in the small by this
lane's own first fixture draft, which the type-width position
defeated before it was fixed.

Observation, no entry filed: the translator also rejects an OpLoad
carrying an Aligned operand on an ordinary (non-physical) pointer
(`InvalidOperandCount(Load, 6)`), reproduced on a hand-built module.
A census of the captured corpus shows Aligned operands only on
physical-pointer loads — which served paths strip during their
rewrites — and on one OpCopyMemory, so no admitted production module
is known to carry the shape; noted here rather than registered.

## Guardrails kept

Work stayed on the lane branch in its own worktree; nothing pushed,
nothing deployed, master and the live site untouched; the warmed
demo archive was used as-is and the user's personal archive never
touched; the probe slot discipline was observed throughout (another
lane was active on the box for much of the run); generated state in
this tree is stripped before handoff.
