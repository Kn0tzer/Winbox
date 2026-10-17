# BUG: Bridge `vkCmdCopyImage` throws C++ exception (guest crash)

**Found by:** bridge API-coverage harness (vkcov), 2026-10-09.
**Severity:** High — any `vkCmdCopyImage` call crashes the guest; `vkCmdCopyImage2`/`vkCmdCopyImage2KHR` share the code path and are presumed affected.
**Status:** GUARDED by d1713623 (2026-10-09) — `if (nreg > 1024) return 0;` prevents crash on garbage input. Original status below.

**Original status:** In uncommitted bridge code (`source/vulkan/vk64bridge.cpp`, "Game-breaking stub
implementations (immediate CPU emulation)" section). Filed for the owning lane.

## Repro (in-browser verified)

Guest `tools/vkcov/vkcov.c`, `t_cmd_all`:
```c
VkImageSubresourceLayers sl = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
VkOffset3D off = { 0, 0, 0 }; VkExtent3D ex = { 64, 64, 1 };
VkImageCopy ic = { sl, off, sl, off, ex };
pvkCmdCopyImage(cmdbuf, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
  img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &ic);  // img = valid 64x64 image, bound
```

Probe log (`test-results/bridge-coverage-vkcov-matrix1/console.log:892`):
```
log: VKCOV-TRY: vkCmdCopyImage
log: vk64: trap vkCmdCopyImage
error: worker sent an error! ... Uncaught [object WebAssembly.Exception]
```

`WebAssembly.Exception` (not `RuntimeError`) = a C++ exception escaping the wasm
bridge to JS. The call args are valid (64x64 image, 1 region). Notably
`vkCmdCopyBuffer` and `vkCmdCopyBufferToImage` (same immediate-emulation family)
do NOT crash — the trigger is specific to the CopyImage path.

## Suspect

The new immediate-emulation body (`case VK64_fn_vkCmdCopyImage`, ~line 4590):
likely a `std::vector` length error / bad alloc from a size underflow in the
bounds-clamp logic (`n = smem->va + smem->size - sOff` underflows if
`sOff > smem->va + smem->size`), or an exception from `readArray` on the region
data. Compare against the working `vkCmdCopyBuffer` path to isolate the
difference.

## Impact

- vkcov coverage run stalls at 101/269 handlers; `vkCmdCopyImage`,
  `vkCmdCopyImage2`, `vkCmdCopyImage2KHR` are skipped in the harness until fixed
  (recorded as crashed, TRY line = repro).
- Any real app calling `vkCmdCopyImage` (vkd3d does) will crash the guest
  on the current tree.

## Suggested next step for fix lane

Repro in isolation, add a klog of `nreg`/`src->w`/`src->h`/`smem->size` at the top
of the case, and guard the vector sizing. Verify with the vkcov probe (remove the skip).
