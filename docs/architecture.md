# Runtime map

The browser launches Wine userspace inside the emulator. Both 32-bit and 64-bit guests use the same browser page, filesystem, input, audio and rendering services.

| Responsibility | Maintained source |
| --- | --- |
| 32-bit CPU and memory | `source/emulation/cpu/normal`, `source/emulation/cpu/jit`, `source/emulation/softmmu` |
| 64-bit CPU and memory | `source/emulation/cpu/cpu64.cpp`, `source/kernel/kmemory64.cpp` |
| Guest processes, Linux syscalls, ELF loading | `source/kernel`, `source/kernel/loader` |
| Wine bridge and guest execution | `source/sdl/emscripten/wine64session.cpp`, `project/emscripten/wine64-launcher.js` |
| Files, ZIP mounts and browser persistence | `source/io`, `web/files.mjs`, `web/storage.mjs`, `web/sparse-memfs.js` |
| X11 windows, keyboard and pointer input | `source/x11`, `source/x11wire`, `platform/sdl` |
| Audio | `source/kernel/devs/devdsp.cpp`, `platform/sdl/kdspaudio.cpp` |
| OpenGL guest shims and host bridge | `tools/rootfs64/libgl32`, `tools/rootfs64/libgl64`, `source/opengl` |
| Vulkan guest shims and recorder | `tools/rootfs64/libvk32`, `tools/rootfs64/libvk64`, `source/vulkan` |
| Vulkan wire decoding and replay | `web/vk-protocol.mjs`, `web/vkwebgpu.mjs` |
| Shader translation | `tools/spirvfix`, `web/shader.mjs`, `web/naga-rt.mjs`, `web/wgsl-utils.mjs` |
| DXVK and vkd3d | `tools/dxvk/src`, `tools/vkd3d/src`; exact vendor pins in adjacent `SOURCES.json` |

D3D9/10/11 route through DXVK into the Vulkan bridge; D3D12 uses vkd3d. Wine's OpenGL path uses the GL bridge. Recorded Vulkan commands become WebGPU operations; GL rendering also uses the WebGL2 path. The browser page owns GPU objects and receives the guest's windows, input and audio.

See [the test catalog](../web/tests/CATALOG.md) for commands and evidence levels, [the wire format](vulkan-protocol.md) for Vulkan transport, and [game compatibility](game-compatibility.md) for the latest recorded gameplay limits. API registration tests establish dispatch coverage; they cannot establish correct pixels or playable games.

Writable browser state currently persists through IndexedDB. OPFS appears in test names and requested coverage, but no OPFS implementation is present; the test catalog records that gap.
