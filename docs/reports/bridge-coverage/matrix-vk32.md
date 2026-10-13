# vk32 Bridge Coverage Matrix (static record)

Source: static analysis of `source/vulkan/vk64bridge.cpp` (`vk32Bridge`, lines 5383-6537),
`tools/rootfs64/libvk32/libvk32.c`, `source/kernel/syscall.cpp:2422`.

**Status: UNTESTABLE IN-BROWSER** — 32-bit wineboot does not complete due to a Wine 8.0 bug
(find_env_var passes env=0x31, name=NULL with NULL return address to wcsnicmp; CPU emulator
verified correct; PEB/GOT/TLS/GS theories all disproved 2026-10-09), so no 32-bit guest can
execute a Vulkan call. Workaround in kthread.cpp (emulate faulting reads as 0) pending 5-boot
verification. This matrix is a static record; every handler below is unverified in practice.

## Surface

| Item | Count | Verified how |
|---|---|---|
| `VK64_fn_*` enum IDs shared with vk64 | 174 | `vk64bridge_abi.h` |
| Bridge cases implemented in `vk32Bridge` | 83 | `grep -c 'case VK64_fn_'` over lines 5383-6537 |
| Guest shim wrappers (`libvk32.c`) | 157 | unique `vk*(` definitions |
| Wrappers without a bridge case → `VK64_fn_unimplemented` → `VK_ERROR_FEATURE_NOT_PRESENT` (-8) | 74 | 157 - 83 |
| Trap mechanism (`int $0x80`, EAX=0x564B0000 → `-ENOSYS` 0xFFFFFFDA when bridge off) | works | STAGE A trap test (native `make test`), 2026-10-08 |

Note: the task brief's "303 handlers, trap-only" is outdated — `vk32Bridge` is a real
dispatcher with 83 implemented cases sharing the 64-bit fnId numbering (32-bit arg
marshalling via `VK32Args`). It is "trap-only" only in the sense that no 32-bit guest
can reach it yet.

## Four-bucket summary

| Bucket | Count |
|---|---|
| 1. Total handlers (shim wrappers) | 157 |
| 2. Fully working | 0 (untestable — 32-bit boot blocked) |
| 3. Slow or flaky | 0 (unknown) |
| 4. Broken or stub | 157 (83 implemented-but-unreachable, 74 explicit -8 stubs) |

## What unblocks this set

32-bit wineboot completing (the 32-bit Wine-bug workaround lane; kthread.cpp workaround pending
5-boot verification as of 2026-10-09). Once a 32-bit guest runs, the same `vkcov` approach
ports directly: build `vkcov.exe` as a 32-bit PE (or run the 64-bit vkcov against the 32-bit
path via the shared fnIds) and enumerate in-browser.
