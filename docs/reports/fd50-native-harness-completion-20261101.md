# FD-50 — the native bridge harness aborted at the pipeline-cache call; it now runs to completion

Lane: `lane-fd50` (worktree `wt-fd50`), off integration tip `306190757`.
Register: FD-50 stays in Section A until this branch lands (a Section B
row records this fix); FD-66 opened for the readback-leak finding the
completed run exposed. Evidence: `test-results/fd50/` in this worktree.

## The defect as found

`python3 tools/vkd3d/tests/run-native.py` on the pristine base dies
with SIGABRT. A gdb backtrace of the abort (preserved in
`test-results/fd50/gdb-pristine.log`) lands on:

- `main` at `vk64_native_test.cpp:1315` — the
  `vkGetPipelineCacheData(deviceId, 1, &cacheSize, nullptr)` call;
- the shim wrapper, which packs the host address of the test's
  `cacheSize` variable verbatim into the trap arguments;
- `vk64Bridge` fnId 921 at `source/vulkan/vk64bridge.cpp` (the
  fn-921 case), writing its 8-byte zero answer through that value
  as a guest address;
- the fixture's `KMemory64::memcpyToGuest` bounds assertion
  (`addr = 0xFFFF…` — a host stack address on this machine —
  against the fixture's 1 MiB guest memory), which kills the
  process.

Every section after that call in `main` never executed — including
the FD-16 SPIR-V audit guards and the FD-62 guard's future home.

## Root cause

The shim has two kinds of entry points. Locally answered ones (`S()`
in its dispatch table — granularity, memory commitment, sparse
requirements, peer features…) write their constant answers straight
into the caller's pointer, so a test may hand them a host pointer
and read the host variable back. Trapping ones (`E()`) forward the
pointer value to the bridge as a **guest address**, because in a
real guest the caller's pointers are guest addresses; the bridge
answers through the guest-memory subsystem, and this fixture's
convention for every other trapping call is to pass a guest address
and read the result back from fixture memory.

The pipeline-cache pair moved from the first kind to the second
when the bridge took ownership of the answer ("the bridge keeps no
persistent pipeline cache blob", so the honest answer is a zero
size, written by the bridge). The test's fn-921 call site never
moved with it: it is the only trapping call in the file that passes
a host pointer and expects a host write-back. The shim and the
bridge are both correct for real guests; the value that violated
the fixture's memory model entered at the test's call site. This is
the register's second named direction: the harness must give those
calls guest memory they may legally write.

## The fix

One file, `tools/vkd3d/tests/vk64_native_test.cpp` (+14/−2): the
fn-921/922 block now reserves a guest slot from the fixture's own
allocator, seeds it with the same sentinel (77), calls the wrapper
with the guest address, pins that the call actually trapped to the
bridge (`lastTrap == VK64_fn_vkGetPipelineCacheData` /
`VK64_fn_vkMergePipelineCaches` — the ownership contract whose
change caused this), and asserts the size read back from guest
memory is 0. No assertion was weakened, skipped, or caught: the
original checks (success result, size becomes 0) run at full
strength through the real marshalling path, and the trap pin adds
one. No product code is touched.

## Proof

- **Before:** the official runner fails — the test binary dies with
  SIGABRT at the fn-921 call (`run-pristine.log`,
  `run-manual.log`, `gdb-pristine.log`).
- **After:** `run-native.py` runs to completion — exit 0, 52 PASS
  lines (`run-official.log`). 52 is exactly the count the FD-16
  lane measured with the two assertions skipped in a scratch copy;
  nothing is skipped now. The previously masked sections execute,
  including `GUARD descriptor-heap texel arrays are admitted when
  R15a-servable and refused by name otherwise (FD-16)`, the
  multi-signature FD-16 guard, the remaining SPIR-V audit guards,
  and the 64-bit and 32-bit query-pool sections that end the run.
  Guards appended by later work (FD-62's native guard) now have a
  harness that reaches them unchanged.
- **Sabotage:** with the bridge's fn-921 write withheld (a scratch
  edit, reverted byte-identically afterwards), the harness aborts
  at the new read-back assertion
  (`memory.readq(cacheSizeAddr) == 0`, test line 1326 —
  `run-sabotage.log`). The assertion discriminates.
- **Sanitizer flavour:** the ASan+UBSan binary executes all 52
  sections with every assertion passing and zero memory or
  undefined-behaviour reports (control run with only the leak
  check off: exit 0, 52 PASS — `run-san-noleakcheck.log`). Its one
  finding is registered as FD-66, below. Through the runner, the
  instrumented suite also exceeds the runner's 30 s execution cap
  on a loaded box (~35 s wall at load ~16), so the runner reports
  a timeout instead of the leak report (`run-sanitizers.log`);
  the binary was therefore run directly for the flavour proof.

## What the completed run exposed — FD-66

LeakSanitizer reports 1,132 bytes in 5 allocations at exit, all
from the bridge's `setupReadbacks` mallocs
(`run-san-manual.log`). Three harness sections call
`setupReadbacks` directly for readback-planning checks and never
call `completeReadbacks`; production submit paths always pair the
two and `completeReadbacks` frees every block, so no production
leak is proven. The latent half is in the bridge:
`setupReadbacks` opens by clearing both pending vectors without
freeing blocks still pending, so an unpaired setup followed by
another setup loses the first round's blocks silently — today
reachable only from the fixture. Registered as FD-66 with both
candidate fix directions; not fixed here, per the register's rule
that a lane registers what it finds beyond its own defect.

## Merge notes

Composition is trivial: one test file, the register (Section B row
+ FD-66), and this record. FD-66's number was FD-61 on the branch, colliding with the
FD-61/FD-62/FD-63 the FD-55/FD-57 branches carry for unrelated
items — the merge lane renumbers one set. Nothing pushed, nothing
deployed; master and the live site untouched.
