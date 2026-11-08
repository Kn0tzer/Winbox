# FD-263 — Direct3D 10 unaligned trap: guest-thread stack overflow into TLS key slots

Filed by the batch-11 merge lane, 2026-11-08. High: can kill a running session.
Status: FIXED. Fix commit: (this lane — see register). Queue verdict: (appended below).

## What the user would see

The Direct3D 10 test — a program that checks the computer's 3D drawing
works — failed on the shipped build with the browser page reporting an
uncaught WebAssembly error, "operation does not support unaligned
accesses", and the session ending. No 3D check reported a wrong result;
the failure struck while the test was starting its next program, after
the first program had finished cleanly. The same test passed on the
previous release, so this was a regression.

## Root cause

A guest thread's emulator stack overflowed its 64 KiB limit during
process teardown and grew down past the guard into the thread's own
thread-local-storage key slots, overwriting them with stack-frame data.
At thread exit the key-destructor walk read the corrupted slot for the
proxying-context list as the value `0x5` — a small integer, not a
pointer — and tried to lock the "mutex" at that bogus node's offset
+16. The lock is an atomic compare-exchange, which the processor
requires to be 4-byte aligned; the bogus address was not, so the
browser raised the unaligned-access trap and the session died.

Evidence chain (all read from preserved logs, not summaries):

- The trap site is wasm offset `0x2172b` in function 728, musl's
  `__pthread_mutex_lock` fast path: an atomic compare-exchange at
  pointer+4. Caller 4601 is Emscripten's `cancel_ctx` (locks the
  mutex at node+16, sets the flag at +8, signals the condition at
  +40). Caller 11145 is `cancel_active_ctxs`, which walks the
  thread's proxying-context list (next pointer at +88) rooted at the
  pointer in the thread's TLS slot for the `active_ctxs` key. Caller
  11141 is reached through the worker message loop into
  `_emscripten_thread_exit` and the key-destructor walk.
  Identification: a symbols build plus normalized-body function
  matching against the shipped wasm (live[728] = `__pthread_mutex_lock`,
  live[4601] = `cancel_ctx`, live[11145] = `cancel_active_ctxs`),
  confirmed by disassembling the exact shipped build.
- Temporary `emscripten_log` instrumentation in Emscripten's
  `proxying.c` reproduced the trap with logging: the walk ran exactly
  once, on the dying thread, with list head `0x5` (kind read 0, next
  read 0 — both from readable low memory), then the trap. The run
  carried 100,714 proxy calls, all synchronous calls targeting the
  main thread; the dying thread's own list was never populated
  (ADD = 0), so the `0x5` was not a leaked proxying context.
- Temporary instrumentation in musl's `pthread_setspecific` (logging
  only values below `0x10000`) fired ZERO times in a full run: no
  API call ever wrote the small value. Temporary logging in
  `__pthread_key_create` showed exactly three keys (0, 1, 2); the
  proxying key is key 0 (created first, on the main thread, at the
  first proxy call). Temporary logging in `__pthread_tsd_run_dtors`
  showed the dying thread's slots 0, 1 and 2 ALL holding `0x5`,
  and a 16-slot read showed a spread of small integers and a float
  constant (`0x3f800000` = 1.0f) across slots 0–15 — the signature of
  stack-frame data, not a single deliberate write.
- The thread block layout (Emscripten `pthread_create.c`) is:
  pthread struct, TLS data, key slots, guard area, stack. The stack
  is 64 KiB (the Emscripten default: `DEFAULT_PTHREAD_STACK_SIZE`
  was unset, so it fell back to the 64 KiB `STACK_SIZE`) with an
  ~8 KiB guard. WebAssembly has no guard-page fault, so the overflow
  is silent until the destructor walk. The overflowing path consumed
  at least the full ~74 KiB of guard plus stack (it reached slot 0,
  the bottom of the key array). The trap is 3/3 deterministic across
  builds and threads, fitting a deterministic deep teardown path.
- Ruled out: stale TLS from thread creation (the block is zeroed at
  creation); a leaked or freed proxying context (the dying thread's
  list was never populated); a `pthread_setspecific` mixup (zero
  small-value writes observed); a torn list (the list is only mutated
  on its own thread). The FD-204/205 fault-containment fix does not
  apply here: this is a single uncaught throw, not repeated fault
  redelivery.

## Fix

One build-flag change, no product code:

- `project/emscripten/makefile`, `wasm64-mt` target:
  `-sDEFAULT_PTHREAD_STACK_SIZE=524288` (512 KiB) added to
  `EXTRA_LD_FLAGS`, with a comment recording the mechanism.
  512 KiB gives headroom over the ~74 KiB the overflowing path
  consumed; Windows threads get 1 MiB. The setting lives in the
  generated JavaScript glue, not the wasm: the fixed build's wasm is
  byte-identical to the shipped batch-11 wasm
  (`e44fe64e4334fe0a08cbb2e61388b539394cbf48a34235e27a26a6585e0a0f1b`,
  5,736,471 bytes); only the JavaScript changes
  (js `1d0e8916ef630758452bc7dbf87a11368fa6db6cf544f24517eec861d9b7bc2a`).
  The 64-bit JIT target (`wasm64-mt-jit`) still carries the 64 KiB
  default; it is not shipped and needs its own proof if it is ever
  enabled.
- The thread-block size grows from 1,150,745 to 1,609,497 bytes
  (measured in the instrumented fix run), i.e. 32 pool workers cost
  ~14 MiB more wasm memory at most — negligible against the 4 GiB
  maximum.

## Proof

- Instrumented fix build (temporary logging still live):
  `d3d10-comprehensive` PASS in 333,107 ms; thread blocks measured
  at 1,609,497 bytes; zero unaligned traps; zero destructor walks
  on a corrupted head.
- Clean fix build (all toolchain instrumentation reverted, shared
  Emscripten cache verified byte-identical, private cache deleted):
  "Browser export verified" exactly once; BUILD_INFO git_sha
  `3e296c28f`, wasm `e44fe64e…` (shipped-identical, 5,736,471 B),
  js `1d0e8916…`; `d3d10-comprehensive` PASS in 196,279 ms,
  SUITE RESULT 1 passed / 0 failed, 0 unaligned traps, 0 page
  errors; the guest's own checks report `tri10: RESULT 0`.
- Unfixed control runs for the same test on instrumented builds:
  FAIL in 233,010 / 252,805 / 233,218 ms, each with the identical
  unaligned trap after `d3d10dev: RESULT 0`.
- Logs: `test-results/fd263/d3d10-fixrun6.log` (instrumented fix),
  `test-results/fd263/d3d10-cleanproof7.log` (clean fix),
  `test-results/fd263/fix-build8.log`,
  `test-results/fd263/clean-build9.log`,
  `test-results/suite-d3d10-comprehensive/console.log` (clean proof run).

## Toolchain hygiene

The investigation temporarily patched three Emscripten system-library
sources (`system/lib/pthread/proxying.c`,
`system/lib/libc/musl/src/thread/pthread_key_create.c`,
`system/lib/libc/musl/src/thread/pthread_setspecific.c`) and built
against a private cache (`/tmp/fd263/emcc-cache`) so the shared
toolchain was never written through. All three files were restored
from preserved originals and verified byte-identical by sha256; the
shared cache was re-hashed file by file and verified identical (the
one apparent gap was a re-hash artifact: a filename containing spaces
that `xargs` split; the file was re-checked and matches). The shared
`libc-mt.a` hash never changed (`06d9bea4…`). The private cache was
deleted. Two Emscripten behaviours were pinned down during the work
and are recorded for future lanes: the cache variable is `EM_CACHE`,
not `EMCC_CACHE` (silently ignored), and `emsdk_env.sh` clears
`EM_CACHE` when sourced, so it must be exported after sourcing;
also, `emcc` does not rebuild a system library when its source
changes — the archive must be deleted (or `--force` used) — and
`fprintf` from inside Emscripten proxy functions recurses through
`_fd_write`'s own proxying; `emscripten_log(EM_LOG_CONSOLE, …)`
is the safe channel there.

## Queue verdict

Checking-service pipeline #64 on the fix tip `5a34d6f8a`
(commit verified exact by verify-source; tree pristine):
citations exit 0 (352 rows: 232 MATCH, 14 MATCH-INHERITED,
99 UNCHECKABLE, 7 EXEMPT); js-unit 352 passed / 2 failed, the
failures exactly the known bare-checkout artifacts
(`provenance-artifacts`, `runtime-completeness`); js-legacy exit 0
(0 fail); native-main 705 tests, 0 FAILED; native-x64 120 tests,
0 FAILED; run-native exit 0. The pipeline status reads "failure"
solely on the js-unit exit — the standing acceptance shape,
identical to the batch-11 gate (pipeline 60). Evidence:
`checks/evidence/pipeline-64/queue-logs/` (step logs 541–549).
One infrastructure note, not a check failure: the publish-results
step could not upload to MinIO (`XMinioStorageFull`, HTTP 507 —
the storage backend hit its free-drive threshold, the box disk
being full); the step logs were still harvested locally and the
verdict was read from them firsthand.

## Open follow-ups

- The exact deep call path in the guest-process teardown that
  consumes ~74 KiB of emulator stack was not isolated (the fix
  removes the overflow by headroom, it does not shorten the path).
  If a future path grows past 512 KiB the same trap returns; the
  tell-tale is a destructor-walk head that is a small integer.
- The `wasm64-mt-jit` target still uses the 64 KiB default.
