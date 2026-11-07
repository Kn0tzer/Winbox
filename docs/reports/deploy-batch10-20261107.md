# Deploy record — batch 10 — 2026-11-07

Status: batch 10 is LIVE at https://kn0tzer.work.gd/winbox/ and, with the
commit carrying this record, PUSHED to origin master. The swap happened at
2026-11-07 13:17 UTC. The live acceptance set passed with one incident,
recorded at full strength below: the first x86 certification series lost two
of five boots to a delivered wasm memfault at the guest process-spawn
transition; the adjudication re-run series certified 5/5 clean on the same
live build. x86 stands at 8/10 first-try for this deploy, x64 at 3/3, the
consumer series at 6/6, the supporting-programs check PASS live.

## What was deployed

- Integration tip `5b12ec2670863b1104dc3c155c6cc62fab9ae83b` (branch
  `integ/winbox-20261022b`): the batch-10 merge — the FD-01 cold-start closure
  proof (CL-136), the FD-176 JIT block-invalidation differential gate made
  honest (CL-134), the FD-189 shipped-prefix PlugPlay sections (CL-135),
  FD-188's driver-load fix (CL-137), the FD-196 prefix-fixture repair
  (CL-138), the FD-198 register-checker allowance (CL-139), and the FD-199
  archive link repair (CL-140) — per
  `docs/reports/merge-batch10-20261107.md`. The FD-194 fix is NOT in this
  batch (it landed after composition on its own branch).
- The batch changes no product code: the deploy build's wasm is
  byte-identical to the previously live batch-9 build (below).

## Build

- Fresh deploy worktree `wt-deploy-batch10`, detached HEAD at the
  integration tip; prepared with `winbox-project/scripts/winbox-wt-prep.sh`
  — 23 links created, payload pins verified against the shared store
  (runtime-assets-20261107), starting state clean. No build seed exists for
  this commit (the store's only seed is `48d27952a`); none was needed.
- Built fresh from the tip through the shared build script's lock, owner
  `deploy-batch10`; the full log is preserved at
  `wt-deploy-batch10/test-results/deploy-batch10-20261107/browser-build.log`
  and carries "Runtime assets verified (13 files)", the completeness result
  (0 failures, 1 standing skip — wine-coherence python-missing, the same
  standing skip as every gate since FD-53, including the FD-188
  drivers-mirror PASS, the FD-189 PlugPlay-section PASS, and the FD-73
  retained-archive PASS), "Browser export verified: 34 .mjs syntax files;
  dependency archive c4dbe35986a5 verified", and "BUILD COMPLETE".
- Build identity (`dist/runtime/BUILD_INFO.json`): git_sha `5b12ec267`,
  git_dirty_files 0, wasm_engines sha256
  `92b36180e9b3469b07014b91cf0054e4f50e1b119b2881e6ca1a85819d27bfdf`
  (5,722,692 bytes — byte-identical to the live batch-9 build, as the merge
  gate found), js sha256 `94669195...` (unchanged from batch 9), builder
  "deploy-batch10 (pid 2312477)".
- JS unit suite against the built dist: **354 passed / 0 failed** (the two
  standing bare-checkout artifacts pass on a built tree, as at the gate).
  Log: `test-results/deploy-batch10-20261107/unit-on-dist.log`.

## Staging and swap

- Pre-swap diff of the fresh dist against the live tree: file sets
  IDENTICAL; the vendor tree IDENTICAL; exactly 7 files differ —
  `runtime/BUILD_INFO.json`, `runtime/desktop-pin-provenance.json`,
  `runtime/prefix32.zip`, `runtime/prefix64.zip`,
  `runtime/wine32-font-provenance.json`, `runtime/wine32.zip`,
  `runtime/wine64.zip`.
  - The wine archives are byte-identical to the committed batch-10 pins
    (wine64 `133bfd70...`, wine32 `aa2bb9d6...`).
  - The shipped prefix zips differ from the committed pins
    (dist prefix32 `8defaa93...` / prefix64 `5b5e9acd...` vs pins
    `68164e56...` / `19725124...`) because the export step rewrites them
    (build log: "Removed 1 winemenubuilder RunServices value(s) from
    prefix32.zip" — the gate build's log carries the identical line, and
    batch 9's live prefixes differ from batch 9's pins the same way). The
    shipped hashes are the ones recorded in the build's own
    `desktop-pin-provenance.json`. This is the standing shape, verified
    three ways during this deploy; it is not a defect.
- Staged into `/home/ubuntu/kn0tzer/winbox-next` through the clean
  bootstrap only (`winbox-setup.sh stage` — the checkout's dist is the
  only payload input; symlinks resolved with `cp -rL`, because dist
  payload files may be symlinks into the shared store): verified
  byte-identical to the dist (`diff -r` clean), **0 symlinks**, and
  exactly ONE support archive staged
  (`support-c4dbe35986a5edcff6646cb8b4f63cc7660e6e1e6dcda6546454db77d241729f.zip`).
  Completeness is a hard gate — a missing file falls through to the BB
  gateway login page — and it was fully green before the swap.
- Rename swap at 2026-11-07 13:17 UTC: the batch-9 tree became
  `winbox-prev-20261107-1317` (the current rollback) and `winbox-next`
  became the live `winbox`. After the live checks completed, the previous
  rollback `winbox-prev-20261106-2237` (batch 8) was removed — newest
  rollback only.
- Off-box HTTPS verification immediately after the swap:
  `https://kn0tzer.work.gd/winbox/runtime/BUILD_INFO.json` serves git_sha
  `5b12ec267`, wasm `92b36180...`, git_dirty_files 0, builder
  deploy-batch10; the shell returns 200, the runtime index 200, a wasm
  range request 206, and a support-archive range request 206.

## Live acceptance

The standing scripted set, run against the live origin with brand-new
browser profiles, serial through the probe-slot system. The driver and
parameters are the batch-9 deploy's (`live-driver.mjs`: `--budget-ms
900000`, `bwretry=2` in the session URL so the launcher's restart budget
is pre-exhausted and no start is a rescue). Evidence:
`wt-deploy-batch10/test-results/deploy-batch10-20261107/`
(`live-checks.log`, `cert-x86/`, `cert-x64/`, `chromium-consumer-a<N>/`,
`common-deps-live.log`, and the re-run's `cert-x86-rerun/`).

### x86 certification — first series 3/5, re-run 5/5 (incident recorded)

- First series: boots 1–3 certified in 17.6 / 17.5 / 17.7 s
  (hwnd `0x10056`, plausible under the hardened FD-47 rule: nonzero, top
  bit clear — the value differs from batch 9's `0x10052`, as handle values
  vary per runtime vintage). Boots 4 and 5 FAILED: each suffered a
  delivered wasm `RuntimeError: memory access out of bounds` in a session
  worker at console lines ~868–873, at the wine-preloader spawn transition
  (immediately after the pid-12 socketpair wiring, where clean boots log
  `setupThreadStack32`; identical JS glue offsets 15105 / 6378 in both).
  The session then stalled — the launcher's stallwatch thread dumps failed
  to unwind — deskpin never reported READY, and each boot exhausted its
  900 s budget with 0 restarts. Full consoles preserved at
  `cert-x86/boot-4/console.log` and `cert-x86/boot-5/console.log`.
- Adjudication re-run (same driver, same live build, ~80 minutes later,
  box load ~4.5 — higher than during the fault window): **5/5 certified**,
  median 17,786 ms, hwnd `0x10056`, 0 restarts, 0 memfaults, uniform
  census. Combined live x86 for this deploy: **8/10 first-try certified**.
- Adjudication: the fault site is deterministic but its incidence is not
  a persistent property of this build — a clean 5/5 series at higher load,
  x64 3/3 and the consumer's six x64 sessions clean in the same hours. A
  delivered guest memfault during a start has one prior recorded instance
  on the previously shipped build (batch-4 deploy, consumer run a4,
  unattributed). The two faulted boots are counted, not waved away: the
  FD-115 first-visit start tally moves from **1 in 50 to 3 in 60** (both
  faulted boots never completed a start). The cause of the spawn-transition
  memfault is NOT identified here; it is the first follow-up below.

### Census

- Every certified boot on both widths (11 of 11 across the two x86 series
  and the x64 series): exactly **5 BW64-INFO** served-link lines (the
  FD-81 census term, unchanged) and exactly **1 BW64-FAIL** line.
- The one FAIL line is a batch-10 census CHANGE, explained rather than a
  regression: `BW64-FAIL netlink-recvfrom flags=0x22
  outcome=returned-EINVAL`, once per session, where batch 9 logged zero
  BW64-FAIL. Batch 10's FD-188/189 changes put the bus/service layer into
  the boot path; the line is the libudev monitor receive already flagged
  in open FD-195 (bus device discovery). It appears identically in
  certified and faulted boots.

### x64 certification — 3/3

- 3/3 certified, median 108,320 ms, hwnd `0x10048` (the genuine handle,
  as batches 9/10-gate), 0 restarts / 0 rescues, census as above, 0
  memfaults. (Slower than batch 9's 88,095 ms median under load ~5–6; no
  verdict rests on duration.)

### Consumer series — Chromium 6/6 live

- Six runs of the consumer demo (DEMO=chromium against
  `https://kn0tzer.work.gd/winbox/`): 6/6 stage=mapped, exit 0, frames
  presented 6 / 6 / 8 / 8 / 8 / 9, click-to-mapped 31.5–32.9 s,
  pageErrors 0 in all six. (Batch-9 baseline frames: 12 / 15 / 8 / 11 /
  12 / 13; the counts are recorded as counted, no verdict attached.
  ~213 console errors per run is the guest Chromium's own noise, the same
  shape as batch 9's ~181.)

### Supporting programs — PASS live

- The common-dependencies check run against the live tree
  (`WINE_DIST_DIR=/home/ubuntu/kn0tzer/winbox`): **PASS** — "SUITE
  RESULT: 1 passed, 0 failed", exit 0 (`common-deps-live.log`).

## Push

- Per the standing directive, the integration tip — including this
  record's commit — is merged into the current remote master through the
  established temp-worktree procedure on the box: a detached worktree at
  the remote tip (`dd92d359d82b558e921a549b1bd04c333474ec77`, read by
  ls-remote immediately before), a `--no-ff` merge of the integration
  tip, `push HEAD:master` to origin (github.com/Kn0tzer/Winbox), and
  verification by reading the remote back. The local master tip
  `69c800476693c0fb9a981acb36ffb3c8e913c80a` (it edits
  `.github/workflows/deploy-pages.yml`; the box credential lacks the
  workflow scope) must never ride — it is excluded by construction (the
  merge's second parent is the integration tip, not local master) and
  verified not an ancestor of the pushed tip after the push. The
  post-push read-back values are carried in the deploy lane's completion
  report.

## State after this deploy

- Live: batch 10, git_sha `5b12ec267`; rollback:
  `/home/ubuntu/kn0tzer/winbox-prev-20261107-1317` (batch 9, `d50bcc02f`).
- FD-115 first-visit start tally: **3 wedged/faulted in 60**.
- Follow-ups: (1) investigate the wine-preloader spawn-transition
  memfault — two live instances preserved in this deploy's evidence
  (`cert-x86/boot-4`, `cert-x86/boot-5`); it is not filed as a register
  entry by this record — the register's owner should file it from the
  preserved consoles. (2) The merge gate's jitbench gap stands as recorded
  in `docs/reports/merge-batch10-20261107.md`.
