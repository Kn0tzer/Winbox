# Deploy record — batch 9 (2026-11-06)

Batch 9 (merge record `docs/reports/merge-batch9-20261106.md`, integ tip
`d50bcc02f`) is deployed to the live site. Gate figures, from the merge
record (re-read from disk by this deploy lane): the gate passed on code
tip `d2c08c524` — 43 passed, 1 known-blocked (input-gamepad, FD-188 /
FD-189), jitbench passing on focused re-run; wasm sha256
`92b36180e9b3469b07014b91cf0054e4f50e1b119b2881e6ca1a85819d27bfdf`
(5,722,692 bytes), js sha256
`94669195ae2041379071bb46b9b6ba8951b788494a01688f1f93051784692c5e`;
browser suite 44 tests (42 passed in the full run, jitbench passing on
focused re-run, input-gamepad known-blocked); guest contracts 5/5; boot
census both widths PASS; M6 two-build proof passes on the adapted
standard (cacheable set plus wasm byte-identity); D3D11/D3D12
conformance reproduces the October baseline (11/12, 19/20); native
serial main 692/0, x64 116/0; run-native 71 PASS; JS 354/0; legacy
115/0/1 skip; citations 352 rows 0 drift. Register at merge: 189 FD
entries (numbers 1..192 except pre-existing gaps 46/56/57), closures
CL-01..CL-133 each exactly once. Closures this batch: FD-115 (CL-127),
FD-166 (CL-128), FD-165 (CL-129), FD-19 (CL-130), FD-170 (CL-131),
FD-173 (CL-126), FD-178 (CL-125), FD-186 (CL-132), FD-187 (CL-133).
FD-167/168/169/171 stay open; FD-24 stays open (FD-188/FD-189 filed as
its blockers).

Note on the merge lane: the merge record documents three gate stops (a
browser-build failure, two deterministic 64-bit fixture crashes plus a
32-bit boot wedge, and a 32-bit D2D1 crash), each fixed and re-gated
before the final gate passed. This deploy verified the merge from disk
instead of taking any narration: the integ tip, the merge record, the
register counts, and the gate's wasm hash were all re-read on the box
before any deploy step. Nothing in this record rests on the merge
lane's say-so.

## Build and staging

The merge lane stripped its generated state after its gate, so no gate
dist survived to stage. This deploy rebuilt the browser dist from the
integration tip (`tools/bw64-build.sh`, owner `deploy-batch9`) after
prepping the fresh tree with `winbox-wt-prep.sh` (23 links, pins OK on
the refreshed 20261106 store). Build log: "Runtime assets verified (13
files)", completeness 0 failures, 1 skip (the standing prefix64
built-artifact skip), FD-73 passes, "Browser export verified: 34 .mjs
syntax files; … dependency archive c4dbe35986a5 verified", "BUILD
COMPLETE".

The rebuild is product-identical to the merge gate's build: the wasm
is byte-identical to the gate's
(`92b36180e9b3469b07014b91cf0054e4f50e1b119b2881e6ca1a85819d27bfdf`,
5,722,692 bytes), because the only commit between the gate build's
tree (`d2c08c524`) and the integ tip (`d50bcc02f`) adds the merge
record document alone. The final dist stamp is git_sha
`d50bcc02f519740b0af20da961d15c32c783e206`, git_dirty_files 0.
Re-verified against the rebuilt dist: exactly one support archive in
the dist, support-c4dbe35986a5; JS unit suite against the rebuilt dist
354 passed, 0 failed — identical to the merge gate. The gate's
remaining figures (native, run-native, legacy, citations,
silent-failure baseline, browser suite) are the merge record's and
were not re-run, per the batching rule — except common-dependencies,
re-run against the live build below.

Staging: the rebuilt dist was copied to winbox-next and verified
byte-identical to the dist; its file set is identical to the previous
live tree's; vendor/ is byte-identical; it carries exactly one support
archive. One deviation from batch 8's copy, recorded: the dist carries
4 slang files as symlinks into the shared store (interface.d.ts,
slang-wasm.js, slang-wasm.wasm, third-party-notices); the staged copy
was made with symlinks resolved (`cp -rL`) so the live tree holds real
files, as the previous live tree did — a symlink into the store would
not serve from the site's mount. Content differs from the previous
live tree in exactly nine files: runtime/boxedwine64.wasm,
runtime/boxedwine64.js, runtime/BUILD_INFO.json, app.mjs +
runtime/index.html (release-id substitution only, 8ac3e6984eee ->
7fefdedbfed4, verified line by line), runtime/wine32.zip +
runtime/wine64.zip (the re-cut 20261106 pins, `12895265…` /
`431d61cb…`, verified against the tree's pins),
runtime/wine32-font-provenance.json (one line: the wine64 pin
reference), runtime/wine64-launcher.js (the FD-115/FD-166
pre-handshake surfacing rules: +111/−1 lines; the single removed line
is the deliberate behavior change — the no-progress dump now runs the
pre-handshake stall check before returning). The swap was then made
(winbox -> winbox-prev-20261106-2237, winbox-next -> winbox) and the
live tree verified over public HTTPS from off the box: BUILD_INFO
serves git_sha `d50bcc02f…`, wasm `92b36180…`, git_dirty_files 0; the
shell answers 200; the runtime index answers 200; range requests on
the wasm and on the support archive answer 206.

## Certifications against the live site

Protocol identical to the previous deploy measurements: brand-new
browser profile per boot, the deskpin-anchored harness session
(session=1) at the public URL, the launcher's restart budget
pre-exhausted (bwretry=2) so no start is a rescue, 900 s window, full
console captured and classified OFFLINE under the hardened FD-47 rule.
Evidence: `test-results/deploy-batch9-20261106/` (live-driver.mjs
preserved there, a copy of the batch-8 deploy's driver).

- x86: **5/5 certified**, handle `0x10052` on every boot, 0 restarts,
  0 rescues, 0 false-ready. Per-boot certified times 18.8 / 18.6 /
  19.7 / 18.8 / 18.4 s; median 18,761 ms (batch 8: 20,445 ms — no
  verdict rests on a duration).
- x64: **3/3 certified**, handle `0x10048` on every boot, 0 restarts,
  0 rescues. Per-boot 84.4 / 116.9 / 88.1 s; median 88,095 ms (batch
  8: 84,016 ms; boot 2 ran at load ~6 — no verdict rests on a
  duration).
- Console census: x86 boots 0 BW64-FAIL and 25 BW64-INFO served-link
  lines (5 boots x exactly 5); x64 boots 0 BW64-FAIL and 15 (3 x 5)
  — the standing expectation, exact.
- FD-115 (wedged first-visit start) tally: 5 more clean first-visit
  32-bit starts with zero rescues moves the running count to
  **1 wedged in 50** (was 1 in 45). FD-115 is closed with this batch
  (CL-127): the pre-handshake surfacing rules that close it are now
  live, so a future wedged start is reported as failed instead of
  sitting as an endless wait.

## Consumer Chromium series against the live site

6 runs (`demo-matrix.mjs`, DEMO=chromium, live URL), **6/6 mapped,
all exit 0, pageErrors 0 on every run**. Frames presented per run:
12 / 15 / 8 / 11 / 12 / 13. Combined consoles: 0 BW64-FAIL, 30
BW64-INFO served-link lines (exactly 5 per session), and no delivered
guest memfault in any run. Console errors 181 per run, the same shape
as batch 8 (179–183) — routine Chromium console noise, not a product
signal.

## Shipped-dependencies check (common-dependencies) — LIVE PASS

One run against the live tree (`WINE_DIST_DIR=/home/ubuntu/kn0tzer/winbox`,
the harness serving the live files): **PASS common-dependencies
(262,962 ms), SUITE RESULT 1 passed, 0 failed.** The harness verified
the served build before running: the test's own preserved artifacts
(`test-results/deploy-batch9-20261106/common-deps-live/`) record
BUILD_INFO git_sha `d50bcc02f` / wasm `92b36180…`.

## Godot capture — not run in this deploy

No Godot capture was run against the live batch-9 build. Nothing in
batch 9 touches the shader path, so FD-55 / FD-72 (and the CL-43
pipeline clause, still not discharged on its exact terms) are
unchanged from batch 8's account; a fresh capture would re-measure the
same known blocker. The next Godot re-check belongs with a batch that
changes the graphics path.

## Rollback

`winbox-prev-20261106-2237` (batch 8, git_sha `8017fdabc`) is retained
as the single rollback. The older `winbox-prev-20261106-0552` (batch 7,
git_sha `4006ddb58`, identity verified before removal) was removed,
per the standing keep-only-the-newest rule, after the live checks
completed.

## Push

This record is committed on integ and the new integ tip is merged into
the current remote master via the established temp-worktree procedure
and pushed to origin (github.com/Kn0tzer/Winbox), with the held-back
workflow-file commit `69c800476` excluded as always; the push is
verified by reading the remote back.
