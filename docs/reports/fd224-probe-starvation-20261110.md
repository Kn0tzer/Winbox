# FD-224 — Browser probes starve behind a build queue: tree-scoped coordination

- **Branch:** `lane-fd224` off integration tip `ea3a5c496`.
- **Worktree:** `wt-fd224`.
- **Status: FIX IMPLEMENTED AND PROVEN AT THE COORDINATION LEVEL; NOT CLOSED.**
  The full browser build and the checking-queue gate that normally prove a
  fix commit could not run: the box disk is at 4.1 GB free, below the 5 GB
  floor in the lane's brief, so the lane stopped those steps rather than
  improvising deletions. FD-224 stays open in the register with a status
  note; the remaining steps are a full `bw64-build.sh` run from the fix
  commit ("Browser export verified") and a queue pipeline on that commit,
  after which the entry can be closed. Nothing in this record claims those
  steps ran.

## The defect (register FD-224, open, Medium)

Two serial build gates stand before any browser start. `acquireProbeSlot`
refuses acquisition while a `build-pending` file names a live pid, and
`tools/bw64-build.sh` claimed that file when a build started *waiting* —
before it held the build lock — removing it only at its own exit, so a
relay of queued builds held the claim continuously. A probe that did
acquire then waited again in setup for the build lock itself
(`waitForBuildIdle`, 600 s default). Measured 2026-11-07 on the 20-lane
wave: the claim was held without a usable gap from 04:24Z to past 06:11Z
across at least six builds, and three suite boots each failed at their
900 s slot wait (901729 ms, 900490 ms, 901735 ms) without testing
anything.

## The fix

Coordination is now scoped to the tree whose `dist/` is at stake, because
a build replaces only its own tree's `dist/`:

- **Per-tree claim.** A build publishes its claim under
  `build-pending-<sha16(canonical tree path)>` (first 16 hex chars),
  holding `pid|tree`. `acquireProbeSlot` ignores claims from any other
  tree. The legacy single `build-pending` file (written by older builds)
  still blocks every tree, fail closed, until it is gone.
- **Claim timing.** The claim is published only while draining for a
  write or while holding the build lock. A build merely queued for the
  lock publishes no claim, so probes in its tree can start while it
  waits — a relay of queued builds no longer holds the claim
  continuously. After a slip (a probe started during lock acquisition)
  the lock is released but the claim is kept, since the build is still
  draining for a write.
- **Per-tree lock.** The build-lock info record gains a 4th field, the
  builder's tree (`pid|started|owner|tree`; 3-field records still parse,
  tree unknown, fail closed). `waitForBuildIdle` waits only for a build
  in the probe's own tree.
- **The probe's tree.** The tree whose `dist/` a probe will test: the
  tree containing the probe module by default; the parent of
  `BW_TEST_DIST`/`WINE_DIST_DIR` when that parent is a git worktree;
  remote (no local build blocks) when `BW_TEST_BASE_URL` is set or the
  dist lives outside any worktree. The slot sidecar records the tree
  (`tree` field; null for a remote dist); a build's drain/validate scans
  consider only its own tree's probes, and a probe with no readable tree
  fails closed. Slot counts are unchanged (heavy 2, light 2).
- The `status` command in `probe-slots.mjs` now also lists claim files.

## Files changed

- `web/tests/probe-slots.mjs` — per-tree `buildPending`, `probeDistTree`,
  `moduleTreeRoot`, `tree` in the slot sidecar, claim listing in status.
- `web/tests/build-lock.mjs` — 4th info field, `buildInTree`,
  `waitForBuildIdle` takes an optional `tree` (default: the probe's dist
  tree).
- `tools/bw64-build.sh` — canonical tree (`pwd -P`), per-tree claim
  file, claim only while draining/holding the lock, tree in the lock
  info, tree-aware `probes_busy`/`validate_probe_slots`,
  `release_lock` keeps the claim (the exit trap still removes it).
- `web/tests/suite/unit/probe-slots.test.mjs` — sidecar key list gains
  `tree`; three new tests (other-tree claim does not block, same-tree
  claim blocks, legacy claim blocks).
- `web/tests/suite/unit/build-lock.test.mjs` — one new test (lock info
  records the tree; `waitForBuildIdle` ignores other trees and remote
  dists, still waits on same-tree and tree-less locks).
- `BUILD_PROTOCOL.md`, `docs/standards/checks.md` — the tree-scoped
  coordination and the fix outcome, in the same change.

## Proof (all read from the runs' own output, not from summaries)

- **Unit tests, direct run on the tree:** `node --test
  web/tests/suite/unit/probe-slots.test.mjs
  web/tests/suite/unit/build-lock.test.mjs` — **16 tests, 16 pass,
  0 fail.** This includes the three new claim tests and the new
  tree-scoped lock test. The first run was 15/16: the pre-existing
  test "live and malformed probe slots reject a build" failed because
  my first `slot_tree` returned a nonzero status that `set -e` in
  `bw64-build.sh` turned into an exit 2 before the caller could read
  it. That was a real defect in the new code, caught by the test, not
  by inspection: `slot_tree` now always succeeds and signals through
  its printed value (`<tree>` / `REMOTE` / `UNKNOWN`), and the suite
  is green on re-run.
- **Controlled starvation experiment** (`test-results/fd224/fd224-exp.mjs`,
  isolated slot/lock dirs, a fake queued build holding a claim and/or
  a lock for another tree while a probe in the tested tree starts;
  5 s timeouts):
  - Against the **old code** (integration tree): legacy claim blocks,
    lock in another tree blocks — the FD-224 starvation, reproduced.
  - Against the **fixed code** (`wt-fd224`): all 5 cases pass — legacy
    claim still blocks (fail closed); per-tree claim from another tree
    does **not** block (the fix, gate 1); per-tree claim from this
    tree still blocks; lock in another tree does **not** block (the
    fix, gate 2); lock in this tree still blocks. Logs
    `test-results/fd224/fd224-exp-old.log` and `fd224-exp-new.log`.
- **Real `bw64-build.sh` claim timing** (isolated slot/lock dirs, a
  fake lock holder, 30 s build wait):
  - Test A (queued build, no probe running): **no claim file appears**
    at any point; the build logs "waiting for build lock" and dies at
    its deadline. Log `test-results/fd224/script-test-a.log`.
  - Test B (queued build + a live same-tree probe): the per-tree
    claim file `build-pending-97a56aa0d9ab3c16` appears while
    draining, holding `4009129|/home/ubuntu/winbox-project/worktrees/wt-fd224`
    (the build's pid and tree; the hash is sha256 of the tree path,
    first 16 hex — verified independently), the build logs "waiting
    for probes to drain", and the claim is gone after the build exits.
    Log `test-results/fd224/script-test-b.log`.

## NOT proven (blocked on disk)

- No full `bw64-build.sh` run from the fix commit ("Browser export
  verified") — disk was 4.1 GB free at the time, below the 5 GB floor.
- No checking-queue pipeline on the fix commit — same block.
- No 2×/3× probe-concurrency measurement — still owed separately
  (needs a quiet box and ≥10 GB free); the fix is the prerequisite,
  not the measurement.

## Disk state at the lane's stop

4.1 GB free (98% used) when the lane stopped building; backups already
at the cap of 8 cuts (3.7 GB), so no authorized prune was available.
The lane's own writes are negligible (tests and experiment scripts in
`/tmp`, since removed). The drop from 9.4 GB at lane start came from
other tenants' work, not this lane. No deletions were improvised.

## No new defects found

The `set -e` hazard above was found and fixed inside this change. No
further defect was filed.
