# BUG: Bridge `vkCmdResolveImage` throws C++ exception (guest crash)

**Found by:** bridge API-coverage harness (vkcov), 2026-10-09.
**Severity:** High — any `vkCmdResolveImage` call crashes the guest;
`vkCmdResolveImageKHR` shares the case and is presumed affected.
**Status:** GUARDED by d1713623 (2026-10-09) — `if (nreg > 1024) return 0;` prevents crash on garbage input. Original status below.

**Original status:** In uncommitted bridge code (`source/vulkan/vk64bridge.cpp`, "Game-breaking stub
implementations (immediate CPU emulation)" section). Filed for the owning lane.

## Repro (in-browser verified)

Guest `tools/vkcov/vkcov.c`, `t_cmd_all` (valid 64x64 image, 1 resolve region).

Probe log (`test-results/bridge-coverage-vkcov-matrix6/console.log`):
```
log: VKCOV-TRY: vkCmdResolveImage
log: vk64: trap vkCmdResolveImage
error: worker sent an error! ... Uncaught [object WebAssembly.Exception]
```

## Suspect

Same family as BUG-vkCmdCopyBuffer2, BUG-vkCmdCopyImage, BUG-vkCmdCopyImageToBuffer2,
BUG-vkCmdBlitImage — likely one common root cause in the immediate-emulation paths.
6/6 crashers in the image/buffer copy-blit-resolve family.

## Impact

- vkcov coverage run stalls at 110/269 handlers; `vkCmdResolveImage` and
  `vkCmdResolveImageKHR` are skipped in the harness until fixed (recorded as
  crashed, TRY line = repro).

## Suggested next step for fix lane

See BUG-vkCmdCopyBuffer2.md — likely the same root cause. Verify with vkcov probe
(remove the skip).
