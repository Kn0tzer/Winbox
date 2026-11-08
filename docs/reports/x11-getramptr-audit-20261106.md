# X11 getRamPtr caller audit — 2026-11-06

Lane `lane-x11-getramptr-audit`, base integ tip `faf95d26c`.
Fix commit `cd030f389`. Register: FD-193 filed and closed as
CL-137 (both lane-local; they settle at the next composition).

## What was examined

Every call site of the guest-memory accessor `KMemory::getRamPtr`
in the window-system bridge code: **19 call sites** — 17 in
`source/x11/x11common.cpp`, 1 in `source/x11/xwindow.cpp`, 1 in
`source/x11/xserver.cpp`. `source/x11wire` has none. A sweep for
sibling raw-pointer accessors (`getRamPage`, `getMemData`,
`ramPage`) confirmed `getRamPtr` is the only host-pointer accessor
in these trees.

The accessor's contract (`source/kernel/kmemory.cpp`,
`KMemory::getRamPtr`): it returns nullptr when (a) the span
crosses a page boundary (it logs a `BW64-FAIL getRamPtr` line;
the comment there names the X11 handlers as the guest-reachable
callers), (b) the page is unmapped (`InvalidPage::getRamPtr`
returns nullptr), or (c) the page's permissions do not allow the
requested access (write on a non-writable page, read on a
non-readable one). A mapped but unbacked page is demand-allocated
and is **not** a refusal case. The checked accessors used
elsewhere in the same file (`readd`/`writed`) take the
guest-fault path for a bad guest pointer instead of returning a
pointer at all.

## Found: 12 unsafe call sites (FD-193)

Each passes a guest-controlled address (two also a
guest-controlled element count) and dereferences the result
unchecked, so a guest passing a page-straddling, unmapped or
read-only struct dereferences a null **host** pointer and kills
the emulator session. Line numbers are the base tree's.

1. `xwindow.cpp:94` — `XSetWindowAttributes::get`: guards the
   null address and the page-fit, but returns getRamPtr's
   nullptr (unmapped / read-only page) to its callers instead of
   falling back. One caller (`CreateWindow`) guards the result;
   `ChangeWindowAttributes` (`x11common.cpp:285`) does not —
   `XWindow::setAttributes` dereferences it. The same path also
   dereferenced null when the guest passed address 0 with a
   nonzero attribute mask, because `get()` returns nullptr for
   address 0.
2. `x11common.cpp:1077` — `x11_FreeModifierMap`: argument 1, no
   checks at all (not even a null-argument guard); dereferences
   the `modifiermap` field.
3. `x11common.cpp:3394` — `x11_XRRFreeCrtcInfo`: argument 1;
   dereferences `outputs` / `possible`.
4. `x11common.cpp:3405` — `x11_XRRFreeOutputInfo`: argument 1;
   dereferences `name` / `crtcs` / `clones` / `modes`.
5. `x11common.cpp:3418` + `3420` — `x11_XRRFreeScreenResources`:
   argument 1 struct, plus its guest `modes` array whose span is
   driven by the guest's `nmode` count.
6. `x11common.cpp:3436` + `3448` — `x11_XRRGetCrtcInfo`:
   argument 2 struct (`ncrtc` guard, `timestamp`), plus the
   guest modes array loop.
7. `x11common.cpp:3463` — `x11_XRRGetOutputInfo`: argument 2
   struct (`noutput` guard, `timestamp`, `nmode` used for an
   allocation size and two loops).
8. `x11common.cpp:3572` + `3578` — `x11_XRRSetCrtcConfig`:
   argument 2 struct, plus the `modes[modeId-1]` dereference.
9. `x11common.cpp:3642` — `x11_XRRFreeProviderResources`:
   argument 1; dereferences `providers`.

## Safe by construction: 7 call sites (no change)

Each passes a fresh `KProcess::alloc` result. The guest heap
allocator (`source/util/bheap.cpp`) subdivides pages into
power-of-two slots with the block at slot+4 and length at most
slot−4, and serves large requests as whole-page mappings — so a
fresh allocation's span can never straddle a page, and its pages
are backed and writable. The returned pointer cannot be null.

- `x11common.cpp:3285` XineramaQueryScreens info (12 B)
- `x11common.cpp:3444` GetCrtcInfo info (48 B)
- `x11common.cpp:3473` GetOutputInfo info (60 B)
- `x11common.cpp:3513` GetScreenResources modes array (at most
  6 entries — the host size plus five fixed fallbacks, deduped —
  336 B maximum)
- `x11common.cpp:3527` GetScreenResources resources (32 B)
- `x11common.cpp:3631` GetProviderResources (12 B)
- `xserver.cpp:684` `DisplayData::pCurrentRequest`: the
  `request` field sits at offset 0x60 in the mirrored `Display`
  struct — always in the allocation's first page. (The pointer
  is dereferenced unguarded in `DisplayData::getNextEventSerial`;
  it cannot be null for the reason above.)

Also examined and deliberately not registered (register
Appendix): `BHeap::free(address)` trusts the size word stored in
front of a guest heap block, but every access goes through the
checked MMU path into the guest's own address space, so a
corrupt word can only corrupt or unmap the guest's own heap —
no host impact.

## The fix (commit `cd030f389`)

- Guest struct fields in the eight handlers are read through
  the checked accessor: `memory->readd(address + offsetof(Mirror,
  field))`, per element for the modes arrays. Counts the code
  relies on repeatedly (`timestamp`, `nmode`, modes pointer)
  are snapshotted into locals first, so a guest racing its own
  struct mid-call cannot change the meaning between the guard
  and the use. A bad pointer now takes the same guest-fault path
  the rest of the file already uses.
- `XSetWindowAttributes::get`: when `getRamPtr` refuses, fall
  through to the existing checked `tmp->read()` fallback instead
  of returning the null.
- `ChangeWindowAttributes`: a null attributes pointer with a
  nonzero mask returns `BadValue`; with a zero mask it is a
  no-op `Success`. (`x11_FreeModifierMap` likewise gained the
  missing null-argument guard, returning `Success`.)

After the fix, the only `getRamPtr` calls left in the bridge
are the guarded `get()` call and the 7 safe sites above —
verified by grep on the fixed tree.

## Proof

Four new native tests (`source/test/x11/testX11RamPtrAudit.cpp`,
registered in `testMain.cpp`) drive the real handlers through
the int 0x99 callback table populated by `x11_init()`, plus
`XSetWindowAttributes::get` directly:

- `testX11RamPtrGetFallback` — backed writable page returns the
  direct pointer with guest values visible; fresh read-only page
  must return the fallback buffer.
- `testX11RamPtrFreeHandlers` — within-page control, then
  page-straddling `XRRCrtcInfo` and `XModifierKeymap`.
- `testX11RamPtrGetCrtcInfoStraddle` — within-page control
  (`ncrtc` 0 → returns 0), then a straddling resources struct.
- `testX11RamPtrSetCrtcConfigStraddle` — arguments passing the
  first guard, straddling zeroed resources (`nmode` 0 → must
  return `RRSetConfigFailed`).

Handler invocations run in a forked child that reports EAX back
through a pipe, with a 10 s budget: on the unfixed code the
straddling cases wedge in the host fault handler on the null
dereference (they neither return nor die), so the child is
killed and the failure is reported by the parent instead of
taking the suite down.

Results, from the preserved logs in
`test-results/x11-getramptr-audit/`:

- RED (unfixed code, `red-601.log` … `red-604.log`): all four
  tests fail. The `get` test fails by assertion ("did not fall
  back to the checked read for a read-only page (returned
  (nil))"). The free-handlers run logs the emulator's own
  refusals at the exact straddle addresses —
  `BW64-FAIL getRamPtr op=hostptr addr=0x10000FF8 len=48` and
  `addr=0x10000FFC len=8` — and both straddle children are
  "killed after timeout"; the controls pass. GetCrtcInfo and
  SetCrtcConfig fail the same way (10 s each, straddle child
  wedged).
- GREEN (fixed code, `green-601.log` … `green-604.log`): all
  four pass, `0 tests FAILED`, exit 0, no timeouts.

Two harness facts worth keeping: `CPU::peek32` reads arguments
at the **SS segment base** plus ESP + n·4 (in the native test
process the SS base is 0xDFE00000) — arguments must be written
to that effective address; and a host null dereference inside a
handler does not kill the native test process, it wedges it in
the fault handler — which is why the tests fork.

## Lane notes

- The fresh-worktree prep script failed at its pin check: the
  shared store's `web-runtime/` zips match neither the
  integration manifest nor FD-189's re-cut (a third re-cut; the
  store is self-consistent with its own SHA256SUMS). This lane
  needed no payload links — the audit is source-level and the
  native test binary builds from source — so it proceeded
  without them and flags the drift for the batch-10 composition,
  which lands a matching manifest anyway.
- Light checks for this branch go through the checking queue
  (`checks/submit-check lane-x11-getramptr-audit`); results are
  read from the queue's own logs.

## Queue results (added after the run)

Submitted as `lane-x11-getramptr-audit` at tip `81bd25c49`;
the queue's pipeline 22 checked that exact commit (its
verify-source log shows expected and mounted commit both
`81bd25c492e21ed3c56cadc09ee71d0b60f89c8c`). Read from the
queue's own step logs in
`checks/evidence/pipeline-22/queue-logs/`:

- citations exit 0 (352 rows).
- js-unit exit 1: 352 passed, 2 failed — only
  `provenance-artifacts` and `runtime-completeness`, the two
  known bare-checkout artifacts that need a built `dist/`;
  identical to the queue's Stage-1 proof run on the
  unmodified integration tip. This branch changes no JS.
- js-legacy exit 0 (fail 0).
- native-main exit 0: **696 tests, 0 FAILED** — the 692-test
  baseline plus this lane's four new tests.
- native-x64 exit 0: 116 tests, 0 FAILED.
- run-native exit 0.
