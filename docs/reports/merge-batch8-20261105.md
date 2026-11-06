# Merge record — batch 8 (2026-11-05)

Composed three finished lanes onto integ/winbox-20261022b (base 78c473a56, the
batch-7 deploy record) and audited the register clean in one audit commit.
Gate target: the audit tip 48d27952a. Evidence under
test-results/merge-batch8-20261105/. NOT deployed, NOT pushed.

## Composition (zero conflicts)

Cherry-picked in order lane-fd162-164, lane-buildrel, lane-cleanup-status.
Register-bearing tip commits were composed by the batch-4/7 method
(cherry-pick -n, register restored from HEAD, commit -C <orig>) so each landed
only its record file; every register edit was made once, in the audit commit.

| Composed | Source | Content |
|---|---|---|
| 7db8f238f | ec62ca78e (lane-fd162-164) | FD-162/163/164 guest tests |
| e45a82ccd | 9b4ce67b1 (lane-fd162-164) | FD-162/163/164 fix |
| 5581dfe41 | 872d1b474 (lane-fd162-164) | slice-safe follow-up |
| 4ffd192f2 | c4dd96ecd (lane-fd162-164) | fd162-164 record file only |
| 6bfd8db61 | 05ab65469 (lane-buildrel) | FD-31/32/33 code (syscall64.cpp auto-merged, no conflict) |
| 42a23c42f | b4681d12f (lane-buildrel) | buildrel record file only |
| 4fdd7812d | 37cdf452d (lane-cleanup-status) | status docs + cleanup record (register deferred) |

Final composed diff vs base, register excluded: 16 files, 1366 insertions,
124 deletions — exactly the lanes' sums (654/46 + 399/73 + 171/5) plus the
deploy record's +142 from the base commit. Cross-diffs of
source/kernel/syscall64.cpp (the one file two lanes touched) against each
lane branch show exactly the other lane's hunks.

Closures this batch: FD-162, FD-163, FD-164 (xattr descriptor forms, unmodelled
xattr namespaces, mid-path dot-dot resolution) and FD-31, FD-32, FD-33
(process-sharded native suites; native Release link failure). FD-165 was filed
by the fd162-164 lane (xattr set/remove name classification) and stays open.

## Register audit — commit 48d27952a (+430/−2)

Applied lane-fd162-164's register patch and the cleanup lane's register patch
with git apply (both computed against the same base blob the composed tree
still carried), and lane-buildrel's five difflib-verified inserts (Section A
accounts for FD-31/32/33 anchored on their entry-tail lines, its Section B
row, and its Section C closure block before the Appendix heading) with an
assertion-guarded script.

Number collision settled by substance, first-composed lane keeps the
contested numbers: lane-fd162-164's CL-119/120/121 stand for FD-162/163/164
exactly as written; lane-buildrel's provisional CL-119/120/121 were
renumbered to CL-122/123/124 for FD-31/32/33 (Section A accounts and closure
blocks renumbered including internal cross-references; its Section B row text
kept verbatim with a prepended "Batch-8 settled numbers" note; the fd162-164
row received the matching note). FD-165 keeps its number and stays open. The
cleanup lane's Section B status correction was applied verbatim, plus a
batch-8 audit note recording that batch 8 composed the two lanes that
correction described as in progress / uncomposed, with closures CL-119–124
and rows added.

Verification, re-read from disk: 162 FD entries — numbers 1..165 each present
exactly once except the pre-existing gaps 46/56/57; 124 closures — CL-01..124
each exactly once, no duplicates, none over range; all six new "see CL-"
pointers match their closure headers (FD-162→CL-119, FD-163→CL-120,
FD-164→CL-121, FD-31→CL-122, FD-32→CL-123, FD-33→CL-124). Counting note:
closure headers must be counted file-wide; CL-20..51 and CL-57..66 physically
sit after the Appendix heading (an old layout artifact).

## Gate — bootstrap and browser build

Runtime assets restored from the retained archive only
(/home/ubuntu/backups/winbox-runtime-assets-20261104/browser-runtime-
20261104.tar; sha256 re-verified
3c80c8a14e77e88a93fe5b9ce80d50077a457370aa7546070a211d95172062ba);
"Restored and verified 13 runtime assets" (bootstrap.log). Pre-build
completeness check exit 0: 0 failures, 1 skip (the standing prefix64-built
artifact skip; batch 7's extra skips were absent because a support pack was
already present in dist from the batch-7 gate). First build attempt was
aborted by the merge lane's own error (restored asset state deleted before
building; the fallback download URL 404s — the known FD-73 residue) and was
re-run by the correct procedure (restore, then build with no deletions in
between). "Browser export verified: 34 mjs syntax files; … dependency archive
c4dbe35986a5 verified" + "BUILD COMPLETE" (browser-build.log). BUILD_INFO:
git_sha 48d27952a39c629f67c979b3d4b97ecc9c1d364f, dirty 0, wasm sha256
9b51d60101ab21a39bd88c4c8391512509361dd4845af22b8bc261eaaa6a5241
(5,688,949 bytes); exactly one support archive in dist/runtime. Post-build
completeness check: exit 0, 0 failures, 1 skip.

## Gate — script and native suites

- Citation audit: 352 rows, 0 drift (232 MATCH / 14 MATCH-INHERITED /
  99 UNCHECKABLE / 7 EXEMPT) — citations.log.
- Silent-failure baseline: unchanged (returned=16, diffs=0); every file
  exactly at allowance including source/kernel/syscall64.cpp {0,7,134}
  despite both lanes editing it; web/package-archive.mjs and web/shader.mjs
  NOT-RETURNED (standing; adjudicated by the JS unit scanner test) —
  silent-failure.log.
- JS unit suite: 353 passed, 0 failed (js-unit.log). 353 is the standing
  count; no unit files were added by this batch.
- JS legacy suite (npm test): 116 tests, 115 pass, 0 fail, 1 skipped —
  js-legacy.log.
- run-native (vkd3d): 69 PASS (run-native.log).
- Native main flavour: 689 tests, 0 failures in 377 s (native-main.log) —
  686 standing + 3 net new from lane-fd162-164 (xattr descriptor forms,
  namespace classification, mid-path dot-dot).
- Native x64 flavour: 113 tests, 0 failures in 30 s (native-x64.log) —
  111 standing + 2 net new.

## Gate — browser guest suite and boot census

The browser suite (32 tests) ran against the composed build (build
identity wasm 9b51d601 in the boot tests' saved artifacts) under the shared
probe-slot discipline, while up to six sibling lanes built and probed on the
same 4-core box (load 4.4–25; five separate lane builds drained in sequence
during the gate). Every test carries a PASS verdict on this build; several
needed focused reruns after contention failures in earlier attempts. No
failure in any attempt carried a product signature — no BW64-FAIL line, no
trap, no failed assertion: every non-passing attempt ended in a probe-slot
acquire timeout, a runner timeout with the guest still progressing, or a
guest-progress wait expiring, and each such test then passed on the same
build once it received CPU.

Full run (browser-suite-attempt1.log; stopped by the merge lane after eight
recorded verdicts to switch to grouped reruns): PASS alsa-mmap-stream,
audio-tone, boot32-vkboot, celeste-boot. FAIL boot64-smoke (guest-progress
timeout at a load-~25 spike; its console 0 BW64-FAIL), common-dependencies
(runner timeout at its 1,260 s envelope), cpu32-branches and
cpu32-jit-growth (probe-slot timeouts in setup).

Grouped reruns (browser-rerun-<group>.log): PASS cpu32-jit-growth;
gl32-framebuffer, gl32-triangle, gl32-webgpu-clear; win32-clock and
win32-fonts; dbwin-echo; fna-runtime; fork-mapping; memory-shared;
mono-runtime; boot64-smoke (focused rerun — see the census below);
d3d10-comprehensive, d3d11-comprehensive, d3d11-triangle and
d3d12-comprehensive; input-keyboard. Contention failures in this round:
cpu32-branches, cpu32-sse3, cpu64-selftest, winsock-http, d3d9-32 and
d3d9-comprehensive (probe-slot timeouts — the d3d9 pair waited out full
1,200 s acquire caps without ever receiving a slot), input-fullscreen
(runner timeout mid-harness, console clean), input-mouse-rel (guest-boot
wait expired at load ~24, console clean), common-dependencies (guest-boot
wait expired), jit64 (probe-slot timeout), and jitbench (runner timeout at
its 3,660 s cap; its load gate never opened).

Further attempts (browser-mopup-<group>.log and
browser-retry-<test>-a<n>.log): a mop-up chain covering all ten outstanding
tests and a six-attempt retry loop for the short-cap tests added no passes;
every failure was the same contention classes, including three more
common-dependencies attempts and a third full-cap slot timeout for
d3d9-32.

Window-timed attempts (browser-window-<test>-w1.log;
browser-window-summary.txt): each remaining test was launched only into a
window with no lane build draining (long-cap tests) or a fully quiet slot
queue (short-cap tests). All nine passed on their first window attempt:
d3d9-32, d3d9-comprehensive, input-fullscreen, input-mouse-rel, jit64,
cpu32-branches, cpu32-sse3, cpu64-selftest, winsock-http.
common-dependencies ran in its window, but its 180 s MSHTML pattern wait
expired with the guest alive and progressing slowly (console again
0 BW64-FAIL) — the busy era's tail rather than a verdict on the guest work
itself.

Final focused runs on the quieted box (browser-final-common.log,
browser-final-jitbench.log; loads in browser-final-loads.txt):
common-dependencies PASS (297 s; 1-minute load 1.4–7.9 across the run),
closing its adjudication as contention, not regression; jitbench PASS
(649 s) under the documented shared-host provision
BW_TEST_JITBENCH_MAX_LOAD=12 (the test's own default gate is 4; observed
1-minute loads 7.5–7.9 stayed under the raised gate), fixture table in the
log.

Boot census (fresh-start checks): boot32-vkboot passed in the full run;
its console carries 0 BW64-FAIL lines and exactly 5 BW64-INFO served-link
lines (boot32-insuite-console.log). boot64-smoke's focused rerun passed on
the same build; its console carries 0 BW64-FAIL lines and exactly
5 BW64-INFO served-link lines (boot64-rerun-console.log). The census is
exact on both architectures.

## Still owed after batch 8

- The batch-7 deploy's common-dependencies LIVE re-check (three preserved
  attempts blocked by shared-box contention, no product fault shown) remains
  owed on the shipped build; the merge-gate evidence for that test is in the
  browser section above.
- FD-165 (xattr set/remove name classification) — filed by the fd162-164
  lane; a fix lane is the follow-on.
- The open build decision flagged by FD-154 (whether the shipped build
  should switch to the JIT variant) is untouched by this batch; the standing
  measurement says the 32-bit JIT is worse — do not flip without
  re-measuring.

Batch 8 state: merged on integ/winbox-20261022b, gate record this file.
Not deployed, not pushed.
