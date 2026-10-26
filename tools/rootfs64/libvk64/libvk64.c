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

// Guest libvulkan.so.1 for the 64-bit Boxedwine Vulkan path — the Vulkan
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
    // readback: must match vk64bridge_abi.h order exactly
    VK64_fn_vkCmdCopyImageToBuffer,
    VK64_fn_vkCmdCopyImage,
    VK64_fn_vkCmdCopyImageToBuffer2,
    VK64_fn_vkCmdCopyImageToBuffer2KHR,
    VK64_fn_vkCmdCopyBuffer,
    VK64_fn_vkCmdCopyBuffer2,
    VK64_fn_vkCmdCopyBuffer2KHR,
    VK64_fn_vkCmdFillBuffer,
    VK64_fn_vkCmdClearColorImage,
    VK64_fn_vkCmdClearAttachments,
    VK64_fn_vkCmdClearAttachmentsKHR,
    VK64_fn_vkCmdCopyImage2,
    VK64_fn_vkCmdCopyImage2KHR,
    VK64_fn_vkCmdCopyBufferToImage2,
    VK64_fn_vkCmdCopyBufferToImage2KHR,
    VK64_fn_vkCmdBlitImage,
    VK64_fn_vkCmdBlitImage2,
    VK64_fn_vkCmdBlitImage2KHR,
    VK64_fn_vkCmdResolveImage,
    VK64_fn_vkCmdResolveImageKHR,
    VK64_fn_vkCmdDrawIndirect,
    VK64_fn_vkCmdDrawIndexedIndirect,
    VK64_fn_vkCmdDrawIndirectCount,
    VK64_fn_vkCmdDrawIndirectCountKHR,
    VK64_fn_vkCmdDrawIndexedIndirectCount,
    VK64_fn_vkCmdExecuteCommands,
    VK64_fn_vkCmdDispatch,
    VK64_fn_vkCmdDispatchBase,
    VK64_fn_vkCmdDispatchIndirect,
    VK64_fn_vkGetBufferDeviceAddress,
    VK64_fn_vkCreateEvent = 841,
    VK64_fn_vkDestroyEvent = 842,
    VK64_fn_vkSetEvent = 843,
    VK64_fn_vkResetEvent = 844,
    VK64_fn_vkGetEventStatus = 845,
    VK64_fn_vkCmdSetEvent = 846,
    VK64_fn_vkCmdResetEvent = 847,
    VK64_fn_vkCmdWaitEvents = 848,
    VK64_fn_vkCreateQueryPool = 849,
    VK64_fn_vkDestroyQueryPool = 850,
    VK64_fn_vkResetQueryPool = 851,
    VK64_fn_vkGetQueryPoolResults = 852,
    VK64_fn_vkCmdResetQueryPool = 853,
    VK64_fn_vkCmdBeginQuery = 854,
    VK64_fn_vkCmdEndQuery = 855,
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
    uint64_t ret;
    register uint64_t rdi __asm__("rdi") = fnId;
    register uint64_t rsi __asm__("rsi") = (uint64_t)(uintptr_t)args;
    __asm__ __volatile__(
        "syscall"
        : "=a"(ret)
        : "a"(VK64_SYSCALL_NR), "r"(rdi), "r"(rsi)
        : "rcx", "r11", "memory");
    return ret;
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
typedef unsigned long       VkHandle;      /* VkInstance, VkDevice, VkBuffer, ... */
typedef unsigned long long  VkDeviceSize;
typedef unsigned int        VkFlags;
typedef int                 VkResult;
typedef unsigned int        VkBool32;

typedef VkHandle VkInstance;
typedef VkHandle VkPhysicalDevice;
typedef VkHandle VkDevice;
typedef VkHandle VkQueue;
typedef VkHandle VkCommandPool;
typedef VkHandle VkCommandBuffer;
typedef VkHandle VkDeviceMemory;
typedef VkHandle VkBuffer;
typedef VkHandle VkImage;
typedef VkHandle VkImageView;
typedef VkHandle VkBufferView;
typedef VkHandle VkSampler;
typedef VkHandle VkShaderModule;
typedef VkHandle VkDescriptorSetLayout;
typedef VkHandle VkDescriptorPool;
typedef VkHandle VkDescriptorSet;
typedef VkHandle VkPipelineLayout;
typedef VkHandle VkPipeline;
typedef VkHandle VkRenderPass;
typedef VkHandle VkFramebuffer;
typedef VkHandle VkPipelineCache;
typedef VkHandle VkFence;
typedef VkHandle VkQueryPool;
typedef VkHandle VkEvent;
typedef VkHandle VkSemaphore;
typedef VkHandle VkSurfaceKHR;
typedef VkHandle VkSwapchainKHR;

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
API void vkDestroyBufferView(VkDevice dev, uint64_t view, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = view; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyBuffer, &a);    /* views and buffers share the bridge handle table */
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
API void vkResetQueryPool(VkDevice dev, VkQueryPool pool, uint32_t first, uint32_t count) {
    VK64Args a = {{0}};
    a.a[0] = dev; a.a[1] = pool; a.a[2] = first; a.a[3] = count;
    (void)vk64_trap(VK64_fn_vkResetQueryPool, &a);
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
/* KHR aliases for core-promoted physical-device query functions.
 * In real Vulkan, e.g. vkGetPhysicalDeviceProperties2KHR IS
 * vkGetPhysicalDeviceProperties2 (promoted in Vulkan 1.1); an ICD must
 * return the same pointer for both names. vkd3d queries the KHR names
 * via vkGetInstanceProcAddr, so alias them to the core wrappers. */
API void vkGetPhysicalDeviceProperties2KHR(VkPhysicalDevice pd, void* p) {
    vkGetPhysicalDeviceProperties2(pd, p);
}
API void vkGetPhysicalDeviceFeatures2KHR(VkPhysicalDevice pd, void* p) {
    vkGetPhysicalDeviceFeatures2(pd, p);
}
API void vkGetPhysicalDeviceMemoryProperties2KHR(VkPhysicalDevice pd, void* p) {
    vkGetPhysicalDeviceMemoryProperties2(pd, p);
}
API void vkGetPhysicalDeviceQueueFamilyProperties2KHR(VkPhysicalDevice pd, uint32_t* count, void* p) {
    vkGetPhysicalDeviceQueueFamilyProperties2(pd, count, p);
}
API void vkGetPhysicalDeviceFormatProperties2KHR(VkPhysicalDevice pd, uint32_t format, void* props) {
    vkGetPhysicalDeviceFormatProperties2(pd, format, props);
}
API VkResult vkGetPhysicalDeviceImageFormatProperties2KHR(VkPhysicalDevice pd, const void* info, void* props) {
    return vkGetPhysicalDeviceImageFormatProperties2(pd, info, props);
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
API void vkCmdPushDescriptorSetWithTemplateKHR(VkCommandBuffer cb, VkHandle tmpl, VkPipelineLayout layout, uint32_t set, const void* data) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = tmpl; a.a[2] = layout; a.a[3] = set; a.a[4] = P(data);
    (void)vk64_trap(VK64_fn_vkCmdPushDescriptorSetWithTemplateKHR, &a);
}
API void vkCmdPushDescriptorSetWithTemplate(VkCommandBuffer cb, VkHandle tmpl, VkPipelineLayout layout, uint32_t set, const void* data) {
    vkCmdPushDescriptorSetWithTemplateKHR(cb, tmpl, layout, set, data);
}
API void vkGetDescriptorSetLayoutSupport(VkDevice dev, const void* ci, void* support) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(support);
    (void)vk64_trap(VK64_fn_vkGetDescriptorSetLayoutSupport, &a);
}
API void vkGetDescriptorSetLayoutSupportKHR(VkDevice dev, const void* ci, void* support) {
    vkGetDescriptorSetLayoutSupport(dev, ci, support);
}
API void vkDestroyDescriptorPool(VkDevice dev, VkDescriptorPool p, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = p; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyDescriptorPool, &a);
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
API void vkCmdClearDepthStencilImage(VkCommandBuffer cb, VkImage image, uint32_t layout, const void* value, uint32_t n, const void* ranges) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = image; a.a[2] = layout; a.a[3] = P(value); a.a[4] = n; a.a[5] = P(ranges);
    (void)vk64_trap(VK64_fn_vkCmdClearDepthStencilImage, &a);
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

// --- readback: image->buffer copies (were TAIL stubs; now trap to host) ---
API void vkCmdCopyImageToBuffer(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout,
                                VkBuffer dstBuffer, uint32_t regionCount, const void* pRegions) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = srcImage; a.a[2] = srcImageLayout;
    a.a[3] = dstBuffer; a.a[4] = regionCount; a.a[5] = P(pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyImageToBuffer, &a);
}
// VkCopyImageToBufferInfo2 64-bit layout (Vulkan spec; verified field offsets)
typedef struct VkCopyImageToBufferInfo2L {
    uint32_t sType; uint32_t _pad0;
    const void* pNext;
    VkImage srcImage;
    uint32_t srcImageLayout; uint32_t _pad1;
    VkBuffer dstBuffer;
    uint32_t regionCount; uint32_t _pad2;
    const void* pRegions;
} VkCopyImageToBufferInfo2L;
API void vkCmdCopyImageToBuffer2(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2L* pInfo) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = pInfo->srcImage; a.a[2] = 0;
    a.a[3] = pInfo->dstBuffer; a.a[4] = pInfo->regionCount; a.a[5] = P(pInfo->pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyImageToBuffer2, &a);
}
API void vkCmdCopyImageToBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyImageToBufferInfo2L* pInfo) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = pInfo->srcImage; a.a[2] = 0;
    a.a[3] = pInfo->dstBuffer; a.a[4] = pInfo->regionCount; a.a[5] = P(pInfo->pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyImageToBuffer2KHR, &a);
}
API void vkCmdCopyBuffer(VkCommandBuffer commandBuffer, VkBuffer srcBuffer, VkBuffer dstBuffer, uint32_t regionCount, const void* pRegions) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = srcBuffer; a.a[2] = dstBuffer;
    a.a[3] = regionCount; a.a[4] = P(pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyBuffer, &a);
}
typedef struct VkCopyBufferInfo2L {
    uint32_t sType; uint32_t _pad0;
    const void* pNext;
    VkBuffer srcBuffer;
    VkBuffer dstBuffer;
    uint32_t regionCount; uint32_t _pad2;
    const void* pRegions;
} VkCopyBufferInfo2L;
API void vkCmdCopyBuffer2(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2L* pInfo) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = pInfo->srcBuffer; a.a[2] = pInfo->dstBuffer;
    a.a[3] = pInfo->regionCount; a.a[4] = P(pInfo->pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyBuffer2, &a);
}
API void vkCmdCopyBuffer2KHR(VkCommandBuffer commandBuffer, const VkCopyBufferInfo2L* pInfo) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = pInfo->srcBuffer; a.a[2] = pInfo->dstBuffer;
    a.a[3] = pInfo->regionCount; a.a[4] = P(pInfo->pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyBuffer2KHR, &a);
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
// Forward declarations for game-breaking stub implementations
API uint64_t vkGetBufferDeviceAddress(VkDevice, const void*);
API uint64_t vkGetBufferDeviceAddressKHR(VkDevice, const void*);
API void vkCmdFillBuffer(VkCommandBuffer, VkBuffer, uint64_t, uint64_t, uint32_t);
API void vkCmdClearColorImage(VkCommandBuffer, VkImage, uint32_t, const void*, uint32_t, const void*);
API void vkCmdClearAttachments(VkCommandBuffer, uint32_t, const void*, uint32_t, const void*);
API void vkCmdClearAttachmentsKHR(VkCommandBuffer, uint32_t, const void*, uint32_t, const void*);
API void vkCmdCopyImage(VkCommandBuffer, VkImage, uint32_t, VkImage, uint32_t, uint32_t, const void*);
API void vkCmdCopyImage2(VkCommandBuffer, const void*);
API void vkCmdCopyImage2KHR(VkCommandBuffer, const void*);
API void vkCmdCopyBufferToImage2(VkCommandBuffer, const void*);
API void vkCmdCopyBufferToImage2KHR(VkCommandBuffer, const void*);
API void vkCmdBlitImage(VkCommandBuffer, VkImage, uint32_t, VkImage, uint32_t, uint32_t, const void*, uint32_t);
API void vkCmdBlitImage2(VkCommandBuffer, const void*);
API void vkCmdBlitImage2KHR(VkCommandBuffer, const void*);
API void vkCmdResolveImage(VkCommandBuffer, VkImage, uint32_t, VkImage, uint32_t, uint32_t, const void*);
API void vkCmdResolveImageKHR(VkCommandBuffer, VkImage, uint32_t, VkImage, uint32_t, uint32_t, const void*);
API void vkCmdResolveImage2(VkCommandBuffer, const void*);
API void vkCmdResolveImage2KHR(VkCommandBuffer, const void*);
API void vkCmdDrawIndirect(VkCommandBuffer, VkBuffer, uint64_t, uint32_t, uint32_t);
API void vkCmdDrawIndexedIndirect(VkCommandBuffer, VkBuffer, uint64_t, uint32_t, uint32_t);
API void vkCmdDrawIndirectCount(VkCommandBuffer, VkBuffer, uint64_t, VkBuffer, uint64_t, uint32_t, uint32_t);
API void vkCmdDrawIndirectCountKHR(VkCommandBuffer, VkBuffer, uint64_t, VkBuffer, uint64_t, uint32_t, uint32_t);
API void vkCmdDrawIndexedIndirectCount(VkCommandBuffer, VkBuffer, uint64_t, VkBuffer, uint64_t, uint32_t, uint32_t);
API void vkCmdExecuteCommands(VkCommandBuffer, uint32_t, const VkCommandBuffer*);
API void vkCmdDispatch(VkCommandBuffer, uint32_t, uint32_t, uint32_t);
API void vkCmdDispatchBase(VkCommandBuffer, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
API void vkCmdDispatchIndirect(VkCommandBuffer, VkBuffer, uint64_t);
API VkResult vkCreateEvent(VkDevice, const void*, const void*, VkEvent*);
API void vkDestroyEvent(VkDevice, VkEvent, const void*);
API VkResult vkSetEvent(VkDevice, VkEvent);
API VkResult vkResetEvent(VkDevice, VkEvent);
API VkResult vkGetEventStatus(VkDevice, VkEvent);
API void vkCmdSetEvent(VkCommandBuffer, VkEvent, uint32_t);
API void vkCmdResetEvent(VkCommandBuffer, VkEvent, uint32_t);
API void vkCmdWaitEvents(VkCommandBuffer, uint32_t, const VkEvent*, uint32_t, uint32_t, uint32_t, const void*, uint32_t, const void*, uint32_t, const void*);
API VkResult vkCreateQueryPool(VkDevice, const void*, const void*, VkQueryPool*);
API void vkDestroyQueryPool(VkDevice, VkQueryPool, const void*);
API VkResult vkGetQueryPoolResults(VkDevice, VkQueryPool, uint32_t, uint32_t, unsigned long, void*, uint64_t, uint32_t);
API void vkCmdResetQueryPool(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
API void vkCmdBeginQuery(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t);
API void vkCmdEndQuery(VkCommandBuffer, VkQueryPool, uint32_t);
API void vkCmdCopyQueryPoolResults(VkCommandBuffer, VkQueryPool, uint32_t, uint32_t, VkBuffer, uint64_t, uint64_t, uint32_t);

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
    E(vkCmdResolveImage2, VK64_fn_vkCmdResolveImage),
    E(vkCmdResolveImage2KHR, VK64_fn_vkCmdResolveImage),
    E(vkResetQueryPool, VK64_fn_vkResetQueryPool),
    E(vkCreateEvent, VK64_fn_vkCreateEvent),
    E(vkDestroyEvent, VK64_fn_vkDestroyEvent),
    E(vkSetEvent, VK64_fn_vkSetEvent),
    E(vkResetEvent, VK64_fn_vkResetEvent),
    E(vkGetEventStatus, VK64_fn_vkGetEventStatus),
    E(vkCmdSetEvent, VK64_fn_vkCmdSetEvent),
    E(vkCmdResetEvent, VK64_fn_vkCmdResetEvent),
    E(vkCmdWaitEvents, VK64_fn_vkCmdWaitEvents),
    E(vkCreateQueryPool, VK64_fn_vkCreateQueryPool),
    E(vkDestroyQueryPool, VK64_fn_vkDestroyQueryPool),
    E(vkGetQueryPoolResults, VK64_fn_vkGetQueryPoolResults),
    E(vkCmdResetQueryPool, VK64_fn_vkCmdResetQueryPool),
    E(vkCmdBeginQuery, VK64_fn_vkCmdBeginQuery),
    E(vkCmdEndQuery, VK64_fn_vkCmdEndQuery),
    E(vkCmdCopyQueryPoolResults, VK64_fn_vkCmdCopyQueryPoolResults),
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
    E(vkGetPhysicalDeviceProperties2KHR, VK64_fn_vkGetPhysicalDeviceProperties2),
    E(vkGetPhysicalDeviceFeatures, VK64_fn_vkGetPhysicalDeviceFeatures),
    E(vkGetPhysicalDeviceFeatures2, VK64_fn_vkGetPhysicalDeviceFeatures2),
    E(vkGetPhysicalDeviceFeatures2KHR, VK64_fn_vkGetPhysicalDeviceFeatures2),
    E(vkGetPhysicalDeviceMemoryProperties, VK64_fn_vkGetPhysicalDeviceMemoryProperties),
    E(vkGetPhysicalDeviceMemoryProperties2, VK64_fn_vkGetPhysicalDeviceMemoryProperties2),
    E(vkGetPhysicalDeviceMemoryProperties2KHR, VK64_fn_vkGetPhysicalDeviceMemoryProperties2),
    E(vkGetPhysicalDeviceQueueFamilyProperties, VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties),
    E(vkGetPhysicalDeviceQueueFamilyProperties2, VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties2),
    E(vkGetPhysicalDeviceQueueFamilyProperties2KHR, VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties2),
    E(vkGetPhysicalDeviceFormatProperties, VK64_fn_vkGetPhysicalDeviceFormatProperties),
    E(vkGetPhysicalDeviceFormatProperties2, VK64_fn_vkGetPhysicalDeviceFormatProperties2),
    E(vkGetPhysicalDeviceFormatProperties2KHR, VK64_fn_vkGetPhysicalDeviceFormatProperties2),
    E(vkGetPhysicalDeviceImageFormatProperties, VK64_fn_vkGetPhysicalDeviceImageFormatProperties),
    E(vkGetPhysicalDeviceImageFormatProperties2, VK64_fn_vkGetPhysicalDeviceImageFormatProperties2),
    E(vkGetPhysicalDeviceImageFormatProperties2KHR, VK64_fn_vkGetPhysicalDeviceImageFormatProperties2),
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
    E(vkGetBufferDeviceAddress, VK64_fn_vkGetBufferDeviceAddress),
    E(vkGetBufferDeviceAddressKHR, VK64_fn_vkGetBufferDeviceAddress),
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
    E(vkResetDescriptorPool, VK64_fn_vkResetDescriptorPool),
    E(vkFreeDescriptorSets, VK64_fn_vkFreeDescriptorSets),
    E(vkCmdPushDescriptorSetKHR, VK64_fn_vkCmdPushDescriptorSetKHR),
    E(vkCmdPushDescriptorSet, VK64_fn_vkCmdPushDescriptorSetKHR),
    E(vkCmdPushDescriptorSetWithTemplateKHR, VK64_fn_vkCmdPushDescriptorSetWithTemplateKHR),
    E(vkCmdPushDescriptorSetWithTemplate, VK64_fn_vkCmdPushDescriptorSetWithTemplateKHR),
    E(vkGetDescriptorSetLayoutSupport, VK64_fn_vkGetDescriptorSetLayoutSupport),
    E(vkGetDescriptorSetLayoutSupportKHR, VK64_fn_vkGetDescriptorSetLayoutSupport),
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
    E(vkCmdClearDepthStencilImage, VK64_fn_vkCmdClearDepthStencilImage),

    E(vkCmdDraw, VK64_fn_vkCmdDraw),
    E(vkCmdDrawIndexed, VK64_fn_vkCmdDrawIndexed),
    E(vkCmdBindVertexBuffers, VK64_fn_vkCmdBindVertexBuffers),
    E(vkCmdBindVertexBuffers2, VK64_fn_vkCmdBindVertexBuffers2),
    E(vkCmdBindVertexBuffers2KHR, VK64_fn_vkCmdBindVertexBuffers2),
    E(vkCmdBindVertexBuffers2EXT, VK64_fn_vkCmdBindVertexBuffers2),
    E(vkCmdBindIndexBuffer, VK64_fn_vkCmdBindIndexBuffer),
    E(vkCmdPushConstants, VK64_fn_vkCmdPushConstants),
    E(vkCmdUpdateBuffer, VK64_fn_vkCmdUpdateBuffer),
    E(vkCmdPipelineBarrier, VK64_fn_vkCmdPipelineBarrier),
    E(vkCmdCopyBufferToImage, VK64_fn_vkCmdCopyBufferToImage),
    E(vkCmdCopyImageToBuffer, VK64_fn_vkCmdCopyImageToBuffer),
    E(vkCmdCopyImageToBuffer2, VK64_fn_vkCmdCopyImageToBuffer2),
    E(vkCmdCopyImageToBuffer2KHR, VK64_fn_vkCmdCopyImageToBuffer2KHR),
    E(vkCmdCopyBuffer, VK64_fn_vkCmdCopyBuffer),
    E(vkCmdCopyBuffer2, VK64_fn_vkCmdCopyBuffer2),
    E(vkCmdCopyBuffer2KHR, VK64_fn_vkCmdCopyBuffer2KHR),
    E(vkCmdFillBuffer, VK64_fn_vkCmdFillBuffer),
    E(vkCmdClearColorImage, VK64_fn_vkCmdClearColorImage),
    E(vkCmdClearAttachments, VK64_fn_vkCmdClearAttachments),
    E(vkCmdClearAttachmentsKHR, VK64_fn_vkCmdClearAttachmentsKHR),
    E(vkCmdCopyImage, VK64_fn_vkCmdCopyImage),
    E(vkCmdCopyImage2, VK64_fn_vkCmdCopyImage2),
    E(vkCmdCopyImage2KHR, VK64_fn_vkCmdCopyImage2KHR),
    E(vkCmdCopyBufferToImage2, VK64_fn_vkCmdCopyBufferToImage2),
    E(vkCmdCopyBufferToImage2KHR, VK64_fn_vkCmdCopyBufferToImage2KHR),
    E(vkCmdBlitImage, VK64_fn_vkCmdBlitImage),
    E(vkCmdBlitImage2, VK64_fn_vkCmdBlitImage2),
    E(vkCmdBlitImage2KHR, VK64_fn_vkCmdBlitImage2KHR),
    E(vkCmdResolveImage, VK64_fn_vkCmdResolveImage),
    E(vkCmdResolveImageKHR, VK64_fn_vkCmdResolveImageKHR),
    E(vkCmdDrawIndirect, VK64_fn_vkCmdDrawIndirect),
    E(vkCmdDrawIndexedIndirect, VK64_fn_vkCmdDrawIndexedIndirect),
    E(vkCmdDrawIndirectCount, VK64_fn_vkCmdDrawIndirectCount),
    E(vkCmdDrawIndirectCountKHR, VK64_fn_vkCmdDrawIndirectCountKHR),
    E(vkCmdDrawIndexedIndirectCount, VK64_fn_vkCmdDrawIndexedIndirectCount),
    E(vkCmdExecuteCommands, VK64_fn_vkCmdExecuteCommands),
    E(vkCmdDispatch, VK64_fn_vkCmdDispatch),
    E(vkCmdDispatchBase, VK64_fn_vkCmdDispatchBase),
    E(vkCmdDispatchIndirect, VK64_fn_vkCmdDispatchIndirect),
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

// Per-name benign stubs. A name we do not implement hands back one of these
// slots instead of the shared vk64_benign, so a later call logs WHICH function
// the app actually called (the shared stub only sees the caller's first argument).
#define VK64_NSLOTS 512
static char g_missName[VK64_NSLOTS][72];
static int g_missCount;
static void* slot_hit(int i) { return vk64_benign(g_missName[i]); }
#define SLOT(i) static void* slot##i(void) { return slot_hit(i); }
SLOT(0)
SLOT(1)
SLOT(2)
SLOT(3)
SLOT(4)
SLOT(5)
SLOT(6)
SLOT(7)
SLOT(8)
SLOT(9)
SLOT(10)
SLOT(11)
SLOT(12)
SLOT(13)
SLOT(14)
SLOT(15)
SLOT(16)
SLOT(17)
SLOT(18)
SLOT(19)
SLOT(20)
SLOT(21)
SLOT(22)
SLOT(23)
SLOT(24)
SLOT(25)
SLOT(26)
SLOT(27)
SLOT(28)
SLOT(29)
SLOT(30)
SLOT(31)
SLOT(32)
SLOT(33)
SLOT(34)
SLOT(35)
SLOT(36)
SLOT(37)
SLOT(38)
SLOT(39)
SLOT(40)
SLOT(41)
SLOT(42)
SLOT(43)
SLOT(44)
SLOT(45)
SLOT(46)
SLOT(47)
SLOT(48)
SLOT(49)
SLOT(50)
SLOT(51)
SLOT(52)
SLOT(53)
SLOT(54)
SLOT(55)
SLOT(56)
SLOT(57)
SLOT(58)
SLOT(59)
SLOT(60)
SLOT(61)
SLOT(62)
SLOT(63)
SLOT(64)
SLOT(65)
SLOT(66)
SLOT(67)
SLOT(68)
SLOT(69)
SLOT(70)
SLOT(71)
SLOT(72)
SLOT(73)
SLOT(74)
SLOT(75)
SLOT(76)
SLOT(77)
SLOT(78)
SLOT(79)
SLOT(80)
SLOT(81)
SLOT(82)
SLOT(83)
SLOT(84)
SLOT(85)
SLOT(86)
SLOT(87)
SLOT(88)
SLOT(89)
SLOT(90)
SLOT(91)
SLOT(92)
SLOT(93)
SLOT(94)
SLOT(95)
SLOT(96)
SLOT(97)
SLOT(98)
SLOT(99)
SLOT(100)
SLOT(101)
SLOT(102)
SLOT(103)
SLOT(104)
SLOT(105)
SLOT(106)
SLOT(107)
SLOT(108)
SLOT(109)
SLOT(110)
SLOT(111)
SLOT(112)
SLOT(113)
SLOT(114)
SLOT(115)
SLOT(116)
SLOT(117)
SLOT(118)
SLOT(119)
SLOT(120)
SLOT(121)
SLOT(122)
SLOT(123)
SLOT(124)
SLOT(125)
SLOT(126)
SLOT(127)
SLOT(128)
SLOT(129)
SLOT(130)
SLOT(131)
SLOT(132)
SLOT(133)
SLOT(134)
SLOT(135)
SLOT(136)
SLOT(137)
SLOT(138)
SLOT(139)
SLOT(140)
SLOT(141)
SLOT(142)
SLOT(143)
SLOT(144)
SLOT(145)
SLOT(146)
SLOT(147)
SLOT(148)
SLOT(149)
SLOT(150)
SLOT(151)
SLOT(152)
SLOT(153)
SLOT(154)
SLOT(155)
SLOT(156)
SLOT(157)
SLOT(158)
SLOT(159)
SLOT(160)
SLOT(161)
SLOT(162)
SLOT(163)
SLOT(164)
SLOT(165)
SLOT(166)
SLOT(167)
SLOT(168)
SLOT(169)
SLOT(170)
SLOT(171)
SLOT(172)
SLOT(173)
SLOT(174)
SLOT(175)
SLOT(176)
SLOT(177)
SLOT(178)
SLOT(179)
SLOT(180)
SLOT(181)
SLOT(182)
SLOT(183)
SLOT(184)
SLOT(185)
SLOT(186)
SLOT(187)
SLOT(188)
SLOT(189)
SLOT(190)
SLOT(191)
SLOT(192)
SLOT(193)
SLOT(194)
SLOT(195)
SLOT(196)
SLOT(197)
SLOT(198)
SLOT(199)
SLOT(200)
SLOT(201)
SLOT(202)
SLOT(203)
SLOT(204)
SLOT(205)
SLOT(206)
SLOT(207)
SLOT(208)
SLOT(209)
SLOT(210)
SLOT(211)
SLOT(212)
SLOT(213)
SLOT(214)
SLOT(215)
SLOT(216)
SLOT(217)
SLOT(218)
SLOT(219)
SLOT(220)
SLOT(221)
SLOT(222)
SLOT(223)
SLOT(224)
SLOT(225)
SLOT(226)
SLOT(227)
SLOT(228)
SLOT(229)
SLOT(230)
SLOT(231)
SLOT(232)
SLOT(233)
SLOT(234)
SLOT(235)
SLOT(236)
SLOT(237)
SLOT(238)
SLOT(239)
SLOT(240)
SLOT(241)
SLOT(242)
SLOT(243)
SLOT(244)
SLOT(245)
SLOT(246)
SLOT(247)
SLOT(248)
SLOT(249)
SLOT(250)
SLOT(251)
SLOT(252)
SLOT(253)
SLOT(254)
SLOT(255)
SLOT(256)
SLOT(257)
SLOT(258)
SLOT(259)
SLOT(260)
SLOT(261)
SLOT(262)
SLOT(263)
SLOT(264)
SLOT(265)
SLOT(266)
SLOT(267)
SLOT(268)
SLOT(269)
SLOT(270)
SLOT(271)
SLOT(272)
SLOT(273)
SLOT(274)
SLOT(275)
SLOT(276)
SLOT(277)
SLOT(278)
SLOT(279)
SLOT(280)
SLOT(281)
SLOT(282)
SLOT(283)
SLOT(284)
SLOT(285)
SLOT(286)
SLOT(287)
SLOT(288)
SLOT(289)
SLOT(290)
SLOT(291)
SLOT(292)
SLOT(293)
SLOT(294)
SLOT(295)
SLOT(296)
SLOT(297)
SLOT(298)
SLOT(299)
SLOT(300)
SLOT(301)
SLOT(302)
SLOT(303)
SLOT(304)
SLOT(305)
SLOT(306)
SLOT(307)
SLOT(308)
SLOT(309)
SLOT(310)
SLOT(311)
SLOT(312)
SLOT(313)
SLOT(314)
SLOT(315)
SLOT(316)
SLOT(317)
SLOT(318)
SLOT(319)
SLOT(320)
SLOT(321)
SLOT(322)
SLOT(323)
SLOT(324)
SLOT(325)
SLOT(326)
SLOT(327)
SLOT(328)
SLOT(329)
SLOT(330)
SLOT(331)
SLOT(332)
SLOT(333)
SLOT(334)
SLOT(335)
SLOT(336)
SLOT(337)
SLOT(338)
SLOT(339)
SLOT(340)
SLOT(341)
SLOT(342)
SLOT(343)
SLOT(344)
SLOT(345)
SLOT(346)
SLOT(347)
SLOT(348)
SLOT(349)
SLOT(350)
SLOT(351)
SLOT(352)
SLOT(353)
SLOT(354)
SLOT(355)
SLOT(356)
SLOT(357)
SLOT(358)
SLOT(359)
SLOT(360)
SLOT(361)
SLOT(362)
SLOT(363)
SLOT(364)
SLOT(365)
SLOT(366)
SLOT(367)
SLOT(368)
SLOT(369)
SLOT(370)
SLOT(371)
SLOT(372)
SLOT(373)
SLOT(374)
SLOT(375)
SLOT(376)
SLOT(377)
SLOT(378)
SLOT(379)
SLOT(380)
SLOT(381)
SLOT(382)
SLOT(383)
SLOT(384)
SLOT(385)
SLOT(386)
SLOT(387)
SLOT(388)
SLOT(389)
SLOT(390)
SLOT(391)
SLOT(392)
SLOT(393)
SLOT(394)
SLOT(395)
SLOT(396)
SLOT(397)
SLOT(398)
SLOT(399)
SLOT(400)
SLOT(401)
SLOT(402)
SLOT(403)
SLOT(404)
SLOT(405)
SLOT(406)
SLOT(407)
SLOT(408)
SLOT(409)
SLOT(410)
SLOT(411)
SLOT(412)
SLOT(413)
SLOT(414)
SLOT(415)
SLOT(416)
SLOT(417)
SLOT(418)
SLOT(419)
SLOT(420)
SLOT(421)
SLOT(422)
SLOT(423)
SLOT(424)
SLOT(425)
SLOT(426)
SLOT(427)
SLOT(428)
SLOT(429)
SLOT(430)
SLOT(431)
SLOT(432)
SLOT(433)
SLOT(434)
SLOT(435)
SLOT(436)
SLOT(437)
SLOT(438)
SLOT(439)
SLOT(440)
SLOT(441)
SLOT(442)
SLOT(443)
SLOT(444)
SLOT(445)
SLOT(446)
SLOT(447)
SLOT(448)
SLOT(449)
SLOT(450)
SLOT(451)
SLOT(452)
SLOT(453)
SLOT(454)
SLOT(455)
SLOT(456)
SLOT(457)
SLOT(458)
SLOT(459)
SLOT(460)
SLOT(461)
SLOT(462)
SLOT(463)
SLOT(464)
SLOT(465)
SLOT(466)
SLOT(467)
SLOT(468)
SLOT(469)
SLOT(470)
SLOT(471)
SLOT(472)
SLOT(473)
SLOT(474)
SLOT(475)
SLOT(476)
SLOT(477)
SLOT(478)
SLOT(479)
SLOT(480)
SLOT(481)
SLOT(482)
SLOT(483)
SLOT(484)
SLOT(485)
SLOT(486)
SLOT(487)
SLOT(488)
SLOT(489)
SLOT(490)
SLOT(491)
SLOT(492)
SLOT(493)
SLOT(494)
SLOT(495)
SLOT(496)
SLOT(497)
SLOT(498)
SLOT(499)
SLOT(500)
SLOT(501)
SLOT(502)
SLOT(503)
SLOT(504)
SLOT(505)
SLOT(506)
SLOT(507)
SLOT(508)
SLOT(509)
SLOT(510)
SLOT(511)
static void* (*const g_slots[VK64_NSLOTS])(void) = { slot0, slot1, slot2, slot3, slot4, slot5, slot6, slot7, slot8, slot9, slot10, slot11, slot12, slot13, slot14, slot15, slot16, slot17, slot18, slot19, slot20, slot21, slot22, slot23, slot24, slot25, slot26, slot27, slot28, slot29, slot30, slot31, slot32, slot33, slot34, slot35, slot36, slot37, slot38, slot39, slot40, slot41, slot42, slot43, slot44, slot45, slot46, slot47, slot48, slot49, slot50, slot51, slot52, slot53, slot54, slot55, slot56, slot57, slot58, slot59, slot60, slot61, slot62, slot63, slot64, slot65, slot66, slot67, slot68, slot69, slot70, slot71, slot72, slot73, slot74, slot75, slot76, slot77, slot78, slot79, slot80, slot81, slot82, slot83, slot84, slot85, slot86, slot87, slot88, slot89, slot90, slot91, slot92, slot93, slot94, slot95, slot96, slot97, slot98, slot99, slot100, slot101, slot102, slot103, slot104, slot105, slot106, slot107, slot108, slot109, slot110, slot111, slot112, slot113, slot114, slot115, slot116, slot117, slot118, slot119, slot120, slot121, slot122, slot123, slot124, slot125, slot126, slot127, slot128, slot129, slot130, slot131, slot132, slot133, slot134, slot135, slot136, slot137, slot138, slot139, slot140, slot141, slot142, slot143, slot144, slot145, slot146, slot147, slot148, slot149, slot150, slot151, slot152, slot153, slot154, slot155, slot156, slot157, slot158, slot159, slot160, slot161, slot162, slot163, slot164, slot165, slot166, slot167, slot168, slot169, slot170, slot171, slot172, slot173, slot174, slot175, slot176, slot177, slot178, slot179, slot180, slot181, slot182, slot183, slot184, slot185, slot186, slot187, slot188, slot189, slot190, slot191, slot192, slot193, slot194, slot195, slot196, slot197, slot198, slot199, slot200, slot201, slot202, slot203, slot204, slot205, slot206, slot207, slot208, slot209, slot210, slot211, slot212, slot213, slot214, slot215, slot216, slot217, slot218, slot219, slot220, slot221, slot222, slot223, slot224, slot225, slot226, slot227, slot228, slot229, slot230, slot231, slot232, slot233, slot234, slot235, slot236, slot237, slot238, slot239, slot240, slot241, slot242, slot243, slot244, slot245, slot246, slot247, slot248, slot249, slot250, slot251, slot252, slot253, slot254, slot255, slot256, slot257, slot258, slot259, slot260, slot261, slot262, slot263, slot264, slot265, slot266, slot267, slot268, slot269, slot270, slot271, slot272, slot273, slot274, slot275, slot276, slot277, slot278, slot279, slot280, slot281, slot282, slot283, slot284, slot285, slot286, slot287, slot288, slot289, slot290, slot291, slot292, slot293, slot294, slot295, slot296, slot297, slot298, slot299, slot300, slot301, slot302, slot303, slot304, slot305, slot306, slot307, slot308, slot309, slot310, slot311, slot312, slot313, slot314, slot315, slot316, slot317, slot318, slot319, slot320, slot321, slot322, slot323, slot324, slot325, slot326, slot327, slot328, slot329, slot330, slot331, slot332, slot333, slot334, slot335, slot336, slot337, slot338, slot339, slot340, slot341, slot342, slot343, slot344, slot345, slot346, slot347, slot348, slot349, slot350, slot351, slot352, slot353, slot354, slot355, slot356, slot357, slot358, slot359, slot360, slot361, slot362, slot363, slot364, slot365, slot366, slot367, slot368, slot369, slot370, slot371, slot372, slot373, slot374, slot375, slot376, slot377, slot378, slot379, slot380, slot381, slot382, slot383, slot384, slot385, slot386, slot387, slot388, slot389, slot390, slot391, slot392, slot393, slot394, slot395, slot396, slot397, slot398, slot399, slot400, slot401, slot402, slot403, slot404, slot405, slot406, slot407, slot408, slot409, slot410, slot411, slot412, slot413, slot414, slot415, slot416, slot417, slot418, slot419, slot420, slot421, slot422, slot423, slot424, slot425, slot426, slot427, slot428, slot429, slot430, slot431, slot432, slot433, slot434, slot435, slot436, slot437, slot438, slot439, slot440, slot441, slot442, slot443, slot444, slot445, slot446, slot447, slot448, slot449, slot450, slot451, slot452, slot453, slot454, slot455, slot456, slot457, slot458, slot459, slot460, slot461, slot462, slot463, slot464, slot465, slot466, slot467, slot468, slot469, slot470, slot471, slot472, slot473, slot474, slot475, slot476, slot477, slot478, slot479, slot480, slot481, slot482, slot483, slot484, slot485, slot486, slot487, slot488, slot489, slot490, slot491, slot492, slot493, slot494, slot495, slot496, slot497, slot498, slot499, slot500, slot501, slot502, slot503, slot504, slot505, slot506, slot507, slot508, slot509, slot510, slot511 };
static PFN_vkVoidFunction vk64_missing_stub(const char* name) {
    int n = __atomic_load_n(&g_missCount, __ATOMIC_ACQUIRE);
    for (int i = 0; i < n; i++) if (strcmp(g_missName[i], name) == 0) return (PFN_vkVoidFunction)g_slots[i];
    int idx = __atomic_fetch_add(&g_missCount, 1, __ATOMIC_ACQ_REL);
    if (idx >= VK64_NSLOTS) return (PFN_vkVoidFunction)vk64_benign;
    int k = 0;
    for (; name[k] && k < 71; k++) g_missName[idx][k] = name[k];
    g_missName[idx][k] = 0;
    return (PFN_vkVoidFunction)g_slots[idx];
}

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
    // Unknown name: report it to the host (hit=0) so the log is the worklist,
    // then hand back the benign stub so the app keeps a callable pointer.
    {
        VK64Args a = {{0}};
        a.a[0] = (uint64_t)(uintptr_t)name;
        a.a[1] = 0;
        (void)vk64_trap(VK64_fn_traceProc, &a);
    }
    return vk64_missing_stub(name);
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
API uint64_t vkGetBufferDeviceAddress(VkDevice dev, const void* info) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(info);
    return vk64_trap(VK64_fn_vkGetBufferDeviceAddress, &a);
}
API uint64_t vkGetBufferDeviceAddressKHR(VkDevice dev, const void* info) {
    return vkGetBufferDeviceAddress(dev, info);
}
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
// =====================================================================
// Game-breaking stub implementations (replacing TAIL)
// =====================================================================

API void vkCmdFillBuffer(VkCommandBuffer commandBuffer, VkBuffer dstBuffer, uint64_t dstOffset, uint64_t size, uint32_t data) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = dstBuffer; a.a[2] = dstOffset;
    a.a[3] = size; a.a[4] = data;
    (void)vk64_trap(VK64_fn_vkCmdFillBuffer, &a);
}

API void vkCmdClearColorImage(VkCommandBuffer commandBuffer, VkImage image, uint32_t imageLayout, const void* pColor, uint32_t rangeCount, const void* pRanges) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = image; a.a[2] = imageLayout;
    a.a[3] = P(pColor); a.a[4] = rangeCount; a.a[5] = P(pRanges);
    (void)vk64_trap(VK64_fn_vkCmdClearColorImage, &a);
}

API void vkCmdClearAttachments(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const void* pAttachments, uint32_t rectCount, const void* pRects) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = attachmentCount; a.a[2] = P(pAttachments);
    a.a[3] = rectCount; a.a[4] = P(pRects);
    (void)vk64_trap(VK64_fn_vkCmdClearAttachments, &a);
}

API void vkCmdClearAttachmentsKHR(VkCommandBuffer commandBuffer, uint32_t attachmentCount, const void* pAttachments, uint32_t rectCount, const void* pRects) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = attachmentCount; a.a[2] = P(pAttachments);
    a.a[3] = rectCount; a.a[4] = P(pRects);
    (void)vk64_trap(VK64_fn_vkCmdClearAttachmentsKHR, &a);
}

API void vkCmdCopyImage(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout, VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void* pRegions) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = srcImage; a.a[2] = srcImageLayout;
    a.a[3] = dstImage; a.a[4] = dstImageLayout; a.a[5] = regionCount; a.a[6] = P(pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyImage, &a);
}

typedef struct VkCopyImageInfo2L {
    uint32_t sType; uint32_t _pad0;
    const void* pNext;
    VkImage srcImage;
    uint32_t srcImageLayout; uint32_t _pad1;
    VkImage dstImage;
    uint32_t dstImageLayout;
    uint32_t regionCount;
    const void* pRegions;
} VkCopyImageInfo2L;
_Static_assert(__builtin_offsetof(VkCopyImageInfo2L, regionCount) == 44 && __builtin_offsetof(VkCopyImageInfo2L, pRegions) == 48, "VkCopyImageInfo2 layout");

API void vkCmdCopyImage2(VkCommandBuffer commandBuffer, const void* pInfoIn) {
    const VkCopyImageInfo2L* pInfo = (const VkCopyImageInfo2L*)pInfoIn;
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = pInfo->srcImage; a.a[2] = pInfo->dstImage;
    a.a[3] = pInfo->regionCount; a.a[4] = P(pInfo->pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyImage2, &a);
}

API void vkCmdCopyImage2KHR(VkCommandBuffer commandBuffer, const void* pInfoIn) {
    const VkCopyImageInfo2L* pInfo = (const VkCopyImageInfo2L*)pInfoIn;
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = pInfo->srcImage; a.a[2] = pInfo->dstImage;
    a.a[3] = pInfo->regionCount; a.a[4] = P(pInfo->pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyImage2KHR, &a);
}

typedef struct VkCopyBufferToImageInfo2L {
    uint32_t sType; uint32_t _pad0;
    const void* pNext;
    VkBuffer srcBuffer;
    VkImage dstImage;
    uint32_t dstImageLayout;
    uint32_t regionCount;
    const void* pRegions;
} VkCopyBufferToImageInfo2L;
_Static_assert(__builtin_offsetof(VkCopyBufferToImageInfo2L, regionCount) == 36 && __builtin_offsetof(VkCopyBufferToImageInfo2L, pRegions) == 40, "VkCopyBufferToImageInfo2 layout");

API void vkCmdCopyBufferToImage2(VkCommandBuffer commandBuffer, const void* pInfoIn) {
    const VkCopyBufferToImageInfo2L* pInfo = (const VkCopyBufferToImageInfo2L*)pInfoIn;
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = pInfo->srcBuffer; a.a[2] = pInfo->dstImage;
    a.a[3] = pInfo->regionCount; a.a[4] = P(pInfo->pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyBufferToImage2, &a);
}

API void vkCmdCopyBufferToImage2KHR(VkCommandBuffer commandBuffer, const void* pInfoIn) {
    const VkCopyBufferToImageInfo2L* pInfo = (const VkCopyBufferToImageInfo2L*)pInfoIn;
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = pInfo->srcBuffer; a.a[2] = pInfo->dstImage;
    a.a[3] = pInfo->regionCount; a.a[4] = P(pInfo->pRegions);
    (void)vk64_trap(VK64_fn_vkCmdCopyBufferToImage2KHR, &a);
}

API void vkCmdBlitImage(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout, VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void* pRegions, uint32_t filter) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = srcImage; a.a[2] = srcImageLayout;
    a.a[3] = dstImage; a.a[4] = dstImageLayout; a.a[5] = regionCount;
    a.a[6] = P(pRegions); a.a[7] = filter;
    (void)vk64_trap(VK64_fn_vkCmdBlitImage, &a);
}

typedef struct VkBlitImageInfo2L {
    uint32_t sType; uint32_t _pad0;
    const void* pNext;
    VkImage srcImage;
    uint32_t srcImageLayout; uint32_t _pad1;
    VkImage dstImage;
    uint32_t dstImageLayout;
    uint32_t regionCount;
    const void* pRegions;
    uint32_t filter;
} VkBlitImageInfo2L;
_Static_assert(__builtin_offsetof(VkBlitImageInfo2L, regionCount) == 44 && __builtin_offsetof(VkBlitImageInfo2L, pRegions) == 48 && __builtin_offsetof(VkBlitImageInfo2L, filter) == 56, "VkBlitImageInfo2 layout");

API void vkCmdBlitImage2(VkCommandBuffer commandBuffer, const void* pInfoIn) {
    const VkBlitImageInfo2L* pInfo = (const VkBlitImageInfo2L*)pInfoIn;
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = pInfo->srcImage; a.a[2] = pInfo->dstImage;
    a.a[3] = pInfo->regionCount; a.a[4] = P(pInfo->pRegions); a.a[5] = pInfo->filter;
    (void)vk64_trap(VK64_fn_vkCmdBlitImage2, &a);
}

API void vkCmdBlitImage2KHR(VkCommandBuffer commandBuffer, const void* pInfoIn) {
    const VkBlitImageInfo2L* pInfo = (const VkBlitImageInfo2L*)pInfoIn;
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = pInfo->srcImage; a.a[2] = pInfo->dstImage;
    a.a[3] = pInfo->regionCount; a.a[4] = P(pInfo->pRegions); a.a[5] = pInfo->filter;
    (void)vk64_trap(VK64_fn_vkCmdBlitImage2KHR, &a);
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

API void vkCmdResolveImage(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout, VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void* pRegions) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = srcImage; a.a[2] = srcImageLayout;
    a.a[3] = dstImage; a.a[4] = dstImageLayout; a.a[5] = regionCount; a.a[6] = P(pRegions);
    (void)vk64_trap(VK64_fn_vkCmdResolveImage, &a);
}

API void vkCmdResolveImageKHR(VkCommandBuffer commandBuffer, VkImage srcImage, uint32_t srcImageLayout, VkImage dstImage, uint32_t dstImageLayout, uint32_t regionCount, const void* pRegions) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = srcImage; a.a[2] = srcImageLayout;
    a.a[3] = dstImage; a.a[4] = dstImageLayout; a.a[5] = regionCount; a.a[6] = P(pRegions);
    (void)vk64_trap(VK64_fn_vkCmdResolveImageKHR, &a);
}

API void vkCmdDrawIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = buffer; a.a[2] = offset;
    a.a[3] = drawCount; a.a[4] = stride;
    (void)vk64_trap(VK64_fn_vkCmdDrawIndirect, &a);
}

API void vkCmdDrawIndexedIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, uint64_t offset, uint32_t drawCount, uint32_t stride) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = buffer; a.a[2] = offset;
    a.a[3] = drawCount; a.a[4] = stride;
    (void)vk64_trap(VK64_fn_vkCmdDrawIndexedIndirect, &a);
}

API void vkCmdDrawIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, uint64_t offset, VkBuffer countBuffer, uint64_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = buffer; a.a[2] = offset;
    a.a[3] = countBuffer; a.a[4] = countBufferOffset; a.a[5] = maxDrawCount; a.a[6] = stride;
    (void)vk64_trap(VK64_fn_vkCmdDrawIndirectCount, &a);
}

API void vkCmdDrawIndirectCountKHR(VkCommandBuffer commandBuffer, VkBuffer buffer, uint64_t offset, VkBuffer countBuffer, uint64_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = buffer; a.a[2] = offset;
    a.a[3] = countBuffer; a.a[4] = countBufferOffset; a.a[5] = maxDrawCount; a.a[6] = stride;
    (void)vk64_trap(VK64_fn_vkCmdDrawIndirectCountKHR, &a);
}

API void vkCmdDrawIndexedIndirectCount(VkCommandBuffer commandBuffer, VkBuffer buffer, uint64_t offset, VkBuffer countBuffer, uint64_t countBufferOffset, uint32_t maxDrawCount, uint32_t stride) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = buffer; a.a[2] = offset;
    a.a[3] = countBuffer; a.a[4] = countBufferOffset; a.a[5] = maxDrawCount; a.a[6] = stride;
    (void)vk64_trap(VK64_fn_vkCmdDrawIndexedIndirectCount, &a);
}

API void vkCmdExecuteCommands(VkCommandBuffer commandBuffer, uint32_t commandBufferCount, const VkCommandBuffer* pCommandBuffers) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = commandBufferCount; a.a[2] = P(pCommandBuffers);
    (void)vk64_trap(VK64_fn_vkCmdExecuteCommands, &a);
}

API void vkCmdDispatch(VkCommandBuffer commandBuffer, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = groupCountX; a.a[2] = groupCountY; a.a[3] = groupCountZ;
    (void)vk64_trap(VK64_fn_vkCmdDispatch, &a);
}

API void vkCmdDispatchBase(VkCommandBuffer commandBuffer, uint32_t baseGroupX, uint32_t baseGroupY, uint32_t baseGroupZ, uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = baseGroupX; a.a[2] = baseGroupY; a.a[3] = baseGroupZ;
    a.a[4] = groupCountX; a.a[5] = groupCountY; a.a[6] = groupCountZ;
    (void)vk64_trap(VK64_fn_vkCmdDispatchBase, &a);
}

API void vkCmdDispatchIndirect(VkCommandBuffer commandBuffer, VkBuffer buffer, uint64_t offset) {
    VK64Args a = {{0}};
    a.a[0] = commandBuffer; a.a[1] = buffer; a.a[2] = offset;
    (void)vk64_trap(VK64_fn_vkCmdDispatchIndirect, &a);
}

API VkResult vkCreateEvent(VkDevice dev, const void* ci, const void* alloc, VkEvent* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateEvent, &a);
}
API void vkDestroyEvent(VkDevice dev, VkEvent ev, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = ev; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyEvent, &a);
}
API VkResult vkSetEvent(VkDevice dev, VkEvent ev) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = ev;
    return (VkResult)vk64_trap(VK64_fn_vkSetEvent, &a);
}
API VkResult vkResetEvent(VkDevice dev, VkEvent ev) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = ev;
    return (VkResult)vk64_trap(VK64_fn_vkResetEvent, &a);
}
API VkResult vkGetEventStatus(VkDevice dev, VkEvent ev) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = ev;
    return (VkResult)vk64_trap(VK64_fn_vkGetEventStatus, &a);
}
API void vkCmdSetEvent(VkCommandBuffer cb, VkEvent ev, uint32_t stage) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = ev; a.a[2] = stage;
    (void)vk64_trap(VK64_fn_vkCmdSetEvent, &a);
}
API void vkCmdResetEvent(VkCommandBuffer cb, VkEvent ev, uint32_t stage) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = ev; a.a[2] = stage;
    (void)vk64_trap(VK64_fn_vkCmdResetEvent, &a);
}
// Immediate execution: every earlier command already ran, so there is nothing to wait for.
API void vkCmdWaitEvents(VkCommandBuffer cb, uint32_t n, const VkEvent* evs, uint32_t srcStage, uint32_t dstStage,
                         uint32_t memN, const void* mem, uint32_t bufN, const void* buf, uint32_t imgN, const void* img) {
    (void)evs; (void)srcStage; (void)dstStage; (void)memN; (void)mem; (void)bufN; (void)buf; (void)imgN; (void)img;
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = n;
    (void)vk64_trap(VK64_fn_vkCmdWaitEvents, &a);
}

API VkResult vkCreateQueryPool(VkDevice dev, const void* ci, const void* alloc, VkQueryPool* out) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = P(ci); a.a[2] = P(alloc); a.a[3] = P(out);
    return (VkResult)vk64_trap(VK64_fn_vkCreateQueryPool, &a);
}
API void vkDestroyQueryPool(VkDevice dev, VkQueryPool pool, const void* alloc) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = pool; a.a[2] = P(alloc);
    (void)vk64_trap(VK64_fn_vkDestroyQueryPool, &a);
}
API VkResult vkGetQueryPoolResults(VkDevice dev, VkQueryPool pool, uint32_t first, uint32_t count,
                                   unsigned long dataSize, void* data, uint64_t stride, uint32_t flags) {
    VK64Args a = {{0}}; a.a[0] = dev; a.a[1] = pool; a.a[2] = first; a.a[3] = count;
    a.a[4] = dataSize; a.a[5] = P(data); a.a[6] = stride; a.a[7] = flags;
    return (VkResult)vk64_trap(VK64_fn_vkGetQueryPoolResults, &a);
}
API void vkCmdResetQueryPool(VkCommandBuffer cb, VkQueryPool pool, uint32_t first, uint32_t count) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = pool; a.a[2] = first; a.a[3] = count;
    (void)vk64_trap(VK64_fn_vkCmdResetQueryPool, &a);
}
API void vkCmdBeginQuery(VkCommandBuffer cb, VkQueryPool pool, uint32_t query, uint32_t flags) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = pool; a.a[2] = query; a.a[3] = flags;
    (void)vk64_trap(VK64_fn_vkCmdBeginQuery, &a);
}
API void vkCmdEndQuery(VkCommandBuffer cb, VkQueryPool pool, uint32_t query) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = pool; a.a[2] = query;
    (void)vk64_trap(VK64_fn_vkCmdEndQuery, &a);
}
API void vkCmdCopyQueryPoolResults(VkCommandBuffer cb, VkQueryPool pool, uint32_t first, uint32_t count,
                                   VkBuffer dst, uint64_t offset, uint64_t stride, uint32_t flags) {
    VK64Args a = {{0}}; a.a[0] = cb; a.a[1] = pool; a.a[2] = first; a.a[3] = count;
    a.a[4] = dst; a.a[5] = offset; a.a[6] = stride; a.a[7] = flags;
    (void)vk64_trap(VK64_fn_vkCmdCopyQueryPoolResults, &a);
}

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
