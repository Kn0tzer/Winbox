# Landing record: checking-queue definition on the integration branch (Stage 1)

Base: integ `e3797b787` (batch-9 deploy record; batch 9 live and pushed).
Branch: `lane-woodpecker-yml`, worktree
`/home/ubuntu/winbox-project/worktrees/wt-woodpecker-yml`.

## What landed

- `.woodpecker.yml` at the repository root — the single-file Woodpecker
  pipeline from Stage 0 (`winbox-project/checks/canonical/.woodpecker.yml`),
  copied byte-identical (sha256
  `ffc678c89aca7317adbc8bf77bcf504b0c8f298f2cc4bab618b28447a81cb76e`,
  verified by `cmp`). No content change: the canonical file already
  names the moved stack paths (`/home/ubuntu/winbox-project/checks/...`).
- `docs/standards/checks.md` §7 rewritten: the queue is now the standard
  door for the light checks (§§1–5 — citations, JS unit, JS legacy,
  native suites, run-native). A lane proves those checks with a queue
  run, not by hand; by-hand runs of those checks are for iterating only
  and are not evidence for a merge. The browser guest suite (§6) still
  runs by hand — the queue does not run it, and this record does not
  claim otherwise.
- This record.

## Why

Stage 0 (pipelines 1–13) proved the queue reproduces by-hand light
checks exactly, including identical failures. Until now the definition
file rode on top of each submission as one extra commit added by
`submit-check`; from this landing on, a tip based on the integration
branch carries the file itself and is submitted as-is. That is the
whole change: no code, no register entry, no runtime asset, no
shipped behaviour. Nothing is deployed by this landing — the live
site is batch 9 and is untouched.

## Proof

A queue run from the landed integration commit follows this landing
and is recorded separately in
`docs/reports/woodpecker-yml-proof-20261106.md`. It must show, per
check, the same verdicts the by-hand checks give on the same commit
(the pipeline-11 acceptance pattern): citations 352 rows 0 drift;
JS unit 352 passed / 2 failed on the bare checkout
(`provenance-artifacts`, `runtime-completeness` — both need a built
`dist/`, exactly as in Stage 0); JS legacy 115 passed, 0 failed,
1 skipped; native serial main 692 / 0 FAILED, x64 116 / 0 FAILED;
run-native 71 PASS; verify-source proves the mount is the landed
commit and refuses writes. The manual baseline is the batch-9 gate
(`docs/reports/deploy-batch9-20261106.md`): native 692/116,
run-native 71, JS 354/0 on a built tree, legacy 115/0/1, citations
352 rows 0 drift. The pipeline's overall status is expected to be
`failure`, driven solely by the two known bare-checkout unit
failures — as pipeline 11 was accepted with status `failure` for
the same reason.

## Notes

- `submit-check` is unchanged and needs no change: it already submits
  a tip as-is when the tip carries `.woodpecker.yml`, and still adds
  the reviewed file as one commit for older tips without it.
- The checking-queue stack itself (`winbox-project/checks/`) is
  unchanged: same containers, same runner image, same Gitea repo.
  The Stage 0 record (`checks/STAGE0-RECORD.md`) gains a Stage 1
  section when the proof run is recorded.
- Register: untouched by this branch (no entry added, closed, or
  renumbered). The composition audit re-confirms the counts at
  composition time: 189 FD entries (1..192 except the pre-existing
  gaps 46/56/57) and CL-01..CL-133 each exactly once.
