# FD-92 — a failed 64-bit file mmap leaves nothing mapped

Lane `lane-fd92mmap` (worktree `/home/ubuntu/wt-fd92mmap`), base
integ `cc1d89ab3`. The FD-92 entry was carried verbatim from
`lane-fd82`'s defect register (tip `c72ad1340`), where the FD-82
lane filed it by inspection while wiring its birth accounting;
this lane fixes it. Nothing pushed, deployed, or merged; master
and the live site untouched.

## The defect

`sys_mmap64_file` (source/kernel/syscall64.cpp) materializes a
64-bit file mapping in two phases: it establishes the mapping
first, then copies the file content in with positioned reads
(`KFile::preadNative`). Two content-read error returns handed the
read's errno back to the guest with the established range left
behind:

- **Private branch.** For `addr == 0` the range is reserved and
  mapped by `mmapReserveAndMap` up front; for an explicit address
  the pages are mapped by `mmapAnonymousFixed`. The copy loop
  then ran; on `(S32)n < 0` it returned the errno with the pages
  still mapped and the range still reserved.
- **Shared branch, `addr == 0` only.** The reservation is taken
  before the branch split; the seed read that fills the staging
  buffer runs before `mmapSharedFile` installs the shared pages,
  and its error return likewise left the reservation in place.
  (With an explicit address the shared branch has established
  nothing at that point, and its behaviour is unchanged.)

A failed host `mmap` leaves nothing mapped. A guest that trusted
the failure believed the range unmapped — an access there
succeeded instead of faulting — and the range was unavailable
for reuse. On a tree carrying FD-82's accounting the leaked
mapping also keeps counting against its description's
close-event deferral, since the birth is recorded before the
read precisely because the mapping exists from that point.

## The fix

Both error returns now release the established range through
`KMemory64::munmap(aligned, mapLen)` before returning the errno:

- The private branch unmaps unconditionally — the span
  `[aligned, aligned + mapLen)` covers both establishment forms
  (the `mmapReserveAndMap` reservation and the
  `mmapAnonymousFixed` map).
- The shared branch unmaps only when it took the reservation
  (`reserved`).

Composition with FD-82 (`lane-fd82`, CL-45) is by construction:
that lane counts the private mapping's birth before the content
read and reports mapping deaths from `KMemory64::munmap`, so
this teardown releases the birth too; in the shared branch it
counts the birth only after the shared pages are installed, so
the seed-failure teardown has no birth to release.

## The trigger, stated honestly

The error return is part of `KFile::preadNative`'s contract: it
forwards the backing's `readNative` result verbatim, and its
callers (this function; `sys_pread64`'s factored core) are
written against `(S32)result < 0` as the error answer. The
in-tree file backings today fold host read errors into a `0`
(EOF) answer (`FsFileOpenNode::readNative` does so deliberately,
with a trace; the dir, mem and zip open nodes likewise never
return negative), so **no real file in the tree currently makes
the positioned read fail** — the defect is latent at the
interface, exactly as the FD-82 lane filed it ("found by
inspection"). The proof therefore drives the contract at that
interface: the test installs an open description whose
`readNative` serves a fixed pattern up to a byte budget and
then returns `-EIO`. That is the recorded failure — an errored
positioned read — produced at the exact call the defect lives
behind.

## Proof

New test `testMmap64FileReadFailure`
(source/test/process/testMmap64FileReadFailure.cpp, registered
in both TEST_ENTRIES lists), in the x64 native suite because the
materialized path exists only in the 64-bit build. It drives the
real 64-bit syscall dispatch (mmap / munmap / close) through
`ksyscall64`, with five cases:

- **A** — explicit address, MAP_PRIVATE, read fails on the first
  byte: the syscall must return `-EIO` and leave both pages
  unmapped and the range reusable.
- **B** — explicit address, MAP_PRIVATE, read serves one page
  then fails: same expectations.
- **C** — control: a never-failing description maps, serves its
  content byte-for-byte, and unmaps cleanly.
- **D** — `addr == 0`, MAP_PRIVATE, read fails on the first
  byte: the reservation must be released. A control allocation
  pins the mmap cursor, so the reservation the failing call
  takes is the two pages right after the control range (the gap
  search starts at the cursor).
- **E** — `addr == 0`, MAP_SHARED, the seed read fails: the
  reservation must be released, located the same way.

RED on the base (`test-results/fd92mmap/red-mmap64-fileread-x64.log`):
the test failed with exactly the recorded assertions —

```
case A: failed mmap left pages mapped at 0x100000000
case B: partially-read failed mmap left pages mapped at 0x100100000
case D: failed mmap left the reserved range mapped at 0x700003000
case E: failed shared mmap left the reserved range mapped at 0x700008000
```

— and the control case passed: the test discriminates on the
error path only, and every failing call did return `-EIO` (the
error contract was never the broken half).

GREEN on the fixed tree
(`test-results/fd92mmap/green-mmap64-fileread-x64.log`): the
test passes. Full native suites, serial
(`full-x64.log`, `full-main.log`): x64 **70/70**, main
**668/668**, 0 FAILED (x64 baseline 69 + this test; main
unchanged — the changed code is 64-bit-only and the test file
compiles to an empty translation unit there).

Browser build of the changed shared code
(`bootstrap.log`, `bootstrap-check.log`, `browser-build.log`):
clean-bootstrap staging only — `bootstrap.py --local` from this
base's pinned retained archive
(`/home/ubuntu/backups/winbox-runtime-assets-20261101/browser-runtime-20261101.tar`)
restored and verified 13 runtime assets; `bootstrap.py --check`
PASS (17 checks, including the FD-73 retained-archive line);
`tools/bw64-build.sh`: **"Browser export verified" / BUILD
COMPLETE**, build git_sha `e37fa3c04` (the fix commit), wasm
`16abfc4c`, support archive `c4dbe35986a5` unchanged. JS unit
suite (`js-unit.log`): **348 passed, 0 failed**.

## Boundaries

- For an explicit-address map over a live mapping, the prior
  contents were already destroyed by the map phase's zeroing —
  the pre-existing two-phase materialization contract, which
  snapshots only the partial head/tail pages. Teardown therefore
  leaves the range unmapped rather than resurrecting the prior
  mapping. "Nothing mapped" is the failed-host-mmap contract
  FD-92 specifies, and it is what the guest now observes; the
  content loss predates this defect and is not widened by the
  fix.
- `mmapSharedFile`'s own failure return, after a successful
  seed read, is a separate contract (its callers include loader
  paths) and is not changed here.
- Nothing else in `sys_mmap64_file` changes: success paths,
  head/tail preservation, the BW64_FMMAP / BW64_MAPWATCH traces
  and the module-tracking notes are untouched.

## Numbering collision (flagged for the merge audit)

The entry number **FD-92 is kept exactly as carried** from
lane-fd82, per the coordination note. The same number is used by
unrelated entries on two other sibling branches: `lane-fd92`
carries an FD-92 for a spirvfix output-ordering defect (marked
FIXED there), and branch `lane-fd92-93` is chartered on the
FD-92 / FD-93 numbers. The closure is filed provisionally as
**CL-46** (CL-40..CL-45 are held provisionally by the FD-66,
FD-65, FD-85, FD-87, FD-64, FD-05 and FD-82 filings). The
composing merge settles both numberings by audit.

## Register changes on this branch

FD-92 carried verbatim from lane-fd82 (commit `44d3cc13f`),
then marked FIXED in place with a pointer to CL-46; a Section B
row for this lane; CL-46 filed provisionally in the closure
list. Commits: `44d3cc13f` (register carry + test, RED),
`e37fa3c04` (fix), and this record's commit (closure).

## Environment notes (lane-local, for the next fresh worktree)

A fresh worktree cannot run the browser build as-is: it needs
`node_modules` (symlink to the main repo's, as the integ tree
does) and the generated payload inputs of
`tools/browser/stage-dependencies.py` (`tools/dxvk/build.w32`,
`build.w64`, and the generated `tools/rootfs64` payload dirs),
which this lane copied from the integ tree for the build and
removed again at strip time. Both gaps fail loudly inside
`build.mjs` (ENOENT), not silently. Separately: three build
launches early in this lane survived their SSH sessions as
detached process groups and briefly compiled the same objects
concurrently; all were killed by exact pid/cwd matching, the
build directory was deleted, and the build was redone as a
single instance — the RED/GREEN evidence above comes only from
the single-instance builds.
