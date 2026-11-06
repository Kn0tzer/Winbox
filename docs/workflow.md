# Workflow — the day-to-day mechanics

_How work on this project is set up and run on the shared box:
worktrees, builds, probes, and waiting. The build and probe
coordination rules themselves live in `BUILD_PROTOCOL.md` at the
repo root; this page is the practical companion. It began as the
"Workflow speedups" section of `docs/WINBOX-STATUS.md`, moved here
unedited on 2026-11-05 by the documentation-library pass._

## Workflow speedups
- New worktree: `/home/ubuntu/scripts/winbox-wt-prep.sh <worktree>` (offline assets; local manifest is assume-unchanged, never commit it).
- Build with `BW64_BUILD_TARGET=wasm64-mt-jit JOBS=2 BINARYEN_CORES=2 BW64_BUILD_OWNER=<owner>`; builds serialize on /tmp/bw64-build.lock.d and wait for probes to drain.
- Wait for load with ONE call, no polling: `/home/ubuntu/scripts/winbox-wait-load.sh [max_load=5] [max_min=9]` (exits when load is low or after the timeout; then proceed and note the load). Builds and probes already queue by themselves (build lock, probe slots).
- Probe slots: web/tests/probe-slots.mjs (acquireProbeSlot(); never touch /tmp/bw64-probe.lock). Demo matrix `DEMO=x JIT=1 OUT=... MAPPED_TIMEOUT_S=n node web/tests/demo-matrix.mjs` (JIT is exactly 0 or 1 — `JIT=y` throws). Suites and probes run SOLO: concurrent unit suites on this box produce contradictory counts.
- Bisect by building a detached worktree of the suspect commit; only compare runtimes built from the same base.
- Never wait with `pgrep -f` patterns that match your own command; wait on a done-file instead. Do not pattern-match `chromium` for probe checks — unrelated Chromium processes run permanently on this box.
- Agents: one narrow chunk each; bisect/build/measure by the manager directly.
