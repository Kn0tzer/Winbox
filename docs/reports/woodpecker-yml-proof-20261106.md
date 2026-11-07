# Proof record: queue run from the landed definition commit (Stage 1)

Landing: `.woodpecker.yml` at the repository root, integ commit
`3d9340c07` (merge record `docs/reports/merge-woodpecker-yml-20261106.md`,
integ tip after landing `738d53187`).
Branch: `lane-woodpecker-proof`, worktree
`/home/ubuntu/winbox-project/worktrees/wt-woodpecker-proof`.
Proof run: Woodpecker pipeline **#14**, submitted with
`submit-check integ/winbox-20261022b` from the landed tip
`738d531875010957156f2316453d9b1da71f0ee5`.
Evidence: `/home/ubuntu/winbox-project/checks/evidence/pipeline-14/`
(step logs under `queue-logs/`, downloaded MinIO result logs under
`minio-results/results/`). Every figure below is quoted from those
logs, not from any summary.

## The submission itself is part of the proof

`submit-check` printed `definition file already present at tip;
submitting tip as-is` and pushed the landed tip unchanged to the
queue — no injection commit was added, for the first time. The
queue's own `verify-source` step then confirmed the mount is the
landed commit (`expected commit: 738d53187…` = `mounted commit:
738d53187…`), that a write to it fails (`Read-only file system`),
and that the tree is pristine.

## Per-check comparison (queue vs by-hand on the same commit)

Manual baseline: the batch-9 gate
(`docs/reports/deploy-batch9-20261106.md`) — native serial main
692/0, x64 116/0; run-native 71 PASS; JS 354/0 on a built tree;
legacy 115 passed, 0 failed, 1 skipped; citations 352 rows 0 drift.

| Check | Queue (pipeline 14) | Manual baseline | Agreement |
|---|---|---|---|
| citations | exit 0; 352 rows (232 MATCH / 14 MATCH-INHERITED / 99 UNCHECKABLE / 7 EXEMPT) | 352 rows, 0 drift | identical |
| js-unit | exit 1; 352 passed, 2 failed: `provenance-artifacts`, `runtime-completeness` | 354/0 on a built tree | the 2 failures are the known bare-checkout artifacts — both tests need a built `dist/` the bare checkout has not got — exactly as pipeline 11 |
| js-legacy | exit 0; 116 tests: 115 pass, 0 fail, 1 skipped | 115/0/1 skip | identical |
| native main | build exit 0; run exit 0; 692 tests, 0 FAILED (253 s) | 692/0 | identical |
| native x64 | build exit 0; run exit 0; 116 tests, 0 FAILED (25 s) | 116/0 | identical |
| run-native | exit 0; 71 PASS, 0 FAIL | 71 PASS | identical |

All eight recorded exit codes match the manual pattern (citations 0,
js-unit 1, js-legacy 0, native builds and runs 0, run-native 0).
Pipeline status is `failure`, driven solely by the js-unit exit —
the same acceptance shape as pipeline 11, which was accepted with
status `failure` for the same reason. The verdict is therefore:
**the queue reproduces the by-hand light checks on the landed
commit, including their known failures. Proof accepted.**

## Incident during the run (no effect on the result)

The submitting SSH session was closed by the remote host about five
minutes into the wait (`Connection to 129.213.44.0 closed by remote
host`; submit-check exited 255). The pipeline itself runs on the box,
independent of the submitting session, and kept running to
completion (started 00:03, finished 00:17 box time). A detached
waiter on the box (`/tmp/wait-p14.sh`, pid 859569) polled the
Woodpecker API and collected the step logs and the MinIO object list
exactly as submit-check's step 7 does, into
`evidence/pipeline-14/`. The result logs were then downloaded from
MinIO into `evidence/pipeline-14/minio-results/results/` and read
from there. Nothing about the pipeline, its inputs, or its verdicts
depended on the dropped session.

## What this closes

- The definition file is on the integration branch and proven by a
  real queue run from the landed commit. `submit-check` no longer
  injects it for any tip based on the integration branch.
- The queue is the standard door for the light checks
  (`docs/standards/checks.md` §7): citations, JS unit, JS legacy,
  native suites, run-native go through `submit-check`; by-hand runs
  of those checks are iterating only, not evidence for a merge.
- The browser guest suite is unchanged: it still runs by hand under
  checks.md §6, because the queue does not run it. This record does
  not claim otherwise.
- The Stage 0 record (`winbox-project/checks/STAGE0-RECORD.md`) gains
  a Stage 1 section recording this landing and proof.
- No deploy follows from this work: nothing shipped changed, and the
  live site stays batch 9.
