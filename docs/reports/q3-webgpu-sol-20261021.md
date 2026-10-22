# Q3 WebGPU investigation, 2026-10-21

Worktree: `/home/ubuntu/boxedwine64-worktrees/winbox-solq3-20261021`.
Evidence root: `/tmp/winbox-finish/r3/work-solq3/`.

Q3 reaches a readable main menu on the default WebGPU backend. The composited
capture `fixed2/mapped-plus-240s.png` shows the title and all menu entries.
This establishes menu rendering, not complete or artifact-free 3D gameplay.

## Established causes

The supplied runtime artifacts did not match the current source. The original
`tools/rootfs64/libgl64/libGL.so.1` exported `glEnableClientState`,
`glVertexPointer`, and `glTexCoordPointer` as one-byte functions. These were
silent no-ops, explaining why the preceding lane saw no enabled arrays despite
adding guest-memory staging. `stale-shim-evidence.txt` records their ELF symbol
sizes; `original-libGL.so.1` preserves the original binary. The new binary
coverage test checks these actual exports in addition to source registration.

The supplied WASM was built from `6626c19e56c9` on October 21 at 03:01 UTC,
before the fixed-function bridge changes. The initial export used those supplied
binaries with the current JS. `baseline/console.log` records `TODO: glShadeModel`,
a WGSL parse error, fallback to WebGL2, and a subsequent `bindTexture` type error.
Both composited baseline screenshots are black. This run is invalid as WebGPU
success evidence. The probe now rejects any logged WebGL2 fallback.

The Doom demo is not a GL reference: `tools/rootfs64/games/build-games.sh`
documents its Doomgeneric Win32 GDI backend.

## Trace and validation

The bridge trace records all 16 ABI slots and matrix payloads. Temporary guest
instrumentation identifies called no-op exports and the unresolved ARB
multitexture functions without changing their baseline rendering behavior.
`decode-trace.py` in the evidence directory decodes the ABI and saves the first
three drawn frames, call counts, and called stubs.

Before the WASM build, the active Emscripten C++ branch compiled with native G++
using temporary declarations for the JS boundary. The ordinary native GL branch
has pre-existing build errors; the active-branch validation avoids those and does
not claim to execute the JS snippets. The focused GL/shader Node tests passed
(15 tests); guest shim coverage passed (5 tests). Generated fixed-function WGSL
passed the real Naga parser for textured/untextured and alpha-test variants.

The first full WASM build used the prescribed two-job command, waited for the
shared build lock, and completed successfully. Its browser export runtime hash
was `a101d892e444`. Subsequent trace findings and visual verification follow.

## First complete frame audit (before rendering fixes)

`trace1/console.log`, `decoded-trace.txt`, `frame-1.txt`, and
`trace-summary.json` contain 454 trapped calls and four complete drawn frames.
The restored client-array wrappers work: every captured draw has arrays `1/1/1`,
stride `16/0/0`, unsigned-32 indices, and guest-memory vertex/color/UV pointers.
The first rectangle spans `(0,0)` to `(1030,755)` in window coordinates.

Dependency-ordered gaps established by the trace and dispatch inspection:

1. `glLoadMatrixf` is a guest no-op (seven calls). Window-pixel vertices therefore
   reach an identity projection instead of Q3's orthographic projection.
2. `glActiveTextureARB` is a guest no-op (two calls, units 1 then 0).
   Q3 disables texture 2D on unit 1 and enables it on unit 0. Emscripten's legacy
   `glEnable`/`glDisable` wrappers additionally swallow texture-enable state
   instead of forwarding it to WebGPUGL's direct fixed-function renderer.
3. A scissored depth clear generates invalid WGSL `Out(depth: ...)`; WGSL
   constructors take positional arguments. This produces the observed WebGL2
   fallback and then incompatible texture-handle errors. The reduced real-Naga
   tests fail for color, depth, and combined scissored clears before the fix.
4. `webGpuFixedDraw` evaluates an IIFE in `EM_ASM_INT` without returning its
   result from the outer JS body. Its successful direct draw is consequently
   followed by a second attempt through the legacy emulator.
5. `glDrawBuffer` is a guest no-op (five calls). The current back-buffer target
   already matches this startup path. Smooth shade and polygon fill are Q3's
   requested defaults, so their no-ops do not block these frames.

The captured path uses RGBA unsigned-byte uploads (RGB/RGBA internal formats),
texture subimages, texture filtering/wrap parameters, modulate texture env,
vertex colors, blend/depth masks, cull-front, viewport and scissor. It does not
call `glBegin`/`glEnd`, display lists, texgen, compiled vertex arrays, fog, or
ARB client-active-texture/multi-coordinate entry points before the clear failure.
No missing feature in those unused areas is claimed as a cause of this frame.


## Implemented fixes and visual result

The guest shim now exposes the existing host ABI for float/double matrix loads,
double matrix multiplication, and byte colors; `glActiveTextureARB` aliases the
real core selector. The checked-in guest binary was rebuilt. Fixed-function
enables/disables bypass the Emscripten emulator, and the direct draw wrapper
returns its result instead of also attempting a legacy draw.

Immediate-mode vertices retain individual colors and use the same WebGPUGL
matrices as array draws. This keeps the GL32 triangle on the direct renderer.
Scissored clears generate positional WGSL constructors. Queued GPU draws
snapshot texture bindings. Odd uint16 index uploads are padded to four bytes
without increasing the draw's index count.

`fixed2` mapped after 43.7 seconds with WASM build `5b6a36d3103a`
(SHA-256 `5b6a36d3103a565154255bb86fad5aeff8cc5f92fd73c037a9b3c4caeb07fdfa`).
The 60-, 120-, and 240-second composited captures are nonblack; the final capture
shows the menu. Its summary reports WebGPU active, no page errors, and no
WebGL2 fallback. Its trace contains 3,682 trapped calls and 33 complete frames;
154 matrix loads now reach the bridge. The only called stub is `glDrawBuffer`,
and the traced argument is `0x405` (`GL_BACK`), matching the existing target.
Neither ARB client-active-texture nor multi-coordinate calls occur in this menu.

That run exposed one recovered GPU error for an odd-sized uint16 index upload.
The alignment fix has a regression that fails before padding and passes after
padding. Final validation uses the cleaned production shim; temporary stub and
ARB-call witnesses have been removed. The probe now rejects recovered GPU draw
errors as well as WebGL2 fallback, and checks host load after queue admission.

## Validation and limits

All 26 focused GL/shader/shim Node checks pass. They include real Naga parsing
of color/depth scissored clears, immediate vertex colors and projection, queued
texture binding identity, odd index upload alignment, and the bundled ELF's
real client-array/matrix exports and x86-64 machine type. Red/green logs and
native compile logs are saved in the evidence root. Two full WASM builds were
used; the last alignment change requires only a JS export.

The post-merge build, GL32 browser gates, and final Q3 captures were verified
on 2026-10-22; results are recorded below. Local implementation commit:
`9cb364001`.

The animated logo has visible artifacts. Normal X11 presentation still reads
the synchronous CPU mirror while WebGPU receives the rendering commands; direct
GPU presentation/readback remains a separate unfinished area. The CPU mirror
is not a complete perspective/depth rasterizer. No claim is made about gameplay,
lightmapped multitexture scenes, texgen, display lists, lighting, fog, stencil,
or other unexercised GL features. This work fixes the traced menu dependencies
rather than claiming a complete desktop GL implementation.

Nothing was pushed or deployed, Docker was not used, and the runtime asset
manifest was not committed.


## Post-merge verification (Luna, 2026-10-22)

Merged `fix/winbox-chromium-20261018` at `e00a6c105` and rebuilt the current
source with the prescribed `wasm64-mt-jit` command. The runtime build ID is
`7954964929f0` (WASM SHA-256
`7954964929f062ff1d531d969cd70596b2611782a18c84b5f0808572743156d9`).

The Q3 probe mapped its first window after 78.2 seconds. Its summary reports
WebGPU active, no WebGPU bridge failures, and no page errors. I inspected the
composited screenshots: `mapped-plus-60s.png` is still black during startup;
`mapped-plus-120s.png` and `mapped-plus-240s.png` show the Quake III Arena main
menu, including the title, menu entries, and copyright line. Evidence is under
`/tmp/winbox-finish/r3/work-luna-q3/evidence/q3/`. Since the later captures are
visible, no additional frame trace was needed.

The pending JS change handles RGBA framebuffer-to-BGRA canvas blits with a
format-converting WebGPU render pass and uses the selected mip level on the
same-format copy path. The focused `gl-webgpu` unit suite passed (3 files), the
`gl-shader-compat` suite passed (1 file), and the GL32 browser suite passed all
three gates: triangle, framebuffer, and WebGPU clear.

The menu is visible, but the animated logo still has rendering artifacts.
Gameplay and other unexercised GL features remain outside this verification.
