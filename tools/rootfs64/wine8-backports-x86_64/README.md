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
| user32.dll | 2002112 | 5da10eb9c69549ceb7cc415eeda3819f5eb183f2817da755082905b2c2770219 |
| kernelbase.dll | 1588997 | 88a997d9525eb187735e7bbad3db33871fda0d2e3fd1701150f77732c1115ed7 |
| kernel32.dll | 859448 | fec1c2dd4993730e407e005ef8aca175fbb406e38cf4b6ccb59ccc4f3cc9f253 |
| iphlpapi.dll | 380852 | 2b0ed793c7440a864e1e631f45ec7070eabda5661af0c2c2dbeb982926ad7b86 |
| win32u.dll | 354218 | 49b874381063acf28652bee5a4c281b21582830b50190d9c2c9875fd5deff708 |
| win32u.so | 1479712 | d382cefb32fbe71d7e52349da06c478a22c03bde2b38d8e6bfb4eaff42ef2608 |
| msvcp140.dll | 1152763 | c8255d140b6bdbba5d7e0d61b6eea3de05f00dba7e1b8344bce08da54142d58a |
| setupapi.dll | 745842 | 7ffe5a9f0bbfe1b428e2e96c40f45fa0baf19cd4e69487f99bf7ea38fd42957e |
| ucrtbase.dll | 1074308 | 6f790d4721f8562c021bfddbb77cdf5da686e64507d02741888fdcc156c057d9 |
| msvcrt.dll | 797741 | 23bad1e9016e8f3f95ba5784a6a1ad383f4d9cc65a21e6aafddc4fbda1a78f84 |
| msvcr70.dll | 742716 | 33a64d83cefcaba027c2a4d00e1ce1d5fdc81de3fcc7eabb719dec5b817cb3b2 |
| msvcr71.dll | 743015 | 6e37ada00292280e3e4dc21d6d4432fa331a057ecbf31e00399d19e33f425a38 |
| msvcr80.dll | 840888 | 8cb9576ab57bc10b8010268572a1410cf62f15b7faee5202ed58e2ae6a45d0f8 |
| msvcr90.dll | 835982 | b68c1d96070e6dae7a366616cb717c8278886f7dd902b0e1ce5e3687537e1728 |
| msvcr100.dll | 955401 | 49dc3a015b4be2d16f7629d106845fd84d4966be23a13e9d82c0caf90bf6997c |
| msvcr110.dll | 984872 | c82bf206e4e93ba8c12113bcad3a67ca8988347f78a66def81921270e7872a69 |
| msvcr120.dll | 1064870 | 89054bd4059865c9b2c404f817f3ad9fa7913676fc9c4e98185fd17f2a53dbfb |
| bluetoothapis.dll | 86807 | 306a2b6fea134bc0e553272c04200301925d3f78b9428820d10e7f7342ea8f78 |
| winusb.dll | 64851 | cb65e30f375e3d05544c6c6c0e2a4dea44c364bf68c77bf5bd93a18fa33cd987 |
| netutils.dll | 61069 | 47652b97794e174c3a0574b04ca621b4665516abc67db18b1c0023ec112beb83 |
| cfgmgr32.dll | 59006 | de32069c5562bb4d7fb57c8de4c0ab6e0fd940d1e754bc86495b142ff2340bee |
| combase.dll | 601038 | 828a6d296a1c46583324911937462bf21b775d49888c46414ded7b3182457768 |
| cryptext.dll | 74229 | cb897bfb1e7122d1dc0a574b097a4d08e0226844e502a7e90e1587d6869bf0f7 |
| d2d1.dll | 762141 | b87c20b0908b61aaad2c21cc883e901e35faf890b99ce80e44e7a9d9a7a784c5 |
| hid.dll | 88067 | a5795479085ec099901273f0583a4b8d622f915db567b540964e35fe846c716f |
| mf.dll | 756486 | 15bb37e3d6db3218df846682d3f79c451bb2cc79f69d27a100377958630746de |
| mfplat.dll | 583270 | bf7a2cf2c6724da2213d2f3ceaa0b4bc792c7832da7d19ef2adae5ce3de06987 |
| mscms.dll | 438256 | 296b37a014cda1728220b840bb3ac72c56366d7d7b24240cd1a9ebaf0cebdbc8 |
| ntdll.dll | 783245 | 135d3c3b2117936ccc6773670fb137bc8fd2d350d1f37e84e450f22ada16e279 |
| pdh.dll | 108648 | 0f16f4093bedc3894664900d6ee12d10b9fc5fba3b44f43eb5ba47bb8c57421f |
| prntvpt.dll | 120719 | 0e96ce6ad036ed3e17177787955c78a43367f7944569d0b29d7e997bca0689f3 |
| propsys.dll | 376373 | 1f8a8649e1837f4234f7f37d41d0b1f7e1488329579ab0eaa57c990419e08425 |
| sspicli.dll | 73233 | dce75850ff63c0e8c11aae05739257cc0560c1fb3b45b4fc52ee0153ed3b0f3f |
| vcomp.dll | 99215 | b164aec2af0a0936c43ba653f35d91507227dfad8336730c6062f2b9774a67e9 |
| vcomp100.dll | 99215 | 1a3a4f216bb413a68fa4868221a6e1d14a959a289ec8b5a509ca21865249e20d |
| vcomp110.dll | 99217 | b44f61c85c5e2f309be6f89124bbb1def44695d68229978bd159efaa5ec580a9 |
| vcomp120.dll | 99217 | 03bd7092fce7df74e9d87a6c4582b0ca417cbd2d40e83d5d9e33f35cffaa18de |
| vcomp140.dll | 99217 | 88e7a6a4c1195e63ac924efbf94ba30a4827b3159628045a53b54cfb42513bb7 |
| vcomp90.dll | 45069 | 3c0f54d483861ce9a847259542be75c3fa2dd23d6c8286765ac210310a48b191 |
| windows.networking.dll | 70042 | bcf5885a09f89e2a0f9c96d7ef54f72fab8ae8d80ac654d6f90c4bae363fe7b0 |
| wintypes.dll | 89020 | 04fd8a385c971b0bdb279a57a23c935bd52de6cb3dc5bead0dbf63343fe8083f |
| wofutil.dll | 60117 | eec4b55338da63f0517468906644952eab2f31525f95859fa3f18002655efeeb |

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
