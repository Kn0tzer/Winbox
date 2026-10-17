# BUG: Bridge `vkCmdBlitImage` crashes guest (ErrorEvent + null function)

**Found by:** bridge API-coverage harness (vkcov), 2026-10-09.
**Severity:** High — any `vkCmdBlitImage` call crashes the guest;
`vkCmdBlitImage2`/`vkCmdBlitImage2KHR` share the code path and are presumed affected.
**Status:** GUARDED by d1713623 (2026-10-09) — `if (nreg > 1024) return 0;` prevents crash on garbage input. Original status below.

**Original status:** In uncommitted bridge code (`source/vulkan/vk64bridge.cpp`, "Game-breaking stub
implementations (immediate CPU emulation)" section). Filed for the owning lane.

## Repro (in-browser verified)

Guest `tools/vkcov/vkcov.c`, `t_cmd_all` (uses valid 64x64 image, 1 blit region).

Probe log (`test-results/bridge-coverage-vkcov-matrix5/console.log`):
```
log: VKCOV-TRY: vkCmdBlitImage
log: vk64: trap vkCmdBlitImage
error: ... ErrorEvent ... null function
```

Note: error is "null function" not "WebAssembly.Exception" — may be a different
crash mechanism than the CopyBuffer2/CopyImage/CopyImageToBuffer2 family, or the
same C++ exception manifesting differently.

## Suspect

Same "Game-breaking stub implementations" section. Likely the same root cause as
BUG-vkCmdCopyBuffer2 (vector sizing / bounds-clamp underflow in immediate CPU
emulation), but the different error signature ("null function") warrants checking
whether it's a null function pointer call rather than a C++ exception.

## Impact

- vkcov coverage run stalls at 107/269 handlers; `vkCmdBlitImage`,
  `vkCmdBlitImage2`, `vkCmdBlitImage2KHR` are skipped in the harness until fixed
  (recorded as crashed, TRY line = repro).

## Suggested next step for fix lane

Check if same root cause as BUG-vkCmdCopyBuffer2; if the "null function" indicates
a different mechanism, root-cause separately. Verify with vkcov probe (remove skip).
