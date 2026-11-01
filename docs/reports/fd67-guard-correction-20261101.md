# FD-67 guard-correction record — the audit guards now exercise the graphics admission path (2026-11-01)

Lane: `lane-fd67fix` (worktree `wt-fd67fix`), branched from the
FD-67 investigation branch tip `05fb4b62b` (`lane-fd67`), whose
register correction and FD-69 filing are preserved and composed
with this work.
Task end state: `run-native.py` runs to completion end to end,
with the descriptor-array audit guards passing on assertions that
genuinely exercise the admission path their PASS lines describe;
FD-67 and FD-69 close on the branch with the completed harness
run named as their proof; no product code changes behaviour.

**Outcome: the end state is reached.** Commits `5e3d1e2c7`
(guards' fixtures) and `8cc080568` (the same-pass storage find).
Nothing was pushed, deployed, or touched outside this lane's
tree; master and the live site are untouched.

## Baseline (reproduced on the pristine branch)

`python3 tools/vkd3d/tests/run-native.py` dies with SIGABRT at
`vk64_native_test.cpp:3481` — FD-62's guard, first bridge-level
assertion (`test-results/fd67fix/run-native-before.log`).
Identical to the merge gate's and the FD-67 lane's measurements.

## The correction

Each fixture below declared an `OpEntryPoint` of GLCompute
(model 5). `vk64AuditSpirv` accepts compute-only modules at
creation by deliberate policy and returns before its R15
section, so the guards' bridge-level assertions were satisfied
through that return instead of the graphics admission path they
narrate (the FD-67 lane's diagnosis, preserved in FD-67's
register correction). The fixtures stand for graphics-path
modules — the captured FD-62 instance
(`testdata/spirv/godot-fs-atomic-texel-heap.spv`) is a Fragment
module — so each fixture's entry-model word is re-authored
5 -> 4, exactly one word per fixture, located by an
instruction-stream walk and verified against the diff (four
lines, one word each; the remaining diff lines are comments):

| Fixture | Guard | Words | Model |
|---|---|---|---|
| `heapTexelAtomic` | FD-62 | 210 | 5 -> 4 |
| `heapTexelAtomicCopy` | FD-62 | 214 | 5 -> 4 |
| `heapTexelAtomicTwo` | FD-62 | 256 | 5 -> 4 |
| `heapTexelMultisig` | FD-16 (FD-69) | 304 | 5 -> 4 |

No assertion was added, removed, reworded, or routed around.
No bridge, JS, or other product code changed; the script-side
fixtures stay as authored (their classifier assertions are
stage-independent) and the JS suites are untouched — the merge
gate's 346/0 over this content stands.

## Proof

- **Before:** official runner, SIGABRT at the guard (above).
- **After:** `python3 tools/vkd3d/tests/run-native.py` runs to
  completion end to end on the committed branch — exit 0,
  **53 PASS lines**, including
  `PASS: GUARD descriptor-heap texel arrays are admitted when
  R15a-servable and refused by name otherwise (FD-16)`,
  `PASS: GUARD texel-pointer atomics through a descriptor-heap
  array refuse by name, attributed per array (FD-62)`, and
  `PASS: GUARD multi-signature bare-image descriptor-heap
  texel arrays are admitted (FD-16)`
  (`test-results/fd67fix/run-native-final.log`).
- **Attribution (FD-69's "R15 section demonstrably reached"):**
  a scratch instrumented build (a temporary `fprintf` after the
  audit's R15 analysis and at the compute-accept return, since
  reverted byte-identically) run with the corrected fixtures
  (`test-results/fd67fix/run-native-diag.log`):
  - FD-62 trio, in guard order: `AUDIT-R15 nw=210 / 214 / 256`,
    all `skip5302=0 arrServable=0` — the R15 section is reached,
    no array is servable, capability 5302 refuses by name;
    the guard's counter assertions (+1 per module) pass.
  - Multisig: `AUDIT-R15 nw=309 skip5302=1 arrServable=1`, no
    compute-accept line — admitted through the R15a strip, as
    its PASS line narrates.
  - The only remaining compute-accept lines in the run belong
    to the V2 compute sections, whose PASS lines narrate
    compute serving — acceptance there is the measured
    behaviour by design.

## Same-pass find — `heapTexelStorage` (FD-69's class)

The instrumented run showed one more compute-accept inside a
guard section: the FD-16 section's storage fixture
(`heapTexelStorage`, 208 words, entry model 5 — it was
distilled from a compute shader, like the multisig fixture),
whose admission assertion was likewise satisfied through the
compute-accept return. Flipping its entry model in a scratch
build (`test-results/fd67fix/storage-flip-probe.log`) shows the
call reaching the R15 section (`nw=213 skip5302=1
arrServable=1`) and the guard passing with assertions
untouched, so it is re-authored as a Fragment module in this
same pass (`8cc080568`) rather than left as a second known
instance of the vacuity FD-69 files. Recorded in FD-69's entry;
CL-23 covers both fixtures.

## Evidence index (`test-results/fd67fix/`)

- `run-native-before.log` — official runner on the pristine
  branch: SIGABRT at `vk64_native_test.cpp:3481`.
- `run-native-final.log` — official runner on the finished
  branch: exit 0, 53 PASS lines, both guard sections passing.
- `run-native-diag.log` — instrumented scratch build with the
  corrected fixtures: per-call audit attribution (R15 reached
  vs compute-accept), ordered against the PASS lines.
- `storage-flip-probe.log` — instrumented scratch build with
  only `heapTexelStorage` flipped: its call reaches R15
  (`nw=213`, servable), guard passes, run completes.

## Register actions on this branch

- FD-67: marked CLOSED (CL-22) — closing terms were the
  register's second branch (guard corrected to the implemented
  attribution; harness completes), discharged here.
- FD-69: marked CLOSED (CL-23) — both fixtures re-authored as
  graphics modules; the guard passes with the audit's R15
  section demonstrably reached; the `heapTexelStorage`
  same-class find is recorded in the entry and covered by the
  same closure.
- The FD-67 lane's correction text is preserved verbatim; a
  one-sentence pointer to CL-22 is appended to FD-67's entry.
