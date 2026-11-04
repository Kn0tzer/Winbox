# FD-125 settlement — the base A/B, four dispositions, and the uncharacterised browser tests classified

Lane `lane-fd125-ab`, based on `lane-fd124` tip `1651c7845`.
This lane settles the two open questions the FD-124 lane left,
with evidence, and files what proves real. It changes no
product code: its commits are the register update described
below and this record.

## The two builds

Both builds were made in fresh worktrees with the lineage's
standard inputs (bootstrap --local from the retained
runtime-assets-20261101 archive, the node_modules symlink to
the main tree, pinned-vintage dxvk builds and gstreamer
payloads copied from wt-fd38, and — for the D3D12 test — the
preserved vkd3d build trees from wt-restage / wt-x86vkd3d):

- Lineage build: `wt-fd125ab` at `1651c7845`, "Browser export
  verified", wasm sha256 `07d5b927` — byte-identical to the
  FD-124 lane's own recorded build (its BUILD_INFO git_sha
  was the fix commit `118d2fc85`; the code is the same).
- Base build: `wt-fd125base` at the unmodified base
  `daf5ec34e`, wasm sha256 `10d65ac0` — the hash the
  FD-120..123 lane recorded for its own build of that base.

Every browser test below ran standalone through the suite
runner (`web/tests/suite/runner.mjs --browser --filter=<name>`),
one at a time through the probe slot, on both builds.
Evidence: `test-results/fd125ab/` in this worktree
(`build-fd124.log`, `build-base.log`, `runs-fd124build.log`,
`runs-basebuild.log`, both builds' per-test console
directories — the lineage build's under `test-results/`,
the base build's copied under `test-results/fd125ab/base-suite/`,
plus `hostref-chmod.c` / `hostref-chmod.out.txt`).

## FD-125 is proven pre-existing, with dispositions

The guest CPU self-test was run standalone against the base
build. Its summary is identical to the lineage build's,
to the digit and to the register values:

- Base (`daf5ec34e`): `=== self-test summary: 951 passed,
  4 failed ===`; FAIL lines: chmod (R15=0xfffffffffffffffe,
  -ENOENT), fchmod (R15=0xfffffffffffffff7, -EBADF),
  "required JIT path did not execute any blocks",
  "no JIT engine selected (engine=off)" (with
  `JIT64: engine=off blocks=0 insns=0`).
- Lineage (`1651c7845`): the same summary and the same four
  lines (also matching the FD-124 lane's preserved console).

The four failures stand on the unmodified base: FD-125 is
pre-existing, not a regression on this lineage. Its register
entry now carries this A/B and the four dispositions:

1. **JIT pair — fixture expectation defect for the standard
   build.** The two assertions exist only when the fixture
   sets BW64_JIT_VERIFY=1, and they demand an engine the
   standard (interpreter) browser build does not select by
   design. engine=off with zero blocks is the correct record
   on that build; the JIT build target (wasm64-mt-jit) is
   where an engine-selected proof belongs. The self-test
   should not count these as failures when no engine is
   selected. No product change owed.
2. **fchmod, descriptor 0 staged unopened — the layer's
   answer is the host's.** A host reference program on the
   box kernel measures: fchmod on a closed descriptor and
   on a never-opened descriptor answers -EBADF; fchmod(0)
   answers 0 only because descriptor 0 (stdin) is open in
   that process. The self-test stages descriptor 0 with
   nothing open, so the emulator's -EBADF is the faithful
   answer. The case's "no-op success" expectation predates
   FD-58, which implemented chmod for real. Disposition:
   the case should be re-staged on an open descriptor
   (expect 0) or expect -EBADF as staged; the layer stands.
3. **chmod, null path — same stale expectation, one real
   divergence underneath.** The host measures chmod(NULL)
   at -EFAULT and chmod("") at -ENOENT. The emulator
   answers -ENOENT — exactly its empty-path answer —
   because the 64-bit dispatcher's shared guest-string
   reader (readGuestString64 in syscall64.cpp) returns an
   empty string for address 0, collapsing a null path into
   "". That divergence is filed separately as **FD-126**
   (measured instance chmod; the reader is shared across
   the dispatcher's path calls, stated as inspection).
   The self-test case should expect the implemented
   layer's real answer.
4. What remains of FD-125 itself is the self-test cases'
   re-staging — a test-side change. The entry stays filed
   with the settlement recorded in it.

## The uncharacterised browser tests, classified

The FD-124 lane's beyond-gate full-suite run failed seven
browser tests under contention (load 6.6); it re-ran three
standalone, and its log note records that the re-run script
was stopped during d3d11-triangle's slot wait, leaving
**four** D3D tests without a standalone verdict (its record
prose says "the remaining three"; the log note names
d3d11-triangle, d3d11-comprehensive, d3d12-comprehensive
and d3d9-32). This lane re-ran all of them standalone, plus
the two that had failed their first standalone re-runs,
plus d3d9-comprehensive — which shared the contended
failure list and likewise had no standalone verdict.

| Test | Lineage build (07d5b927) | Base build (10d65ac0) | Classification |
| --- | --- | --- | --- |
| cpu64-selftest | FAIL 951/4 (4,099 ms, load 1.9) | FAIL 951/4, identical lines | Pre-existing → FD-125 settled |
| cpu32-jit-growth | FAIL (20,398 ms, load 1.9): guest `RESULT 0`, then the harness's `stats.cpu32.retiredCounting === true` reads undefined | FAIL (22,860 ms), identical shape | Pre-existing → FD-130 |
| d3d10-comprehensive | FAIL (112,252 ms, load 2.2): `tri10: RESULT 1`; console: device OK, shaders compiled, `LIT=0` | FAIL (135,994 ms): identical assertion | Pre-existing → FD-127 |
| d3d11-triangle | FAIL (96,453 ms): `tri1193: RESULT 1` | FAIL (141,195 ms): identical assertion | Pre-existing → FD-127 |
| d3d11-comprehensive | FAIL (100,795 ms): `D3D11 must render pixels (LIT=0)` | FAIL (141,590 ms): identical assertion | Pre-existing → FD-127 |
| d3d12-comprehensive | **PASS** (109,509 ms) once the tree carried the vkd3d payload | **PASS** (152,758 ms, under load 12.3) | Passes standalone — contention/staging, no defect |
| d3d9-32 | FAIL (326,564 ms): `/clear9: CAPTURE_READY/ not seen within 300000ms` | FAIL (326,067 ms): identical, incl. the stall dump's guest thread state | Pre-existing → FD-129 |
| d3d9-comprehensive | FAIL (95,772 ms, load 1.3): `x64 DXVK clear must reach the browser compositor with its final red color` | FAIL (272,771 ms): identical assertion | Pre-existing → FD-128 |

No test passes on the base and fails on the lineage build:
**there is no regression on the FD-124 lineage in this
set.** The D3D10/11 shape is one filing (FD-127): device
and shaders succeed, the draw reports no error, and the
readback is empty — while the same builds pass the D3D12
test, so the browser graphics path renders and the failing
route is specifically DXVK's D3D10/11 one, on the pinned
DXVK vintage both lane trees stage (a different vintage is
unmeasured, and the entry says so). FD-128 (D3D9 clear
never reaches the compositor) and FD-129 (32-bit D3D9
capture never ready; the guest console carries Wine's
"Failed to load Wine graphics driver supporting Vulkan" /
"VK_KHR_surface not supported" lines on both builds) are
filed separately: same family silhouette, different stage
and architecture, unproven shared root. FD-130 records the
cpu32-jit-growth mechanism from the source: the JIT
statistics implementation compiles only under the JIT build
flag, so the standard build's `bw64_jit_stats_json_text`
carries no cpu32 section for the test to read — the same
build-flavour mismatch as FD-125's JIT pair, in harness
form.

## Two invalid runs, recorded rather than hidden

- This lane's first d3d12-comprehensive run failed in
  65,444 ms with `ENOENT: ... tools/vkd3d/build.w64/libs/
  d3d12/d3d12.dll` — the fresh worktree lacked the generated
  vkd3d payload (the sibling setup notes for this lineage
  list dxvk and gstreamer inputs, not vkd3d). The FD-124
  lane's tree lacks it too, so that lane's contended
  d3d12-comprehensive failure was the same staging gap and
  never a product signal. With the payload copied from the
  preserved transfer trees, the test passes on both builds.
- The base series' first cpu64-selftest run "failed" at
  180,210 ms without running: a sibling lane's build
  (wt-fd127fix) held build-pending and the test's probe-slot
  acquisition timed out at its 180 s bound. That entry is
  annotated in `runs-basebuild.log` as a non-verdict; the
  series was stopped and re-run clean once the interlock
  cleared, and the table above uses the clean re-run.
  (The FD-124 lane's base A/B died of the same interlock
  from the other side; here it only cost a re-run.)

## Register changes in this lane's commit

- FD-125: the "NOT measured" passage and closing terms are
  replaced by the measured A/B and the four dispositions
  above; the entry stays open on its test-side remainder.
- FD-126 filed: null path pointer answers the empty-path
  error (-ENOENT) where the host answers -EFAULT; the
  shared guest-string reader is the mechanism; chmod is
  the measured instance.
- FD-127 filed: D3D10/D3D11 draws land nothing (LIT=0) on
  both builds.
- FD-128 filed: D3D9 clear never reaches the compositor
  with its final colour, on both builds.
- FD-129 filed: d3d9-32's capture never becomes ready,
  on both builds.
- FD-130 filed: cpu32-jit-growth's statistics assertions
  read fields the standard build does not emit, on both
  builds.
All numbers are this lineage's provisional numbering; the
merge audit renumbers, and each entry is described by its
substance for that reason.

## Numbers in this record

Re-read from the repository after the fact: lineage tip
`1651c7845`, base `daf5ec34e`; lineage wasm `07d5b927`
(5,642,849 B), base wasm `10d65ac0`; both builds "Browser
export verified" with staged archive `c4dbe35986a5` as
pinned. Host reference: box kernel 6.17.0-1020-oracle,
`test-results/fd125ab/hostref-chmod.c`.
