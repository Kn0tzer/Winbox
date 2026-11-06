# Disk cleanup — 2026-11-05

Lane: disk cleanup (branch `lane-disk-cleanup`, off integ tip `48d27952a`).
All figures below were measured live on the box during the work
(2026-11-05 23:59 UTC → 2026-11-06 00:2x UTC; EST evening of 2026-11-05).

## Assignment and guardrails

Recover root-disk free space using only generated, rebuildable state from
finished work and stale scratch. Protected and not touched: the integration
tree, the main checkout, the working copies of the efforts still running
(the four placeholder draw-downs, the game-controller work, the batch-8
composition, the compiler-speedup work), the automated-checking setup
directory and its in-use container images, the two preserved transfer-source
trees, the pinned-payload donor tree, the shared build store, all branches
and commits, the backups/evidence store, and the live website tree.
Other tenants' processes and files are untouchable.

## Measured state before

`df -h /` across the session (the figure kept falling while lanes wrote,
then rose on the removals):

- First read: 193G total, 187G used, **6.4G free (97%)**
- At container inventory: **5.7G free (98%)**
- Immediately before removal: **4.8G free (98%)**

Note: the brief described ~0.5G free and ~0.2G of /tmp scratch. Live
measurement found 6.4G free at first read and **/tmp at 6.6G**. Both
briefing figures are superseded by the measurements in this record.

## Where the space actually was

- Finished lanes' working copies were already stripped: their rebuildable
  output folders measure zero (coordinator's check, consistent with tree
  sizes being bare checkouts of ~0.35–1.5G). Nothing to recover there.
- The day's growth is mostly work that is still running and therefore
  protected: the running lanes' trees (wt-fd169 4.7G, integration tree
  4.2G, wt-fd168 3.0G, wt-gamepad 2.9G) and their /tmp build scratch
  (~3.1G across five directories, one confirmed written 12 minutes before
  the check), plus the automated-checking setup (checks-ci 2.0G on disk,
  its runner image 3.85G, its live build cache ~3.85G).
- Backups 17G (recorded evidence — kept) and the live website tree 22G
  (production — kept) account for most of the rest.
- Container system (`docker system df`): images 26.46G with 4.443G
  "reclaimable"; build cache 6.878G in 21 records; containers 379M;
  volumes 3.578G (131.8M reclaimable — not touched; volumes can hold
  tenant data).

## Actions taken

### Container images
- `docker image prune -f` (dangling images only): **reclaimed 0B** — the
  dangling list was empty; nothing to remove.
- The 4.443G docker calls reclaimable is tagged images with no container:
  ~3.73G is the previous version pair of another service on this box
  (its current version is running) — not this project's to delete;
  ~0.68G is a command-line tool image the still-running checking-setup
  work pulled today and uses — in use, kept.

### Container build cache — selective prune attempted, not possible
Cache partition by last access: ~3.85G fresh (the checking lane's runner
image cache; records accessed minutes before each check — it rebuilt its
image repeatedly while this cleanup ran) and ~3.03G stale (15 records last
accessed 3 days ago, ~30h-old remainder negligible).
Four selective attempts each reclaimed **0B** and left the table unchanged:

- `docker buildx prune -f --filter until=24h`
- `docker builder prune -f --filter until=24h`
- `docker buildx prune -f --filter id=<largest stale record>`
- `docker buildx prune -f --max-used-space 4G`

The only operation this buildx version (v0.38.1) offers beyond those is a
full prune of all 6.878G, which would also destroy the running lane's
active rebuild cache. Under the in-use guardrail it was **not** run.
Follow-up, safe the moment the checking-setup lane finishes:
`docker buildx prune -f` reclaims the full ~6.9G then (the lane's own
cache included, once nothing needs it).

### /tmp stale scratch removed (12 items, ~2.5G)
Each item: owning work finished, no process running inside it (checked per
directory), newest content aged as noted. `du` sizes at removal:

| Removed | Size | Basis |
|---|---|---|
| /tmp/wine10-fd167-build | 479M | FD-167 lane complete and verified; build scratch |
| /tmp/rt20261104 | 468M | Staging copy of the retained runtime archive; the archive itself is preserved under backups/ |
| /tmp/dllprobe | 453M | DLL-census lane complete; scratch probe tree holding copies of the runtime archives |
| /tmp/winbox-fd128-9xG97R | 247M | FD-128 scratch, newest content 51.8h old |
| /tmp/bw32-wine32-cache | 213M | Downloaded package cache from finished 32-bit work, 40.7h old |
| /tmp/tmpii2bub13 | 204M | Anonymous temp dir, newest content 322.9h old |
| /tmp/fd128-dis.txt | 195M | Disassembly text from finished FD-128 work (Nov 3) |
| /tmp/fd128-dis2.txt | 195M | Same |
| /tmp/gn-deploy | 91M | Demo scratch, newest content 82.3h old |
| /tmp/game-debug | 20M | Demo debugging scratch (Nov 2) |
| /tmp/gn-launch-script-1.js | 16M | Same family (Nov 2) |
| /tmp/gn-compare | 5.8M | Same family (Nov 2) |

### Deliberately left in /tmp (doubt rule — named for the next cleanup)
- /tmp/wine8-backports-build (530M) — content written 12 minutes before
  the check: a running lane's live build tree. Not scratch.
- /tmp/rtfake (235M) — may be a fixture of the still-running batch-8 gate.
- /tmp/celeste-dbg (199M) — touched 5.2h before the check; owner unclear.
- /tmp/jit64-wasm-* (~180M across three dirs) — per-run build outputs,
  but the still-running batch-8 gate produces the same pattern.
- /tmp/bw32-debs (101M) — 3.3h old; plausible fetch by a running lane.
- /tmp/kn-shot (39M) — may serve a pending screenshot deliverable.
- /tmp/k3bpwritev* (52 dirs) — 4.3G apparent but 616K actually allocated
  (sparse test files); nothing to gain.
- Running lanes' own scratch (fd168/fd169/fd170/fd171/gamepad/probe-slot
  and browser-test infrastructure) — untouched.

## Measured state after

- `df -h /` immediately after removal: **7.3G free (97%)**, up from 4.8G
  immediately before — gross removed ~2.5G, all of it the /tmp items
  above (concurrent lane writes consumed part of the session's gains;
  across the whole session free space went 6.4G → 7.3G while the running
  lanes kept writing).
- /tmp: 6.6G → 4.1G.
- Automated-checking stack verified healthy after the cleanup: its four
  containers up (server/agent healthy), and a fresh pipeline step
  container was already running its next job.
- Nothing in the protected list was modified: no tree stripped, no image
  or volume removed, no branch or commit touched, backups and the live
  site untouched.

## Honest bottom line

Free space is back to ~7G with everything protected intact — not the ~41G
of this morning. The difference is not unrecovered waste; it is today's
running work (protected by the guardrails) plus recorded evidence and
production data. The remaining identified recoveries, in order of when
they unlock: ~6.9G of container build cache the moment the checking-setup
lane finishes (one full prune), ~0.75G of doubt-listed /tmp items once
their owners finish, and each running lane's tree/scratch as it lands and
is stripped per standing disk discipline.

## This lane's own footprint

This record was committed from a fresh worktree (`wt-disk-cleanup`,
~0.36G checkout), which was removed again after the commit; the branch
`lane-disk-cleanup` carries the record.
