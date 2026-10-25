# Runtime / prefix completeness audit (P2-R, 2026-10-24)

Scope: the Windows runtime a guest program sees — `web/runtime/*.zip`
(pinned by `tools/browser/runtime-assets.json`, regenerable via
`tools/rootfs64/build-wine64-zip.sh`, `tools/build-prefix32.sh`,
`tools/rootfs64/build-prefix64.sh`) and the lazy "support pack" staged by
`tools/browser/stage-dependencies.py` (the CJK gap found inside it was
fixed by this lane in bdd258899; the MFC row below documents why that
lane's remaining item is externally constrained).

Method: every checklist item was verified against the *actual shipped
artifacts* (zip member lists and registry hives extracted from
`web/runtime/`), not against assumptions. Each row names the evidence path.
Presence/absence claims about zip members are additionally pinned by
`docs/known-gaps/runtime-artifact-manifest.md` — a checked-in census
generated from the shipped zips by `tools/rootfs64/gen_runtime_manifest.py`
and freshness-checked by `check_runtime_completeness.py` (audit wave 2
found the VC++ and d3dcompiler rows rested on uncited census claims;
the manifest is the durable fix, same standard as the staged-binary
proofs that repaired NAG-20/32/36 in wave 1).
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
| `tools/rootfs64/build-wine64-zip.sh` (was: `copy /usr/lib/wine/wineserver`) | Debian's `/usr/lib/wine/wineserver` is a `#!/bin/sh` dispatch script, and the guest rootfs64 has no `/bin/sh` (no `bin/sh` member in `glibc-rootfs64.zip`), so a guest exec of the compiled-in server path could not run. Latent in the shipped configuration: the launcher sets `WINESERVER=/usr/lib/wine/wineserver64` for every spawn (`web/runtime/wine64-launcher.js:96,181,248,275`) and execs the ELF directly, which is why prefixes boot. Found by the tests lane (`packaging-prefix.test.mjs` ratchet). | would break any guest flow that execs `/usr/lib/wine/wineserver` without the launcher env | IMPLEMENTED: the build no longer copies the script; it stages `usr/lib/wine/wineserver` as a symlink to the ELF `wineserver64` (mirroring `usr/bin/wineserver` in the same script and the 32-bit bridge in `tools/build-prefix32.sh`); the shipped `wine64.zip` member was replaced with the symlink form; `check_runtime_completeness.py` (`wineserver dispatch is ELF`) pins both entries plus the ELF magic. One correction to the tests-lane row: in the shipped archive `usr/bin/wineserver` already carried the target text `/usr/lib/wine/wineserver64` (correct); the script member at `usr/lib/wine/wineserver` was the sole broken entry. |
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
| msvcp/vcruntime 60/70/80/90/100/110/120, msvcrt20/40, msvcrt.dll | YES both | manifest `runtime-artifact-manifest.md` §VC++ runtime census: every set member present at `usr/lib/x86_64-linux-gnu/wine/x86_64-windows/<name>` in `w64` and `opt/wine-stable/lib/wine/i386-windows/<name>` in `w32` (zip census 2026-10-25, wave-2 evidence pass) | – | NOT-A-GAP; guard: `check_runtime_completeness.py` (`VC++ runtime sets`) asserts the members (wave-1 citation of the guard was aspirational — the checker did not assert VC++ sets until this pass). |
| msvcp140/vcruntime140 + msvcp140_1/_2/_atomic_wait/_codecvt_ids, ucrtbase, concrt140 | YES both zips for the full set (after the codecvt backport row below); the one remaining asymmetry is `vcruntime140_1.dll`: x86_64 only | manifest §VC++ runtime census + per-name lines: `vcruntime140_1.dll` PRESENT in `w64` at `usr/lib/x86_64-linux-gnu/wine/x86_64-windows/vcruntime140_1.dll`, ABSENT in `w32`; all other set members present in both (paths as in the legacy row) | – | `vcruntime140_1` is x64/ARM64-only upstream (`dlls/vcruntime140_1/{Makefile.in,vcruntime140_1.spec}` — `except_x86_64.c`/`__CxxFrameHandler4`), so its absence in i386 is NOT-A-GAP, not a packaging omission. Wave-2 correction: this row's original brace list also claimed `msvcp140_codecvt_ids.dll` for `w64`; the shipped zip did not contain it (Debian wine 8.0 amd64 omits the module; WineHQ 10.0 i386 ships it). That half of the claim was FALSE and is dispositioned in the next row. Guard: `check_runtime_completeness.py` (`VC++ runtime sets`) pins the full set and the asymmetry. |
| msvcp140_codecvt_ids.dll (x86_64) | Was NO in `w64` (present in `w32`) | zip census 2026-10-25 (wave-2 evidence pass): absent from every member of shipped `wine64.zip`; present in `w32` at `opt/wine-stable/lib/wine/i386-windows/msvcp140_codecvt_ids.dll` | would break 64-bit MSVC 2015-2022 apps importing the codecvt facet DLL | IMPLEMENTED: backported from the same pinned WineHQ wine-stable 10.0 amd64 deb as d3d12core/msmpeg2vdec (`tools/rootfs64/winehq10-x86_64/msvcp140_codecvt_ids.dll`, 66024 bytes, sha256 `2215e331…`, imports only `kernel32.dll`); staged by `build-wine64-zip.sh` + `stage-wine64-payloads.sh`, required by `prepare_prefixes.validate_service_runtime`, pinned by `check_runtime_completeness.py` (source hash + shipped-zip presence + VC++ set check); present in the shipped `wine64.zip` (manifest §VC++ census). |
| MFC (mfc140, mfc140u, …) | NO (either arch) | not in `w32`/`w64` | would break real MFC-based installers/old games | NOT-IMPLEMENTED — documented external constraint (this register's one non-trichotomy disposition, disclosed in the lane status below): (1) Wine upstream does not implement MFC at all (no `mfc*` in `dlls/`; Wine bug 44856 tracks the resulting mfc140 failures). (2) Extracting Microsoft's MFC DLLs from the VC++ redistributable into our own runtime pack is not license-clean: Microsoft's redistribution terms allow the MFC merge modules/DLLs to be distributed only *as part of the installation package of your own program* and state that distribution of the redistributable package and its individual binaries "is limited to licensed Visual Studio users" (https://learn.microsoft.com/en-us/visualstudio/releases/2026/redistribution); Wine's own HOWTO-winelib calls MFC "distributed under a very restrictive license" (https://www.winehq.org/pipermail/wine-patches/2002-November/004108.html). Winbox is not distributing its own VS-built program. (3) The license-clean route — running Microsoft's official `vc_redist` installer inside the guest, as winetricks `vcrun*` does — requires an installer-execution step the pre-booted browser prefix pipeline does not have; that is a launcher feature, not a staging step. Guest-visible failure is loud, never silent: Wine's loader reports `err:module:import_dll Library MFC140.DLL (which is needed by ...) not found` and process init fails with status c0000135, exactly as on upstream Wine without vcrun. Revisit if a guest-side installer step lands or Microsoft changes the redist terms. |

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
| d3dcompiler_33..43, _46, _47 | YES both; 24..32 and 44..45 do not exist in either pinned build | manifest §DirectX numbered families + per-name lines (wave-2 verification against the actual zips): `d3dcompiler_33.dll`…`d3dcompiler_43.dll`, `d3dcompiler_46.dll`, `d3dcompiler_47.dll` present in both zips at the arch windows paths; `d3dcompiler_24.dll`, `_32.dll`, `_44.dll`, `_45.dll` ABSENT in both (full 20..50 scan in the evidence pass found exactly {33..43, 46, 47}); the WineHQ 10.0 amd64 deb's `x86_64-windows` listing independently shows the same set | – | NOT-A-GAP (upstream parity); guard: `check_runtime_completeness.py` (`DirectX redist sets`) pins the exact set including the absences |
| xinput1_1..1_4, xinput9_1_0, xinputuap | YES both | `xinput1_1.dll`…`xinput1_4.dll`, `xinput9_1_0.dll`, `xinputuap.dll`, `winexinput.sys` | – | NOT-A-GAP |
| xaudio2_0..2_9 | YES both | `xaudio2_0.dll`…`xaudio2_9.dll` | – | NOT-A-GAP; FAudio is statically linked into Wine's xaudio/wined3d upstream, no separate faudio.dll ships |
| xactengine2_0/2_4/2_7/2_9, 3_0..3_7 | YES both; 2_1..2_3/2_5/2_6/2_8 do not exist upstream | `xactengine2_0.dll`, `xactengine2_4.dll`, …, `xactengine3_7.dll` | – | NOT-A-GAP (upstream parity) |
| x3daudio1_0..1_7 | YES both | `x3daudio1_0.dll`…`x3daudio1_7.dll` | – | NOT-A-GAP |
| dinput, dinput8, dplay, dplayx, dpnet, dplaysvr, ddraw, dsound | YES both | `dinput.dll`, `dinput8.dll`, `dplayx.dll`, `dpnet.dll`, `dplaysvr.exe`, `dsound.dll` | – | NOT-A-GAP |
| d3d8, d3d9, d3d10core, d3d11, d3d12, dxgi (builtin) | YES both | `d3d8.dll`, `d3d9.dll`, `d3d10core.dll`, `d3d11.dll`, `d3d12.dll`, `dxgi.dll` | – | NOT-A-GAP |
| d3d12core.dll | YES x86 (`w32:i386-windows/d3d12core.dll`); NO x64 | `w64` has no `x86_64-windows/d3d12core.dll` (Debian wine 8.0 predates `dlls/d3d12core`) | would break games that `LoadLibrary("d3d12core.dll")` on the 64-bit prefix | IMPLEMENTED: `tools/rootfs64/winehq10-x86_64/d3d12core.dll` (WineHQ wine-stable 10.0 amd64, sha256-pinned) staged into wine64.zip by `build-wine64-zip.sh`; required by `prepare_prefixes.validate_service_runtime`; pinned by `runtime-completeness.test.mjs` Shipped-zip presence delivered by `stage-wine64-payloads.sh` (a2377ce48) while the container build is unavailable on this ARM64 host; `validate_service_runtime` PASS on the shipped pair (2026-10-25) |
| msmpeg2vdec.dll (MF video decoder) | YES x86 (`w32:i386-windows/msmpeg2vdec.dll`); NO x64 | `w64` has no `x86_64-windows/msmpeg2vdec.dll` (not built by Debian wine 8.0) | would break WMV/H.264 cutscene decode on the 64-bit prefix | IMPLEMENTED: `tools/rootfs64/winehq10-x86_64/msmpeg2vdec.dll` (WineHQ wine-stable 10.0 amd64, sha256-pinned) staged into wine64.zip; its mfplat imports (`MFTRegister`/`MFTUnregister`) verified present in wine64's mfplat.dll; required by `validate_service_runtime`; pinned by `runtime-completeness.test.mjs` Shipped-zip presence delivered by `stage-wine64-payloads.sh` (a2377ce48) while the container build is unavailable on this ARM64 host; `validate_service_runtime` PASS on the shipped pair (2026-10-25) |
| Native DXVK/DXVK-DLL set | YES | `p32:home/username/{d3d9,d3d11,dxgi}.dll` (per-app override DLLs in the 32-bit prefix root, loaded before builtin via Wine's DLL search order); vulkan guest shims `libvk32/libvk64` via `support` | – | NOT-A-GAP; guard: `prepare_prefixes.py` requires the 32-bit payloads (`validate_prefix_zip` win32 branch) |
| WINEDLLOVERRIDES defaults d3d8/9/10/11/12, dxgi, d3d12core | N/A (no override needed) | `web/runtime/wine64-launcher.js:193` passes `WINEDLLOVERRIDES` only when the `dlloverides` URL param is set; builtin Wine D3D is the default and 32-bit games find the per-app DXVK DLLs by DLL search order, so an override would *disable* the fix | – | NOT-A-GAP (reference: `wine64-launcher.js` env construction) |

### Media Foundation / gstreamer / video codecs

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| mfplat, mfreadwrite, mf, quartz, wmvcore, evr, amstream | YES both | `x86_64-windows` and `i386-windows` contain `mfplat.dll`, `mfreadwrite.dll`, `mf.dll`, `quartz.dll`, `wmvcore.dll`, `evr.dll`, `amstream.dll` | – | NOT-A-GAP |
| winegstreamer availability | PARTIAL: PE+unix side present, but gstreamer *plugins* absent → element creation fails → cutscene decode fails | `w64:x86_64-unix/winegstreamer.so`, `w32:i386-unix/winegstreamer.so` both DT_NEED `libgstreamer-1.0.so.0`, `libgstaudio-1.0.so.0`, `libgstvideo-1.0.so.0`, `libgsttag-1.0.so.0`, `libgstbase-1.0.so.0` (present in `glibc-rootfs64.zip`/`wine32.zip`), but neither `wine64.zip` nor `wine32.zip` contained any `gstreamer-1.0/` plugin directory | would break a real game (video cutscenes, Bink-free FMV) | IMPLEMENTED: `tools/rootfs64/prepare-gstreamer.py` pins Debian bookworm `gstreamer1.0-plugins-base`/`-good`/`-libav` (+libav) debs by sha256, stages the plugin `.so` set plus its DT_NEEDED closure into `gstreamer64/`/`gstreamer32/`; `build-wine64-zip.sh` bakes the 64-bit set into wine64.zip and `tools/rootfs64/stage-gstreamer.sh` does the same for wine32.zip (mirroring `stage-alsa32.sh`); `prepare_prefixes.validate_service_runtime` requires the demux/decode plugins; pinned by `runtime-completeness.test.mjs`. Size: +85 MB compressed on the 210 MB wine64.zip / 237 MB wine32.zip boot download (wine32 verified: 236,709,881 → 321,016,564 bytes); the closure is DT_NEEDED-complete, so it cannot be trimmed without dropping demux/decode features. Shipped wine64.zip carries the set via `stage-wine64-payloads.sh` (a2377ce48); shipped wine32.zip via `stage-gstreamer.sh`; `validate_service_runtime` PASS on both shipped pairs (2026-10-25) |
| Native DirectShow/MF decoders not gstreamer-backed | N/A | Staged-tree proof (2026-10-25): the full `x86_64-unix/` census of `wine64.zip` (35 modules) contains exactly one media-decoder unixlib, `winegstreamer.so`; the i386 tree likewise ships only `winegstreamer.so` (+ its `winegstreamer.dll.so` PE-side twin). The PE consumers route to it in the shipped binaries themselves: `w64` `mfplat.dll` contains the wide strings `CLSID_GStreamerByteStreamHandler` and `resolver_create_gstreamer_handler`. No second native decoder module exists in either pinned Wine build to stage. | – | NOT-A-GAP (artifact-cited) |

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
| winhttp, wininet, ws2_32, dnsapi | YES both | `w32/w64` `{winhttp,wininet}.dll`; unix side exact members: `w64:usr/lib/x86_64-linux-gnu/wine/x86_64-unix/ws2_32.so` + `…/x86_64-unix/dnsapi.so`; `w32:opt/wine-stable/lib/wine/i386-unix/ws2_32.so` + `…/i386-unix/dnsapi.so` (the i386 tree additionally carries a `dnsapi.dll.so` variant — an earlier version of this row mixed the two trees' names) | – | NOT-A-GAP |
| crypt32, bcrypt, secur32 (TLS) | YES both | `x86_64-unix/crypt32.so` (dlopens `libgnutls.so.30` + closure, `build-wine64-zip.sh` gnutls staging), `bcrypt.so`, `secur32.so`; same set i386 | – | NOT-A-GAP |

### Fonts

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| Tahoma / Arial / Segoe substitutes | YES | `w64:usr/share/wine/fonts/{tahoma,tahomabd}.ttf` + DejaVu substitution set; `w32`: fontconfig path via `glibc-rootfs32.zip:usr/share/fonts/truetype/dejavu/DejaVuSans.ttf` with Arial→DejaVu mapping recorded in `dist/runtime/wine32-font-provenance.json` (`stage-wine32-fonts.py`) | – | NOT-A-GAP |
| DejaVu Bold/Mono for the 32-bit guest | PARTIAL: only `DejaVuSans.ttf` shipped | `glibc-rootfs32.zip` carries exactly one DejaVu face; bold/mono requests synthesize or fall back to the regular face | harmless today (single-subset staging), visible as wrong bold/mono | IMPLEMENTED: `tools/rootfs64/fonts32/` (pinned from Debian `fonts-dejavu-core`) staged into glibc-rootfs32.zip by `tools/rootfs64/stage-fonts32.sh` (mirroring `stage-alsa32.sh`); pinned by `runtime-completeness.test.mjs` |
| CJK fallback font | YES (support pack) | `support:usr/share/fonts/truetype/wqy/wqy-microhei.ttc` (5,177,387 bytes, sha256 `2420e807…`) + `support:etc/fonts/conf.d/65-wqy-microhei.conf` + license at `support:opt/winbox/licenses/wqy-microhei-copyright`; provenance in `winbox-runtime.json` `cjkFont` entry | would break a real (CJK-locale) game | IMPLEMENTED (bdd258899): `tools/browser/stage-dependencies.py` downloads the Debian bookworm `fonts-wqy-microhei` deb (sha256-pinned; Apache-2.0/GPL-3+ font-exception license — freely redistributable, and the 5 MB TTC beats ~120 MB Noto CJK), extracts and hash-verifies the TTC, stages font + the package's own fontconfig alias conf. Verified by a full `stage-dependencies.py` run (support zip contains all three members, provenance hashes match) and pinned by `runtime-completeness.test.mjs`. |

### Guest GL shim buffer mapping (libgl64.c, flagged by the GL lane)

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| `tools/rootfs64/libgl64/libgl64.c` buffer-map emulation (`glMapBufferRange`/`glMapBuffer`/`glUnmapBuffer`/`glFlushMappedBufferRange`, ~L849-913) | Audited 2026-10-25 | Read maps returned uninitialised scratch (never downloaded from the real buffer); `glFlushMappedBufferRange` used the flush offset as the *buffer* offset (spec: relative to the mapping start), corrupting the buffer head for ranged maps not starting at 0; a second simultaneous map on the same scratch slot silently clobbered the first map's state | would break a real game (wined3d readbacks / explicit-flush uploads) | IMPLEMENTED (53fa5b05f): read maps populate the scratch via the bridged `glGetBufferSubData`; flush destination is `map.off + offset`, bounds-checked against the mapped length and an active map; nested same-slot maps fail with NULL instead of corrupting. Rebuilt `libGL.so.1` (host cross-gcc), SONAME unchanged, exports = source set (the stale committed binary had lacked 4 source-defined exports). Remaining limit, REJECTED-LOUDLY by value: maps over the 8 MB scratch return NULL — the spec failure value, visible to the guest, never silent corruption; the shim has no local GL error queue to raise `GL_OUT_OF_MEMORY` (would need GL-bridge error state, GL lane). Non-ARRAY/ELEMENT targets share the array slot; wined3d maps only ARRAY/ELEMENT buffers. |

### Registry defaults (prefix state)

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | ---|
| Direct3D | YES x64 (`p64:user.reg` `[Software\\Wine\\Direct3D] csmt=0:00000000`); x86 relies on `WINE_D3D_CONFIG=csmt=0x0` | `p64:home/username/.wine/user.reg`; `web/runtime/wine64-launcher.js` env push (`WINE_D3D_CONFIG` read by wined3d's DllMain overrides the registry) | – | NOT-A-GAP; guard: `prepare_prefixes.validate_prefix_zip` (win64 branch) + `runtime-completeness.test.mjs` |
| DirectInput registry keys | none needed | Staged-binary proof (2026-10-25): the only Wine registry path string in the shipped `w64` `dinput.dll` and `dinput8.dll` is `Software\Wine\DirectInput` — the optional HKCU override root, read on demand; `usr/share/wine/wine.inf` in `wine64.zip` (the file wineboot installs defaults from) contains zero DirectInput sections, and neither shipped prefix hive (`p32`/`p64` `system.reg`/`user.reg`) carries a DirectInput section — the 32-bit hive set is real wineboot output, so a mandatory default would appear there. | – | NOT-A-GAP (artifact-cited) |
| Audio driver | NO explicit `Audio` value (deliberate) | `prepare_prefixes.py` audio check tolerates absence; `docs/reports/wine-boot-cost-20261023.md` measured that forcing `Audio=alsa` regressed click-to-mapped time, so it was removed and stays removed | – | NOT-A-GAP (documented regression evidence) |
| DPI / LogPixels | YES | both prefixes: `[Software\\Wine\\Fonts] "LogPixels"=dword:00000060` (96 DPI) | – | NOT-A-GAP |
| ScreenResolution / fullscreen | N/A | Wine's winex11/winex11drv has no mandatory `ScreenResolution`/`ScreenInvert` registry default; fullscreen defaults come from d3d/gl defaults | – | NOT-A-GAP |
| Mouse warp override | none needed | Staged-binary proof (2026-10-25): `w64:usr/lib/x86_64-linux-gnu/wine/x86_64-unix/winex11.so` contains `GrabFullscreen` (an optional override it reads if present). `MouseWarpSpeed` appears in NO member of either shipped wine tree (full-member search, ANSI + UTF-16LE, 2026-10-25) — this pinned build does not read that value at all, so its absence from the prefix cannot change behaviour. Neither prefix carries either value (`p32`/`p64` `user.reg` checked). | – | NOT-A-GAP (artifact-cited) |
| Keyboard layout preload / locale default | YES | `p64/p32 system.reg [System\\CurrentControlSet\\Control\\Nls\\...]` (codepage table incl. `c_932.nls`, `c_936.nls`, …) + `w32/w64:.../nls/` (154 NLS files); `Keyboard Layout\\Preload "1"="00000409"` | – | NOT-A-GAP |
| Windows version emulation default | PARTIAL: `p32` has `[Software\\Microsoft\\Windows NT\\CurrentVersion]` (ProductName "Microsoft Windows 7", CurrentBuild 7601, CSDVersion "Service Pack 1", InstallDate, DigitalProductId, RegisteredOwner) — `p64` was missing the *entire section*: apps reading `ProductName`/`CurrentBuild` directly (installers, DRM, engine config tools) got nothing, and `RtlGetVersion` only worked because `version_init` falls back to compiled-in Win7 SP1 defaults (`dlls/ntdll/version.c:470`) | `p32:system.reg:37547`; absent in `p64:system.reg`; wine.inf `[Version]` section (`w64:usr/share/wine/wine.inf`) proves a real wineboot writes these values (`HKLM,%CurrentVersionNT%,"ProductName",2,"Microsoft Windows 7"` etc.) | would break a real game (version-gating installers/DRM/engines) | IMPLEMENTED: `build-prefix64.sh` merges `tools/rootfs64/prefix64-reg.txt` (the wine.inf `[Version]` values + mono registry keys) into system.reg; required by `prepare_prefixes.validate_prefix_zip`; pinned by `runtime-completeness.test.mjs` |
| Service/controller/device keys (RpcSs, PlugPlay, winebus, wineusb, Gamepad, RawGameController, HID) | YES | required by `prepare_prefixes.validate_prefix_zip` for both architectures (guarded since commit 1b8b6b6f) | – | NOT-A-GAP |
| winemenubuilder autostart | ABSENT (deliberate) | removed by `prepare_prefixes.py sanitize` (`docs/reports/wine-boot-cost-20261023.md`) | – | NOT-A-GAP (documented, tested) |

### Lazy archive fetch stall (routed from the GL lane, 2026-10-25)

| Location | What valid guest behaviour hits it | Severity | Disposition |
| --- | --- | --- | --- |
| `source/io/fszipurl.cpp:59-104` (`bw64StartFetch` EM_JS + `bw64SyncFetchRange`) | Lazy `bw64url:` block-range fetch has **no timeout anywhere**: the EM_JS `fetch()` is issued with no `AbortController`, `r.arrayBuffer()` on a stalled body never settles, `bw64FetchDone` is then never called, and the guest pthread blocked in `emscripten_proxy_sync_with_ctx` never wakes; threads waiting on the same block wait on `fetchCv` (`fszipurl.cpp:196-199`) with no deadline either. Settled failures are handled loudly (`fetchRaw` logs `range fetch FAILED` and waiters retry as fetchers) — only a *never-settling* fetch hangs, and nothing logs it: the 16 MB-cadence progress line (`:171-175`) only fires after successful fetches. Evidence (GL lane, gl.md Celeste row): Wine launched Celeste, fetched 113 MB of its 2.69 GB archive, then emitted no new logs for 535 s and timed out in `not-mapped`; a separate live-era run mapped and rendered (0.75 nonblank input screenshots), so the failure is intermittent — consistent with a stalled fetch, not a deterministic Celeste incompatibility. The upfront chunk loop in `web/games.mjs` shares the no-timeout shape (secondary; not the Celeste demo path). | would break a real game (any lazily loaded title can hang silently at startup) | IMPLEMENTED (Celeste fix lane, 2026-10-25) — stall watchdog at every layer. (1) `bw64StartFetch` EM_JS (`source/io/fszipurl.cpp:81`) wires an AbortController to a 60 s STALL deadline: the clock starts at issuance and resets when response headers arrive and on every streamed chunk (body-reader path), so a slow-but-progressing fetch is never aborted; where the launcher prefetch wrapper buffers the body before the promise resolves, no progress is visible and the window acts as a per-2 MB-block deadline. On a stall it logs once — `fszipurl: fetch STALL url=… off=… len=… bytesSoFar=… stallMs=…` — aborts the fetch, and settles the job itself with `BW64_FETCH_STALLED` (-2, `fszipurl.cpp:179`) through a settled-guard, so the guest pthread in `emscripten_proxy_sync_with_ctx` always wakes. (2) `fetchRaw` (`fszipurl.cpp:254,262`) retries a failed or stalled chunk up to 3 attempts (permanent 404/403 still fail fast and latch `permanentError`); exhaustion logs `range fetch STALLED` / `range fetch FAILED` as before. (3) Same-block waiters wait on `fetchCv` with a 240 s deadline (`FETCH_WAIT_DEADLINE{240}`, `fszipurl.cpp:189,315`; 4× the stall window, above the fetcher worst case of 3 stall-bounded attempts ≈ 3 min); past it the read fails loudly (`STALL waiting for in-flight block`) without touching the fetcher-owned `inFlight` entry, so no duplicate fetch and no deadlock. (4) The launcher prefetch wrapper (`project/emscripten/wine64-launcher.js:126`, `block()`) adds a 120 s backstop watchdog (2× the EM_JS window) that aborts the native fetch and rejects the job, so its `pending` map cannot wedge a block forever (`bw64-prefetch: fetch STALL …`). (5) `web/games.mjs:72` (`stallWatchdog`) gives the upfront download the same shape per request: progress resets per chunk, a stall aborts and surfaces as a loud `download stalled` error. Why 60 s: healthy same-origin 2 MB range fetches settle in 50–200 ms (fszipurl design comment); a full minute of zero bytes is a dead connection, not a slow one — and the observed Celeste stall was 535 s of silence, far past the trip point. Retries are per-block, so an aborted transfer loses at most one 2 MB block. Test: `web/tests/suite/unit/fszipurl-fetch-stall.test.mjs` (9 tests, PASS: EM_JS body extracted and run against a never-settling fetch — settles -2, abort fired, exactly one stall log with url+offset; a progressing chunked fetch spanning longer than the window is NOT aborted; settled-failure codes -1/-404 preserved; arrayBuffer fallback; launcher job rejects, pending clears and the retry refetches fresh; games.mjs stall rejects loudly and a progressing download completes; C++ source pins for the watchdog/deadline/retry/log lines). WASM build compile-verified in this lane (see commit). Remaining verification: the solo Celeste browser probe at the integ gate (no browser probe run in this lane, per coordinator routing). |

### Size budget (runtime-assets.json)

`tools/browser/runtime-assets.json` pins per-file `bytes` + `sha256` for the
six boot zips plus the slang compiler triple — i.e. any change to a zip's
*content* is caught by `bootstrap.py --check`. There is no separate
byte-count cap; the boot download is ~478 MB total
(glibc-rootfs64 10 MB + wine64 210 MB + prefix64 19 MB + glibc-rootfs32 3 MB +
wine32 237 MB + prefix32 6 MB), and large optional components (wine-mono 82 MB,
wine-gecko, OpenAL, DXVK, guest GL/Vulkan shims) are already lazy-loaded via
the support pack + `dependencies.json` manifest
(`tools/browser/stage-dependencies.py`). No silent size failure exists:
every byte count is hash-pinned. Size state after the 2026-10-25 lane
pass (disk is the binding constraint on the box — /home/ubuntu:
docker 23G, kn0tzer 21G, worktrees 17G, backups 7.7G, main repo 6.3G):
wine64.zip grew 201 MB → 285 MB and wine32.zip 237 MB → 307 MB with the
gstreamer closure (+85 MB compressed per arch, DT_NEEDED-complete, see
the winegstreamer row — accepted because cutscene decode has no smaller
complete form); the support pack grew ~196 MB → 207,599,381 bytes with
the 5 MB CJK TTC (lazy-loaded, not boot cost). The shipped zips' new
hashes require a `runtime-assets.json` re-pin by the release step
(`bootstrap.py --check` will flag them loudly until then — by design).

## Wave-2 evidence sweep (2026-10-25, runtime evidence lane)

Audit wave 2 sampled the register's NOT-A-GAP rows and found the VC++
(NAG-2) and d3dcompiler (NAG-10) rows unproven by citation: their census
claims rested on the lane's word with no in-tree artifact to check
against. This pass generated `runtime-artifact-manifest.md` from the
actual shipped zips and swept **every** NOT-A-GAP row for the same
defect class:

- **Verified TRUE against the zips + manifest** (evidence now checkable
  in-tree): VC++ legacy set; d3dx9_24..43; d3dx10_33..43 (+ `d3dx10.dll`
  absent); d3dx11_42/43 only; xinput set; xaudio2_0..2_9; xactengine
  2_0/2_4/2_7/2_9 + 3_0..3_7 exactly; x3daudio1_0..1_7; dinput/dplay/
  ddraw/dsound set; d3d8/9/10core/11/12/dxgi; MF set (mfplat,
  mfreadwrite, mf, quartz, wmvcore, evr, amstream); winealsa.drv;
  vulkan-1/opengl32; winhttp/wininet/ws2_32/dnsapi; crypt32/bcrypt/
  secur32 (dll + unix .so); Tahoma faces; mono + gecko + OpenAL members
  in the retained support pack; prefix32 DXVK root DLLs; steam_api/
  steamclient absences; MFC absences (now manifest-pinned); FAudio
  absence (no `faudio.dll` in any shipped zip).
- **Proved FALSE in one half**: the VC++ 140 row claimed
  `msvcp140_codecvt_ids.dll` for x86_64; the shipped wine64.zip did not
  contain it. Fixed as a third winehq10 backport (row above) — the
  register's NOT-A-GAP verdict on `vcruntime140_1` itself was verified
  TRUE (present x86_64, absent i386).
- **Flagged, not re-dispositioned**: the CJK row's "support zip contains
  all three members" verification cannot be re-checked from retained
  artifacts — the support pack referenced by
  `dist/runtime/dependencies.json` on this box (`support-b68aea…`,
  205 MB) predates CJK staging and contains no `wqy` members (manifest
  §Support-pack). The staging implementation and its sha256 pins are
  in-tree and statically checked (`check_runtime_completeness.py`,
  CJK font pin), so the row stays IMPLEMENTED, but the retained-artifact
  proof must be re-run when the release step rebuilds/re-pins the
  support pack (the same disclosed re-pin state noted in the size
  section). No other row depends on the support pack's CJK members.
- Rows whose proof is code/document-based rather than zip-presence
  (registry defaults, `WINEDLLOVERRIDES`, deliberate audio/DPI choices,
  the libgl64 audit) cite in-tree sources and were out of this sweep's
  defect class; the staged-binary proofs from wave 1 (gstreamer-only
  decoder, DirectInput keys, mouse warp) already meet the standard.

## Lane status (2026-10-25, runtime lane close-out)

Disclosure (Celeste fix lane): while compile-verifying this fix, a
`bootstrap.py --local` restore (the build's pinned-asset step; the
remote archive 404s) overwrote the four un-pinned `web/runtime` zips
with the 2026-10-13 pinned copies. They were recovered the same day
with the lane's own tooling: `prefix64.zip` restored byte-identical
from `tools/rootfs64/dist/` (sha256 matches the pre-incident
manifest), `wine64.zip` via `stage-wine64-payloads.sh` + the
wineserver symlink member replacement, `wine32.zip` via
`stage-gstreamer.sh`, `glibc-rootfs32.zip` via `stage-fonts32.sh`.
Member counts are unchanged (1518/2171/58); w64 is within +2 bytes
of its pre-incident size; w32/g32 sizes are identical (zip hashes
differ — entries were rewritten). `runtime-artifact-manifest.md`
was regenerated and `check_runtime_completeness.py` passes 9/9.
The regenerated manifest also records the fresh support pack built
by this lane's verification build (69 members, incl. the CJK TTC),
 superseding the retained pre-CJK pack the wave-2 section cites.

Register: **OPEN: 0** after the Celeste fetch-stall fix lane
(2026-10-25) closed the last OPEN row: the lazy-fetch stall is
IMPLEMENTED with `fszipurl-fetch-stall.test.mjs` (unit) + WASM
compile verification; the solo Celeste browser probe remains owed
at the integ gate. Prior state: **OPEN: 1** after the wave-2
evidence pass (the Celeste lazy-fetch stall row above, routed
from the GL lane and registered OPEN with its fix sketch; it was
not in the register at the original close-out). The original close-out stood at OPEN: 0; this pass kept
every prior row at its disposition, corrected one false census claim
(msvcp140_codecvt_ids, now IMPLEMENTED via backport), fixed the
wineserver dispatch script defect routed from the tests lane
(IMPLEMENTED), and pinned all zip-presence claims to the generated
artifact manifest. One row (MFC) carries the disclosed
NOT-IMPLEMENTED documented-external-constraint disposition above; every
other row is IMPLEMENTED (with the cited guard) or NOT-A-GAP with an
artifact/code citation. Rows corrected during this pass after
independent review found citation defects: NAG-20 (gstreamer-only
decoder), NAG-32 (DirectInput keys), NAG-36 (mouse warp) re-cited to
staged-binary proof; NAG-28 (ws2_32/dnsapi unix paths) corrected to the
exact member names. Three rows the previous audit had marked
IMPLEMENTED did not exist in the tree (prefix64 registry merge,
fonts32 staging, runtime-completeness test) — all three are now
implemented for real and verified; one register overstatement was
caught by the new guard itself (d3dcompiler_44/45 in neither pinned
build) and corrected.

Commits on `lane/winbox-wake-latency-r5-20261023`:
`fdaead11d` (wip verified/amended — the wip's silent deletion of the
libGL-missing warning was reverted), `353e2047c` (prefix64 registry
merge), `a2377ce48` (stage-wine64-payloads), `f4a46fd00` (fonts32),
`bdd258899` (CJK support-pack staging), `53fa5b05f` (libgl64 map
fixes), `69f32bfaa` (runtime-completeness guard), plus this register
update.

Known environment limit: `build-wine64-zip.sh`'s container staging
crashes in the binfmt/QEMU layer on this ARM64 host
(`x86_64-binfmt-P: QEMU internal SIGSEGV`), so shipped-zip payload
updates were delivered with the merge scripts (`stage-*.sh`, the same
pattern wine32 already used); the container build remains the canonical
regeneration path on an x86_64 host. Prefix-boot commits carried by
this branch for the integ merge: `f7d352a17` (winemenubuilder
autostart removal via prepare_prefixes sanitize), `5e0e277a8` (browser
prefix sanitizer), `63362bd55`/`fae954a69` (prefix startup differential
docs, timing-sensitive — do not chase the numbers), `1b8b6b6f2`
(prefix boot cost + package guards), `507ba5d0f` (gstreamer pin),
`7ffdc951d` (this register).
