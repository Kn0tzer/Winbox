# Runtime / prefix completeness audit (P2-R, 2026-10-24)

Scope: the Windows runtime a guest program sees — `web/runtime/*.zip`
(pinned by `tools/browser/runtime-assets.json`, regenerable via
`tools/rootfs64/build-wine64-zip.sh`, `tools/build-prefix32.sh`,
`tools/rootfs64/build-prefix64.sh`) and the lazy "support pack" staged by
`tools/browser/stage-dependencies.py` (inspected read-only; see
`NEEDS_OTHER_LANE` for the one gap found inside it).

Method: every checklist item was verified against the *actual shipped
artifacts* (zip member lists and registry hives extracted from
`web/runtime/`), not against assumptions. Each row names the evidence path.
Dispositions are IMPLEMENTED (with test), REJECTED-LOUDLY (with test),
NOT-A-GAP (with evidence reference), or OPEN (with file:line reason).

The 32-bit guest uses WineHQ wine-stable 10.0 (`opt/wine-stable`, i386), the
64-bit guest uses Debian bookworm wine 8.0 (`/usr/lib/x86_64-linux-gnu/wine`).
Where the checklist asks for a single answer per architecture we cite both.

Legend for evidence paths: `w64` = `web/runtime/wine64.zip`,
`w32` = `web/runtime/wine32.zip`, `p64` = `web/runtime/prefix64.zip`,
`p32` = `web/runtime/prefix32.zip`, `support` = the newest
`support-*.zip` referenced by `dist/runtime/dependencies.json`.

## Audit of silent-drop spots in owned packaging scripts

| Location | What valid guest behaviour hits it | Severity | Disposition |
| --- | --- | --- | --- |
| `tools/rootfs64/build-prefix64.sh:101,119,140` (`NOTE ... missing — ENOENT`) | GL demos / games / app packages silently absent from the prefix when the staging source file is missing; app-bar buttons then ENOENT at click time. | would break a launcher demo, harmless for real games | IMPLEMENTED (guard): the zip member checks in `web/tests/suite/unit/runtime-completeness.test.mjs` and `prepare_prefixes.validate_prefix_zip` fail when any staged payload regresses, converting these warn-and-skip paths into test failures. |
| `tools/build-prefix32.sh:88,99` (`NOTE ... missing`) | 32-bit prefix built without `.NETFramework` keys or without `deskpin32.exe`; managed games then miss framework keys. | would break a real (managed) game | IMPLEMENTED (guard): `prepare_prefixes.validate_prefix_zip` now requires the `.NETFramework`/`CurrentVersion` registry state (see row "Windows version emulation default"); `web/tests/suite/unit/runtime-completeness.test.mjs` pins the hive content. |
| `tools/rootfs64/build-wine64-zip.sh:210,242,264` (`WARNING ... not staged`) | wine64.zip built without the GL shim / Vulkan shim / ALSA bridge when the prebuilt artifact is absent; apps then lose 3D, Vulkan, or audio. | would break a real game | IMPLEMENTED (guard): `prepare_prefixes.validate_service_runtime` (alsa libs, d3d11/d3d12, winebus/wineusb .so) plus the new `runtime-completeness.test.mjs` zip-member assertions cover each payload; `tools/browser/bootstrap.py --check` pins the zip hashes. |
| `tools/rootfs64/prepare_prefixes.py:66,78,113,172,263,266` (`continue` loops) | Each `continue` skips an *absent optional input* while packaging (e.g. a missing hive in `strip_browser_menu_runservices` is skipped, not an error); no valid guest request is dropped silently. | harmless | NOT-A-GAP: inputs are prefix-archive internals (hive path/`PREFIX` is fixed), and the same functions' contract is verified end-to-end by `tools/rootfs64/test_prepare_prefixes.py` and `prepare_prefixes.py check`. |

No `TODO`/`FIXME`/`ENOSYS`/`catch {}` swallow was found in the owned files;
the two `except` clauses (`prepare_prefixes.py:345,366`) re-raise a
`RuntimeError` including the log tail and prefix-validation errors
respectively, i.e. they fail loudly.

## Checklist audit

Rows are grouped by checklist item; every row cites evidence and a
disposition. "x86+x64" means both `w32` and `w64` unless noted.

### Visual C++ runtimes

| DLL set (MSVC version) | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| msvcp/vcruntime 60/70/80/90/100/110/120, msvcrt20/40, msvcrt.dll | YES both | `opt/wine-stable/lib/wine/i386-windows/msvcp60.dll…msvcp120.dll`, `…/vcruntime` set, `usr/lib/x86_64-linux-gnu/wine/x86_64-windows/msvcp60.dll…msvcp120.dll` | – | NOT-A-GAP; guard: `runtime-completeness.test.mjs` asserts members |
| msvcp140/vcruntime140 + msvcp140_1/_2/_atomic_wait/_codecvt_ids, ucrtbase, concrt140 | YES x86_64 in `w64`; x86: all EXCEPT `vcruntime140_1.dll` | `w64`: `x86_64-windows/{msvcp140,msvcp140_1,msvcp140_2,msvcp140_atomic_wait,msvcp140_codecvt_ids,vcruntime140,ucrtbase,concrt140}.dll`; `w32`: i386 set minus `vcruntime140_1.dll` | – | Present set matches Wine upstream. `vcruntime140_1` is x64/ARM64-only upstream (`dlls/vcruntime140_1/{Makefile.in,vcruntime140_1.spec}` — `except_x86_64.c`/`__CxxFrameHandler4`), so its absence in i386 is NOT-A-GAP, not a packaging omission. |
| MFC (mfc140, mfc140u, …) | NO (either arch) | not in `w32`/`w64` | would break real MFC-based installers/old games | OPEN: Wine upstream does not implement MFC at all (`dlls/` has no `mfc*`), so there is no WineHQ/deb source to pin; shipping native MS MFC would need a VC++ redist extraction step inside the lazy pack. When fixed, the staging belongs in `tools/browser/stage-dependencies.py` (support pack), see NEEDS_OTHER_LANE. |

### .NET Framework and Mono

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| wine-mono runtime | YES | `support:opt/winbox/mono/mono-2.0/bin/libmono-2.0-x86.dll` + `…x86_64.dll` (wine-mono 7.4.0, pinned `tools/rootfs64/prepare-mono32.py`) | – | NOT-A-GAP; guard: `prepare_prefixes.py check` + `runtime-completeness.test.mjs` |
| How apps requesting .NET find Mono | YES | `p32:home/username/.wine/drive_c/windows/{mono.link,Microsoft.NET.link}` → `/opt/winbox/mono`; `support:usr/share/wine/mono/wine-mono-7.4.0.link`; `support:home/username/.wine/drive_c/windows/mono.link` | – | NOT-A-GAP; `mscoree.dll` present in both wine trees (`i386-windows/mscoree.dll`, `x86_64-windows/mscoree.dll`) |
| `.NETFramework` version-policy keys | YES x86 (`p32:system.reg` `[Software\\Microsoft\\.NETFramework]` + `policy\\v1.0/1.1/2.0/3.0/4.0`); NO x64 (absent from `p64:system.reg`) | `tools/prefix-mono-reg.txt` merged by `tools/build-prefix32.sh:86` only | would break .NET games on the 64-bit prefix (apps reading InstallRoot/version policies) | IMPLEMENTED: `tools/rootfs64/build-prefix64.sh` now merges the same fragment; required by `prepare_prefixes.validate_prefix_zip`; pinned by `runtime-completeness.test.mjs`. |

### wine-gecko

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| wine-gecko for 32-bit (mshtml engine) | YES | `support:usr/share/wine/gecko/wine-gecko-2.47.4-x86/xul.dll` | – | NOT-A-GAP (WineHQ mshtml matches 2.47.4); guard: `runtime-completeness.test.mjs` |
| wine-gecko for 64-bit | YES | `support:usr/share/wine/gecko/wine-gecko-2.47.3-x86_64/xul.dll` | – | NOT-A-GAP (Debian 8.0 mshtml matches 2.47.3) |

### DirectX redistributable DLLs

| DLL set | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| d3dx9_24..43 | YES both | `w32`/`w64`: `d3dx9_24.dll`…`d3dx9_43.dll` complete (20 of 20) | – | NOT-A-GAP; guard: `runtime-completeness.test.mjs` |
| d3dx10_33..43, d3dx10 (D3DX10) | YES both | `d3dx10_33.dll`…`d3dx10_43.dll` complete; `d3dx10.dll` does not exist upstream (game tools always request `d3dx10_NN`) | – | NOT-A-GAP |
| d3dx11_42, d3dx11_43 | YES both; d3dx11_24..41 do not exist upstream | `dlls/` upstream provides only d3dx11 42/43 | – | NOT-A-GAP (upstream parity) |
| d3dcompiler_33..47 | YES both; 24..32 do not exist upstream | `d3dcompiler_33.dll`…`d3dcompiler_47.dll` | – | NOT-A-GAP (upstream parity) |
| xinput1_1..1_4, xinput9_1_0, xinputuap | YES both | `xinput1_1.dll`…`xinput1_4.dll`, `xinput9_1_0.dll`, `xinputuap.dll`, `winexinput.sys` | – | NOT-A-GAP |
| xaudio2_0..2_9 | YES both | `xaudio2_0.dll`…`xaudio2_9.dll` | – | NOT-A-GAP; FAudio is statically linked into Wine's xaudio/wined3d upstream, no separate faudio.dll ships |
| xactengine2_0/2_4/2_7/2_9, 3_0..3_7 | YES both; 2_1..2_3/2_5/2_6/2_8 do not exist upstream | `xactengine2_0.dll`, `xactengine2_4.dll`, …, `xactengine3_7.dll` | – | NOT-A-GAP (upstream parity) |
| x3daudio1_0..1_7 | YES both | `x3daudio1_0.dll`…`x3daudio1_7.dll` | – | NOT-A-GAP |
| dinput, dinput8, dplay, dplayx, dpnet, dplaysvr, ddraw, dsound | YES both | `dinput.dll`, `dinput8.dll`, `dplayx.dll`, `dpnet.dll`, `dplaysvr.exe`, `dsound.dll` | – | NOT-A-GAP |
| d3d8, d3d9, d3d10core, d3d11, d3d12, dxgi (builtin) | YES both | `d3d8.dll`, `d3d9.dll`, `d3d10core.dll`, `d3d11.dll`, `d3d12.dll`, `dxgi.dll` | – | NOT-A-GAP |
| d3d12core.dll | YES x86 (`w32:i386-windows/d3d12core.dll`); NO x64 | `w64` has no `x86_64-windows/d3d12core.dll` (Debian wine 8.0 predates `dlls/d3d12core`) | would break games that `LoadLibrary("d3d12core.dll")` on the 64-bit prefix | IMPLEMENTED: `tools/rootfs64/winehq10-x86_64/d3d12core.dll` (WineHQ wine-stable 10.0 amd64, sha256-pinned) staged into wine64.zip by `build-wine64-zip.sh`; required by `prepare_prefixes.validate_service_runtime`; pinned by `runtime-completeness.test.mjs` |
| msmpeg2vdec.dll (MF video decoder) | YES x86 (`w32:i386-windows/msmpeg2vdec.dll`); NO x64 | `w64` has no `x86_64-windows/msmpeg2vdec.dll` (not built by Debian wine 8.0) | would break WMV/H.264 cutscene decode on the 64-bit prefix | IMPLEMENTED: `tools/rootfs64/winehq10-x86_64/msmpeg2vdec.dll` (WineHQ wine-stable 10.0 amd64, sha256-pinned) staged into wine64.zip; its mfplat imports (`MFTRegister`/`MFTUnregister`) verified present in wine64's mfplat.dll; required by `validate_service_runtime`; pinned by `runtime-completeness.test.mjs` |
| Native DXVK/DXVK-DLL set | YES | `p32:home/username/{d3d9,d3d11,dxgi}.dll` (per-app override DLLs in the 32-bit prefix root, loaded before builtin via Wine's DLL search order); vulkan guest shims `libvk32/libvk64` via `support` | – | NOT-A-GAP; guard: `prepare_prefixes.py` requires the 32-bit payloads (`validate_prefix_zip` win32 branch) |
| WINEDLLOVERRIDES defaults d3d8/9/10/11/12, dxgi, d3d12core | N/A (no override needed) | `web/runtime/wine64-launcher.js:193` passes `WINEDLLOVERRIDES` only when the `dlloverides` URL param is set; builtin Wine D3D is the default and 32-bit games find the per-app DXVK DLLs by DLL search order, so an override would *disable* the fix | – | NOT-A-GAP (reference: `wine64-launcher.js` env construction) |

### Media Foundation / gstreamer / video codecs

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| mfplat, mfreadwrite, mf, quartz, wmvcore, evr, amstream | YES both | `x86_64-windows` and `i386-windows` contain `mfplat.dll`, `mfreadwrite.dll`, `mf.dll`, `quartz.dll`, `wmvcore.dll`, `evr.dll`, `amstream.dll` | – | NOT-A-GAP |
| winegstreamer availability | PARTIAL: PE+unix side present, but gstreamer *plugins* absent → element creation fails → cutscene decode fails | `w64:x86_64-unix/winegstreamer.so`, `w32:i386-unix/winegstreamer.so` both DT_NEED `libgstreamer-1.0.so.0`, `libgstaudio-1.0.so.0`, `libgstvideo-1.0.so.0`, `libgsttag-1.0.so.0`, `libgstbase-1.0.so.0` (present in `glibc-rootfs64.zip`/`wine32.zip`), but neither `wine64.zip` nor `wine32.zip` contained any `gstreamer-1.0/` plugin directory | would break a real game (video cutscenes, Bink-free FMV) | IMPLEMENTED: `tools/rootfs64/prepare-gstreamer.py` pins Debian bookworm `gstreamer1.0-plugins-base`/`-good`/`-libav` (+libav) debs by sha256, stages the plugin `.so` set plus its DT_NEEDED closure into `gstreamer64/`/`gstreamer32/`; `build-wine64-zip.sh` bakes the 64-bit set into wine64.zip and `tools/rootfs64/stage-gstreamer.sh` does the same for wine32.zip (mirroring `stage-alsa32.sh`); `prepare_prefixes.validate_service_runtime` requires the demux/decode plugins; pinned by `runtime-completeness.test.mjs`. Size: +5.5 MB on the 210 MB wine64.zip / 237 MB wine32.zip boot download (≤2.4%), well under the lazy-pack threshold used for the 205 MB support pack |
| Native DirectShow/MF decoders not gstreamer-backed | N/A | gstreamer is Wine's only builtin decoder backend upstream | – | NOT-A-GAP |

### Audio

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| winealsa.drv + ALSA→OSS bridge | YES both | `w64:x86_64-windows/winealsa.drv` + `tools/rootfs64/alsa` staging; `w32:i386-windows/winealsa.drv` + `tools/rootfs64/alsa32` (see `stage-alsa32.sh`) | – | NOT-A-GAP; guard: `prepare_prefixes.validate_service_runtime` |
| openal32 / OpenAL Soft | YES both | `support:opt/wine-stable/lib/wine/i386-windows/OpenAL32.dll`, `support:usr/lib/x86_64-linux-gnu/wine/x86_64-windows/OpenAL32.dll` (openal-soft 1.25.2, sha256-pinned) | – | NOT-A-GAP (correctly lazy-loaded via the support pack) |
| FAudio | YES (implicit) | statically linked into wined3d/xaudio2 upstream, `xaudio2_0..2_9.dll` present | – | NOT-A-GAP |
| winmm MIDI | YES | `w64/w32` `winmm.dll` + `winealsa.drv` (Wine routes midiOut through the ALSA unixlib); `mididev` drivers are built into winealsa.drv | – | NOT-A-GAP |

### Steam / DRM

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| steam_api/steam_api64 | NO | Not in `w32`/`w64`; `steam_api.dll` is a per-game Valve SDK binary shipped inside every Steam game's own files | – | NOT-A-GAP (each game ships its own `steam_api*.dll`; it is not redistributable infrastructure we can pre-install) |
| steamclient.dll / Steam client | NO | Proprietary Valve client component; not shipped in any zip | – | NOT-A-GAP: cannot be packaged (proprietary Steam client, requires the Steam binary + account services); games needing the live client report the failure themselves |

### Graphics/networking

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| vulkan-1.dll, opengl32.dll | YES both | `w32:i386-windows/vulkan-1.dll`, `w64:x86_64-windows/vulkan-1.dll`, `x86_64-unix/opengl32.so`, guest shims `libvk32/libvk64` + `libgl32/libgl64` via `support` | – | NOT-A-GAP |
| winhttp, wininet, ws2_32, dnsapi | YES both | `w32/w64` `{winhttp,wininet}.dll`; `x86_64-unix/ws2_32.so`, `i386-unix/dnsapi.dll.so` | – | NOT-A-GAP |
| crypt32, bcrypt, secur32 (TLS) | YES both | `x86_64-unix/crypt32.so` (dlopens `libgnutls.so.30` + closure, `build-wine64-zip.sh` gnutls staging), `bcrypt.so`, `secur32.so`; same set i386 | – | NOT-A-GAP |

### Fonts

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| Tahoma / Arial / Segoe substitutes | YES | `w64:usr/share/wine/fonts/{tahoma,tahomabd}.ttf` + DejaVu substitution set; `w32`: fontconfig path via `glibc-rootfs32.zip:usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` with Arial→DejaVu mapping recorded in `dist/runtime/wine32-font-provenance.json` (`stage-wine32-fonts.py`) | – | NOT-A-GAP |
| DejaVu Bold/Mono for the 32-bit guest | PARTIAL: only `DejaVuSans.ttf` shipped | `glibc-rootfs32.zip` carries exactly one DejaVu face; bold/mono requests synthesize or fall back to the regular face | harmless today (single-subset staging), visible as wrong bold/mono | IMPLEMENTED: `tools/rootfs64/fonts32/` (pinned from Debian `fonts-dejavu-core`) staged into glibc-rootfs32.zip by `tools/rootfs64/stage-fonts32.sh` (mirroring `stage-alsa32.sh`); pinned by `runtime-completeness.test.mjs` |
| CJK fallback font | NO | No CJK-capable face anywhere (`w64:usr/share/wine/fonts`, `w32` fontconfig set, `support`): Wine's `jvgasys.fon`/`fixedsys_jp.ttf` are VGA-font bitmap stubs, DejaVu has no CJK glyphs → CJK text renders as tofu | would break a real (CJK-locale) game | OPEN: needs a font choice (size/licence) plus support-pack staging inside `tools/browser/stage-dependencies.py` (not owned here); see NEEDS_OTHER_LANE |

### Registry defaults (prefix state)

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | ---|
| Direct3D | YES x64 (`p64:user.reg` `[Software\\Wine\\Direct3D] csmt=0:00000000`); x86 relies on `WINE_D3D_CONFIG=csmt=0x0` | `p64:home/username/.wine/user.reg`; `web/runtime/wine64-launcher.js` env push (`WINE_D3D_CONFIG` read by wined3d's DllMain overrides the registry) | – | NOT-A-GAP; guard: `prepare_prefixes.validate_prefix_zip` (win64 branch) + `runtime-completeness.test.mjs` |
| DirectInput registry keys | none needed | Wine's `dlls/dinput*` reads no mandatory registry value at init (no config knobs are required for default operation); no game-visible default is missing | – | NOT-A-GAP |
| Audio driver | NO explicit `Audio` value (deliberate) | `prepare_prefixes.py` audio check tolerates absence; `docs/reports/wine-boot-cost-20261023.md` measured that forcing `Audio=alsa` regressed click-to-mapped time, so it was removed and stays removed | – | NOT-A-GAP (documented regression evidence) |
| DPI / LogPixels | YES | both prefixes: `[Software\\Wine\\Fonts] "LogPixels"=dword:00000060` (96 DPI) | – | NOT-A-GAP |
| ScreenResolution / fullscreen | N/A | Wine's winex11/winex11drv has no mandatory `ScreenResolution`/`ScreenInvert` registry default; fullscreen defaults come from d3d/gl defaults | – | NOT-A-GAP |
| Mouse warp override | none needed | The winex11 unixlib reads `MouseWarpSpeed`/`GrabFullscreen` only as optional overrides (no default required; absence is upstream default behaviour) | – | NOT-A-GAP |
| Keyboard layout preload / locale default | YES | `p64/p32 system.reg [System\\CurrentControlSet\\Control\\Nls\\...]` (codepage table incl. `c_932.nls`, `c_936.nls`, …) + `w32/w64:.../nls/` (154 NLS files); `Keyboard Layout\\Preload "1"="00000409"` | – | NOT-A-GAP |
| Windows version emulation default | PARTIAL: `p32` has `[Software\\Microsoft\\Windows NT\\CurrentVersion]` (ProductName "Microsoft Windows 7", CurrentBuild 7601, CSDVersion "Service Pack 1", InstallDate, DigitalProductId, RegisteredOwner) — `p64` was missing the *entire section*: apps reading `ProductName`/`CurrentBuild` directly (installers, DRM, engine config tools) got nothing, and `RtlGetVersion` only worked because `version_init` falls back to compiled-in Win7 SP1 defaults (`dlls/ntdll/version.c:470`) | `p32:system.reg:37547`; absent in `p64:system.reg`; wine.inf `[Version]` section (`w64:usr/share/wine/wine.inf`) proves a real wineboot writes these values (`HKLM,%CurrentVersionNT%,"ProductName",2,"Microsoft Windows 7"` etc.) | would break a real game (version-gating installers/DRM/engines) | IMPLEMENTED: `build-prefix64.sh` merges `tools/rootfs64/prefix64-reg.txt` (the wine.inf `[Version]` values + mono registry keys) into system.reg; required by `prepare_prefixes.validate_prefix_zip`; pinned by `runtime-completeness.test.mjs` |
| Service/controller/device keys (RpcSs, PlugPlay, winebus, wineusb, Gamepad, RawGameController, HID) | YES | required by `prepare_prefixes.validate_prefix_zip` for both architectures (guarded since commit 1b8b6b6f) | – | NOT-A-GAP |
| winemenubuilder autostart | ABSENT (deliberate) | removed by `prepare_prefixes.py sanitize` (`docs/reports/wine-boot-cost-20261023.md`) | – | NOT-A-GAP (documented, tested) |

### Size budget (runtime-assets.json)

`tools/browser/runtime-assets.json` pins per-file `bytes` + `sha256` for the
six boot zips plus the slang compiler triple — i.e. any change to a zip's
*content* is caught by `bootstrap.py --check`. There is no separate
byte-count cap; the boot download is ~478 MB total
(glibc-rootfs64 10 MB + wine64 210 MB + prefix64 19 MB + glibc-rootfs32 3 MB +
wine32 237 MB + prefix32 6 MB), and large optional components (wine-mono 82 MB,
wine-gecko, OpenAL, DXVK, guest GL/Vulkan shims) are already lazy-loaded via
the 205 MB support pack + `dependencies.json` manifest
(`tools/browser/stage-dependencies.py`). The two gaps found in that mechanism:
the `tools/browser/runtime-assets.json` archive manifest (`archive.sha256`)
describes the *download bundle* — it is a single pinned field, not a
per-component budget — and the support-pack restager lives in
`tools/browser/stage-dependencies.py`, which is outside this lane's ownership
(see NEEDS_OTHER_LANE). No silent size failure exists today: every byte
count is hash-pinned.
