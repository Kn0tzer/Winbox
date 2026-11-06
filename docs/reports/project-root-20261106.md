# Project root consolidation — 2026-11-06

## What was asked

The project is to exist in one place on this server: its repository, working copies, shared build material, automated checking setup, supporting scripts, and evidence records under a single project root, with a written map at that root. Nothing project-owned scattered in the home directory's top level, in temporary storage, or anywhere the map does not name. Live-served material that genuinely cannot move is named in the map as a deliberate exception, with its reason. Running work is not moved or renamed under its feet: its moves are prepared completely and recorded as ready-to-apply.

## The root and the map

- Root created: `/home/ubuntu/winbox-project/`, with subdirectories `worktrees/` (populated), `repo/`, `store/`, `checks/`, `scripts/`, `backups/`, `evidence/`, `cache/`, `records/` (targets named in the map).
- Map: `/home/ubuntu/winbox-project/MAP.md` — 10,211 bytes, sha256 `06e822c9198c8eae9fa0f1b10a4fe4d7df8fa39e5911873dcaff3af06bbc8b3b`. It names every project asset on the box as in-place, ready-to-apply (moves M1–M8, each with its exact procedure, its proof, and the work it waits for), a named live exception, or a proposed removal.

## Moved now, with proof

Three preserved trees, all idle, verified before moving (no open handles in any process, no live script referencing them, the worktree-preparation script reads only the shared store since the shared-build-store work landed):

- `wt-fd38` → `winbox-project/worktrees/wt-fd38` (`git worktree move`). After: HEAD `0ddb6f31a`, 0 status entries — identical to before; `tools/browser` present.
- `wt-restage` → `winbox-project/worktrees/wt-restage`. After: HEAD `f69fbcb3a`, 1 status entry — identical to before; `tools/vkd3d/build.w64/build.ninja` present.
- `wt-x86vkd3d` → `winbox-project/worktrees/wt-x86vkd3d`. After: HEAD `7e079ebdf`, 1,501 status entries — identical to before (the vkd3d payload outputs are untracked by design); `tools/vkd3d/build.w32/build.ninja` present.

Old paths no longer exist; `git worktree list` shows the three trees at their new homes. Nothing else was moved, and nothing was deleted.

## Why everything else is ready-to-apply rather than moved

At survey time the rest of the project's material was owned by work actively running on the shared box:

- Eleven working trees plus the integration tree had lanes in flight (four placeholder efforts, the game-controller effort, three compiler-speedup efforts and their proof tree, the code-organization and documentation efforts, and the batch-8 composition in the integration tree).
- The shared build store had live links from five running trees (census at survey: 27, 23, 27, 22, and 21 links) — moving it would have broken those trees mid-build.
- The supporting scripts were edited by the running compiler lanes within the hour before the survey (the preparation script and the build-seed publisher both carry same-hour timestamps).
- The checking setup was up (its server and agent processes running) and hosts the laptop-overflow effort's request machinery.
- The archives and evidence directories were being read by the running batch-8 gate and written by the running placeholder lanes (fresh snapshots the same evening).
- The compiler cache directory belongs to the still-running compiler-cache effort, and the 599 MB staging directory was staged by the still-running Wine-backport work.

Each of these has its move fully prepared in the map (M1–M8). As each owning lane lands, its move is a single mechanical step with its proof, executed in the same round the completion is processed. M1 (the main repository) and the integration tree's M7 share one quiet window.

## Exceptions and non-project material

Named in the map as deliberate exceptions: the live website and its rollback (served from the user's web tree; moving them is a redeploy decision), the network relay service (runs in the box's shared container setup), and the laptop-access helper plus its address file (shared with another campaign). Verified as not this project and left alone: the microapps monorepo, the home directory's own website repository, the box-maintenance log `clean.log`, the shared `docker/` service data, and other tenants' material.

## Proposed removals (nothing deleted by this effort)

Listed in the map with their evidence: the dead batch-6 deploy script in `/tmp` (it references the preserved trees at their pre-move paths and can no longer run as written), three unattributed scratch directories in `/tmp`, the superseded runtime-asset archive generations beyond the standing keep-current-plus-one rule, and each Wine-backport snapshot once its owning lane lands.

## Defect register

No product defects were found during this effort, and no register entry is made. The one stale consumer found — the dead deploy script above — is a leftover of finished work, recorded as a proposed removal rather than a product defect.

## Follow-through owed

- Execute M7 per lane as lanes land; M2 when the last store-linked tree lands; M4 when the compiler lanes finish with the scripts; M3 when the checking and laptop-overflow work finishes; M5 when the batch-8 gate and the snapshot-cutting lanes finish; M6 with the compiler-cache lane; M8 on the backport lanes' confirmation; M1 with the integration tree in the batch-9 quiet window.
- Amend MAP.md at each executed move (status flips from ready-to-apply to in place, with the proof recorded).
