# FD-132 — a null path pointer is erased to the empty string before the empty-path route

Lane `lane-fd132`, based on `lane-fd131` tip `8efc6f564`. Tests
`33be78c82`, fix `a6718ffd7`, this record and the register changes
in the lane's closing commit. Evidence in `test-results/fd132/`.
Register closure CL-78 (lineage-provisional; the batch-5 merge
audit renumbers this lineage's entries, so this record describes
the defect by substance throughout).

## 1. The defect, by substance

A guest path system call receives a pointer to a path string. For
a null pointer, the host kernel's name fetch faults: the call
answers -EFAULT (bad address), before any lookup and — at the *at
forms — before descriptor resolution. The emulator never saw the
null pointer:

- On the 32-bit side, every path helper reads its path through
  `KMemory::readString`, which returns the empty string for a null
  address *by construction* (`if (!address) return
  BString::empty;`). A null path was therefore indistinguishable
  from an empty one by the time it reached the process layer, and
  FD-131's (correct, for "") guards answered -ENOENT for it.
- On the 64-bit side, the helper-routed calls (open/openat, the
  stat family, readlink) and FD-126's two guarded sites already
  checked the raw address and answered -EFAULT. The dispatcher's
  open-coded cases — the access family, mkdir/mkdirat,
  symlink/symlinkat, link/linkat, rename/renameat/renameat2,
  unlink/unlinkat — copied the path with `memcpyFromGuest` and
  carried no null check, so a null pointer became a zeroed buffer
  and took the same empty route.

The empty route is not a benign wrong answer: the shared resolver
resolves "" to the directory node itself, so the base's answers
for a null path included -ENOENT (most calls), -EEXIST at the
create-style cases (mkdir, a symlink's linkpath side), -EPERM at
link's old side, and outright **success at unlink/unlinkat** — a
guest passing a null pointer to unlink was told the call worked.

## 2. Host measurement (first, before any product change)

`test-results/fd132/hostref-fd132.c`, run on the box kernel
(aarch64; libc wrappers and `syscall()` with the `<sys/syscall.h>`
numbers — the asm-generic set, per the campaign's standing
caution). The matrix, in substance:

- **-EFAULT for a null path at every plain call**: open, stat,
  lstat, access, chmod, chdir, mkdir, rmdir, unlink, rename and
  link on either side, symlink on either side, readlink, utimes,
  statfs, getxattr, lgetxattr (and at getxattr a null attribute
  *name* faults too, once the path has resolved).
- **-EFAULT at every *at form**, ahead of descriptor resolution
  (an unopened dirfd still answers -EFAULT): openat, mkdirat,
  unlinkat, renameat, symlinkat, readlinkat, faccessat.
- **Three exceptions**, each measured across the full compound
  grid (AT_FDCWD / directory fd / file fd / unopened fd × flags):
  - `newfstatat` and `statx`: a null path **with** AT_EMPTY_PATH
    is a working descriptor form — a valid descriptor or AT_FDCWD
    answers the descriptor's stat (0), an unopened one -EBADF.
    Without the flag, null answers -EFAULT everywhere.
  - `faccessat2`, `fchmodat`, `linkat`: **no** null descriptor
    form — null faults even with AT_EMPTY_PATH.
  - `utimensat`: a null path answers **-EINVAL in every shape**
    (any dirfd incl. unopened, null or valid times, with or
    without AT_EMPTY_PATH). The kernel special-cases the null
    filename before any name fetch; this is the one family where
    the filing's "-EFAULT" does not hold, and the fix follows the
    measurement (recorded as a correction in CL-78).
- `inotify_add_watch`: the descriptor is validated **before** the
  path fetch — a good descriptor with a null path answers
  -EFAULT, a bad one -EBADF (the same order FD-131 measured for
  the empty path).

Measurement-integrity note: this lane's first hostref run printed
the raw-`syscall()` rows' codes wrong (the printer read the
return value as -errno; `syscall()` returns -1 with the code in
errno, FD-126's printer being the correct model). The rows were
re-measured with the corrected printer before any use; the
preserved output is the corrected run. The same printer shape
appears in FD-131's hostref, so that file's raw-syscall rows show
"-1 (Operation not permitted)" where the true codes are the ones
its guest pins enforced; flagged here for the merge audit, as
FD-131's own conclusions rested on its libc rows and pins, not on
those printed codes.

## 3. Where the fix sits, and why

At the dispatcher boundary, on the raw guest address, ahead of
the reader/copy — the same placement FD-126 used for its two
sites. The reader itself is unchanged (FD-126's precedent: its
other consumers — argv/envp element walks, logging reads — are
not this defect, and several rely on the null-to-empty mapping).
The process layer is unchanged: FD-131's guards there already
answer correctly for the empty string; the defect was that a
*different* input (null) was being rewritten into that string
before arrival. The shared resolver is untouched, for FD-131's
reason (internal callers pass "" deliberately).

## 4. What changed

`source/kernel/syscall.cpp` (32-bit dispatcher) — a guard on the
path address at each helper that reads a path through the string
reader, returning -EFAULT before the read:

- Plain: open, link (both sides), unlink, execve, chdir, chmod,
  access, rename (both sides), mkdir, rmdir, symlink (both
  sides), readlink, statfs, statfs64, stat64, lstat64, utimes.
- *at (path argument): openat, mkdirat, unlinkat, fchmodat,
  faccessat, renameat (both sides), symlinkat (target and
  linkpath).
- Conditioned: faccessat2 faults null unconditionally (no null
  descriptor form, even with AT_EMPTY_PATH); fstatat64 and statx
  fault null **only without** AT_EMPTY_PATH (with the flag, null
  keeps the descriptor form the "" route already implements).
- utimensat and utimensat_time64 answer **-EINVAL** for a null
  path (the measured exception).
- inotify_add_watch: for a null path the helper mirrors the
  descriptor-first order — it validates the descriptor exactly as
  the process layer does and answers -EBADF for a bad one,
  -EFAULT for a good one.
- getxattr/lgetxattr: the path pointer is guarded ahead of the
  reader, and the attribute **name** pointer is guarded after
  FD-131's empty-path resolution (preserving its measured order:
  an empty path still answers -ENOENT before the name is
  considered; a null name with a resolvable path answers
  -EFAULT, as the host does).

`source/kernel/syscall64.cpp` (64-bit dispatcher) — a guard on
the raw address at each open-coded case, ahead of the copy,
returning -EFAULT: the access family (access/faccessat/
faccessat2, unconditioned — no null descriptor form), mkdir/
mkdirat, symlink/symlinkat (both arguments), link/linkat (both
arguments, unconditioned), rename (both arguments) and
renameat/renameat2 (both arguments; placed after renameat2's
existing flags rejection, which keeps answering -EINVAL first),
unlink/unlinkat, chdir, and inotify_add_watch (with the
descriptor check mirrored, as on the 32-bit side).

Deliberately unchanged: the string reader, the resolver, the
process layer, the never-resolving ignored stubs that never
reach a path (the chown family, mount, utime — FD-131's
precedent), the 64-bit stat helpers (they already honoured both
null shapes), the 64-bit utimensat and statfs stubs (FD-136 and
FD-135 respectively), and memfd_create's name (FD-137).

## 5. Proof

New guest tests, both staging a null path at representative calls
on their dispatcher against the host's answers, with empty-path,
real-path, and already-correct controls:

- `testNullPath32Syscalls` (main suite, through int 0x80):
  **RED on the base — 40 assertions failing**, every one with the
  erased answer (-ENOENT for the empty route, the descriptor form
  answering 0 for a null pointer at faccessat2/utimensat with
  AT_EMPTY_PATH, -ENOTSUP for the erased xattr name, -ENOENT where
  utimensat's measured answer is -EINVAL); all 7 controls passing
  on the base. **GREEN with the fix.** (`red-main-null32.log`,
  `green-main-null32.log`.)
  One test-writing note: the 32-bit harness's
  `testNewInstruction` resets the whole test heap, so the fixture
  strings are re-staged inside the invoke helper after each code
  emission; the first RED draft's real-path control failure was
  this harness behaviour, diagnosed and corrected before the RED
  was taken.
- `testNullPath64Syscalls` (x64 suite, through ksyscall64):
  **RED on the base — 24 assertions failing** (-ENOENT at the
  access family/rename/chdir/inotify shapes, -EEXIST at mkdir and
  the symlink/linkpath create shapes, -EPERM at link's old side,
  **0 — success — at unlink/unlinkat**, the descriptor form at
  faccessat2 with AT_EMPTY_PATH); all controls passing, including
  the stat helpers' null descriptor form. **GREEN with the fix.**
  (`red-x64-null64.log`, `green-x64-null64.log`.)

Gates on the fixed tree, all serial: full native suites **main
670/670 and x64 92/92, 0 FAILED** (`suite-main-green.log`,
`suite-x64-green.log`); JS unit **348 passed / 0 failed**
(`js-unit.log`); browser build **BUILD COMPLETE, "Browser export
verified"**, staged dependency archive `c4dbe35986a5` as pinned
(BUILD_INFO git_sha `a6718ffd7`, wasm sha256 `650f80f9`)
(`browser-build.log`); `tools/known-gaps-audit.mjs --check`
passes — the guards answer through the existing idioms and add
no counted silent-failure site.

## 6. Filed from this lane

- **FD-136** — the 64-bit dispatcher's utimensat stub answers
  success for a null path pointer; the host answers -EINVAL in
  every shape (measured, §2). FD-131's stub comment records the
  null shape as deliberately "never dereferenced"; the
  measurement post-dates that choice. The 32-bit side is a real
  call and was fixed by this lane; the stub stands.
- **FD-137** — a null memfd_create name is erased to the empty
  string on both dispatchers (the 32-bit helper's string reader;
  the 64-bit case copies the name only when the pointer is
  nonzero), so the call creates the file where the host faults
  the name copy. Measured separately
  (`hostref-fd132-memfd.c`: null → -EFAULT, "" → a descriptor).
  The name is a label, not a path, so it sat outside FD-132's
  closing terms; the mechanism is the same erasure.

Observed, not filed: i386 **linkat has no helper at all** — a
nullptr entry in the 32-bit dispatch table answering -ENOSYS for
every input. That is an unimplemented call (the FD-23 census's
territory), not the null-erasure, and this lane's test
deliberately does not pin it.

## 7. What this lane did not touch

No KProcess or resolver change; no reader change; no change to
any answer for a non-null path (every FD-131 empty-path pin and
every pre-existing suite test stands: the full suites are green
at exactly the sibling counts plus this lane's two tests). The
ignored-stub family (chown/mount/utime and friends) keeps its
standing stub answers, as FD-131 left them.
