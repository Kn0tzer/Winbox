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
FD-167 set; at composition the later substitution wins and both
lanes' pins must be reconciled — FD-171's propsys carries only
PropVariantGetStringElem beyond stock 10.0, FD-167's carries its
VariantCompare fix; the composed tree needs a propsys build with
both (the two patches touch disjoint functions).

| file | bytes | sha256 |
| --- | --- | --- |
| cfgmgr32.dll | 69490 | 4d4ca217b8b4173e6e821b7860f07c3eb388930fb58eb7a84d847fb6b4fd5957 |
| combase.dll | 611120 | da03dcd0f19e0759844f6d2e38d65a5e8f85ea15da43417202dba7f0e2db99dc |
| cryptext.dll | 64776 | 1ed21bab40c9aa95e5fc8998d14d5e164445b46a5d0d52d4bc2b8ef148826253 |
| d2d1.dll | 817432 | e5c89536f398c95f47a87eadbfd896c8afe631614ac62555eae5015e49c3071d |
| hid.dll | 79121 | 616c678dbd7a349e50839e231ea3b1a578994cdcd4513fad05bbbbb4b547d816 |
| kernel32.dll | 830764 | 46c41b95b74dde715a59b10cceff6d44fc1db1acab4c61c64265fa7e05645995 |
| kernelbase.dll | 1603777 | 1847ac4136515dc43b178d9849b4ddab44e3b8469d627787e9d3372a5f545676 |
| mf.dll | 789094 | 352432760c5ca11ba162eb01e4591252c9e4b4e677ea1d270a6df270ab1a58dd |
| mscms.dll | 437213 | 1c313f89f88bc3b94546115f85a81bf25f4c3f18886d5c2bc9881ad1a3e71328 |
| ntdll.dll | 819619 | 1f326d17feba65f46e1119f243b7dc8d83adf6be575c74f14945d57bc89f27cc |
| pdh.dll | 99424 | 693de5a82e04d87e9458136df7b553c6165dea188aed364be69f6c0154b28daa |
| prntvpt.dll | 106893 | 3a5285c8d0a6fc48762e463ddd80530b54d6fd939d752f98e96e86abec0f4bec |
| propsys.dll | 388325 | 7e776f6d3de7a35ec5a99d42386929ffd1cc13bf6eac530271013f18529fba29 |
| setupapi.dll | 766368 | 3b878c2dbdde59020851f67a279ce9d7faf1b0a2dca6c3eac84e6cf8f09fddf6 |
| sspicli.dll | 64250 | 6540168dfc8e4c56f9f74f89d357e916cac98c368c7fb43f7e974d0d6e19745a |
| vcomp.dll | 98669 | 1481435300757f8b202b1c7ec85394fcf9e80984b3c041dfa149c7e7edc48aac |
| vcomp100.dll | 98669 | 4e534f2cc7dfe02f8b076cee55fa7ddd5dbc58ecfb76b9ae08d48ba9ffb90e48 |
| vcomp110.dll | 98671 | b862859ed8ffa3a7bda913b1fb289ecc4eb21af4087d4df1216dc3470e847ade |
| vcomp120.dll | 98671 | c5eec207a56fceb4fa4efdca97090d210c6df305c1f5729f1b70cfa99ccaa7e6 |
| vcomp140.dll | 98671 | a66fc0452544a4ea11cca519ed9c091a077f5fd22c4bada303058930c331fa98 |
| vcomp90.dll | 40936 | 1574571555952b734472aa338f35e9259a56c12c31dc77932d851b06b7ce3ccf |
