# Deploy 2026-11-03 — batch 3 (21 composed lanes)

Deployed by the deploy+push lane. Live site serves git_sha
caadab5ad / wasm f45abfb4 (verified over public HTTPS from
off the box: the BUILD_INFO stamp matches the staged dist
byte-for-byte, wasm 5,645,490 bytes). Rollback:
kn0tzer/winbox-prev-20261103-0023 (the 8ac504abf build);
the older rollback (winbox-prev-20261102-2119, the
9e37e5640 build) was pruned per the newest-only rule
after the live checks completed. Master untouched as a
working branch; the personal game archive untouched.

## What shipped

The composed batch at integ tip caadab5ad (merge record
merge-batch3-20261102.md): 21 lane branches composed,
the register settled at 111 FD entries with closures
CL-01…CL-66 sequential and audit-clean. It carries the
FD-89 Godot sibling classes (CL-43), FD-90 (CL-66),
FD-64 (CL-44), FD-05 (CL-45), FD-78 (CL-46), FD-92
(CL-47), FD-82 (CL-48), FD-83/FD-84 (CL-49/50), FD-104
(CL-51), FD-101 (CL-52), FD-105/106/107 (CL-53/54/55),
FD-97 (CL-56), FD-103 (CL-57), FD-93 (CL-58), FD-94
(CL-59), FD-96 (CL-60), FD-98 (CL-61), FD-99 (CL-62),
FD-100 (CL-63), FD-86 (CL-64) and FD-23 (CL-65), the
merge's two composition decisions (FD-95 settled; the
positioned vectored write's wrong-mode answer aligned
to −EBADF), the FD-88 and FD-72 register narrowings,
and the FD-85 conviction record (FD-85 stays open,
re-gated on FD-108). No deploy composition commits
were needed.

## Build and staging — one rebuild, stated exactly

The merge lane stripped the integration tree's
generated state after its gate, so no gate dist
survived to stage. This deploy rebuilt the browser
dist from the integration tip (tools/bw64-build.sh)
after restoring the runtime assets through the clean
bootstrap path ONLY: bootstrap.py --local from the
retained runtime-assets-20261103 archive — the archive
the merge gate used — "Restored and verified 13
runtime assets"; bootstrap.py --check exit 0 ("Runtime
assets verified (13 files)", the FD-73 line agreeing
the retained archive matches the file pins). No manual
restage was used.

The rebuild is product-identical to the merge gate's
build: every commit between the gate build's stamp
(129462a63) and the tip (caadab5ad) touches only
documentation and test files (docs/known-gaps/
vulkan.md, the merge record, web/tests/…) — verified
by diff. The one figure that therefore differs from
the merge record is the BUILD_INFO stamp itself:
git_sha caadab5ad, git_dirty_files 0, against the
gate's 129462a63. Every behavioural gate figure was
either re-verified against the rebuilt dist or stands
as the merge record's own: the build log carries the
literal "Browser export verified" + BUILD COMPLETE;
exactly one support archive in the dist,
support-c4dbe35986a5 (sha256 c4dbe35986a5edcff6646cb
8b4f63cc7660e6e1e6dcda6546454db77d241729f), the
deterministic FD-53 pack, build-verified and
byte-identical to the previous live build's;
check_runtime_completeness.py 0 failures, 1 skip (the
standing prefix64 built-artifact skip) with FD-53's
and FD-73's checks active and passing; JS unit suite
against the rebuilt dist 352 passed, 0 failed —
identical to the merge gate. The gate's remaining
figures (native main 675/675, x64 83/83, JS legacy
115/0, run-native 62 PASS, citations 0 drift, clean
gate boots) are the merge record's and were not
re-run, per the batching rule.

Staging: the rebuilt dist was copied to winbox-next
and verified byte-identical to the dist (recursive
diff empty); its file set is identical to the
previous live tree's; vendor/ is byte-identical; it
carries exactly one support archive. Content differs
from the previous live tree in exactly eight files:
runtime/boxedwine64.wasm, runtime/boxedwine64.js and
spirvfix.mjs (the batch's emulator and fixup-layer
work), runtime/prefix64.zip (FD-86's staged Arial
replacement — the desktop-pin-provenance.json prefix
pin follows it), runtime/BUILD_INFO.json, and app.mjs
+ runtime/index.html (release-id substitution only,
verified line by line).

## First-visit starts — measured through the public route

Protocol identical to the previous deploy measurements:
brand-new browser profile per boot, the deskpin-anchored
harness session (session=1) at the public URL, the
launcher's restart budget pre-exhausted (bwretry=2) so
no start is a rescue, 900 s window, full console captured
and classified OFFLINE under the hardened FD-47 rule.
Evidence: test-results/deploy-batch3-20261103/
(live-driver.mjs preserved there).

- x86 n=5: certified 5/5. Handle 0x10052 every time,
  certified-ready 20.7–25.1 s (median 22.9 s), zero
  restarts, zero false-ready, zero rescues.
- x64 n=3: certified 3/3, handle 0x10048 on every
  boot, certified-ready 119.5–184.0 s (median
  150.2 s), with the same zero counters. (The series
  interleaved with two other lanes' browser builds
  through the probe-slot/build interlock; the second
  boot waited in the slot queue behind them. The
  figures are reported as measured.)
- Console census across all 8 boots (4,232 lines):
  24 explorer OPENED spawn signals, 0 session
  restarts, 0 BW64-FAIL lines of any kind, 0 memory
  faults. Link lines: 40 BW64-INFO "hard-link …
  outcome=served by shared backing path" lines —
  exactly 5 per boot on BOTH widths (the x86 le32d4
  set and the x64 le64 fontconfig set), FD-81's
  behaviour intact on the shipped build.

## The wedged-start entry — this deploy's count

The register's open wedged-start entry (filed on
branch lane-wedge-incidence from the previous deploy's
single wedged boot; it renumbers off FD-103 at batch-4
composition under the merge record's first-composed-
wins rule) counts first-visit x86 starts on shipped
builds. This deploy adds five: 5/5 certified, no
wedge, consoles clean (above). Running tally on
shipped builds: the batch-2 deploy's 1 wedged in its
5, the filing lane's 10/10 clean, this deploy's 5/5
clean — 1 wedged in 20 measured starts. The entry
stays open on its stated terms; nothing in this
deploy's series reproduces it.

## Consumer series — Chromium through the public website path

Six uninstrumented runs of the standard demo path
against the live site (demo-matrix, DEMO=chromium,
BW_TEST_BASE_URL = https://kn0tzer.work.gd/winbox/,
fresh profile per run, 900 s mapped window). Evidence:
test-results/deploy-batch3-20261103/
chromium-consumer-a1…a6 (+ .log files, consumer-tally.txt).

- 6/6 runs MAPPED, zero stalls. Every run: stage
  "mapped", overlay cleared, click-to-mapped 38.9,
  41.1, 48.2, 40.5, 38.3, 32.9 s, frames presented
  20, 7, 7, 9, 10, 11, 99.16% non-dark / 2,265
  distinct colors (the standing success pixel
  signature), the input script completed, 0 page
  errors, run exit 0.
- Link census across the six consumer consoles
  (3,260 lines): 0 BW64-FAIL lines of any kind, 0
  memory faults, and 30 BW64-INFO served-link
  lines — exactly 5 per session.

## The FD-89 proof owed by CL-43 — attempted twice in vivo; NOT discharged

CL-43's closing terms owe the two pipelines' creations
succeeding in vivo on a shipped build. This deploy ran
the proof twice. Protocol (both attempts): the
standard demo path against the live site (the warmed
archive), vktrace on with vkd3d shader dumps enabled —
the FD-70 capture configuration — with a 3,300 s
deadline. Evidence: test-results/deploy-batch3-
20261103/godot-proof-attempt1/ and godot-proof/ (full
consoles preserved), godot-driver-out-attempt1.log
and godot-driver-out.log.

- Attempt 1: Godot banner at 114.5 s. The demo's
  window never mapped; the capture driver's launch
  wait (the page log's 'first window mapped', 1,200 s)
  expired and the driver exited (WARM_CAPTURE_EXIT=1)
  before the run's deadline phase. Its console (7,570
  lines) carries 0 vkCreateShaderModule lines and 0
  BW64-FAIL lines, and its final metrics windows show
  the guest alive and executing at the expiry
  (41–147 syscalls per 500 ms window, up to ~2.5 s of
  syscall time per window).
- Attempt 2 (immediate retry, same configuration):
  banner at 181 s; the same launch wait expired the
  same way (WARM_CAPTURE_EXIT=1). Console 7,280
  lines, 0 vkCreateShaderModule, 0 BW64-FAIL; the
  guest likewise alive and executing at the expiry
  (90–140 syscalls in the final windows).

Verdict, stated plainly: the owed clause is NOT
discharged by this deploy. Neither run reached the
demo's window, so no shader-module or pipeline
evidence was obtained in either direction: whether
the two modules this batch newly serves (the
2,340-byte module end to end, the 369,400-byte
module's SampledBuffer class) create in vivo, which
pipeline attempts remain, and what kills them, are
all still unmeasured. The two pipelines still gate
on FD-88 + FD-72, and CL-43's clause stays owed to a
future deploy's run. The same account is recorded as
an update block in the register's FD-89 entry. The
box's 1-minute load across the two attempts ranged
~3 to ~12.7 with other lanes' builds and suites
active throughout; the harness's launch wait, not
the guest, is what expired in both attempts, and no
mechanism is claimed beyond that. Two attempts were
made and no more.

## Push to GitHub

This deploy continues the standing every-deploy push
rule. The push is executed as the final step of this
same deploy, after this record: the integration tip
(including this record and the FD-89 register update)
is merged into the current remote master tip
(47f8932dc at push start) through a detached temporary
worktree on the box (--no-ff), pushed to origin
(github.com/Kn0tzer/Winbox) with the box's existing
GitHub credential, and verified by reading the remote
back. The held-back local commit 69c800476 (the
workflow-file edit the box credential cannot publish)
is not an ancestor of the integration branch and does
not ride; verified before the push against both the
remote tip and the integration branch, and again by
read-back after it. The verified remote tip resulting
from the push is carried in the deploy lane's delivery
and the coordinator's state, since this record is
itself part of what is pushed; local branches and
history are otherwise exactly as they were.

## Incidents

- The SSH transport carrying the x64 certification
  driver was closed by the remote host mid-series
  (after boot 1). The driver itself continued on the
  box under nohup and completed the series; every
  figure above was read from its preserved results
  files, and no figure rests on a swallowed output.
- Box 1-minute load ranged ~1.3 to ~12.7 across the
  measurement windows (other lanes' builds, native
  suites and probes active throughout, including the
  two builds the x64 series queued behind). The
  figures are reported as measured.

## State left behind

Deployment record and the FD-89 register update
committed on integ on top of caadab5ad. Integ tree
stripped (dist, web/runtime, project/linux/Build,
project/emscripten/Build, __pycache__ gone — after the
live checks, which read the local dist BUILD_INFO as
their report stamp) and clean; the deploy evidence
stays on disk under test-results/deploy-batch3-
20261103/ (gitignored, as all deploy evidence). Live
disk at deploy end: 30G free before the strip.
