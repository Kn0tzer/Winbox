# Guest hangs investigation, 2026-10-21

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-solhang-20261021`.
Branch: `lane/winbox-solhang-20261021`.
Evidence root: `/tmp/winbox-finish/r3/work-solhang/`.

## Mindustry

The previous round6 capture repeatedly reports Wine's `sock_errno_to_status
unknown errno 5`. AF_INET6 creation failures are separate and expected.
The Windows JVM runs through Wine; Linux socket errors can originate either in
wineserver or Wine's Unix ntdll socket implementation.

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

This defect is **not yet established as Mindustry's root cause**. In particular,
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
root) records host Emscripten socket calls before errno translation, together
with Wine `+winsock,+timestamp` output. It uses the same prepared IPv4/heap
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
