# FD-03 machine-definition raise — lane record

**Date:** 2026-11-05
**Branch:** `lane-machine-definition` on base `4006ddb58` (batch-7 integ tip)
**Register:** FD-03 annotated in place (entry stays OPEN); Section B row added.
**Evidence:** `test-results/machine/` in this tree.

## What FD-03 asked

The emulated machine was far smaller than modern programs expect, in
four independent floors: (a) no OpenCL ICD and no CUDA; (b) the CPU
advertises at most SSE4.2, no AVX/AVX2; (c) the machine definition was
1 GiB RAM and 1 CPU; (d) the Vulkan bridge advertised a 2 GiB
device-local heap. This lane's end state: raise every answer that can
be delivered honestly, prove each raised answer with a purpose-built
test program that checks the answer and then exercises it, keep the
standard suites green at their standing sizes, and record plainly —
with the measurement — any capability that cannot be delivered in a
browser.

## Disposition per floor

- **(c) RAM and CPUs — RAISED.** 1 GiB → 4 GiB, 1 CPU → 2 vCPUs, each
  from its single existing source (`KSystem::guestRamInfo`,
  `KSystem::guestCpuCount`). Every guest-visible face was made to
  agree (details below).
- **(d) Vulkan device-local heap — RAISED.** 2 GiB → 4 GiB on the
  64-bit bridge, matching guest RAM and the 32-bit bridge's existing
  advertisement. The heap is guest RAM by construction, so the figure
  is deliverable. The single-allocation cap
  (`maxMemoryAllocationSize`) stays 2 GiB on purpose: one allocation
  is backed by one WebGPU buffer, and that is the honest unit.
- **(b) AVX/AVX2 — NOT RAISED, recorded plainly.** The emulator has no
  YMM register state, no XSAVE machinery, and no VEX decoding in
  either engine, so an AVX advertisement would be a claim the first
  AVX instruction disproves. CPUID leaf 7 stays all-zero and the AVX
  bit stays clear; the guest fixture pins the bit clear so the
  negative cannot silently flip. Delivering AVX is a CPU-engine
  project (YMM state + XSAVE + VEX decode across engines), not a
  machine-definition change.
- **(a) OpenCL/CUDA — NOT RAISED, recorded plainly.** No OpenCL ICD
  or CUDA runtime exists in a browser for a guest to call; there is
  nothing to exercise, so nothing was advertised. This floor falls
  only to a compute-over-the-bridge project, if ever.

## Changes

Tests first (`a241fdc00`, `c314fd8eb`, `85880ca2c`, `5cc01cc9d`):

- `source/test/process/testGuestCpuTopology.cpp` — pins count 2, list
  and online file "0-1", /proc/stat cpu0+cpu1 with no cpu2, meminfo
  total 4 GiB.
- `source/test/process/testSchedAffinity32.cpp` — getaffinity mask 3;
  mask 2 (vCPU 1 alone) accepted; mask 4 (no online CPU) −EINVAL.
- `source/test/process/testSysinfo64.cpp` — the x64 sysinfo baseline
  follows the machine to 4 GiB (the rest of the test is
  size-agnostic consistency).
- `tools/vkd3d/tests/vk64_native_test.cpp` — new 64-bit memory
  properties block pinning heap[0] at 4 GiB device-local (the 32-bit
  block already pinned the single-allocation cap at 2 GiB).
- `tools/testdata/machine_definition_check.c` (+ static ELF, wired
  into `tools/run_x64_smoke.sh`) — the purpose-built guest fixture:
  checks sysinfo totalram == 4 GiB, then **exercises** it (a 2 GiB
  mmap — twice the old advertised total — committed and verified
  page by page); affinity/sysconf/CPUID all report 2; affinity is
  pinned to vCPU 1 alone, read back as exactly vCPU 1, and getcpu
  reports 1 while pinned; SSE4.2 present; AVX bit clear.
- `web/tests/suite/unit/linux-runtime-syscalls64.test.mjs` — the
  extracted-source harnesses' CPU64 stubs gain the affinity mask,
  and the getcpu test pins mask 2 → CPU 1, mask 1 → CPU 0.

Fix (`90a2e8ea3`, `938c684d4`):

- `source/kernel/ksystem.cpp` — `guestCpuCount()` returns 2;
  `guestRamInfo` total 4 GiB; both comments rewritten to state the
  basis. `include/ksystem.h` topology comment updated.
- `include/cpu64.h` — default `affinityMask` 0x3.
- `source/kernel/syscall64.cpp` — `sys_sched_setaffinity64` accepts
  any mask intersecting the online set and stores requested &
  online (was: bit 0 required, mask forced to 1);
  `sys_getcpu64` reports the lowest set bit of the thread's
  affinity mask (was: hardcoded 0).
- `source/kernel/syscall.cpp` — 32-bit getaffinity writes the online
  mask; setaffinity validates against it.
- `source/emulation/cpu/cpu64.cpp` — CPUID leaf 1 EBX
  logical-processor field = `guestCpuCount()`.
- `source/kernel/proc/proccpuinfo.cpp` — one package: physical id 0,
  siblings and cpu cores = count, per-vCPU core ids.
- `source/kernel/proc/procstat.cpp` — one synthetic per-CPU line per
  vCPU (see residuals).
- `source/vulkan/vk64bridge.cpp` — `fillMemoryProperties` heap[0]
  4 GiB, with the single-allocation reasoning in a comment.

Citations (`6160ca64c`): the heap comment's 4 added lines pushed 9
Vulkan-register citations past the audit tolerance; 3 re-pinned by
the audit's `--fix`, 6 by hand to the constructs' verified lines.
Audit back to 352 rows, 0 drift.

## Proof

RED on base `4006ddb58` (logs `*-base.log`, `fixture-base.log`,
`fixture-getcpu-red.log`):

- Main suite: exactly 2 FAILED — the topology and 32-bit affinity
  tests; nothing else.
- run-native: abort at the new assertion
  `memory.readq(32768 + 264) == 0x100000000` (heap[0] is 2 GiB).
- Guest fixture: 5 FAILs (totalram, affinity count, sysconf,
  setaffinity-to-vCPU-1, CPUID logical count); SSE4.2/AVX negatives
  pass; the 2 GiB commit exercise already passes (guest memory is
  demand-paged — the advertisement was the only false part).
- The getcpu fixture check, added later, fails alone (exit 16)
  against the pre-fix binary.

GREEN on the final branch (logs `*-fix3.log`, `run-native-fix.log`,
`citation-audit-fix.log`, `js-unit-fix.log`, `js-legacy-fix.log`):

- Native main suite: **686 tests, 0 FAILED** (standing size).
- Native x64 suite: **111 tests, 0 FAILED** (standing size). First
  run caught `testSysinfo64Consistency` still pinning 1 GiB — that
  baseline is part of this change and was updated (`c314fd8eb`).
- run-native: **70 PASS** (69 standing + the new heap block).
- Guest fixture: **ALL PASS**, guest exit 0; x64 smoke **27/27**
  including the new `machineDefinition` entry.
- Browser build: `Browser export verified`, wasm `ece9a0f0`,
  dependency archive pin `c4dbe35986a5` unchanged.
- JS unit **353/0**; JS legacy **115 pass / 0 fail / 1 skipped**
  (116). The first umbrella run caught the getcpu harness break
  (352/1); fixed in `5cc01cc9d` and re-run clean.
- Citation audit: **352 rows, 0 drift**.

## Residuals, stated plainly

- /proc/stat per-vCPU counters mirror the aggregate line: per-vCPU
  accounting does not exist. The code comment says so; only the
  line *presence* (what topology readers consume) is claimed.
- getcpu reports the first vCPU in the thread's affinity mask.
  vCPUs are an affinity abstraction, not pinned execution slots, so
  no truer "current CPU" exists in this design; the answer is
  exactly right for a pinned thread and a valid online CPU
  otherwise. The 32-bit getcpu slot remains unimplemented — an
  absence, not a wrong answer.
- The `--x64-run-elf` runner does not serve /proc and does not
  implement clone, so the fixture SKIPs /proc/cpuinfo there (the
  native topology test covers it) and cannot rendezvous two guest
  threads in that path. The basis for the 2-vCPU claim is
  architectural and stated, not hidden: in the multi-threaded
  build, guest threads are host threads (browser workers), so two
  guest threads genuinely run in parallel; every topology face
  agrees on the count.
- Raising advertised RAM changes JVM heap ergonomics (the default
  max-heap fraction now resolves against 4 GiB). Guest memory is
  demand-paged, so this raises a cap, not a reservation; no JVM
  workload was re-measured in this lane — flagged for the next
  Mindustry re-baseline (D22).
- The 32-test browser guest suite was not run in this lane
  (single probe lane; sibling release work active). The browser
  *build* is verified; the merge gate owns the browser suite.

## Build-inputs note for composition

A fresh tree needs, before `tools/bw64-build.sh`: the root
`node_modules` symlink, dxvk `build.w32`/`build.w64` and
`tools/rootfs64/gstreamer32|64` payloads (donor `wt-fd38`), vkd3d
`build.w64`/`build.w32` (donors `wt-restage`/`wt-x86vkd3d`), the
untracked `tools/browser/slang/` directory (donor: integ tree),
`node tools/browser/stage-vendor.mjs`, and
`python3 tools/browser/bootstrap.py --local
/home/ubuntu/backups/winbox-runtime-assets-20261104/browser-runtime-20261104.tar`
(the release download URL 404s by standing decision; bootstrap from
the retained archive only). All of it was staged into this tree for
the build and stripped again afterwards; the branch carries none of
it.
