# FD-97 — the 64-bit single-call write paths answer for unreadable guest buffers

Register: FD-97 carried verbatim from `lane-fd94-96`'s register
(tip `a50afdada`), closed here as CL-51 (provisional). FD-101
filed (the sendmsg gather form of the same trait).

## The defect

`KMemory64::memcpyFromGuest` zero-fills its whole destination when
any part of the guest range fails its access check, and the 64-bit
single-call write paths copy the guest buffer through it before
writing: `sys_write64` and `sys_pwrite64`
(`source/kernel/syscall64.cpp`) handed the file whatever the copy
produced, so a write from an unmapped or unreadable guest address
landed that many zero bytes in the file and returned success.

## Host contract, measured first

`test-results/fd97/hostref-fd97.c` on the box's host kernel
(6.17.0-1020-oracle), regular file:

- write/pwrite from a fully unmapped buffer: -EFAULT, file size 0.
- write crossing from a mapped page into an unmapped one: the
  readable prefix is written as a short count (96 of 200; 1 of 2
  starting at the mapped page's last byte), and the landed bytes
  are the real prefix bytes. pwrite crossing at offset 8: 96,
  size 104.
- write from a PROT_NONE page: -EFAULT.
- write from a PROT_WRITE-only page: succeeds with the real bytes
  (the kernel's copy reads it). The guest page flags agree: the
  emulator's prot mapping sets READ for write-only pages ("x86:
  write implies read", kmemory64.cpp), so one flags predicate
  (mapped + READ) is the guest's own readability model on both
  sides of the comparison.
- write count 0 with a bad buffer: 0.
- write with a bad fd and a bad buffer: -EBADF — the descriptor
  answer precedes the copy fault.

The filed entry's "the host answers EFAULT and writes nothing"
was measured for a bad first segment; the crossing cases refine
it: EFAULT is owed only when no byte is readable. The fix
implements the measured contract, not the simpler uniform one.

## The fix (commit `c734fe89d`)

One helper, `sys_guestReadablePrefix64`, counts the leading
readable bytes of the guest range page by page under the same
predicate the copy's own access check applies (mapped + READ via
`KMemory64::getPageFlags` — the flags source
`sys_rangeHasPermission64` and the process_vm paths already use).

- `sys_write64`: the count becomes the readable prefix before the
  copy; only the prefix is copied and written. The -EFAULT for a
  fully unreadable buffer is returned at the write point, after
  the descriptor and mode answers, so the measured EBADF order
  survives (a bad fd with a bad buffer still answers -EBADF, and
  the wrong-mode answer keeps its place ahead of the fault).
- `sys_pwrite64`: the same, after its existing descriptor/mode
  answers; a prefix pwrite seeks to the offset, writes the
  prefix, and restores the position exactly as before.
- `sys_writev64` walks `sys_write64` per segment, so it inherits
  the measured vectored shape with no change of its own: a bad
  first segment answers -EFAULT, a bad later segment returns the
  bytes already written. `sys_pwritev64` (FD-94, on its own
  branch) already range-checks per segment and is untouched.

Fully readable buffers take the identical path as before — the
prefix is the whole count and the copy proceeds as today
(including per-page zero reads for reserved-uncommitted pages,
which pass the predicate exactly as they pass the copy's own
check).

## Audit of the other copy sites

Every `memcpyFromGuest` call site in syscall64.cpp was checked
for the same trait:

- process_vm_writev/readv: already range-checks both sides with
  `sys_rangeHasPermission64` and answers -EFAULT / partial
  counts. Not affected.
- The mmap file path's head/tail saves and `readGuestString64`:
  kernel-internal copies (page preservation around a mapping;
  string reads whose failure mode is an empty string), not guest
  data written to a file. Not this defect.
- `bounceSockaddrTo32`: a bounded (<=256 byte) address struct for
  the socket layer, not file data. Not this defect.
- `sys_sendmsg64`'s segment gather: the same trait in the
  vectored socket path — filed as FD-101 with its own host
  measurement (`test-results/fd97/hostref-sendmsg.c`: a fully
  unreadable segment base answers -EFAULT on the host and sends
  nothing; the emulator sends fabricated zeros), left for its
  owning lane.

## Proof

- New test `testWriteBadBuffer64Syscalls` (x64 suite), commit
  `6d7c28d00`: twelve cases through the real 64-bit dispatch —
  unmapped and PROT_NONE writes/pwrites (-EFAULT, nothing
  landed), the control write, both crossing shapes, pwrite
  position preservation, count-0, the EBADF-order guard, both
  writev shapes, and the write-only-page guard. RED on the base
  (`test-results/fd97/red-x64.log`, the suite's only failure):
  "write from an unmapped buffer returned 16, file holds 16
  bytes"; crossing write returned 200; writev with an unmapped
  second segment returned 24. GREEN on the fix
  (`green-x64.log`).
- Full native suites, serial, 1 thread: main 668/668, x64 70/70,
  0 FAILED (`green-main.log`, `green-x64.log`).
- Browser build of the fixed tree: BUILD COMPLETE, "Browser
  export verified"; BUILD_INFO git_sha = `c734fe89d` (the fixing
  commit), wasm `a1300a80`, support archive `c4dbe35986a5`
  unchanged (`build-browser2.log`). The first attempt stopped at
  asset staging (the manifest's release URL 404s by design);
  staging was completed from the retained archive
  (`bootstrap.py --local`, runtime-assets-20261101, this base's
  pins) and the build then ran clean.
- JS unit: 348 passed, 0 failed (`js-unit-2.log`). The first run
  (`js-unit-1.log`) was taken before the browser build staged
  dist/ and failed only in artifact-reading families
  (provenance-artifacts, runtime-completeness, the gl/vk/
  spirvfix groups) plus a probe-slots timing failure under load;
  all clear on the re-run. The no-silent-failure baseline is
  unchanged: its syscall64.cpp counter matches only
  ENOSYS/EINVAL-class returns, and the new answers are EFAULT,
  so the count stays 115 with no bump to review. (On the composed
  tree the count remains whatever the composing lanes' bumps
  settle to; this lane adds nothing to it.)

## Boundaries kept

- The wrong-mode answer in the write family stays -EINVAL on
  this base — that is FD-93's registered territory on
  lane-fd92-93, untouched here; the EFAULT sits behind it exactly
  where the host's copy fault sits behind the mode check.
- sys_write64's no-process early return now reports the readable
  prefix (0 for an unreadable buffer) instead of the requested
  count; that path serves emulator-internal contexts with no
  guest process, where no descriptor exists to write to either
  way.
- The 32-bit write paths run over the 32-bit memory model and
  were outside the filing; FD-97 was filed against the 64-bit
  paths only and is closed on those terms.
