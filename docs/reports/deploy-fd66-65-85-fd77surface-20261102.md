# Deploy 2026-11-02 — FD-66 + FD-65 + FD-85 + FD-77-stall-surface batch

Deployed by the deploy+push lane. Live site serves git_sha
8ac504abf / wasm 9a7d101c (verified over public HTTPS from
off the box: the BUILD_INFO stamp matches the merge gate's
build byte-for-byte, wasm 5,620,653 bytes). Rollback:
kn0tzer/winbox-prev-20261102-2119 (the 9e37e5640 build);
the older rollback (winbox-prev-20261102-0133, the
99798cd08 build) was pruned per the newest-only rule after
the off-box verification and the live checks passed.
Master untouched as a working branch; the personal game
archive untouched.

## What shipped

The composed batch at integ tip 22d6b63dd (merge record
merge-fd66-65-85-fd77surface-20261102.md): FD-66 (the
bridge readback-setup leak closed by CL-40), FD-65 (Wine
Gecko's usable font on the 64-bit stack — win32u rebuilt
with freetype/fontconfig and six DejaVu faces staged —
CL-41), the FD-85 audio-leg test fix (the capture now
taps the AudioWorklet output; settled as FD-87 / CL-42
for this batch), and the FD-77 remainder (a booted-but-
windowless stall in a default session now ends as a
reported failure with its evidence preserved, discharging
FD-77's surfacing debt). FD-102 (the launcher UI test's
stale demo-card count) was resolved at composition and
consumed no CL number. No deploy composition commits
were needed.

Staging: the merge gate's dist was staged as-is (its
BUILD_INFO is the shipped stamp) after re-verification
on the deploy tree — bootstrap.py --check exit 0
("Runtime assets verified (13 files)", the FD-73 line
agreeing the retained runtime-assets-20261102 archive,
sha256 7df0cdc23cf11f81949d44dcdba79ee992c8a92fe071af
52d8921f8a58f74398, matches the file pins);
check_runtime_completeness.py 0 failures, 1 skip (the
standing prefix64 built-artifact skip) with FD-53's and
FD-73's checks active and passing. No manual restage
was used. The staged tree is byte-identical to the gate
dist (diff-verified), its file set is identical to the
previous live tree's, vendor/ is byte-identical, and it
carries exactly one support archive: support-c4dbe35986a5
(sha256 c4dbe35986a5edcff6646cb8b4f63cc7660e6e1e6dcda654
6454db77d241729f), the deterministic FD-53 pack,
byte-identical to the previous live build's. Content
differs from the previous live tree in exactly eight
files: runtime/boxedwine64.wasm, runtime/BUILD_INFO.json
and runtime/index.html (the build stamp / release-id),
app.mjs (the launcher stall surfacing), runtime/
wine64-launcher.js (the FD-65 payload side),
runtime/wine64.zip (the FD-65 rebuilt win32u.so),
runtime/glibc-rootfs64.zip (the FD-65 staged faces) and
runtime/wine32-font-provenance.json. JS unit suite
against the staged dist: 349 passed, 0 failed —
identical to the merge gate.

## First-visit starts — measured through the public route

Protocol identical to the previous deploy measurements:
brand-new browser profile per boot, the deskpin-anchored
harness session (session=1) at the public URL, the
launcher's restart budget pre-exhausted (bwretry=2) so
no start is a rescue, 900 s window, full console captured
and classified OFFLINE under the hardened FD-47 rule.
Evidence: test-results/deploy-fd66-65-85-fd77surface-
20261102/ (live-driver.mjs preserved there).

- x86 n=5: certified 4/5. The four certified boots took
  handle 0x10052 every time, certified-ready 33.6–47.1 s
  (median 39.9 s), zero restarts, zero false-ready.
  Boot 1 FAILED — see the failure account below.
- x64 n=3: certified 3/3, handle 0x10048 on every boot,
  certified-ready 164.3–226.7 s (median 222.7 s), with
  the same zero counters. (Slower than the previous
  deploy's 87.7 s median; the box's 1-minute load across
  these measurement windows ran ~12–17, against ~7.6–12.1
  in the previous deploy's x64 windows and ~3.3–21.6
  across this deploy's x86 windows, with other lanes'
  builds and probes active throughout. The figures are
  reported as measured.)
- Console census across all 8 boots (5,141 lines):
  21 explorer OPENED spawn signals, 0 session restarts,
  and 65 BW64-FAIL lines — every one of them in x86
  boot 1's console; the other seven boots' consoles are
  clean. Link lines: 40 BW64-INFO "hard-link …
  outcome=served by shared backing path" lines —
  exactly 5 per boot on BOTH widths this time: the x86
  boots carry the standing le32d4 set (CACHEDIR.TAG +
  four caches) and the x64 boots now carry their le64
  fontconfig cache set, which exists because FD-65 gave
  the 64-bit rootfs real faces and a fontconfig-linked
  win32u. FD-81's behaviour is intact on both widths.

## The one failed start — x86 boot 1, stated plainly

Boot 1 exhausted its 900 s budget without the desktop
opening (result line: class "other", pageReady false,
"budget exhausted"). Its console (684 lines, preserved
at live-x86/boot-1/console.log in the evidence) shows
the session starting normally — wineserver32 and the
deskpin spawned, the fontconfig link set served — and
then a winedevice-host guest thread (pid=71, tid=76)
entering a delivered-memfault loop at eip 0x10584384,
fault address 0xfffffd08, repeated for the rest of the
window (65 BW64-FAIL guest-memfault32 lines; the thread
dump at the end reads noprogress_ms=780018 on that
thread). The desktop handshake never completed, so the
new FD-77 stall surfacing did not fire — by its stated
design it only reports stalls after the handshake. No
mechanism is claimed for the fault. The four following
x86 starts and all three x64 starts certified with
clean consoles, and all six consumer sessions below ran
clean, so this deploy records boot 1 as a single wedged
first-visit start on the shipped build, with its full
console preserved — not as a passing check, and not
waved through.

## Consumer series — Chromium through the public website path

Six uninstrumented runs of the standard demo path
against the live site (demo-matrix, DEMO=chromium,
BW_TEST_BASE_URL = https://kn0tzer.work.gd/winbox/,
fresh profile per run, 900 s mapped window). Evidence:
test-results/deploy-fd66-65-85-fd77surface-20261102/
chromium-consumer-a1…a6 (+ .log files, consumer-tally.txt).

- 6/6 runs MAPPED, zero stalls. Every run: stage
  "mapped", overlay cleared, click-to-mapped 63.6, 63.4,
  50.0, 49.6, 45.7, 49.3 s, frames presented 3, 7, 4,
  20, 7, 8, 99.16% non-dark / 2,265–2,269 distinct
  colors (the standing success pixel signature), the
  input script completed, 0 page errors, run exit 0.
- Link census across the six consumer consoles (3,263
  lines): 0 BW64-FAIL lines of any kind, 0 memory
  faults, and 30 BW64-INFO served-link lines — exactly
  5 per session.

## Push to GitHub

This deploy continues the standing every-deploy push
rule. The push is executed as the final step of this
same deploy, after this record: the integration tip
(including this record) is merged into the current
remote master tip (1dbbb2000 at push start) through a
detached temporary worktree on the box (--no-ff),
pushed to origin (github.com/Kn0tzer/Winbox) with the
box's existing GitHub credential, and verified by
reading the remote back. The held-back local commit
69c800476 (the workflow-file edit the box credential
cannot publish) is not an ancestor of the integration
branch and does not ride; verified before the push
against both the remote tip and the integration branch,
and again by read-back after it. The verified remote
tip resulting from the push is carried in the deploy
lane's delivery and the coordinator's state, since this
record is itself part of what is pushed; local branches
and history are otherwise exactly as they were.

## State left behind

Deployment record committed on integ on top of
22d6b63dd. Integ tree stripped (dist, web/runtime,
project/linux/Build, __pycache__ gone — after the live
checks, which read the local dist BUILD_INFO as their
report stamp) and clean except the pre-existing
untracked vkd3d probe DLL. FD-66 stands CLOSED by
CL-40, FD-65 by CL-41, and the FD-85 audio-leg fix by
CL-42 (settled as FD-87 for this batch; the sibling
renumbers belong to batch 3) on the shipped register;
FD-77's surfacing debt stands discharged; FD-102 stands
resolved inline. Live disk at deploy end: 32G free
before the strip.
