# Workflow — the day-to-day mechanics

_How work on this project is set up and run on the shared box:
worktrees, builds, probes, and waiting. The build and probe
coordination rules themselves live in `BUILD_PROTOCOL.md` at the
repo root; this page is the practical companion. It began as the
"Workflow speedups" section of `docs/WINBOX-STATUS.md`, moved here
unedited on 2026-11-05 by the documentation-library pass._

## Workflow speedups
- New worktree: `git worktree add <tree> <base>`, then ONE step: `/home/ubuntu/winbox-project/scripts/winbox-wt-prep.sh <worktree>`. The generated payload inputs every tree needs (the pinned runtime zips and Slang assets, the DXVK/vkd3d build outputs, the `tools/rootfs64` payload extractions, the zig/disasm tool payloads) live once in the shared store `/home/ubuntu/winbox-project/store` and are LINKED into the worktree, never copied — a tree previously real-copied ~1.98 GB of them; it now holds none. The script first verifies the store against the committed `runtime-assets.json` pins and the store's own `SHA256SUMS` and refuses to link on any mismatch. It no longer overwrites the worktree's runtime-assets manifest — the old stale-pin overwrite (and its "checkout before committing" trap) is gone; ignore it in older notes. The store is read-only for consumers: to intentionally modify a payload, replace its link with a real copy first (`rm <worktree>/<path>` && `cp -a /home/ubuntu/winbox-project/store/<store-relative-path> <worktree>/<path>`). Existing trees are untouched and keep their copies. Details and side-by-side proof: `docs/reports/shared-build-store-20261105.md`.
- Build with `BW64_BUILD_TARGET=wasm64-mt-jit JOBS=2 BINARYEN_CORES=2 BW64_BUILD_OWNER=<owner>`; builds serialize on /tmp/bw64-build.lock.d and wait for probes to drain.
- Wait for load with ONE call, no polling: `/home/ubuntu/winbox-project/scripts/winbox-wait-load.sh [max_load=5] [max_min=9]` (exits when load is low or after the timeout; then proceed and note the load). Builds and probes already queue by themselves (build lock, probe slots).
- Probe slots: web/tests/probe-slots.mjs (acquireProbeSlot(); never touch /tmp/bw64-probe.lock). Demo matrix `DEMO=x JIT=1 OUT=... MAPPED_TIMEOUT_S=n node web/tests/demo-matrix.mjs` (JIT is exactly 0 or 1 — `JIT=y` throws). Suites and probes run SOLO: concurrent unit suites on this box produce contradictory counts.
- Bisect by building a detached worktree of the suspect commit; only compare runtimes built from the same base.
- Never wait with `pgrep -f` patterns that match your own command; wait on a done-file instead. Do not pattern-match `chromium` for probe checks — unrelated Chromium processes run permanently on this box.
- Agents: one narrow chunk each; bisect/build/measure by the manager directly.
