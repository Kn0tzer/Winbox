# Merge record — FD-62 serving (lane-fd62serve), 2026-11-01

Composition of `lane-fd62serve` (FD-62 texel-pointer serving, closed
CL-25 on its branch) onto integ. Base: `41882e0fb` (the FD-53+FD-55pipe
deploy record). Result: deploy-ready, **not deployed**. Evidence:
`test-results/merge-fd62serve-20261101/`.

## Composition

The branch forked at `a1e8928ba` and carried eight commits. Three are
patch-equivalent duplicates of commits integ already carries — proven
by `git cherry` (`-`), not assumed — and were **dropped**:

| Dropped | Why |
|---|---|
| `aaf4182de`, `c7c2409f7` | FD-55 pre-warm packaging picks; integ carries the same work via the FD-55pipe composition |
| `de92a9ee1` | Refusal-logging; patch-identical to integ's `72360dda5` |

Five commits were cherry-picked, in branch order:

| Composed | Source | Content |
|---|---|---|
| `068ad3f67` | `24becdb53` | FD-62 serving fix: texel-pointer atomics rewritten to storage-buffer form (spirvfix R12A + bridge mirror + page-tier binding), fixtures, native guards, unit suites |
| `dbbd3e97d` | `cdecd2fa6` | Five vulkan citation re-pins |
| `5a1060905` | `2e890f315` | Texel format tables as if-chains (no-silent-failure guard) |
| `ea7283041` | `7d8673a56` | Legacy corpus gate re-baseline on measured sets (+3 citation re-pins) |
| `096bd9bab` | `be9669e76` | Register: CL-25 closure, FD-72 + FD-73 filed, FD-55 annotation; lane record |
| `8803de521` | settlement | CL-25 placement repair (below) |

Composed diff vs `41882e0fb`: exactly 25 files (+2404/−296) — the
union of the five picks, nothing extra. The composed **code is
byte-identical to the branch's** for every file the branch touched
(verified by diff against `lane-fd62serve` over source/tools/web/
testdata); the only diffs in that span are integ-side files the branch
never carried (FD-53's staging exclusion + completeness check, the
pre-warm capture driver's fast-engine mode). The gate build's wasm
(`a2e1059d…`) is byte-identical to the serving lane's own in-vivo
build wasm — the composed tree reproduces the lane's proven artifact.

### Conflicts and their resolutions

- **`docs/known-gaps/vulkan.md`** (fix pick): citation-line hunks
  only — integ's pins predate the fix's +315-line bridge shift.
  Resolved to the branch's audit-verified pins, which are the correct
  ones for a composed bridge byte-identical to the branch's. Proven
  after the fact by the citation audit (351 rows, zero drift, zero
  missing) and by a row-identity comparison: 381 register rows before
  and after, the same rows throughout, differing only in re-pinned
  line numbers. No integ-side row content existed to lose (integ's
  post-base vulkan.md changes were re-pins only).
- **`docs/defect-register.md`** (register pick), section-A tail:
  integ's last entry is FD-71; the branch appended FD-72/FD-73 at the
  same anchor. All three entries kept, in numeric order.

### ⚠️ Placement repair, on the record (settlement `8803de521`)

The register pick auto-merged the CL-25 block **cleanly into the
wrong place**: after CL-19, before the Appendix — its context (a
section-A entry quoting CL-23's tail) matched there. The lane's own
composition note in CL-25 requires the closure to **follow FD-53's
CL-24**. The post-pick audit caught the misplacement (closure bullets
did not read in order); the settlement commit moves the CL-25 block
**verbatim** to after CL-24. Closure bullets now read CL-01…CL-25 in
document order, zero duplicates. (Standing layout, unchanged:
CL-20…CL-24 already sat after the Appendix header in the
pre-composition register — a previous composition's append point,
not this one's doing.) A clean cherry-pick is not proof of a
faithful one; the audit is what caught this — the same lesson as the
FD-69 repair in the previous batch.

### Register settlement (this lane's audit)

- **FD-72 and FD-73 stand as filed.** Numbering audited against
  integ's register before composition: FD-70 and FD-71 are integ's
  highest entries and neither 72 nor 73 appears anywhere in it.
  FD-72's entry already records this settlement expectation.
- **CL-25 (FD-62) stands exactly as the branch wrote it**, naming
  fix `24becdb53` and its three-level proof; the FD-62 entry header
  carries the lane's "CLOSED 2026-11-01 — see CL-25" marker.
- **FD-55 annotated textually, as the evidence now shows:** the
  serving lane's update block was appended inside FD-55's entry,
  after the pre-warm and pipeline-creation annotations (placement
  grep-verified). It records the in-vivo result verbatim: the
  byte-identified module created twice with no refusal, zero audit
  refusals and zero pipeline-creation E_FAILs across 254 module
  creations, the demo still presenting nothing (no swapchain;
  submissions dropped at the bridge's view guard, logged), and the
  entry staying open on FD-70 + FD-72 and that downstream behavior.
  FD-55's closing condition is updated accordingly: FD-62's
  condition is discharged; FD-70 and FD-72 remain.
- Counts (line-anchored bullet audit, same method before and
  after): FD entry bullets 67 → 69 (+2 = FD-72, FD-73), zero
  duplicates; closures CL-01…CL-25 in order.

## The legacy re-baseline — which case changed, and why that is justified

The branch re-baselined the legacy corpus gate (`7d8673a56`), and a
moved baseline is exactly where a quiet regression can hide, so the
gate treated it as a claim to prove, not a number to accept:

- **Pre-composition baseline, measured on `41882e0fb`** (evidence:
  `spirvfix-corpus-BASE.log`): `web/tests/spirvfix.test.mjs`
  subtest 2 — "every corpus module converts through toWGSL(spirv)
  with the pre-pass" — FAILS, first failure
  `atomic-texel-heap-copy.spv: unexpected rewrite set`. This is
  exactly FD-64's "one spirvfix corpus case" from the previous
  gates' 109/5 baseline. The old gate table pinned 11 corpus
  members; the corpus has carried heap-era members for several
  batches, and the gate died on the first one it met.
- **The re-baseline itself** (diff read in full): every corpus
  member's measured applied-rewrite set is now tabled; a new
  REFUSED table pins the members that must keep throwing, each
  with its named reason (FD-62's boundary — escape/mixed →
  ImageBuffer, r64ui → Int64, r8ui/weak → ImageBuffer — plus the
  standing unknown-format storage-image and sampled-buffer
  classes, and the captured Godot module →
  `UnsupportedBuiltIn(23)`, which is FD-72). The test logic is
  **strengthened, not weakened**: refused members must now keep
  throwing with the reason named, and the applied-pass census
  expands from 6 pass names to the full 20-pass vocabulary.
- **Post-composition gate result**: legacy group 115 tests,
  **110 pass / 4 fail**. The failing set is exactly the previous
  gate's four vkwebgpu cases (identical subtest names — "two
  sampled views…", "a DXVK-shaped frame…", "legacy sampler
  records…", "malformed frames…", FD-64's V2 DESC_SET family).
  The **only** delta against the baseline is the corpus case
  flipping fail → pass. Nothing else moved in either direction.

## Gate (evidence: `test-results/merge-fd62serve-20261101/`)

The runtime-assets set was restaged before building per the
established split: retained-archive sha256 verified against the
manifest (`9978166f…`); four members extracted from it and
pin-verified per file; `wine64.zip` (pin `53d77f33…`) and
`wine32.zip` (pin `7f2209cf…`) copied from the live site's exact
copies and pin-verified (FD-73 records why the tar's inner wine
zips are not used). `bootstrap.py --check`: "Runtime assets
verified (13 files)".

| Check | Result |
|---|---|
| Dist build (`tools/bw64-build.sh`) | ✅ `Browser export verified`; `BUILD COMPLETE`; support archive **`c4dbe35986a5…` (517,564,189 B) — byte-identical to FD-53's proven pack**, reproduced again; BUILD_INFO git_sha `8803de521`, wasm `a2e1059d…` (= the serving lane's in-vivo build wasm) |
| Vulkan citation audit | ✅ 351 rows: MATCH 232, MATCH-INHERITED 14, UNCHECKABLE 98, EXEMPT 7 — zero drift, zero missing |
| JS unit suite (`runner.mjs --unit`) | ✅ **347 passed, 0 failed** (integ baseline 346 + the texel suite's net growth; the rewritten texel suite passes 12/12 inside it) |
| Legacy JS group (`node --test web/tests/*.test.mjs`) | ✅ 110 pass / 4 fail — failing set identical to FD-64's standing vkwebgpu set; sole delta vs baseline is the corpus case, justified above |
| Packaging completeness (`check_runtime_completeness.py`) | ✅ **0 failures**, 1 standing skip; FD-53's member-uniqueness + shim-pin checks active against the built pack and passing |
| Native vkd3d harness (`run-native.py`) | ✅ **Completes end to end — exit 0, 54 PASS lines**: the FD-62 guard now passes asserting the class is *served, attributed per array*, the new texel-cone boundary guard passes (value form served; weak/sub-word/64-bit/mixed/escape refuse by name), FD-16's guards intact |
| Native suite, main flavour (serial) | ✅ **663/663**, 0 FAILED (240 s) |
| Native suite, x64 flavour (serial) | ✅ **62/62**, 0 FAILED (20 s) |
| Session start, 64-bit (`boot64-smoke`, hardened rules) | ✅ PASS (159 s) |
| Session start, 32-bit (`boot32-vkboot`, hardened rules) | ✅ PASS (54 s) |

## State of the register's open items after this composition

- FD-62 is **closed** (CL-25) and now composed.
- FD-55 stays open on FD-70 + FD-72 and the downstream
  submit/swapchain behavior the in-vivo run exposed.
- FD-72 (HelperInvocation translation) and FD-73 (runtime-assets
  archive/manifest mismatch) stand open as filed.
- FD-70 and FD-71 unchanged.

## Notes

- The park lane's consumer-run sequence (wt-fd12) ran concurrently
  throughout; all probe work went through the slot system one at a
  time; no other lane's processes, builds, or trees were touched,
  and no test runner was stopped by pattern.
- The gate ran detached on the box (stamp file
  `gate.done`); the launching SSH channel was held open by the
  detached script and cleared locally without disturbing it.
- One discarded baseline attempt: an early full-group baseline
  run's output was misdirected to /dev/null by a redirection error
  in this lane's launch command and the run was abandoned; the
  baseline that stands is the targeted corpus-file run recorded
  above, which measures exactly the case the re-baseline moves.
- The integration tree's generated state (`dist`, `web/runtime`,
  `project/linux/Build`, `__pycache__`) is stripped after this
  record; status is clean except the pre-existing untracked vkd3d
  probe DLL.

## Verdict

Composition: complete and faithful (three patch-equivalent
duplicates recognized and dropped; two conflicts resolved with
their reasoning recorded; one clean-but-wrong auto-merge placement
caught by audit and repaired). Gate: **green on every item**,
including the re-baseline scrutiny and the required
archive-identity reproduction. The composed tree (integ tip after
this record's commit) is **deploy-ready and NOT deployed**.
