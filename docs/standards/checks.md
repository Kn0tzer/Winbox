# Standard: running checks

Checks run in a fixed order, cheapest first. Every run's raw log is
preserved under `test-results/<your-work>/` in your tree. **Never
trust a delivered summary of a test run — read the run's own log.**
A check's verdict is its failure count; totals grow as tests are
added, and standing totals below are quoted so drift is visible.

## 0. Preconditions

- The tree is prepared (fresh-copy standard) and, for any check that
  audits the built artifact, the browser build has been run in this
  tree (building standard). One unit test, `provenance-artifacts`,
  audits `dist/` and fails until this tree has been built — in a
  prepared tree it is the **only** pre-build unit failure (352
  passed / 1 failed), and that is its correct behaviour, not a
  defect. In a bare checkout that was never prepared,
  `runtime-completeness` fails too, because the runtime assets were
  never staged; the checking queue reproduces both failures on an
  unbuilt, unprepared tip for the same reasons.

## 1. Fast JS checks (no browser)

    node tools/run-tests.mjs

Runs both, and prints `ALL TESTS PASSED` only when both pass:

- legacy: `node --test web/tests/*.test.mjs`
  (standing: 115 passed, 0 failed, 1 skipped of 116)
- unit: `node web/tests/suite/runner.mjs --unit`
  (standing: 353 passed, 0 failed)

Either can be run alone by its own command. The unit suite also
carries two standing mechanical guards: the Vulkan citation guard
(check 2) and the silent-failure baseline (check 3).

## 2. Citation audit

    python3 tools/vk-register-citation-audit.py

Every register row in `docs/known-gaps/vulkan.md` that cites a file
and line is checked mechanically. Gate standard: **0 drift**
(standing distribution over 352 rows: 232 MATCH / 14 MATCH-INHERITED
/ 99 UNCHECKABLE / 7 EXEMPT). `--fix` rewrites a drifted citation
only under the narrow conditions printed in the tool's own header;
everything else is report-only, and a re-pin is a visible edit in
the change that causes it.

## 3. Silent-failure baseline

Enforced inside the unit suite by its scanner test: every file the
scanner returns must measure exactly its recorded allowance of
unlogged failure paths. Gate standard: **baseline unchanged**. A
change that moves a number must move the allowance deliberately, in
the same commit, with the reason in the record.

## 4. Native suites (serial is the gate standard)

Build first (building standard, §2), then from `project/linux/`:

    ./Build/Test/boxedwine 0 0 1
    ./Build/X64Test/boxedwine 0 0 1

The arguments are start entry, count (0 = all), worker count.
**One worker is the gate standard** — the verdict a merge rests on
is the serial one. Standing totals at the last verification:
main 689, x64 113, both 0 FAILED.

Parallel modes (more workers; process shards) exist and are useful
while iterating, but a parallel total only counts when it equals
the serial total exactly. A single failure that appears only under
load is adjudicated by re-running that one test standalone and
serially, with both logs preserved — never by editing the test,
loosening it, or re-running the suite until it passes.

## 5. run-native

    python3 tools/vkd3d/tests/run-native.py

Runs the native bridge harness end to end. Gate standard: exit 0
with every section PASS (standing: 69 PASS). A harness that aborts
partway has failed, however many PASS lines precede the abort.

## 6. Browser guest suite

    node web/tests/suite/runner.mjs --browser              # full suite
    node web/tests/suite/runner.mjs --browser --filter=<name>   # one area

- Browser probes take a slot from `web/tests/probe-slots.mjs`
  (the light kind has 2 slots) and queue when all slots are held.
  Never create or touch `/tmp/bw64-probe.lock` — it is a legacy
  mechanism the slot system superseded. On a busy box, waiting for
  a slot is normal and is not a failure — but the wait itself has
  a timeout (900 s observed): a run that cannot get a slot in time
  **fails without testing anything**, and its log says so
  (`timed out waiting for a probe slot`, with no guest output at
  all). That failure is a load artifact: preserve the log and
  re-run when the queue drains; it is not evidence about the
  product, and it is never a reason to loosen the check.
- Standing boot-census expectation: the `boot64-smoke` and
  `boot32-vkboot` consoles each show **0 `BW64-FAIL` lines and
  exactly 5 `BW64-INFO` served lines**. The console is not the
  runner's own output: each browser test leaves its guest console
  at `test-results/suite-<test-name>/console.log` in your tree,
  and the census is counted there.
- Long tests carry their own shared-host load guards (the benchmark
  test waits for the 1-minute load average to fall before each of
  its boots). A timeout under load is adjudicated from the
  preserved log and a re-run — it is never evidence about the
  product until a re-run on a quieter box says so.

## 7. The automated checking queue

    /home/ubuntu/checks-ci/submit-check <lane-branch>

- This is the **only door** into the queue. It takes the branch tip
  from the main repository (adding the reviewed `.woodpecker.yml`
  as exactly one commit if the tip does not carry it), submits it
  to the on-box queue — never to GitHub — and waits. The box is the
  queue's one and only executor; there is no target argument.
- A queue run executes, in a sealed container with the source
  mounted **read-only** (the run's own first step proves the mount
  refuses writes): citations, JS unit, JS legacy, native suites,
  run-native. Its results are published to the run's output area
  whether it passes or fails.
- `submit-check` prints **paths and exit status only, never a
  summary**. The verdict is what the raw step logs at those paths
  say. Stage 0 proved the queue's results identical to the same
  checks run by hand on the same commit — including identical
  failures (the two bare-checkout unit failures of check 0 appear
  in queue runs of unbuilt tips, correctly).
- The queue does not replace the browser guest suite (check 6),
  which it does not run.

Verified literally: 2026-11-05 —
`docs/reports/standards-verification-20261105.md`.
