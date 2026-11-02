# FD-66 — readback blocks a setup discards are freed; the sanitizer flavour finishes clean

Lane `lane-fd66` off integ `1d932714c`. Fix `7d3f4ff31`;
citation re-pins `eaf03b285`; register update and this record in
the third commit. Evidence: `test-results/fd66/`. Every figure
below was read from the preserved log named beside it.

## The defect as filed

Two halves. (1) Verification debt: the native bridge harness's
sanitizer flavour (`VK64_TEST_SANITIZERS=address,undefined`)
could not finish clean — LeakSanitizer reported the readback
blocks the fixture's planning sections left pending, and through
the standard runner the instrumented suite outlasted the flat
30 s run cap and ended as a timeout instead of a verdict.
(2) A latent bridge sharp edge: `setupReadbacks`
(`source/vulkan/vk64bridge.cpp`) opened by clearing both pending
vectors without freeing the heap blocks their entries owned, so
a second setup after an unpaired first one lost the blocks
silently.

## Reachability audit

Every mention of the two pending vectors in the bridge was
censused. The only exits for a pending entry are
`completeReadbacks` (frees every block, then clears) and the
`setupReadbacks` prologue (the bare clears — the defect). All
three production submit paths pair the calls
(`setupReadbacks(frames)`, then `completeReadbacks()` whenever a
plan exists). No other clear, erase or discard site exists. The
discard was therefore reachable with a non-empty plan only from
a caller that plans without completing — today only the native
harness's three planning sections. No production leak is
proven, and none is claimed; the fix removes the latent half by
construction.

## The fix (`7d3f4ff31`)

- `freePendingReadbacks()` is now the only unpaired exit: it
  frees every pending block and clears both vectors, and
  `setupReadbacks` replans through it. `completeReadbacks`'
  behaviour is unchanged.
- The finding inside the fix: frees must not go through
  `heapPtr`. Those fields (and `flagPtr`) are the page-tier
  handles, narrowed to 32 bits by the bridge/page ABI — exact
  in the 32-bit browser build. The first version of this fix
  freed through them and the sanitizer harness died with an
  ASan SEGV inside `free()`, called from the disposal at the
  harness's first planning section: under this host's 64-bit
  allocator a block at `0x50b000000040` narrows to the handle
  `0x40` (measured with a minimal program), so the free was
  wild. Both pending structs now carry `heapRaw`, the
  allocation's true host pointer, set at both allocation
  sites; every free in the readback path — the new disposal
  and `completeReadbacks`' two frees — goes through it. The
  page-tier handles and the wire records are untouched, and
  the reason is written at the struct.
- Harness: the three planning sections dispose through the
  same helper (two previously ended with raw clears of the
  vectors — the same discard, in the fixture; the third
  abandoned its block to the next setup). A new section plans
  one image and one buffer readback, plans again without
  completing (the second plan must dispose of the first), then
  disposes, pinning the pending counts at each step.
- Runner: `tools/vkd3d/tests/run-native.py`'s run cap is
  30 s for the plain flavour and 180 s (the compile steps'
  budget) for the sanitizer flavour, whose suite needs ~35 s
  on a loaded box. The cap still catches hangs; the flavour
  now reports a verdict through the standard runner.

## Proof

- Base tree, sanitizer flavour (lane runner, same recipe as
  `run-native.py`): exit 1; LeakSanitizer **1,132 bytes in 5
  allocations**, every stack in `setupReadbacks` (96 B in 2 at
  the image malloc; 776 B in 2 and 260 B in 1 at the buffer
  malloc) — the filed FD-50 measurement reproduced exactly.
  `run-san-red-unfixed.log`
- Base tree, plain flavour: 56 PASS, exit 0.
  `run-plain-unfixed.log`
- Fixed tree, sanitizer flavour: **57 PASS, exit 0, zero
  LeakSanitizer / ASan / UBSan reports** — via the lane runner
  and via the standard runner
  (`VK64_TEST_SANITIZERS=address,undefined python3
  tools/vkd3d/tests/run-native.py`). `run-san-green-fixed.log`,
  `run-san-official-fixed.log`
- Fixed tree, plain flavour (official runner): 57 PASS
  (56 + the new section), exit 0. `run-plain-official-fixed.log`
- Sabotage: with only the setup prologue reverted to the bare
  clears (helper, `heapRaw` and fixture changes kept), the
  sanitizer flavour fails again with **328 bytes in 2
  allocations** — 260 B buffer + 68 B image, exactly the new
  section's first plan — exit 1. The fixed file was restored
  and verified by sha256 against the pre-sabotage checksum.
  `run-san-sabotage.log`
- The first-fix crash, preserved: ASan SEGV in `free()` from
  `freePendingReadbacks` (vk64bridge.cpp:5765) called from the
  harness's first disposal (vk64_native_test.cpp:899),
  reproduced from the preserved binary.
  `run-san-firstfix-crash.log`
- Citations: the fix's insertions drifted 31 citations in
  `docs/known-gaps/vulkan.md`; re-pinned in `eaf03b285`
  (17 by the audit tool's `--fix`, 14 by hand to the verified
  construct lines). Final audit: 352 rows, 0 DRIFT /
  0 MISSING; a digit-masked diff of the re-pin commit proves
  only citation numbers changed. Row 376's second cite
  (`:12014`) was stale before this lane — at the base it held
  an unrelated case — and was corrected to the verified
  32-bit clear handler line while re-pinning its row.
  `citations.log`, `citations-fixed.log`
- Native serial: main **667/667**, x64 **66/66**, 0 FAILED —
  the integ baselines. `native-full-main.log`,
  `native-full-x64.log`
- JS unit: **348 passed / 0 failed**, the integ baseline
  (includes the citation and readback source-shape guards).
  `js-unit.log`
- Browser build on the fixed tree: BUILD COMPLETE, "Browser
  export verified", wasm `df27c5e0…`, support archive
  `c4dbe35986a5…` byte-identical to the standing pack.
  `dist-build2.log`, `fd66-gate.done`
- Runtime completeness: 0 failures / 1 standing skip.
  `completeness.log`

## Incidents

- The first browser-build attempt waited on the shared build
  lock behind a sibling lane's build and died with the lock's
  FATAL when the lock changed hands at its wait expiry (the
  default wait is 600 s). It was relaunched detached with
  `BW64_BUILD_WAIT_SECONDS=7200` and completed through the
  queue — the same pattern sibling lanes used the same night.
  A duplicate queued build left by the first attempt's dropped
  session was stopped by its exact pid. No other lane's
  process, tree or build was touched.
- Under load, several SSH sessions dropped and several tool
  deliveries arrived empty. The detached builds and the gate
  waiter on the box were unaffected; every figure above was
  read from a preserved log, never from a delivered summary.

## Disposition

FD-66 is CLOSED by CL-38, filed provisionally on this branch
(CL-35, CL-36 and CL-37 stand filed on the `lane-fd54`,
`lane-fd63` and `lane-fd70int` branches; the composing merge
verifies the numbering by audit). No further defects were
found; the narrowed-handle hazard surfaced inside this lane's
own fix path and is discharged by the same change.
