# FD-176: the block-invalidation differential gate links again — and actually executes blocks

Branch `lane-fd176`, base integration tip `faf95d26c`.
Record: `docs/reports/fd176-invalidate-gate-link-20261106.md`.

## The defect

`scripts/build-jit-wasm.sh` failed on the integration tip at the §14.3
block-invalidation tests: the `interp` invalidate-test build did not link,
so the invalidation tests had no working build path at all. Reproduced on
the base tip before any change (`BUILD_TMP=/tmp/bw64-jit-fd176`,
`./scripts/build-jit-wasm.sh --selftest-only` stopped with
`FAIL: interp invalidate-test build`, log `inv_build_interp.log`):
three undefined references — `FsOpenNode::noteMappedPagesReleased`,
`bwCurrentThreadRegistryLookup()`, and `KSystem::guestCpuCount()`.

## The fix (two files)

`scripts/build-jit-wasm.sh` — the invalidate link recipe now links the
kernel core set (FD-82/FD-129 cross-references plus the machine
definition pull the kernel in; the chain closes through inotify, the
condition/scheduler pair, the file node, and the Linux platform layer):

- `source/io/fsopennode.cpp` (noteMappedPagesReleased)
- `source/io/fsnode.cpp` (FsNode::linkGroupMembers)
- `source/kernel/kthread.cpp` (bwCurrentThreadRegistryLookup)
- `source/kernel/ksystem.cpp` (guestCpuCount)
- `source/kernel/kinotify.cpp` (KInotify::notifyClosed)
- `source/kernel/kscheduler.cpp` (removeTimer, scheduleThread)
- `source/util/synchronization.cpp` (BoxedWineCondition)
- `source/util/bfile.cpp` (BWriteFile, from ksystem.cpp's static init)
- `platform/linux/platform.cpp` (Platform time/page methods)

plus the SDL include flags from `sdl2-config` (as the native makefile
uses), falling back to `-I/usr/include/SDL2`. `--gc-sections` keeps the
link to what the tests actually reach; the interp binary is 2,336,200 B,
the blocks binary 2,656,696 B.

`source/emulation/cpu/tests/jit64_invalidate_tests.cpp` — the three
harness stubs that stood in for the unlinked kernel core are removed
(they would now be multiple-definition errors): `KThread::runningThread`,
`KSystem::useF64`, `KSystem::getSystemTimeAsMicroSeconds`. The header
comment's build recipe is updated to the new link, and the driver note
records the run environment below. One correction embedded in this:
the old stub set `useF64 = true`; the product value for the 64-bit guest
is `false` (ksystem.cpp, BOXEDWINE_GUEST_X64 branch — the SoftFloat
80-bit path; `true` silently rounds 80-bit values to double). The tests
now run with the product value. No product code changed.

## The second defect in the same gate: as scripted, the differential was vacuous

Fixing the link was not enough. The script ran both binaries with no
environment, and in that environment the "blocks" binary never executes
a block:

- `jit64Enabled()` is true only with `BW64_JIT=1`, and `tryBlockStep`
  returns immediately otherwise — so with no environment both binaries
  are pure interpreters and their identical output proves nothing.
- With `BW64_JIT=1` alone it is still vacuous: `jit64FastDispatchEnabled()`
  defaults to true when `BW64_JIT_FAST_DISPATCH` is unset, so
  `runBounded` skips the `tryBlockStep` call entirely; and the JIT engine
  defaults to interp, so `tryJitStep` shadows it anyway.
- The BBlock cache is reached only with `BW64_JIT=1 BW64_JIT_ENGINE=off
  BW64_JIT_FAST_DISPATCH=0`.

Proven with a temporary fprintf probe in a scratch build of cpu64.cpp
(probe reverted afterwards; the tree was restored with
`git checkout --` and the status re-verified): with no environment, and
with `BW64_JIT=1` alone, `tryBlockStep` was never called; with the three
variables set it was called 15 times in one run. The script now runs the
blocks binary with exactly that environment (interp explicitly with
`BW64_JIT=0`), and both the script comment and the test-file driver note
record why.

## Sensitivity: the gate is not vacuous (re-verified on the new recipe)

Two scratch-only sabotages (neutered copies of kmemory64.cpp in /tmp,
never committed), blocks binary, correct environment:

- `bw64NoteWriteSlow` neutered (the funnel both `noteGuestWrite` and
  `noteGuestWriteRange` call): T1 fails (`after rewrite eax=5 want 9
  (stale=5)`), T3 fails (`eax=0x0 want 0x12345678 (stale=0)`), T4 fails
  (`after rewrite ebx=7 want 9 (stale=7)`); total pass=17 fail=37.
  This is the sensitivity check the script comment claims, now true.
- `noteGuestWrite` alone neutered: T1 and T4 fail with stale values;
  T3 passes — T3's within-block store evidently invalidates through
  `noteGuestWriteRange` as well. Total pass=18 fail=36 (interp binary)
  and pass=18 fail=36 (blocks binary, correct environment).

## Proof: the full gate, through the real script

Fresh build dir, `./scripts/build-jit-wasm.sh --selftest-only`,
exit code 0. Preserved evidence: `test-results/fd176/` (the full script
log `build-jit-wasm-selftest-only.log` and both invalidate logs):

- jit64 phase-1 unit tests: 873 passed, 0 failed.
- Generated emitters byte-identical (`gen-jit64.mjs --check`, gen tests
  compared=0 skipped=31304 failures=0).
- Block-invalidation: `OK: block-invalidation tests match the independent
  instruction interpreter.` Both binaries print `invalidate_tests:
  pass=54 fail=0` (T1–T4 plus the generation/registry/concurrency
  checks); the two logs are byte-identical.
- Emitted-wasm fixture tests (node --test jit64_wasm_tests.mjs):
  314 pass, 0 fail.

The emcc phases [2/5]–[5/5] were not re-run: this change does not touch
the emcc recipe (the emcc glob prunes `source/emulation/cpu/tests/`,
so the test-file edit cannot affect it); the full script belongs to the
merge gate.

## Filed from this lane

- FD-193 (Low, diagnostic only): `BW64_BLOCKSTATS` prints garbage.
  The `tryBlockStep` atexit reporter keeps a `static CPU64*` to the
  first CPU that allocates a block table, but every test context owns a
  new CPU that is destroyed at the end of its test — by process exit the
  pointer dangles, and the printed counters are nonsense
  (e.g. `built=281474198778416 ... coverage=5120879205109700.0%`).
  It also registers once per CPU, so the line repeats. No effect on the
  gate (the flag is off by default and was only set during diagnosis);
  the BLOCKSTATS numbers must not be used as proof of block execution.
  Found while proving the FD-176 environment fix; not fixed here.

## Scope notes

- No product code changed; only the gate script and its test harness.
- The register entry for FD-176 is closed as CL-134 (next free number
  after CL-133); FD-193 is filed as the next free FD number.
