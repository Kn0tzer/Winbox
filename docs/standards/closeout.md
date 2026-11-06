# Standard: closing out a finished piece of work

A lane is not done until its tree is stripped and, when the work is
fully finished, its tree is removed. Closeout leaves behind: the
branch (always), the record (committed), the register entries, and
the preserved evidence. Nothing else.

## Preconditions

- All work committed on the lane branch; the tree is clean
  (`git status` empty). An uncommitted change is never discarded:
  if it is the only copy of something, preserve it into the
  evidence store first (the `wt-fd129pre` precedent: one untracked
  file, copied out with its hash verified, before the tree went).
- The lane record is written (recording standard) and every defect
  found is on the register.

## Steps

1. **Strip generated state from your tree.**
   - Always generated, always removed: `dist/`,
     `project/linux/Build/`, every `__pycache__` under the tree,
     and any real (non-link) build output in `web/runtime/`.
   - Payload directories (`tools/rootfs64`, `tools/dxvk`,
     `tools/vkd3d`): in a tree prepared by the fresh-copy standard
     the payload paths are links into the shared store, held in
     directories git does not track — remove those link
     directories as part of the strip (removing a link never
     touches the store), or step 3's removal will refuse. In a
     tree that built payloads in place, `tools/rootfs64` and
     `tools/dxvk` hold generated payloads and are removed by name —
     but check first with `git status --ignored` and the
     tracked-file counts: in a tree that never built payloads
     there, they are tracked sources and removing them creates
     phantom deletions. Never remove `tools/` wholesale.
   - Delete your scratch under `/tmp` (anything you created there:
     build dirs, downloaded tooling, extracted archives).
2. **Preserve the evidence.** Copy your tree's `test-results/`
   into the consolidated evidence store:

       /home/ubuntu/backups/winbox-evidence-consolidated-20261101/<tree-name>/

   with a `REMOVAL-INFO.txt` naming the tree, branch, tip commit,
   date, and what was preserved.
3. **Remove the tree** (from the main repository):

       git worktree remove /home/ubuntu/wt-<name>

   It must succeed without `--force`. If git refuses, something
   uncommitted remains — go back to the preconditions and account
   for it; `--force` past an unaccounted change is never the
   answer. **Never delete the branch**; it is the work's permanent
   handle.
4. **Verify the end state** and quote it in your report:
   `git worktree list` no longer shows the tree; the branch tip
   still resolves to your final commit; `df -h /` shows the space
   returned.

## Exceptions — trees that are never stripped or removed by a lane

- The main checkout and the integration tree.
- The preserved transfer sources (`wt-restage`, `wt-x86vkd3d`) and
  the pinned-payload donor (`wt-fd38`).
- Any tree whose lane is still running — including its `/tmp`
  scratch, which may look identical to yours.

Verified literally: 2026-11-05 (this standard was followed to close
out the tree it was written in) —
`docs/reports/standards-verification-20261105.md`.
