# BUG: Bridge `vkCmdCopyBuffer2` throws C++ exception (guest crash)

**Found by:** bridge API-coverage harness (vkcov), 2026-10-09.
**Severity:** High — any `vkCmdCopyBuffer2`/`vkCmdCopyBuffer2KHR` call crashes the guest.
**Status:** GUARDED by d1713623 (2026-10-09) — `if (nreg > 1024) return 0;` prevents crash on garbage input. Original status below.

**Original status:** In uncommitted bridge code (`source/vulkan/vk64bridge.cpp`, "Game-breaking stub
implementations (immediate CPU emulation)" section). Filed for the owning lane.

## Repro (in-browser verified)

Guest `tools/vkcov/vkcov.c`, `t_cmd_all`:
```c
VkBufferCopy2 bc2 = { VK_STRUCTURE_TYPE_BUFFER_COPY_2, 0, 0, 0, 256 };
VkCopyBufferInfo2 cbi = { VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2, 0, vbuf, vbuf, 1, &bc2 };
pvkCmdCopyBuffer2(cmdbuf, &cbi);   // vbuf = valid 1024-byte buffer, bound to valid memory
```

Probe log (`test-results/bridge-coverage-vkcov-run7/console.log:876`):
```
log: VKCOV-TRY: vkCmdCopyBuffer2
log: vk64: trap vkCmdCopyBuffer2
error: worker sent an error! ... Uncaught [object WebAssembly.Exception]
```

`WebAssembly.Exception` (not `RuntimeError`) = a C++ exception escaping the wasm
bridge to JS. Setup verified healthy (`vkCreateDevice=0`, `vkCreateBuffer=0`,
`vkAllocateMemory=0`, `vkBindBufferMemory=0`); the call args are valid
(src=dst=vbuf, 1 region, 256 bytes).

## Suspect

The new immediate-emulation body (`case VK64_fn_vkCmdCopyBuffer2`, ~line 4476):
likely a `std::vector` length error / bad alloc from a size underflow in the
bounds-clamp logic, or an exception from `readArray`/`memcpyFromGuest` on the
region data. The `vkCmdCopyBuffer` (non-2) path does not crash.

## Impact

- vkcov coverage run stalls at 97/269 handlers; `vkCmdCopyBuffer2` and
  `vkCmdCopyBuffer2KHR` are skipped in the harness until fixed (recorded as
  crashed, TRY line = repro).
- Any real app calling `vkCmdCopyBuffer2` (vkd3d does) will crash the guest
  on the current tree.

## Root-cause hypothesis (from matrix lane, 2026-10-09)

The bounds-clamp logic underflows: `if (sOff + n > smem->va + smem->size) n =
smem->va + smem->size - sOff;` — if `sOff` is already past `smem->va +
smem->size`, the subtraction wraps to a huge U64, `std::vector<U8> tmp((size_t)n)`
throws `std::bad_alloc`, and the C++ exception escapes to JS as
`WebAssembly.Exception`. Handlers that pre-check (`if (offset >= dmem->size)
return 0`, e.g. `vkCmdCopyBuffer`, `vkCmdFillBuffer`) do NOT crash; handlers
without the pre-check (`vkCmdCopyBuffer2`, `vkCmdCopyImage`,
`vkCmdCopyImageToBuffer2`, `vkCmdBlitImage`, `vkCmdResolveImage`) all crash.
Fix: add the pre-check before the subtraction in each crasher.

## Suggested next step for fix lane

Add the `if (offset >= size) return 0` / `continue` pre-check to each crasher
(see list above). Verify with the vkcov probe (remove the skip).
