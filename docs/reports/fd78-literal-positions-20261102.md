# FD-78 — literal words no longer count as references in any serving-layer liveness scan

Lane `lane-fd78`, worktree `wt-fd78`, off integration tip
`cc1d89ab3`. Register: FD-78 CLOSED, closure row CL-45
(provisional number; the merge lane renumbers by audit).

## The defect

The SPIR-V serving layer decides what is dead by scanning
instruction words for id references. In its raw form the scan
counts every operand word as a reference, so a *literal* word that
happens to equal a swept type's or variable's id keeps it alive.
The visible failure is FD-70's shape: a module refused one layer
earlier than its true blocker, or a capability silently retained.
FD-70 fixed the idiom at the positions one captured module proved
(a MemoryAccess operand group, OpTypeInt/OpTypeFloat widths) in
the descriptor-array orphan sweep and its bridge mirror. FD-87
later bounded the ExtInst instruction number at the same two
scans. FD-78 is the rest of the class: every remaining literal
position, in every liveness scan of the serving layer.

## What changed

The operand-aware walker (`idOperandUseIndex`, present in both
mirrors) already documents, per opcode, which operand positions
are id references and which are literals. This lane gives it an
**enumerating sibling** — `operandRefIds(byId, ins)` in
`tools/spirvfix/spirvfix.mjs`, `r15::operandRefIds` in
`source/vulkan/vk64bridge.cpp` — which returns exactly the set of
ids the walker would find a use of. A scan routed through the
enumerator counts a word as a reference exactly where the walker
would, and nowhere else. The walker also gained its missing
OpExecutionModeId case (331) in both mirrors.

Five raw-word liveness scans in the fixup layer and one in the
bridge were rewired to the enumerator, each keeping its existing
op-exclusion list:

1. the descriptor-array orphan sweep (FD-70's sweep; its
   hand-written MemoryAccess bound is now subsumed),
2. the storage-image type prune — previously the rawest scan in
   the layer, with no exclusions at all: constant values and
   type widths also counted as references there,
3. the R15b root-cone sweep,
4. the texel-orphan `referencedOutside` scan,
5. the specialized-alias function-body scan,
6. the bridge's skip-5302 simulation inside `analyzeArrays`
   (the orphan sweep's audit mirror).

## Per-position dispositions

| Position | Where it could bite | Guard |
|---|---|---|
| VectorShuffle components | orphan sweep, prune, bridge sim | JS walker + mutation guards; native carrier 6 |
| CompositeExtract indices | same | JS guards; native carrier 4 |
| CompositeInsert indices | same | JS guards; native carrier 5 |
| OpSwitch case literals | same (walker needs the switch target's type from byId) | JS guards; native carrier 3 |
| OpLine words | orphan sweep, prune, cone, texel, alias scans | JS guards on three sweeps + walker test; native carrier 2 |
| OpArrayLength structure index | orphan sweep, bridge sim | JS guards; native carrier 7 |
| OpFunction control mask / OpVariable storage class | prune (function shape), bridge sim | prune fixture `fd78-prune-function.spv`; native function carrier |
| OpConstant literal value | prune (no exclusions existed) | fixture `fd78-prune-constant.spv` |
| ExtInst instruction number | orphan sweep, bridge sim | JS guards (bounded here in full generality; subsumes FD-87's targeted bound — see composition note) |
| MemoryAccess group, OpTypeInt/Float widths | fixed under FD-70 | standing FD-70 guards, unchanged |

Audited and deliberately **not** changed:

- `stripDeadUndefs` counts raw words on purpose: its documented
  contract is fail-closed ("any undef with even one word equal
  to its id is kept — the module then stays untranslated, never
  mistranslated"). Counting literals there can only keep an
  undef, the safe direction.
- The veto scans (`vk64RawIdUse`, the storage-texel variable
  scan, the R8 downgrade blocker census) are refusal scans, not
  liveness: a literal hit can only refuse service, never retain
  a dead entity or admit an unservable one. `vk64RawIdUse`
  already bounds the positions its register rows cite.
- The OpGroupDecorate / OpGroupMemberDecorate operand family
  stays at the walker's fail-closed default full scan: no sweep
  liveness routes through the walker for those ops, and widening
  the walker's table there is a different lane's question.

One behaviour of the fixed sweeps is worth recording: liveness
is a fixpoint. A literal carried by an instruction that is
itself a sweep candidate stops mattering once that instruction
is swept, so a guard carrier must be genuinely live — the
native control case is built accordingly (a struct/pointer/
variable chain in the types area), and an earlier draft whose
carrier was itself sweepable correctly showed no effect.

## Proof

- New JS suite file `web/tests/suite/unit/spirvfix-fd78.test.mjs`
  (31 checks): walker unit tests per position; mutation guards
  that splice one literal carrier of each position into the
  FD-70 fixture (`fd70-aligned-literal-sweep.spv`) and assert
  the array type is still swept; two new assembled fixtures
  (`testdata/spirv/fd78-prune-constant.spv`,
  `testdata/spirv/fd78-prune-function.spv`, spirv-val clean) for
  the prune; mutation guards on captured modules for the cone
  (`d3d12-ps-root-cbv.spv`), texel
  (`godot-fs-atomic-texel-heap.spv`) and alias
  (`godot-cs-c9c003a7487b6126.spv`) sweeps. RED/GREEN verified
  empirically, not by construction: against the base scans every
  carrier keeps its swept entity (array type survives, prune
  does not fire, cone copy kept, texel type kept, alias variable
  kept); against the patched scans every one sweeps clean.
- Native guard in `tools/vkd3d/tests/vk64_native_test.cpp`:
  nine literal carriers, a function-control carrier, and a
  genuine-reference control against the bridge simulation. RED
  on the base bridge (carrier assert fails), GREEN with the
  fix; the full native suite passes.
- Regression: spirvfix corpus tests 10/10; full JS unit suite
  349 passed, 0 failed (the base's 348 plus this lane's new
  test file). Lane build: BUILD COMPLETE, "Browser export
  verified", wasm `c9a30661` (staging from the retained
  `runtime-assets-20261101` archive; DXVK and GStreamer
  payloads copied from the integration tree for the export
  step and stripped again afterwards). The vulkan register's line citations
  drifted by the patch's line insertions were re-pinned
  (audit `--fix` plus a positional pass for multi-occurrence
  constructs); the citation audit reports 0 drift and its unit
  test passes.

## Composition note

Lane-fd87 bounded the ExtInst instruction number at the orphan
sweep and the bridge simulation with targeted code. This lane's
enumerator bounds it as one case of the general rule at the same
two scans, so when the branches compose the merge lane should
keep the enumerator and drop the targeted bounds; FD-87's R18
helper (not present on this lane's base) is untouched by this
change. B
...[truncated 153 bytes]