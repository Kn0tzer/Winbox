# FD-77 — the silent freeze is an msync ABBA deadlock; convicted by the fault-stage capture and fixed

Lane `lane-fd77b` (worktree `/home/ubuntu/wt-fd77b`), off integration
`012247bdd`. Evidence in `test-results/fd77b/`. Register: FD-77 closed
on this branch, closure CL-33 (provisional; settles at composition).

## What the register asked for

The FD-77 entry ended with three candidates for the wait that freezes
a fully booted session before its first window — the per-process
memory mutex, the RAM-page allocator's mutex, or a proxied filesystem
call on a non-archive backing file — and a specified discriminating
instrument: a per-thread marker of which stage of the demand path
each thread reached, printed by the thread dump, plus a visible
owner on the memory mutex, so one parked capture separates the
three. This lane built exactly that instrument, captured one park,
and the capture convicted the first candidate in a specific form:
not a slow section, an ABBA deadlock.

## The instrument (commit `3ca9decf4`)

- `include/faultstage.h` (new): the stage enum — demand entry,
  memory-mutex acquire, memory-mutex work, RAM allocation,
  file-position lock, backing read, install — a save/restore scope
  marker, and `MemoryDemandLock`, the tracked form of the memory
  mutex acquisition: it records the acquire stage while blocked, the
  work stage once held, and the holding thread as the mutex's owner
  (RAII-correct across the recursive mutex's nested acquisitions;
  acquisitions outside the wrapper remain legal and simply do not
  move the owner record, which the header documents).
- `KThread` gains atomic stage fields: stage, stage-entry time,
  faulted page, and for backing reads the file path, offset and node
  type. `KMemory` gains the owner record (tid, depth, since) for its
  mutex; `soft_ram.cpp` gains the same record for the allocator's
  mutex, exposed to the dump as a footer line.
- Stage sites: `FilePage::onDemmand` (both mutex sections; the
  backing path/offset/node type recorded before the read),
  `RWPage::onDemmand` and its RAM-pointer path,
  `CopyOnWritePage::onDemmand`, `CodePage::onDemmand` and its
  write-path sections, and `KFile::preadNative` (position lock vs
  backing read). The dump prints the stage fields per thread, a
  backing-read detail line, the per-process memory-mutex owner, and
  the allocator owner — all atomic loads, so the dump's
  never-block contract is preserved.

Verification of the instrument itself: the citation audit over the
patched tree is 352 rows, 0 drift, 0 missing; the unpatched base in
this tree reproduced the gate wasm byte-identically (`7b377507`)
before the patch, and the instrumented browser build completed with
the completeness gate at 0 failures (wasm `5ebee420`).

## The capture (`test-results/fd77b/run-stage-1/`)

The first armed run on the instrumented build (public route,
Chromium, JIT off, dumps every 30 s) parked. The freeze was complete
by the second dump (~61 s; the console's last milestones are the
persist sync at 59.1 s and the 192 MB line at 59.7 s, then silence).
The final dumps of the main process (pid 10, ten running threads)
read, in the fields the instrument added:

- `memmutex_owner_tid=162`, `memmutex_owner_ms` growing to 635,162.
  Tid 162 sits at stage `mem-work`, `fpage=0x8de0`,
  `rip=0x6f0e605a`, registers holding `0x8de0000, 0x2000`: inside
  `FilePage::onDemmand`'s first tracked section — which contains
  exactly one blocking call, `KProcess::getMappedFile`, itself a
  leaf section on the mapped-files mutex.
- Tid 157 sits at stage `mem-lock`, `fpage=0x8dd3`, at the Wine
  syscall stub `0xb001abb0` with `eax=0x90` — i386 syscall 144,
  msync — and arguments `0x8dd0000, 0x5000, 0x1(MS_ASYNC),
  …, 0xc000000d`. It is inside msync's writeback: the writeback
  demand-faulted page 0x8dd3 of its range (page 4 of 5 — pages
  before it were resident or filled) and waits for the memory
  mutex.
- Tid 123 waits at `mem-lock` on an ordinary demand page; the other
  seven running threads show stage `none` frozen at the register
  set's known block-entry instruction pointers — collateral behind
  the memory mutex through acquisitions the instrument does not
  wrap.
- `ramalloc owner_tid=0` at every dump after the freeze: the
  allocator candidate is cleared for this capture. No thread is in
  the backing-read stage and `proxyfetch inflight=0` at every dump:
  the fetch layer stays cleared, and the third candidate (a proxied
  read) is not what this freeze is made of.

## The mechanism, from the code

`KProcess::msync` took the mapped-files mutex for its whole body,
including the writeback `m->file->pwrite(...)`. The writeback reads
the guest range; reading a page that is not resident demand-faults
it, and the demand path's first act is the memory mutex — after
which `FilePage::onDemmand` calls `getMappedFile`, taking the
mapped-files mutex while still holding the memory mutex. The two
orders meet head-on:

- demand: memory mutex → mapped-files mutex (holder: tid 162);
- msync: mapped-files mutex → (writeback fault) → memory mutex
  (holder: tid 157).

Audit of every other mapped-files-mutex user: `KMemory::mmap` and
`mremap` take the memory mutex before their mapped-files sections;
`getMappedFile` / `getMappedFileForRange` are leaves whose callers
use the result after release; `getModuleName` / `getModuleEip` /
`printMappedFiles` (a post-mortem log path) touch no guest memory
and take no second lock that can cycle. msync was the sole
inversion.

Why this stayed invisible so long: the interleave needs a thread
msync-ing a shared mapping whose later pages were never touched —
exactly what a runtime's cache/file writer does minutes into a
heavy session start, and exactly what a short boot never does —
colliding with any other thread's first touch of a file-backed page
in the same instant. Both halves are everyday operations; only
their overlap is rare, which matches the observed incidence (2
parks in 7 comparable pre-fix public-route runs on the FD-12-lane
build, and the first-run park here).

## The fix (commit `52ee264c7`)

`KProcess::msync` snapshots the first containing mapping inside the
mapped-files section, releases the mutex, and runs the writeback
from the snapshot; the `shared_ptr` keeps the mapping and its file
alive across the write. Behaviour is unchanged (first containing
mapping wins; a range no single mapping contains falls through to
the existing page-check loop, which now also runs without the mutex
it never needed — it reads page flags only). The lock order —
memory mutex before mapped-files mutex; never hold the mapped-files
mutex across guest-memory access — is written at the mutex
declaration in `include/kprocess.h` and at the fix site.

## The deterministic test (commit `5436ead2b`)

`testMsyncLockOrder` (both suites) forms the interleave with no
timing race: it maps three pages of a real file shared, dirties page
0, then the test thread takes the memory mutex. A worker thread
calls msync over the whole mapping; its writeback faults on page 1
and blocks on the held memory mutex — the fault-stage marker
reaching `mem-lock` is the deterministic signal that the worker is
inside the writeback. At that moment a third thread performs a
mapped-file lookup, which must complete. On the unfixed code it
cannot (msync still holds the mapped-files mutex): the test then
releases the memory mutex, the interleave unwinds by itself — the
suite never hangs — and the test fails with the diagnosis. On the
fixed code the lookup completes while the demand is still blocked,
and after release the test also verifies msync returned the full
length and the dirtied word is present in the host file.

RED (unfixed, preserved in `test-results/fd77b/test-red.log`):

    Test msync mapped-files lock order (FD-77 ABBA) ... FAILED
      msync lock order: getMappedFileForRange was still blocked
      after 3s while msync's writeback demand was blocked on the
      memory mutex -- msync holds mappedFilesMutex across
      guest-memory access, inverting the demand path's
      memory -> mappedFiles order (FD-77 ABBA deadlock)

GREEN (fixed, `test-results/fd77b/test-green.log`): the same test
passes in under a second, and inside both full suites.

## Gate figures on the final tree

- Native suites, serial: main 666/666 (baseline 665 + the new test),
  x64 65/65 (baseline 64 + the new test), 0 FAILED.
- JS unit suite: 348 passed, 0 failed.
- Browser build on the fixed tree: BUILD COMPLETE, "Browser export
  verified", git `52ee264c7`, wasm `646f85cc…`, completeness 0
  failures.
- Consumer series on the fixed build (public route, JIT off, dumps
  armed): 6 of 6 runs mapped — one full run mapped at 45.8 s with 6
  frames presented and 0 page errors; five mapped-only runs mapped
  at 71.4 / 64.8 / 57.4 / 57.3 / 53.7 s. Zero parks.

## What remains

FD-77 is closed on this branch (CL-33, provisional). The
instrument stays in the tree: the next freeze of any shape now
arrives with its stage configuration attached. The launcher
surfacing of "booted, no window, no output" as a reported failure
remains specified in the earlier FD-77 record and unbuilt — it was
not part of this lane's conviction work and is not smuggled in here.
