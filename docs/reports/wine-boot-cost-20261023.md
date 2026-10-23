# Wine prefix boot cost (2026-10-23)

## Finding

The first-launch trace does not show a large prefix installation in the shipped 64-bit prefix. Its Wine updater marker is already .update-timestamp=disable, which tells Wine to skip the wine.inf update. The remaining launch delay is in the normal wineboot --init service/startup path. Prewriting more INF state therefore did not remove the measured work.

Wine 8.0 wineboot --init creates shared kernel data and registry values, handles pending renames, processes RunServicesOnce and RunServices, starts services.exe, then calls the prefix updater. When the marker is absent or stale, the updater runs PreInstall, architecture-specific DefaultInstall/Wow64Install, installs root PnP devices, updates the user profile, and records the INF timestamp. Wine's source treats the literal disable as a request to skip that updater: [Wine 8.0 wineboot.c](https://raw.githubusercontent.com/wine-mirror/wine/wine-8.0/programs/wineboot/wineboot.c).

## First normal boot diff

I captured the shipped 64-bit prefix before and after a browser Notepad launch.

- The archive began with .update-timestamp=disable; Wine left it unchanged.
- The writable prefix overlay gained the drive_c/ and drive_c/windows/ directories; no files were copied out of the read-only prefix archive.
- userdef.reg did not change.
- system.reg grew from 1,443,755 to 1,444,064 bytes (+309). Wine added ten font-substitution values and changed NUMBER_OF_PROCESSORS from 8 to 1.
- user.reg grew from 9,306 to 9,307 bytes (+1). Wine refreshed the Fonts-key timestamp and sorted the existing Direct3D csmt=0 entry into registry order.
- In the trace, wineboot.exe --init started around 17–18 seconds and exited around 38.7 seconds. services.exe started around 30 seconds and remained active during app initialization. Notepad opened around 50.6 seconds and mapped at 65.614 seconds in the original run.

This shows the updater was already skipped. The prefix still pays for Wine's per-launch startup and service path.

## Full initialization capture

To identify the first-install writes, I removed the marker in a disposable prefix copy and ran the native Linux Boxedwine build with wineboot --init. The process remained active after six minutes, so I stopped it after preserving the prefix overlay. The trace showed wine.inf PreInstall/DefaultInstall, setupapi INF/PNF work, and registry writes. The overlay contained 43 paths absent from the starting archive and changed system.reg and user.reg; userdef.reg stayed unchanged.

The new files included profile utilities (explorer.exe, notepad.exe, regedit.exe, hh.exe), ADO/OLE DB files, input.inf, winebus.inf, winehid.inf, wineusb.inf, winexinput.inf and PNF files, sorting data, themes, TWAIN modules, and VC80/VC90/common-controls/GDI+ WinSxS payloads. system.reg grew from 1,444,065 to 1,685,360 bytes (37,484 to 44,170 lines), adding RpcSs, PlugPlay, winebus and wineusb registrations, bus device-enumeration keys, and Gamepad/RawGameController classes. user.reg grew from 9,306 to 22,361 bytes (268 to 660 lines) and retained Direct3D csmt=0.

The native headless trace reported winedevice.exe load failure (c0000135), Wine bus/USB start error 126, PlugPlay timeout 1053, and RpcSs start failure. I did not disable these services. Capturing the prefix writes does not fix those startup failures or remove the per-launch service work.

## Browser measurements

I built the runtime once at commit 2c49cb2f8 (WASM SHA-256 08398781…). All numbers below are JIT=0 click-to-first-mapped-window measurements from demo-matrix.mjs. The same-build pinned-prefix controls used the exact original prefix archive hashes. Screenshots were inspected: Notepad showed typed text; Snake showed its game-over screen.

| Demo | Before: pinned prefix, same build | Full-init package trial | Final local runtime after reverting trial |
| --- | ---: | ---: | ---: |
| Notepad | 62.663 s | 106.813 s | 62.663 s |
| Snake | 52.231 s | Not mapped within 300 s | 52.231 s |

A smaller generated-prefix trial also regressed: Notepad mapped in 84.962 s and Snake in 59.299 s. Its 64-bit user.reg differed from the pinned archive only by an added Audio=alsa value; the 32-bit archive also changed. I removed the automatic ALSA override from the packaging script and restored both browser prefix archives to their original pinned bytes. The browser runtime manifest also matches its original copy. The final local runtime therefore has no prefix-state change and no startup-time win.

For historical context, the earlier runtime measured 65.614 s for Notepad and 56.991 s for Snake; those measurements used a different runtime build and are not a same-build comparison. The pinned-prefix same-build control is the comparison used above.

## Preserved runtime state and guard

The final browser prefix archives are byte-identical to the original pinned archives.

- Both Wine runtime zips contain winealsa.drv. Neither pinned prefix has an explicit Audio registry value; I did not add one. The Notepad/Snake probes did not produce audio, so this verifies that the ALSA payload survived packaging, not audible playback.
- The 32-bit prefix retains its existing d3d9.dll, d3d11.dll, and dxgi.dll payloads; their hashes match the pinned archive. The Wine runtime zips retain D3D11/D3D12 modules. The 32-bit runtime also has d3d12core.dll; the 64-bit runtime does not. No custom vkd3d DLL is installed into either prefix.
- Both prefixes retain Gamepad and RawGameController registry keys. The 32-bit prefix also has WINEBUS/WINEUSB enumeration keys. Service/device images (services.exe, rpcss.exe, plugplay.exe, winedevice.exe, winebus.sys, winehid.sys, wineusb.sys) remain in the linked Wine runtimes. The 32-bit registry has service Start values; the 64-bit prefix relies on Wine defaults and has no service override sections.

tools/rootfs64/build-prefix64.sh now starts from the pinned browser prefix by default, accepts an explicit captured/native boot state for controlled packaging, and runs a packaging guard over both architecture prefixes and their linked Wine runtimes. The guard checks the completed update marker, registry hives and links, enabled service overrides, controller/HID keys, ALSA driver, and D3D/DXVK payloads. It does not force a full-init prefix or an Audio override into the local browser assets.

tools/rootfs64/test_prepare_prefixes.py adds six unit tests for boot-state overlay, marker and registry validation, service settings, controller/HID state, and linked ALSA/service/D3D runtime payloads. The unit tests, tools/browser/bootstrap.py --check, and the guard against both pinned prefix/runtime pairs passed. One WASM build was used; no second build was run.

## Open work

A packaged prefix marker cannot skip wineboot --init itself. Avoiding that remaining startup path requires a launcher-level change, outside the owned paths for this task. The full-init candidate was not kept because it did not improve startup; its Snake probe did not map within 300 seconds and logged guest exit_group status 1 in imm32.dll, whose root cause was not isolated. The smaller Audio override trial was slower. ALSA playback and end-to-end controller input were not separately exercised; only runtime payloads and registry keys were checked. The separate 32-bit prefix builder, tools/build-prefix32.sh, was not changed, and no new initialized state is shipped in either prefix archive.
