# Deploy record — batch 12 — 2026-11-08

Status: batch 12 is LIVE at https://kn0tzer.work.gd/winbox/ and, with the
commit carrying this record, PUSHED to origin master. The swap happened at
2026-11-08 17:22 UTC. The live acceptance set passed with one incident,
recorded at full strength below: the consumer browser program hit the guest
fault loop three times, and the new containment fix stopped each loop cold —
but the session then sat silent instead of recovering, which is filed as
FD-266. The consumer series stands at 5/6 mapped on baseline terms. x86
certification 5/5, x64 certification 3/3, the supporting-programs check PASS
live, and the controller test PASS live on both architectures after one
adjudication re-run.

## What was deployed

- Integration tip `40a5dd699a50406d6c03374f7eeadab802496804` (branch
  `integ/winbox-20261022b`): the batch-12 merge — 3 lanes, 8
  cherry-picks — per `docs/reports/merge-batch12-20261108.md`. Headlines:
  the fault-loop fix and its containment (FD-204/FD-205, closed CL-188/
  CL-189); the multisample-texture catch fix (FD-207, closed CL-190); the
  Direct3D 10 stack-overflow fix (FD-263, closed CL-191, 64-bit thread
  stacks raised to 512 KiB). The register runs to FD-265, closures to
  CL-191. Gate: checking-service pipeline 65 standard (citations 0 drift;
  JS unit 352 passed / 2 known bare-checkout artifacts; native 706/0 +
  121/0); browser guest suite 52 passed / 3 failed, all adjudicated — the
  Direct3D 10 comprehensive test PASSED (202,173 ms), the controller test
  PASSED (177,004 ms); the failures are the two already-open defects
  FD-264 (64-bit boot smoke) and FD-265 (guest-DNS witness path) plus the
  jitbench benchmark-completeness gap.
- This batch changes product code only: the deploy build's wasm is NEW
  (`62f6123e…`, 5,737,538 bytes) against batch 11's `e44fe64e…`
  (5,736,471 bytes). The runtime file set is UNCHANGED from batch 11
  (runtime-assets-20261107-batch11) — no wine archive, no guest prefix was
  re-cut or re-pinned. Verified after the swap: the live tree differs from
  the batch-11 rollback in exactly 5 files (`app.mjs`,
  `runtime/BUILD_INFO.json`, `runtime/boxedwine64.js`,
  `runtime/boxedwine64.wasm`, `runtime/index.html`); the four runtime
  archives are byte-identical to the rollback (wine64 `13f254e3…`, wine32
  `8133274c…`, prefix64 `3ea7f63f…`, prefix32 `8defaa93…`).

## Build

- Fresh deploy worktree `wt-deploy-batch12`, detached HEAD at the
  integration tip; prepared with `winbox-project/scripts/winbox-wt-prep.sh`
  — payload pins verified against the shared store
  (runtime-assets-20261107-batch11), starting state clean. No build seed
  exists for this commit (the store's only seed is `48d27952a`); none was
  needed (warm compiler cache).
- Built fresh from the tip through the shared build script's lock, owner
  `deploy-batch12`; the full log is preserved at
  `wt-deploy-batch12/test-results/deploy-batch12-20261108/browser-build.log`
  and carries "Runtime assets verified (13 files)", the completeness result
  (0 failures, 1 standing skip — wine-coherence python-missing, the same
  standing skip as every gate since FD-53), "Browser export verified"
  exactly once, and "BUILD COMPLETE".
- Build identity (`dist/runtime/BUILD_INFO.json`): git_sha
  `40a5dd699a50406d6c03374f7eeadab802496804`, git_dirty_files 0, wasm sha256
  `62f6123e396ef5350d66746085a88de52db8e21d253d7d4c375bc8824cec2082`
  (5,737,538 bytes — NEW, as the merge gate found), js sha256
  `5d4df3843148ee41cd64db196e3e670936ab58214ba77c86b4084aea050d4fae`,
  builder "deploy-batch12 (pid 338361)". JS unit on the built dist: 354
  passed, 0 failed (`unit-on-dist.log`).

## Staging and swap

- Staged the fresh dist into `/home/ubuntu/kn0tzer/winbox-next` via a clean
  bootstrap only, with symlinks resolved (`cp -rL`); the staged tree was
  verified against the fresh dist, 0 symlinks, exactly ONE support archive
  (`support-c4dbe35986a5…`, the same archive id as batch 11).
- Pre-swap diff of the fresh dist against the live tree: only the 5 files
  listed above differ — the wine archives and prefixes do not differ,
  consistent with the unchanged runtime set; file sets identical modulo the
  4 symlinks under `vendor/slang` (live has real files; `diff -r` on vendor
  = IDENTICAL).
- Swap at 2026-11-08 17:22 UTC: the batch-11 tree became
  `winbox-prev-20261108-1722` (the current rollback); `winbox-next` became
  live `winbox`.
- Off-box HTTPS verification (from outside the box, after the swap):
  BUILD_INFO 200 (git_sha `40a5dd699`, wasm `62f6123e…`, dirty 0, builder
  deploy-batch12); shell 200; runtime index 200; wasm range request 206;
  support-archive range request 206.

## Live acceptance

All runs served from https://kn0tzer.work.gd/winbox, through the probe-slot
system, one at a time. Full logs under
`wt-deploy-batch12/test-results/deploy-batch12-20261108/` (drivers
`live-driver.mjs`, `live-gamepad.mjs`, `run-live-checks.sh`,
`run-consumer-bseries.sh`).

- 32-bit certification 5/5: every boot certified first try, handle
  `0x10052`, 0 restarts, 0 false-ready; ready times 19.8 / 19.8 / 19.8 /
  21.0 / 18.8 s, median 19,820 ms (`cert-x86.log`).
- 64-bit certification 3/3: every boot certified first try, handle
  `0x10048`, 0 restarts, 0 rescues; ready times 139.9 / 145.5 / 119.6 s,
  median 139,932 ms (`cert-x64.log`).
- Census: all 8 certification boots logged exactly 5 BW64-INFO served-link
  lines and exactly 1 BW64-FAIL — the standing `netlink-recvfrom
  flags=0x22` EINVAL line (FD-195's layer). No start fault: the FD-115
  tally stays at 3 in 60.
- Supporting programs: PASS live (1 passed, 0 failed;
  `common-dependencies`).
- Controller: first attempt x86 PASS (all 10 phases), x64 FAIL at triggers
  — a HARNESS race, not a product defect (see below); adjudication re-run
  2/2 PASS, both widths, all 10 phases.

## Incident — the fault loop fires three times; containment works, recovery does not (FD-266)

Recorded at full strength; the follow-up is filed as FD-266 in the register.

- The consumer series was run twice. The first two runs (a1, a2) used
  stricter session parameters than the batch-10/11 baseline
  (`BW_TEST_URL_PARAMS='session=1&persist=0&fresh=1&bwretry=2'` — this
  pre-exhausts the launcher's restart budget, so those runs are NOT on
  baseline terms and stand as supplementary evidence only). Both did NOT
  map: the identical guest fault loop as batch 11's run a4
  (guest-memfault32, addr `0xfffffd08`, eip `0x10584384`, 65 memfault lines
  each). A third strict run (a3) was stopped ~5 min in (partial evidence
  only) once the parameter error was spotted, and the series was re-run on
  baseline terms.
- Baseline b-series (no URL parameters — the batch-10/11 deploy terms): b1
  mapped (5 frames, 33.853 s), b2 mapped (11 frames, 39.323 s), b3
  NOT-MAPPED, b4 mapped (4 frames, 32.282 s), b5 mapped (9 frames,
  33.678 s), b6 mapped (9 frames, 36.676 s). Consumer stands at 5/6 mapped
  on baseline terms. All five mapped runs show 0 page errors.
- b3 is the incident run: the same fault loop (pid=78 tid=81 — the SAME
  pid/tid as batch 11's a4 — 65 memfault lines), and the FD-205 containment
  PROVED itself live for the third time in this deploy: delivery refused at
  count 130 (`delivered=0`), zero page errors (batch 11's a4 died on a
  worker RangeError; this time there were 0 RangeErrors). But the session
  then died silently: the preserved console carries zero `restart` lines,
  its last entries are the lazy-fetch lines at ~23 s, and the run timed out
  at 900 s with a nearly-dark screenshot and 0 frames — even though the
  restart budget was intact on baseline terms. The containment stops the
  loop; nothing restarts or reports the dead session afterwards. That is
  FD-266 (Medium, filed by this lane).
- The fault fired 3 times in 8 consumer runs this deploy (a1, a2, b3) with
  the identical signature. It is a running-session fault loop, not a start
  fault, so it does not move the FD-115 tally. The underlying loop's
  trigger remains the open question behind FD-205's containment; the loop
  itself is contained, the session's silence after containment is FD-266.

## Adjudication — controller first-attempt x64 failure

- The first live controller attempt failed x64 at the triggers phase: the
  driver's trigger-wait matcher takes the first STATE line matching
  `lt=255`, which read `rt=0` — the two triggers update in separate
  prints. The preserved x64 console's final STATE line is `lt=255 rt=127`,
  exactly correct. Harness race, not a product defect. Re-run: 2/2 PASS
  (x86 + x64, all 10 phases: boot-ready, ready, xinput-connected, caps,
  resting-state, dinput-enum, button-a, stick, triggers, dpad).

## Disk incident

- Free space during the deploy: the build dipped to 1.4G; staging to 458M;
  another tenant's Minecraft-server backup
  (`/home/ubuntu/backups/minecraft-fastback/`, actively writing at the time
  — NOT touched) drained it to 117M. The deploy lane stubbed out its own
  `dist/` (960M), keeping ONLY `dist/runtime/BUILD_INFO.json` — the build
  identity is also in the live tree, the build log, and this record — and
  disk recovered to 1.1G free and held. The stub stays in place until the
  deploy tree is stripped (the build is reproducible from the tip; the live
  tree carries the shipped bytes).
- After the live checks were verified, the superseded batch-10 rollback
  `/home/ubuntu/kn0tzer/winbox-prev-20261108-0549` (982M, build `5b12ec267`)
  was removed. Disk 1.1G → 2.1G free. The ONLY rollback now is
  `/home/ubuntu/kn0tzer/winbox-prev-20261108-1722` (batch 11, `a2f963898`).

## Push

- Remote master before the push: `b5ab2c21a44facb645051d82edc71911366bcd48`
  (batch 11). The held-back commit `69c800476` (edits
  `.github/workflows/deploy-pages.yml`; the box credential lacks workflow
  scope) was verified NOT an ancestor before the push and is verified NOT
  an ancestor after it (see the push verification in the coordinator's
  report; the read-back below is the authority).
- Procedure (standing): merge the integration tip into the current remote
  master via the temp-worktree procedure — a `--no-ff` merge in a detached
  worktree at the remote tip, pushing `HEAD:master` to origin
  (github.com/Kn0tzer/Winbox) — verified by reading the remote back with
  `git ls-remote`. Local branches/history untouched; the temp worktree
  removed afterwards.
