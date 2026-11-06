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
| cfgmgr32.dll | 69490 | 34ceba5a529294a15710977e496170c9b62c90b51203eafea5f6c7afb9ed5e12 |
| combase.dll | 611120 | 3b7a291a63eddc5d8a73a43b87a8485e11cecf4ac6627e6d21764857d9e34b61 |
| cryptext.dll | 64776 | 53e417c2c68de322d5a7793dea2275709013e05e6dba61e7b1008db460efe1a1 |
| d2d1.dll | 817432 | 642621d661a1b22bba36b579460e2bd943c7252acae2275ef9a20f6af7a8a9af |
| hid.dll | 79121 | 34b191001a4c8835a7cd1388d9d141e814ff7d4a087538c150fa0dc80b0aaa6a |
| kernel32.dll | 830764 | 752d40eed73ec976eb6d99dbb7cd5896952ba640ee9d32797f018c30c59cda7a |
| kernelbase.dll | 1603777 | 4959557be956b09d4b79d379c1354ad7edb13673c5bffdd4bde2f5d34de0f31c |
| mscms.dll | 437213 | 331b045cfbc8cf93acd9f820f4060f73d83c6a8132db07fab88a9c3b9521bd23 |
| ntdll.dll | 819619 | 5f2f866d8d0bf3529965f49f0362ed46ce9366677423417016b24391da28581e |
| pdh.dll | 99424 | 5d79d70f7a2942d159090ea0ecb8407da5370d144adcc19f025af2b62876a68c |
| prntvpt.dll | 106893 | 36e998791374b6de07747d91ffe93ea3167e0c407194aca9c0475c9c3ba19133 |
| setupapi.dll | 766368 | 5fcef9aa9344772bdb1ee7c71517f3abfc456850aa4c9e64965114e02971d29f |
| sspicli.dll | 64250 | 0c4b8cb4234d468d8b1cb742b6ef4fc7a9e9b61634709aadc8561431d8e8a719 |
| vcomp.dll | 98669 | 739020d398a48f14b8c290e036e2c0da9201a623756453c9feeccaf1f937d9b4 |
| vcomp100.dll | 98669 | 5e62b5281b786cad54ea07a671d2b2630f1e4ed61e03f51f1b128882386f5036 |
| vcomp110.dll | 98671 | 5ccad41c55939f6a084de1aaabe3633645cac511a379c0be634f34124e73b81c |
| vcomp120.dll | 98671 | 4d1a0e4ce170a21ae959fcce87a0775c8ae5fb48f1207f7f0d896b7f686a1a60 |
| vcomp140.dll | 98671 | cd95ffcd6da529438863bc85fc90c7b10da28cf3e37ca0e30729baab1db23d63 |
| vcomp90.dll | 40936 | 774c9483c79d91fea4e4d3bbbd7f40db22804714ac1d2a15d471f3631a08a075 |
