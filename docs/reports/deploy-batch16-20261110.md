# Deploy record — batch 16 — 2026-11-10

Status: batch 16 is LIVE at https://kn0tzer.work.gd/winbox/ and, with the
commit carrying this record, PUSHED to origin master. The swap happened at
2026-11-10 23:53 UTC. The live acceptance set passed in full: x86
certification 5/5, x64 certification 3/3, the consumer browser program
series 6/6 mapped (zero guest-memfault lines in all six consoles), the
supporting-programs check PASS live, and the controller test PASS live on
both architectures first try.

## What was deployed

- Integration tip `e23320cbdeb8e730f9c2d9119d940d3697a1ec82` (branch
  `integ/winbox-20261022b`): the batch-16 merge — 3 lanes — per
  `docs/reports/merge-batch16-20261110.md`. Headlines: a guest program
  that draws and reads its own picture back without binding its read
  target first now gets the correct pixels instead of black (FD-270,
  closed CL-202); the supporting-programs check's web-page wait is
  raised 180 s to 360 s so it passes under heavy load instead of
  failing on a too-short wait (FD-273, closed CL-203 — test-only, no
  product code changed); the browser-program check now refuses to
  start when machine memory is too low, retries once if its tab dies,
  and fails fast on a tab death instead of sitting silent until its
  own timeout (FD-272, closed CL-204 — test driver only). The register
  runs to FD-273, closures to CL-204. Gate: checking service pipeline
  89 standard (citations 0 drift; JS unit 352 passed / 2 known
  bare-checkout artifacts; native 706/0 + 121/0); browser guest suite
  not re-run at gate scale — the three fixes are proven by their own
  lanes' probes and the live acceptance below.
- The shipped runtime set is unchanged from batch 15: the support
  archive stays `93fd17c09af7...` (517,564,415 bytes); no wine archive
  and no guest prefix changes. The deploy build's compiled core is new
  (wasm `bcd9d500...`, 5,756,971 bytes; only the FD-270 bridge change
  touches product code).

## Build

- Fresh deploy worktree `wt-deploy-batch16`, branch
  `lane-deploy-batch16` at the integration tip; prepared with
  `winbox-project/scripts/winbox-wt-prep.sh` — payload pins verified
  against the shared store, starting state clean. No build seed existed
  for this tip; the build compiled from scratch against the warm
  compiler cache.
- Built fresh from the tip through the shared build script's lock; the
  full log is preserved at
  `wt-deploy-batch16/test-results/deploy-batch16-20261110/build.log`
  and carries "Browser export verified" exactly once and "BUILD
  COMPLETE".
- Build identity (`dist/runtime/BUILD_INFO.json`): git_sha
  `e23320cbdeb8e730f9c2d9119d940d3697a1ec82`, git_dirty_files 0, wasm
  sha256 `bcd9d500a0a70c13e522dffb8d40c7f6a747d4752c43b3e2c6c0935a3f71e17b`
  (5,756,971 bytes), js sha256
  `0312a6d4dee06cda70da820a750c85192cf76e8ee9b8e62e511b47b0120568d1`,
  builder "build:3804006 (pid 3804006)". JS unit on the built dist: 354
  passed, 0 failed (`unit-on-dist.log`).

## Staging and swap

- Staged the fresh dist into `/home/ubuntu/kn0tzer/winbox-next` with
  symlinks resolved (`cp -rL`); the staged tree was verified
  byte-identical to the fresh dist (`diff -r` clean), 0 symlinks,
  exactly ONE support archive
  (`support-93fd17c09af7...`).
- Swap at 2026-11-10 23:53 UTC: the batch-15 tree became
  `winbox-prev-20261110-2353` (the current rollback); `winbox-next`
  became live `winbox`.
- Off-box HTTPS verification (from outside the box, after the swap):
  BUILD_INFO 200 (git_sha `e23320cbd`, wasm `bcd9d500...`, dirty 0,
  builder build:3804006); shell 200; runtime index 200; wasm range
  request 206; support-archive range request 206; served
  `wine64-launcher.js` sha256 matches the live tree exactly
  (`ba1e86b3...`, unchanged from batch 14).
- Disk note: free space was 2.5G at lane start and 2.0G at the build,
  so the superseded batch-14 rollback
  (`winbox-prev-20261110-1607`, 984M, build `d3b9bb7f5`) was removed
  BEFORE the build rather than after the swap, to leave room for the
  fresh dist plus the staging copy. The safety property is preserved:
  the batch-15 build was live throughout, so a known-good build was
  always available, and it became the rollback at the swap. Free space
  recovered to 8.7G during the live checks as other tenants freed
  space. The ONLY rollback now is
  `/home/ubuntu/kn0tzer/winbox-prev-20261110-2353` (batch 15,
  `e3971e98c`).

## Live acceptance

All runs served from https://kn0tzer.work.gd/winbox, through the
probe-slot system, one at a time. Full logs under
`wt-deploy-batch16/test-results/deploy-batch16-20261110/` (drivers
`live-driver.mjs`, `live-gamepad.mjs`, `run-certs.sh`,
`run-consumer.sh`, `run-part2.sh`).

- Certifications: x86 5/5 certified (median 26,506 ms, handle
  `0x10052`, 0 false-ready, 0 restarts, 0 memfaults); x64 3/3
  certified (median 363,098 ms under load 8–14, handle `0x10048`, 0
  false-ready, 0 restarts, 0 memfaults). Census exact on all 8 boots:
  5 BW64-INFO served-link lines + 1 standing BW64-FAIL netlink line
  each.
- Consumer browser program series 6/6 mapped, first attempt each:
  stage "mapped", overlay cleared, 1,273,748 lit pixels each, frames
  15/6/5/6/8/7, pageErrorCount 0 and httpErrorCount 0 in all six
  runs. Zero guest-memfault lines in all six consoles (re-grepped:
  no `guest-memfault`, no `0xfffffd08`, no "Failed to get first op").
  Console error counts 223/223/223/223/223/221 are the standing benign
  Emscripten GL-emulation warnings. The memory gate passed on every
  run (~24,000 MiB free vs 5,120 MiB needed); no retry was needed.
- Supporting programs: `PASS common-dependencies` (1 passed, 0
  failed) on the live build — the FD-273 wait fix working live.
- Controller: PASS 2/2 first try, all 10 phases on both
  architectures (`boot-ready, ready, xinput-connected, caps,
  resting-state, dinput-enum, button-a, stick, triggers, dpad`).
- No A/B adjudication was needed: nothing failed, so there was
  nothing to adjudicate. The local-serve arm was not run (the stale
  web-server configuration for the games files is still open — no
  restart without permission — and the live-site checks are
  same-origin and unaffected).

## Notes for the coordinator

- The games mirror on port 8901 (left running by the FD-272 lane) was
  not used and was left running as found.
- A leftover monitoring loop from the FD-272 lane (a memory-scoped
  watcher writing `test-results/fd272-mem-scoped.csv` in `wt-fd272`)
  was observed still running during the consumer series; it exits on
  its own when no demo-matrix run is active and was left alone.
- The SSH proxy to the box dropped repeatedly during the gamepad run
  (several exit-255 disconnects); all waits were recovered by
  re-reading the preserved logs, and nothing was re-run or repeated
  on an unknown outcome.
