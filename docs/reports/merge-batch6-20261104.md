# Batch-6 merge record — 2026-11-04

Integration branch `integ/winbox-20261022b`, base tip `7a2308cd6` (the batch-5
deploy record). Four lane branches composed, the defect register settled by
audit, and the full gate run on the composed tree. **Nothing in this record is
deployed or pushed; the deploy is a separate lane that follows.**

Gate evidence: `test-results/merge-batch6-20261104/` in this tree (including
`control-base/` for the base-control runs).

## Composition

17 cherry-picks onto `7a2308cd6`, in lane order lane-fd153 → lane-fd151 →
lane-fd139-152 → lane-fd148-149. Register-bearing commits were composed by the
batch-4 method (`cherry-pick -n`, register restored from HEAD, `commit -C
<orig>`), so only each lane's lane record landed from those commits; every
register edit was made once, in the audit commit `b922b4ab9`.

| Lane lineage | Composed |
|---|---|
| fd153 `f31f44d15` (tests) | `f174fd4f6` |
| fd153 `a8720731e` (fix) | `b1228fa7b` |
| fd153 `b30b6ff84` (register/record) | `ceedfde7a` |
| fd151 `7d422cb8f` (fix) | `9d1ffadda` |
| fd151 `20129b456` (restage) | `8af46e1be` |
| fd151 `fae50a27e` (instructions) | `2d77bf0c1` |
| fd151 `6adea7b39` (register/record) | `5a35eeb56` |
| fd139-152 `c178a704e` (tests) | `18195a13d` |
| fd139-152 `00d8f5369` (fix) | `76212dfba` |
| fd139-152 `e8f120b22` (test corrections) | `0a2e3e857` |
| fd139-152 `2de1494b8` (JS stubs) | `1af4c89f5` |
| fd139-152 `55f009893` (register/record) | `702977ff9` |
| fd148-149 `5ac7f7431` (FD-148 fix) | `f8a11fdfe` |
| fd148-149 `82e3ebb51` (record-layout test pin) | `a55ef211f` |
| fd148-149 `6e3dba081` (FD-149 fix) | `ee6c0e78d` |
| fd148-149 `57651cfea` (citation re-pins) | `92a9580ec` |
| fd148-149 `781c574fc` (register/record) | `562c46319` |

Gate commits after composition: `869bc69ab` (citation re-pin, below) and
`c6f5ce676` (legacy-test expectation, below). The gate's product tree is
`869bc69ab`; both later commits are documentation/test-only.

### One hand resolution

`57651cfea` (fd148-149's vulkan citation re-pins) conflicted with fd153's
re-pins in `docs/known-gaps/vulkan.md`: 6 hunks, the same rows on both sides
pinned to different line numbers. Resolved arithmetically per pin position —
composed pin = HEAD pin + incoming pin − base pin — including range pins and
multi-citation rows (example: range row base [11190,11301] → composed
[11217,11309]). The gate citation audit over the composed tree confirms the
result (0 drift, below).

## Register settlements (audit commit `b922b4ab9`)

Settled by the substance of each defect, first-composed lane keeping a
contested number, per the standing rule. Closure blocks were taken verbatim
from each lane branch's register; every anchor asserted exactly-once.

Closures (the four-way claim on CL-104 and the two-way claim on CL-105 settle
to the first-composed lanes):

- CL-104 — FD-153 (lane-fd153): bridge drop logging fired on legal no-ops.
- CL-105 — FD-151 (lane-fd151): Node wasm64-selftest target compiles again.
- CL-106 — FD-152 (lane-fd139-152): disp64 fstatfs descriptor verdict.
- CL-107 — FD-139 (lane-fd139-152): utimensat flags verdict hoisted; a refused
  call no longer applies the times.
- CL-108 — FD-140 (lane-fd139-152): resolver `notDir` out-report → −ENOTDIR
  through a non-directory at guest-boundary calls.
- CL-109 — FD-148 (lane-fd148-149): `unnormalizedCoordinates` sampler flag
  carried bridge → wire → per-draw WGSL rewrite.
- CL-110 — FD-149 (lane-fd148-149): 32-bit `win32u.so` backport rebuilt with
  Vulkan support; produced the new retained archive (below).

Filings (the three-way claim on FD-155 settles to lane-fd153's filing;
lane-fd139-152's lineage FD-155…FD-159 renumber to FD-157…FD-161):

- FD-155 — 32-bit spec-info silent discard (filed by lane-fd153). Kept.
- FD-156 — MEMORY64 variant target break, SDL audio EM_ASM int casts (filed
  by lane-fd151).
- FD-157 — 32-bit xattr non-resolution (lane-fd139-152).
- FD-158 — readlink missing-path answers −EINVAL (lane-fd139-152).
- FD-159 — normalization-erased path shapes (`a.txt/`, `a.txt/.`, `a.txt/..`)
  (lane-fd139-152).
- FD-160 — x64 rmdir has no dispatcher case, answers −ENOSYS (lane-fd139-152).
- FD-161 — bare-filename link answers −ENOENT (lane-fd139-152).

Audit result on the composed register, re-verified at this record's writing:
**158 FD entries** — numbers 1..161 each exactly once except the pre-existing
gaps 46/56/57 — and **110 closures, CL-01…CL-110 each exactly once**, no
duplicates. Section B rows carry prepended "Batch-6 settled numbers" notes
with the settled mapping; the row texts keep the lanes' provisional numbers
(batch-5 style). Every Section A "see CL-" pointer carries a settled number.
Each closure names its lane fixing commit; the table above maps lineage to
composed commits.

## Gate bootstrap

Only from the newest retained archive, produced by the FD-149 lane and the
gate standard from this batch on:
`/home/ubuntu/backups/winbox-runtime-assets-20261104/browser-runtime-20261104.tar`
(514,457,600 B, sha256
`3c80c8a14e77e88a93fe5b9ce80d50077a457370aa7546070a211d95172062ba`).
13 assets restored and verified; `bootstrap.py --check` exit 0 before the
build (0 failures / 6 skips) and after (0 failures / 1 standing prefix64
skip); runtime completeness 19 PASS, 0 failures, 1 skip.

Setup note: the first gate build failed at staging — this tree was missing
the generated payload inputs (`tools/dxvk/build.w32|w64`,
`tools/rootfs64/{gstreamer32,gstreamer64,gecko,openal,cjk-font}`). They were
copied from the preserved wt-fd38 tree (read-only source, left as found); the
staged support archive then verified at its pin, proving the pinned vintage.
The copied inputs are generated state and are stripped from this tree after
the gate; they are not part of the composition.

## Gate figures (product tree `869bc69ab`)

- Browser build: **"Browser export verified"**; BUILD_INFO git_sha
  `869bc69ab`, dirty files 0, wasm sha256
  `9abd2f249db98a215a04f85332fce0b6ecc81063afc932cd95ffff37d20f195f`
  (5,684,501 B); exactly one staged support archive, at pin `c4dbe35986a5`.
- Citation audit: **352 rows, 0 drift** (232 MATCH / 14 MATCH-INHERITED /
  99 UNCHECKABLE / 7 EXEMPT — the batch-5 distribution). One drift was found
  and fixed at the gate (`869bc69ab`): reg:388's texel-buffer-view row cited
  lines 5549/5558; the constructs sit at 5565/5574 in the composed
  vk64bridge.cpp, verified by reading those lines.
- Silent-failure baseline: **unchanged**. Every scanner-returned file
  measures exactly its allowance (syscall64.cpp {0,7,134}, vk64bridge.cpp
  {0,10,0}, vkwebgpu.mjs {19,0,0}; all 17 returned files SAME).
  `web/shader.mjs` and `web/package-archive.mjs` are not returned by the
  scanner (as at batch 5) and are adjudicated by the JS unit scanner test,
  which passed.
- Native suites, serial: **main 682 tests, 0 FAILED** (679 standing + 3 from
  lane-fd139-152); **x64 108 tests, 0 FAILED** (105 + 3). Test-build targets
  exit 0.
- run-native: **67 PASS**, exit 0 (62 standing + 5 from lane-fd153).
- JS unit: **353 passed, 0 failed** (352 + lane-fd148-149's
  vk-unnorm-emulation).
- JS legacy: **115 passed, 0 failed** on re-run after gate commit `c6f5ce676`
  (see adjudications).
- Browser guest suite (full 32): **27 passed, 5 failed** — the five are
  FD-154's five, adjudicated against the base below. Boot census exact:
  boot64-smoke and boot32-vkboot consoles each show 0 BW64-FAIL lines and
  exactly 5 BW64-INFO served-link lines. Notable passes: cpu64-selftest
  (FD-151 discharge holds on the composed tree), d3d9-32 (FD-149),
  d3d9-comprehensive (FD-148), common-dependencies, both boots.
  Durations in this run are load-inflated (the box was busy throughout);
  no verdict rests on a duration.

## Adjudications

**project/linux Release build link failure — pre-existing, control-proven.**
The composed tree's Release build exits 2 with undefined references to
`hasMainThreadWork()`, `msgScratch(KThread*)`, and `tickXWirePresent()`. A
control build of the unmodified base `7a2308cd6` in a scratch worktree failed
identically (same three symbols, RELEASE_EXIT=2; log
`control-release-build.log` in the gate evidence directory).
No batch-6 lane touched project/linux. The gate's native standard is the
test suites, which pass.

**Browser suite failures — FD-154's five, reproduced on the base.**
input-fullscreen, input-mouse-rel, jit64, jitbench, and winsock-http failed
on the composed tree. Each was re-run individually against the unmodified
base `7a2308cd6`, built in a scratch worktree bootstrapped from the
20261103 archive (the base's own manifest; control build "Browser export
verified", BUILD_EXIT=0, same support-archive pin `c4dbe35986a5`). All five
fail on the base with identical signatures:

- input-fullscreen: Playwright `locator.click` timeout 30000 ms on
  `#canvas` (composed 53,942 ms; control 54,305 ms).
- input-mouse-rel: pattern `RAW sequence > 0, dx=100, dy=50` not seen within
  30000 ms; timeout-evidence deliveredMotion = 1 on both (composed
  53,847 ms; control 56,148 ms).
- jit64: pattern `CPU64: exit_group syscall, status=0 .*wineboot\.exe--init`
  not seen within 120000 ms on both (composed 127,883 ms; control
  128,343 ms).
- jitbench: assertion "jitbench32: requested JIT mode latched — false !==
  true" on both (composed 73,523 ms; control 86,421 ms).
- winsock-http: guest output `BW_TCP connected_event=1` not seen; relay
  counters accepted 0, toTcpBytes 0, toWebSocketBytes 0 on both (composed
  51,352 ms; control 54,052 ms).

FD-154 was filed open at batch 5 and stays open; no new filing, and no
browser-suite regression is attributable to this batch.

**JS legacy first-run failure — the FD-148 lane's own test gap, fixed at the
gate.** The first legacy run went 113 pass / 1 fail:
`web/tests/vkwebgpu-v2.test.mjs` deep-equal missed `unnorm: false` in the
decoded sampler model. The identical failure reproduces on the lane tip
`781c574fc` in a scratch worktree (18 pass / 1 fail, same assertion), proving
the gap is the lane's, not a composition interaction. Gate commit `c6f5ce676`
updates the expectation (test-only, in the file's established FD-64 pattern);
the legacy re-run passes 115 / 0.

## Still owed / unchanged

- Deploy (live tree + GitHub push under the standing rule) is the next lane.
- FD-154 open (the five base-reproduced browser failures above).
- FD-155…FD-161 open as filed.
- FD-115 tally unchanged: 1 wedged in 30 shipped-build first-visit x86
  starts; the deploy lane's series counts toward its closing terms.
- CL-43's pipeline clause remains owed on its exact terms (a shipped-build
  run creating its two named modules).
