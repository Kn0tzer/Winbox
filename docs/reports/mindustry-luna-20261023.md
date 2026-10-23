# Mindustry WebGPU GL investigation, 2026-10-23

## Scope and source state

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-solhang-20261021`

Branch: `lane/winbox-solhang-20261021`

I fast-forwarded this worktree to `integ/winbox-20261022b` (`3d0ecbc24`). This brings the integration build's browser JavaScript into the worktree while leaving the integration worktree and WASM untouched. The GLSL precision/conditional and interface-location fixes from `c5bad39fe` and `1e9f3c653` are present in `web/gl-webgpu.mjs` and their Naga-backed regression fixtures are in `web/tests/suite/unit/gl-shader-compat.test.mjs`.

The supplied integration runtime exists at `/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022/web/runtime`. For the probe, `WINE_DIST_DIR` must point to its parent `/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022/web`, because the script appends `runtime/`. It is not a runtime build made from this branch, and no WASM build was run.

The probe previously omitted the `jit` launch parameter. `tools/bench/gl-game-probe.mjs` now accepts `JIT=0|1`, defaults to `0`, validates the value, records it in the summary, and forwards it to the runtime URL.

## Preserved probe evidence

The prior capture in `/tmp/winbox-finish/r3/work-luna-hang/mindustry-shader-dump/verify3` reached the first window after 402.1 seconds. Its summary reports `webgpuActive: true`, no WebGPU recovery, and no page errors. The GL bridge logged `host GL up (WebGPU)`. The filtered console contains no shader link/preparation error. It does contain Emscripten's warning that immediate-mode emulation is limited.

The capture `/tmp/winbox-finish/r3/work-luna-hang/mindustry-shader-dump/verify3/mapped-plus-60s.png` is black. Its sample has the normal guest and GL canvases, and the console shows subsequent GL activity, so successful WebGPU initialization alone does not produce the menu. The earlier shader-dump runs and their initial `UnexpectedNewLine`, duplicate-location, and `ElseOutsideOfBlock` failures are documented in `guest-hangs-luna-20261022.md`; the precision/location regression tests now pass.

Focused verification on the merged source:

```text
node --test web/tests/suite/unit/gl-shader-compat.test.mjs
11 passed, 0 failed
```

## Restored-runtime reruns

The game was restored at `/tmp/winbox-finish/r2/work-glwgpu/mindustry/Mindustry/Mindustry.exe`. I assembled a temporary probe root under `/tmp/winbox-finish/r4/dist-stage`: top-level web modules are symlinks to this worktree's `web/`; runtime binaries are symlinks to the supplied integration `web/runtime`; the required page entrypoint and support/dependency files come from the integration distribution. The staged WASM SHA-256 matches `BUILD_INFO.json`: `6c2fbf7e32eab884e4b2dea362a1c7aa98ced64f7c0cd94447e3d3b5993cf5ed`. No WASM build was run and the integration worktree was not edited.

Both requested runs completed with the same game zip and runtime build:

| JIT | First window | WebGPU GL active | Outcome |
| --- | ---: | --- | --- |
| 0 | no, 480 s timeout | no | OpenJDK exits during initialization: `Too small maximum heap` |
| 1 | no, 480 s timeout | no | OpenJDK exits during initialization: `Too small maximum heap` |

Evidence:

- JIT=0: `/tmp/winbox-finish/r4/mindustry-jit0/summary.json`, `console.log`, `timeout.png`
- JIT=1: `/tmp/winbox-finish/r4/mindustry-jit1/summary.json`, `console.log`, `timeout.png`
- Both logs show the Java VM startup error before any GL context, shader compilation, or `host GL up (WebGPU)` message. There is no shader failure or WebGL2 recovery in these runs.

The likely blocker is the x86-64 guest memory report. `source/kernel/syscall64.cpp`, `X64_SYS_sysinfo` (case 99), returns success after zero-filling all 112 bytes, so `totalram`, `freeram`, and `mem_unit` are zero. In contrast, the 32-bit `KSystem::sysinfo` reports 1 GiB total and 768 MiB free, and `/proc/meminfo` reports 1 GiB total. OpenJDK now aborts with “Too small maximum heap” on the x64 path, consistent with a zero physical-memory report. The current logs do not trace the syscall itself, so this is a strong root-cause candidate rather than direct syscall-level proof.

This C++ fix needs a WASM rebuild, which was explicitly excluded. I did not modify it. The required follow-up is to populate the x86-64 `sysinfo` fields with a nonzero, consistent memory total/free value and `mem_unit`, add focused coverage, rebuild the WASM runtime, then rerun both modes. Until that is done, the GL menu failure cannot be evaluated on the supplied runtime.

## Build 5 reruns (2026-10-23)

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-mindustry-build5-20261023`

Branch: `lane/winbox-mindustry-build5-20261023`, based on integration HEAD `336572461556d787b9ad997d36f88ccfa0430a41`.

The supplied build-5 `web/runtime` and `dist/runtime` agree on WASM SHA-256 `3a6bd71f8778c0e1e266844363d8078ce5333fc1f4d6de9a761ad1ec4bb2a5d0`, and both BUILD_INFO files identify HEAD `336572461`. No WASM build was run and the integration worktree was not modified. The earlier zero-memory sysinfo startup blocker is resolved: Mindustry starts Java 25.0.1 and reaches its OpenGL initialization logs.

### Probe staging correction

The first build-5 JIT=0 and JIT=1 runs mapped the window but initialized WebGL2. Those runs were not valid WebGPU checks: `winbox-wt-prep.sh` had left an old `web/runtime/wine64-launcher.js` in the new worktree, and the initial temporary stage preferred that copy. It does not set `BW64_GL_BACKEND`. Build 5's fresh `dist/runtime/wine64-launcher.js` does set the WebGPU default. I corrected only the temporary stage under `/tmp/winbox-finish/r5/dist-stage` to use that build artifact; the stage still serves this branch's current `web/` modules, including `gl-webgpu.mjs`.

### Corrected WebGPU results

| JIT | First window | WebGPU active | Captures | Result |
| --- | ---: | --- | --- | --- |
| 0 | 319.7 s | yes | +60 s | Black; no page errors, bridge recovery, shader link failure, or WebGPU failure |
| 1 | 371.4 s | yes | +60, +120, +240 s | All black; no page errors, bridge recovery, shader link failure, or WebGPU failure |
| 0, `GL_TRACE=400` | 252.4 s | yes | +60 s | Black; sample reports zero GL calls and zero presents |

The JIT=1 console reaches `[I] [GL] Version: OpenGL 3.2.0`, `[I] [GL] Using OpenGL 3 API`, `[I] [JAVA] Version: 25.0.1`, and `[I] [RAM] Available: 259.5 MB`. Its +120 and +240 second samples continue reporting CPU64 instructions and syscalls, but zero GL calls and zero presents. No shader is submitted to the WebGPU translator in these captures. The guest-reported GL version string includes `WebGL2`, but the host bridge independently confirms `host GL up (WebGPU)` and the probe summary remains `webgpuActive: true`; there was no fallback.

The bounded `GL_TRACE=400` run emitted no `gl64: call fnId` trace. `gl64bridge.cpp` documents that `getenv` can be unavailable on guest worker threads in this pthread build, so the silence is not evidence that no calls happened before the sampled interval. The bridge stage counters provide the interval-specific evidence: zero GL calls/presents at the JIT=0 trace capture and at JIT=1 +120/+240.

### Current blocker and evidence

The precision macro, conditional, and interface-location fixes from `c5bad39fe` and `1e9f3c653` are present in this branch. The build-5 black frame is not currently attributable to GLSL-to-WGSL translation: after Java reports OpenGL initialization, the captured later samples contain no GL traffic, and the WebGPU failure list is empty. The next blocker is a guest-side stall after Java/GL initialization and before the first rendered frame. The available probe output does not identify the guest thread or call site responsible. Resolving that will require a guest-thread/CPU diagnostic (likely C++ instrumentation and a rebuilt WASM), which was outside the no-rebuild runtime verification requested here. No C++ change or renderer workaround was made without evidence.

Evidence:

- Initial mis-staged JIT=0/JIT=1 runs: `/tmp/winbox-finish/r5/mindustry-jit0/` and `/tmp/winbox-finish/r5/mindustry-jit1/` (WebGL2; not used to assess the WebGPU path).
- Corrected JIT=0: `/tmp/winbox-finish/r5/mindustry-jit0-corrected/summary.json`, `console.log`, `mapped-plus-60s.png`.
- Corrected JIT=1: `/tmp/winbox-finish/r5/mindustry-jit1-corrected/summary.json`, `console.log`, and `mapped-plus-{60,120,240}s.png`.
- Bounded GL trace: `/tmp/winbox-finish/r5/mindustry-jit0-gltrace/summary.json`, `console.log`, `mapped-plus-60s.png`.

## Build 5 guest-thread diagnosis (2026-10-23)

I reused the guest diagnostics from `thr_5annabscvc` against the corrected build-5 runtime without rebuilding WASM. The probe patched the served `wine64-launcher.js` to enable thread snapshots, crash ring/socket-read diagnostics, `BW64_TRACE_EXE=Mindustry.exe`, memory samples, and RIP heartbeats. It also invoked `bw64_dump_guest_threads()` at +60, +120, and +180 seconds after the first mapped window. The launcher patch succeeded (`diagnosticLauncherPatched: true`), runtime build ID was `3a6bd71f8778`, and the probe reached the window after 470.9 seconds. All three screenshots remain black; WebGPU was active and `webgpuFailures` and `pageErrors` are empty.

### Thread inventory and wait sites

The first snapshot contains 24 guest threads across PIDs 10, 14, 22, and 38. PID 10 is Mindustry's main Wine process (the filtered syscall trace matches `Mindustry.exe`). Its 18 observed threads were TID 11, TIDs 44–55, and TIDs 57–61. The repeated snapshots show the same inventory and state classes:

- PID 10/TID 11, the application main thread, is **running in every snapshot**, not blocked in a syscall. Its RIP moves through `jvm.dll` (for example `+0x79a0e9` at +120 seconds and `+0x466090` at +180 seconds). The first native stack includes JVM frames such as `jvm.dll+0x7a02af`, `+0x9a2b84`, `+0x290bd0`, and ends at `Mindustry.exe+0xd216`, `+0xc550`, `+0xbb9c`. Across all three snapshots there are no `libGL`, LWJGL, or GL bridge frames on this thread.
- PID 10/TIDs 44–46 and 50–54 are blocked in `futex(#202)`, with sampled libc RIPs around `libc.so.6+0xdb829` / `+0xd22ec` and wait addresses in `0x70970c0e8..0x70970c138`.
- PID 10/TIDs 47–49, 55, 58, and 59 are blocked in `read(#0)` with `wait=KUnixSocketObject::lockCond` (fds include `0x1e`, `0x22`, `0x26`, `0x3e`, `0x4c`, `0x51`).
- PID 10/TID 60 was blocked on `futex(#202)` at `0x7d1ada00`; TID 61 and the adjacent stack frames are in `winealsa.drv` (`+0x3a83` and `+0x1842`), identifying this as the audio worker path rather than rendering.
- PID 14/TID 15 is blocked in `epoll_wait(#232)` with `wait=KThread::pollCond`.
- PIDs 22 (TIDs 23–24) and 38 (TIDs 39–41) are blocked in `read(#0)` / `KUnixSocketObject::lockCond`.

RIP heartbeat offsets for the same inventory (nearest samples around the three snapshots) are:

| Threads | Sampled RIPs |
| --- | --- |
| PID 10/TID 11 | `jvm.dll+0x805012`, `zip.dll+0x3d03`, `jvm.dll+0x28a6c6`; snapshot stack tails include `Mindustry.exe+0xd216/+0xc550/+0xbb9c` |
| PID 10/TIDs 44–46, 50–51, 54 | `libc.so.6+0xdb829` |
| PID 10/TIDs 47–49, 55, 58–59 | `libc.so.6+0xd22ec` |
| PID 10/TID 52 | `libc.so.6+0xdb829` or `+0xa944f` |
| PID 10/TID 53 | `libc.so.6+0xa944f` or `jvm.dll+0x695dc0` |
| PID 10/TID 57 | `libc.so.6+0xdb829`, `+0x69dd4`, or `+0xe48ba` |
| PID 10/TID 60 | `libc.so.6+0x6012b` or `arc64.dll+0x265bd/+0x5f700` |
| PID 10/TID 61 | `libc.so.6+0xd22ec`, `+0x60193`, `+0xd238f`, or `winealsa.drv+0x1842` |
| PID 14/TID 15 | `0x700108ef3` (sampler reports unmapped), blocked in `epoll_wait` |
| PID 22/TIDs 23–24; PID 38/TIDs 39–41 | `libc.so.6+0xd22ec` |

The trace also records `pselect6` with `nfds=0`, repeated futex operations, and a read on fd `0x54`; none changes the main-thread state reported by the snapshots. The guest ELF RIPs are mapped to `libc.so.6` by the RIP sampler; the host libc installed for this diagnostic is not the guest binary, so I did not infer libc function names from host symbols.

### Diagnosis

There is no concrete blocked wait site on Mindustry's application main thread in this capture: it remains runnable in JVM code. The observable futex, socket-read, epoll, and ALSA waits belong to helper threads. The snapshots therefore rule out the suspected main-thread X11 event wait, GLX/EGL call, and an unnotified futex as the immediate cause. They also show that the main thread has not entered a native GL/LWJGL call at the sampled points. The concrete blocker is earlier in the guest Java/JVM execution path, between GL initialization and the first GL submission; these artifacts do not resolve the active Java frame to a Java method, so they cannot identify a missing extension query or prove which Java operation is looping. No renderer or C++ fix is justified by this evidence.

Evidence:

- Diagnostic run summary and all 212k console lines: `/tmp/winbox-finish/r6/mindustry-diagnostics/summary.json` and `console.log`.
- Black captures: `/tmp/winbox-finish/r6/mindustry-diagnostics/mapped-plus-{60,120,180}s.png`.
- Probe run: JIT=0, build ID `3a6bd71f8778`, WebGPU active, map at 470.9 s; no WASM build.

## Long JIT run and WebGPU shader / syscall blockers (2026-10-23)

The user asked to distinguish a slow Java startup from a stuck guest and requested a long JIT=1 window with periodic captures. On the supplied build-5 runtime, Mindustry's first mapped screenshot is still the BoxedWine startup splash, not the game menu. JIT=1 took 429.2 seconds to reach that window. It logged Mindustry content and GL/Java initialization (`[I] [GL] Version`, `[JAVA] Version 25.0.1`, `[RAM] Available: 259.5 MB`), but no later Mindustry progress lines appeared during the 25-minute observation. The screenshots stayed black.

CPU64's retired-instruction counter initially increased at about 5.7 million instructions/sec, slowed to about 1 million/sec, and then stopped. At +1500 seconds it was 4.199 billion instructions; this by itself could have indicated very slow JVM work. The exact failure log from that run instead identifies a translator failure at about +332 seconds: the vertex shader declares an `out mat4 v_model`, which Naga rejects as an invalid vertex entry point. The shader was submitted on WebGPU; the recovery layer then attempted a WebGL2 query on the failed program and emitted an error. The run ended without a menu.

I added generic lowering for matrix-valued GLSL stage interfaces: each matrix varying becomes one vector varying per column, and the matrix is reconstructed or split around `main`. The unit regression compiles the vertex/fragment pair through the bundled Naga path. This fix is not tied to Mindustry shader names.

With that fix, a WebGPU-only JIT=1 rerun (`PROBE_RECOVER=0`) reported `webgpuActive: true`, no WebGPU failures, and no page errors. It still showed no menu. The guest instruction counter reached 3.686 billion by +360 seconds and remained exactly there through +900 seconds. The console then showed an unimplemented x86-64 syscall #27 (`mincore`) followed by JVM `EXCEPTION_PRIV_INSTRUCTION` and `exit_group(3)`. SoLoud's audio-thread mutex assertion occurred during shutdown and is secondary. This proves the later state is a process crash rather than a long-running JVM wait. The current source now implements generic x64 `mincore`, returns residency from committed guest pages, and validates the queried pages and output vector before writing. A focused C++-snippet unit test covers mapped/resident pages and invalid arguments/ranges. This C++ change has not been built, as requested.

Mindustry's `Mindustry.json` does not set `-Xint`, `-Xshare`, `TieredStopAtLevel`, `-Xmx`, or `UseSerialGC`. JVM flag experiments were not useful after the runs exposed a GLSL translation failure and then a deterministic unimplemented-syscall crash. We do not yet have a measured time-to-menu or a usable hot-JVM profile; the current evidence says the app cannot get there on build 5 until the C++ syscall change is built and retested.

Evidence:

- JIT=1 25-minute run: `/tmp/winbox-finish/r7/mindustry-jit1-25min/summary.json`, `console.log`, and `mapped-plus-*.png`.
- WebGPU-only run after matrix varying lowering: `/tmp/winbox-finish/r8/mindustry-jit1-matrixfix/summary.json`, `console.log`, and `mapped-plus-{420,600,900}s.png`.
- Existing runtime build ID: `3a6bd71f8778`; no WASM build was run and the integration worktree was not modified.


## Build 6 JVM crash and signal audit (2026-10-23)

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-mindustry-build5-20261023`

Branch base: `fccddb067` (includes the x86-64 JVM/Wine syscall audit). Both runs used the integration build-6 runtime at `/home/ubuntu/boxedwine64-worktrees/winbox-integ-20261022/dist`, build ID `b0438f56f93a`. No WASM build was run.

I added guest crash-report collection to `tools/bench/gl-game-probe.mjs`. At each capture and on map timeout, it checks the guest app and temp directories for `hs_err_pid*.log` and copies found reports into the probe artifact directory. The JIT=1 run verified the guest path is `/root/home/username/apps/hs_err_pid32.log`; the report was copied successfully.

### JIT=0 result

The first window mapped after 296.4 seconds. The WebGPU GL context initialized, and Mindustry logged OpenGL 3.2, Java 25.0.1, and 259.5 MiB available RAM. Captures through +540 seconds stayed black, with zero GL calls and zero presents. No JVM crash report was written.

The console contains 2,855 unimplemented-opcode messages at stable guest RIP `0x17005541e`, fetching bytes `4b 65 79 42 69 6e 64` (ASCII “KeyBind”). This interpreter-only run is stuck decoding data as instructions; it did not reach rendering.

### JIT=1 result and HotSpot report

The first window mapped after 384.5 seconds. The WebGPU backend stayed active, with no WebGPU failures or page errors. At +300 seconds, the probe counted 29 host GL calls but no presents. By +360 seconds, the guest JVM had aborted and the sample showed zero GL calls/presents. Screenshots remain black; no menu or gameplay appeared.

The guest report says:

```text
Internal Error (symbol.cpp:335)
fatal error: refcount underflow
```

The JVM had been running 619.715 seconds. The current thread was Java thread `main` in `_thread_in_vm`; its Java stack was:

```text
arc.graphics.gl.IndexBufferObject.bind()V+42
arc.graphics.Mesh.bind(...)V+26
arc.graphics.Mesh.render(...)V+13
arc.graphics.Mesh.render(...)V+10
arc.graphics.g2d.SpriteBatch.flush()V+129
arc.graphics.g2d.Draw.flush()V+3
mindustry.graphics.LoadRenderer.draw()V+2918
mindustry.ClientLauncher.update()V+129
arc.backend.sdl.SdlApplication.loop()V+255
mindustry.desktop.DesktopLauncher.main(...)V+63
```

This is a HotSpot internal assertion, not the suspected implicit-exception signal path. The report contains no `SIGSEGV`/`SIGFPE`, `si_code`, or `si_addr`; the native frame is `0x1ce8379` with no source information, and HotSpot's symbol engine could not initialize. Therefore there is no faulting instruction or signal metadata in this report to compare. The failure occurs while Mindustry enters its render path, before any frame is presented.

### CPU64 signal-path audit

Static inspection found the synchronous paths currently construct these fields:

- x86 #DE becomes `SIGFPE/FPE_INTDIV`, with `si_addr` set to the faulting RIP and `TRAPNO=0`.
- CPU64 instruction-fetch #PF sets `si_addr=RIP`, `TRAPNO=14`, and `ERR=0x14` for an unmapped page or `0x15` for a mapped non-executable page. `si_code` is `SEGV_MAPERR` or `SEGV_ACCERR` respectively.
- The signal frame saves guest RIP, RSP, and EFLAGS, and hardware-fault delivery overwrites TRAPNO/ERR/CR2 with the provided fault data. It currently sets the ucontext `fpregs` pointer to null and does not save x87/SSE state.
- `KMemory64::mprotect` updates page flags, but normal data reads/writes intentionally ignore those protections. Thus HotSpot stack-bang or safepoint polling-page accesses may not fault. This is a separate compatibility gap; the captured JVM failure did not raise a signal, so I have not changed this broad memory behavior without a reproducing signal case.

The JIT=0 and JIT=1 outcomes differ: JIT=0 loops in invalid instruction decoding, while JIT=1 advances into Mindustry rendering and then hits the HotSpot symbol refcount assertion. This rules out the earlier assumption that this fatal came from malformed Linux `siginfo_t` or `ucontext_t`, but does not yet identify why HotSpot's symbol reference count underflows. No C++ fix is justified by the current evidence.

Evidence:

- JIT=0, build ID `b0438f56f93a`: `/tmp/winbox-finish/r11/mindustry-build6-jit0/` (`summary.json`, `console.log`, and +180/+360/+540 screenshots).
- JIT=1 extended capture: `/tmp/winbox-finish/r13/mindustry-build6-jit1-extended-hserr/` (`summary.json`, `console.log`, +300/+360 screenshots, and copied `mapped-plus-360s-hs_err-1.log`).
- JIT=1 early captures without a fatal: `/tmp/winbox-finish/r12/mindustry-build6-jit1-hserr/`.
- The probe change passed `node --check tools/bench/gl-game-probe.mjs`; the report was collected from the actual guest filesystem. No build was run.

## Build 6 CPU follow-up

The JIT=0 log's repeated `unimpl opcode` at `0x17005541e` is not an unsupported CPU instruction. The bytes are `4b 65 79 42 69 6e 64 62 69 6e 64 56 61 6c ...`, which are ASCII `KeyBindbindVal...`; objdump decodes the first 16 bytes as a REX prefix, `gs jns`, `imul`, `push`, invalid opcode, and `insb`. The adjacent one-off addresses likewise contain ASCII `bindVal` and `alue`. This indicates control flow reached data, so clearing AVX/BMI/etc. CPUID bits or implementing a guessed vector opcode would not address the observed failure. CPUID leaf 1 currently advertises the supported SSE3/SSSE3/SSE4.1/SSE4.2/POPCNT/CX16 set, and leaf 7 advertises no extensions; the existing CPU64 self-tests assert those values.

The unimplemented-opcode diagnostic previously printed only seven bytes. It now prints 16 to help identify the instruction stream and prefix state on the next instrumented run. The current JIT=0 artifact does not include a guest register/stack dump or memory map at the repeated RIP, so the preceding bad control transfer remains unresolved; no opcode implementation or CPUID change is justified yet.

JIT64's classification explicitly keeps LOCK-prefixed instructions on the CPU64 interpreter path. The interpreter serializes memory RMW operations with host `std::recursive_mutex` shards shared across guest threads. CMPXCHG updates the accumulator only on comparison failure, performs no memory write on failure, and calculates SUB flags; XADD returns the old memory value in its source register and calculates ADD flags; XCHG with memory is implicitly locked; INC/DEC preserve CF. `jit64.cpp` emits none of these locked operations as fast code. The CPU64 tests cover XCHG, matching and failed CMPXCHG, XADD, and LOCK INC/DEC flags. The JIT tests now assert that LOCK CMPXCHG, LOCK XADD, and memory XCHG remain interpreter-only. The suite does not include a true concurrent guest-thread stress case. Such a test would require the emulator's full shared-memory/thread harness; a single-CPU instruction test cannot validate host-thread visibility.

Build 6 JIT=0 evidence: `/tmp/winbox-finish/r11/mindustry-build6-jit0/console.log` (repeated data bytes and zero GL calls/presents). JIT=1's later failure is a HotSpot `symbol.cpp:335` refcount assertion, not a signal fault: `/tmp/winbox-finish/r13/mindustry-build6-jit1-extended-hserr/mapped-plus-360s-hs_err-1.log`. The artifacts establish correlation but do not establish that either issue shares a CPU64 atomic cause.
