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

The four DLLs in this directory are **the same Wine 8.0 code the rest of
the tree ships**, rebuilt from the pinned `wine-8.0` source tag with
three small backport patches that add exactly those exports, mirroring
how upstream Wine later implemented them:

| patch | change |
| --- | --- |
| `tools/buildWine/patches/wine8-user32-pointer-family.patch` | spec + stub-family implementations for `GetPointerPenInfo`, `GetPointerPenInfoHistory`, `GetPointerInfoHistory`, `GetPointerFrameInfo`, `GetPointerFrameInfoHistory`, `GetPointerFrameTouchInfo`, `GetPointerFrameTouchInfoHistory`, `GetPointerFramePenInfo`, `GetPointerFramePenInfoHistory` — the same FIXME-stub pattern Wine 8.0 itself uses for the five family members it does ship (`GetPointerInfo` etc. all return FALSE in 8.0; on a machine with no pointer devices FALSE is also the behaviourally correct answer). |
| `tools/buildWine/patches/wine8-kernelbase-copyfile2.patch` | `CopyFile2` implemented in `kernelbase` on top of the existing copy engine (upstream's later refactor of `CopyFileExW` into a shared `copy_file` helper, backported verbatim in structure), plus the `kernel32` `-import` forward. This is a real implementation — `CopyFile2` copies files. |
| `tools/buildWine/patches/wine8-iphlpapi-anycast-table.patch` | `GetAnycastIpAddressTable` enabled and implemented exactly as upstream does (validate arguments, return an allocated empty table with `NO_ERROR`), plus the `MIB_ANYCASTIPADDRESS_*` types upstream added to `netioapi.h`. |

Because the base is the identical Wine 8.0 source — not a newer Wine —
there is no version skew against the tree's unix side, `win32u`, or
`ntdll` (a WineHQ-10.0 `kernelbase.dll` was evaluated and rejected: it
regular-imports `ntdll!RtlFreeActivationContextStack`, which Wine 8.0's
`ntdll` does not export, so it cannot even load on this tree).

## Rebuild

    tools/rootfs64/build-wine8-backports.sh

It clones the `wine-8.0` tag, applies the three patches, cross-builds
the four PE DLLs with the host's mingw toolchain (native build tools
tree + `--host=x86_64-w64-mingw32` tree; no emulated container — the
linux/amd64 container's QEMU layer is unreliable on this ARM64 host),
and copies the DLLs here, printing their sha256 for re-pinning.

## Packaging integrity

sha256 of these exact files, verified by
`tools/rootfs64/check_runtime_completeness.py` (source hash + shipped
`wine64.zip` member hash):

| file | bytes | sha256 |
| --- | --- | --- |
| user32.dll | 2002112 | 5da10eb9c69549ceb7cc415eeda3819f5eb183f2817da755082905b2c2770219 |
| kernelbase.dll | 1588877 | 344b7faa452f06f7d08e4f20a54ac75a6dcbfdd68e271650ad550d17430d2a85 |
| kernel32.dll | 859511 | e2145d2a6fbd53aa99f0c77817f30c93ab7810738cc0b348617a0de7c6d16793 |
| iphlpapi.dll | 380852 | 2b0ed793c7440a864e1e631f45ec7070eabda5661af0c2c2dbeb982926ad7b86 |

Staged into `wine64.zip` by `tools/rootfs64/build-wine64-zip.sh`
(container path) and `tools/rootfs64/stage-wine8-backports.sh`
(merge-script path, run from `apply-runtime-poststeps.sh`).
