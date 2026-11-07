# Merge record: checking-queue definition landing (Stage 1)

Base: integ `e3797b787` (batch-9 deploy record; batch 9 live and pushed).
Pool: one branch, `lane-woodpecker-yml`, tip
`fe1496cbd9c00f45851590535984b620d49dff87` (worktree
`/home/ubuntu/winbox-project/worktrees/wt-woodpecker-yml`).
Method: cherry-pick onto the integration branch in the integration
tree. No conflicts (the branch was cut from the base minutes earlier
and nothing else moved).

## Composition table

| Lane commit | Composed commit | Content |
|---|---|---|
| `fe1496cbd` | `3d9340c07` | `.woodpecker.yml` (new, byte-identical to `checks/canonical/.woodpecker.yml`, sha256 `ffc678c8…`), `docs/standards/checks.md` §7 rewritten, landing record `docs/reports/woodpecker-yml-landing-20261106.md` |

## Register audit (verified from disk)

Script `/tmp/audit-verify.py` (the batch-9 audit script, re-run on the
composed tree): **AUDIT: CLEAN** — Section A 189 FD entries
(1..192 except the pre-existing gaps 46/56/57), Section C 133 CL
closures (CL-01..CL-133 each exactly once), no duplicates, no
missing, no extra, 0 CL headers in the Appendix, all 27 settled-entry
spot-checks pass, no stale provisional references, keeper FD-165
xattr entry intact. No register change was made by this composition
(the lane's register diff against its base is empty); the audit
re-confirms the post-batch-9 register is untouched.

## Light checks

- **Full-diff accounting:** the diff base → composed tip is exactly
  the 3 files in the composition table (223 insertions, 6 deletions).
  No code, no runtime asset, no pin, no manifest touched.
- **Citation audit:** run in the composed tree — 352 rows,
  232 MATCH / 14 MATCH-INHERITED / 99 UNCHECKABLE / 7 EXEMPT,
  **0 drift**, identical to the standing distribution.
- **JS unit, native suites (serial), run-native:** these are proven
  by the queue run from the landed commit, not by hand — that is the
  point of this landing. See the adjudication below and the proof
  record `docs/reports/woodpecker-yml-proof-20261106.md`, which
  follows. The queue run's per-check verdicts are compared there
  against the batch-9 manual baseline (native serial 692/0 and
  116/0, run-native 71 PASS, JS 354/0 on a built tree, legacy
  115/0/1 skip, citations 352 rows 0 drift).

## Adjudication

The combining standard calls for the light checks once per
composition, by hand in the merge tree. This composition does the
citation audit and the full-diff accounting by hand (above) and
deliberately leaves JS unit, native suites, and run-native to the
queue run from the landed commit. Reason: running those three by
hand would duplicate the proof run and would itself be the kind of
evidence this landing retires — the queue is now the standard door
for exactly those checks, and the proof run executes them on the
composed tree. A hand run would add no signal the queue run does
not carry, and the queue run is the acceptance the task defines.
Nothing is dropped silently: the proof record names the pipeline
and quotes every per-check verdict from the run's own logs.

## Deployment

None. This landing changes no shipped file: the live site stays
batch 9 (`d50bcc02f`), and no deploy or push follows from it. The
next deploy, whenever it happens, ships whatever the integration
branch holds then.

## New integration tip

The record commit (this file); quoted in the report, re-read from disk.
