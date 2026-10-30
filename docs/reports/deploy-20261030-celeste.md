# Deploy 2026-10-30 (second) — Celeste GLX fix + vk content-hash widening

Deployed by the celeste-merge lane. Live site serves git_sha 5741034f3 /
wasm e297364a (verified over public HTTPS: BUILD_INFO stamp, shell 200,
range 206 on wasm, wine64.zip and the support archive). Rollback:
kn0tzer/winbox-prev-20261030-1023 (the cc1aecbaa build); the older
rollback was pruned per the newest-only rule. Never pushed.

## What merged
- lane-celeste (5 commits, base 421e18dce): the GLX 1.3 drawable surface
  (glXCopyContext, pbuffers, window/pixmap wrappers, queries) across the
  guest libGL shim and the host GL bridge, + ABI/test pin bumps and
  artifact-manifest regens. Merged as 6f00b2c5f.
- lane-perf (2 commits, same base): vk64bridge v2 content hash widened
  to four interleaved FNV-1a chains + vulkan register citation re-pin.
  Merged as 5741034f3.
- Combined diff vs the previous integ tip (f6e22a1cd) accounted file by
  file: exactly the two lanes' 11 files, nothing else. The shared
  artifact-manifest rows composed (w64 line from integ, support line
  from celeste); a post-build regeneration produced no drift.

## Gate on the shipped build (5741034f3)
- bw64-build.sh: literal "Browser export verified" (dependency archive
  3f1268a546e0 verified); a pristine --export-only re-export was the
  shipped artifact (prefix64 back to the canonical 152 members, no
  probe executables; dist wine64.zip byte-identical to canonical;
  exactly one support archive).
- check_runtime_completeness.py: 0 failures (1 standing environmental
  skip). bootstrap.py --check: 13 files verified.
- JS unit suite: 333 passed / 0 failed.
- Emulator native suite: 648 tests / 0 FAILED (canonical single-threaded
  invocation `boxedwine 0 0 1`).
- Vulkan register citation audit: 231 MATCH + 14 MATCH-INHERITED, 0 drift.
- Guest probes on the gate dist: tri12 RESULT 0; tri12-32 RESULT 0.
- Celeste through the LIVE website (site flow: click the Celeste card):
  DONE LOADING at 406 s, title screen reached and animating (two
  screenshots 10 s apart differ; title art visually confirmed), 120 s
  measure = 33 frames by CDP screencast (0.27 FPS) and 33 presents by
  the project's own counter (0.3 FPS, path "sdl") — the two instruments
  agree, matching the lane's hybrid numbers. Evidence:
  test-results/celeste-liveproof-20261030/ (driver + summary + shots).

## Deploy mechanics
Staged dist -> kn0tzer/winbox-next; full-tree diff vs live showed only
the intended changes (emulator js/wasm, BUILD_INFO, dependencies.json,
the new support archive replacing the old, and runtime release-id
stamps in app.mjs / runtime index.html); vendor and assets identical.
Rename-swap; live verified as above. Disk after cleanup: see git log
record; generated state (dist, native Build, pycache) stripped from
the integ tree afterwards, canonical web/runtime set left intact.
