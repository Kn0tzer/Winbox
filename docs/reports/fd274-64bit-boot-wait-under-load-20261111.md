# FD-274: the 64-bit boot wait under load — desktop-wait budgets raised

Filed 2026-11-11 by the batch-17 composition lane. Register bullet (line 11363,
section A): "A fresh 64-bit boot takes ~403 s quiet and ~500-600 s under load
against the browser suite's 600 s boot wait, so the suite's 64-bit-boot tests
fail under load before the guest is ready." Closing terms: `win32u-fd168-slice4`
passing in a gate run under load, with the boot-latency cause named.

## What the defect was, measured firsthand

The first lane (`9a115a75`) ran a discovery boot against the live batch-17 build
with the boot wait raised to 1800 s. It failed at **1,815,534 ms** with the guest
alive and progressing the whole time (13,526 console lines). The timeline, read
from `test-results/suite-win32u-fd168-slice4/console.log`:

- the shell echo `vkboot` arrived at ~1329 s;
- at ~1454 s the guest reported `002c:err:winediag:nodrv_CreateWindow
  Application tried to create a window, but no driver could be loaded.`
  followed by `error: session: desktop initialization failed (boot attempt 1);
  scheduling a clean session restart` and `L"The explorer process failed to
  start."`

**Cause, named:** the desktop-wait budgets from the FD-01 fix — a 300 s
input-idle wait and a 600 x 500 ms driver-publication retry in the backports
patch — are exhausted when the desktop parade runs slower still under load
18-27, so the graphics driver is never published and the null driver latches.
A test-wait raise alone cannot close this: `ready()` never fires in attempt 1.
The boot itself is slow (FD-26 owns boot speed); this fix buys the desktop the
time it needs under load.

## The fix

Commit `422112fe6` ("FD-274: raise the FD-01 desktop-wait budgets and the suite
boot wait"), branch `lane-fd274`, base `5031e62ed`:

- `tools/buildWine/patches/wine8-win32u-desktop-wait.patch` — input-idle wait
  300,000 ms -> 1,800,000 ms; driver-publication retry 600 -> 3,600 attempts
  (1,800 s), comments updated.
- `web/tests/suite/lib/probe-base.mjs` — `bootGuest` wait 600 s -> 2,400 s.
- `web/tests/suite/browser/win32u-fd168-slice4.test.mjs` — `timeoutMs`
  900 s -> 3,000 s.

**Defect in the fix commit, found and repaired by the resume lane:** the first
lane added comment lines inside the patch file without updating its hunk
headers, so `git apply` died with `error: corrupt patch at line 136` on the
first build run. A hunk-count checker proved it: driver.c hunk 2 declared
`+944,81` but carries 87 new lines (+6 FD-274 comments); winstation.c declared
`+478,18` but carries 21 (+3 comments). Repaired in commit `6fad228d0`
("FD-274: repair the desktop-wait patch hunk counts"): headers corrected to
`+944,87` and `+478,21`. Verified by the checker (all three hunks OK) and by
`git apply --check` against the WORK tree with the first three patches applied.

## The build (resume lane)

Backports build 3 (`tools/rootfs64/build-wine8-backports.sh`, log
`test-results/fd274/backports-build3.log`): all 15 patches applied; the unix
tree stopped at its freetype probe exactly as the slice-2 notes predict
("configure: error: FreeType 64-bit development files not found"), was
configured manually with the script's exact configure line plus the wine8
sysroot env (`/home/ubuntu/winbox-project/cache/wine8-sysroot`, freetype
2.12.1 / fontconfig 2.14.1), then the script re-ran to finish. The FD-65
guard passed (win32u.so carries freetype + fontconfig).

Blast radius, verified from disk: the from-scratch rebuild changed all 45
outputs (expected drift); the other 43 were restored to their pinned bytes via
`git checkout`, leaving exactly the win32u pair:

- `win32u.dll` `00ae6cbf4598650148e4c2a90cf4d5360438c4b9443b1dec546f92a999b06ba1`
  (359,360 B; 7 bytes differ from the pinned build)
- `win32u.so` `9c8e78a38daf9715eb6ba42d352aab3c5d175a41122ae69fe8dfa2f4c0a71406`
  (1,493,256 B; 25 bytes differ)

Commit `533ff6727` ("FD-274: ship the raised desktop-wait win32u pair and
re-cut the retained archive"): re-pinned the pair in
`tools/rootfs64/check_runtime_completeness.py` and in the backports README
(desktop-wait row now reads 1800 s); restaged `wine64.zip` via
`tools/rootfs64/stage-wine8-backports.sh` — new hash
`4058885a458473ca2ec07967f4529b1078cca1049269b11d15ceb0017b4b3a6b`,
195,994,431 B, 1,281 members (backup in
`evidence/winbox-wine8-backports-20261111-174402`); re-cut the retained
archive with `tar -h` as `runtime-assets-20261111-fd274`
(`9a84cbc8299cef8609b7f648d6bcc926bfc42e46b656cce56cc2fa0514ae7069`,
493,568,000 B) and verified all 13 members against their pins; re-pinned
`tools/browser/runtime-assets.json` to the new release. Backups pruned to the
cap of 8 cuts (oldest non-anchor `winbox-runtime-assets-20261106-fd168s2`
removed; the batch-11 anchor kept).

Dist build 1 failed its export gate on the stale manifest, as in the slice-4
precedent (`check_runtime_completeness.py`: 1 failure — "manifest matches
shipped zips: drifted; regenerate it"). The manifest was regenerated with
`tools/rootfs64/gen_runtime_manifest.py`; the diff is exactly the one w64
line (195994432 `e1479bea...` -> 195994431 `4058885a...`, 1,281 members);
the check then reported 0 failures. Commit `5b52f1a0d`.

Dist build 2 (`test-results/fd274/dist-build2.log`): **"Browser export
verified"**, `[bw64-build] BUILD COMPLETE`. The wasm is byte-identical to
batch 16/17 (`bcd9d500a0a70c13e522dffb8d40c7f6a747d4752c43b3e2c6c0935a3f71e17b`,
5,756,971 B); js identical (`0312a6d4...`). BUILD_INFO: git_sha `5b52f1a0d`,
dirty 0.

## Proof: the suite test twice under load

Both runs used the lane's built dist (BUILD_INFO git_sha `5b52f1a0d`, wasm
`bcd9d500...`) and served the new `wine64.zip` (195,994,431 B). Read from each
run's own preserved console log:

- **Run 1** (`test-results/fd274/suite-run1.log`, console
  `test-results/fd274/console-run1.log`): `PASS win32u-fd168-slice4`
  (551,463 ms), `SUITE RESULT: 1 passed, 0 failed`. `RESULT 0`; all 21
  checks ok=1, stub=0, zero stub=1. `deskpin: READY hwnd=0x10048`. No
  "desktop initialization failed", no session restart, no null-driver error —
  the first boot attempt succeeded.
- **Run 2** (`test-results/fd274/suite-run2.log`, console
  `test-results/fd274/console-run2.log`): `PASS win32u-fd168-slice4`
  (569,429 ms), `SUITE RESULT: 1 passed, 0 failed`. `RESULT 0`; 21/21 ok=1,
  stub=0. First boot attempt succeeded (`deskpin: READY hwnd=0x10048`).

Box load during the runs: 5-9 (run 1 launched at load ~9-11). The discovery
failure that filed this defect was under load 18-27; the mechanism is the
same — the raised budgets let the first attempt complete where 300 s did not.

Negative control is NOT owed here: the slice-4 lane already proved the
fixture discriminates (20/21 on the pre-repair fixture, RESULT 18 on the old
runtime).

## Light checks (run on the built tree)

- Citations: 352 rows, 0 drift (MATCH 232, MATCH-INHERITED 14, UNCHECKABLE
  99, EXEMPT 7), exit 0.
- JS unit on the built tree: **354 passed, 0 failed**.
- JS legacy: 117 tests, 116 pass, 0 fail, 1 skipped.
- Log: `test-results/fd274/light-checks.log`.

## Queue gate

Pipeline **#96** on tip `5b52f1a0d`, read from the preserved step logs in
`checks/evidence/pipeline-96/queue-logs/` (steps 820-828) — the standing
shape:

- verify-source exact on `5b52f1a0d`; the source mount read-only, tree
  pristine.
- citations: 352 rows, 0 drift, exit 0.
- js-unit: **352 passed, 2 failed** — the two failures are exactly the known
  bare-checkout artifacts (`provenance-artifacts`, `runtime-completeness`,
  which need a built `dist/` / staged runtime assets). The pipeline's
  overall "failure" status is driven solely by these two.
- js-legacy: 0 fail, 1 skipped.
- native-main: 706 tests, 0 FAILED in 293 s; native-x64: 121 tests, 0 FAILED
  in 34 s.
- run-native: 0.

## Register

FD-274 closed 2026-11-11 (see the register entry). FD-168 stays open at
2,011 — this patch changes no export surface, so the census is unchanged.
