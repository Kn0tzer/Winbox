# Batch-7 merge record — 2026-11-04

Integration branch `integ/winbox-20261022b`, base tip `3ea2fd97d` (the batch-6
deploy state). Four lane branches composed, the defect register settled by
audit, and the full gate run on the composed tree. **Nothing in this record is
deployed or pushed; the deploy is a separate lane that follows.**

Gate evidence: `test-results/merge-batch7-20261104/` in this tree.

## Composition

24 cherry-picks onto `3ea2fd97d`, in lane order lane-fd155 → lane-fd156 →
lane-fd157-161 → lane-fd154, zero conflicts. Register-bearing tip commits were
composed by the batch-4 method (`cherry-pick -n`, register restored from
HEAD, `commit -C <orig>`), so only each lane's record landed from those
commits; every register edit was made once, in the audit commit `c2e6b11e4`.

| Lane lineage | Composed |
|---|---|
| fd155 `64dbe21e5` (tests) | `ac2734793` |
| fd155 `5bd07e141` (fix) | `ea6fef050` |
| fd155 `0dc8a0c7f` (citation re-pins) | `466fec957` |
| fd155 `730fa687d` (register/record) | `c522915ef` |
| fd156 `3ee906b3a` (fix) | `ffa1b3aaf` |
| fd156 `b4d3f2e8f` (run instructions) | `6cb346142` |
| fd156 `84116a08e` (register/record) | `9c74170eb` |
| fd157-161 `c22bc6805` (tests) | `eacac5294` |
| fd157-161 `7239fe69c` (fix) | `87a4c0026` |
| fd157-161 `119e6c5c5` (register/record) | `dfd780ff7` |
| fd154 `d26f606a1` (winsock fix) | `41a1fa6f8` |
| fd154 `3cce91bcb` (mouse-rel fix) | `431376004` |
| fd154 `189f6b302` (jit64 pattern) | `354a94df6` |
| fd154 `b99c92154` (jitbench jit32) | `d1a9638ee` |
| fd154 `e482eb6a6` (input-fullscreen harness) | `0582a2310` |
| fd154 `f610e90b2` (grab freeze fix) | `a6f21e248` |
| fd154 `dbe7705f3` (own-grab delivery) | `310300ce1` |
| fd154 `25471296c` (sync-as-async) | `d1067de50` |
| fd154 `260545f5b` (jitbench stats gate) | `eca29cf14` |
| fd154 `1d2c059d8` (jitbench stats gate 2) | `e213fa86c` |
| fd154 `41de3c327` (jit64 availability gate) | `f05288921` |
| fd154 `87d9e021b` (jitbench stub) | `bb88bfcc4` |
| fd154 `58f759879` (xwire unit contracts) | `df7356eb7` |
| fd154 `0561aeba6` (annotation/record) | `19559751d` |

## Register settlements (audit commit `c2e6b11e4`)

Settled by the substance of each defect, first-composed lane keeping a
contested number, per the standing rule. Lane closure blocks were taken
verbatim from each lane branch's register; placement and counts were checked
by section anchors and duplicate scans, not by the pick tool's clean report.

Closures (CL-111 was claimed by three lanes for three different defects;
lane-fd157-161's five closures ran CL-111…CL-115 provisionally and settle to
CL-113…CL-117; lane-fd154 had annotated FD-154 with per-item verdicts instead
of closing it, so the audit authored its closure):

- CL-111 — FD-155 (lane-fd155): the 32-bit specialization capture's silent
  header discards now report loudly.
- CL-112 — FD-156 (lane-fd156): the MEMORY64 self-test target compiles again.
- CL-113 — FD-157 (lane-fd157-161): 32-bit xattr path forms resolve before
  answering.
- CL-114 — FD-158 (lane-fd157-161): readlink/readlinkat of a missing path
  answers −ENOENT.
- CL-115 — FD-159 (lane-fd157-161): trailing-marker paths through a file
  answer −ENOTDIR; the resolver judges the anchor.
- CL-116 — FD-160 (lane-fd157-161): x64 rmdir (84) dispatcher case added.
- CL-117 — FD-161 (lane-fd157-161): `Fs::getParentPath` of a slash-less path
  returns the working directory; a hard link to a bare filename succeeds.
- CL-118 — FD-154 (lane-fd154, closure authored by this audit): all five
  batch-5 browser-suite failures adjudicated to product or harness causes
  and fixed; the closure names the lane fixing commit per item.

Filings (uncontested, kept at their numbers):

- FD-162 — the 32-bit xattr descriptor forms answer the emulator-private
  −EBADFD (77) for an unopened descriptor where the host answers −EBADF (9);
  flistxattr never consults its descriptor. 64-bit is correct.
- FD-163 — an xattr name in an unmodelled namespace answers −ENODATA where
  the host answers −ENOTSUP.
- FD-164 — the mid-path ".." erasure family (`missing/../a.txt`,
  `a.txt/../d`, ".." at the root) still diverges after FD-159's
  trailing-marker fix.

Audit result on the composed register, re-verified fresh at this record's
writing: **161 FD entries** — numbers 1..164 each exactly once except the
pre-existing gaps 46/56/57 — and **118 closures, CL-01…CL-118 each exactly
once**, no duplicate titles. Section A entries FD-154…FD-161 carry the lanes'
closing accounts verbatim with settled `see CL-` pointers (FD-154's entry is
the lane's annotation verbatim plus an audit closing account ending "see
CL-118"). Section B gained a row per lane, each prepended with a "Batch-7
settled numbers" note; row texts keep the lanes' provisional numbers
(batch-5/6 style). Lane-fd154 had no Section B row of its own; the audit
authored one from its record.

## Gate bootstrap

Only from the newest retained archive:
`/home/ubuntu/backups/winbox-runtime-assets-20261104/browser-runtime-20261104.tar`
(514,457,600 B, sha256
`3c80c8a14e77e88a93fe5b9ce80d50077a457370aa7546070a211d95172062ba` — the same
archive the batch-6 gate used). 13 assets restored and verified;
`bootstrap.py --check` exit 0 before the build (0 failures / 5 skips) and
after the build (0 failures / 1 standing prefix64 skip — the batch-6
profile).

Setup note: this tree was missing the generated payload inputs
(`tools/dxvk/build.w32|w64`,
`tools/rootfs64/{gstreamer32,gstreamer64,gecko,openal,cjk-font}`). They were
copied from the preserved wt-fd38 tree (read-only source, left as found);
the staged support archive then verified at its pin, proving the pinned
vintage. The copied inputs are generated state and are stripped from this
tree after the gate; they are not part of the composition.

## Gate figures (product tree `c2e6b11e4`)

- Browser build: **"Browser export verified"**; BUILD_INFO git_sha
  `c2e6b11e4b88586e4ce0304eaba3cae4b244ae73`, dirty files 0, wasm sha256
  `c0af957ae56bc11055714e62e37055218ccbc5e8ac775bded0d229ef0c82577a`
  (5,687,273 B); exactly one staged support archive, at pin `c4dbe35986a5`.
- Citation audit: **352 rows, 0 drift** (232 MATCH / 14 MATCH-INHERITED /
  99 UNCHECKABLE / 7 EXEMPT — the standing distribution).
- Silent-failure baseline: **unchanged**. Every scanner-returned file
  measures exactly its allowance (syscall64.cpp {0,7,134}, vk64bridge.cpp
  {0,10,0}, vkwebgpu.mjs {19,0,0}; all 16 returned files SAME).
  `web/package-archive.mjs` and `web/shader.mjs` are not returned by the
  scanner (standing) and are adjudicated by the JS unit scanner test, which
  passed.
- Native suites, serial: **main 686 tests, 0 FAILED** (682 standing + 4 net
  new from lane-fd157-161); **x64 111 tests, 0 FAILED** (108 + 3).
  Test-build targets exit 0.
- run-native: **69 PASS**, exit 0 (67 standing + FD-155's 2 new).
- JS unit: **353 passed, 0 failed**.
- JS legacy: **115 passed, 0 failed** (116 tests, one skipped).
- Browser guest suite (full 32): **31 passed, 1 failed** — the one failure
  is jitbench, adjudicated below. Boot census exact: boot64-smoke and
  boot32-vkboot consoles each show 0 BW64-FAIL lines and exactly 5
  BW64-INFO served lines. All four other FD-154 tests pass on the composed
  tree (winsock-http, input-fullscreen, input-mouse-rel, jit64), as do
  d3d12-comprehensive, celeste-boot, and common-dependencies. Durations in
  this run are load-inflated (the box was busy throughout); no verdict
  rests on a duration.

## Adjudication — jitbench

The in-suite failure is a runner timeout, not an assertion: "browser test
timed out after 3660000ms (hung probe killed)". jitbench runs 16 guest
boots (4 fixtures × 4 modes) and, before each, waits for the box's 1-minute
load average to fall below 4 — its own shared-host guard. All three runs
below are preserved in the gate evidence directory.

- In-suite run: the test's own saved results (`jitbench-insuite.json`)
  record invocation load 8.22 (1-minute) at 17:27 and 5 of 16 runs
  complete — jitbench32 in all four modes and jitbench64 mode 0 — each
  with the full four samples and consistent checksums. The remaining
  budget was consumed in the load gate while other tenants held the box
  at load 5.6–12.1 (observed).
- Standalone re-run 1 (load gate raised to 8 under the test header's
  documented provision for shared hosts; observed load recorded in
  `jitbench-rerun-load.txt`): 8 of 16 runs complete and correct
  (jitbench32 and jitbench64, all modes), then a single "memory access
  out of bounds" wasm trap killed the guest during the first
  jitbenchmix-x86 boot (`jitbench-rerun-console.log`).
- Focused mix re-run (fixtures jitbenchmix-x86 and jitbenchmix-x64, modes
  0 and 1): **PASS** (190,836 ms). All four runs complete, four samples
  each, one checksum `b8476f000fa05ee2` across every run; the boot that
  trapped in re-run 1 ran cleanly, as did the other three mix boots —
  four mix boots, zero traps (`jitbench-mix-console.log`,
  `jitbench-mix.json`).
- A third attempt at the full default matrix reproduced the in-suite
  pattern exactly (same first five runs complete and correct, then parked
  at the load gate as tenant load rose to 12.13) and was terminated by
  this lane once the adjudication was closed; its console is preserved
  (`jitbench-full3-console.log`) and no verdict rests on it.

Coverage across the preserved runs: every fixture × mode of the default
matrix passed on this build, except the mix fixtures' modes 2 and 3, whose
JIT settings duplicate modes 1 and 0 (mode order is 0,1,1,0) and whose
fixtures passed in both distinct settings. Verdict: **no product
regression attributable to this batch**. The timeout is the test's own
load gate on a shared box; the single boot trap did not reproduce in four
subsequent mix boots and is recorded here — if it recurs it should be
filed on its own evidence.

## Still owed / unchanged

- Deploy (live tree + GitHub push under the standing rule) is the next
  lane.
- FD-162, FD-163, FD-164 open as filed by lane-fd157-161.
- FD-115 tally unchanged by this gate (its count covers shipped-build
  first-visit starts; this gate ran on the integ tree): 1 wedged in 35
  shipped first-visit x86 starts, per the batch-6 deploy record.
- CL-43's pipeline clause remains owed on its exact terms (a shipped-build
  run creating its two named modules; still gated on FD-72).
- The question lane-fd154 flagged — whether the shipped build should
  switch to the JIT variant — remains an open build decision; the
  standing measurement has 32-bit JIT worse, so the default does not flip
  without re-measuring.
