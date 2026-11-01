# FD-58 / FD-59 / FD-60 — kernel semantics lane record (2026-11-01)

Branch `lane-fd58-60` off integ `45f44d427`. Commits: `81681e834`
(the three fixes + test), `8c7c34ba8` (FD-60 event fan-out
refinement + test extension). Nothing pushed, nothing deployed.

## Method

Host-kernel reference programs first (kernel 6.17.0-1020-oracle on
the box), then one minimal test — `testChmodAccessHardLink`,
registered in both native suites — written against pre-existing
APIs so it runs on the unfixed code: RED in both suites, then the
fixes, then GREEN in both suites, then the full suites.

## Host ground truth (measured; sources + outputs in test-results/fd58-60/)

- Reads: read/pread/readv fire IN_ACCESS on the file watch and on
  the parent watch (with the name used). A read returning 0 bytes
  fires nothing. readdir fires IN_ACCESS on the directory's own
  watch. An mmap PROT_READ fault fires nothing.
- mmap writes: a MAP_SHARED mapping written and then msync'd
  (MS_SYNC and MS_ASYNC), fsync'd, sync'd, unmapped and closed
  produces **no IN_MODIFY at any point** — only IN_OPEN and
  IN_CLOSE_WRITE. MAP_PRIVATE likewise. (ref_inotify.c, ref2.c)
- chmod: the mode is stored verbatim, setuid included (04755 →
  4755); every chmod/fchmod fires IN_ATTRIB on the file and parent
  watches; fchmodat AT_SYMLINK_NOFOLLOW on a symlink fails with
  ENOTSUP (95) and leaves the target untouched; a missing path is
  ENOENT. (ref3.c)
- link: st_ino identical for both names, st_nlink 2 on both; the
  queue order is file-watch IN_ATTRIB first, then dir-watch
  IN_CREATE; a write through one name is readable through the
  other; unlinking one name leaves the survivor at nlink 1 with
  the content intact. (ref3.c)
- Adjacent finding: with a writable mapping still open, the host
  defers IN_CLOSE_WRITE until munmap. The emulator reports it at
  fd close. Filed as FD-79.

## FD-58 — chmod (was: returns success, changes nothing)

Before (RED): the reported mode stayed at the synthetic default
(0722 in the fixture) through chmod 0600 and chmod 04755; 64-bit
fchmod was the same no-op; fchmodat nr 268 fell to the
dispatcher's unimplemented path (-ENOSYS, with the dispatcher's
own "unimplemented syscall #268" line in the RED log); 32-bit
fchmod/fchmodat returned 0 marked IGNORED.

Fix: the mode is stored on the node (bits 0..07777) and all stat
writers report it — stat64, lstat64, fstat64, statx, fstatat64 on
both dispatchers and KFile::stat. `KProcess::chmod`,
`fchmod`, `fchmodat` share one core; success fires IN_ATTRIB.
fchmodat AT_SYMLINK_NOFOLLOW on a symlink returns -ENOTSUP.
32-bit syscall wrappers call the same KProcess methods (the
syscall-level proof is on the 64-bit dispatcher plus the
KProcess-level proof in both suites; the 32-bit wrappers are
one-line forwards — stated as the proof boundary).

Boundary: the stored mode is guest-visible state only. The
emulator's own access checks (canRead/canWrite) still use the
synthetic mode, so chmod does not change what the emulator itself
allows. Zip-backed files accept the mode as session state, the
way link counts already behaved.

## FD-59 — inotify IN_ACCESS + mmap IN_MODIFY (was: never generated)

IN_ACCESS half — fixed. Hooks fire only when a read completed
with >0 bytes delivered: KObject-layer reads
(`FsOpenNode::read`), the 64-bit read/readv completion point
(`KFile::readNative`), pread64 (in the dispatcher, deliberately
not inside `KFile::preadNative`, whose internal callers are page
fills that must not fire), and directory enumeration
(`KProcess::getdents`). Events land on the file watch and the
parent watch with the name used. A registration fast-out
(`KInotify::anyInstance`) keeps the read hot path at one atomic
load when no inotify instance exists. Page-fault fills do not
fire — matching the host.

mmap half — NOT-A-GAP. Per the host measurements above, the host
kernel generates no IN_MODIFY for mapping writes under any
sync/close ordering; the emulator matches by generating none. No
code change; the disposition is written into kinotify.cpp's
coverage comment. FD-59 closes with one disposition per half
(CL-32).

## FD-60 — link() (was: copies the content)

Before (RED): the new name had its own inode (a=1, b=2 in the
main RED; a=1, b=3 in the x64 RED), a write through /b.txt was
invisible through /a.txt, no IN_ATTRIB fired on the source, the
count stayed 2 after unlinking one name, and a "hard link" of a
symlink was not a symlink.

Fix: the new name is a second node carrying the source's node id
(`Fs::addFileNodeWithSharedId`); both nodes join a link group
(`FsNodeLinkGroup` — live membership is the count for non-file
nodes; host-file nodes report the host st_nlink, which is exact
because the guest's names are mirrored by a host `::link()`
between the native paths). Writes through either name land in
the same host file; unlinking one name leaves the other's data
intact. File-level inotify events fan out to all group members:
the originating name sweeps its parent with its own name, other
members get the file-level sweep only (their parents see
nothing — as on Linux); the source's IN_ATTRIB precedes the new
name's IN_CREATE; unlinking one name fires the link-count
IN_ATTRIB on the surviving names' watches. A symlink source
links its .link sidecar, so the new name is a symlink to the
same target. Sources that are not host files are refused with
EPERM plus a BW64-FAIL line, never silently copied. A zip-backed
source is materialized first (`ensurePathIsLocal`, made public
for this).

Residuals: the group-liveness count for a non-file node can lag
while an unlinked name's node is still open (host-file nodes are
exact); path-derived synthetic modes can differ between two
names of one file in path-special cases; hard links already on
disk in a scanned tree still load as separate nodes, as before.

## Proof

- RED: `test-results/fd58-60/red-window-main.log` (13 assertions
  failing), `red-window-x64.log` (18, including fchmod/fchmodat
  through the real dispatcher).
- GREEN: `green-window-main.log`, `green-window-x64.log` — the
  test passes in both suites, neighbouring inotify tests green.
- Full native suites, serial: main 665/665, 0 FAILED in 262 s (full-main-serial.log);
  x64 64/64, 0 FAILED in 21 s (full-x64-serial.log).
- JS unit suite (the fsnode mock compiles fsnode.cpp):
  the fs-dynamic-link-cache test, which
  compiles slices of the changed fs.cpp/fsnode.cpp against an
  in-memory mock, passes standalone and in the runner. The full
  unit run in this un-bootstrapped lane tree: 272 passed / 76
  failed, every sampled failure an ERR_MODULE_NOT_FOUND for
  generated web/vendor files the lane tree never generated
  (js-unit.log); the full JS run on a bootstrapped tree belongs
  to the merge gate (integ baseline 348/0). The mock initially
  caught a real defect of this lane: addFileNodeWithSharedId had
  been placed inside the mock's extracted fs.cpp slice, where
  FsFileNode is undeclared; it was moved below addFileNode
  (eb53fe34a).
- One earlier full-suite attempt with the runner's default
  parallel workers died with a host segfault deep into a
  memory-fault test's output under heavy shared-box load (615 OK,
  0 FAILED before the death; the affected tail window passes
  standalone, and the serial re-run above is the gate-standard
  measurement).

## Found and filed on this branch

- FD-79 (Low): IN_CLOSE_WRITE reported at fd close while a
  writable mapping is still open; the host defers it to munmap.
- FD-80 (Low): 64-bit preadv (295) / preadv2 (327) have
  name-table entries but no dispatch case → ENOSYS (pread64
  itself is implemented). Found by inspection during the
  read-path work.

## Register changes on this branch

FD-58 → CL-30, FD-60 → CL-31, FD-59 → CL-32 (numbers
provisional; the merge lane re-audits), FD-79 and FD-80 filed
open, and a Section B row for this lane.
