# BUG: Host (bridge) writes to guest PE BSS do not land

**Found by:** bridge API-coverage harness (vkcov), 2026-10-09.
**Severity:** High — any bridge out-pointer write to a guest BSS/global silently fails.
**Lane:** Filed for a fix lane (emulator/KMemory, not bridge; this lane is measurement-only).

## Repro (minimal, in-browser verified)

Guest `vkcov.exe` (Windows PE, Wine):
```c
static VkInstance inst;  // BSS, address 0x4001b958
pvkCreateInstance(&ci, 0, &inst);   // bridge: wr64(0x4001b958, 0x564b0100), rc=0
// guest reads inst -> 0x0   (expected 0x564b0100)
```

Control (same binary, same run):
```c
VkInstance inst2;  // stack, address 0x21f9a0
pvkCreateInstance(&ci, 0, &inst2); // bridge: wr64(0x21f9a0, 0x564b0102), rc=0
// guest reads inst2 -> 0x564b0102   (correct)
```

Probe log (`test-results/bridge-coverage-vkcov-run5/console.log`):
```
log: VKCOV-NOTE: post-create inst=0x0 addr=4001b958
log: VKCOV-NOTE: stack-create rc=0 inst=0x564b0102 addr=21f9a0
log: VKCOV-NOTE: enumerate rc=0 n=1 inst=0x564b0102   (works once instance is valid)
```

## What is ruled out

- Bridge dispatch: correct (fnId enums match 174/174; bridge klogged the right instance ID).
- `wr64`/`memcpyToGuest` logic: works for stack/heap (vkd3d, DXVK games all rely on it).
- Guest-side memory: BSS reads as 0 (zero-init works); guest assignments to BSS work.
- The write is silently dropped — no fault, no log; `wr64` guards on `a && g_mem`
  (both non-null here).

## Suspect

`KMemory64::memcpyToGuest` page/DTLB coherency for the PE image region:
either the DTLB fast path (`k64DTlbLookup`) serves a stale host page for the BSS
page, or `commitPageLocked` + `k64DTlbInsert` diverge from the page the guest's
read path uses. The guest's read path returns the (correct) zero-init contents;
the host's write lands elsewhere (or nowhere guest-visible).

## Impact

- vkcov had to route ALL bridge out-pointers through stack temporaries
  (see NOTE in `tools/vkcov/vkcov.c` `do_setup`).
- Any present/future guest that passes a BSS/global out-pointer to a bridge
  call will silently get zeros. vkd3d/DXVK are unaffected (they use heap/stack).

## Suggested next step for fix lane

Repro without a browser: native unit test that maps a page, writes via
`memcpyToGuest` to an address in the 0x40000000 region, reads back via the
emulator read path; then bisect DTLB vs commit path (try with DTLB disabled).
Check `k64DTlbInvalidateAll` coverage on `commitPageLocked` reallocations.
