# vulkan-1.dll / bw64vulkan.dll - 64-bit-correct PE Vulkan shim

## Purpose
PE shim DLL forwarding Vulkan calls from Wine PE executables (bsstest.exe,
DXVK, vkd3d-proton) to the host vk64bridge via private syscall
(RAX=0x564b0000, RDI=fnId, RSI=VK64Args*).

## Critical fix (2026-10-09)
Previous DLL truncated pointer args to 32-bit, corrupting guest pointers
above 4GB (e.g. 0x14000d030 became 0x4000d030). Affected all 269 exports.
Rebuilt DLL passes full 64-bit values.

## Rebuilding
python3 tools/vkd3d/probe/gen_vk64pe.py
x86_64-w64-mingw32-gcc -shared -O2 -static-libgcc -o tools/vkd3d/probe/vulkan-1.dll /tmp/vk64pe.c
cp tools/vkd3d/probe/vulkan-1.dll tools/vkd3d/probe/bw64vulkan.dll

IMPORTANT: Use -static-libgcc. Without it, DLL needs libgcc_s_seh-1.dll
which Wine does not provide.

## Files
- vulkan-1.dll: bsstest-probe.mjs stages as guest vulkan-1.dll
- bw64vulkan.dll: d3d-conformance.mjs and d3d12-pixels.mjs stage as vulkan-1.dll
- Both byte-identical.

## Verification
- bsstest 3x PASS (bss_inst=0x564b0100 at 0x14000d030)
- Probes: test-results/bssdiag-bsstest-vk64pe-static1/2/3/
