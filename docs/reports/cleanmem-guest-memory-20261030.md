# Guest-memory cleanup, logging, and recovery — lane record (2026-10-30)

Branch `lane-cleanmem`, worktree `wt-cleanmem`, base: integration tip
`538f03db5`. Standard followed: `docs/failure-logging-and-recovery.md`
(pathfinder lane) and the clean-up skill. Scope: guest memory management —
`source/kernel/kmemory.cpp` / `include/kmemory.h` (32-bit) and
`source/kernel/kmemory64.cpp` / `include/kmemory64.h` (64-bit).

## What was audited

- Every failure exit in both memory managers: `mmap`/`munmap`/`mprotect`,
  `mlock` family, `madvise`, `mremap` helpers, guest-buffer copy paths
  (`performOnMemory`, `getRamPtr`, `memcpy`), page-fault reporting.
- Every caller of the paths changed, to confirm the new behaviour is the
  one callers can survive (evidence over inference).
- The syscall layer above both managers (`sys_madvise` in
  `source/kernel/syscall.cpp`, `sys_madvise64` in
  `source/kernel/syscall64.cpp`) to see which manager-level paths a guest
  can actually reach.

## Findings and changes

### 32-bit (`kmemory.cpp`, `kmemory.h`) — commit 23e157d35

1. **`KMemory::performOnMemory` panicked the whole emulator on unmapped
   guest buffers.** `InvalidPage::getRamPtr` returns `nullptr`
   unconditionally, and three sites in `performOnMemory` answered that
   with `kpanic`. With recovery off (the default), `kpanic` exits the
   emulator process — so a guest handing an unmapped buffer to a
   read/write-style operation killed the program instead of getting a
   short result. All three panics are gone. The function now returns
   `bool` (false = a chunk had no host RAM; the copy stopped there and
   the failure was reported with address, length, operation and a running
   count). All 27 in-tree callers invoke it as a statement, so the
   signature change is source-compatible; callers degrade to the short /
   zero counts they already handle.
2. **`KMemory::getRamPtr` panicked on cross-page requests.** X11 handlers
   pass guest addresses plus computed lengths; a request spilling past
   the page end hit `kpanic`. It now logs (counted) and returns
   `nullptr` — exactly what its 64-bit sibling has always done for the
   identical request, and what guarded callers (e.g. `common_lock.cpp`)
   already expect.
3. **`KMemory::memcpy` panicked on integer overflow** of
   `address + len`. It now logs (counted) and returns without copying.
4. **`mlock` / `munlock` / `mlockAll` returned `-K_ENOMEM` silently.**
   Every denial now logs one `BW64-FAIL` line: operation, range or
   totals, and the reason (`reason=unmapped`, or `reason=limit` with the
   locked/requested/limit byte counts).
5. **`madvise` silently accepted unimplemented advice values.** The
   default branch now logs `op=hint outcome=noop` with the advice value
   and still returns 0 — the no-op success is deliberate (see "left
   alone"), but it is no longer silent at the manager layer.

### 64-bit (`kmemory64.cpp`, `kmemory64.h`) — commit 9d52bc981

6. **`lockRange` / `unlockRange` / `lockAllCurrent` returned `-ENOMEM`
   silently.** Each denial now logs one `BW64-FAIL` line with the range
   or totals and the reason (unmapped page, or `RLIMIT_MEMLOCK` exceeded
   with locked/requested/limit values).
7. **`madvise` default branch** logs the same `op=hint outcome=noop`
   line as the 32-bit side and still returns 0.
8. **`mprotect` silently skipped unmapped pages** in the requested
   range. The skip itself is documented semantics (see "left alone");
   the function now counts skipped pages and, when any were skipped,
   logs `outcome=partial` with range, request and skip count — the
   partial application is no longer invisible.
9. Removed a dangling comment fragment ("Mirrors the" with no object) in
   the `MAP_SHARED` design comment.

### Logging shape

All new lines are single greppable `BW64-FAIL key=value` lines per the
pathfinder standard. Repeat-prone lines (bad buffer / cross-page /
overflow) use the counting cap — first 64 in full, then every 1024th,
always with `count=` — via a small file-local helper in each manager.
One-shot configuration-style denials (lock limits) log unconditionally.
Reporting is never gated behind a flag.

## Proof

New regression tests (commit 132930bca), each exercising a path that
previously panicked or silently mis-answered:

- `testMemoryPerformOnMemoryBadBuffer32` — callback must not run for an
  unmapped buffer; the test process surviving is the assertion.
- `testMemoryGetRamPtrCrossPage32` — in-page pointer returned; a
  page-crossing request returns `nullptr`.
- `testMemoryMemcpyOverflow32` — ordinary copies still work; a wrapping
  copy is refused (and does not happen).
- `testMemoryMadviseLayer32` / `testMemoryMadviseLayer64` — behaviour
  preservation guards: `MADV_DONTNEED`/`MADV_REMOVE` still discard,
  unimplemented advice still returns 0 (now with a log line).

Red/green, run as single tests against binaries built from this tree:

- Unfixed code: `testMemoryPerformOnMemoryBadBuffer32` dies with
  `KMemory::performOnMemory failed to get ram addr=0x60000000 …`
  (exit 1); `testMemoryGetRamPtrCrossPage32` dies inside the
  `KMemory::getRamPtr` panic (exit 1); `testMemoryMemcpyOverflow32`
  dies with `integer overflow in memcpy` (exit 1). The madvise guard
  passes on unfixed code, as designed (behaviour preservation).
- Fixed code: all four pass, and the new reporting is visible in the
  run output, e.g. `BW64-FAIL performOnMemory op=copy addr=0x60000000
  len=4 page=0x60000 readOnly=0 tid=11 pid=10 outcome=stopped count=1`
  and `BW64-FAIL madvise op=hint addr=0xF0001000 len=4096 advice=7
  outcome=noop count=1`.
- Neighbourhood: the 15 tests surrounding the new ones (mremap,
  fork-madvise inheritance, clone family, x64 dispatch guard,
  topology, /proc maps) all pass.
- Full native suite, serial, fixed code: **652 passed / 0 failed**
  (648-test baseline + the 4 new tests above).
- 64-bit (X64Test binary): `testMemoryMadviseLayer64` passes, in a
  9-test window with its x64 neighbours (madvise-remove/fork, mlock
  accounting, signal-frame tests), 0 failed. The existing x64 mlock
  accounting test also exercised the new lock-denial reporting live:
  `BW64-FAIL lockRange op=lock … reason=unmapped outcome=-ENOMEM`,
  `BW64-FAIL lockRange op=lock … reason=limit locked=8192 new=4096
  limit=8192 outcome=-ENOMEM`, and `BW64-FAIL lockAllCurrent op=lock
  reason=limit locked=0 new=81920 limit=8192 outcome=-ENOMEM`, plus
  the madvise line for advice 7 at the test's fixture address.
  (The known pre-existing X64Test full-run segfault after ~33 passes
  on pristine code was not chased; single-window runs were used.)

## Deliberately left alone

- **`KMemory::logPageFault`'s terminal `kpanic("pf")`** — it is the
  diagnostic dump at the end of an already-unrecoverable fault path;
  loud by design, not a silent failure.
- **The `removeCodeBlock` panic (32-bit)** — an internal code-cache
  invariant, not reachable from guest input.
- **64-bit `mprotect` hole-skipping semantics** — documented v1
  behaviour in `include/kmemory64.h`; guests are written against it.
  Only the silence was removed, not the semantics.
- **Writes on the 64-bit side** — commit-on-write by design and cannot
  fail; nothing to report.
- **Syscall-layer `madvise` validation** — both syscall entries already
  validate the advice value and implement `MADV_DONTNEED` themselves;
  the manager-level default is defence in depth, hardened with a log
  line only.
- **Env-gated diagnostic hooks in `kmemory64.cpp`** (`STRAYWRITE`,
  `MEMRING`, `BSSDIAG`, `BW64_WATCH`) — load-bearing debugging
  infrastructure used by recent fault hunts, not dead code.
- **`bw64RecoverLogOnce` gating on the integration tip** — the
  pathfinder's ungating exists on `lane-cleanlog` and lands when that
  branch merges; this lane's lines use direct counted logging, matching
  these files' existing convention, so nothing here waits on it.

## Residual risks / notes for the merge

- Some X11 callers dereference `KMemory::getRamPtr` results without a
  null check. They now receive `nullptr` for cross-page requests instead
  of a process-wide panic — strictly better, but a caller-side audit
  belongs to the X11/graphics lane.
- `performOnMemory`'s new `bool` is available to callers that want to
  distinguish "guest buffer unmapped" from a short copy; existing
  callers ignoring it get report-and-continue behaviour today.
- Build/test environment note: during this lane, `project/linux/Build`
  state in this tree was lost more than once while the box was under
  severe disk pressure (two completed builds vanished before they could
  be run). Recovery: test binaries were copied out of the Build tree in
  the same shell command that linked them and run from the copy; one
  rebuild was shortened by transplanting objects for byte-identical
  sources from a sibling lane tree on the same base, forcing rebuild
  of every file whose source differed (branch diff plus both trees'
  uncommitted changes) — the resulting binary passed the full suite
  652/0, which is the check that validates the transplant.
