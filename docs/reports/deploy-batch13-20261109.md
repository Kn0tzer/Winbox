# Deploy record — batch 13 — 2026-11-09

Status: batch 13 is LIVE at https://kn0tzer.work.gd/winbox/ and, with the
commit carrying this record, PUSHED to origin master. The swap happened at
2026-11-09 06:29 UTC. The live acceptance set passed in full: x86
certification 5/5, x64 certification 3/3, the consumer browser program
series 6/6 mapped (no fault-loop incident this deploy), the
supporting-programs check PASS live, and the controller test PASS live on
both architectures after adjudication re-runs (both x64 first failures are
the known driver matcher race, proven from the preserved consoles).

## What was deployed

- Integration tip `47a45376b1fec70b6f2738e2f4807295e92b6a72` (branch
  `integ/winbox-20261022b`): the batch-13 merge — 2 lanes, 7 cherry-picks —
  per `docs/reports/merge-batch13-20261109.md`. Headlines: the session no
  longer dies silently after fault containment ends a faulting guest
  process (FD-266, closed CL-192), and the 64-bit boot smoke test's wait
  is calibrated to the measured boot (FD-264, closed CL-193, confirmed by
  the batch gate's boot64-smoke PASS). The register runs to FD-266,
  closures to CL-193. Gate: checking-service pipeline 67 standard
  (citations 0 drift; JS unit 352 passed / 2 known bare-checkout
  artifacts; native 706/0 + 121/0); browser guest suite 53 passed / 2
  failed, both adjudicated (FD-265 already open; jitbench
  benchmark-completeness gap).
- This batch changes only the served launcher script and one test: the
  deploy build's compiled core is byte-identical to batch 12's
  (wasm `62f6123e...`, 5,737,538 bytes; boxedwine64.js sha256
  `5d4df384...`, unchanged). The runtime file set is UNCHANGED from
  batch 11 (runtime-assets-20261107-batch11) — no wine archive, no guest
  prefix was re-cut or re-pinned. Verified before the swap: the fresh
  dist differs from the live batch-12 tree in exactly 4 files
  (`app.mjs`, `runtime/BUILD_INFO.json`, `runtime/index.html`,
  `runtime/wine64-launcher.js`); the four runtime archives are
  byte-identical to the rollback. The served launcher's sha256 changed
  from the rollback's `2edf26d9...` to `ba1e86b3...` (the FD-266 fix),
  verified over public HTTPS after the swap.

## Build

- Fresh deploy worktree `wt-deploy-batch13`, detached HEAD at the
  integration tip; prepared with `winbox-project/scripts/winbox-wt-prep.sh`
  — payload pins verified against the shared store
  (runtime-assets-20261107-batch11), starting state clean.
- Built fresh from the tip through the shared build script's lock, owner
  `deploy-batch13`; the full log is preserved at
  `wt-deploy-batch13/test-results/deploy-batch13-20261109/browser-build.log`
  and carries "Runtime assets verified (13 files)", "Browser export
  verified" exactly once, and "BUILD COMPLETE".
- Build identity (`dist/runtime/BUILD_INFO.json`): git_sha
  `47a45376b1fec70b6f2738e2f4807295e92b6a72`, git_dirty_files 0, wasm
  sha256 `62f6123e396ef5350d66746085a88de52db8e21d253d7d4c375bc8824cec2082`
  (5,737,538 bytes — byte-identical to batch 12, as expected), js sha256
  `5d4df3843148ee41cd64db196e3e670936ab58214ba77c86b4084aea050d4fae`,
  builder "deploy-batch13 (pid 1028497)". JS unit on the built dist: 354
  passed, 0 failed (`unit-on-dist.log`).

## Staging and swap

- Staged the fresh dist into `/home/ubuntu/kn0tzer/winbox-next` via a
  clean bootstrap only, with symlinks resolved (`cp -rL`); the staged
  tree was verified byte-identical to the fresh dist (`diff -r` clean),
  0 symlinks, exactly ONE support archive
  (`support-c4dbe35986a5...`, the same archive id as batches 11 and 12).
- Swap at 2026-11-09 06:29 UTC: the batch-12 tree became
  `winbox-prev-20261109-0629` (the current rollback); `winbox-next`
  became live `winbox`.
- Off-box HTTPS verification (from outside the box, after the swap):
  BUILD_INFO 200 (git_sha `47a45376b`, wasm `62f6123e...`, dirty 0,
  builder deploy-batch13); shell 200; runtime index 200; wasm range
  request 206; support-archive range request 206; served
  `wine64-launcher.js` sha256 matches the live tree and differs from
  the rollback.
- After the live checks were verified, the superseded batch-11 rollback
  `/home/ubuntu/kn0tzer/winbox-prev-20261108-1722` (984M, build
  `a2f963898`) was removed. The ONLY rollback now is
  `/home/ubuntu/kn0tzer/winbox-prev-20261109-0629` (batch 12,
  `40a5dd699`).

## Live acceptance

All runs served from https://kn0tzer.work.gd/winbox, through the
probe-slot system, one at a time. Full logs under
`wt-deploy-batch13/test-results/deploy-batch13-20261109/` (drivers
`live-driver.mjs`, `live-gamepad.mjs`, `run-certs.sh`,
`run-consumer.sh`, `run-part2.sh`).

- 32-bit certification 5/5: every boot certified first try, handle
  `0x10052`, 0 restarts, 0 false-ready; median ready 19,603 ms
  (`cert-x86/summary.json`).
- 64-bit certification 3/3: every boot certified first try, handle
  `0x10048`, 0 restarts, 0 rescues; median ready 71,079 ms
  (`cert-x64/summary.json`).
- Census: the 7 preserved certification consoles (5 x86, 2 x64) each
  logged exactly 5 BW64-INFO served-link lines and exactly 1 BW64-FAIL —
  the standing `netlink-recvfrom flags=0x22` EINVAL line. The driver's
  results file certifies x64 boot 3 (handle `0x10048`, ready 71,079 ms)
  but its console file was not preserved, so the census covers 7 of 8
  boots. No start fault: the FD-115 tally is unchanged.
- Consumer series (baseline terms, no URL parameters): 6/6 mapped —
  b1 7 frames / 45.759 s, b2 8 / 32.415 s, b3 8 / 31.647 s, b4 7 /
  33.121 s, b5 10 / 33.338 s, b6 8 / 31.359 s (click-to-mapped); 0 page
  errors in every run; zero guest-memfault lines in all six preserved
  consoles. The fault loop behind FD-266 did not fire this deploy, so
  the fix's live behaviour rests on its lane proof (pipeline 66 +
  the lane's live intercept runs); the shipped launcher carrying the
  fix is verified live above.
- Supporting programs: PASS live (1 passed, 0 failed;
  `common-dependencies`).
- Controller: PASS live 2/2 after adjudication re-runs — see below.

## Adjudication — controller x64 first-match matcher race (twice)

- First attempt: x86 PASS (all 10 phases); x64 FAIL at triggers — the
  driver's trigger-wait matcher takes the first STATE line matching
  `lt=255`, which read `rt=0`; the preserved x64 console's final STATE
  line was `lt=255 rt=127`, exactly correct. Same harness race as the
  batch-12 deploy. Adjudication re-run 1: x86 PASS; x64 FAIL at
  stick-y-down — the matcher again took an earlier line, and the
  preserved console (kept as `gamepad-x64-console-rerun1.log`) contains
  the full-scale `ly=-32767` STATE lines the phase required.
- Adjudication re-run 2: 2/2 PASS, both widths, all 10 phases
  (boot-ready, ready, xinput-connected, caps, resting-state,
  dinput-enum, button-a, stick, triggers, dpad)
  (`gamepad-live-rerun2.log`). The guest-reported controller values
  were correct in every attempt; the failures are the deploy driver's
  first-matching-line pattern racing the guest's separate prints, not
  a product defect. The first attempt's results stand in
  `gamepad-live.log`; the matcher race itself remains a harness
  property, as recorded at batch 12.

## Delivery note — one garbled progress summary discarded

Mid-deploy, a background delivery summary reported consumer runs b3/b4
with timestamps ~10 minutes in the future and a b3 failure that never
happened. It was discarded: every figure in this record was re-read
from the preserved logs on disk (`live-checks.log`, each run's own
result line, the certification summaries), and the consumer series
stands 6/6 mapped on those reads.

## Push

- Remote master before the push: `6416a2adc01769a7b7dfebdcfa84f02a5c9f1e65`
  (batch 12). The held-back commit `69c800476` (edits
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
