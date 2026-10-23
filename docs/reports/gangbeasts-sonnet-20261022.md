# Gang Beasts never reaches a window: Boehm GC stop-the-world hang (Sonnet, 2026-10-22)

Branch `lane/winbox-gb2-20261022` (from main e00a6c105). Fix commit `2122062f1`.

**Status: root cause found and fixed in the guest kernel. The fix is verified natively
(x64 build, `--x64-selftest` = 907 passed, 0 failed) but NOT validated under wasm or in the
browser, because the 3-build budget was used up before the fix existed.** Treat "Gang Beasts now
reaches a window" as unproven until the validation steps below are run.

## Symptom

Unity 2021.3.33f1 IL2CPP (D3D11 -> DXVK -> Vulkan -> WebGPU). The log reaches
`Initialize engine` -> `GfxDevice: creating device client; threaded=1; jobified=1` -> DXVK
`vkCreateDevice` -> `Direct3D: Version 11.0 [level 11.0]` -> ~10x `vkAllocateMemory`/buffer ->
mmdevapi/audio init, and then nothing. No swapchain, no mapped window, in JIT 0 or JIT 1.

Two earlier theories were ruled out:
- Graphics: the vk64 trace shows no further traps after device init.
- Audio: with winealsa/winepulse disabled (`/tmp/gb2/noaudio`) the stall is identical.

## What the main thread is blocked on

Evidence: `/tmp/gb2/c1/console.txt` (build 3, `BW_THREAD_DUMP_INTERVAL_S`; 9 identical samples
from 244 s to 404 s).

pid 10 (wine64 / GangBeasts.exe), main thread tid 11 (Wine thread 0024):

```
state=blocked syscall=read(#0) fd=9 wait=KUnixSocketObject::lockCond      (Wine server reply_fd)
syscallframe rip=ntdll.dll+0xd8e4 rax=0x46 rdx=0x10eaf0 rsp=0x10ea98       (Wine syscall frame via TEB+0x328)
winstack: ... ntdll.dll+0x5d4a9 / +0x29d88 ... kernelbase.dll+0x3f7d1 ...
          GameAssembly.dll+0x2f08c5, 0x305ec7, 0x300dc5, 0x29de9a, 0x29a069, 0x2afa65 ... (GC_stop_world path)
```

rax=0x46 is `NtGetContextThread`. The caller chain is IL2CPP's Boehm GC `GC_stop_world()`,
which for each registered thread does `SuspendThread` then `GetThreadContext(flags 0x100003)`.
~30 other threads are idle in server waits, futex waits or polls. Nothing else is making progress,
so the main thread waits forever.

## Root cause

Wine implements SuspendThread/GetThreadContext on another thread by having wineserver
`tgkill(client_pid, client_tid, SIGUSR1)`. The target's `usr1_handler` then reports its context to
the server, which releases the requester's wait.

Boxedwine's 64-bit `tgkill`/`tkill` (`source/kernel/syscall64.cpp`) only set the target's
`pendingSignals`. Signals are then picked up only at: every 64k interpreted instructions, scheduler
slices, and the futex wait loop. A target thread parked in a Wine-server socket `read`
(`KUnixSocketObject::lockCond`), a `poll`/`pselect`, or an untimed futex never looked at
`pendingSignals`, and nothing woke it. SIGUSR1 was never delivered, the context was never
reported, and `NtGetContextThread` never returned. This is a missing "signal interrupts a blocked
syscall" semantic in the guest kernel, not a Wine configuration or Unity-specific problem. Any
program whose runtime suspends threads (Boehm GC, debuggers, .NET/Mono) can hit it.

## Fix

- `CPU64::interruptSyscallWithSignal(allowRestart)` (`source/kernel/syscall64.cpp`, `include/cpu64.h`):
  delivers a pending, unmasked, handled signal from inside a blocked syscall. Linux semantics:
  if the handler has `SA_RESTART` (Wine installs usr1 with `SA_RESTART|SA_SIGINFO|SA_ONSTACK`) and
  restart is allowed, rip is rewound to the SYSCALL instruction (`cpu->syscallRip`) so the call
  re-executes after `rt_sigreturn`; otherwise RAX = `-EINTR`. The poll/select/sleep/futex family
  always passes `allowRestart=false`. If delivery fails, rip/RAX are restored.
- `tgkill`/`tkill` to another thread now also `SIGNAL_ALL`s the target's `waitingCond` so it wakes.
- `KThread::interruptBlockedSyscall` (`kthread.cpp/.h`) wraps this for the wait loops.
- Wait loops now check for signals and wake on a 200 ms slice for 64-bit threads
  (`BOXEDWINE_CONDITION_WAIT_SIGNALABLE`): `KUnixSocketObject::readNative/read/recvmsg`
  (`kunixsocket.cpp`), `internal_poll_impl` (`kpoll.cpp`), futex wait (`kthread.cpp`).
  The 200 ms slice is a backstop; the explicit condition signal is the fast path.

Behaviour changes to be aware of (watch for regressions):
- The futex path now returns `-EINTR` with a correct signal frame; previously the saved RAX in the
  frame was the syscall number.
- 64-bit threads in long waits now wake every 200 ms (tiny extra CPU).
- Restart relies on `syscallRip` being the SYSCALL instruction address.

## Diagnostics added (kept, wasm-only `#ifdef __EMSCRIPTEN__`)

`source/kernel/syscall64.cpp` `bwDumpGuestStack`: module list from PEB->Ldr; per-thread
`BW_THREAD_DUMP rip|syscallframe|syscallmem|winstack|stack` lines; the Wine syscall frame is read
from TEB+0x328 (a thread inside a Wine syscall runs on the TEB kernel stack, so the Windows stack
comes from `frame->rsp`); call-validated PE stack scan resolving `module+RVA`. `demo-matrix.mjs`
gained `BW_THREAD_DUMP_INTERVAL_S`, `BW_WINEDEBUG`, `BW_TEST_URL_PARAMS`, `BW_CAPTURE_PLAYER_LOG`.

## Evidence

- `/tmp/gb2/c1/` - key run (build 3): console.txt (thread table + syscallframe + stack @ 404 s)
- `/tmp/gb2/long1`, `/tmp/gb2/b1`, `/tmp/gb2/noaudio` - earlier runs (1500 s old build, 600 s, audio disabled)
- `/tmp/gb2/tools/{dis.py,pesym.py,ntsys.txt}`, `/tmp/gb2/ga`, `/tmp/gb2/wz` - Capstone disassembly, PE
  symbolisation, extracted GameAssembly.dll / ntdll.dll
- Prior: `/tmp/winbox-finish/r3/work-gb/diagnosis.md`, `/tmp/winbox-finish/r3/work-solhang/gangbeasts{2,3}/`

(`/tmp` is not durable; copy what you need.)

## Not validated / how to validate

Native: `make -C project/linux x64` and `boxedwine --x64-selftest` -> 907 passed, 0 failed. These do
not exercise the blocked-syscall signal path with a real Wine.

Needs one more wasm build (budget exhausted; requires approval):

```
BW64_BUILD_TARGET=wasm64-mt-jit JOBS=2 BINARYEN_CORES=2 BW64_BUILD_OWNER=<name> BW64_BUILD_WAIT_SECONDS=10800 bash tools/bw64-build.sh
DEMO=gangbeasts JIT=0 OUT=/tmp/gb2/v1 MAPPED_TIMEOUT_S=900 node web/tests/demo-matrix.mjs   # then JIT=1
# regression: DEMO=notepad|celeste|snake, JIT=0 and 1
```

Success = the main thread leaves NtGetContextThread (no identical `syscallframe` repeats) and the
log proceeds past audio init to a swapchain/window. Even then, Gang Beasts may hit further
independent blockers after the GC stop-the-world; this fix removes the first one found.

## After the map: what Gang Beasts does next (2026-10-23)

With the signal-interrupt fix in the integration build the game maps and Unity runs and presents. The
remaining blockers were all in the Vulkan/WebGPU path, not the emulator; pure JS (`web/*.mjs`,
`tools/spirvfix`) except the notes marked C++. All fixes have unit tests (`--unit`: 161 passed).

| Symptom | Fix | Where |
|---|---|---|
| "Failed to create GPU event" - Unity aborts device init | shim now exports `vkCreate/Destroy/Set/Reset/GetEventStatus`, `vkCmdSet/Reset/WaitEvents` (fn ids 841-848) | `tools/rootfs64/libvk64/libvk64.c`, rebuilt `libvulkan.so.1` |
| DXVK probes 300+ unknown instance/device proc names | 512 named stub slots for unknown `vkGet*ProcAddr` names | libvk64.c |
| naga rejects the DXVK MSAA-resolve pipeline: integer varyings without `Flat`; `BuiltIn Layer` in a fragment shader | spirvfix R10 `flat-integer-varyings`, R11 `fragment-layer-to-zero` | `tools/spirvfix/spirvfix.mjs`, `spirvfix-r10-r11.test.mjs` |
| Dawn rejects every frame: pipeline vs pass attachment state; scissor outside render target; Float texture bound where Depth expected | reset `st.pipeRec` per pass; `clampScissor`; `depth32float` placeholder for `texture_depth_*` | `web/vkwebgpu.mjs`, `vkwebgpu-pass-state-carryover.test.mjs` |
| ~30 pipelines: `textureSample must only be called from uniform control flow` | fragment modules start with `diagnostic(off, derivative_uniformity);` | `vkwebgpu.mjs`, `vkwebgpu-derivative-uniformity.test.mjs` |
| 5 pipelines: VS output vec4 vs FS input vec3/vec2 at the same location | `matchFragmentInputsToVertexOutputs` widens FS input and swizzles the private copy | `web/wgsl-utils.mjs`, `wgsl-varying-match.test.mjs` |
| 7 pipelines: `texture_multisampled_2d_array` unresolved | `rewriteMultisampledTextures`: binding becomes the resolved `texture_2d_array`, per-sample loads read level 0 | `web/wgsl-utils.mjs`, `wgsl-multisampled-texture.test.mjs` |

### Known remaining gaps

- 1 pipeline: shader declares `texture_2d` but the descriptor set binds a cube view (layout Cube vs shader 2D). Rare, not handled.
- Typed buffer SRV/UAV (`texel buffer`) bindings are not supported by the page tier.
- C++ bridge (`source/vulkan/vk64bridge.cpp`) has no cases for `vkCmdClearDepthStencilImage`,
  `vkCmdWriteTimestamp`, `vkCmdSetDepthBounds`; `vkCmdSetFrontFace`/`vkCmdSetCullMode`
  (~1900 calls each) and `vkCmdSetEvent2` (~394) hit the extended-dynamic-state path. Command buffers
  with no `CMD_BEGIN_RP` log "submit(v2): frame N has no framebuffer, skipped".
- The shim change needs the rebuilt `libvulkan.so.1` packaged into the production rootfs.
- The integration `dist/` lacks `shader-cache.mjs`; `game-probe.mjs` exits at ~17 s with a 404 against it
  (probes here symlink it in from `web/`). The JS above must also reach the integration dist
  (`vkwebgpu.mjs`, `wgsl-utils.mjs`, root-level `spirvfix.mjs`).
- Probe helper: `PROBE_IGNORE_TERM=1` (another agent's cleanup can SIGTERM a long probe).

### The extra signal checks

Not added. The traces show Unity running, presenting and creating Vulkan resources; no thread sits in
`connect`/`accept`/`KThread::sleep`/`epoll_wait` waiting for a signal. They remain the same latent
hang class (below) and should be added only if a trace shows one.

## Residual / next

- Other blocking sites not covered: `connect`/`accept`, `KThread::sleep`, `epoll_wait`, and any
  condition wait not using `WAIT_SIGNALABLE`. Same hang class if a runtime signals a thread there.
- The target thread of the stuck SuspendThread was not named; a short `WINEDEBUG=+server` trace
  (`BW_WINEDEBUG`) in the validation run would confirm the SIGUSR1 round trip now completes.
- The winealsa timer thread (tid 75/77) burns CPU while idle; unrelated to the hang.
