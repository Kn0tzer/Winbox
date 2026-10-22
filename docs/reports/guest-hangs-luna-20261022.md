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

Evidence:

- Build log: `/tmp/winbox-finish/r3/work-luna-hang/build1.log`
- Probe summary and console: `/tmp/winbox-finish/r3/work-luna-hang/mindustry/summary.json`, `console.log`
- Screenshot: `/tmp/winbox-finish/r3/work-luna-hang/mindustry/mapped-plus-60s.png`

## Native socket safety test

The three failing subtests compiled extracted production snippets against fixtures missing the new guest-loopback fields, helpers, error constants, and receive data types. I added those stubs to the three affected fixture classes. The original assertions are unchanged.

`node --test web/tests/suite/unit/native-socket-safety.test.mjs` passes all six subtests. The repair is committed as `a33f495d8`.

## Gang Beasts handoff

Per the scope change, the Sonnet lane owns further Gang Beasts investigation. The existing trace in `guest-hangs-sol-20261021.md` shows Wine thread `0024` waiting on auto-reset event `021c`; thread `0088` signals it and the main thread resumes. The main thread later requests `terminate_process(exit_code=1)`, followed by UnityCrashHandler startup. That trace does not establish a lost kernel wakeup. I made no new Gang Beasts fix or probe.
