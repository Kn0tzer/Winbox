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

// Shared ABI between the 64-bit guest libvulkan.so.1 (tools/rootfs64/libvk64)
// and the host marshaller (source/vulkan/vk64bridge.cpp). This file is the
// single source of truth for:
//   - the private syscall number used to trap from guest to host
//   - the fixed-layout argument block passed across the trap
//   - the function-id enum identifying which Vulkan entry point is being called
//
// It is plain C (compiles into both the host C++ tree and the guest C library),
// uses only fixed-width integers, and must stay self-contained (no other
// includes) so the guest build needs nothing from the host tree.
//
// This is the Vulkan analogue of source/opengl/gl64bridge_abi.h and follows it
// exactly; the design is tasks/p1-final.md PART 2 (sections 2.2-2.3).
//
// Trap ABI (x86-64 Linux `syscall`):
//   RAX = VK64_SYSCALL_NR
//   RDI = function id (one of VK64_fn_*)
//   RSI = guest virtual address of a `struct VK64Args`
//   -> kernel returns the call's result (or 0 for void) in RAX
//
// Argument marshalling is uniform: every wrapper writes its scalar arguments
// into args.a[0..N-1] and traps. Conventions per slot:
//   - integer / enum / boolean / handle args : zero- or sign-extended into u64
//   - pointer args (create-infos, arrays, out-params) : the GUEST virtual
//     address, as u64. Never a host address, never dereferenced host-side
//     without a memcpyFromGuest first.
//   - VkDeviceSize (64-bit)                 : plain u64, no cast
//   - float args                            : bit-cast to u32 in the low half.
//     None of the 85 pass a float scalar (they all go through a struct or an
//     array pointer), but vkCmdSetViewport-style arrays are read as structs, so
//     this convention is reserved rather than exercised.
// Out-parameters are guest VAs the host writes via KMemory64::memcpyToGuest /
// writed / writeq.
//
// WIDTH. VK64_MAX_ARGS stays at 16 (the GL abi's value). The widest call among
// the 85 is vkCmdPipelineBarrier at 10 args, then vkCmdBindDescriptorSets at 7
// and vkCreateGraphicsPipelines at 6. Unlike GL there is no 32-bit packing to
// exploit: every Vulkan handle, VkDeviceSize and guest address occupies a full
// u64 slot, so the same 16 slots carry less information than in GL.
//
// STRUCTS ARE NOT FLATTENED. Almost every Vulkan call passes its interesting
// arguments by pointer (VkXxxCreateInfo, VkRenderPassBeginInfo,
// VkPipelineViewportStateCreateInfo, ...). Those pointers stay guest VAs; the
// host memcpyFromGuests the struct out of guest memory once per call and
// dereferences it there. Each entry point below therefore documents whether
// its pointer slots are create-infos the host reads, plain arrays, or
// out-params the host writes.

#ifndef __VK64BRIDGE_ABI_H__
#define __VK64BRIDGE_ABI_H__

#include <stdint.h>

// Private syscall number, well outside the Linux x86-64 ABI range (max ~547 as
// of 6.x). 'VK' = 0x564B in the high half keeps it recognizable in logs and
// collision-free against any real syscall. GL64 owns 0x474C0000 ('GL').
#define VK64_SYSCALL_NR  ((uint64_t)0x564B0000ULL)

// Fixed argument block. POD with a stable layout so guest and host agree
// byte-for-byte.
#define VK64_MAX_ARGS 16
typedef struct VK64Args {
    uint64_t a[VK64_MAX_ARGS];
} VK64Args;

// Function ids. APPEND-ONLY: never renumber, the guest libvulkan.so.1
// (tools/rootfs64/libvk64/libvk64.c) and this host bridge are compiled from
// this same list, and the guest copy MUST stay byte-identical.
//
// The 85 ids marked "one of the 85" are exactly the entry points
// /tmp/p1work/ref/vkcube_core.txt records the P1 Vulkan loader + vkcube run
// actually reaching. They are grouped by how they marshal, as tasks/p1-final.md
// section 2.3 specifies: 25 queries + 19 creates + 8 alloc/bind/update/map +
// 20 destroys + 11 command recording + 2 frame boundary = 85.
enum {
    // --- shim plumbing (ids 0..99, NOT part of the 85) -------------------
    // 0 is a load-time witness: the guest shim's __attribute__((constructor))
    // fires vk64_trap(0, 0) the instant wine / dlopen maps libvulkan.so.1. The
    // host logs it, which is the proof that the guest found THIS library (see
    // tools/rootfs64/libvk64/libvk64.c and unknown #1 in tasks/p1-final.md
    // section 2.10). Carries no args.
    VK64_fn_witness = 0,
    // (name*) - one of the vk* names we do NOT implement was requested through
    // vkGetInstanceProcAddr/vkGetDeviceProcAddr. The guest shim hands back a
    // benign stub that traps with the name so the host can log the exact
    // worklist, which is what P1's loader_tally measured. Answer: a benign
    // failure code (never a fault).
    VK64_fn_unimplemented = 1,
    // (name*, hit) - a vkGet*ProcAddr resolution hit/miss trace, mirroring
    // GL64_fn_traceProc.
    VK64_fn_traceProc = 2,

    // === B. Queries and state answers: 25 (host answers from its tables) ===
    VK64_fn_vkEnumerateInstanceExtensionProperties = 100, // (layerName*, out pCount*, out pProps*)
    VK64_fn_vkEnumerateInstanceLayerProperties,      // (out pCount*, out pProps*)
    VK64_fn_vkEnumeratePhysicalDevices,              // (instance, out pCount*, out pPhysDevs*)
    VK64_fn_vkEnumerateDeviceExtensionProperties,    // (physDev, pLayerName*, out pCount*, out pProps*)
    VK64_fn_vkGetPhysicalDeviceProperties,           // (physDev, out VkPhysicalDeviceProperties, 800B)
    VK64_fn_vkGetPhysicalDeviceFeatures,             // (physDev, out VkPhysicalDeviceFeatures)
    VK64_fn_vkGetPhysicalDeviceMemoryProperties,     // (physDev, out VkPhysicalDeviceMemoryProperties)
    VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties,// (physDev, out pCount*, out p*)
    VK64_fn_vkGetPhysicalDeviceFormatProperties,     // (physDev, format, out VkFormatProperties)
    VK64_fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR,   // (physDev, surface, out VkSurfaceCapabilitiesKHR)
    VK64_fn_vkGetPhysicalDeviceSurfaceFormatsKHR,         // (physDev, surface, out pCount*, out p*)
    VK64_fn_vkGetPhysicalDeviceSurfacePresentModesKHR,   // (physDev, surface, out pCount*, out p*)
    VK64_fn_vkGetPhysicalDeviceSurfaceSupportKHR,         // (physDev, qFam, surface, out VkBool32*)
    VK64_fn_vkGetDeviceQueue,                        // (device, family, index, out VkQueue*)
    VK64_fn_vkGetBufferMemoryRequirements,           // (device, buffer, out VkMemoryRequirements)
    VK64_fn_vkGetBufferMemoryRequirements2,          // (device, info2, out VkMemoryRequirements2)
    VK64_fn_vkGetDeviceBufferMemoryRequirements,     // (device, pInfo, out VkMemoryRequirements2)
    VK64_fn_vkGetImageMemoryRequirements,            // (device, image, out VkMemoryRequirements)
    VK64_fn_vkGetImageMemoryRequirements2,           // (device, info2, out VkMemoryRequirements2)
    VK64_fn_vkGetImageSubresourceLayout,             // (device, image, pSubresource*, out VkSubresourceLayout)
    VK64_fn_vkGetSwapchainImagesKHR,                 // (device, swapchain, out pCount*, out VkImage*)
    VK64_fn_vkAcquireNextImageKHR,                   // (device, swapchain, timeout, sem, fence, out u32*)
    VK64_fn_vkGetPastPresentationTimingGOOGLE,       // (device, swapchain, out pCount*, out p*)
    VK64_fn_vkResetFences,                           // (device, n, pFences*)
    VK64_fn_vkWaitForFences,                         // (device, n, pFences*, waitAll, timeout)
    VK64_fn_vkDeviceWaitIdle,                        // (device)
    VK64_fn_vkResetCommandBuffer,                    // (cmdBuf, flags)
    VK64_fn_vkFreeCommandBuffers,                    // (device, pool, n, pCmdBufs*)

    // === C1. Creates: 19 (create-info read out of guest memory) ==========
    VK64_fn_vkCreateInstance = 200,                  // (pCreateInfo*, pAllocator, out VkInstance*)
    VK64_fn_vkCreateDevice,                          // (physDev, pCreateInfo*, pAllocator, out VkDevice*)
    VK64_fn_vkCreateXcbSurfaceKHR,                   // (instance, pCreateInfo*, pAllocator, out VkSurfaceKHR*)
    VK64_fn_vkCreateSwapchainKHR,                    // (device, pCreateInfo*, pAllocator, out VkSwapchainKHR*)
    VK64_fn_vkCreateCommandPool,                     // (device, pCreateInfo*, pAllocator, out VkCommandPool*)
    VK64_fn_vkCreateBuffer,                          // (device, pCreateInfo*, pAllocator, out VkBuffer*)
    VK64_fn_vkCreateImage,                           // (device, pCreateInfo*, pAllocator, out VkImage*)
    VK64_fn_vkCreateImageView,                       // (device, pCreateInfo*, pAllocator, out VkImageView*)
    VK64_fn_vkCreateBufferView,                       // (device, pCreateInfo*, pAllocator, out VkBufferView*)
    VK64_fn_vkCreateSampler,                         // (device, pCreateInfo*, pAllocator, out VkSampler*)
    VK64_fn_vkCreateRenderPass,                      // (device, pCreateInfo*, pAllocator, out VkRenderPass*)
    VK64_fn_vkCreateFramebuffer,                     // (device, pCreateInfo*, pAllocator, out VkFramebuffer*)
    VK64_fn_vkCreateGraphicsPipelines,               // (device, cache, n, pCreateInfos*, pAllocator, out VkPipeline*)
    VK64_fn_vkCreateComputePipelines,                // (device, cache, n, pCreateInfos*, pAllocator, out VkPipeline*)
    VK64_fn_vkCreatePipelineLayout,                  // (device, pCreateInfo*, pAllocator, out VkPipelineLayout*)
    VK64_fn_vkCreateDescriptorSetLayout,             // (device, pCreateInfo*, pAllocator, out VkDescriptorSetLayout*)
    VK64_fn_vkCreateDescriptorPool,                  // (device, pCreateInfo*, pAllocator, out VkDescriptorPool*)
    VK64_fn_vkCreateShaderModule,                    // (device, pCreateInfo* (+pCode bytes), pAllocator, out VkShaderModule*)
    VK64_fn_vkCreatePipelineCache,                   // (device, pCreateInfo*, pAllocator, out VkPipelineCache*)
    VK64_fn_vkCreateFence,                           // (device, pCreateInfo*, pAllocator, out VkFence*)
    VK64_fn_vkCreateSemaphore,                       // (device, pCreateInfo*, pAllocator, out VkSemaphore*)

    // === C2. Allocation / binding / update / map: 8 =======================
    VK64_fn_vkAllocateMemory = 300,                  // (device, pAllocateInfo*, pAllocator, out VkDeviceMemory*)
    VK64_fn_vkAllocateCommandBuffers,                // (device, pAllocateInfo*, out VkCommandBuffer*)
    VK64_fn_vkAllocateDescriptorSets,                // (device, pAllocateInfo*, out VkDescriptorSet*)
    VK64_fn_vkBindBufferMemory,                      // (device, buffer, memory, offset)
    VK64_fn_vkBindImageMemory,                       // (device, image, memory, offset)
    VK64_fn_vkUpdateDescriptorSets,                  // (device, nWrite, pWrites*, nCopy, pCopies*)
    VK64_fn_vkMapMemory,                             // (device, memory, offset, size, flags, out void**) -> GUEST VA
    VK64_fn_vkUnmapMemory,                           // (device, memory)

    // === C3. Destroys: 20 =================================================
    VK64_fn_vkDestroyInstance = 400,                 // (instance, pAllocator)
    VK64_fn_vkDestroyDevice,                         // (device, pAllocator)
    VK64_fn_vkDestroySurfaceKHR,                     // (instance, surface, pAllocator)
    VK64_fn_vkDestroySwapchainKHR,                   // (device, swapchain, pAllocator)
    VK64_fn_vkDestroyCommandPool,                    // (device, pool, pAllocator)
    VK64_fn_vkDestroyBuffer,                         // (device, buffer, pAllocator)
    VK64_fn_vkDestroyImage,                          // (device, image, pAllocator)
    VK64_fn_vkDestroyImageView,                      // (device, view, pAllocator)
    VK64_fn_vkDestroySampler,                        // (device, sampler, pAllocator)
    VK64_fn_vkDestroyRenderPass,                     // (device, renderPass, pAllocator)
    VK64_fn_vkDestroyFramebuffer,                    // (device, framebuffer, pAllocator)
    VK64_fn_vkDestroyPipeline,                       // (device, pipeline, pAllocator)
    VK64_fn_vkDestroyPipelineLayout,                 // (device, layout, pAllocator)
    VK64_fn_vkDestroyDescriptorSetLayout,            // (device, setLayout, pAllocator)
    VK64_fn_vkDestroyDescriptorPool,                 // (device, pool, pAllocator)
    VK64_fn_vkDestroyShaderModule,                   // (device, module, pAllocator)
    VK64_fn_vkDestroyPipelineCache,                  // (device, cache, pAllocator)
    VK64_fn_vkDestroyFence,                          // (device, fence, pAllocator)
    VK64_fn_vkDestroySemaphore,                      // (device, semaphore, pAllocator)
    VK64_fn_vkFreeMemory,                            // (device, memory, pAllocator)

    // === D. Command recording: 11 (into the host-side command array) =======
    VK64_fn_vkBeginCommandBuffer = 500,              // (cmdBuf, pBeginInfo*)
    VK64_fn_vkEndCommandBuffer,                      // (cmdBuf)
    VK64_fn_vkCmdBeginRenderPass,                    // (cmdBuf, pRenderPassBegin* (clear values!), contents)
    VK64_fn_vkCmdBindPipeline,                       // (cmdBuf, bindPoint, pipeline)
    VK64_fn_vkCmdBindDescriptorSets,                 // (cmdBuf, bindPoint, layout, firstSet, setCount, pSets*, nDyn, pDynOffsets*)
    VK64_fn_vkCmdSetViewport,                        // (cmdBuf, first, count, pViewports*)
    VK64_fn_vkCmdSetScissor,                         // (cmdBuf, first, count, pScissors*)
    VK64_fn_vkCmdDraw,                               // (cmdBuf, vertexCount, instanceCount, firstVertex, firstInstance)
    VK64_fn_vkCmdPipelineBarrier,                    // (cmdBuf, srcStage, dstStage, depFlags, nMemBar, pMemBar*, nBufBar, pBufBar*, nImgBar, pImgBar*)  [widest: 10 slots]
    VK64_fn_vkCmdCopyBufferToImage,                   // (cmdBuf, srcBuffer, dstImage, layout, nRegions, pRegions*)
    VK64_fn_vkCmdEndRenderPass,                      // (cmdBuf)

    // === E. Frame boundary: 2 =============================================
    VK64_fn_vkQueueSubmit = 600,                     // (queue, nSubmits, pSubmits*, fence) -> builds the frame manifest
    VK64_fn_vkQueuePresentKHR,                       // (queue, pPresentInfo*) -> hops the pending frame to the page

    // === Extras (ids 700+, NOT part of the 85) ===========================
    // Append-only convenience set. These are the calls a loader-shaped caller
    // or a 1.1+ guest reaches for that are NOT in ref/vkcube_core.txt, but
    // that are cheap to answer honestly and would otherwise fall into the
    // benign-error tail. They are listed separately so the 85 stay mechanically
    // diffable against the P1 reference list.
    VK64_fn_vkEnumerateInstanceVersion = 700,        // (out u32*) -> 1.1.0. Also answered
                                                     // guest-side so a NULL-instance
                                                     // GIPA probe never traps.
    VK64_fn_vkGetPhysicalDeviceProperties2,          // (physDev, out VkPhysicalDeviceProperties2)
    VK64_fn_vkGetPhysicalDeviceFeatures2,            // (physDev, out VkPhysicalDeviceFeatures2)
    VK64_fn_vkGetPhysicalDeviceMemoryProperties2,    // (physDev, out VkPhysicalDeviceMemoryProperties2)
    VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties2,// (physDev, out pCount*, out p*)
    VK64_fn_vkGetFenceStatus,                        // (device, fence) -> VkResult (immediate sync)
    VK64_fn_vkResetCommandPool,                      // (device, pool, flags)
    VK64_fn_vkQueueWaitIdle,                         // (queue)
    VK64_fn_vkCreateHeadlessSurfaceEXT,              // (instance, pCreateInfo*, pAllocator, out VkSurfaceKHR*)

    // --- P2-NOW block (audit items 1-6). Also NOT part of the 85, but these are
    // --- the calls a DXVK-on-wine workload cannot get past without: the Win32
    // --- surface path (d3d11.dll -> vulkan-1.dll -> winevulkan -> this shim
    // --- creates a Win32 surface, never a headless one), the timeline-semaphore
    // --- quartet (DXVK puts a u64 wait/signal fence on EVERY command-list flush,
    // --- dxvk_context.cpp), and vkCmdPushConstants (DXVK pushes constants
    // --- per-draw for essentially everything).
    VK64_fn_vkCreateWin32SurfaceKHR,                 // (instance, pCreateInfo*, pAllocator, out VkSurfaceKHR*)
    VK64_fn_vkGetSemaphoreCounterValue,              // (device, semaphore, out u64*)
    VK64_fn_vkWaitSemaphores,                        // (device, pWaitInfo*, timeout)
    VK64_fn_vkSignalSemaphore,                       // (device, pSignalInfo*)
    VK64_fn_vkCmdPushConstants,                      // (cmdBuf, layout, stageFlags, offset, size, pValues)

    // --- G2 (DXVK-on-wine): wine 8.x's winex11 Vulkan init (wine_vk_init in
    // --- dlls/winex11.drv/vulkan.c) dlopen()s this shim as libvulkan.so.1 and
    // --- dlsym()s both names below with LOAD_FUNCPTR (hard fail: silent
    // --- dlclose, then winevulkan's init_vulkan reports "Failed to load Wine
    // --- graphics driver supporting Vulkan" and DXVK never gets
    // --- VK_KHR_surface). Same append-only order as libvk64.c.
    VK64_fn_vkCreateXlibSurfaceKHR,                // (instance, pCreateInfo*, pAllocator, out VkSurfaceKHR*)
    VK64_fn_vkGetPhysicalDeviceXlibPresentationSupportKHR, // (physDev, qFam, dpy*, visual) -> VkBool32

    // --- G2 round 2: DXVK's D3D9SwapChainEx::CreateBackBuffers gates every
    // --- texture on vkGetPhysicalDeviceImageFormatProperties2 (benign stub =>
    // --- zero maxExtent => "D3D9: Cannot create texture"); DXVK's
    // --- getFormatFeatures reads VkFormatProperties3 off the FormatProperties2
    // --- pNext chain; and DXVK draws ~100% indexed, so the binds + the indexed
    // --- draw must be recorded for the manifest to carry any draw at all.
    VK64_fn_vkGetPhysicalDeviceImageFormatProperties2, // (physDev, pInfo2*, pProps2*) -> VkResult
    VK64_fn_vkGetPhysicalDeviceImageFormatProperties,  // (physDev, fmt, type, tiling, usage, flags, pProps*) -> VkResult
    VK64_fn_vkGetPhysicalDeviceFormatProperties2,      // (physDev, format, pProps2*) -> void
    VK64_fn_vkCmdDrawIndexed,             // (cmdBuf, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance)
    VK64_fn_vkCmdBindVertexBuffers,        // (cmdBuf, firstBinding, bindingCount, pBuffers*, pOffsets*)
    VK64_fn_vkCmdBindIndexBuffer,           // (cmdBuf, buffer, offset, indexType)

    VK64_fn_vkQueueSubmit2,                       // (queue, nSubmits, pSubmitInfo2*, fence) -> builds the frame manifest
    VK64_fn_vkQueueSubmit2KHR,                    // (queue, nSubmits, pSubmitInfo2*, fence) -> alias
    VK64_fn_vkCreateDescriptorUpdateTemplate,     // (device, pCreateInfo*, pAllocator, out VkDescriptorUpdateTemplate*) -> template object
    VK64_fn_vkDestroyDescriptorUpdateTemplate,    // (device, template, pAllocator)
    VK64_fn_vkUpdateDescriptorSetWithTemplate,    // (device, set, template, pData)
    VK64_fn_vkCmdBeginRenderPass2,                // (cmdBuf, pBeginInfo*, pDependencyInfo*) -> records render pass begin
    VK64_fn_vkCmdBeginRenderPass2KHR,             // alias
    VK64_fn_vkCmdEndRenderPass2,                  // (cmdBuf, pDependencyInfo*) -> records render pass end
    VK64_fn_vkCmdEndRenderPass2KHR,               // alias
    VK64_fn_vkCmdPipelineBarrier2,               // (cmdBuf, pDependencyInfo*) -> records barrier
    VK64_fn_vkCmdPipelineBarrier2KHR,            // alias
    VK64_fn_vkCmdPushConstants2,                 // (cmdBuf, pPushConstantsInfo*) -> records push constants
    VK64_fn_vkCmdPushConstants2KHR,              // alias
    VK64_fn_vkCmdBindIndexBuffer2,               // (cmdBuf, buffer, offset, size, indexType) -> binds index buffer
    VK64_fn_vkCmdBindIndexBuffer2KHR,            // alias
    VK64_fn_vkCmdBeginRendering,                // (cmdBuf, pRenderingInfo*) -> records dynamic rendering begin
    VK64_fn_vkCmdBeginRenderingKHR,             // alias
    VK64_fn_vkCmdEndRendering,                  // (cmdBuf) -> records dynamic rendering end
    VK64_fn_vkCmdEndRenderingKHR,               // alias
    VK64_fn_vkCmdUpdateBuffer,                  // (cmdBuf, dstBuffer, dstOffset, dataSize, pData*) -> records a transfer write
    // vkd3d supporting calls: append-only, separately from inherited extras.
    VK64_fn_vkFlushMappedMemoryRanges = 800,       // (device, count, pRanges*)
    VK64_fn_vkInvalidateMappedMemoryRanges,       // (device, count, pRanges*)
    VK64_fn_vkCmdBindVertexBuffers2,
    VK64_fn_vkBindBufferMemory2,                  // (device, bindInfoCount, pBindInfos*) -> records buffer binds
    VK64_fn_vkBindBufferMemory2KHR,               // alias
    VK64_fn_vkBindImageMemory2,                   // (device, bindInfoCount, pBindInfos*) -> records image binds
    VK64_fn_vkBindImageMemory2KHR,                // alias
    // dxgi (D3D12 swapchain): init_vk_funcs resolves this from vulkan-1.dll
    // when loaded as our PE shim. Same append-only position as libvk64.c.
    VK64_fn_vkGetPhysicalDeviceWin32PresentationSupportKHR, // (physDev, qFam) -> VkBool32
    // readback: image->buffer copies (append-only; DXVK readback path)
    VK64_fn_vkCmdCopyImageToBuffer,                  // (cmdBuf, srcImage, srcImageLayout, dstBuffer, regionCount, pRegions*) -> records CMD_COPY_I2B
    VK64_fn_vkCmdCopyImage,                          // (cmdBuf, ...) -> no-op (ID stability)
    VK64_fn_vkCmdCopyImageToBuffer2,                 // (cmdBuf, srcImage, 0, dstBuffer, regionCount, pRegions*) -> records CMD_COPY_I2B
    VK64_fn_vkCmdCopyImageToBuffer2KHR,              // alias of vkCmdCopyImageToBuffer2
    VK64_fn_vkCmdCopyBuffer,                        // (cmdBuf, srcBuffer, dstBuffer, regionCount, pRegions*) -> immediate CPU memcpy
    VK64_fn_vkCmdCopyBuffer2,                       // (cmdBuf, pCopyBufferInfo*) -> immediate CPU memcpy
    VK64_fn_vkCmdCopyBuffer2KHR,                    // alias of vkCmdCopyBuffer2
    // Game-breaking stub implementations (immediate CPU emulation)
    VK64_fn_vkCmdFillBuffer,                        // (cmdBuf, dstBuffer, dstOffset, size, data) -> fill with 32-bit pattern
    VK64_fn_vkCmdClearColorImage,                   // (cmdBuf, image, layout, pColor*, rangeCount, pRanges*) -> fill image
    VK64_fn_vkCmdClearAttachments,                  // (cmdBuf, attachCount, pAttach*, rectCount, pRects*) -> clear in RP
    VK64_fn_vkCmdClearAttachmentsKHR,               // alias
    VK64_fn_vkCmdCopyImage2,                        // (cmdBuf, srcImg, dstImg, nRegions, pRegions*) -> immediate copy (unpacked)
    VK64_fn_vkCmdCopyImage2KHR,                     // alias
    VK64_fn_vkCmdCopyBufferToImage2,                // (cmdBuf, srcBuf, dstImg, nRegions, pRegions*) -> immediate copy (unpacked)
    VK64_fn_vkCmdCopyBufferToImage2KHR,             // alias
    VK64_fn_vkCmdBlitImage,                         // (cmdBuf, srcImg, srcLayout, dstImg, dstLayout, nRegions, pRegions*, filter) -> scaled copy
    VK64_fn_vkCmdBlitImage2,                        // (cmdBuf, srcImg, dstImg, nRegions, pRegions*, filter) -> scaled copy (unpacked)
    VK64_fn_vkCmdBlitImage2KHR,                     // alias
    VK64_fn_vkCmdResolveImage,                      // (cmdBuf, srcImg, srcLayout, dstImg, dstLayout, nRegions, pRegions*) -> MSAA resolve
    VK64_fn_vkCmdResolveImageKHR,                   // alias
    VK64_fn_vkCmdDrawIndirect,                      // (cmdBuf, buffer, offset, drawCount, stride) -> indirect to direct draws
    VK64_fn_vkCmdDrawIndexedIndirect,               // (cmdBuf, buffer, offset, drawCount, stride) -> indirect to direct draws
    VK64_fn_vkCmdDrawIndirectCount,                 // (cmdBuf, buffer, offset, countBuf, countOffset, maxDraws, stride)
    VK64_fn_vkCmdDrawIndirectCountKHR,              // alias
    VK64_fn_vkCmdDrawIndexedIndirectCount,          // (cmdBuf, buffer, offset, countBuf, countOffset, maxDraws, stride)
    VK64_fn_vkCmdExecuteCommands,                   // (cmdBuf, cmdCount, pCmdBufs*) -> merge secondary CBs
    VK64_fn_vkCmdDispatch,                          // (cmdBuf, x, y, z) -> record compute dispatch
    VK64_fn_vkCmdDispatchBase,                      // (cmdBuf, baseX, baseY, baseZ, x, y, z) -> record compute dispatch
    VK64_fn_vkCmdDispatchIndirect,                  // (cmdBuf, buffer, offset) -> record compute dispatch
    VK64_fn_vkGetBufferDeviceAddress,              // (device, VkBufferDeviceAddressInfo*) -> u64 VA
    VK64_fn_vkGetPhysicalDeviceProperties2KHR,     // alias of vkGetPhysicalDeviceProperties2
    VK64_fn_vkGetPhysicalDeviceFeatures2KHR,       // alias of vkGetPhysicalDeviceFeatures2
    VK64_fn_vkGetPhysicalDeviceMemoryProperties2KHR, // alias of vkGetPhysicalDeviceMemoryProperties2
    // D3D9 query sync (append-only): D3D9 event/occlusion queries via DXVK need
    // real VkEvent/VkQueryPool entry points. The benign stub faults Wine's Unix
    // handler (it reads the fake handle as a string) and trips the PE assert,
    // so these are implemented in the bridge. Same explicit positions as libvk32.c.
    VK64_fn_vkCreateEvent = 841,                // (device, pCreateInfo*, pAllocator, out VkEvent*)
    VK64_fn_vkDestroyEvent,                     // (device, event, pAllocator)
    VK64_fn_vkSetEvent,                         // (device, event) -> signalled
    VK64_fn_vkResetEvent,                       // (device, event) -> unsignalled
    VK64_fn_vkGetEventStatus,                   // (device, event) -> SET/RESET
    VK64_fn_vkCmdSetEvent,                      // (cmdBuf, event, stageMask) -> signalled
    VK64_fn_vkCmdResetEvent,                    // (cmdBuf, event, stageMask) -> unsignalled
    VK64_fn_vkCmdWaitEvents,                    // (...) -> no-op (immediate execution already done)
    VK64_fn_vkCreateQueryPool,                  // (device, pCreateInfo*, pAllocator, out VkQueryPool*)
    VK64_fn_vkDestroyQueryPool,                 // (device, pool, pAllocator)
    VK64_fn_vkResetQueryPool,                   // (device, pool, first, count) -> SUCCESS
    VK64_fn_vkGetQueryPoolResults,              // (device, pool, first, count, dataSize, pData*, stride, flags) -> available occlusion/timestamp results
    VK64_fn_vkCmdResetQueryPool,                // (cmdBuf, pool, first, count) -> SUCCESS
    VK64_fn_vkCmdBeginQuery,                    // (cmdBuf, pool, query, flags) -> SUCCESS
    VK64_fn_vkCmdEndQuery,                      // (cmdBuf, pool, query) -> SUCCESS
    // Core dynamic raster state. Append-only; old trap IDs stay unchanged.
    VK64_fn_vkCmdSetBlendConstants = 900,
    VK64_fn_vkCmdSetStencilCompareMask,
    VK64_fn_vkCmdSetStencilWriteMask,
    VK64_fn_vkCmdSetStencilReference,
    VK64_fn_vkCmdCopyQueryPoolResults = 904, // (cmdBuf, pool, first, count, dstBuffer, offset, stride, flags)
    VK64_fn_vkCmdSetDepthBias = 905,          // (cmdBuf, constantFactor bits, clamp bits, slopeFactor bits)
    // Extended dynamic state / events / timestamps (2026-10-23). Append-only.
    VK64_fn_vkCmdSetCullMode = 906,           // (cmdBuf, VkCullModeFlags) -> replayed for pipelines that declare VK_DYNAMIC_STATE_CULL_MODE
    VK64_fn_vkCmdSetFrontFace = 907,          // (cmdBuf, VkFrontFace) -> replayed for pipelines that declare VK_DYNAMIC_STATE_FRONT_FACE
    VK64_fn_vkCmdSetEvent2 = 908,             // (cmdBuf, event, pDependencyInfo*) -> signalled (same as vkCmdSetEvent)
    VK64_fn_vkCmdClearDepthStencilImage = 909, // (cmdBuf, image, layout, pDepthStencil*, rangeCount, pRanges*) -> GPU clear (64-bit shim only)
    VK64_fn_vkCmdWriteTimestamp = 910,        // (cmdBuf, stage, pool, query) -> accepted; timestamp results are synthesized from the host clock
    VK64_fn_vkCmdSetDepthBounds = 911,        // (cmdBuf, min bits, max bits) -> accepted; WebGPU has no depth-bounds test
    // Descriptor pools, push descriptors, layout support (2026-10-24). Append-only.
    VK64_fn_vkResetDescriptorPool = 912,      // (device, pool, flags) -> frees every set allocated from the pool
    VK64_fn_vkFreeDescriptorSets = 913,       // (device, pool, count, pSets*) -> frees the named sets
    VK64_fn_vkCmdPushDescriptorSetKHR = 914,  // (cmdBuf, bindPoint, layout, set, writeCount, pWrites*) -> binds a synthetic set
    VK64_fn_vkCmdPushDescriptorSetWithTemplateKHR = 915, // (cmdBuf, template, layout, set, pData*)
    VK64_fn_vkGetDescriptorSetLayoutSupport = 916, // (device, pCreateInfo*, pSupport*) -> supported, variable count limit
    VK64_fn_vkGetDeviceImageMemoryRequirements = 917, // (device, VkDeviceImageMemoryRequirements*, VkMemoryRequirements2*) -> size from the create info
    VK64_fn_vkCmdSetExtendedDynamicState = 918, // (cmdBuf, which, value) -> core-1.3 / EDS2 dynamic state; which: 2 topology 3 depthTest 4 depthWrite 5 depthOp 6 stencilTest 7 depthBias 8 rasterizerDiscard 9 primitiveRestart 10 stencilOp(packed face|fail<<4|pass<<8|depthFail<<12|cmp<<16) 11 depthBoundsTest
    VK64_fn__MAX
};

#endif // __VK64BRIDGE_ABI_H__
