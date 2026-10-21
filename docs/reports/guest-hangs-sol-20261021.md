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
Build two is recorded in `build2.log`. Window/menu recovery remains to be
verified on that build.

The first diagnostic URL accidentally decoded plus signs in the requested
WINEDEBUG value as spaces; its host-call trace and default Wine fixmes remain
valid. The follow-up probe passes WINEDEBUG directly through URLSearchParams.

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

## Broader verification limit

`native-socket-safety.log` contains three pre-existing fixture compilation
failures: native poller subscription, native scatter receive, and browser TCP
connect. Their simplified C++ socket fixtures lack the loopback members now
used by the extracted production methods. Running against the original
`ac56c0fa5^` source reproduces those failures
(`native-socket-safety-baseline.log`); that run also has a separate native
socket-creation harness failure. The focused new regressions do not depend on
these stale fixtures.
