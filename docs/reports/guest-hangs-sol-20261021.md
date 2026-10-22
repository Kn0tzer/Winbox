# Guest hangs investigation, 2026-10-21

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-solhang-20261021`.
Branch: `lane/winbox-solhang-20261021`.
Evidence root: `/tmp/winbox-finish/r3/work-solhang/`.

## Mindustry

The previous round6 capture repeatedly reports Wine's `sock_errno_to_status
unknown errno 5`. AF_INET6 creation failures are separate and expected.
The Windows JVM runs through Wine; Linux socket errors can originate either in
wineserver or Wine's Unix ntdll socket implementation.

### Root cause of the captured EIO loop

The instrumented launch reproduced the error chain: Emscripten host
`sendto(-1, ..., 36, ...)` and `recvfrom(-1, ..., 4096, ...)` return host
EBADF (`-8` in Emscripten), immediately followed by Wine's guest errno 5.
See `mindustry1/live.log`, starting at entries 435-449, and its later repeated
recvfrom failures. `translateNativeSocketError` does not map EBADF and returns
`-K_EIO` for it.

Browser AF_INET UDP sockets deliberately have no host descriptor. Their
sendto/recvfrom methods use guest-local packet queues, but sendmsg/recvmsg
still called host socket functions. Wine's Unix socket implementation uses
message I/O, exposing this gap. This is a missing transport dispatch, not an
AF_INET6 failure or an EINPROGRESS guess.

Commit `da96fd81a` routes UDP message sends through `sendLoopbackBytes` and
shares packet dequeue logic between recvfrom and recvmsg. A receive consumes
one datagram across all iovecs, retains the packet for MSG_PEEK, writes its
source address, and marks truncation. The empty nonblocking queue returns
EAGAIN without invoking a host socket.

`udp-msg-red.log` fails on the original message path. `udp-msg-green.log` and
`socket-tests2.log` pass on the fix (10 focused tests). The native translation
unit compiles to `knativesocket-udp.o`; `native-udp-compile.log` has no errors.
Build two is recorded in `build2.log`. The build-three run confirms that EIO is gone but exposes a second receive deadlock, described below.

The first diagnostic URL accidentally decoded plus signs in the requested
WINEDEBUG value as spaces; its host-call trace and default Wine fixmes remain
valid. The follow-up probe passes WINEDEBUG directly through URLSearchParams.

### Java loopback pipe deadlock

The build-three launch (`mindustry3/summary.json`, WASM prefix `bf7a3bb9aa6b`)
ran for 480 seconds without mapping. It has no `unknown errno` or invalid-host-fd
messages. Its Winsock trace shows Java connecting socket `0x268` to
`127.0.0.1:32768`, successfully sending the pipe secret, and accepting socket
`0x270`. The final call is a receive on `0x270` that never returns
(`mindustry3/live.log`, entries 904-966).

`recvmsg` took `readingCond->m` before invoking `recvLoopbackStream`, which takes
the same nonrecursive `std::mutex`. The secret can already be queued and the
receive still deadlocks. The UDP message fix also needs to avoid this outer
lock because its shared packet dequeue helper owns the condition mutex.

Commit `05562a8f6` keeps the outer condition lock only for host-native receives.
The regression now uses real nonrecursive condition mutexes and the actual
TCP/UDP dequeue helpers. Before the fix it times out; afterward it receives
the 16-byte pipe secret and preserves the UDP packet checks
(`recvmsg-lock-red.log`, `recvmsg-lock-green.log`, `socket-tests4.log`).

Java 25's PipeImpl next sets TCP_NODELAY on the accepted pipe socket. That
operation was rejected solely because the local stream has no host descriptor.
Commit `8ea4c5a4d` retains the option in guest state; local writes already
transfer immediately. A native set/get regression fails before the fix and
passes after it (`tcp-options-red.log`, `tcp-options-green.log`).

Commit `463f464cc` also preserves x64 message addresses and receive flags,
and bounds MSG_TRUNC scatter copies to the bytes buffered in scratch.
`socket64-red.log` and `socket64-green.log` demonstrate the adapter fix.

All 12 focused tests pass in `socket-tests-final.log`. Both changed translation
units were compiled natively before WASM builds. The fourth/final allowed
build is `build4.log`; its window/menu result is recorded below when available.

### Verified transport defect

`KNativeSocketObject::ioctl` forwarded FIONREAD and FIONBIO to host ioctl even
for guest-local TCP/UDP sockets. Accepted guest-local TCP sockets have
`nativeSocket == -1`, so this path fails instead of inspecting the guest queue
or changing guest blocking state. The generic native error translator turns
unhandled host errors into guest EIO.

Commit `ac56c0fa5` answers FIONREAD from queued TCP bytes or the first UDP
packet, and handles FIONBIO using the guest blocking flag. The regression
harness compiles and executes the actual ioctl branches with an absent host
socket. It checks empty and populated TCP queues, non-consuming queries,
blocking toggles, UDP packet length, and the native fallback.

This separate ioctl defect does not explain the observed x64 EIO loop. In particular,
the x64 syscall dispatcher handles FIONBIO separately and currently returns a
constant zero for FIONREAD; it does not invoke this legacy ioctl method.

Evidence:

- `ioctl-red.log`: original implementation fails the first empty-queue query.
- `ioctl-green.log`: patched implementation passes.
- `native-compile.log`: changed C++ translation unit passes native compilation.
- `socket-tests.log`: all 9 tests pass across loopback ioctl, VFS socket
  isolation, and browser socket wakeup test files.
- `build1.log`: first requested WASM build.

A temporary copy of the GL game probe (`mindustry-probe.mjs` in the evidence
root) records host Emscripten socket calls before errno translation. It uses the same prepared IPv4/heap
limited archive as round6, GL 3.2, and this worktree's runtime.

## Gang Beasts

The earlier diagnosis is at `/tmp/winbox-finish/r3/work-gb/diagnosis.md`.
Its run4 proves device creation at D3D11 feature level 11_0 but no swapchain
creation or presentation.

A closer reading identifies the initial game-process thread: guest pid 10,
tid 11. From 124 through 304 seconds it is blocked in `read(fd=9, ..., 16)`
waiting on `KUnixSocketObject::lockCond`, with both socket queues empty
(`run4/console.txt`, entries 3560-3561 and 4594-4595). The final pselect6 waits
belong to later threads. An empty Wine server reply channel can be a legitimate
NT wait; these snapshots do not reveal the awaited Windows object or prove
that the guest kernel lost a wakeup.

No Gang Beasts fix is claimed from these snapshots. DispatcherQueue/WMI and
empty-directory warnings remain unproven causes.

### Corrected Wine server trace

`BW_WINEDEBUG` originally changed only the routed fetch URL; the iframe still
read `winedbg=-all` from its actual location. Commit `dc990484b` fixes the
checked-in demo driver to apply this setting to the iframe src.

The verified `+server,+sync,+timestamp` run is `gangbeasts2/` on build three.
Wine main thread `0024` waits indefinitely on auto-reset event `021c`
(`live.log` entries 83102-83107). Thread `0088` signals that same event at
162.5 seconds (83144-83146), and the main thread resumes. It later requests
`terminate_process(exit_code=1)` at 180.3 seconds (93219-93221), followed by
UnityCrashHandler startup. Thus this capture proves the event wait completes;
it does not establish a permanently lost kernel wakeup. It still has no mapped
window or Vulkan presentation.

A follow-up (`gangbeasts3/`) retains Wine errors, exception/module traces,
and redirects Unity's player log to stdout with `-logFile -`. This is needed
because the existing player-log capture is empty.

## Broader verification limit

`native-socket-safety.log` contains three pre-existing fixture compilation
failures: native poller subscription, native scatter receive, and browser TCP
connect. Their simplified C++ socket fixtures lack the loopback members now
used by the extracted production methods. Running against the original
`ac56c0fa5^` source reproduces those failures
(`native-socket-safety-baseline.log`); that run also has a separate native
socket-creation harness failure. The focused new regressions do not depend on
these stale fixtures.
