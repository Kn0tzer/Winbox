# FD-151 — the Node wasm64-selftest target compiles again; its two standing failing cases were stale staging

Lane `lane-fd151` (worktree `wt-fd151`), based on the integration tip
`7a2308cd6`. Commits: `7d422cb8f` (compile fix), `20129b456` (fixture
restaging), `fae50a27e` (run-instruction correction), plus the register
and record commit that carries this file. Evidence:
`test-results/fd151/` in this worktree.

## The filing

FD-151 (filed by the FD-145 restage lane) recorded that the Node-side
build target `make wasm64-selftest` in `project/emscripten` fails to
compile — "member reference base type 'BOXEDWINE_MUTEX' (aka 'void *')
is not a structure or union" in `faultstage.h` at the staged mutex's
lock/unlock calls — A/B-proven pre-existing on the lineage base, while
the browser dist builds compile the same units clean. Its closing
terms: the target compiles and its run is reported, or the target is
retired. The parent's framing added the self-test's two standing
failing cases (visible in the browser-suite run of the same fixture):
the discharge has to cover the build break and both cases, fixing the
product or the staging as the evidence convicts.

Reproduction on this branch's base was exact (`build-base.log`): the
same error text at the same two calls, instantiated from the two
soft-page translation units.

## Defect 1 — code written against the multi-threaded configuration, built single-threaded

The self-test target builds **without** `BOXEDWINE_MULTI_THREADED`.
In that configuration `source/util/synchronization.h` typedefs
`BOXEDWINE_MUTEX` to `void*` and defines the `BOXEDWINE_MUTEX_LOCK` /
`BOXEDWINE_MUTEX_UNLOCK` macros as empty — there is no mutex object to
call methods on, by design. Two call sites written against the
multi-threaded configuration touched the type directly:

1. **`include/faultstage.h`** — `MemoryDemandLock`'s constructor and
   destructor called `mem->mutex.lock()` / `.unlock()` (landed with
   FD-77's fix, `9d32b5ca9`; when the break began). This is the filed
   site.
2. **`source/kernel/ksocket.cpp`** — `ksocketpairFds` acquired the
   process descriptor-table lock with
   `std::unique_lock<std::recursive_mutex>` + try_lock/lock, with
   BW_THREAD_DUMP-gated diagnostic notes (FD-127-era code). This site
   surfaced only on the rebuild after the first fix (`build-fixed.log`,
   MAKE_EXIT=2); under the single-threaded typedef the lock object's
   type does not exist. The lock object is used only inside the
   acquisition bracket; the descriptor work below it is unguarded in
   both configurations' intent (the bracket's purpose is the
   multi-threaded hand-off).

Class audit: every other direct member-lock site in the tree is
either `#ifdef BOXEDWINE_MULTI_THREADED`-guarded (kmemory64.cpp,
log.cpp, kprocess.cpp — the last is the idiom the ksocket fix
follows), locks a genuine `std::recursive_mutex` global (cpu64.cpp's
atomic lock bank, syscall64.cpp's teardown mutex), or is gated out of
this target entirely (JIT and OpenGL bridge code). The native Linux
makefile always defines `BOXEDWINE_MULTI_THREADED`, which is why the
native suites never saw the break.

Fix (`7d422cb8f`): faultstage.h's two calls go through the
configuration's own macros — they expand to exactly those calls under
multi-threaded, to nothing single-threaded. ksocket.cpp's whole
acquisition bracket is wrapped in `#ifdef BOXEDWINE_MULTI_THREADED`
with an explanatory comment. Multi-threaded behaviour is
byte-identical at both sites; single-threaded builds lose only
operations that were no-ops by configuration.

## Defect 2 — two self-test cases staged against superseded answers

First successful run of the repaired target (`run-standard.log`):
**951 passed, 2 failed**. Both failures convicted as stale test
staging in `source/emulation/cpu/cpu64SelfTest.cpp`; the product is
correct in both. Host behaviour was measured first with raw syscalls
(`hostref-fd151.c` / `.out`; aarch64 asm-generic numbers — a guest
issues raw syscalls, so the wrapper's userspace rejections are not
evidence):

1. **chmod, null path.** The case expected −ENOENT — the FD-125
   settlement's staging at the layer answer before FD-126/FD-146
   landed. The product now answers **−EFAULT**: the 64-bit
   dispatcher's chmod case guards a null path pointer ahead of the
   string read (`if (!a1) ret = -EFAULT`). Host, raw `fchmodat`
   (53): (AT_FDCWD, NULL, 0) → −EFAULT; (AT_FDCWD, "", 0) → −ENOENT.
   The product matches the kernel; the fixture was never updated by
   FD-146's fix. Case renamed "chmod → -EFAULT (null path)",
   expectation −EFAULT, comment rewritten to record the disposition.
2. **pidfd_getfd (438).** One fixture case looped {334 rseq, 425
   io_uring_setup, 426 io_uring_enter, 427 io_uring_register, 438}
   under a shared "returns ENOSYS" expectation, on the premise that
   all five are deliberately rejected centrally. The first four pass;
   438 answers **−EBADF** because pidfd_getfd is fully implemented
   (dispatcher case present; FD-23's host-measured order: flags →
   −EINVAL first, then descriptor checks → −EBADF). Host, raw
   pidfd_getfd (438): a pipe descriptor → −EBADF; a closed descriptor
   → −EBADF; invalid flags → −EINVAL (flags first, matching). Case
   split: 438 leaves the ENOSYS loop (renamed "unsupported
   rseq/io_uring syscall returns ENOSYS", premise comment corrected)
   and stands alone as "pidfd_getfd → -EBADF (descriptor not a
   pidfd)". Case count preserved.

Restaging is `20129b456`, fixture only.

## Runs — full marks on both builds

The Emscripten shell is CommonJS while the repository package.json is
`type: module`, so runs use a `.cjs` copy of the shell next to its
wasm (the treatment `scripts/build-jit-wasm.sh` already gives the JIT
variant), with the env-forwarding preRun header that script uses.
`project/emscripten/buildInstructions.md` documented the `.js`
invocation, which cannot run on this tree; `fae50a27e` corrects the
instructions to the `.cjs` run and drops the stale pinned case count.

- Standard build (`make wasm64-selftest`, MAKE_EXIT=0):
  **953 passed, 0 failed**, exit 0 (`run-standard-fixed.log`).
- Speed-engine build (`scripts/build-jit-wasm.sh`'s exact recipe in
  an isolated build dir — BLOCK_EXEC/BLOCK_CACHE_INFRA defines,
  `-sALLOW_TABLE_GROWTH=1`; the script itself targets the MT product
  build, so its recipe was applied to the self-test target by hand):
  compiles (MAKE_EXIT=0); interpreted run (BW64_JIT=0) **953/0**;
  JIT run (BW64_JIT=1, BW64_JIT_VERIFY=1, BW64_JIT_HOT=0) **953/0**
  with genuine emitted execution — `JIT64: engine=wasm blocks=319
  insns=816`, `WASM64: blocks=319 insns=816 entered=319 rejected=0`,
  mode "table". The 319/816 figures match the FD-145 restage lane's
  JIT proof exactly.
- Output equivalence: the standard log is byte-identical to the
  JIT-build interpreted log; the JIT-mode run is identical at case
  level (953 PASS / 0 FAIL in all three). The only difference is
  three added `bw64: METRICS {...}` telemetry lines in the JIT-mode
  run: the metrics emitter fires on wall-clock windows, and the
  emitted run's instantiation time crosses more windows.
  Characterised as telemetry cadence — not a defect, not filed.

## Gates

- Native suites, serial (1 worker): main **0 tests FAILED** (679
  "... OK" lines, 382 s, exit 0); x64 **0 tests FAILED** (105 OK
  lines, 28 s, exit 0). (`suite-main.log`, `suite-x64.log`.) The two
  `BW64-FAIL` zip lines in the main log are a negative test's
  deliberate failure-path logging; its test passes.
- JS unit suite: **352 passed, 0 failed** (`suite-js-unit.log`).
- Browser build: `tools/bw64-build.sh` — "Browser export verified:
  34 .mjs syntax files; … dependency archive c4dbe35986a5 verified",
  BUILD COMPLETE, exit 0. BUILD_INFO: git_sha `fae50a27e`, dirty 0,
  wasm sha256 `2fd905f6…` (5,682,948 bytes).
- Browser suite, filtered: **cpu64-selftest PASS** against that dist
  (`suite-browser-selftest.log`) — the browser guest self-test runs
  the same restaged fixture.
- Worktree setup followed the standing fresh-worktree recipe:
  node_modules symlink to the main tree; runtime assets restored from
  the retained `runtime-assets-20261103` archive (13 files, pins
  verified); generated payload inputs copied from the preserved trees
  (dxvk build.w32/w64 and rootfs64 gstreamer/mono payloads from
  `wt-fd38`; vkd3d build.w64 from `wt-restage`, build.w32 from
  `wt-x86vkd3d`).

## Filed separately — FD-155

The MEMORY64 variant of the target, `make wasm64-selftest-mem64`,
does not compile, for an unrelated reason: the SDL audio bridge's
EM_ASM call at `platform/sdl/kdspaudio.cpp:371` casts the ring-buffer
pointers to `int` (`(int)&bwRingWrite`, `(int)&bwRingRead`), which a
64-bit-pointer build rejects ("cast from pointer to smaller type 'int' loses information" — the build's only error site,
`build-mem64.log`). Filed as FD-155 rather than fixed here: different
subsystem, and the FD-151 lane's diff does not touch the file or its
inputs (byte-identical on the integration tip — pre-existing by
construction). The wasm32-host targets accept the casts, which is
why the shipped builds never saw it.

## Notes

- The box was heavily contended throughout (several sibling lanes
  building and probing; load averages above 10 on 4 CPUs). All waits
  were passive — build lock and probe slot did the serialising — and
  no result here depends on timing.
- No product divergence was found by this lane: every conviction
  landed on the build configuration or the test staging, each backed
  by a host measurement or a dispatcher read.
