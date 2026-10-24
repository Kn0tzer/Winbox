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

// Guest libvulkan.so.1 for the 32-bit Boxedwine Vulkan path — the Vulkan
// analogue of tools/rootfs64/libgl64/libgl64.c.
//
// THE WHOLE POINT: there is no Vulkan loader and no ICD .so in the guest. This
// library IS the Vulkan API surface. Every vk* entry point below packs its
// arguments into a VK64Args block and traps to the Boxedwine64 kernel on the
// private syscall VK64_SYSCALL_NR, where source/vulkan/vk64bridge.cpp owns all
// the state and hands each completed frame to the browser page. That removes the
// whole loader layer P1 needed (12 LOOKUP_REQUIRED_GIPA entry points, the
// negotiate handshake, VK_LOADER_DATA member 0, VK_ICD_FILENAMES) — see
// tasks/p1-final.md PART 2 section 2.0.
//
// An application reaches the API the way vulkan-tools' vkcube does: it dlopen()s
// "libvulkan.so.1", dlsym()s vkGetInstanceProcAddr, and resolves everything else
// through it. vkcube has ZERO direct vk* imports (verified with readelf on the
// P1 binary), so vkGetInstanceProcAddr's static table below is the ONLY thing
// that decides which entry points exist — an unimplemented name must still hand
// back a callable pointer (vk64_benign) so the app does not treat the library as
// broken.
//
// Self-contained: no Vulkan headers, no libc. Freestanding like libgl64.c — the
// few fixed-width types, and string/mem helpers, are declared locally so the .so
// has no DT_NEEDED.

typedef unsigned char       uint8_t;
typedef unsigned short      uint16_t;
typedef unsigned int        uint32_t;
typedef int                 int32_t;
typedef unsigned long long  uint64_t;
typedef long                intptr_t;
typedef unsigned long       uintptr_t;

static void* vk_memcpy(void* d, const void* s, unsigned long n) {
    unsigned char* dd = (unsigned char*)d; const unsigned char* ss = (const unsigned char*)s;
    while (n--) { *dd++ = *ss++; }
    return d;
}
static int vk_strcmp(const char* a, const char* b) {
    while (*a && (*a == *b)) { a++; b++; }
    return (int)((unsigned char)*a) - (int)((unsigned char)*b);
}
#define memcpy vk_memcpy
#define strcmp vk_strcmp

// ---- ABI shared with the host (MUST stay byte-identical to
// ---- source/vulkan/vk64bridge_abi.h; that header's comment is the spec) ----
#define VK64_SYSCALL_NR ((uint64_t)0x564B0000ULL)
#define VK64_MAX_ARGS 16
typedef struct VK64Args { uint64_t a[VK64_MAX_ARGS]; } VK64Args;

enum {
    VK64_fn_witness = 0,
    VK64_fn_unimplemented,
    VK64_fn_traceProc,

    VK64_fn_vkEnumerateInstanceExtensionProperties = 100,
    VK64_fn_vkEnumerateInstanceLayerProperties,
    VK64_fn_vkEnumeratePhysicalDevices,
    VK64_fn_vkEnumerateDeviceExtensionProperties,
    VK64_fn_vkGetPhysicalDeviceProperties,
    VK64_fn_vkGetPhysicalDeviceFeatures,
    VK64_fn_vkGetPhysicalDeviceMemoryProperties,
    VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties,
    VK64_fn_vkGetPhysicalDeviceFormatProperties,
    VK64_fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR,
    VK64_fn_vkGetPhysicalDeviceSurfaceFormatsKHR,
    VK64_fn_vkGetPhysicalDeviceSurfacePresentModesKHR,
    VK64_fn_vkGetPhysicalDeviceSurfaceSupportKHR,
    VK64_fn_vkGetDeviceQueue,
    VK64_fn_vkGetBufferMemoryRequirements,
    VK64_fn_vkGetBufferMemoryRequirements2,
    VK64_fn_vkGetDeviceBufferMemoryRequirements,
    VK64_fn_vkGetImageMemoryRequirements,
    VK64_fn_vkGetImageMemoryRequirements2,
    VK64_fn_vkGetImageSubresourceLayout,
    VK64_fn_vkGetSwapchainImagesKHR,
    VK64_fn_vkAcquireNextImageKHR,
    VK64_fn_vkGetPastPresentationTimingGOOGLE,
    VK64_fn_vkResetFences,
    VK64_fn_vkWaitForFences,
    VK64_fn_vkDeviceWaitIdle,
    VK64_fn_vkResetCommandBuffer,
    VK64_fn_vkFreeCommandBuffers,

    VK64_fn_vkCreateInstance = 200,
    VK64_fn_vkCreateDevice,
    VK64_fn_vkCreateXcbSurfaceKHR,
    VK64_fn_vkCreateSwapchainKHR,
    VK64_fn_vkCreateCommandPool,
    VK64_fn_vkCreateBuffer,
    VK64_fn_vkCreateImage,
    VK64_fn_vkCreateImageView,
    VK64_fn_vkCreateBufferView,
    VK64_fn_vkCreateSampler,
    VK64_fn_vkCreateRenderPass,
    VK64_fn_vkCreateFramebuffer,
    VK64_fn_vkCreateGraphicsPipelines,
    VK64_fn_vkCreateComputePipelines,
    VK64_fn_vkCreatePipelineLayout,
    VK64_fn_vkCreateDescriptorSetLayout,
    VK64_fn_vkCreateDescriptorPool,
    VK64_fn_vkCreateShaderModule,
    VK64_fn_vkCreatePipelineCache,
    VK64_fn_vkCreateFence,
    VK64_fn_vkCreateSemaphore,

    VK64_fn_vkAllocateMemory = 300,
    VK64_fn_vkAllocateCommandBuffers,
    VK64_fn_vkAllocateDescriptorSets,
    VK64_fn_vkBindBufferMemory,
    VK64_fn_vkBindImageMemory,
    VK64_fn_vkUpdateDescriptorSets,
    VK64_fn_vkMapMemory,
    VK64_fn_vkUnmapMemory,

    VK64_fn_vkDestroyInstance = 400,
    VK64_fn_vkDestroyDevice,
    VK64_fn_vkDestroySurfaceKHR,
    VK64_fn_vkDestroySwapchainKHR,
    VK64_fn_vkDestroyCommandPool,
    VK64_fn_vkDestroyBuffer,
    VK64_fn_vkDestroyImage,
    VK64_fn_vkDestroyImageView,
    VK64_fn_vkDestroySampler,
    VK64_fn_vkDestroyRenderPass,
    VK64_fn_vkDestroyFramebuffer,
    VK64_fn_vkDestroyPipeline,
    VK64_fn_vkDestroyPipelineLayout,
    VK64_fn_vkDestroyDescriptorSetLayout,
    VK64_fn_vkDestroyDescriptorPool,
    VK64_fn_vkDestroyShaderModule,
    VK64_fn_vkDestroyPipelineCache,
    VK64_fn_vkDestroyFence,
    VK64_fn_vkDestroySemaphore,
    VK64_fn_vkFreeMemory,

    VK64_fn_vkBeginCommandBuffer = 500,
    VK64_fn_vkEndCommandBuffer,
    VK64_fn_vkCmdBeginRenderPass,
    VK64_fn_vkCmdBindPipeline,
    VK64_fn_vkCmdBindDescriptorSets,
    VK64_fn_vkCmdSetViewport,
    VK64_fn_vkCmdSetScissor,
    VK64_fn_vkCmdDraw,
    VK64_fn_vkCmdPipelineBarrier,
    VK64_fn_vkCmdCopyBufferToImage,
    VK64_fn_vkCmdEndRenderPass,

    VK64_fn_vkQueueSubmit = 600,
    VK64_fn_vkQueuePresentKHR,

    VK64_fn_vkEnumerateInstanceVersion = 700,
    VK64_fn_vkGetPhysicalDeviceProperties2,
    VK64_fn_vkGetPhysicalDeviceFeatures2,
    VK64_fn_vkGetPhysicalDeviceMemoryProperties2,
    VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties2,
    VK64_fn_vkGetFenceStatus,
    VK64_fn_vkResetCommandPool,
    VK64_fn_vkQueueWaitIdle,
    VK64_fn_vkCreateHeadlessSurfaceEXT,

    // P2-NOW block: the Win32 surface a wine-hosted DXVK creates, the
    // timeline-semaphore quartet its per-flush tracking fence needs, and
    // vkCmdPushConstants. Same list, same order, as vk64bridge_abi.h.
    VK64_fn_vkCreateWin32SurfaceKHR,
    VK64_fn_vkGetSemaphoreCounterValue,
    VK64_fn_vkWaitSemaphores,
    VK64_fn_vkSignalSemaphore,
    VK64_fn_vkCmdPushConstants,

    // G2 (DXVK-on-wine): wine 8.x's winex11 Vulkan init dlsym()s these two with
    // LOAD_FUNCPTR (hard fail). Same append-only order as vk64bridge_abi.h.
    VK64_fn_vkCreateXlibSurfaceKHR,
    VK64_fn_vkGetPhysicalDeviceXlibPresentationSupportKHR,

    VK64_fn_vkGetPhysicalDeviceImageFormatProperties2,
    VK64_fn_vkGetPhysicalDeviceImageFormatProperties,
    VK64_fn_vkGetPhysicalDeviceFormatProperties2,
    VK64_fn_vkCmdDrawIndexed,
    VK64_fn_vkCmdBindVertexBuffers,
    VK64_fn_vkCmdBindIndexBuffer,
    VK64_fn_vkQueueSubmit2,
    VK64_fn_vkQueueSubmit2KHR,
    VK64_fn_vkCreateDescriptorUpdateTemplate,
    VK64_fn_vkDestroyDescriptorUpdateTemplate,
    VK64_fn_vkUpdateDescriptorSetWithTemplate,
    VK64_fn_vkCmdBeginRenderPass2,
    VK64_fn_vkCmdBeginRenderPass2KHR,
    VK64_fn_vkCmdEndRenderPass2,
    VK64_fn_vkCmdEndRenderPass2KHR,
    VK64_fn_vkCmdPipelineBarrier2,
    VK64_fn_vkCmdPipelineBarrier2KHR,
    VK64_fn_vkCmdPushConstants2,
    VK64_fn_vkCmdPushConstants2KHR,
    VK64_fn_vkCmdBindIndexBuffer2,
    VK64_fn_vkCmdBindIndexBuffer2KHR,
    VK64_fn_vkCmdBeginRendering,
    VK64_fn_vkCmdBeginRenderingKHR,
    VK64_fn_vkCmdEndRendering,
    VK64_fn_vkCmdEndRenderingKHR,
    VK64_fn_vkCmdUpdateBuffer,

    VK64_fn_vkFlushMappedMemoryRanges = 800,
    VK64_fn_vkInvalidateMappedMemoryRanges,
    VK64_fn_vkCmdBindVertexBuffers2,
    VK64_fn_vkBindBufferMemory2,
    VK64_fn_vkBindBufferMemory2KHR,
    VK64_fn_vkBindImageMemory2,
    VK64_fn_vkBindImageMemory2KHR,
    // dxgi (D3D12 swapchain): init_vk_funcs dlsym()s this when it loads
    // vulkan-1.dll as our PE shim. Same append-only position as
    // vk64bridge_abi.h.
    VK64_fn_vkGetPhysicalDeviceWin32PresentationSupportKHR,
    // Transfer IDs shared with vk64bridge_abi.h. DXVK uses the Vulkan 1.3
    // copy commands even when the older names also resolve.
    VK64_fn_vkCmdCopyBuffer = 812,
    VK64_fn_vkCmdCopyBuffer2 = 813,
    VK64_fn_vkCmdCopyBuffer2KHR = 814,
    VK64_fn_vkCmdCopyBufferToImage2 = 821,
    VK64_fn_vkCmdCopyBufferToImage2KHR = 822,
    VK64_fn_vkCmdResolveImage = 826,
    VK64_fn_vkCmdCopyImageToBuffer = 808,
    VK64_fn_vkCmdCopyImage = 809,
    VK64_fn_vkCmdCopyImageToBuffer2 = 810,
    VK64_fn_vkCmdCopyImageToBuffer2KHR = 811,
    VK64_fn_vkCmdFillBuffer = 815,
    VK64_fn_vkCmdClearColorImage = 816,
    VK64_fn_vkCmdClearAttachments = 817,
    VK64_fn_vkCmdClearAttachmentsKHR = 818,
    VK64_fn_vkCmdCopyImage2 = 819,
    VK64_fn_vkCmdCopyImage2KHR = 820,
    VK64_fn_vkCmdBlitImage = 823,
    VK64_fn_vkCmdBlitImage2 = 824,
    VK64_fn_vkCmdBlitImage2KHR = 825,
    VK64_fn_vkCmdResolveImageKHR = 827,
    VK64_fn_vkCmdDrawIndirect = 828,
    VK64_fn_vkCmdDrawIndexedIndirect = 829,
    VK64_fn_vkCmdDrawIndirectCount = 830,
    VK64_fn_vkCmdDrawIndirectCountKHR = 831,
    VK64_fn_vkCmdDrawIndexedIndirectCount = 832,
    VK64_fn_vkCmdExecuteCommands = 833,
    VK64_fn_vkCmdDispatch = 834,
    VK64_fn_vkCmdDispatchBase = 835,
    VK64_fn_vkCmdDispatchIndirect = 836,
    VK64_fn_vkGetBufferDeviceAddress = 837,
    VK64_fn_vkGetPhysicalDeviceProperties2KHR = 838,
    VK64_fn_vkGetPhysicalDeviceFeatures2KHR = 839,
    VK64_fn_vkGetPhysicalDeviceMemoryProperties2KHR = 840,
    // D3D9 query sync (append-only): same explicit positions as vk64bridge_abi.h.
    VK64_fn_vkCreateEvent = 841,
    VK64_fn_vkDestroyEvent,
    VK64_fn_vkSetEvent,
    VK64_fn_vkResetEvent,
    VK64_fn_vkGetEventStatus,
    VK64_fn_vkCmdSetEvent,
    VK64_fn_vkCmdResetEvent,
    VK64_fn_vkCmdWaitEvents,
    VK64_fn_vkCreateQueryPool,
    VK64_fn_vkDestroyQueryPool,
    VK64_fn_vkResetQueryPool,
    VK64_fn_vkGetQueryPoolResults,
    VK64_fn_vkCmdResetQueryPool,
    VK64_fn_vkCmdBeginQuery,
    VK64_fn_vkCmdEndQuery,
    // Core dynamic raster state. Append-only; old trap IDs stay unchanged.
    VK64_fn_vkCmdSetBlendConstants = 900,
    VK64_fn_vkCmdSetStencilCompareMask,
    VK64_fn_vkCmdSetStencilWriteMask,
    VK64_fn_vkCmdSetStencilReference,
    VK64_fn_vkCmdCopyQueryPoolResults = 904,
    VK64_fn_vkCmdSetDepthBias = 905,
    VK64_fn_vkCmdSetCullMode = 906,
    VK64_fn_vkCmdSetFrontFace = 907,
    VK64_fn_vkCmdSetEvent2 = 908,
    VK64_fn_vkCmdClearDepthStencilImage = 909,
    VK64_fn_vkCmdWriteTimestamp = 910,
    VK64_fn_vkCmdSetDepthBounds = 911,
    VK64_fn_vkResetDescriptorPool = 912,
    VK64_fn_vkFreeDescriptorSets = 913,
    VK64_fn_vkCmdPushDescriptorSetKHR = 914,
    VK64_fn_vkCmdPushDescriptorSetWithTemplateKHR = 915,
    VK64_fn_vkGetDescriptorSetLayoutSupport = 916,
    VK64_fn_vkGetDeviceImageMemoryRequirements = 917,
    VK64_fn_vkCmdSetExtendedDynamicState = 918,
    VK64_fn_vkCreateRenderPass2 = 919,
    VK64_fn_vkCmdNextSubpass = 920,
    VK64_fn__MAX
};

// ---- the trap ---------------------------------------------------------------
static inline uint64_t vk64_trap(uint64_t fnId, VK64Args* args) {
    uint32_t ret;
    register uint32_t ebx __asm__("ebx") = (uint32_t)fnId;
    register uint32_t ecx __asm__("ecx") = (uint32_t)(uintptr_t)args;
    __asm__ __volatile__(
        "int $0x80"
        : "=a"(ret)
        : "a"((uint32_t)VK64_SYSCALL_NR), "r"(ebx), "r"(ecx)
        : "memory");
    return (uint64_t)ret;
}

// Load-time witness: fire a trap with fnId=0 the moment the guest dlopen()s this
// libvulkan.so.1. The host bridge logs it as "vk64: FIRST trap". If that line
// never prints, the guest never loaded THIS library and every later failure is in
// the search path, not in the bridge — which is exactly the question P2 could not
// answer in advance (tasks/p1-final.md unknown #1).
__attribute__((constructor))
static void vk64_loaded_witness(void) {
    vk64_trap(0 /* sentinel: libvulkan loaded */, 0);
}

// The benign stub handed back for a vk* name we do not implement: it reports the
// name to the host (so the log is the worklist of what a real guest actually
// asks for — P1 measured the same thing with icd/loader_tally.c) and returns a
// failure code, never a fault.
static void* vk64_benign(const char* name) {
    VK64Args a = {{0}};
    a.a[0] = (uint64_t)(uintptr_t)name;
    (void)vk64_trap(VK64_fn_unimplemented, &a);
    return (void*)(intptr_t)(-8);   /* VK_ERROR_FEATURE_NOT_PRESENT */
}

#define API __attribute__((visibility("default")))

// ---- Vulkan types, declared only as wide as the ABI needs -------------------
// Handles and VkDeviceSize are 64-bit on the guest (x86-64), so unsigned long /
// unsigned long long are exact. No Vulkan header is included (and none is needed:
// every wrapper is a pass-through of scalars and pointers).
/* 32-bit Vulkan ABI: dispatchable handles are 32-bit pointers,
   non-dispatchable handles are 64-bit integers. */
typedef unsigned int        VkDispatchableHandle;  /* VkInstance, VkDevice, VkQueue, VkCommandBuffer */
typedef unsigned long long  VkNonDispatchableHandle; /* VkBuffer, VkImage, VkDeviceMemory, ... */
typedef unsigned long long  VkDeviceSize;
typedef unsigned int        VkFlags;
typedef int                 VkResult;
typedef unsigned int        VkBool32;

/* Dispatchable (32-bit) */
typedef VkDispatchableHandle VkInstance;
typedef VkDispatchableHandle VkPhysicalDevice;
typedef VkDispatchableHandle VkDevice;
typedef VkDispatchableHandle VkQueue;
typedef VkDispatchableHandle VkCommandPool;
typedef VkDispatchableHandle VkCommandBuffer;
/* Non-dispatchable (64-bit) */
typedef VkNonDispatchableHandle VkDeviceMemory;
typedef VkNonDispatchableHandle VkBuffer;
typedef VkNonDispatchableHandle VkImage;
typedef VkNonDispatchableHandle VkImageView;
typedef VkNonDispatchableHandle VkBufferView;
typedef VkNonDispatchableHandle VkSampler;
typedef VkNonDispatchableHandle VkShaderModule;
typedef VkNonDispatchableHandle VkDescriptorSetLayout;
typedef VkNonDispatchableHandle VkDescriptorPool;
typedef VkNonDispatchableHandle VkDescriptorSet;
typedef VkNonDispatchableHandle VkPipelineLayout;
typedef VkNonDispatchableHandle VkPipeline;
typedef VkNonDispatchableHandle VkRenderPass;
typedef VkNonDispatchableHandle VkFramebuffer;
typedef VkNonDispatchableHandle VkPipelineCache;
typedef VkNonDispatchableHandle VkFence;
typedef VkNonDispatchableHandle VkEvent;
typedef VkNonDispatchableHandle VkQueryPool;
typedef VkNonDispatchableHandle VkSemaphore;
typedef VkNonDispatchableHandle VkSurfaceKHR;
typedef VkNonDispatchableHandle VkSwapchainKHR;

#define VK_NULL_HANDLE ((VkHandle)0)
#define P(x) ((uint64_t)(uintptr_t)(x))

// ===========================================================================
// A. Queries and state answers (25 of the 85)
// ===========================================================================
API VkResult vkEnumerateInstanceExtensionProperties(const char* layer, uint32_t* count, void* props) {
    VK64Args a = {{0}}; a.a[0] = P(layer); a.a[1] = P(count); a.a[2] = P(props);
    return (VkResult)vk64_trap(VK64_fn_vkEnumerateInstanceExtensionProperties, &a);
}
API VkResult vkEnumerateInstanceLayerProperties(uint32_t* count, void* props) {
    VK64Args a = {{0}}; a.a[0] = P(count); a.a[1] = P(props);
    return (VkResult)vk64_trap(VK64_fn_vkEnumerateInstanceLayerProperties, &a);
}
API VkResult vkEnumeratePhysicalDevices(VkInstance inst, uint32_t* count, VkPhysicalDevice* devs) {
    VK64Args a = {{0}}; a.a[0] = inst; a.a[1] = P(count); a.a[2] = P(devs);
    return (VkResult)vk64_trap(VK64_fn_vkEnumeratePhysicalDevices, &a);
}
API VkResult vkEnumerateDeviceExtensionProperties(VkPhysicalDevice pd, const char* layer, uint32_t* count, void* props) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(layer); a.a[2] = P(count); a.a[3] = P(props);
    return (VkResult)vk64_trap(VK64_fn_vkEnumerateDeviceExtensionProperties, &a);
}
API void vkGetPhysicalDeviceProperties(VkPhysicalDevice pd, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceProperties, &a);
}
API void vkGetPhysicalDeviceProperties2(VkPhysicalDevice pd, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceProperties2, &a);
}
API void vkGetPhysicalDeviceFeatures(VkPhysicalDevice pd, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceFeatures, &a);
}
API void vkGetPhysicalDeviceFeatures2(VkPhysicalDevice pd, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceFeatures2, &a);
}
API void vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice pd, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceMemoryProperties, &a);
}
API void vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice pd, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceProperties2KHR, &a);
}
API void vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice pd, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceFeatures2KHR, &a);
}
API void vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice pd, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceMemoryProperties2KHR, &a);
}
API void vkGetPhysicalDeviceMemoryProperties2(VkPhysicalDevice pd, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceMemoryProperties2, &a);
}
API void vkGetPhysicalDeviceQueueFamilyProperties(VkPhysicalDevice pd, uint32_t* count, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(count); a.a[2] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties, &a);
}
API void vkGetPhysicalDeviceQueueFamilyProperties2(VkPhysicalDevice pd, uint32_t* count, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(count); a.a[2] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties2, &a);
}
API void vkGetPhysicalDeviceFormatProperties(VkPhysicalDevice pd, uint32_t format, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = format; a.a[2] = P(p);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceFormatProperties, &a);
}
API VkResult vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice pd, VkSurfaceKHR s, void* c) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = s; a.a[2] = P(c);
    return (VkResult)vk64_trap(VK64_fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR, &a);
}
API VkResult vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice pd, VkSurfaceKHR s, uint32_t* count, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = s; a.a[2] = P(count); a.a[3] = P(p);
    return (VkResult)vk64_trap(VK64_fn_vkGetPhysicalDeviceSurfaceFormatsKHR, &a);
}
API VkResult vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice pd, VkSurfaceKHR s, uint32_t* count, void* p) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = s; a.a[2] = P(count); a.a[3] = P(p);
    return (VkResult)vk64_trap(VK64_fn_vkGetPhysicalDeviceSurfacePresentModesKHR, &a);
}
API VkResult vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice pd, uint32_t qfam, VkSurfaceKHR s, VkBool32* ok) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = qfam; a.a[2] = s; a.a[3] = P(ok);
    return (VkResult)vk64_trap(VK64_fn_vkGetPhysicalDeviceSurfaceSupportKHR, &a);
}
API void vkGetDeviceQueue(VkDevice dev, uint32_t family, uint32_t index, VkQueue* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = family; a.a[2] = index; a.a[3] = P(out);
    (void)vk64_trap(VK64_fn_vkGetDeviceQueue, &a);
}
API void vkGetBufferMemoryRequirements(VkDevice dev, VkBuffer buf, void* r) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = buf; a.a[2] = P(r);
    (void)vk64_trap(VK64_fn_vkGetBufferMemoryRequirements, &a);
}
API void vkGetBufferMemoryRequirements2(VkDevice dev, const void* info, void* r) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(info); a.a[2] = P(r);
    (void)vk64_trap(VK64_fn_vkGetBufferMemoryRequirements2, &a);
}
API void vkGetDeviceImageMemoryRequirements(VkDevice dev, const void* info, void* r) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(info); a.a[2] = P(r);
    (void)vk64_trap(VK64_fn_vkGetDeviceImageMemoryRequirements, &a);
}
API void vkGetDeviceBufferMemoryRequirements(VkDevice dev, const void* info, void* r) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(info); a.a[2] = P(r);
    (void)vk64_trap(VK64_fn_vkGetDeviceBufferMemoryRequirements, &a);
}
API void vkGetImageMemoryRequirements(VkDevice dev, VkImage img, void* r) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = img; a.a[2] = P(r);
    (void)vk64_trap(VK64_fn_vkGetImageMemoryRequirements, &a);
}
API void vkGetImageMemoryRequirements2(VkDevice dev, const void* info, void* r) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(info); a.a[2] = P(r);
    (void)vk64_trap(VK64_fn_vkGetImageMemoryRequirements2, &a);
}
API void vkGetImageSubresourceLayout(VkDevice dev, VkImage img, const void* sub, void* layout) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = img; a.a[2] = P(sub); a.a[3] = P(layout);
    (void)vk64_trap(VK64_fn_vkGetImageSubresourceLayout, &a);
}
API VkResult vkGetSwapchainImagesKHR(VkDevice dev, VkSwapchainKHR sc, uint32_t* count, VkImage* imgs) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = sc; a.a[2] = P(count); a.a[3] = P(imgs);
    return (VkResult)vk64_trap(VK64_fn_vkGetSwapchainImagesKHR, &a);
}
API VkResult vkAcquireNextImageKHR(VkDevice dev, VkSwapchainKHR sc, uint64_t timeout,
                                  VkSemaphore sem, VkFence fence, uint32_t* idx) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = sc; a.a[2] = timeout; a.a[3] = sem; a.a[4] = fence; a.a[5] = P(idx);
    return (VkResult)vk64_trap(VK64_fn_vkAcquireNextImageKHR, &a);
}
API VkResult vkGetPastPresentationTimingGOOGLE(VkDevice dev, VkSwapchainKHR sc, uint32_t* count, void* p) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = sc; a.a[2] = P(count); a.a[3] = P(p);
    return (VkResult)vk64_trap(VK64_fn_vkGetPastPresentationTimingGOOGLE, &a);
}
API VkResult vkResetFences(VkDevice dev, uint32_t n, const VkFence* fences) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = n; a.a[2] = P(fences);
    return (VkResult)vk64_trap(VK64_fn_vkResetFences, &a);
}
API VkResult vkWaitForFences(VkDevice dev, uint32_t n, const VkFence* fences, VkBool32 waitAll, uint64_t timeout) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = n; a.a[2] = P(fences); a.a[3] = waitAll; a.a[4] = timeout;
    return (VkResult)vk64_trap(VK64_fn_vkWaitForFences, &a);
}
API VkResult vkGetFenceStatus(VkDevice dev, VkFence fence) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = fence;
    return (VkResult)vk64_trap(VK64_fn_vkGetFenceStatus, &a);
}
// D3D9 query sync: real bridge entry points (the benign stub faults Wine's
// Unix handler on the fake handle and trips the PE assert).
API VkResult vkCreateEvent(VkDevice dev, const void* ci, const void* alloc, VkEvent* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateEvent, &a);
}
API void vkDestroyEvent(VkDevice dev, VkEvent e, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = e; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyEvent, &a);
}
API VkResult vkSetEvent(VkDevice dev, VkEvent e) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = e;
    return (VkResult)vk64_trap(VK64_fn_vkSetEvent, &a);
}
API VkResult vkResetEvent(VkDevice dev, VkEvent e) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = e;
    return (VkResult)vk64_trap(VK64_fn_vkResetEvent, &a);
}
API VkResult vkGetEventStatus(VkDevice dev, VkEvent e) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = e;
    return (VkResult)vk64_trap(VK64_fn_vkGetEventStatus, &a);
}
API VkResult vkCreateQueryPool(VkDevice dev, const void* ci, const void* alloc, VkQueryPool* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateQueryPool, &a);
}
API void vkDestroyQueryPool(VkDevice dev, VkQueryPool q, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = q; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyQueryPool, &a);
}
API VkResult vkResetQueryPool(VkDevice dev, VkQueryPool q, uint32_t first, uint32_t count) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = q; a.a[2] = first; a.a[3] = count;
    return (VkResult)vk64_trap(VK64_fn_vkResetQueryPool, &a);
}
API VkResult vkGetQueryPoolResults(VkDevice dev, VkQueryPool q, uint32_t first, uint32_t count,
                                  unsigned long dataSize, void* data, uint64_t stride, VkFlags flags) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = q; a.a[2] = first; a.a[3] = count;
    a.a[4] = dataSize; a.a[5] = P(data); a.a[6] = stride; a.a[7] = flags;
    return (VkResult)vk64_trap(VK64_fn_vkGetQueryPoolResults, &a);
}
API VkResult vkDeviceWaitIdle(VkDevice dev) {
    VK64Args a = {{0}}; a.a[0] = dev;
    return (VkResult)vk64_trap(VK64_fn_vkDeviceWaitIdle, &a);
}
API VkResult vkQueueWaitIdle(VkQueue q) {
    VK64Args a = {{0}}; a.a[0] = q;
    return (VkResult)vk64_trap(VK64_fn_vkQueueWaitIdle, &a);
}
API VkResult vkResetCommandBuffer(VkCommandBuffer cb, VkFlags flags) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = flags;
    return (VkResult)vk64_trap(VK64_fn_vkResetCommandBuffer, &a);
}
API VkResult vkResetCommandPool(VkDevice dev, VkCommandPool pool, VkFlags flags) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = pool; a.a[2] = flags;
    return (VkResult)vk64_trap(VK64_fn_vkResetCommandPool, &a);
}
API void vkFreeCommandBuffers(VkDevice dev, VkCommandPool pool, uint32_t n, const VkCommandBuffer* cbs) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = pool; a.a[2] = n; a.a[3] = P(cbs);
    (void)vk64_trap(VK64_fn_vkFreeCommandBuffers, &a);
}

// ===========================================================================
// B. Creates (19 of the 85)
// ===========================================================================
API VkResult vkCreateInstance(const void* ci, const void* alloc, VkInstance* out) {
    VK64Args a = {{0}}; a.a[0] = P(ci); a.a[1] = P(alloc); a.a[2] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateInstance, &a);
}
API VkResult vkCreateDevice(VkPhysicalDevice pd, const void* ci, const void* alloc, VkDevice* out) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateDevice, &a);
}
API VkResult vkCreateXcbSurfaceKHR(VkInstance inst, const void* ci, const void* alloc, VkSurfaceKHR* out) {
    VK64Args a = {{0}}; a.a[0] = inst; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateXcbSurfaceKHR, &a);
}
/* The surface wine 8.x's winex11 Vulkan init asks for: X11DRV substitutes
 * VK_KHR_win32_surface -> VK_KHR_xlib_surface and builds its host-side surface
 * with vkCreateXlibSurfaceKHR through libvulkan.so.1 (== us). Same mint-an-id
 * arm as xcb/win32/headless: the create-info is never read. */
API VkResult vkCreateXlibSurfaceKHR(VkInstance inst, const void* ci, const void* alloc, VkSurfaceKHR* out) {
    VK64Args a = {{0}}; a.a[0] = inst; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateXlibSurfaceKHR, &a);
}
/* Returns VkBool32 (not VkResult): TRUE, every queue family presents through
 * the page tier's canvas. */
API VkBool32 vkGetPhysicalDeviceXlibPresentationSupportKHR(VkPhysicalDevice pd, uint32_t qfam, const void* dpy, unsigned long visual) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = qfam; a.a[2] = P(dpy); a.a[3] = (uint64_t)visual;
    return (VkBool32)vk64_trap(VK64_fn_vkGetPhysicalDeviceXlibPresentationSupportKHR, &a);
}
/* Returns VkBool32 (not VkResult): TRUE, every queue family presents through
 * the page tier's canvas -- same rationale as the Xlib variant. dxgi's
 * init_vk_funcs resolves this from vulkan-1.dll during swapchain setup. */
API VkBool32 vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice pd, uint32_t qfam) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = qfam;
    return (VkBool32)vk64_trap(VK64_fn_vkGetPhysicalDeviceWin32PresentationSupportKHR, &a);
}
/* DXVK's D3D9SwapChainEx::CreateBackBuffers gates every texture on
 * vkGetPhysicalDeviceImageFormatProperties2 (via DxvkAdapter::getFormatLimits):
 * a missing entry point used to fail the whole device. The create-info is
 * trusted; the limits are the bridge's advertised caps. */
API VkResult vkGetPhysicalDeviceImageFormatProperties2(VkPhysicalDevice pd, const void* info, void* props) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = P(info); a.a[2] = P(props);
    return (VkResult)vk64_trap(VK64_fn_vkGetPhysicalDeviceImageFormatProperties2, &a);
}
API VkResult vkGetPhysicalDeviceImageFormatProperties(VkPhysicalDevice pd, uint32_t format, uint32_t type,
        uint32_t tiling, uint32_t usage, uint32_t flags, void* props) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = format; a.a[2] = type; a.a[3] = tiling;
    a.a[4] = usage; a.a[5] = flags; a.a[6] = P(props);
    return (VkResult)vk64_trap(VK64_fn_vkGetPhysicalDeviceImageFormatProperties, &a);
}
/* DXVK's getFormatFeatures reads VkFormatProperties3 off this call's pNext. */
API void vkGetPhysicalDeviceFormatProperties2(VkPhysicalDevice pd, uint32_t format, void* props) {
    VK64Args a = {{0}}; a.a[0] = pd; a.a[1] = format; a.a[2] = P(props);
    (void)vk64_trap(VK64_fn_vkGetPhysicalDeviceFormatProperties2, &a);
}
API VkResult vkCreateHeadlessSurfaceEXT(VkInstance inst, const void* ci, const void* alloc, VkSurfaceKHR* out) {
    VK64Args a = {{0}}; a.a[0] = inst; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateHeadlessSurfaceEXT, &a);
}
/* The surface a wine-hosted DXVK actually creates: d3d11.dll -> vulkan-1.dll ->
 * winevulkan -> dlopen("libvulkan.so.1") == us, and the window belongs to
 * winevulkan, so it asks for a WIN32 surface. hwnd and the create-info travel as
 * an opaque guest pointer; the host mints an id without reading them (there is no
 * windowing system behind the handle, same as the xcb/headless arms). */
API VkResult vkCreateWin32SurfaceKHR(VkInstance inst, const void* ci, const void* alloc, VkSurfaceKHR* out) {
    VK64Args a = {{0}}; a.a[0] = inst; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateWin32SurfaceKHR, &a);
}
API VkResult vkCreateSwapchainKHR(VkDevice dev, const void* ci, const void* alloc, VkSwapchainKHR* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateSwapchainKHR, &a);
}
API VkResult vkCreateCommandPool(VkDevice dev, const void* ci, const void* alloc, VkCommandPool* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateCommandPool, &a);
}
API VkResult vkCreateBuffer(VkDevice dev, const void* ci, const void* alloc, VkBuffer* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateBuffer, &a);
}
API VkResult vkCreateImage(VkDevice dev, const void* ci, const void* alloc, VkImage* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateImage, &a);
}
API VkResult vkCreateImageView(VkDevice dev, const void* ci, const void* alloc, VkImageView* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateImageView, &a);
}
API VkResult vkCreateBufferView(VkDevice dev, const void* ci, const void* alloc, VkBufferView* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateBufferView, &a);
}
API VkResult vkCreateSampler(VkDevice dev, const void* ci, const void* alloc, VkSampler* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateSampler, &a);
}
API VkResult vkCreateRenderPass(VkDevice dev, const void* ci, const void* alloc, VkRenderPass* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateRenderPass, &a);
}
API VkResult vkCreateRenderPass2(VkDevice dev, const void* ci, const void* alloc, VkRenderPass* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateRenderPass2, &a);
}
API VkResult vkCreateRenderPass2KHR(VkDevice dev, const void* ci, const void* alloc, VkRenderPass* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateRenderPass2, &a);
}
API VkResult vkCreateFramebuffer(VkDevice dev, const void* ci, const void* alloc, VkFramebuffer* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateFramebuffer, &a);
}
API VkResult vkCreateGraphicsPipelines(VkDevice dev, VkPipelineCache cache, uint32_t n,
                                      const void* cis, const void* alloc, VkPipeline* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = cache; a.a[2] = n; a.a[3] = P(cis); a.a[4] = P(alloc); a.a[5] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateGraphicsPipelines, &a);
}
API VkResult vkCreateComputePipelines(VkDevice dev, VkPipelineCache cache, uint32_t n,
                                     const void* cis, const void* alloc, VkPipeline* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = cache; a.a[2] = n; a.a[3] = P(cis); a.a[4] = P(alloc); a.a[5] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateComputePipelines, &a);
}
API VkResult vkCreatePipelineLayout(VkDevice dev, const void* ci, const void* alloc, VkPipelineLayout* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreatePipelineLayout, &a);
}
API VkResult vkCreateDescriptorSetLayout(VkDevice dev, const void* ci, const void* alloc, VkDescriptorSetLayout* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateDescriptorSetLayout, &a);
}
API VkResult vkCreateDescriptorPool(VkDevice dev, const void* ci, const void* alloc, VkDescriptorPool* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateDescriptorPool, &a);
}
API VkResult vkResetDescriptorPool(VkDevice dev, VkDescriptorPool pool, VkFlags flags) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = pool; a.a[2] = flags;
    return (VkResult)vk64_trap(VK64_fn_vkResetDescriptorPool, &a);
}
API VkResult vkFreeDescriptorSets(VkDevice dev, VkDescriptorPool pool, uint32_t count, const void* sets) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = pool; a.a[2] = count; a.a[3] = P(sets);
    return (VkResult)vk64_trap(VK64_fn_vkFreeDescriptorSets, &a);
}
API void vkCmdPushDescriptorSetKHR(VkCommandBuffer cb, uint32_t bindPoint, VkPipelineLayout layout, uint32_t set, uint32_t writeCount, const void* writes) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = bindPoint; a.a[2] = layout; a.a[3] = set; a.a[4] = writeCount; a.a[5] = P(writes);
    (void)vk64_trap(VK64_fn_vkCmdPushDescriptorSetKHR, &a);
}
API void vkCmdPushDescriptorSet(VkCommandBuffer cb, uint32_t bindPoint, VkPipelineLayout layout, uint32_t set, uint32_t writeCount, const void* writes) {
    vkCmdPushDescriptorSetKHR(cb, bindPoint, layout, set, writeCount, writes);
}
API void vkCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer cb, VkNonDispatchableHandle tmpl, VkPipelineLayout layout, uint32_t set, const void* data) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = tmpl; a.a[2] = layout; a.a[3] = set; a.a[4] = P(data);
    (void)vk64_trap(VK64_fn_vkCmdPushDescriptorSetWithTemplateKHR, &a);
}
API void vkCmdPushDescriptorSetWithTemplate(VkCommandBuffer cb, VkNonDispatchableHandle tmpl, VkPipelineLayout layout, uint32_t set, const void* data) {
    vkCmdPushDescriptorSetWithTemplateKHR(cb, tmpl, layout, set, data);
}
API void vkGetDescriptorSetLayoutSupport(VkDevice dev, const void* ci, void* support) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(support);
    (void)vk64_trap(VK64_fn_vkGetDescriptorSetLayoutSupport, &a);
}
API void vkGetDescriptorSetLayoutSupportKHR(VkDevice dev, const void* ci, void* support) {
    vkGetDescriptorSetLayoutSupport(dev, ci, support);
}
API VkResult vkCreateShaderModule(VkDevice dev, const void* ci, const void* alloc, VkShaderModule* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateShaderModule, &a);
}
API VkResult vkCreatePipelineCache(VkDevice dev, const void* ci, const void* alloc, VkPipelineCache* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreatePipelineCache, &a);
}
API VkResult vkCreateFence(VkDevice dev, const void* ci, const void* alloc, VkFence* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateFence, &a);
}
API VkResult vkCreateSemaphore(VkDevice dev, const void* ci, const void* alloc, VkSemaphore* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateSemaphore, &a);
}
// The timeline-semaphore quartet. DXVK signals a u64 fence on every
// command-list flush and reads it back with vkGetSemaphoreCounterValue, so
// without these three the first flush dies in the benign tail. The wait/signal
// info structs are opaque guest pointers, as everywhere else in this shim.
API VkResult vkGetSemaphoreCounterValue(VkDevice dev, VkSemaphore sem, uint64_t* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = sem; a.a[2] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkGetSemaphoreCounterValue, &a);
}
API VkResult vkWaitSemaphores(VkDevice dev, const void* waitInfo, uint64_t timeout) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(waitInfo); a.a[2] = timeout;
    return (VkResult)vk64_trap(VK64_fn_vkWaitSemaphores, &a);
}
API VkResult vkSignalSemaphore(VkDevice dev, const void* signalInfo) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(signalInfo);
    return (VkResult)vk64_trap(VK64_fn_vkSignalSemaphore, &a);
}

// ===========================================================================
// C. Allocation / binding / update / map (8 of the 85)
// ===========================================================================
API VkResult vkAllocateMemory(VkDevice dev, const void* ai, const void* alloc, VkDeviceMemory* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ai); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkAllocateMemory, &a);
}
API VkResult vkAllocateCommandBuffers(VkDevice dev, const void* ai, VkCommandBuffer* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ai); a.a[2] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkAllocateCommandBuffers, &a);
}
API VkResult vkAllocateDescriptorSets(VkDevice dev, const void* ai, VkDescriptorSet* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ai); a.a[2] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkAllocateDescriptorSets, &a);
}
API VkResult vkBindBufferMemory(VkDevice dev, VkBuffer buf, VkDeviceMemory mem, VkDeviceSize off) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = buf; a.a[2] = mem; a.a[3] = off;
    return (VkResult)vk64_trap(VK64_fn_vkBindBufferMemory, &a);
}
API VkResult vkBindImageMemory(VkDevice dev, VkImage img, VkDeviceMemory mem, VkDeviceSize off) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = img; a.a[2] = mem; a.a[3] = off;
    return (VkResult)vk64_trap(VK64_fn_vkBindImageMemory, &a);
}
API VkResult vkBindBufferMemory2(VkDevice dev, uint32_t n, const void* infos) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = n; a.a[2] = P(infos);
    return (VkResult)vk64_trap(VK64_fn_vkBindBufferMemory2, &a);
}
API VkResult vkBindBufferMemory2KHR(VkDevice dev, uint32_t n, const void* infos) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = n; a.a[2] = P(infos);
    return (VkResult)vk64_trap(VK64_fn_vkBindBufferMemory2KHR, &a);
}
API VkResult vkBindImageMemory2(VkDevice dev, uint32_t n, const void* infos) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = n; a.a[2] = P(infos);
    return (VkResult)vk64_trap(VK64_fn_vkBindImageMemory2, &a);
}
API VkResult vkBindImageMemory2KHR(VkDevice dev, uint32_t n, const void* infos) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = n; a.a[2] = P(infos);
    return (VkResult)vk64_trap(VK64_fn_vkBindImageMemory2KHR, &a);
}
API void vkUpdateDescriptorSets(VkDevice dev, uint32_t nw, const void* writes,
                                uint32_t nc, const void* copies) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = nw; a.a[2] = P(writes); a.a[3] = nc; a.a[4] = P(copies);
    (void)vk64_trap(VK64_fn_vkUpdateDescriptorSets, &a);
}
API VkResult vkMapMemory(VkDevice dev, VkDeviceMemory mem, VkDeviceSize off, VkDeviceSize size,
                         VkFlags flags, void** pp) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = mem; a.a[2] = off; a.a[3] = size; a.a[4] = flags; a.a[5] = P(pp);
    return (VkResult)vk64_trap(VK64_fn_vkMapMemory, &a);
}
API VkResult vkFlushMappedMemoryRanges(VkDevice dev, uint32_t count, const void* ranges) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = count; a.a[2] = P(ranges);
    return (VkResult)vk64_trap(VK64_fn_vkFlushMappedMemoryRanges, &a);
}
API VkResult vkInvalidateMappedMemoryRanges(VkDevice dev, uint32_t count, const void* ranges) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = count; a.a[2] = P(ranges);
    return (VkResult)vk64_trap(VK64_fn_vkInvalidateMappedMemoryRanges, &a);
}
API void vkUnmapMemory(VkDevice dev, VkDeviceMemory mem) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = mem;
    (void)vk64_trap(VK64_fn_vkUnmapMemory, &a);
}

// ===========================================================================
// D. Destroys (20 of the 85)
// ===========================================================================
API void vkDestroyInstance(VkInstance inst, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = inst; a.a[1] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyInstance, &a);
}
API void vkDestroyDevice(VkDevice dev, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyDevice, &a);
}
API void vkDestroySurfaceKHR(VkInstance inst, VkSurfaceKHR s, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = inst; a.a[1] = s; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroySurfaceKHR, &a);
}
API void vkDestroySwapchainKHR(VkDevice dev, VkSwapchainKHR sc, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = sc; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroySwapchainKHR, &a);
}
API void vkDestroyCommandPool(VkDevice dev, VkCommandPool pool, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = pool; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyCommandPool, &a);
}
API void vkDestroyBuffer(VkDevice dev, VkBuffer buf, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = buf; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyBuffer, &a);
}
API void vkDestroyImage(VkDevice dev, VkImage img, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = img; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyImage, &a);
}
API void vkDestroyImageView(VkDevice dev, VkImageView v, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = v; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyImageView, &a);
}
API void vkDestroySampler(VkDevice dev, VkSampler s, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = s; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroySampler, &a);
}
API void vkDestroyRenderPass(VkDevice dev, VkRenderPass rp, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = rp; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyRenderPass, &a);
}
API void vkDestroyFramebuffer(VkDevice dev, VkFramebuffer fb, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = fb; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyFramebuffer, &a);
}
API void vkDestroyPipeline(VkDevice dev, VkPipeline p, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = p; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyPipeline, &a);
}
API void vkDestroyPipelineLayout(VkDevice dev, VkPipelineLayout l, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = l; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyPipelineLayout, &a);
}
API void vkDestroyDescriptorSetLayout(VkDevice dev, VkDescriptorSetLayout l, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = l; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyDescriptorSetLayout, &a);
}
API void vkDestroyDescriptorPool(VkDevice dev, VkDescriptorPool p, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = p; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyDescriptorPool, &a);
}
API void vkDestroyBufferView(VkDevice dev, VkBufferView v, const void* alloc) {
    (void)dev; (void)v; (void)alloc;
}
API void vkDestroyShaderModule(VkDevice dev, VkShaderModule m, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = m; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyShaderModule, &a);
}
API void vkDestroyPipelineCache(VkDevice dev, VkPipelineCache c, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = c; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyPipelineCache, &a);
}
API void vkDestroyFence(VkDevice dev, VkFence f, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = f; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyFence, &a);
}
API void vkDestroySemaphore(VkDevice dev, VkSemaphore s, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = s; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroySemaphore, &a);
}
API void vkFreeMemory(VkDevice dev, VkDeviceMemory mem, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = mem; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkFreeMemory, &a);
}

// ===========================================================================
// E. Command recording (11 of the 85)
// ===========================================================================
API VkResult vkBeginCommandBuffer(VkCommandBuffer cb, const void* bi) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(bi);
    return (VkResult)vk64_trap(VK64_fn_vkBeginCommandBuffer, &a);
}
API VkResult vkEndCommandBuffer(VkCommandBuffer cb) {
    VK64Args a = {{0}}; a.a[0] = cb;
    return (VkResult)vk64_trap(VK64_fn_vkEndCommandBuffer, &a);
}
API void vkCmdBeginRenderPass(VkCommandBuffer cb, const void* bi, uint32_t contents) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(bi); a.a[2] = contents;
    (void)vk64_trap(VK64_fn_vkCmdBeginRenderPass, &a);
}
API void vkCmdEndRenderPass(VkCommandBuffer cb) {
    VK64Args a = {{0}}; a.a[0] = cb;
    (void)vk64_trap(VK64_fn_vkCmdEndRenderPass, &a);
}
API void vkCmdBeginRenderPass2(VkCommandBuffer cb, const void* bi, const void* di) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(bi); a.a[2] = P(di);
    (void)vk64_trap(VK64_fn_vkCmdBeginRenderPass2, &a);
}
API void vkCmdBeginRenderPass2KHR(VkCommandBuffer cb, const void* bi, const void* di) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(bi); a.a[2] = P(di);
    (void)vk64_trap(VK64_fn_vkCmdBeginRenderPass2KHR, &a);
}
API void vkCmdEndRenderPass2(VkCommandBuffer cb, const void* di) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(di);
    (void)vk64_trap(VK64_fn_vkCmdEndRenderPass2, &a);
}
API void vkCmdNextSubpass(VkCommandBuffer cb, uint32_t contents) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = contents;
    (void)vk64_trap(VK64_fn_vkCmdNextSubpass, &a);
}
API void vkCmdNextSubpassKHR(VkCommandBuffer cb, uint32_t contents) { vkCmdNextSubpass(cb, contents); }
API void vkCmdNextSubpass2(VkCommandBuffer cb, const void* bi, const void* ei) {
    (void)ei; vkCmdNextSubpass(cb, 0); (void)bi;
}
API void vkCmdNextSubpass2KHR(VkCommandBuffer cb, const void* bi, const void* ei) { vkCmdNextSubpass2(cb, bi, ei); }
API void vkCmdEndRenderPass2KHR(VkCommandBuffer cb, const void* di) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(di);
    (void)vk64_trap(VK64_fn_vkCmdEndRenderPass2KHR, &a);
}
API void vkCmdPipelineBarrier2(VkCommandBuffer cb, const void* di) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(di);
    (void)vk64_trap(VK64_fn_vkCmdPipelineBarrier2, &a);
}
API void vkCmdPipelineBarrier2KHR(VkCommandBuffer cb, const void* di) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(di);
    (void)vk64_trap(VK64_fn_vkCmdPipelineBarrier2KHR, &a);
}
API void vkCmdPushConstants2(VkCommandBuffer cb, const void* info) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(info);
    (void)vk64_trap(VK64_fn_vkCmdPushConstants2, &a);
}
API void vkCmdPushConstants2KHR(VkCommandBuffer cb, const void* info) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(info);
    (void)vk64_trap(VK64_fn_vkCmdPushConstants2KHR, &a);
}
API void vkCmdBindIndexBuffer2(VkCommandBuffer cb, uint64_t buf, uint64_t off, uint64_t size, uint32_t idxType) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = buf; a.a[2] = off; a.a[3] = size; a.a[4] = idxType;
    (void)vk64_trap(VK64_fn_vkCmdBindIndexBuffer2, &a);
}
API void vkCmdBindIndexBuffer2KHR(VkCommandBuffer cb, uint64_t buf, uint64_t off, uint64_t size, uint32_t idxType) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = buf; a.a[2] = off; a.a[3] = size; a.a[4] = idxType;
    (void)vk64_trap(VK64_fn_vkCmdBindIndexBuffer2KHR, &a);
}
API void vkCmdBeginRendering(VkCommandBuffer cb, const void* ri) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(ri);
    (void)vk64_trap(VK64_fn_vkCmdBeginRendering, &a);
}
API void vkCmdBeginRenderingKHR(VkCommandBuffer cb, const void* ri) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(ri);
    (void)vk64_trap(VK64_fn_vkCmdBeginRenderingKHR, &a);
}
API void vkCmdEndRendering(VkCommandBuffer cb) {
    VK64Args a = {{0}}; a.a[0] = cb;
    (void)vk64_trap(VK64_fn_vkCmdEndRendering, &a);
}
API void vkCmdEndRenderingKHR(VkCommandBuffer cb) {
    VK64Args a = {{0}}; a.a[0] = cb;
    (void)vk64_trap(VK64_fn_vkCmdEndRenderingKHR, &a);
}
API void vkCmdBindPipeline(VkCommandBuffer cb, uint32_t bindPoint, VkPipeline p) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = bindPoint; a.a[2] = p;
    (void)vk64_trap(VK64_fn_vkCmdBindPipeline, &a);
}
API void vkCmdBindDescriptorSets(VkCommandBuffer cb, uint32_t bindPoint, VkPipelineLayout layout,
                                 uint32_t firstSet, uint32_t setCount, const VkDescriptorSet* sets,
                                 uint32_t dynOffCount, const uint32_t* dynOffs) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = bindPoint; a.a[2] = layout; a.a[3] = firstSet; a.a[4] = setCount;
    a.a[5] = P(sets); a.a[6] = dynOffCount; a.a[7] = P(dynOffs);
    (void)vk64_trap(VK64_fn_vkCmdBindDescriptorSets, &a);
}
API void vkCmdSetBlendConstants(VkCommandBuffer cb, const float* values) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(values);
    (void)vk64_trap(VK64_fn_vkCmdSetBlendConstants, &a);
}
API void vkCmdSetDepthBias(VkCommandBuffer cb, float constantFactor, float clamp, float slopeFactor) {
    union { float f; uint32_t u; } c, k, s; c.f=constantFactor; k.f=clamp; s.f=slopeFactor;
    VK64Args a = {{0}}; a.a[0]=cb; a.a[1]=c.u; a.a[2]=k.u; a.a[3]=s.u;
    (void)vk64_trap(VK64_fn_vkCmdSetDepthBias, &a);
}
API void vkCmdSetCullMode(VkCommandBuffer cb, uint32_t mode) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = mode;
    (void)vk64_trap(VK64_fn_vkCmdSetCullMode, &a);
}
API void vkCmdSetCullModeEXT(VkCommandBuffer cb, uint32_t mode) { vkCmdSetCullMode(cb, mode); }
API void vkCmdSetFrontFace(VkCommandBuffer cb, uint32_t face) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = face;
    (void)vk64_trap(VK64_fn_vkCmdSetFrontFace, &a);
}
API void vkCmdSetFrontFaceEXT(VkCommandBuffer cb, uint32_t face) { vkCmdSetFrontFace(cb, face); }
static void vk64_setDyn(VkCommandBuffer cb, uint32_t which, uint32_t value) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = which; a.a[2] = value;
    (void)vk64_trap(VK64_fn_vkCmdSetExtendedDynamicState, &a);
}
API void vkCmdSetPrimitiveTopology(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 2, v); }
API void vkCmdSetPrimitiveTopologyEXT(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 2, v); }
API void vkCmdSetDepthTestEnable(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 3, v); }
API void vkCmdSetDepthTestEnableEXT(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 3, v); }
API void vkCmdSetDepthWriteEnable(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 4, v); }
API void vkCmdSetDepthWriteEnableEXT(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 4, v); }
API void vkCmdSetDepthCompareOp(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 5, v); }
API void vkCmdSetDepthCompareOpEXT(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 5, v); }
API void vkCmdSetStencilTestEnable(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 6, v); }
API void vkCmdSetStencilTestEnableEXT(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 6, v); }
API void vkCmdSetDepthBiasEnable(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 7, v); }
API void vkCmdSetDepthBiasEnableEXT(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 7, v); }
API void vkCmdSetRasterizerDiscardEnable(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 8, v); }
API void vkCmdSetRasterizerDiscardEnableEXT(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 8, v); }
API void vkCmdSetPrimitiveRestartEnable(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 9, v); }
API void vkCmdSetPrimitiveRestartEnableEXT(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 9, v); }
API void vkCmdSetDepthBoundsTestEnable(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 11, v); }
API void vkCmdSetDepthBoundsTestEnableEXT(VkCommandBuffer cb, uint32_t v) { vk64_setDyn(cb, 11, v); }
API void vkCmdSetStencilOp(VkCommandBuffer cb, uint32_t faceMask, uint32_t failOp, uint32_t passOp, uint32_t depthFailOp, uint32_t compareOp) {
    vk64_setDyn(cb, 10, (faceMask & 3u) | ((failOp & 7u) << 4) | ((passOp & 7u) << 8) | ((depthFailOp & 7u) << 12) | ((compareOp & 7u) << 16));
}
API void vkCmdSetStencilOpEXT(VkCommandBuffer cb, uint32_t faceMask, uint32_t failOp, uint32_t passOp, uint32_t depthFailOp, uint32_t compareOp) {
    vkCmdSetStencilOp(cb, faceMask, failOp, passOp, depthFailOp, compareOp);
}
API void vkCmdSetDepthBounds(VkCommandBuffer cb, float lo, float hi) {
    union { float f; uint32_t u; } l, h; l.f = lo; h.f = hi;
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = l.u; a.a[2] = h.u;
    (void)vk64_trap(VK64_fn_vkCmdSetDepthBounds, &a);
}
API void vkCmdSetEvent2(VkCommandBuffer cb, VkEvent e, const void* dependencyInfo) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = e; a.a[2] = P(dependencyInfo);
    (void)vk64_trap(VK64_fn_vkCmdSetEvent2, &a);
}
API void vkCmdSetEvent2KHR(VkCommandBuffer cb, VkEvent e, const void* dependencyInfo) { vkCmdSetEvent2(cb, e, dependencyInfo); }
// Immediate execution: reset/wait collapse to the same bookkeeping as the v1 entry points.
API void vkCmdResetEvent2(VkCommandBuffer cb, VkEvent e, uint64_t stages) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = e; a.a[2] = (uint32_t)stages;
    (void)vk64_trap(VK64_fn_vkCmdResetEvent, &a);
}
API void vkCmdResetEvent2KHR(VkCommandBuffer cb, VkEvent e, uint64_t stages) { vkCmdResetEvent2(cb, e, stages); }
API void vkCmdWaitEvents2(VkCommandBuffer cb, uint32_t n, const VkEvent* events, const void* dependencyInfos) {
    (void)events; (void)dependencyInfos;
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = n;
    (void)vk64_trap(VK64_fn_vkCmdWaitEvents, &a);
}
API void vkCmdWaitEvents2KHR(VkCommandBuffer cb, uint32_t n, const VkEvent* events, const void* dependencyInfos) { vkCmdWaitEvents2(cb, n, events, dependencyInfos); }
API void vkCmdWriteTimestamp(VkCommandBuffer cb, uint32_t stage, VkQueryPool pool, uint32_t query) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = stage; a.a[2] = pool; a.a[3] = query;
    (void)vk64_trap(VK64_fn_vkCmdWriteTimestamp, &a);
}
API void vkCmdWriteTimestamp2(VkCommandBuffer cb, uint64_t stage, VkQueryPool pool, uint32_t query) {
    vkCmdWriteTimestamp(cb, (uint32_t)stage, pool, query);
}
API void vkCmdWriteTimestamp2KHR(VkCommandBuffer cb, uint64_t stage, VkQueryPool pool, uint32_t query) {
    vkCmdWriteTimestamp(cb, (uint32_t)stage, pool, query);
}
API void vkCmdSetStencilCompareMask(VkCommandBuffer cb, uint32_t faces, uint32_t value) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = faces; a.a[2] = value;
    (void)vk64_trap(VK64_fn_vkCmdSetStencilCompareMask, &a);
}
API void vkCmdSetStencilWriteMask(VkCommandBuffer cb, uint32_t faces, uint32_t value) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = faces; a.a[2] = value;
    (void)vk64_trap(VK64_fn_vkCmdSetStencilWriteMask, &a);
}
API void vkCmdSetStencilReference(VkCommandBuffer cb, uint32_t faces, uint32_t value) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = faces; a.a[2] = value;
    (void)vk64_trap(VK64_fn_vkCmdSetStencilReference, &a);
}
API void vkCmdSetViewport(VkCommandBuffer cb, uint32_t first, uint32_t count, const void* viewports) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = first; a.a[2] = count; a.a[3] = P(viewports);
    (void)vk64_trap(VK64_fn_vkCmdSetViewport, &a);
}
API void vkCmdSetScissor(VkCommandBuffer cb, uint32_t first, uint32_t count, const void* scissors) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = first; a.a[2] = count; a.a[3] = P(scissors);
    (void)vk64_trap(VK64_fn_vkCmdSetScissor, &a);
}
// The recorder/page currently replay viewport 0 only. These forms start at 0,
// so share the existing recording path; do not imply multiViewport support.
API void vkCmdSetViewportWithCount(VkCommandBuffer cb, uint32_t count, const void* viewports) {
    vkCmdSetViewport(cb, 0, count, viewports);
}
API void vkCmdSetViewportWithCountEXT(VkCommandBuffer cb, uint32_t count, const void* viewports) {
    vkCmdSetViewportWithCount(cb, count, viewports);
}
API void vkCmdSetScissorWithCount(VkCommandBuffer cb, uint32_t count, const void* scissors) {
    vkCmdSetScissor(cb, 0, count, scissors);
}
API void vkCmdSetScissorWithCountEXT(VkCommandBuffer cb, uint32_t count, const void* scissors) {
    vkCmdSetScissorWithCount(cb, count, scissors);
}
API void vkCmdDraw(VkCommandBuffer cb, uint32_t vertexCount, uint32_t instanceCount,
                   uint32_t firstVertex, uint32_t firstInstance) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = vertexCount; a.a[2] = instanceCount; a.a[3] = firstVertex; a.a[4] = firstInstance;
    (void)vk64_trap(VK64_fn_vkCmdDraw, &a);
}
/* DXVK draws ~100% indexed: without this the manifest carries no draws. */
API void vkCmdDrawIndexed(VkCommandBuffer cb, uint32_t indexCount, uint32_t instanceCount,
                          uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = indexCount; a.a[2] = instanceCount; a.a[3] = firstIndex;
    a.a[4] = (uint64_t)vertexOffset; a.a[5] = firstInstance;
    (void)vk64_trap(VK64_fn_vkCmdDrawIndexed, &a);
}
API void vkCmdBindVertexBuffers(VkCommandBuffer cb, uint32_t firstBinding, uint32_t bindingCount,
                                const VkBuffer* buffers, const uint64_t* offsets) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = firstBinding; a.a[2] = bindingCount; a.a[3] = P(buffers); a.a[4] = P(offsets);
    (void)vk64_trap(VK64_fn_vkCmdBindVertexBuffers, &a);
}
API void vkCmdBindVertexBuffers2(VkCommandBuffer cb, uint32_t first, uint32_t count,
    const VkBuffer* buffers, const VkDeviceSize* offsets, const VkDeviceSize* sizes,
    const VkDeviceSize* strides) {
    VK64Args a = {{(uint64_t)cb, first, count, (uint64_t)(uintptr_t)buffers,
        (uint64_t)(uintptr_t)offsets, (uint64_t)(uintptr_t)sizes, (uint64_t)(uintptr_t)strides}};
    (void)vk64_trap(VK64_fn_vkCmdBindVertexBuffers2, &a);
}
API void vkCmdBindVertexBuffers2KHR(VkCommandBuffer cb, uint32_t first, uint32_t count,
    const VkBuffer* buffers, const VkDeviceSize* offsets, const VkDeviceSize* sizes,
    const VkDeviceSize* strides) {
    vkCmdBindVertexBuffers2(cb, first, count, buffers, offsets, sizes, strides);
}
API void vkCmdBindVertexBuffers2EXT(VkCommandBuffer cb, uint32_t first, uint32_t count,
    const VkBuffer* buffers, const VkDeviceSize* offsets, const VkDeviceSize* sizes,
    const VkDeviceSize* strides) {
    vkCmdBindVertexBuffers2(cb, first, count, buffers, offsets, sizes, strides);
}
API void vkCmdBindIndexBuffer(VkCommandBuffer cb, VkBuffer buffer, uint64_t offset, uint32_t indexType) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = buffer; a.a[2] = offset; a.a[3] = indexType;
    (void)vk64_trap(VK64_fn_vkCmdBindIndexBuffer, &a);
}
API void vkCmdPushConstants(VkCommandBuffer cb, VkPipelineLayout layout, VkFlags stageFlags,
                            uint32_t offset, uint32_t size, const void* values) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = layout; a.a[2] = stageFlags; a.a[3] = offset; a.a[4] = size; a.a[5] = P(values);
    (void)vk64_trap(VK64_fn_vkCmdPushConstants, &a);
}
API void vkCmdSetEvent(VkCommandBuffer cb, VkEvent e, VkFlags stages) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = e; a.a[2] = stages;
    (void)vk64_trap(VK64_fn_vkCmdSetEvent, &a);
}
API void vkCmdResetEvent(VkCommandBuffer cb, VkEvent e, VkFlags stages) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = e; a.a[2] = stages;
    (void)vk64_trap(VK64_fn_vkCmdResetEvent, &a);
}
API void vkCmdWaitEvents(VkCommandBuffer cb, uint32_t nEv, const void* events,
                         VkFlags src, VkFlags dst, uint32_t nMem, const void* memBars,
                         uint32_t nBuf, const void* bufBars, uint32_t nImg, const void* imgBars) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = nEv; a.a[2] = P(events); a.a[3] = src; a.a[4] = dst;
    a.a[5] = nMem; a.a[6] = P(memBars); a.a[7] = nBuf; a.a[8] = P(bufBars);
    a.a[9] = nImg; a.a[10] = P(imgBars);
    (void)vk64_trap(VK64_fn_vkCmdWaitEvents, &a);
}
API void vkCmdResetQueryPool(VkCommandBuffer cb, VkQueryPool q, uint32_t first, uint32_t count) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = q; a.a[2] = first; a.a[3] = count;
    (void)vk64_trap(VK64_fn_vkCmdResetQueryPool, &a);
}
API void vkCmdBeginQuery(VkCommandBuffer cb, VkQueryPool q, uint32_t query, VkFlags flags) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = q; a.a[2] = query; a.a[3] = flags;
    (void)vk64_trap(VK64_fn_vkCmdBeginQuery, &a);
}
API void vkCmdEndQuery(VkCommandBuffer cb, VkQueryPool q, uint32_t query) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = q; a.a[2] = query;
    (void)vk64_trap(VK64_fn_vkCmdEndQuery, &a);
}
API void vkCmdCopyQueryPoolResults(VkCommandBuffer cb, VkQueryPool q, uint32_t first, uint32_t count,
                                   VkBuffer dst, uint64_t offset, uint64_t stride, VkFlags flags) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = q; a.a[2] = first; a.a[3] = count;
    a.a[4] = dst; a.a[5] = offset; a.a[6] = stride; a.a[7] = flags;
    (void)vk64_trap(VK64_fn_vkCmdCopyQueryPoolResults, &a);
}
API void vkCmdUpdateBuffer(VkCommandBuffer cb, VkBuffer dst, uint64_t off,
                           uint64_t size, const void* data) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = dst; a.a[2] = off; a.a[3] = size; a.a[4] = P(data);
    (void)vk64_trap(VK64_fn_vkCmdUpdateBuffer, &a);
}
API void vkCmdPipelineBarrier(VkCommandBuffer cb, VkFlags srcStage, VkFlags dstStage,
                              VkFlags depFlags, uint32_t nmc, const void* pmc,
                              uint32_t nbc, const void* pbc, uint32_t nic, const void* pic) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = srcStage; a.a[2] = dstStage; a.a[3] = depFlags;
    a.a[4] = nmc; a.a[5] = P(pmc); a.a[6] = nbc; a.a[7] = P(pbc); a.a[8] = nic; a.a[9] = P(pic);
    (void)vk64_trap(VK64_fn_vkCmdPipelineBarrier, &a);
}
API void vkCmdCopyBufferToImage(VkCommandBuffer cb, VkBuffer src, VkImage dst, uint32_t layout,
                                uint32_t nRegions, const void* regions) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = src; a.a[2] = dst; a.a[3] = layout; a.a[4] = nRegions; a.a[5] = P(regions);
    (void)vk64_trap(VK64_fn_vkCmdCopyBufferToImage, &a);
}

// DXVK's staging uploads use copy-commands2. These must dispatch to the
// bridge: a callable benign stub leaves the destination textures all zero.
API void vkCmdCopyBuffer(VkCommandBuffer cb, VkBuffer src, VkBuffer dst,
                         uint32_t nRegions, const void* regions) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = src; a.a[2] = dst; a.a[3] = nRegions; a.a[4] = P(regions);
    (void)vk64_trap(VK64_fn_vkCmdCopyBuffer, &a);
}
API void vkCmdCopyBuffer2(VkCommandBuffer cb, const void* info) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(info);
    (void)vk64_trap(VK64_fn_vkCmdCopyBuffer2, &a);
}
API void vkCmdCopyBuffer2KHR(VkCommandBuffer cb, const void* info) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(info);
    (void)vk64_trap(VK64_fn_vkCmdCopyBuffer2KHR, &a);
}
API void vkCmdCopyBufferToImage2(VkCommandBuffer cb, const void* info) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(info);
    (void)vk64_trap(VK64_fn_vkCmdCopyBufferToImage2, &a);
}
API void vkCmdCopyBufferToImage2KHR(VkCommandBuffer cb, const void* info) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = P(info);
    (void)vk64_trap(VK64_fn_vkCmdCopyBufferToImage2KHR, &a);
}

// Vulkan 1.3 resolves have the same region semantics as vkCmdResolveImage.
// Repack each pointer-bearing *2 region using the guest compiler's own ABI,
// then dispatch through the existing ordered resolve command in the bridge.
// DXVK calls these names when copying its MSAA framebuffer for postprocessing.
typedef struct VkImageResolveRegionL {
    uint32_t srcSubresource[4]; int32_t srcOffset[3];
    uint32_t dstSubresource[4]; int32_t dstOffset[3];
    uint32_t extent[3];
} VkImageResolveRegionL;
typedef struct VkImageResolve2L {
    uint32_t sType; const void* pNext;
    VkImageResolveRegionL region;
} VkImageResolve2L;
typedef struct VkResolveImageInfo2L {
    uint32_t sType; const void* pNext;
    VkImage srcImage; uint32_t srcImageLayout;
    VkImage dstImage; uint32_t dstImageLayout;
    uint32_t regionCount; const VkImageResolve2L* pRegions;
} VkResolveImageInfo2L;
API void vkCmdResolveImage2(VkCommandBuffer commandBuffer, const void* info) {
    const VkResolveImageInfo2L* pInfo = (const VkResolveImageInfo2L*)info;
    for (uint32_t i = 0; i < pInfo->regionCount; ++i) {
        VkImageResolveRegionL region = pInfo->pRegions[i].region;
        VK64Args a = {{0}};
        a.a[0] = commandBuffer; a.a[1] = pInfo->srcImage; a.a[2] = pInfo->srcImageLayout;
        a.a[3] = pInfo->dstImage; a.a[4] = pInfo->dstImageLayout; a.a[5] = 1; a.a[6] = P(&region);
        (void)vk64_trap(VK64_fn_vkCmdResolveImage, &a);
    }
}
API void vkCmdResolveImage2KHR(VkCommandBuffer commandBuffer, const void* info) {
    vkCmdResolveImage2(commandBuffer, info);
}

// ---- commands the 32-bit bridge implements (they used to be TAIL stubs that
// silently returned -8 to a 32-bit guest). Argument slots mirror libvk64.c; the
// *2 info structs are unpacked here with the i386 layout (4-byte uint64 alignment
// inside structs, 4-byte pointers).
API void vkCmdFillBuffer(VkCommandBuffer cb, VkBuffer dst, VkDeviceSize offset, VkDeviceSize size, uint32_t data) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = dst; a.a[2] = offset; a.a[3] = size; a.a[4] = data;
    (void)vk64_trap(VK64_fn_vkCmdFillBuffer, &a);
}
API void vkCmdClearColorImage(VkCommandBuffer cb, VkImage image, uint32_t layout, const void* color, uint32_t n, const void* ranges) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = image; a.a[2] = layout; a.a[3] = P(color); a.a[4] = n; a.a[5] = P(ranges);
    (void)vk64_trap(VK64_fn_vkCmdClearColorImage, &a);
}
API void vkCmdClearDepthStencilImage(VkCommandBuffer cb, VkImage image, uint32_t layout, const void* value, uint32_t n, const void* ranges) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = image; a.a[2] = layout; a.a[3] = P(value); a.a[4] = n; a.a[5] = P(ranges);
    (void)vk64_trap(VK64_fn_vkCmdClearDepthStencilImage, &a);
}
API void vkCmdClearAttachments(VkCommandBuffer cb, uint32_t nAtt, const void* atts, uint32_t nRects, const void* rects) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = nAtt; a.a[2] = P(atts); a.a[3] = nRects; a.a[4] = P(rects);
    (void)vk64_trap(VK64_fn_vkCmdClearAttachments, &a);
}
API void vkCmdClearAttachmentsKHR(VkCommandBuffer cb, uint32_t nAtt, const void* atts, uint32_t nRects, const void* rects) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = nAtt; a.a[2] = P(atts); a.a[3] = nRects; a.a[4] = P(rects);
    (void)vk64_trap(VK64_fn_vkCmdClearAttachmentsKHR, &a);
}
API void vkCmdCopyImage(VkCommandBuffer cb, VkImage src, uint32_t srcLayout, VkImage dst, uint32_t dstLayout, uint32_t n, const void* regions) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = src; a.a[2] = srcLayout; a.a[3] = dst; a.a[4] = dstLayout; a.a[5] = n; a.a[6] = P(regions);
    (void)vk64_trap(VK64_fn_vkCmdCopyImage, &a);
}
typedef struct VkCopyImageInfo2L {
    uint32_t sType; const void* pNext;
    VkImage srcImage; uint32_t srcImageLayout;
    VkImage dstImage; uint32_t dstImageLayout;
    uint32_t regionCount; const void* pRegions;
} VkCopyImageInfo2L;
#ifdef __i386__
_Static_assert(__builtin_offsetof(VkCopyImageInfo2L, regionCount) == 32 && __builtin_offsetof(VkCopyImageInfo2L, pRegions) == 36, "VkCopyImageInfo2 i386 layout");
#endif
static void vk32_copyImage2(uint64_t fn, VkCommandBuffer cb, const void* infoIn) {
    const VkCopyImageInfo2L* i = (const VkCopyImageInfo2L*)infoIn;
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = i->srcImage; a.a[2] = i->dstImage; a.a[3] = i->regionCount; a.a[4] = P(i->pRegions);
    (void)vk64_trap(fn, &a);
}
API void vkCmdCopyImage2(VkCommandBuffer cb, const void* info) { vk32_copyImage2(VK64_fn_vkCmdCopyImage2, cb, info); }
API void vkCmdCopyImage2KHR(VkCommandBuffer cb, const void* info) { vk32_copyImage2(VK64_fn_vkCmdCopyImage2KHR, cb, info); }
API void vkCmdBlitImage(VkCommandBuffer cb, VkImage src, uint32_t srcLayout, VkImage dst, uint32_t dstLayout, uint32_t n, const void* regions, uint32_t filter) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = src; a.a[2] = srcLayout; a.a[3] = dst; a.a[4] = dstLayout; a.a[5] = n;
    a.a[6] = P(regions); a.a[7] = filter;
    (void)vk64_trap(VK64_fn_vkCmdBlitImage, &a);
}
typedef struct VkBlitImageInfo2L {
    uint32_t sType; const void* pNext;
    VkImage srcImage; uint32_t srcImageLayout;
    VkImage dstImage; uint32_t dstImageLayout;
    uint32_t regionCount; const void* pRegions; uint32_t filter;
} VkBlitImageInfo2L;
#ifdef __i386__
_Static_assert(__builtin_offsetof(VkBlitImageInfo2L, regionCount) == 32 && __builtin_offsetof(VkBlitImageInfo2L, filter) == 40, "VkBlitImageInfo2 i386 layout");
#endif
static void vk32_blitImage2(uint64_t fn, VkCommandBuffer cb, const void* infoIn) {
    const VkBlitImageInfo2L* i = (const VkBlitImageInfo2L*)infoIn;
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = i->srcImage; a.a[2] = i->dstImage; a.a[3] = i->regionCount; a.a[4] = P(i->pRegions); a.a[5] = i->filter;
    (void)vk64_trap(fn, &a);
}
API void vkCmdBlitImage2(VkCommandBuffer cb, const void* info) { vk32_blitImage2(VK64_fn_vkCmdBlitImage2, cb, info); }
API void vkCmdBlitImage2KHR(VkCommandBuffer cb, const void* info) { vk32_blitImage2(VK64_fn_vkCmdBlitImage2KHR, cb, info); }
API void vkCmdCopyImageToBuffer(VkCommandBuffer cb, VkImage src, uint32_t srcLayout, VkBuffer dst, uint32_t n, const void* regions) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = src; a.a[2] = srcLayout; a.a[3] = dst; a.a[4] = n; a.a[5] = P(regions);
    (void)vk64_trap(VK64_fn_vkCmdCopyImageToBuffer, &a);
}
typedef struct VkCopyImageToBufferInfo2L {
    uint32_t sType; const void* pNext;
    VkImage srcImage; uint32_t srcImageLayout;
    VkBuffer dstBuffer; uint32_t regionCount; const void* pRegions;
} VkCopyImageToBufferInfo2L;
#ifdef __i386__
_Static_assert(__builtin_offsetof(VkCopyImageToBufferInfo2L, dstBuffer) == 20 && __builtin_offsetof(VkCopyImageToBufferInfo2L, pRegions) == 32, "VkCopyImageToBufferInfo2 i386 layout");
#endif
static void vk32_copyImageToBuffer2(uint64_t fn, VkCommandBuffer cb, const void* infoIn) {
    const VkCopyImageToBufferInfo2L* i = (const VkCopyImageToBufferInfo2L*)infoIn;
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = i->srcImage; a.a[2] = i->srcImageLayout; a.a[3] = i->dstBuffer; a.a[4] = i->regionCount; a.a[5] = P(i->pRegions);
    (void)vk64_trap(fn, &a);
}
API void vkCmdCopyImageToBuffer2(VkCommandBuffer cb, const void* info) { vk32_copyImageToBuffer2(VK64_fn_vkCmdCopyImageToBuffer2, cb, info); }
API void vkCmdCopyImageToBuffer2KHR(VkCommandBuffer cb, const void* info) { vk32_copyImageToBuffer2(VK64_fn_vkCmdCopyImageToBuffer2KHR, cb, info); }
API void vkCmdResolveImage(VkCommandBuffer cb, VkImage src, uint32_t srcLayout, VkImage dst, uint32_t dstLayout, uint32_t n, const void* regions) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = src; a.a[2] = srcLayout; a.a[3] = dst; a.a[4] = dstLayout; a.a[5] = n; a.a[6] = P(regions);
    (void)vk64_trap(VK64_fn_vkCmdResolveImage, &a);
}
API void vkCmdResolveImageKHR(VkCommandBuffer cb, VkImage src, uint32_t srcLayout, VkImage dst, uint32_t dstLayout, uint32_t n, const void* regions) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = src; a.a[2] = srcLayout; a.a[3] = dst; a.a[4] = dstLayout; a.a[5] = n; a.a[6] = P(regions);
    (void)vk64_trap(VK64_fn_vkCmdResolveImageKHR, &a);
}
API void vkCmdDrawIndirect(VkCommandBuffer cb, VkBuffer buf, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = buf; a.a[2] = offset; a.a[3] = drawCount; a.a[4] = stride;
    (void)vk64_trap(VK64_fn_vkCmdDrawIndirect, &a);
}
API void vkCmdDrawIndexedIndirect(VkCommandBuffer cb, VkBuffer buf, VkDeviceSize offset, uint32_t drawCount, uint32_t stride) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = buf; a.a[2] = offset; a.a[3] = drawCount; a.a[4] = stride;
    (void)vk64_trap(VK64_fn_vkCmdDrawIndexedIndirect, &a);
}
static void vk32_drawIndirectCount(uint64_t fn, VkCommandBuffer cb, VkBuffer buf, VkDeviceSize offset, VkBuffer countBuf, VkDeviceSize countOffset, uint32_t maxDraws, uint32_t stride) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = buf; a.a[2] = offset; a.a[3] = countBuf; a.a[4] = countOffset; a.a[5] = maxDraws; a.a[6] = stride;
    (void)vk64_trap(fn, &a);
}
API void vkCmdDrawIndirectCount(VkCommandBuffer cb, VkBuffer buf, VkDeviceSize offset, VkBuffer countBuf, VkDeviceSize countOffset, uint32_t maxDraws, uint32_t stride) {
    vk32_drawIndirectCount(VK64_fn_vkCmdDrawIndirectCount, cb, buf, offset, countBuf, countOffset, maxDraws, stride);
}
API void vkCmdDrawIndirectCountKHR(VkCommandBuffer cb, VkBuffer buf, VkDeviceSize offset, VkBuffer countBuf, VkDeviceSize countOffset, uint32_t maxDraws, uint32_t stride) {
    vk32_drawIndirectCount(VK64_fn_vkCmdDrawIndirectCountKHR, cb, buf, offset, countBuf, countOffset, maxDraws, stride);
}
API void vkCmdDrawIndexedIndirectCount(VkCommandBuffer cb, VkBuffer buf, VkDeviceSize offset, VkBuffer countBuf, VkDeviceSize countOffset, uint32_t maxDraws, uint32_t stride) {
    vk32_drawIndirectCount(VK64_fn_vkCmdDrawIndexedIndirectCount, cb, buf, offset, countBuf, countOffset, maxDraws, stride);
}
API void vkCmdExecuteCommands(VkCommandBuffer cb, uint32_t n, const VkCommandBuffer* cbs) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = n; a.a[2] = P(cbs);
    (void)vk64_trap(VK64_fn_vkCmdExecuteCommands, &a);
}
API void vkCmdDispatch(VkCommandBuffer cb, uint32_t x, uint32_t y, uint32_t z) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = x; a.a[2] = y; a.a[3] = z;
    (void)vk64_trap(VK64_fn_vkCmdDispatch, &a);
}
API void vkCmdDispatchBase(VkCommandBuffer cb, uint32_t bx, uint32_t by, uint32_t bz, uint32_t x, uint32_t y, uint32_t z) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = bx; a.a[2] = by; a.a[3] = bz; a.a[4] = x; a.a[5] = y; a.a[6] = z;
    (void)vk64_trap(VK64_fn_vkCmdDispatchBase, &a);
}
API void vkCmdDispatchIndirect(VkCommandBuffer cb, VkBuffer buf, VkDeviceSize offset) {
    VK64Args a = {{0}};
    a.a[0] = cb; a.a[1] = buf; a.a[2] = offset;
    (void)vk64_trap(VK64_fn_vkCmdDispatchIndirect, &a);
}
// The i386 trap hands back only EAX, so the 64-bit address would lose its high
// half. The bridge also stores the full address through the out slot in arg 2.
API uint64_t vkGetBufferDeviceAddress(VkDevice dev, const void* info) {
    uint64_t va = 0;
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(info); a.a[2] = P(&va);
    (void)vk64_trap(VK64_fn_vkGetBufferDeviceAddress, &a);
    return va;
}
API uint64_t vkGetBufferDeviceAddressKHR(VkDevice dev, const void* info) {
    return vkGetBufferDeviceAddress(dev, info);
}

// ===========================================================================
// F. Frame boundary (2 of the 85)
// ===========================================================================
API VkResult vkQueueSubmit(VkQueue q, uint32_t n, const void* submits, VkFence fence) {
    VK64Args a = {{0}}; a.a[0] = q; a.a[1] = n; a.a[2] = P(submits); a.a[3] = fence;
    return (VkResult)vk64_trap(VK64_fn_vkQueueSubmit, &a);
}
API VkResult vkQueuePresentKHR(VkQueue q, const void* pi) {
    VK64Args a = {{0}}; a.a[0] = q; a.a[1] = P(pi);
    return (VkResult)vk64_trap(VK64_fn_vkQueuePresentKHR, &a);
}
API VkResult vkQueueSubmit2(VkQueue q, uint32_t n, const void* submits, VkFence fence) {
    VK64Args a = {{0}}; a.a[0] = q; a.a[1] = n; a.a[2] = P(submits); a.a[3] = fence;
    return (VkResult)vk64_trap(VK64_fn_vkQueueSubmit2, &a);
}
API VkResult vkQueueSubmit2KHR(VkQueue q, uint32_t n, const void* submits, VkFence fence) {
    VK64Args a = {{0}}; a.a[0] = q; a.a[1] = n; a.a[2] = P(submits); a.a[3] = fence;
    return (VkResult)vk64_trap(VK64_fn_vkQueueSubmit2KHR, &a);
}
API VkResult vkCreateDescriptorUpdateTemplate(VkDevice dev, const void* ci, const void* alloc, void* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateDescriptorUpdateTemplate, &a);
}
API void vkDestroyDescriptorUpdateTemplate(VkDevice dev, uint64_t t, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = t; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyDescriptorUpdateTemplate, &a);
}
API void vkUpdateDescriptorSetWithTemplate(VkDevice dev, uint64_t set, uint64_t templ, const void* data) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = set; a.a[2] = templ; a.a[3] = P(data);
    (void)vk64_trap(VK64_fn_vkUpdateDescriptorSetWithTemplate, &a);
}

// ===========================================================================
// G. Global (no instance): instance version
// ===========================================================================
API VkResult vkEnumerateInstanceVersion(uint32_t* pVersion) {
    VK64Args a = {{0}}; a.a[0] = P(pVersion);
    return (VkResult)vk64_trap(VK64_fn_vkEnumerateInstanceVersion, &a);
}

// ===========================================================================
// H. Shim-local entry points. These have a fixed, spec-exact answer on this
// implementation (one device, one queue family, no sparse, no wide lines, no
// lazily allocated memory), so they are answered here instead of trapping.
// Each is listed with S() in the proc table below and covered by
// vk-shim-bridge-parity.test.mjs.
// ===========================================================================
enum { VKS_INFO2_HDR = sizeof(void*) * 2 };   /* sType + pNext of a Vk*Info2 */
API void vkGetBufferMemoryRequirements2KHR(VkDevice dev, const void* info, void* r) {
    vkGetBufferMemoryRequirements2(dev, info, r);
}
API void vkGetImageMemoryRequirements2KHR(VkDevice dev, const void* info, void* r) {
    vkGetImageMemoryRequirements2(dev, info, r);
}
/* VkDeviceQueueInfo2 {sType,pNext,flags,queueFamilyIndex,queueIndex}. Only
 * flags == 0 queues exist (protectedMemory is off), so a flagged query yields NULL. */
API void vkGetDeviceQueue2(VkDevice dev, const void* info, VkQueue* out) {
    const uint32_t* w = (const uint32_t*)((const char*)info + VKS_INFO2_HDR);
    if (w[0]) { *out = 0; return; }
    vkGetDeviceQueue(dev, w[1], w[2], out);
}
API void vkGetRenderAreaGranularity(VkDevice dev, uint64_t renderPass, uint32_t* granularity) {
    (void)dev; (void)renderPass;
    granularity[0] = 1; granularity[1] = 1;   /* WebGPU render passes have no tile alignment */
}
/* Sparse resources are not supported (sparseBinding and sparseResidency* are off): zero formats. */
API void vkGetImageSparseMemoryRequirements(VkDevice dev, uint64_t img, uint32_t* count, void* reqs) {
    (void)dev; (void)img; (void)reqs;
    *count = 0;
}
API void vkGetDeviceImageSparseMemoryRequirements(VkDevice dev, const void* info, uint32_t* count, void* reqs) {
    (void)dev; (void)info; (void)reqs;
    *count = 0;
}
/* Only lazily allocated memory has a commitment; no such memory type is advertised. */
API void vkGetDeviceMemoryCommitment(VkDevice dev, uint64_t mem, uint64_t* committed) {
    (void)dev; (void)mem;
    *committed = 0;
}
/* One physical device: no peer exists, and peer indices are required to differ. */
API void vkGetDeviceGroupPeerMemoryFeatures(VkDevice dev, uint32_t heap, uint32_t local, uint32_t remote, uint32_t* features) {
    (void)dev; (void)heap; (void)local; (void)remote;
    *features = 0;
}
/* Device masks other than 1 are invalid with a single physical device. */
API void vkCmdSetDeviceMask(VkCommandBuffer cb, uint32_t mask) { (void)cb; (void)mask; }
/* wideLines is not advertised, so the only valid width is 1.0, which is what the rasterizer draws. */
API void vkCmdSetLineWidth(VkCommandBuffer cb, float width) { (void)cb; (void)width; }
/* A hint to return pool memory to the system; command pools here hold no recyclable memory. */
API void vkTrimCommandPool(VkDevice dev, uint64_t pool, uint32_t flags) { (void)dev; (void)pool; (void)flags; }
API void vkTrimCommandPoolKHR(VkDevice dev, uint64_t pool, uint32_t flags) { (void)dev; (void)pool; (void)flags; }
/* Pipeline caches are accepted and empty: pipelines are rebuilt by the WebGPU layer, so there is
 * no driver blob to export. A zero-size blob is a valid answer; creating a cache from it is a no-op. */
API VkResult vkGetPipelineCacheData(VkDevice dev, uint64_t cache, uintptr_t* size, void* data) {
    (void)dev; (void)cache; (void)data;
    *size = 0;
    return 0;     /* VK_SUCCESS */
}
API VkResult vkMergePipelineCaches(VkDevice dev, uint64_t dst, uint32_t count, const uint64_t* src) {
    (void)dev; (void)dst; (void)count; (void)src;
    return 0;     /* VK_SUCCESS */
}

// ===========================================================================
// vkGetInstanceProcAddr / vkGetDeviceProcAddr — resolved GUEST-side, never trap
// for the lookup itself. Every name we implement maps to its own wrapper; every
// other name maps to vk64_benign, which reports the name to the host and returns
// a failure code. Handing back a callable for unknown names is deliberate (P1's
// lesson): returning NULL makes an application conclude the driver is broken and
// bail out of Vulkan entirely.
// ===========================================================================
typedef void (*PFN_vkVoidFunction)(void);

struct procEntry { const char* name; uint64_t fnId; PFN_vkVoidFunction fn; };
#define E(n, id) { #n, (uint64_t)(id), (PFN_vkVoidFunction)n }
#define S(n) { #n, 0, (PFN_vkVoidFunction)n }   /* shim-local: no bridge id */
static const struct procEntry g_procs[] = {
    S(vkGetBufferMemoryRequirements2KHR),
    S(vkGetImageMemoryRequirements2KHR),
    S(vkGetDeviceQueue2),
    S(vkGetRenderAreaGranularity),
    S(vkGetImageSparseMemoryRequirements),
    S(vkGetDeviceImageSparseMemoryRequirements),
    S(vkGetDeviceMemoryCommitment),
    S(vkGetDeviceGroupPeerMemoryFeatures),
    S(vkCmdSetDeviceMask),
    S(vkCmdSetLineWidth),
    S(vkTrimCommandPool),
    S(vkTrimCommandPoolKHR),
    S(vkGetPipelineCacheData),
    S(vkMergePipelineCaches),
    E(vkGetDeviceImageMemoryRequirements, VK64_fn_vkGetDeviceImageMemoryRequirements),
    E(vkCmdSetPrimitiveTopology, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetPrimitiveTopologyEXT, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetDepthTestEnable, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetDepthTestEnableEXT, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetDepthWriteEnable, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetDepthWriteEnableEXT, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetDepthCompareOp, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetDepthCompareOpEXT, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetStencilTestEnable, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetStencilTestEnableEXT, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetDepthBiasEnable, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetDepthBiasEnableEXT, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetRasterizerDiscardEnable, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetRasterizerDiscardEnableEXT, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetPrimitiveRestartEnable, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetPrimitiveRestartEnableEXT, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetDepthBoundsTestEnable, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetDepthBoundsTestEnableEXT, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetStencilOp, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdSetStencilOpEXT, VK64_fn_vkCmdSetExtendedDynamicState),
    E(vkCmdBlitImage, VK64_fn_vkCmdBlitImage),
    E(vkCmdBlitImage2, VK64_fn_vkCmdBlitImage2),
    E(vkCmdBlitImage2KHR, VK64_fn_vkCmdBlitImage2KHR),
    E(vkCmdClearAttachments, VK64_fn_vkCmdClearAttachments),
    E(vkCmdClearAttachmentsKHR, VK64_fn_vkCmdClearAttachmentsKHR),
    E(vkCmdClearColorImage, VK64_fn_vkCmdClearColorImage),
    E(vkCmdClearDepthStencilImage, VK64_fn_vkCmdClearDepthStencilImage),
    E(vkCmdCopyImage, VK64_fn_vkCmdCopyImage),
    E(vkCmdCopyImage2, VK64_fn_vkCmdCopyImage2),
    E(vkCmdCopyImage2KHR, VK64_fn_vkCmdCopyImage2KHR),
    E(vkCmdCopyImageToBuffer, VK64_fn_vkCmdCopyImageToBuffer),
    E(vkCmdCopyImageToBuffer2, VK64_fn_vkCmdCopyImageToBuffer2),
    E(vkCmdCopyImageToBuffer2KHR, VK64_fn_vkCmdCopyImageToBuffer2KHR),
    E(vkCmdDispatch, VK64_fn_vkCmdDispatch),
    E(vkCmdDispatchBase, VK64_fn_vkCmdDispatchBase),
    E(vkCmdDispatchIndirect, VK64_fn_vkCmdDispatchIndirect),
    E(vkCmdDrawIndexedIndirect, VK64_fn_vkCmdDrawIndexedIndirect),
    E(vkCmdDrawIndexedIndirectCount, VK64_fn_vkCmdDrawIndexedIndirectCount),
    E(vkCmdDrawIndirect, VK64_fn_vkCmdDrawIndirect),
    E(vkCmdDrawIndirectCount, VK64_fn_vkCmdDrawIndirectCount),
    E(vkCmdDrawIndirectCountKHR, VK64_fn_vkCmdDrawIndirectCountKHR),
    E(vkCmdExecuteCommands, VK64_fn_vkCmdExecuteCommands),
    E(vkCmdFillBuffer, VK64_fn_vkCmdFillBuffer),
    E(vkCmdResolveImageKHR, VK64_fn_vkCmdResolveImageKHR),
    E(vkGetBufferDeviceAddress, VK64_fn_vkGetBufferDeviceAddress),
    E(vkGetBufferDeviceAddressKHR, VK64_fn_vkGetBufferDeviceAddress),
    E(vkCmdResolveImage, VK64_fn_vkCmdResolveImage),
    E(vkCmdResolveImage2, VK64_fn_vkCmdResolveImage),
    E(vkCmdResolveImage2KHR, VK64_fn_vkCmdResolveImage),
    // --- global ---
    E(vkEnumerateInstanceVersion, VK64_fn_vkEnumerateInstanceVersion),
    E(vkEnumerateInstanceExtensionProperties, VK64_fn_vkEnumerateInstanceExtensionProperties),
    E(vkEnumerateInstanceLayerProperties, VK64_fn_vkEnumerateInstanceLayerProperties),
    E(vkCreateInstance, VK64_fn_vkCreateInstance),
    E(vkDestroyInstance, VK64_fn_vkDestroyInstance),
    // --- queries ---
    E(vkEnumeratePhysicalDevices, VK64_fn_vkEnumeratePhysicalDevices),
    E(vkEnumerateDeviceExtensionProperties, VK64_fn_vkEnumerateDeviceExtensionProperties),
    E(vkGetPhysicalDeviceProperties, VK64_fn_vkGetPhysicalDeviceProperties),
    E(vkGetPhysicalDeviceProperties2, VK64_fn_vkGetPhysicalDeviceProperties2),
    E(vkGetPhysicalDeviceFeatures, VK64_fn_vkGetPhysicalDeviceFeatures),
    E(vkGetPhysicalDeviceFeatures2, VK64_fn_vkGetPhysicalDeviceFeatures2),
    E(vkGetPhysicalDeviceMemoryProperties, VK64_fn_vkGetPhysicalDeviceMemoryProperties),
    E(vkGetPhysicalDeviceMemoryProperties2, VK64_fn_vkGetPhysicalDeviceMemoryProperties2),
    E(vkGetPhysicalDeviceProperties2KHR, VK64_fn_vkGetPhysicalDeviceProperties2KHR),
    E(vkGetPhysicalDeviceFeatures2KHR, VK64_fn_vkGetPhysicalDeviceFeatures2KHR),
    E(vkGetPhysicalDeviceMemoryProperties2KHR, VK64_fn_vkGetPhysicalDeviceMemoryProperties2KHR),
    E(vkGetPhysicalDeviceQueueFamilyProperties, VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties),
    E(vkGetPhysicalDeviceQueueFamilyProperties2, VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties2),
    E(vkGetPhysicalDeviceFormatProperties, VK64_fn_vkGetPhysicalDeviceFormatProperties),
    E(vkGetPhysicalDeviceFormatProperties2, VK64_fn_vkGetPhysicalDeviceFormatProperties2),
    E(vkGetPhysicalDeviceImageFormatProperties, VK64_fn_vkGetPhysicalDeviceImageFormatProperties),
    E(vkGetPhysicalDeviceImageFormatProperties2, VK64_fn_vkGetPhysicalDeviceImageFormatProperties2),
    E(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, VK64_fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR),
    E(vkGetPhysicalDeviceSurfaceFormatsKHR, VK64_fn_vkGetPhysicalDeviceSurfaceFormatsKHR),
    E(vkGetPhysicalDeviceSurfacePresentModesKHR, VK64_fn_vkGetPhysicalDeviceSurfacePresentModesKHR),
    E(vkGetPhysicalDeviceSurfaceSupportKHR, VK64_fn_vkGetPhysicalDeviceSurfaceSupportKHR),
    E(vkGetPhysicalDeviceXlibPresentationSupportKHR, VK64_fn_vkGetPhysicalDeviceXlibPresentationSupportKHR),
    E(vkGetPhysicalDeviceWin32PresentationSupportKHR, VK64_fn_vkGetPhysicalDeviceWin32PresentationSupportKHR),
    E(vkGetDeviceQueue, VK64_fn_vkGetDeviceQueue),
    E(vkGetBufferMemoryRequirements, VK64_fn_vkGetBufferMemoryRequirements),
    E(vkGetBufferMemoryRequirements2, VK64_fn_vkGetBufferMemoryRequirements2),
    E(vkGetDeviceBufferMemoryRequirements, VK64_fn_vkGetDeviceBufferMemoryRequirements),
    E(vkGetImageMemoryRequirements, VK64_fn_vkGetImageMemoryRequirements),
    E(vkGetImageMemoryRequirements2, VK64_fn_vkGetImageMemoryRequirements2),
    E(vkGetImageSubresourceLayout, VK64_fn_vkGetImageSubresourceLayout),
    E(vkGetSwapchainImagesKHR, VK64_fn_vkGetSwapchainImagesKHR),
    E(vkAcquireNextImageKHR, VK64_fn_vkAcquireNextImageKHR),
    E(vkGetPastPresentationTimingGOOGLE, VK64_fn_vkGetPastPresentationTimingGOOGLE),
    E(vkResetFences, VK64_fn_vkResetFences),
    E(vkWaitForFences, VK64_fn_vkWaitForFences),
    E(vkGetFenceStatus, VK64_fn_vkGetFenceStatus),
    E(vkCreateEvent, VK64_fn_vkCreateEvent),
    E(vkDestroyEvent, VK64_fn_vkDestroyEvent),
    E(vkSetEvent, VK64_fn_vkSetEvent),
    E(vkResetEvent, VK64_fn_vkResetEvent),
    E(vkGetEventStatus, VK64_fn_vkGetEventStatus),
    E(vkCreateQueryPool, VK64_fn_vkCreateQueryPool),
    E(vkDestroyQueryPool, VK64_fn_vkDestroyQueryPool),
    E(vkResetQueryPool, VK64_fn_vkResetQueryPool),
    E(vkGetQueryPoolResults, VK64_fn_vkGetQueryPoolResults),
    E(vkDeviceWaitIdle, VK64_fn_vkDeviceWaitIdle),
    E(vkQueueWaitIdle, VK64_fn_vkQueueWaitIdle),
    E(vkResetCommandBuffer, VK64_fn_vkResetCommandBuffer),
    E(vkResetCommandPool, VK64_fn_vkResetCommandPool),
    E(vkFreeCommandBuffers, VK64_fn_vkFreeCommandBuffers),
    // --- creates ---
    E(vkCreateDevice, VK64_fn_vkCreateDevice),
    E(vkCreateXcbSurfaceKHR, VK64_fn_vkCreateXcbSurfaceKHR),
    E(vkCreateXlibSurfaceKHR, VK64_fn_vkCreateXlibSurfaceKHR),
    E(vkCreateHeadlessSurfaceEXT, VK64_fn_vkCreateHeadlessSurfaceEXT),
    E(vkCreateWin32SurfaceKHR, VK64_fn_vkCreateWin32SurfaceKHR),
    E(vkCreateSwapchainKHR, VK64_fn_vkCreateSwapchainKHR),
    E(vkCreateCommandPool, VK64_fn_vkCreateCommandPool),
    E(vkCreateBuffer, VK64_fn_vkCreateBuffer),
    E(vkCreateImage, VK64_fn_vkCreateImage),
    E(vkCreateImageView, VK64_fn_vkCreateImageView),
    E(vkCreateBufferView, VK64_fn_vkCreateBufferView),
    E(vkCreateSampler, VK64_fn_vkCreateSampler),
    E(vkCreateRenderPass, VK64_fn_vkCreateRenderPass),
    E(vkCreateRenderPass2, VK64_fn_vkCreateRenderPass2),
    E(vkCreateRenderPass2KHR, VK64_fn_vkCreateRenderPass2),
    E(vkCmdNextSubpass, VK64_fn_vkCmdNextSubpass),
    E(vkCmdNextSubpassKHR, VK64_fn_vkCmdNextSubpass),
    E(vkCmdNextSubpass2, VK64_fn_vkCmdNextSubpass),
    E(vkCmdNextSubpass2KHR, VK64_fn_vkCmdNextSubpass),
    E(vkCreateFramebuffer, VK64_fn_vkCreateFramebuffer),
    E(vkCreateGraphicsPipelines, VK64_fn_vkCreateGraphicsPipelines),
    E(vkCreateComputePipelines, VK64_fn_vkCreateComputePipelines),
    E(vkCreatePipelineLayout, VK64_fn_vkCreatePipelineLayout),
    E(vkCreateDescriptorSetLayout, VK64_fn_vkCreateDescriptorSetLayout),
    E(vkCreateDescriptorPool, VK64_fn_vkCreateDescriptorPool),
    E(vkResetDescriptorPool, VK64_fn_vkResetDescriptorPool),
    E(vkFreeDescriptorSets, VK64_fn_vkFreeDescriptorSets),
    E(vkCmdPushDescriptorSetKHR, VK64_fn_vkCmdPushDescriptorSetKHR),
    E(vkCmdPushDescriptorSet, VK64_fn_vkCmdPushDescriptorSetKHR),
    E(vkCmdPushDescriptorSetWithTemplateKHR, VK64_fn_vkCmdPushDescriptorSetWithTemplateKHR),
    E(vkCmdPushDescriptorSetWithTemplate, VK64_fn_vkCmdPushDescriptorSetWithTemplateKHR),
    E(vkGetDescriptorSetLayoutSupport, VK64_fn_vkGetDescriptorSetLayoutSupport),
    E(vkGetDescriptorSetLayoutSupportKHR, VK64_fn_vkGetDescriptorSetLayoutSupport),
    E(vkCreateShaderModule, VK64_fn_vkCreateShaderModule),
    E(vkCreatePipelineCache, VK64_fn_vkCreatePipelineCache),
    E(vkCreateFence, VK64_fn_vkCreateFence),
    E(vkCreateSemaphore, VK64_fn_vkCreateSemaphore),
    E(vkGetSemaphoreCounterValue, VK64_fn_vkGetSemaphoreCounterValue),
    E(vkWaitSemaphores, VK64_fn_vkWaitSemaphores),
    E(vkSignalSemaphore, VK64_fn_vkSignalSemaphore),
    // --- allocation / binding / update / map ---
    E(vkAllocateMemory, VK64_fn_vkAllocateMemory),
    E(vkAllocateCommandBuffers, VK64_fn_vkAllocateCommandBuffers),
    E(vkAllocateDescriptorSets, VK64_fn_vkAllocateDescriptorSets),
    E(vkBindBufferMemory, VK64_fn_vkBindBufferMemory),
    E(vkBindBufferMemory2, VK64_fn_vkBindBufferMemory2),
    E(vkBindBufferMemory2KHR, VK64_fn_vkBindBufferMemory2KHR),
    E(vkBindImageMemory, VK64_fn_vkBindImageMemory),
    E(vkBindImageMemory2, VK64_fn_vkBindImageMemory2),
    E(vkBindImageMemory2KHR, VK64_fn_vkBindImageMemory2KHR),
    E(vkUpdateDescriptorSets, VK64_fn_vkUpdateDescriptorSets),
    E(vkMapMemory, VK64_fn_vkMapMemory),
    E(vkUnmapMemory, VK64_fn_vkUnmapMemory),
    // --- destroys ---
    E(vkDestroyDevice, VK64_fn_vkDestroyDevice),
    E(vkDestroySurfaceKHR, VK64_fn_vkDestroySurfaceKHR),
    E(vkDestroySwapchainKHR, VK64_fn_vkDestroySwapchainKHR),
    E(vkDestroyCommandPool, VK64_fn_vkDestroyCommandPool),
    E(vkDestroyBuffer, VK64_fn_vkDestroyBuffer),
    E(vkDestroyBufferView, VK64_fn_vkDestroyBuffer),
    E(vkDestroyImage, VK64_fn_vkDestroyImage),
    E(vkDestroyImageView, VK64_fn_vkDestroyImageView),
    E(vkDestroySampler, VK64_fn_vkDestroySampler),
    E(vkDestroyRenderPass, VK64_fn_vkDestroyRenderPass),
    E(vkDestroyFramebuffer, VK64_fn_vkDestroyFramebuffer),
    E(vkDestroyPipeline, VK64_fn_vkDestroyPipeline),
    E(vkDestroyPipelineLayout, VK64_fn_vkDestroyPipelineLayout),
    E(vkDestroyDescriptorSetLayout, VK64_fn_vkDestroyDescriptorSetLayout),
    E(vkDestroyDescriptorPool, VK64_fn_vkDestroyDescriptorPool),
    E(vkDestroyShaderModule, VK64_fn_vkDestroyShaderModule),
    E(vkDestroyPipelineCache, VK64_fn_vkDestroyPipelineCache),
    E(vkDestroyFence, VK64_fn_vkDestroyFence),
    E(vkDestroySemaphore, VK64_fn_vkDestroySemaphore),
    E(vkFreeMemory, VK64_fn_vkFreeMemory),
    // --- command recording ---
    E(vkBeginCommandBuffer, VK64_fn_vkBeginCommandBuffer),
    E(vkEndCommandBuffer, VK64_fn_vkEndCommandBuffer),
    E(vkCmdBeginRenderPass, VK64_fn_vkCmdBeginRenderPass),
    E(vkCmdEndRenderPass, VK64_fn_vkCmdEndRenderPass),
    E(vkCmdBindPipeline, VK64_fn_vkCmdBindPipeline),
    E(vkCmdBindDescriptorSets, VK64_fn_vkCmdBindDescriptorSets),
    E(vkCmdSetViewport, VK64_fn_vkCmdSetViewport),
    E(vkCmdSetScissorWithCountEXT, VK64_fn_vkCmdSetScissor),
    E(vkCmdSetScissorWithCount, VK64_fn_vkCmdSetScissor),
    E(vkCmdSetViewportWithCountEXT, VK64_fn_vkCmdSetViewport),
    E(vkCmdSetViewportWithCount, VK64_fn_vkCmdSetViewport),
    E(vkInvalidateMappedMemoryRanges, VK64_fn_vkInvalidateMappedMemoryRanges),
    E(vkFlushMappedMemoryRanges, VK64_fn_vkFlushMappedMemoryRanges),
    E(vkCmdSetScissor, VK64_fn_vkCmdSetScissor),
    E(vkCmdSetBlendConstants, VK64_fn_vkCmdSetBlendConstants),
    E(vkCmdSetStencilCompareMask, VK64_fn_vkCmdSetStencilCompareMask),
    E(vkCmdSetStencilWriteMask, VK64_fn_vkCmdSetStencilWriteMask),
    E(vkCmdSetStencilReference, VK64_fn_vkCmdSetStencilReference),
    E(vkCmdSetDepthBias, VK64_fn_vkCmdSetDepthBias),
    E(vkCmdSetCullMode, VK64_fn_vkCmdSetCullMode),
    E(vkCmdSetCullModeEXT, VK64_fn_vkCmdSetCullMode),
    E(vkCmdSetFrontFace, VK64_fn_vkCmdSetFrontFace),
    E(vkCmdSetFrontFaceEXT, VK64_fn_vkCmdSetFrontFace),
    E(vkCmdSetDepthBounds, VK64_fn_vkCmdSetDepthBounds),
    E(vkCmdSetEvent2, VK64_fn_vkCmdSetEvent2),
    E(vkCmdSetEvent2KHR, VK64_fn_vkCmdSetEvent2),
    E(vkCmdResetEvent2, VK64_fn_vkCmdResetEvent),
    E(vkCmdResetEvent2KHR, VK64_fn_vkCmdResetEvent),
    E(vkCmdWaitEvents2, VK64_fn_vkCmdWaitEvents),
    E(vkCmdWaitEvents2KHR, VK64_fn_vkCmdWaitEvents),
    E(vkCmdWriteTimestamp, VK64_fn_vkCmdWriteTimestamp),
    E(vkCmdWriteTimestamp2, VK64_fn_vkCmdWriteTimestamp),
    E(vkCmdWriteTimestamp2KHR, VK64_fn_vkCmdWriteTimestamp),

    E(vkCmdDraw, VK64_fn_vkCmdDraw),
    E(vkCmdDrawIndexed, VK64_fn_vkCmdDrawIndexed),
    E(vkCmdBindVertexBuffers, VK64_fn_vkCmdBindVertexBuffers),
    E(vkCmdBindVertexBuffers2, VK64_fn_vkCmdBindVertexBuffers2),
    E(vkCmdBindVertexBuffers2KHR, VK64_fn_vkCmdBindVertexBuffers2),
    E(vkCmdBindVertexBuffers2EXT, VK64_fn_vkCmdBindVertexBuffers2),
    E(vkCmdBindIndexBuffer, VK64_fn_vkCmdBindIndexBuffer),
    E(vkCmdPushConstants, VK64_fn_vkCmdPushConstants),
    E(vkCmdSetEvent, VK64_fn_vkCmdSetEvent),
    E(vkCmdResetEvent, VK64_fn_vkCmdResetEvent),
    E(vkCmdWaitEvents, VK64_fn_vkCmdWaitEvents),
    E(vkCmdResetQueryPool, VK64_fn_vkCmdResetQueryPool),
    E(vkCmdBeginQuery, VK64_fn_vkCmdBeginQuery),
    E(vkCmdEndQuery, VK64_fn_vkCmdEndQuery),
    E(vkCmdCopyQueryPoolResults, VK64_fn_vkCmdCopyQueryPoolResults),
    E(vkCmdUpdateBuffer, VK64_fn_vkCmdUpdateBuffer),
    E(vkCmdPipelineBarrier, VK64_fn_vkCmdPipelineBarrier),
    E(vkCmdCopyBufferToImage, VK64_fn_vkCmdCopyBufferToImage),
    E(vkCmdCopyBuffer, VK64_fn_vkCmdCopyBuffer),
    E(vkCmdCopyBuffer2, VK64_fn_vkCmdCopyBuffer2),
    E(vkCmdCopyBuffer2KHR, VK64_fn_vkCmdCopyBuffer2KHR),
    E(vkCmdCopyBufferToImage2, VK64_fn_vkCmdCopyBufferToImage2),
    E(vkCmdCopyBufferToImage2KHR, VK64_fn_vkCmdCopyBufferToImage2KHR),
    // --- frame boundary ---
    E(vkQueueSubmit, VK64_fn_vkQueueSubmit),
    E(vkQueueSubmit2, VK64_fn_vkQueueSubmit2),
    E(vkQueueSubmit2KHR, VK64_fn_vkQueueSubmit2KHR),
    E(vkCreateDescriptorUpdateTemplate, VK64_fn_vkCreateDescriptorUpdateTemplate),
    E(vkDestroyDescriptorUpdateTemplate, VK64_fn_vkDestroyDescriptorUpdateTemplate),
    E(vkUpdateDescriptorSetWithTemplate, VK64_fn_vkUpdateDescriptorSetWithTemplate),
    E(vkCmdBeginRenderPass2, VK64_fn_vkCmdBeginRenderPass2),
    E(vkCmdBeginRenderPass2KHR, VK64_fn_vkCmdBeginRenderPass2KHR),
    E(vkCmdEndRenderPass2, VK64_fn_vkCmdEndRenderPass2),
    E(vkCmdEndRenderPass2KHR, VK64_fn_vkCmdEndRenderPass2KHR),
    E(vkCmdPipelineBarrier2, VK64_fn_vkCmdPipelineBarrier2),
    E(vkCmdPipelineBarrier2KHR, VK64_fn_vkCmdPipelineBarrier2KHR),
    E(vkCmdPushConstants2, VK64_fn_vkCmdPushConstants2),
    E(vkCmdPushConstants2KHR, VK64_fn_vkCmdPushConstants2KHR),
    E(vkCmdBindIndexBuffer2, VK64_fn_vkCmdBindIndexBuffer2),
    E(vkCmdBindIndexBuffer2KHR, VK64_fn_vkCmdBindIndexBuffer2KHR),
    E(vkCmdBeginRendering, VK64_fn_vkCmdBeginRendering),
    E(vkCmdBeginRenderingKHR, VK64_fn_vkCmdBeginRenderingKHR),
    E(vkCmdEndRendering, VK64_fn_vkCmdEndRendering),
    E(vkCmdEndRenderingKHR, VK64_fn_vkCmdEndRenderingKHR),
    E(vkQueuePresentKHR, VK64_fn_vkQueuePresentKHR),
};
#undef E
#define NPROCS (sizeof(g_procs) / sizeof(g_procs[0]))

static PFN_vkVoidFunction vk64_resolve(const char* name) {
    unsigned i;
    if (!name) return 0;
    for (i = 0; i < NPROCS; i++) {
        if (strcmp(g_procs[i].name, name) == 0) {
            // hit=1: we have a real wrapper. Trace so the log names the exact
            // resolved-and-implemented set (this is P1's loader_tally evidence,
            // taken live).
            VK64Args a = {{0}};
            a.a[0] = (uint64_t)(uintptr_t)name;
            a.a[1] = 1;
            (void)vk64_trap(VK64_fn_traceProc, &a);
            return g_procs[i].fn;
        }
    }
    return 0;
}

// loader-spec rule: a NULL instance must still resolve the global commands
// (vkEnumerateInstanceVersion, vkEnumerateInstanceExtensionProperties,
//  vkEnumerateInstanceLayerProperties, vkCreateInstance, vkDestroyInstance) —
// and with no loader in the guest there is nothing else to gate, so every name
// resolves regardless of the instance handle.
API PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice dev, const char* name);

API PFN_vkVoidFunction vkGetInstanceProcAddr(VkInstance inst, const char* name) {
    (void)inst;   /* no loader: the instance handle gates nothing */
    PFN_vkVoidFunction f = vk64_resolve(name);
    if (f) return f;
    if (strcmp(name, "vkGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (strcmp(name, "vkGetDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetDeviceProcAddr;
    // The three ICD-interface names are exported as aliases in case a guest app
    // probes for them (a loader would; nothing in this guest does — with no
    // loader the contract is vacuous).
    if (strcmp(name, "vk_icdGetInstanceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (strcmp(name, "vk_icdGetPhysicalDeviceProcAddr") == 0) return (PFN_vkVoidFunction)vkGetInstanceProcAddr;
    if (strcmp(name, "vk_icdNegotiateLoaderICDInterfaceVersion") == 0) return (PFN_vkVoidFunction)vk64_benign;
    // Return NULL (not benign) for MapMemory2/UnmapMemory2 so Wine does not attempt the VK_EXT_map_memory_placed path.
    if (strcmp(name, "vkMapMemory2") == 0) return 0;
    if (strcmp(name, "vkMapMemory2KHR") == 0) return 0;
    if (strcmp(name, "vkUnmapMemory2") == 0) return 0;
    if (strcmp(name, "vkUnmapMemory2KHR") == 0) return 0;
    // Unknown name: report it to the host (hit=0) so the log is the worklist,
    // then hand back the benign stub so the app keeps a callable pointer.
    {
        VK64Args a = {{0}};
        a.a[0] = (uint64_t)(uintptr_t)name;
        a.a[1] = 0;
        (void)vk64_trap(VK64_fn_traceProc, &a);
    }
    return (PFN_vkVoidFunction)vk64_benign;
}

API PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice dev, const char* name) {
    PFN_vkVoidFunction f = vk64_resolve(name);
    if (f) return f;
    return (PFN_vkVoidFunction)vkGetInstanceProcAddr(dev, name);
}

// Direct-symbol aliases for the ICD interface, so a guest that dlopen()s us and
// dlsym()s an ICD name finds it (nothing in the guest calls them).
API void* vk_icdGetInstanceProcAddr(VkInstance inst, const char* name) {
    return (void*)vkGetInstanceProcAddr(inst, name);
}
API void* vk_icdGetPhysicalDeviceProcAddr(VkInstance inst, const char* name) {
    return (void*)vkGetInstanceProcAddr(inst, name);
}
API unsigned vk_icdNegotiateLoaderICDInterfaceVersion(unsigned* pVersion) {
    if (!pVersion) return 0;
    *pVersion = 5;      /* as P1's ICD did; unused without a loader */
    return 0;
}

// ===========================================================================
// The unimplemented tail, exported as real symbols.
//
// Two jobs:
//  1. An application that links libvulkan.so.1 DIRECTLY (rather than resolving
//     through vkGetInstanceProcAddr) needs every symbol it references present at
//     load time, or the guest's ld.so fails the whole binary on a relocation.
//     vkcube happens to have zero direct vk* imports, but DXVK-built PE modules
//     and other Vulkan consumers do not.
//  2. Each stub reports its name to the host, which is how the "what does a real
//     guest actually ask for" worklist is collected (P1's loader_tally.c did the
//     same from the loader side).
// Declared `void*(void)`: an application calling one of these through a
// PFN_vkVoidFunction only reads RAX, and the arguments are ignored either way.
// ===========================================================================
#define TAIL(n) API void* n(void) { return vk64_benign(#n); }
TAIL(vkGetBufferOpaqueCaptureAddressKHR)
TAIL(vkGetDeviceImageSubresourceLayout)
TAIL(vkGetDeviceGroupPresentCapabilitiesKHR)
TAIL(vkGetDeviceGroupSurfacePresentModesKHR)
TAIL(vkGetFenceStatus2)
TAIL(vkGetFenceStatus2KHR)
TAIL(vkGetImageSubresourceLayout2)
TAIL(vkGetImageSubresourceLayout2EXT)
TAIL(vkGetPhysicalDevicePresentRectanglesKHR)
TAIL(vkGetPhysicalDeviceSurfaceCapabilities2EXT)
TAIL(vkGetPhysicalDeviceSurfaceFormats2EXT)
TAIL(vkGetPhysicalDeviceSurfacePresentModes2EXT)
TAIL(vkGetQueryPoolResults64)
TAIL(vkGetQueryPoolResults64KHR)
TAIL(vkGetRenderingAreaGranularity)
TAIL(vkSwapchainImagesKHR2)
TAIL(vkCreateSwapchainKHR2)
TAIL(vkDestroyDeviceKHR)
TAIL(vkDestroySwapchainKHR2)
TAIL(vkResetPipelineCache)
TAIL(vkSetDebugUtilsObjectNameEXT)
TAIL(vkSetDebugUtilsObjectTagEXT)
TAIL(vkCreateDebugUtilsMessengerEXT)
TAIL(vkDestroyDebugUtilsMessengerEXT)
TAIL(vkSubmitDebugUtilsMessageEXT)
TAIL(vkGetPipelineLayoutSupport)
TAIL(vkQueueBindSparse)
TAIL(vkCmdSetBlendConstant)
TAIL(vkCmdSetDepthBiasNV)
TAIL(vkCmdSetVertexInputEXT)
TAIL(vkCmdBeginConditionalRenderingEXT)
TAIL(vkCmdEndConditionalRenderingEXT)
TAIL(vkCmdDrawIndirectByteCountEXT)

TAIL(vkCreateFramebuffer2KHR)
TAIL(vkCreateSamplerYcbcrConversion)
TAIL(vkDestroySamplerYcbcrConversion)
#undef TAIL
