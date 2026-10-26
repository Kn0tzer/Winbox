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

## Follow-up: full-init Snake failure and browser startup path

I compared the pinned and full-init `system.reg`, `user.reg`, `userdef.reg`, and `system32.link` entries. `system32.link` resolves to the same Wine runtime in both prefixes, and neither archive carries a replacement `imm32.dll`. The `Keyboard Layout\\Preload` value is also identical (`"1"="00000409"`). Full init adds empty CTF `SystemShared`/`TIP` keys and NLS `Language` values (`Default`/`InstallLanguage` = `0409`); those state differences remain present in the successful Snake differential below.

The material startup difference was `[Software\\Microsoft\\Windows\\CurrentVersion\\RunServices]`: the pinned prefix has no value there, while full init adds `winemenubuilder.exe -a -r`. The full-init trace opens and starts this helper twice. With `WINEDEBUG=+loaddll,+imm,+err`, Wine loads builtin `imm32.dll`, `DllMain` process attach completes, and `ImeInquire` runs; there is no imm32 error. The earlier `exit_group status=1` had a return address symbolized inside `imm32.dll`, but the debug trace does not show an imm32 failure. The timing pattern points to a cold-start race involving the optional RunServices helper, not an IME registry or DLL mismatch. Because Snake also mapped in a debug-enabled run with the helper present, the helper is a likely race trigger rather than a deterministic failure on every launch.

For the differential, I removed only the `winemenubuilder` RunServices value from a disposable full-init archive. Snake then mapped in 117.091 s with JIT0; the other CTF/NLS state and all Wine service registrations remained intact. `prepare_prefixes.py sanitize` now removes that one host Start Menu integration value when packaging either prefix architecture and when exporting the pinned archives to `dist/runtime`. It leaves other RunServices entries, normal Run entries, RpcSs, PlugPlay, winebus, and wineusb unchanged. The pinned source ZIPs and `tools/browser/runtime-assets.json` were not re-pinned or edited; `tools/browser/bootstrap.py --check` remains the source hash guard.

## Launcher behavior and measurements

The existing normal app path already runs `wine64 <program>` (or the 32-bit `wine <program>`) directly against the initialized prefix. Only the explicit boot probe passes `wineboot --init`. Wine 11's `wineboot -u` is an update operation, not a skip switch ([Wine 11.0 `wineboot.c`](https://raw.githubusercontent.com/wine-mirror/wine/wine-11.0/programs/wineboot/wineboot.c)). Starting only a persistent `wineserver` does not start the GUI-required service stack, so it is not a working shortcut. I added a unit guard for these launcher paths, but did not add an unsafe mode that suppresses required service startup.

Same-build click-to-mapped measurements from `demo-matrix.mjs`:

| 64-bit demo | JIT0 before | JIT0 after | JIT1 before | JIT1 after |
| --- | ---: | ---: | ---: | ---: |
| Notepad | 62.663 s | 79.183 s | 83.824 s | 63.709 s |
| Snake | 52.231 s | 53.232 s | 84.042 s | 55.631 s |

The post-export 64-bit prefix had zero RunServices values to remove, so the packaging cleanup did not change its bytes or startup path. The timing spread is noise across runs, not a demonstrated 64-bit improvement. The post-export probes used the harness's mapped-only mode and reported `stage=mapped-no-input`.

The 32-bit pinned prefix had one `winemenubuilder` RunServices value, which the exported prefix removed. Notepad mapped at 21.4 s (JIT0) and 21.636 s (JIT1) before; it mapped at 19.085 s and 18.826 s after. Snake is not present in `prefix32.zip`, so there is no valid 32-bit Snake measurement.

## Tests and remaining work

- `tools/rootfs64/test_prepare_prefixes.py` now checks the narrow registry edit, archive idempotence, and retention of service/controller entries.
- `web/tests/suite/unit/prefix-launch-mode.test.mjs` checks direct app launch for both architectures and session modes, plus the explicit boot-probe path.
- `web/tests/suite/unit/provenance-artifacts.test.mjs` checks that exported prefixes equal the pinned archives with only the sanitizer and existing desktop/font staging changes.
- Verification: Python prefix unit tests, launcher/provenance unit tests, shell and JS syntax checks, both exported prefix package checks, and the pinned-asset hash guard.

The 32-bit prefix cleanup measured a 2.3–2.8 s Notepad improvement in these runs. The pinned 64-bit prefix was already free of the helper, and this work did not improve its startup. Wine 11 still starts services on the first Wine server instance; a supported way to skip that service startup was not found. ALSA playback and end-to-end controller input were not separately exercised; their packaged drivers and registry keys were preserved by the sanitizer.
