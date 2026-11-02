# FD-92/FD-93 — 32-bit readv wired; the write family's wrong-mode answers

Lane `lane-fd92-93` (base: integration tip `cc1d89ab3`), 2026-11-02.
Register: FD-92 and FD-93 carried verbatim from `lane-fd83-84`'s
register (tip `936dfc646`), closed here as CL-47 and CL-48
(provisional numbers — see "Numbering" below). New filings from
this lane: FD-94, FD-95, FD-96. Evidence: `test-results/fd92-93/`.

## What was filed

- **FD-92** — the 32-bit dispatcher's readv (145) was a null
  slot answering -ENOSYS; only the 64-bit readv existed. The
  FD-83 one-IN_ACCESS-per-successful-call contract applies to it.
- **FD-93** — the write family answered -EINVAL (or a silent 0)
  where the host answers -EBADF for a write on a descriptor
  opened read-only: the mirror of FD-84, host-measured by the
  filing lane.

## Host measurements (this lane, host kernel 6.17.0-1020-oracle)

The filing lane's write matrix (`test-results/fd83-84/hostref-write.c`)
was extended before fixing (`hostref-write2.c`):

- write on a bad descriptor with count 0 answers EBADF (the
  descriptor answer precedes the zero-count return).
- writev validation order: mode ahead of the iovcnt range check
  (read-only descriptor + iovcnt -1 → EBADF) and ahead of the
  iovec pointer check (read-only + NULL iovec → EBADF); on a
  writable descriptor iovcnt -1 and IOV_MAX+1 → EINVAL, NULL
  iovec with iovcnt 1 → EFAULT, with iovcnt 0 → 0.
- writev with a good first segment and a bad second returns the
  bytes written (3), not the error.
- The positioned family answers a negative offset EINVAL ahead
  of **even the descriptor check**: pread, pwrite, preadv and
  pwritev on a bad descriptor with offset -1 all answer EINVAL
  (`hostref-off-order.c`). FD-84's composed fix orders the
  descriptor check first in `sys_pread64Impl`/`sys_preadv64`, so
  that combination diverges there — filed as FD-95 rather than
  widened into this lane.

## FD-92 — the fix

- Slot 145 now dispatches to a new `syscall_readv`, which
  forwards to a new `KProcess::readv`. Validation mirrors
  `sys_readv64`'s, in the host's order: bad descriptor EBADF,
  wrong mode EBADF, iovcnt outside 0..IOV_MAX EINVAL, null
  iovec with iovcnt > 0 EFAULT.
- The walk is a new `KObject::readv`, the read mirror of
  `KObject::writev`, in the host's (and `sys_readv64`'s) shape:
  zero-length segments skipped, a segment error after bytes
  were read returns those bytes, a short segment ends the walk.
- Notification (FD-83's contract): exactly one IN_ACCESS per
  successful call. A byte-moving walk notifies per segment
  through the file layer and the inotify queue coalesces those
  into one observable event; a successful call that moved no
  bytes (EOF, iovcnt 0, all-zero-length segments) raises its
  one event in `KProcess::readv`. Failures generate none; plain
  read at EOF stays silent (FD-59).

One boundary characterized, not changed: the host answers a
readv whose segment lengths overflow with EINVAL; neither the
64-bit walk nor (now) the 32-bit walk implements that sum
check. It is shared by both dispatchers and was left as found.

## FD-93 — the fix

Every site the entry names, in the host's validation order:

- `KProcess::write`: -EINVAL → -EBADF for !canWrite.
- `KProcess::writev`: -EINVAL → -EBADF, and the walk gains the
  validation its 64-bit counterpart carries (iovcnt range
  EINVAL, null iovec EFAULT — both after the mode check, per
  the measured order). On the base, the null-iovec case never
  returned at all: the walk dereferenced address 0 and the
  guest took a fault there.
- `KProcess::pwrite64`: gained the two checks it lacked, after
  the directory check and before the buffer check — negative
  offset -EINVAL (ahead of the mode answer), then !canWrite
  -EBADF. Without the mode check the call fell through to the
  backing write, whose host EBADF the backing layer swallows
  into a silent 0; the RED run's log shows the backing layer
  reporting that host EBADF (`errno=9`) while the call itself
  answered 0.
- `sys_write64` (64-bit): a descriptor+mode gate now runs
  ahead of the zero-count return (bad descriptor EBADF — with
  the body's existing fd 1/2 console-tee carve-out — and
  read-only EBADF); the body's own later gate changed EINVAL →
  EBADF. One correction to the filed entry's text: it says
  `sys_write64` "carries no mode gate at all"; the base does
  carry one (answering -EINVAL), but only after the buffer
  copy, and the count==0 return bypasses it entirely — the
  entry's observable claim (plain write and, through it,
  writev inherit the wrong answer) stands.
- `sys_writev64`: gained the validation block its read
  counterpart carries (descriptor, mode, iovcnt range, iovec
  pointer, in that order). On the base, iovcnt 0 on a read-only
  descriptor returned a silent 0 and an out-of-range iovcnt
  was never EINVAL.
- `sys_pwrite64`: reordered to the measured positioned-family
  order — negative offset -EINVAL ahead of the descriptor and
  mode answers; mode answer EINVAL → EBADF.
- The 32-bit dispatcher's `syscall_pwrite64` checks the
  offset's high word ahead of forwarding, so the 32-bit route
  keeps the same offset-before-descriptor order (the shared
  `KProcess::pwrite64` orders its own checks behind its
  descriptor lookup, like every sibling core there).

FD-84's sites (`KProcess::read`, `KProcess::pread64`,
`sys_pread64Impl`) were deliberately not touched — that fix
lives on `lane-fd83-84`; the two lanes compose at merge.

## Tests and proof

Commits: `48bb12b1c` (tests), `8d2da6430` (fix),
`0acf72a4b` (silent-failure baseline review).

- `testReadv32Syscall` (main suite) drives the real int 0x80
  dispatch: the full FD-83 notification contract (one segment,
  three segments, short final segment, EOF, iovcnt 0,
  zero-length segments — exactly one IN_ACCESS each; failures
  none), the validation matrix, plus the 32-bit pwrite64
  dispatcher's offset-before-descriptor order. Segments are
  staged host-side and flushed after `testNewInstruction`,
  which wipes the test heap — the first version of this test
  wrote them before it and read zeros back, a harness artefact
  worth recording for the next dispatcher test.
- `testWriteFamilyWrongMode` (both suites) pins the matrix at
  the shared `KProcess` layer.
- `testWriteFamily64Syscalls` (x64 suite) pins it through the
  64-bit dispatcher, including write on a bad descriptor with
  count 0 and pwrite64's offset-before-descriptor order.

RED on the base (final test form): all three tests fail —
readv's 20 assertions (every call -ENOSYS), the write family's
EINVAL/silent-0 answers (9 assertions + the null-iovec guest
fault at the KProcess layer, 11 at the dispatcher layer).
GREEN with the fix: all three pass; full native suites serial
main main 670/670 and x64 x64 71/71, 0 FAILED. Browser build of the
fixed tree: BUILD COMPLETE, 'Browser export verified' (wasm
`bcc8a852`); JS unit 348 passed / 0 failed (after the reviewed silent-failure baseline bump for `syscall64.cpp`, 115 -> 116, commit `0acf72a4b` — the new validation returns are guest-answer returns in the same unlogged shape the file's baseline already carries), legacy 110 passed / 4 failed (FD-64's standing descriptor-set fixture set on this base, unchanged).

## Also filed by this lane

- **FD-94** — pwritev/pwritev2 are undispatched on the 64-bit
  dispatcher (present in the name table only; no dispatch case)
  and absent from the 32-bit table entirely: FD-92's write-side
  twin, found while scoping FD-93's positioned family. The
  measured offset-before-descriptor order is recorded in the
  entry for the wiring lane.
- **FD-95** — the (bad descriptor, negative offset) combination
  in the read family: the host answers EINVAL; FD-84's composed
  fix (descriptor first) answers EBADF there, and
  `KProcess::pread64` carries no offset check in either shape.
- **FD-96** — the 32-bit vectored write's partial-error shape:
  `KObject::writev` returns a later segment's error even after
  earlier segments wrote bytes, where the host returns the
  bytes written (measured: 3) and the 64-bit walk returns them
  too. FD-93's fix adds validation ahead of that walk but does
  not change the walk's return shape.

## Numbering (coordination note)

The number FD-92 is used twice across sibling branches: this
lane's FD-92 (the filing lane's number, carried exactly) and an
unrelated shader-fixup entry on branch `lane-fd92`. Likewise
CL-45 names both FD-83's closure (lane-fd83-84) and the
provisional closure of that branch's FD-92. This lane's
closures are CL-47 (FD-92) and CL-48 (FD-93), verified unused
on every lane branch at filing; all numbers here are
provisional until the composing merge's audit settles them.
