# wine10-fd167-fd171-i386 — FD-167 + FD-171 combined rebuilds (i386)

Two Wine 10.0 PE DLLs rebuilt from the pinned Wine 10.0 source tarball
(sha256 c5e0b3f5f7efafb30e9cd4d9c624b85c583171d33549d933cd3402f341ac3601)
with BOTH contract patches applied in order,
`tools/buildWine/patches/wine10-fd167-contracts.patch` then
`tools/buildWine/patches/wine10-fd171-contracts.patch`, by
`tools/rootfs32/build-wine10-fd167-fd171.sh`.

`propsys.dll` and `mf.dll` are the only overlap between the two lanes'
DLL sets, and the two patches touch disjoint functions in each:

- `propsys.dll` — FD-167 `VariantCompare` (on `PropVariantCompareEx`)
  plus FD-171 `PropVariantGetStringElem`.
- `mf.dll` — FD-167 `MFCreateDeviceSource` plus FD-171
  `MFTranscodeGetAudioOutputAvailableTypes`.

A single-lane build of either DLL loses the other lane's function, and
`build-wine32-base.sh` used to substitute FD-171 last, so at
composition the shipped wine32.zip would silently have carried
FD-171's propsys/mf and dropped FD-167's VariantCompare /
MFCreateDeviceSource. This combined build is the composed tree's
answer: one build per library carrying both patches. The two patches
apply cleanly in sequence (verified 2026-11-06; the touched hunks are
disjoint), and the build script's export check requires all four
functions to be present as real exports.

Substituted into the wine32 payload by
`tools/rootfs32/build-wine32-base.sh` (FD-167+FD-171 step, hash-pinned,
fail-closed, applied after the FD-167 and FD-171 steps) and verified
against `wine32.zip` by `tools/rootfs64/check_runtime_completeness.py`
(`WINE10_FD167_FD171_PINS32`).

| file | bytes | sha256 |
| --- | --- | --- |
| mf.dll | 789153 | 98800d6b755221094fc67d4e3ce023f41546873df893b1f7e83609ed67457a59 |
| propsys.dll | 388367 | f3116589e19adadf5cbe4182d40e37899f70e8738a5562a56921517b692b6c7f |
