# Merge record — FD-53 + FD-55 pipeline/pre-warm composition (2026-11-01)

Lane: composition of `lane-fd53` and `lane-fd55pipe` (the latter
carrying `lane-fd55prewarm`'s packaging commits) into
`integ/winbox-20261022b` (worktree `winbox-integ-20261022`), on top of
the deploy-record tip `a1e8928ba`. **Outcome: composition complete
and faithful; the full batch gate is GREEN on every item, including
the gate build reproducing FD-53's proven support archive
byte-for-byte. The composed tree is deploy-ready. Nothing was
deployed; nothing was pushed; master and the live site were not
touched.**

## Composition

| Integ commit | Source | Content |
|---|---|---|
| `1e228a7ed` | `lane-fd53` (fast-forward) | FD-53 fix: support-archive staging excludes the distro Vulkan loader under both names; `add()` refuses duplicate member names; completeness gains the member-uniqueness + shim-pin check; runtime-artifact manifest regenerated |
| `6f0034fdf` | `lane-fd53` (fast-forward) | FD-53 register closure CL-24 + lane record — preserved exactly as written on the branch |
| `b21375ce8` | `lane-fd55pipe` ex `lane-fd55prewarm` `2fa876399` | Pre-warm packaging: capture driver, warmed-archive packager, first seed set; `web/demos.mjs` names the warmed archive |
| `17ec89150` | `lane-fd55pipe` ex `lane-fd55prewarm` `3241b04c0` | Pre-warm iteration 2 seed merge (67 seeds) |
| `bfb99e2cd` | `lane-fd55pipe` ex `lane-fd55prewarm` `bc9c3a5e0` | Capture driver optional fast-engine mode (banking state only) |
| `1deffaf9a` | `lane-fd55pipe` `72360dda5` | Both bridges' `vkCreateShaderModule` audit-failure paths log the refused module's size+djb2 hash (log-only; verdicts unchanged) |
| `02edfe14e` | `lane-fd55pipe` `14290fc87` | Vulkan citation re-pin for the lines the logging change shifted |
| `45df1fbda` | `lane-fd55pipe` `b14341dae` | Four further citation re-pins (content-verified) |
| `03a4b4d61` | `lane-fd55pipe` `afeebe7bc` | Pipeline-creation record + register: FD-55/FD-62 annotated, FD-70 filed provisionally |
| `df05684ef` | `lane-fd55prewarm` `8edea306b` | Pre-warm record + register update (FD-55 update block + the speed entry, provisionally numbered FD-67 on that branch) |
| `a7b26e29f` | `lane-fd55prewarm` `4932856cf` | Pre-warm record test-note correction |
| `6cb3dd4a0` | this lane | Register numbering settlement + placement repair (below) |

Composed diff vs `a1e8928ba`: exactly 13 files — the union of the two
branches' changes, **minus** `tools/browser/runtime-assets.json`:

- **Dropped at composition: `lane-fd55pipe`'s `c8793ccfd`** (a
  lane-local re-pin of the runtime-assets manifest to that tree's
  post-export payload bytes). Its own commit message assigns the
  re-pin to the composing merge's own reproduction. Integ's committed
  manifest was instead verified directly at this gate: the staged set
  was restaged per the established split (4 members from the retained
  archive `/home/ubuntu/backups/winbox-runtime-assets-20261026/`,
  `wine64.zip` pin `53d77f33…` and `wine32.zip` pin `7f2209cf…` from
  the live site's exact copies, 7 slang files already present) and
  `bootstrap.py --check` reported "Runtime assets verified
  (13 files)". Carrying the lane-local pins would have pinned another
  tree's post-export bytes as canonical.

## Register settlement (this lane's audit)

Numbering was settled by auditing the register on integ, not by
assumption: **FD-68 is a real entry on integ** (a module-declaration
finding, cross-referenced from FD-61 and FD-66), FD-69 is taken
(closed CL-23), and FD-70 was free.

- **FD-70 confirmed as FD-70** (the pipeline lane's late-wave class:
  non-specializable descriptor runtime array + used-form
  physical-storage-buffer addressing). The "filed provisionally /
  final numbering is the composition lane's" phrases in the register
  entry and the lane record now record the settlement.
- **The pre-warm lane's speed entry renumbered FD-67 → FD-71**
  ("The Godot demo's raw cold first-run startup is far too slow to
  fit the standard demo window"), with the renumbering and its reason
  noted in the entry itself and in both places the pre-warm record
  cites it. Next free number after the audit was 71.
- **CL-24 (FD-53) stands as the branch wrote it**; closures now run
  CL-01…CL-24 in order, zero duplicates.
- Final audit of the composed register: **68 unique FD entry
  bullets, zero duplicates** (the previous composition's audited 66,
  plus FD-70 and FD-71; FD-46/FD-56/FD-57 remain absent by earlier
  renumbering, unchanged).
- **Placement repair, on the record:** cherry-picking `8edea306b`
  applied with a clean auto-merge, but its FD-55 update block landed
  inside **FD-69's** entry — the hunk's context phrase from the old
  register also occurs there. The post-pick audit caught it (the
  speed entry's duplicated FD-67 title plus placement greps). The
  settlement commit moves the block into FD-55's entry, ahead of the
  pipeline-creation annotation (chronological order), and FD-69's
  entry is verified restored (its text flows directly from
  "…so CL-23 covers both fixtures." to the next entry). A clean
  cherry-pick is not proof of a faithful one; the audit is what
  caught this.

## Gate (evidence: `test-results/merge-fd53-fd55pipe-20261101/`)

| Check | Result |
|---|---|
| Dist build (`tools/bw64-build.sh`) | ✅ `Browser export verified`; `BUILD COMPLETE`; **support archive `c4dbe35986a5…` (517,564,189 B) — byte-identical to FD-53's proven pack**, reproduced by the gate build; BUILD_INFO git_sha `6cb3dd4a0`, wasm `36b184ee…` (delta vs live `11d2e651…` is the refusal-logging change) |
| Vulkan citation audit | ✅ 351 rows: MATCH 232, MATCH-INHERITED 14, UNCHECKABLE 98, EXEMPT 7 — zero drift, zero missing |
| JS unit suite (`runner.mjs --unit`) | ✅ **346 passed, 0 failed** (baseline held exactly) |
| Legacy JS group (`node --test web/tests/*.test.mjs`) | ⚠️ 109 pass / 5 fail — the failing subtest set is **identical to the previous gate's** (diff-verified against `test-results/merge-fd67-20261101/js-legacy.log`): exactly FD-64's pre-existing baseline. Nothing new, nothing hidden |
| Packaging completeness (`check_runtime_completeness.py`) | ✅ **0 failures**, 1 skip (the standing prefix64 built-artifact skip); the FD-53 check (support-pack member uniqueness + Vulkan shim pins) is active against the built pack and passes |
| Native vkd3d harness (`run-native.py`) | ✅ **Completes end to end — exit 0, 53 PASS lines** on the composed tree (bridge refusal-logging change included) |
| Native suite, main flavour (serial) | ✅ **663/663**, 0 FAILED (242 s) |
| Native suite, x64 flavour (serial) | ✅ **62/62**, 0 FAILED (20 s) |
| Session start, 64-bit (`boot64-smoke`, hardened rules) | ✅ PASS (148 s) |
| Session start, 32-bit (`boot32-vkboot`, hardened rules) | ✅ PASS (55 s) |

## State of the register's open items after this composition

- FD-53 is **closed** (CL-24) and now composed.
- FD-55 stays open: its closing condition is FD-62's closing
  condition plus FD-70's class (the FD-62 serving lane is running
  concurrently and is unaffected by this composition).
- FD-70 stays open as filed; FD-71 (raw startup speed) stays open —
  it can only be sized once FD-55's pipeline-creation defect is
  fixed. The warmed archive and the packaging step that produces it
  are now on integ via `web/demos.mjs` and `tools/demo-packaging/`.

## Notes

- The runtime-assets set was restaged **before** building (the
  standing procedure), so the dist build passed on its first attempt;
  no 404 was hit this gate.
- The FD-62 serving lane ran concurrently throughout; all probe work
  went through the slot system one at a time; no other lane's
  processes, builds, or trees were touched, and no test runner was
  stopped by pattern.
- The integration tree's generated state (`dist`, `web/runtime`,
  `project/linux/Build`, `__pycache__`) is stripped after this
  record; status is clean except the pre-existing untracked vkd3d
  probe DLL.

## Verdict

Composition: complete and faithful (one lane-local commit dropped
with its reason recorded; one auto-merge misplacement caught by
audit and repaired). Gate: **green on every item**, including the
required archive-identity reproduction. The composed tree (integ tip
after this record's commit) is **deploy-ready and NOT deployed**.
