# FD-67 lane record — the harness abort is a guard-attribution defect, not a bridge admission defect (2026-11-01)

Lane: `lane-fd67` (worktree `wt-fd67`), off integ tip `abe1ba591`.
Task end state: `run-native.py` completes end to end with FD-62's
descriptor-array audit guard passing as written, because the bridge's
module-admission decision agrees with the classifiers' recorded
verdicts for the texel-pointer class.

**Outcome: the end state was NOT reached, and no bridge fix was made,
because the evidence shows the guard itself misstates the intended
contract for the modules it exercises.** Per the lane's standing
instruction — never edit or route around the guard; if the evidence
shows the guard misstates the contract, stop and report with proof —
this record is that report. The register's FD-67 entry carries the
same correction; FD-67 stays open, its closing terms reduced to the
register's own second branch (guard corrected to the implemented
attribution). Nothing was pushed, deployed, or touched outside this
lane's tree; the live site and master are untouched.

## Baseline (the failure, reproduced on the pristine branch)

- `python3 tools/vkd3d/tests/run-native.py` dies with SIGABRT
  (`test-results/fd67/run-native-before.log`).
- The same build run directly: **43 PASS lines**, then
  `vk64_native_test.cpp:3481: Assertion 'module({heapTexelAtomic.begin() + 5, heapTexelAtomic.end()}) == VK_ERROR_FEATURE_NOT_PRESENT' failed`
  (`test-results/fd67/run-native-before-direct.log`). Identical to
  the merge gate's measurement.

## Diagnosis step 1 — the audit never reaches its R15 section for this call

A scratch diagnostic build (a temporary `fprintf` in
`vk64AuditSpirv` after the R15 analysis, since reverted) prints one
line per audit call. The FD-16 guard's module calls print verdicts
(`servable` / `skip5302` as expected); the FD-62 guard's first
`module()` call prints **nothing** before the abort
(`test-results/fd67/run-native-diag.log`). So the audit returns
before the R15 section for this module — the recorded-verdict wiring
further down cannot be the operative cause, whatever its state.

The only returns before that point are the malformed-module returns
and the compute-only acceptance. The fixture is well-formed (its
classifier asserts in the same guard pass; the JS fixtures are
spirv-val clean), leaving the compute acceptance.

## Diagnosis step 2 — the guard's fixtures are compute modules

Decoding the header sections of the fixtures (mechanical word walk):

| Fixture | Entry model |
|---|---|
| `heapTexelAtomic` (native guard) | 5 (GLCompute) |
| `heapTexelAtomicCopy` (native guard) | 5 (GLCompute) |
| `heapTexelAtomicTwo` (native guard) | 5 (GLCompute) |
| `atomic-texel-heap.spv` (JS twin) | 5 (GLCompute) |
| `atomic-texel-heap-copy.spv` (JS twin) | 5 (GLCompute) |
| `atomic-texel-heap-twoarrays.spv` (JS twin) | 5 (GLCompute) |
| `atomic-texel-heap-escape.spv` (JS only) | 5 (GLCompute) |
| `godot-fs-atomic-texel-heap.spv` (the captured FD-62 instance) | **4 (Fragment)** |
| `heapTexel` / `heapTexelMixed` (FD-16 refusal/admission fixtures) | 4 (Fragment) |
| `heapTexelMultisig` (FD-16, admission-only asserts) | 5 (GLCompute) — see FD-69 |

`vk64AuditSpirv`'s compute policy is explicit in the source
(vk64bridge.cpp, the block ending at the `spirv-compute-entry` count,
line 1903): a compute-only module is **accepted at creation** so
`vkCreateComputePipelines` can hand out a real pipeline object
(vkd3d-proton builds internal compute pipelines during device
creation and treats creation refusal as device-fatal); its
dispatches are served by the V2 compute records, and a module the
page tier cannot translate **fails there, permanently and counted** —
the fail-closed gate for compute. "The capability/storage/SSBO-write
checks below protect the graphics translation path at creation time
and are not duplicated here." A texel-pointer compute module is
therefore accepted at creation by design and refused at translation
(naga rejects `OpImageTexelPointer` on every backend — FD-62's own
measurement).

## Diagnosis step 3 — on the graphics path, the class already refuses by name (probe)

Probe: the harness's own preamble and create-module path (guest
memory layout, `call64` route and device creation copied from the
guard's `module()` helper), run against the **pristine** bridge on
this branch. Source preserved at `test-results/fd67/fd67_probe.cpp`;
full log at `test-results/fd67/probe-before.log`. Results are the
`vkCreateShaderModule` return value and the deltas in the bridge's
unsupported counters:

| Case | Result | Counter deltas |
|---|---|---|
| Captured `godot-fs-atomic-texel-heap.spv` (Fragment) | **-8 (VK_ERROR_FEATURE_NOT_PRESENT)** | `spirv-capability:RuntimeDescriptorArray` +1, `spirv-capability:ImageBuffer` +1 |
| `atomic-texel-heap.spv` as authored (GLCompute) | 0 (VK_SUCCESS) | `spirv-compute-entry` +1 |
| `atomic-texel-heap.spv`, entry-model word flipped 5 -> 4 only | **-8** | `RuntimeDescriptorArray` +1, `ImageBuffer` +1 |
| `atomic-texel-heap-copy.spv` flipped 5 -> 4 only | **-8** | `RuntimeDescriptorArray` +1, `ImageBuffer` +1 |
| `atomic-texel-heap-twoarrays.spv` as authored (GLCompute) | 0 (VK_SUCCESS) | `spirv-compute-entry` +1 |
| `atomic-texel-heap-twoarrays.spv` flipped 5 -> 4 only | **-8** | `RuntimeDescriptorArray` +1, `ImageBuffer` +1, `spirv-storage-image-write` +1 |

One word toggles the outcome. With the fixtures authored as
graphics modules, every bridge-level assertion in FD-62's guard —
refusal with VK_ERROR_FEATURE_NOT_PRESENT, the
RuntimeDescriptorArray counter advancing exactly +1 per module,
including the two-array case where one array stays servable — holds
on the unmodified bridge. (The probe runs the .spv twins with their
own headers where the guard's `module()` substitutes a header; the
audit's verdicts here depend on the instruction stream and the
entry-model operand, not on the header's version/bound words, and
the guard's own classifier assertions already pass on the fixtures
as authored, confirming classification is stage-independent.)

The graphics-path mechanism, for the record: texel formation fails
the array classifier's per-chain servability check closed
(`cu.texelFormed > 0` -> bad), so no array of the class is servable,
`analyzeArrays` leaves `skip5302` false, and the capability loop
refuses capability 5302 by name
(`spirv-capability:RuntimeDescriptorArray`, vk64bridge.cpp:1984).
Nothing in the audit needs to consult `texelAtomicByVar` for this
outcome.

## Why the bridge was not changed

1. **There is no admission hole on the path the defect travels.**
   FD-62's instance is a fragment module; it and its class refuse at
   creation by name today (probe case 1). The merge lane's FD-67
   mechanism ("nothing consumes `texelAtomicByVar`") is literally
   true but not operative: any check placed in the audit's graphics
   section is unreachable for the guard's fixtures, because the
   compute acceptance returns first.
2. **Refusing the class on the compute path would override a
   deliberate policy, not repair a wiring slip.** The compute
   acceptance is documented, load-bearing (device-fatal refusals
   for vkd3d-proton's internal compute pipelines), and its
   fail-closed gate (translation) does refuse this class. It would
   also change creation verdicts for compute modules, breaking
   FD-62's own hardening promise: "zero modules change verdict".
3. **The guard's remaining content is sound.** Its classifier-level
   assertions pass as authored (they are stage-independent), and
   its bridge-level assertions are exactly the graphics contract —
   they become true, unedited, the moment the fixtures are graphics
   modules, as FD-16's refusal fixtures already are.

## What closes FD-67 (for the guard-correction pass)

- Re-author the three native fixtures' entry points as Fragment
  (model 4) — three words across three arrays — or substitute the
  captured fragment module's shape; the guard then passes as
  written and `run-native.py` runs to completion end to end. (The
  JS fixtures may stay compute: the JS classifier assertions are
  stage-independent and the JS suite passes; aligning them too is
  optional consistency, not correctness.)
- In the same pass, address FD-69 (below).
- No bridge change, no JS change, no register verdict changes.

## Secondary finding — FD-69 (provisional)

`heapTexelMultisig` (FD-16 guard) is also a GLCompute fixture
(model 5). Its assertions — admitted, SampledBuffer /
RuntimeDescriptorArray counts unchanged — are satisfied through the
compute-accept return rather than the R15a admission its PASS line
narrates. The class's servability is genuinely proven on the JS
mirror and over the guard's graphics fixtures, so the vacuity is
attribution-only; it is registered as FD-69 so the same correction
pass can re-author it as a graphics module (its class does pass the
graphics path: multi-signature arrays classify servable and the
capability strip applies) or narrow the guard's claim.

## Evidence index (`test-results/fd67/`)

- `run-native-before.log` — `run-native.py` on the pristine branch:
  SIGABRT.
- `run-native-before-direct.log` — the harness binary run directly:
  43 PASS lines, abort at `vk64_native_test.cpp:3481`.
- `run-native-diag.log` — instrumented-audit build: FD-16 module
  calls print R15 verdicts; the FD-62 guard's first module call
  prints none (return before the R15 section).
- `fd67_probe.cpp`, `probe-before.log` — the create-module probe
  and its six measured cases (table above).

## Register actions on this branch

- FD-67: correction appended (mechanism, probe summary,
  disposition: closing terms reduce to the guard-correction branch;
  entry stays open until the correction lands and the harness
  completes).
- FD-69 filed (provisional number) for the `heapTexelMultisig`
  attribution vacuity.

No product code, test, or JS file was modified on this branch.
