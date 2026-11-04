# FD-126 — a null path at the 64-bit chmod/fchmodat calls answers -EFAULT; an empty path answers -ENOENT

Lane `lane-fd126`, based on `lane-fd125-ab` tip `b298ed6eb`.
The defect, by substance: a path system call given a null path
pointer answered whatever the emulator's empty-path route
answers, where the host answers -EFAULT. The 64-bit
dispatcher's shared guest-string reader maps a null pointer
to the empty string, so at the two path calls that read
through it — chmod and fchmodat — a null path was
indistinguishable from "". (Lineage number FD-126, filed by
the FD-125 settlement lane; the next merge audit renumbers
this lineage, so the substance above is the durable
description. This lane's closure is CL-76 and its one new
filing is FD-131, both lineage-provisional.)

## Host measurement first

`test-results/fd126/hostref-fd126.c`, run on the box host
(kernel 6.17.0-1020-oracle), output
`test-results/fd126/hostref-fd126.out.txt`. One caution
recorded for the next lane: the box kernel is aarch64, so the
first version of this program, written with x86_64 syscall
numbers, measured unrelated syscalls and was discarded (its
chmod of a real file answering -EINVAL is what exposed it);
the recorded run uses the asm-generic numbers and glibc's
chmod, and the errno semantics it measures are the kernel's
shared path-copy-in logic, which is what the guest ABI
models. The answers:

- chmod(NULL) -EFAULT; chmod("") -ENOENT; chmod(real) 0.
- fchmodat(AT_FDCWD or a real dirfd, NULL, flags 0)
  -EFAULT; with "" -ENOENT; with a real relative name 0.
- fchmodat(dirfd, NULL, AT_EMPTY_PATH) -EFAULT;
  fchmodat(dirfd, "", AT_EMPTY_PATH) -ENOENT — the legacy
  fchmodat gives an empty name the plain lookup answer even
  with the flag (fchmodat2 was probed for contrast; its
  -EINVAL here is a struct-size artifact of the probe, not
  semantics, and no guest fchmodat2 exists to model).
- Ordering: fchmodat(unopened dirfd, NULL) -EFAULT and
  fchmodat(unopened dirfd, "") -ENOENT, while
  fchmodat(unopened dirfd, "nonexistent") -EBADF — both the
  null and the empty answers precede dirfd resolution; the
  null answer also precedes flag handling.
- execve(NULL) -EFAULT — the control the dispatcher already
  matches (its path is guarded before the reader).

The filing is corroborated, with one correction to its
measurement, below.

## What the base actually does (the RED run)

The guest test's RED run on the unmodified base
(`test-results/fd126/red-x64.log`) failed on more than the
filed shape, and the extra failures are the mechanism made
visible. In the test's fixture (working directory "/"):

- chmod(NULL) and chmod("") both answered 0 — and chmodded
  the directory. fchmodat's null and empty shapes answered
  0 likewise, at AT_FDCWD and at a real dirfd, with and
  without AT_EMPTY_PATH. fchmodat(unopened dirfd, NULL)
  answered -EBADF. The real-name controls (chmod and
  fchmodat landing modes 0600 and 0640) already passed.

So the null shape takes the empty-path route, and the
empty-path route itself does not answer -ENOENT in general:
`Fs::getNodeFromLocalPath` returns the root node for a full
path of "" or "/", and a relative empty path joins to the
working directory, whose walk returns the directory node —
an empty final path is never refused. Where the working
directory's own path does not resolve, the same route
answers -ENOENT, which is the environment the filing
measured in the browser self-test. The defect is therefore
two-layered at these calls: the reader erases the null, and
the resolver promotes the empty string to the directory.
FD-126's closing terms name the null; the lane brief's end
state names both answers at these calls, so both are fixed
here, and the resolver behaviour underneath — shared by
every path call — is filed separately as FD-131 rather
than changed under this lane's audit.

## The reader audit

The closing terms allowed fixing at the reader with its
other consumers audited. The reader's consumers are four:

- execve's path — already guarded to -EFAULT ahead of the
  reader, matching the host control above. Untouched.
- The argv/envp walk — it dereferences only non-null
  element pointers (a null element terminates the walk), so
  the reader never receives address 0 from it. Untouched.
- chmod and fchmodat — the two unguarded path calls, fixed
  below.

The dispatcher's other path calls do not use this reader;
they copy into fixed buffers behind their own null guards
(the readlink shape) or resolve through KProcess. Changing
the reader itself (for example, making it report null)
would have moved every consumer's behaviour to fix two
call sites, so the guards land at the call sites, in the
same idiom execve already uses.

## The fix

`7ecaf95db` (source/kernel/syscall64.cpp, two sites): the
chmod case guards `!a1` to -EFAULT before reading, and the
string it then reads to -ENOENT if empty; the fchmodat case
guards `!a2` and its read string the same way. Placement is
the host's ordering: both answers precede KProcess's flag
handling and dirfd resolution, and the resolver is never
reached with an empty path from these calls. No other
behaviour moves: real names, modes, AT_SYMLINK_NOFOLLOW's
-ENOTSUP and the descriptor answers all flow through the
unchanged KProcess code.

## Proof

- New guest test testChmodNullPath64Syscalls (`d5a21e7a8`,
  comment corrected to the measured mechanism in
  `f86cd4a75`), registered in the x64 table. RED on the
  base: the eight null/empty shapes above against the
  host's -EFAULT/-ENOENT, every control passing. GREEN on
  the fix (`test-results/fd126/green-x64.log`).
- Native suites serial, one worker, full runs on the final
  tree: main 668/668 and x64 90/90, 0 FAILED
  (`suite-main-green.log`, `green-x64.log`; the x64 count
  is the base's 89 plus the one new test).
- JS unit (the sibling lanes' gate form, `runner.mjs
  --unit`): 348 passed / 0 failed on the built tree
  (`js-unit.log`). A first run before the browser build
  read 273 passed / 75 failed (`js-unit-unbuilt.log`) —
  every sampled failure a missing generated build file
  (BUILD_INFO, the export closure, staged vendor files),
  the standing un-built-lane-tree pattern the FD-109..112
  lane recorded at 272/76; it is preserved beside the built
  run rather than hidden.
- Browser build: BUILD COMPLETE, "Browser export verified"
  (`browser-build.log`); BUILD_INFO git_sha `7ecaf95db`
  (the fix commit), git_dirty_files 0, wasm sha256
  `e097e1cf`, staged dependency archive `c4dbe35986a5` as
  pinned.
- The silent-failure allowance for syscall64.cpp stays
  120: the scanner counts unlogged -ENOSYS/-EINVAL returns,
  and the guards return -EFAULT/-ENOENT through the
  existing `ret` idiom, adding no counted site;
  no-silent-failure passes in both JS runs above.

## Register changes in this lane's final commit

- FD-126: header marked FIXED with the CL-76 pointer; the
  closure CL-76 names the fixing commit and carries the
  measurement correction (the filed -ENOENT is the
  empty-path route's answer only where the working
  directory does not resolve).
- FD-131 filed: an empty path resolves to the directory
  node itself in the shared path resolver, so path calls
  generally answer as if the directory had been named
  where the host answers -ENOENT; closing terms ask for
  the empty path to answer -ENOENT at the path calls
  generally, at the resolver with its callers audited or
  call by call.
- Section B row added for `lane-fd126`.

## Build inputs

As the sibling records describe, plus the vkd3d payloads
the settlement lane's addendum names: bootstrap --local
from the retained runtime-assets-20261101 archive (this
branch's manifest pin), the node_modules symlink to the
main tree, pinned-vintage dxvk builds and gstreamer
payloads copied from wt-fd38, and the vkd3d build.w64 /
build.w32 trees copied from wt-restage / wt-x86vkd3d. All
generated state is stripped again before the lane reports.
