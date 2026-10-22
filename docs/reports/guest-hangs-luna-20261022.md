# Guest hang investigation, Luna, 2026-10-22

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-solhang-20261021`

Branch: `lane/winbox-solhang-20261021` (fast-forwarded to `e00a6c105b46`)

## Mindustry

The current merged branch maps Mindustry. The original guest-kernel defects are already fixed in the branch:

- Wine’s IPv4 UDP `sendmsg`/`recvmsg` path called host socket functions for guest-local sockets with descriptor `-1`. Emscripten returned EBADF, which the guest translator converted to EIO. Commit `da96fd81a` dispatches UDP message I/O to the guest-local packet queues.
- Java’s selector pipe uses a guest loopback TCP pair. `recvmsg` held the receive condition mutex and then called `recvLoopbackStream`, which tried to lock the same nonrecursive mutex. Commit `05562a8f6` limits the outer lock to native receives.
- Java 25 sets TCP_NODELAY on the accepted pipe socket. Commit `8ea4c5a4d` stores that option in guest state for loopback streams.

I built commit `e00a6c105b46` with the requested `wasm64-mt-jit` target. The build completed successfully at 2026-10-22 18:33 UTC; its WASM SHA-256 begins `7954964929f0`. The fresh Mindustry probe mapped its first window after 344.9 seconds.

The game did not render a usable menu. The +60 second screenshot is black. The probe logged `program 5 link failed: UnexpectedNewLine`, then a WebGL2 `getAttribLocation` TypeError and a fallback from WebGPU to WebGL2. Its summary reports `mapped: true`, `webgpuActive: false`, and a page error, so the probe exits 1 despite the successful window map. This remaining failure is in the GL rendering path.

### WebGPU shader failure

The diagnostic probe captured program 5's vertex shader. It contains the common desktop GLSL precision compatibility block:

```glsl
#ifndef GL_ES
#define lowp
#define mediump
#define highp
#endif
```

`rewriteGlslForWebGPU` removed the precision words but only handled the corresponding `#ifdef GL_ES` form. That left empty `#define` directives in the GLSL passed to Naga, which rejected the vertex shader as `UnexpectedNewLine`. The failed link then called the bridge's device-failure callback, which restored WebGL2; Emscripten subsequently passed the bridge's JavaScript program object to native WebGL2, producing the `getAttribLocation` TypeError.

The translator now removes `#ifndef GL_ES` blocks only when their non-comment contents are precision qualifier macro definitions, and removes standalone `lowp`/`mediump`/`highp` macro definitions before stripping precision qualifiers. Other `#ifndef GL_ES` blocks are preserved. The shader failure logger now emits the first failed shader's guest source and pre-pass GLSL through the bridge logger, including failures before Naga translation.

The new `gl-es-precision-macros.vert.glsl` fixture is copied from the captured guest shader. It failed with `UnexpectedNewLine` before the precision-macro fix and now translates through the pre-pass and Naga.

That fix exposed a second issue in `liftForNaga`: when the pre-pass added `layout(location = 0)` for the bound `a_position`, the allocator did not reserve that explicit location. It then assigned location 0 to the unbound `a_texCoord0`, and Naga rejected the vertex entry point. The allocator now records explicit interface locations and advances the namespace counter before assigning automatic locations. The mixed explicit/automatic location test failed on the duplicate 0 and now passes with `a_position` at 0 and `a_texCoord0` at 1.

The shader compatibility suite now passes all 10 tests. The post-macro-fix probe mapped in 290.3 seconds and cleared `UnexpectedNewLine`, but exposed the duplicate-location validation failure; the final probe with both fixes is in progress. One `winbox-wait-load.sh 5 9` invocation timed out at load 8.67 after nine minutes, so the queued probe proceeded under the requested timeout rule. No WASM rebuild was needed; only the local browser export was refreshed.

Diagnostic artifacts:

- Full shader source and console: `/tmp/winbox-finish/r3/work-luna-hang/mindustry-shader-dump/run3/console.log`
- Diagnostic summary: `/tmp/winbox-finish/r3/work-luna-hang/mindustry-shader-dump/run3/summary.json`
- Diagnostic screenshot: `/tmp/winbox-finish/r3/work-luna-hang/mindustry-shader-dump/run3/mapped-plus-60s.png`
- Post-macro-fix summary: `/tmp/winbox-finish/r3/work-luna-hang/mindustry-shader-dump/verify1/summary.json`
- Post-macro-fix screenshot: `/tmp/winbox-finish/r3/work-luna-hang/mindustry-shader-dump/verify1/mapped-plus-60s.png`

Evidence:

- Build log: `/tmp/winbox-finish/r3/work-luna-hang/build1.log`
- Probe summary and console: `/tmp/winbox-finish/r3/work-luna-hang/mindustry/summary.json`, `console.log`
- Screenshot: `/tmp/winbox-finish/r3/work-luna-hang/mindustry/mapped-plus-60s.png`

## Native socket safety test

The three failing subtests compiled extracted production snippets against fixtures missing the new guest-loopback fields, helpers, error constants, and receive data types. I added those stubs to the three affected fixture classes. The original assertions are unchanged.

`node --test web/tests/suite/unit/native-socket-safety.test.mjs` passes all six subtests. The repair is committed as `a33f495d8`.

## Gang Beasts handoff

Per the scope change, the Sonnet lane owns further Gang Beasts investigation. The existing trace in `guest-hangs-sol-20261021.md` shows Wine thread `0024` waiting on auto-reset event `021c`; thread `0088` signals it and the main thread resumes. The main thread later requests `terminate_process(exit_code=1)`, followed by UnityCrashHandler startup. That trace does not establish a lost kernel wakeup. I made no new Gang Beasts fix or probe.
