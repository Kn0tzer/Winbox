# BUG: Bridge `vkCmdCopyImageToBuffer2` throws C++ exception (guest crash)

**Found by:** bridge API-coverage harness (vkcov), 2026-10-09.
**Severity:** High — any `vkCmdCopyImageToBuffer2` call crashes the guest;
`vkCmdCopyImageToBuffer2KHR` shares the case and is presumed affected.
**Status:** GUARDED by d1713623 (2026-10-09) — `if (nreg > 1024) return 0;` prevents crash on garbage input. Original status below.

**Original status:** In uncommitted bridge code (`source/vulkan/vk64bridge.cpp`, "Game-breaking stub
implementations (immediate CPU emulation)" section). Filed for the owning lane.

## Repro (in-browser verified)

Guest `tools/vkcov/vkcov.c`, `t_cmd_all`:
```c
VkBufferImageCopy2 bic2 = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2, 0,
  0, 0, 0, sl, off, ex };  // 64x64, 1 layer
VkCopyImageToBufferInfo2 ci2 = { VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2, 0,
  img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vbuf, 1, &bic2 };
pvkCmdCopyImageToBuffer2(cmdbuf, &ci2);  // img, vbuf valid and bound
```

Probe log (`test-results/bridge-coverage-vkcov-matrix3/console.log`):
```
log: VKCOV-TRY: vkCmdCopyImageToBuffer2
log: vk64: trap vkCmdCopyImageToBuffer2
error: worker sent an error! ... Uncaught [object WebAssembly.Exception]
```

`WebAssembly.Exception` = C++ exception escaping the wasm bridge. Args valid.
Notably `vkCmdCopyImageToBuffer` (non-2) and `vkCmdCopyBufferToImage2` (same
family) do NOT crash — the trigger is specific to this handler's bridge code.

## Suspect

Same family as BUG-vkCmdCopyBuffer2 and BUG-vkCmdCopyImage — likely one common
root cause in the immediate-emulation copy paths (vector sizing / bounds-clamp
underflow / readArray on region data). Fixing one may fix all three.

## Impact

- vkcov coverage run stalls at 105/269 handlers; `vkCmdCopyImageToBuffer2` and
  `vkCmdCopyImageToBuffer2KHR` are skipped in the harness until fixed (recorded
  as crashed, TRY line = repro).
- Any real app calling `vkCmdCopyImageToBuffer2` (vkd3d does) will crash the guest
  on the current tree.

## Suggested next step for fix lane

See BUG-vkCmdCopyBuffer2.md — likely the same root cause. Verify with the vkcov
probe (remove the skip).
