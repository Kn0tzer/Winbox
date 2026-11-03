# Deploy 2026-11-03 — batch 4 (six composed lanes)

Deployed by the deploy+push lane. Live site serves git_sha
600da7651 / wasm c274128e (verified over public HTTPS from
off the box: the BUILD_INFO stamp matches the staged dist
byte-for-byte, wasm 5,664,763 bytes, git_dirty_files 0).
Rollback: kn0tzer/winbox-prev-20261103-0521 (the caadab5ad
build); the older rollback (winbox-prev-20261103-0023, the
8ac504abf build) was pruned per the newest-only rule after
the live checks completed. Master untouched as a working
branch; the personal game archive untouched.

## What shipped

The composed batch at integ tip 600da7651 (merge record
merge-batch4-20261103.md): six lane branches composed on
the batch-3 deploy tip ff31d5bfe — lane-fd87loss,
lane-wedge-incidence, lane-fd105-108, lane-fd109-112,
lane-fd113-115, lane-fd116-117. The register settled at
123 FD entries with closures CL-01…CL-81 sequential and
audit-clean. It carries the x87 stale-cache fix closing
FD-108 (CL-67), FD-85 closed as CL-68 on the gate's
composed common-dependencies PASS, the wedged-start
filing settled as FD-115 (open, incidence-counted), the
message quartet closures CL-69…CL-72, the datagram four
FD-116…FD-119 (CL-73…CL-76), FD-120…FD-122
(CL-77…CL-79), FD-123 and FD-124 (CL-80, CL-81), the
parallel-worker crash filing folded into FD-32 as an
addendum, and the gate's one composition fix (194876c51,
the FD-97/FD-116 zeroCount interaction). FD-125 and
FD-126 stay open for the sibling lane-fd118-119; FD-88,
FD-72 and FD-74 are unchanged. No deploy composition
commits were needed.

## Build and staging — two incidents, stated exactly

The merge lane stripped the integration tree's generated
state after its gate, so no gate dist survived to stage.
This deploy rebuilt the browser dist from the
integration tip (tools/bw64-build.sh) after restoring
the runtime assets through the clean bootstrap path
ONLY: bootstrap.py --local from the retained
runtime-assets-20261103 archive — the archive the merge
gate used — "Restored and verified 13 runtime assets";
bootstrap.py --check exit 0 ("Runtime assets verified
(13 files)"). No manual restage was used.

Incident 1: the first build failed at the export
staging step — stage-dependencies.py FileNotFoundError
on tools/dxvk/build.w32/src/d3d9/d3d9.dll, because the
merge lane's strip had also removed the tree's
generated payload inputs. Remedy (the rehearsed one,
used by sibling lanes before): the pinned-vintage
payload inputs were copied from the preserved tree
/home/ubuntu/wt-fd38 — tools/dxvk/build.w32,
tools/dxvk/build.w64, and tools/rootfs64/{cjk-font,
gecko, gstreamer32, gstreamer64, openal}. The staged
support archive then verified exactly against the pin,
proving the copies are the pinned vintage. The copies
are generated state and were stripped again at the end
of this deploy.

Incident 2: the next build stamped git_dirty_files: 1,
caused solely by this lane's own untracked scratch
driver copy (web/tests/deploy-batch4-godot.mjs) sitting
in the tree. It was removed and the build re-run; the
final dist stamp is git_sha
600da7651afd53ba6d933b158325c7b297f94882,
git_dirty_files 0.

The rebuild is product-identical to the merge gate's
build: the wasm is byte-identical to the gate's
(c274128e7f0580c1500427eeb70d458aedf4d3441ae43aed2314
c2e236827971, 5,664,763 bytes), because every commit
between the gate build's code tip (194876c51) and the
integ tip (600da7651) touches only
docs/defect-register.md, the merge record, and
web/tests/suite/known-silent.json — verified by diff.
Re-verified against the rebuilt dist: the build log
carries the literal "Browser export verified"; exactly
one support archive in the dist, support-c4dbe35986a5
(sha256 c4dbe35986a5edcff6646cb8b4f63cc7660e6e1e6dcda
6546454db77d241729f), the deterministic FD-53 pack,
byte-identical to the previous live build's;
check_runtime_completeness.py 0 failures, 1 skip (the
standing prefix64 built-artifact skip), 19 PASS; JS
unit suite against the rebuilt dist 352 passed, 0
failed — identical to the merge gate. The gate's
remaining figures (native main 675/675 run twice, x64
95/95 after the composition fix, JS legacy 115/0,
run-native 62 PASS, citations 352 rows 0 drift,
silent-failure baseline 134, common-dependencies PASS
unloosened in 352 s with OPENAL: RESULT 0) are the
merge record's and were not re-run, per the batching
rule — except common-dependencies, re-run against the
live build below as FD-85's live confirmation.

Staging: the rebuilt dist was copied to winbox-next
and verified byte-identical to the dist (recursive
diff empty); its file set is identical to the previous
live tree's; vendor/ is byte-identical; it carries
exactly one support archive. Content differs from the
previous live tree in exactly five files:
runtime/boxedwine64.wasm, runtime/boxedwine64.js,
runtime/BUILD_INFO.json, and app.mjs +
runtime/index.html (release-id substitution only,
104bf4223e7a -> c4977e23e8d5, verified line by line).
The swap was then made and the live tree verified over
public HTTPS: BUILD_INFO serves git_sha 600da7651…,
wasm c274128e…, git_dirty_files 0; the shell answers
200; a wasm range request answers 206.

## First-visit starts — measured through the public route

Protocol identical to the previous deploy measurements:
brand-new browser profile per boot, the deskpin-anchored
harness session (session=1) at the public URL, the
launcher's restart budget pre-exhausted (bwretry=2) so
no start is a rescue, 900 s window, full console captured
and classified OFFLINE under the hardened FD-47 rule.
Evidence: test-results/deploy-batch4-20261103/
(live-driver.mjs preserved there).

- x86 n=5: certified 5/5. Handle 0x10052 every time,
  certified-ready median 21,679 ms, zero restarts,
  zero false-ready, zero rescues.
- x64 n=3: certified 3/3, handle 0x10048 on every
  boot, certified-ready median 110,105 ms, with the
  same zero counters.
- Console census across all 8 boots (4,075 lines):
  0 BW64-FAIL lines of any kind, and 40 BW64-INFO
  "hard-link … outcome=served by shared backing path"
  lines — exactly 5 per boot on BOTH widths, FD-81's
  behaviour intact on the shipped build.

## The wedged-start entry (FD-115) — this deploy's count

FD-115 counts first-visit x86 starts on shipped builds.
This deploy adds five: 5/5 certified, no wedge,
consoles clean (above). Running tally on shipped
builds: the batch-2 deploy's 1 wedged in its 5, the
filing lane's 10/10 clean, the batch-3 deploy's 5/5
clean, this deploy's 5/5 clean — 1 wedged in 25
measured starts. The entry stays open on its stated
terms; nothing in this deploy's series reproduces it.

## Consumer series — Chromium through the public website path

Six uninstrumented runs of the standard demo path
against the live site (demo-matrix, DEMO=chromium,
BW_TEST_BASE_URL = https://kn0tzer.work.gd/winbox/,
fresh profile per run, 900 s mapped window). Evidence:
test-results/deploy-batch4-20261103/
chromium-consumer-a1…a6 (+ .log files, consumer-tally.txt).

- 6/6 runs MAPPED, zero stalls, every run exit 0.
  Every run: stage "mapped", overlay cleared,
  click-to-mapped 43.4, 34.9, 46.6, 35.4, 34.5, 46.9 s,
  frames presented 9, 6, 7, 12, 9, 4, 99.16% non-dark /
  2,265 distinct colors (the standing success pixel
  signature), 0 page errors.
- Link census across the six consumer consoles
  (3,242 lines): 30 BW64-INFO served-link lines —
  exactly 5 per session.
- One BW64-FAIL line, stated exactly: run a4, at
  18.5 s, during the 32-bit winedevice/ntoskrnl load:
  "BW64-FAIL guest-memfault32 pid=71 tid=75
  addr=0xfff5f3b8 eip=0x1079af08 err=0 delivered=1
  count=1". The fault was delivered to the guest
  (delivered=1), occurred once, and execution
  continued immediately; the run went on to map and
  complete with the full success signature. Observed
  and unattributed — the same disposition as the
  guest memfault noted in FD-89's entry from the
  FD-70-batch deploy run; it is recorded here in
  full and is not a stall or a run failure.

## FD-85's live confirmation — common-dependencies on the shipped build

FD-85 closed at this batch's gate (CL-68) on the
composed tree's common-dependencies PASS. This deploy
re-ran the shipped test against the live build itself:
WINE_DIST_DIR=/home/ubuntu/kn0tzer/winbox, the
ProbeTest harness serving exactly the live tree and
self-verifying its BUILD_INFO hashes before running
(the run's preserved BUILD_INFO.json is the live
stamp: git_sha 600da7651…, wasm c274128e…, dirty 0).
Result: PASS unloosened, 269 s, suite result 1 passed /
0 failed, exit 0. Its console (2,388 lines, preserved
in test-results/suite-common-dependencies/) carries
both "MSHTML: RESULT 0" and "OPENAL: RESULT 0" and 0
BW64-FAIL lines. The audio defect's closing term is
therefore confirmed on the shipped bytes, not only on
the gate's composed tree. Deploy log:
test-results/deploy-batch4-20261103/common-deps.log.

## The FD-89 proof owed by CL-43 — retried in vivo with the extended launch wait

CL-43's closing terms owe the two pipelines' creations
succeeding in vivo on a shipped build. This deploy
retried the proof once, with the two changes batch 3's
account left open: the capture driver's launch wait
extended from 1,200 s to 2,700 s (the deploy driver
copy, web/tests/deploy-batch4-godot.mjs, differs from
godot-warm-capture-fd70.mjs in exactly that one line —
line 209 — verified by diff), and the run sequenced
into the quietest window of the deploy, launched after
the sibling FD-124 lane's build and suite had both
finished, at 1-minute load 2.24 (batch 3's attempts ran
at ~3 to ~12.7). Protocol otherwise identical: the
standard demo path against the live site (the warmed
archive), vktrace on with vkd3d shader dumps enabled —
the FD-70 capture configuration — with a 5,400 s
deadline. Evidence: test-results/deploy-batch4-
20261103/godot-proof/ (full console preserved),
godot-driver-out.log.

- Godot banner at 173.2 s. Vulkan instance creation
  and vkd3d-proton device initialisation completed
  (instance configuration at 208.8 s;
  vkd3d_init_device_caps at 210.4 s, the console's
  last Vulkan milestone).
- The demo's window never mapped; the extended launch
  wait (page.waitForFunction, 2,700,000 ms) expired
  and the driver exited (WARM_CAPTURE_EXIT=1). Its
  console (16,369 lines) carries 0 vkCreateShaderModule
  lines, 0 vkCreateGraphicsPipelines lines, and 0
  BW64-FAIL lines; the present count stayed 0
  throughout.
- The guest stayed alive and executing to the expiry
  (final metrics windows show 90–146 syscalls per
  500 ms window) but parked: after 1,000 s the console
  carries only metrics and thread dumps, and the
  thread dump at 2,675.3 s shows the guest process
  running with min_noprogress_ms 2,460,001 and its
  threads parked in ntdll.

Verdict, stated plainly: the owed clause is NOT
discharged by this deploy. The run reached device
initialisation and nothing beyond it, and never
reached a window, so whether the two modules this
lineage newly serves (the 2,340-byte module end to
end, the 369,400-byte module's SampledBuffer class)
create in vivo, which pipeline attempts remain, and
what kills them are all still unmeasured. The two
pipelines still gate on FD-88 + FD-72, and CL-43's
clause stays owed to a future deploy's run. What this
attempt does settle, on its evidence: neither the
1,200 s launch wait nor box load explains the missing
window — both were removed here and the outcome was
identical, with the guest parked rather than
progressing. One attempt was made and no second:
batch 3's two attempts at the shorter wait already
established the shape's reproducibility, and a repeat
of this configuration would re-measure the same
state. The same account is recorded as an update
block in the register's FD-89 entry.

## Push to GitHub

This deploy continues the standing every-deploy push
rule. The push is executed as the final step of this
same deploy, after this record: the integration tip
(including this record and the FD-89 register update)
is merged into the current remote master tip
(15f2e8cd57 at push start) through a detached temporary
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

- The SSH transport carrying several of this lane's
  commands was closed by the remote host mid-flight
  (the first build launch, the certification launch,
  the consumer launch, and two of the lane's waiter
  connections). Every affected job itself continued
  on the box under nohup; every figure above was read
  from its preserved results files, and no figure
  rests on a swallowed output.
- One of the lane's own waiters matched its own
  command line in a process check and reported a
  sibling lane's build as still running for ~20
  minutes after it had completed. The error was the
  lane's, cost only deploy time, and affected no
  measurement: the Godot retry above was launched
  only after a direct process read showed no build
  and no other probe running.
- Box 1-minute load across the measurement windows
  ranged ~1.3 to ~12.7 (other lanes' builds, native
  suites and probes active at various points,
  including the FD-124 lane's build and suite, which
  this deploy waited out before the Godot retry).
  The figures are reported as measured.

## State left behind

Deployment record and the FD-89 register update
committed on integ on top of 600da7651. Integ tree
stripped (dist, web/runtime, project/emscripten/Build,
__pycache__, the copied payload inputs
tools/dxvk/build.w32, tools/dxvk/build.w64,
tools/rootfs64/{cjk-font, gecko, gstreamer32,
gstreamer64, openal}, and the scratch driver copy
web/tests/deploy-batch4-godot.mjs gone — after the
live checks, which read the local dist BUILD_INFO as
their report stamp) and clean; the deploy evidence
stays on disk under test-results/deploy-batch4-
20261103/ (gitignored, as all deploy evidence). Live
disk at deploy end: 22G free before the strip.
