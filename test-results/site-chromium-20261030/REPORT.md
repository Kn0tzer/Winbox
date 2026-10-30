# Chromium demo regression — site-path investigation and fix (2026-10-30)

Branch: `lane-sitedemo` (off integ tip `a3dc6af9b`, the shipped DX12 build).
Method: every run below drives the real website the way a person does —
a real Chromium browser (Playwright) on the box, the actual site pages,
clicking the Chromium demo card — never the server-side probe harness.
Success = the guest log reports the first window mapped AND the site
player dismisses its loading state with a presented frame
("window-mapped+frame"). Guest CPU is sampled from /proc: a healthy run
burns ~1-2 cores; the failure signature is a parked guest (~0.07 cores)
whose log stops growing mid-startup.

## Symptom on the shipped build (efa0189a1 / wasm 18137f34)

- Live site, fresh profile: STALL x2. Session boots, chrome.exe starts,
  then the guest log freezes at ~12.2 KB and the guest parks forever.
- Live site, return visit (stored install): STALL. Not a first-run issue.
- Same build served locally at low box load (~3): STALL. Not load.
- WinMerge demo (x86) on the live site the same day: WORKS. The site
  path itself was healthy; the failure was Chromium-startup-specific.

## What was exonerated first

- Site shell: functionally identical between the previous (working-era)
  deploy and the current one (only the cache-bust release id differs);
  launch flags identical to the known-good set.
- Runtime payloads: member-identical (name/size/CRC) except the intended
  DX12 DLL swaps; fonts intact in glibc-rootfs32.zip; chromium.zip intact
  (468,348,892 B, range requests answer HTTP 206).
- A hybrid (live tree + ONLY the Oct-17 emulator core js/wasm) PASSES,
  and the Oct-17 era build passes 3/3 through the site. The regression
  is in the emulator core, in the Oct 17 -> Oct 27 window
  (d1ca72d70..c01fdb666, 286 first-parent commits).

## Bisect through the website (one build + site run per point)

PASS = window mapped + frame presented. "Late park" = window mapped,
then the guest froze at ~14.1 KB before the first frame. "Early park" =
froze at ~12.1 KB, no window ever.

| pos | commit | result |
|----|--------|--------|
| 0 | d1ca72d70 (Oct 17) | PASS x3 |
| 143 | 2c49cb2f8 (Oct 23) | PASS |
| 168 | 9d82d9616 | PASS |
| 181 | d019ba15b | PASS |
| 187 | 1386a50b1 | PASS |
| 189 | 18b611d06 (msync's sibling: mlock validation) | PASS |
| 190 | eaccf777f "fix: validate 32-bit msync memory ranges" | FAIL (late park) |
| 193 | 8682e43ef | FAIL (late park) |
| 218 | 900503899 | FAIL (late park) |
| 230 | 982295490 | FAIL (late park) |
| 235 | 2882a52a3 | PASS (1 run — the late park is probabilistic) |
| 236 | c96884335 | FAIL (early park once, late park once) |
| 240 | 2f689625e | PASS x2 |
| 242 | 4c2449e71 (inotify impl is pos 241) | FAIL (early park) x2 |
| 286 | c01fdb666 (Oct 27) | FAIL (early park) x5 incl. live site |

Two distinct failure shapes means the single-defect bisect assumption
breaks; the table supports two readings, both pursued below: a
probabilistic post-window park that appears at position 190, and a
deterministic pre-window park that appears by position 242.

## The pre-window park (the one users hit): the inotify feature

The stalled guest log of the shipped build ends with the line
`no inode matches 2`. That string exists nowhere in this repo; it is
printed by the guest's `wineserver32` (Wine 10.0 `server/change.c`,
its ReadDirectoryChangesW machinery) when it drops a change event it
cannot match. It appears in stalled logs of the shipped build family
and in NO log from builds before position 242.

In-emulator tracing of the stalled run (temporary klog in the inotify
syscalls) shows a Wine process pinning the pattern down: it creates a
(blocking) inotify instance, watches the Wine DLL directory and
`C:\windows` via `/proc/self/fd/N` paths with mask 0x7C6, removes one
watch — and startup never proceeds past that point.

The feature landed at position 241: e49d833ef "Implement inotify with
real watch delivery (was ENOSYS)". Controlled experiments on the
shipped tip:

- tip + inotify_init1 -> ENOSYS (guests use their fallback): PASS.
- tip + inotify_add_watch -> ENOSYS (instances allowed): PASS.
- tip with inotify fully enabled: FAIL, every time.

So the defect is in the watch/event-delivery semantics, not in instance
creation or read plumbing. Three defects were found in the feature;
the first two are fixed outright on this branch:

1. **Record format corruption (fixed, commit 0d7d44e92).**
   `KInotify::readNative` reported `len = 4` for nameless events
   (IN_IGNORED, self events). Linux reports 0. Readers walk the buffer
   by `sizeof(inotify_event) + len`, so one nameless record
   desynchronized every record behind it in the same read — this is
   exactly how the wineserver ended up printing "no inode matches"
   for a garbage watch descriptor and dropping real events.
2. **IN_CREATE for files that never were created (fixed, commit
   9e275a32e).** Directory child loads funnel through
   `FsNode::addChild`, so every pre-existing file was reported as
   IN_CREATE the first time its directory was enumerated. Linux never
   reports pre-existing files. (The feature's own commit message
   documented this as a "gap"; it is a semantics violation, not a gap.)
3. **Unisolated remainder.** With (1) and (2) fixed, the demo still
   parks at the same startup point (log frozen at exactly 12,217
   bytes in two independent runs), and matching watches by canonical
   path instead of node identity did not change it. The remaining
   incompatibility sits somewhere in the event-generation/coverage
   semantics for this consumer and is NOT reproduced by the feature's
   synthetic syscall tests, which all passed while the product was
   broken.

Disposition (commit c9259ec42): `KProcess::inotify_init1` returns
-K_ENOSYS — the project's REJECTED-LOUDLY form. Guests fall back to
polling, which is the exact behavior the site shipped with from its
first working day until 2026-10-25. The KInotify machinery and the
delivery test bodies stay in the tree; the inotify tests now pin the
loud rejection at the syscall and process-API layers.

## The post-window park: an open thread, honestly flagged

Builds at positions 190-236 repeatedly (but not always) mapped the
window and then parked before the first frame; position 189 passed
and 235 passed once. The msync validation commit at 190 is the first
bad point, but reverting it alone on the shipped tip does NOT restore
the demo (the inotify park masks everything downstream), and msync
has no waiting semantics that park a thread by itself. A separate,
already-documented suspect exists for exactly this shape: the 32-bit
futex lost-wakeup flaw, proven by a deterministic native test on the
unmerged wake32 lane (commit c66a35684, whose traced stall signature —
threads piled on Wine's loader critical section — matches). This
investigation did not isolate the late park further; it needs its own
pass with the inotify defect out of the way. It does NOT block the
demo on the fixed build in any run performed (see below), but the
sample is small and the parent should treat the late park as a live,
unconvicted suspect rather than a closed one.

## Verification of the fix (branch tip: c9259ec42, wasm 879122b6)

All runs are full site-path runs (real browser, demo click, fresh
browser profile) against this branch's own dist:

- final-run1: window mapped at 58 s + frame. Screenshot `final.png`
  (this directory): the Chromium 109 window fully drawn in the site
  player — tab strip, address bar on about:blank, --no-sandbox
  infobar.
- final-run2: window mapped at 74 s + frame. (`logfull.txt` and
  `events.log` here are from run 1.)
- An earlier build differing only in unreachable code (the ENOSYS
  experiment) also passed end-to-end at box load ~7.
- Native suite: 648 tests, 0 failed (single-threaded run; the
  4-thread harness crashed in an early CPU fault test under this
  box's memory pressure — swap 100% full — unrelated to this change;
  the first-6 chunk passes single-threaded and the full suite is
  green single-threaded). The two x64-guest inotify test files also
  syntax-check clean under the guest-x64 configuration.
- The stalled traced log is preserved as `stalled-traced-logfull.txt`
  (INOTIFYDBG lines 186-192 are the watch pattern quoted above).

## What "different before" looks like, in one line

Until 2026-10-25 the emulator answered inotify with ENOSYS and Wine
used its fallback; the Oct-25 feature turned it on with a corrupted
record format and creation events for files that already existed, and
Chromium — the demo whose startup waits on directory-change
notifications — never got its window again.
