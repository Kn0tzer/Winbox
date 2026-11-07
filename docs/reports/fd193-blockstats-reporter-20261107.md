# FD-193: the BW64_BLOCKSTATS exit reporter read a dangling CPU pointer

Branch `lane-fd193`, base integ `faf95d26c`. Fix commit `f5fac0447`.

## The defect

Filed by lane-fd176 (its register entry is the authoritative
filing): with `BW64_BLOCKSTATS` set, the `tryBlockStep` exit
reporter kept a `static CPU64*` to the first CPU that allocated a
block table and registered an atexit handler once per CPU. Test
contexts own a new CPU that is destroyed at the end of its test,
so by process exit the pointer dangled and the printed counters
were heap garbage; with one handler per CPU, the line repeated.

Reproduced on the base tip before any change (block-invalidation
harness built from this tree with the FD-176 lane's link recipe,
run with `BW64_BLOCKSTATS=1 BW64_JIT=1 BW64_JIT_ENGINE=off
BW64_JIT_FAST_DISPATCH=0`): four identical lines —

```
BLOCKSTATS: built=281473917630304 execs=281473917630288 insnsInBlocks=202814378554980 totalInsns=4 coverage=5070359463874500.0%
```

— while the harness itself reported `pass=54 fail=0`. Evidence:
`test-results/fd193/red_blockstats.log`.

## The fix (commit `f5fac0447`)

No product behaviour changes; the flag remains opt-in and unset
by default.

- A CPU that allocates a block table while the flag is set joins
  a process-wide registry (`CPU64::blockStatsRegister`).
- The exit reporter is registered exactly once
  (`std::call_once`).
- On destruction a registered CPU folds its counters into
  process-wide atomic totals and leaves the registry
  (`CPU64::blockStatsUnregister`, called from `~CPU64` under the
  same BLOCK_EXEC + BLOCK_CACHE_INFRA gate the registry code is
  compiled with — BLOCK_EXEC-only builds never register, so
  there is nothing to undo there).
- The reporter (`CPU64::blockStatsReport`) prints the folded
  totals plus the counters of any CPUs still alive at exit. It
  never dereferences a CPU that is not either folded or alive.

## Proof

All runs of the block-invalidation harness (four CPU contexts:
T1–T4), which is the workload that produced the garbage.

- GREEN, flag set, fixed build — a single BLOCKSTATS line,
  byte-identical across two runs:
  `built=7 execs=7 insnsInBlocks=13 totalInsns=14 coverage=92.9%`
  (`test-results/fd193/green_blockstats_run1.log`, `..._run2.log`).
- Independent measurement: `totalInsns=14` is exactly the
  harness's own total — its four contexts execute 4+4+2+4
  instructions (each `runBounded(2)` count is CHECK-asserted or
  pinned by the tests' rip assertions). `built=7` / `execs=7`
  match the workload's block builds and executions (two per
  context except the single-run self-modifying context). The one
  non-block instruction is that context's tail, which by the
  test's design is re-decoded outside the cached block after its
  own store rewrites it.
- Differential unchanged: interp and blocks logs byte-identical,
  `pass=54 fail=0` (`test-results/fd193/inv_interp_fixed.log`,
  `inv_blocks_fixed.log`).
- Flag unset: zero BLOCKSTATS lines in the blocks run.
- CPU lifetime gate (`scripts/test-jit-lifetime.sh`, block
  execution off and on — the "on" mode is BLOCK_EXEC without
  BLOCK_CACHE_INFRA, the gate combination the destructor hook
  had to respect): PASS in both modes
  (`test-results/fd193/lifetime.log`).
- jit64 phase-1 unit tests: 873 passed, 0 failed
  (`test-results/fd193/phase1.log`).

## Notes

- The worktree prep script (`winbox-wt-prep.sh`) currently
  aborts for a fresh tree off the integration tip: the shared
  store's `wine64.zip` / `wine32.zip` / `prefix32.zip` were
  replaced (store mtimes 2026-11-07 03:27 UTC; hashes
  `1b725073...` / `aa2bb9d6...` / `68164e56...`) and match
  neither the integration tip's committed pins nor lane-fd189's.
  `prefix64.zip` still matches the tip. This lane's work needed
  no store payloads (native builds from tracked sources; light
  checks via the checking queue), so it proceeded unprepped and
  did not touch the store. Flagged for the coordinator: any
  fresh tree off the tip will hit the same abort until the
  store and the pins agree again.
- Disk-steer compliance during this lane: the tree carried no
  generated state to strip (prep had aborted before linking;
  the only ignored path is the preserved `test-results/fd193/`
  evidence). Scratch in /tmp (`fd193-build`, `fd193-jit`,
  `fd193_edit.py`) was deleted as each phase finished with it —
  about 15 MB freed in total; no new store, staging copy, or
  large scratch was created.

## Queue checks (the checking queue, the standard door)

- The first submission ran as pipeline 20 and never reached the
  checks: its verify-source step found the shared source mount
  holding another lane commit (`81bd25c4...`) instead of this
  branch tip. With half a dozen lanes submitting at once, each
  `submit-check` re-prepares the single shared mount
  (`checks/src`), so most pipelines in that batch (18–21)
  failed verify-source identically — no check ran and every
  step log is empty. Evidence: `checks/evidence/pipeline-20/`.
- Re-submitted after the submission traffic drained. Pipeline
  26 verified the mounted commit is exactly this branch tip
  (`345a6a5d7...`) and ran the full definition: citations exit
  0; js-unit exit 1 with 352 passed / 2 failed — the two known
  bare-checkout artifacts (`provenance-artifacts`,
  `runtime-completeness`), the accepted baseline shape;
  js-legacy exit 0; native-main-build exit 0; native-main exit
  0 (692 tests, 0 FAILED in 269 s); native-x64-build exit 0;
  native-x64 exit 0 (116 tests, 0 FAILED in 25 s); run-native
  exit 0. Evidence: `checks/evidence/pipeline-26/`.
- Infrastructure note for the coordinator: the shared source
  mount makes concurrent submissions destroy each other's
  pipelines at verify-source — in the 17–21 batch only pipeline
  17 verified. Submissions need serialising (or a per-pipeline
  mount) before multi-lane fan-outs can rely on the queue.
