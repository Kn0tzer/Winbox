# Wine 8.0 core DLLs rebuilt with loader-surface backports

`web/runtime/wine64.zip` is staged from Debian bookworm's `wine64 8.0`
(`boxedwine64/wine64-debian:bookworm`). That module set is missing exports
that real Windows DLLs have and real programs import at load time — a
missing *regular* import is a hard load failure before the program's own
code runs. The loader census (`tools/conformance/pe_census.py`, see
`docs/reports/broad-conformance-analysis-20261030.md`) convicted three
gaps on demo load paths:

* `user32.dll` — the whole `GetPointer*Info` family beyond the five basic
  entries is absent. A demo executable (`SquashTheCreeps.exe`, Godot)
  regular-imports `GetPointerPenInfo` and **cannot load at all**.
* `kernelbase.dll` / `kernel32.dll` — `CopyFile2` is absent from both
  (in the 8.0 source it is a commented-out spec stub). A bundled demo
  library (`steam_api64.dll`) regular-imports it from `kernel32.dll`.
* `iphlpapi.dll` — `GetAnycastIpAddressTable` is absent (also a
  commented-out spec stub in 8.0). A bundled JRE `net.dll` delay-imports
  it; upstream Wine's own implementation returns an empty table.
* `win32u.dll` — not a load gap but a behaviour fix in code the tree
  already ships. Wine's desktop initialization gives up on a slow
  desktop process: the first process to need the desktop starts
  `explorer.exe` and waits only 10 s for it to go input idle, after
  which the server fabricates a detached desktop window the real
  desktop process can never adopt, and `load_desktop_driver` latches
  the null driver permanently if the desktop's driver publication
  (guid property + `GraphicsDriver` registry value) has not landed
  yet. On hardware explorer wins that race easily; under emulation
  the desktop parade takes minutes, so first-visit session starts
  died with "The explorer process failed to start" while the desktop
  came up moments later. The patch makes both waits patient (see the
  patch table); a desktop that is already up costs nothing.

The DLLs in this directory are **the same Wine 8.0 code the rest of
the tree ships**, rebuilt from the pinned `wine-8.0` source tag with
small backport patches. The first three add exactly the missing
exports above, mirroring how upstream Wine later implemented them;
the fourth changes no export surface at all; the fifth (FD-168) implements fourteen win32u syscalls Wine 8.0 shipped as spec stubs, backported from the later upstream implementations and adapted to this tree's server protocol and display-device model. The fourth patch's code
lives in the unix half of its module, so `win32u.so` is rebuilt and
shipped alongside `win32u.dll` from the same patched source tree —
until 2026-10-31 only the PE half was rebuilt, and the patch shipped
in no binary at all (FD-01, reopened per FD-43):

| patch | change |
| --- | --- |
| `tools/buildWine/patches/wine8-user32-pointer-family.patch` | spec + stub-family implementations for `GetPointerPenInfo`, `GetPointerPenInfoHistory`, `GetPointerInfoHistory`, `GetPointerFrameInfo`, `GetPointerFrameInfoHistory`, `GetPointerFrameTouchInfo`, `GetPointerFrameTouchInfoHistory`, `GetPointerFramePenInfo`, `GetPointerFramePenInfoHistory` — the same FIXME-stub pattern Wine 8.0 itself uses for the five family members it does ship (`GetPointerInfo` etc. all return FALSE in 8.0; on a machine with no pointer devices FALSE is also the behaviourally correct answer). |
| `tools/buildWine/patches/wine8-kernelbase-copyfile2.patch` | `CopyFile2` implemented in `kernelbase` on top of the existing copy engine (upstream's later refactor of `CopyFileExW` into a shared `copy_file` helper, backported verbatim in structure), plus the `kernel32` `-import` forward. This is a real implementation — `CopyFile2` copies files. |
| `tools/buildWine/patches/wine8-iphlpapi-anycast-table.patch` | `GetAnycastIpAddressTable` enabled and implemented exactly as upstream does (validate arguments, return an allocated empty table with `NO_ERROR`), plus the `MIB_ANYCASTIPADDRESS_*` types upstream added to `netioapi.h`. |
| `tools/buildWine/patches/wine8-fd167-contracts.patch` | FD-167 contract backports in `msvcp140` and `setupapi`: `?_Xbad_function_call@std@@YAXXZ`, `?_XGetLastError@std@@YAXXZ` and `?_Throw_C_error@std@@YAXH@Z` become real throwing implementations (upstream Wine master's bodies, backported together with the `system_error` error-code member, the `bad_function_call` class data, and the error-category objects for msvcp140), and `SetupDiGetDeviceInterfaceAlias` becomes a real lookup over the device's registered interfaces. A bundled demo library (an Xbox services thunks DLL) imports the three throw helpers; calling a spec stub kills the guest process. `?_Throw_Cpp_error@std@@YAXH@Z` remains a stub: no authoritative semantics for it exist in any open source consulted. |
| `tools/buildWine/patches/wine8-win32u-desktop-wait.patch` | Desktop-init patience in `win32u`: `get_desktop_window`'s `NtUserWaitForInputIdle` for the just-started desktop process goes from 10 s to 300 s (the wait still returns immediately if the process exits), so the server-side fabricated desktop window is not created under a healthy-but-slow desktop; and `load_desktop_driver` retries its publication probe (desktop-window guid property + `GraphicsDriver`/`DriverError` registry values) every 500 ms for up to 300 s instead of latching the null driver on the first missing probe. A named driver or a driver error remains a definitive, immediate answer; only a missing publication is retried. Export surface is byte-identical to the Debian build's (1321 exports, verified). |
| `tools/buildWine/patches/wine8-win32u-fd168-backports.patch` | FD-168 win32u stub draw-down: real implementations of `NtUserAlterWindowStyle`, `NtUserBuildHimcList`, `NtUserBuildNameList`, `NtUserBuildPropList`, `NtUserGetClipCursor`, `NtUserGetPointerType`, `NtUserGetThreadState`, `NtUserQueryDisplayConfig`, `NtUserQueryWindow`, `NtUserRegisterWindowMessage`, `NtGdiGetMiterLimit`, `NtGdiSetMiterLimit`, `NtGdiIcmBrushInfo` (spec stubs become `-syscall` entries; the syscall table grows 276 -> 289). `NtUserBuildNameList` loops this tree's index-based `enum_winstation`/`enum_desktop` server requests (upstream's single-call request does not exist in this vintage); `NtUserQueryDisplayConfig` maps upstream's source/GPU walk onto this tree's adapter/monitor model; `NtUserRegisterWindowMessage` uses this tree's single server atom table (`add_atom`). The `NtUserGetClipCursor` header inline over `NtUserCallOneParam` is replaced by the real export (the dispatch case stays, so clients built against the inline are unaffected). |
| `tools/buildWine/patches/wine8-win32u-fd168-slice3.patch` | FD-168 win32u stub draw-down, slice 3: four more spec stubs become real, composed from machinery already in the tree. `NtUserDrawCaption` forwards to `NtUserDrawCaptionTemp` with the window's own font/icon/text and the caller flags masked to the public caption bits (0x103f), exactly what user32's `DrawCaption` itself does; `NtGdiDrawEscape` is the no-output form of the existing `NtGdiExtEscape` wrapper, exactly what gdi32's `ExtEscape` routes to; `NtGdiPolyPatBlt` paints each entry by selecting its brush and calling `NtGdiPatBlt` with the caller's rop (kernel semantics: invalid-brush entries skipped, invalid DC fails, count zero is a TRUE no-op; `POLYPATBLT` typedef added to `wingdi.h`); `NtUserWindowFromPhysicalPoint` joins the syscall table and forwards to `NtUserWindowFromPoint` (this tree's logical/physical point mapping is the identity, as its `LogicalToPhysicalPoint`/`PhysicalToLogicalPoint` already implement). Guest proof: `web/tests/suite/browser/win32u-fd168-slice3.test.mjs`. |
| `tools/buildWine/patches/wine8-fd170-contracts.patch` | FD-170 whole-DLL backports. Wine 8.0 builds `bluetoothapis`, `winusb` and `netutils` as pure spec-stub modules (95/96, 21/22 and 22/22 named exports are stubs). The patch gives the three real code from the project's pinned newer sources: `bluetoothapis` gains Wine 10.0's own `main.c`/`sdp.c` (radio enumeration over setupapi, SDP stream parsing) plus the GATT/radio contract bodies `wine10-fd167-contracts.patch` proved on the i386 side, and the headers they need (`bluetoothapis.h` at its 10.0 state, new `bthdef.h`, upstream `bluetoothleapis.h`/`bthledef.h`); `winusb` gains the same FD-167 no-device contract bodies (FALSE + ERROR_INVALID_HANDLE, out-parameters validated first); `netutils` gains Wine 10.0's `main.c` (the `NetApiBuffer*` process-heap buffer functions). The base stays the identical Wine 8.0 source for everything else, so there is no version skew. Guest proof: `web/tests/suite/browser/fd170-contracts64.test.mjs`. |

Because the base is the identical Wine 8.0 source — not a newer Wine —
there is no version skew against the tree's unix side or
`ntdll` (a WineHQ-10.0 `kernelbase.dll` was evaluated and rejected: it
regular-imports `ntdll!RtlFreeActivationContextStack`, which Wine 8.0's
`ntdll` does not export, so it cannot even load on this tree).

## Rebuild

    tools/rootfs64/build-wine8-backports.sh

It clones the `wine-8.0` tag, applies the patches, cross-builds
the PE DLLs with the host's mingw toolchain (native build tools
tree + `--host=x86_64-w64-mingw32` tree; no emulated container — the
linux/amd64 container's QEMU layer is unreliable on this ARM64 host),
and copies the DLLs here, printing their sha256 for re-pinning. A
third tree (--host=x86_64-linux-gnu) builds the unix half of win32u
(`win32u.so`) from the same patched source and copies it here too.
That tree is configured WITH freetype and fontconfig (FD-65): the
font driver and the fontconfig enumerator live in the unix half, and
the earlier `--without-freetype --without-fontconfig` configuration
left the guest's GDI with no enumerable or realizable fonts at all
(EnumFontFamiliesEx returned zero families; GetTextMetrics failed),
which aborted Wine Gecko at startup ('unable to find a usable font
(serif)'). The build now verifies both SONAMEs are present in the
product and fails loudly otherwise. The rebuild needs the x86_64
freetype/fontconfig development files on the build host (system
packages or a sysroot named by FREETYPE_CFLAGS/FREETYPE_LIBS and
FONTCONFIG_CFLAGS/FONTCONFIG_LIBS).

## Packaging integrity

sha256 of these exact files, verified by
`tools/rootfs64/check_runtime_completeness.py` (source hash + shipped
`wine64.zip` member hash):

| file | bytes | sha256 |
| --- | --- | --- |
| user32.dll | 2002500 | b7b39a1b3daa48ffc273328359340f210bbca996ea2135ee130c7e93005da338 |
| kernelbase.dll | 1588997 | 5d94b2639e2e408da5fc4fd2fc27466b990ced859603ef3e123ff4ef85c3f251 |
| kernel32.dll | 859448 | ccf67fb5bba80af571952c5beefae8c4ca9f92ca4787da9a2424fc4a49fcd2e1 |
| iphlpapi.dll | 380852 | 1653f8600391d81727a9391cffbed3c5f9bad2a76564c5fd4f2029dc815b4f80 |
| win32u.dll | 359360 | 424ee50377eae732fedf593ea202a5994571223bba9bdb1ecc6902f658405b88 |
| win32u.so | 1493256 | 513d71262241583f56e8ffb40459ea356efb7e10b7dafa1257484cf6c6f7cef3 |
| msvcp140.dll | 1153111 | 8525ebf0220f595714acef764ba6e3fc2e542edbe158a43fad1ebf7cc2c8b940 |
| msvcp140_2.dll | 75079 | dd990f0b50454bba0cfce4e1746abd397f0660204c8caf5fe880e46d615f665c |
| msvcp140_atomic_wait.dll | 72402 | b712301e2e0a35f4ce256c3c541a8582e1dad5d3d4577f0028eb0a1259a9e7d0 |
| setupapi.dll | 750009 | a3d63eea9af804e41a540e291a9a1c2ff6d597d47f2f8b4f878b785d66b9cba9 |
| ucrtbase.dll | 1074308 | daed274b229dfc1c5d9ae5780dfab75d361796243c0432088b171dae1f63bbd7 |
| msvcrt.dll | 797741 | f64900841b15e7d577d2ec610578e0ee2f01ebf9de4c148aad5431ee03f030c0 |
| msvcr70.dll | 742716 | e7b1b1a68f16ade508a32aa2523f32ff60b2a196ff4697fd72c61c2e05ec68e5 |
| msvcr71.dll | 743015 | 534758d16a7a3d9aa8de1a94db3dc019871405a0a9e8cdbe0e3e850f9942474d |
| msvcr80.dll | 840888 | eae4c063710b284dde64d13a533e579d714e6cb6aefc53015aca76746dbcfe82 |
| msvcr90.dll | 835982 | 208802fc10e132186444bfe4992f0c96c7093291ec114cd412affbc258bb622b |
| msvcr100.dll | 955401 | 97243e6863c1bb7d8faeceae715aa5b550c7ab2f7586ad7c125de4533807b996 |
| msvcr110.dll | 984872 | d260db9d92628d9d8ed74b058c2b29a4b449eabbe4d6f3ddb2a341be559e8649 |
| msvcr120.dll | 1064870 | 33f46fac7eab905612bcf3c8d290f0842a4d28d359059f72c09acd422cfa623e |
| bluetoothapis.dll | 86807 | 8e53bf81f6d734d47a0eb4fdd0cecadf88e76afa92714842c2804407ecbf57c7 |
| winusb.dll | 64851 | 1135959a3ff6b46e31d539a4ef0a79a8882c622bc1217ad78f17ae2833bd742f |
| netutils.dll | 61069 | 696531741bb38ecf06ba0f708340aea8b6838a59f381654bb2f4401c0aa1fa92 |
| cfgmgr32.dll | 59006 | 4df21c0d34e66b103c8039dd5faa5e941ca6b8a91872f8d858bfa3ff58171514 |
| combase.dll | 601038 | f6051d5d254e888fd190692462fd2e897cc1619dd9e2c7ba8733a33093140cad |
| cryptext.dll | 74229 | ab6e0b932923e4db5a757f0546e11cfd3a237b80fc106126da30221dee51b8d7 |
| d2d1.dll | 762141 | 1afe8f059055abc37c92dcb7bbb2909bd143f4dcbf5d8d0d0a3d8bcaf4b5abc2 |
| hid.dll | 88067 | 16b08432e431c4dc9c0bc8bd637f6ded3b691556437334a18ab2a735b57939e8 |
| hidclass.sys | 108241 | ecb13aed3d1ee725e93d06cc91fe427d434a24c7a1d0caf8bb630de5d0f9fc21 |
| mf.dll | 756486 | f61db07e215a171fc53dfe111324acea9e4f3699f3d4458cd87227205dee208b |
| mfplat.dll | 583270 | 84a8d777a398ceec8cd63d5072a09fd7a634a2d4e081836999f48a6a2070d2e6 |
| mscms.dll | 438256 | e1616c36ea928b5058187e454ef676cc0243ca6d091767d9ad30515bacd89958 |
| ntdll.dll | 783245 | 904c4e6107acb560d7bd3f7690efb6baf8a2d8e2fea08150719e7989233dd0bb |
| pdh.dll | 108648 | df56ea38553a8ae6ef07d4db44f949c57f5fdb883bf2ae2c6c29a46d9ac51330 |
| prntvpt.dll | 120719 | afb5b277b57688931ac95170bea45ead1495bc9d941c424de53cc656910663a1 |
| propsys.dll | 376373 | 6f5c0e0b534cd29c127bc7a470285c9bb6aff35a9c39ca6f8b84952512a0a32c |
| sspicli.dll | 73233 | db2d5a16a94abf7c3fc8d6726415598ccbba91d2838f9f45e9cf0e67504846e8 |
| vcomp.dll | 99215 | 29e766d4382d2374526f4516d9b67c87a1d35edfc5a808b0a3bd92693b4db97c |
| vcomp100.dll | 99215 | a264b5c4b94bc4919b54189469b6eb23d979f38491362c8a5562c5668b070f7d |
| vcomp110.dll | 99217 | d2e0461eabc57f27162e6d31fa380b2c7e7faa81873914803c429c400ac2eef4 |
| vcomp120.dll | 99217 | 31c21ec9642e09edacd982f934403fafdb64f3f27f8c2ed5f32f27061284e313 |
| vcomp140.dll | 99217 | fedbfc38b1ac96500cad4767c2a4ff9d4189a74640b9a56b1d6564e73264bcd0 |
| vcomp90.dll | 45069 | 9a6dcc07ee03f51ba16bc5a34993fc15f1666f909505d5f6284427d3f88ab53b |
| windows.networking.dll | 70042 | 26e57eae67a5005f9716bfc4fcd91aa13172ddef9197b7703a13e69dd632dda9 |
| wintypes.dll | 89020 | 50b105d1b7725f4fb6147f923539b54fd16af621c007ecefc5f324dee48693ba |
| wofutil.dll | 60117 | 924ff1fd2c1c688a7433290b211e05e0fb2c7426f097e40937a7ee4fdfe9e1f1 |

Staged into `wine64.zip` by `tools/rootfs64/build-wine64-zip.sh`
(container path) and `tools/rootfs64/stage-wine8-backports.sh`
(merge-script path, run from `apply-runtime-poststeps.sh`).

## FD-169: the Microsoft C runtime family

The same backports build also rebuilds the whole Microsoft C runtime
family (`ucrtbase.dll`, `msvcrt.dll`, `msvcr70.dll` … `msvcr120.dll`)
from the same patched Wine 8.0 tree, with
`tools/buildWine/patches/wine8-msvcrt-fd169-crt.patch`. In the Debian
module set, 916 of the family's exports were winebuild stubs in these
nine DLLs — functions that load fine and then kill the caller with
the Wine stub exception when a program actually calls them. The patch
implements them: a backport set taken from upstream Wine (post-8.0)
(the `_mbs*_l` family, `_aligned_recalloc`, console `_cprintf_l`
family, `cimag`/`crealf`, `imaxdiv`, `feholdexcept`), plus
hand-written implementations of the `_ismbb*` classification family,
the MSVC complex helpers and the C99 complex set, the positional and
secure printf entry points (the 8.0 formatting engine already carries
both modes), the public va_list scanf forms, the 32-bit-time/
64-bit-size `_find*32i64` forms, the C11 `mbrtoc16`/`c16rtomb` family,
and the remaining secure-form string functions. See
`docs/reports/fd169-crt-family-20261105.md` for the per-family
accounting, the guest-level proof, and what remains stubbed with its
reasons. The nine DLLs' hashes are pinned in the table above alongside
the core five.
