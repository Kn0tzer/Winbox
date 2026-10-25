# Unowned known-gap coverage

Scope: the audit's `UNOWNED` files (see `tools/known-gaps-audit.mjs`, `OWNERS`) are assigned to this lane for remediation and include the X11 host/window layer, io/fs nodes, UI, util, platform host, `include/`, and `web/*.mjs` other than gl/vk. Vendored `lib/` is excluded (imgui, asmjit, glew, zlib, simde, tiny-process).

The scan also reads `platform/`, `include/`, `scripts/`, `tasks/`, `testdata/`, all of `project/`, and the `tools/buildWine/` patches. No upstream Wine tree is present locally, so those patches are scanned, not excluded.

This register is a marker scan. It finds `unsupported`, `not implemented`, `TODO`, `FIXME`, `kpanic(`, `ENOSYS`, `stub` and similar text in code. Zero hits does **not** mean a feature works. Behavioural gaps with no marker are listed under "Not covered by the scan" below.

## Summary

| Area | Files | Hits | Disposition |
| --- | --- | --- | --- |
| X11 host (`source/x11/`) | 2 | 13 | High. Remaining marker hits include Shape handlers, legacy Xlib stubs, and loud format limitations. |
| X11 wire GLX (`source/x11wire/`) | 1 | 1 | High, if the GLX-over-wire path is used by a target build. |
| io / fs nodes (`source/io/`) | 8 | 19 | Low. The one guest-reachable abort (`FsVirtualNode::removeDir`) is fixed. |
| UI (`source/ui/`) | 8 | 11 | Low. Save-on-exit and error-message TODOs. |
| Platform host (`platform/`) | 5 | 11 | Medium for thread exception sync; low for cursor, audio device and CPU frequency. |
| softmmu (`source/emulation/softmmu/`) | 4 | 7 | Invariants only, see allowlist. |
| util (`source/util/`) | 6 | 13 | Invariants and vendored code, see allowlist. |
| `web/naga-rt.mjs`, `web/frame-metrics.mjs` | 2 | 5 | Medium for shader versions routed to ANGLE. The other hit is a UI label. |
| `include/`, `source/sdl/main.cpp` | 3 | 5 | Invariants and comments, see allowlist. |

Current scan: 38 UNOWNED files, 93 hits. 20 files are in the register below and 18 are allowlisted with reasons. `--check` fails on any UNOWNED hit that is neither.

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
| XSetWMProperties command | `source/x11/x11common.cpp` | low | Encodes argv strings as the NUL-separated `WM_COMMAND` property and deletes it for argc zero. | IMPLEMENTED: `x11-wm-command.test.mjs` |
| XGetWindowAttributes | `source/x11/x11common.cpp` | medium | Returns the guest 32-bit ABI fields for geometry, visual, root, class, gravity/backing, colormap, map state, this-client and all-client event masks, override-redirect and screen; a missing window returns BadWindow. | IMPLEMENTED: `x11-get-window-attributes.test.mjs` |
| XInternAtom | `source/x11/x11common.cpp` | medium | Interns one atom and honors only-if-exists without aborting. | IMPLEMENTED: `x11-intern-atom.test.mjs` |
| XDisplayName | `source/x11/x11common.cpp` | low | Returns an explicit display string unchanged; null returns the configured virtual default `:0`. | IMPLEMENTED: `x11-display-name.test.mjs` |
| XGetDefault resource database | `source/x11/x11common.cpp` | low | No X resource database is loaded; queries warn once and return the Xlib NULL result. | REJECTED-LOUDLY: `x11-xgetdefault-unavailable.test.mjs` |
| XIconifyWindow | `source/x11/x11common.cpp` | low | Unmaps the target, stores Iconic WM_STATE, and returns failure for an invalid window. | IMPLEMENTED: `x11-iconify-window.test.mjs` |
| XSetWMProperties command | `source/x11/x11common.cpp` | low | Encodes argv strings as the NUL-separated `WM_COMMAND` property and deletes it for argc zero. | IMPLEMENTED: `x11-wm-command.test.mjs` |
| Dropped pixel write | `x11common.cpp:1688` (`x11_PutPixel`) | high | `XPutPixel` now writes the pixel into the guest XImage for 8, 16, 24 and 32 bpp. Sub-byte depths and a non-zero xoffset are logged once and not written. | FIXED (test `x11-putpixel-writes.test.mjs`) |
| Fullscreen and window state (EWMH) | `source/x11/xwindow.cpp` state handler; `_NET_ACTIVE_WINDOW` dispatch in `x11common.cpp` | high | Fullscreen add/remove/toggle updates/restores window geometry; above state raises/lowers windows; active-window messages set X input focus; root advertises supported atoms; CWSibling/stack modes reorder sibling windows. `_NET_WM_NAME` and `_NET_WM_ICON` use property storage. | IMPLEMENTED: `x11-ewmh-state.test.mjs` |
| Clipping | `source/x11/xdrawable.cpp` (`copyImageData`, `copy`, `plotPixel`, `clipAllows`); `XSetClipMask`/`XSetClipRectangles` in `x11common.cpp` | high | Clip rectangles and depth-one clip-mask pixmaps are applied to pixel writes, XPutImage and XCopyArea; mask origins and unset/out-of-bounds bits suppress drawing. `XSetClipMask` validates drawable/depth and replaces rectangles, and `XSetClipRectangles` replaces the mask. Shape extension remains open. | PARTLY IMPLEMENTED: `x11-drawable-raster.test.mjs`, `x11-copy-paths.test.mjs`; Shape extension basics remain open. |
| GLX over the X wire | `source/x11wire/xwireconnection.cpp` GLX major opcode handler | high, if the path is used | QueryVersion and query-string requests return protocol-framed values; GLX rendering runs through the direct host bridge. Unknown GLX minors now return BadRequest with a warning instead of corrupting the X reply stream with a fake reply. | PARTLY IMPLEMENTED / REJECTED-LOUDLY for unsupported minors: `xwire-glx-protocol.test.mjs`; full GLX protocol rendering remains outside the wire path. |
| Keyboard text entry | `x11common.cpp` (`XLookupString`, `XmbLookupString`, `XkbTranslateKeysym`, `RefreshKeyboardMapping`) | medium | Key events translate to bounded bytes and keysyms; multibyte lookup reports overflow/status; extra_rtrn reports uncopied bytes; static keymap refresh succeeds. Compose status is accepted, but dead-key composition is not yet implemented. | PARTLY IMPLEMENTED: `x11-keyboard-text.test.mjs`; dead-key composition remains open. |
| IME / XIM | `x11common.cpp` (`x11_*IC` / `x11_*IM`) | medium (high for CJK input) | XOpenIM reports the unavailable host method once and returns NULL; XLocaleOfIM does the same and returns NULL. Browser composition remains disconnected from guest text events. | REJECTED-LOUDLY for unavailable host XIM: `x11-ime-unavailable.test.mjs`; browser IME composition remains OPEN. |
| Synthetic events | `x11common.cpp` (`XSendEvent`) and `xserver.cpp` event-mask dispatch | medium | PointerWindow/InputFocus resolve to live targets; propagate walks ancestors and delivers to the first selecting level. | IMPLEMENTED: `x11-send-event.test.mjs` |
| MIT-SHM | `x11common.cpp` (`XQueryExtension`, `XShmAttach/CreateImage/Detach/PutImage`) | medium | MIT-SHM is explicitly unavailable and unadvertised; direct attach/put calls fail with False or NULL and a rate-limited warning directing clients to XPutImage. | REJECTED-LOUDLY: `x11-mitshm-unavailable.test.mjs` |
| Tiled fill | `source/x11/xdrawable.cpp` `fillRectangle` | medium | A fill with a tile pattern aborts the emulator. | FIXED (tile repeated from the tile origin; test `x11-drawable-raster.test.mjs`) |
| Other X11 input/cursor gaps | `x11common.cpp` (`XISelectEvents`, `CursorImageLoadCursor`, `CursorLibraryLoadCursor`) | low | XI2 masks support the protocol's 33 event bits and multiple device masks (folded onto the single virtual pointer), rejecting only malformed/out-of-range masks with `BadValue`; custom Xcursor images create host cursors. Unknown library names warn and return a fallback cursor. XFixes cursor and animated updates remain open. | PARTLY IMPLEMENTED: `x11-xinput-cursor.test.mjs`; XFixes/animation remain open. |
| Shader versions routed to ANGLE | `web/naga-rt.mjs:70,283-286` | medium | Unsupported GLSL is classified as `unsupported-version`, excluded from Naga failures, and the wrapper always passes the exact original source to ANGLE. Probe: `naga-angle-fallback.test.mjs`. ANGLE remains responsible for syntax validity. | IMPLEMENTED: original call-through is behavior-tested. |
| Thread exception sync (host) | `platform/linux/platformThreads-x64.cpp:146,187` (`syncFromException` kpanics) | medium | An unexpected host thread state aborts the emulator. | OPEN (work item 14) |
| Directory rmdir on virtual nodes | `source/io/fsvirtualnode.cpp:86-89` (was kpanic `removeDir not implemented`) | medium (before the fix) | Guest `rmdir` of a virtual directory aborted the emulator (`kprocess.cpp:1204-1211` calls `node->removeDir()`). | FIXED: logs and returns `-EPERM`. Test: `web/tests/suite/unit/fs-virtual-removedir.test.mjs`. |
| Virtual `setTimes` | `source/io/fsvirtualnode.cpp` | low | Virtual nodes retain atime/mtime in milliseconds, preserve `UTIME_OMIT`, and expose updated mtime to stat. | IMPLEMENTED: `fs-virtual-settimes.test.mjs` |
| memfd nodes | `source/io/fsmemnode.cpp:28,33,46,59,64` (kpanic on rename, remove, open, removeDir, setTimes) | none for now | Unreachable: memfd nodes never enter the path namespace (see allowlist). Becomes reachable if `/proc/self/fd` is added. | ALLOWLISTED (work item 15) |
| Zip-backed write and truncate | `source/io/fszipopennode.cpp:43,236` (kpanic) | low | Unreachable from guest code: writable fds are rejected before `setLength` and `writeNative` (`kprocess.cpp:1680`), and write-open moves the file first (`fsfilenode.cpp:192`). | NOT_A_GAP for the write path; invariant kept |
| Async and event hooks on fs nodes | `fsdiropennode.cpp:63,71`; `fsfileopennode.cpp:97,105`; `fsmemopennode.cpp:63,71`; `fsvirtualopennode.h:37,39`; `fszipopennode.cpp:75,83`; kernel inotify `syscall.cpp:1498,2272-2273` | medium | File node readiness hooks are empty and log only at debug level; kernel inotify init is ignored and add/remove watch syscalls are absent. Implementing reliable async fs notifications requires the kernel lane to first add inotify/watch lifetime and SIGIO delivery contracts. | NEEDS_OTHER_LANE: `source/kernel/syscall.cpp:1498,2272-2273` implement inotify init/add/remove watch APIs and notify FsNode mutation hooks; then wire `setAsync`/`waitForEvents` across fs node types. |
| Audio output device | `platform/sdl/knativeaudio.cpp:77` (TODO: device choice) | low | Audio always uses the default device. The scan found no other marker in audio output or input. Their correctness is not assessed here. | OPEN, registered |
| CPU frequency reporting | `platform/linux/platform.cpp` | low | Linux reads host cpufreq sysfs in kHz and falls back to `/proc/cpuinfo`; browser builds report a nominal 1000 MHz when host procfs/sysfs are unavailable. Per-CPU current/max nodes use scaling sysfs and API fallback. | IMPLEMENTED: `platform-cpu-frequency.test.mjs` |
| Cursor shapes | `platform/sdl/knativescreenSDL.cpp:616,626,661` (TODO: approximate shapes) | low | Some X cursor shapes map to the nearest SDL system cursor. | OPEN, registered |
| UI save and error paths | `source/ui/mainui.cpp:382,664`; `source/ui/controls/optionsView.cpp:106`; `source/ui/data/globalSettings.cpp:674,1072`; `source/ui/data/boxedContainer.cpp:363`; `source/ui/data/appFile.cpp:60`; `source/ui/controls/downloadDlg.cpp:156` | low | Settings not saved on focus loss, quit not checked against a failed save, error messages missing, a link name not parsed, a download socket with no stuck-socket handling. None is game-facing. | OPEN, registered |
| Icon format | `source/ui/utils/readIcons.cpp:646` (`Icon 20b not implemented`, logged with `kwarn`) | low | One icon format is not shown. Loud and UI-only. | OPEN, registered |
| bstring copy | `source/util/bstring.cpp:570` (TODO) | low | Performance note only. | OPEN, registered |

### Not covered by the scan

These categories were requested for the game-impact review. The scan found no marker for them in scope, so this lane does not count them as covered. Each needs a behavioural probe before anyone states a status.

| Category | Scan result | Status |
| --- | --- | --- |
| Audio output and input | Output is implemented by SDL queued audio or the browser Web Audio ring; `audio-audible-output.test.mjs` exercises output conversion and bridge delivery. No capture/microphone input path was found. | Output: tested. Input: NEEDS_KERNEL_DEPENDENCY because no guest capture device exists to feed Wine audio capture. |
| Gamepad / XInput | No SDL game-controller/joystick event handling or guest joystick device is present; `/dev/input/event3` and `event4` are mouse and keyboard only (`source/sdl/startupArgs.cpp:152-154`, boot lane owned). | OPEN: browser Gamepad API events cannot reach Wine DirectInput/XInput without a guest joystick device/event path. NEEDS_KERNEL_DEPENDENCY for `source/kernel/devs/devinput.cpp` plus SDL polling/translation here. |
| Window, cursor, fullscreen, pointer lock | Markers in the table above (mode switch, relative mouse, EWMH, grab). | Partly assessed. |
| Clipboard (X selections) | `source/x11wire/xwireconnection.cpp` selection conversion and `web/clipboard-bridge.mjs` | IMPLEMENTED: browser copy reads the guest selection bridge; paste stages host text for X selection conversion. `clipboard-bridge.test.mjs` covers both directions; `xwireconnection.cpp` applies TARGETS, UTF8_STRING, STRING and TEXT conversions. |
| IME | See the IME row above. | Partly assessed. |
| Networking sockets and DNS | Native TCP/UDP and browser TCP relay probes exist (`loopback-tcp-relay`, `loopback-datagram-msg`, `browser-network`). | Socket transport: tested. DNS: NEEDS_OTHER_LANE to expose host resolution through the guest network resolver path and add a deterministic probe. |
| File dialogs | Browser folder selection uses `showDirectoryPicker`; import accepts file and directory entries through the browser picker/drop pipeline. | Browser picker/import: partly tested at the data layer; DOM picker and guest Windows common dialogs need an integration probe. |
| Filesystem semantics (case-insensitivity, symlinks) | ZIP import rejects archive symlinks (`web/files.mjs`); VFS decodes Wine `.link` and `(_colon_)` drive aliases (`source/io/fsnode.cpp`, `source/io/fsopennode.cpp`); lookup can ignore case. | VFS case folding and Wine alias lookup: `fs-node-case-symlink.test.mjs`. Import collision checks lowercase names (`web/files.mjs:22`). Host-backed case resolution remains platform-dependent and needs a native probe. |
| Timers | Linux timer syscalls include nanosleep, setitimer, timerfd, and clock_gettime; browser and native scheduler use host timer callbacks. | NEEDS_OTHER_LANE: add deterministic timerfd and setitimer expiration probes with the kernel timer implementation. |

The scan's pattern is also narrow. It misses a plain `not supported` outside `kpanic(` and `klog_fmt`, a capitalised `Not implemented`, and a silent `return -K_ENOSYS` with no text. Those need a separate pass.

## Proposed work items

Ranked by severity if reached, then by how likely a real game is to reach the path. Items are updated as the lane implements them; remaining partial items name what still needs work.

1. **XRandR family (`x11common.cpp:3029-3246`, `xserver.cpp:676`).** DONE. XRandR 1.2 exposes the host size list, current one-output topology, CRTC/output info, size range, primary output, mode changes and screen/CRTC/output notifications. The emulated output supports only rotation 0 and rejects unsupported rotations/modes. Regression probes: `x11-xrandr-set-screen-config.test.mjs`, `x11-xrandr-resources.test.mjs`, and `x11-xrandr-events.test.mjs`.
2. **Pointer grab and relative mouse (`x11common.cpp:506,510,514`, `xserver.cpp`, `knativeinputSDL.cpp`).** IMPLEMENTED: owner_events and event masks route correctly; synchronous pointer grabs freeze and release one event per SyncPointer; async modes drain the pending queue; invalid XAllowEvents values return BadValue; relative SDL deltas feed XI2 raw motion without recentering; confined grabs request browser pointer lock; per-grab cursor switches on the SDL main thread and restores on ungrab. Tests: `xwire-pointer-grab.test.mjs`, `x11-legacy-relative-mouse.test.mjs`.
3. **Core drawing entry points (the list in the table).** DONE: every listed kpanic and dashed strokes are implemented. Regression probe `x11-drawable-raster.test.mjs` executes the raster code and verifies OnOffDash/DoubleDash pattern phase, foreground/background pixels, and existing clip/fill behavior.
4. **`XPutPixel` write (`x11common.cpp:1688`).** DONE: the pixel is written into the guest XImage for 8, 16, 24 and 32 bpp in either byte order. Sub-byte depths and a non-zero xoffset are logged once and not written. See Fixes.
5. **EWMH state and stacking (`xwindow.cpp:610,1032`).** IMPLEMENTED: state add/remove/toggle deduplicates atoms; fullscreen resizes to the root and restores prior geometry; `_NET_ACTIVE_WINDOW` updates X focus; `_NET_SUPPORTED` advertises fullscreen, above, active, name and icon support; above changes and CWSibling stack requests reorder windows. Test: `x11-ewmh-state.test.mjs`.
6. **Clip regions (`xdrawable.cpp`).** PARTLY IMPLEMENTED: clip rectangles and depth-one pixmap masks gate raster, XPutImage, and XCopyArea writes with clip origins; setter validation, copy behavior, and mutual replacement semantics are tested by `x11-drawable-raster.test.mjs` and `x11-copy-paths.test.mjs`. Shape extension window regions remain open.
7. **GLX over the X wire (`xwireconnection.cpp`).** PARTLY IMPLEMENTED: GLX QueryVersion and string queries have protocol replies; Create/DestroyWindow maintains drawable mappings. Other GLX minors return BadRequest with a warning instead of a fabricated reply. GL rendering remains on the direct host path. Test: `xwire-glx-protocol.test.mjs`.
8. **Keyboard text entry (`x11common.cpp:777,806,847,940`).** PARTLY IMPLEMENTED: lookup handlers return bounded text/keysyms, status-out, overflow and extra byte counts; refresh succeeds with the fixed browser map. Test: `x11-keyboard-text.test.mjs`. Dead-key composition remains open.
9. **XIM / IC family (`x11common.cpp:2372-2460`).** PARTLY IMPLEMENTED: XOpenIM returns NULL for unavailable host input methods and related calls now fail safely without aborting. Test: `x11-keyboard-text.test.mjs`. Browser IME composition remains open.
10. **Synthetic events (`x11common.cpp:717,720,755`).** IMPLEMENTED: special target windows resolve through pointer/focus state; propagated events walk to the first ancestor with a selecting client. `iterateEventMask` now reports whether delivery occurred. Test: `x11-send-event.test.mjs`.
11. **MIT-SHM (`x11common.cpp:2562-2575`).** REJECTED-LOUDLY: extension query reports unavailable, and direct hostcalls return False/NULL with a rate-limited warning because shared mappings are not exposed. Clients fall back to XPutImage. Test: `x11-mitshm-unavailable.test.mjs`.
12. **XI2 masks and Xcursor images (`x11common.cpp:3442,3547`).** PARTLY IMPLEMENTED: XI2 event mask bytes now preserve bit 32, and malformed bits return BadValue; `XcursorImageLoadCursor` creates a native cursor from guest pixels. Test: `x11-xinput-cursor.test.mjs`. XFixes cursor changes and animated frame scheduling remain open.
12. **Tiled fill (`xdrawable.cpp:216`).** Impact: medium-low. Approach: repeat the tile pixmap across the fill rectangle, offset by the tile origin. Test: a 2x2 tile over a 5x3 rectangle, against a reference.
14. **Naga to ANGLE fallback (`web/naga-rt.mjs:70,283-286`).** IMPLEMENTED: a node probe drives an unsupported-version shader through the installed WebGL wrapper, verifies the original ANGLE method receives the identical shader and returns normally, and checks the unsupported observation record without marking the lane broken. Test: `naga-angle-fallback.test.mjs`.
15. **Host thread exception sync (`platformThreads-x64.cpp:146,187`).** Impact: medium. Approach: record the unexpected thread state as a guest fault and deliver it through the existing signal path. Needs kernel-lane coordination. Test: an injected exception-state fixture.
16. **memfd `FsMemNode` (`fsmemnode.cpp`).** Allowlisted as unreachable. If `/proc/self/fd` is added, the rename, remove, open, removeDir and setTimes paths become reachable and must be implemented first. Test: the allowlist reason is the guard.
17. **Async fs events (the io group above).** NEEDS_KERNEL_DEPENDENCY. Kernel inotify is currently ignored (`source/kernel/syscall.cpp:1498`), and add/remove watch syscall slots are null (`:2272-2273`). The kernel lane must expose watch registration, event queue delivery and `O_ASYNC`/SIGIO ownership before FsNode mutations can reliably notify open-node waiters. After that, implement and test the fs node hooks here.
18. **CPU frequency (`platform.cpp:130,134,138`).** IMPLEMENTED: native Linux reads cpufreq sysfs then `/proc/cpuinfo`; browser builds use a nominal 1000MHz fallback when host files are absent. Per-CPU scaling nodes fall back to base frequency. Test: `platform-cpu-frequency.test.mjs`.

## NEEDS_OTHER_LANE

- **Kernel**: `source/emulation/softmmu/kmemory_soft.cpp:157` (kpanic on a mapped-file offset that is not page-aligned). Allowlisted on the premise that `source/kernel/syscall64.cpp:1627` aligns the offset before mapping. Kernel lane: confirm that premise, or make the syscall return `-EINVAL` for an unaligned offset.
- **Kernel**: `platform/linux/platformThreads-x64.cpp:146,187` may need kernel-lane coordination. See work item 14.

## Fixes applied

- **`source/io/fsvirtualnode.cpp:86-89`** (`FsVirtualNode::removeDir`): a guest `rmdir` of a virtual directory aborted the emulator. It now logs the attempt and returns `-K_EPERM`. Virtual directories are generated, so the guest cannot remove one, and `EPERM` is the errno for an operation the node does not permit. This is a choice, not a copy of a host result; the host was not probed. Regression: `web/tests/suite/unit/fs-virtual-removedir.test.mjs`, which compiles the real function body against stub `kpanic` and `klog` and checks the return value, the log count, and that no panic is reached. The test fails on the original code and passes with the fix. The register entry drops from 2 (medium) to 1 (low, `setTimes`).
- **`source/x11/x11common.cpp:1688`** (`x11_PutPixel`): `XPutPixel` edits the image in client memory, so the handler reads the guest XImage header and writes the pixel bytes at `data + bytes_per_line * y + x * bytes`. It writes 8, 16, 24 and 32 bpp, most significant byte first for `MSBFirst`. A pixel outside the image is ignored and the call returns 0. Sub-byte depths and a non-zero `xoffset` are logged once per process and not written. The return value of 1 for a written pixel is taken from libX11's convention as remembered, not checked against a host libX11 here. Regression: `web/tests/suite/unit/x11-putpixel-writes.test.mjs`, which compiles the real body against a stub guest memory and checks the bytes for each depth and byte order. It replaces `x11-putpixel-logged.test.mjs`, which asserted the drop. The test fails on the original code.
- **`source/x11/x11common.cpp` `x11_GetGeometry` and `x11_FreeFont`**: `XGetGeometry` aborted the emulator. It now writes the root, the position in the parent, the border, the size and the depth to the guest pointers, and returns 1. A pixmap reports position and border as 0. An unknown drawable returns `BadDrawable` and writes nothing. `XFreeFont` aborted too; the legacy path never loads a font, so it now returns `Success`. Regression: `web/tests/suite/unit/x11-getgeometry.test.mjs` and `web/tests/suite/unit/x11-freefont.test.mjs`, which compile the real bodies against stubs and check the results. Both fail on the original code.
- **Core drawing calls in `source/x11/x11common.cpp` and `source/x11/xdrawable.cpp`** (were kpanics): `XQueryTree` lists the root, the parent and every child, bottom-most first, from `XWindow::getChildrenBottomToTop`. `XGetPixel` reads a pixel from the guest XImage, and `XSubImage` returns a new guest XImage holding a region, with 0 where the region is outside the source. `XAddPixel` adds to each pixel and wraps at the depth. Images are handled only for byte-aligned depths up to 32 bits and a zero xoffset; others are logged once. `XCopyPlane` paints the GC foreground for set bits of the plane and the background for clear bits, through the GC function and clip. `XCreateGC` honours the valuemask. `XSetFillStyle` and `XSetArcMode` store their values, and `XFillArc` and `XDrawArc` draw pie, chord and full ellipses, with the tile honoured for fills. `XSetDashes` stores and draws the validated pattern, phase, and DoubleDash background pixels. Regression: `x11-querytree`, `x11-image-pixels`, `x11-copyplane`, `x11-gc-setters`, `x11-drawable-raster` and `x11-arc-drawing` unit tests, plus `x11-putpixel-writes` which now also covers `XGetPixel`.
- **`source/x11/xdrawable.cpp` copy paths** (`copyImageData`, `copy`): both now cut copies to the drawable, honour clip rectangles and depth-one pixmap masks, apply every GX function for 32-bit, and read each source row before a later row overwrites it. The direct memcpy fast path is disabled when a GC clip is active. Regression: `web/tests/suite/unit/x11-copy-paths.test.mjs` runs both XCopyArea and XPutImage with mask pixels set and clear.
- **`source/x11/x11common.cpp` `x11_IconifyWindow`**: a live target is unmapped and receives the Iconic WM_STATE value; unknown windows return BadWindow. Regression: `web/tests/suite/unit/x11-iconify-window.test.mjs`.
- **`source/x11/x11common.cpp` `x11_GetDefault`**: no resource database is loaded; the callback logs once and returns NULL, which is Xlib's no-match result. Regression: `web/tests/suite/unit/x11-xgetdefault-unavailable.test.mjs`.
- **`source/x11/x11common.cpp` XIM unavailable path**: `XOpenIM` and `XLocaleOfIM` warn once and return NULL because browser IME composition is not connected to guest text events. Regression: `web/tests/suite/unit/x11-ime-unavailable.test.mjs` verifies the warning is emitted once and calls return safely.
- **`source/x11/x11common.cpp` `x11_DisplayName`**: returns a caller-supplied display name unchanged and allocates the virtual display name `:0` for null, matching `XOpenDisplay` in this build. Regression: `web/tests/suite/unit/x11-display-name.test.mjs`.
- **`source/x11/x11common.cpp` `x11_SetWMProperties`**: argv is serialized as NUL-separated `WM_COMMAND` bytes using STRING/8 format; a zero argc deletes the property, and a null argv leaves it unchanged. Regression: `web/tests/suite/unit/x11-wm-command.test.mjs`.
- **`source/x11/x11common.cpp` `x11_GetWindowAttributes` / `XInternAtom`**: `XGetWindowAttributes` now returns the documented 32-bit ABI fields, including per-client and aggregate event masks; `XInternAtom` shares the atom table and honors `only_if_exists`. Regressions: `web/tests/suite/unit/x11-get-window-attributes.test.mjs` (including direct execution of the aggregate-mask implementation) and `x11-intern-atom.test.mjs`.
- **`source/x11/x11common.cpp` `x11_MatchVisualInfo`**: XMatchVisualInfo now searches the guest display visuals for a matching screen/depth/class, writes the matching XVisualInfo and returns False if no match exists. Regression: `web/tests/suite/unit/x11-match-visual-info.test.mjs` compiles and calls the real handler for both cases.

## Register

Machine-readable. `tools/known-gaps-audit.mjs --check` reads this block. Counts may only go down without a new justification. A file that is not in this block and not in `allowlist.json` fails `--check`.

<!-- known-gaps:register:begin -->
```json
{
  "platform/linux/platformThreads-x64.cpp": { "count": 2, "severity": "medium", "area": "platform", "gap": "syncFromException kpanics on an unexpected thread state" },
  "platform/sdl/knativeaudio.cpp": { "count": 1, "severity": "low", "area": "audio", "gap": "TODO: no audio device choice" },
  "platform/sdl/knativescreenSDL.cpp": { "count": 5, "severity": "low", "area": "cursor", "gap": "approximate X cursor shapes (TODO); 8-bit screenshots not supported (logged); recorder invariant kpanic" },
  "source/io/fsdiropennode.cpp": { "count": 2, "severity": "low", "area": "fs", "gap": "setAsync and waitForEvents are no-ops" },
  "source/io/fsfileopennode.cpp": { "count": 2, "severity": "low", "area": "fs", "gap": "setAsync and waitForEvents are no-ops" },
  "source/io/fsmemopennode.cpp": { "count": 2, "severity": "low", "area": "fs", "gap": "setAsync and waitForEvents are no-ops" },
  "source/io/fsvirtualopennode.h": { "count": 2, "severity": "low", "area": "fs", "gap": "setAsync and waitForEvents are no-ops" },
  "source/io/fszipopennode.cpp": { "count": 4, "severity": "low", "area": "fs", "gap": "write and truncate kpanics unreachable; async no-ops" },
  "source/ui/controls/downloadDlg.cpp": { "count": 1, "severity": "low", "area": "ui", "gap": "TODO: stuck download socket not handled" },
  "source/ui/controls/optionsView.cpp": { "count": 1, "severity": "low", "area": "ui", "gap": "TODO: settings not saved on focus loss" },
  "source/ui/data/appFile.cpp": { "count": 2, "severity": "low", "area": "ui", "gap": "TODO: dependency ordering; runOnRestartUI invariant" },
  "source/ui/data/boxedContainer.cpp": { "count": 1, "severity": "low", "area": "ui", "gap": "TODO: link name not parsed" },
  "source/ui/data/globalSettings.cpp": { "count": 2, "severity": "low", "area": "ui", "gap": "TODO: settings error messages missing" },
  "source/ui/mainui.cpp": { "count": 2, "severity": "low", "area": "ui", "gap": "TODO: quit does not check a failed save" },
  "source/ui/utils/readIcons.cpp": { "count": 1, "severity": "low", "area": "ui", "gap": "one icon format not shown" },
  "source/util/bstring.cpp": { "count": 2, "severity": "low", "area": "util", "gap": "TODO: copy on substring (performance); BString::BString invariant" },
  "source/x11/x11common.cpp": { "count": 12, "severity": "high", "area": "x11", "gap": "Shape handlers abort; several legacy Xlib/XIM calls remain unimplemented; unsupported image layouts are loud" },
  "source/x11/xdrawable.cpp": { "count": 1, "severity": "medium", "area": "x11", "gap": "non-32-bit legacy line drawing is logged and skipped" },
  "source/x11wire/xwireconnection.cpp": { "count": 1, "severity": "high", "area": "x11", "gap": "unknown GLX requests return BadRequest and log a warning" },
  "web/naga-rt.mjs": { "count": 5, "severity": "medium", "area": "shader", "gap": "shader versions routed to ANGLE; fallback not verified; naga 'Not implemented' probe note" }
}
```
<!-- known-gaps:register:end -->

## Allowlist

`docs/known-gaps/allowlist.json` holds 17 files that are not gaps: invariants that abort loudly, vendored code, errno tables, comments, and labels. Each entry has a reason and a count. The reasons are in the file.

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

### `platform/linux/platformThreads-x64.cpp` (2)

| Line | Match | Text |
| --- | --- | --- |
| 146 | `kpanic(` | kpanic("Linux x64 syncFromException"); |
| 187 | `kpanic(` | kpanic("Linux x64 syncFromException xmm"); |

### `platform/sdl/knativeaudio.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 77 | `TODO` | // :TODO: maybe allow the user to choose in the future |

### `platform/sdl/knativeinputSDL.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 532 | `kpanic(` | kpanic("KNativeInputSDL::processCustomEvents should only be called on main thread"); |

### `platform/sdl/knativescreenSDL.cpp` (5)

| Line | Match | Text |
| --- | --- | --- |
| 289 | `kpanic(` | kpanic("script recorder overwrote memory when copying screen"); |
| 442 | `not supported` | klog("KNativeScreenSDL::internalScreenShot 8 bit screen shots not supported"); |
| 616 | `TODO` | // :TODO: |
| 626 | `TODO` | // :TODO: |
| 661 | `TODO` | // :TODO: |

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

### `source/io/fsdiropennode.cpp` (2)

| Line | Match | Text |
| --- | --- | --- |
| 63 | `not implemented` | kdebug("FsDirOpenNode::setAsync not implemented"); |
| 71 | `not implemented` | kdebug("FsDirOpenNode::waitForEvents not implemented"); |

### `source/io/fsfilenode.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 336 | `ENOSYS` | case ENOSYS: return K_ENOSYS; |

### `source/io/fsfileopennode.cpp` (2)

| Line | Match | Text |
| --- | --- | --- |
| 97 | `not implemented` | kdebug("FsFileOpenNode::setAsync not implemented"); |
| 105 | `not implemented` | kdebug("FsFileOpenNode::::waitForEvents not implemented"); |

### `source/io/fsmemnode.cpp` (5)

| Line | Match | Text |
| --- | --- | --- |
| 28 | `kpanic(` | kpanic("FsMemNode::rename not supported"); |
| 33 | `kpanic(` | kpanic("FsMemNode::remove not supported"); |
| 46 | `kpanic(` | kpanic("FsMemNode::open not supported"); |
| 59 | `kpanic(` | kpanic("FsMemNode::removeDir not supported"); |
| 64 | `kpanic(` | kpanic("FsMemNode::setTimes not supported"); |

### `source/io/fsmemopennode.cpp` (2)

| Line | Match | Text |
| --- | --- | --- |
| 63 | `not implemented` | kdebug("FsMemOpenNode::setAsync not implemented"); |
| 71 | `not implemented` | kdebug("FsMemOpenNode::::waitForEvents not implemented"); |

### `source/io/fsvirtualopennode.h` (2)

| Line | Match | Text |
| --- | --- | --- |
| 37 | `not implemented` | void setAsync(bool isAsync) override {if (isAsync) kdebug("FsVirtualOpenNode::setAsync not implemented");} |
| 39 | `not implemented` | void waitForEvents(BOXEDWINE_CONDITION& parentCondition, U32 events) override { kdebug("FsVirtualOpenNode::waitForEvents not implemented");} |

### `source/io/fszipopennode.cpp` (4)

| Line | Match | Text |
| --- | --- | --- |
| 43 | `kpanic(` | kpanic("FsZipOpenNode::setLength should not have been called"); |
| 75 | `not implemented` | kdebug("FsZipOpenNode::setAsync not implemented"); |
| 83 | `not implemented` | kdebug("FsZipOpenNode::waitForEvents not implemented"); |
| 236 | `kpanic(` | kpanic("FsZipOpenNode::writeNative not implemented"); |

### `source/sdl/main.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 170 | `XXX` | // server-1-XXX socket dirs behind; on the next launch the new wineserver |

### `source/ui/controls/ImGuiLayout.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 95 | `kpanic(` | kpanic("Auto layout only supports 1 variable length control in a row"); |

### `source/ui/controls/downloadDlg.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 156 | `TODO` | // :TODO: what if the socket is stuck, this will only work if the socket is just too slow |

### `source/ui/controls/optionsView.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 106 | `TODO` | // :TODO: track when we loose focus and save |

### `source/ui/data/appFile.cpp` (2)

| Line | Match | Text |
| --- | --- | --- |
| 60 | `TODO` | // :TODO: find dependencies, if there are some then delay the other changes |
| 143 | `kpanic(` | kpanic("runApps runOnRestartUI should not be set"); |

### `source/ui/data/boxedContainer.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 363 | `TODO` | // :TODO: parse link to get name |

### `source/ui/data/globalSettings.cpp` (2)

| Line | Match | Text |
| --- | --- | --- |
| 674 | `TODO` | continue; // :TODO: error msg? |
| 1072 | `TODO` | return B(""); // :TODO: error msg? |

### `source/ui/mainui.cpp` (2)

| Line | Match | Text |
| --- | --- | --- |
| 382 | `TODO` | // :TODO: should we stop the quit action if this save fails? |
| 664 | `TODO` | // :TODO: |

### `source/ui/utils/readIcons.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 646 | `not implemented` | kwarn("Icon 20b not implemented"); |

### `source/util/bstring.cpp` (2)

| Line | Match | Text |
| --- | --- | --- |
| 115 | `kpanic(` | kpanic("BString::BString oops"); |
| 570 | `TODO` | // :TODO in the future maybe don't make a copy, just a ref and use offsets into the data |

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

### `source/x11/x11common.cpp` (12)

| Line | Match | Text |
| --- | --- | --- |
| 740 | `TODO` | // :TODO: properly marshal |
| 769 | `TODO` | // :TODO: properly marshal |
| 1377 | `kpanic(` | //kpanic("x11_FreeColorMap"); |
| 1530 | `not implemented` | kwarn("x11_AllocColor COLOR_WRITE not implemented"); |
| 1773 | `not supported` | klog("x11 image not handled: bits per pixel or xoffset not supported by XPutPixel, XGetPixel, XSubImage or XAddPixel"); |
| 2803 | `unsupported` | EAX = 0; // XSetICValues returns the first unsupported attribute; none exist. |
| 2812 | `TODO` | // :TODO: |
| 3071 | `kpanic(` | kpanic("x11_ShapeCombineMask"); |
| 3075 | `kpanic(` | kpanic("x11_ShapeCombineRectangles"); |
| 3079 | `kpanic(` | kpanic("x11_ShapeOffsetShape"); |
| 3109 | `kpanic(` | kpanic("x11_WindowEvent"); |
| 3744 | `not supported` | klog("x11_CursorImagesLoadCursor animated cursors not supported, will use first frame"); |

### `source/x11/xdrawable.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 268 | `not supported` | kwarn_fmt("XDrawable::drawLine depth %d not supported", visual->bits_per_rgb); |

### `source/x11wire/xwireconnection.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 1820 | `unsupported` | kwarn_fmt("XWire: unsupported GLX request minor %u", unsigned(minor)); |

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
