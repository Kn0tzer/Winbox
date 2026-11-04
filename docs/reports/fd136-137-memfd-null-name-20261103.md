# FD-136 / FD-137 lane record — the memfd null name fixed on both dispatchers; the utimensat filing contradicted by raw-kernel measurement

Branch `lane-fd136-137`, based on `lane-fd132` tip `dcb9c93a0`.
Date: 2026-11-03. Lane tip at the register commit (see git log).

## 1. Outcome

- **FD-137 — CLOSED (CL-79).** A null `memfd_create` name was
  erased to the empty string on both dispatchers and the call
  created the file; the host faults the name copy (-EFAULT). Both
  dispatchers now guard the raw guest address ahead of the name
  copy/read and answer -EFAULT, with the host's validation order
  preserved (invalid flags answer -EINVAL first). Fix `0e5d33859`,
  guest tests `dd47a7a50`.
- **FD-136 — NOT fixed; its filing is amended in place and stays
  open.** The filing's host claim ("the host answers -EINVAL in
  every shape" for a null path at the 64-bit dispatcher's
  utimensat stub) is the host *libc wrapper's* answer, not the raw
  kernel's. Re-measured at the raw syscall level (§2, §4), the
  kernel's answers are per-shape, and the shape the filing calls
  success-for-null is in one common case the kernel's own working
  answer — it is the exact shape glibc's `futimens()` issues.
  Implementing the filing as written would have broken guest
  `futimens()` at the 64-bit dispatcher. Per the lane's
  instructions, the contradiction is reported here and in the
  register entry, with corrected closing terms for the merge
  audit. No product code was changed for FD-136.
- **FD-138 — filed.** `memfd_create` accepts names longer than
  the host's 249-char limit on both dispatchers (§5).

## 2. Host measurement (before any product change)

Two reference programs written for this lane, preserved in
`test-results/fd136-137/`: `hostref-fd137.c` (memfd_create name
shapes) and `hostref-fd136.c` + `hostref-fd136-rawgrid.c`
(utimensat null path). Each prints libc-wrapper and raw
`syscall()` rows side by side, the raw rows reading `errno`
immediately after a -1 return; the headline utimensat shapes were
additionally verified under `strace`. (The sibling lane's warning
was heeded: a raw `syscall()` result was never trusted without a
libc cross-check on the same shape.)

**memfd_create — filing confirmed exactly, libc and raw agree on
every row:** `memfd_create(NULL, 0)` -> -EFAULT; `(NULL,
MFD_CLOEXEC)` -> -EFAULT; `(NULL, MFD_ALLOW_SEALING)` -> -EFAULT;
`("", 0)` -> descriptor; `("label", 0)` -> descriptor; `(NULL,
0x80000000)` -> **-EINVAL** and `("label", 0x80000000)` ->
-EINVAL: the host validates the flags word before it faults the
name copy. Name length: 249 chars -> descriptor; 250 and 319
chars -> -EINVAL (see FD-138).

**utimensat null path — filing contradicted.** The libc wrapper
`utimensat` answers -EINVAL for every null shape (AT_FDCWD,
directory fd, file fd, unopened fd; null or valid times; flags 0,
AT_EMPTY_PATH, AT_SYMLINK_NOFOLLOW) — the filing's matrix. But
`strace` shows the wrapper returning that answer *without issuing
the syscall*: glibc rejects a null path in userspace. The raw
kernel's answers (strace-verified for the headline shapes) are:

| raw utimensat(dfd, NULL, times, flags) | kernel |
| --- | --- |
| AT_FDCWD, flags 0 / AT_EMPTY_PATH / AT_SYMLINK_NOFOLLOW | **-EFAULT** |
| valid dirfd or filefd, flags 0 | **0** (the descriptor form works) |
| valid dirfd or filefd, AT_EMPTY_PATH | -EINVAL |
| valid filefd, AT_SYMLINK_NOFOLLOW | -EINVAL |
| unopened fd, flags 0 | **-EBADF** |
| unopened fd, AT_EMPTY_PATH | -EINVAL |

And the load-bearing consequence, also from strace: glibc's own
`futimens(fd, ts)` enters the kernel as `utimensat(fd, NULL, ts,
0)` and returns 0. On x86-64 there is likewise no futimens syscall
number, so a 64-bit guest's `futimens()` reaches the dispatcher's
utimensat case in exactly the (valid fd, NULL, times, 0) shape.
The FD-132 lane's preserved host grid
(`test-results/fd132/hostref-fd132.out.txt`) shows -EINVAL in all
nine of its utimensat rows, including (filefd, NULL, NULL, 0) —
rows consistent with the wrapper, not the raw kernel; its filing
text ("the kernel special-cases the null filename") rests on that
measurement.

## 3. FD-137 by substance

The defect: `memfd_create`'s name is a label, not a path, so
FD-132's path guards never covered it, but the erasure mechanism
is the same one FD-126/FD-131/FD-132 chased. On the 64-bit side
(`source/kernel/syscall64.cpp`, case `X64_SYS_memfd_create`), the
name copy ran only when the pointer was nonzero —
`if (a1) cpu->memory->memcpyFromGuest(...)` — so a null pointer
passed the zero-initialised buffer, i.e. the empty string, to
`KProcess::memfd_create`, which created the file. On the 32-bit
side (`source/kernel/syscall.cpp`, `syscall_memfd_create`), the
name came from `KMemory::readString(ARG1)`, which returns the
empty string for a null address by construction. The empty string
is a *legal* memfd name on the host, so both sides created the
file and returned a descriptor where the host faults.

The fix (both sites, on the raw guest address, ahead of the
copy/read — the placement FD-126/FD-131/FD-132 established): if
the name pointer is null, answer -EFAULT — except that the host
answers the flags verdict first, so an invalid flags word still
answers -EINVAL. The flags verdict is the process layer's own:
`KProcess::memfd_create` used to keep its accepted-flags mask
(1|2, MFD_CLOEXEC|MFD_ALLOW_SEALING) in a function-local constant;
it is now shared as `KProcess::MEMFD_SUPPORTED_FLAGS`
(`include/kprocess.h`), consumed by the process layer and both
dispatcher guards, so the three cannot drift. Not changed:
`readString` itself (FD-126's precedent — its other consumers are
not this defect), the process layer's create path, and the legal
empty name.

Proof: new guest tests `testMemfdNullName32Syscalls` (main suite,
through the int 0x80 dispatcher, nr 356) and
`testMemfdNullName64Syscalls` (x64 suite, through ksyscall64, nr
319), in `source/test/process/testMemfdNullName.cpp`. **RED on the
base**: on both dispatchers a null name returned descriptors
(0, 1, 2 across the flags variants) instead of -EFAULT; every
control already passed (empty and labelled names create, bad
flags -EINVAL, null + bad flags -EINVAL). **GREEN with the fix**;
the empty name still creates. Logs:
`test-main-fd137-red.log`, `test-x64-fd137-red.log`,
`test-main-fd137-green.log`, `test-x64-fd137-green.log`.

## 4. FD-136 by substance

The defect as filed: the 64-bit dispatcher's utimensat case
(`source/kernel/syscall64.cpp`, case `X64_SYS_utimensat`) is a
stub that answers success for almost every input; FD-131 taught
it to answer -ENOENT for a readable empty first byte without
AT_EMPTY_PATH, and its comment records a null path as "never
dereferenced", keeping the success answer. FD-132 fixed the
32-bit side's real utimensat to -EINVAL-for-null and filed this
entry for the 64-bit stub.

What the re-measurement shows (§2): the stub's success answer for
a null path is wrong for *some* shapes (AT_FDCWD should fault
-EFAULT; a valid descriptor with AT_EMPTY_PATH or
AT_SYMLINK_NOFOLLOW should answer -EINVAL; an unopened descriptor
should answer -EBADF/-EINVAL), but it is the kernel's own answer
for the descriptor form — a valid descriptor with flags 0, which
is the `futimens()` shape real guest code issues. The filing's
prescribed fix (unconditional -EINVAL) would make the 64-bit
dispatcher strictly worse than the stub for that shape: a guest
calling `futimens()` on an open file would fail where the kernel
succeeds. The two dispatchers would also disagree.

So this lane changes no FD-136 code. The register entry is
amended with the raw matrix, the wrapper/kernel distinction, and
corrected closing terms (answer the raw per-shape matrix for a
null path, proven by a guest test). One knock-on the merge audit
must see: FD-132's 32-bit utimensat exception (CL-78) rests on
the same wrapper measurement — the 32-bit side currently answers
-EINVAL for the (fd, NULL, 0) futimens shape where the kernel
answers 0, and -EINVAL at AT_FDCWD where the kernel answers
-EFAULT. Whether the campaign's contract for these two calls is
the raw kernel or the POSIX wrapper surface is a decision above
this lane; the evidence for it is now on the record in both
places.

FD-131's empty-path answers at the stub are untouched by this
lane either way (no FD-136 code change), which the filing's
closing terms required of the fix.

## 5. Filed from this lane

- **FD-138** — `memfd_create` accepts names longer than the
  host's limit on both dispatchers. Host (measured): 249 chars
  creates, 250+ answers -EINVAL. Emulator: no length validation
  anywhere on the path — the 64-bit case truncates the name at
  255 chars into its buffer and creates; the 32-bit helper passes
  the whole guest string through and creates. Guest-observed in
  the FD-137 tests (printed observation, not pinned): a 300-char
  name returns a descriptor on both dispatchers
  (`test-main-fd137-green.log`, `test-x64-fd137-green.log`).

## 6. Gates (fixed tree, all serial)

- Full native suites: **main 671/671 and x64 93/93, 0 FAILED**
  (`suite-main-serial.log`, `suite-x64-serial.log`).
- JS unit: **348 passed / 0 failed** (`js-unit.log`). Note: a
  first run before the browser build failed 75 export-dependent
  unit tests on the fresh worktree's missing browser export
  (e.g. export-module-closure); after the build the same suite is
  348/0, matching the sibling lanes. The preserved log is the
  post-build run.
- Browser build: **BUILD COMPLETE, "Browser export verified"**;
  staged dependency archive `c4dbe35986a5` as pinned; BUILD_INFO
  git_sha `0e5d33859`, wasm sha256 `6e877d79`
  (`browser-build.log`).
- `tools/known-gaps-audit.mjs --check` passes
  (`known-gaps-audit.log`); its one note (a stale fsmemnode
  allowlist entry) is present identically on the base tree.

## 7. Evidence (`test-results/fd136-137/`)

- `hostref-fd136.c`, `hostref-fd136.out.txt` — utimensat null:
  libc grid, raw rows, controls.
- `hostref-fd136-rawgrid.c`, `hostref-fd136-rawgrid.out.txt` —
  the full raw-kernel null matrix.
- `hostref-fd137.c`, `hostref-fd137.out.txt` — memfd_create name
  shapes (libc + raw in agreement) and the length boundary.
- `test-main-fd137-red.log`, `test-x64-fd137-red.log`,
  `test-main-fd137-green.log`, `test-x64-fd137-green.log` — the
  guest tests on base and fixed trees.
- `suite-main-serial.log`, `suite-x64-serial.log`, `js-unit.log`,
  `browser-build.log`, `known-gaps-audit.log`,
  `build-native-base.log`, `build-native-fix.log` — gates.

## 8. Lane mechanics

Fresh worktree `wt-fd136-137` on branch `lane-fd136-137` from
`dcb9c93a0`, set up per the lineage recipe (node_modules symlink;
dxvk build.w32/build.w64 and gstreamer32/64 payloads from
`wt-fd38`; vkd3d build.w64 from `wt-restage`, build.w32 from
`wt-x86vkd3d`; bootstrap `--local` from the
winbox-runtime-assets-20261101 archive; "Restored and verified 13
runtime assets"). Two transport incidents, both caught by
verification and repaired before commit: an SSH drop mid-build
left a detached make running, a second build was started over it,
and both were killed by exact pid (a pattern-based pkill matched
this lane's own shell once — the documented self-match trap);
the build dirs were wiped and rebuilt cleanly once. Two
content-bearing transfers mangled characters (a spaced-pipe loss
in one hostref line; backticked spans in one register edit
executed by the shell); every load-bearing file was re-read from
disk and byte-checked after writing, and the register diff was
reviewed in full before commit.
