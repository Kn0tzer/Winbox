# Portal and Gang Beasts support — 2026-10-19

Portal takes priority, followed by Gang Beasts. Optimization follows functional
acceptance of both games. The CPU JIT settings rollout is complete and is not
being revalidated here.

## Portal rendering

The current combined interpreter/JIT runtime (`7b4ab076519b`) reaches Portal's
main menu and starts a level. The CPU32 SSE3 repair has fixed the earlier DXVK
allocation overlap: a read-only scan found 95 nonzero offsets among 101 bound
images, rather than almost every texture sharing offset zero. Evidence:
`test-results/portal-resume-20261019/image-bindings.json`.

The live level still had corrupted textures. The captured frame can reproduce
that defect independently of the guest's startup:
`test-results/portal-resume-20261019/scene-frame.json.gz`.
Its source textures decode correctly with an independent DDS decoder.

The SPIR-V pre-pass splits Vulkan combined image/sampler descriptors into separate
WebGPU bindings. The renderer looked up sampling state using the new sampler
binding, which does not exist in the guest descriptor set. Consequently it used
default nearest/clamped sampling throughout the scene. The generated sampler
identifier now preserves the original descriptor binding, including when the
input SPIR-V has no names. Resource scanning carries that binding to descriptor
lookup and bind-group cache fingerprints; the WebGPU binding stays unchanged.

Replaying the same frame restores wall/floor texture detail. The repaired replay
has 163 sampler cache hits, versus zero before the repair, with no rejected
frames. Compare `test-results/portal-scene-replay-20261019/frame.png` and
`test-results/portal-sampler-replay-20261019/frame.png`. This is rendering evidence,
not completion of live gameplay acceptance.

Some live draws also rejected valid odd-length uint16 index streams because
WebGPU mapped buffer sizes must be divisible by four. Allocation now rounds up
while preserving every source byte. The fail-first renderer regression exercises
three uint16 indices and checks both the resulting allocation and contents.

Focused launch, shader translation, buffer replay and OCR checks pass (18 tests);
type checking passes. OCR uses one OpenMP thread so recognizing the menu does
not time out under emulator load. Browser probes match versioned launcher URLs,
and can exercise the ordinary launcher without graphics overlays.

## Remaining acceptance

A normal-launcher Portal run is checking the shared Direct3D installation and
live level rendering/input. Gang Beasts follows Portal; neither game is yet
claimed fully playable by this report.

## Linear and sRGB rendering

The normal-launcher run reaches the menu, chapter selector and Test Chamber 00
with the repaired textures and no rejected frames. Its level screenshot remains
too dim, matching the user's observation. The run's menu-only `pass` flag does
not establish gameplay acceptance.

The renderer previously forced every render target and pipeline attachment to
the canvas format. Portal alternates BGRA linear (44) and sRGB (50) attachment
views of the same image. Losing the sRGB write conversion darkens the scene.
Colour targets now retain their formats, with compatible linear/sRGB views
sharing storage. Pipeline keys include attachment formats; multisample storage
also follows the image identity instead of being shared by dimensions alone.
Presentation converts channels when the guest's BGRA swapchain and browser's
RGBA canvas cannot be copied directly. It adds no brightness adjustment.

The exact captured frame is substantially brighter after this repair, with
zero rejected frames: `portal-srgb-present-replay-20261019/frame.png`. The unit
regression fails before the repair when one image switches from an sRGB write
view to a linear view, and passes afterward.

Sampled descriptors must preserve view formats too. The native emitter now
sends the view format in the existing IMAGE_DATA format field, preserving the
record layout. The decoder retains a format per view, and texture/bind-group
cache keys distinguish aliases of the same image. Native emission and decoder
regressions cover simultaneous linear/sRGB aliases. The focused colour tests
pass (24 tests), as does type checking. Fresh live gameplay verification on the
rebuilt native emitter remains required. The prepared release has not been
activated while this rendering work continues.

## Shared graphics installation

The shared lazy runtime now includes architecture-checked DXVK `d3d9`, `d3d11`
and `dxgi` DLLs in the canonical Wine PE directories, plus the verified 32-bit
and 64-bit Vulkan bridges in the system library directories. The manifest
records each DLL's hash, size and installation path. DXVK's license is included.
The support archive is `support-44165220e3b9…zip` (205,217,398 bytes).

Source launches select native D3D9; Unity launches select native D3D11/DXGI and
new Unity profiles request D3D11 explicitly. Overrides derive from the stored
engine so existing saved uploads also select the installed native libraries.

`portal-launcher-20261019` uses the ordinary Portal demo button, with no graphics
overlay or `LD_LIBRARY_PATH` override. Its log confirms DXVK v2.4.1 and Vulkan32
initialization from the shared installation. Full live acceptance is ongoing.
An optional `SCENARIO=direct3d` launcher dependency probe also uploads only a
small application fixture, without supplying any helper DLLs.

## Live colour verification and mouse capture

`portal-colour-live-20261019` boots the rebuilt emitter into Test Chamber 00
with normal lighting and post-processing. The live descriptor formats include
both BGRA linear and sRGB (44/50). At the saved scene, 1,060 frames rendered,
with zero rejected frames, unknown formats or unsupported operations. Actual
browser mouse movement changes the view, and keyboard movement changes the
camera position. The new self-contained `scene-frame.json.gz` retains the view
formats for further rendering diagnosis. This is not full gameplay acceptance.

The browser cursor remains unlocked during gameplay. Wine's confined
XGrabPointer was stored only as an ordinary grab, so its ClipCursor request
could not enable continuous relative input. The server now retains the confine
window and releases it on ungrab, unmap, destroy and client cleanup. The SDL
main thread synchronizes confined grabs to relative mode; ordinary pointer
capture stays absolute. A busy registry defers synchronization without blocking
the main thread. SDL handles deferred pointer lock and user gesture retries.
The focused native handler and SDL synchronization regressions pass; the rebuilt
browser still needs a live capture and release check.

The next live run (`portal-input-live-20261019`) still reports no confined grab,
so that bridge alone does not establish capture for Portal. Mouse movement and
WASD reach the guest, but the camera repeatedly looks toward the floor/ceiling.
The wire server also discarded XWarpPointer. It now updates the virtual pointer,
emits synthetic core motion without XI raw motion, and resets relative baselines.
Relative SDL movement advances the same position returned by XQueryPointer.
Both SDL sinks protect pointer state against guest-thread warps.

The player toolbar adds **Capture mouse**. After selecting it, clicking inside
the game invokes SDL relative mode within the browser's user gesture. Escape
releases it and clears the manual request. This avoids inferring a capture
request from ordinary cursor moves. Confined guest grabs retain automatic support.
Focused regressions cover warp destinations, source rectangles, recentering and
relative position updates. Live browser verification remains pending on this
rebuilt runtime; the staged release must be refreshed again before activation.

`portal-warp-live-20261019` verifies real browser pointer lock, native relative
mode, release through the control, and recapture. Trusted DOM mouse events carry
deltas while locked. The toolbar wiring and its visible instruction also pass a
small UI fixture. CDP's simulated Escape key did not trigger Chromium's browser
unlock, so that is not counted as an Escape acceptance check.

Mouse-look is still unreliable in the live scene. Further inspection finds that
XQueryPointer and XTranslateCoords assume screen and window coordinates are
identical, while emitted core motion adds the window origin. Cursor warps also
stored root positions directly in the window-relative sink. The corrected paths
share parent-aware origins; root queries add the focused window origin, queried
window coordinates subtract their own origin, and warps convert back into sink
coordinates. A failing native regression reproduces the disagreement with a
window offset by (3,9); it also covers nested window translation and relative
warps. This build still requires live verification before activation.
