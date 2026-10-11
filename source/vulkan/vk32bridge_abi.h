/*
 *  Copyright (C) 2012-2025  The Boxedwine Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

// Shared ABI between the 32-bit guest libvulkan.so.1 (tools/rootfs64/libvk32)
// and the host marshaller (vk32Bridge in source/vulkan/vk64bridge.cpp).
//
// This is the 32-bit analogue of vk64bridge_abi.h. The function IDs are SHARED
// with the 64-bit ABI (same enum, same values) so the host dispatches on one
// numbering. What differs is the GUEST side:
//
// Trap ABI (i386 Linux `int $0x80`):
//   EAX = VK32_SYSCALL_NR (0x564B0000, same numeric value as VK64_SYSCALL_NR)
//   EBX = function id (one of VK64_fn_* — shared with the 64-bit ABI)
//   ECX = guest virtual address of a `struct VK32Args`
//   -> kernel returns the call's result (or 0 for void) in EAX
//
// Argument marshalling: every wrapper writes its scalar arguments into
// args.a[0..N-1] and traps. The block layout is IDENTICAL to VK64Args
// (16 x u64 slots) so the host can share dispatch code; the 32-bit guest
// simply zero-extends its 32-bit values into the u64 slots. Conventions:
//   - integer / enum / boolean args : zero-extended into the u64 slot
//   - dispatchable handles (VkInstance, VkDevice, ...) : 32-bit IDs,
//     zero-extended into the u64 slot. The host mints these from a 32-bit
//     range (VK32_HANDLE_BASE) so they always fit.
//   - non-dispatchable handles (VkBuffer, VkImage, ...) : full u64, per the
//     Vulkan spec they are 64-bit even on 32-bit.
//   - pointer args : the 32-bit GUEST virtual address, zero-extended to u64.
//     Never a host address.
//   - VkDeviceSize (64-bit) : plain u64, no cast.
//   - float args : bit-cast to u32 in the low half (reserved, unused).
//
// STRUCTS ARE NOT FLATTENED. Pointer slots stay 32-bit guest VAs; the host
// memcpy's the struct out of 32-bit guest memory using the i386 layouts in
// source/vulkan/vk32_guest.h (NOT vk64_guest.h — pointer widths and struct
// alignment differ).
//
// Out-parameters are 32-bit guest VAs the host writes via KMemory.

#ifndef __VK32BRIDGE_ABI_H__
#define __VK32BRIDGE_ABI_H__

#include <stdint.h>

// Same numeric trap as the 64-bit ABI; on i386 it arrives in EAX.
#define VK32_SYSCALL_NR ((uint32_t)0x564B0000U)

// Fixed argument block. Byte-identical layout to VK64Args so the host shares
// dispatch; the name differs so guest code reads unambiguously.
#define VK32_MAX_ARGS 16
typedef struct VK32Args {
    uint64_t a[VK32_MAX_ARGS];
} VK32Args;

// Function IDs are shared with the 64-bit ABI — see vk64bridge_abi.h.
// (Including it here keeps the guest shim to a single include.)
#include "vk64bridge_abi.h"

// 32-bit handle ID range. The host mints 32-bit guest handles from here so a
// dispatchable handle always fits in the guest's 32-bit VkHandle. Disjoint
// from the 64-bit range (0x564B0100+) so the shared object table never
// collides across architectures.
#define VK32_HANDLE_BASE ((uint64_t)0x324B0100ULL)

#endif // __VK32BRIDGE_ABI_H__
