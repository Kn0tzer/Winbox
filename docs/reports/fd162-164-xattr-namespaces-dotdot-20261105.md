# FD-162..FD-164 — the xattr descriptor forms, the xattr namespace verdict, and mid-path ".." stepping

Branch `lane-fd162-164` on base `4006ddb58` (the batch-7
integration tip). Tests `ec62ca78e`, fix `9b4ce67b1`,
slice-harness follow-up `872d1b474`. Lane-local closures
CL-119..CL-121 (provisional; numbers settle at the next
composition). Evidence in `test-results/fd162-164/`.

An earlier attempt at this lane was stopped mid-work on
2026-11-05; this run resumed from its real state (host
references measured, guest tests written, no product fix)
and carried it through.

## Host measurement first

The host behaviour was measured at the raw syscall level
before any fix: hand-written syscall instructions (the
asm-generic numbers this kernel interface uses), never the
C library wrappers, cross-checked with strace so the shape
that enters the kernel is the shape that was measured
(`hostref-fd162-164.out`, `hostref-fd163b.out`,
`hostref-fd163c.out`, `hostref-fd163d.out`). Four matrices:
the descriptor forms against an unopened descriptor and a
real file; the namespace classifier across every name class
the filings turn on; the fetch-stage ordering (flags, null
name, empty name) against null paths, missing paths and bad
descriptors; and the statfs matrix for a ".." behind a
missing component, a regular file, a directory and a link,
plus ".." at the root.

## The three defects, by substance

### FD-162 — the 32-bit xattr descriptor forms answered an emulator-private error, and one never looked at its descriptor

fgetxattr, fsetxattr and fremovexattr answered -EBADFD (77)
for an unopened descriptor — an error the host does not
have; the host answers -EBADF (9). flistxattr answered
-ENOTSUP unconditionally, without consulting its descriptor
at all: the host answers -EBADF for an unopened descriptor
and an empty list (0) for any open one. fsetxattr also had
no flags validation; the host refuses a flags word outside
0x3 with -EINVAL ahead of the name fetch and the descriptor
verdict alike. The four forms now share the host's entry
order: flags (set form), name fetch, descriptor (-EBADF),
then the standing answers. Guest test
`testXattrDescriptor32Syscalls`.

### FD-163 — every unmodelled xattr name answered "attribute absent", whatever the namespace

On a resolved target the get forms answered -ENODATA for
every name, so a guest could not tell "this namespace is
not supported" from "the attribute is absent". The measured
host rule is a namespace lookup ahead of the attribute
lookup: a prefix with no handler (an unknown word,
"system.*" other than the two posix_acl names, a name with
no recognised prefix) answers -ENOTSUP; a recognised
namespace (user.*, trusted.*, security.*,
system.posix_acl_access, system.posix_acl_default) with an
absent attribute answers -ENODATA; a recognised prefix with
an empty attribute part ("user." itself) answers -EINVAL.
An empty name answers -ERANGE at fetch time, ahead of the
null-path, missing-path and bad-descriptor verdicts. Both
dispatchers now classify the fetched name on a resolved
target (one classifier per dispatcher, kept in step by
hand, as they are separate translation units); the
set/remove forms keep their standing -ENOTSUP on a resolved
target per FD-157. Guest tests
`testXattrNamespace32Syscalls` and
`testXattrNamespace64Syscalls`.

### FD-164 — a ".." away from the end of a path was erased with its predecessor, sight unseen

The resolver's normalisation erased each ".." together
with the component before it before any lookup, so
"missing/../a.txt" resolved as "a.txt" (the host answers
-ENOENT — the host steps through the missing component
first) and "a.txt/../d" resolved as "d" (the host answers
-ENOTDIR); and a ".." at the root failed the walk, where
the host stays at the root (its parent is itself). When a
caller asks for the notDir verdict, the resolver now judges
each non-trailing ".." by resolving the path ahead of it —
a miss propagates as a miss, a predecessor that resolves to
a non-directory sets the notDir verdict — and the
normalisation clamps a ".." at the root instead of failing.
The trailing-marker family stays FD-159's rule, and callers
that do not ask for the verdict keep the erased walk.
Guest tests `testMidDotDot32Syscalls` and
`testMidDotDot64Syscalls`, which ride the statfs/stat64/
stat/getxattr path calls on both widths.

## Proof

RED on the base (tests committed first, `ec62ca78e`): the
three new main-flavour tests fail with 41 wrong answers in
the filed shapes (`red-main.log`), and the two new x64
tests fail the same way (`red-x64.log`). GREEN on the fixed
tree: the targeted ranges pass on both flavours; the full
native suites run serially with 0 FAILED (main 689 tests,
x64 113 tests — `green2-main-full.log`,
`green2-x64-full.log`); the JS unit suite passes 353/0
(`js-unit2.log`); the browser build verifies on the fixed
tree ('Browser export verified', BUILD COMPLETE, wasm
`0e5fa5dee8b2`, built from `872d1b474` with a clean tree —
`build-browser-final.log`).

One harness lesson re-learned on the way: the resolver
slice compiled by the fs-dynamic-link-cache unit test is a
restricted subset of the filesystem sources, and the first
form of the mid-path scan used a string facility outside
that subset, failing the slice build while every native
suite passed. The scan now splits the path through the same
path-splitting entry the resolver itself uses
(`872d1b474`); behaviour is identical, and the slice test
passes.

## New filings from this lane's measurements

- FD-165 (provisional) — the set/remove forms' namespace
  classification is unmeasured and unmodelled. FD-163's
  matrix covered the get forms; beyond the modelled name,
  whether the host classifies names for the set/remove
  forms (and what the descriptor remove form answers for
  the modelled name on a file that does not carry it) is
  unmeasured, and both dispatchers answer their standing
  -ENOTSUP for every non-modelled name there. Filed in the
  register by substance; closing terms are a raw-syscall
  matrix for the set/remove forms and alignment proven by
  guest tests.

Nothing else new was found: every other shape this lane
measured matched the standing behaviour it was compared
against, and the controls pinned for the standing answers
(the set/remove -ENOTSUP, the modelled attribute's own
verdicts, the trailing-marker family) all held.

## Setup note

A fresh working copy for this lane needed, beyond the
documented payload inputs (node_modules symlink, dxvk,
gstreamer and vkd3d payloads), two more steps before its
build results meant anything: the pinned Slang assets under
`tools/browser/slang/` are untracked and must be copied
from a built tree (here, the integration tree), and the
runtime assets must be restored from the retained local
archive (`tools/browser/bootstrap.py --local
/home/ubuntu/backups/winbox-runtime-assets-20261104/
browser-runtime-20261104.tar`) because the pinned release
URL answers 404 (FD-29, accepted). `stage-vendor.mjs` then
runs clean; note it empties the staged vendor directories
before copying, so a failed stage leaves them incomplete
until it is run again after the inputs are fixed. The
build's lock and probe-drain queues are shared with every
other lane on the box; two queued attempts of this lane's
build died on the lock-wait bound and were simply
restarted — the machinery's behaviour, not a defect.
