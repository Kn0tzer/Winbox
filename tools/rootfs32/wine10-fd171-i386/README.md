# wine10-fd171-i386 — FD-171 contract backports (i386 PE)

21 PE DLLs rebuilt from the pinned Wine 10.0 source tarball
(sha256 c5e0b3f5f7efafb30e9cd4d9c624b85c583171d33549d933cd3402f341ac3601)
plus `tools/buildWine/patches/wine10-fd171-contracts.patch`, by
`tools/rootfs32/build-wine10-fd171.sh`. They replace the WineHQ 10.0
deb's builds, whose exports include 96 of the FD-171 long-tail
winebuild stubs (register FD-171): the config-manager family in
setupapi/cfgmgr32, ntdll's splay-tree generic tables and fast
activation-context calls, pdh's formatted counter arrays, combase's
HSTRING marshaling and fail-fast entry points, kernelbase/kernel32's
HeapSummary / ReadConsoleInputEx / RegisterWaitForInputIdle, and the
single-function DLLs (cryptext, d2d1, hid, mf, mscms, prntvpt,
sspicli, the vcomp family, propsys's PropVariantGetStringElem).

Substituted into the wine32 payload by
`tools/rootfs32/build-wine32-base.sh` (FD-171 block, hash-pinned,
fail-closed) and verified against `wine32.zip` by
`tools/rootfs64/check_runtime_completeness.py`
(`WINE10_FD171_PINS32`). Note propsys.dll and mf.dll are also in the
FD-167 set; neither lane's build is substituted for them — the
combined FD-167+FD-171 build
(`tools/rootfs32/wine10-fd167-fd171-i386/`, both patches applied)
carries both lanes' functions (FD-171's PropVariantGetStringElem and
MFTranscodeGetAudioOutputAvailableTypes plus FD-167's VariantCompare
fix and MFCreateDeviceSource) and is substituted last by the
combined step in `build-wine32-base.sh`. The single-lane propsys/mf
builds in this directory are superseded and no longer pinned.

| file | bytes | sha256 |
| --- | --- | --- |
| cfgmgr32.dll | 69490 | 9aad4b1c185244d184a2087fe9e6f367e89fa201652d7b55437b4fd537301c60 |
| combase.dll | 611120 | cca9be3845f46d616c5c2fd0d31d9153fc9ccf40e09b651f6f003dbe5b97696e |
| cryptext.dll | 64776 | 7783edb4d30d4e71f98687222c4d1874c5830a28a0134d02b14f2e215ce3c119 |
| d2d1.dll | 817432 | b549db04f4811d67407993588f72134a32aaec7e555f9bd1d6285d81ae04cde3 |
| hid.dll | 79121 | ca37389cc7f4646f488163128d6110a80688496b8c0399e70838d1a7f308f69b |
| kernel32.dll | 830764 | 35628e12975ab305abd6785c371acdf1a1f302b26f7dc454053d5b56d7fa0607 |
| kernelbase.dll | 1603777 | d50f6a31710f8241483ceca281796b53d27db7c3fc02a485c835b94d25b70c53 |
| mscms.dll | 437213 | 99c8c3006511653e276d9a00016f7ce1a58f8214a8c2f0e91fbf54744ddbefcc |
| ntdll.dll | 819619 | 2da0bf73e03d6ecbbece6e2b92985bbfda75e73e1ed465d6c452d3a0e2b7fc90 |
| pdh.dll | 99424 | d386d02950cb2c9f0c3d5cb85b74858d4d4c47b7bf9b17bbbcdff5d232501bc0 |
| prntvpt.dll | 106893 | 636d13280be1256bd1b8f3c6c84aa465ca6fafbbd65d0959e1b980db3d5ff6c2 |
| setupapi.dll | 766368 | 50615e6394c3169848f0f3b076a5cf1d9c2ecf767f149ff395c5b12996606ed9 |
| sspicli.dll | 64250 | 1d7daa3e8e377e4950baeeb402100ffcae0cd256ab1e213c737f10a445890a9e |
| vcomp.dll | 98669 | c42871b35a25fa777cdda468e58caea7fa911580fd818ab88c74612ec9f7892f |
| vcomp100.dll | 98669 | 25ffbfd9726c5db96bff35a4e0e0640ec9ba126f736e390029b7370d88874d7f |
| vcomp110.dll | 98671 | bed64f70d41d408aab3885a02fd3cfbe0013aa75bcb242b5a7ea40a72d62a995 |
| vcomp120.dll | 98671 | 0566b5fb2121c984f6629e8f9810b78974e15bded8787f653fd27bb070e4206d |
| vcomp140.dll | 98671 | d2ccda0b5c74d7b7d3f518af3cb0be28525e9255d1cd9c96917ee2ebc19892c0 |
| vcomp90.dll | 40936 | bb48278533d3beea3f4962913309dc35c7ae6aac0904aa1dcb119feb63ec8d87 |
