# Deploy record — batch 8 (2026-11-06)

Batch 8 (merge record `docs/reports/merge-batch8-20261105.md`, integ tip
`8017fdabc`) is deployed to the live site. Gate figures, from the merge
record (re-read from disk by this deploy lane): build verified from
`runtime-assets-20261104` only, BUILD_INFO git_sha `48d27952a` dirty 0,
wasm sha256
`9b51d60101ab21a39bd88c4c8391512509361dd4845af22b8bc261eaaa6a5241`
(5,688,949 bytes), exactly one support archive at pin `c4dbe35986a5`;
native serial main 689/0, x64 113/0; run-native 69 PASS; JS 353/0; legacy
115/0/1 skip; citations 352 rows 0 drift; browser suite 32 tests, every
test carrying a PASS verdict on the composed build (contention failures
in earlier attempts adjudicated per-test, none with a product
signature). Register at merge: 162 FD entries (numbers 1..165 except
pre-existing gaps 46/56/57), closures CL-01..CL-124 each exactly once
(the closure count was re-verified from disk by the coordinator during
this deploy: 124 unique headers, no duplicates, none missing).
Closures this batch: FD-162/163/164 (CL-119/120/121) and FD-31/32/33
(CL-122/123/124, renumbered from the buildrel lane's provisional
CL-119/120/121 by the batch-8 audit). FD-165 stays open.

Note on the merge lane: its completion report never arrived, so this
deploy verified the merge from disk instead of taking any narration:
the integ tip, the merge record, the register audit counts, and the
gate log files were all re-read on the box before any deploy step.
Nothing in this record rests on the merge lane's say-so.

## Build and staging

The merge lane stripped the integration tree's generated state after
its gate, so no gate dist survived to stage. This deploy rebuilt the
browser dist from the integration tip (`tools/bw64-build.sh`, owner
`deploy-batch8`) after restoring the runtime assets through the clean
bootstrap path ONLY: `bootstrap.py --local` from the retained
`runtime-assets-20261104` archive (sha256 re-verified
`3c80c8a14e77e88a93fe5b9ce80d50077a457370aa7546070a211d95172062ba`)
— "Restored and verified 13 runtime assets"; `bootstrap.py --check`
exit 0 before the build (0 failures, 5 skips) and again after it
(0 failures, 1 skip, the standing prefix64 built-artifact skip).
Unlike batches 4–6, the strip had NOT removed the tree's generated
payload inputs (tools/dxvk build.w32 + build.w64 and the rootfs64
payloads were still present), so no payload copies were needed.

The rebuild is product-identical to the merge gate's build: the wasm
is byte-identical to the gate's
(`9b51d60101ab21a39bd88c4c8391512509361dd4845af22b8bc261eaaa6a5241`,
5,688,949 bytes), because the only commit between the gate build's
tree (`48d27952a`) and the integ tip (`8017fdabc`) adds the merge
record document alone — verified by diff (1 file, +188 lines). The
final dist stamp is git_sha
`8017fdabcadde86e7dfad59a1c8eb50db609d4da`, git_dirty_files 0.
Re-verified against the rebuilt dist: the build log carries the
literal "Browser export verified: 34 .mjs syntax files; …
dependency archive c4dbe35986a5 verified" plus "BUILD COMPLETE";
exactly one support archive in the dist,
support-c4dbe35986a5 (sha256
c4dbe35986a5edcff6646cb8b4f63cc7660e6e1e6dcda6546454db77d241729f);
check_runtime_completeness.py 19 PASS, 0 failures, 1 skip, exit 0;
JS unit suite against the rebuilt dist 353 passed, 0 failed —
identical to the merge gate. The gate's remaining figures (native,
run-native, legacy, citations, silent-failure baseline, browser
suite) are the merge record's and were not re-run, per the batching
rule — except common-dependencies, re-run against the live build
below.

Staging: the rebuilt dist was copied to winbox-next and verified
byte-identical to the dist (recursive diff empty); its file set is
identical to the previous live tree's; vendor/ is byte-identical; it
carries exactly one support archive. Content differs from the
previous live tree in exactly five files:
runtime/boxedwine64.wasm, runtime/boxedwine64.js,
runtime/BUILD_INFO.json, app.mjs + runtime/index.html (release-id
substitution only, 043410782ae7 -> 8ac3e6984eee, verified line by
line). The swap was then made (winbox ->
winbox-prev-20261106-0552, winbox-next -> winbox) and the live tree
verified over public HTTPS from off the box: BUILD_INFO serves
git_sha `8017fdabc…`, wasm `9b51d601…`, git_dirty_files 0; the shell
answers 200; the runtime index answers 200; range requests on the
wasm and on the support archive answer 206.

## Certifications against the live site

Protocol identical to the previous deploy measurements: brand-new
browser profile per boot, the deskpin-anchored harness session
(session=1) at the public URL, the launcher's restart budget
pre-exhausted (bwretry=2) so no start is a rescue, 900 s window,
full console captured and classified OFFLINE under the hardened
FD-47 rule. Evidence: `test-results/deploy-batch8-20261106/`
(live-driver.mjs preserved there, a copy of the batch-7 deploy's
driver).

- x86: **5/5 certified**, handle `0x10052` on every boot, 0 restarts,
  0 rescues, 0 false-ready. Per-boot certified times 20.6 / 20.4 /
  20.4 / 19.4 / 20.6 s; median 20,445 ms (box load 0.6 → 2.0 across
  the series — the quietest certification series yet).
- x64: **3/3 certified**, handle `0x10048` on every boot, 0 restarts,
  0 rescues. Per-boot 86.3 / 84.0 / 82.2 s; median 84,016 ms
  (box load 1.0 → 5.7; batch 7's x64 median was ~196 s at load ~8 —
  no verdict rests on a duration).
- Console census: x86 boots 0 BW64-FAIL and 25 BW64-INFO served-link
  lines (5 boots x exactly 5); x64 boots 0 BW64-FAIL and 15 (3 x 5)
  — the standing expectation, exact.
- FD-115 (wedged first-visit start) tally: 5 more clean first-visit
  32-bit starts with zero rescues moves the running count to
  **1 wedged in 45** (was 1 in 40). FD-115 stays open on its closing
  terms; the surfacing rules that close it on the branch ship with
  batch 9, not this one.

## Consumer Chromium series against the live site

6 runs (`demo-matrix.mjs`, DEMO=chromium, live URL), **6/6 mapped,
all exit 0, pageErrors 0 on every run**. Frames presented per run:
10 / 7 / 12 / 11 / 13 / 13. Combined consoles: 0 BW64-FAIL, 30
BW64-INFO served-link lines (exactly 5 per session), and no
delivered guest memfault in any run.

## Shipped-dependencies check (common-dependencies) — LIVE PASS

The check owed from batch 7 is discharged here. One run against
the live tree (`WINE_DIST_DIR=/home/ubuntu/kn0tzer/winbox`, the
harness serving the live files): **PASS common-dependencies
(184,489 ms), SUITE RESULT 1 passed, 0 failed.** The harness
verified the served build before running: the test's own preserved
artifacts (`test-results/deploy-batch8-20261106/common-deps-live/`)
record BUILD_INFO git_sha `8017fdabc` / wasm `9b51d601…` and a
verified runtime code hash. This is the first live pass of this
check since the batch-7 gate; batch 7's three attempts all failed
on shared-box contention with no product fault shown.

## Godot capture — not run in this deploy

No Godot capture was run against the live batch-8 build. Nothing in
batch 8 touches the shader path, so FD-55 / FD-72 (and the CL-43
pipeline clause, still not discharged on its exact terms) are
unchanged from batch 7's account; a fresh capture would re-measure
the same known blocker. The next Godot re-check belongs with a
batch that changes the graphics path.

## Rollback

`winbox-prev-20261106-0552` (batch 7, git_sha `4006ddb58`) is
retained as the single rollback. The older
`winbox-prev-20261105-1244` (batch 6, git_sha `49c67137d`,
identity verified before removal) was removed, per the standing
keep-only-the-newest rule, after the live checks completed.

## Push

This record is committed on integ and the new integ tip is merged
into the current remote master via the established temp-worktree
procedure and pushed to origin (github.com/Kn0tzer/Winbox), with the
held-back workflow-file commit `69c800476` excluded as always; the
push is verified by reading the remote back.
