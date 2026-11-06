# Standard: combining finished work into the main line

Composition is one lane's job at a time: the **merge lane**. It is
the only work that writes to the integration branch
(`integ/winbox-20261022b`), and it never deploys and never pushes —
deploy is a separate procedure that starts from a verified merge.

## Preconditions

- You own the integration tree for the whole composition. No other
  lane composes concurrently, and pool branches do not move while
  you compose.
- Every pool branch is verified **from disk** before it is picked:
  tip commit re-read, the lane's record read, its register changes
  read, its evidence present. A lane's summary is never a basis
  for composing it.

## Composition

1. Fix the order before picking: stacked branches compose in stack
   order; independent branches in the order that makes register
   settlement easiest. Record the order and the reason in the
   merge record.
2. Cherry-pick each lane's commits, in order, onto the integration
   branch. Every picked commit is accounted for in the merge
   record's composition table (lane commit → composed commit).
   Conflicts are resolved by hand and noted; a pick that cannot be
   reconciled stops the composition and is reported, not forced.
3. **Register settlement.** After all picks, audit the composed
   register mechanically:
   - extract the entry headings (`FD-NN` in section A, `CL-NN` in
     section C) and confirm every number appears **exactly once**;
   - collisions (two lanes claimed one number) are settled here:
     first-composed keeps the number; the later entry is renumbered
     to the next free number with its text preserved;
   - closures claimed provisionally on lane branches become real
     only here: the entry moves A → C naming the fixing commit and
     the proof (recording standard, §4);
   - section B ends a composition empty of the composed branches'
     rows except as its own preamble directs.
   The settlement is one auditable commit, and the merge record
   quotes its result (entry counts per section, duplicates: none).

## Checks

- **Light checks, once per composition** (the batching rule):
  full-diff accounting against the pool branches, citation audit
  0 drift, JS unit suite, native suites serial, run-native
  (checks standard, §§1–5).
- **Full gate, once per batch boundary and at queue end:** a fresh
  dist rebuild of the composed tree from a clean bootstrap —
  restore the newest retained canonical runtime archive
  (`/home/ubuntu/winbox-project/backups/winbox-runtime-assets-<yyyymmdd>/`, named
  by date only; lane-suffixed archives are lane artifacts, never
  the gate source) with `tools/browser/bootstrap.py --local
  <archive>` and require `bootstrap.py --check` at 0 failures
  before and after the build — then the browser build verified,
  every suite in the checks standard at or above its standing size
  with zero failures, and the full browser guest suite with the
  boot census exact.
- Every non-passing item is **adjudicated in the merge record**
  with evidence: product defect (filed on the register; the batch
  does not ship over it), harness defect (fixed as harness work,
  re-proven), or load artifact (re-run preserved). Nothing is
  dropped silently, and a check that cannot fail is not proof.

## The merge record

`docs/reports/merge-batch<N>-<yyyymmdd>.md`, committed on the
integration branch as the composition's final commit: the pool and
order, the composition table, the register audit result, the gate
figures quoted from the runs' own logs (preserved under
`test-results/merge-batch<N>-<yyyymmdd>/` in the integration tree),
and every adjudication. The new integration tip is the record
commit; quote it in your report, re-read from disk.

Verified literally: 2026-11-05 — the register audit and the pick
mechanics were followed on a scratch composition that was then
discarded; the integration branch was not written, because a real
composition is the merge lane's act and one was already running.
See `docs/reports/standards-verification-20261105.md`.
