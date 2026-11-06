# Standard: starting a fresh working copy

End state: a working copy (worktree) of the project at the current
integration tip, on your own branch, prepared so it can build and
test — with no payload bytes copied.

## Steps

1. **Read the current integration tip; never guess it.** In the main
   repository (`/home/ubuntu/winbox-project/repo/boxedwine64`):

       git log -1 --format='%H %s' integ/winbox-20261022b

2. **Create the worktree on a lane branch.** Naming is fixed: the
   tree is `/home/ubuntu/winbox-project/worktrees/wt-<name>`, the branch is `lane-<name>`.

       cd /home/ubuntu/winbox-project/repo/boxedwine64
       git worktree add /home/ubuntu/winbox-project/worktrees/wt-<name> -b lane-<name> integ/winbox-20261022b

   Never work on `master`, and never create a worktree from another
   lane's branch unless your task is explicitly stacked on it (say so
   in your record when it is).

3. **Prepare the copy — one step.** Run the installed prep script:

       /home/ubuntu/winbox-project/scripts/winbox-wt-prep.sh /home/ubuntu/winbox-project/worktrees/wt-<name>

   The script is the standard; do not replace its steps by hand. It:
   - verifies the shared build-material store
     (`/home/ubuntu/winbox-project/store`) against your tree's committed
     pins (`tools/browser/runtime-assets.json`, 13 files) and against
     the store's own `SHA256SUMS` — any mismatch aborts before
     anything is linked; an abort means stop and report, never work
     around it;
   - links every generated payload input (23 links: runtime zips,
     Slang assets, DXVK and vkd3d build outputs, rootfs payload
     extractions, browser tool payloads) — it copies 0 payload bytes;
   - links `node_modules` and `web/vendor` from the main repository;
   - prepares the machine-wide compiler cache (one ccache directory
     shared by every worktree);
   - installs a verified build seed for your exact HEAD commit if
     the store holds one, so your first browser build only recompiles
     what changed after that commit.

   Success is the final line:
   `prepared <path> (linked=23 kept-existing=0; copied 0 payload bytes)`.
   A `kept-existing` count above 0 means the tree already held real
   content at a payload path — read the warnings before building.

4. **Check the disk before you start.** `df -h /` — under 5 GB free,
   cleanup comes first (see the closeout standard); do not start a
   build on a nearly full disk.

## Rules that keep the copy honest

- Payload paths in your tree are **links into a read-only store**.
  To modify a payload, replace its link with a real copy first:
  `rm <path> && cp -a /home/ubuntu/winbox-project/store/<store-path> <path>`.
  Never write through a link.
- **Never run `tools/browser/stage-vendor.mjs` in a prepared tree.**
  `web/vendor` is a link into the main repository; staging writes
  through it and dirties every other copy's vendor tree.
- **Never run a fresh `npm install`.** The linked `node_modules` is
  the project's dependency set; a fresh resolve produces a different
  set that is known to break the test tooling.
- The prep script lives outside version control at
  `/home/ubuntu/winbox-project/scripts/winbox-wt-prep.sh` (the store verification
  makes a stale or edited copy fail loudly rather than silently).
  Record the script's sha256 in your verification notes when you
  rely on it for a gate.

Verified literally: 2026-11-05 (prep script sha256
`7731a66de5d2…` at verification time) —
`docs/reports/standards-verification-20261105.md`.
