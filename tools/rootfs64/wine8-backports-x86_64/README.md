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
| user32.dll | 2002500 | 226bfff8294147694b9f587e99272f31ba8b7f23f4fe293622e490c87bd396e7 |
| kernelbase.dll | 1588877 | 36481e46ca66c266b0b7a047de4344ba7be51b1f83e04dd8247c07869dd3bb09 |
| kernel32.dll | 859511 | ba1a9d9dc67ab402c2dc73a93465768d49a3da2e0af0dc9002465138f2d7c115 |
| iphlpapi.dll | 380852 | af9f98b669441af888c633a1d517257a02ea3b8689e4670d0c21eb8864c48515 |
| win32u.dll | 354741 | 41da01c8027bc45bc2b72b105a90847d8d700c7019d6c8902f8fffc37c58cd69 |
| win32u.so | 1492928 | c160be3a76e5837ace446dd50372cb1a78994760e14555328eae5a5efb1f5772 |
| msvcp140.dll | 1152763 | c8255d140b6bdbba5d7e0d61b6eea3de05f00dba7e1b8344bce08da54142d58a |
| setupapi.dll | 731113 | c7e1555da0fedb6153044bb823185a995f268903bfe55c4cf26193c80ede6d21 |

Staged into `wine64.zip` by `tools/rootfs64/build-wine64-zip.sh`
(container path) and `tools/rootfs64/stage-wine8-backports.sh`
(merge-script path, run from `apply-runtime-poststeps.sh`).
