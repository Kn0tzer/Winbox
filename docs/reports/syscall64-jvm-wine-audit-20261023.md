# x86-64 JVM and Wine syscall audit (2026-10-23)

Audited `source/kernel/syscall64.cpp` at integration base `d5adf2f43` against the requested Java 25 / HotSpot and Wine Unix syscall set. This was a source-only audit. No build, game probe, or runtime syscall trace was run.

## Coverage

| Call | x64 path | Limits and likely impact |
| --- | --- | --- |
| `mincore` | Implemented in the preceding change; unit-covered. | Reports mapped pages with committed backing. |
| `madvise` | Returns success without acting on the advice. | `MADV_DONTNEED`/`MADV_FREE` and other stateful advice do not get Linux memory semantics; no `ENOSYS` retry, but memory retention can differ. |
| `membarrier` | Missing; falls through to `-ENOSYS`. | Not called directly in the reviewed OpenJDK 25 HotSpot Linux port. Native libraries that probe it must handle its absence. |
| `rseq` | Explicitly returns `-ENOSYS`. | Intentional glibc fallback to non-rseq paths. Not expected to block ordinary HotSpot startup. |
| `sched_getaffinity` | Implemented; reports one CPU. | Correct for the one-CPU guest model. |
| `sched_setaffinity` | Implemented as a successful no-op after checking mask size. | Does not reject an empty mask or change scheduling. |
| `sched_yield` | Implemented with host-thread yield (and cooperative yield in single-thread mode). | Appropriate to this emulator’s thread model. |
| `getrandom` | Implemented with deterministic pseudo-random output, capped at 256 bytes per call. | Startup callers can continue, but output is not cryptographically secure; do not use for guest secrets. |
| `prlimit64` | Implemented as “unlimited”; ignores new limits. | JVM heap sizing sees no address-space/resource limit. Callers setting limits get no enforcement. |
| `sysinfo` | Implemented with 1 GiB total and 768 MiB free RAM. | Supplies a nonzero JVM memory estimate. |
| `nanosleep` / `clock_nanosleep` | Previously returned immediately; now validates x86-64 timespecs and waits through `KThread::nanoSleep`. Relative and `TIMER_ABSTIME` sleeps support realtime, monotonic, boottime, and TAI clock IDs. | This is the most directly JVM-relevant behavior fix in this audit. It does not implement remaining-time output on interruption. |
| `timerfd_create`, `timerfd_settime`, `timerfd_gettime` | Missing; fall through to `-ENOSYS`. | Existing 32-bit timerfd code is not a drop-in x64 path: it uses 32-bit timespec fields and currently ignores clock selection. Wine components or libraries that use timerfd can fail their timer/event-loop setup. |
| `eventfd2` | Now routed to the existing shared `KEvent` implementation; accepts `EFD_CLOEXEC` and `EFD_NONBLOCK`, rejects other flags. | Provides readable/writable eventfd behavior through the existing file-descriptor and poll paths. |
| `pipe2` | Implemented through the shared pipe path with supported close-on-exec/nonblocking flags. | Uses the process’s existing pipe and poll integration. |
| `sync_file_range` | Now handled as success, matching the 32-bit path. | VFS writes are synchronous in this environment; flags, range, and fd are not validated. |
| `fadvise64` | Returns success without acting on the advice. | Safe for an advisory hint; no page-cache advice is modeled. |
| `get_mempolicy`, `mbind` | Missing; both return `-ENOSYS`. | Primarily affects NUMA-aware allocation or `-XX:+UseNUMA`; ordinary one-node JVM startup can fall back. |
| `sigaltstack` | Implemented. | Stores per-thread alternate-stack state used by signal handling. |
| `tgkill`, `tkill` | Implemented. | Routes signals through the guest thread/process signal machinery. |
| `set_tid_address` | Implemented. | Records the clear-child-tid word and wakes its futex on thread exit. |
| `futex` WAIT/WAKE, WAIT_BITSET/WAKE_BITSET | Implemented for real guest threads; there is a counter-based fallback for threadless CPU tests. | These are the ordinary pthread synchronization operations used by HotSpot/glibc. |
| `futex` REQUEUE, CMP_REQUEUE, WAKE_OP | Return success without moving waiters or applying the wake operation. | This is a semantic stub, not an `ENOSYS` result; process-shared or requeue-dependent synchronization can stall. |
| `futex` PI operations | Missing; return `-ENOSYS`. | Only affects priority-inheritance futex users; standard HotSpot monitor paths do not request PI mutexes by default. |
| `process_vm_readv`, `process_vm_writev` | Missing; return `-ENOSYS`. | Cross-process inspection/debugger paths; not an ordinary JVM execution dependency. |
| `getcpu` | Now reports virtual CPU 0 and NUMA node 0; either output pointer may be null. | Correct for the single-CPU/single-node guest model. |

The syscall-name mapper now labels the remaining unimplemented entries so a future guest trace reports names instead of `?`.

## Changes and verification

Added relative/absolute nanosleep handling, single-CPU `getcpu`, eventfd2 routing, and synchronous-VFS `sync_file_range` handling. Added focused C++-snippet tests for nanosleep and getcpu plus source routing assertions for eventfd2 and sync_file_range. The prior `mincore64` test also passed.

Focused test command:

```sh
node --test web/tests/suite/unit/linux-runtime-syscalls64.test.mjs web/tests/suite/unit/mincore64.test.mjs
```

Result: 4 tests passed. No C++/WASM build was run. The implementation still needs build validation before it is included in a runtime.

OpenJDK source reviewed: [OpenJDK 25u HotSpot Linux OS implementation](https://github.com/openjdk/jdk25u/blob/master/src/hotspot/os/linux/os_linux.cpp). The syscall contract follows the Linux x86-64 ABI and existing 32-bit BoxedWine syscall implementations where they are applicable.
