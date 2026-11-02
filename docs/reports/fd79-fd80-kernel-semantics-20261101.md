# FD-79 + FD-80 — kernel semantics, second pair (2026-11-01)

Lane `lane-fd79-80`, off integ `1d932714c`. Two Low entries filed by
the FD-58..60 lane, worked together because both live in the
read/notify surface it opened. Host references were re-measured for
this lane on the box's 6.17.0-1020-oracle kernel (programs preserved
under `test-results/fd79-80/`).

## FD-79 — close events vs mapping lifetime

### What the host does (ref_closewrite.c, 11 cases)

Any live mapping of an open file description — MAP_SHARED or
MAP_PRIVATE, read or write, including a mapping mprotect'd to write
after creation — defers the description's close event
(IN_CLOSE_WRITE, or IN_CLOSE_NOWRITE for a read-only description)
until the last such mapping is unmapped. With no mapping live the
event fires at close; unmapping before close returns it to the
close; a partial unmap does not fire it; two mappings hold it until
the second is gone; a second, unmapped description of the same file
fires at its own close while the first description's event stays
deferred.

### What the emulator did (measured, RED)

Worse than the filed text ("reports the close at fd close"): on the
demand-paged path the event was **lost in-session**. The close event
is generated from the open node's close(), reached through the KFile
destructor; a mapped file's KFile is pinned by its MappedFile
entries (never reaped on unmap) and by the system file cache, so
neither close nor munmap delivered the event — it surfaced only at
process teardown. The RED test
(`testCloseWriteDeferral`, both native suites) fails on the base in
five places: no arrival at munmap (shared-writable, two-mapping,
private-read/NOWRITE, and the two-description case) and no arrival
at close after an unmap-before-close. Its "nothing at close while
mapped" assertions passed on the base vacuously, by that same
pinning; the fix had to add delivery without adding earliness.

### Fix

The contract is now tracked explicitly at the open description:

- `FsOpenNode` gains the deferral state: a live mapped-page count,
  a pending flag, a delivered flag (`signalCloseEvent`,
  `noteMappedPagesAdded/Released`, `resetCloseEventState`).
  `FsFileOpenNode::close()` and `FsMemOpenNode::close()` route their
  notification through it — delivered at once when no mapped pages
  are live, held pending otherwise. A destructor backstop delivers
  a still-pending event rather than losing it (teardown paths that
  drop pages without an unmap), and `reopen()` re-arms the state.
- `KProcess::close()`: when the close drops the last descriptor
  **this process** holds for a file/mem open node, it signals the
  close event at close time (previously the only signal was the
  destructor, which the pins above make unreachable in-session for
  mapped files).
- `KMemory` reports page births and deaths: mapped-page counts are
  added at the three MappedFile creation sites (mmap, the
  mremap old-size-0 duplicate, the mremap shared move/grow), and
  deaths are reported by `noteMappedFilePagesDying()` before pages
  are invalidated or replaced — called from `unmap()`, from
  `mmap()` (overwrite of a live range), and for the whole address
  space from `KProcess::execve()` before its reset (the mapping
  table is still intact there). A File page's entry carries its
  mapping key, so deaths resolve to exactly the description that
  deferred. One subtlety the first version of this fix missed and
  the test caught: a write fault converts a file page to
  CopyOnWrite, whose entry names the shared cache page rather than
  the mapping — so CopyOnWrite deaths are attributed by range, to
  the newest mapping-table entry containing the page (the same
  selection `getMappedFileForRange` makes). When a description's
  count reaches zero with a close pending, the event is delivered
  there — the munmap moment.

### Boundaries (stated exactly)

- Descriptions shared with **another process** (fork-inherited):
  the close is signalled by the first process to drop its last
  descriptor, and page deaths are counted across the sharers
  through the shared mapping entries, so in the fork corner the
  event can be delivered when the first sharer's pages are gone
  rather than the last (early, never lost; counts saturate at zero
  rather than wrapping). The host ties the event to the last
  system-wide reference. Single-process behaviour — the filed
  case and the test's contract — is exact.
- The **64-bit guest memory path** materializes file mappings as
  copied/shared pages with no open-description accounting, so a
  64-bit guest's close event still fires at fd close while such a
  mapping is live. Filed separately as FD-82.
- FsZipOpenNode never generated close events and still does not;
  untouched.

### Proof

`testCloseWriteDeferral` (registered in both native suites): RED on
the base (5 assertions), GREEN with the fix — all six cases: shared
writable deferral, unmap-before-close, two mappings, private-read
NOWRITE deferral, plain close, and the two-description split.
Full native suites, serial: main 668/668, 0 FAILED (544 s;
667 baseline + this test) and x64 68/68, 0 FAILED (66 baseline +
this test and the FD-80 test). JS unit suite: 348 passed /
0 failed (two reviewed harness adjustments on this branch: the
exec source-extraction test's Memory stub gained the new
noteAllMappedFilePagesDying collaborator with the same
quiescence assertion as execvReset — the real call site runs
after killAllThreads — and the silent-failure baseline for
syscall64.cpp was reviewed 112 -> 115 for preadv's three
guest-validation returns, the same unlogged shape as the
neighbouring calls). Browser build on the fixed tree (clean
staging: retained-archive bootstrap, FD-73 archive-agreement
PASS): "Browser export verified" / BUILD COMPLETE, wasm
ea871129..., support archive c4dbe35986a5 unchanged.

## FD-80 — 64-bit preadv (295) / preadv2 (327)

### What the host does (ref_preadv.c, ref_preadv2.c, ref_preadv3.c)

Segments fill in order at successive offsets; the file position
does not move; a short read at EOF ends the walk; a zero-length
segment is skipped. IN_ACCESS is generated exactly once per
successful call — **including calls that return 0** (at EOF, or
with iovcnt 0): the vectored path generates it where plain
pread/read stay silent at 0 bytes. Any negative offset is EINVAL
(raw-syscall -1 measured: EINVAL — there is no readv fallback on
this kernel; the glibc wrapper agrees). Negative iovcnt or
iovcnt > IOV_MAX is EINVAL. An unknown preadv2 flag bit is
EOPNOTSUPP (95); every defined RWF flag (HIPRI/DSYNC/SYNC/NOWAIT/
APPEND) is accepted on a read. A bad descriptor is EBADF.

### Fix

`sys_preadv64` in the 64-bit dispatch, over a factored
`sys_pread64Impl` (the existing pread64 body, with its IN_ACCESS
notification gated by a flag so the wrapper's behaviour is
byte-for-byte the old one). preadv validates in host order —
descriptor, preadv2 flags (EOPNOTSUPP outside the 0x1f mask),
iovcnt (EINVAL outside 0..IOV_MAX), offset (EINVAL when negative),
iov pointer (EFAULT when null with iovcnt > 0) — then walks the
iovec calling the positioned core per segment with notification
suppressed, and generates exactly one IN_ACCESS on success,
including 0-byte success. Dispatch cases added for 295 and 327
(the name table already had both).

One deliberate house-shape choice: a descriptor that cannot be read
answers -EINVAL, inherited from pread64's core. The host answers
EBADF for the positioned-read family; that divergence predates this
lane, applies to pread64 equally, and is filed as FD-84 rather than
fixed inside a wiring change.

### Proof

`testPreadv64Syscalls` (x64 native suite): RED on the base (every
call -ENOSYS), GREEN with the fix — data/position/segment walk,
short read at EOF, EOF and iovcnt-0 IN_ACCESS, the validation
matrix, pos_h honored (offset 2^32+2 is EOF, not offset 2),
zero-length segment skip, the preadv2 flag matrix, and the
write-only descriptor answer.
Full native suites, serial: main 668/668, 0 FAILED (544 s;
667 baseline + this test) and x64 68/68, 0 FAILED (66 baseline +
this test and the FD-80 test). JS unit suite: 348 passed /
0 failed (two reviewed harness adjustments on this branch: the
exec source-extraction test's Memory stub gained the new
noteAllMappedFilePagesDying collaborator with the same
quiescence assertion as execvReset — the real call site runs
after killAllThreads — and the silent-failure baseline for
syscall64.cpp was reviewed 112 -> 115 for preadv's three
guest-validation returns, the same unlogged shape as the
neighbouring calls). Browser build on the fixed tree (clean
staging: retained-archive bootstrap, FD-73 archive-agreement
PASS): "Browser export verified" / BUILD COMPLETE, wasm
ea871129..., support archive c4dbe35986a5 unchanged.

## Filed by this lane

- FD-82 (Low): 64-bit guest file mappings (the materialized-copy
  path) do not participate in FD-79's close-event deferral; a
  64-bit guest still sees IN_CLOSE_WRITE / IN_CLOSE_NOWRITE at fd
  close while such a mapping is live.
- FD-83 (Low): readv generates no IN_ACCESS when it succeeds with
  0 bytes (at EOF, or with iovcnt 0); the host generates exactly
  one. The emulator's readv notifies per segment through the plain
  read path, whose bytes>0 rule is correct for read/pread but not
  for the vectored calls. preadv/preadv2 follow the host rule as of
  FD-80.
- FD-84 (Low): the positioned/plain read family answers -EINVAL on
  a descriptor that cannot be read (pread64's core check, inherited
  by preadv; KProcess::read does the same); the host answers
  -EBADF.

## Register changes on this branch

FD-79 → CL-37, FD-80 → CL-38 (numbers provisional — CL-35 and CL-36 were taken provisionally by the FD-54 and FD-63 lanes; the merge lane
re-audits), FD-82 / FD-83 / FD-84 filed open, and a Section B row
for this lane.
