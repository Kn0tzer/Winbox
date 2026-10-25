
# WineHQ wine-stable 10.0 amd64 DLLs backported into the Debian wine64 tree

`web/runtime/wine64.zip` is staged from Debian bookworm's `wine64 8.0`
(`boxedwine64/wine64-debian:bookworm`). Three DLLs that 32-bit guests already
have (the 32-bit tree uses WineHQ wine-stable 10.0) do not exist in Debian's
wine 8.0 `x86_64-windows` module set, but real games need them:

* `d3d12core.dll` — Wine only added this module after Debian's 8.0 package
  was cut. Games and engines that `LoadLibrary("d3d12core.dll")` fail to find
  it on the 64-bit prefix.
* `msmpeg2vdec.dll` — Media Foundation's H.264 / MPEG-2 video decoder. It is
  the decoder DirectShow / Media Foundation resolves for virtually every
  modern WMV / H.264 in-game cutscene. Without it, cutscene playback fails
  on the 64-bit prefix.
* `msvcp140_codecvt_ids.dll` — the MSVC 2015-2022 codecvt facet DLL. The
  i386 WineHQ-10.0 tree ships it, Debian's wine 8.0 amd64 tree does not
  (found by the 2026-10-25 runtime-register evidence audit: the register's
  VC++ 140 brace list claimed it for x86_64, the shipped zip did not have
  it). It imports only `kernel32.dll`, so it is a drop-in like d3d12core.

Both are pure builtin-Wine PE DLLs taken from the official WineHQ Debian
bookworm package (wine-stable 10.0.0.0~bookworm-1). They are drop-in with the
wine 8.0 tree because their imports resolve against it:

* `d3d12core.dll` imports only `kernel32.dll`.
* `msmpeg2vdec.dll` imports `MFTRegister`/`MFTUnregister` from `mfplat.dll`
  (both are present in Debian wine 8.0's `mfplat.dll`) plus `kernel32.dll`,
  `combase.dll`, `ntdll.dll`, and `ucrtbase.dll`.

Refresh source:

    curl -fLO https://dl.winehq.org/wine-builds/debian/pool/main/w/wine/wine-stable-amd64_10.0.0.0~bookworm-1_amd64.deb
    sha256sum wine-stable-amd64_10.0.0.0~bookworm-1_amd64.deb
    # 57ea31156f508dfba175eb2db3467346aaa9b0ca948785959ac67778cfdac42b
    # (deb-level hash as downloaded 2026-10-25; an earlier revision of this
    # README recorded a different deb hash, but the DLL-level pins below are
    # the enforced provenance and match this deb exactly.)
    dpkg-deb -x wine-stable-amd64_10.0.0.0~bookworm-1_amd64.deb out
    cp out/opt/wine-stable/lib64/wine/x86_64-windows/{d3d12core,msmpeg2vdec,msvcp140_codecvt_ids}.dll .

Packaging integrity (sha256 of these exact files, verified by
`web/tests/suite/unit/runtime-completeness.test.mjs` and by
`tools/rootfs64/prepare_prefixes.py`):

| file | bytes | sha256 |
| --- | --- | --- |
| d3d12core.dll | 70654 | a00b2ea3454d2569dcebf52bb99d79f18c8153071990601953773e0d9537cd1e |
| msmpeg2vdec.dll | 625356 | bbf91fc84f2bc66c70802e41e5ff1ee2e18335ef9cf07769957b0fb37484d046 |
| msvcp140_codecvt_ids.dll | 66024 | 2215e3312bcc817c628669ec69baff3f050ec0bef91c099e0d12fd66f2c3d9b1 |
