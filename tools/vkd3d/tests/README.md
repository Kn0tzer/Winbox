# Native Vulkan bridge boundary checks

```sh
python3 tools/vkd3d/tests/run-native.py
VK64_TEST_SANITIZERS=address,undefined python3 tools/vkd3d/tests/run-native.py
```

The runner compiles the current `libvk64.c` shim and current
`source/vulkan/vk64bridge.cpp`. It replaces only the private x86 syscall
transport with a direct call and supplies bounds-checked byte-addressed
emulator memory adapters. It uses no Vulkan loader, browser or GPU. Optional
RIP profiling is disabled in the fixture; Vulkan dispatch/object/command and
serialization code remain the real implementation.

Assertions cover shim lookup and dispatch, core/EXT viewport/scissor wrappers,
mapped ranges, command-pool isolation/reset/destruction, dynamic attachment
layouts, per-draw stride/pipeline identities, depth metadata, update-buffer
capture and descriptor offsets. Buffer records validate the current 28-byte
wire header, exact bounded slice bytes/hashes, distinct replay identities
and zero rebased vertex offsets. Vulkan32 checks also execute i386 allocation
layout, four-byte map outputs and cleanup after zero, oversized or failed
allocations. These selected boundary cases are not all-API coverage.

Each compiler subprocess is limited to 180 seconds and test execution to
30 seconds. Temporary build files are removed automatically. Failures remain
failures; the fixture does not replace bridge behavior to force a pass.
