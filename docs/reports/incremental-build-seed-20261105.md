# Incremental first builds for fresh worktrees — build seeds (FD-173, FD-174)

Lane: `lane-incremental-build`, base: integration tip `48d27952a`.
Fix commit: `1dae2e9d5`. Evidence: `test-results/incbuild/` in the lane's
worktree (each build's own log and timing file, preserved).

## What was actually wrong (measured first)

The suspicion going in was that the build recompiles everything after
any change. Measured on the base, that is not what the compiler layer
does. In an already-built copy, the standard build path
(`tools/bw64-build.sh` driving the `project/emscripten` makefile):

- recompiles **0** of the 270 object files when nothing changed;
- recompiles **exactly 1** object file after a one-file change
  (`source/util/crc.cpp`), and exactly 1 again when the change is
  reverted.

The full compile is instead paid at the working-copy lifecycle, every
time: `project/emscripten/Build/` is git-ignored, every lane starts in a
fresh worktree with no build directory at all, finished trees are
stripped of theirs, and the one preparation step
(`tools/winbox-wt-prep.sh`) linked the generated *payload inputs* from
the shared store but did nothing for *build outputs* — even though the
project's builds are byte-for-byte reproducible (every build in this
record, in two different trees, produced the identical program file,
sha256 `9b51d60101ab21a39bd88c4c8391512509361dd4845af22b8bc261eaaa6a5241`,
5,688,949 bytes — the same value recorded for this commit by the
shared-build-store lane and the FD-167 lane from their own trees).

## The change (commit `1dae2e9d5`)

- `tools/winbox-wt-prep.sh` (also installed at
  `/home/ubuntu/scripts/winbox-wt-prep.sh`; the previous installed
  version is kept beside it as `winbox-wt-prep.sh.pre-incbuild-20261105`)
  gains a build-seed section, run after the existing store verification
  and payload linking. If the shared store holds a seed for the
  worktree's exact HEAD commit, prep verifies it (recorded commit equals
  HEAD; the seed's program file matches its recorded hash; the
  store-wide SHA256SUMS check earlier in the same run already covered
  the seed's bytes), installs it as the worktree's
  `project/emscripten/Build/Wasm64Mt`, relocates the mirrored
  source-path subtree to the new worktree (object files live under
  `Build/Wasm64Mt/<absolute source path>.o`), rewrites that prefix in
  the generated dependency files, and refreshes timestamps so the link
  outputs are newest. Any mismatch or failure leaves the worktree
  unseeded with a printed reason — never a failed prep, never a partial
  seed. Only the standard browser target is seeded.
- `tools/winbox-publish-build-seed.sh` (new; also installed in
  `/home/ubuntu/scripts/`) publishes a seed from a verified build: it
  refuses a worktree with modified tracked files, requires
  `BUILD_INFO.json` to name the worktree's HEAD commit and match the
  program file's size and hash, stores the build directory under
  `build-seed/<commit>/` with a `SEED.json`, and refreshes the store's
  SHA256SUMS for the build-seed entries.
- `BUILD_PROTOCOL.md` documents the mechanism, the publish step (run it
  after a verified build of a commit — for example the integration tip
  at the end of a merge gate — so the next copies of that commit start
  seeded), and the from-scratch rule: delete
  `project/emscripten/Build/` and build as usual. The wrapper and the
  makefile are untouched; the from-scratch path behaves exactly as
  before.
- FD-174 (found while verifying publish cleanliness): `.gitignore`
  named the payload directories in trailing-slash form, which matches
  real directories only, so the preparation step's directory symlinks
  showed as 13 untracked files in every prepared copy and inflated
  `BUILD_INFO.json`'s `git_dirty_files` (14 recorded for a tree with
  exactly one real change). The same paths are now also ignored without
  the trailing slash; a prepared tree's status shows only real changes.

A seed for the current integration tip `48d27952a` has been published
from this lane's verified build, so fresh copies of the tip start
seeded today.

## Proof

All builds are the standard `tools/bw64-build.sh` on the shared box
(4 processors, `JOBS` default 2). Wall times include the build-lock and
probe-drain queue that serializes every lane's builds on this box;
loads ranged 6–15 during the day. Object counts are read from the build
directories after each run; every run's program file hashed to the
value above unless stated.

Base tree (`wt-incbuild`, fresh at `48d27952a`, no seed existed):

| run | change | wall | recompiled | result |
| --- | --- | --- | --- | --- |
| 1 | — (first build) | 2,491 s | 270 / 270 | BUILD COMPLETE |
| 2 | none | 1,962 s (nearly all lock queue) | 0 | BUILD COMPLETE |
| 3 | one file (`crc.cpp`, comment) | 518 s | 1 (`crc.cpp.o`) | BUILD COMPLETE |
| 4 | change reverted | 268 s | 1 (`crc.cpp.o`) | BUILD COMPLETE |

Proof tree (`wt-incproof`, fresh at `48d27952a`, prepared with the new
prep; the seed installed from the base tree's build — the mirrored
subtree renamed to the proof tree, 0 dependency files still naming the
origin tree, 195 naming the proof tree):

| run | change | wall | recompiled | result |
| --- | --- | --- | --- | --- |
| proof 1 | one file (`crc.cpp`, same comment) | 213 s | 1 (`crc.cpp.o`) | BUILD COMPLETE |
| proof full | seed deleted, same source state | 2,345 s | 270 / 270 | BUILD COMPLETE, program file byte-identical |

So in the same tree at the same source state: a one-file-change rebuild
from the seed took 213 s and recompiled 1 file; a full rebuild took
2,345 s and recompiled all 270; both produced the identical program
file. The proof-full build also recorded `git_dirty_files` **2** (the
two genuine modifications in that tree), against 14 for the same tree
state before the FD-174 fix.

Suites on the proof tree's seeded, incremental output (dist built by
proof run 1): JS unit suite **353 passed / 0 failed** (exit 0); legacy
suite **115 passed / 0 failed / 1 skipped** (exit 0) — the standing
totals for this commit.

Seedless path: a scratch worktree at `4fdd7812d` (no seed stored)
prepared with the new prep printed `seed: no build seed stored for
4fdd7812d; first build will compile from scratch`, linked everything,
exited 0, and created no build directory — exactly the pre-change
behaviour. (The scratch tree was removed after the check.)

## Limits, stated plainly

- Seeds are per-commit and exact. A lane that moves to a newer tip has
  no seed until someone publishes one from a verified build of that
  tip; that is deliberate (a seed is only trustworthy for the commit it
  was built from), and publishing takes one command after any verified
  clean build.
- Only the standard browser build (`wasm64-mt`) is seeded; the JIT
  variant and the headless targets still build from scratch on first
  use in a copy.
- Seeded dependency files are the only place the origin tree's path
  survives into a copy (rewritten at install); the seeded
  `BUILD_INFO.json` still names the origin builder until the first
  build rewrites it, which the wrapper does on every run.

## Measurement integrity note

One intermediate poll during the proof-full build returned figures
(a 139 s completion) that the run's own files on disk contradicted —
the build was in fact still queued behind a sibling lane's build at
that moment. Those figures were discarded. Every number in this record
was re-read from the runs' preserved log/time files and the build
directories after the runs finished.
