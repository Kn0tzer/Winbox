# Runtime regression checkpoint — 2026-10-13

The full browser suite passed all 11 files. The fast suite passed all 104
unit files and the legacy checks (73 passed, one existing skip). Native
checks passed 602 CPU32 cases, 315 CPU64 cases and the six Vulkan bridge
harness groups. Type checking and the browser runtime build passed.

The tested runtime WASM SHA-256 is `d8ad843c953fed7b5dced8322bf535911e4d44e0e3252b734dda7db4d6c20c3f`. Local evidence
is in `test-results/suite-*/`; D3D12 includes `triangle.png` and `pixels.json`.
Its clear and draw/present programs returned zero, and its compositor
screenshot contained 6,090 red-dominant triangle pixels. Input checks
observed all six key-down/key-up pairs and the relative mouse packet,
burst and negative-delta cases in both guest CPU modes. Audio checked actual
64-bit DSP output PCM, including nonzero samples and RMS.

The fixes cover UNIX acceptance/cancellation, fork descriptor snapshots,
audio callbacks outside the registry lock, guest terminal line framing,
32-bit standalone readiness, Vulkan PE proc lookup and WithCount argument
adapters, and pipeline binding before render attachments exist. Native
regressions reproduce the corresponding failures. The first D3D9 allocator
failure and its symbolized fork stack are preserved in the functional
backup directory; subsequent focused and full browser runs passed.

Celeste 1.4.0.0 reached `GAME DISPLAYED` after loading, but its captured canvas
was black. This boot result does not establish rendering or playability.
Interactive Celeste, Portal and Gang Beasts verification and the requested
optimization phase remain outstanding. These tests exercise selected
operations; they do not establish complete coverage of every API or variant.

## Contract checks added after the first checkpoint

The initial browser result was insufficient: guest return codes could pass
while the presented image was black. A new CPU32 D3D9 variant executes the
same four workloads through 32-bit DXVK and checks the actual Vulkan canvas.
Its clear initially produced zero lit pixels despite all four workloads
returning zero. The Vulkan producer discarded render passes with no draws,
including attachment clears used by the subsequent presentation pass.
Preserving those passes produced 286,296 red-dominant pixels. A native
serialization regression fails before that fix and passes afterward;
the Vulkan harness now passes seven groups.

Small guest contracts also reproduced and verified fixes for shared Wine
clock mappings, consecutive poll deadlines, and fullscreen clipped input.
Both CPU modes now advance Win32 clocks through consecutive sleeps and
deliver a clipped 1,500/-1,500 relative motion packet exactly once while
the window message pump remains responsive. XInput2 replies and pointer-grab
ownership have native protocol tests. The Mono fixture exercises a managed
thread, reflection, a file round trip and garbage collection without
loading Celeste's assets. Native CPU64 now passes 318 cases; the fast suite
passes 108 unit files and the unchanged legacy checks. Type checking passes.

`runner.mjs --game=celeste|portal|gangbeasts` selects executable subsystem
prerequisites and records their runtime hashes, missing checks and remaining
game-specific behavior. A passing preflight never sets `fullGameVerified`.
Interactive verification and optimization remain outstanding.

## Startup and FNA checks — 2026-10-14

The first Celeste preflight failed two probes before their guest entry points
ran. Both logs showed the launcher's old manual Wine server handshake
receiving no reply. The launcher reported readiness as soon as that request
was queued, and the host waited on the browser main loop for replies.
Normal 32-bit Wine now executes real PE programs and initializes its own
startup event; that bypass came from an earlier broken runtime.

The launcher now lets Wine initialize its services. Persistent server and
desktop pins are spawned sequentially in both CPU modes, with readiness
remaining false on a spawn failure or timeout. Executable launcher tests
cover sequencing and readiness; the unused manual protocol implementation
was removed after the fullscreen and clock guest probes passed with the
new path. The failed logs and runtime metadata are preserved under
`/home/ubuntu/backups/boxedwine64-functional-20261014/startup-before/`.

A small FNA integration program also passed: texture upload/readback,
SpriteBatch rendering and keyboard input. Its 16,384-pixel red rectangle
moved from center x=103.5 to x=223.5 after ArrowRight. This uses the same
Wine Mono/FNA/SDL2/OpenGL libraries as Celeste without loading game content.
It is now an additional Celeste preflight requirement, rather than evidence
that Celeste itself is playable.

## Memory contracts and isolated probes — 2026-10-14

Repeated browser runs still exposed intermittent GUI startup and a host
allocator crash. Focused D3D9 and input runs passed in isolation. Probes
now default to one emulator at a time, and mouse tests wait for observed
delivery while retaining packet-count and accumulated-motion assertions.
Game preflights select the game's CPU architecture and reject evidence
that lacks that architecture; default input/clock suite runs retain both.

Independent native regressions then reproduced memory faults: an
AddressSanitizer use-after-free when a shared mapping replaces backing
still held by an in-flight access, a stale pointer acquiring a newer cache
generation, fork allocation overwriting inherited reservations, fork
turning shared files into private copies, and anonymous remapping clearing
another process's shared file. The two lifetime/cache checks now pass and
the native CPU64 harness passes 321 cases. Replaced private buffers remain
owned until their page dies, so a cache invalidation cannot expose freed
host memory to an access already underway.

The real Linux fork/mapping fixture passed under QEMU, but initially failed
in the browser with shared=13 instead of the child's written 29. It exposed
a separate syscall shortcut that shared only Wine-named files. The mapping
syscall now follows MAP_SHARED/MAP_PRIVATE for any file. Wine's ordinary
PE image mapping already requests private copies; its
[Wine 8 mapping code](https://github.com/wine-mirror/wine/blob/wine-8.0/dlls/ntdll/unix/virtual.c)
supplies that distinction. The fork-mapping browser fixture and both D3D9
variants subsequently passed with runtime
`1006d7f19a34348225eb82d9f3e149c374b035717d346b331af81a0af9c40286`.
Full-suite repetition and game playability remain separate checks.

## Continuation repairs — 2026-10-14

A fresh full CPU32 run exposed an unaligned locked-increment mismatch:
10,000,001 instead of 10,000,000. Its cold standalone test hung in the native
ARM exception handler. A diagnostic showed an indirect jump to address zero
with an empty per-CPU flag-helper table. A sibling could compile the shared
block first, after which JIT entry skipped helper initialization for the
other CPUs. The fix initializes each CPU before entering shared code and
protects the constructor's helper-table copy against partial initialization.
The targeted regression failed before the fix and passes in production and
test modes. The cold native check passes in one second; the complete CPU32
table passes with zero failures in 187 seconds. Commit: `b7b06e2a`.

The browser audio probe also hit a host allocator crash on its first repeat.
An isolated retry passed, leaving the intermittent failure unexplained.
The isolated WASM sanitizer build booted but progressed too slowly to finish
the audio fixture; that result is inconclusive. A smaller native test then
forced the actual loading-screen label copy to overlap a guest stage update.
AddressSanitizer reported a four-byte write into freed `BStringData` in its
reference-count increment. Label snapshots now acquire the stage writer's
lock, and the progress percentage is atomic. The same forced interleaving
passes with the fix. Three consecutive audio browser runs then passed with
runtime `c89c7870ff0d63934c292a80706ebebdd12b850917d542a82423e5c5c9e6b618`.
Each captured 118 audio callbacks, over 229,000 nonzero PCM samples and RMS
above 0.016. The fork-mapping browser check passed on the same runtime, and
the fixture also passed independently under QEMU/Linux. The rebuilt native
CPU64 harness passed all 321 cases. Full browser-suite repetition remains
outstanding. Commits: `694abc75` (label ownership) and `98c3a8e3` (mapping
contracts).
Failure evidence is preserved under
`/home/ubuntu/backups/boxedwine64-functional-20261014/continuation-boot-label-before/`.

Preflight regressions also exposed missing architecture evidence in the
D3D10/11/12 comprehensive probes and an exit-zero result for an incomplete
game preflight. Those probes now record their actual x64 boot, and missing
prerequisites produce exit status one. The fast suite passes 112 unit files
and 73 legacy checks, with the unchanged legacy skip. Type checking passes.
The D3D11 comprehensive guest also passed after its architecture evidence
fix. Commit: `40d30912`. The ordered game acceptance and performance criteria are in
`tasks/plan.md`; Celeste, Portal and Gang Beasts gameplay remain unverified.

## Framebuffer and x87 repairs — 2026-10-14

The first interactive Celeste attempt drew an animated mountain but no
title or menu. The GL bridge resized framebuffer storage on each viewport
change, collapsed independent read/draw bindings, and presented the last
offscreen target rather than the drawable backbuffer. A small WGL fixture
failed on its split blit before the repair. An additional binding-query
check caught host framebuffer IDs leaking into guest queries.

The bridge now sizes storage from drawable geometry, preserves split
bindings and pixel-pack state, returns guest object names and swaps the
drawable's default framebuffer. These rules follow the Khronos reference
pages for [glViewport](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glViewport.xml),
[glBindFramebuffer](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl4/glBindFramebuffer.xml)
and [glXSwapBuffers](https://raw.githubusercontent.com/KhronosGroup/OpenGL-Refpages/main/gl2.1/xhtml/glXSwapBuffers.xml).
The fixture now captures a 320×240 drawable with exactly 4,800 red blit
pixels and more than 60,000 retained green pixels. FNA, GL32 and all 18
Celeste prerequisites pass on WASM `48f86483f39a`. The real game finishes
loading and renders its title and logo. Commit: `7d53f065`.

The CPU differential harness also contained an invalid FYL2XP1 input
outside Intel's defined domain. Replacing that input with valid vectors
exposed real tiny-value and negative-zero errors in the FPU's `log(1+x)`
calculation. `log1p(x)` retains those inputs. Exact native/QEMU comparisons
pass all 1,560 cases in 167 forms; the native CPU64 harness passes 327
checks, including both FPU storage paths. The rebuilt CPU32 table passes
with zero failures in 192 seconds. The differential CLI now returns
nonzero for a failed reference or wrong result and rebuilds stale binaries.
Its failed-reference control reports every form broken. Commit: `2e2f7e1c`.

The first interaction with the repaired Celeste title stalled platform
input ticks at 23,660, phase 4, with a pending event and no delivered keys.
A targeted test reproduces a blocking connection pump while a guest owns
its I/O lock. The pump now defers busy connections and holds the lock
across delivery and flush. That regression and all 113 fast unit files
pass, as does type checking. Browser and real-game verification of this
change remain pending. This is a visible title, not verified gameplay.
Evidence is preserved under
`/home/ubuntu/backups/boxedwine64-functional-20261014/celeste-input-stall/`.

The next full browser run on `59d71ee62ed7` still hit allocator faults during
audio, D3D10 and x64 keyboard startup. Different allocator callers do not
identify the original corrupting write. The ZIP read/seek regression
forced a concurrent position reset while an actual `FsZipOpenNode` read
was underway. ASan caught a 1,028-byte write into the four-byte destination.
ZIP seek, position queries and reopen now acquire the read mutex. File
mmap and pread also use `KFile::preadNative`, whose position lock is shared
with lseek; the older global file-read mutex did not exclude lseek.
The forced regression passes after the repair. All 114 fast unit files and
327 native CPU64 checks pass. Commit: `b6d2bc2a`. The real startup failures
remain open until browser repetition confirms the rebuilt runtime.

A fast harness check timed out while importing Playwright under concurrent
build load. Its three-second child-exit assertion now starts when the
fixture reports completion, with a separate bounded startup wait. The
deadline-cleanup behavior remains required. Commit: `8277a2b5`.

## Concurrent process teardown — 2026-10-15

The release audio probe reproduced an out-of-bounds allocator failure on
WASM `c79107937d38`. An isolated optimized build with allocator checks also
aborted during loading-screen allocation. The complete ASan audio probe
passed with 240,535 nonzero PCM samples; its changed timing/layout did not
explain the release failure.

A native regression then reproduced duplicate process cleanup during
concurrent final thread exits. The platform loop removed threads before
calling `KProcess::deleteThread`, which removed them again. Multiple exit
paths could see an empty table, close process resources and delete the same
32-bit scratch memory. The exit path now serializes removal and resource
release per process. Both CPU modes delegate cleanup to that path, and the
platform thread count reaches zero only after teardown finishes.

The regression executes the production exit tail and `deleteThread` with
scheduled cleanup work. It fails on duplicate process cleanup before the
repair and passes afterward, including the single shutdown notification.
All 115 fast unit files, 73 legacy checks and type checking pass; the legacy
skip remains unchanged. Three release audio repeats on WASM
`bd004303a645d8864e30d5533da2b5e7ae53f44a3fbf39fb595537828c28819c`
pass, each with 118 callbacks, over 226,000 nonzero samples and RMS above
0.016. The rebuilt native CPU32 table passes all 602 checks in 206 seconds.
The rebuilt native CPU64 harness passes all 327 checks. The full browser
suite remains pending. These repeats do not yet close the intermittent startup failure.

The initial release and allocator-check failures are preserved under
`/home/ubuntu/backups/boxedwine64-functional-20261015/`. Current repetition
artifacts are in `test-results/suite-audio-exit-fix-{1,2,3}/`.

The first complete browser run on that runtime passed 18 of 19 files, with
no allocator crash. The mouse test reported 450 instead of 500 units after
its per-sample wait consumed a late duplicate of the preceding Wine debug
line. A packet sequence now advances each wait; burst boundaries also
exclude late duplicates of reset packets. Reversing the preceding measured
motion avoids assuming an initial host cursor position. The ten-packet and
500-unit assertions remain, as do the burst and negative-motion checks.
The focused browser probe passes both CPU modes, and the fast suite now
passes 116 unit files. A later full browser repetition reproduced an allocator
crash during D3D11 startup and was stopped for investigation. Commits:
`ae31b112` (teardown), `5e978310` (mouse evidence), and `02002af7` (inherited
architecture and presentation checks).


The first teardown repair did not include cleanup inside ordinary exit or
exit_group. Native regressions forced both missed paths to overlap final
executor teardown, reproducing premature memory release. MT ordinary exit
now defers removal until the executor leaves its run loop. exit_group
serializes its cleanup with sibling teardown; the final executor publishes
termination even when both ordinary exits initially observe another thread.
Commit `33edab1e` includes those repairs and an extended fork fixture that
checks the parent's waitpid status after concurrent raw SYS_exit calls.
The fixture passes under Linux/QEMU and in the browser on WASM
`68956b49a713c38b44ae987e616e11949c3cc0f1ed243042fc3b84b286286c5b`.
The rebuilt native tables pass 602 CPU32 and 327 CPU64 cases; all 116 fast
unit files and type checking pass.

A following D3D11 startup returned `D3D11CreateDevice` failure because Wine
could not load its graphics driver; explorer and the desktop pin had failed
to initialize. There was no allocator exception in that run. The launcher
only waited for the desktop pin's PID, allowing later applications to load
while graphics initialization was still underway. Desktop readiness now
requires an acknowledgement after the helper successfully creates a window.
The helper is staged over the pinned prefix during export; all unrelated
archive members remain byte-identical. Startup repetitions and the complete
browser suite are still required. Preserved failures are under
`graphics-6895-before`, `audio-5215-before` and `d3d11-bd004-before` in the
2026-10-15 functional backup directory. No game gameplay or FPS acceptance
has been claimed.


Three consecutive D3D11/audio pairs passed with the desktop gate on the same
`68956b49a713` runtime. D3D11 reported 8,192 lit pixels each time. Audio
captured 224–228 callbacks, at least 247,003 nonzero samples, and RMS above
0.0117. Commit `30f8e18f` records the readiness gate, both rebuilt helpers,
and prefix packaging verification. All 116 unit files, 73 legacy checks,
type checking and the prefix-member/provenance checks pass. The following full browser run reached the GL32 triangle probe, where Wine32
reported that its server never started. It was stopped for diagnosis; game
acceptance remained gated. The failed process dump showed two live server
processes in poll, with both initial Wine clients terminated.


## Filesystem publication — 2026-10-15

Two independent forced interleavings reproduced filesystem faults. A second
thread could observe a directory's loaded flag before its children had been
published. Concurrent O_CREAT calls could also publish separate FsNode
objects for the same native path, giving that path different inode IDs and
independent record-lock state. Directory loading now holds its directory
mutex through publication, and file creation rechecks the namespace under a
shared creation lock. Both regressions fail before their respective repairs
and pass afterward. Temporary server path tracing was removed after six
traced startups passed; that timing change did not reproduce or close the
untraced Wine32 failure. Normal release repetition is required.

Failure evidence and both native regression failures are preserved under
`/home/ubuntu/backups/boxedwine64-functional-20261015/server32-6895-before/`.
The filesystem repair runtime is WASM
`a97994c0725fab35b83d899f2860a9ad50a0aa400a8cc0717bb53a158c3a7731`.
Commit `be66e10b` contains the repairs and both regressions. Native CPU32
passes all 602 cases in 182 seconds, CPU64 passes all 327 cases, and all
118 unit files, 73 legacy checks and type checking pass. Three consecutive
untraced GL32 triangle probes pass on that runtime. A new complete browser
run is in progress; gameplay remains gated on its outcome.


## Complete test gate — 2026-10-15

The fresh browser suite passes all 19 files on WASM `a97994c0725f`.
This includes the previously failing GL32 startup, both input architectures,
D3D9 presentation, D3D10/11/12, actual audio PCM, FNA input/rendering, the
extended fork/mapping fixture, shared memory, Mono and clocks. All 118 unit
files, 73 legacy checks, native CPU32 (602) and CPU64 (327), type checking,
and the seven Vulkan harness groups also pass; the existing legacy skip
remains unchanged. The shader/Vulkan groups were checked earlier in this
continuation; those bridge sources have not changed during these repairs.

The complete report, fast/native logs and game-specific prerequisite reports
are saved under `test-results/acceptance-oct15/`. Each prerequisite report is
explicitly derived from the full suite, requires the game's architecture,
and records the common runtime and browser-code hashes. These reports do
not claim game playability. A separate Celeste browser has started for
interactive level movement and jumping. Portal and Gang Beasts acceptance
and Portal gameplay FPS remain outstanding.
