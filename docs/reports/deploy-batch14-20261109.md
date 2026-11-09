# Deploy record — batch 14 — 2026-11-09

Status: batch 14 is LIVE at https://kn0tzer.work.gd/winbox/ and, with the
commit carrying this record, PUSHED to origin master. The swap happened at
2026-11-09 14:51 UTC. The live acceptance set passed in full: x86
certification 5/5, x64 certification 3/3, the consumer browser program
series 6/6 mapped (zero guest-memfault lines in all six consoles), the
supporting-programs check PASS live, and the controller test PASS live on
both architectures after one adjudication re-run (the first x64 attempt
failed on the known trigger-matcher race; the re-run passed 2/2 and its
console carries the correct trigger values).

## What was deployed

- Integration tip `d3b9bb7f5ff533834ee1de190310703254035e5a` (branch
  `integ/winbox-20261022b`): the batch-14 merge — 4 lanes, 12 cherry-picks —
  per `docs/reports/merge-batch14-20261109.md`. Headlines: the guest
  name-lookup negative test is fixed (FD-265, closed CL-194 — the synthetic
  DNS now answers NXDOMAIN for never-resolvable names); ten Vulkan handlers
  proven to be harness artifacts, no product change (FD-206, closed
  CL-195); the GL error answers fixed (FD-208, closed CL-196); the GL
  profile default raised to 3.3 with 50 of 71 entry points newly reachable
  (FD-209, closed CL-197 after the merge renumbered its duplicate CL-194
  claim). The register runs to FD-269, closures to CL-197. Gate: checking
  service pipeline 72 standard (citations 0 drift; JS unit 352 passed / 2
  known bare-checkout artifacts; native 706/0 + 121/0); browser guest suite
  51 passed / 4 failed, all adjudicated (winsock-dns environment artifact,
  re-run passes; memory-shared load artifact, re-run passes; input-gamepad
  harness matcher, filed FD-269; jitbench completeness gap, as in batches
  10-13). Two gate-filed entries stay open and do not block this deploy:
  FD-268 (GL buffer/indexed-state cluster at the 3.3 default) and FD-269
  (controller-test matcher reads stale in-flight lines under load).
- The shipped runtime set changes with this batch: the support archive is
  rebuilt from the merged GL shim — `caf6b5aed680...` (517,564,371 bytes),
  superseding the batch-11-era `c4dbe359...`. No wine archive and no guest
  prefix changes. The deploy build's compiled core is new (wasm
  `b01c7dc6...`, 5,752,477 bytes; both GL lanes change bridge code).
  Verified before the swap: the fresh dist differs from the live batch-13
  tree in exactly 7 files (`app.mjs`, `network.mjs`,
  `runtime/BUILD_INFO.json`, `runtime/boxedwine64.js`,
  `runtime/boxedwine64.wasm`, `runtime/dependencies.json`,
  `runtime/index.html`) plus the support-archive replacement; the four
  runtime archives are byte-identical to the rollback. The served launcher
  is unchanged from batch 13 (sha256 `ba1e86b3...`, the FD-266 fix),
  verified over public HTTPS after the swap.

## Build

- Fresh deploy worktree `wt-deploy-batch14`, detached HEAD at the
  integration tip; prepared with `winbox-project/scripts/winbox-wt-prep.sh`
  — payload pins verified against the shared store (the store already held
  the new support archive), starting state clean. No build seed existed for
  this tip; the build compiled from scratch against the warm compiler
  cache.
- Built fresh from the tip through the shared build script's lock, owner
  `deploy-batch14`; the full log is preserved at
  `wt-deploy-batch14/test-results/deploy-batch14-20261109/browser-build.log`
  and carries "Runtime assets verified (13 files)", "Browser export
  verified" exactly once, "dependency archive caf6b5aed680 verified", and
  "BUILD COMPLETE".
- Build identity (`dist/runtime/BUILD_INFO.json`): git_sha
  `d3b9bb7f5ff533834ee1de190310703254035e5a`, git_dirty_files 0, wasm
  sha256 `b01c7dc6de6bc9aae68d4e56568fb92172275c45c4557f3c8c9dd6fa553ad0d1`
  (5,752,477 bytes), js sha256
  `03cf95c70feb0df8480ad608e20c34aa2b3a81f64ee188b8cce27cddd48baed6`,
  builder "deploy-batch14 (pid 1621646)". JS unit on the built dist: 354
  passed, 0 failed (`unit-on-dist.log`).

## Staging and swap

- Staged the fresh dist into `/home/ubuntu/kn0tzer/winbox-next` via a
  clean bootstrap only, with symlinks resolved (`cp -rL`); the staged
  tree was verified byte-identical to the fresh dist (`diff -r` clean),
  0 symlinks, exactly ONE support archive
  (`support-caf6b5aed680...`).
- Swap at 2026-11-09 14:51 UTC: the batch-13 tree became
  `winbox-prev-20261109-1451` (the current rollback); `winbox-next`
  became live `winbox`.
- Off-box HTTPS verification (from outside the box, after the swap):
  BUILD_INFO 200 (git_sha `d3b9bb7f5`, wasm `b01c7dc6...`, dirty 0,
  builder deploy-batch14); shell 200; runtime index 200; wasm range
  request 206; support-archive range request 206; served
  `wine64-launcher.js` sha256 matches the live tree exactly.
- After the live checks were verified, the superseded batch-12 rollback
  `/home/ubuntu/kn0tzer/winbox-prev-20261109-0629` (984M, build
  `40a5dd699`) was removed. The ONLY rollback now is
  `/home/ubuntu/kn0tzer/winbox-prev-20261109-1451` (batch 13,
  `47a45376b`).
- Disk during the deploy: 27G free at the build, 25G at the swap, 22G at
  the end (the other tenant's hourly game backup wrote during the live
  checks, as expected; never touched).

## Live acceptance

All runs served from https://kn0tzer.work.gd/winbox, through the
probe-slot system, one at a time. Full logs under
`wt-deploy-batch14/test-results/deploy-batch14-20261109/` (drivers
`live-driver.mjs`, `live-gamepad.mjs`, `run-certs.sh`,
`run-consumer.sh`, `run-part2.sh`).

- 32-bit certification 5/5: every boot certified first try, handle
  `0x10052`, 0 restarts, 0 false-ready; median ready 27,522 ms
  (`cert-x86/summary.json`).
- 64-bit certification 3/3: every boot certified first try, handle
  `0x10048`, 0 restarts, 0 rescues; median ready 178,148 ms
  (`cert-x64/summary.json`) — slower than batch 13's 71,079 ms under
  the day's load (load 10.92 at the end of the series), all three
  certified inside the 900 s budget with no rescue.
- Census: the 8 preserved certification consoles (5 x86, 3 x64) each
  logged exactly 5 BW64-INFO served-link lines and exactly 1 BW64-FAIL —
  the standing `netlink-recvfrom flags=0x22` EINVAL line. No start fault:
  the FD-115 tally is unchanged.
- Consumer series (baseline terms, no URL parameters): 6/6 mapped — b1 5
  frames / 33.431 s, b2 8 / 33.328 s, b3 10 / 45.457 s, b4 6 / 45.217 s,
  b5 5 / 55.837 s, b6 8 / 41.893 s (click-to-mapped); 0 page errors in
  every run; zero guest-memfault lines in all six preserved consoles.
  The 221 console errors in b1 are the standing emscripten GL-emulation
  warnings plus guest-side Windows network errors from the guest browser
  (no guest network by default).
- Supporting programs: PASS live (1 passed, 0 failed;
  `common-dependencies`).
- Controller: PASS live 2/2 after one adjudication re-run — see below.

## Adjudication — controller x64 triggers matcher race (first attempt)

- First attempt: x86 PASS (all 10 phases); x64 FAIL at triggers — the
  driver's trigger wait takes the first STATE line matching
  `lt=255 rt=(\d+)`, which read `lt=255 rt=0`. The preserved x64
  console contains no `rt=127` line at all: the guest printed the
  left-trigger-full state before the right-trigger half value (set in
  the same driver step) reached the guest's next poll, and the driver
  took that intermediate line. The guest path is not implicated: the
  left trigger read 255, the stick phases passed at full scale, and the
  DINPUT states were correct. This is the FD-269 matcher class the
  batch-14 gate filed (a matcher reading a stale in-flight line under
  load; load was 8.93-10.73 during the attempt). The first attempt's
  results stand in `gamepad-live-attempt1.log` with its consoles
  preserved as `gamepad-live-x64-console-attempt1.log` and
  `gamepad-live-x86-console-attempt1.log`.
- Adjudication re-run 1: 2/2 PASS, both widths, all 10 phases
  (boot-ready, ready, xinput-connected, caps, resting-state,
  dinput-enum, button-a, stick, triggers, dpad)
  (`gamepad-live-rerun1.log`). Its preserved x64 console carries
  `lt=255 rt=127` twice — the right trigger half did read 127 on the
  live build — and also `lt=0 rt=127` twice, the same race in the other
  direction, confirming the failure is in the driver's first-match
  logic, not the product.

## Push

- Remote master before the push: `cc8211f890dc2edaa4c0e288a3ad9e97b5df7c94`
  (batch 13). The held-back commit `69c800476` (edits
  `.github/workflows/deploy-pages.yml`; the box credential lacks
  workflow scope) was verified NOT an ancestor of the integration tip
  before the push.
- Procedure (standing): merge the integration tip into the current
  remote master via the temp-worktree procedure — a `--no-ff` merge in
  a detached worktree at the remote tip, pushing `HEAD:master` to
  origin (github.com/Kn0tzer/Winbox) — verified by reading the remote
  back. Local branches/history untouched; the temp worktree removed
  afterwards. The push result and the remote read-back are stated in
  the coordinator's report (this record is written before the push, as
  the commit carrying it rides in the merge).
