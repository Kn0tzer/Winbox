# FD-204 / FD-205 — graphics blit no-op and identical-fault loop containment

Branch `lane-fd204-fd205`, base `3e296c28f` (batch 11 + deploy record).
Register entries: `docs/defect-register.md` FD-204 / FD-205 (settled
filings, read 2026-11-08). This record is the fixing lane's evidence;
the register entries close with the fixing commit + this proof named.

## FD-204 — the blit entry points return normally but copy nothing

### Root cause (deterministic, proven by the repro run)

`Image::pixBytes` carries two meanings. The bind sites set it to the
image's TOTAL byte count, on both widths:

- 64-bit `vkBindImageMemory` / `vkBindImageMemory2`
  (`source/vulkan/vk64bridge.cpp`, the two `im->pixBytes =
  imageDataBytes(*im)` assignments)
- 32-bit `vkBindImageMemory` / `vkBindImageMemory2` (same assignment)

But the copy family uses it as BYTES PER PIXEL for the row pitch and
the per-pixel copy, at five sites:

- `vkCmdCopyImage`, `vkCmdCopyImage2`/`2KHR`, `vkCmdBlitImage`,
  `vkCmdBlitImage2`/`2KHR`, `vkCmdResolveImage`/`KHR`

(`U32 pxBytes = (U32)src->pixBytes;` then `srcRowPitch = src->w *
pxBytes`, then the per-pixel loop guarded by `sOff >= sEndB || dOff >=
dEndB`.)

For the harness's 256x256 R8G8B8A8 image, `pixBytes` was 262144
(the total) instead of 4. The row pitch became 256 x 262144, every
pixel's offset landed far beyond the allocation, the guard dropped
all 256 pixels, and the blit returned 0 having copied nothing. The
repro run (wasm `d4164f52`, batch-11-based, skips removed) proved
it: the diagnostic showed `pixBytes=262144` for a 256x256 image and
the drop counter reached x256 per blit call, while the run still
returned 0 and completed.

The same defect makes the whole copy family a no-op on both widths
after bind (the 32-bit create path sets `pixBytes = fmt_bpp(format)`,
the correct per-pixel value, but bind then overwrites it with the
total). The texture-upload and dirty-range paths use `pixBytes` as
the total byte count and are correct as-is, so the bind sites are
untouched.

### Fix

In the copy family (all five sites), take bytes-per-pixel from the
format instead of from the overloaded field:

    U32 pxBytes = fmt_bpp(src->format);
    if (!pxBytes) pxBytes = 4;

`fmt_bpp` is exact for the uncompressed formats the CPU-side copy
loop handles (4 for R8G8B8A8_UNORM); the `!pxBytes` fallback is kept
for formats it does not know. The diagnostic line added for the
repro run was removed once the root cause was pinned.

### Proof (FD-204 closing terms)

vkcov probe run `TAG=fd204-fix` on the fixed build (wasm
`1bd945d2918d9d99d92de6ea2216f859d158f284dbc6a81a3983c3f38832bbb3`,
5,737,538 B; vkcov.exe sha256
`1e9f0cfe92d9a0446c1dfa00c64a1f09050f0163b06271286963f2caf76144ab`,
skips removed):

- Verdict `{"passed":true,"vkcovResult":0,"handlersTested":309,
  "lastTryBeforeEnd":"vkDestroyInstance","errors":[]}`; 315 VKCOV
  lines; `vkcov: RESULT 0`.
- All three blit entry points return 0 on both runs:
  `vkCmdBlitImage=0,0`, `vkCmdBlitImage2=0,0`,
  `vkCmdBlitImage2KHR=0,0` (vkcov-results.json rc1/rc2 all 0).
- Zero pixel drops at the blit guard (the repro run dropped all 256
  per call); zero `guest-fetchfault` / `guest-memfault` lines in the
  whole console.
- Evidence: `test-results/bridge-coverage-fd204-fix/console.log`,
  `summary.json`, `vkcov-results.json`; probe log
  `test-results/fd204-fd205/probe-fix.log`.

### The 2026-11-07 wedge did not reproduce

The repro run and the fix run both completed end to end on the
batch-11-based build; the endless delivered-fault loop from the
2026-11-07 batch-10 run did not recur in either. A single clean run
does not prove the wedge class is gone (it was intermittent, and
the 2026-11-07 run was under wave load while these ran on a quiet
box). Two things now bound it regardless: the blit actually copies
pixels (the deterministic defect is fixed, so the call no longer
silently does nothing), and the FD-205 containment below ends any
recurrence after 128 identical deliveries instead of looping
forever. The 2026-11-07 drop count stopping at 129 (rather than
reaching 256) is consistent with the fault interrupting the blit
mid-way; the exact trigger of that fault was not isolated.

## FD-205 — identical-fault loop containment

### Design

A guest handler that returns to the faulting instruction without
fixing the fault makes the emulator re-deliver the identical fault
forever (observed past 28M deliveries on 64-bit, past 5.5M on
32-bit, and as a 64-line guest-memfault32 loop in a live session).
Containment, at the single funnel for each width:

- 64-bit: `CPU64::raiseSyncFault` (`source/kernel/syscall64.cpp`).
  Key = (signal, `this->rip`, fault address). The callers deliver
  with rip at the faulting instruction, so the key identifies one
  fault.
- 32-bit: `KThread::faultLoopTripped` (`source/kernel/kthread.cpp`),
  called from `seg_mapper` / `seg_access` (key = signal, faulting
  EIP, fault address) and from `CPU::prepareException`
  (`source/emulation/cpu/common/cpu.cpp`, same mapping the branches
  apply).

Threshold `BW64_FAULT_LOOP_LIMIT = 128` (`include/ksignal.h`, with
the full rationale): legitimate repeats at one site are 1-2 (demand
paging, guard-page and stack-growth faults each recur at a new
address); a handler that fixes the fault and re-faults repeats a
handful of times; 128 sits far above any legitimate recurrence and
far below the observed runaway counts. The launcher's pre-handshake
trip (32) is a different, stricter context. A different fault
(different faulting instruction or address) resets the count.

On breach: log `BW64-FAIL fault-loop-contained pid=.. tid=.. sig=..
rip=.. addr=.. count=..` (once per loop) and end the faulting
thread's process via `signal(K_SIGKILL)` -- the Linux/Wine outcome
for an unhandled fault -- returning false so callers take their
existing non-delivery path (`signalProcess` + yield/break). In a
fixture with no thread/process the kill is skipped and false is
returned. Deliberately no completion reset: a fault that is fixed
and the instruction completes, then faults again later, simply
continues the count from where it was (still far under the limit
for any legitimate pattern); a program legitimately faulting 128+
times at the exact same rip/addr with completions in between is not
known in Wine guests.

### Proof (FD-205 closing terms)

Native tests, RED-on-base / GREEN-on-fix pattern (the tests fail on
the base tree: without the containment the loop never trips):

- 64-bit `testFaultLoopContainment64`
  (`source/test/process/testMemoryFault64.cpp`): calls
  `raiseSyncFault` directly, simulating the guest handler's
  sigreturn after each delivery (RIP back to the faulting
  instruction, RSP back to its pre-frame value). 128 identical
  deliveries succeed; the 129th is refused (no new frame, RIP stays
  at the faulting instruction, `faultLoopCount` is 129); a different
  fault address resets the count and delivers; the original fault
  then delivers again. Passes; the log shows
  `BW64-FAIL fault-loop-contained pid=-1 tid=-1 sig=11
  rip=0x79100000 addr=0x79300000 count=129`.
- 32-bit `testFaultLoopContainment32`
  (`source/test/process/testFaultDelivery32.cpp`): installs a SIGSEGV
  handler on a throwaway process and calls `seg_mapper` repeatedly
  with the same address/EIP, simulating the sigreturn (EIP/ESP
  restored after each delivery; `seg_mapper` throws 2 on a delivery,
  1 on the no-handler path). 128 deliveries succeed; the 129th takes
  the no-handler path (throw 1, `thread->terminating`,
  `faultLoopCount` 129); a reset case proves a different fault
  restarts the count. Passes; the log shows
  `BW64-FAIL fault-loop-contained pid=12 tid=13 sig=11
  eip=0xd0000000 addr=0x60000000 count=129` and the post-mortem dump
  on the new stack.
- Guest-level: the vkcov fix run above completed with zero fault
  lines -- the session continues instead of looping. The a4
  live-instance shape (32-bit guest-memfault32 loop) is covered by
  the 32-bit test; no live 32-bit probe was needed.

Test-fixture finding (worth recording, not a product defect):
`makeFaultFixture`'s stack sits at TEST_STACK_ADDRESS (0xE0000000),
above the process mmap limit (0xC0000000), so its mmap silently
fails and the stack is unmapped -- a signal frame cannot be built
there. The new `makeContainmentFixture` helper maps a real stack at
0x70000000 (below the limit) and points ESP/EBP at it. The existing
no-handler tests never touch the stack, which is why this went
unnoticed.

### Native suite results (local, this tree)

- Main (32-bit): 706 tests (705 base + 1 new), 702 OK, 4 FAILED --
  all four are the source-guard tests below; the new 32-bit
  containment test passes in the full-suite run
  (`test-results/fd204-fd205/native-main2.log`).
- x64: 121 tests (120 base + 1 new), 118 OK, 3 FAILED -- all three
  are the source-guard tests below; the new 64-bit containment test
  passes.

The source-guard failures (`x64 syscall fallback guard`, `guest CPU
topology consistency`, `KObject lock stub dispositions`, `x64 futex
dispatch guard`) are environmental, not code: the guards derive the
source path from `__FILE__`, which ccache relativises to
`../../source/...`, and that only resolves when the binary is run
from `project/linux/`. Proven: the syscall guard passes when run
from `project/linux/`. They pass in the checking queue (0 FAILED on
the batch-11 base); the queue submission is the authoritative check.

## Changes (this lane, uncommitted at record time)

- `source/vulkan/vk64bridge.cpp`: FD-204 fix at the five
  copy-family sites (`fmt_bpp(src->format)`).
- `source/kernel/syscall64.cpp`, `source/kernel/kthread.cpp`,
  `source/emulation/cpu/common/cpu.cpp`, `include/cpu64.h`,
  `include/kthread.h`, `include/ksignal.h`: FD-205 containment.
- `source/test/process/testMemoryFault64.cpp`,
  `source/test/process/testFaultDelivery32.cpp`,
  `source/test/testMain.cpp`: the two containment tests + registration.
- `tools/vkcov/vkcov.c` (+ rebuilt `tools/vkcov/vkcov.exe`): blit
  skips removed (the final harness state for the FD-204 proof).

## Light checks

`submit-check lane-fd204-fd205` (one at a time, only when the queue
is idle). Standard acceptance shape: citations 0, js-legacy 0,
native-main 0 FAILED, native-x64 0 FAILED, run-native 0, js-unit
352 passed / 2 failed (known bare-checkout artifacts).
