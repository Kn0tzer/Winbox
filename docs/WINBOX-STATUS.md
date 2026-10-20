# Winbox manager status (keep updated; read this first)

Main branch: `fix/winbox-chromium-20261018` in /home/ubuntu/boxedwine64 (local only, unpushed, undeployed).
Last merged: integration/winbox-20261020 (10 lane branches). Final report: docs/reports/winbox-finish-20261020.md.

## Open problems (owner: none yet)
1. JIT=1 breaks Wine64 startup for notepad/doom/snake/gangbeasts on the merged build (explorer/celeste/chromium OK). Not bisected. Suspects: cpu64 + jit64speed interaction. Bisect by merging branches one at a time.
2. Portal black screen on merged build (was rendering on the live Oct 19 build abde7983b) -> likely merge regression, bisect same way.
3. Gang Beasts: epoll fix landed, never verified; blocked by #1.
4. Gamepad e2e fails. 5. Profiling incomplete (only Doom JIT=1). 6. 7 unit failures untriaged. 7. JIT32 slower than interpreter on mixed code; JIT64 ~1.9x on benchmark only.

## Workflow speedups
- New worktree: `/home/ubuntu/scripts/winbox-wt-prep.sh <worktree>` (offline assets; revert runtime-assets.json before commit).
- Build ~15 min with 4 CPUs; run only one at a time. Demo matrix: two heavy slots; boot ~100-180 s, run `DEMO=x JIT=y OUT=... node web/tests/demo-matrix.mjs`.
- Never wait with `pgrep -f` patterns that match your own command; wait on a done-file instead.
- Agents: give Luna one narrow chunk; bisect/build/measure tasks should be done by me directly, not delegated.
