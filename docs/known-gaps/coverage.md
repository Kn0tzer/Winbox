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
| Mode switch (XRandR) | `source/x11/x11common.cpp:2709-2789` (17 kpanics on `x11_XRR*`) | high | Any call to one of these aborts the emulator. Games that read or change the display mode through XRandR stop. | OPEN (work item 1) |
| Pointer grab options | `x11common.cpp:506,510,514` (kpanic on `owner_events`, `GrabModeSync`, `cursor`); `source/x11/xserver.cpp:1052` (ungrab with no active grab returns `GrabSuccess`; TODO says no spec use case was found) | high | A grab with those arguments aborts. An ungrab with no grab reports success, which hides a client error. | OPEN (work item 2) |
| Relative mouse / pointer lock | `source/x11/xserver.cpp:937` (TODO: "trial and error with vkQuake") | high | Mouse-look in FPS games depends on relative motion. The code has no spec basis for it. Whether real games hit it is not verified. | OPEN (work item 2) |
| Core drawing entry points | `x11common.cpp:246,452,1205,1681,1719,1723,1846,1878,1958,1962,1966,2260` (kpanic on `QueryTree`, `FreeFont`, `GetGeometry`, `GetPixel`, `SubImage`, `AddPixel`, `CopyPlane`, `SetDashes`, `SetArcMode`, `FillArc`, `DrawArc`, `SetFillStyle`) | high if reached | Each of these Xlib calls aborts the emulator. Arcs and dashed lines are common in game UIs. Whether the guest calls them is not verified. `ClearArea`, `DrawLines`, `FillRectangles`, `DrawPoint` and `FillPolygon` now draw (see the drawing-core and handler commits). | OPEN (work item 3) |
| Dropped pixel write | `x11common.cpp:1688` (`x11_PutPixel`) | high | `XPutPixel` now writes the pixel into the guest XImage for 8, 16, 24 and 32 bpp. Sub-byte depths and a non-zero xoffset are logged once and not written. | FIXED (test `x11-putpixel-writes.test.mjs`) |
| Fullscreen and window state (EWMH) | `source/x11/xwindow.cpp:610` (`_NET_WM_STATE` toggle kpanic); `:1032` (`CWSibling` kpanic) | high | A `_NET_WM_STATE` toggle or a sibling stacking request aborts the emulator. | OPEN (work item 5) |
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

1. **XRandR family (`x11common.cpp:2709-2789`).** Impact: high (mode-switch games abort). Approach: report the single host output with its current mode from the SDL display list. `XRRGetScreenResources`, `XRRGetCrtcInfo` and `XRRGetOutputInfo` return it. `XRRSetCrtcConfig` and `XRRSetScreenSize` succeed only for the current mode, and any other mode returns `BadValue` through a logged error. Test: a native harness that calls each entry point through the X wire and checks the replies.
2. **Pointer grab and relative mouse (`x11common.cpp:506,510,514`, `xserver.cpp:937,1052`).** Impact: high (FPS mouse look, drag-and-hold). Approach: implement `owner_events` as redirection in event delivery, `GrabModeSync` as an async grab with replay rejected loudly, and a per-grab cursor. Ungrab with no active grab returns Success per the spec, now with a log line. For relative mouse, call `SDL_SetRelativeMouseMode` while a grab is active. Test: the grab state machine on a fixture server, plus the event deltas.
3. **Core drawing entry points (the list in the table).** Impact: high if reached. Approach: most of these have an existing XDrawable method to delegate to (`fillRectangle` exists; `copyImageData` is clipped separately). Implement the fill and draw calls first, then `GetPixel` and `SubImage`. Reject the rest loudly with the function name. Test: each call against a reference pixel buffer, with a fixture drawable.
4. **`XPutPixel` write (`x11common.cpp:1688`).** DONE: the pixel is written into the guest XImage for 8, 16, 24 and 32 bpp in either byte order. Sub-byte depths and a non-zero xoffset are logged once and not written. See Fixes.
5. **EWMH state and stacking (`xwindow.cpp:610,1032`).** Impact: high (fullscreen toggles, sibling stacking). Approach: map `_NET_WM_STATE_FULLSCREEN` and `_NET_WM_STATE_MAXIMIZED_*` toggles to `SDL_SetWindowFullscreen` and `SDL_MaximizeWindow`. Implement `CWSibling` by inserting the window after its sibling in the stacking list. Test: property-change events on a fixture window.
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
  "source/x11/x11common.cpp": { "count": 78, "severity": "high", "area": "x11", "gap": "XRandR, XIM/IC, XShm, remaining drawing calls (arcs, dashes, GetPixel, SubImage, CopyPlane, SetFillStyle), XSendEvent and keyboard text abort; XGrabPointer owner_events and GrabModeSync logged, not honoured" },
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

### `source/x11/x11common.cpp` (78)

| Line | Match | Text |
| --- | --- | --- |
| 246 | `kpanic(` | kpanic("x11_QueryTree"); |
| 452 | `kpanic(` | kpanic("x11_FreeFont"); |
| 713 | `TODO` | // :TODO: properly marshal |
| 737 | `kpanic(` | kpanic("XSendEvent w=PointerWindow not implemented"); |
| 740 | `kpanic(` | kpanic("XSendEvent w=InputFocus not implemented"); |
| 748 | `TODO` | // :TODO: properly marshal |
| 775 | `kpanic(` | kpanic("XSendEvent propagate not implemented"); |
| 797 | `kpanic(` | kpanic("x11_LookupString status_in_out was not null"); |
| 826 | `kpanic(` | kpanic("x11_MbLookupString"); |
| 867 | `kpanic(` | kpanic("x11_KbTranslateKeysym extra_rtrn not implemented"); |
| 960 | `kpanic(` | kpanic("x11_RefreshKeyboardMapping"); |
| 1205 | `kpanic(` | kpanic("x11_GetGeometry"); |
| 1301 | `kpanic(` | //kpanic("x11_FreeColorMap"); |
| 1454 | `not implemented` | kwarn("x11_AllocColor COLOR_WRITE not implemented"); |
| 1681 | `kpanic(` | kpanic("x11_GetPixel"); |
| 1703 | `not supported` | klog("x11_PutPixel not handled: XPutPixel with bits per pixel or xoffset not supported"); |
| 1719 | `kpanic(` | kpanic("x11_SubImage"); |
| 1723 | `kpanic(` | kpanic("x11_AddPixel"); |
| 1846 | `kpanic(` | kpanic("x11_CopyPlane"); |
| 1856 | `kpanic(` | kpanic("x11_CreateGC valuemask not implemented"); // winex11 doesn't use this |
| 1878 | `kpanic(` | kpanic("x11_SetDashes"); |
| 1958 | `kpanic(` | kpanic("x11_SetArcMode"); |
| 1962 | `kpanic(` | kpanic("x11_FillArc"); |
| 1966 | `kpanic(` | kpanic("x11_DrawArc"); |
| 2260 | `kpanic(` | kpanic("x11_SetFillStyle"); |
| 2506 | `kpanic(` | kpanic("x11_SetWMProperties not implemented"); |
| 2512 | `kpanic(` | kpanic("x11_VaCreateNestedList"); |
| 2516 | `kpanic(` | kpanic("x11_UnsetICFocus"); |
| 2520 | `kpanic(` | kpanic("x11_SetICFocus"); |
| 2524 | `kpanic(` | kpanic("x11_DestroyIC"); |
| 2528 | `kpanic(` | kpanic("x11_SetICValues"); |
| 2532 | `kpanic(` | kpanic("x11_MbResetIC"); |
| 2537 | `TODO` | // :TODO: |
| 2547 | `kpanic(` | kpanic("x11_CloseIM"); |
| 2552 | `kpanic(` | kpanic("x11_SetIMValues"); |
| 2556 | `kpanic(` | kpanic("x11_GetIMValues"); |
| 2560 | `kpanic(` | kpanic("x11_DisplayOfIM"); |
| 2564 | `kpanic(` | kpanic("x11_UnregisterIMInstantiateCallback"); |
| 2573 | `kpanic(` | kpanic("x11_FreeStringList"); |
| 2594 | `kpanic(` | kpanic("x11_CreateIC"); |
| 2638 | `kpanic(` | kpanic("x11_DisplayName"); |
| 2642 | `kpanic(` | kpanic("x11_GetDefault"); |
| 2646 | `kpanic(` | kpanic("x11_GetWindowAttributes"); |
| 2650 | `kpanic(` | //kpanic("x11_IconifyWindow"); |
| 2654 | `kpanic(` | kpanic("x11_InternAtom"); |
| 2659 | `kpanic(` | kpanic("x11_LocaleOfIM"); |
| 2663 | `kpanic(` | kpanic("x11_MatchVisualInfo"); |
| 2684 | `kpanic(` | kpanic("x11_ShapeCombineMask"); |
| 2688 | `kpanic(` | kpanic("x11_ShapeCombineRectangles"); |
| 2692 | `kpanic(` | kpanic("x11_ShapeOffsetShape"); |
| 2696 | `kpanic(` | kpanic("x11_ShmAttach"); |
| 2701 | `kpanic(` | kpanic("x11_ShmCreateImage"); |
| 2705 | `kpanic(` | kpanic("x11_ShmDetach"); |
| 2709 | `kpanic(` | kpanic("x11_ShmPutImage"); |
| 2713 | `kpanic(` | kpanic("x11_WindowEvent"); |
| 2797 | `TODO` | cpu->memory->writed(ARG3, 1); // :TODO: maybe figure out 1.4? |
| 2811 | `kpanic(` | //kpanic("x11_XRRSetScreenConfig"); |
| 2843 | `kpanic(` | kpanic("x11_XRRFreeCrtcInfo"); |
| 2848 | `kpanic(` | kpanic("x11_XRRFreeOutputInfo"); |
| 2853 | `kpanic(` | kpanic("x11_XRRFreeScreenResources"); |
| 2858 | `kpanic(` | kpanic("x11_XRRGetCrtcInfo"); |
| 2863 | `kpanic(` | kpanic("x11_XRRGetOutputInfo"); |
| 2868 | `kpanic(` | kpanic("x11_XRRGetOutputProperty"); |
| 2873 | `kpanic(` | kpanic("x11_XRRGetScreenResources"); |
| 2878 | `kpanic(` | kpanic("x11_XRRGetScreenResourcesCurrent"); |
| 2883 | `kpanic(` | kpanic("x11_XRRGetScreenSizeRange"); |
| 2888 | `kpanic(` | kpanic("x11_XRRSetCrtcConfig"); |
| 2893 | `kpanic(` | kpanic("x11_XRRSetScreenSize"); |
| 2898 | `kpanic(` | kpanic("x11_XRRSelectInput"); |
| 2903 | `kpanic(` | kpanic("x11_XRRGetOutputPrimary"); |
| 2908 | `kpanic(` | kpanic("x11_XRRGetProviderResources"); |
| 2913 | `kpanic(` | kpanic("x11_XRRFreeProviderResources"); |
| 2918 | `kpanic(` | kpanic("x11_XRRGetProviderInfo"); |
| 2923 | `kpanic(` | kpanic("x11_XRRFreeProviderInfo"); |
| 2983 | `kpanic(` | kpanic("x11_XISelectEvents num_masks was expected to be 1"); |
| 2988 | `kpanic(` | kpanic("x11_XISelectEvents currently expecting masks in the first 32-bits"); |
| 3078 | `kpanic(` | kpanic("x11_CursorImageLoadCursor"); |
| 3120 | `not supported` | klog("x11_CursorImagesLoadCursor animated cursors not supported, will use first frame"); |

### `source/x11/xdrawable.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 240 | `not supported` | kwarn_fmt("XDrawable::drawLine depth %d not supported", visual->bits_per_rgb); |

### `source/x11/xserver.cpp` (1)

| Line | Match | Text |
| --- | --- | --- |
| 947 | `TODO` | // :TODO: I'm not really sure how realitive mouse is supposed to work, it was just trial and error with vkQuake |

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
