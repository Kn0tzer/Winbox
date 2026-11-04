# Deploy 2026-11-04 — batch 6 (the four-lane composition)

Deployed by the deploy+push lane. Live site serves git_sha
49c67137d / wasm 9abd2f24 (verified over public HTTPS from
off the box: the BUILD_INFO stamp matches the staged dist
byte-for-byte, wasm 5,684,501 bytes, git_dirty_files 0).
Rollback: kn0tzer/winbox-prev-20261104-1038 (the 19babd34a
build); the older rollback (winbox-prev-20261103-2249, the
600da7651 build) was pruned per the newest-only rule after
the live checks completed. Master untouched as a working
branch; the personal game archive untouched.

## What shipped

The composed batch at integ tip 49c67137d (merge record
merge-batch6-20261104.md): 17 cherry-picks from 4 lane
branches plus the audit and two gate commits, register
settled at 158 FD entries (numbers 1..161, pre-existing
gaps 46/56/57) with closures CL-01…CL-110 sequential and
audit-clean. It carries FD-153's drop-logging correction
(CL-104), FD-151's self-test target discharge (CL-105),
the fstatfs descriptor verdict (CL-106), the utimensat
flags verdict (CL-107), the resolver −ENOTDIR verdict
(CL-108), and the Direct3D 9 pair: FD-148's
unnormalizedCoordinates sampler flag carried end to end
(CL-109) and FD-149's 32-bit win32u.so backport rebuilt
with Vulkan support (CL-110), which also produced the
new retained runtime-assets archive below. New filings
carried forward open: FD-155…FD-161. Unchanged open
items: FD-115, FD-154, and the externally gated FD-72/
FD-74 (shader toolchain) and FD-88 (Chromium bind-group
texture-array counts). No deploy composition commits
were needed.

## Build and staging

The merge lane stripped the integration tree's generated
state after its gate, so no gate dist survived to stage.
This deploy rebuilt the browser dist from the
integration tip (tools/bw64-build.sh) after restoring
the runtime assets through the clean bootstrap path
ONLY: bootstrap.py --local from the retained
runtime-assets-20261104 archive — the archive the merge
gate used — "Restored and verified 13 runtime assets";
bootstrap.py --check exit 0 before the build (0 failures,
5 skips) and again after it (0 failures, 1 skip, the
standing prefix64 built-artifact skip). As in batches 4
and 5, the strip had also removed the tree's generated
payload inputs; the rehearsed remedy was used: the
pinned-vintage payload inputs were copied from the
preserved tree /home/ubuntu/wt-fd38 (tools/dxvk/build.w32,
tools/dxvk/build.w64, tools/rootfs64/{cjk-font, gecko,
gstreamer32, gstreamer64, openal}). The staged support
archive then verified exactly against the pin, proving
the copies are the pinned vintage. The copies are
generated state and were stripped again at the end of
this deploy.

The rebuild is product-identical to the merge gate's
build: the wasm is byte-identical to the gate's
(9abd2f249db98a215a04f85332fce0b6ecc81063afc932cd95
ffff37d20f195f, 5,684,501 bytes), because every commit
between the gate build's tree (869bc69ab) and the integ
tip (49c67137d) touches only the merge record and a
legacy test expectation — verified by diff. The final
dist stamp is git_sha
49c67137da7d8d1949e6923c34fc09f5a63aecf2,
git_dirty_files 0. Re-verified against the rebuilt
dist: the build log carries the literal "Browser
export verified"; exactly one support archive in the
dist, support-c4dbe35986a5 (sha256
c4dbe35986a5edcff6646cb8b4f63cc7660e6e1e6dcda
6546454db77d241729f), the deterministic FD-53 pack,
byte-identical to the previous live build's;
check_runtime_completeness.py 19 PASS, 0 failures,
1 skip, exit 0; JS unit suite against the rebuilt dist
353 passed, 0 failed — identical to the merge gate.
The gate's remaining figures (native main 682/682, x64
108/108, run-native 67 PASS, JS legacy 115/0, citations
352 rows 0 drift, silent-failure baselines at their
standing allowances, the browser suite's 27/5 with all
five failures adjudicated as FD-154's base-reproduced
five) are the merge record's and were not re-run, per
the batching rule — except common-dependencies, re-run
against the live build below.

Staging: the rebuilt dist was copied to winbox-next
and verified byte-identical to the dist (recursive
diff empty); its file set is identical to the previous
live tree's; vendor/ is byte-identical; it carries
exactly one support archive. Content differs from the
previous live tree in exactly nine files:
runtime/boxedwine64.wasm, runtime/boxedwine64.js,
runtime/BUILD_INFO.json, app.mjs + runtime/index.html
(release-id substitution only, eb374a6dba52 ->
2c9a0ba318a1, verified line by line), runtime/wine32.zip
(FD-149's rebuilt 32-bit win32u.so), and vk-protocol.mjs,
vkwebgpu.mjs, wgsl-utils.mjs (FD-148's sampler flag on
the wire and in the shader rewrite). The swap was then
made and the live tree verified over public HTTPS:
BUILD_INFO serves git_sha 49c67137d…, wasm 9abd2f24…,
git_dirty_files 0; the shell answers 200; the runtime
index answers 200; range requests on the wasm and on
the support archive answer 206.

## First-visit starts — measured through the public route

Protocol identical to the previous deploy measurements:
brand-new browser profile per boot, the deskpin-anchored
harness session (session=1) at the public URL, the
launcher's restart budget pre-exhausted (bwretry=2) so
no start is a rescue, 900 s window, full console
captured and classified OFFLINE under the hardened
FD-47 rule. Evidence: test-results/deploy-batch6-
20261104/ (live-driver.mjs preserved there, a copy of
the batch-5 deploy's driver).

- x86 n=5: certified 5/5. Handle 0x10052 every time,
  certified-ready 20.8, 20.8, 20.7, 21.6, 22.8 s
  (median 20,786 ms), zero restarts, zero false-ready,
  zero rescues.
- x64 n=3: certified 3/3, handle 0x10048 on every
  boot, certified-ready 94.4, 95.4, 89.9 s (median
  94,442 ms), with the same zero counters.
- Console census across all 8 boots (10,396 lines):
  0 BW64-FAIL lines of any kind, and 40 BW64-INFO
  "hard-link … outcome=served by shared backing path"
  lines — exactly 5 per boot on BOTH widths, FD-81's
  behaviour intact on the shipped build.

## The wedged-start entry (FD-115) — this deploy's count

FD-115 counts first-visit x86 starts on shipped builds.
This deploy adds five: 5/5 certified, no wedge,
consoles clean (above). With the batch-5 deploy's five
clean starts (its record brought the tally to 1 in
30), the running tally on shipped builds is now 1
wedged in 35 measured starts. The entry stays open on
its stated terms; nothing in this deploy's series
reproduces it. The same account is recorded as an
update block in the register's FD-115 entry.

## Consumer series — Chromium through the public website path

Six uninstrumented runs of the standard demo path
against the live site (demo-matrix, DEMO=chromium,
BW_TEST_BASE_URL = https://kn0tzer.work.gd/winbox/,
fresh profile per run, 900 s mapped window). Evidence:
test-results/deploy-batch6-20261104/
chromium-consumer-a1…a6 (+ .log files,
consumer-tally.txt).

- 6/6 runs MAPPED, zero stalls, every run exit 0.
  Every run: stage "mapped", overlay cleared,
  click-to-mapped 39.2, 40.8, 39.3, 42.9, 41.3,
  39.5 s, frames presented 10, 12, 10, 8, 12, 11,
  99.16% non-dark / 2,265 distinct colors (the
  standing success pixel signature), 0 page errors.
- Census across the six consumer consoles (15,832
  lines): 0 BW64-FAIL lines of any kind — in
  particular, no delivered guest memfault was
  observed in this series — and 30 BW64-INFO
  served-link lines, exactly 5 per session.

## The shipped dependencies test — common-dependencies on the live build

The batch's shipped test was re-run against the live
build itself: WINE_DIST_DIR=/home/ubuntu/kn0tzer/
winbox, the suite harness serving exactly the live
tree and self-verifying its BUILD_INFO hashes before
running (the run's preserved BUILD_INFO.json is the
live stamp: git_sha 49c67137d…, wasm 9abd2f24…,
dirty 0). Result: PASS unloosened, 241 s, suite result
1 passed / 0 failed, exit 0. Its console (preserved in
test-results/suite-common-dependencies/ and copied
into this deploy's evidence) carries both
"MSHTML: RESULT 0" and "OPENAL: RESULT 0" and 0
BW64-FAIL lines. Deploy log:
test-results/deploy-batch6-20261104/common-deps.log.

## The FD-89 proof owed by CL-43 — the in-vivo account on the shipped build

CL-43's closing terms owe the two pipelines' creations
succeeding in vivo on a shipped build. This deploy
took the owed account once, on the shipped build: the
lineage-committed capture driver
web/tests/fd128-godot.mjs in the FD-128 lane's capture
configuration (the standard demo path with the warmed
archive, vktrace on with vkd3d shader dumps to the
guest home, 1,200 s deadline), pointed at the public
live site (BW_TEST_BASE_URL =
https://kn0tzer.work.gd/winbox/), so the bytes under
test are the shipped ones; the driver's recorded build
stamp is the shipped stamp (git_sha 49c67137d…, wasm
9abd2f24…, git_dirty_files 0). Evidence:
test-results/deploy-batch6-20261104/godot-proof/
(full console preserved), godot-driver-out.log.

- Godot banner at 99.3 s; the demo's window MAPPED
  at 144.6 s — the FD-127 device-setup park does not
  occur on the shipped build, as at batch 5.
- Shader modules: 144 vkCreateShaderModule
  creations succeeded (133 distinct module hashes;
  the first, at 126.7 s, is the 1,216-byte module,
  hash f598618b6233f21a — the same first module as
  the branch captures and the batch-5 run). Exactly
  2 creations were refused, both the FD-72
  helper-invocation module (5,704 B, hash
  0b3b4d8cc939485a), each logged
  "vkCreateShaderModule REFUSED … result=-8"
  (VK_ERROR_FEATURE_NOT_PRESENT) at 242.7 s and
  243.2 s — refused by name under FD-75, as
  designed.
- Pipelines: the bridge trapped 6
  vkCreateGraphicsPipelines calls and 121
  vkCreateComputePipelines calls. The only failed
  pipeline attempts in the run are the guest's own
  two log lines "ERROR: Create(Graphics)PipelineState
  failed with error 0x80004005", at 242.7 s and
  243.2 s — each the direct consequence of one of
  the two FD-72 refusals above. Also trapped: 6
  vkQueueSubmit2 (0 plain vkQueueSubmit), 5
  vkAllocateMemory, 32 vkCreateImage, 10
  vkCreateBuffer, 349 vkCreatePipelineLayout.
- Submits and present: no vkCreateSwapchainKHR
  and no vkQueuePresentKHR occurred; the metrics
  present count reached 2 while the Vulkan present
  count stayed 0; the final display was the mapped
  window without rendered frames (5,699 lit pixels,
  0.44% non-dark). After the early phase the guest
  settles into the downstream shader-retry cycle:
  module and compute-pipeline creations recurred
  intermittently (last at 1,176.3 s) without
  changing any outcome.
- Run health: the 8,589-line console carries 0
  BW64-FAIL lines; the driver's CDP exception watch
  captured no wasm trap (no exceptions file;
  trapCapturedAtSeconds null) — the FD-128 worker
  death does not occur on the shipped build; the
  guest was alive and executing at the 1,200 s
  deadline (outcome "deadline", driver exit 0).
  5 bridge DROPPED lines, all
  destroy-of-unknown-object guards (1×
  vkDestroySemaphore, 4× vkDestroyPipelineCache).

Verdict, stated plainly: the owed clause is NOT
discharged on its exact terms. The clause names the
two modules the FD-70 lineage newly served —
369,400 B (djb2 d4da162f9cbdb948) and 2,340 B (djb2
fe166085df72b93f) — and neither was created in this
run: no 369,400-byte and no 2,340-byte creation
appears (0 occurrences of either size among the 144
creations), so their in-vivo creation on a shipped
build remains unmeasured. This capture's guest
reaches the FD-72 module first, is refused there, and
cycles without reaching the phase that produced
those modules in the FD-70-era run. What the run
does settle, on its evidence: the shipped build
carries the demo past every blocker this lineage
fixed (FD-127's park, FD-128's trap, FD-129's
writer — CL-100, CL-101, CL-102) with its window
mapped and the module/pipeline account above, and
the only in-vivo pipeline failures remaining in the
capture are FD-72's two. The first presented frame
remains gated on FD-72/FD-74 (and on FD-88 for the
descriptor-array class), per FD-55's entry. One
attempt was made and no second, per this clause's
one-run-per-deploy pattern across batches 3, 4 and
5. The same account is recorded as an update block
in the register's FD-89 entry.

## Push to GitHub

This deploy continues the standing every-deploy push
rule. The push is executed as the final step of this
same deploy, after this record: the integration tip
(including this record and the FD-89/FD-115 register
updates) is merged into the current remote master tip
(c6fe0d9ad4 at push start) through a detached temporary
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

- Several local transport sessions to the box dropped
  or held open under load during this deploy; every
  long job ran detached (nohup/setsid) and ran to
  completion — verified by direct process and log
  reads, and no figure above rests on an interrupted
  delivery.
- Box 1-minute load across the measurement windows
  ranged ~1.1 to ~7.2 (other tenants' work). The
  figures are reported as measured.

## State left behind

Deployment record and the FD-89/FD-115 register
updates committed on integ on top of 49c67137d. Integ
tree stripped (dist, web/runtime,
project/linux/Build, project/emscripten/Build,
__pycache__, and the copied payload inputs
tools/dxvk/build.w32, tools/dxvk/build.w64,
tools/rootfs64/{cjk-font, gecko, gstreamer32,
gstreamer64, openal} gone) and clean; the deploy
evidence stays on disk under
test-results/deploy-batch6-20261104/ (gitignored, as
all deploy evidence). Live disk at deploy end is
stated in the deploy lane's delivery.
