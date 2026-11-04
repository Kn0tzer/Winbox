# FD-127 — Godot demo parks in device setup: a thread creation's pipe2 never returns

Lane: `lane-godotpark` (characterisation lane, off integ tip `0c7e2f3fa`).
Date: 2026-11-03. Register entry: FD-127 (Section A).
Evidence: the preserved deploy captures — batch-4
`test-results/deploy-batch4-20261103/godot-proof/console.txt` and batch-3
`test-results/deploy-batch3-20261103/godot-proof/console.txt` +
`godot-proof-attempt1/console.txt` (integ worktree). No new capture was
run: the preserved dumps settle the account (see "Why no fresh capture").

## The account in one paragraph

On the current shipped builds the Godot demo's main thread does not wait
on a guest lock, a futex, a condition variable, or the GPU. It waits on
the emulator itself. During vkd3d-proton's D3D12 device setup —
immediately after `vkd3d_init_device_caps` is logged — the main thread
creates a thread. That creation crosses into Wine's unix side, where
`NtCreateThreadEx` calls `wine_server_handle_to_fd`, which calls
`pipe2(fds, O_CLOEXEC)`. The emulator's 64-bit pipe2 handler never
returns for this call: for the remaining 2,460 s of the batch-4 capture
the thread executed **zero** guest instructions, sat at the instruction
after libc's pipe2 `syscall` with the call's arguments still in its
registers, and held no guest wait condition. Everything downstream —
window creation, `vkCreateShaderModule`, queue submission, present —
never happens because device setup never finishes. The two worker
threads created in the same setup window sit in ordinary
condition-variable waits; they are waiting for the main thread, not
blocking it.

## The chain, link by link (each link verified, not inferred)

1. **The thread and the freeze window.** Emulator pid 10 is
   SquashTheCreeps.exe (its main thread's stack bottom is the exe entry;
   the vkd3d guest log lines are tagged pid 10). In the batch-4 capture
   the Godot banner prints at 173.2 s; `vkd3d_init_device_caps` logs at
   210.4 s; the last vk64 trap is at 210.9 s. Thread dumps run every
   60 s: at 215.2 s the main thread (tid 11) is already in its final
   position with `noprogress_ms=0`; at 275.2 s `noprogress_ms=60003`;
   at 2675.2 s `noprogress_ms=2460001`. No vk64 trap occurs after
   210.9 s; the console records 0 `vkCreateShaderModule`, 0
   BW64-FAIL, present count 0 throughout.

2. **What "noprogress" proves.** Per the FD-77 instrument's own
   documentation in `source/kernel/syscall64.cpp`, `noprogress_ms` is
   derived from the CPU core's executed-instruction counter: it
   advances only while the thread's own host thread runs guest ops. A
   "running" thread (no guest wait condition) with a frozen counter is,
   in the instrument's words, "waiting inside a host-side path the
   dump cannot see (… a wait inside a syscall implementation)". So
   tid 11 is not spinning in guest code and not blocked on a guest
   object: it is inside a host-side path of the emulator. The dump's
   two host-side attributions it *can* see are both excluded:
   `proxyfetch inflight=0` and `memmutex_owner_tid=0` in the final
   dump, and tid 11's fault stage is `none`.

3. **The frozen position is a pipe2 call.** tid 11's register snapshot
   is byte-identical in every dump: rip `0x7000f8b37`, rdi
   `0x91f9c0`, rsi `0x80000`. The ELF at `0x700000000` is the guest's
   glibc (identification cross-checked: the read-blocked threads' rip
   `0x7000f82ea` is the instruction after `read`'s syscall; the
   futex-blocked threads' rip `0x700101827` is inside glibc's
   `syscall()` wrapper). Disassembly of the shipped
   `libc.so.6` (from the retained `runtime-assets-20261103` archive)
   at that address: a wrapper loads `eax = 0x125` (293 =
   `SYS_pipe2`), executes `syscall` at `0xf8b35`, and tid 11 sits at
   `0xf8b37`, the return point. rsi `0x80000` is `O_CLOEXEC` — the
   pipe2 flags argument; rdi is the fd-array pointer. The thread is
   parked exactly at the emulator's pipe2 syscall boundary, with the
   syscall's return never delivered.

4. **The PE-side call is NtCreateThreadEx.** The dump's saved syscall
   frame for tid 11: rip `0x17000d5c4` = `ntdll.dll+0xd5c4`,
   rax `0x2d`, in every dump. Disassembly of the shipped `ntdll.dll`
   (same archive) shows the stub at that address is `NtCreateThreadEx`
   (`mov $0x2d,%eax`). The thread's scanned Windows stack corroborates
   the path: d3d12core.dll frames → kernelbase CreateThread cluster →
   `RtlCreateUserThread` → the NtCreateThreadEx stub. The frame never
   changes across 44 dumps: the thread completed no further PE
   transition after entering this call.

5. **The unix-side caller of pipe2 is wine_server_handle_to_fd,
   called from NtCreateThreadEx.** In the shipped `ntdll.so` (the
   unix half of ntdll, same archive), there are exactly three pipe2
   call sites: `__wine_unix_spawnvp`, `NtCreateUserProcess`, and
   `wine_server_handle_to_fd` (+0x113, with `esi = 0x80000` =
   O_CLOEXEC, matching the frozen rsi). Disassembly of the unix
   `NtCreateThreadEx` shows it calls `wine_server_handle_to_fd`
   directly. No process is created at ~215 s (the session's pid set is
   unchanged across the freeze) and no spawn occurs, which excludes
   the other two sites. The pipe2 in `wine_server_handle_to_fd` is
   conditional on state inside ntdll.so (a cached/global check at the
   call site); earlier thread creations in the same session — the two
   workers below, and the session's whole startup — either did not
   take this branch or took it with a working return.

6. **The emulator's pipe2 handler is synchronous and simple.** In the
   shipped source (`syscall64.cpp`, case `X64_SYS_pipe2`): validate
   the fd-array pointer, translate flags (O_CLOEXEC `0x80000`,
   O_NONBLOCK `0x800`), call `ksocketpairFds()` — which takes the
   process's `fdsMutex` (a host `std::recursive_mutex`) and wires two
   `KUnixSocketObject`s together — write the two fds, return 0. There
   is no guest wait anywhere in the handler; for the call to never
   return, the thread must be blocked on (or spinning in) host code
   inside this path or in the syscall dispatch around it. That is
   precisely the FD-77 signature in (2).

7. **The other parked threads are victims, not the cause.**
   pid 10's only other threads, tids 44 and 45, were created in the
   same setup window (maxTid goes 43 → 45 between the 155 s and
   215 s dumps). Both sit in `SleepConditionVariableSRW` →
   `RtlSleepConditionVariableSRW` → `RtlWaitOnAddress` →
   `NtWaitForAlertByThreadId` (stub verified: `ntdll.dll+0xeb84`,
   rax `0xdb`) → Linux futex waits on adjacent addresses
   (`0x…d790e8` / `0x…d790f0`, op WAIT|PRIVATE, expected 0) — the
   ordinary shape of idle worker threads waiting for work. Every
   other process in the session is likewise idle-normal: wineserver
   (pid 14) in `epoll_wait`, services.exe (pid 22) and explorer.exe
   (pid 38) blocked in wineserver socket reads.

## Reproduction: three runs, two shipped builds, byte-identical

The freeze is not a race, load, or timing artefact. All three long
Godot captures on the batch-3 and batch-4 builds end in the identical
state — same thread, same rip `0x7000f8b37`, same registers
(`0x91f9c0, 0x80000, …`), same NtCreateThreadEx frame, same two
futex-idle workers:

- batch-3 attempt 1 (build `caadab5ad`): frozen by its first dumps;
  final recorded `noprogress_ms=1020000` when the capture ended.
- batch-3 attempt 2 (build `caadab5ad`): banner 181.0 s, device caps
  222.7 s, then the same freeze; final `noprogress_ms=899999`.
- batch-4 retry (build `600da7651`): banner 173.2 s, caps 210.4 s,
  freeze by 215.2 s; `noprogress_ms=2460001` at the 2675.2 s dump.

Contrast: on build `9e37e5640` (the 2026-11-02 deploy), the same demo
under the same capture configuration proceeded through device setup —
both specification shader modules created in vivo and pipelines were
attempted over a 3,328 s run (the FD-70/FD-89 deploy evidence). The
behaviour therefore appeared between that build and batch 3's. Which
change introduced it is not established by this evidence and is not
guessed here; the range is stated for the fix lane's bisect.

This also supersedes FD-55's 2026-11-01 characterisation ("startup
inside engine shader compilation, main thread on a futex hand-off")
for current builds: the main thread now gets through Vulkan instance
and device-capability setup and stops later, at a thread creation
inside device setup, before any shader module exists. FD-55's
downstream items (no swapchain, submissions dropped at a bridge view
guard) remain behind this freeze.

## What remains unknown

- **Which host primitive blocks.** `fdsMutex` contention is the only
  named host lock inside the handler, but it is recursive and the
  only other threads of the process are the two condition-variable
  waiters; no preserved evidence shows either holding it. The block
  could equally be another host wait inside socketpair/fd allocation,
  a host-side spin inside the handler, or a wait in the syscall
  dispatch/scheduler layer around the handler. The thread dump has
  no visibility into any of these (by its documented design), and the
  captures preserved no host-side CPU profile of the page that would
  separate "blocked" from "spinning" (a spinning handler pegs one
  host core; a blocked one idles).
- **What state makes this pipe2 different.** Thread creation succeeds
  throughout the session, including twice in the same setup window,
  and the session's process spawns used the same O_CLOEXEC pipe2
  shape. The ntdll.so call site is conditional on internal state;
  what that state is at 215 s, and whether the discriminator is
  fd-table state, socket-object state, or server-handle state, is
  not established.
- Whether the same freeze can bite other guests: any program whose
  Wine thread creation takes the same conversion path in the same
  state is exposed in principle; no second instance is recorded.

## Closing terms (for the fix lane)

1. Instrument the 64-bit pipe/pipe2 handler and `ksocketpairFds` with
   BW64-FAIL-style entry/exit logging naming the calling thread and
   each host-lock acquisition (with the holder's thread id where the
   lock tracks one), plus a host CPU sample of the page during the
   freeze to separate blocked from spinning.
2. Re-run this lane's capture configuration (warmed archive, vktrace,
   the deploy's extended-wait driver shape) against the instrumented
   build; the freeze reproduces deterministically at ~215 s of guest
   time, so the capture can stop at the first thread dump after the
   freeze instead of running the full window.
3. The entry closes when the named primitive's block (or spin) is
   removed and the same capture shows the main thread proceeding past
   device setup — window mapped and the first `vkCreateShaderModule`
   observed — with the native and JS gates green. If the
   instrumentation instead shows the handler returning and the block
   sitting in the dispatch/scheduler layer, the closure evidence is
   the same capture under the corrected layer.

## Why no fresh capture in this lane

The end state asked for the account from the preserved evidence, with
a fresh instrumented capture only if the dumps did not settle it. The
dumps settle every element of the account above — the waiting thread,
the call chain on both sides of the Wine boundary, the exact syscall
and its arguments, the deterministic reproduction across three runs
and two builds — because the FD-77 instrument was built for exactly
this question. The one unsettled point (which host primitive inside
the handler) cannot be settled by an *uninstrumented* re-run, and
instrumenting the handler is the fix lane's first step, specified in
the closing terms; a fourth identical freeze would add nothing.
