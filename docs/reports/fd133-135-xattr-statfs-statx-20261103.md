# FD-133 / FD-134 / FD-135 — xattr resolution, statx AT_EMPTY_PATH, statfs resolution (2026-11-03)

**Lane:** `lane-fd133-135`, worktree `/home/ubuntu/wt-fd133-135`,
branched from `lane-fd136-137` tip `a4b1ce100` (the current tip of
the lineage containing the FD-131 filings).

**Commits:** tests `4ebfe696c`, fix `d8ca8945a`, then this record +
register in the closing commit.

**Outcome:** all three filings resolved to the host's raw-kernel
measured behaviour. Nothing pushed, nothing deployed, no master
commits.

## The host was measured first, at the raw syscall level

Reference programs issue the aarch64 asm-generic syscalls directly
through `syscall()` and print every shape's verdict:
`test-results/fd133-135/hostref-fd133-135.c` (the main matrix) plus
companions `hostref-fd133b.c`, `hostref-fd133c.c`, `hostref-fd133d.c`
(the entry-order probes). For these three families the raw answers
equal the libc answers — no wrapper rejects anything in userspace —
so the filings' shapes stand as filed, and the programs also pinned
the exact entry order below.

### xattr (FD-133)

- Every path form answers **-ENOENT** for a missing path and for an
  empty path; a **null path answers -EFAULT**.
- Existing file: get -ENODATA, list 0, set stores (returns 0),
  remove of an absent attribute -ENODATA on the host (the guest
  stub keeps -ENOTSUP — see "kept by the filing's terms" below).
- **Entry order, measured:** the set forms validate their flags
  word first (-EINVAL for any bit outside XATTR_CREATE|XATTR_
  REPLACE, mask 0x3; CREATE|REPLACE together is *not* an entry
  error — it is answered -ENODATA for an absent attribute); then
  every name-taking form copies the attribute name (-EFAULT for a
  null name — this verdict beats a missing path, an empty path,
  *and* a bad descriptor); then the path or descriptor is resolved
  (-ENOENT / -EBADF).
- Follow vs no-follow genuinely discriminates: a dangling symlink
  answers -ENOENT for the following forms and resolves the link
  itself for the l-forms (lgetxattr -ENODATA, llistxattr 0); getxattr
  through a symlink to a marked target returns the value while
  lgetxattr answers -ENODATA.
- Descriptor forms: an unopened descriptor answers **-EBADF**,
  with the name-copy and flags verdicts ahead of that check.

### statx (FD-134)

- `statx(dirfd, "inner", AT_EMPTY_PATH)` returns the **named file**
  (mode 0100644, its own size and inode); `statx(dirfd, "",
  AT_EMPTY_PATH)` returns the directory itself; an absolute path
  under the flag stats itself; `statx(dirfd, "", 0)` answers
  -ENOENT; a file descriptor with a non-empty relative path
  answers -ENOTDIR.

### statfs (FD-135)

- Missing path **-ENOENT**; empty path -ENOENT; **null path
  -EFAULT**; file and directory 0; a symlink to a file 0 (follows);
  a dangling symlink -ENOENT; a path descending through a regular
  file **-ENOTDIR** (see FD-140).
- fstatfs on an unopened descriptor answers **-EBADF** (see FD-139).

## The three defects, by substance

### FD-133 — the 64-bit xattr cases never resolved anything

The twelve cases (syscalls 188–199: setxattr/lsetxattr/fsetxattr,
getxattr/lgetxattr/fgetxattr, listxattr/llistxattr/flistxattr,
removexattr/lremovexattr/fremovexattr) answered their stub values
unconditionally — get -ENODATA, list 0, set/remove -ENOTSUP — for
missing paths, null pointers, bad descriptors and bad flags alike.
**Fix** (in `d8ca8945a`, `source/kernel/syscall64.cpp`): the three
case groups are merged into one block that reproduces the measured
entry order — set-form flags validation, name copy for the
name-taking forms (all but the three list forms), then, when a
process exists, path resolution through the shared resolver
(following for the plain forms, no-follow for the l-forms) or a
descriptor check for the fd forms — and only a resolved target
reaches the stub answers. Without a process the stub answers stand
as before. The Wine bring-up comment's substance is preserved.

Kept by the filing's terms: the guest filesystem models no
attributes, so a resolved target keeps the stub answers — the
host's real storage (setxattr returning 0, removexattr -ENODATA)
is *not* modelled; only resolution is in scope.

### FD-134 — statx substituted the descriptor's path for a named path

`KProcess::statx` replaced the caller's path with the descriptor's
own path whenever AT_EMPTY_PATH was set, whatever the path said —
so `statx(dirfd, "inner", AT_EMPTY_PATH)` stat'ed the directory
itself instead of the file inside it. **Fix** (in `d8ca8945a`,
`source/kernel/kprocess.cpp`): the substitution now applies only
when the path is empty. A non-empty path then flows through the
existing machinery (`Fs::getFullPath` resolves it against the
descriptor's directory; absolute paths pass through), the
empty-path refusal without the flag (FD-131) is untouched, and the
descriptor-only form is unchanged. The 64-bit dispatcher's own
statx helper never shared the behaviour.

### FD-135 — the 64-bit statfs case answered success for any path

The dispatcher's statfs case (syscall 137) returned the fabricated
statistics unconditionally. **Fix** (in `d8ca8945a`,
`source/kernel/syscall64.cpp`): with a process, a null path answers
-EFAULT, the path is copied and resolved through the shared
resolver from the working directory (following symlinks), an
unresolvable path answers -ENOENT, and a resolvable one keeps the
fabricated answer. FD-131's earlier readable-empty-first-byte guard
is subsumed by the same -ENOENT. Without a process the fabricated
answer stands as before. The process layer's own statfs/statfs64
methods already resolved this way; the descriptor form there
already answers -EBADF for a bad descriptor.

## Proof

New tests in `source/test/process/testPathResolve64.cpp`
(registered in `source/test/testMain.cpp`), commit `4ebfe696c`:

- `testXattrResolve64Syscalls` (x64 suite) — ~50 assertions: the
  full resolution matrix, the entry-order compounds (bad flags
  before null name; null name before missing path, empty path and
  bad fd), the dangling-symlink split between following and
  l-forms, follow-through to a marked target, and the descriptor
  forms.
- `testStatfsResolve64Syscalls` (x64 suite) — missing, empty, null,
  file, directory, symlink and dangling shapes, plus a *printed,
  unpinned* observation for a path through a regular file (see
  FD-140).
- `testStatxAtEmptyPathProcessApi` (main suite) — the host contract
  above, reading mode and inode back out of the statx buffer.

**RED on the base product code** (same test commit, fix stashed):
main suite 672 tests with exactly 1 FAILED (the statx test — 4
assertions: the named-path shapes returned the directory's mode
and inode, ino 1 where the file's ino 3 was expected, and a
missing named path answered 0); x64 suite with exactly 2 FAILED
(the xattr test — 37 assertions failing with the stub answers
-ENODATA/-ENOTSUP/0 where -ENOENT/-EFAULT/-EINVAL/-EBADF were
wanted — and the statfs test — missing, dangling and null paths
answering 0). Every control passed on the base: the shapes already
right (resolved-target stubs, the descriptor statx form, the
no-flag empty-path refusal) were already right.

**GREEN on the fix:** x64 suite 95/95, main suite 672/672, 0
FAILED, run serially. The statfs observation line on the fixed
tree prints -ENOENT for the through-a-file path where the host
answers -ENOTDIR — that gap is FD-140, deliberately not pinned by
the test.

## Filed from this lane

- **FD-139** — the 64-bit dispatcher's fstatfs case (syscall 138)
  shares the statfs helper and ignores its descriptor entirely,
  answering the fabricated statistics for any descriptor; the host
  answers -EBADF for an unopened one (measured in the hostref
  matrix), and the process layer's fstatfs64 already checks. Not
  in FD-135's filing, which named only the path form.
- **FD-140** — a path descending through a non-directory answers
  -ENOTDIR on the host, but the shared resolver reports no node,
  so every call routed through it answers -ENOENT. Resolver-level;
  observed at statfs after the FD-135 fix (guest -ENOENT vs host
  -ENOTDIR). FD-131/FD-135 deliberately left the resolver
  unchanged.

## Gates

- Native suites serial: main **672/672**, x64 **95/95**, 0 FAILED.
- JS unit suite: **348 passed / 0 failed**.
- Browser build: BUILD COMPLETE with the literal "Browser export
  verified" line; `BUILD_INFO` git_sha `d8ca8945a` (the fix
  commit), git_dirty_files 0, wasm sha256
  `bd6816da12113abde51e966e6fa78b31a54c85abf7e78284a108acb61b3bb452`,
  staged dependency archive `c4dbe35986a5` as pinned.
- `tools/known-gaps-audit.mjs --check`: exit 0 (one stale
  fsmemnode allowlist note, present identically on the base).

## Register

FD-133 → **CL-80**, FD-134 → **CL-81**, FD-135 → **CL-82**
(lineage-provisional numbers; the next merge audit owns
renumbering). FD-139 and FD-140 filed in Section A. A Section B
row for `lane-fd133-135` summarises the lane.

## Evidence

`test-results/fd133-135/` in this worktree: the four hostref
programs and their outputs; `red-main.log`, `red-x64.log`,
`green-main.log`, `green-x64.log`; the build, browser-build,
js-unit and known-gaps logs.

## Lane mechanics

- Fresh-worktree setup followed the lineage recipe: shared
  node_modules symlink, pinned-vintage payloads (dxvk and
  gstreamer from `wt-fd38`, vkd3d `build.w64` from `wt-restage`,
  `build.w32` from `wt-x86vkd3d`), runtime assets restored from the
  20261101 bootstrap archive ("Restored and verified 13 runtime
  assets"), vendor staged.
- Two test-file-only include fixes were needed while bringing the
  new test file up (`kmemory64.h`/`cpu64.h`, then `syscall64.h`);
  no product code was touched for them.
- Content went to the box through files/stdin, never heredocs;
  every patch was read back from disk before building on it.
