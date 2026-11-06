# Server inventory and organization pass — 2026-11-05

Measured live on the box 2026-11-06 01:35–02:05 UTC (2026-11-05 evening EST).
Standard applied: the same one the user set for their personal laptop —
everything belonging to this project is purposeful, named, and explainable;
anything whose owner or purpose cannot be established is left untouched and
put on the decision list at the end. Other tenants' property is listed only
as "not ours, left alone." No branch or commit was deleted; nothing was
pushed.

## Actions taken by this pass

Two process groups were ended, each verified orphaned from finished work
before any signal was sent (details in the Processes section): a test-suite
process group hung for 5 days at zero CPU in a deleted directory, and a
host-reference measurement pipeline blocked for 3 days at zero CPU whose
owning work was composed into batch 3. Both ended on SIGTERM; all six PIDs
verified gone afterwards.

Thirteen working copies whose lanes are finished and verified were
unregistered (`git worktree remove`). Each was checked first: clean version
control status (no uncommitted changes), no process sitting in it, and the
always-generated output folders already stripped by its owning lane, per the
standing disk discipline. Each tree's test evidence was first copied to the
consolidated evidence store (`/home/ubuntu/backups/winbox-evidence-consolidated-20261101/<tree>/`)
with a `REMOVAL-INFO.txt`. All thirteen branches remain in the repository
untouched — only the working folders went. This freed about 7 GB: the disk
went from 5.7 GB free at the start of the pass to 13 GB free after.

Nothing else was removed. Scratch space, container storage, and backups
were inventoried only (see the disk ledger): nearly everything large in
them belongs to work still running or to other tenants.

## Processes and services

**Belonging to this project — long-lived:** none on the host itself. The
project's standing machinery runs in containers (next section). Every host
process the project has is a transient child of one of the ten efforts
running at inventory time (batch-8 combining and its check chain, the four
placeholder draw-downs, the game-controller effort, the three
compiler-speedup efforts, the laptop-expansion effort): compilers, suite
runners, browser tests, and one request-polling loop owned by the
laptop-expansion effort inside the checking setup's folder. At the final
check, the only emulator test processes alive were a suite started minutes
earlier by a running effort (`./Build/Test/boxedwine 0 0 1` ×2).

**Ended by this pass (verified orphans from finished work):**
- PID group 3221249/3221250/3221279 — a native suite launcher and its
  emulator test process, started about 5 days earlier from the preserved
  payload-donor tree's build folder. Elapsed 5 days 1 hour, total CPU time
  00:00:00, working directory already deleted, its log files already gone.
  A suite that normally finishes in minutes had been hung, consuming
  nothing, since before batch 5.
- PID group 3785884/3785915/3785916 — a one-off host reference program
  (plus its output pipe) from the file-descriptor measurement work composed
  into batch 3. Elapsed 3 days 4 hours, total CPU time 00:00:00, output file
  untouched since it started on Nov 2. This group was also the "leftover
  process" that had kept the finished fd101 working copy registered; with
  it gone, that copy was unregistered normally (list below).

**Box platform (runs the agents; not project-managed, left alone):** the
bb server and host-daemon processes (up 3.5 days), and the platform's own
monitoring processes (gomon, psimon). No system monitor of any other kind
was running at inventory time; the one flagged earlier on Nov 5 was ended
that day with the user's permission and has not restarted.

**Systemd:** no running systemd unit belongs to this project.

**Cron:** root's crontab holds a platform sync every 5 minutes and one
hourly entry pointing at a directory that no longer lists (decision list
D7). Nothing in cron belongs to this project.

**Not ours, left alone:** the Minecraft server process and its two
containers; the remote-desktop stack (the desktop service and its browser
— the user's own desktop, genuinely used); the photo, notes, chat, mail,
and download services and their databases; two small unattributed node
servers (a portfolio server and a games-mirror script in scratch space —
decision list D9); the postgres/nginx/apache processes belonging to those
services.

## Containers (23 running)

Belonging to this project:
- `checks-woodpecker-server`, `checks-woodpecker-agent`, `checks-gitea`,
  `checks-minio` — the automated checking queue, proven on Nov 5. Home:
  `/home/ubuntu/checks-ci/` (1.6 GB, including the laptop-overflow request
  machinery of the currently running laptop-expansion effort: its request
  script, requests, and results — inventoried, untouched). If these go
  away: the queue stops; checks fall back to being run by hand; the record
  of past runs stays on disk under `checks-ci/`.
- `wisp` — the network relay the live demos' guest programs reach the
  internet through. If it goes away: the live demos lose networking;
  programs still run offline.

Shared box infrastructure (serves this project among others; not
project-managed): `caddy`, the box's web server — the live Winbox site is
one of the many sites it serves. If it goes away, every site on the box
goes dark, not just this project's.

The other 18 containers belong to other services on the box: not ours,
left alone.

## Working copies (registered: 27 at pass start, 14 at pass end)

Running or preserved — every one has a named reason to exist:
- `/home/ubuntu/boxedwine64` [master, `69c800476`, clean] — the main
  checkout, and the sole home of the held-back commit (it edits the
  automated deploy workflow; it never rides a push). If it goes away, that
  commit's tip exists nowhere else. Treat as a permanent fixture.
- `/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022`
  [integ/winbox-20261022b, `48d27952a`] — the combining seat; batch-8
  composition and its check chain are running there now. If it goes away:
  the composed state survives on the branch, but the in-progress gate
  scratch does not.
- `/home/ubuntu/wt-buildcache` [lane-buildcache] — running: compiler
  memory speedup.
- `/home/ubuntu/wt-pch` [lane-pch] — running: shared instruction files
  processed once per build.
- `/home/ubuntu/wt-incbuild` [lane-incremental-build] — running:
  incremental everyday rebuilds.
- `/home/ubuntu/wt-fd168`, `wt-fd169`, `wt-fd170`, `wt-fd171` — running:
  the four placeholder draw-down efforts (desktop library, C runtime,
  whole-library absences, long tail).
- `/home/ubuntu/wt-gamepad` [lane-gamepad] — running: game-controller
  support.
- `/home/ubuntu/wt-fd38` [lane-fd38] — preserved: the pinned-payload donor
  tree fresh copies used before the shared build store existed. Kept on
  purpose; see decision list D8.
- `/home/ubuntu/wt-restage` [lane-restage] — preserved transfer source:
  holds the 64-bit graphics payload the worktree preparation step relies
  on. Its one uncommitted entry is its working state. If it goes away, the
  only copy of that built payload goes with it.
- `/home/ubuntu/wt-x86vkd3d` [lane-x86vkd3d] — preserved transfer source:
  the 32-bit twin (its ~1,500 uncommitted entries are its built payload
  state). Same warning.
- `/home/ubuntu/wt-fd129pre` [lane-fd129pre] — evidence-only, and the one
  tree that could not be unregistered: it holds a single uncommitted
  change that exists nowhere else. Decision list D1.

Unregistered by this pass (branch and tip kept in the repository; evidence
preserved in the consolidated store with a REMOVAL-INFO per tree):
wt-audit-l1l2 (`ab2d2bc81`), wt-buildrel (`b4681d12f`), wt-cleanup-status
(`37cdf452d`), wt-dllcensus (`1e0a37f99`), wt-fd101 (`4ac6d024e`),
wt-fd115 (`7cfe714c4`), wt-fd162-164 (`c4dd96ecd`), wt-fd165 (`e5fabcbfb`),
wt-fd167 (`3c30ddc56`), wt-fd71 (`7f77bc6bb`), wt-machine (`8c48d3ee5`),
wt-perf2 (`64bf61970`), wt-sharedbuild (`5b979f616`).

This pass's own temporary tree (`wt-server-inventory`, branch
`lane-server-inventory`) was created only to commit this record and is
removed immediately after; the branch carries the record.

## Stores and storage areas

- **Shared build store** `/home/ubuntu/boxedwine64-shared/` (~1.9 GB,
  with manifest and checksums) — the generated payload inputs every fresh
  working copy links from, set up once instead of copied per tree. If it
  goes away: the one-step preparation for new copies fails closed until
  the store is rebuilt from the retained gate archive.
- **Live site** `/home/ubuntu/kn0tzer/winbox/` (~1.0 GB; served build is
  batch 7, BUILD_INFO `4006ddb58`) and its rollback
  `/home/ubuntu/kn0tzer/winbox-prev-20261105-1244/` (~1.0 GB, batch 6).
  If the live tree goes away: the public site is down until a redeploy.
  If the rollback goes away: no one-step return to batch 6.
- **Project backups** inside `/home/ubuntu/backups/` (~7.8 GB of a ~17 GB
  folder; the rest is not ours): the consolidated evidence store (~1.2 GB,
  including this pass's preserved test results); nine retained
  runtime-asset archives (~4.5 GB — the 20261104 set is the current gate
  archive; the 20261105 sets include three running lanes' own cuts);
  eight Wine backport payload snapshots (~1.6 GB, cut by the placeholder
  lanes before rebuilding payloads, the newest today at 01:24 UTC); two
  wine64 payload snapshots (~0.4 GB); small font sets. If the gate archive
  goes away: gates and fresh copies lose their pinned starting point.
- **Scratch space `/tmp`** (~5.9 GB at inventory): ~5.3 GB is the running
  efforts' own build and test scratch (the four placeholder lanes'
  backport builds and censuses, the compiler lanes' experiments, and the
  batch-8 gate's per-run folders) plus reference material with live
  processes pointing at it. ~0.5 GB is unattributed — decision list D4/D5.
- **Container storage:** images total 26.46 GB across all services on the
  box; the project's own are the checking queue's set including its
  ~3.85 GB runner image. Build cache is 0 (cleared Nov 5). Volumes
  (3.2 GB) belong to the box's other services.
- **Project scripts** `/home/ubuntu/scripts/`: `winbox-wt-prep.sh` (the
  one-step worktree preparation; two kept predecessor versions alongside,
  one left by the running compiler-memory effort), `laptop-ssh` (the only
  path to the user's laptop), `winbox-wait-load.sh`,
  `winbox-runtime-assets.local.json`.

## Disk ledger — where reclaimable weight sits, with owner and safe bound

Disk: 193 GB total; 5.7 GB free at pass start, 13 GB free after this
pass's unregistrations (94% used). The weight cannot silently regrow
unnoticed if each category below keeps its owner:

1. **Finished working copies still registered** — was the largest
   project-owned leak. Bound: exactly the clean, finished, process-free
   trees. Owner: whoever closes out a lane. This pass cleared all 13
   (~7 GB). The standing rule stands: a lane is not done until its tree
   is stripped and unregistered.
2. **Hung or forgotten project processes** — 0 GB but they pin deleted
   folders and confuse later checks. Bound: only groups with days of
   elapsed time at zero CPU, a finished owner, and stale/deleted outputs
   qualify. Owner: this kind of pass. Cleared: 2 groups.
3. **Container build cache** — 0 today (cleared Nov 5, reclaiming
   6.9 GB). Regrows whenever an image is built; owner: the image builder
   (the checking queue's runner image). Safe move, proven Nov 5: a full
   prune, only while no image build is in flight.
4. **Container images flagged "reclaimable" (8.29 GB)** — NOT a safe
   bound for this project: most of it is other services' tagged images
   (the Nov 5 cleanup measured ~3.7 GB as another service's
   previous-version pair). Owner: each service's owner. Hands off.
5. **Superseded gate archives** — the three oldest runtime-asset archives
   (20261101–20261103, ~1.5 GB) are superseded by 20261104. Owner: the
   merge/gate process. Not removed — decision list D2.
6. **Lane-named asset sets and payload snapshots (~4.1 GB)** — owners:
   the lanes that cut them; three of those lanes are still running.
   Revisit when they land — decision list D3.
7. **Unattributed scratch (~0.5 GB)** — decision list D4/D5.
8. **Structural, not reclaimable by this project:** other services' data
   under the container folder (~29 GB), other tenants' images, and the
   non-project share of the backups folder (~9 GB).

## Decision list — untouched, questions stated plainly

- **D1.** The evidence-only tree `wt-fd129pre` holds one uncommitted
  change that exists nowhere else. Should that change be committed to its
  branch or exported to a file, so the tree can be unregistered like the
  others?
- **D2.** How many generations of gate archives should be kept? The
  20261101–20261103 archives (~1.5 GB) are superseded by 20261104; the
  20261105 sets belong to lanes. Nothing was deleted.
- **D3.** May each Wine backport payload snapshot (~205 MB each, eight of
  them) be deleted once its owning lane lands? Three owners are still
  running; ask at batch-9 composition.
- **D4.** `/tmp/rtfake` (240 MB, last written Nov 5 18:30): is it a
  fixture of the still-running batch-8 gate? Left in place; re-ask when
  batch 8 lands.
- **D5.** `/tmp/celeste-dbg` (203 MB, last written Nov 5 18:56) and
  `/tmp/kn-shot` (40 MB, Nov 5 03:59): whose are they? No running process
  references either.
- **D6.** The checking queue's home keeps per-run areas (`scratch/`,
  `src/`, `manual/`). How long should queue run scratch be retained? The
  queue owner's call; nothing inside the queue's home was touched.
- **D7.** Root's hourly cron runs `/home/ubuntu/zcode-queue/watchdog.sh`,
  but that directory no longer lists. Not this project's — flagged for
  the box owner only.
- **D8.** The preserved donor tree `wt-fd38` had its build-output subtree
  deleted at some point (the hung suite process ended this pass was
  sitting in it). The tree is kept for its pinned payloads: are all of
  those in tracked or linked areas, or did the deleted output hold one?
  For the next payload-touching lane to confirm; the tree itself was not
  touched.
- **D9.** `/tmp/games-mirror.mjs` — a small node server running for 10
  days from a scratch-space script, parented to init. Unattributed;
  possibly the user's games download mirror. Not this project's; left
  alone.

## State after this pass

- Registered working copies: 27 → 14, and every remaining one maps to
  running work, a preserved source, the main/integration fixtures, or
  decision item D1.
- Project processes: no process remains whose owning work is finished;
  the only long-lived project machinery is the checking queue's and the
  network relay's containers.
- Disk: 13 GB free (94% used), with the remaining reclaimable weight
  itemized above, each category carrying an owner and a safe bound.
