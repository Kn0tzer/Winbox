# Q3 WebGPU and CPU fault report, 2026-10-22/23

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-solq3-20261021`.
Prior rendering findings: [q3-webgpu-sol-20261021.md](q3-webgpu-sol-20261021.md).
Probe evidence: `/tmp/winbox-finish/r3/work-luna-q3/evidence/`.

> **Correction (2026-10-23):** The initial reading of RIP `0x10270` as a
> legitimate Q3 CPU-detection probe was wrong. The Build 5 wild-jump trace
> shows a null indirect call from Q3 followed by unintended low-memory
> execution. See “Root-cause correction and CPU64 fix” below.

## Result

The exception at guest RIP `0x10270` is a valid string-I/O instruction, not an
unknown or misdecoded opcode. The diagnostic runtime logged these bytes:

```text
6d 03 7c 03 4e 02 8c 03 11 02 11 02 11 02 35
```

Opcode `6D` is `INSD`: read a 32-bit value from the port in DX and store it
through ES:[RDI], then advance or retreat RDI according to DF. The emulator
recognized it in its explicit user-mode privileged-instruction path and raised
#GP. The remaining bytes are subsequent instructions, not part of the
one-byte `INSD`.

The fault reproduces with `recover=1` under both probe JIT settings. JIT-off
reports the x64 engine disabled; JIT-on reports the WASM engine enabled
(10,828 blocks emitted and 411,481,226 block entries before capture). Both
runs use copied integration build `a4ffa02c3eee` from commit
`dda8743b1827af5a8843bd5dc0f3a462ac120664`. The diagnostic run used local
runtime build `bcb7f7188717`, with JIT off and recovery on. All three runs
mapped the game, used WebGPU, and reported no page errors or WebGPU failures.

| Probe | JIT | Recovery | Result |
| --- | ---: | ---: | --- |
| `q3-fault-jit0-final` | off | on | #GP at `0x10270` |
| `q3-fault-jit1` | on | on | #GP at `0x10270` |
| `q3-fault-byte-dump` | off | on | #GP at `0x10270`; byte dump above |

The +600 s composited capture still shows the Quake III exception card. The
capture is
[`mapped-plus-600s.png`](/tmp/winbox-finish/r3/work-luna-q3/evidence/q3-fault-byte-dump/mapped-plus-600s.png).
Earlier map-start captures recorded in the prior report show Q3DM1 rendering
through WebGPU behind the loading overlay; the guest does not reach active
gameplay.

## Change and validation

Recovery mode now handles non-REP `INS` word/dword forms by writing zero for
an absent port and updating the destination index using the operand width,
address width, and direction flag. This covers the observed `INSD` and its
operand-size-prefixed `INSW` form. Strict mode still raises #GP for `INSD`;
other privileged I/O forms keep their existing fault behavior.

The CPU64 self-test adds recovery checks for zero-valued `INSD` and `INSW`
writes and pointer updates, plus a strict-mode `INSD` fault check. Native
`x64-cache` compilation succeeded. The native CPU64 self-test passed 909/909
cases, both normally and with `BW64_JIT=1 BW64_JIT_VERIFY=1` (the native
target reports its JIT engine as interpreter fallback).

The browser runtime used for the diagnostic probe was built before this
compatibility change. The WASM build allowance was exhausted, so no post-change
browser run verifies that Q3 advances beyond this instruction. The first
subsequent guest fault, if any, is also unknown. Byte dumping is gated by the
`unimpldump=1` probe option.

## Evidence

- JIT off, recovery on: `/tmp/winbox-finish/r3/work-luna-q3/evidence/q3-fault-jit0-final/`
- JIT on, recovery on: `/tmp/winbox-finish/r3/work-luna-q3/evidence/q3-fault-jit1/`
- Fault-byte diagnostic: `/tmp/winbox-finish/r3/work-luna-q3/evidence/q3-fault-byte-dump/`
- Copied integration export: `/tmp/winbox-finish/r3/work-luna-q3/integration-dist/`

No other worktree was modified. No push, deployment, Docker action, or runtime
asset manifest change was made.

## Build 5 browser verification, 2026-10-23

Build 5 came from integration commit
\`336572461556d787b9ad997d36f88ccfa0430a41\`, built at 02:16 UTC. Its WASM
SHA-256 is
\`3a6bd71f8778c0e1e266844363d8078ce5333fc1f4d6de9a761ad1ec4bb2a5d0\`
(build ID \`3a6bd71f8778\`). I copied \`dist/\` into
\`/tmp/winbox-finish/r3/work-luna-q3/build5-dist/\` without editing the
integration worktree.

The copied export omitted \`shader-cache.mjs\`, which \`shader.mjs\` imports.
The first browser attempt got 404s and fell back to WebGL2, so I stopped it.
For the valid run I copied the matching \`web/shader-cache.mjs\` into the
temporary export. The WASM and integration worktree remained unchanged. The
valid probe reports WebGPU active, no WebGPU failures, and no page errors.

With \`jit=1\`, \`recover=1\`, and \`+map q3dm1\`, the first window mapped after
114.6 seconds. The +60 s capture shows guest console startup; the first SDL
present was recorded 80.5 seconds after mapping (about 195 seconds after
launch). That single 500 ms metrics window reports 2 FPS / 500.06 ms. By +120 s
the composited image shows the Quake logo and “connecting to localhost.”
Later sampled windows report no SDL presents, so this run does not establish a
steady gameplay frame rate.

At +360 s and +480 s, the composited screenshots show a textured Q3DM1 scene
through WebGPU behind “LOADING Q3DM1” / “LOADING... MAPS/Q3DM1.BSP.” The prior
fault at \`0x10270\` did not recur; the guest reached map loading. It did not
reach a playable match. The emulator logged recovery skips at \`0x1029e\`
(\`c5 03 c6 03\`), \`0x1031c\` (\`1f\`), and \`0x1032a\` (\`2f\`), followed by
#GP at \`0x10396\` with bytes \`f4 04 03 05...\` (\`HLT\`). Wine then reported an
invalid SEH frame and could not dispatch the exception. The guest exited with
status \`0xC0000096\`, closing the browser page before the +600 s capture.

I left HLT behavior unchanged. User-mode HLT should fault, and the log shows
earlier recovered opcode skips in the same low-address code region; treating
HLT as a no-op could hide a bad control-flow path. The preceding guest state
and the effect of those skips need tracing before choosing another compatibility
rule.

The shared-load waiter returned \`load 4.67 < 5\`. Evidence is under
\`/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/\`:
[\`+60 s\`](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/mapped-plus-60s.png),
[\`+120 s\`](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/mapped-plus-120s.png),
[\`+360 s\`](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/mapped-plus-360s.png),
[\`+480 s\`](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/mapped-plus-480s.png),
[summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/summary.json),
[console log](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-recover-valid/console.log).

## Root-cause correction and CPU64 fix, 2026-10-23

The focused Build 5 wild-jump trace supersedes the earlier guess that `0x10270` was a Q3 CPU-detection probe. The new trace does not itself record `0x10270`; it does show the chain into the low-address region that preceded the later #GP:

```text
RIP=0x14009ff2e  ff 15 6c 6b fa 00    call qword ptr [rip+0xfa6b6c]
RIP=0x0          00 00 00 00
RIP=0x2          00 00 00 00
recover skipped RIP=0x1029e len=2 bytes=c5 03
recover skipped RIP=0x1031c len=1 bytes=1f
recover skipped RIP=0x1032a len=1 bytes=2f
#GP at RIP=0x10396 bytes=f4 ... (HLT)
```

The call site is in `quake3e.x64.exe`: PE ImageBase `0x140000000`, RVA `0x9ff2e`, inside `.text`. Its RIP-relative operand resolves to VA `0x141046aa0`, RVA `0x1046aa0`, inside the zero-filled virtual tail of the Q3 image's `.data` section. It is outside the PE import directory. The next RIP of zero proves that this callback slot held a null pointer at call time. The reason Q3 reached this internal null callback remains unknown.

The recovery skips and HLT are at `0x103xx`, far outside the Q3 PE image. The trace does not identify that low memory as a Wine/DLL image, and the bytes are not a verified instruction stream from a module. They were executed after the null call, while absent memory reads were being zero-filled and decoded. The earlier `INSD` observation at `0x10270` should likewise be treated as untrusted low-memory execution; this trace does not prove its exact position in the same run. HLT is genuinely privileged in user mode. CPU64 already raises #GP for it and routes the synchronous SIGSEGV through Wine's exception handler. The trace then shows Wine rejecting an invalid SEH frame; whether the runaway itself caused that frame corruption is not proven.

CPU64 now checks each instruction page before interpreter or JIT execution. An unmapped fetch raises #PF/SIGSEGV with `si_addr=RIP`, `CR2=RIP`, and the user instruction-fetch error bits; a mapped non-executable page raises the protection form. The page-permission cache follows mapping generations. `munmap` marks pages unmapped while retaining backing buffers, so the existing Wine-compatible stale data reads remain intact. Recovery no longer turns `INSD`/`INSW` into zero input; both remain #GP faults.

The CPU64 self-tests now cover the null indirect call stopping at its first unmapped fetch, non-executable fetches, permission-cache invalidation after `munmap`, and preservation of #GP for `INSD`, `INSW`, and `HLT` in recovery mode. I did not compile or run these tests because you are scheduling builds. No post-fix browser probe has run; this commit needs to be included in the next scheduled build to verify the Wine SEH path and Q3 progress.

Evidence: [wild-jump console log](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-wildjump-trace/console.log), [wild-jump summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build5-map-wildjump-trace/summary.json), [Build 5 Q3 executable in the copied probe archive](/tmp/winbox-finish/r3/work-luna-q3/q3-probe.zip).

## Null callback at Q3 RVA `0x9ff2e`, 2026-10-23

The indirect call is Q3's `qglGenProgramsARB(1, &vbo_vp[index])` in
`CompileVertexProgram`, followed by construction of the `!!ARBvp1.0` source
through `BuildVP`. The PE has no usable symbols/PDB, so this identification is
from the call ABI (`n = 1`, output-program pointer), the `vbo_vp` indexed slot,
and the embedded vertex-program strings immediately after the call. The matching
Quake3e source is [`tr_vbo.c`](https://github.com/ec-/Quake3e/blob/main/code/renderer/tr_vbo.c#L141-L210)
and its `CompileVertexProgram` caller at
[`tr_vbo.c#L450-L457`](https://github.com/ec-/Quake3e/blob/main/code/renderer/tr_vbo.c#L450-L457).

`qglGenProgramsARB` is populated only when both
`GL_ARB_vertex_program` and `GL_ARB_fragment_program` are present in the
extension string. Quake3e's resolver gates that lookup on both tokens in
[`tr_init.c#L523-L535`](https://github.com/ec-/Quake3e/blob/main/code/renderer/tr_init.c#L523-L535).
The current `libgl64.c` extension string advertises multitexture, VBO, shader
objects, and vertex/fragment *shaders*, but not either ARB *program* extension
([`libgl64.c`](../../tools/rootfs64/libgl64/libgl64.c), extension string and
`g_extList`). Therefore Q3 leaves `qglGenProgramsARB` null by design; there is
no `glGenProgramsARB` GetProcAddress miss in the saved logs. The call still
being reached means the renderer's VBO/program path ran without its required
ARB-program pointer. Current Quake3e source has an outer null check in
`R_BuildWorldVBO`, while `CompileVertexProgram` itself has no pointer check.
The saved stripped binary does not establish which caller/gate differed or was
bypassed at runtime.

The Build 5 wild-jump log has no proc-resolution records, so it cannot supply
the exact GetProcAddress list for that run. The earlier proc-traced Q3 runs
`q3-map-r3` and `q3-map-r4` both recorded these misses:

```text
glClientActiveTextureARB
glMultiTexCoord2fARB
glXAllocateMemoryNV
glXCopyContext
glXCreatePbuffer
glXCreatePixmap
glXCreateWindow
glXDestroyPbuffer
glXDestroyPixmap
glXDestroyWindow
glXFreeMemoryNV
glXQueryDrawable
```

Those `MISS` records are not NULL returns. The `glXGetProcAddressARB` fallback
in `libgl64.c` returns the non-null `gl64_noop` stub for an unknown name. At the
time of those runs, the two missing GL entrypoints were a real incompleteness:
the bridge advertised `GL_ARB_multitexture` but lacked working
`glMultiTexCoord2fARB` and `glClientActiveTextureARB`. Commit `30a921c26` later
added their ABI handlers and WebGPU fixed-function behavior. They did not match
the faulting call's two-argument `glGenProgramsARB` signature and did not
explain the zero callback slot.

The extension gate is withholding unsupported ARB vertex/fragment programs.
Advertising them and returning placeholder functions would let Q3 enter ARB
shader compilation without a compatible WebGPU implementation. The probe now
sets `r_vbo 0` to keep that path disabled, and commit `30a921c26` implements the
two previously missing multitexture entrypoints. The renderer source is outside
this Boxedwine worktree, so the probe's Build 6 result is still needed to
confirm that Quake3e stays on its client-array path.

I attempted one miss-only diagnostic probe using the copied Build 5 export and
the same Q3 map/recovery settings. It stayed queued behind other heavy probes
for 18 minutes and never launched; I stopped it before browser startup. The
proc-name evidence above is therefore explicitly from `q3-map-r3/r4`, not the
Build 5 wild-jump run. No build was run.

## Build 6 multitexture and map-start changes (Luna, 2026-10-23)

Quake3e's checked-in `tr_init.c` sets `r_vbo`'s default to `0`. Its
`R_BuildWorldVBO` path returns unless `qglBindBufferARB` exists and `r_vbo` is
nonzero; it then returns if `qglGenProgramsARB` is null or fewer than three
texture units are reported. The only `CompileVertexProgram` calls in this
source are below that path through `isStaticShader`. The probe did not set
`r_vbo`, so its launch now explicitly uses `+set r_vbo 0` before `+map q3dm1`.
This overrides any archived or persisted cvar value and drives the probe into
the ordinary client-array renderer. The checked source guard and old saved
runtime call still do not prove which cvar value or binary path produced the
earlier fault; the Build 6 run must confirm that.

The GL bridge now has dedicated ABI IDs 884 and 885 and guest wrappers for
`glMultiTexCoord2fARB` and `glClientActiveTextureARB`; both names resolve through
the proc table instead of the no-op fallback. `glClientActiveTextureARB`
selects a separate client texture-coordinate array for units 0 and 1, and
`glMultiTexCoord2fARB` updates fixed-function current coordinates, including
inside recorded immediate-mode blocks. The fixed-function WebGPU path snapshots
both client arrays, tracks texture enable/environment/matrix state per server
unit, and emits a two-sampler shader with separate UV inputs. It supports Q3's
modulate, replace, decal, and add texture environment modes. The synchronous
compositor mirror now applies both texture units too, so lightmaps are retained
in CPU-backed window presentation/readback.

The focused GL/Q3 unit run passed 35 tests, including Naga validation of the
two-texture WGSL, separate unit-0/unit-1 UV buffers, immediate-mode coordinates,
ABI/handler/proc registration, and the Q3 `r_vbo`/map arguments. Node syntax
checks and `git diff --check` also passed. At the time of this entry the source
was awaiting Build 6; the browser verification is recorded below.

## Q3 extension advertisement audit (Luna, 2026-10-23)

I compared the extension gates and proc lists in Quake3e's `renderer/tr_init.c`
and `renderer/qgl.h` with the guest `GL_EXTENSIONS` and `glGetStringi` lists,
the `libgl64` proc table, and the bridge handlers. The monolithic and indexed
extension lists currently contain the same 17 tokens (the new unit test checks
their ordered equality).

Only two advertised extensions activate Q3's extension-init proc lookups:

| Advertised token | Q3 proc lookups | Result |
| --- | --- | --- |
| `GL_ARB_multitexture` | `glMultiTexCoord2fARB`, `glActiveTextureARB`, `glClientActiveTextureARB` | All resolve to implemented wrappers and dedicated bridge handling. The WebGPU fixed-function path retains separate unit 0/1 coordinates and samples both enabled units. |
| `GL_ARB_vertex_buffer_object` | `glGenBuffersARB`, `glDeleteBuffersARB`, `glBindBufferARB`, `glBufferDataARB` | All four resolve to wrappers forwarding to their implemented core entrypoints. The probe's `+set r_vbo 0` keeps Q3's world-VBO/ARB-program path disabled. |

The extension-init features whose bridge path is missing, deliberately stubbed,
or not supported by the Q3 WebGPU renderer remain absent from both extension
queries: compiled vertex arrays; texture edge/border clamp; texture env combine
and add; anisotropic filtering; ARB/S3TC compression; ARB vertex/fragment
programs; and the EXT framebuffer-object/blit pair. In particular,
`glLockArraysEXT` and `glUnlockArraysEXT` are no-op hint functions, but
`GL_EXT_compiled_vertex_array` is not advertised, so Quake3e does not resolve or
call them from its extension initializer. The same applies to the other
withheld feature branches. Q3's FBO initializer requires both EXT framebuffer
tokens, and its ARB program initializer requires both ARB program tokens; neither
gate is satisfied by the current string.

Added `q3-gl-extension-coverage.test.mjs` to assert that the two advertised Q3
paths have registered, non-fallback wrappers and that the unsupported Q3
feature tokens stay absent in both string forms. This audit found no remaining
Q3 extension-gate/proc mismatch after the multitexture implementation; it
needed regression coverage and report correction, not another bridge behavior
change. The focused GL/Q3 unit command passed 39 tests, with 0 failures. That
static audit did not build or probe; Build 6 browser results follow.

## Build 6 Q3 browser verification (Luna, 2026-10-23)

I ran the existing Q3 archive against the read-only Build 6 dist from
`integ/winbox-20261022b`, Git SHA `4f603f50f71a9e4dd6d2c0c3b32fca53acbb31af`
(`wasm_sha256` prefix `b0438f56f93a`). Both probes used JIT and recovery,
`+set r_vbo 0`, and `+map q3dm1`; no build was run.

The initial run mapped the first SDL window 126.8 seconds after launch. Its
60-second capture showed Q3's renderer initialization console. At +120 and +240
seconds the screen showed the Quake logo and `CONNECTING TO LOCALHOST / AWAITING
CONNECTION...0`. An extended run mapped the first window in 76.4 seconds and
captured at +360 seconds. That screenshot showed the richly textured Q3DM1
arena view, distinct from the packaged 128x128 `levelshots/Q3DM1.jpg`, with
`LOADING Q3DM1`, `CONNECTING TO LOCALHOST`, and `LOADING... MAPS/Q3DM1.BSP`
overlaid. This confirms that a Q3DM1 view is drawn through the WebGPU GL path,
but the map load has not completed and gameplay/control is not confirmed.

Both runs reported `gl64: host GL up (WebGPU)`, no WebGPU bridge failures, no
page errors, and no probe exception. The trace logged the first indexed GL draw
(`mode=0x4`, 12 indices). The next visible blocker is the local-host connection
and BSP load remaining in progress. Wine also logged `getaddrinfo Failed to
resolve your host name IP` and a guest UDP socket failure for domain 10
(AF_INET6), errno 97. Those network diagnostics may be related to the local
connect wait, but their causal link is not established; trace the Q3 local
server/client handshake next. FPS is not established: one SDL metrics sample in
the initial run reported 2.0 FPS / 502.92 ms, while the later capture had no
usable presentation sample.

Evidence: [Build 6 +120s capture](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-4f603f50f/mapped-plus-120s.png), [Build 6 +240s capture](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-4f603f50f/mapped-plus-240s.png), [Build 6 +360s capture](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-4f603f50f-360/mapped-plus-360s.png), [initial run summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-4f603f50f/summary.json), [extended run summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-4f603f50f-360/summary.json), and [extended run console](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-4f603f50f-360/console.log).

## Q3 local connection and UDP loopback diagnosis (Luna, 2026-10-23)

Quake3e's `+map q3dm1` path starts a listen server in the same process. `SV_Map_f`
calls `SV_SpawnServer`; that calls `CL_MapLoading`, which sets the client's
server name to the literal `localhost` and immediately calls
`CL_CheckForResend`. `NET_StringToAdr` special-cases that exact string to
`NA_LOOPBACK` before its `getaddrinfo` path. `NET_SendPacket` routes
`NA_LOOPBACK` through Quake3e's in-process `NET_SendLoopPacket` queue. The main
frame drains those queues with `NET_GetLoopPacket` and dispatches them to
`CL_PacketEvent` / `SV_PacketEvent`. Therefore the built-in localhost handshake
does not use Wine name resolution or the guest UDP sockets.

The browser rootfs also has a working localhost mapping: the virtual
`/etc/hosts` contains `127.0.0.1 localhost`, and the packaged rootfs
`/etc/nsswitch.conf` says `hosts: files`. Wine's earlier `getaddrinfo Failed to
resolve your host name IP` message is not evidence that the Quake3e literal
`localhost` failed to resolve; that warning concerns a separate hostname path.

The prior AF_INET6 UDP `EAFNOSUPPORT` / errno 97 is expected for this runtime.
With `USE_IPV6`, Quake3e defaults `net_enabled` to 3 (IPv4 and IPv6). Its
`NET_OpenIP` tries the IPv6 socket, stops that address-family retry on
`EAFNOSUPPORT`, then independently enters the IPv4 bind block because bit 1
remains enabled. `+set net_enabled 1` disables the IPv6 attempt while preserving
IPv4. The emulator's `ksocket` currently accepts AF_INET UDP for the browser
loopback transport and rejects AF_INET6 with `-K_EAFNOSUPPORT`.

The browser UDP loopback implementation itself routes the requested wildcard
case: it accepts `0.0.0.0` and `127/8` binds, registers by port, matches a
`127.0.0.1` destination against a wildcard-bound receiver, queues the payload,
and signals its read condition. `isReadReady` reports queued packets, and
`internal_poll` uses that predicate for `POLLIN`; `kselect` also delegates
readiness to `internal_poll`. Expanded the process selftest to bind a receiver
on `0.0.0.0:27960`, send to `127.0.0.1:27960`, verify empty/ready poll results,
and receive the payload and source address.

One IPv4-only browser probe against the read-only Build 6 dist used
`+set net_enabled 1 +map q3dm1`. The first window mapped after 131.3 seconds;
WebGPU initialized with no bridge failures or page errors. At +180 seconds the
capture still read `CONNECTING TO LOCALHOST / WAITING CONNECTION... 0`; at
+360 seconds it remained on the Q3DM1 loading screen with `CONNECTING TO
LOCALHOST`. Disabling IPv6 did not advance the local handshake. The +60-second
capture showed renderer startup in the Q3 console. No stable gameplay FPS was
recorded; later captures had no presentation frames in their metrics.

This eliminates DNS, IPv6 fallback, and the kernel UDP wildcard/readiness path
as established causes of this specific Q3 local connection stall. No runtime
network code change is justified by this trace. The remaining blocker is in
Quake3e's own loopback packet/frame processing or the startup state around it;
the next trace should count the `NA_LOOPBACK` request entering
`NET_SendLoopPacket`, server-side `NET_GetLoopPacket` draining it, and the
resulting `SV_PacketEvent` response. This diagnostic was not a build.

Evidence: [IPv4-only +60s console capture](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-netenabled1-20261023/mapped-plus-60s.png), [IPv4-only +180s capture](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-netenabled1-20261023/mapped-plus-180s.png), [IPv4-only +360s capture](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-netenabled1-20261023/mapped-plus-360s.png), and [IPv4-only probe summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-netenabled1-20261023/summary.json).


## Q3 present latency measurement, 2026-10-23

**Retrospective correction:** this measurement ran at roughly 9 load average on
a four-core host. Treat the rates below as contaminated raw history only; they
do not support a performance or present-bound conclusion. The later Build 6
rerun below also did not reach the requested quiet-host threshold.

I ran the Q3 probe against the read-only Build 6 export (`4f603f50f71a`, WASM build ID `b0438f56f93a`) with JIT and recovery enabled, `+set net_enabled 1`, and `+map q3dm1`. The first window mapped 117.9 seconds after probe launch. The +90-second composited capture still shows `CONNECTING TO LOCALHOST / AWAITING CONNECTION...0`; it is not the BSP asset-loading screen, so these timings do not measure Q3's per-chunk map loader.

For the 30-second interval ending at mapped+90s, the timestamped metrics stream recorded 22 frames at the XWire SDL sink (0.73 frames/s over the nominal interval). The metric stream covered 19.55 seconds of that interval; it had a 12.5-second gap and its last record was 8.15 seconds before the screenshot. Because metrics emit only after GL/audio/present activity, that gap means the presentation counter is a lower bound if a final frame was not followed by another metrics event. One `gl64 PHASE: swap` marker appeared in the interval, but this trace is best-effort on guest worker threads and is not a reliable exact swap count.

The same records show the x64 JIT retired 18,092,937 guest instructions between counters 541,587,716 and 559,680,653 across the covered 19.55 seconds, or about 0.93 million instructions/s. The counter was not refreshed during the final 8.15 seconds, so this is the measured subspan rate rather than an exact 30-second instruction rate. The GL bridge stage accumulated 3,409.02 ms across 122 GL calls in the captured records, averaging 27.94 ms per GL call. That stage includes marshaling and the synchronous main-thread hop; it does not isolate `glXSwapBuffers` latency.

The present path explains why the old 500 ms sample is not evidence of a GPU completion wait:

- `GL64_fn_glXSwapBuffers` calls `glOnMain`, which enqueues a closure through `xwireRunOnMainThread` and blocks on its condition variable until the main loop runs it. The closure calls `glFlush()` and `readbackAndPresent()`.
- `glReadPixels` in `gl-webgpu.mjs` copies from the CPU shadow pixel array. `readbackAndPresent()` then converts RGBA to BGRX and `submitFrame()` copies the pixels into the sink's pending buffer. The current metrics combine this work with the main-thread wait; they do not time those pieces separately.
- The sink's SDL composition (`putBitsOnWnd` and `screen->present`) runs later in `tickMainThread`, after the queued GL closure has returned and signaled the guest thread.
- `gl-webgpu.mjs` queues the canvas copy with `GPUQueue.submit()` and does not await GPU completion in `_present()`. There is no `mapAsync` readback in this path. `onSubmittedWorkDone()` appears only in promise callbacks that defer temporary-buffer cleanup.

This source and runtime evidence does not show a per-present GPU round trip. The main-thread bridge hop is synchronous, but the SDL compositor runs after the hop signals the guest, and current telemetry cannot attribute its time per swap. I made no async-swap change and ran no build. There is no post-change timing to compare; the 27.94 ms figure is an average over all GL bridge calls, not a per-present result.

Evidence: [mapped+90s screenshot](/tmp/winbox-finish/r3/work-luna-q3/evidence/present-metric-window-jit1-20261023/mapped-plus-90s.png), [full probe summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/present-metric-window-jit1-20261023/summary.json), and [console log](/tmp/winbox-finish/r3/work-luna-q3/evidence/present-metric-window-jit1-20261023/console.log). The temporary probe instrumentation used to retain the 30-second event window was reverted after capture.

## Build 6 Q3 12-minute progression and metrics run (2026-10-23)

I waited once with `/home/ubuntu/scripts/winbox-wait-load.sh 3 15`. It timed out
after 15 minutes with load 8.51 (threshold 3), so I proceeded and recorded the
actual load. `/proc/loadavg` at probe start was `7.90 7.46 7.97`; at probe end
it was `6.89 7.87 8.06` (1-, 5-, and 15-minute averages). This was not a
quiet-host measurement and its throughput must not be compared as an
uncontended performance result.

The single probe used the read-only Build 6 dist (`4f603f50f71a9e4dd6d2c0c3b32fca53acbb31af`, WASM build ID `b0438f56f93a`), JIT and recovery enabled, and
`+set net_enabled 1 +set developer 1 +set com_printf 1 +map q3dm1` (along with
the existing windowed OpenGL1 and `r_vbo 0` settings). The first window mapped
92.2 seconds after launch. I captured at mapped+120, +240, +360, +480, +600,
and +720 seconds.

The +120s and +240s screenshots still show `CONNECTING TO LOCALHOST / AWAITING
CONNECTION...0`. By +360s that specific wait has cleared and Q3 is presenting
the Q3DM1 loading screen with `LOADING... MAPS/Q3DM1.BSP`. Later captures show
asset progress (`SHOTGUN`, then `CELLS`) over the textured 3D arena background.
At +720s it is still on the `CELLS` loading screen; no playable gameplay frame
was captured. The `CONNECTING TO LOCALHOST` heading remains during map/asset
loading, so this run proves transition into the BSP/asset loader, not completion
of the client/server handshake. Since it left `AWAITING CONNECTION`, I did not
start the proposed NA_LOOPBACK trace. `developer 1` and `com_printf 1` were
passed, but the guest log contained no useful Q3 network diagnostics.

Across the mapped 12-minute stream, the first metrics record was at mapped+1.5s
and the last at mapped+719.8s. The JIT counter advanced by 1,586,687,603 retired
instructions over 718.35 measured seconds: **2.21 million instructions/s**.
The SDL sink recorded 92 presented frames over the same span: **0.128
presents/s**. These are observed aggregate rates under the high host load above,
not a clean estimate of Q3's unconstrained CPU throughput or GPU presentation
latency. The metric stream is activity-triggered and can omit a trailing frame
that is not followed by another metrics event; the SDL count is therefore a
lower bound.

The activity-triggered 30-second SDL present counts were:

| Mapped interval | SDL presents | Mapped interval | SDL presents |
| --- | ---: | --- | ---: |
| 0–30s | 43 | 30–60s | 0 |
| 60–90s | 32 | 90–120s | 0 |
| 120–150s | 0 | 150–180s | 0 |
| 180–210s | 0 | 210–240s | 0 |
| 240–270s | 0 | 270–300s | 1 |
| 300–330s | 2 | 330–360s | 1 |
| 360–390s | 0 | 390–420s | 0 |
| 420–450s | 0 | 450–480s | 0 |
| 480–510s | 1 | 510–540s | 0 |
| 540–570s | 5 | 570–600s | 1 |
| 600–630s | 1 | 630–660s | 1 |
| 660–690s | 1 | 690–720s | 3 |

The counters are XWire SDL sink presents, not direct `SwapBuffers` calls. A
missing metrics record means there was no GL/audio/present activity to trigger
an emission in that interval; instruction-rate coverage is consequently
aggregated between the first and last available cumulative JIT snapshots.
Neither the high-load run nor its sparse per-window telemetry justifies making
the SwapBuffers path asynchronous yet. No source change or build was made.

Evidence: [mapped+120s](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-q3-quiet-12min-20261023/mapped-plus-120s.png), [mapped+240s](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-q3-quiet-12min-20261023/mapped-plus-240s.png), [mapped+360s](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-q3-quiet-12min-20261023/mapped-plus-360s.png), [mapped+480s](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-q3-quiet-12min-20261023/mapped-plus-480s.png), [mapped+600s](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-q3-quiet-12min-20261023/mapped-plus-600s.png), [mapped+720s](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-q3-quiet-12min-20261023/mapped-plus-720s.png), [summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-q3-quiet-12min-20261023/summary.json), and [full metrics/console log](/tmp/winbox-finish/r3/work-luna-q3/evidence/build6-q3-quiet-12min-20261023/console.log).


## Build 8 Q3 extended probe and depth-range fix (2026-10-23)

I ran the quiet-host helper once; it returned `load 4.68 < 5: go`. The first,
trace-enabled probe started with load `4.55 6.76 6.28` (1-, 5-, and 15-minute
averages). It closed after the +360s screenshot, before it could answer the
full progression question. The no-trace retry started at `7.25 7.93 7.21`, so
its rate measurements are load-contaminated. The page closed at 10:41:03,
19m25s after launch and before the scheduled +1200s through +1500s captures. I
recorded one post-run load sample at 10:46:07 (`5.24 5.98 6.79`); it is not an
exact page-close reading.

The retry used the read-only Build 8 dist (`integ 72315a6d5`, WASM build ID
`ba9aa2ed6a32`), JIT and recovery enabled, and `+map q3dm1`. It mapped 113.9s
after launch, with WebGPU active and no WebGPU bridge failures. The captures
show `AWAITING CONNECTION` at +120s and +240s; a correctly textured Q3DM1 arena
at +360s with `LOADING... MAPS/Q3DM1.BSP`; `GAME MEDIA` at +480s; `ROCKET
LAUNCHER` at +600s; and `AWAITING SNAPSHOT...` at +720s, +840s, and +960s.
The +960s screenshot confirms the textured 3D scene is being rendered through
the WebGPU path. It does not prove that the local client/server handshake
completed, because the visible frame may be stale after the runtime error
below.

The metrics stream covered mapped+1.5s through mapped+686.6s. It recorded
1,628,269,262 retired guest instructions (2.38 million/s) and 91 XWire SDL sink
presents (0.133 presents/s). Metrics stopped at the failure point; later
screenshots did not produce more activity. Treat these as raw rates under the
load increase above, not as a clean throughput estimate.

At 10:34:59.374, about mapped+686.5s, the runtime logged an uncaught
`TypeError: GLctx.depthRange is not a function`, from Emscripten's
`_emscripten_glDepthRangef`. The error interrupts the emulated main loop during
late map startup, before I could establish whether Q3 would receive its first
server snapshot. The missing method was a real GL/WebGPU bridge gap, not
continued BSP or ZIP loading. I added `depthRange()` state, `GL_DEPTH_RANGE`
query support, and passed the range to WebGPU's per-draw viewport. A focused
unit test exercises the method and verifies the submitted viewport bounds.

All 18 `gl-webgpu*.test.mjs` unit tests pass. No build or post-fix browser run
was performed, per the no-build instruction. The next run should include this
commit in a new integration dist; it will show whether this exception was the
last blocker or whether the client remains at `AWAITING SNAPSHOT`.

Evidence: [+360s BSP / textured arena](/tmp/winbox-finish/r3/work-luna-q3/evidence/build8-q3-25min-nodiag-20261023/mapped-plus-360s.png), [+720s awaiting snapshot](/tmp/winbox-finish/r3/work-luna-q3/evidence/build8-q3-25min-nodiag-20261023/mapped-plus-720s.png), [+960s final capture](/tmp/winbox-finish/r3/work-luna-q3/evidence/build8-q3-25min-nodiag-20261023/mapped-plus-960s.png), [probe summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build8-q3-25min-nodiag-20261023/summary.json), and [metrics and error log](/tmp/winbox-finish/r3/work-luna-q3/evidence/build8-q3-25min-nodiag-20261023/console.log).

## Build 8 depth-range re-probe: active client reached (2026-10-23)

I reran Q3 against the read-only Build 8 integration dist (`72315a6d5a98`,
WASM build ID `ba9aa2ed6a32`) with JIT and recovery enabled. The launch used
windowed OpenGL1, `r_vbo 0`, IPv4 networking, developer logging, and `+map
q3dm1`. The shared-load waiter returned `load 1.50 < 5: go`; one start sample was
`0.99 3.62 5.65`. The first window mapped 81.9 seconds after launch. The
probe took screenshots every 120 seconds and enabled guest thread/syscall
diagnostics.

The +120s and +240s screenshots still show `AWAITING CONNECTION`. By +360s it
is loading `MAPS/Q3DM1.BSP`; the +600s capture shows game-media loading, and
+720s/+840s show asset loading. At +960s it shows `AWAITING SNAPSHOT`. This
wait clears by +1080s: the screenshot reports `Going from CS_PRIMED to
CS_ACTIVE for UnnamedPlayer` and `ServerCommand: 1: print "UnnamedPlayer
entered the game"`, then displays the player HUD and Q3DM1 scene. This is
positive evidence that the local client/listen-server handshake completed;
the earlier Build 6 `AWAITING CONNECTION` diagnosis does not describe this run.
The syscall trace records 15 `sendmsg`, 1,958 `recvmsg`, and 4 `recvfrom`
calls in Q3 PID 10. Those counts alone do not identify loopback packets.

The client is in the game, but the view is not yet playable. The +1080s through
+1440s screenshots show large stretched polygons over the arena, with the HUD
visible; the FPS overlay reports 0 in +1320s and +1440s. The probe did not send
keyboard or mouse input, so input response remains unverified. There were no
`GLctx.depthRange` exceptions, WebGPU recovery failures, or browser page errors.

Thread evidence points at rendering activity rather than the former snapshot
wait: the sampled Q3 main thread (PID 10/TID 11) remained at
`libGL.so.1+0x270f`, inside the GL wrapper region previously mapped to the
tail of `glDrawElements`. Its final thread dump is `state=running`, with a
caller frame at `opengl32.dll+0x942b2` and Q3 renderer frames on the stack. The
separate PID 14/TID 15 thread is blocked in `epoll_wait`; it is not evidence of
a starved Q3 server thread. The screenshot's `CS_ACTIVE` transition confirms
that server/client progress did occur. This probe therefore does not justify a
loopback networking change; the remaining blocker is the broken/sparse
in-game rendering, followed by an input-response check.

Across the metrics stream from guest counters 188,612,314 to 1,925,528,504,
1.737 billion guest instructions retired over 1,435 seconds (about 1.21 million
instructions/s). The 927 emitted metric records sum to 94 SDL sink presents
(about 0.066 presents/s). The guest trace adds overhead, so treat the instruction rate as a traced average,
not an unconstrained performance figure. SDL telemetry is activity-triggered,
so the present count may be a lower bound. No end-of-run host load sample was
available.

The browser page closed after the +1440s capture, before the requested +1560s
through +1800s captures. The probe ended with `page.waitForTimeout: Target
page, context or browser has been closed`; no page error or WebGPU failure was
recorded before that. No code fix was identified or build run in this probe.

Evidence: [+960s, awaiting snapshot](/tmp/winbox-finish/r3/work-luna-q3/evidence/build8-depthrange-fixed-q3-30min-20261023/mapped-plus-960s.png), [+1080s, CS_ACTIVE and player view](/tmp/winbox-finish/r3/work-luna-q3/evidence/build8-depthrange-fixed-q3-30min-20261023/mapped-plus-1080s.png), [+1440s, latest view](/tmp/winbox-finish/r3/work-luna-q3/evidence/build8-depthrange-fixed-q3-30min-20261023/mapped-plus-1440s.png), [probe summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build8-depthrange-fixed-q3-30min-20261023/summary.json), and [metrics/thread/syscall trace](/tmp/winbox-finish/r3/work-luna-q3/evidence/build8-depthrange-fixed-q3-30min-20261023/console.log).

## Newer integration build W-key input check (2026-10-23)

While the prior input attempt was queued, the integration dist refreshed at
11:17 UTC. This focused probe therefore used the newer prebuilt runtime from
integration SHA `577a6c9d9752cdb78c4a17258cc1a94a7c83b6dc` (WASM SHA/build ID
`10ef3489d8a6`), with no build started here. It reached `CS_ACTIVE` by
mapped+840s and again displayed the server message that UnnamedPlayer entered
the game.

I added opt-in `PROBE_FORWARD_AT_SECONDS` / `PROBE_FORWARD_HOLD_MS` support to
the GL game probe. At +840s it captured the active view, sent W keydown for
3 seconds, released W, waited 1 second, and captured again. The saved
`q3config.cfg` binds W to `+forward`. ImageMagick `compare -metric AE` reports
zero changed pixels between the two screenshots. Thus the browser sent the
keyboard event but the game produced no visible movement or new rendered frame
in response. The probe summary reports WebGPU active, no WebGPU failures, no
page errors, and no probe exception. One adjacent 500ms metrics record had one
SDL present; subsequent records had zero presents, while the last sampled GL
stage accumulator was 16.53 seconds. This supports investigating the stalled
or sparse in-game GL/render path next; it does not identify which bridge call
causes it. The local network handshake is already past `CS_ACTIVE`, so no
loopback networking fix is indicated by this input test.

Evidence: [+840s before W](/tmp/winbox-finish/r3/work-luna-q3/evidence/build9-q3-forward-input-20261023/mapped-plus-840s.png), [after W keydown/up](/tmp/winbox-finish/r3/work-luna-q3/evidence/build9-q3-forward-input-20261023/mapped-plus-840s-after-w.png), [summary](/tmp/winbox-finish/r3/work-luna-q3/evidence/build9-q3-forward-input-20261023/summary.json), and [console/metrics log](/tmp/winbox-finish/r3/work-luna-q3/evidence/build9-q3-forward-input-20261023/console.log).
