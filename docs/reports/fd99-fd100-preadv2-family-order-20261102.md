# FD-99 / FD-100 — preadv2's -1 mode and 0x3f mask, and the vectored family's flags-last order (2026-11-02)

Branch `lane-fd99-100`, based on `lane-fd98` tip
`fa3b5eeef`. Commits: `9e1b617c7` (tests) and
`fad85c19c` (fix); this record and the register
closures (CL-53, CL-54) ride in the register
commit on top. Both defects were filed by the
FD-98 lane from fresh host measurements; this
lane closes them to those measurements.

## Outcome

- FD-99: `sys_preadv64` (the 64-bit dispatcher's
  preadv/preadv2) now implements preadv2's offset
  -1 as the file-position mode and accepts the
  full defined read flag mask 0x3f.
- FD-100: the v2 flags check runs last — below
  the iovcnt range check and the iovec array
  check — in all three wired implementations
  that had it first: `sys_preadv64`,
  `sys_pwritev64`, and `KProcess::pwritev`
  (32-bit pwritev2). `KProcess::preadv` (FD-98)
  already used the measured order and is
  untouched.

## What the host does (filing lane's measurements)

Kernel 6.17.0-1020-oracle, programs in
`test-results/fd98/` on lane-fd98:

- `hostref-preadv.c/.out`: preadv2 with offset -1
  and flags 0 on a descriptor at position 5 reads
  4 bytes, lands at position 9, one IN_ACCESS —
  the file-position mode. preadv2 accepts flag
  bits 0x01 through 0x20 singly (0x20 is
  RWF_NOAPPEND; the read side's defined mask is
  0x3f) and answers EOPNOTSUPP at 0x40. preadv
  (295) with a bad descriptor and offset -1
  answers EINVAL (offset validity precedes the
  descriptor check); preadv2 with the same
  answers EBADF (-1 is valid for v2, so the
  descriptor answers); preadv2 with offset -2
  answers EINVAL even on a bad descriptor.
- `hostref-preadv-order2.c/.out`,
  `hostref-pwritev-order.c/.out`: iovcnt -1 or
  IOV_MAX+1 with an undefined flag answers
  EINVAL; a null or unmapped iovec array with an
  undefined flag answers EFAULT — the flags
  check runs after both.
- `hostref-preadv.out` also pins: a good
  preadv2 call with an unmapped iovec array
  answers EFAULT.

## What was wrong

- `sys_preadv64` answered EINVAL for every
  negative offset including -1, and EOPNOTSUPP
  for any flag outside mask 0x1f — its header
  comment recorded the -1 refusal as measured
  behaviour, which the fresh measurement
  contradicts for preadv2 (it still holds for
  plain preadv).
- All three wired v2 implementations tested the
  flags before the iovcnt range and the array,
  answering EOPNOTSUPP for the two combinations
  above.
- `sys_preadv64`'s array check was null-only.
  `KMemory64::readq` returns 0 on an unmapped
  page, so an unmapped array read as zero-length
  segments: the call returned 0 and generated
  IN_ACCESS where the host answers EFAULT. The
  RED log shows exactly this ("preadv2 with an
  unmapped iovec was not -EFAULT" alongside the
  flag-order failures).

## The fix

- `sys_preadv64` restructured to the family's
  measured order, mirroring `sys_pwritev64`:
  offset validity first (preadv2's -1 excepted
  as the position mode), then the descriptor
  check, the iovcnt range check, the iovec
  array check — now a range check over the
  array (`sys_rangeHasPermission64`, as the
  write side's), the flags check last with
  mask 0x3f. In position mode the walk starts
  at the descriptor's position and, when bytes
  moved, seeks it forward by the total read;
  IN_ACCESS is generated exactly once, as
  before. The stale FD-80 header comment is
  rewritten to the measured contract.
- `sys_pwritev64` and `KProcess::pwritev`: the
  flags test (and its comment) moved below the
  iovcnt range and array checks; nothing else
  in either function changes.

Decisions taken inside the fix, recorded here
because the register entries only summarize
them:

1. **sys_preadv64's offset check moved ahead of
   its descriptor check** (the base had the
   descriptor first, as FD-80's "descriptor
   checked first, against the family" note
   recorded). The fresh bad-descriptor
   measurements make offset-first the host's
   order for this arm too, and both sibling
   implementations are offset-first. This takes
   the host's side of FD-95's open question for
   the preadv arm, exactly as CL-51 did for the
   32-bit arm; the interaction is spelled out
   in CL-53 — if the merge lane settles FD-95
   descriptor-first, this arm's offset check
   moves behind the descriptor check and the
   pinned bad-descriptor cases flip with it.
2. **The array range check** (decision in
   "What was wrong" above): FD-100's named
   unmapped-array combination cannot answer
   EFAULT without it, and it also brings the
   good-flags unmapped case to the measured
   EFAULT.
3. **Two neighbouring combinations were
   characterized, not fixed and not given new
   numbers.** In the two 64-bit arms the mode
   answer comes from the walk's per-segment
   core, behind the range/array checks, so a
   wrong-mode descriptor with an out-of-range
   iovcnt or a bad array answers EINVAL/EFAULT
   where the host answers EBADF. On the read
   side that is a facet of FD-84's open entry
   (the core's mode gate is one of its named
   sites; its fix rides on lane-fd83-84) — a
   dated addendum on FD-84's entry records the
   combination. On the write side it belongs
   to FD-93's composition, whose register entry
   lives on lane-fd92-93; CL-54 names it for
   the merge lane. Elsewhere the orders agree:
   `KProcess::pwritev` gates the mode ahead of
   its range/array checks (FD-94's
   register-pinned host order), and so does
   `KProcess::preadv`.

## Proof

RED on the base (`fa3b5eeef` + the test commit):

- x64 suite, full, serial: exactly the two
  target tests FAILED, everything else passed
  (`test-results/fd99-100/red-x64-full.log`).
  testPreadv64Syscalls: "preadv2 offset -1
  returned -22 with segments '' ''", position
  left at 5 not 13, "preadv2 with RWF_NOAPPEND
  (0x20) did not read 4 bytes at offset 4",
  the three bad-descriptor order cases, all
  four FD-100 combinations, "preadv2 with an
  unmapped iovec was not -EFAULT", and the
  no-notification check. testPwritev64Syscalls:
  the four FD-100 combinations.
- Main suite, targeted (index 652):
  testPwritev32Dispatch FAILED with the four
  FD-100 combination failures
  (`test-results/fd99-100/red-main-pwritev32.log`).

GREEN on the fix, full suites serial
(workers=1):

- Main: 671/671, 0 FAILED
  (`green-main-full.log`).
- x64: 70/70, 0 FAILED (`green-x64-full.log`).
- Browser build of the fixed tree (shared code:
  syscall64.cpp, kprocess.cpp): literal
  'Browser export verified' + '[bw64-build]
  BUILD COMPLETE' (`build-browser.log`);
  BUILD_INFO git_sha `fad85c19c` (the fix
  commit), wasm `52da72d5`, support archive
  `c4dbe35986a5` unchanged. Runtime assets were
  restored first with `bootstrap.py --local`
  from the retained archive
  (`/home/ubuntu/backups/winbox-runtime-assets-20261101/`,
  this base's pins; `bootstrap-local.log`).
  The first build attempt (`build-browser1.log`)
  never started compiling — it timed out
  waiting for the shared build lock held by
  another lane's build; the retry ran clean.
- JS unit: 348 passed, 0 failed
  (`js-unit.log`), run after the browser build.
  No silent-failure baseline change: the fix
  moves existing errno returns without adding
  or removing return sites in the scanned
  files. (An earlier run before any dist
  existed, `js-unit-nodist.log`, failed the
  dist-dependent audits wholesale — the known
  unbuilt-worktree class, not signal about
  this change; the post-build run is the
  evidence.)

## Boundaries and deliberate non-goals

- FD-95 itself is not on this lineage's
  register (it lives on lane-fd92-93, assigned
  to the merge lane as a composition decision
  about plain pread/`KProcess::pread64`); this
  lane touches neither plain pread nor
  `KProcess::pread64`.
- The mode answer's own value is unchanged
  everywhere: the 64-bit arms keep the house
  -EINVAL from the per-segment core (pinned in
  both x64 tests' wrong-mode sections), the
  32-bit arms keep their measured EBADF/EINVAL
  split. Only its position relative to the
  range/array checks on the read side is
  characterized (decision 3).
- Pipe and other non-file descriptors keep the
  per-segment core's answers, unchanged by the
  restructure; position mode only seeks when
  bytes moved, so a drained pipe cannot move a
  position it does not have.
