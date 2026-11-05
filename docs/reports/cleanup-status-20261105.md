# Storage cleanup and status refresh — 2026-11-05

Lane `lane-cleanup-status`, based on the integration tip `4006ddb58`
(batch-7 merge). Prompted by the 2026-11-05 codebase audit's
organization findings (its O1–O4). Nothing was pushed or deployed by
this lane, and nothing was committed from the main checkout.

## Main checkout restored

The main checkout (`/home/ubuntu/boxedwine64`, local master tip
`69c800476`) showed exactly 580 deleted files, every one a tracked
source under `tools/rootfs64`. The deletions were unstaged working-
tree damage, not a commit. Restored with a plain git checkout of that folder; afterwards `git status --porcelain` reads 0 lines
and the tip is unchanged. No commit was made from that checkout.

## Worktrees removed (56)

Each tree below was re-verified clean (no modified or untracked
files) at removal time, its `test-results/` evidence was copied to
`/home/ubuntu/backups/winbox-evidence-consolidated-20261101/<tree>/`
together with a `REMOVAL-INFO.txt` naming its branch, tip, and the
preserved file count (69–316 files per tree), and the tree was then
removed with `git worktree remove`. **Branches were not deleted** —
every removed tree's branch remains a repository ref and can be
checked out again at any time.

- `wt-d3d11lit`
- `wt-fd05`
- `wt-fd102-104`
- `wt-fd105-108`
- `wt-fd109-112`
- `wt-fd113-115`
- `wt-fd116-117`
- `wt-fd118-119`
- `wt-fd120-123`
- `wt-fd124`
- `wt-fd125-restage`
- `wt-fd125ab`
- `wt-fd126`
- `wt-fd127fix`
- `wt-fd128`
- `wt-fd129`
- `wt-fd131`
- `wt-fd132`
- `wt-fd133-135`
- `wt-fd136-137`
- `wt-fd136-138`
- `wt-fd139-152`
- `wt-fd148-149`
- `wt-fd151`
- `wt-fd153`
- `wt-fd154`
- `wt-fd155`
- `wt-fd156`
- `wt-fd157-161`
- `wt-fd23res`
- `wt-fd64`
- `wt-fd65`
- `wt-fd66`
- `wt-fd72b`
- `wt-fd77surface`
- `wt-fd78`
- `wt-fd7980`
- `wt-fd82`
- `wt-fd83-84`
- `wt-fd85`
- `wt-fd85b`
- `wt-fd85pay`
- `wt-fd86`
- `wt-fd87`
- `wt-fd87loss`
- `wt-fd88`
- `wt-fd90`
- `wt-fd92`
- `wt-fd92-93`
- `wt-fd92mmap`
- `wt-fd94-96`
- `wt-fd97`
- `wt-fd98`
- `wt-fd99-100`
- `wt-godotpark`
- `wt-wedge`

## Deliberately kept

- `wt-fd38` — preserved pinned-payload source; a long-running
  leftover process also has it as its working directory.
- `wt-restage`, `wt-x86vkd3d` — preserved payload transfer sources;
  never removed.
- `wt-fd129pre` — evidence-only tree with an uncommitted change; it
  is the sole copy of that identification evidence.
- `wt-fd101` — a leftover process has this tree as its working
  directory; left exactly as found.
- `wt-fd162-164` — a lane is actively working in it.
- The main checkout and the integration worktree, as always.

Registered worktrees: 64 before, 8 after.

## Disk figures (server root volume, 193 GB)

- At this lane's start: 6.1 GB free (97% used).
- After the main-checkout restore and the batch-7 deploy lane's
  staging: 4.6 GB free.
- Immediately after the removals: 43 GB free.
- At this record's writing: about 41 GB free (the deploy lane's
  staging accounts for the difference). Net recovery from the
  removals: roughly 38 GB.

## Status documents refreshed

- `docs/WINBOX-STATUS.md`: new current-state section dated
  2026-11-05 (batch-7 build live, register at 161 entries / 118
  closures, rare stuck start at 1 in 35, Godot's remaining
  blocker, this cleanup); the 2026-10-30 section is marked
  superseded, and the "current deploy record" pointer names the
  batch-6 record with the batch-7 live stamp beside it.
- `docs/game-compatibility.md`: header now states the rows are the
  2026-10-30 sweep and have not been re-measured on the current
  build; the Al-Khaser and Godot rows no longer claim their fixes
  are unmerged — both shipped in later batches, and the Godot row
  describes the shipped build's state (window opens, pipelines
  build, first frame still blocked by the one shader operation).
- `docs/defect-register.md` Section B: a dated status correction
  at the head of the section states the table is a historical
  ledger — every row's branch has been composed into the
  integration branch (cleanup wave 2026-10-31 and batches 1–7) —
  and names the only branches not yet in the integration tree.
