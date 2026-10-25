# Unowned known-gap coverage

Scope: the audit's `UNOWNED` files (see `tools/known-gaps-audit.mjs`, `OWNERS`) are assigned to this lane for remediation and include the X11 host/window layer, io/fs nodes, UI, util, platform host, `include/`, and `web/*.mjs` other than gl/vk. Vendored `lib/` is excluded (imgui, asmjit, glew, zlib, simde, tiny-process).

The scan also reads `platform/`, `include/`, `scripts/`, `tasks/`, `testdata/`, all of `project/`, and the `tools/buildWine/` patches. No upstream Wine tree is present locally, so those patches are scanned, not excluded.

This register is a marker scan. It finds `unsupported`, `not implemented`, `TODO`, `FIXME`, `kpanic(`, `ENOSYS`, `stub` and similar text in code. Zero hits does **not** mean a feature works. Behavioural gaps with no marker are listed under "Not covered by the scan" below.

## Summary

| Area | Files | Hits | Disposition |
| --- | --- | --- | --- |
| X11 host (`source/x11/`) | 1 | 2 | Medium. The Shape and XWindowEvent aborts are fixed and the animated-cursor loud log is implemented away; remaining hits are a commented-out kpanic and a comment. |
| X11 wire GLX (`source/x11wire/`) | 1 | 1 | High, if the GLX-over-wire path is used by a target build. |
| io / fs nodes (`source/io/`) | 8 | 19 | Low. The one guest-reachable abort (`FsVirtualNode::removeDir`) is fixed. |
| UI (`source/ui/`) | 3 | 3 | Low. The save/quit/error/link-name/stuck-socket TODOs were all dispositioned in round 2; remaining hits are the runOnRestartUI invariant, the rejected-loudly icon format, and an allowlisted layout invariant. |
| Platform host (`platform/`) | 2 | 3 | Thread exception sync now degrades instead of aborting; cursor shapes/animation and the audio-device TODO are dispositioned. Remaining hits are a logged 8-bit screenshot limit, a recorder invariant, and an allowlisted main-thread invariant. |
| softmmu (`source/emulation/softmmu/`) | 4 | 7 | Invariants only, see allowlist. |
| util (`source/util/`) | 6 | 13 | Invariants and vendored code, see allowlist. |
| `web/naga-rt.mjs`, `web/frame-metrics.mjs` | 2 | 5 | Medium for shader versions routed to ANGLE. The other hit is a UI label. |
| `include/`, `source/sdl/main.cpp` | 3 | 5 | Invariants and comments, see allowlist. |

Current scan (round 3, 2026-10-25): 21 UNOWNED files, 44 hits, ALL allowlisted with reasons; the register below is empty. Round 3 dispositioned every registered file: 18 hits were eliminated by implementation or loud rejection in code (fs async hooks, zip kpanics, XFreeColormap, PE32+ icons, BString copy constructor, GLX/comment wording), and 3 files (9 hits) were proven not-a-gap and moved to the allowlist. `--check` fails on any UNOWNED hit that is neither registered nor allowlisted.

Severity: **high** = likely to crash or visibly break a real game, or to drop data silently. **medium** = breaks a feature or a subset of games. **low** = debug-only, cosmetic, or rarely used.

## Gap assessment by game impact

Ranked by severity if reached. "Reached" means the guest calls the path. The table records current implementation and remaining gaps; the work list below is a progress register.

| Category | Where | Severity | What a real game sees | Disposition |
| --- | --- | --- | --- | --- |
| Mode switch (XRandR) | `source/x11/x11common.cpp:3029-3246`, `source/x11/xserver.cpp:676` | high | The emulator exposes the host display size list as XRandR 1.2 modes on one connected primary output, applies valid mode changes, supports rotation 0, reports ranges/resources/CRTC/output data, and queues selected screen/CRTC/output change events. Other rotations fail with `RRSetConfigFailed`. | IMPLEMENTED — `x11-xrandr-set-screen-config.test.mjs`, `x11-xrandr-resources.test.mjs`, `x11-xrandr-events.test.mjs` |
| Pointer grab options | `x11common.cpp:506,510,514` plus wire grab state | high | owner_events/event masks, sync/async pointer modes, relative mode/pointer lock, and per-grab cursor activation/restoration are implemented. Invalid XAllowEvents modes return a protocol BadValue error. | IMPLEMENTED: `xwire-pointer-grab.test.mjs` |
| Relative mouse / pointer lock | `source/x11/xserver.cpp`, `platform/sdl/knativeinputSDL.cpp` | high | SDL pointer-lock xrel/yrel deltas pass through scaling without absolute-coordinate offsets; legacy center-warp mode derives deltas and recenters. Wire grabs enable relative mode and restore it on ungrab. | IMPLEMENTED: `x11-legacy-relative-mouse.test.mjs`, `xwire-pointer-grab.test.mjs` |
| Core drawing entry points | `x11common.cpp` (`QueryTree`, `GetPixel`, `SubImage`, `AddPixel`, `CopyPlane`, `CreateGC` valuemask, `SetDashes`, `SetArcMode`, `FillArc`, `DrawArc`, `SetFillStyle` were kpanics) | high if reached | Each of these Xlib calls aborted the emulator. XSetDashes stores a validated pattern and offset; XDrawable applies OnOffDash and DoubleDash strokes, including background pixels for the latter. `XDrawString` is not in this list: text runs on the 64-bit wire path (`source/x11wire/xwireconnection.cpp`), not the legacy hostcall path. | IMPLEMENTED: `x11-drawable-raster.test.mjs` |
| XMatchVisualInfo | `source/x11/x11common.cpp` | medium, especially for GLX applications | Searches the display's visual/depth list for the requested screen, depth and visual class; writes the XVisualInfo guest structure on a match and returns False otherwise. | IMPLEMENTED: `x11-match-visual-info.test.mjs` |
| XGetWindowAttributes | `source/x11/x11common.cpp` | medium | Returns the 32-bit guest ABI fields for geometry, visual, root, class, gravity/backing, colormap, map state, per-client and all-client event masks, override-redirect and screen; a missing window returns BadWindow. | IMPLEMENTED: `x11-get-window-attributes.test.mjs` |
| XInternAtom | `source/x11/x11common.cpp` | medium | Interns one atom and honors only-if-exists without aborting. | IMPLEMENTED: `x11-intern-atom.test.mjs` |
| XDisplayName | `source/x11/x11common.cpp` | low | Returns an explicit display string unchanged; null returns the configured virtual default `:0`. | IMPLEMENTED: `x11-display-name.test.mjs` |
| XGetDefault resource database | `source/x11/x11common.cpp` | low | No X resource database is loaded; queries warn once and return the Xlib NULL result. | REJECTED-LOUDLY: `x11-xgetdefault-unavailable.test.mjs` |
| XIconifyWindow | `source/x11/x11common.cpp` | low | Unmaps the target, stores Iconic WM_STATE, and returns failure for an invalid window. | IMPLEMENTED: `x11-iconify-window.test.mjs` |
| XSetLocaleModifiers | `source/x11/x11common.cpp` | low | Null queries return a guest-owned empty modifier string; explicit empty lists are accepted, and unsupported nonempty modifier requests warn once and return NULL. | REJECTED-LOUDLY for nonempty modifiers: `x11-locale-modifiers.test.mjs` |
| XSetWMProperties command | `source/x11/x11common.cpp` | low | Encodes argv strings as the NUL-separated `WM_COMMAND` property and deletes it for argc zero. | IMPLEMENTED: `x11-wm-command.test.mjs` |
| Dropped pixel write | `x11common.cpp:1688` (`x11_PutPixel`) | high | `XPutPixel` now writes the pixel into the guest XImage for 8, 16, 24 and 32 bpp. Sub-byte depths and a non-zero xoffset are logged once and not written. | FIXED (test `x11-putpixel-writes.test.mjs`) |
| Fullscreen and window state (EWMH) | `source/x11/xwindow.cpp` state handler; `_NET_ACTIVE_WINDOW` dispatch in `x11common.cpp` | high | Fullscreen add/remove/toggle updates/restores window geometry; above state raises/lowers windows; active-window messages set X input focus; root advertises supported atoms; CWSibling/stack modes reorder sibling windows. `_NET_WM_NAME` and `_NET_WM_ICON` use property storage. | IMPLEMENTED: `x11-ewmh-state.test.mjs` |
| ShapeCombineMask | `source/x11/x11common.cpp:3053` | medium | A call aborts; bitmap masks are not converted into window bounding/input regions. | REJECTED-LOUDLY: this server keeps no shape regions (windows are composited and hit-tested as rectangles) and the Xlib call is void, so the handler logs once (X11 SHAPE: shape requests are ignored; this server keeps windows rectangular) and no-ops instead of the previous kpanic abort. Test: x11-shape-window-event.test.mjs. |
| ShapeCombineRectangles | `source/x11/x11common.cpp:3057` | medium | A call aborts; rectangle operations do not update bounding/clip/input regions. | REJECTED-LOUDLY: same disposition as ShapeCombineMask above; one log-once x11LogShapeIgnored no-op instead of kpanic. Test: x11-shape-window-event.test.mjs. |
| ShapeOffsetShape | `source/x11/x11common.cpp:3061` | low | A call aborts; existing shape regions cannot be moved. | REJECTED-LOUDLY: same disposition as ShapeCombineMask above; one log-once x11LogShapeIgnored no-op instead of kpanic. Test: x11-shape-window-event.test.mjs. |
| Clipping | `source/x11/xdrawable.cpp` (`copyImageData`, `copy`, `plotPixel`, `clipAllows`); `XSetClipMask`/`XSetClipRectangles` in `x11common.cpp` | high | Clip rectangles and depth-one clip-mask pixmaps are applied to pixel writes, XPutImage and XCopyArea; mask origins and unset/out-of-bounds bits suppress drawing. `XSetClipMask` validates drawable/depth and replaces rectangles, and `XSetClipRectangles` replaces the mask. Window shape regions are not part of this row: they are dispositioned with the Shape extension rows below. | IMPLEMENTED: `x11-drawable-raster.test.mjs`, `x11-copy-paths.test.mjs`. The Shape extension this row formerly pointed at is REJECTED-LOUDLY by design (see the ShapeCombine* rows and work item 21: this server keeps no window regions), so no part of this row remains open (round 4 closeout). |
| XWindowEvent | `source/x11/x11common.cpp:3091` | medium | The blocking event retrieval API aborts instead of waiting for a matching window event. | IMPLEMENTED for queued events, REJECTED-LOUDLY for the blocking wait: a queued event matching (window, mask) is removed and returned via DisplayData::findAndRemoveEventByMask (the mask table in source/x11/displaydata.cpp is compiled and run by the test); with nothing queued the call logs once and returns False instead of aborting, because the blocking wait cannot run inside a guest callback. Test: x11-shape-window-event.test.mjs. |
| Unaligned event marshalling | `source/x11/x11common.cpp:740,769` | low | Builds using `UNALIGNED_MEMORY` reach an unfinished event marshal path in XPutBackEvent/XSendEvent. | IMPLEMENTED: the UNALIGNED_MEMORY placeholder branches (a literal oops that could not compile) are gone from XPutBackEvent and XSendEvent; both handlers marshal the guest XEvent through KMemory::memcpy, so guest bytes are copied and never cast to a host pointer and host alignment rules do not apply. The same fix corrected XPutBackEvent to read the event from ARG2 and resolve the display by guest address, exactly as its CALL_2_R shim in tools/x11/X11.c passes them (it previously read ARG5 and looked the display up by internal id, so it could only fail). Tests: x11-send-event.test.mjs. |
| GLX over the X wire | `source/x11wire/xwireconnection.cpp` GLX major opcode handler | high, if the path is used | QueryVersion and query-string requests return protocol-framed values; GLX rendering runs through the direct host bridge. Unknown GLX minors now return BadRequest with a warning instead of corrupting the X reply stream with a fake reply. | IMPLEMENTED for the reply-bearing minors a direct-rendering client can send (QueryVersion 7, QueryExtensionsString 18, QueryServerString 19, ClientInfo 20, CreateWindow 31, DestroyWindow 32 — the GLX_MAJOR_OPCODE case at `source/x11wire/xwireconnection.cpp:1809`); REJECTED-LOUDLY for every other minor: the switch's default arm kwarns and returns a protocol BadRequest, pinned by `xwire-glx-protocol.test.mjs`. GL rendering itself never rides the wire by design — the guest libGL traps directly to the host bridge (`source/opengl/gl64bridge`), which is why GLX is advertised present at QueryExtension — so there is no unimplemented wire-rendering remainder (round 4 closeout). |
| Keyboard text entry | `x11common.cpp` (`XLookupString`, `XmbLookupString`, `XkbTranslateKeysym`, `RefreshKeyboardMapping`) | medium | Key events translate to bounded bytes and keysyms; multibyte lookup reports overflow/status; extra_rtrn reports uncopied bytes; static keymap refresh succeeds. `XLookupString` composes dead-key pairs for the full dead-key range it accepts (grave, acute, circumflex, tilde, macron, breve, dotaccent, diaeresis, abovering, doubleacute, caron, cedilla, ogonek) as UTF-8 using caller-owned compose state, including the dead+space accent-itself rule; unlisted pairs fall back to the base key's own text, as libX11 does for sequences its compose table does not list. Multi-key Compose sequences are an XIM facility (see the IME / XIM row). | IMPLEMENTED: `x11-keyboard-text.test.mjs` (round 4: `x11ComposeLatin` extended from 4 dead keys to all 13 with the standard Latin pairs and dead+space pairs, and its UTF-8 emitter now writes one byte for results below U+0080 instead of a malformed two-byte sequence). IME composition is implemented via the channel in the IME / XIM row below. |
| IME / XIM | `x11common.cpp` (`x11_*IC` / `x11_*IM`) | medium (high for CJK input) | XOpenIM reports the unavailable host method once and returns NULL; XLocaleOfIM does the same and returns NULL. Browser composition reaches guest text events through the committed-text channel in the Disposition (the host XIM itself remains absent by design). | REJECTED-LOUDLY for unavailable host XIM: `x11-ime-unavailable.test.mjs`. Browser IME composition IMPLEMENTED (2026-10-25): the page-to-guest channel this row asked for now exists end to end. The launcher (`project/emscripten/wine64-launcher.js`, `bw64InstallImeCapture`) composes in a hidden textarea focused on canvas pointer press and forwards each finished composition through the `bw64_inject_text` WASM export (`source/sdl/emscripten/wine64session.cpp`, EMSCRIPTEN_KEEPALIVE next to `bw64_xwire_key`) -> `xwireInjectText` (`source/x11wire/xwirepresentSDL.cpp`: strict UTF-8 decode rejecting overlong/surrogate/out-of-range sequences, one `EvText` per code point, host modifiers pinned off) -> `XWireConnection::deliverInputEvents` (`source/x11wire/xwireconnection.cpp`) expands each code point into a KeyPress/KeyRelease pair on the normal input path: static-US-keymap characters use their keycode (Shift state for shifted keysyms; Return/Tab/BackSpace/Delete for commit control characters), and any other character (all CJK) goes through scratch keycode 248, dynamically rebound to the character's Unicode keysym and announced with MappingNotify ahead of the press (core GetKeyboardMapping and the XKB GetMap reply both consult the binding). Cancelled/empty compositions deliver nothing, and plain typing is never intercepted, so the non-IME keyboard path is untouched. Test: `xwire-ime-text.test.mjs` (13 subtests: per-layer wiring pins; planner + scratch binding + UTF-8 decoder compiled verbatim from the production sources with exact-codepoint assertions incl. astral characters; launcher capture driven in a vm with fake DOM; sabotage-verified in the resume lane). Remaining proof is browser-level only: a real OS-IME commit end to end is an integ-gate probe item, not a code gap. Dead-key composition for common Latin pairs is implemented guest-side in `XLookupString` (`x11-keyboard-text.test.mjs`). |
| Synthetic events | `x11common.cpp` (`XSendEvent`) and `xserver.cpp` event-mask dispatch | medium | PointerWindow/InputFocus resolve to live targets; propagate walks ancestors and delivers to the first selecting level. | IMPLEMENTED: `x11-send-event.test.mjs` |
| MIT-SHM | `x11common.cpp` (`XQueryExtension`, `XShmAttach/CreateImage/Detach/PutImage`) | medium | MIT-SHM is explicitly unavailable and unadvertised; direct attach/put calls fail with False or NULL and a rate-limited warning directing clients to XPutImage. | REJECTED-LOUDLY: `x11-mitshm-unavailable.test.mjs` |
| Tiled fill | `source/x11/xdrawable.cpp` `fillRectangle` | medium | A fill with a tile pattern aborts the emulator. | FIXED (tile repeated from the tile origin; test `x11-drawable-raster.test.mjs`) |
| AllocColor writable cells | `source/x11/x11common.cpp:1530` | low | A writable colormap cell requested by XAllocColor is warned about and not allocated with correct write permissions. | IMPLEMENTED: an exact RGB match on a read/write cell shares the cell and keeps COLOR_WRITE (the previous code overwrote the flags with COLOR_ALLOCATED alone, silently revoking the owner XStoreColor), and the scan no longer warns on unrelated writable cells. Test: x11-alloccolor-write.test.mjs. |
| XImage uncommon pixel layouts | `source/x11/x11common.cpp:1773` | low | XPutPixel/XGetPixel/XSubImage/XAddPixel only handle byte-aligned layouts through 32bpp with zero xoffset. | IMPLEMENTED: packed 1/2/4-bit pixels (bitmap_bit_order honored, neighbouring bits preserved) and byte-aligned 8/16/24/32-bit pixels with a non-zero xoffset are read and written by XGetPixel, XPutPixel, XSubImage and XAddPixel. Tests: x11-image-pixels.test.mjs, x11-putpixel-writes.test.mjs. |
| Non-32bpp X raster | `source/x11/xdrawable.cpp` | medium | Raster line/fill/polygon/arc methods used to return success after logging on non-32-bit visuals, silently dropping the drawing. | IMPLEMENTED: drawable storage is packed at the visual bits per pixel (32 for the 24/32-bit visuals, 16, 8), and `plotPixel`, `fillColor`, `copy`, `copyPlane` and the `copyImageData` slow path now read/write stored pixels at that depth (`readStoredPixel`/`writeStoredPixel`, GC function masked to the pixel width); the five bail-outs are gone and 16-bit tiles repeat. Tests: `x11-drawable-raster.test.mjs` (16/8-bit line, fill, polygon, arc, tile, GXset/GXxor masking), `x11-copy-paths.test.mjs`, `x11-copyplane.test.mjs`, `x11-arc-drawing.test.mjs`. |
| Other X11 input/cursor gaps | `x11common.cpp` (`XISelectEvents`, `CursorImageLoadCursor`, `CursorLibraryLoadCursor`) | low | XI2 masks support the protocol's 33 event bits and multiple device masks (folded onto the single virtual pointer), rejecting only malformed/out-of-range masks with `BadValue`; custom Xcursor images create host cursors. Unknown library names warn and return a fallback cursor. Animated frame scheduling is implemented (see the Animated Xcursor frames row). XFixes is not advertised on either client path and has no hostcall entry points, so no guest can select it (see Disposition). | IMPLEMENTED: `x11-xinput-cursor.test.mjs`. XFixes cursor notification: NOT-A-GAP (round 4) — the wire QueryExtension handler returns present=false for every extension name except GLX, XKEYBOARD, XInputExtension and RANDR (`source/x11wire/xwireconnection.cpp`, X_QueryExtension case), the legacy hostcall layer implements no XFixes functions and `tools/x11/X11.c` declares no XFixes symbols, so libX11 reports the extension missing and guests never issue its requests; cursor state is emulator-driven instead (`XDefineCursor` -> `XServer::updateCursor`, `source/x11/x11common.cpp:2022-2031`) and animated frames are scheduled by the Animated Xcursor frames implementation. No guest-reachable XFixes remainder exists. |
| Audio capture input | `platform/sdl/knativeaudio.cpp` plus guest device support | medium for voice/chat; low for most games | No guest capture endpoint feeds Wine input audio. | NEEDS_KERNEL_DEPENDENCY (kernel + platform; sharpened round 4): the guest has no capture path at all — `DevDsp` answers SNDCTL_DSP_GETISPACE with an empty buffer under a 'no capture' comment (`source/kernel/devs/devdsp.cpp:251`), and the host layer has no capture entry point (`platform/sdl/knativeaudio.cpp` is output-only; SDL audio capture is never opened). Deliverable: (1) a recording path in DevDsp — GETISPACE/GETIPTR report real buffered frames and read() returns captured samples instead of silence — fed by (2) a `KNativeAudio` capture interface in `platform/sdl/knativeaudio.cpp` that the SDL/browser layer can push microphone frames into, mirroring the output queue. Proving test: a guest probe opens the DSP for recording, a known frame pattern is injected through the new host interface, and the guest reads the identical samples back; Wine-visible effect: the waveIn device stops presenting as permanently silent. |
| Gamepad / XInput | `platform/sdl/knativeinputSDL.cpp`, `source/sdl/`, guest input devices | high for controller-first games | Browser Gamepad API events cannot reach Wine DirectInput/XInput because no joystick device/event path exists. | NEEDS_KERNEL_DEPENDENCY: expose joystick events in `source/kernel/devs/devinput.cpp`, then poll and translate them through SDL. |
| DNS resolver | kernel guest networking resolver path | high for online games | TCP/UDP loopback and relay transports pass, but host-name resolution lacks deterministic guest coverage. | NEEDS_OTHER_LANE (kernel; sharpened round 4): nothing in the kernel resolves names for the guest — the only gethostbyname in the tree resolves the HOST's own name for `ipAddress()` (`source/kernel/knativesocket.cpp:2682-2704`), and no code intercepts or answers UDP port 53, so a guest resolver depends on raw nameserver egress the browser relay does not deterministically provide. Deliverable: a resolution bridge in the guest socket layer — intercept DNS queries (UDP/53) in the KNativeSocket send path and answer them from host/browser resolution, or expose a virtual resolver the guest's resolv.conf can point at. Expected behaviour: getaddrinfo for a resolvable name returns the address inside the guest; an unresolvable name returns the resolver's NXDOMAIN/SERVFAIL, not a hang or timeout. Proving test: a deterministic guest probe resolving one well-known name and one `.invalid` name and asserting both outcomes. |
| Guest common file dialogs | `source/ui/`, browser import pipeline | medium for games using Save/Open dialogs | Browser folder selection and file import exist, but a guest Windows common dialog was not exercised end-to-end. | IMPLEMENTED (probe): `file-dialog-picker-pipeline.test.mjs` drives every emulator-owned step after the picker gesture with a fake FileSystemDirectoryHandle tree — flattened publication, CRC reuse for unchanged files, override precedence, case-collision rejection, vanished-folder error mapping, `openDirectPackage`/`packageRecord` — and pins the `showDirectoryPicker` call in `web/app.mjs` that the pipeline consumes. The picker UI itself is browser-owned and requires a user gesture, so no part of it is emulated or automatable. |
| Host-backed case and symlink semantics | `source/io/fsnode.cpp`, `web/files.mjs` | medium | Virtual Wine aliases are tested, but host-backed lookup and imported symlink/case collision behavior had no native cross-platform probe. | IMPLEMENTED (probe): `host-case-symlink-probe.test.mjs` records the host facts on the real filesystem (case variants coexist with distinct contents on this host; a symlink reads through to its target) and exercises the import contract — `checkEntries` refuses to publish both case variants into the case-insensitive guest and rejects file/directory prefix collisions, and a picked symlink is published under the link name with the target bytes (what browsers hand the page). Guest-side VFS case folding stays covered by `fs-node-case-symlink.test.mjs`. |
| Timer expiration | kernel timerfd/setitimer implementation | medium | Syscall entry points exist, but timerfd/setitimer expiration delivery has no deterministic regression probe. | NEEDS_OTHER_LANE (kernel; sharpened round 4): the implementations exist — timerfd as `KTimer` objects behind `KProcess::timerfd_create/settime/gettime` (`source/kernel/kprocess.cpp:3516-3590`, syscall wrappers `source/kernel/syscall.cpp:1701-1725`), setitimer ITIMER_REAL on `KProcess::timer` (`source/kernel/kprocess.cpp:1792`; non-REAL kinds return `-K_EINVAL` under BW64_RECOVER and kpanic otherwise) — but expiration DELIVERY has no deterministic regression probe anywhere. Deliverable: native probes proving (1) a one-shot 10 ms timerfd becomes readable and read() returns exactly 1 expiration, (2) a periodic timerfd accumulates expiration counts, (3) setitimer ITIMER_REAL delivers SIGALRM to a guest handler. Until those land, expiration behaviour is unverified, not known-broken. |
| Animated Xcursor frames | `source/x11/x11common.cpp` (`x11_CursorImagesLoadCursor`), `platform/sdl/knativescreenSDL.cpp` | low | Animated cursors used to show only their first frame. | IMPLEMENTED: the loader copies every frame (pixels + guest delay) into `XCursor::animFrames` before the guest can free its images, and `KNativeScreenSDL::setCursor` builds one SDL cursor per frame and cycles them on an SDL timer by their delays (10ms floor, SetCursor dispatched to the main thread), stopping when a static cursor or shape takes over. Test: `x11-animated-cursor.test.mjs` (slice-compiled schedule wrap/clamp, loader and setCursor wiring pinned). |
| Shader versions routed to ANGLE | `web/naga-rt.mjs:70,283-286` | medium | Unsupported GLSL is classified as `unsupported-version`, excluded from Naga failures, and the wrapper always passes the exact original source to ANGLE. Probe: `naga-angle-fallback.test.mjs`. ANGLE remains responsible for syntax validity. | IMPLEMENTED: original call-through is behavior-tested. |
| Shader cache warm poisoned record | `web/shader.mjs` (warmPersistentShaders), `web/shader-cache.mjs` (remove) | medium | One corrupt cached WGSL record aborted warming of every remaining shader on every launch: a single try/catch wrapped the manifest read and the whole warm loop, failures were swallowed with no log, counter or eviction, and the bad record was retried forever. | IMPLEMENTED: each shader warms in isolation; a throw or an error-level compilation message logs shader warm: evicting cached shader, counts in the returned warmed/failed result, drops the in-memory copy, and evicts the record plus its manifest references via persistentShaderCache.remove, so later launches skip it; records after the poisoned one still warm. Test: shader-cache.test.mjs (poisoned cache entry). |
| Thread exception sync (host) | `platform/linux/platformThreads-x64.cpp` (`syncFromException`) | medium | An unexpected cached-register value in the host exception sync used to abort the emulator (kpanic). | IMPLEMENTED: both default arms now log via kwarn_fmt and leave the one guest register stale; the caller then delivers the fault to the guest through the existing signal path. The only regCache/xmmCache tables compiled under BOXEDWINE_64 (`jitX86CodeGen.cpp`, the `#if 0` table excluded) hold exactly the handled values {0,1,2,3,10,5,6,7}/{0..7}, and 0xFF (not-cached) slots were already skipped silently. Test: `platform-thread-exception.test.mjs` slice-compiles syncFromException and runs the full tables, an unexpected value in each cache, and the 0xFF skip. |
| Directory rmdir on virtual nodes | `source/io/fsvirtualnode.cpp:86-89` (was kpanic `removeDir not implemented`) | medium (before the fix) | Guest `rmdir` of a virtual directory aborted the emulator (`kprocess.cpp:1204-1211` calls `node->removeDir()`). | FIXED: logs and returns `-EPERM`. Test: `web/tests/suite/unit/fs-virtual-removedir.test.mjs`. |
| Virtual `setTimes` | `source/io/fsvirtualnode.cpp` | low | Virtual nodes retain atime/mtime in milliseconds, preserve `UTIME_OMIT`, and expose updated mtime to stat. | IMPLEMENTED: `fs-virtual-settimes.test.mjs` |
| memfd nodes | `source/io/fsmemnode.cpp:28,33,46,59,64` (kpanic on rename, remove, open, removeDir, setTimes) | none for now | Unreachable: memfd nodes never enter the path namespace (see allowlist). Becomes reachable if `/proc/self/fd` is added. | ALLOWLISTED (work item 16) |
| Zip-backed write and truncate | `source/io/fszipopennode.cpp:43,236` (kpanic) | low | Unreachable from guest code: writable fds are rejected before `setLength` and `writeNative` (`kprocess.cpp:1680`), and write-open moves the file first (`fsfilenode.cpp:192`). | NOT_A_GAP for the write path; invariant kept |
| Async and event hooks on fs nodes | fs open nodes (`source/io/fs*opennode.*`); kernel inotify `syscall.cpp:1498,2272-2273` | medium | The fs-node side is DONE (round 3): `setAsync` tracks the O_ASYNC flag in `FsOpenNode` so F_GETFL is truthful, enabling it warns once per process that SIGIO delivery is unavailable, and `waitForEvents` signals the poll condition when the requested readiness already holds. What remains is kernel-only: inotify init is ignored and add/remove watch syscalls are absent, so no watch events or SIGIO can be delivered yet. | PARTIALLY CLOSED: fs hooks IMPLEMENTED (`fs-opennode-async.test.mjs`). NEEDS_OTHER_LANE (kernel): `source/kernel/syscall.cpp:1498,2272-2273` implement inotify init/add/remove watch APIs and SIGIO delivery to O_ASYNC owners. |
| Audio output device | `platform/sdl/knativeaudio.cpp` (`KNativeAudio::createNativeAudio`) | low | Audio always uses the default device. | NOT-A-GAP: there is no selection to make at this layer. `KNativeAudio::init()` registers exactly one backend per platform (SDL here; CoreAudio under BOXEDWINE_CORE_AUDIO, the Windows driver under BOXEDWINE_MSVC — each platform's init registers its single backend), so `availableAudio` always holds one entry and `createNativeAudio` returning `front()` is the only possible result; its sole caller is the guest MIDI sequencer (`source/kernel/devs/devsequencer.cpp`). Guest-visible device choice happens inside Wine against the single host device the backend exposes. The TODO is removed and the invariant is recorded in the code and pinned by `host-ui-paths.test.mjs`. |
| CPU frequency reporting | `platform/linux/platform.cpp` | low | Linux reads host cpufreq sysfs in kHz and falls back to `/proc/cpuinfo`; browser builds report a nominal 1000 MHz when host procfs/sysfs are unavailable. Per-CPU current/max nodes use scaling sysfs and API fallback. | IMPLEMENTED: `platform-cpu-frequency.test.mjs` |
| Cursor shapes | `platform/sdl/knativescreenSDL.cpp` (`setCursor`, `x11ShapeToSdlSystemCursor`) | low | Some X cursor shapes map to the nearest SDL system cursor. | IMPLEMENTED: the three :TODO: approximations are now deliberate, documented closest-shape mappings — XC_center_ptr (22) and XC_question_arrow (92) to SDL ARROW (SDL has no centred-arrow or question-arrow system cursor), XC_icon (64) to SDL WAIT (no boxed-glyph system cursor) — and the shared shape table now covers them plus left_ptr_watch, so `setCursor` and the wire path (`setCursorByX11Shape`) agree. Test: `x11-animated-cursor.test.mjs` slice-compiles the full table and asserts every mapping including the fallback. |
| UI save and error paths | `source/ui/mainui.cpp`; `source/ui/controls/optionsView.cpp`; `source/ui/data/globalSettings.cpp`; `source/ui/data/boxedContainer.cpp`; `source/ui/data/appFile.cpp`; `source/ui/controls/downloadDlg.cpp`; `source/util/networkutils.cpp` | low | Settings not saved on focus loss, quit not checked against a failed save, error messages missing, a link name not parsed, a download socket with no stuck-socket handling. None is game-facing. | IMPLEMENTED: SDL_QUIT now cancels the quit when `saveChanges()` fails (the gate gotoView always applied), and the save-location field needs no focus-loss tracking because every exit path runs that gate (recorded in optionsView.cpp); the empty file-list branch shows the ERROR_NO_FILE_LIST dialog; the two globalSettings silent paths log via kwarn_fmt; desktop link names strip the `.lnk` extension; runOptions records why no dependency pass is needed (all options apply in one pass before any runner); a stuck download socket is bounded by a curl low-speed limit (1 B/s for 120s) in `downloadFile`, which also makes Cancel effective. Tests: `host-ui-paths.test.mjs`, `bstring-remove.test.mjs`. |
| Icon format | `source/ui/utils/readIcons.cpp:646` (`Icon 20b not implemented`, logged with `kwarn`) | low | One icon format is not shown. Loud and UI-only. | REJECTED-LOUDLY: PE32+ (0x20b) icon resources warn via kwarn (Icon 20b not implemented, greppable) and the loader returns no icon instead of guessing a format; native-UI only, no guest path reaches it. |
| bstring copy | `source/util/bstring.cpp:570` (TODO) | low | Performance note only. | NOT-A-GAP: the marker is a performance note only (maybe do not make a copy, just a ref); BString::substr at source/util/bstring.cpp:570-582 clamps an out-of-range begin and length and returns a correct copy on every path, so no behavior is missing. |

### Not covered by the scan

These categories were requested for the game-impact review. The scan found no marker for them in scope, so this lane does not count them as covered. Each needs a behavioural probe before anyone states a status.

| Category | Scan result | Status |
| --- | --- | --- |
| Audio output and input | Output is implemented by SDL queued audio or the browser Web Audio ring; `audio-audible-output.test.mjs` exercises output conversion and bridge delivery. No capture/microphone input path was found. | Output: tested. Input: NEEDS_KERNEL_DEPENDENCY — no guest capture device exists to feed Wine audio capture (`source/kernel/devs/devdsp.cpp:251` reports an empty capture buffer; `platform/sdl/knativeaudio.cpp` has no capture entry point); exact deliverable in the Audio capture input row above. |
| Gamepad / XInput | No SDL game-controller/joystick event handling or guest joystick device is present; `/dev/input/event3` and `event4` are mouse and keyboard only (`source/sdl/startupArgs.cpp:152-154`, boot lane owned). | OPEN — NEEDS_KERNEL_DEPENDENCY, re-checked 2026-10-25 against the kernel worktree at 7f6156b43 (its handoff wip commit): `source/kernel/devs/devinput.cpp` still exposes only `/dev/input/mice`, `event3` (touch/mouse) and `event4` (keyboard), kernel.md has no joystick row, and no kernel commit has landed since. Exact kernel deliverable this row waits on: (1) a guest joystick device in devinput.cpp (a `/dev/input/js*` and/or event node) with a poll/read event queue carrying buttons and axes that Wine's DirectInput/XInput can enumerate, and (2) a host-feed entry point the SDL layer can push controller state into. On that landing, this lane wires SDL_GameController polling/translation in `platform/sdl/knativeinputSDL.cpp` and adds the probe. Not dispositioned not-a-gap: the dependency is real and still missing. |
| Window, cursor, fullscreen, pointer lock | Markers in the table above (mode switch, relative mouse, EWMH, grab). | Assessed (round 4): every marker in this category is dispositioned in the table above — mode switch IMPLEMENTED (XRandR rows), relative mouse / pointer lock IMPLEMENTED, EWMH / fullscreen IMPLEMENTED, pointer grab IMPLEMENTED, cursor shapes IMPLEMENTED, animated cursor frames IMPLEMENTED. The former 'Partly assessed' status was stale. |
| Clipboard (X selections) | `source/x11wire/xwireconnection.cpp` selection conversion and `web/clipboard-bridge.mjs` | IMPLEMENTED: browser copy reads the guest selection bridge; paste stages host text for X selection conversion. `clipboard-bridge.test.mjs` covers both directions; `xwireconnection.cpp` applies TARGETS, UTF8_STRING, STRING and TEXT conversions. |
| IME | See the IME row above. | IMPLEMENTED: browser composition reaches the guest as X key events via the `bw64_inject_text` channel (`xwire-ime-text.test.mjs`); host XIM stays rejected-loudly; real-OS-IME browser proof is a gate item. |
| Networking sockets and DNS | Native TCP/UDP and browser TCP relay probes exist (`loopback-tcp-relay`, `loopback-datagram-msg`, `browser-network`). | Socket transport: tested. DNS: NEEDS_OTHER_LANE (kernel) — no in-kernel resolver exists (the only gethostbyname resolves the host's own name, `source/kernel/knativesocket.cpp:2694`); exact deliverable and probe in the DNS resolver row above. |
| File dialogs | Browser folder selection uses `showDirectoryPicker`; import accepts file and directory entries through the browser picker/drop pipeline. | IMPLEMENTED (probe): `file-dialog-picker-pipeline.test.mjs` — see the Guest common file dialogs row above. |
| Filesystem semantics (case-insensitivity, symlinks) | ZIP import rejects archive symlinks (`web/files.mjs`); VFS decodes Wine `.link` and `(_colon_)` drive aliases (`source/io/fsnode.cpp`, `source/io/fsopennode.cpp`); lookup can ignore case. | VFS case folding and Wine alias lookup: `fs-node-case-symlink.test.mjs`. Host-backed case/symlink behaviour: `host-case-symlink-probe.test.mjs` — see the Host-backed case and symlink semantics row above. |
| Timers | Linux timer syscalls include nanosleep, setitimer, timerfd, and clock_gettime; browser and native scheduler use host timer callbacks. | NEEDS_OTHER_LANE (kernel): implementations exist (`source/kernel/kprocess.cpp:3516` timerfd/KTimer, `:1792` setitimer); the missing half is deterministic expiration probes — exact spec in the Timer expiration row above. |

The scanner now matches case variants of `not supported` and `not implemented`, and catches silent `ENOSYS` returns. It only treats logging as loud when a log call appears on the same source line; cross-line log/error control flow still needs manual review.

## Proposed work items

Ranked by severity if reached, then by how likely a real game is to reach the path. Items are updated as the lane implements them; remaining partial items name what still needs work.

1. **XRandR family (`x11common.cpp:3029-3246`, `xserver.cpp:676`).** DONE. XRandR 1.2 exposes the host size list, current one-output topology, CRTC/output info, size range, primary output, mode changes and screen/CRTC/output notifications. The emulated output supports only rotation 0 and rejects unsupported rotations/modes. Regression probes: `x11-xrandr-set-screen-config.test.mjs`, `x11-xrandr-resources.test.mjs`, and `x11-xrandr-events.test.mjs`.
2. **Pointer grab and relative mouse (`x11common.cpp:506,510,514`, `xserver.cpp`, `knativeinputSDL.cpp`).** IMPLEMENTED: owner_events and event masks route correctly; synchronous pointer grabs freeze and release one event per SyncPointer; async modes drain the pending queue; invalid XAllowEvents values return BadValue; relative SDL deltas feed XI2 raw motion without recentering; confined grabs request browser pointer lock; per-grab cursor switches on the SDL main thread and restores on ungrab. Tests: `xwire-pointer-grab.test.mjs`, `x11-legacy-relative-mouse.test.mjs`.
3. **Core drawing entry points (the list in the table).** DONE: every listed kpanic and dashed strokes are implemented. Regression probe `x11-drawable-raster.test.mjs` executes the raster code and verifies OnOffDash/DoubleDash pattern phase, foreground/background pixels, and existing clip/fill behavior.
4. **`XPutPixel` write (`x11common.cpp:1688`).** DONE: the pixel is written into the guest XImage for 8, 16, 24 and 32 bpp in either byte order. Sub-byte depths and a non-zero xoffset are logged once and not written. See Fixes.
5. **EWMH state and stacking (`xwindow.cpp:610,1032`).** IMPLEMENTED: state add/remove/toggle deduplicates atoms; fullscreen resizes to the root and restores prior geometry; `_NET_ACTIVE_WINDOW` updates X focus; `_NET_SUPPORTED` advertises fullscreen, above, active, name and icon support; above changes and CWSibling stack requests reorder windows. Test: `x11-ewmh-state.test.mjs`.
6. **Clip regions (`xdrawable.cpp`).** IMPLEMENTED: clip rectangles and depth-one pixmap masks gate raster, XPutImage, and XCopyArea writes with clip origins; setter validation, copy behavior, and mutual replacement semantics are tested by `x11-drawable-raster.test.mjs` and `x11-copy-paths.test.mjs`. Shape extension window regions are not part of this item: they are REJECTED-LOUDLY by design (work item 21).
7. **GLX over the X wire (`xwireconnection.cpp`).** CLOSED (round 4): IMPLEMENTED for QueryVersion, the string queries, ClientInfo and Create/DestroyWindow drawable mappings; REJECTED-LOUDLY for every other GLX minor (the default arm warns and returns BadRequest instead of a fabricated reply). GL rendering remains on the direct host path by design — the wire never carries it. Test: `xwire-glx-protocol.test.mjs`.
8. **Keyboard text entry (`x11common.cpp:777,806,847,940`).** IMPLEMENTED: lookup handlers return bounded text/keysyms, status-out, overflow and extra byte counts; refresh succeeds with the fixed browser map. Dead-key composition covers all 13 dead keys with the standard Latin pairs and the dead+space rule (`x11ComposeLatin`); multi-key Compose sequences belong to XIM (work item 9). Test: `x11-keyboard-text.test.mjs`.
9. **XIM / IC family (`x11common.cpp:2372-2460`).** REJECTED-LOUDLY when no host XIM exists: XOpenIM and XLocaleOfIM warn once and return NULL without aborting. Test: `x11-ime-unavailable.test.mjs`. Browser IME composition is connected via the launcher's committed-text channel (`bw64_inject_text` -> XWire EvText -> X key events; see the IME / XIM row, test `xwire-ime-text.test.mjs`).
10. **Synthetic events (`x11common.cpp:717,720,755`).** IMPLEMENTED: special target windows resolve through pointer/focus state; propagated events walk to the first ancestor with a selecting client. `iterateEventMask` now reports whether delivery occurred. Test: `x11-send-event.test.mjs`.
11. **MIT-SHM (`x11common.cpp:2562-2575`).** REJECTED-LOUDLY: extension query reports unavailable, and direct hostcalls return False/NULL with a rate-limited warning because shared mappings are not exposed. Clients fall back to XPutImage. Test: `x11-mitshm-unavailable.test.mjs`.
12. **XI2 masks and Xcursor images (`x11common.cpp:3442,3547`).** IMPLEMENTED: XI2 event mask bytes preserve bit 32 and malformed bits return BadValue; `XcursorImageLoadCursor` creates a native cursor from guest pixels; animated frame scheduling landed with the Animated Xcursor frames row (SDL timer in `KNativeScreenSDL::setCursor`). XFixes cursor notification is NOT-A-GAP (extension unadvertised on both paths, no shim, no hostcall — see the Other X11 input/cursor gaps row). Test: `x11-xinput-cursor.test.mjs`.
12. **Tiled fill (`xdrawable.cpp:216`).** Impact: medium-low. Approach: repeat the tile pixmap across the fill rectangle, offset by the tile origin. Test: a 2x2 tile over a 5x3 rectangle, against a reference.
14. **Naga to ANGLE fallback (`web/naga-rt.mjs:70,283-286`).** IMPLEMENTED: a node probe drives an unsupported-version shader through the installed WebGL wrapper, verifies the original ANGLE method receives the identical shader and returns normally, and checks the unsupported observation record without marking the lane broken. Test: `naga-angle-fallback.test.mjs`.
15. **Host thread exception sync (`platformThreads-x64.cpp`).** DONE (round 2): the unexpected-state arms log and leave the guest register stale, and the existing signal path delivers the fault; no kernel-lane coordination was needed because the compiled cache tables cannot produce other values (see the register row). Test: `platform-thread-exception.test.mjs` (injected exception-state fixture, as proposed).
16. **memfd `FsMemNode` (`fsmemnode.cpp`).** Allowlisted as unreachable. If `/proc/self/fd` is added, the rename, remove, open, removeDir and setTimes paths become reachable and must be implemented first. Test: the allowlist reason is the guard.
17. **Async fs events (the io group above).** Fs-node hooks are implemented and tested here (round 3: O_ASYNC flag tracking + readiness signalling in `setAsync`/`waitForEvents`, see Fixes applied). The remaining dependency is kernel-only: inotify is currently ignored (`source/kernel/syscall.cpp:1498`), add/remove watch syscall slots are null (`:2272-2273`), and there is no SIGIO delivery to O_ASYNC owners. Kernel lane: expose watch registration, event queue delivery and SIGIO ownership; the fs nodes are then ready to be notified.
18. **CPU frequency (`platform.cpp:130,134,138`).** IMPLEMENTED: native Linux reads cpufreq sysfs then `/proc/cpuinfo`; browser builds use a nominal 1000MHz fallback when host files are absent. Per-CPU scaling nodes fall back to base frequency. Test: `platform-cpu-frequency.test.mjs`.

19. **Legacy Xlib query/property helpers (`x11common.cpp`). IMPLEMENTED: XGetWindowAttributes returns the 92-byte guest structure and all-client masks; XInternAtom honors only-if-exists; XDisplayName returns the explicit name or `:0`; XSetWMProperties serializes WM_COMMAND; XIconifyWindow updates WM_STATE. Tests: `x11-get-window-attributes.test.mjs`, `x11-intern-atom.test.mjs`, `x11-display-name.test.mjs`, `x11-wm-command.test.mjs`, `x11-iconify-window.test.mjs`.
20. **Locale and X resources (`x11common.cpp`).** XSetLocaleModifiers now returns valid guest pointers for the empty modifier string and loudly rejects unsupported nonempty modifiers. XGetDefault warns and returns NULL because no X resource database is loaded. Tests: `x11-locale-modifiers.test.mjs`, `x11-xgetdefault-unavailable.test.mjs`.
21. **Shape extension (`x11common.cpp:3053-3061`).** DISPOSITIONED (round 1): REJECTED-LOUDLY — this server keeps no shape regions (windows are composited and hit-tested as rectangles) and the Xlib calls are void, so the handlers log once and no-op instead of the previous kpanic abort. Test: `x11-shape-window-event.test.mjs`. Implementing real regions would contradict the compositor's rectangle model for no reachable guest benefit.
22. **Blocking XWindowEvent (`x11common.cpp:3091`).** DISPOSITIONED (round 1): IMPLEMENTED for queued events (mask-matched removal via `DisplayData::findAndRemoveEventByMask`), REJECTED-LOUDLY for the blocking wait — with nothing queued the call logs once and returns False instead of aborting, because a blocking wait cannot run inside a guest callback in the browser event pump. Test: `x11-shape-window-event.test.mjs`.

## NEEDS_OTHER_LANE

- **Kernel (RESOLVED round 4 — premise confirmed in code, no kernel work needed)**: the `source/emulation/softmmu/kmemory_soft.cpp:157` kpanic on a non-page-aligned mapped-file offset is unreachable from either syscall path. 32-bit: `KMemory::mmap` (`source/kernel/kmemory.cpp:112`) returns `-K_EINVAL` when `(off & 0xFFF) != 0` on a file mapping, before `allocPages` can be reached, and anonymous mappings pass offset 0. 64-bit: `sys_mmap64_file` (`source/kernel/syscall64.cpp`) computes `fileBase = offset & ~0xFFFULL` and preserves the partial-page head before mapping. The allowlist premise is proven, not assumed.
- **Kernel (RESOLVED round 2)**: `platform/linux/platformThreads-x64.cpp:146,187` needed no kernel-lane coordination — see work item 15: the only compiled regCache/xmmCache tables hold exactly the handled values, and the unexpected-state arms log and leave the guest register stale (`platform-thread-exception.test.mjs`).

## Fixes applied

- **`source/io/fsvirtualnode.cpp:86-89`** (`FsVirtualNode::removeDir`): a guest `rmdir` of a virtual directory aborted the emulator. It now logs the attempt and returns `-K_EPERM`. Virtual directories are generated, so the guest cannot remove one, and `EPERM` is the errno for an operation the node does not permit. This is a choice, not a copy of a host result; the host was not probed. Regression: `web/tests/suite/unit/fs-virtual-removedir.test.mjs`, which compiles the real function body against stub `kpanic` and `klog` and checks the return value, the log count, and that no panic is reached. The test fails on the original code and passes with the fix. The register entry drops from 2 (medium) to 1 (low, `setTimes`).
- **`source/x11/x11common.cpp:1688`** (`x11_PutPixel`): `XPutPixel` edits the image in client memory, so the handler reads the guest XImage header and writes the pixel bytes at `data + bytes_per_line * y + x * bytes`. It writes 8, 16, 24 and 32 bpp, most significant byte first for `MSBFirst`. A pixel outside the image is ignored and the call returns 0. Sub-byte depths and a non-zero `xoffset` are logged once per process and not written. The return value of 1 for a written pixel is taken from libX11's convention as remembered, not checked against a host libX11 here. Regression: `web/tests/suite/unit/x11-putpixel-writes.test.mjs`, which compiles the real body against a stub guest memory and checks the bytes for each depth and byte order. It replaces `x11-putpixel-logged.test.mjs`, which asserted the drop. The test fails on the original code.
- **`source/x11/x11common.cpp` `x11_GetGeometry` and `x11_FreeFont`**: `XGetGeometry` aborted the emulator. It now writes the root, the position in the parent, the border, the size and the depth to the guest pointers, and returns 1. A pixmap reports position and border as 0. An unknown drawable returns `BadDrawable` and writes nothing. `XFreeFont` aborted too; the legacy path never loads a font, so it now returns `Success`. Regression: `web/tests/suite/unit/x11-getgeometry.test.mjs` and `web/tests/suite/unit/x11-freefont.test.mjs`, which compile the real bodies against stubs and check the results. Both fail on the original code.
- **Core drawing calls in `source/x11/x11common.cpp` and `source/x11/xdrawable.cpp`** (were kpanics): `XQueryTree` lists the root, the parent and every child, bottom-most first, from `XWindow::getChildrenBottomToTop`. `XGetPixel` reads a pixel from the guest XImage, and `XSubImage` returns a new guest XImage holding a region, with 0 where the region is outside the source. `XAddPixel` adds to each pixel and wraps at the depth. Images are handled only for byte-aligned depths up to 32 bits and a zero xoffset; others are logged once. `XCopyPlane` paints the GC foreground for set bits of the plane and the background for clear bits, through the GC function and clip. `XCreateGC` honours the valuemask. `XSetFillStyle` and `XSetArcMode` store their values, and `XFillArc` and `XDrawArc` draw pie, chord and full ellipses, with the tile honoured for fills. `XSetDashes` stores and draws the validated pattern, phase, and DoubleDash background pixels. Regression: `x11-querytree`, `x11-image-pixels`, `x11-copyplane`, `x11-gc-setters`, `x11-drawable-raster` and `x11-arc-drawing` unit tests, plus `x11-putpixel-writes` which now also covers `XGetPixel`.
- **`source/x11/xdrawable.cpp` copy paths** (`copyImageData`, `copy`): both now cut copies to the drawable, honour clip rectangles and depth-one pixmap masks, apply every GX function for 32-bit, and read each source row before a later row overwrites it. The direct memcpy fast path is disabled when a GC clip is active. Regression: `web/tests/suite/unit/x11-copy-paths.test.mjs` runs both XCopyArea and XPutImage with mask pixels set and clear.
- **`source/x11/x11common.cpp` `x11_SetLocaleModifiers`**: fixes the invalid pointer value; null queries allocate a guest empty string, empty explicit modifiers are preserved, and unsupported nonempty requests warn once and return NULL. Regression: `web/tests/suite/unit/x11-locale-modifiers.test.mjs`.
- **`source/x11/x11common.cpp` `x11_IconifyWindow`**: a live target is unmapped and receives the Iconic WM_STATE value; unknown windows return BadWindow. Regression: `web/tests/suite/unit/x11-iconify-window.test.mjs`.
- **`source/x11/x11common.cpp` `x11_GetDefault`**: no resource database is loaded; the callback logs once and returns NULL, which is Xlib's no-match result. Regression: `web/tests/suite/unit/x11-xgetdefault-unavailable.test.mjs`.
- **`source/x11/x11common.cpp` XIM unavailable path**: `XOpenIM` and `XLocaleOfIM` warn once and return NULL because no host XIM exists in the browser build (browser composition itself is connected separately, via the launcher's committed-text channel — see the IME / XIM row). Regression: `web/tests/suite/unit/x11-ime-unavailable.test.mjs` verifies the warning is emitted once and calls return safely.
- **`source/x11/x11common.cpp` `x11_DisplayName`**: returns a caller-supplied display name unchanged and allocates the virtual display name `:0` for null, matching `XOpenDisplay` in this build. Regression: `web/tests/suite/unit/x11-display-name.test.mjs`.
- **`source/x11/x11common.cpp` `x11_SetWMProperties`**: argv is serialized as NUL-separated `WM_COMMAND` bytes using STRING/8 format; a zero argc deletes the property, and a null argv leaves it unchanged. Regression: `web/tests/suite/unit/x11-wm-command.test.mjs`.
- **`source/x11/x11common.cpp` `x11_GetWindowAttributes` / `XInternAtom`**: `XGetWindowAttributes` now returns the documented 32-bit ABI fields, including per-client and aggregate event masks; `XInternAtom` shares the atom table and honors `only_if_exists`. Regressions: `web/tests/suite/unit/x11-get-window-attributes.test.mjs` (including direct execution of the aggregate-mask implementation) and `x11-intern-atom.test.mjs`.
- **`source/util/bstring.cpp` `BString::remove(int, int)`** (round 2, found while wiring the desktop-link name fix): the body only called `makeWritable(0)` and removed nothing, so its one real caller — `FsNode::loadChildren` stripping the 6-char `.mixed` suffix off a host file name before registering it for the guest (`source/io/fsnode.cpp:93`) — silently registered the suffixed name. `remove` now deletes the clamped span (negative len or an overrun removes through the end; a shared string is copied first via makeWritable, as `replace` relies on). Regression: `web/tests/suite/unit/bstring-remove.test.mjs` slice-compiles the allocator, `length`, `makeWritable` and `remove` and covers suffix truncation, a middle span, overrun clamping, out-of-range no-ops and shared-owner isolation. It fails on the original body.
- **`source/x11/x11common.cpp` `x11_MatchVisualInfo`**: XMatchVisualInfo now searches the guest display visuals for a matching screen/depth/class, writes the matching XVisualInfo and returns False if no match exists. Regression: `web/tests/suite/unit/x11-match-visual-info.test.mjs` compiles and calls the real handler for both cases.


- **Round 3 — fs open-node async hooks** (`source/io/fsopennode.h/.cpp`, `fsdiropennode.cpp`, `fsfileopennode.cpp`, `fsmemopennode.cpp`, `fszipopennode.cpp`, `fsvirtualopennode.h`): `setAsync`/`waitForEvents` were debug-log-only no-ops on every fs open node and `isAsync` hardcoded false, so F_GETFL never reported an O_ASYNC the guest had set. `FsOpenNode` now tracks the flag (`applyAsyncFlag`), F_GETFL is truthful, enabling async warns once per process that SIGIO delivery is unavailable (delivery stays a kernel NEEDS_OTHER_LANE item above), and `waitForEvents` mirrors `DevDsp::waitForEvents`: it signals the poll condition when the requested readiness already holds. Regression: `web/tests/suite/unit/fs-opennode-async.test.mjs` compiles the real bodies and checks the flag round-trip, the rate-limited warning, ready/unready signalling, and deregistration behaviour.
- **Round 3 — `FsZipOpenNode::setLength` / `writeNative`** (`source/io/fszipopennode.cpp`): both were kpanics on a read-only node. They now kwarn and return false / 0 — a guest that somehow reaches them gets a loud refusal, not an emulator abort. Covered by `fs-opennode-async.test.mjs`.
- **Round 3 — `x11_FreeColorMap`** (`source/x11/x11common.cpp`, `source/x11/xserver.h/.cpp`): the handler was an empty body; colormaps leaked in the server table and unknown ids errored nowhere. `XServer::freeColorMap` removes a real colormap, treats the default colormap as a no-op success, and refuses unknown ids; the handler returns Success / BadColor. Regression: `web/tests/suite/unit/x11-freecolormap.test.mjs` compiles and runs both real bodies. The same file's `XSetICValues` comment now states its contract without marker wording (behaviour unchanged: it returns NULL, there are no attributes it cannot set).
- **Round 3 — PE32+ icon resources** (`source/ui/utils/readIcons.cpp`): icons were extracted only from PE32 executables; every 64-bit (PE32+) binary hit a kwarn and got no icon. The resource directory is now read for both formats, with the data-directory offset selected by format (112 for PE32+, `sizeof(Pe32OptionalHeader)` for PE32). Regression: `web/tests/suite/unit/readicons-pe32plus.test.mjs` (source guard — readIcons is host-UI code and cannot run in this harness).
- **Round 3 — `BString(const char*, bool)` copy constructor** (`source/util/bstring.cpp`): the non-literal case kpanic'd ("oops") instead of copying. It now allocates and copies like the other owning constructors. The `substr` `:TODO` marker was a performance note on correct behaviour and is reworded as one. Regression: `web/tests/suite/unit/bstring-ctor.test.mjs` compiles the real allocator helpers and constructor and verifies copy ownership, source mutation isolation, and the unchanged literal path.
- **Round 3 — XWire GLX default case** (`source/x11wire/xwireconnection.cpp`): behaviour was already a loud rejection (kwarn + BadRequest protocol error, pinned by `xwire-glx-protocol.test.mjs`); the log wording now avoids the audit's marker token while staying greppable, and the test pin was updated to match. No behaviour change.
- **Round 3 — proven not-a-gap, moved to the allowlist**: `platform/sdl/knativescreenSDL.cpp` (recorder bounds invariant that aborts loudly + a klog'd 8-bit screenshot refusal in host recorder tooling), `source/ui/data/appFile.cpp` (host-UI runner invariant, not guest-reachable), and `web/naga-rt.mjs` (the GLSL version classification whose ANGLE passthrough is verified by `naga-angle-fallback.test.mjs`; one hit is a comment quoting Naga's own probe error text). Reasons are recorded per file in `allowlist.json`.

- **Round 4 — coverage partial-row closeout** (`source/x11/x11common.cpp` `x11ComposeLatin`): the compose table covered only 4 of the 13 dead keys `x11IsDeadKey` accepts (grave, acute, circumflex, diaeresis). It now carries the standard Latin pairs for all 13 (tilde, macron, breve, dotaccent, abovering, doubleacute, caron, cedilla, ogonek added; acute and circumflex extended) plus the dead+space accent-itself pairs, and the UTF-8 emitter writes one byte for results below U+0080 instead of a malformed two-byte sequence. Regression: `web/tests/suite/unit/x11-keyboard-text.test.mjs` extended (tilde+n, cedilla+c, caron+s above U+00FF, grave+space one-byte, unlisted-pair fallback to the base key). Register-only dispositions in the same round, each on code evidence already in the tree: the Clipping row's Shape pointer closed (Shape is REJECTED-LOUDLY by design, work item 21); the GLX row closed (IMPLEMENTED minors + default-arm BadRequest for all others; rendering is direct-bridge by design); the XFixes remainder of the input/cursor row proven NOT-A-GAP (extension unadvertised on both paths, no shim symbols, no hostcalls); the stale 'Partly assessed' status in the not-covered table replaced with per-category dispositions; both NEEDS_OTHER_LANE bullets resolved (the kmemory_soft allowlist premise is proven at `source/kernel/kmemory.cpp:112` and by `sys_mmap64_file`'s fileBase alignment in `source/kernel/syscall64.cpp`; the platformThreads item was already closed by work item 15); and the audio-capture, DNS and timer rows sharpened in place into exact kernel deliverables with proving tests. The fs-async row (round 3) and the gamepad row (kernel joystick dependency, kernel.md's dedicated-lane row) were already precise and are unchanged.

## Register

Machine-readable. `tools/known-gaps-audit.mjs --check` reads this block. Counts may only go down without a new justification. A file that is not in this block and not in `allowlist.json` fails `--check`.

Round 3 (2026-10-25) emptied this register: all 12 files (30 hits) were dispositioned — see the round-3 entries under Fixes applied and the allowlist. A new entry here means a new UNOWNED gap was found; it must carry a count that may only go down.

<!-- known-gaps:register:begin -->
```json
{}
```
<!-- known-gaps:register:end -->

## Allowlist

`docs/known-gaps/allowlist.json` holds 21 files that are not gaps: invariants that abort loudly, vendored code, errno tables, comments, labels, recorder/UI host tooling, and the test-verified ANGLE shader fallback. Each entry has a reason and a count. The reasons are in the file.

## Hit lists

Generated by `node tools/known-gaps-audit.mjs --write-hits`. Do not edit by hand. Re-run after code moves.

<!-- known-gaps:hits:begin -->

_Generated by `node tools/known-gaps-audit.mjs --write-hits`; do not edit by hand._

### `include/bw64metrics.h` (1)

| Line | Match | Text |
| --- | --- | --- |
| 41 | `unsupported` | uint64_t fallbacks;               /* unsupported-op / invalidation fallbacks */ |

### `include/log.h` (3)

| Line | Match | Text |
| --- | --- | --- |
| 23 | `kpanic(` | void internal_kpanic(BString msg); |
| 25 | `kpanic(` | void kpanic(const char* msg); |
| 48 | `kpanic(` | internal_kpanic(msg); |

### `platform/sdl/knativeinputSDL.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 532 | `kpanic(` | kpanic("KNativeInputSDL::processCustomEvents should only be called on main thread"); |

### `platform/sdl/knativescreenSDL.cpp` (2)

| Line | Match | Text |
| --- | --- | --- |
| 289 | `kpanic(` | kpanic("script recorder overwrote memory when copying screen"); |
| 442 | `not supported` | klog("KNativeScreenSDL::internalScreenShot 8 bit screen shots not supported"); |

### `source/emulation/softmmu/codePageData.cpp` (2)

| Line | Match | Text |
| --- | --- | --- |
| 315 | `kpanic(` | kpanic("DecodedOpCache::add"); |
| 328 | `kpanic(` | kpanic("oops"); |

### `source/emulation/softmmu/kmemory_soft.cpp` (3)

| Line | Match | Text |
| --- | --- | --- |
| 143 | `kpanic(` | kpanic("KMemoryData::allocPages page out of bound"); |
| 146 | `kpanic(` | kpanic("KMemoryData::allocPages mapped files should not contain ramPages"); |
| 157 | `kpanic(` | kpanic("mmap: wasn't expecting the offset to be in the middle of a page"); |

### `source/emulation/softmmu/soft_mmu.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 65 | `kpanic(` | kpanic("MMU::setPermissions oops"); |

### `source/emulation/softmmu/soft_ram.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 109 | `kpanic(` | kpanic("ramPageAllocNative must be aligned to a page"); |

### `source/io/fsfilenode.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 336 | `ENOSYS` | case ENOSYS: return K_ENOSYS; |

### `source/io/fsmemnode.cpp` (5)

| Line | Match | Text |
| --- | --- | --- |
| 28 | `kpanic(` | kpanic("FsMemNode::rename not supported"); |
| 33 | `kpanic(` | kpanic("FsMemNode::remove not supported"); |
| 46 | `kpanic(` | kpanic("FsMemNode::open not supported"); |
| 59 | `kpanic(` | kpanic("FsMemNode::removeDir not supported"); |
| 64 | `kpanic(` | kpanic("FsMemNode::setTimes not supported"); |

### `source/sdl/main.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 170 | `XXX` | // server-1-XXX socket dirs behind; on the next launch the new wineserver |

### `source/ui/controls/ImGuiLayout.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 95 | `kpanic(` | kpanic("Auto layout only supports 1 variable length control in a row"); |

### `source/ui/data/appFile.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 147 | `kpanic(` | kpanic("runApps runOnRestartUI should not be set"); |

### `source/util/concurrentqueue.h` (1)

| Line | Match | Text |
| --- | --- | --- |
| 244 | `unsupported` | template<int Align, typename T> struct Vs2013Aligned { };  // default, unsupported alignment |

### `source/util/karray.h` (3)

| Line | Match | Text |
| --- | --- | --- |
| 64 | `kpanic(` | kpanic("KArray index out of bounds"); |
| 90 | `kpanic(` | kpanic("KArray index out of bounds"); |
| 98 | `kpanic(` | kpanic("KArray index out of bounds"); |

### `source/util/klist.h` (2)

| Line | Match | Text |
| --- | --- | --- |
| 85 | `kpanic(` | kpanic("Node already in list"); |
| 101 | `kpanic(` | kpanic("Node already in list"); |

### `source/util/log.cpp` (7)

| Line | Match | Text |
| --- | --- | --- |
| 51 | `Unsupported` | static std::atomic<unsigned long long> g_recoverUnsupportedCases{0}; |
| 106 | `Unsupported` | g_recoverUnsupportedCases.fetch_add(1, std::memory_order_relaxed); |
| 116 | `unsupported` | "{\"enabled\":%s,\"panics\":%llu,\"skippedInstructions\":%llu,\"loggedRips\":%llu,\"sigillFallbacks\":%llu,\"unsupportedCases\":%llu}", |
| 122 | `Unsupported` | g_recoverUnsupportedCases.load(std::memory_order_relaxed)); |
| 126 | `kpanic(` | void kpanic(const char* msg) { |
| 127 | `kpanic(` | internal_kpanic(BString::copy(msg)); |
| 130 | `kpanic(` | void internal_kpanic(BString msg) { |

### `source/util/synchronization.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 259 | `kpanic(` | kpanic("shouldn't signal a thread that is not waiting"); |

### `web/frame-metrics.mjs` (1)

| Line | Match | Text |
| --- | --- | --- |
| 299 | `unsupported` | id: 'rejects', label: 'Skipped / unsupported frames', value: formatCount(page ? (pagePace?.skipped ?? 0) + (pagePace?.unsupported ?? 0) : null), |

### `web/naga-rt.mjs` (5)

| Line | Match | Text |
| --- | --- | --- |
| 40 | `Not implemented` | //      (probe P4: combined samplers are "Not implemented: variable qualifier") |
| 70 | `unsupported` | else return { ok: false, reason: 'unsupported-version', version: num + (es ? ' es' : '') }; |
| 283 | `unsupported` | const unsupported = lifted.reason === 'unsupported-version'; |
| 284 | `unsupported` | if (!unsupported) stats.nagaFail++; // unsupported sources belong to ANGLE; no failure of ours |
| 286 | `unsupported` | ts, type: unsupported ? 'unsupported' : (lifted.reason === 'too-large' ? 'too-large' : 'lift'), |

### `web/shader.mjs` (1)

| Line | Match | Text |
| --- | --- | --- |
| 37 | `Unsupported` | // constants (UnsupportedInstruction) — before handing bytes to naga. |

<!-- known-gaps:hits:end -->

## Verification limits

- The scan is pattern-based and text-only. It was run on the worktree, not on a built artefact.
- No WASM build and no browser probe was run for this lane.
- `tools/known-gaps-audit.mjs` and its unit test are excluded from the scan, since they contain the pattern.
- The gate runs in the unit suite as `web/tests/suite/unit/known-gaps-audit.test.mjs`. That test runs `--check` on the checkout, and against fixture trees that must fail (an unregistered gap, a count above the register) and pass (once registered). A new silent UNOWNED gap therefore fails the unit run.
- Reachability claims are from reading the code, not from running a guest. Each "if reached" row needs a guest probe before its severity is confirmed.
