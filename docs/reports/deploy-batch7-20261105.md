# Deploy record — batch 7 (2026-11-05)

Batch 7 (merge record `docs/reports/merge-batch7-20261104.md`, integ tip
`4006ddb58`) is deployed to the live site. Gate figures, from the merge
record: build verified from `runtime-assets-20261104` only, BUILD_INFO
git_sha `c2e6b11e4` dirty 0, wasm sha256
`c0af957ae56bc11055714e62e37055218ccbc5e8ac775bded0d229ef0c82577a`,
exactly one support archive at pin `c4dbe35986a5`; native serial main
686/0, x64 111/0; run-native 69 PASS; JS 353/0; legacy 115/0; citations
352 rows 0 drift; browser suite 31 passed / 1 failed (jitbench,
adjudicated a load-gated runner timeout, not a regression). Register at
merge: 161 FD entries, CL-01…CL-118.

## How this deploy ran

The first deploy attempt was stopped mid-work by a coordinator stop.
Before the stop it had completed staging and the rename-swap, run both
certification series, and launched the consumer-series driver. This
completion pass adopted those steps only after re-verifying each one
from disk; nothing was taken on the stopped attempt's narration.

Adoption checks, all fresh reads: live `BUILD_INFO.json` fetched on-box
and off-box over HTTPS — git_sha `4006ddb58`, wasm sha256 `c0af957a…`
(byte-identical to the gate wasm, `cmp` clean), git_dirty_files 0,
built 2026-11-05T12:35:48Z. Staging evidence in
`test-results/deploy-batch7-20261104/`: bootstrap.log (13 assets
restored/verified from `runtime-assets-20261104`), completeness 19 PASS
/ 0 failures / 1 skip, vendor-diff, fileset-diff and staging-diff all
empty, exactly one support archive in the live runtime. Certification
logs re-read from disk (figures below). The consumer driver (bash,
nohup) was still alive and was adopted mid-run; every run was verified
individually from its preserved log and console files, cross-checked
against the box wall clock.

One evidence-channel failure during this deploy, recorded for the
standing hazard file: a background poll delivery reported the consumer
series fully complete with per-run timestamps when disk showed only
the first run finished. The claim was rejected, the reporting channel
was treated as unreliable for the rest of the deploy, and every figure
in this record was read from preserved files on disk.

## Certifications against the live site

- x86: **5/5 certified**, handle `0x10052` on every boot, 0 restarts,
  0 rescues, 0 false-ready. Per-boot certified times 21.6 / 21.6 /
  22.6 / 23.8 / 26.6 s; median 22,557 ms (box load 1.9 → 5.2 across
  the series).
- x64: **3/3 certified**, handle `0x10048` on every boot, 0 restarts,
  0 rescues. Per-boot 196.0 / 196.4 / 172.7 s; median 195,995 ms
  (box load ~8).
- Console census: x86 boots 0 BW64-FAIL and 25 BW64-INFO served-link
  lines (5 boots × exactly 5); x64 boots 0 BW64-FAIL and 15 (3 × 5) —
  the standing expectation, exact.
- FD-115 (wedged first-visit start) tally: 8 more clean first-visit
  starts with zero rescues moves the running count to **1 wedged in
  40** (was 1 in 35). FD-115 stays open on its closing terms.

## Consumer Chromium series against the live site

6 runs (`demo-matrix.mjs`, DEMO=chromium, live URL), **6/6 mapped,
all exit 0, pageErrors 0 on every run**. Frames presented per run:
15 / 10 / 6 / 10 / 3 / 8 — single runs under widely varying load (the
series ran from load ~3 to ~24; run a3 waited ~35 minutes in the
probe queue behind sibling lanes' builds, and a5/a6 ran at load ~24,
which is where the low frame counts come from; no verdict rests on a
frame count). Combined consoles: 0 BW64-FAIL, 30 BW64-INFO
served-link lines (exactly 5 per session), and no delivered guest
memfault in any run — the unattributed-memfault question from earlier
batches does not recur here.

## Shipped-dependencies check (common-dependencies) — no live pass claimed

Three attempts were made to re-run the shipped-dependencies test
against the live tree (`WINE_DIST_DIR=/home/ubuntu/kn0tzer/winbox`;
the harness self-verified the served BUILD_INFO as git_sha
`4006ddb58` / wasm `c0af957a…` in every attempt). All three failed on
shared-box contention mechanics; none produced a product fault
signature (0 BW64-FAIL in every attempt console):

- Attempt 1 (595,203 ms, box load ~17–20): the guest booted and ran —
  continuous metrics and syscall flow throughout — but no applet
  result line appeared within the test's 180 s window.
- Attempt 2 (949,197 ms, box load ~28–31): the guest boot itself
  exceeded the test's 600 s ready-wait.
- Attempt 3 (1,260,499 ms, box load ~10–12): the test's overall
  1,260 s cap was consumed mostly by a ~15-minute wait for a test
  slot behind sibling lanes; the session had only ~5 minutes before
  the cap killed the probe.

All three attempts are preserved in the evidence directory
(`common-deps-attempt1/`, `common-deps-attempt2/`,
`common-deps-attempt3/`). The identical shipped build passed this test
unloosened in the batch-7 merge gate (see the merge record's browser
suite account). **No live pass is claimed for batch 7**; the live
re-check remains owed when the box is quieter. This does not block the
deploy: the live product itself is proven above by the certification
series and the consumer series against the same live tree.

## Godot capture against the live site (CL-43 account)

Attempt 1 never reached the guest: the runner's build-idle wait has a
600 s cap and a sibling lane's build held the build lock past it
(preserved as `godot-attempt1.log`). Attempt 2 was launched in a
build-idle gap and ran the full course against the live site. Its
summary records the shipped build stamp (git_sha `4006ddb58`, wasm
`c0af957a…`).

- Outcome: **deadline** — the guest was alive and progressing for the
  whole 1,200 s warm window (elapsed 1,207.6 s), no trap captured.
- Godot banner at 394.8 s, window mapped at 544.6 s (batch 6: 99 s /
  145 s on a quiet box; this run shared the box at load ~10 — no
  verdict rests on a duration).
- 196 `vkCreateShaderModule` calls; the only refusals were exactly
  two, both the known 5,704-byte module (hash `0b3b4d8cc939485a`)
  refused with the FD-72 helper-invocation explanation
  (`spirv-builtin-helper-invocation`, result −8). 6 graphics-pipeline
  and 68 compute-pipeline creation calls. No swapchain creation, no
  next-image acquire, **cumVkPresents 0** — no first frame, as in
  batches 5 and 6. 0 BW64-FAIL, no memfault.
- CL-43's pipeline clause is **again not discharged on its exact
  terms**: the two named modules (369,400 B and 2,340 B) were never
  created in this run either (0 occurrences each) — the guest reaches
  the FD-72 module first, is refused, and cycles. FD-55 and FD-72
  remain open; FD-72 remains the blocker in front.

Evidence: `test-results/deploy-batch7-20261104/godot-proof/`
(`summary.json`, `status.jsonl`, `console.txt`, warm screenshots).

## Rollback

`winbox-prev-20261105-1244` (batch 6, git_sha `49c67137d`) is retained
as the single rollback. The older `winbox-prev-20261104-1038` (batch
5, git_sha `19babd34a`, identity verified before removal) was removed,
per the standing keep-only-the-newest rule.

## Push

This record is committed on integ and the new integ tip is merged into
the current remote master via the established temp-worktree procedure
and pushed to origin (github.com/Kn0tzer/Winbox), with the held-back
workflow-file commit `69c800476` excluded as always; the push is
verified by reading the remote back.
