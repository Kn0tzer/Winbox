# Unowned known-gap coverage

Scope: every file the audit assigns to `UNOWNED` (see `tools/known-gaps-audit.mjs`, `OWNERS`). Those files belong to no lane: the X11 host and window layer, io/fs nodes, UI, util, platform host, `include/`, and `web/*.mjs` other than gl/vk. The lists below come from `node tools/known-gaps-audit.mjs` run on 2026-10-24. Vendored `lib/` is excluded (imgui, asmjit, glew, zlib, simde, tiny-process).

The scan also reads `platform/`, `include/`, `scripts/`, `tasks/`, `testdata/`, all of `project/`, and the `tools/buildWine/` patches. No upstream Wine tree is present locally, so those patches are scanned, not excluded.

This register is a marker scan. It finds `unsupported`, `not implemented`, `TODO`, `FIXME`, `kpanic(`, `ENOSYS`, `stub` and similar text in code. Zero hits does **not** mean a feature works. Behavioural gaps with no marker are listed under "Not covered by the scan" below.

## Summary

| Area | Files | Hits | Disposition |
| --- | --- | --- | --- |
| X11 host (`source/x11/`) | 4 | 96 | High. Aborts on many Xlib entry points, plus clipping that is partly silent and pixel writes that are dropped (now logged once). |
| X11 wire GLX (`source/x11wire/`) | 1 | 1 | High, if the GLX-over-wire path is used by a target build. |
| io / fs nodes (`source/io/`) | 8 | 19 | Low. The one guest-reachable abort (`FsVirtualNode::removeDir`) is fixed. |
| UI (`source/ui/`) | 8 | 11 | Low. Save-on-exit and error-message TODOs. |
| Platform host (`platform/`) | 5 | 11 | Medium for thread exception sync; low for cursor, audio device and CPU frequency. |
| softmmu (`source/emulation/softmmu/`) | 4 | 7 | Invariants only, see allowlist. |
| util (`source/util/`) | 6 | 13 | Invariants and vendored code, see allowlist. |
| `web/naga-rt.mjs`, `web/frame-metrics.mjs` | 2 | 5 | Medium for shader versions routed to ANGLE. The other hit is a UI label. |
| `include/`, `source/sdl/main.cpp` | 3 | 5 | Invariants and comments, see allowlist. |

Total: 41 files, 168 hits. 24 files are in the register below (counts may only go down). 17 are in `allowlist.json` with reasons. `--check` fails on any UNOWNED hit that is neither.

Severity: **high** = likely to crash or visibly break a real game, or to drop data silently. **medium** = breaks a feature or a subset of games. **low** = debug-only, cosmetic, or rarely used.

## Gap assessment by game impact

Ranked by severity if reached. "Reached" means the guest calls the path. This scan does not show that, and each row says so where it matters. Proposed work items are in "Proposed work items", with the same numbering.

| Category | Where | Severity | What a real game sees | Disposition |
| --- | --- | --- | --- | --- |
| Mode switch (XRandR) | `source/x11/x11common.cpp:3029-3246`, `source/x11/xserver.cpp:676` | high | The emulator exposes the host display size list as XRandR 1.2 modes on one connected primary output, applies valid mode changes, supports rotation 0, reports ranges/resources/CRTC/output data, and queues selected screen/CRTC/output change events. Other rotations fail with `RRSetConfigFailed`. | IMPLEMENTED — `x11-xrandr-set-screen-config.test.mjs`, `x11-xrandr-resources.test.mjs`, `x11-xrandr-events.test.mjs` |
| Pointer grab options | `x11common.cpp:506,510,514` plus wire grab state | high | owner_events and grab event masks were not applied; synchronous pointer grabs did not freeze input. | IMPLEMENTED: wire delivery honors owner_events and masks; sync/async pointer modes queue and release pointer events; relative mode and pointer lock are driven for confined grabs. Tests: `xwire-pointer-grab.test.mjs`. Grab cursor remains open under XInput/cursor work. |
| Relative mouse / pointer lock | `source/x11/xserver.cpp:937` (TODO: "trial and error with vkQuake") | high | Mouse-look in FPS games depends on relative motion. | IMPLEMENTED: relative motion positions advance and confined pointer grabs request SDL relative mode/browser pointer lock. Tests: `xwire-pointer-grab.test.mjs`. |
| Core drawing entry points | `x11common.cpp` (`QueryTree`, `GetPixel`, `SubImage`, `AddPixel`, `CopyPlane`, `CreateGC` valuemask, `SetDashes`, `SetArcMode`, `FillArc`, `DrawArc`, `SetFillStyle` were kpanics) | high if reached | Each of these Xlib calls aborted the emulator. Now none of them does. `SetDashes` stores the list, but dashed lines are still drawn solid; this is logged once. `XDrawString` is not in this list: text runs on the 64-bit wire path (`source/x11wire/xwireconnection.cpp`), not the legacy hostcall path. | FIXED (see Fixes; dashed lines are a caveat) |
| Dropped pixel write | `x11common.cpp:1688` (`x11_PutPixel`) | high | `XPutPixel` now writes the pixel into the guest XImage for 8, 16, 24 and 32 bpp. Sub-byte depths and a non-zero xoffset are logged once and not written. | FIXED (test `x11-putpixel-writes.test.mjs`) |
| Fullscreen and window state (EWMH) | `source/x11/xwindow.cpp` state handler; `_NET_ACTIVE_WINDOW` dispatch in `x11common.cpp` | high | Fullscreen add/remove/toggle now updates/restores window geometry; above state is retained; active-window messages set X input focus; root advertises supported atoms. `_NET_WM_NAME` and `_NET_WM_ICON` use property storage. `CWSibling` stacking remains open. | PARTLY IMPLEMENTED: `x11-ewmh-state.test.mjs`; CWSibling remains open. |
| Clipping | `source/x11/xdrawable.cpp` (`copyImageData`, `copy`, `plotPixel`, `clipAllows`) | high | Clip rectangles are honoured by fills, lines, image puts and copies. A `clip_mask` pixmap is not applied; it is logged once, not silent. | PARTLY FIXED: rectangles FIXED (tests `x11-copy-paths.test.mjs`, `x11-drawable-raster.test.mjs`); `clip_mask` OPEN (work item 6) |
| GLX over the X wire | `source/x11wire/xwireconnection.cpp:1739` (`GLX minor=%d (stub-reply)`) | high, if the path is used | A GLX request gets a stub reply with no real GL behind it. Logged with `klog_fmt`, so not silent, but wrong. | NEEDS_OTHER_LANE (gl), work item 7 |
| Keyboard text entry | `x11common.cpp:777` (`XLookupString` with non-null `status_in_out`), `:806` (`XmbLookupString`), `:847` (`XkbTranslateKeysym` with `extra_rtrn`), `:940` (`RefreshKeyboardMapping`) | medium | Xlib calls that turn key events into text abort. Text entry in a game that uses them stops the emulator. | OPEN (work item 8) |
| IME / XIM | `x11common.cpp:2372-2460` (14 kpanics on `x11_*IC` / `x11_*IM`); `:2403` TODO | medium (high for CJK input) | Any input-method call aborts the emulator. Composed text never arrives. | OPEN (work item 9) |
| Synthetic events | `x11common.cpp:717,720,755` (kpanic on `PointerWindow`, `InputFocus`, and `propagate`) | medium | `XSendEvent` with those targets or with `propagate` aborts. | OPEN (work item 10) |
| MIT-SHM | `x11common.cpp:2562-2575` (kpanic on `ShmAttach`, `ShmCreateImage`, `ShmDetach`, `ShmPutImage`) | medium | Clients that use shared-memory images abort. Whether this build advertises MIT-SHM is not verified here. | OPEN (work item 11) |
| Tiled fill | `source/x11/xdrawable.cpp` `fillRectangle` | medium | A fill with a tile pattern aborts the emulator. | FIXED (tile repeated from the tile origin; test `x11-drawable-raster.test.mjs`) |
| Other X11 kpanics | `x11common.cpp:2849,2854` (`XISelectEvents` shape checks); `:2944` (`CursorImageLoadCursor`); `:2663` TODO | low | Unusual XInput2 selections and Xcursor loads abort. | OPEN, registered |
| Shader versions routed to ANGLE | `web/naga-rt.mjs:70,283-286` | medium | Sources naga cannot lift are classed `unsupported-version` and left to ANGLE. Whether every routed shader then compiles is not verified here. | NEEDS_OTHER_LANE (gl), work item 13 |
| Thread exception sync (host) | `platform/linux/platformThreads-x64.cpp:146,187` (`syncFromException` kpanics) | medium | An unexpected host thread state aborts the emulator. | OPEN (work item 14) |
| Directory rmdir on virtual nodes | `source/io/fsvirtualnode.cpp:86-89` (was kpanic `removeDir not implemented`) | medium (before the fix) | Guest `rmdir` of a virtual directory aborted the emulator (`kprocess.cpp:1204-1211` calls `node->removeDir()`). | FIXED: logs and returns `-EPERM`. Test: `web/tests/suite/unit/fs-virtual-removedir.test.mjs`. |
| Virtual `setTimes` | `source/io/fsvirtualnode.cpp:92` (klog, then success) | low | `utimensat` on a virtual node reports success without storing the time, and logs it. | OPEN, registered |
| memfd nodes | `source/io/fsmemnode.cpp:28,33,46,59,64` (kpanic on rename, remove, open, removeDir, setTimes) | none for now | Unreachable: memfd nodes never enter the path namespace (see allowlist). Becomes reachable if `/proc/self/fd` is added. | ALLOWLISTED (work item 15) |
| Zip-backed write and truncate | `source/io/fszipopennode.cpp:43,236` (kpanic) | low | Unreachable from guest code: writable fds are rejected before `setLength` and `writeNative` (`kprocess.cpp:1680`), and write-open moves the file first (`fsfilenode.cpp:192`). | NOT_A_GAP for the write path; invariant kept |
| Async and event hooks on fs nodes | `fsdiropennode.cpp:63,71`; `fsfileopennode.cpp:97,105`; `fsmemopennode.cpp:63,71`; `fsvirtualopennode.h:37,39`; `fszipopennode.cpp:75,83` | low | `setAsync` and `waitForEvents` do nothing and log at debug level. `O_ASYNC` therefore never delivers a signal. | OPEN (work item 16) |
| Audio output device | `platform/sdl/knativeaudio.cpp:77` (TODO: device choice) | low | Audio always uses the default device. The scan found no other marker in audio output or input. Their correctness is not assessed here. | OPEN, registered |
| CPU frequency reporting | `platform/linux/platform.cpp:130,134,138` (returns 0) | low | `/proc/cpuinfo` shows 0 MHz; `sysfs` hides the MHz node when 0 (`source/kernel/sys/sysfs.cpp:54`). | OPEN (work item 17) |
| Cursor shapes | `platform/sdl/knativescreenSDL.cpp:616,626,661` (TODO: approximate shapes) | low | Some X cursor shapes map to the nearest SDL system cursor. | OPEN, registered |
| UI save and error paths | `source/ui/mainui.cpp:382,664`; `source/ui/controls/optionsView.cpp:106`; `source/ui/data/globalSettings.cpp:674,1072`; `source/ui/data/boxedContainer.cpp:363`; `source/ui/data/appFile.cpp:60`; `source/ui/controls/downloadDlg.cpp:156` | low | Settings not saved on focus loss, quit not checked against a failed save, error messages missing, a link name not parsed, a download socket with no stuck-socket handling. None is game-facing. | OPEN, registered |
| Icon format | `source/ui/utils/readIcons.cpp:646` (`Icon 20b not implemented`, logged with `kwarn`) | low | One icon format is not shown. Loud and UI-only. | OPEN, registered |
| bstring copy | `source/util/bstring.cpp:570` (TODO) | low | Performance note only. | OPEN, registered |

### Not covered by the scan

These categories were requested for the game-impact review. The scan found no marker for them in scope, so this lane does not count them as covered. Each needs a behavioural probe before anyone states a status.

| Category | Scan result | Status |
| --- | --- | --- |
| Audio output and input | One TODO (`knativeaudio.cpp:77`). | Not assessed. |
| Gamepad / XInput | No marker in `platform/sdl/` input or `source/x11/`. | Not assessed. |
| Window, cursor, fullscreen, pointer lock | Markers in the table above (mode switch, relative mouse, EWMH, grab). | Partly assessed. |
| Clipboard (X selections) | No marker. | Not assessed. |
| IME | See the IME row above. | Partly assessed. |
| Networking sockets and DNS | No marker in scope. | Not assessed. |
| File dialogs | No marker in scope. | Not assessed. |
| Filesystem semantics (case-insensitivity, symlinks) | Symlinks are reported and not followed (35 in the scan): `web/vendor` (intentional) and fontconfig and `sh` links in `tools/rootfs64`. Case-insensitivity has no marker. | Symlinks: visible, not followed. Case: not assessed. |
| Timers | No marker. | Not assessed. |

The scan's pattern is also narrow. It misses a plain `not supported` outside `kpanic(` and `klog_fmt`, a capitalised `Not implemented`, and a silent `return -K_ENOSYS` with no text. Those need a separate pass.

## Proposed work items

Ranked by severity if reached, then by how likely a real game is to reach the path. Each is OPEN and has an implementation approach. None is started in this lane.

1. **XRandR family (`x11common.cpp:3029-3246`, `xserver.cpp:676`).** DONE. XRandR 1.2 exposes the host size list, current one-output topology, CRTC/output info, size range, primary output, mode changes and screen/CRTC/output notifications. The emulated output supports only rotation 0 and rejects unsupported rotations/modes. Regression probes: `x11-xrandr-set-screen-config.test.mjs`, `x11-xrandr-resources.test.mjs`, and `x11-xrandr-events.test.mjs`.
2. **Pointer grab and relative mouse (`x11common.cpp:506,510,514`, `xserver.cpp:937,1052`).** IMPLEMENTED: owner_events and event masks route correctly; synchronous pointer grabs freeze and release one event per SyncPointer; async modes drain the pending queue; confined grabs request SDL relative mode/browser pointer lock. Tests: `xwire-pointer-grab.test.mjs`. Remaining grab cursor behavior is tracked with XInput/cursor work.
3. **Core drawing entry points (the list in the table).** DONE: every listed kpanic is implemented, and the tests are listed in Fixes. Open follow-up: dashed lines are drawn solid, so `XSetDashes` needs a dash pattern in the line drawing code.
4. **`XPutPixel` write (`x11common.cpp:1688`).** DONE: the pixel is written into the guest XImage for 8, 16, 24 and 32 bpp in either byte order. Sub-byte depths and a non-zero xoffset are logged once and not written. See Fixes.
5. **EWMH state and stacking (`xwindow.cpp:610,1032`).** PARTLY IMPLEMENTED: state add/remove/toggle deduplicates atoms; fullscreen resizes to the root and restores prior geometry; `_NET_ACTIVE_WINDOW` updates X focus; `_NET_SUPPORTED` advertises fullscreen, above, active, name and icon support. Tests: `x11-ewmh-state.test.mjs`. Remaining: compositor stacking for `_NET_WM_STATE_ABOVE`, icons/names display integration, and `CWSibling` stacking.
6. **Clip regions (`xdrawable.cpp`).** Impact: high (silent wrong pixels). DONE for clip rectangles: fills, lines, image puts and copies are clipped, and the silent sites are gone. Still open: a `clip_mask` pixmap is not applied. It is logged once per process, not silent. Applying it needs a per-pixel bitmap test. Test for the rectangles: `x11-copy-paths.test.mjs` and `x11-drawable-raster.test.mjs`.
7. **GLX over the X wire (`xwireconnection.cpp:1739`).** Impact: high if used. NEEDS_OTHER_LANE (gl). Approach: route each GLX minor to a real reply from the host GLX or EGL path, or return `BadRequest` with a log line. Test: the GLX minor table checked against the protocol spec.
8. **Keyboard text entry (`x11common.cpp:777,806,847,940`).** Impact: medium-high (text fields in games). Approach: implement `XLookupString` and `XmbLookupString` for the status-out case, with the keysym-to-string table already used by key events. Implement the `extra_rtrn` path for `XkbTranslateKeysym`. `RefreshKeyboardMapping` re-reads the keymap. Test: a key event sequence checked against the expected string.
9. **XIM / IC family (`x11common.cpp:2372-2460`).** Impact: medium (high for CJK). Approach: `XOpenIM` returns NULL when no input method is configured. Xlib clients then fall back to plain keysyms, which removes the aborts. Composition is a separate, larger item. Test: a client that calls `XOpenIM` gets NULL and does not abort.
10. **Synthetic events (`x11common.cpp:717,720,755`).** Impact: medium. Approach: deliver to the pointer window and the focus window the way the server already delivers real events. Implement `propagate` by walking up the window tree. Test: a synthetic `ButtonPress` to a child window.
11. **MIT-SHM (`x11common.cpp:2562-2575`).** Impact: medium if advertised. Approach: first verify whether the extension is advertised. If it is, back the shm segment with the existing guest-memory mapping, and if it is not, leave it unadvertised so clients fall back. Test: the extension query reply, and an `XShmPutImage` round trip.
12. **Tiled fill (`xdrawable.cpp:216`).** Impact: medium-low. Approach: repeat the tile pixmap across the fill rectangle, offset by the tile origin. Test: a 2x2 tile over a 5x3 rectangle, against a reference.
13. **Naga to ANGLE fallback (`web/naga-rt.mjs:70,283-286`).** Impact: medium. NEEDS_OTHER_LANE (gl). Approach: confirm the ANGLE route exists for each `unsupported-version` source. Add a test that feeds one such source through the route and checks compile success or a loud failure.
14. **Host thread exception sync (`platformThreads-x64.cpp:146,187`).** Impact: medium. Approach: record the unexpected thread state as a guest fault and deliver it through the existing signal path. Needs kernel-lane coordination. Test: an injected exception-state fixture.
15. **memfd `FsMemNode` (`fsmemnode.cpp`).** Allowlisted as unreachable. If `/proc/self/fd` is added, the rename, remove, open, removeDir and setTimes paths become reachable and must be implemented first. Test: the allowlist reason is the guard.
16. **Async fs events (the io group above).** Impact: low. Approach: implement `O_ASYNC` as SIGIO delivery, only when a target app uses it. Test: a pipe with `F_SETFL O_ASYNC`.
17. **CPU frequency (`platform.cpp:130,134,138`).** Impact: low. Approach: read `/sys/devices/system/cpu/cpu0/cpufreq` on hosts that expose it. Test: a non-zero value on a host that exposes it, and absent otherwise.

## NEEDS_OTHER_LANE

- **Kernel**: `source/emulation/softmmu/kmemory_soft.cpp:157` (kpanic on a mapped-file offset that is not page-aligned). Allowlisted on the premise that `source/kernel/syscall64.cpp:1627` aligns the offset before mapping. Kernel lane: confirm that premise, or make the syscall return `-EINVAL` for an unaligned offset.
- **Kernel**: `platform/linux/platformThreads-x64.cpp:146,187` may need kernel-lane coordination. See work item 14.
- **GL**: `source/x11wire/xwireconnection.cpp:1739` (GLX stub reply). See work item 7.
- **GL**: `web/naga-rt.mjs:70,283-286` depends on the ANGLE fallback existing. See work item 13.

## Fixes applied

- **`source/io/fsvirtualnode.cpp:86-89`** (`FsVirtualNode::removeDir`): a guest `rmdir` of a virtual directory aborted the emulator. It now logs the attempt and returns `-K_EPERM`. Virtual directories are generated, so the guest cannot remove one, and `EPERM` is the errno for an operation the node does not permit. This is a choice, not a copy of a host result; the host was not probed. Regression: `web/tests/suite/unit/fs-virtual-removedir.test.mjs`, which compiles the real function body against stub `kpanic` and `klog` and checks the return value, the log count, and that no panic is reached. The test fails on the original code and passes with the fix. The register entry drops from 2 (medium) to 1 (low, `setTimes`).
- **`source/x11/x11common.cpp:1688`** (`x11_PutPixel`): `XPutPixel` edits the image in client memory, so the handler reads the guest XImage header and writes the pixel bytes at `data + bytes_per_line * y + x * bytes`. It writes 8, 16, 24 and 32 bpp, most significant byte first for `MSBFirst`. A pixel outside the image is ignored and the call returns 0. Sub-byte depths and a non-zero `xoffset` are logged once per process and not written. The return value of 1 for a written pixel is taken from libX11's convention as remembered, not checked against a host libX11 here. Regression: `web/tests/suite/unit/x11-putpixel-writes.test.mjs`, which compiles the real body against a stub guest memory and checks the bytes for each depth and byte order. It replaces `x11-putpixel-logged.test.mjs`, which asserted the drop. The test fails on the original code.
- **`source/x11/x11common.cpp` `x11_GetGeometry` and `x11_FreeFont`**: `XGetGeometry` aborted the emulator. It now writes the root, the position in the parent, the border, the size and the depth to the guest pointers, and returns 1. A pixmap reports position and border as 0. An unknown drawable returns `BadDrawable` and writes nothing. `XFreeFont` aborted too; the legacy path never loads a font, so it now returns `Success`. Regression: `web/tests/suite/unit/x11-getgeometry.test.mjs` and `web/tests/suite/unit/x11-freefont.test.mjs`, which compile the real bodies against stubs and check the results. Both fail on the original code.
- **Core drawing calls in `source/x11/x11common.cpp` and `source/x11/xdrawable.cpp`** (were kpanics): `XQueryTree` lists the root, the parent and every child, bottom-most first, from `XWindow::getChildrenBottomToTop`. `XGetPixel` reads a pixel from the guest XImage, and `XSubImage` returns a new guest XImage holding a region, with 0 where the region is outside the source. `XAddPixel` adds to each pixel and wraps at the depth. Images are handled only for byte-aligned depths up to 32 bits and a zero xoffset; others are logged once. `XCopyPlane` paints the GC foreground for set bits of the plane and the background for clear bits, through the GC function and clip. `XCreateGC` honours the valuemask. `XSetFillStyle` and `XSetArcMode` store their values, and `XFillArc` and `XDrawArc` draw pie, chord and full ellipses, with the tile honoured for fills. `XSetDashes` stores the list but dashed lines are still drawn solid, logged once. Regression: `x11-querytree`, `x11-image-pixels`, `x11-copyplane`, `x11-gc-setters` and `x11-arc-drawing` unit tests, each compiling the real body against a stub and failing on the original code, plus the `x11-putpixel-writes` test, which now also covers `XGetPixel`. The C++ build itself was not compiled in this lane; see the report.
- **`source/x11/xdrawable.cpp` copy paths** (`copyImageData`, `copy`): both ignored the GC clip, applied only GXcopy and, for 32-bit, GXxor, and wrote a negative destination before the buffer. `copy` also corrupted a copy inside one drawable when the destination was below the source. Both now cut the copy to the drawable, honour the clip rectangles and every GX function for 32-bit, and read each source row before a later row overwrites it. `putImage` no longer logs a function it honours. A `clip_mask` pixmap is logged once. Regression: `web/tests/suite/unit/x11-copy-paths.test.mjs`, which compiles the real functions against a stub guest memory and checks each case. It fails on the original code. Its clip-mask case fails at load there because its slice marker is new, so that case is not checked on the original code by its own assertion.

## Register

Machine-readable. `tools/known-gaps-audit.mjs --check` reads this block. Counts may only go down without a new justification. A file that is not in this block and not in `allowlist.json` fails `--check`.

<!-- known-gaps:register:begin -->
```json
{
  "platform/linux/platform.cpp": { "count": 3, "severity": "low", "area": "platform", "gap": "CPU frequency reported as 0 (TODO)" },
  "platform/linux/platformThreads-x64.cpp": { "count": 2, "severity": "medium", "area": "platform", "gap": "syncFromException kpanics on an unexpected thread state" },
  "platform/sdl/knativeaudio.cpp": { "count": 1, "severity": "low", "area": "audio", "gap": "TODO: no audio device choice" },
  "platform/sdl/knativescreenSDL.cpp": { "count": 5, "severity": "low", "area": "cursor", "gap": "approximate X cursor shapes (TODO); 8-bit screenshots not supported (logged); recorder invariant kpanic" },
  "source/io/fsdiropennode.cpp": { "count": 2, "severity": "low", "area": "fs", "gap": "setAsync and waitForEvents are no-ops" },
  "source/io/fsfileopennode.cpp": { "count": 2, "severity": "low", "area": "fs", "gap": "setAsync and waitForEvents are no-ops" },
  "source/io/fsmemopennode.cpp": { "count": 2, "severity": "low", "area": "fs", "gap": "setAsync and waitForEvents are no-ops" },
  "source/io/fsvirtualnode.cpp": { "count": 1, "severity": "low", "area": "fs", "gap": "setTimes logs and succeeds" },
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
  "source/x11/x11common.cpp": { "count": 46, "severity": "high", "area": "x11", "gap": "XIM/IC, XShm, XSendEvent and keyboard text abort; XSetDashes is stored but dashed lines are drawn solid" },
  "source/x11/xdrawable.cpp": { "count": 1, "severity": "medium", "area": "x11", "gap": "clip_mask pixmap logged once, not applied; sub-byte and 8-bit drawing and copies logged, not drawn" },
  "source/x11/xserver.cpp": { "count": 1, "severity": "high", "area": "x11", "gap": "relative mouse for the legacy path is not driven (wire path does it)" },
  "source/x11/xwindow.cpp": { "count": 4, "severity": "high", "area": "x11", "gap": "_NET_WM_STATE toggle and CWSibling abort; two TODOs" },
  "source/x11wire/xwireconnection.cpp": { "count": 1, "severity": "high", "area": "x11", "gap": "GLX minor answered with a stub reply" },
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

### `platform/linux/platform.cpp` (3)

| Line | Match | Text |
| --- | --- | --- |
| 130 | `TODO` | return 0; // :TODO: |
| 134 | `TODO` | return 0; // :TODO: |
| 138 | `TODO` | return 0; // :TODO: |

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
| 527 | `kpanic(` | kpanic("KNativeInputSDL::processCustomEvents should only be called on main thread"); |

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

### `source/io/fsvirtualnode.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 93 | `not implemented` | klog("FsVirtualNode::setTimes not implemented"); |

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

### `source/x11/x11common.cpp` (46)

| Line | Match | Text |
| --- | --- | --- |
| 740 | `TODO` | // :TODO: properly marshal |
| 764 | `kpanic(` | kpanic("XSendEvent w=PointerWindow not implemented"); |
| 767 | `kpanic(` | kpanic("XSendEvent w=InputFocus not implemented"); |
| 775 | `TODO` | // :TODO: properly marshal |
| 802 | `kpanic(` | kpanic("XSendEvent propagate not implemented"); |
| 824 | `kpanic(` | kpanic("x11_LookupString status_in_out was not null"); |
| 853 | `kpanic(` | kpanic("x11_MbLookupString"); |
| 894 | `kpanic(` | kpanic("x11_KbTranslateKeysym extra_rtrn not implemented"); |
| 987 | `kpanic(` | kpanic("x11_RefreshKeyboardMapping"); |
| 1351 | `kpanic(` | //kpanic("x11_FreeColorMap"); |
| 1504 | `not implemented` | kwarn("x11_AllocColor COLOR_WRITE not implemented"); |
| 1747 | `not supported` | klog("x11 image not handled: bits per pixel or xoffset not supported by XPutPixel, XGetPixel, XSubImage or XAddPixel"); |
| 2732 | `kpanic(` | kpanic("x11_SetWMProperties not implemented"); |
| 2738 | `kpanic(` | kpanic("x11_VaCreateNestedList"); |
| 2742 | `kpanic(` | kpanic("x11_UnsetICFocus"); |
| 2746 | `kpanic(` | kpanic("x11_SetICFocus"); |
| 2750 | `kpanic(` | kpanic("x11_DestroyIC"); |
| 2754 | `kpanic(` | kpanic("x11_SetICValues"); |
| 2758 | `kpanic(` | kpanic("x11_MbResetIC"); |
| 2763 | `TODO` | // :TODO: |
| 2773 | `kpanic(` | kpanic("x11_CloseIM"); |
| 2778 | `kpanic(` | kpanic("x11_SetIMValues"); |
| 2782 | `kpanic(` | kpanic("x11_GetIMValues"); |
| 2786 | `kpanic(` | kpanic("x11_DisplayOfIM"); |
| 2790 | `kpanic(` | kpanic("x11_UnregisterIMInstantiateCallback"); |
| 2799 | `kpanic(` | kpanic("x11_FreeStringList"); |
| 2820 | `kpanic(` | kpanic("x11_CreateIC"); |
| 2857 | `kpanic(` | kpanic("x11_DisplayName"); |
| 2861 | `kpanic(` | kpanic("x11_GetDefault"); |
| 2865 | `kpanic(` | kpanic("x11_GetWindowAttributes"); |
| 2869 | `kpanic(` | //kpanic("x11_IconifyWindow"); |
| 2873 | `kpanic(` | kpanic("x11_InternAtom"); |
| 2878 | `kpanic(` | kpanic("x11_LocaleOfIM"); |
| 2882 | `kpanic(` | kpanic("x11_MatchVisualInfo"); |
| 2903 | `kpanic(` | kpanic("x11_ShapeCombineMask"); |
| 2907 | `kpanic(` | kpanic("x11_ShapeCombineRectangles"); |
| 2911 | `kpanic(` | kpanic("x11_ShapeOffsetShape"); |
| 2915 | `kpanic(` | kpanic("x11_ShmAttach"); |
| 2920 | `kpanic(` | kpanic("x11_ShmCreateImage"); |
| 2924 | `kpanic(` | kpanic("x11_ShmDetach"); |
| 2928 | `kpanic(` | kpanic("x11_ShmPutImage"); |
| 2932 | `kpanic(` | kpanic("x11_WindowEvent"); |
| 3401 | `kpanic(` | kpanic("x11_XISelectEvents num_masks was expected to be 1"); |
| 3406 | `kpanic(` | kpanic("x11_XISelectEvents currently expecting masks in the first 32-bits"); |
| 3496 | `kpanic(` | kpanic("x11_CursorImageLoadCursor"); |
| 3538 | `not supported` | klog("x11_CursorImagesLoadCursor animated cursors not supported, will use first frame"); |

### `source/x11/xdrawable.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 270 | `not supported` | kwarn_fmt("XDrawable::drawLine depth %d not supported", visual->bits_per_rgb); |

### `source/x11/xserver.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 1000 | `TODO` | // :TODO: I'm not really sure how realitive mouse is supposed to work, it was just trial and error with vkQuake |

### `source/x11/xwindow.cpp` (4)

| Line | Match | Text |
| --- | --- | --- |
| 616 | `kpanic(` | kpanic("XWindow::handleNetWmStatePropertyEvent toggle not handled"); |
| 1033 | `TODO` | // :TODO: |
| 1063 | `kpanic(` | kpanic("XReconfigureWMWindow CWSibling not handled"); |
| 1082 | `TODO` | // :TODO: |

### `source/x11wire/xwireconnection.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 1739 | `stub` | klog_fmt("XWire: GLX minor=%d (stub-reply) seq=%d", (int)minor, (int)sequence); |

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
