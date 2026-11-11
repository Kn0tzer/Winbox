# Deploy record — batch 17 — 2026-11-11

Status: batch 17 is LIVE at https://kn0tzer.work.gd/winbox/ and, with the
commit carrying this record, PUSHED to origin master. The swap happened at
2026-11-11 12:22 UTC. The live acceptance set passed in full except for one
check, adjudicated below as a load artifact and proven not a regression:
x86 certification 5/5, x64 certification 3/3, the consumer browser program
series 6/6 mapped (zero guest-memfault lines in all six consoles), the
controller test PASS live on both architectures first try, and the
supporting-programs check's failure reproduced identically on the outgoing
known-good build under the same load.

## What was deployed

- Integration tip `176e81cae63ae0415b6c9967971d86277480e5af` (branch
  `integ/winbox-20261022b`): the batch-17 merge — 3 lanes — per
  `docs/reports/merge-batch17-20261111.md`. Headlines: the archive producer
  path that re-cut `wine64.zip` no longer flattens symlink type bits
  (FD-200, closed CL-205); browser probes no longer starve behind a build
  queue — claims are now per-tree and published only while draining for a
  write or holding the lock (FD-224, closed CL-206); three win32u
  placeholders genuinely implemented in the Wine 8 backports build
  (`NtUserGetTopLevelWindow`, `NtGdiGetTextExtent`,
  `NtGdiGetGlyphIndicesWInternal` — FD-168 slice 4, proven, entry stays
  open at 2,011). FD-274 filed at composition: a fresh 64-bit boot takes
  ~403 s quiet and ~500–600 s under load against the suite's 600 s boot
  wait. The register runs to FD-274, closures to CL-206. Gate: checking
  service pipeline 94 standard (citations 0 drift; JS unit 352 passed /
  2 known bare-checkout artifacts; native 706/0 + 121/0).
- The shipped compiled core is byte-identical to batch 16 (wasm
  `bcd9d500...`, 5,756,971 bytes; no product code moved). What changed is
  the runtime: `wine64.zip` is now `e1479bea...` (the FD-168 slice-4
  build); the support archive stays `93fd17c09af7...` (517,564,415 bytes);
  no guest prefix changes.

## Build

- Fresh deploy worktree `wt-deploy-batch17`, branch
  `lane-deploy-batch17` at the integration tip; prepared with
  `winbox-project/scripts/winbox-wt-prep.sh` — payload pins verified
  against the shared store, starting state clean. No build seed existed
  for this tip; the build compiled from scratch against the warm
  compiler cache.
- Built fresh from the tip through the shared build script's lock; the
  full log is preserved at
  `wt-deploy-batch17/test-results/deploy-batch17-20261111/build.log`
  and carries "Browser export verified" exactly once and "BUILD
  COMPLETE".
- Build identity (`dist/runtime/BUILD_INFO.json`): git_sha
  `176e81cae63ae0415b6c9967971d86277480e5af`, git_dirty_files 0, wasm
  sha256 `bcd9d500a0a70c13e522dffb8d40c7f6a747d4752c43b3e2c6c0935a3f71e17b`
  (5,756,971 bytes), js sha256
  `0312a6d4dee06cda70da820a750c85192cf76e8ee9b8e62e511b47b0120568d1`,
  builder "build:570847 (pid 570847)". JS unit on the built dist: 354
  passed, 0 failed (`unit-on-dist.log`).

## Staging and swap

- Staged the fresh dist into `/home/ubuntu/kn0tzer/winbox-next` with
  symlinks resolved (`cp -rL`); the staged tree was verified
  byte-identical to the fresh dist (`diff -r` clean), 0 symlinks,
  exactly ONE support archive
  (`support-93fd17c09af7...`).
- Swap at 2026-11-11 12:22 UTC: the batch-16 tree became
  `winbox-prev-20261111-1222` (the current rollback); `winbox-next`
  became live `winbox`. The superseded batch-15 rollback
  (`winbox-prev-20261110-2353`) was removed AFTER the swap, so a
  known-good build was available throughout and exactly one rollback
  remains.
- Off-box HTTPS verification (from outside the box, after the swap):
  BUILD_INFO 200 (git_sha `176e81cae`, wasm `bcd9d500...`, dirty 0,
  builder build:570847); shell 200; runtime index 200; wasm range
  request 206; support-archive range request 206; served
  `wine64-launcher.js` sha256 matches the live tree exactly
  (`ba1e86b3...`, unchanged from batch 14).

## Live acceptance

All runs served from https://kn0tzer.work.gd/winbox, through the
probe-slot system, one at a time. Full logs under
`wt-deploy-batch17/test-results/deploy-batch17-20261111/` (drivers
`live-driver.mjs`, `live-gamepad.mjs`, `run-certs.sh`,
`run-consumer.sh`, `run-part2.sh`).

- Certifications: x86 5/5 certified (median 35,973 ms, handle
  `0x10052`, 0 false-ready, 0 restarts, 0 rescues); x64 3/3
  certified (median 446,261 ms under load 10–12, handle `0x10048`,
  0 false-ready, 0 restarts, 0 rescues). Census exact on all 8 boots:
  5 BW64-INFO lines + 1 standing BW64-FAIL netlink line each.
- Consumer browser program series 6/6 mapped, first attempt each:
  stage "mapped", overlay cleared, 1,273,748 lit pixels each, frames
  5/5/8/4/4/6, pageErrorCount 0 and httpErrorCount 0 in all six
  runs. Zero guest-memfault lines in all six consoles (re-grepped:
  no `guest-memfault`, no `0xfffffd08`, no "Failed to get first op").
  Console error counts 221/219/219/215/221/217 are the standing benign
  Emscripten GL-emulation warnings. The memory gate passed on every
  run; no retry was needed.
- Controller: PASS 2/2 first try, all 10 phases on both
  architectures (`boot-ready, ready, xinput-connected, caps,
  resting-state, dinput-enum, button-a, stick, triggers, dpad`).
- Supporting programs: FAIL on `common-dependencies` — adjudicated
  below. No A/B adjudication was needed for anything else: nothing
  else failed. The local-serve arm was not run (the stale web-server
  configuration for the games files is still open — no restart without
  permission — and the live-site checks are same-origin and
  unaffected).

## Adjudication — supporting-programs failure is load, not a regression

The `common-dependencies` check failed three times, every time with
the identical mode — "Guest output /MSHTML: RESULT 0/ not seen within
360000ms", with the guest demonstrably alive and still working (CPU64
STACK lines from live threads in all three logs):

- Live batch 17, first run: FAIL in 834,815 ms, load 15.54 at exit.
- A/B on the batch-16 rollback (`winbox-prev-20261111-1222`,
  `e23320cbd`, same wasm `bcd9d500...`): FAIL in 830,651 ms, load
  16.31 falling to 10.79. The A/B runs the identical suite against the
  rollback tree through a local same-origin server
  (`WINE_DIST_DIR=<rollback>`); log
  `common-deps-ab-rollback.log`, suite dir preserved as
  `suite-common-dependencies-ab-rollback-batch16`.
- Live batch 17, re-run in a quieter moment: FAIL in 883,596 ms,
  load 9.18 rising to 12.81. Log `common-deps-rerun-live.log`, suite
  dir preserved as `suite-common-dependencies-rerun-live-batch17`.

The outgoing known-good build fails identically, in the same mode and
within seconds of the same duration, under the same load — so the
failure is not something batch 17 did. The product code is
byte-identical to batch 16, so no regression in the wasm is possible;
the runtime delta (three win32u exports) is outside the MSHTML path,
and the A/B empirically confirms it has no effect. The cause is the
test's waits under sustained load: the guest boot alone takes ~470 s
under load (FD-274) and the MSHTML stage then needs more than its
360 s wait (FD-273 raised it 180 s to 360 s on measurements up to
load 12–14, where the stage took 230 s; under sustained load 9–16 it
now needs more). The deploy ships on the strength of the three
passing live checks plus this exoneration.

Follow-up owed (flagged for the coordinator to file): the MSHTML 360 s
wait is now too short under sustained high load and needs raising
again, or load-aware waiting — in the FD-273/FD-274 family.

## Notes for the coordinator

- The SSH proxy to the box dropped repeatedly during the long runs
  (several exit-255 disconnects, one at ~275 s killing a foreground
  unit-suite run mid-flight). All waits were recovered by re-reading
  the preserved logs. One duplicate unit-suite run was started on the
  mistaken belief the first had died — the first had survived the
  drop; both were killed by exact PID and the suite re-run once,
  cleanly, to 354/0. Nothing was re-run or repeated on an unknown
  outcome.
- A local service restart during the part-2 wait wiped /tmp, including
  the lane's waiter scripts; they were recreated and the wait resumed
  from the preserved logs.
- The deploy tree `wt-deploy-batch17` still needs its generated state
  stripped before it is retired (`dist`, `project/linux/Build`).
- Disk: 9.1G free at lane start, 7.6G after staging, 8.4G at lane
  close; never under the 5G floor.
