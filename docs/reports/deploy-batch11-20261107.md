# Deploy record — batch 11 — 2026-11-08

Status: batch 11 is LIVE at https://kn0tzer.work.gd/winbox/ and, with the
commit carrying this record, PUSHED to origin master. The swap happened at
2026-11-08 05:49 UTC. The live acceptance set passed with one incident,
recorded at full strength below: consumer Chromium run a4 never showed its
window — a guest fault loop during a load spike; the adjudication re-run a7
mapped cleanly with 8 frames and 0 page errors. The consumer series stands
at 6/7 mapped for this deploy. x86 certification 5/5, x64 certification
3/3, the supporting-programs check PASS live, and the controller test —
this batch's headline — PASS live on both architectures.

## What was deployed

- Integration tip `a2f963898dc901fbc7de48eb628c4c6b60c1cf2e` (branch
  `integ/winbox-20261022b`): the batch-11 merge — 24 lanes, 97
  cherry-picks — per `docs/reports/merge-batch11-20261107.md`. Headlines:
  FD-195 controller visibility CLOSED (a program in the emulator can see
  and use a host game controller on both architectures); FD-201 start
  fault FIXED (the batch-10 dead-start mechanism — an unlocked
  process-table scan across a rehash); FD-194 PlugPlay timeout; FD-169
  remainder (exactly 100 stub slots genuinely implemented, msvcp140_2
  complete); FD-168 slices 2+3 (9 more win32u implementations, remaining
  count 2,014); FD-193 BLOCKSTATS; the X11 getRamPtr audit (12 unsafe
  call sites); the CPU differential (REP LODS fix, 7,920/7,920 cases);
  D2/D7/D8; D-items; FD-26 speed (two more JIT fast-path families);
  timing/wait contracts (19 fixed, 11 filed open); four unfiled gaps;
  guest-DNS test-only proof; FD-03 negative pinned; FD-28 generator;
  FD-39 residue; the audio check strengthening plus the FD-202 reversal;
  D22 Mindustry JVM ergonomics; the cold-start x64 record; the Vulkan/GL
  coverage runs (FD-204..209 filed); the probe-parallelism finding
  (build-pending starvation). The register runs to FD-265, closures to
  CL-187. Gate filings FD-263 (d3d10 unaligned trap, High, a regression
  against batch 10), FD-264 (boot64-smoke), FD-265 (winsock-dns witness
  path).
- This batch changes product code: the deploy build's wasm is NEW
  (`e44fe64e…`, 5,736,471 bytes) against batch 10's `92b36180…`
  (5,722,692 bytes).

## Build

- Fresh deploy worktree `wt-deploy-batch11`, detached HEAD at the
  integration tip; prepared with `winbox-project/scripts/winbox-wt-prep.sh`
  — 23 links created, payload pins verified against the shared store
  (runtime-assets-20261107-batch11), starting state clean. No build seed
  exists for this commit (the store's only seed is `48d27952a`); none was
  needed.
- Built fresh from the tip through the shared build script's lock, owner
  `deploy-batch11`; the full log is preserved at
  `wt-deploy-batch11/test-results/deploy-batch11-20261108/browser-build.log`
  and carries "Runtime assets verified (13 files)", the completeness result
  (0 failures, 1 standing skip — wine-coherence python-missing, the same
  standing skip as every gate since FD-53), "Browser export verified"
  exactly once, and "BUILD COMPLETE".
- Build identity (`dist/runtime/BUILD_INFO.json`): git_sha `a2f963898`,
  git_dirty_files 0, wasm sha256
  `e44fe64e4334fe0a08cbb2e61388b539394cbf48a34235e27a26a6585e0a0f1b`
  (5,736,471 bytes — NEW, as the merge gate found), js sha256
  `896f55af674a0fc1bc21a0b827916d7983b862d3df74c2e2038595287164b1eb`,
  builder "deploy-batch11 (pid 3835449)".
- The wine archives are byte-identical to the committed batch-11 pins
  (wine64 `13f254e3…`, wine32 `8133274c…`).
- JS unit suite against the built dist: **354 passed / 0 failed** (the two
  standing bare-checkout artifacts pass on a built tree, as at the gate).
  Log: `test-results/deploy-batch11-20261108/unit-on-dist.log`.

## Staging and swap

- Pre-swap diff of the fresh dist against the live tree: file sets
  IDENTICAL; the vendor tree IDENTICAL; exactly 11 files differ —
  `app.mjs`, `gl-webgpu.mjs`, `runtime/BUILD_INFO.json`,
  `runtime/boxedwine64.js`, `runtime/boxedwine64.wasm`,
  `runtime/desktop-pin-provenance.json`, `runtime/index.html`,
  `runtime/prefix64.zip`, `runtime/wine32-font-provenance.json`,
  `runtime/wine32.zip`, `runtime/wine64.zip`. (`prefix32.zip` does NOT
  differ — unchanged pin, same shipped hash as batch 10.)
  - The shipped prefix zips differ from the committed pins
    (dist prefix32 `8defaa93…` / prefix64 `3ea7f63f…` vs pins
    `68164e56…` / `c344364a…`) because the export step rewrites them.
    The shipped hashes are the ones recorded in the build's own
    `desktop-pin-provenance.json`. This is the standing shape, verified
    during this deploy; it is not a defect.
- Staged into `/home/ubuntu/kn0tzer/winbox-next` through a clean
  bootstrap only (`cp -rL dist /home/ubuntu/kn0tzer/winbox-next`; dist
  had 4 symlinks, all under `vendor/slang`, resolved by the copy — note:
  `winbox-setup.sh`, named in the batch-10 record, does not exist on
  disk): verified byte-identical to the dist (`diff -r` clean),
  **0 symlinks**, and exactly ONE support archive staged
  (`support-c4dbe35986a5edcff6646cb8b4f63cc7660e6e1e6dcda6546454db77d241729f.zip`,
  517,564,189 B). Completeness is a hard gate — a missing file falls
  through to the BB gateway login page — and it was fully green before
  the swap.
- Rename swap at 2026-11-08 05:49 UTC: the batch-10 tree became
  `winbox-prev-20261108-0549` (the current rollback) and `winbox-next`
  became the live `winbox`. After the live checks completed and were
  adjudicated, the previous rollback `winbox-prev-20261107-1317`
  (batch 9, `d50bcc02f`, 982M) was removed — newest rollback only.
- Off-box HTTPS verification immediately after the swap:
  `https://kn0tzer.work.gd/winbox/runtime/BUILD_INFO.json` serves git_sha
  `a2f963898`, wasm `e44fe64e…`, git_dirty_files 0, builder
  deploy-batch11; the shell returns 200, the runtime index 200, a wasm
  range request 206, and a support-archive range request 206.

## Live acceptance

The standing scripted set, run against the live origin with brand-new
browser profiles, serial through the probe-slot system. The driver and
parameters are the batch-10 deploy's (`live-driver.mjs`: `--budget-ms
900000`, `bwretry=2` in the session URL so the launcher's restart budget
is pre-exhausted and no start is a rescue). Evidence:
`wt-deploy-batch11/test-results/deploy-batch11-20261108/`
(`live-checks.log`, `cert-x86/`, `cert-x64/`, `chromium-consumer-a<N>/`,
`common-deps-live.log`, `gamepad-live*.log`,
`gamepad-live-results.jsonl`).

### x86 certification — 5/5

- 5/5 certified, median 18,698 ms, hwnd `0x10052` every boot (the
  genuine handle), 0 restarts, 0 false-ready, 0 memfaults. Clean — no
  FD-201-style fault.

### Census

- Every certified boot on both widths (8 of 8): exactly **5 BW64-INFO**
  served-link lines (the FD-81 census term, unchanged) and exactly
  **1 BW64-FAIL** line.
- The one FAIL line is the standing batch-10 census shape:
  `BW64-FAIL netlink-recvfrom flags=0x22 outcome=returned-EINVAL`, once
  per session — the libudev monitor receive inside open FD-195. It
  appears identically in certified boots.

### x64 certification — 3/3

- 3/3 certified, median 118,274 ms, hwnd `0x10048` every boot, 0
  restarts / 0 rescues, census as above, 0 memfaults.

### Consumer series — Chromium 6/7 mapped (incident recorded)

- Six runs: a1 mapped / 6 frames / 0 page errors; a2 mapped / 9 / 0;
  a3 mapped / 10 / 0; **a4 NOT-MAPPED**; a5 mapped / 6 / 0; a6 mapped /
  10 / 0. All six exited 0. Click-to-mapped 31.8–32.9 s for the mapped
  runs.
- a4 (06:21–06:36 UTC, ~15 min against ~7 min for the others) started
  at load 13.82 (the load at a3's exit): its guest Chromium session hit
  a guest-memfault32 fault loop — pid=78 tid=81, addr `0xfffffd08` /
  eip `0x10584384`, delivered repeatedly (64 memfault lines), plus a
  worker `RangeError: Maximum call stack size exceeded` page error; the
  window never mapped (tMapped null, timeout at 903.7 s). The other five
  runs have **0** guest-memfault32 lines; the cert boots have none. This
  fault-loop shape is the FD-205 class (unbounded fault redelivery,
  filed OPEN from the coverage runs). Full console preserved at
  `chromium-consumer-a4/`.
- Adjudication re-run a7 (07:08–07:15 UTC): **MAPPED at 34.989 s, 8
  frames presented, 0 page errors, 0 guest-memfault32 lines.**
- Adjudication: a4's failure is not a persistent property of this
  build — the re-run mapped cleanly on the same live build, and the
  cert series (8/8) plus the other five consumer runs are clean. It is
  counted, not waved away: the consumer series stands at **6/7 mapped**
  for this deploy. The fault loop is the known open FD-205 shape, now
  seen live for the first time; the FD-204/FD-205 fix lanes are the
  follow-up.

### Supporting programs — PASS live

- The common-dependencies check run against the live tree
  (`WINE_DIST_DIR=/home/ubuntu/kn0tzer/winbox`): **PASS** — "SUITE
  RESULT: 1 passed, 0 failed" (`common-deps-live.log`, 06:50–06:54 UTC).

### Controller — PASS live 2/2 (the batch's headline)

- New live driver `live-gamepad.mjs` (modeled on the suite's
  `input-gamepad` test, same phases and patterns, same simulated Xbox
  360 pad via a stubbed Gamepad API, loading the PUBLIC runtime URL).
- First attempt FAILED on x86 at the witness-READY wait (180 s) — the
  guest was alive (step-ring lines) but the witness never printed.
  Cause was the DEPLOY LANE'S OWN DRIVER BUG, proven from the file
  bytes: the spawn command string had doubled backslashes (`\\\\n` /
  `\\\\\\\\` where the suite's `probe-base.mjs` has `\\n` / `\\\\`), so
  the guest never launched the witness (spawn still returned > 0). The
  failed attempt's evidence is preserved
  (`gamepad-live-x86-attempt1-driver-bug/`,
  `gamepad-live-results-attempt1.jsonl`). The driver was fixed by exact
  byte replacement (verified with `node --check` and a string comparison
  against probe-base), the driver process killed by explicit PID, and
  the fixed driver re-run.
- Re-run: **x86 PASS** phases [boot-ready, ready, xinput-connected,
  caps, resting-state, dinput-enum, button-a, stick, triggers, dpad];
  **x64 PASS** with the same 10 phases. SUMMARY gamepad-live 2/2 arches
  passed (`gamepad-live-rerun.log`, `gamepad-live-results.jsonl`).
  Witness lines read from the preserved consoles: x86 `DINPUT devices=1
  name0=Controller (XBOX 360 For Windows)`, `READY xinput=1 dinput=1`,
  `XINPUT slot=0 CONNECTED`; x64 the same (pid 78 wine64). Census:
  exactly **1 BW64-FAIL** line per gamepad session, and it is the
  standing `netlink-recvfrom flags=0x22` EINVAL line.

## Push

- Per the standing directive, the integration tip — including this
  record's commit — is merged into the current remote master through the
  established temp-worktree procedure on the box: a detached worktree at
  the remote tip (`763d695860a7f9b6a2038872d1f60aea25b1d167`, read by
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

- Live: batch 11, git_sha `a2f963898`; rollback:
  `/home/ubuntu/kn0tzer/winbox-prev-20261108-0549` (batch 10, `5b12ec267`).
- FD-115 first-visit start tally: **3 in 60** (unchanged — this deploy's
  live checks had no start fault; the a4 consumer fault loop is the
  FD-205 class, counted in the consumer tally above, not a start fault).
- Follow-ups: (1) FD-204/FD-205 fault containment — the a4 live fault
  loop is the first live sighting of the FD-205 shape; the fix lanes are
  queued. (2) The gate's open filings FD-263 (d3d10 unaligned trap,
  High, a regression against batch 10), FD-264 (boot64-smoke), FD-265
  (winsock-dns witness path) — none of the three manifested in this
  deploy's live checks. (3) The merge gate's jitbench gap stands as
  recorded in `docs/reports/merge-batch11-20261107.md`.
