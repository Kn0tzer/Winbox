# FD-139 / FD-140 / FD-152 — utimensat flags verdict, through-a-file -ENOTDIR, fstatfs descriptor verdict

Lane `lane-fd139-152`, worktree `wt-fd139-152`, base integ tip
`7a2308cd6` (batch-5 deploy record). Tests `c178a704e`, fix
`00d8f5369`, test-row corrections `e8f120b22`, JS slice-stub
adaptations `2de1494b8`. Entry numbers are the lane's own
(FD-139/FD-140/FD-152 as they stand on the integ register;
closures CL-104/CL-105/CL-106 and filings FD-155…FD-159
provisional — the merge audit renumbers, so this record keys
everything by substance).

## The three defects, by substance

1. **The descriptor-form filesystem-statistics call ignores
   its descriptor (register FD-152).** The 64-bit dispatcher's
   fstatfs case (138) shared the statfs helper and answered the
   fabricated statistics for any descriptor, valid or not; the
   host answers -EBADF for a descriptor that is not open.
2. **utimensat accepts flag bits outside
   AT_SYMLINK_NOFOLLOW | AT_EMPTY_PATH on non-null paths
   (register FD-139).** Both dispatchers validated the flags
   word only on the null-path shapes; elsewhere an outside bit
   was ignored — success on the 64-bit stub, success with the
   times actually applied on the 32-bit process path. The host
   answers -EINVAL.
3. **A path through a non-directory answers -ENOENT where
   the host answers -ENOTDIR (register FD-140).** The shared
   resolver reports no node for such a path, and every call
   routed through it inherited the bare -ENOENT.

## Host measurement (raw kernel level, before any product change)

Reference programs issue raw `syscall()` with asm-generic
numbers on the box's aarch64 kernel (6.17.0-1020-oracle) and
were strace-verified where a libc form exists, so the shapes
below are the kernel's answers, not a wrapper's:
`test-results/fd139-152/hostref-fd139-152{,b,c}.c` with
outputs alongside.

- **fstatfs**: 0 for ANY open descriptor (regular file,
  directory, pipe read end, socket); -EBADF for fd -1, fd
  9999, and a just-closed descriptor; -EFAULT for an open
  descriptor with a null buffer; -EBADF when both are wrong —
  the descriptor verdict precedes the buffer fault. (An early
  "closed fd → 0" row was an artefact of fd-number reuse by a
  later pipe(); the follow-up program's -EBADF is
  authoritative.)
- **utimensat flags**: valid path with flags 0 / 0x100 /
  0x1000 / 0x1100 → 0; with 0x2000 / 0x1 / 0x800 / -1 →
  -EINVAL. Missing path + 0x2000 → -EINVAL (the verdict
  precedes the name fetch); "" + 0x2000 → -EINVAL; "" + 0 →
  -ENOENT; unreadable times pointer + 0x2000 → -EFAULT (the
  times copy-in precedes the verdict); null times + 0x2000 →
  -EINVAL; dirfd-relative + 0x2000 → -EINVAL; "a.txt/child" +
  0x2000 → -EINVAL (flags precede the -ENOTDIR shape).
- **Through a regular file** ("a.txt/child"): -ENOTDIR
  uniformly across statfs, newfstatat (both flag forms),
  statx, openat (O_RDONLY and O_CREAT|O_WRONLY), faccessat,
  fchmodat, readlinkat, unlinkat, mkdirat, renameat (both
  sides), symlinkat (linkpath side), linkat (both sides),
  libc utimes, getxattr, truncate, inotify_add_watch —
  also through a symlink to a file, with deeper nesting, and
  in dirfd-relative forms. Controls: "missing/child" →
  -ENOENT; through a symlink to a directory → 0. Trailing
  shapes "a.txt/", "a.txt/.", "a.txt/.." also answer
  -ENOTDIR on the host (see FD-157 below — a different
  mechanism).

## The fix (`00d8f5369`)

- **FD-152**: syscall64 case 138 checks the descriptor
  against the process first (mirroring the process layer's
  own fstatfs64): not open → -EBADF, ahead of the helper's
  null-buffer -EFAULT; open → the fabricated answer stands.
  Without a process the fabricated answer stands, as in
  statfs case 137.
- **FD-139**: the 64-bit utimensat stub hoists the flags
  verdict (`a4 & ~(0x100|0x1000)` → -EINVAL) to immediately
  after its times-readable check and before the null-path
  branch; the FD-136 null matrix is preserved
  answer-for-answer (the inner branch check is subsumed).
  `KProcess::utimesat` and `utimesat64` validate the same
  word at entry, ahead of the FD-131 empty-path guard, so a
  refused call no longer applies the times. The null path
  never reaches them (it routes to `utimensatNullPath`,
  unchanged).
- **FD-140**: `Fs::getNodeFromLocalPath` gains an optional
  `bool* notDir` out-report as its last parameter (both
  overloads). At entry it clears the flag; a caller that
  asks always walks (the negative cache is bypassed — a
  cached miss cannot say whether the component that stopped
  it sat under a directory); in the walk's miss branch the
  flag is set when the node whose child lookup just failed
  is not a directory. Callers that do not pass it are
  unchanged in behaviour and cost. Guest-boundary sites
  translate a miss with the flag set to -ENOTDIR:
  - process API: openFileDescriptor (first resolution, and
    the create path — the parent is checked before any node
    is fabricated under a file), stat64, lstat64, the statfs
    pair, statx, fstatat64, access, faccessat, chmod,
    fchmodat, chdir, unlinkFile, unlinkat, rmdir, mkdir
    (first resolution and parent resolution), mkdirat via
    mkdir, rename and renameat (source side, plus a
    destination-parent probe before the node's rename),
    link (source side; destination parent via its null check
    plus a followed-resolution probe), symlinkInDirectory
    (parent resolved followed; a non-directory parent is
    refused before the link node is added),
    readlinkInDirectory (only ahead of its -EINVAL
    fallthrough; a plain miss stays -EINVAL — see FD-156),
    utimes, utimesat/utimesat64, inotify_add_watch, and
    execve via a new optional out-parameter on findInPath
    that reports only the direct resolution's verdict;
  - 64-bit dispatcher: statfs (137), sys_statx64,
    sys_stat_path64, sys_readlink64, the xattr cases
    188–199, the access/faccessat/faccessat2 block, and the
    utimensat stub for absolute and working-directory-
    relative paths (a probe only — the stub still resolves
    nothing else, and descriptor-relative shapes keep its
    standing success answer; a recorded carve-out);
  - 32-bit dispatcher: the getxattr/lgetxattr DOSATTRIB
    branches (the only 32-bit xattr branches that resolve;
    see FD-155).
  Parent verdicts are judged on **followed** resolutions:
  a symlink node's own directory flag is false even when
  its target is a directory, so creation through a
  symlinked directory keeps working (control-pinned in the
  tests). The resolver's other consumers — the loader, the
  socket layer, sysfs, CPU and startup paths, Fs-internal
  consumers — do not ask and are unchanged, per the
  filing's closing terms.

## Proof

Guest tests (tests `c178a704e`): `testFstatfsDescriptor64Syscalls`,
`testUtimensatFlags64Syscalls`, `testNotDir64Syscalls` (x64
suite); `testUtimensatFlags32Syscalls`, `testNotDir32Syscalls`,
`testNotDirProcessApi` (main suite). Fixtures stage a private
filesystem root per test (a file, a directory tree, symlinks
to both) so the verdicts are measured against known nodes.

- **RED on base `7a2308cd6`** (`test-results/fd139-152/
  red-main.log`, `red-x64.log`): exactly the six new tests
  fail, nothing else in the targeted windows.
  - fstatfs: unopened fd, fd -1, just-closed fd all answer
    0; bad fd + null buffer answers -EFAULT (the buffer
    check ran with no descriptor check). 4 assertions.
  - utimensat: bad-flag rows answer 0 on the 64-bit stub
    and 0 / -ENOENT on the 32-bit side; the refused 32-bit
    call demonstrably applied the times (fixture mtime
    became the staged 1600000000); the time64 form answers
    0. 18 assertions (-EINVAL rows and the mtime pin).
  - -ENOTDIR: rows answer -ENOENT and kin across both
    suites (-EIO at symlink creation, -EINVAL at readlink
    and at O_CREAT open — the base fabricates a node under
    the file and then fails its native open — a statfs of
    -61/-ENODATA at the DOSATTRIB branch off the fabricated
    node, an inotify watch granted on it). 69 assertions.
  - Two further RED rows were traced to unrelated
    pre-existing defects, not to these filings: the 64-bit
    rmdir row answered -ENOSYS (the dispatcher has no rmdir
    case — FD-158) and the process-API link control with a
    bare-filename destination answered -ENOENT (FD-159).
    Both rows were corrected in `e8f120b22` (the rmdir shape
    stays pinned at KProcess::rmdir on the 32-bit side; the
    link control now links into a subdirectory).
  - The x64 -ENOTDIR test also prints (does not pin) the
    normalization-erased shapes: statfs("a.txt/"),
    statfs("a.txt/."), statfs("a.txt/..") all answer 0 on
    the base where the host answers -ENOTDIR — FD-157.
- **GREEN on the fix**: targeted windows pass all six tests
  (`green-main-targeted.log`, `green-x64-targeted.log`);
  full native suites serial, one worker: main **682/682**,
  x64 **108/108**, 0 FAILED (`green-main-full.log`,
  `green-x64-full.log`).
- **JS unit**: 352 passed / 0 failed (`js-unit3.log`).
  Getting there took one stub adaptation (`2de1494b8`): two
  unit tests compile source slices of the product (the
  resolver implementation; `KProcess::openFileDescriptor`)
  against hand-written stubs, and the stubs declared the
  pre-FD-140 resolver signature (the file-create harness
  also lacked K_ENOTDIR). The stubs now accept and ignore
  the notDir out-report — neither harness requests the
  verdict — and no assertion changed. The first two runs
  (`js-unit.log`, `js-unit2.log`, 350/2) preserve the
  failures.
- **Browser build**: BUILD COMPLETE, "Browser export
  verified" (`browser-build.log`); BUILD_INFO git_sha
  `e8f120b22`, git_dirty_files 0, wasm sha256 `c6499c3c`,
  staged dependency archive `c4dbe35986a5` as pinned.
- **known-gaps audit** `--check`: exit 0 (`known-gaps.log`;
  the one stale fsmemnode allowlist note is present
  identically on the base).

## Filed from this lane (provisional numbers)

- **FD-155** — the 32-bit xattr cases do not resolve their
  target for names outside the DOSATTRIB special case:
  missing path + "user.x" answers -ENOTSUP where the host
  answers -ENOENT; existing file answers -ENOTSUP where
  the host answers -ENODATA (host rows in
  `hostref-fd139-152c`).
- **FD-156** — readlink/readlinkat answer -EINVAL for a
  missing path on both dispatchers; the host answers
  -ENOENT (the -ENOTDIR case is closed by CL-106; the
  plain-missing case was deliberately left).
- **FD-157** — the normalization-erased -ENOTDIR shapes
  ("a.txt/", "a.txt/.", "a.txt/.."): the resolver's
  normalization drops the trailing component before the
  walk, so no walk step looks inside the file; guest
  answers 0 (printed observations), host -ENOTDIR.
- **FD-158** — the 64-bit dispatcher's rmdir (84) has no
  case and answers -ENOSYS for every path; the process
  layer implements rmdir and the 32-bit dispatcher routes
  it.
- **FD-159** — `KProcess::link` with a bare-filename
  destination answers -ENOENT even when the source exists:
  `Fs::getParentPath` of a slash-less name returns the name
  itself (lastIndexOf finds no slash; substr(0, -1) yields
  the whole string), so the destination-parent resolution
  looks up the not-yet-existing destination. Measured on
  base and fixed trees alike; unrelated to FD-140.

## Deliberately unchanged

- `KProcess::statx`'s AT_EMPTY_PATH descriptor branch and
  `sys_statx64`'s (a descriptor shape, not a path walk).
- `sys_newfstatat64`'s relative-path-with-dirfd literal
  -ENOENT (a standing unsupported shape, untouched by
  these filings).
- openFileDescriptor's O_TMPFILE final-component branch.
- startProcess/getcwd/executable-path resolutions and the
  socket layer, loader, sysfs and startup consumers of the
  resolver (the filing's "other consumers unchanged"
  clause).
- `FsNode::rename`'s internal parent resolution (an
  Fs-layer consumer; the guest boundary now pre-checks).
- `linkSharedBacking` (reached only after `link()`'s own
  checks pass).

## Setup notes

Fresh worktree off `7a2308cd6`: node_modules symlinked from
the main tree; dxvk build.w32/build.w64 and rootfs64 payload
dirs (cjk-font, gecko, gstreamer32, gstreamer64, openal)
copied from `wt-fd38`; vkd3d build.w64 from `wt-restage`,
build.w32 from `wt-x86vkd3d`; runtime assets restored from
the pinned gate archive `winbox-runtime-assets-20261103`
(bootstrap verified 13 assets). Native suites ran serially
throughout (parallel native runs fail spuriously — the
FD-32 pattern). One environment note: the local /tmp was
wiped mid-lane and the prepared product edits were lost
before being pushed; they were reconstructed from the
lane's working notes, re-applied with count-asserted
scripts against pristine copies, and the resulting diff was
reviewed hunk by hunk before committing — the fix commit's
tree is the reviewed one.
