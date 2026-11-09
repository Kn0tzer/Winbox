/*
 * vkcov.c — systematic Vulkan bridge API coverage harness for Boxedwine64.
 *
 * Calls every vk64bridge handler through the PE vulkan-1.dll shim in-browser,
 * times each call (2 runs), and reports machine-readable results:
 *   VKCOV: <name>=<rc1>,<rc2>:<us1>:<us2>
 *   VKCOV-TRY: <name>          (before each call; crash repro marker)
 *   vkcov: RESULT 0|1
 *
 * Buckets (analyzer-assigned):
 *   working : rc==0, <10ms, rc1==rc2
 *   slow    : rc==0, >=10ms
 *   flaky   : rc1 != rc2
 *   broken  : rc==-8 (VK_ERROR_FEATURE_NOT_PRESENT = explicit stub),
 *             other errors, or crash (no RESULT; last TRY is the repro)
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -o vkcov.exe vkcov.c \
 *          -I../../dxvk/src/include/vulkan/include
 */
#define VK_NO_PROTOTYPES
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>

/* ---- PFN types missing from the Vulkan 1.3 headers ---- */
/* Real KHR/EXT aliases with known signatures: */
typedef void (VKAPI_PTR *PFN_vkCmdBindIndexBuffer2)(VkCommandBuffer, VkBuffer, VkDeviceSize, VkDeviceSize, VkIndexType);
typedef void (VKAPI_PTR *PFN_vkCmdBindVertexBuffers2KHR)(VkCommandBuffer, uint32_t, uint32_t, const VkBuffer*, const VkDeviceSize*, const VkDeviceSize*, const VkDeviceSize*);
typedef void (VKAPI_PTR *PFN_vkCmdClearAttachmentsKHR)(VkCommandBuffer, uint32_t, const VkClearAttachment*, uint32_t, const VkClearRect*);
typedef void (VKAPI_PTR *PFN_vkCmdNextSubpassKHR)(VkCommandBuffer, VkSubpassContents);
typedef void (VKAPI_PTR *PFN_vkCmdPushConstants2)(VkCommandBuffer, VkPipelineLayout, VkShaderStageFlags, uint32_t, uint32_t, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdPushDescriptorSet)(VkCommandBuffer, VkPipelineBindPoint, VkPipelineLayout, uint32_t, uint32_t, const VkWriteDescriptorSet*);
typedef void (VKAPI_PTR *PFN_vkCmdResolveImageKHR)(VkCommandBuffer, const void*);
typedef void (VKAPI_PTR *PFN_vkCmdSetBlendConstant)(VkCommandBuffer, const float[4]);
typedef void (VKAPI_PTR *PFN_vkCmdSetDepthBiasNV)(VkCommandBuffer, float, float, float);
typedef VkResult (VKAPI_PTR *PFN_vkCreateWin32SurfaceKHR)(VkInstance, const VkWin32SurfaceCreateInfoKHR*, const VkAllocationCallbacks*, VkSurfaceKHR*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateXcbSurfaceKHR)(VkInstance, const void*, const VkAllocationCallbacks*, VkSurfaceKHR*);
typedef VkResult (VKAPI_PTR *PFN_vkCreateXlibSurfaceKHR)(VkInstance, const void*, const VkAllocationCallbacks*, VkSurfaceKHR*);
typedef VkBool32 (VKAPI_PTR *PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR)(VkPhysicalDevice, uint32_t);
typedef VkBool32 (VKAPI_PTR *PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR)(VkPhysicalDevice, uint32_t, void*, uint64_t);
typedef void (VKAPI_PTR *PFN_vkGetRenderingAreaGranularity)(VkDevice, VkRenderPass, VkExtent2D*);
typedef void (VKAPI_PTR *PFN_vkGetDeviceImageSubresourceLayout)(VkDevice, const void*, VkSubresourceLayout*);
typedef void (VKAPI_PTR *PFN_vkGetImageSubresourceLayout2)(VkDevice, VkImage, const void*, void*);
typedef VkResult (VKAPI_PTR *PFN_vkGetFenceStatus2)(VkDevice, VkFence);
typedef VkResult (VKAPI_PTR *PFN_vkGetFenceStatus2KHR)(VkDevice, VkFence);
typedef VkResult (VKAPI_PTR *PFN_vkGetQueryPoolResults64)(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void*, VkDeviceSize, VkQueryResultFlags);
typedef VkResult (VKAPI_PTR *PFN_vkGetQueryPoolResults64KHR)(VkDevice, VkQueryPool, uint32_t, uint32_t, size_t, void*, VkDeviceSize, VkQueryResultFlags);
/* Shim-specific fictional exports (all TAIL stubs): generic signature is fine */
typedef VkResult (VKAPI_PTR *PFN_vkGetPhysicalDeviceSurfaceFormats2EXT)(VkPhysicalDevice, VkSurfaceKHR, uint32_t*, void*);
typedef void* (VKAPI_PTR *PFN_vkCreateFramebuffer2KHR)(void);
typedef void* (VKAPI_PTR *PFN_vkCreateSwapchainKHR2)(void);
typedef void* (VKAPI_PTR *PFN_vkDestroyDeviceKHR)(void);
typedef void* (VKAPI_PTR *PFN_vkDestroySwapchainKHR2)(void);
typedef void* (VKAPI_PTR *PFN_vkGetPipelineLayoutSupport)(void);
typedef void* (VKAPI_PTR *PFN_vkResetPipelineCache)(void);
typedef void* (VKAPI_PTR *PFN_vkSwapchainImagesKHR2)(void);


static uint64_t qpf_us(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (uint64_t)c.QuadPart * 1000000ULL / (uint64_t)f.QuadPart;
}
static void rep(const char* name, long long rc1, long long rc2,
                uint64_t us1, uint64_t us2) {
    fprintf(stderr, "VKCOV: %s=%lld,%lld:%llu:%llu\n", name, rc1, rc2,
            (unsigned long long)us1, (unsigned long long)us2);
    fflush(stderr);
}
static void trybegin(const char* name) {
    fprintf(stderr, "VKCOV-TRY: %s\n", name); fflush(stderr);
}
static void note(const char* fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt);
    wvsprintfA(b, fmt, ap); va_end(ap);
    fprintf(stderr, "VKCOV-NOTE: %s\n", b); fflush(stderr);
}

/* test macros: T2 = timed 2-run, T2V = void 2-run, T1 = single-run */
#define T2(name, expr) do { trybegin(#name); \
    uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(expr); uint64_t _t1=qpf_us(); \
    uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(expr); uint64_t _t3=qpf_us(); \
    rep(#name,_r1,_r2,_t1-_t0,_t3-_t2); } while(0)
#define T2V(name, expr) do { trybegin(#name); \
    uint64_t _t0=qpf_us(); (expr); uint64_t _t1=qpf_us(); \
    uint64_t _t2=qpf_us(); (expr); uint64_t _t3=qpf_us(); \
    rep(#name,0,0,_t1-_t0,_t3-_t2); } while(0)
#define T1(name, expr) do { trybegin(#name); \
    uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(expr); uint64_t _t1=qpf_us(); \
    rep(#name,_r1,_r1,_t1-_t0,0); } while(0)
#define T1V(name, expr) do { trybegin(#name); \
    uint64_t _t0=qpf_us(); (expr); uint64_t _t1=qpf_us(); \
    rep(#name,0,0,_t1-_t0,0); } while(0)

/* globals: objects from the setup chain */
static HMODULE vkdll;
static VkInstance inst;
static VkPhysicalDevice phys;
static VkDevice dev;
static VkQueue queue;
static uint32_t qfam;
static VkCommandPool cmdpool;
static VkCommandBuffer cmdbuf;
static VkBuffer vbuf;
static VkDeviceMemory vbufmem;
static VkImage img;
static VkDeviceMemory imgmem;
static VkImageView imgview;
static VkSampler sampler;
static VkDescriptorSetLayout dslayout;
static VkPipelineLayout playout;
static VkShaderModule vshader;
static VkRenderPass rpass;
static VkFramebuffer fb;
static VkDescriptorPool dpool;
static VkDescriptorSet dset;
static VkFence fence;
static VkSemaphore sema;
static VkEvent event;
static VkQueryPool qpool;
static VkSurfaceKHR surface;
static VkSwapchainKHR swapchain;

/* minimal SPIR-V vertex shader (hand-assembled, 29 words) */
static const uint32_t min_vert_spv[] = {
    0x07230203, 0x00010000, 0x00000000, 0x00000005, 0x00000000,
    0x00020011, 0x00000001,
    0x0003000E, 0x00000000, 0x00000001,
    0x0005000F, 0x00000000, 0x00000003, 0x6E69616D, 0x00000000,
    0x00020013, 0x00000001,
    0x00030021, 0x00000002, 0x00000001,
    0x00050036, 0x00000001, 0x00000003, 0x00000000, 0x00000002,
    0x000200F8, 0x00000004,
    0x000100FD,
    0x00010038,
};

/* ---- function pointers (all DLL exports) ---- */
#define DECLFN(n) static PFN_##n p##n = 0
DECLFN(vkAcquireNextImageKHR);
DECLFN(vkAllocateCommandBuffers);
DECLFN(vkAllocateDescriptorSets);
DECLFN(vkAllocateMemory);
DECLFN(vkBeginCommandBuffer);
DECLFN(vkBindBufferMemory);
DECLFN(vkBindBufferMemory2);
DECLFN(vkBindBufferMemory2KHR);
DECLFN(vkBindImageMemory);
DECLFN(vkBindImageMemory2);
DECLFN(vkBindImageMemory2KHR);
DECLFN(vkCmdBeginConditionalRenderingEXT);
DECLFN(vkCmdBeginQuery);
DECLFN(vkCmdBeginRenderPass);
DECLFN(vkCmdBeginRenderPass2);
DECLFN(vkCmdBeginRenderPass2KHR);
DECLFN(vkCmdBeginRendering);
DECLFN(vkCmdBeginRenderingKHR);
DECLFN(vkCmdBindDescriptorSets);
DECLFN(vkCmdBindIndexBuffer);
DECLFN(vkCmdBindIndexBuffer2);
DECLFN(vkCmdBindIndexBuffer2KHR);
DECLFN(vkCmdBindPipeline);
DECLFN(vkCmdBindVertexBuffers);
DECLFN(vkCmdBindVertexBuffers2);
DECLFN(vkCmdBindVertexBuffers2EXT);
DECLFN(vkCmdBindVertexBuffers2KHR);
DECLFN(vkCmdBlitImage);
DECLFN(vkCmdBlitImage2);
DECLFN(vkCmdBlitImage2KHR);
DECLFN(vkCmdClearAttachments);
DECLFN(vkCmdClearAttachmentsKHR);
DECLFN(vkCmdClearColorImage);
DECLFN(vkCmdCopyBuffer);
DECLFN(vkCmdCopyBuffer2);
DECLFN(vkCmdCopyBuffer2KHR);
DECLFN(vkCmdCopyBufferToImage);
DECLFN(vkCmdCopyBufferToImage2);
DECLFN(vkCmdCopyBufferToImage2KHR);
DECLFN(vkCmdCopyImage);
DECLFN(vkCmdCopyImage2);
DECLFN(vkCmdCopyImage2KHR);
DECLFN(vkCmdCopyImageToBuffer);
DECLFN(vkCmdCopyImageToBuffer2);
DECLFN(vkCmdCopyImageToBuffer2KHR);
DECLFN(vkCmdDispatch);
DECLFN(vkCmdDispatchBase);
DECLFN(vkCmdDispatchIndirect);
DECLFN(vkCmdDraw);
DECLFN(vkCmdDrawIndexed);
DECLFN(vkCmdDrawIndexedIndirect);
DECLFN(vkCmdDrawIndexedIndirectCount);
DECLFN(vkCmdDrawIndirect);
DECLFN(vkCmdDrawIndirectByteCountEXT);
DECLFN(vkCmdDrawIndirectCount);
DECLFN(vkCmdDrawIndirectCountKHR);
DECLFN(vkCmdEndConditionalRenderingEXT);
DECLFN(vkCmdEndQuery);
DECLFN(vkCmdEndRenderPass);
DECLFN(vkCmdEndRenderPass2);
DECLFN(vkCmdEndRenderPass2KHR);
DECLFN(vkCmdEndRendering);
DECLFN(vkCmdEndRenderingKHR);
DECLFN(vkCmdExecuteCommands);
DECLFN(vkCmdFillBuffer);
DECLFN(vkCmdNextSubpass);
DECLFN(vkCmdNextSubpass2);
DECLFN(vkCmdNextSubpass2KHR);
DECLFN(vkCmdNextSubpassKHR);
DECLFN(vkCmdPipelineBarrier);
DECLFN(vkCmdPipelineBarrier2);
DECLFN(vkCmdPipelineBarrier2KHR);
DECLFN(vkCmdPushConstants);
DECLFN(vkCmdPushConstants2);
DECLFN(vkCmdPushConstants2KHR);
DECLFN(vkCmdPushDescriptorSet);
DECLFN(vkCmdResetEvent);
DECLFN(vkCmdResetQueryPool);
DECLFN(vkCmdResolveImage);
DECLFN(vkCmdResolveImageKHR);
DECLFN(vkCmdSetBlendConstant);
DECLFN(vkCmdSetCullModeEXT);
DECLFN(vkCmdSetDepthBias);
DECLFN(vkCmdSetDepthBiasEnableEXT);
DECLFN(vkCmdSetDepthBiasNV);
DECLFN(vkCmdSetDepthBounds);
DECLFN(vkCmdSetDeviceMask);
DECLFN(vkCmdSetEvent);
DECLFN(vkCmdSetFrontFaceEXT);
DECLFN(vkCmdSetLineWidth);
DECLFN(vkCmdSetPrimitiveTopologyEXT);
DECLFN(vkCmdSetScissor);
DECLFN(vkCmdSetScissorWithCount);
DECLFN(vkCmdSetScissorWithCountEXT);
DECLFN(vkCmdSetStencilReference);
DECLFN(vkCmdSetVertexInputEXT);
DECLFN(vkCmdSetViewport);
DECLFN(vkCmdSetViewportWithCount);
DECLFN(vkCmdSetViewportWithCountEXT);
DECLFN(vkCmdUpdateBuffer);
DECLFN(vkCmdWaitEvents);
DECLFN(vkCmdWriteTimestamp);
DECLFN(vkCmdWriteTimestamp2);
DECLFN(vkCmdWriteTimestamp2KHR);
DECLFN(vkCreateBuffer);
DECLFN(vkCreateBufferView);
DECLFN(vkCreateCommandPool);
DECLFN(vkCreateComputePipelines);
DECLFN(vkCreateDebugUtilsMessengerEXT);
DECLFN(vkCreateDescriptorPool);
DECLFN(vkCreateDescriptorSetLayout);
DECLFN(vkCreateDescriptorUpdateTemplate);
DECLFN(vkCreateDevice);
DECLFN(vkCreateEvent);
DECLFN(vkCreateFence);
DECLFN(vkCreateFramebuffer);
DECLFN(vkCreateFramebuffer2KHR);
DECLFN(vkCreateGraphicsPipelines);
DECLFN(vkCreateHeadlessSurfaceEXT);
DECLFN(vkCreateImage);
DECLFN(vkCreateImageView);
DECLFN(vkCreateInstance);
DECLFN(vkCreatePipelineCache);
DECLFN(vkCreatePipelineLayout);
DECLFN(vkCreateQueryPool);
DECLFN(vkCreateRenderPass);
DECLFN(vkCreateSampler);
DECLFN(vkCreateSamplerYcbcrConversion);
DECLFN(vkCreateSemaphore);
DECLFN(vkCreateShaderModule);
DECLFN(vkCreateSwapchainKHR);
DECLFN(vkCreateSwapchainKHR2);
DECLFN(vkCreateWin32SurfaceKHR);
DECLFN(vkCreateXcbSurfaceKHR);
DECLFN(vkCreateXlibSurfaceKHR);
DECLFN(vkDestroyBuffer);
DECLFN(vkDestroyBufferView);
DECLFN(vkDestroyCommandPool);
DECLFN(vkDestroyDebugUtilsMessengerEXT);
DECLFN(vkDestroyDescriptorPool);
DECLFN(vkDestroyDescriptorSetLayout);
DECLFN(vkDestroyDescriptorUpdateTemplate);
DECLFN(vkDestroyDevice);
DECLFN(vkDestroyDeviceKHR);
DECLFN(vkDestroyEvent);
DECLFN(vkDestroyFence);
DECLFN(vkDestroyFramebuffer);
DECLFN(vkDestroyImage);
DECLFN(vkDestroyImageView);
DECLFN(vkDestroyInstance);
DECLFN(vkDestroyPipeline);
DECLFN(vkDestroyPipelineCache);
DECLFN(vkDestroyPipelineLayout);
DECLFN(vkDestroyQueryPool);
DECLFN(vkDestroyRenderPass);
DECLFN(vkDestroySampler);
DECLFN(vkDestroySamplerYcbcrConversion);
DECLFN(vkDestroySemaphore);
DECLFN(vkDestroyShaderModule);
DECLFN(vkDestroySurfaceKHR);
DECLFN(vkDestroySwapchainKHR);
DECLFN(vkDestroySwapchainKHR2);
DECLFN(vkDeviceWaitIdle);
DECLFN(vkEndCommandBuffer);
DECLFN(vkEnumerateDeviceExtensionProperties);
DECLFN(vkEnumerateInstanceExtensionProperties);
DECLFN(vkEnumerateInstanceLayerProperties);
DECLFN(vkEnumerateInstanceVersion);
DECLFN(vkEnumeratePhysicalDevices);
DECLFN(vkFlushMappedMemoryRanges);
DECLFN(vkFreeCommandBuffers);
DECLFN(vkFreeDescriptorSets);
DECLFN(vkFreeMemory);
DECLFN(vkGetBufferDeviceAddress);
DECLFN(vkGetBufferDeviceAddressKHR);
DECLFN(vkGetBufferMemoryRequirements);
DECLFN(vkGetBufferMemoryRequirements2);
DECLFN(vkGetBufferMemoryRequirements2KHR);
DECLFN(vkGetBufferOpaqueCaptureAddressKHR);
DECLFN(vkGetDescriptorSetLayoutSupport);
DECLFN(vkGetDescriptorSetLayoutSupportKHR);
DECLFN(vkGetDeviceBufferMemoryRequirements);
DECLFN(vkGetDeviceGroupPeerMemoryFeatures);
DECLFN(vkGetDeviceGroupPresentCapabilitiesKHR);
DECLFN(vkGetDeviceGroupSurfacePresentModesKHR);
DECLFN(vkGetDeviceImageMemoryRequirements);
DECLFN(vkGetDeviceImageSparseMemoryRequirements);
DECLFN(vkGetDeviceImageSubresourceLayout);
DECLFN(vkGetDeviceMemoryCommitment);
DECLFN(vkGetDeviceProcAddr);
DECLFN(vkGetDeviceQueue);
DECLFN(vkGetDeviceQueue2);
DECLFN(vkGetEventStatus);
DECLFN(vkGetFenceStatus);
DECLFN(vkGetFenceStatus2);
DECLFN(vkGetFenceStatus2KHR);
DECLFN(vkGetImageMemoryRequirements);
DECLFN(vkGetImageMemoryRequirements2);
DECLFN(vkGetImageMemoryRequirements2KHR);
DECLFN(vkGetImageSparseMemoryRequirements);
DECLFN(vkGetImageSubresourceLayout);
DECLFN(vkGetImageSubresourceLayout2);
DECLFN(vkGetImageSubresourceLayout2EXT);
DECLFN(vkGetInstanceProcAddr);
DECLFN(vkGetPastPresentationTimingGOOGLE);
DECLFN(vkGetPhysicalDeviceFeatures);
DECLFN(vkGetPhysicalDeviceFeatures2);
DECLFN(vkGetPhysicalDeviceFeatures2KHR);
DECLFN(vkGetPhysicalDeviceFormatProperties);
DECLFN(vkGetPhysicalDeviceFormatProperties2);
DECLFN(vkGetPhysicalDeviceFormatProperties2KHR);
DECLFN(vkGetPhysicalDeviceImageFormatProperties);
DECLFN(vkGetPhysicalDeviceImageFormatProperties2);
DECLFN(vkGetPhysicalDeviceImageFormatProperties2KHR);
DECLFN(vkGetPhysicalDeviceMemoryProperties);
DECLFN(vkGetPhysicalDeviceMemoryProperties2);
DECLFN(vkGetPhysicalDeviceMemoryProperties2KHR);
DECLFN(vkGetPhysicalDevicePresentRectanglesKHR);
DECLFN(vkGetPhysicalDeviceProperties);
DECLFN(vkGetPhysicalDeviceProperties2);
DECLFN(vkGetPhysicalDeviceProperties2KHR);
DECLFN(vkGetPhysicalDeviceQueueFamilyProperties);
DECLFN(vkGetPhysicalDeviceQueueFamilyProperties2);
DECLFN(vkGetPhysicalDeviceQueueFamilyProperties2KHR);
DECLFN(vkGetPhysicalDeviceSurfaceCapabilities2EXT);
DECLFN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
DECLFN(vkGetPhysicalDeviceSurfaceFormats2EXT);
DECLFN(vkGetPhysicalDeviceSurfaceFormatsKHR);
DECLFN(vkGetPhysicalDeviceSurfacePresentModes2EXT);
DECLFN(vkGetPhysicalDeviceSurfacePresentModesKHR);
DECLFN(vkGetPhysicalDeviceSurfaceSupportKHR);
DECLFN(vkGetPhysicalDeviceWin32PresentationSupportKHR);
DECLFN(vkGetPhysicalDeviceXlibPresentationSupportKHR);
DECLFN(vkGetPipelineCacheData);
DECLFN(vkGetPipelineLayoutSupport);
DECLFN(vkGetQueryPoolResults);
DECLFN(vkGetQueryPoolResults64);
DECLFN(vkGetQueryPoolResults64KHR);
DECLFN(vkGetRenderAreaGranularity);
DECLFN(vkGetRenderingAreaGranularity);
DECLFN(vkGetSemaphoreCounterValue);
DECLFN(vkGetSwapchainImagesKHR);
DECLFN(vkInvalidateMappedMemoryRanges);
DECLFN(vkMapMemory);
DECLFN(vkMergePipelineCaches);
DECLFN(vkQueueBindSparse);
DECLFN(vkQueuePresentKHR);
DECLFN(vkQueueSubmit);
DECLFN(vkQueueSubmit2);
DECLFN(vkQueueSubmit2KHR);
DECLFN(vkQueueWaitIdle);
DECLFN(vkResetCommandBuffer);
DECLFN(vkResetCommandPool);
DECLFN(vkResetEvent);
DECLFN(vkResetFences);
DECLFN(vkResetPipelineCache);
DECLFN(vkSetDebugUtilsObjectNameEXT);
DECLFN(vkSetDebugUtilsObjectTagEXT);
DECLFN(vkSetEvent);
DECLFN(vkSignalSemaphore);
DECLFN(vkSubmitDebugUtilsMessageEXT);
DECLFN(vkSwapchainImagesKHR2);
DECLFN(vkTrimCommandPool);
DECLFN(vkTrimCommandPoolKHR);
DECLFN(vkUnmapMemory);
DECLFN(vkUpdateDescriptorSetWithTemplate);
DECLFN(vkUpdateDescriptorSets);
DECLFN(vkWaitForFences);
DECLFN(vkWaitSemaphores);
#define LOADFN(n) do { p##n = (PFN_##n)(void*)GetProcAddress(vkdll, #n); if (!p##n) note("no export: %s", #n); } while (0)

/* ---- setup chain: creates one of every object type ---- */
static int do_setup(void) {
    VkResult r;
    VkInstance l_inst = 0;
    VkPhysicalDevice l_phys = 0;
    VkDevice l_dev = 0;
    VkQueue l_queue = 0;
    VkCommandPool l_cmdpool = 0;
    VkCommandBuffer l_cmdbuf = 0;
    VkBuffer l_vbuf = 0;
    VkDeviceMemory l_vbufmem = 0;
    VkImage l_img = 0;
    VkDeviceMemory l_imgmem = 0;
    VkImageView l_imgview = 0;
    VkSampler l_sampler = 0;
    VkDescriptorSetLayout l_dslayout = 0;
    VkPipelineLayout l_playout = 0;
    VkShaderModule l_vshader = 0;
    VkRenderPass l_rpass = 0;
    VkFramebuffer l_fb = 0;
    VkDescriptorPool l_dpool = 0;
    VkDescriptorSet l_dset = 0;
    VkFence l_fence = 0;
    VkSemaphore l_sema = 0;
    VkEvent l_event = 0;
    VkQueryPool l_qpool = 0;
    /* NOTE 2026-10-09: bridge out-pointer writes to BSS do not land
       (host-write coherency issue, filed separately); all creates
       target stack temporaries, synced to globals at the end. */
    { VkApplicationInfo ai = { VK_STRUCTURE_TYPE_APPLICATION_INFO, 0,
        "vkcov", 1, "vkcov", 1, VK_API_VERSION_1_0 };
      VkInstanceCreateInfo ci = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, 0, 0,
        &ai, 0, 0, 0, 0 };
      trybegin("vkCreateInstance[setup]");
      uint64_t t0=qpf_us(); r = pvkCreateInstance(&ci, 0, &l_inst); uint64_t t1=qpf_us();
      rep("vkCreateInstance", r, r, t1-t0, 0);
      if (r) return 1; }
    { uint32_t n = 0; pvkEnumeratePhysicalDevices(l_inst, &n, 0);
      if (!n) { note("no physical devices"); return 1; }
      VkPhysicalDevice pds[8]; uint32_t n2 = 8;
      T2(vkEnumeratePhysicalDevices, pvkEnumeratePhysicalDevices(l_inst, &n2, pds));
      l_phys = pds[0]; }
    { uint32_t n = 0; pvkGetPhysicalDeviceQueueFamilyProperties(l_phys, &n, 0);
      VkQueueFamilyProperties qps[8]; uint32_t n2 = 8;
      pvkGetPhysicalDeviceQueueFamilyProperties(l_phys, &n2, qps);
      qfam = 0; for (uint32_t i = 0; i < n2; i++)
        if (qps[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qfam = i; break; } }
    { float prio = 1.0f;
      VkDeviceQueueCreateInfo qi = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        0, 0, qfam, 1, &prio };
      VkDeviceCreateInfo ci = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO, 0, 0,
        1, &qi, 0, 0, 0, 0, 0 };
      T2(vkCreateDevice, pvkCreateDevice(l_phys, &ci, 0, &l_dev));
      if (!l_dev) return 1;
      T2V(vkGetDeviceQueue, pvkGetDeviceQueue(l_dev, qfam, 0, &l_queue)); }
    { VkCommandPoolCreateInfo ci = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        0, VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT, qfam };
      T2(vkCreateCommandPool, pvkCreateCommandPool(l_dev, &ci, 0, &l_cmdpool));
      VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        0, l_cmdpool, VK_COMMAND_BUFFER_LEVEL_PRIMARY, 1 };
      T2(vkAllocateCommandBuffers, pvkAllocateCommandBuffers(l_dev, &ai, &l_cmdbuf)); }
    { VkBufferCreateInfo ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0,
        1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, 0 };
      T2(vkCreateBuffer, pvkCreateBuffer(l_dev, &ci, 0, &l_vbuf));
      VkMemoryRequirements mr;
      T2V(vkGetBufferMemoryRequirements, pvkGetBufferMemoryRequirements(l_dev, l_vbuf, &mr));
      VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, mr.size, 0 };
      VkPhysicalDeviceMemoryProperties mp;
      pvkGetPhysicalDeviceMemoryProperties(l_phys, &mp);
      for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
        if ((mr.memoryTypeBits & (1u << i)) &&
            (mp.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
          ai.memoryTypeIndex = i; break; }
      T2(vkAllocateMemory, pvkAllocateMemory(l_dev, &ai, 0, &l_vbufmem));
      T2(vkBindBufferMemory, pvkBindBufferMemory(l_dev, l_vbuf, l_vbufmem, 0)); }
    { VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, 0, 0,
        VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { 256, 256, 1 }, 1, 1,
        VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
        VK_SHARING_MODE_EXCLUSIVE, 0, 0, VK_IMAGE_LAYOUT_UNDEFINED };
      T2(vkCreateImage, pvkCreateImage(l_dev, &ci, 0, &l_img));
      VkMemoryRequirements mr;
      T2V(vkGetImageMemoryRequirements, pvkGetImageMemoryRequirements(l_dev, l_img, &mr));
      VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, mr.size, 0 };
      T2(vkAllocateMemory, pvkAllocateMemory(l_dev, &ai, 0, &l_imgmem));
      T2(vkBindImageMemory, pvkBindImageMemory(l_dev, l_img, l_imgmem, 0)); }
    { VkImageViewCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO, 0, 0,
        l_img, VK_IMAGE_VIEW_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM,
        { VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
          VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY },
        { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 } };
      T2(vkCreateImageView, pvkCreateImageView(l_dev, &ci, 0, &l_imgview));
      VkSamplerCreateInfo sci = { VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO, 0, 0,
        VK_FILTER_NEAREST, VK_FILTER_NEAREST, VK_SAMPLER_MIPMAP_MODE_NEAREST,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,
        VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE, 0.0f, VK_FALSE, 1.0f, VK_FALSE,
        VK_COMPARE_OP_NEVER, 0.0f, 0.0f, VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK, VK_FALSE };
      T2(vkCreateSampler, pvkCreateSampler(l_dev, &sci, 0, &l_sampler)); }
    { VkDescriptorSetLayoutBinding b = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        1, VK_SHADER_STAGE_VERTEX_BIT, 0 };
      VkDescriptorSetLayoutCreateInfo ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        0, 0, 1, &b };
      T2(vkCreateDescriptorSetLayout, pvkCreateDescriptorSetLayout(l_dev, &ci, 0, &l_dslayout));
      VkPipelineLayoutCreateInfo pi = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        0, 0, 1, &l_dslayout, 0, 0 };
      T2(vkCreatePipelineLayout, pvkCreatePipelineLayout(l_dev, &pi, 0, &l_playout)); }
    { VkShaderModuleCreateInfo ci = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
        0, 0, sizeof(min_vert_spv), min_vert_spv };
      T2(vkCreateShaderModule, pvkCreateShaderModule(l_dev, &ci, 0, &l_vshader)); }
    { VkAttachmentDescription ad = { 0, VK_FORMAT_R8G8B8A8_UNORM,
        VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
      VkAttachmentReference ar = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
      VkSubpassDescription sd = { 0, VK_PIPELINE_BIND_POINT_GRAPHICS,
        0, 0, 1, &ar, 0, 0, 0, 0 };
      VkRenderPassCreateInfo ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        0, 0, 1, &ad, 1, &sd, 0, 0 };
      T2(vkCreateRenderPass, pvkCreateRenderPass(l_dev, &ci, 0, &l_rpass));
      VkFramebufferCreateInfo fi = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        0, 0, l_rpass, 1, &l_imgview, 256, 256, 1 };
      T2(vkCreateFramebuffer, pvkCreateFramebuffer(l_dev, &fi, 0, &l_fb)); }
    { VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 4 };
      VkDescriptorPoolCreateInfo ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        0, 0, 4, 1, &ps };
      T2(vkCreateDescriptorPool, pvkCreateDescriptorPool(l_dev, &ci, 0, &l_dpool));
      VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        0, l_dpool, 1, &l_dslayout };
      T2(vkAllocateDescriptorSets, pvkAllocateDescriptorSets(l_dev, &ai, &l_dset)); }
    { VkFenceCreateInfo ci = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO, 0, 0 };
      T2(vkCreateFence, pvkCreateFence(l_dev, &ci, 0, &l_fence));
      VkSemaphoreCreateInfo sci = { VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO, 0, 0 };
      T2(vkCreateSemaphore, pvkCreateSemaphore(l_dev, &sci, 0, &l_sema));
      VkEventCreateInfo eci = { VK_STRUCTURE_TYPE_EVENT_CREATE_INFO, 0, 0 };
      T2(vkCreateEvent, pvkCreateEvent(l_dev, &eci, 0, &l_event));
      VkQueryPoolCreateInfo qci = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        0, 0, VK_QUERY_TYPE_OCCLUSION, 4, 0 };
      T2(vkCreateQueryPool, pvkCreateQueryPool(l_dev, &qci, 0, &l_qpool)); }    inst = l_inst;
    phys = l_phys;
    dev = l_dev;
    queue = l_queue;
    cmdpool = l_cmdpool;
    cmdbuf = l_cmdbuf;
    vbuf = l_vbuf;
    vbufmem = l_vbufmem;
    img = l_img;
    imgmem = l_imgmem;
    imgview = l_imgview;
    sampler = l_sampler;
    dslayout = l_dslayout;
    playout = l_playout;
    vshader = l_vshader;
    rpass = l_rpass;
    fb = l_fb;
    dpool = l_dpool;
    dset = l_dset;
    fence = l_fence;
    sema = l_sema;
    event = l_event;
    qpool = l_qpool;

    return 0;
}

/* ---- per-handler tests ---- */
static void t_instance_queries(void) {
    uint32_t ver = 0;
    T2(vkEnumerateInstanceVersion, pvkEnumerateInstanceVersion(&ver));
    { uint32_t n = 0; T2(vkEnumerateInstanceExtensionProperties,
        pvkEnumerateInstanceExtensionProperties(0, &n, 0)); }
    { uint32_t n = 0; T2(vkEnumerateInstanceLayerProperties,
        pvkEnumerateInstanceLayerProperties(&n, 0)); }
    { uint32_t n = 0; T2(vkEnumerateDeviceExtensionProperties,
        pvkEnumerateDeviceExtensionProperties(phys, 0, &n, 0)); }
}
static void t_physdev_getters(void) {
    VkPhysicalDeviceProperties pp;
    T2V(vkGetPhysicalDeviceProperties, pvkGetPhysicalDeviceProperties(phys, &pp));
    VkPhysicalDeviceFeatures pf;
    T2V(vkGetPhysicalDeviceFeatures, pvkGetPhysicalDeviceFeatures(phys, &pf));
    VkPhysicalDeviceMemoryProperties mp;
    T2V(vkGetPhysicalDeviceMemoryProperties, pvkGetPhysicalDeviceMemoryProperties(phys, &mp));
    { uint32_t n = 0;
      pvkGetPhysicalDeviceQueueFamilyProperties(phys, &n, 0);
      VkQueueFamilyProperties2 q2[8]; uint32_t n2 = 8;
      for (uint32_t i = 0; i < 8; i++) {
        q2[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        q2[i].pNext = 0; }
      T2V(vkGetPhysicalDeviceQueueFamilyProperties2,
          pvkGetPhysicalDeviceQueueFamilyProperties2(phys, &n2, q2)); }
    { VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, 0, {0} };
      T2V(vkGetPhysicalDeviceFeatures2, pvkGetPhysicalDeviceFeatures2(phys, &f2)); }
    { VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, 0, {0} };
      T2V(vkGetPhysicalDeviceProperties2, pvkGetPhysicalDeviceProperties2(phys, &p2)); }
    { VkPhysicalDeviceMemoryProperties2 m2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, 0, {0} };
      T2V(vkGetPhysicalDeviceMemoryProperties2, pvkGetPhysicalDeviceMemoryProperties2(phys, &m2)); }
    { VkFormatProperties fp;
      T2V(vkGetPhysicalDeviceFormatProperties,
          pvkGetPhysicalDeviceFormatProperties(phys, VK_FORMAT_R8G8B8A8_UNORM, &fp)); }
    { VkFormatProperties2 fp2 = { VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2, 0, {0} };
      T2V(vkGetPhysicalDeviceFormatProperties2,
          pvkGetPhysicalDeviceFormatProperties2(phys, VK_FORMAT_R8G8B8A8_UNORM, &fp2)); }
    { VkImageFormatProperties ifp;
      T2(vkGetPhysicalDeviceImageFormatProperties,
         pvkGetPhysicalDeviceImageFormatProperties(phys, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT, 0, &ifp)); }
    { VkImageFormatProperties2 ifp2 = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, 0, {0} };
      VkPhysicalDeviceImageFormatInfo2 ii = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
        0, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT, 0 };
      T2(vkGetPhysicalDeviceImageFormatProperties2,
         pvkGetPhysicalDeviceImageFormatProperties2(phys, &ii, &ifp2)); }
}

static void t_memory_ops(void) {
    void* p = 0;
    T2(vkMapMemory, pvkMapMemory(dev, vbufmem, 0, 1024, 0, &p));
    if (p) { memset(p, 0xAB, 64);
        VkMappedMemoryRange rg = { VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE, 0,
            vbufmem, 0, 64 };
        T2(vkFlushMappedMemoryRanges, pvkFlushMappedMemoryRanges(dev, 1, &rg));
        T2(vkInvalidateMappedMemoryRanges, pvkInvalidateMappedMemoryRanges(dev, 1, &rg));
        T2V(vkUnmapMemory, pvkUnmapMemory(dev, vbufmem)); }
    { VkBufferMemoryRequirementsInfo2 bi2;
      VkMemoryRequirements2 m22;
      bi2.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_REQUIREMENTS_INFO_2;
      bi2.pNext = 0; bi2.buffer = vbuf;
      m22.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
      m22.pNext = 0; memset(&m22.memoryRequirements, 0, sizeof(m22.memoryRequirements));
      T2V(vkGetBufferMemoryRequirements2, pvkGetBufferMemoryRequirements2(dev, &bi2, &m22)); }
    { VkImageMemoryRequirementsInfo2 ii = { VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2, 0, img };
      VkMemoryRequirements2 m2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, 0, {0} };
      T2V(vkGetImageMemoryRequirements2, pvkGetImageMemoryRequirements2(dev, &ii, &m2)); }
    { VkDeviceBufferMemoryRequirements db = { VK_STRUCTURE_TYPE_DEVICE_BUFFER_MEMORY_REQUIREMENTS, 0, 0 };
      db.pCreateInfo = &(VkBufferCreateInfo){ VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0,
        1024, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, 0 };
      VkMemoryRequirements2 m2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, 0, {0} };
      T2V(vkGetDeviceBufferMemoryRequirements, pvkGetDeviceBufferMemoryRequirements(dev, &db, &m2)); }
    { VkBindBufferMemoryInfo bi = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, 0, vbuf, vbufmem, 0 };
      T2(vkBindBufferMemory2, pvkBindBufferMemory2(dev, 1, &bi)); }
    { VkBindBufferMemoryInfo bi = { VK_STRUCTURE_TYPE_BIND_BUFFER_MEMORY_INFO, 0, vbuf, vbufmem, 0 };
      T2(vkBindBufferMemory2KHR, pvkBindBufferMemory2KHR(dev, 1, &bi)); }
    { VkBindImageMemoryInfo ii = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, 0, img, imgmem, 0 };
      T2(vkBindImageMemory2, pvkBindImageMemory2(dev, 1, &ii)); }
    { VkBindImageMemoryInfo ii = { VK_STRUCTURE_TYPE_BIND_IMAGE_MEMORY_INFO, 0, img, imgmem, 0 };
      T2(vkBindImageMemory2KHR, pvkBindImageMemory2KHR(dev, 1, &ii)); }
    { VkImageSubresource sr = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0 };
      VkSubresourceLayout lo;
      T2V(vkGetImageSubresourceLayout, pvkGetImageSubresourceLayout(dev, img, &sr, &lo)); }
    { VkBufferDeviceAddressInfo ai = { VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, 0, vbuf };
      uint64_t va1 = 0, va2 = 0; uint64_t t0, t1, t2, t3;
      trybegin("vkGetBufferDeviceAddress");
      t0=qpf_us(); va1 = pvkGetBufferDeviceAddress(dev, &ai); t1=qpf_us();
      t2=qpf_us(); va2 = pvkGetBufferDeviceAddress(dev, &ai); t3=qpf_us();
      rep("vkGetBufferDeviceAddress", (long long)va1 != 0, (long long)va2 != 0, t1-t0, t3-t2);
      note("buffer VA: 0x%llx (stable=%d)", (unsigned long long)va1, va1 == va2); }
}

static void t_views_descriptors(void) {
    { VkBufferViewCreateInfo ci = { VK_STRUCTURE_TYPE_BUFFER_VIEW_CREATE_INFO, 0, 0,
        vbuf, VK_FORMAT_R32_SFLOAT, 0, 256 };
      VkBufferView bv = 0;
      T2(vkCreateBufferView, pvkCreateBufferView(dev, &ci, 0, &bv));
      if (bv) pvkDestroyBufferView(dev, bv, 0); }
    { VkDescriptorBufferInfo bi = { vbuf, 0, 1024 };
      VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0,
        dset, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &bi, 0 };
      T2V(vkUpdateDescriptorSets, pvkUpdateDescriptorSets(dev, 1, &w, 0, 0)); }
    { VkDescriptorUpdateTemplateEntry en = { 0, 0, 1,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, sizeof(VkDescriptorBufferInfo) };
      VkDescriptorUpdateTemplateCreateInfo ci = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, 0, 0,
        1, &en, VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET,
        dslayout, VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 0 };
      VkDescriptorUpdateTemplate tmpl = 0;
      T2(vkCreateDescriptorUpdateTemplate,
         pvkCreateDescriptorUpdateTemplate(dev, &ci, 0, &tmpl));
      if (tmpl) {
        VkDescriptorBufferInfo bi = { vbuf, 0, 1024 };
        T2V(vkUpdateDescriptorSetWithTemplate,
            pvkUpdateDescriptorSetWithTemplate(dev, dset, tmpl, &bi));
        T2V(vkDestroyDescriptorUpdateTemplate,
            pvkDestroyDescriptorUpdateTemplate(dev, tmpl, 0)); } }
}

static void t_cmd_all(void) {
    VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, 0 };
    T2(vkBeginCommandBuffer, pvkBeginCommandBuffer(cmdbuf, &bi));
    /* render pass begin/end variants */
    { VkClearValue cv; cv.color.float32[0]=0; cv.color.float32[1]=0;
      cv.color.float32[2]=0; cv.color.float32[3]=1;
      VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, 0,
        rpass, fb, { {0,0}, {256,256} }, 1, &cv };
      T2V(vkCmdBeginRenderPass, pvkCmdBeginRenderPass(cmdbuf, &rbi, VK_SUBPASS_CONTENTS_INLINE));
      T2V(vkCmdEndRenderPass, pvkCmdEndRenderPass(cmdbuf)); }
    { VkClearValue cv = {0};
      VkRenderPassBeginInfo rbi = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO, 0,
        rpass, fb, { {0,0}, {256,256} }, 1, &cv };
      { VkSubpassBeginInfo sbi = { VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO, 0, VK_SUBPASS_CONTENTS_INLINE };
      T2V(vkCmdBeginRenderPass2, pvkCmdBeginRenderPass2(cmdbuf, &rbi, &sbi)); }
      { VkSubpassEndInfo ei = { VK_STRUCTURE_TYPE_SUBPASS_END_INFO, 0, 0 };
      T2V(vkCmdEndRenderPass2, pvkCmdEndRenderPass2(cmdbuf, &ei)); }
      { VkSubpassBeginInfo sbi2 = { VK_STRUCTURE_TYPE_SUBPASS_BEGIN_INFO, 0, VK_SUBPASS_CONTENTS_INLINE };
      T2V(vkCmdBeginRenderPass2KHR, pvkCmdBeginRenderPass2KHR(cmdbuf, &rbi, &sbi2)); }
      { VkSubpassEndInfo ei2 = { VK_STRUCTURE_TYPE_SUBPASS_END_INFO, 0, 0 };
      T2V(vkCmdEndRenderPass2KHR, pvkCmdEndRenderPass2KHR(cmdbuf, &ei2)); } }
    { VkRenderingInfo ri = { VK_STRUCTURE_TYPE_RENDERING_INFO, 0, 0,
        { {0,0}, {256,256} }, 1, 0, 0, 0, 0 };
      T2V(vkCmdBeginRendering, pvkCmdBeginRendering(cmdbuf, &ri));
      T2V(vkCmdEndRendering, pvkCmdEndRendering(cmdbuf));
      T2V(vkCmdBeginRenderingKHR, pvkCmdBeginRenderingKHR(cmdbuf, &ri));
      T2V(vkCmdEndRenderingKHR, pvkCmdEndRenderingKHR(cmdbuf)); }
    /* binds */
    T2V(vkCmdBindPipeline, pvkCmdBindPipeline(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, 0));
    { VkBuffer bs[1] = { vbuf }; VkDeviceSize off[1] = { 0 };
      T2V(vkCmdBindVertexBuffers, pvkCmdBindVertexBuffers(cmdbuf, 0, 1, bs, off));
      T2V(vkCmdBindVertexBuffers2, pvkCmdBindVertexBuffers2(cmdbuf, 0, 1, bs, off, 0, 0));
      T2V(vkCmdBindIndexBuffer, pvkCmdBindIndexBuffer(cmdbuf, vbuf, 0, VK_INDEX_TYPE_UINT16));
      T2V(vkCmdBindIndexBuffer2, pvkCmdBindIndexBuffer2(cmdbuf, vbuf, 0, 256, VK_INDEX_TYPE_UINT16));
      T2V(vkCmdBindIndexBuffer2KHR, pvkCmdBindIndexBuffer2KHR(cmdbuf, vbuf, 0, 256, VK_INDEX_TYPE_UINT16)); }
    { T2V(vkCmdBindDescriptorSets, pvkCmdBindDescriptorSets(cmdbuf,
        VK_PIPELINE_BIND_POINT_GRAPHICS, playout, 0, 1, &dset, 0, 0)); }
    /* draws */
    T2V(vkCmdDraw, pvkCmdDraw(cmdbuf, 3, 1, 0, 0));
    T2V(vkCmdDrawIndexed, pvkCmdDrawIndexed(cmdbuf, 3, 1, 0, 0, 0));
    T2V(vkCmdDrawIndirect, pvkCmdDrawIndirect(cmdbuf, vbuf, 0, 1, 16));
    T2V(vkCmdDrawIndexedIndirect, pvkCmdDrawIndexedIndirect(cmdbuf, vbuf, 0, 1, 20));
    T2V(vkCmdDrawIndirectCount, pvkCmdDrawIndirectCount(cmdbuf, vbuf, 0, vbuf, 512, 1, 16));
    T2V(vkCmdDrawIndirectCountKHR, pvkCmdDrawIndirectCountKHR(cmdbuf, vbuf, 0, vbuf, 512, 1, 16));
    T2V(vkCmdDrawIndexedIndirectCount, pvkCmdDrawIndexedIndirectCount(cmdbuf, vbuf, 0, vbuf, 512, 1, 20));
    T2V(vkCmdDispatch, pvkCmdDispatch(cmdbuf, 1, 1, 1));
    T2V(vkCmdDispatchBase, pvkCmdDispatchBase(cmdbuf, 0, 0, 0, 1, 1, 1));
    T2V(vkCmdDispatchIndirect, pvkCmdDispatchIndirect(cmdbuf, vbuf, 0));
    /* barriers */
    { VkMemoryBarrier mb = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, 0,
        VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT };
      T2V(vkCmdPipelineBarrier, pvkCmdPipelineBarrier(cmdbuf,
        VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT,
        0, 1, &mb, 0, 0, 0, 0));
      VkMemoryBarrier2 mb2 = { VK_STRUCTURE_TYPE_MEMORY_BARRIER_2, 0,
        VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_2_VERTEX_INPUT_BIT, VK_ACCESS_2_VERTEX_ATTRIBUTE_READ_BIT };
      VkDependencyInfo di = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO, 0, 0, 1, &mb2, 0, 0, 0, 0 };
      T2V(vkCmdPipelineBarrier2, pvkCmdPipelineBarrier2(cmdbuf, &di));
      T2V(vkCmdPipelineBarrier2KHR, pvkCmdPipelineBarrier2KHR(cmdbuf, &di)); }
    /* push constants */
    { uint32_t vals[4] = { 1, 2, 3, 4 };
      T2V(vkCmdPushConstants, pvkCmdPushConstants(cmdbuf, playout,
        VK_SHADER_STAGE_VERTEX_BIT, 0, 16, vals));
      T2V(vkCmdPushConstants2, pvkCmdPushConstants2(cmdbuf, playout,
        VK_SHADER_STAGE_VERTEX_BIT, 0, 16, vals));
      { VkPushConstantsInfoKHR pci = { VK_STRUCTURE_TYPE_PUSH_CONSTANTS_INFO_KHR, 0, playout, VK_SHADER_STAGE_VERTEX_BIT, 0, 16, vals };
      T2V(vkCmdPushConstants2KHR, pvkCmdPushConstants2KHR(cmdbuf, &pci)); } }
    /* viewport/scissor */
    { VkViewport vp = { 0, 0, 256, 256, 0.0f, 1.0f };
      T2V(vkCmdSetViewport, pvkCmdSetViewport(cmdbuf, 0, 1, &vp));
      VkRect2D sc = { {0,0}, {256,256} };
      T2V(vkCmdSetScissor, pvkCmdSetScissor(cmdbuf, 0, 1, &sc)); }
    /* copies */
    { VkBufferCopy bc = { 0, 0, 256 };
      T2V(vkCmdCopyBuffer, pvkCmdCopyBuffer(cmdbuf, vbuf, vbuf, 1, &bc));
      VkBufferCopy2 bc2 = { VK_STRUCTURE_TYPE_BUFFER_COPY_2, 0, 0, 0, 256 };
      VkCopyBufferInfo2 cbi = { VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2, 0, vbuf, vbuf, 1, &bc2 };
      /* DIAG 2026-10-09: re-enabled for crasher root-causing (bridge has klogs) */
      T2V(vkCmdCopyBuffer2, pvkCmdCopyBuffer2(cmdbuf, &cbi));
      T2V(vkCmdCopyBuffer2KHR, pvkCmdCopyBuffer2KHR(cmdbuf, &cbi)); }
    { VkImageSubresourceLayers sl = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      VkOffset3D off = { 0, 0, 0 }; VkExtent3D ex = { 64, 64, 1 };
      VkBufferImageCopy bic = { 0, 0, 0, sl, off, ex };
      T2V(vkCmdCopyBufferToImage, pvkCmdCopyBufferToImage(cmdbuf, vbuf, img,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic));
      VkBufferImageCopy2 bic2 = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2, 0,
        0, 0, 0, sl, off, ex };
      VkCopyBufferToImageInfo2 cb2i = { VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2, 0,
        vbuf, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &bic2 };
      T2V(vkCmdCopyBufferToImage2, pvkCmdCopyBufferToImage2(cmdbuf, &cb2i));
      T2V(vkCmdCopyBufferToImage2KHR, pvkCmdCopyBufferToImage2KHR(cmdbuf, &cb2i)); }
    { VkImageSubresourceLayers sl = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      VkOffset3D off = { 0, 0, 0 }; VkExtent3D ex = { 64, 64, 1 };
      VkImageCopy ic = { sl, off, sl, off, ex };
      /* vkCmdCopyImage SKIPPED 2026-10-09: bridge throws C++ exception
         (WebAssembly.Exception), crashing the guest. Valid args (64x64 img,
         1 region). Filed as BUG-vkCmdCopyImage.md; -2 = crashed. */
      note("SKIP vkCmdCopyImage: known crasher");
      trybegin("vkCmdCopyImage"); rep("vkCmdCopyImage",-2,-2,0,0);
      note("SKIP vkCmdCopyImage2: known crasher (same code path)");
      trybegin("vkCmdCopyImage2"); rep("vkCmdCopyImage2",-2,-2,0,0);
      note("SKIP vkCmdCopyImage2KHR: known crasher (same code path)");
      trybegin("vkCmdCopyImage2KHR"); rep("vkCmdCopyImage2KHR",-2,-2,0,0); }
    { VkImageSubresourceLayers sl = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      VkOffset3D off = { 0, 0, 0 }; VkExtent3D ex = { 64, 64, 1 };
      VkBufferImageCopy bic = { 0, 0, 0, sl, off, ex };
      T2V(vkCmdCopyImageToBuffer, pvkCmdCopyImageToBuffer(cmdbuf, img,
        VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vbuf, 1, &bic));
      VkBufferImageCopy2 bic2 = { VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2, 0,
        0, 0, 0, sl, off, ex };
      VkCopyImageToBufferInfo2 ci2 = { VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2, 0,
        img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, vbuf, 1, &bic2 };
      /* vkCmdCopyImageToBuffer2 SKIPPED 2026-10-09: bridge throws C++ exception
         (WebAssembly.Exception), crashing the guest. Valid args. Filed as
         BUG-vkCmdCopyImageToBuffer2.md; -2 = crashed. */
      note("SKIP vkCmdCopyImageToBuffer2: known crasher");
      trybegin("vkCmdCopyImageToBuffer2"); rep("vkCmdCopyImageToBuffer2",-2,-2,0,0);
      note("SKIP vkCmdCopyImageToBuffer2KHR: known crasher (same code path)");
      trybegin("vkCmdCopyImageToBuffer2KHR"); rep("vkCmdCopyImageToBuffer2KHR",-2,-2,0,0); }
    { VkImageSubresourceLayers sl = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      VkOffset3D off = { 0, 0, 0 }; VkExtent3D ex = { 32, 32, 1 };
      VkImageBlit blit = { sl, { off, {32,32,1} }, sl, { off, {16,16,1} } };
      T2V(vkCmdBlitImage, pvkCmdBlitImage(cmdbuf, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_NEAREST));
      VkImageBlit2 blit2 = { VK_STRUCTURE_TYPE_IMAGE_BLIT_2, 0, sl,
        { off, {32,32,1} }, sl, { off, {16,16,1} } };
      VkBlitImageInfo2 bii = { VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2, 0,
        img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit2, VK_FILTER_NEAREST };
      T2V(vkCmdBlitImage2, pvkCmdBlitImage2(cmdbuf, &bii));
      T2V(vkCmdBlitImage2KHR, pvkCmdBlitImage2KHR(cmdbuf, &bii)); }
    { VkImageSubresourceLayers sl = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      VkOffset3D off = { 0, 0, 0 }; VkExtent3D ex = { 64, 64, 1 };
      VkImageResolve rs = { sl, off, sl, off, ex };
      T2V(vkCmdResolveImage, pvkCmdResolveImage(cmdbuf, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rs));
      VkImageResolve2 rs2 = { VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2, 0, sl, off, sl, off, ex };
      VkResolveImageInfo2 rii = { VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2, 0,
        img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &rs2 };
      T2V(vkCmdResolveImageKHR, pvkCmdResolveImageKHR(cmdbuf, &rii)); }
    /* clears */
    { VkClearAttachment ca; ca.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
      ca.colorAttachment = 0; ca.clearValue.color.float32[0] = 1.0f;
      VkClearRect cr = { { {0,0}, {64,64} }, 0, 1 };
      T2V(vkCmdClearAttachments, pvkCmdClearAttachments(cmdbuf, 1, &ca, 1, &cr));
      T2V(vkCmdClearAttachmentsKHR, pvkCmdClearAttachmentsKHR(cmdbuf, 1, &ca, 1, &cr)); }
    { VkClearColorValue cv; cv.float32[0]=1; cv.float32[1]=0; cv.float32[2]=0; cv.float32[3]=1;
      VkImageSubresourceRange rg = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
      T2V(vkCmdClearColorImage, pvkCmdClearColorImage(cmdbuf, img,
        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cv, 1, &rg)); }
    /* fill/update */
    T2V(vkCmdFillBuffer, pvkCmdFillBuffer(cmdbuf, vbuf, 0, 256, 0x12345678));
    { uint32_t d[4] = { 1, 2, 3, 4 };
      T2V(vkCmdUpdateBuffer, pvkCmdUpdateBuffer(cmdbuf, vbuf, 0, 16, d)); }
    T2(vkEndCommandBuffer, pvkEndCommandBuffer(cmdbuf));
    T2(vkResetCommandBuffer, pvkResetCommandBuffer(cmdbuf, 0));
    T2(vkResetCommandPool, pvkResetCommandPool(dev, cmdpool, 0));
}

static void t_queue_sync(void) {
    { VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, 0,
        0, 0, 0, 1, &cmdbuf, 0, 0 };
      T2(vkQueueSubmit, pvkQueueSubmit(queue, 1, &si, fence)); }
    { VkCommandBufferSubmitInfo cbi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO,
        0, cmdbuf, 0 };
      VkSubmitInfo2 si2 = { VK_STRUCTURE_TYPE_SUBMIT_INFO_2, 0, 0, 0, 0, 1, &cbi, 0, 0 };
      T2(vkQueueSubmit2, pvkQueueSubmit2(queue, 1, &si2, fence));
      T2(vkQueueSubmit2KHR, pvkQueueSubmit2KHR(queue, 1, &si2, fence)); }
    T2(vkQueueWaitIdle, pvkQueueWaitIdle(queue));
    T2(vkDeviceWaitIdle, pvkDeviceWaitIdle(dev));
    T2(vkGetFenceStatus, pvkGetFenceStatus(dev, fence));
    T2(vkWaitForFences, pvkWaitForFences(dev, 1, &fence, VK_TRUE, 1000000));
    T2(vkResetFences, pvkResetFences(dev, 1, &fence));
    { uint64_t v = 0;
      T2(vkGetSemaphoreCounterValue, pvkGetSemaphoreCounterValue(dev, sema, &v)); }
    { VkSemaphoreSignalInfo si = { VK_STRUCTURE_TYPE_SEMAPHORE_SIGNAL_INFO, 0, sema, 1 };
      T2(vkSignalSemaphore, pvkSignalSemaphore(dev, &si)); }
    { VkSemaphoreWaitInfo wi = { VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO, 0, 0, 1, &sema, &(uint64_t){1} };
      T2(vkWaitSemaphores, pvkWaitSemaphores(dev, &wi, 1000000)); }
}
static void t_pipelines(void) {
    { VkComputePipelineCreateInfo ci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, 0, 0,
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0,
          VK_SHADER_STAGE_COMPUTE_BIT, vshader, "main", 0 },
        playout, 0, 0 };
      VkPipeline p = 0;
      T2(vkCreateComputePipelines, pvkCreateComputePipelines(dev, 0, 1, &ci, 0, &p));
      if (p) pvkDestroyPipeline(dev, p, 0); }
    { VkPipelineShaderStageCreateInfo st = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        0, 0, VK_SHADER_STAGE_VERTEX_BIT, vshader, "main", 0 };
      VkVertexInputBindingDescription vb = { 0, 12, VK_VERTEX_INPUT_RATE_VERTEX };
      VkVertexInputAttributeDescription va = { 0, 0, VK_FORMAT_R32G32B32_SFLOAT, 0 };
      VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        0, 0, 1, &vb, 1, &va };
      VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        0, 0, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE };
      VkViewport vp = { 0, 0, 256, 256, 0, 1 };
      VkRect2D sc = { {0,0}, {256,256} };
      VkPipelineViewportStateCreateInfo vw = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        0, 0, 1, &vp, 1, &sc };
      VkPipelineRasterizationStateCreateInfo rs = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        0, 0, VK_FALSE, VK_FALSE, VK_POLYGON_MODE_FILL, VK_CULL_MODE_NONE,
        VK_FRONT_FACE_COUNTER_CLOCKWISE, VK_FALSE, 0, 0, 0, 1.0f };
      VkPipelineMultisampleStateCreateInfo ms = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        0, 0, VK_SAMPLE_COUNT_1_BIT, VK_FALSE, 0, 0, 0, VK_FALSE, VK_FALSE };
      VkPipelineColorBlendAttachmentState cb = { VK_FALSE, 0, 0, 0, 0, 0, 0,
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT };
      VkPipelineColorBlendStateCreateInfo bl = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        0, 0, VK_FALSE, VK_LOGIC_OP_CLEAR, 1, &cb, {0,0,0,0} };
      VkGraphicsPipelineCreateInfo ci = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO, 0, 0,
        1, &st, &vi, &ia, 0, &vw, &rs, &ms, 0, &bl, 0, playout, rpass, 0, 0, 0 };
      VkPipeline p = 0;
      T2(vkCreateGraphicsPipelines, pvkCreateGraphicsPipelines(dev, 0, 1, &ci, 0, &p));
      if (p) pvkDestroyPipeline(dev, p, 0); }
    { VkPipelineCacheCreateInfo ci = { VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO, 0, 0, 0, 0 };
      VkPipelineCache pc = 0;
      T2(vkCreatePipelineCache, pvkCreatePipelineCache(dev, &ci, 0, &pc));
      if (pc) { size_t n = 0;
        T2(vkGetPipelineCacheData, pvkGetPipelineCacheData(dev, pc, &n, 0));
        T2V(vkDestroyPipelineCache, pvkDestroyPipelineCache(dev, pc, 0)); } }
}

static void t_surface_swapchain(void) {
    VkSurfaceKHR l_surface = 0;
    VkSwapchainKHR l_swapchain = 0;
    /* NOTE 2026-10-09: stack temporaries; see do_setup. */
    { VkHeadlessSurfaceCreateInfoEXT ci = { VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT, 0, 0 };
      T2(vkCreateHeadlessSurfaceEXT, pvkCreateHeadlessSurfaceEXT(inst, &ci, 0, &l_surface)); }
    if (l_surface) {
      VkBool32 sup = 0;
      T2(vkGetPhysicalDeviceSurfaceSupportKHR,
         pvkGetPhysicalDeviceSurfaceSupportKHR(phys, qfam, l_surface, &sup));
      { VkSurfaceCapabilitiesKHR caps;
        T2(vkGetPhysicalDeviceSurfaceCapabilitiesKHR,
           pvkGetPhysicalDeviceSurfaceCapabilitiesKHR(phys, l_surface, &caps)); }
      { uint32_t n = 0;
        T2(vkGetPhysicalDeviceSurfaceFormatsKHR,
           pvkGetPhysicalDeviceSurfaceFormatsKHR(phys, l_surface, &n, 0)); }
      { uint32_t n = 0;
        T2(vkGetPhysicalDeviceSurfacePresentModesKHR,
           pvkGetPhysicalDeviceSurfacePresentModesKHR(phys, l_surface, &n, 0)); }
      { VkSwapchainCreateInfoKHR ci = { VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR, 0, 0,
          l_surface, 2, VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,
          { 256, 256 }, 1, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
          VK_SHARING_MODE_EXCLUSIVE, 0, 0, VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR,
          VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_PRESENT_MODE_FIFO_KHR, VK_TRUE, 0 };
        T2(vkCreateSwapchainKHR, pvkCreateSwapchainKHR(dev, &ci, 0, &l_swapchain));
        if (l_swapchain) {
          uint32_t n = 0;
          T2(vkGetSwapchainImagesKHR, pvkGetSwapchainImagesKHR(dev, l_swapchain, &n, 0));
          { uint32_t idx = 0;
            T2(vkAcquireNextImageKHR, pvkAcquireNextImageKHR(dev, l_swapchain,
               1000000, sema, 0, &idx)); }
          { VkPresentInfoKHR pi = { VK_STRUCTURE_TYPE_PRESENT_INFO_KHR, 0,
              0, 0, 1, &l_swapchain, &(uint32_t){0}, 0 };
            T2(vkQueuePresentKHR, pvkQueuePresentKHR(queue, &pi)); }
          T1V(vkDestroySwapchainKHR, pvkDestroySwapchainKHR(dev, l_swapchain, 0));
          l_swapchain = 0; } }
      T1V(vkDestroySurfaceKHR, pvkDestroySurfaceKHR(inst, l_surface, 0));
      l_surface = 0; }
    { VkWin32SurfaceCreateInfoKHR ci = { VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR,
        0, 0, 0, 0 };
      VkSurfaceKHR s = 0;
      T2(vkCreateWin32SurfaceKHR, pvkCreateWin32SurfaceKHR(inst, &ci, 0, &s));
      if (s) pvkDestroySurfaceKHR(inst, s, 0); }
    { char ci[32]; memset(ci, 0, sizeof(ci));
      VkSurfaceKHR s = 0;
      T2(vkCreateXlibSurfaceKHR, pvkCreateXlibSurfaceKHR(inst, ci, 0, &s));
      if (s) pvkDestroySurfaceKHR(inst, s, 0); }
    { char ci2[32]; memset(ci2, 0, sizeof(ci2));
      VkSurfaceKHR s = 0;
      T2(vkCreateXcbSurfaceKHR, pvkCreateXcbSurfaceKHR(inst, ci2, 0, &s));
      if (s) pvkDestroySurfaceKHR(inst, s, 0); }
    T2(vkGetPhysicalDeviceWin32PresentationSupportKHR,
       pvkGetPhysicalDeviceWin32PresentationSupportKHR(phys, qfam));
    T2(vkGetPhysicalDeviceXlibPresentationSupportKHR,
       pvkGetPhysicalDeviceXlibPresentationSupportKHR(phys, qfam, 0, 0));
    { uint64_t dummy = 0;
      T2(vkGetPastPresentationTimingGOOGLE,
         pvkGetPastPresentationTimingGOOGLE(dev, l_swapchain, &dummy, 0)); }    surface = l_surface;
    swapchain = l_swapchain;

}

static void t_destroy_all(void) {
    { VkBufferCreateInfo ci = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO, 0, 0,
        64, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, 0 };
      VkBuffer b = 0;
      if (!pvkCreateBuffer(dev, &ci, 0, &b) && b)
        T1V(vkDestroyBuffer, pvkDestroyBuffer(dev, b, 0)); }
    { VkImageCreateInfo ci = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, 0, 0,
        VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { 16, 16, 1 }, 1, 1,
        VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, 0,
        VK_IMAGE_LAYOUT_UNDEFINED };
      VkImage im = 0;
      if (!pvkCreateImage(dev, &ci, 0, &im) && im)
        T1V(vkDestroyImage, pvkDestroyImage(dev, im, 0)); }
    T1V(vkDestroyImageView, pvkDestroyImageView(dev, imgview, 0));
    T1V(vkDestroySampler, pvkDestroySampler(dev, sampler, 0));
    T1V(vkDestroyShaderModule, pvkDestroyShaderModule(dev, vshader, 0));
    T1V(vkDestroyPipelineLayout, pvkDestroyPipelineLayout(dev, playout, 0));
    T1V(vkDestroyRenderPass, pvkDestroyRenderPass(dev, rpass, 0));
    T1V(vkDestroyFramebuffer, pvkDestroyFramebuffer(dev, fb, 0));
    T1V(vkDestroyDescriptorSetLayout, pvkDestroyDescriptorSetLayout(dev, dslayout, 0));
    T1V(vkDestroyDescriptorPool, pvkDestroyDescriptorPool(dev, dpool, 0));
    T1V(vkDestroyFence, pvkDestroyFence(dev, fence, 0));
    T1V(vkDestroySemaphore, pvkDestroySemaphore(dev, sema, 0));
    T1V(vkDestroyEvent, pvkDestroyEvent(dev, event, 0));
    T1V(vkDestroyQueryPool, pvkDestroyQueryPool(dev, qpool, 0));
    { VkMemoryAllocateInfo ai = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO, 0, 4096, 0 };
      VkDeviceMemory m = 0;
      if (!pvkAllocateMemory(dev, &ai, 0, &m) && m)
        T1V(vkFreeMemory, pvkFreeMemory(dev, m, 0)); }
    T1V(vkFreeCommandBuffers, pvkFreeCommandBuffers(dev, cmdpool, 1, &cmdbuf));
    T1V(vkDestroyCommandPool, pvkDestroyCommandPool(dev, cmdpool, 0));
    T1V(vkDestroyDevice, pvkDestroyDevice(dev, 0));
    T1V(vkDestroyInstance, pvkDestroyInstance(inst, 0));
}


/* ---- extended coverage (vkgl-coverage lane) ----
 * The ICD shim tables expose 43 entry points the matrix never called:
 * the extended-dynamic-state and synchronization2 command families,
 * push-descriptor commands, and a few device functions. Only six of
 * the 43 are vulkan-1.dll exports; the rest are resolved through
 * vkGetDeviceProcAddr, the path a real application uses, and three
 * push-descriptor exports are resolved from the DLL directly.
 * The section ends with a proc-addr census of every vk.xml command
 * the shim does NOT expose (VKCOV-GPA lines), classifying each by
 * whether either proc-addr entry point resolves it anyway.
 * See docs/reports/vkgl-spec-coverage-20261106.md. */
typedef void (VKAPI_PTR *XCmdBool)(VkCommandBuffer, VkBool32);
typedef void (VKAPI_PTR *XCmdMask32)(VkCommandBuffer, VkStencilFaceFlags, uint32_t);
typedef VkResult (VKAPI_PTR *XCreateRenderPass2)(VkDevice,
    const VkRenderPassCreateInfo2*, const VkAllocationCallbacks*, VkRenderPass*);
typedef VkResult (VKAPI_PTR *XResetDescriptorPool)(VkDevice, VkDescriptorPool,
    VkDescriptorPoolResetFlags);
typedef void (VKAPI_PTR *XResetQueryPool)(VkDevice, VkQueryPool, uint32_t, uint32_t);
typedef void (VKAPI_PTR *XCmdPushDesc)(VkCommandBuffer, VkPipelineBindPoint,
    VkPipelineLayout, uint32_t, uint32_t, const VkWriteDescriptorSet*);
typedef void (VKAPI_PTR *XCmdPushDescTmpl)(VkCommandBuffer,
    VkDescriptorUpdateTemplate, VkPipelineLayout, uint32_t, const void*);

#define REP_GPA_V(name, var, call) do { \
    if (var) { T2V(name, call); } \
    else { trybegin(#name); note("no gpa: %s", #name); \
           rep(#name, -1, -1, 0, 0); } } while (0)
#define REP_GPA_R(name, var, call) do { \
    if (var) { T2(name, call); } \
    else { trybegin(#name); note("no gpa: %s", #name); \
           rep(#name, -1, -1, 0, 0); } } while (0)

/* vk.xml commands the ICD shim tables do not expose; generated by
 * tools/conformance/vkgl_spec_census.py (spec_not_exposed) on the
 * base tip. */
static const char* const gpa_names[] = {
    "vkAcquireDrmDisplayEXT", "vkAcquireFullScreenExclusiveModeEXT", "vkAcquireImageANDROID",
    "vkAcquireNextImage2KHR", "vkAcquirePerformanceConfigurationINTEL", "vkAcquireProfilingLockKHR",
    "vkAcquireWinrtDisplayNV", "vkAcquireXlibDisplayEXT", "vkBindAccelerationStructureMemoryNV",
    "vkBindOpticalFlowSessionImageNV", "vkBindVideoSessionMemoryKHR", "vkBuildAccelerationStructuresKHR",
    "vkBuildMicromapsEXT", "vkCmdBeginDebugUtilsLabelEXT", "vkCmdBeginQueryIndexedEXT",
    "vkCmdBeginTransformFeedbackEXT", "vkCmdBeginVideoCodingKHR", "vkCmdBindDescriptorBufferEmbeddedSamplers2EXT",
    "vkCmdBindDescriptorBufferEmbeddedSamplersEXT", "vkCmdBindDescriptorBuffersEXT", "vkCmdBindDescriptorSets2KHR",
    "vkCmdBindInvocationMaskHUAWEI", "vkCmdBindPipelineShaderGroupNV", "vkCmdBindShadersEXT",
    "vkCmdBindShadingRateImageNV", "vkCmdBindTransformFeedbackBuffersEXT", "vkCmdBuildAccelerationStructureNV",
    "vkCmdBuildAccelerationStructuresIndirectKHR", "vkCmdBuildAccelerationStructuresKHR", "vkCmdBuildMicromapsEXT",
    "vkCmdControlVideoCodingKHR", "vkCmdCopyAccelerationStructureKHR", "vkCmdCopyAccelerationStructureNV",
    "vkCmdCopyAccelerationStructureToMemoryKHR", "vkCmdCopyMemoryIndirectNV", "vkCmdCopyMemoryToAccelerationStructureKHR",
    "vkCmdCopyMemoryToImageIndirectNV", "vkCmdCopyMemoryToMicromapEXT", "vkCmdCopyMicromapEXT",
    "vkCmdCopyMicromapToMemoryEXT", "vkCmdCuLaunchKernelNVX", "vkCmdCudaLaunchKernelNV",
    "vkCmdDebugMarkerBeginEXT", "vkCmdDebugMarkerEndEXT", "vkCmdDebugMarkerInsertEXT",
    "vkCmdDecodeVideoKHR", "vkCmdDecompressMemoryIndirectCountNV", "vkCmdDecompressMemoryNV",
    "vkCmdDispatchBaseKHR", "vkCmdDispatchGraphAMDX", "vkCmdDispatchGraphIndirectAMDX",
    "vkCmdDispatchGraphIndirectCountAMDX", "vkCmdDrawClusterHUAWEI", "vkCmdDrawClusterIndirectHUAWEI",
    "vkCmdDrawIndexedIndirectCountAMD", "vkCmdDrawIndexedIndirectCountKHR", "vkCmdDrawIndirectCountAMD",
    "vkCmdDrawMeshTasksEXT", "vkCmdDrawMeshTasksIndirectCountEXT", "vkCmdDrawMeshTasksIndirectCountNV",
    "vkCmdDrawMeshTasksIndirectEXT", "vkCmdDrawMeshTasksIndirectNV", "vkCmdDrawMeshTasksNV",
    "vkCmdDrawMultiEXT", "vkCmdDrawMultiIndexedEXT", "vkCmdEncodeVideoKHR",
    "vkCmdEndDebugUtilsLabelEXT", "vkCmdEndQueryIndexedEXT", "vkCmdEndTransformFeedbackEXT",
    "vkCmdEndVideoCodingKHR", "vkCmdExecuteGeneratedCommandsNV", "vkCmdInitializeGraphScratchMemoryAMDX",
    "vkCmdInsertDebugUtilsLabelEXT", "vkCmdOpticalFlowExecuteNV", "vkCmdPreprocessGeneratedCommandsNV",
    "vkCmdPushDescriptorSet2KHR", "vkCmdPushDescriptorSetWithTemplate2KHR", "vkCmdRefreshObjectsKHR",
    "vkCmdSetAlphaToCoverageEnableEXT", "vkCmdSetAlphaToOneEnableEXT", "vkCmdSetAttachmentFeedbackLoopEnableEXT",
    "vkCmdSetCheckpointNV", "vkCmdSetCoarseSampleOrderNV", "vkCmdSetColorBlendAdvancedEXT",
    "vkCmdSetColorBlendEnableEXT", "vkCmdSetColorBlendEquationEXT", "vkCmdSetColorWriteEnableEXT",
    "vkCmdSetColorWriteMaskEXT", "vkCmdSetConservativeRasterizationModeEXT", "vkCmdSetCoverageModulationModeNV",
    "vkCmdSetCoverageModulationTableEnableNV", "vkCmdSetCoverageModulationTableNV", "vkCmdSetCoverageReductionModeNV",
    "vkCmdSetCoverageToColorEnableNV", "vkCmdSetCoverageToColorLocationNV", "vkCmdSetDepthBias2EXT",
    "vkCmdSetDepthClampEnableEXT", "vkCmdSetDepthClipEnableEXT", "vkCmdSetDepthClipNegativeOneToOneEXT",
    "vkCmdSetDescriptorBufferOffsets2EXT", "vkCmdSetDescriptorBufferOffsetsEXT", "vkCmdSetDeviceMaskKHR",
    "vkCmdSetDiscardRectangleEXT", "vkCmdSetDiscardRectangleEnableEXT", "vkCmdSetDiscardRectangleModeEXT",
    "vkCmdSetExclusiveScissorEnableNV", "vkCmdSetExclusiveScissorNV", "vkCmdSetExtraPrimitiveOverestimationSizeEXT",
    "vkCmdSetFragmentShadingRateEnumNV", "vkCmdSetFragmentShadingRateKHR", "vkCmdSetLineRasterizationModeEXT",
    "vkCmdSetLineStippleEXT", "vkCmdSetLineStippleEnableEXT", "vkCmdSetLineStippleKHR",
    "vkCmdSetLogicOpEXT", "vkCmdSetLogicOpEnableEXT", "vkCmdSetPatchControlPointsEXT",
    "vkCmdSetPerformanceMarkerINTEL", "vkCmdSetPerformanceOverrideINTEL", "vkCmdSetPerformanceStreamMarkerINTEL",
    "vkCmdSetPolygonModeEXT", "vkCmdSetProvokingVertexModeEXT", "vkCmdSetRasterizationSamplesEXT",
    "vkCmdSetRasterizationStreamEXT", "vkCmdSetRayTracingPipelineStackSizeKHR", "vkCmdSetRenderingAttachmentLocationsKHR",
    "vkCmdSetRenderingInputAttachmentIndicesKHR", "vkCmdSetRepresentativeFragmentTestEnableNV", "vkCmdSetSampleLocationsEXT",
    "vkCmdSetSampleLocationsEnableEXT", "vkCmdSetSampleMaskEXT", "vkCmdSetShadingRateImageEnableNV",
    "vkCmdSetTessellationDomainOriginEXT", "vkCmdSetViewportShadingRatePaletteNV", "vkCmdSetViewportSwizzleNV",
    "vkCmdSetViewportWScalingEnableNV", "vkCmdSetViewportWScalingNV", "vkCmdSubpassShadingHUAWEI",
    "vkCmdTraceRaysIndirect2KHR", "vkCmdTraceRaysIndirectKHR", "vkCmdTraceRaysKHR",
    "vkCmdTraceRaysNV", "vkCmdUpdatePipelineIndirectBufferNV", "vkCmdWriteAccelerationStructuresPropertiesKHR",
    "vkCmdWriteAccelerationStructuresPropertiesNV", "vkCmdWriteBufferMarker2AMD", "vkCmdWriteBufferMarkerAMD",
    "vkCmdWriteMicromapsPropertiesEXT", "vkCompileDeferredNV", "vkCopyAccelerationStructureKHR",
    "vkCopyAccelerationStructureToMemoryKHR", "vkCopyImageToImageEXT", "vkCopyImageToMemoryEXT",
    "vkCopyMemoryToAccelerationStructureKHR", "vkCopyMemoryToImageEXT", "vkCopyMemoryToMicromapEXT",
    "vkCopyMicromapEXT", "vkCopyMicromapToMemoryEXT", "vkCreateAccelerationStructureKHR",
    "vkCreateAccelerationStructureNV", "vkCreateAndroidSurfaceKHR", "vkCreateBufferCollectionFUCHSIA",
    "vkCreateCuFunctionNVX", "vkCreateCuModuleNVX", "vkCreateCudaFunctionNV",
    "vkCreateCudaModuleNV", "vkCreateDebugReportCallbackEXT", "vkCreateDeferredOperationKHR",
    "vkCreateDescriptorUpdateTemplateKHR", "vkCreateDirectFBSurfaceEXT", "vkCreateDisplayModeKHR",
    "vkCreateDisplayPlaneSurfaceKHR", "vkCreateExecutionGraphPipelinesAMDX", "vkCreateIOSSurfaceMVK",
    "vkCreateImagePipeSurfaceFUCHSIA", "vkCreateIndirectCommandsLayoutNV", "vkCreateMacOSSurfaceMVK",
    "vkCreateMetalSurfaceEXT", "vkCreateMicromapEXT", "vkCreateOpticalFlowSessionNV",
    "vkCreatePrivateDataSlot", "vkCreatePrivateDataSlotEXT", "vkCreateRayTracingPipelinesKHR",
    "vkCreateRayTracingPipelinesNV", "vkCreateSamplerYcbcrConversionKHR", "vkCreateScreenSurfaceQNX",
    "vkCreateSemaphoreSciSyncPoolNV", "vkCreateShadersEXT", "vkCreateSharedSwapchainsKHR",
    "vkCreateStreamDescriptorSurfaceGGP", "vkCreateValidationCacheEXT", "vkCreateViSurfaceNN",
    "vkCreateVideoSessionKHR", "vkCreateVideoSessionParametersKHR", "vkCreateWaylandSurfaceKHR",
    "vkDebugMarkerSetObjectNameEXT", "vkDebugMarkerSetObjectTagEXT", "vkDebugReportMessageEXT",
    "vkDeferredOperationJoinKHR", "vkDestroyAccelerationStructureKHR", "vkDestroyAccelerationStructureNV",
    "vkDestroyBufferCollectionFUCHSIA", "vkDestroyCuFunctionNVX", "vkDestroyCuModuleNVX",
    "vkDestroyCudaFunctionNV", "vkDestroyCudaModuleNV", "vkDestroyDebugReportCallbackEXT",
    "vkDestroyDeferredOperationKHR", "vkDestroyDescriptorUpdateTemplateKHR", "vkDestroyIndirectCommandsLayoutNV",
    "vkDestroyMicromapEXT", "vkDestroyOpticalFlowSessionNV", "vkDestroyPrivateDataSlot",
    "vkDestroyPrivateDataSlotEXT", "vkDestroySamplerYcbcrConversionKHR", "vkDestroySemaphoreSciSyncPoolNV",
    "vkDestroyShaderEXT", "vkDestroyValidationCacheEXT", "vkDestroyVideoSessionKHR",
    "vkDestroyVideoSessionParametersKHR", "vkDisplayPowerControlEXT", "vkEnumerateDeviceLayerProperties",
    "vkEnumeratePhysicalDeviceGroups", "vkEnumeratePhysicalDeviceGroupsKHR", "vkEnumeratePhysicalDeviceQueueFamilyPerformanceQueryCountersKHR",
    "vkExportMetalObjectsEXT", "vkGetAccelerationStructureBuildSizesKHR", "vkGetAccelerationStructureDeviceAddressKHR",
    "vkGetAccelerationStructureHandleNV", "vkGetAccelerationStructureMemoryRequirementsNV", "vkGetAccelerationStructureOpaqueCaptureDescriptorDataEXT",
    "vkGetAndroidHardwareBufferPropertiesANDROID", "vkGetBufferCollectionPropertiesFUCHSIA", "vkGetBufferDeviceAddressEXT",
    "vkGetBufferOpaqueCaptureAddress", "vkGetBufferOpaqueCaptureDescriptorDataEXT", "vkGetCalibratedTimestampsEXT",
    "vkGetCalibratedTimestampsKHR", "vkGetCommandPoolMemoryConsumption", "vkGetCudaModuleCacheNV",
    "vkGetDeferredOperationMaxConcurrencyKHR", "vkGetDeferredOperationResultKHR", "vkGetDescriptorEXT",
    "vkGetDescriptorSetHostMappingVALVE", "vkGetDescriptorSetLayoutBindingOffsetEXT", "vkGetDescriptorSetLayoutHostMappingInfoVALVE",
    "vkGetDescriptorSetLayoutSizeEXT", "vkGetDeviceAccelerationStructureCompatibilityKHR", "vkGetDeviceBufferMemoryRequirementsKHR",
    "vkGetDeviceFaultInfoEXT", "vkGetDeviceGroupPeerMemoryFeaturesKHR", "vkGetDeviceGroupSurfacePresentModes2EXT",
    "vkGetDeviceImageMemoryRequirementsKHR", "vkGetDeviceImageSparseMemoryRequirementsKHR", "vkGetDeviceImageSubresourceLayoutKHR",
    "vkGetDeviceMemoryOpaqueCaptureAddress", "vkGetDeviceMemoryOpaqueCaptureAddressKHR", "vkGetDeviceMicromapCompatibilityEXT",
    "vkGetDeviceProcAddr", "vkGetDeviceSubpassShadingMaxWorkgroupSizeHUAWEI", "vkGetDisplayModeProperties2KHR",
    "vkGetDisplayModePropertiesKHR", "vkGetDisplayPlaneCapabilities2KHR", "vkGetDisplayPlaneCapabilitiesKHR",
    "vkGetDisplayPlaneSupportedDisplaysKHR", "vkGetDrmDisplayEXT", "vkGetDynamicRenderingTilePropertiesQCOM",
    "vkGetEncodedVideoSessionParametersKHR", "vkGetExecutionGraphPipelineNodeIndexAMDX", "vkGetExecutionGraphPipelineScratchSizeAMDX",
    "vkGetFaultData", "vkGetFenceFdKHR", "vkGetFenceSciSyncFenceNV",
    "vkGetFenceSciSyncObjNV", "vkGetFenceWin32HandleKHR", "vkGetFramebufferTilePropertiesQCOM",
    "vkGetGeneratedCommandsMemoryRequirementsNV", "vkGetImageDrmFormatModifierPropertiesEXT", "vkGetImageOpaqueCaptureDescriptorDataEXT",
    "vkGetImageSparseMemoryRequirements2", "vkGetImageSparseMemoryRequirements2KHR", "vkGetImageSubresourceLayout2KHR",
    "vkGetImageViewAddressNVX", "vkGetImageViewHandleNVX", "vkGetImageViewOpaqueCaptureDescriptorDataEXT",
    "vkGetInstanceProcAddr", "vkGetLatencyTimingsNV", "vkGetMemoryAndroidHardwareBufferANDROID",
    "vkGetMemoryFdKHR", "vkGetMemoryFdPropertiesKHR", "vkGetMemoryHostPointerPropertiesEXT",
    "vkGetMemoryRemoteAddressNV", "vkGetMemorySciBufNV", "vkGetMemoryWin32HandleKHR",
    "vkGetMemoryWin32HandleNV", "vkGetMemoryWin32HandlePropertiesKHR", "vkGetMemoryZirconHandleFUCHSIA",
    "vkGetMemoryZirconHandlePropertiesFUCHSIA", "vkGetMicromapBuildSizesEXT", "vkGetPerformanceParameterINTEL",
    "vkGetPhysicalDeviceCalibrateableTimeDomainsEXT", "vkGetPhysicalDeviceCalibrateableTimeDomainsKHR", "vkGetPhysicalDeviceCooperativeMatrixPropertiesKHR",
    "vkGetPhysicalDeviceCooperativeMatrixPropertiesNV", "vkGetPhysicalDeviceDirectFBPresentationSupportEXT", "vkGetPhysicalDeviceDisplayPlaneProperties2KHR",
    "vkGetPhysicalDeviceDisplayPlanePropertiesKHR", "vkGetPhysicalDeviceDisplayProperties2KHR", "vkGetPhysicalDeviceDisplayPropertiesKHR",
    "vkGetPhysicalDeviceExternalBufferProperties", "vkGetPhysicalDeviceExternalBufferPropertiesKHR", "vkGetPhysicalDeviceExternalFenceProperties",
    "vkGetPhysicalDeviceExternalFencePropertiesKHR", "vkGetPhysicalDeviceExternalImageFormatPropertiesNV", "vkGetPhysicalDeviceExternalMemorySciBufPropertiesNV",
    "vkGetPhysicalDeviceExternalSemaphoreProperties", "vkGetPhysicalDeviceExternalSemaphorePropertiesKHR", "vkGetPhysicalDeviceFragmentShadingRatesKHR",
    "vkGetPhysicalDeviceMultisamplePropertiesEXT", "vkGetPhysicalDeviceOpticalFlowImageFormatsNV", "vkGetPhysicalDeviceQueueFamilyPerformanceQueryPassesKHR",
    "vkGetPhysicalDeviceRefreshableObjectTypesKHR", "vkGetPhysicalDeviceSciBufAttributesNV", "vkGetPhysicalDeviceSciSyncAttributesNV",
    "vkGetPhysicalDeviceScreenPresentationSupportQNX", "vkGetPhysicalDeviceSparseImageFormatProperties", "vkGetPhysicalDeviceSparseImageFormatProperties2",
    "vkGetPhysicalDeviceSparseImageFormatProperties2KHR", "vkGetPhysicalDeviceSupportedFramebufferMixedSamplesCombinationsNV", "vkGetPhysicalDeviceSurfaceCapabilities2KHR",
    "vkGetPhysicalDeviceSurfaceFormats2KHR", "vkGetPhysicalDeviceToolProperties", "vkGetPhysicalDeviceToolPropertiesEXT",
    "vkGetPhysicalDeviceVideoCapabilitiesKHR", "vkGetPhysicalDeviceVideoEncodeQualityLevelPropertiesKHR", "vkGetPhysicalDeviceVideoFormatPropertiesKHR",
    "vkGetPhysicalDeviceWaylandPresentationSupportKHR", "vkGetPhysicalDeviceXcbPresentationSupportKHR", "vkGetPipelineExecutableInternalRepresentationsKHR",
    "vkGetPipelineExecutablePropertiesKHR", "vkGetPipelineExecutableStatisticsKHR", "vkGetPipelineIndirectDeviceAddressNV",
    "vkGetPipelineIndirectMemoryRequirementsNV", "vkGetPipelinePropertiesEXT", "vkGetPrivateData",
    "vkGetPrivateDataEXT", "vkGetQueueCheckpointData2NV", "vkGetQueueCheckpointDataNV",
    "vkGetRandROutputDisplayEXT", "vkGetRayTracingCaptureReplayShaderGroupHandlesKHR", "vkGetRayTracingShaderGroupHandlesKHR",
    "vkGetRayTracingShaderGroupHandlesNV", "vkGetRayTracingShaderGroupStackSizeKHR", "vkGetRefreshCycleDurationGOOGLE",
    "vkGetRenderingAreaGranularityKHR", "vkGetSamplerOpaqueCaptureDescriptorDataEXT", "vkGetScreenBufferPropertiesQNX",
    "vkGetSemaphoreCounterValueKHR", "vkGetSemaphoreFdKHR", "vkGetSemaphoreSciSyncObjNV",
    "vkGetSemaphoreWin32HandleKHR", "vkGetSemaphoreZirconHandleFUCHSIA", "vkGetShaderBinaryDataEXT",
    "vkGetShaderInfoAMD", "vkGetShaderModuleCreateInfoIdentifierEXT", "vkGetShaderModuleIdentifierEXT",
    "vkGetSwapchainCounterEXT", "vkGetSwapchainGrallocUsage2ANDROID", "vkGetSwapchainGrallocUsageANDROID",
    "vkGetSwapchainStatusKHR", "vkGetValidationCacheDataEXT", "vkGetVideoSessionMemoryRequirementsKHR",
    "vkGetWinrtDisplayNV", "vkImportFenceFdKHR", "vkImportFenceSciSyncFenceNV",
    "vkImportFenceSciSyncObjNV", "vkImportFenceWin32HandleKHR", "vkImportSemaphoreFdKHR",
    "vkImportSemaphoreSciSyncObjNV", "vkImportSemaphoreWin32HandleKHR", "vkImportSemaphoreZirconHandleFUCHSIA",
    "vkInitializePerformanceApiINTEL", "vkLatencySleepNV", "vkMapMemory2KHR",
    "vkMergeValidationCachesEXT", "vkQueueBeginDebugUtilsLabelEXT", "vkQueueEndDebugUtilsLabelEXT",
    "vkQueueInsertDebugUtilsLabelEXT", "vkQueueNotifyOutOfBandNV", "vkQueueSetPerformanceConfigurationINTEL",
    "vkQueueSignalReleaseImageANDROID", "vkRegisterDeviceEventEXT", "vkRegisterDisplayEventEXT",
    "vkReleaseDisplayEXT", "vkReleaseFullScreenExclusiveModeEXT", "vkReleasePerformanceConfigurationINTEL",
    "vkReleaseProfilingLockKHR", "vkReleaseSwapchainImagesEXT", "vkResetQueryPoolEXT",
    "vkSetBufferCollectionBufferConstraintsFUCHSIA", "vkSetBufferCollectionImageConstraintsFUCHSIA", "vkSetDeviceMemoryPriorityEXT",
    "vkSetHdrMetadataEXT", "vkSetLatencyMarkerNV", "vkSetLatencySleepModeNV",
    "vkSetLocalDimmingAMD", "vkSetPrivateData", "vkSetPrivateDataEXT",
    "vkSignalSemaphoreKHR", "vkTransitionImageLayoutEXT", "vkUninitializePerformanceApiINTEL",
    "vkUnmapMemory2KHR", "vkUpdateDescriptorSetWithTemplateKHR", "vkUpdateVideoSessionParametersKHR",
    "vkWaitForPresentKHR", "vkWaitSemaphoresKHR", "vkWriteAccelerationStructuresPropertiesKHR",
    "vkWriteMicromapsPropertiesEXT",
};

static void t_extended_coverage(void) {
    /* -- resolve the device extension entry points -- */
    XCmdBool xSetCullMode = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetCullMode");
    XCmdBool xSetDepthBiasEnable = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetDepthBiasEnable");
    XCmdBool xSetDepthBoundsTestEnable = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetDepthBoundsTestEnable");
    XCmdBool xSetDepthBoundsTestEnableEXT = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetDepthBoundsTestEnableEXT");
    XCmdBool xSetDepthTestEnable = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetDepthTestEnable");
    XCmdBool xSetDepthTestEnableEXT = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetDepthTestEnableEXT");
    XCmdBool xSetDepthWriteEnable = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetDepthWriteEnable");
    XCmdBool xSetDepthWriteEnableEXT = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetDepthWriteEnableEXT");
    XCmdBool xSetPrimitiveRestartEnable = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetPrimitiveRestartEnable");
    XCmdBool xSetPrimitiveRestartEnableEXT = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetPrimitiveRestartEnableEXT");
    XCmdBool xSetRasterizerDiscardEnable = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetRasterizerDiscardEnable");
    XCmdBool xSetRasterizerDiscardEnableEXT = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetRasterizerDiscardEnableEXT");
    XCmdBool xSetStencilTestEnable = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetStencilTestEnable");
    XCmdBool xSetStencilTestEnableEXT = (XCmdBool)pvkGetDeviceProcAddr(dev, "vkCmdSetStencilTestEnableEXT");
    typedef void (VKAPI_PTR *XCmdFrontFace)(VkCommandBuffer, VkFrontFace);
    XCmdFrontFace xSetFrontFace = (XCmdFrontFace)pvkGetDeviceProcAddr(dev, "vkCmdSetFrontFace");
    typedef void (VKAPI_PTR *XCmdPrimTopo)(VkCommandBuffer, VkPrimitiveTopology);
    XCmdPrimTopo xSetPrimitiveTopology = (XCmdPrimTopo)pvkGetDeviceProcAddr(dev, "vkCmdSetPrimitiveTopology");
    typedef void (VKAPI_PTR *XCmdCompareOp)(VkCommandBuffer, VkCompareOp);
    XCmdCompareOp xSetDepthCompareOp = (XCmdCompareOp)pvkGetDeviceProcAddr(dev, "vkCmdSetDepthCompareOp");
    XCmdCompareOp xSetDepthCompareOpEXT = (XCmdCompareOp)pvkGetDeviceProcAddr(dev, "vkCmdSetDepthCompareOpEXT");
    typedef void (VKAPI_PTR *XCmdStencilOp)(VkCommandBuffer, VkStencilFaceFlags,
        VkStencilOp, VkStencilOp, VkStencilOp, VkCompareOp);
    XCmdStencilOp xSetStencilOp = (XCmdStencilOp)pvkGetDeviceProcAddr(dev, "vkCmdSetStencilOp");
    XCmdStencilOp xSetStencilOpEXT = (XCmdStencilOp)pvkGetDeviceProcAddr(dev, "vkCmdSetStencilOpEXT");
    XCmdMask32 xSetStencilCompareMask = (XCmdMask32)pvkGetDeviceProcAddr(dev, "vkCmdSetStencilCompareMask");
    XCmdMask32 xSetStencilWriteMask = (XCmdMask32)pvkGetDeviceProcAddr(dev, "vkCmdSetStencilWriteMask");
    typedef void (VKAPI_PTR *XCmdBlendConst)(VkCommandBuffer, const float*);
    XCmdBlendConst xSetBlendConstants = (XCmdBlendConst)pvkGetDeviceProcAddr(dev, "vkCmdSetBlendConstants");
    typedef void (VKAPI_PTR *XCmdSetEvent2)(VkCommandBuffer, VkEvent, const VkDependencyInfo*);
    XCmdSetEvent2 xSetEvent2 = (XCmdSetEvent2)pvkGetDeviceProcAddr(dev, "vkCmdSetEvent2");
    XCmdSetEvent2 xSetEvent2KHR = (XCmdSetEvent2)pvkGetDeviceProcAddr(dev, "vkCmdSetEvent2KHR");
    typedef void (VKAPI_PTR *XCmdResetEvent2)(VkCommandBuffer, VkEvent, VkPipelineStageFlags2);
    XCmdResetEvent2 xResetEvent2 = (XCmdResetEvent2)pvkGetDeviceProcAddr(dev, "vkCmdResetEvent2");
    XCmdResetEvent2 xResetEvent2KHR = (XCmdResetEvent2)pvkGetDeviceProcAddr(dev, "vkCmdResetEvent2KHR");
    typedef void (VKAPI_PTR *XCmdWaitEvents2)(VkCommandBuffer, uint32_t,
        const VkEvent*, const VkDependencyInfo*);
    XCmdWaitEvents2 xWaitEvents2 = (XCmdWaitEvents2)pvkGetDeviceProcAddr(dev, "vkCmdWaitEvents2");
    XCmdWaitEvents2 xWaitEvents2KHR = (XCmdWaitEvents2)pvkGetDeviceProcAddr(dev, "vkCmdWaitEvents2KHR");
    typedef void (VKAPI_PTR *XCmdClearDS)(VkCommandBuffer, VkImage, VkImageLayout,
        const VkClearDepthStencilValue*, uint32_t, const VkImageSubresourceRange*);
    XCmdClearDS xClearDepthStencilImage = (XCmdClearDS)pvkGetDeviceProcAddr(dev, "vkCmdClearDepthStencilImage");
    typedef void (VKAPI_PTR *XCmdCopyQP)(VkCommandBuffer, VkQueryPool, uint32_t,
        uint32_t, VkBuffer, VkDeviceSize, VkDeviceSize, VkQueryResultFlags);
    XCmdCopyQP xCopyQueryPoolResults = (XCmdCopyQP)pvkGetDeviceProcAddr(dev, "vkCmdCopyQueryPoolResults");
    typedef void (VKAPI_PTR *XCmdResolve2)(VkCommandBuffer, const VkResolveImageInfo2*);
    XCmdResolve2 xResolveImage2 = (XCmdResolve2)pvkGetDeviceProcAddr(dev, "vkCmdResolveImage2");
    XCmdResolve2 xResolveImage2KHR = (XCmdResolve2)pvkGetDeviceProcAddr(dev, "vkCmdResolveImage2KHR");
    XCreateRenderPass2 xCreateRenderPass2 = (XCreateRenderPass2)pvkGetDeviceProcAddr(dev, "vkCreateRenderPass2");
    XCreateRenderPass2 xCreateRenderPass2KHR = (XCreateRenderPass2)pvkGetDeviceProcAddr(dev, "vkCreateRenderPass2KHR");
    XResetDescriptorPool xResetDescriptorPool = (XResetDescriptorPool)pvkGetDeviceProcAddr(dev, "vkResetDescriptorPool");
    XResetQueryPool xResetQueryPool = (XResetQueryPool)pvkGetDeviceProcAddr(dev, "vkResetQueryPool");
    /* the three push-descriptor exports: resolved from the DLL */
    XCmdPushDesc xPushDescriptorSetKHR = (XCmdPushDesc)(void*)GetProcAddress(vkdll, "vkCmdPushDescriptorSetKHR");
    XCmdPushDescTmpl xPushDescriptorSetWithTemplate = (XCmdPushDescTmpl)(void*)GetProcAddress(vkdll, "vkCmdPushDescriptorSetWithTemplate");
    XCmdPushDescTmpl xPushDescriptorSetWithTemplateKHR = (XCmdPushDescTmpl)(void*)GetProcAddress(vkdll, "vkCmdPushDescriptorSetWithTemplateKHR");

    /* push-descriptor update template (for the WithTemplate commands) */
    VkDescriptorUpdateTemplate ptmpl = 0;
    { VkDescriptorUpdateTemplateEntry en = { 0, 0, 1,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, sizeof(VkDescriptorBufferInfo) };
      VkDescriptorUpdateTemplateCreateInfo ci = {
        VK_STRUCTURE_TYPE_DESCRIPTOR_UPDATE_TEMPLATE_CREATE_INFO, 0, 0,
        1, &en, VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_PUSH_DESCRIPTORS_KHR,
        0, VK_PIPELINE_BIND_POINT_GRAPHICS, playout, 0 };
      if (pvkCreateDescriptorUpdateTemplate(dev, &ci, 0, &ptmpl) != VK_SUCCESS)
        ptmpl = 0; }

    /* secondary command buffer for vkCmdExecuteCommands */
    VkCommandBuffer secondary = 0;
    { VkCommandBufferAllocateInfo ai = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        0, cmdpool, VK_COMMAND_BUFFER_LEVEL_SECONDARY, 1 };
      if (pvkAllocateCommandBuffers(dev, &ai, &secondary) != VK_SUCCESS)
        secondary = 0; }
    if (secondary) {
      VkCommandBufferInheritanceInfo inh = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO,
        0, rpass, 0, fb, VK_FALSE, 0, 0 };
      VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        0, VK_COMMAND_BUFFER_USAGE_RENDER_PASS_CONTINUE_BIT, &inh };
      pvkBeginCommandBuffer(secondary, &bi);
      pvkEndCommandBuffer(secondary); }

    /* -- command-buffer section: everything below records into cmdbuf -- */
    { VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, 0 };
      pvkBeginCommandBuffer(cmdbuf, &bi); }
    /* push descriptors */
    { VkDescriptorBufferInfo dbi = { vbuf, 0, 1024 };
      VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, 0,
        0, 0, 0, 1, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 0, &dbi, 0 };
      REP_GPA_V(vkCmdPushDescriptorSetKHR, xPushDescriptorSetKHR,
        xPushDescriptorSetKHR(cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS, playout, 0, 1, &w));
      if (ptmpl) {
        REP_GPA_V(vkCmdPushDescriptorSetWithTemplate, xPushDescriptorSetWithTemplate,
          xPushDescriptorSetWithTemplate(cmdbuf, ptmpl, playout, 0, &dbi));
        REP_GPA_V(vkCmdPushDescriptorSetWithTemplateKHR, xPushDescriptorSetWithTemplateKHR,
          xPushDescriptorSetWithTemplateKHR(cmdbuf, ptmpl, playout, 0, &dbi));
      } else {
        trybegin("vkCmdPushDescriptorSetWithTemplate");
        note("no push template"); rep("vkCmdPushDescriptorSetWithTemplate", -1, -1, 0, 0);
        trybegin("vkCmdPushDescriptorSetWithTemplateKHR");
        note("no push template"); rep("vkCmdPushDescriptorSetWithTemplateKHR", -1, -1, 0, 0);
      } }
    /* extended dynamic state */
    { const float bc[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
      REP_GPA_V(vkCmdSetBlendConstants, xSetBlendConstants, xSetBlendConstants(cmdbuf, bc)); }
    REP_GPA_V(vkCmdSetCullMode, xSetCullMode, xSetCullMode(cmdbuf, VK_CULL_MODE_NONE));
    REP_GPA_V(vkCmdSetFrontFace, xSetFrontFace, xSetFrontFace(cmdbuf, VK_FRONT_FACE_COUNTER_CLOCKWISE));
    REP_GPA_V(vkCmdSetPrimitiveTopology, xSetPrimitiveTopology, xSetPrimitiveTopology(cmdbuf, VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST));
    REP_GPA_V(vkCmdSetPrimitiveRestartEnable, xSetPrimitiveRestartEnable, xSetPrimitiveRestartEnable(cmdbuf, VK_FALSE));
    REP_GPA_V(vkCmdSetPrimitiveRestartEnableEXT, xSetPrimitiveRestartEnableEXT, xSetPrimitiveRestartEnableEXT(cmdbuf, VK_FALSE));
    REP_GPA_V(vkCmdSetRasterizerDiscardEnable, xSetRasterizerDiscardEnable, xSetRasterizerDiscardEnable(cmdbuf, VK_FALSE));
    REP_GPA_V(vkCmdSetRasterizerDiscardEnableEXT, xSetRasterizerDiscardEnableEXT, xSetRasterizerDiscardEnableEXT(cmdbuf, VK_FALSE));
    REP_GPA_V(vkCmdSetDepthBiasEnable, xSetDepthBiasEnable, xSetDepthBiasEnable(cmdbuf, VK_FALSE));
    REP_GPA_V(vkCmdSetDepthBoundsTestEnable, xSetDepthBoundsTestEnable, xSetDepthBoundsTestEnable(cmdbuf, VK_FALSE));
    REP_GPA_V(vkCmdSetDepthBoundsTestEnableEXT, xSetDepthBoundsTestEnableEXT, xSetDepthBoundsTestEnableEXT(cmdbuf, VK_FALSE));
    REP_GPA_V(vkCmdSetDepthTestEnable, xSetDepthTestEnable, xSetDepthTestEnable(cmdbuf, VK_TRUE));
    REP_GPA_V(vkCmdSetDepthTestEnableEXT, xSetDepthTestEnableEXT, xSetDepthTestEnableEXT(cmdbuf, VK_TRUE));
    REP_GPA_V(vkCmdSetDepthWriteEnable, xSetDepthWriteEnable, xSetDepthWriteEnable(cmdbuf, VK_TRUE));
    REP_GPA_V(vkCmdSetDepthWriteEnableEXT, xSetDepthWriteEnableEXT, xSetDepthWriteEnableEXT(cmdbuf, VK_TRUE));
    REP_GPA_V(vkCmdSetDepthCompareOp, xSetDepthCompareOp, xSetDepthCompareOp(cmdbuf, VK_COMPARE_OP_LESS));
    REP_GPA_V(vkCmdSetDepthCompareOpEXT, xSetDepthCompareOpEXT, xSetDepthCompareOpEXT(cmdbuf, VK_COMPARE_OP_LESS));
    REP_GPA_V(vkCmdSetStencilTestEnable, xSetStencilTestEnable, xSetStencilTestEnable(cmdbuf, VK_FALSE));
    REP_GPA_V(vkCmdSetStencilTestEnableEXT, xSetStencilTestEnableEXT, xSetStencilTestEnableEXT(cmdbuf, VK_FALSE));
    REP_GPA_V(vkCmdSetStencilOp, xSetStencilOp, xSetStencilOp(cmdbuf, VK_STENCIL_FACE_FRONT_BIT, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS));
    REP_GPA_V(vkCmdSetStencilOpEXT, xSetStencilOpEXT, xSetStencilOpEXT(cmdbuf, VK_STENCIL_FACE_FRONT_BIT, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_STENCIL_OP_KEEP, VK_COMPARE_OP_ALWAYS));
    REP_GPA_V(vkCmdSetStencilCompareMask, xSetStencilCompareMask, xSetStencilCompareMask(cmdbuf, VK_STENCIL_FACE_FRONT_BIT, 0xff));
    REP_GPA_V(vkCmdSetStencilWriteMask, xSetStencilWriteMask, xSetStencilWriteMask(cmdbuf, VK_STENCIL_FACE_FRONT_BIT, 0xff));
    /* synchronization2 */
    { VkDependencyInfo di = { VK_STRUCTURE_TYPE_DEPENDENCY_INFO, 0,
        0, 0, 0, 0, 0, 0, 0 };
      REP_GPA_V(vkCmdSetEvent2, xSetEvent2, xSetEvent2(cmdbuf, event, &di));
      REP_GPA_V(vkCmdSetEvent2KHR, xSetEvent2KHR, xSetEvent2KHR(cmdbuf, event, &di));
      REP_GPA_V(vkCmdWaitEvents2, xWaitEvents2, xWaitEvents2(cmdbuf, 1, &event, &di));
      REP_GPA_V(vkCmdWaitEvents2KHR, xWaitEvents2KHR, xWaitEvents2KHR(cmdbuf, 1, &event, &di));
      REP_GPA_V(vkCmdResetEvent2, xResetEvent2, xResetEvent2(cmdbuf, event, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT));
      REP_GPA_V(vkCmdResetEvent2KHR, xResetEvent2KHR, xResetEvent2KHR(cmdbuf, event, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT)); }
    /* clears / copies / resolve (image is a colour image; the depth-stencil
     * clear is recorded against it with a depth aspect — no validation
     * layer is present, and the row exists to exercise the bridge path) */
    { VkClearDepthStencilValue dsv = { 1.0f, 0 };
      VkImageSubresourceRange rg = { VK_IMAGE_ASPECT_DEPTH_BIT, 0, 1, 0, 1 };
      REP_GPA_V(vkCmdClearDepthStencilImage, xClearDepthStencilImage,
        xClearDepthStencilImage(cmdbuf, img, VK_IMAGE_LAYOUT_GENERAL, &dsv, 1, &rg)); }
    REP_GPA_V(vkCmdCopyQueryPoolResults, xCopyQueryPoolResults,
      xCopyQueryPoolResults(cmdbuf, qpool, 0, 1, vbuf, 0, 8, 0));
    { VkImageSubresourceLayers sl = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
      VkImageResolve2 reg = { VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2, 0,
        sl, {0,0,0}, sl, {0,0,0}, {256,256,1} };
      VkResolveImageInfo2 ri = { VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2, 0,
        img, VK_IMAGE_LAYOUT_GENERAL, img, VK_IMAGE_LAYOUT_GENERAL, 1, &reg };
      REP_GPA_V(vkCmdResolveImage2, xResolveImage2, xResolveImage2(cmdbuf, &ri));
      REP_GPA_V(vkCmdResolveImage2KHR, xResolveImage2KHR, xResolveImage2KHR(cmdbuf, &ri)); }
    /* execute commands (DLL export with an existing harness pointer) */
    if (secondary) {
      T2V(vkCmdExecuteCommands, pvkCmdExecuteCommands(cmdbuf, 1, &secondary));
    } else {
      trybegin("vkCmdExecuteCommands"); note("no secondary buffer");
      rep("vkCmdExecuteCommands", -1, -1, 0, 0); }
    pvkEndCommandBuffer(cmdbuf);

    /* -- non-command rows -- */
    { uint32_t n = 0;
      pvkGetPhysicalDeviceQueueFamilyProperties(phys, &n, 0);
      VkQueueFamilyProperties props[8];
      uint32_t m = n > 8 ? 8 : n;
      T2(vkGetPhysicalDeviceQueueFamilyProperties,
         (pvkGetPhysicalDeviceQueueFamilyProperties(phys, &m, props), VK_SUCCESS)); }
    { VkAttachmentDescription2 ad = { VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2, 0, 0,
        VK_FORMAT_R8G8B8A8_UNORM, VK_SAMPLE_COUNT_1_BIT, VK_ATTACHMENT_LOAD_OP_CLEAR,
        VK_ATTACHMENT_STORE_OP_STORE, VK_ATTACHMENT_LOAD_OP_DONT_CARE,
        VK_ATTACHMENT_STORE_OP_DONT_CARE, VK_IMAGE_LAYOUT_UNDEFINED,
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR };
      VkAttachmentReference2 ar = { VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2, 0, 0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, 0 };
      VkSubpassDescription2 sd = { VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2, 0, 0,
        VK_PIPELINE_BIND_POINT_GRAPHICS, 0, 0, 1, &ar, 0, 0, 0, 0, 0, 0, 0 };
      VkRenderPassCreateInfo2 ci = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2, 0,
        0, 1, &ad, 1, &sd, 0, 0, 0, 0 };
      VkRenderPass rp2 = 0;
      REP_GPA_R(vkCreateRenderPass2, xCreateRenderPass2,
        xCreateRenderPass2(dev, &ci, 0, &rp2));
      if (rp2) pvkDestroyRenderPass(dev, rp2, 0);
      VkRenderPass rp3 = 0;
      REP_GPA_R(vkCreateRenderPass2KHR, xCreateRenderPass2KHR,
        xCreateRenderPass2KHR(dev, &ci, 0, &rp3));
      if (rp3) pvkDestroyRenderPass(dev, rp3, 0); }
    /* resets against locally owned pools: the setup globals stay intact */
    { VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
      VkDescriptorPoolCreateInfo ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        0, 0, 1, 1, &ps };
      VkDescriptorPool lp = 0;
      if (pvkCreateDescriptorPool(dev, &ci, 0, &lp) == VK_SUCCESS && lp) {
        REP_GPA_R(vkResetDescriptorPool, xResetDescriptorPool,
          xResetDescriptorPool(dev, lp, 0));
        pvkDestroyDescriptorPool(dev, lp, 0); } }
    { VkQueryPoolCreateInfo ci = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        0, 0, VK_QUERY_TYPE_OCCLUSION, 1, 0 };
      VkQueryPool lq = 0;
      if (pvkCreateQueryPool(dev, &ci, 0, &lq) == VK_SUCCESS && lq) {
        REP_GPA_V(vkResetQueryPool, xResetQueryPool,
          xResetQueryPool(dev, lq, 0, 1));
        pvkDestroyQueryPool(dev, lq, 0); } }
    /* vkDestroyPipeline reported once (a second destroy would be a
     * double free of the same handle) */
    { VkComputePipelineCreateInfo ci = { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO, 0, 0,
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, 0, 0,
          VK_SHADER_STAGE_COMPUTE_BIT, vshader, "main", 0 },
        playout, 0, 0 };
      VkPipeline p = 0;
      if (pvkCreateComputePipelines(dev, 0, 1, &ci, 0, &p) == VK_SUCCESS && p)
        T1V(vkDestroyPipeline, pvkDestroyPipeline(dev, p, 0)); }
    if (ptmpl) pvkDestroyDescriptorUpdateTemplate(dev, ptmpl, 0);
    if (secondary) pvkFreeCommandBuffers(dev, cmdpool, 1, &secondary);

    /* -- proc-addr census of the spec names the shim does not expose -- */
    { unsigned ri = 0, rd = 0;
      unsigned total = (unsigned)(sizeof(gpa_names) / sizeof(gpa_names[0]));
      for (unsigned i = 0; i < total; i++) {
        void* a = (void*)pvkGetInstanceProcAddr(inst, gpa_names[i]);
        void* b = (void*)pvkGetDeviceProcAddr(dev, gpa_names[i]);
        if (a) ri++;
        if (b) rd++;
        fprintf(stderr, "VKCOV-GPA: %s=%p:%p\n", gpa_names[i], a, b);
      }
      fflush(stderr);
      note("gpa census: %u names, instance-resolved %u, device-resolved %u",
           total, ri, rd); }
}

/* ---- stub-only exports: expect VK_ERROR_FEATURE_NOT_PRESENT (-8) ---- */
static void t_stubs(void) {
    { trybegin("vkCmdBeginConditionalRenderingEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdBeginConditionalRenderingEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdBeginConditionalRenderingEXT)(); uint64_t _t3=qpf_us(); rep("vkCmdBeginConditionalRenderingEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdBeginQuery"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdBeginQuery)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdBeginQuery)(); uint64_t _t3=qpf_us(); rep("vkCmdBeginQuery",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdBindVertexBuffers2EXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdBindVertexBuffers2EXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdBindVertexBuffers2EXT)(); uint64_t _t3=qpf_us(); rep("vkCmdBindVertexBuffers2EXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdBindVertexBuffers2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdBindVertexBuffers2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdBindVertexBuffers2KHR)(); uint64_t _t3=qpf_us(); rep("vkCmdBindVertexBuffers2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdDrawIndirectByteCountEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdDrawIndirectByteCountEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdDrawIndirectByteCountEXT)(); uint64_t _t3=qpf_us(); rep("vkCmdDrawIndirectByteCountEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdEndConditionalRenderingEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdEndConditionalRenderingEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdEndConditionalRenderingEXT)(); uint64_t _t3=qpf_us(); rep("vkCmdEndConditionalRenderingEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdEndQuery"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdEndQuery)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdEndQuery)(); uint64_t _t3=qpf_us(); rep("vkCmdEndQuery",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdNextSubpass"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdNextSubpass)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdNextSubpass)(); uint64_t _t3=qpf_us(); rep("vkCmdNextSubpass",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdNextSubpass2"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdNextSubpass2)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdNextSubpass2)(); uint64_t _t3=qpf_us(); rep("vkCmdNextSubpass2",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdNextSubpass2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdNextSubpass2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdNextSubpass2KHR)(); uint64_t _t3=qpf_us(); rep("vkCmdNextSubpass2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdNextSubpassKHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdNextSubpassKHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdNextSubpassKHR)(); uint64_t _t3=qpf_us(); rep("vkCmdNextSubpassKHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdPushDescriptorSet"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdPushDescriptorSet)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdPushDescriptorSet)(); uint64_t _t3=qpf_us(); rep("vkCmdPushDescriptorSet",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdResetQueryPool"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdResetQueryPool)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdResetQueryPool)(); uint64_t _t3=qpf_us(); rep("vkCmdResetQueryPool",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetBlendConstant"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetBlendConstant)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetBlendConstant)(); uint64_t _t3=qpf_us(); rep("vkCmdSetBlendConstant",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetCullModeEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetCullModeEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetCullModeEXT)(); uint64_t _t3=qpf_us(); rep("vkCmdSetCullModeEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetDepthBias"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetDepthBias)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetDepthBias)(); uint64_t _t3=qpf_us(); rep("vkCmdSetDepthBias",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetDepthBiasEnableEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetDepthBiasEnableEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetDepthBiasEnableEXT)(); uint64_t _t3=qpf_us(); rep("vkCmdSetDepthBiasEnableEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetDepthBiasNV"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetDepthBiasNV)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetDepthBiasNV)(); uint64_t _t3=qpf_us(); rep("vkCmdSetDepthBiasNV",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetDepthBounds"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetDepthBounds)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetDepthBounds)(); uint64_t _t3=qpf_us(); rep("vkCmdSetDepthBounds",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetDeviceMask"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetDeviceMask)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetDeviceMask)(); uint64_t _t3=qpf_us(); rep("vkCmdSetDeviceMask",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetFrontFaceEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetFrontFaceEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetFrontFaceEXT)(); uint64_t _t3=qpf_us(); rep("vkCmdSetFrontFaceEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetLineWidth"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetLineWidth)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetLineWidth)(); uint64_t _t3=qpf_us(); rep("vkCmdSetLineWidth",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetPrimitiveTopologyEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetPrimitiveTopologyEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetPrimitiveTopologyEXT)(); uint64_t _t3=qpf_us(); rep("vkCmdSetPrimitiveTopologyEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetScissorWithCount"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetScissorWithCount)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetScissorWithCount)(); uint64_t _t3=qpf_us(); rep("vkCmdSetScissorWithCount",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetScissorWithCountEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetScissorWithCountEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetScissorWithCountEXT)(); uint64_t _t3=qpf_us(); rep("vkCmdSetScissorWithCountEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetStencilReference"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetStencilReference)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetStencilReference)(); uint64_t _t3=qpf_us(); rep("vkCmdSetStencilReference",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetVertexInputEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetVertexInputEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetVertexInputEXT)(); uint64_t _t3=qpf_us(); rep("vkCmdSetVertexInputEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetViewportWithCount"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetViewportWithCount)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetViewportWithCount)(); uint64_t _t3=qpf_us(); rep("vkCmdSetViewportWithCount",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdSetViewportWithCountEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdSetViewportWithCountEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdSetViewportWithCountEXT)(); uint64_t _t3=qpf_us(); rep("vkCmdSetViewportWithCountEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdWaitEvents"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdWaitEvents)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdWaitEvents)(); uint64_t _t3=qpf_us(); rep("vkCmdWaitEvents",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdWriteTimestamp"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdWriteTimestamp)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdWriteTimestamp)(); uint64_t _t3=qpf_us(); rep("vkCmdWriteTimestamp",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdWriteTimestamp2"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdWriteTimestamp2)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdWriteTimestamp2)(); uint64_t _t3=qpf_us(); rep("vkCmdWriteTimestamp2",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCmdWriteTimestamp2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCmdWriteTimestamp2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCmdWriteTimestamp2KHR)(); uint64_t _t3=qpf_us(); rep("vkCmdWriteTimestamp2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCreateDebugUtilsMessengerEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCreateDebugUtilsMessengerEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCreateDebugUtilsMessengerEXT)(); uint64_t _t3=qpf_us(); rep("vkCreateDebugUtilsMessengerEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCreateEvent"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCreateEvent)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCreateEvent)(); uint64_t _t3=qpf_us(); rep("vkCreateEvent",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCreateFramebuffer2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCreateFramebuffer2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCreateFramebuffer2KHR)(); uint64_t _t3=qpf_us(); rep("vkCreateFramebuffer2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCreateQueryPool"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCreateQueryPool)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCreateQueryPool)(); uint64_t _t3=qpf_us(); rep("vkCreateQueryPool",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCreateSamplerYcbcrConversion"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCreateSamplerYcbcrConversion)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCreateSamplerYcbcrConversion)(); uint64_t _t3=qpf_us(); rep("vkCreateSamplerYcbcrConversion",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkCreateSwapchainKHR2"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkCreateSwapchainKHR2)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkCreateSwapchainKHR2)(); uint64_t _t3=qpf_us(); rep("vkCreateSwapchainKHR2",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkDestroyBufferView"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkDestroyBufferView)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkDestroyBufferView)(); uint64_t _t3=qpf_us(); rep("vkDestroyBufferView",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkDestroyDebugUtilsMessengerEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkDestroyDebugUtilsMessengerEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkDestroyDebugUtilsMessengerEXT)(); uint64_t _t3=qpf_us(); rep("vkDestroyDebugUtilsMessengerEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkDestroyDeviceKHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkDestroyDeviceKHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkDestroyDeviceKHR)(); uint64_t _t3=qpf_us(); rep("vkDestroyDeviceKHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkDestroyEvent"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkDestroyEvent)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkDestroyEvent)(); uint64_t _t3=qpf_us(); rep("vkDestroyEvent",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkDestroyQueryPool"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkDestroyQueryPool)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkDestroyQueryPool)(); uint64_t _t3=qpf_us(); rep("vkDestroyQueryPool",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkDestroySamplerYcbcrConversion"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkDestroySamplerYcbcrConversion)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkDestroySamplerYcbcrConversion)(); uint64_t _t3=qpf_us(); rep("vkDestroySamplerYcbcrConversion",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkDestroySwapchainKHR2"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkDestroySwapchainKHR2)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkDestroySwapchainKHR2)(); uint64_t _t3=qpf_us(); rep("vkDestroySwapchainKHR2",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetBufferDeviceAddressKHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetBufferDeviceAddressKHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetBufferDeviceAddressKHR)(); uint64_t _t3=qpf_us(); rep("vkGetBufferDeviceAddressKHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetBufferMemoryRequirements2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetBufferMemoryRequirements2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetBufferMemoryRequirements2KHR)(); uint64_t _t3=qpf_us(); rep("vkGetBufferMemoryRequirements2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetBufferOpaqueCaptureAddressKHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetBufferOpaqueCaptureAddressKHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetBufferOpaqueCaptureAddressKHR)(); uint64_t _t3=qpf_us(); rep("vkGetBufferOpaqueCaptureAddressKHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetDescriptorSetLayoutSupport"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetDescriptorSetLayoutSupport)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetDescriptorSetLayoutSupport)(); uint64_t _t3=qpf_us(); rep("vkGetDescriptorSetLayoutSupport",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetDescriptorSetLayoutSupportKHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetDescriptorSetLayoutSupportKHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetDescriptorSetLayoutSupportKHR)(); uint64_t _t3=qpf_us(); rep("vkGetDescriptorSetLayoutSupportKHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetDeviceGroupPeerMemoryFeatures"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetDeviceGroupPeerMemoryFeatures)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetDeviceGroupPeerMemoryFeatures)(); uint64_t _t3=qpf_us(); rep("vkGetDeviceGroupPeerMemoryFeatures",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetDeviceGroupPresentCapabilitiesKHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetDeviceGroupPresentCapabilitiesKHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetDeviceGroupPresentCapabilitiesKHR)(); uint64_t _t3=qpf_us(); rep("vkGetDeviceGroupPresentCapabilitiesKHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetDeviceGroupSurfacePresentModesKHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetDeviceGroupSurfacePresentModesKHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetDeviceGroupSurfacePresentModesKHR)(); uint64_t _t3=qpf_us(); rep("vkGetDeviceGroupSurfacePresentModesKHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetDeviceImageSparseMemoryRequirements"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetDeviceImageSparseMemoryRequirements)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetDeviceImageSparseMemoryRequirements)(); uint64_t _t3=qpf_us(); rep("vkGetDeviceImageSparseMemoryRequirements",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetDeviceImageSubresourceLayout"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetDeviceImageSubresourceLayout)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetDeviceImageSubresourceLayout)(); uint64_t _t3=qpf_us(); rep("vkGetDeviceImageSubresourceLayout",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetDeviceMemoryCommitment"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetDeviceMemoryCommitment)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetDeviceMemoryCommitment)(); uint64_t _t3=qpf_us(); rep("vkGetDeviceMemoryCommitment",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    /* vkGetDeviceProcAddr SKIPPED as a stub row 2026-11-07 (run phase):
       the row calls the REAL loader entry with no arguments (garbage
       handle and name pointer); in the run of 2026-11-07 the call
       wedged the guest in a delivered data-fault loop at a fixed rip
       (delivered count past 5.5 million) and never returned. This name
       is a working loader function (the harness resolves everything
       through it), not a -8 stub, so the row's premise is false. The
       endless fault redelivery is filed as FD-205; -2 = crashed. */
    { note("SKIP vkGetDeviceProcAddr: stub row calls the real loader entry; wedged the guest (fault loop)");
      trybegin("vkGetDeviceProcAddr"); rep("vkGetDeviceProcAddr",-2,-2,0,0); }
    { trybegin("vkGetDeviceQueue2"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetDeviceQueue2)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetDeviceQueue2)(); uint64_t _t3=qpf_us(); rep("vkGetDeviceQueue2",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetFenceStatus2"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetFenceStatus2)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetFenceStatus2)(); uint64_t _t3=qpf_us(); rep("vkGetFenceStatus2",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetFenceStatus2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetFenceStatus2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetFenceStatus2KHR)(); uint64_t _t3=qpf_us(); rep("vkGetFenceStatus2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetImageMemoryRequirements2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetImageMemoryRequirements2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetImageMemoryRequirements2KHR)(); uint64_t _t3=qpf_us(); rep("vkGetImageMemoryRequirements2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetImageSparseMemoryRequirements"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetImageSparseMemoryRequirements)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetImageSparseMemoryRequirements)(); uint64_t _t3=qpf_us(); rep("vkGetImageSparseMemoryRequirements",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetImageSubresourceLayout2"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetImageSubresourceLayout2)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetImageSubresourceLayout2)(); uint64_t _t3=qpf_us(); rep("vkGetImageSubresourceLayout2",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetImageSubresourceLayout2EXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetImageSubresourceLayout2EXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetImageSubresourceLayout2EXT)(); uint64_t _t3=qpf_us(); rep("vkGetImageSubresourceLayout2EXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    /* vkGetInstanceProcAddr SKIPPED with the pair above: same
       stub-row shape (the real loader entry called with no
       arguments); not individually measured -- skipped on the pair
       precedent, and the record says so. */
    { note("SKIP vkGetInstanceProcAddr: stub row calls the real loader entry (pair of vkGetDeviceProcAddr)");
      trybegin("vkGetInstanceProcAddr"); rep("vkGetInstanceProcAddr",-2,-2,0,0); }
    { trybegin("vkGetPhysicalDeviceFeatures2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetPhysicalDeviceFeatures2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetPhysicalDeviceFeatures2KHR)(); uint64_t _t3=qpf_us(); rep("vkGetPhysicalDeviceFeatures2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetPhysicalDeviceFormatProperties2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetPhysicalDeviceFormatProperties2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetPhysicalDeviceFormatProperties2KHR)(); uint64_t _t3=qpf_us(); rep("vkGetPhysicalDeviceFormatProperties2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetPhysicalDeviceMemoryProperties2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetPhysicalDeviceMemoryProperties2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetPhysicalDeviceMemoryProperties2KHR)(); uint64_t _t3=qpf_us(); rep("vkGetPhysicalDeviceMemoryProperties2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetPhysicalDevicePresentRectanglesKHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetPhysicalDevicePresentRectanglesKHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetPhysicalDevicePresentRectanglesKHR)(); uint64_t _t3=qpf_us(); rep("vkGetPhysicalDevicePresentRectanglesKHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetPhysicalDeviceQueueFamilyProperties2KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetPhysicalDeviceQueueFamilyProperties2KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetPhysicalDeviceQueueFamilyProperties2KHR)(); uint64_t _t3=qpf_us(); rep("vkGetPhysicalDeviceQueueFamilyProperties2KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetPhysicalDeviceSurfaceCapabilities2EXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetPhysicalDeviceSurfaceCapabilities2EXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetPhysicalDeviceSurfaceCapabilities2EXT)(); uint64_t _t3=qpf_us(); rep("vkGetPhysicalDeviceSurfaceCapabilities2EXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetPhysicalDeviceSurfaceFormats2EXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetPhysicalDeviceSurfaceFormats2EXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetPhysicalDeviceSurfaceFormats2EXT)(); uint64_t _t3=qpf_us(); rep("vkGetPhysicalDeviceSurfaceFormats2EXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetPhysicalDeviceSurfacePresentModes2EXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetPhysicalDeviceSurfacePresentModes2EXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetPhysicalDeviceSurfacePresentModes2EXT)(); uint64_t _t3=qpf_us(); rep("vkGetPhysicalDeviceSurfacePresentModes2EXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetPipelineCacheData"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetPipelineCacheData)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetPipelineCacheData)(); uint64_t _t3=qpf_us(); rep("vkGetPipelineCacheData",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetPipelineLayoutSupport"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetPipelineLayoutSupport)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetPipelineLayoutSupport)(); uint64_t _t3=qpf_us(); rep("vkGetPipelineLayoutSupport",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetQueryPoolResults64"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetQueryPoolResults64)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetQueryPoolResults64)(); uint64_t _t3=qpf_us(); rep("vkGetQueryPoolResults64",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetQueryPoolResults64KHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetQueryPoolResults64KHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetQueryPoolResults64KHR)(); uint64_t _t3=qpf_us(); rep("vkGetQueryPoolResults64KHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetRenderAreaGranularity"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetRenderAreaGranularity)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetRenderAreaGranularity)(); uint64_t _t3=qpf_us(); rep("vkGetRenderAreaGranularity",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkGetRenderingAreaGranularity"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkGetRenderingAreaGranularity)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkGetRenderingAreaGranularity)(); uint64_t _t3=qpf_us(); rep("vkGetRenderingAreaGranularity",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkMergePipelineCaches"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkMergePipelineCaches)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkMergePipelineCaches)(); uint64_t _t3=qpf_us(); rep("vkMergePipelineCaches",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkQueueBindSparse"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkQueueBindSparse)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkQueueBindSparse)(); uint64_t _t3=qpf_us(); rep("vkQueueBindSparse",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkResetPipelineCache"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkResetPipelineCache)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkResetPipelineCache)(); uint64_t _t3=qpf_us(); rep("vkResetPipelineCache",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkSetDebugUtilsObjectNameEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkSetDebugUtilsObjectNameEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkSetDebugUtilsObjectNameEXT)(); uint64_t _t3=qpf_us(); rep("vkSetDebugUtilsObjectNameEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkSetDebugUtilsObjectTagEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkSetDebugUtilsObjectTagEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkSetDebugUtilsObjectTagEXT)(); uint64_t _t3=qpf_us(); rep("vkSetDebugUtilsObjectTagEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkSubmitDebugUtilsMessageEXT"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkSubmitDebugUtilsMessageEXT)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkSubmitDebugUtilsMessageEXT)(); uint64_t _t3=qpf_us(); rep("vkSubmitDebugUtilsMessageEXT",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkSwapchainImagesKHR2"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkSwapchainImagesKHR2)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkSwapchainImagesKHR2)(); uint64_t _t3=qpf_us(); rep("vkSwapchainImagesKHR2",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkTrimCommandPool"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkTrimCommandPool)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkTrimCommandPool)(); uint64_t _t3=qpf_us(); rep("vkTrimCommandPool",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
    { trybegin("vkTrimCommandPoolKHR"); uint64_t _t0=qpf_us(); void* _r1=((void*(*)())pvkTrimCommandPoolKHR)(); uint64_t _t1=qpf_us(); uint64_t _t2=qpf_us(); void* _r2=((void*(*)())pvkTrimCommandPoolKHR)(); uint64_t _t3=qpf_us(); rep("vkTrimCommandPoolKHR",(long long)(intptr_t)_r1,(long long)(intptr_t)_r2,_t1-_t0,_t3-_t2); }
}


/* ---- FD-206: real-argument calls for ten implemented handlers ----
 * These ten names used to sit in t_stubs(), whose probes call the
 * function pointer with NO arguments at all
 * (((void*(*)())pfn)()). That probe shape is only meaningful for a
 * thunk that answers without touching its arguments (the -8 loud
 * stubs). These ten are implemented handlers that validate their
 * arguments, so the no-argument probe delivered whatever the
 * registers happened to hold and every one answered
 * VK_ERROR_INITIALIZATION_FAILED (-3) at its argument validation --
 * the uniform -3 measured in 2026-11-07 and filed as FD-206. They
 * are called here with the valid arguments the coverage design
 * always intended (the setup chain's own objects), in t_stubs()'s
 * place in the sequence (after it, before teardown), and their rows
 * record the handlers' real answers. Expected answers, per spec:
 *   vkSetEvent / vkResetEvent            VK_SUCCESS (0)
 *   vkGetEventStatus                     VK_EVENT_RESET (4) after a
 *                                        reset, VK_EVENT_SET (3)
 *                                        after a set -- both are
 *                                        recorded; the results map
 *                                        keeps the last (SET) line
 *   vkCmdSetEvent / vkCmdResetEvent      void (recorded 0)
 *   vkGetQueryPoolResults                VK_NOT_READY (1) for a
 *                                        query never executed;
 *                                        VK_SUCCESS (0), value 1,
 *                                        for one executed below
 *   vkFreeDescriptorSets                 VK_SUCCESS (0)
 *   vkGetDeviceImageMemoryRequirements   void (recorded 0)
 *   vkGetPhysicalDeviceProperties2KHR    void (recorded 0)
 *   vkGetPhysicalDeviceImageFormatProperties2KHR  VK_SUCCESS (0)
 * Four more rows of the same no-argument class were seen answering
 * -3 in this lane's first fixed run (the garbage a no-argument probe
 * delivers changes with every recompilation of the harness, so the
 * affected rows change too): vkCreateQueryPool and the void KHR
 * aliases vkGetPhysicalDeviceFeatures2KHR,
 * vkGetPhysicalDeviceMemoryProperties2KHR and
 * vkGetPhysicalDeviceQueueFamilyProperties2KHR. They are not part
 * of FD-206's named ten -- vkCreateQueryPool is already called with
 * real arguments by the setup chain -- but they are given real
 * calls here as well, so no implemented handler's row in the
 * matrix depends on register garbage.
 */
static void t_fd206_calls(void) {
    note("FD-206 real-argument calls begin");
    /* -- event family, host-side -- */
    T2(vkResetEvent, pvkResetEvent(dev, event));
    T2(vkGetEventStatus, pvkGetEventStatus(dev, event));
    T2(vkSetEvent, pvkSetEvent(dev, event));
    T2(vkGetEventStatus, pvkGetEventStatus(dev, event));
    /* -- query results for a query that was never executed -- */
    { uint64_t buf[2] = { 0, 0 };
      note("FD-206: vkGetQueryPoolResults on never-executed query 1 (expect VK_NOT_READY=1)");
      T2(vkGetQueryPoolResults, pvkGetQueryPoolResults(dev, qpool, 1, 1,
         sizeof(buf), buf, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT)); }
    /* -- command-buffer section: event commands + one executed query -- */
    pvkResetCommandBuffer(cmdbuf, 0);
    { VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
          0, VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT, 0 };
      pvkBeginCommandBuffer(cmdbuf, &bi);
      T2V(vkCmdResetEvent, pvkCmdResetEvent(cmdbuf, event,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
      T2V(vkCmdSetEvent, pvkCmdSetEvent(cmdbuf, event,
          VK_PIPELINE_STAGE_ALL_COMMANDS_BIT));
      pvkCmdBeginQuery(cmdbuf, qpool, 0, 0);
      pvkCmdEndQuery(cmdbuf, qpool, 0);
      pvkEndCommandBuffer(cmdbuf);
      { VkSubmitInfo si = { VK_STRUCTURE_TYPE_SUBMIT_INFO, 0,
            0, 0, 0, 1, &cmdbuf, 0, 0 };
        pvkQueueSubmit(queue, 1, &si, fence); }
      pvkQueueWaitIdle(queue); }
    /* -- query results for the executed query -- */
    { uint64_t buf[2] = { 0, 0 };
      T2(vkGetQueryPoolResults, pvkGetQueryPoolResults(dev, qpool, 0, 1,
         sizeof(buf), buf, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT));
      note("FD-206: executed occlusion query 0 value: %u (expect 1)",
           (unsigned)buf[0]); }
    /* -- free a descriptor set from a pool created for freeing -- */
    { VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1 };
      VkDescriptorPoolCreateInfo ci = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
          0, VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT, 1, 1, &ps };
      VkDescriptorPool lp = 0;
      if (pvkCreateDescriptorPool(dev, &ci, 0, &lp) == VK_SUCCESS && lp) {
        VkDescriptorSetAllocateInfo ai = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            0, lp, 1, &dslayout };
        VkDescriptorSet ls = 0;
        if (pvkAllocateDescriptorSets(dev, &ai, &ls) == VK_SUCCESS && ls)
          T2(vkFreeDescriptorSets, pvkFreeDescriptorSets(dev, lp, 1, &ls));
        pvkDestroyDescriptorPool(dev, lp, 0); } }
    /* -- device image memory requirements (void; sized from create info) -- */
    { VkImageCreateInfo ici = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO, 0, 0,
          VK_IMAGE_TYPE_2D, VK_FORMAT_R8G8B8A8_UNORM, { 64, 64, 1 }, 1, 1,
          VK_SAMPLE_COUNT_1_BIT, VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_SAMPLED_BIT, VK_SHARING_MODE_EXCLUSIVE, 0, 0,
          VK_IMAGE_LAYOUT_UNDEFINED };
      VkDeviceImageMemoryRequirements req = {
          VK_STRUCTURE_TYPE_DEVICE_IMAGE_MEMORY_REQUIREMENTS, 0, &ici, 0 };
      VkMemoryRequirements2 mr2 = { VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2, 0, {0} };
      T2V(vkGetDeviceImageMemoryRequirements,
          pvkGetDeviceImageMemoryRequirements(dev, &req, &mr2));
      note("FD-206: device image memory requirements size: %u",
           (unsigned)mr2.memoryRequirements.size); }
    /* -- the two KHR aliases, with the same arguments as their core twins -- */
    { VkPhysicalDeviceProperties2 p2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, 0, {0} };
      T2V(vkGetPhysicalDeviceProperties2KHR,
          pvkGetPhysicalDeviceProperties2KHR(phys, &p2)); }
    { VkPhysicalDeviceImageFormatInfo2 ii = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
          0, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
          VK_IMAGE_USAGE_SAMPLED_BIT, 0 };
      VkImageFormatProperties2 ifp2 = { VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2, 0, {0} };
      T2(vkGetPhysicalDeviceImageFormatProperties2KHR,
         pvkGetPhysicalDeviceImageFormatProperties2KHR(phys, &ii, &ifp2)); }
    /* -- same-class rows: a pool create and the void KHR aliases -- */
    { VkQueryPoolCreateInfo ci = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
          0, 0, VK_QUERY_TYPE_OCCLUSION, 1, 0 };
      VkQueryPool lp = 0;
      T2(vkCreateQueryPool, pvkCreateQueryPool(dev, &ci, 0, &lp));
      if (lp) pvkDestroyQueryPool(dev, lp, 0); }
    { VkPhysicalDeviceFeatures2 f2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, 0, {0} };
      T2V(vkGetPhysicalDeviceFeatures2KHR,
          pvkGetPhysicalDeviceFeatures2KHR(phys, &f2)); }
    { VkPhysicalDeviceMemoryProperties2 m2 = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2, 0, {0} };
      T2V(vkGetPhysicalDeviceMemoryProperties2KHR,
          pvkGetPhysicalDeviceMemoryProperties2KHR(phys, &m2)); }
    { uint32_t n = 0;
      pvkGetPhysicalDeviceQueueFamilyProperties(phys, &n, 0);
      VkQueueFamilyProperties2 q2[8]; uint32_t n2 = 8;
      for (uint32_t i = 0; i < 8; i++) {
        q2[i].sType = VK_STRUCTURE_TYPE_QUEUE_FAMILY_PROPERTIES_2;
        q2[i].pNext = 0; }
      T2V(vkGetPhysicalDeviceQueueFamilyProperties2KHR,
          pvkGetPhysicalDeviceQueueFamilyProperties2KHR(phys, &n2, q2)); }
    note("FD-206 real-argument calls end");
}

int main(void) {
    vkdll = LoadLibraryA("vulkan-1.dll");
    if (!vkdll) { fprintf(stderr, "vkcov: RESULT 1\n"); return 1; }
    note("vulkan-1.dll loaded at %p", (void*)vkdll);
    /* load all 269 exports */
    LOADFN(vkAcquireNextImageKHR);
    LOADFN(vkAllocateCommandBuffers);
    LOADFN(vkAllocateDescriptorSets);
    LOADFN(vkAllocateMemory);
    LOADFN(vkBeginCommandBuffer);
    LOADFN(vkBindBufferMemory);
    LOADFN(vkBindBufferMemory2);
    LOADFN(vkBindBufferMemory2KHR);
    LOADFN(vkBindImageMemory);
    LOADFN(vkBindImageMemory2);
    LOADFN(vkBindImageMemory2KHR);
    LOADFN(vkCmdBeginConditionalRenderingEXT);
    LOADFN(vkCmdBeginQuery);
    LOADFN(vkCmdBeginRenderPass);
    LOADFN(vkCmdBeginRenderPass2);
    LOADFN(vkCmdBeginRenderPass2KHR);
    LOADFN(vkCmdBeginRendering);
    LOADFN(vkCmdBeginRenderingKHR);
    LOADFN(vkCmdBindDescriptorSets);
    LOADFN(vkCmdBindIndexBuffer);
    LOADFN(vkCmdBindIndexBuffer2);
    LOADFN(vkCmdBindIndexBuffer2KHR);
    LOADFN(vkCmdBindPipeline);
    LOADFN(vkCmdBindVertexBuffers);
    LOADFN(vkCmdBindVertexBuffers2);
    LOADFN(vkCmdBindVertexBuffers2EXT);
    LOADFN(vkCmdBindVertexBuffers2KHR);
    LOADFN(vkCmdBlitImage);
    LOADFN(vkCmdBlitImage2);
    LOADFN(vkCmdBlitImage2KHR);
    LOADFN(vkCmdClearAttachments);
    LOADFN(vkCmdClearAttachmentsKHR);
    LOADFN(vkCmdClearColorImage);
    LOADFN(vkCmdCopyBuffer);
    LOADFN(vkCmdCopyBuffer2);
    LOADFN(vkCmdCopyBuffer2KHR);
    LOADFN(vkCmdCopyBufferToImage);
    LOADFN(vkCmdCopyBufferToImage2);
    LOADFN(vkCmdCopyBufferToImage2KHR);
    LOADFN(vkCmdCopyImage);
    LOADFN(vkCmdCopyImage2);
    LOADFN(vkCmdCopyImage2KHR);
    LOADFN(vkCmdCopyImageToBuffer);
    LOADFN(vkCmdCopyImageToBuffer2);
    LOADFN(vkCmdCopyImageToBuffer2KHR);
    LOADFN(vkCmdDispatch);
    LOADFN(vkCmdDispatchBase);
    LOADFN(vkCmdDispatchIndirect);
    LOADFN(vkCmdDraw);
    LOADFN(vkCmdDrawIndexed);
    LOADFN(vkCmdDrawIndexedIndirect);
    LOADFN(vkCmdDrawIndexedIndirectCount);
    LOADFN(vkCmdDrawIndirect);
    LOADFN(vkCmdDrawIndirectByteCountEXT);
    LOADFN(vkCmdDrawIndirectCount);
    LOADFN(vkCmdDrawIndirectCountKHR);
    LOADFN(vkCmdEndConditionalRenderingEXT);
    LOADFN(vkCmdEndQuery);
    LOADFN(vkCmdEndRenderPass);
    LOADFN(vkCmdEndRenderPass2);
    LOADFN(vkCmdEndRenderPass2KHR);
    LOADFN(vkCmdEndRendering);
    LOADFN(vkCmdEndRenderingKHR);
    LOADFN(vkCmdExecuteCommands);
    LOADFN(vkCmdFillBuffer);
    LOADFN(vkCmdNextSubpass);
    LOADFN(vkCmdNextSubpass2);
    LOADFN(vkCmdNextSubpass2KHR);
    LOADFN(vkCmdNextSubpassKHR);
    LOADFN(vkCmdPipelineBarrier);
    LOADFN(vkCmdPipelineBarrier2);
    LOADFN(vkCmdPipelineBarrier2KHR);
    LOADFN(vkCmdPushConstants);
    LOADFN(vkCmdPushConstants2);
    LOADFN(vkCmdPushConstants2KHR);
    LOADFN(vkCmdPushDescriptorSet);
    LOADFN(vkCmdResetEvent);
    LOADFN(vkCmdResetQueryPool);
    LOADFN(vkCmdResolveImage);
    LOADFN(vkCmdResolveImageKHR);
    LOADFN(vkCmdSetBlendConstant);
    LOADFN(vkCmdSetCullModeEXT);
    LOADFN(vkCmdSetDepthBias);
    LOADFN(vkCmdSetDepthBiasEnableEXT);
    LOADFN(vkCmdSetDepthBiasNV);
    LOADFN(vkCmdSetDepthBounds);
    LOADFN(vkCmdSetDeviceMask);
    LOADFN(vkCmdSetEvent);
    LOADFN(vkCmdSetFrontFaceEXT);
    LOADFN(vkCmdSetLineWidth);
    LOADFN(vkCmdSetPrimitiveTopologyEXT);
    LOADFN(vkCmdSetScissor);
    LOADFN(vkCmdSetScissorWithCount);
    LOADFN(vkCmdSetScissorWithCountEXT);
    LOADFN(vkCmdSetStencilReference);
    LOADFN(vkCmdSetVertexInputEXT);
    LOADFN(vkCmdSetViewport);
    LOADFN(vkCmdSetViewportWithCount);
    LOADFN(vkCmdSetViewportWithCountEXT);
    LOADFN(vkCmdUpdateBuffer);
    LOADFN(vkCmdWaitEvents);
    LOADFN(vkCmdWriteTimestamp);
    LOADFN(vkCmdWriteTimestamp2);
    LOADFN(vkCmdWriteTimestamp2KHR);
    LOADFN(vkCreateBuffer);
    LOADFN(vkCreateBufferView);
    LOADFN(vkCreateCommandPool);
    LOADFN(vkCreateComputePipelines);
    LOADFN(vkCreateDebugUtilsMessengerEXT);
    LOADFN(vkCreateDescriptorPool);
    LOADFN(vkCreateDescriptorSetLayout);
    LOADFN(vkCreateDescriptorUpdateTemplate);
    LOADFN(vkCreateDevice);
    LOADFN(vkCreateEvent);
    LOADFN(vkCreateFence);
    LOADFN(vkCreateFramebuffer);
    LOADFN(vkCreateFramebuffer2KHR);
    LOADFN(vkCreateGraphicsPipelines);
    LOADFN(vkCreateHeadlessSurfaceEXT);
    LOADFN(vkCreateImage);
    LOADFN(vkCreateImageView);
    LOADFN(vkCreateInstance);
    LOADFN(vkCreatePipelineCache);
    LOADFN(vkCreatePipelineLayout);
    LOADFN(vkCreateQueryPool);
    LOADFN(vkCreateRenderPass);
    LOADFN(vkCreateSampler);
    LOADFN(vkCreateSamplerYcbcrConversion);
    LOADFN(vkCreateSemaphore);
    LOADFN(vkCreateShaderModule);
    LOADFN(vkCreateSwapchainKHR);
    LOADFN(vkCreateSwapchainKHR2);
    LOADFN(vkCreateWin32SurfaceKHR);
    LOADFN(vkCreateXcbSurfaceKHR);
    LOADFN(vkCreateXlibSurfaceKHR);
    LOADFN(vkDestroyBuffer);
    LOADFN(vkDestroyBufferView);
    LOADFN(vkDestroyCommandPool);
    LOADFN(vkDestroyDebugUtilsMessengerEXT);
    LOADFN(vkDestroyDescriptorPool);
    LOADFN(vkDestroyDescriptorSetLayout);
    LOADFN(vkDestroyDescriptorUpdateTemplate);
    LOADFN(vkDestroyDevice);
    LOADFN(vkDestroyDeviceKHR);
    LOADFN(vkDestroyEvent);
    LOADFN(vkDestroyFence);
    LOADFN(vkDestroyFramebuffer);
    LOADFN(vkDestroyImage);
    LOADFN(vkDestroyImageView);
    LOADFN(vkDestroyInstance);
    LOADFN(vkDestroyPipeline);
    LOADFN(vkDestroyPipelineCache);
    LOADFN(vkDestroyPipelineLayout);
    LOADFN(vkDestroyQueryPool);
    LOADFN(vkDestroyRenderPass);
    LOADFN(vkDestroySampler);
    LOADFN(vkDestroySamplerYcbcrConversion);
    LOADFN(vkDestroySemaphore);
    LOADFN(vkDestroyShaderModule);
    LOADFN(vkDestroySurfaceKHR);
    LOADFN(vkDestroySwapchainKHR);
    LOADFN(vkDestroySwapchainKHR2);
    LOADFN(vkDeviceWaitIdle);
    LOADFN(vkEndCommandBuffer);
    LOADFN(vkEnumerateDeviceExtensionProperties);
    LOADFN(vkEnumerateInstanceExtensionProperties);
    LOADFN(vkEnumerateInstanceLayerProperties);
    LOADFN(vkEnumerateInstanceVersion);
    LOADFN(vkEnumeratePhysicalDevices);
    LOADFN(vkFlushMappedMemoryRanges);
    LOADFN(vkFreeCommandBuffers);
    LOADFN(vkFreeDescriptorSets);
    LOADFN(vkFreeMemory);
    LOADFN(vkGetBufferDeviceAddress);
    LOADFN(vkGetBufferDeviceAddressKHR);
    LOADFN(vkGetBufferMemoryRequirements);
    LOADFN(vkGetBufferMemoryRequirements2);
    LOADFN(vkGetBufferMemoryRequirements2KHR);
    LOADFN(vkGetBufferOpaqueCaptureAddressKHR);
    LOADFN(vkGetDescriptorSetLayoutSupport);
    LOADFN(vkGetDescriptorSetLayoutSupportKHR);
    LOADFN(vkGetDeviceBufferMemoryRequirements);
    LOADFN(vkGetDeviceGroupPeerMemoryFeatures);
    LOADFN(vkGetDeviceGroupPresentCapabilitiesKHR);
    LOADFN(vkGetDeviceGroupSurfacePresentModesKHR);
    LOADFN(vkGetDeviceImageMemoryRequirements);
    LOADFN(vkGetDeviceImageSparseMemoryRequirements);
    LOADFN(vkGetDeviceImageSubresourceLayout);
    LOADFN(vkGetDeviceMemoryCommitment);
    LOADFN(vkGetDeviceProcAddr);
    LOADFN(vkGetDeviceQueue);
    LOADFN(vkGetDeviceQueue2);
    LOADFN(vkGetEventStatus);
    LOADFN(vkGetFenceStatus);
    LOADFN(vkGetFenceStatus2);
    LOADFN(vkGetFenceStatus2KHR);
    LOADFN(vkGetImageMemoryRequirements);
    LOADFN(vkGetImageMemoryRequirements2);
    LOADFN(vkGetImageMemoryRequirements2KHR);
    LOADFN(vkGetImageSparseMemoryRequirements);
    LOADFN(vkGetImageSubresourceLayout);
    LOADFN(vkGetImageSubresourceLayout2);
    LOADFN(vkGetImageSubresourceLayout2EXT);
    LOADFN(vkGetInstanceProcAddr);
    LOADFN(vkGetPastPresentationTimingGOOGLE);
    LOADFN(vkGetPhysicalDeviceFeatures);
    LOADFN(vkGetPhysicalDeviceFeatures2);
    LOADFN(vkGetPhysicalDeviceFeatures2KHR);
    LOADFN(vkGetPhysicalDeviceFormatProperties);
    LOADFN(vkGetPhysicalDeviceFormatProperties2);
    LOADFN(vkGetPhysicalDeviceFormatProperties2KHR);
    LOADFN(vkGetPhysicalDeviceImageFormatProperties);
    LOADFN(vkGetPhysicalDeviceImageFormatProperties2);
    LOADFN(vkGetPhysicalDeviceImageFormatProperties2KHR);
    LOADFN(vkGetPhysicalDeviceMemoryProperties);
    LOADFN(vkGetPhysicalDeviceMemoryProperties2);
    LOADFN(vkGetPhysicalDeviceMemoryProperties2KHR);
    LOADFN(vkGetPhysicalDevicePresentRectanglesKHR);
    LOADFN(vkGetPhysicalDeviceProperties);
    LOADFN(vkGetPhysicalDeviceProperties2);
    LOADFN(vkGetPhysicalDeviceProperties2KHR);
    LOADFN(vkGetPhysicalDeviceQueueFamilyProperties);
    LOADFN(vkGetPhysicalDeviceQueueFamilyProperties2);
    LOADFN(vkGetPhysicalDeviceQueueFamilyProperties2KHR);
    LOADFN(vkGetPhysicalDeviceSurfaceCapabilities2EXT);
    LOADFN(vkGetPhysicalDeviceSurfaceCapabilitiesKHR);
    LOADFN(vkGetPhysicalDeviceSurfaceFormats2EXT);
    LOADFN(vkGetPhysicalDeviceSurfaceFormatsKHR);
    LOADFN(vkGetPhysicalDeviceSurfacePresentModes2EXT);
    LOADFN(vkGetPhysicalDeviceSurfacePresentModesKHR);
    LOADFN(vkGetPhysicalDeviceSurfaceSupportKHR);
    LOADFN(vkGetPhysicalDeviceWin32PresentationSupportKHR);
    LOADFN(vkGetPhysicalDeviceXlibPresentationSupportKHR);
    LOADFN(vkGetPipelineCacheData);
    LOADFN(vkGetPipelineLayoutSupport);
    LOADFN(vkGetQueryPoolResults);
    LOADFN(vkGetQueryPoolResults64);
    LOADFN(vkGetQueryPoolResults64KHR);
    LOADFN(vkGetRenderAreaGranularity);
    LOADFN(vkGetRenderingAreaGranularity);
    LOADFN(vkGetSemaphoreCounterValue);
    LOADFN(vkGetSwapchainImagesKHR);
    LOADFN(vkInvalidateMappedMemoryRanges);
    LOADFN(vkMapMemory);
    LOADFN(vkMergePipelineCaches);
    LOADFN(vkQueueBindSparse);
    LOADFN(vkQueuePresentKHR);
    LOADFN(vkQueueSubmit);
    LOADFN(vkQueueSubmit2);
    LOADFN(vkQueueSubmit2KHR);
    LOADFN(vkQueueWaitIdle);
    LOADFN(vkResetCommandBuffer);
    LOADFN(vkResetCommandPool);
    LOADFN(vkResetEvent);
    LOADFN(vkResetFences);
    LOADFN(vkResetPipelineCache);
    LOADFN(vkSetDebugUtilsObjectNameEXT);
    LOADFN(vkSetDebugUtilsObjectTagEXT);
    LOADFN(vkSetEvent);
    LOADFN(vkSignalSemaphore);
    LOADFN(vkSubmitDebugUtilsMessageEXT);
    LOADFN(vkSwapchainImagesKHR2);
    LOADFN(vkTrimCommandPool);
    LOADFN(vkTrimCommandPoolKHR);
    LOADFN(vkUnmapMemory);
    LOADFN(vkUpdateDescriptorSetWithTemplate);
    LOADFN(vkUpdateDescriptorSets);
    LOADFN(vkWaitForFences);
    LOADFN(vkWaitSemaphores);
    if (do_setup()) { fprintf(stderr, "vkcov: RESULT 1\n"); return 1; }
    t_instance_queries();
    t_physdev_getters();
    t_memory_ops();
    t_views_descriptors();
    t_cmd_all();
    t_queue_sync();
    t_pipelines();
    t_surface_swapchain();
    t_extended_coverage();
    t_stubs();
    t_fd206_calls();
    t_destroy_all();
    fprintf(stderr, "vkcov: RESULT 0\n");
    return 0;
}
