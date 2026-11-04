# FD-136 / FD-138 — the timestamp call's null-path matrix, and the memory-file name limit (2026-11-03)

Lane `lane-fd136-138`, based on `lane-fd136-137` tip `a4b1ce100`.
Tests `e2f19ce23`, fix `4abf51d88`, this record and the register
changes in the register commit that follows.

## The defects, by substance

1. **The timestamp call (utimensat) with a null path answered one
   flat thing per dispatcher, and neither was the kernel's
   answer.** On the 32-bit side a guard installed by the FD-132
   lane answered -EINVAL for every null shape; on the 64-bit side
   the stub answered success for every null shape. The guard's
   -EINVAL came from measuring the host through its C library:
   glibc's utimensat() wrapper refuses a null path in userspace
   and never issues the syscall, so the "host measurement" was
   the wrapper's contract, not the kernel's. The raw kernel's
   answers are per-shape (measured below): the descriptor form —
   an open descriptor, a null path, flags 0 — succeeds, and it is
   not an exotic shape: it is exactly what a guest's
   descriptor-based timestamp call (futimens) issues. The flat
   -EINVAL broke that shape on the 32-bit side, and the flat
   success on the 64-bit side told guests that shapes the kernel
   faults or refuses (the current-directory shape, flagged
   shapes, unopened descriptors) had worked.

2. **The memory-file creation call (memfd_create) accepted names
   of any length on both dispatchers.** The host refuses names
   longer than 249 chars with -EINVAL; the emulator validated no
   length anywhere on the path, so an over-limit name created the
   file and returned a descriptor.

3. **Filed, not fixed (FD-139): the same call accepts undefined
   flag bits on non-null paths.** The kernel validates the flags
   word ahead of the name fetch for every shape, so a non-null
   path with a bit outside AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH
   answers -EINVAL; both dispatchers consult only the bits they
   use and proceed. Found in this lane's measurement (the control
   row below), filed with its own closing terms rather than
   folded into FD-136, whose scope is the null path.

## Host measurement (raw kernel level)

`test-results/fd136-138/hostref-fd136b.c` issues the call through
an inline `svc`, so the return value is the kernel's own and no
wrapper can answer in its place; libc calls are printed beside
the raw rows as cross-checks only. One trap is worth recording
for the next lane: the asm-generic number for this call on the
box's aarch64 kernel is 88, not the x86-64 280 (280 is a
different call there — bpf — and answered -E2BIG for every row
of this program's first run). Only the non-null control row
exposed it: a real path cannot answer -E2BIG at this call, and
the libc descriptor-form call beside it succeeded. The numbers
now come from the box's own headers, and the program prints the
number it used. (memfd_create's 279 is the same on both
numberings, which is why its rows were right even in the first
run.)

The measured matrix for utimensat(dfd, NULL, times, flags),
valid or null times unless noted:

- flags with a bit outside {AT_SYMLINK_NOFOLLOW (0x100),
  AT_EMPTY_PATH (0x1000)}: **-EINVAL**, every dfd shape —
  the flags verdict comes first among the flag shapes.
- dfd = AT_FDCWD, flags 0 / AT_EMPTY_PATH / AT_SYMLINK_NOFOLLOW /
  both: **-EFAULT** (the name fetch faults; there is no null
  special-case ahead of it).
- dfd = a valid descriptor (directory, file, pipe all measured),
  flags 0: **0** — the descriptor form. Functional rows: the
  call really sets the times; fstat afterwards shows the staged
  mtime on both a file and a directory descriptor.
- dfd = a valid descriptor, any nonzero flags: **-EINVAL**.
- dfd = an unopened descriptor (or another negative value),
  flags 0: **-EBADF**; with any nonzero flag: **-EINVAL**.
- An unreadable non-null times pointer answers **-EFAULT**
  ahead of everything, including the invalid-flags verdict and
  the descriptor form (the kernel copies the times first).
- Controls: a real relative path answers 0, with invalid flags
  -EINVAL (the FD-139 filing); an empty path answers -ENOENT,
  and with AT_EMPTY_PATH 0 — FD-131's answers, unchanged.
- libc cross-checks: futimens(fd, ts) = 0, while libc
  utimensat(fd, NULL, ts, 0) = -1/EINVAL — the wrapper refusing
  in userspace, beside the raw row's 0 for the identical shape.
  That pair is the whole FD-132-era mistake in two lines.

memfd_create, raw: 249 chars returns a descriptor; 250 and 300
answer -EINVAL; a null name answers -EFAULT (FD-137's ground);
an empty name creates.

## The fix

- 64-bit dispatcher (the utimensat stub in syscall64.cpp): the
  null path now walks the matrix inline — times-readability
  verdict first (an unreadable non-null times pointer faults,
  as measured, ahead of every other verdict and now for the
  case's non-null shapes too, where the kernel's copy order is
  the same), then invalid flag bits -EINVAL, then the
  current-directory shape -EFAULT, then any other flag -EINVAL,
  then an unopened descriptor -EBADF, else success. FD-131's
  empty-path guard is untouched below it. The stub applies no
  times on any shape, so the descriptor form is the success
  answer alone there.
- 32-bit dispatchers (syscall.cpp): the FD-132 -EINVAL guards in
  the utimensat and utimensat_time64 helpers now route a null
  path to a new process method, `KProcess::utimensatNullPath`,
  which answers the same matrix and, for the descriptor form,
  applies the times to the descriptor's node through the
  parse-and-set tail the path form already used (extracted as
  `KProcess::setNodeTimes`, behaviour unchanged). Descriptors
  whose node stores no times (a pipe's, a memfd's) still answer
  the kernel's success; the application is best-effort there.
- memfd_create: one length check in `KProcess::memfd_create`,
  behind the existing flags verdict, so both dispatchers inherit
  it. The 64-bit case's 255-char copy cap sits above the limit,
  so an over-limit name still measures over the limit when it
  arrives at the process layer.

## Proof

Guest tests first, RED on the base, then the fix. The FD-132
test's stale utimensat pins and its header's wrapper claim are
corrected in the tests commit (the reconciliation is also
written into the register at CL-78, and CL-80 carries it).

- Main suite RED (base + tests): 669 tests OK, exactly the two
  target tests failing — testNullPath32Syscalls with 14
  utimensat assertions answering the old guard's -EINVAL
  (including the pin that the descriptor form really applies
  the times: the fixture file's mtime was its creation time,
  not the staged one) and testMemfdNullName32Syscalls with the
  250- and 300-char names returning descriptors.
- x64 suite RED: 91 OK, exactly the two target tests failing —
  testNullPath64Syscalls with 11 utimensat assertions answering
  the stub's blanket success, and the same two name lengths.
- GREEN on the fix: main **671/671**, x64 **93/93**, 0 FAILED,
  serial, one worker (logs
  `test-results/fd136-138/test-*-fd136-138-green.log`).
- JS unit: **348 passed / 0 failed**. (A first run before the
  browser build showed export-dependent failures; with the
  export built the suite is clean — the failures were the
  missing export, not the change.)
- Browser build: BUILD COMPLETE, "Browser export verified"
  (BUILD_INFO git_sha `4abf51d88`, wasm sha256 `e4d4260b`,
  staged dependency archive `c4dbe35986a5` as pinned).
- known-gaps audit `--check`: passes.

## Setup notes (for whoever reproduces this tree)

Fresh worktree off `a4b1ce100`: node_modules symlinked from the
main tree; dxvk build.w32/build.w64 and the gstreamer32/64
payloads copied from `wt-fd38` (the main tree's generated dxvk
outputs have drifted from their pin; the pinned-vintage copies
there verify as `c4dbe35986a5` in the build above); vkd3d
build.w64 from `wt-restage`, build.w32 from `wt-x86vkd3d`;
runtime assets bootstrapped `--local` from the
`winbox-runtime-assets-20261101` archive this branch's manifest
pins ("Restored and verified 13 runtime assets").

## For the merge audit

Closure numbers CL-80 (FD-136) and CL-81 (FD-138) and the new
entry FD-139 are this lineage's provisionals, like every lane's;
renumber by substance if they collide. The register's CL-78
entry carries a dated reconciliation note: its utimensat
exception rested on the wrapper measurement and is superseded
by CL-80; the rest of CL-78 stands.
