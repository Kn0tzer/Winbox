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
proofs that repaired NAG-20/32/36 in wave 1). A later 2026-10-25
extension added a staging-tree-anchored census of the ALSA-to-OSS
bridge members (see the census extension section below); it found
the 32-bit bridge absent from the shipped zips and restored it
(see the Audio rows).
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
| winegstreamer availability | PARTIAL: PE+unix side present, but gstreamer *plugins* absent → element creation fails → cutscene decode fails | `w64:x86_64-unix/winegstreamer.so`, `w32:i386-unix/winegstreamer.so` both DT_NEED `libgstreamer-1.0.so.0`, `libgstaudio-1.0.so.0`, `libgstvideo-1.0.so.0`, `libgsttag-1.0.so.0`, `libgstbase-1.0.so.0` (present in `glibc-rootfs64.zip`/`wine32.zip`), but neither `wine64.zip` nor `wine32.zip` contained any `gstreamer-1.0/` plugin directory | would break a real game (video cutscenes, Bink-free FMV) | IMPLEMENTED: `tools/rootfs64/prepare-gstreamer.py` pins Debian bookworm `gstreamer1.0-plugins-base`/`-good`/`-libav` (+libav) debs by sha256, stages the plugin `.so` set plus its DT_NEEDED closure into `gstreamer64/`/`gstreamer32/`; `build-wine64-zip.sh` bakes the 64-bit set into wine64.zip and `tools/rootfs64/stage-gstreamer.sh` does the same for wine32.zip (mirroring `stage-alsa32.sh`); `prepare_prefixes.validate_service_runtime` requires the demux/decode plugins; pinned by `runtime-completeness.test.mjs`. Size: +85 MB compressed on the 210 MB wine64.zip / 237 MB wine32.zip boot download (wine32 verified: 236,709,881 → 321,016,564 bytes); the closure is DT_NEEDED-complete, so it cannot be trimmed without dropping demux/decode features. Shipped wine64.zip carries the set via `stage-wine64-payloads.sh` (a2377ce48); shipped wine32.zip via `stage-gstreamer.sh`; `validate_service_runtime` PASS on both shipped pairs (2026-10-25). **Lazy split (2026-10-25, supersedes the placement sentences above):** the plugin set + DT_NEEDED closure no longer rides the boot zips — `tools/browser/stage-dependencies.py` stages the `gstreamer64/`/`gstreamer32/` trees into the lazy support pack at the same guest paths (symlinks stored dereferenced), `build-wine64-zip.sh`/`stage-wine64-payloads.sh` no longer merge it, and `stage-gstreamer.sh` is deleted. The pack is mounted as a `bw64url:` `-zip` on EVERY launch (`project/emscripten/wine64-launcher.js` `buildArguments`) after `web/runtime.html` HEAD-verifies `dependencies.json` + the archive (exact bytes, accept-ranges) and throws before Wine starts if it is unavailable — so a media program’s plugin reads range-fetch the members on first touch, non-media programs never download those bytes, and a missing pack fails loudly pre-boot, never as a silent missing-DLL hunt. `validate_service_runtime` now requires the support pack and rejects the split in both directions (plugin baked into a boot zip = error; plugin missing from the pack = error; both unit-tested in `test_prepare_prefixes.py`), and checker test `t_gstreamer_split` pins every staged closure member’s presence in the retained pack (provenance member counts + tree digests in `dependencies.json`) and absence from both boot zips, plus `prepare-gstreamer.py --check`. wine64.zip 298,882,954 → 210,270,035 B; wine32.zip 321,016,564 → 236,638,528 B; support pack 207,600,063 → 517,840,753 B (closure stored dereferenced: x86_64 262 members / 373,443,728 B, i386 247 members / 351,488,608 B). `check_runtime_completeness.py` 10/10 PASS. The guest-visible filesystem union is unchanged — only the archive carrying the closure changed |
| Native DirectShow/MF decoders not gstreamer-backed | N/A | Staged-tree proof (2026-10-25): the full `x86_64-unix/` census of `wine64.zip` (35 modules) contains exactly one media-decoder unixlib, `winegstreamer.so`; the i386 tree likewise ships only `winegstreamer.so` (+ its `winegstreamer.dll.so` PE-side twin). The PE consumers route to it in the shipped binaries themselves: `w64` `mfplat.dll` contains the wide strings `CLSID_GStreamerByteStreamHandler` and `resolver_create_gstreamer_handler`. No second native decoder module exists in either pinned Wine build to stage. | – | NOT-A-GAP (artifact-cited) |

### Audio

| Item | Present? | Evidence | Severity | Disposition |
| --- | --- | --- | --- | --- |
| winealsa.drv + ALSA→OSS bridge | YES both | `w64:x86_64-windows/winealsa.drv` + `tools/rootfs64/alsa` staging; `w32:i386-windows/winealsa.drv` + `tools/rootfs64/alsa32` (see `stage-alsa32.sh`) | – | NOT-A-GAP for winealsa.drv and the 64-bit bridge (byte-identical to the staged tree, census-verified 2026-10-25). The 32-bit bridge members were NOT covered by any census when this row was written and were in fact absent from the shipped zips — see the next row. Guards now: `prepare_prefixes.validate_service_runtime` (requires the OSS plugin pair per arch), manifest §ALSA-to-OSS bridge census, `check_runtime_completeness.py` (`ALSA-to-OSS bridge members`) |
| ALSA→OSS bridge members (32-bit plugins + config) | Was NO in the shipped artifacts, despite the row above | census extension 2026-10-25: shipped `wine32.zip` contained neither `usr/lib/i386-linux-gnu/alsa-lib/libasound_module_pcm_oss.so` nor `libasound_module_ctl_oss.so`, and shipped `glibc-rootfs32.zip` contained no `etc/asound.conf` and no `usr/share/alsa/` member at all; the manual `stage-alsa32.sh` post-step had not been applied to the current zips (the sibling `stage-fonts32.sh` post-step had), and no check named the members, so the loss was invisible to every guard; the 64-bit bridge was verified byte-identical to `tools/rootfs64/alsa` the same day | would break 32-bit audio: winealsa resolves the default device via libasound, which had neither config nor an OSS module, so `snd_pcm_open` fails | IMPLEMENTED: re-ran `tools/rootfs64/stage-alsa32.sh web/runtime` — `wine32.zip` 236,638,528 → 236,650,075 bytes (+2 members, sha256 `ce9a29ee…`), `glibc-rootfs32.zip` 3,813,320 → 3,875,770 bytes (+91 members, sha256 `84e343ab…`), every added member byte-identical to `tools/rootfs64/alsa32`; both zips re-pinned in `tools/browser/runtime-assets.json` (release id `runtime-assets-20261025` unchanged, still unpublished) and the retained box archive rebuilt at 510,474,240 bytes (sha256 `59448b7d…`; previous archive kept as `browser-runtime-20261025-prealsa.tar`, pre-fix zips in `/home/ubuntu/backups/winbox-alsa-bridge-20261025/`); `bootstrap.py --check` passes and a fake-root `--local` restore from the new archive verifies all 13 assets. Guards: manifest §ALSA-to-OSS bridge census lists every bridge member (name, size, sha256) with expected sets anchored on the staged trees; `check_runtime_completeness.py` (`ALSA-to-OSS bridge members`, 11/11) fails in both directions naming the members — negative proof on doctored scratch copies (one plugin dropped, one config member renamed) fails the checker and drifts the manifest, naming both members; `validate_service_runtime` requires the plugin pair per arch (unit-tested both ways in `test_prepare_prefixes.py`) |
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
| `source/io/fszipurl.cpp:59-104` (`bw64StartFetch` EM_JS + `bw64SyncFetchRange`) | Lazy `bw64url:` block-range fetch has **no timeout anywhere**: the EM_JS `fetch()` is issued with no `AbortController`, `r.arrayBuffer()` on a stalled body never settles, `bw64FetchDone` is then never called, and the guest pthread blocked in `emscripten_proxy_sync_with_ctx` never wakes; threads waiting on the same block wait on `fetchCv` (`fszipurl.cpp:196-199`) with no deadline either. Settled failures are handled loudly (`fetchRaw` logs `range fetch FAILED` and waiters retry as fetchers) — only a *never-settling* fetch hangs, and nothing logs it: the 16 MB-cadence progress line (`:171-175`) only fires after successful fetches. Evidence (GL lane, gl.md Celeste row): Wine launched Celeste, fetched 113 MB of its 2.69 GB archive, then emitted no new logs for 535 s and timed out in `not-mapped`; a separate live-era run mapped and rendered (0.75 nonblank input screenshots), so the failure is intermittent — consistent with a stalled fetch, not a deterministic Celeste incompatibility. The upfront chunk loop in `web/games.mjs` shares the no-timeout shape (secondary; not the Celeste demo path). | would break a real game (any lazily loaded title can hang silently at startup) | IMPLEMENTED (Celeste fix lane, 2026-10-25) — stall watchdog at every layer. (1) `bw64StartFetch` EM_JS (`source/io/fszipurl.cpp:81`) wires an AbortController to a 60 s STALL deadline: the clock starts at issuance and resets when response headers arrive and on every streamed chunk (body-reader path), so a slow-but-progressing fetch is never aborted; where the launcher prefetch wrapper buffers the body before the promise resolves, no progress is visible and the window acts as a per-2 MB-block deadline. On a stall it logs once — `fszipurl: fetch STALL url=… off=… len=… bytesSoFar=… stallMs=…` — aborts the fetch, and settles the job itself with `BW64_FETCH_STALLED` (-2, `fszipurl.cpp:179`) through a settled-guard, so the guest pthread in `emscripten_proxy_sync_with_ctx` always wakes. (2) `fetchRaw` (`fszipurl.cpp:254,262`) retries a failed or stalled chunk up to 3 attempts (permanent 404/403 still fail fast and latch `permanentError`); exhaustion logs `range fetch STALLED` / `range fetch FAILED` as before. (3) Same-block waiters wait on `fetchCv` with a 240 s deadline (`FETCH_WAIT_DEADLINE{240}`, `fszipurl.cpp:189,315`; 4× the stall window, above the fetcher worst case of 3 stall-bounded attempts ≈ 3 min); past it the read fails loudly (`STALL waiting for in-flight block`) without touching the fetcher-owned `inFlight` entry, so no duplicate fetch and no deadlock. (4) The launcher prefetch wrapper (`project/emscripten/wine64-launcher.js:126`, `block()`) adds a 120 s backstop watchdog (2× the EM_JS window) that aborts the native fetch and rejects the job, so its `pending` map cannot wedge a block forever (`bw64-prefetch: fetch STALL …`). (5) `web/games.mjs:72` (`stallWatchdog`) gives the upfront download the same shape per request: progress resets per chunk, a stall aborts and surfaces as a loud `download stalled` error. Why 60 s: healthy same-origin 2 MB range fetches settle in 50–200 ms (fszipurl design comment); a full minute of zero bytes is a dead connection, not a slow one — and the observed Celeste stall was 535 s of silence, far past the trip point. Retries are per-block, so an aborted transfer loses at most one 2 MB block. Test: `web/tests/suite/unit/fszipurl-fetch-stall.test.mjs` (9 tests, PASS: EM_JS body extracted and run against a never-settling fetch — settles -2, abort fired, exactly one stall log with url+offset; a progressing chunked fetch spanning longer than the window is NOT aborted; settled-failure codes -1/-404 preserved; arrayBuffer fallback; launcher job rejects, pending clears and the retry refetches fresh; games.mjs stall rejects loudly and a progressing download completes; C++ source pins for the watchdog/deadline/retry/log lines). WASM build compile-verified in this lane (see commit). Browser proof (celesteproof lane, 2026-10-25; evidence `/home/ubuntu/backups/winbox-celeste-proof-20261025/`): dist rebuilt at this branch tip 72a1d02e3 (BUILD_INFO git_sha == HEAD, 0 dirty files; stall strings present in the built wasm/js). Solo demo-matrix run (DEMO=celeste, JIT=1, celeste-drmfree.zip): window mapped 34.0 s after click; `CELESTE : 1.4.0.0` running at 52 s; lazy fetch progressed continuously for the whole session (609 MB fetched by 360 s; the 16 MB-cadence progress lines never went silent — cf. the original failure's 113 MB then 535 s of silence) with ZERO `fetch STALL` / `range fetch FAILED` lines; first rendered frame between +240 s and +360 s post-map (canvas 1024x768, nonblankFraction 0.75, 10,000 unique colours, overlay cleared); the title-screen starfield is live-animated and the scripted inputs (C x3, arrows, X) advanced the game to its file-select UI (all 7 input screenshots 0.75 nonblank); zero page errors, zero HTTP errors. Stall path exercised separately (route-level hang of every archive range GET): `fszipurl: fetch STALL ... stallMs=60000` fired at exactly 60 s per attempt, retries logged `range fetch retry 2/3 ... -> -2` and `3/3`, and the read failed loudly at 189 s with `range fetch STALLED ... retries exhausted` — a never-settling fetch now fails loudly in ~3 min instead of hanging silently forever. Scope limits, stated plainly: an in-level gameplay frame was not reached inside the matrix window (the run ended on the title/file-select menu), and that menu's glyph tiles render fragmented — a texture-atlas rendering matter owned by gl.md's Celeste row, not a fetch defect. The gl.md Celeste cross-reference (fetch stall before any GL work) is now answered on the fetch side, so its remaining visual verification can proceed at the integ gate on the merged build; the Celeste demo re-run remains part of the standard integ demo-matrix gate like every other demo. |

### Size budget (runtime-assets.json)

`tools/browser/runtime-assets.json` pins per-file `bytes` + `sha256` for the
six boot zips plus the slang compiler triple — i.e. any change to a zip's
*content* is caught by `bootstrap.py --check`. There is no separate
byte-count cap; the boot download is ~659 MB total
(glibc-rootfs64 10 MB + wine64 299 MB + prefix64 19 MB + glibc-rootfs32 4 MB +
wine32 321 MB + prefix32 6 MB), and large optional components (wine-mono 82 MB,
wine-gecko, OpenAL, DXVK, guest GL/Vulkan shims) are already lazy-loaded via
the support pack + `dependencies.json` manifest
(`tools/browser/stage-dependencies.py`). No silent size failure exists:
every byte count is hash-pinned. Size state after the 2026-10-25 lane
pass (disk is the binding constraint on the box — /home/ubuntu:
docker 23G, kn0tzer 21G, worktrees 17G, backups 7.7G, main repo 6.3G):
wine64.zip grew 201 MB → 299 MB and wine32.zip 237 MB → 321 MB with the
gstreamer closure (+85 MB compressed per arch, DT_NEEDED-complete, see
the winegstreamer row — accepted because cutscene decode has no smaller
complete form); the support pack grew ~196 MB → 207,600,063 bytes with
the 5 MB CJK TTC (lazy-loaded, not boot cost). The shipped zips' new
hashes were re-pinned into `runtime-assets.json` on 2026-10-25 (release
`runtime-assets-20261025`; see the re-pin addendum under Lane status) —
`bootstrap.py --check` passes against the shipped artifacts again. The
pins’ remote half (publishing the release asset) remains a release-step
item, listed in that addendum.

**Split update (2026-10-25):** the gstreamer closure moved from the boot zips to the support pack (see the winegstreamer row), so the boot download is now ~486 MB total (glibc-rootfs64 10 MB + wine64 210 MB + prefix64 19 MB + glibc-rootfs32 4 MB + wine32 237 MB + prefix32 6 MB) and the support pack is 517,840,753 bytes (lazy, range-fetched per member — not a boot cost). The two changed zips were re-pinned into `runtime-assets.json` the same day (release id `runtime-assets-20261025` unchanged, still unpublished); the retained box archive was rebuilt at 510,382,080 bytes (sha256 `8da7911a16d9221734e9253bd1e406b58c24e0f5d86edfaf0711226e49061c32`), with the pre-split archive kept alongside as `browser-runtime-20261025-presplit.tar` and the pre-split zips in `/home/ubuntu/backups/winbox-gstsplit-20261025/`. `bootstrap.py --check` passes and a fake-root `--local` restore from the new archive verifies all 13 assets.

## Census extension (2026-10-25, manifest census lane)

The wave-2 manifest censused the member families the register's rows
cite, but any zip member class outside those families was invisible to
the checker: a member could vanish (or a wrong-arch member land) in a
re-stage and, after the manifest was regenerated, no check would fail —
the whole-zip sha256 pins only detect drift until the next legitimate
re-pin. This pass inventoried every shipped zip's member classes
against the census + checker pins:

- **ALSA-to-OSS bridge (was uncensused; now pinned — and it had in
  fact silently broken, see the Audio rows):** the OSS plugin modules
  and `/usr/share/alsa` config trees staged from `tools/rootfs64/alsa`
  and `tools/rootfs64/alsa32` are censused in manifest §ALSA-to-OSS
  bridge census (every member by name, size and sha256; expected sets
  anchored on the staged trees) and asserted in both directions by
  `check_runtime_completeness.py`. Both arches' `etc/asound.conf` are
  pinned byte-identical to the canonical config (the 64-bit copy is
  printf-generated into `glibc-rootfs64.zip` by
  `build-wine64-zip.sh`). Provenance: x86_64 from Debian bookworm
  `libasound2` + `libasound2-plugins` (the `alsa/README` refresh record
  names packages, not versions); i386 plugins from Debian
  `libasound2-plugins` 1.2.7.1-1 with the config tree from the same
  i386 extraction (`alsa32/README`).
- **Deliberately not member-pinned** (whole-zip hashes plus the family
  censuses remain the evidence; per-member pins would churn on every
  vendor rebuild without covering a register claim): zip directory
  entries (structural); the Wine vendor data trees (`usr/share/wine`
  fonts/nls/inf, the X11 locale tree, Wine locale data); the glibc/libc
  vendor trees in the rootfs zips (the register's only member claims
  there are the DejaVu faces, sha-pinned); prefix-zip payloads and
  skeleton files (build products of the prefix builders — registry
  content is pinned by the checker and
  `runtime-completeness.test.mjs`, and the p32 DXVK root DLLs are
  censused); the support pack's mono/gecko payload beyond the named
  census (vendor payload; the claimed members are named there);
  generated caches (`var/cache/fontconfig/*.cache-8`, `etc/ld.so.cache`
  — regenerated from the fonts/config they cache).

## Post-step elimination (2026-10-25, post-step elimination lane)

The census extension above caught the 32-bit ALSA bridge omission *after
the fact*. This pass eliminates the defect class at build time: the
silent-incomplete state — a shipped zip missing a staged member class,
afterwards blessed by a re-pin — is now unreachable through every path
that produces, exports, or verifies the runtime set.

The manual steps a human previously had to remember, and their
disposition now:

| Manual step | Artifact(s) | Disposition |
| --- | --- | --- |
| `tools/rootfs32/stage-fonts.py` (fontconfig/expat/DejaVuSans into the 32-bit glibc rootfs) | `glibc-rootfs32.zip` (web + dist) | Run by `apply-runtime-poststeps.sh`; presence pinned by the checker (fonts32 faces + manifest census) |
| `tools/rootfs64/stage-fonts32.sh` (DejaVu Bold/Mono faces) | `glibc-rootfs32.zip` (web + dist) | Run by `apply-runtime-poststeps.sh`; pinned by checker test `fonts32 faces` |
| `tools/rootfs64/stage-alsa32.sh` (OSS plugins + ALSA config) — **the step whose omission caused the 32-bit bridge defect** | `wine32.zip`, `glibc-rootfs32.zip` | Run by `apply-runtime-poststeps.sh` (web + dist); pinned both directions by checker test `ALSA-to-OSS bridge members` and `validate_service_runtime` |
| `tools/rootfs64/stage-wine64-payloads.sh` (winehq10 backports, merge-script path used when the container build is unavailable) | `wine64.zip` (web + dist) | Run by `apply-runtime-poststeps.sh`; pinned by checker test `winehq backports (source + shipped zip)` |
| Copying `tools/rootfs64/dist/*.zip` (from `build-wine64-zip.sh`) into `web/runtime` | `wine64.zip`, `glibc-rootfs64.zip` | Still a manual copy, but no longer blessable incomplete: `bootstrap.py --check` and the `build.mjs` gate (below) both refuse an incomplete set, and `build-wine64-zip.sh` now hard-fails (was: warn-and-continue) when the ALSA tree or a winehq10 backport DLL is missing, since the checker requires those members in its output |
| Regenerating `docs/known-gaps/runtime-artifact-manifest.md` after any zip change | the manifest | Run by `apply-runtime-poststeps.sh` from the final zip bytes; drift fails checker test `manifest matches shipped zips` |
| Re-pinning `tools/browser/runtime-assets.json` + rebuilding the retained archive after zip changes | the pins | Still manual (it is the act of publishing new canonical bytes), but `bootstrap.py --check` now runs the completeness checker after the pin comparison, so re-pinning an incomplete set fails verification instead of blessing it |
| `tools/browser/stage-wine32-fonts.py` (prefix font aliases) | exported `dist/runtime` prefixes only | Already automated: `build.mjs` runs it on every export; it never touches the canonical set, so it is deliberately not part of the finisher |

Mechanisms, smallest change per choke point:

- **`tools/rootfs64/apply-runtime-poststeps.sh`** (new) is the single
  automated finisher for the assemble-by-hand path. If the current set
  already verifies complete it changes nothing (pinned bytes untouched);
  otherwise it applies every post-step above, regenerates the manifest
  from the final zips, and exits nonzero unless
  `check_runtime_completeness.py` passes. After a byte-changing run it
  prints the re-pin instruction.
- **`tools/browser/build.mjs`** runs `check_runtime_completeness.py` as
  a hard gate at the end of staging (after the copies, prefix sanitize,
  font staging and desktop pin), pointed at the export's own freshly
  built support pack via `BW64_SUPPORT_DIR`; a failure aborts the
  export, so `tools/bw64-build.sh` cannot produce a deployable tree from
  an incomplete canonical set.
- **`tools/browser/bootstrap.py --check`** runs the completeness checker
  after the pins verify, closing the re-pin hole directly: pins matching
  incomplete bytes no longer verify clean.
- **`tools/rootfs64/build-wine64-zip.sh`** hard-fails on a missing ALSA
  tree or winehq10 backport DLL (the members the checker requires in its
  output zips) instead of warn-and-continue. The libGL/libvulkan
  prebuilt warnings are unchanged: those shims are not post-steps onto
  finished zips, and libvulkan already self-verifies via
  `build_vulkan_shims.py --check` when present.
- **`check_runtime_completeness.py`** resolves the support pack through
  `BW64_SUPPORT_DIR` (default: the canonical `dist/runtime`), and the two
  pack-dependent checks (`validate_service_runtime`,
  `gstreamer closure rides the support pack`) skip — loudly, printed —
  only when no support pack has been built yet; at both gates above the
  pack has just been built (or staged by a prior export), so they run
  for real. No check was weakened: the zip/source checks are unchanged
  and run in every context.

Negative proof (scratch tree, 2026-10-25): the original failure was
reproduced exactly — the two OSS plugins removed from `wine32.zip`
(doctored copy came out at 236,638,528 bytes, the pre-fix shipped size)
and `etc/asound.conf` + the `usr/share/alsa` tree removed from
`glibc-rootfs32.zip`, with `runtime-assets.json` re-pinned to the
doctored bytes. `bootstrap.py --check` then exited 1 naming the missing
members (`missing ALSA->OSS bridge plugins: ...libasound_module_pcm_oss.so,
...libasound_module_ctl_oss.so`), despite the pins matching; the
`build.mjs` gate invocation failed identically. Running
`apply-runtime-poststeps.sh` healed the set (healed `wine32.zip`:
236,650,075 bytes, the fixed shipped size), regenerated the manifest,
and gated green; after re-pinning the healed bytes,
`bootstrap.py --check` passed fully (checker 11/11, 13 assets
verified). On the real tree the finisher is a no-op ("already
complete"), `bootstrap.py --check` passes with the gate in the path, a
full `build.mjs` export with the gate in the path exits 0, and no
shipped artifact bytes or pins changed in this pass.

### Producer hardening follow-on (2026-10-25, wine64-zip producer lane)

Completes the `build-wine64-zip.sh` bullet above: the libGL/libvulkan
warn-and-continue paths it deliberately left are now closed, the
script's late-discovered dependencies are preflighted, and a
pre-existing quoting breakage in its container payload is repaired.

- **Up-front host preflight.** Before any work, the script checks
  every host tool it uses (`dirname`, `mkdir`, `rm`, `cp`, `docker`,
  `python3`, `zip`) and exits 1 naming the missing one. Previously
  `zip` was first invoked at the very end (after all container + host
  staging) and `python3` mid-run (the `build_vulkan_shims.py --check`
  call), so a missing tool surfaced as a bare exit 127 with the
  expensive staging already done. The trailing informational
  `ls -lh` is guarded so its failure cannot misreport a successful
  build.
- **Input preflight.** The docker image and every prebuilt input
  (the libGL/libvulkan shims, the three winehq10 backport DLLs, the
  ALSA tree) are verified before the staging run; the staging-point
  hard-fails remain as backstops.
- **libGL / libvulkan: hard-fail by default, explicit opt-in.**
  Caller evidence: nothing in the repo invokes this script
  programmatically (no `build.mjs` / `bw64-build.sh` caller; it is
  run by hand as the canonical producer, and
  `publish-rootfs-release.sh` publishes its output), and no caller
  depends on the warning path — the merge script
  `stage-wine64-payloads.sh`, used when the container build is
  unavailable, does not invoke it. A missing shim therefore exits 1
  naming the prebuilt and its build script. The only exceptions are
  the explicit opt-ins `BW64_ALLOW_NO_GL=1` / `BW64_ALLOW_NO_VK=1`
  (for workflows that deliberately source a shim elsewhere — the
  browser support pack also carries `libvulkan.so.1`, staged by
  `tools/browser/stage-dependencies.py`), which print a banner at
  the preflight, a warning at the staging point, and a closing
  summary naming exactly what was omitted; the default path never
  degrades silently. This supersedes the "warnings are unchanged"
  sentence in the bullet above.
- **Container-script quoting repair (pre-existing breakage, found
  by this pass).** The `docker run ... bash -c '<script>'` payload
  contained apostrophes in nine comment lines (`dlopen'd`,
  `crypt32's`, ...), which terminated the single-quoted string:
  docker received a ~1 KB truncated script plus stray words, and
  the host shell then executed the remaining payload lines itself
  (`copy: command not found`, exit 127) — the script as committed
  could not complete a run on any host. The nine comments are
  reworded without apostrophes (comment text only; the executed
  payload is otherwise unchanged), and an argv-capture check proves
  docker now receives the full 8,413-byte payload as a single
  argument.
- **Scratch evidence** (stub `docker` fabricating the container's
  staging output, restricted PATH farms; shipped artifacts
  untouched): 37/37 assertions — the happy path produces both zips
  with every staged payload present and the staged shim bytes
  identical to the prebuilts; missing `zip` / `python3` fail before
  the dist directory is even created, naming the tool; a missing
  libGL or libvulkan prebuilt fails before any staging, and with
  its opt-in the run completes with the three loud notices and
  exactly that shim absent from both zips. A real-docker run on
  this ARM64 host passed both preflights and then stopped inside
  the container staging with the documented binfmt/QEMU SIGSEGV
  (the environment limit recorded below), so byte-identity with
  the shipped zips cannot be produced on this host; the container
  build remains the canonical regeneration path on an x86_64
  host. No shipped artifact bytes or pins changed in this pass;
  the runtime gate set re-verified green afterwards (checker
  11/11, `test_prepare_prefixes.py` 9 OK, `bootstrap.py --check`
  green, unit suite 184/0 solo). Register stays **OPEN: 0**.

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
  section). No other row depends on the support pack's CJK members. **Resolved (2026-10-25, split-finish lane):** the retained pack is now `support-e6f74fef…` (517,840,753 B), staged by the current `stage-dependencies.py`, and it contains all three CJK members (`usr/share/fonts/truetype/wqy/wqy-microhei.ttc`, `etc/fonts/conf.d/65-wqy-microhei.conf`, `opt/winbox/licenses/wqy-microhei-copyright`) — verified directly against the retained artifact; the manifest’s support-pack census reflects it.
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
compile verification; the solo Celeste browser probe has since
been run by the celesteproof lane on this branch (2026-10-25:
mapped 34.0 s, continuous fetch progress with zero stall lines,
rendered title/file-select UI, stall path demonstrated loud —
see the row above; the demo re-runs at the integ gate on the
merged build like every other demo). Prior state: **OPEN: 1** after the wave-2
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

### Re-pin addendum (2026-10-25, runtime asset re-pin lane)

`tools/browser/runtime-assets.json` no longer pins the 2026-10-13
hashes. It now describes release `runtime-assets-20261025`: the four
changed boot zips (wine64, wine32, prefix64, glibc-rootfs32) carry the
bytes + sha256 of the shipped `web/runtime/` artifacts exactly as
recorded in `runtime-artifact-manifest.md` (cross-checked file by
file before pinning; glibc-rootfs64, prefix32 and the seven Slang
files are unchanged since 2026-10-13 and keep their pins). A new
local archive `browser-runtime-20261025.tar` (510,382,080 bytes,
sha256 `8da7911a16d9221734e9253bd1e406b58c24e0f5d86edfaf0711226e49061c32`)
containing exactly the 13 pinned members was built from the shipped
files and is retained at
`/home/ubuntu/backups/winbox-runtime-assets-20261025/` on the build
box (the 2026-10-13 archive under `boxedwine64-maintenance-20261013`
is untouched). Verified in this lane: `bootstrap.py --check` passes
in the worktree; `bootstrap.py --local <new tar>` restores and
verifies all 13 assets into a clean tree and `--check` passes there;
`test-bootstrap.py` 4/4; `check_runtime_completeness.py` 0 failures;
`tools/browser/build.mjs` export completes (build.mjs does not read
the pins — it copies `web/runtime/` into `dist/` and then mutates
only the dist copies, so the pins describe the `web/runtime/`
sources, not `dist/`).

Release-step checklist (the only remaining half of the re-pin —
publishing; nothing here was pushed or published by this lane):

1. Create the GitHub release tagged `runtime-assets-20261025` in
   `Kn0tzer/Boxedwine64` (the `release` / `repository` fields of
   `runtime-assets.json`).
2. Upload one asset named exactly `browser-runtime-20261025.tar`
   (510,382,080 bytes, sha256
   `8da7911a16d9221734e9253bd1e406b58c24e0f5d86edfaf0711226e49061c32`;
   build-box copy:
   `/home/ubuntu/backups/winbox-runtime-assets-20261025/browser-runtime-20261025.tar`).
   `bootstrap.py` constructs the download URL from the manifest, so
   tag and asset name must match it exactly.
3. From a clean checkout, run `python3 tools/browser/bootstrap.py`
   with no `--local` and confirm it downloads, restores and verifies
   all 13 assets (the 2026-10-13 release URL no longer serves its
   asset; until this release exists, clean builds restore offline
   via `--local` with the retained box archive above).

## GStreamer split finish (2026-10-25, split audit/finish lane)

A cancelled lane had left this split half-landed: staging scripts edited,
boot zips rebuilt smaller, support pack rebuilt with the closure — but the
manifest stale, the pins stale (`bootstrap.py --check` failing on both wine
zips), and nothing committed. Audit verdict: the design was sound and the
trigger honest (the support pack is an always-mounted, pre-boot-verified
lazy zip — see the winegstreamer row), so this lane finished it rather than
reverting. Audit evidence: the partial lane had retained the pre-split zips
in `/home/ubuntu/backups/winbox-gstsplit-20261025/` (byte sizes match the
fdf4c66be pins exactly); the rebuilt zips pass full CRC; the re-run of
`tools/browser/build.mjs` (export) in this lane reproduced the rebuilt
support pack byte-identically (517,840,753 B, sha256 `e6f74fef…`), proving
the pack on disk is exactly what the now-committed staging code produces;
the export also re-stamped `wine32-font-provenance.json` against the new
wine64.zip, clearing the one unit failure the split had caused
(`provenance-artifacts`, stale `source_wine64_sha256` — a generated record,
regenerated by the pipeline, not hand-edited). Verification: manifest
regenerated; `check_runtime_completeness.py` 10/10 PASS, 0 skips;
`test_prepare_prefixes.py` 9 OK; `bootstrap.py --check` passes (13 files);
fake-root `--local` restore from the rebuilt archive verifies all 13
assets; solo unit suite 184 passed / 0 failed (an earlier 183/1 run
overlapped another lane's suite and is discarded per the solo rule).
Commits: `9af7fe783` (split) and the re-pin/register commit carrying this
note. Register stays **OPEN: 0**; no row changed disposition — the
winegstreamer row's placement text was updated to the split, the size
budget was corrected, and the wave-2 CJK retained-artifact flag is resolved
above. Remaining proof is unchanged in kind: an end-to-end media-playing
guest program is a gate/browser item, as it was pre-split (the
guest-visible filesystem union is identical; only the archive carrying the
closure changed).
