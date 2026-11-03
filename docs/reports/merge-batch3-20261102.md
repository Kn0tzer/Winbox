# Merge record — batch 3 (2026-11-02)

Integration branch `integ/winbox-20261022b`, composed in the
integration worktree from the batch-2 deploy tip `7f2bf5f6d`.
21 lane branches composed (79 commits including this merge's own
composition, audit and decision commits). NOT deployed, NOT
pushed — the tree is deploy-ready for the carrying deploy lane.

## Branches composed (pool order = register settlement order)

lane-fd87 (Godot sibling classes) · lane-fd90 (ALSA OSS position;
carries lane-fd85b's commits — fd85b never composed separately) ·
lane-fd64 (legacy fixture) · lane-fd05 (fd-table exhaustion) ·
lane-fd78 (literal positions) · lane-fd88 (register-only FD-88
narrowing) · lane-fd92 (spirvfix splice ordering) · lane-fd72b
(register/record only) · lane-fd83-84 · lane-fd82 · lane-fd92mmap ·
lane-fd92-93 · lane-fd94-96 · lane-fd97 · lane-fd98 · lane-fd99-100 ·
lane-fd101 · lane-fd102-104 · lane-fd23res · lane-fd86 (only its
own commits from a3349c4cb; its lane-fd65 base was already
composed) · lane-fd85pay (conviction commit only).

## Register audit — settlement (audit commit 742adb632)

Final state, verified by re-reading the register: 111 FD bullets,
no duplicate FD headers; 66 closure records, CL-01…CL-66 each
exactly once, closures sequential. Every lane's carried register
text was replaced wholesale with its authoritative branch text
where concat composition had misplaced paragraphs, then the
number maps below were applied to entry headers, closure lines,
CL record titles and Section B rows.

FD renumbers (filing order by git commit timestamp, UTC):
- Godot FD-87 → **FD-89**, closure CL-43 (the composed register's
  FD-87/CL-42 belong to the FD-85 capture fix from batch 2).
- spirvfix splice filing (lane-fd92, 13:05) KEEPS **FD-92**.
- readv wiring filing (14:35) → **FD-103**; mmap filing (14:41) →
  **FD-104**; socket trio (00:51) → **FD-105** SCM_RIGHTS,
  **FD-106** sendto, **FD-107** recvmsg; write-loss filing
  (01:42) → **FD-108**; fd23res pair (01:55) → **FD-109** rseq,
  **FD-110** pselect6; message quartet (02:54) → **FD-111**
  recvfrom, **FD-112** control chain, **FD-113** recvmsg name
  copyback, **FD-114** datagram sendto.
- FD-91 (vk-protocol comment) stays open under its own number.
  FD-93…FD-102 keep theirs (FD-102 = launcher catalog, closed).

Closure settlement (composition order, after CL-42): CL-43
FD-89 (Godot) · CL-44 FD-64 · CL-45 FD-05 · CL-46 FD-78 ·
CL-47 FD-92 · CL-48 FD-82 · CL-49 FD-83 · CL-50 FD-84 ·
CL-51 FD-104 (mmap) · CL-52 FD-101 · CL-53/54/55 = FD-105/106/107
· CL-56 FD-97 · CL-57 FD-103 (readv) · CL-58 FD-93 · CL-59 FD-94
· CL-60 FD-96 · CL-61 FD-98 · CL-62 FD-99 · CL-63 FD-100 ·
CL-64 FD-86 · CL-65 FD-23 · CL-66 FD-90 (record written by this
merge from the lane's branch commits 223cb8fbf / dd00eee9b /
adb032921 — the lane wrote none).

**FD-103 collision rule (coordinator steer, 2026-11-02):**
lane-wedge-incidence (tip 6f9b53e1e, NOT in this pool) filed the
batch-2 wedged boot as FD-103 on its own branch. Rule adopted:
**first-composed wins** — the campaign's batch-2 FD-87/CL-42
precedent. This pool's readv filing keeps FD-103; the wedge
entry renumbers when its branch composes in batch 4.

## The two composition decisions (commit 129462a63)

(1) **FD-95 — the positioned family's validation order** is
settled from the host measurements on record (FD-83/84's and
FD-98's, agreeing): offset validity (EINVAL) first — except the
v2 calls' offset −1, the file-position mode — then descriptor
answers (EBADF, closed or wrong-mode), then iovcnt range
(EINVAL), then the iovec array (EFAULT), then the v2 flags
check last (EOPNOTSUPP, mask 0x3f). The positioned cores and
KProcess::preadv already held it; sys_preadv64, sys_pwritev64
and KProcess::pwritev gained the mode gate at the descriptor
check. FD-95 is closed inline in the register, no closure
number consumed (batch-2 FD-102 precedent).

(2) **The positioned vectored write's wrong-mode answer** is
−EBADF at every site, aligned with FD-93's settled family
contract; FD-94's pinned −EINVAL case (testPwritev64Syscalls
section 9) was flipped to −EBADF in the same commit.

## Composition repairs (not lane work — merge fixes)

- **Helper dedup (74b541003):** fd97 and fd101 each defined a
  byte-identical `sys_guestReadablePrefix64`; the composed tree
  keeps one definition plus a forward declaration.
- **Zipped x64 test cluster (40094df41):** the fd82/fd101/fd97/
  fd92-93/fd94-96 test insertions stacked their headers at one
  anchor in testInotify.cpp and fd99-100's preadv sections lost
  a block close — visible only in the x64 build. Each function
  was restored verbatim from its authoring lane
  (testPwritev64Syscalls from lane-fd99-100, carrying the FD-94
  + FD-100 sections); testPreadv64Syscalls keeps the composed
  FD-83/84 + FD-99/100 sections with its block close restored.
- **kobject.cpp line endings:** fd94-96's lane rewrote the file
  as LF over a CRLF base; the composed file keeps CRLF with
  FD-96's writev hunk hand-applied.
- **Vectored-read core choice:** where fd99-100's rewrite met
  FD-84's composed wrapper additions, fd99-100's version stands
  (its per-segment core answers the mode); the family mode
  answer moved to the wrappers under decision (1).
- **FD-85:** one entry — batch-2 text + fd85b continuation +
  fd85pay's conviction addendum, renumbered to FD-108. FD-85
  stays OPEN, re-gated on the FD-108 write-loss fix; the audio
  payload stands exonerated by the fpprobe reproduction.

## Silent-failure baseline

known-silent.json, syscall64.cpp unloggedErrno: batch-2 base
115, lane-accounted moves +1 (fd83-84) +2 (fd102-104) +1
(fd92-93) +3 (fd94-96) = 122; the scanner measures **132** on
the composed tree (the FD-23 residue implementations are the
largest single contributor beyond the lanes' own accounting).
Baseline set to the measured 132, per the merge brief.

## Gate (evidence: test-results/merge-batch3-20261102/)

- Bootstrap from the retained runtime-assets-20261103 archive
  ONLY: 13 assets restored and verified; post-build --check
  0 failures, 1 skip. (The sibling lane's payload-drift warning
  did not bite here: this tree's payloads come from the
  retained archive, and the build verified support archive
  c4dbe35986a5. No payloads were regenerated.)
- Browser build: "Browser export verified"; BUILD_INFO git_sha
  129462a63 (the decision-commit tip the build ran from).
- Native suites serial, 1 thread: main **675/675**, x64
  **83/83**, 0 FAILED. (An early 4-thread main run died in a
  guest-memfault flood — the registered FD-32 multi-worker
  shape; the gate standard is the serial run. Log kept as
  native-full-main-4t-died.log.)
- JS unit: **352 passed, 0 failed** (final run). JS legacy:
  **115 pass, 0 fail** — FD-64's empty failing set confirmed.
- run-native: **62 PASS, 0 FAIL**.
- Citations: **0 DRIFT** (352 rows; --fix moved 33, 22 rows
  hand-pinned per-cite against the composed files).
- Boots: boot64-smoke PASS, 0 BW64-FAIL; boot32-vkboot PASS,
  0 BW64-FAIL, exactly 5 BW64-INFO hard-link served lines.

Gate pins settled on the composed tree (52f3b72c9, ff31c9f7e):
the legacy corpus table re-baselined for the FD-78 + FD-88
corpus members (the lanes pinned their contracts only in their
own unit suites; three FD-88 modules sit in REFUSED with the
translator's UnsupportedCapability(SampledImageArray
DynamicIndexing), exactly as spirvfix-fd87 pins); spirvfix-fd78
(F) accepts FD-87's R18 strip removing its variable first
(outcome unchanged); the thread-dump source pin rescoped to
the dump function's own body (FD-23's select legitimately uses
the blocking fd lookup in syscall context).

## Owed after this merge

- FD-89/CL-43's in-vivo pipeline-creation proof belongs to the
  carrying deploy, not this gate; it stays owed (pipelines
  still gate on FD-88 + FD-72).
- The batch-2 wedged-boot entry on lane-wedge-incidence
  renumbers off FD-103 at batch-4 composition (rule above).
