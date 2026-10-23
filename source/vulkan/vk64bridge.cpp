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

// Host side of the 64-bit Vulkan bridge — see vk64bridge.h / vk64bridge_abi.h.
//
// This is the P2 successor of the P1 native ICD (/tmp/p1work/icd/vkwebgpu_icd.c,
// 1764 LOC, proven by driving the real Vulkan loader 1.3.275 + vkcube to a clean
// exit 0 through it). The object tables, the command recording, the eager
// copyBufferToImage execution and the submit-time frame reconstruction are ported
// essentially verbatim. THREE things change, and only three:
//
//   1. There is no Vulkan LOADER in the guest and no ICD .so. The guest shim
//      (tools/rootfs64/libvk64/libvk64.c) IS the API surface: it exports the 85
//      vk* entry points plus vkGetInstanceProcAddr/vkGetDeviceProcAddr, each of
//      which packs a VK64Args block and traps here on VK64_SYSCALL_NR. So the
//      12 LOOKUP_REQUIRED_GIPA entry points, the negotiate handshake, the
//      VK_LOADER_DATA member-0 rule and the ICD JSON manifest are all gone — the
//      loader contract is vacuous. See tasks/p1-final.md PART 2 section 2.0.
//
//   2. Memory is GUEST memory, not host malloc. KMemory64 is a soft MMU (a
//      per-4KiB-page backing store; getRamPtr() returns nullptr whenever a range
//      crosses a page boundary), so there is no flat host pointer to hand a
//      guest write into. vkAllocateMemory therefore maps through
//      KMemory64::mmapReserveAndMap and vkMapMemory returns that GUEST VA;
//      everything that needs the bytes copies them with memcpyFromGuest /
//      memcpyToGuest. (tasks/p1-final.md section 2.4.)
//
//   3. The sink is one MAIN_THREAD_EM_ASM hop per frame into the page, not a
//      file tree. P1 wrote icd/vkwgpu_stream/frameNNNN.json + sidecar .bin/.spv
//      files that a separate node+chromium process read; P2 emits the SAME
//      manifest schema (tasks/p1-final.md section 2.6 — host/p1render.mjs stays
//      a valid reference consumer) with the sidecar payloads inlined as base64,
//      because there is no filesystem between the wasm host and the page.
//
// Everything runs inline on the calling guest thread (no main-thread hop per
// call): Vulkan already separates recording from submission, so the command
// buffer IS the batching boundary, and only the per-frame handoff hops.

#include "boxedwine.h"
#include "vk32bridge_abi.h"
namespace vk32abi {
#include "vk32_guest.h"
}
#include "bwlog.h"

#ifdef BOXEDWINE_VULKAN64

#include "vk64bridge.h"
#include "vk64bridge_abi.h"
#include "vk_query_results.h"
#include "cpu64.h"
#include "kmemory64.h"
#include "kprocess.h"
#include "ripSampler.h"   // BW64_VKPROF frame-boundary RIPPROF dump (additive)
#include "bw64metrics.h"   // live frame-stage metrics (BW64_METRICS, default on)

// Vulkan core types, for STRUCT LAYOUTS, enums and constants only.
//
// This header is GENERATED from source/vulkan/vk/vulkan_core.h by
// tools/vulkan/gen/gen_vk64_guest.py and is NOT vulkan_core.h itself, because
// this file copies structs out of x86-64 GUEST memory while compiling into the
// wasm32 Emscripten build: in-tree, a `const void* pNext` /
// `const char* const* ppEnabledExtensionNames` / `const VkCommandBuffer*
// pCommandBuffers` member is FOUR bytes here and EIGHT in the guest, so every
// field after the first pointer would be read from the wrong offset. The
// generated header declares the same structs with every pointer/handle member
// widened to uint64_t, which IS the x86-64 layout — verified by diffing
// sizeof/offsetof of 69 structs/offsets against the real header on an LP64 host,
// and guarded by the static_asserts inside the generated header itself.
#include "vk64_guest.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#else
#include <time.h>   // clock_gettime for the BW64_VKPROF vkProfNow() fallback
#endif

#include <algorithm>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <mutex>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <tuple>
#include <deque>
#include <chrono>

namespace {

// ---------------------------------------------------------------------------
// Diagnostics
// ---------------------------------------------------------------------------
// BW64_VKTRACE=1 logs every trapped entry point by name; =2 is unlimited.
//
// getenv() is unreliable on guest worker threads in the pthread build
// (Module.ENV is not always wired to workers), so the env var alone cannot be
// the gate for "the trap log proves calls crossed the boundary". The first
// g_freeTraceCalls calls are therefore ALWAYS logged: that budget covers a whole
// bring-up sequence (P1's loader_tally needed 12 calls to reach vkQueueSubmit;
// vkcube's whole setup is ~40), which makes the evidence independent of env
// plumbing.
// BW64_VKFRAME=1 additionally dumps the full frame manifest JSON.
int  g_traceEnv     = 0;
bool g_traceEnvInit = false;
U64  g_tracedCalls  = 0;
const U64 g_freeTraceCalls = 256;

inline bool vkTrace() {
    if (!g_traceEnvInit) {
        const char* t = getenv("BW64_VKTRACE");
        g_traceEnv = t ? (t[0] == '2' ? 2 : 1) : 0;
        g_traceEnvInit = true;
    }
    return g_traceEnv != 0 || g_tracedCalls < g_freeTraceCalls;
}
// 0 = off, 1 = on with the payload-elision cap, 2 = on, never elide.
inline int vkDumpFrames() {
    static int cached = -1;
    if (cached < 0) {
        const char* t = getenv("BW64_VKFRAME");
        cached = (!t || !t[0] || t[0] == '0') ? 0 : (t[0] == '2' ? 2 : 1);
    }
    return cached;
}
// BW64_FRAMELOG=1 restores the per-frame "FRAME %u built" log line.
// Off by default: klog_fmt costs ~7ms/call (format+mutex+fwrite+fflush).
inline bool vkFrameLog() {
    static int cached = -1;
    if (cached < 0) {
        const char* t = getenv("BW64_FRAMELOG");
        cached = (t && t[0] == '1') ? 1 : 0;
    }
    return cached != 0;
}
// Above this, the manifest's base64 payloads are stripped from the log.
const size_t kManifestLogCap = 64 * 1024;

// ---------------------------------------------------------------------------
// Function-id name table (trace only). MUST stay in sync with
// vk64bridge_abi.h — the guest shim carries the same enum.
// ---------------------------------------------------------------------------
#define VK64_FN_LIST(X)                                                      \
    X(VK64_fn_witness,                              "witness")                \
    X(VK64_fn_unimplemented,                        "unimplemented")          \
    X(VK64_fn_traceProc,                            "traceProc")              \
    X(VK64_fn_vkEnumerateInstanceExtensionProperties,"vkEnumerateInstanceExtensionProperties") \
    X(VK64_fn_vkEnumerateInstanceLayerProperties,   "vkEnumerateInstanceLayerProperties")       \
    X(VK64_fn_vkEnumeratePhysicalDevices,           "vkEnumeratePhysicalDevices")               \
    X(VK64_fn_vkEnumerateDeviceExtensionProperties, "vkEnumerateDeviceExtensionProperties")     \
    X(VK64_fn_vkGetPhysicalDeviceProperties,        "vkGetPhysicalDeviceProperties")            \
    X(VK64_fn_vkGetPhysicalDeviceFeatures,          "vkGetPhysicalDeviceFeatures")              \
    X(VK64_fn_vkGetPhysicalDeviceMemoryProperties,  "vkGetPhysicalDeviceMemoryProperties")        \
    X(VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties,"vkGetPhysicalDeviceQueueFamilyProperties")\
    X(VK64_fn_vkGetPhysicalDeviceFormatProperties,  "vkGetPhysicalDeviceFormatProperties")      \
    X(VK64_fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR") \
    X(VK64_fn_vkGetPhysicalDeviceSurfaceFormatsKHR, "vkGetPhysicalDeviceSurfaceFormatsKHR")     \
    X(VK64_fn_vkGetPhysicalDeviceSurfacePresentModesKHR,"vkGetPhysicalDeviceSurfacePresentModesKHR") \
    X(VK64_fn_vkGetPhysicalDeviceSurfaceSupportKHR, "vkGetPhysicalDeviceSurfaceSupportKHR")     \
    X(VK64_fn_vkGetDeviceQueue,                     "vkGetDeviceQueue")       \
    X(VK64_fn_vkGetBufferMemoryRequirements,        "vkGetBufferMemoryRequirements") \
    X(VK64_fn_vkGetBufferMemoryRequirements2,       "vkGetBufferMemoryRequirements2") \
    X(VK64_fn_vkGetDeviceBufferMemoryRequirements,    "vkGetDeviceBufferMemoryRequirements") \
    X(VK64_fn_vkGetImageMemoryRequirements,         "vkGetImageMemoryRequirements")  \
    X(VK64_fn_vkGetImageMemoryRequirements2,        "vkGetImageMemoryRequirements2") \
    X(VK64_fn_vkGetImageSubresourceLayout,          "vkGetImageSubresourceLayout")   \
    X(VK64_fn_vkGetSwapchainImagesKHR,              "vkGetSwapchainImagesKHR")       \
    X(VK64_fn_vkAcquireNextImageKHR,                "vkAcquireNextImageKHR")         \
    X(VK64_fn_vkGetPastPresentationTimingGOOGLE,    "vkGetPastPresentationTimingGOOGLE") \
    X(VK64_fn_vkResetFences,                        "vkResetFences")       \
    X(VK64_fn_vkWaitForFences,                      "vkWaitForFences")     \
    X(VK64_fn_vkDeviceWaitIdle,                     "vkDeviceWaitIdle")    \
    X(VK64_fn_vkResetCommandBuffer,                 "vkResetCommandBuffer")\
    X(VK64_fn_vkFreeCommandBuffers,                 "vkFreeCommandBuffers")\
    X(VK64_fn_vkCreateInstance,                     "vkCreateInstance")    \
    X(VK64_fn_vkCreateDevice,                       "vkCreateDevice")      \
    X(VK64_fn_vkCreateXcbSurfaceKHR,                "vkCreateXcbSurfaceKHR") \
    X(VK64_fn_vkCreateSwapchainKHR,                 "vkCreateSwapchainKHR") \
    X(VK64_fn_vkCreateCommandPool,                  "vkCreateCommandPool") \
    X(VK64_fn_vkCreateBuffer,                       "vkCreateBuffer")      \
    X(VK64_fn_vkGetBufferDeviceAddress,             "vkGetBufferDeviceAddress") \
    X(VK64_fn_vkCreateImage,                        "vkCreateImage")       \
    X(VK64_fn_vkCreateImageView,                    "vkCreateImageView")   \
    X(VK64_fn_vkCreateBufferView,                       "vkCreateBufferView") \
    X(VK64_fn_vkCreateSampler,                      "vkCreateSampler")     \
    X(VK64_fn_vkCreateRenderPass,                   "vkCreateRenderPass")  \
    X(VK64_fn_vkCreateFramebuffer,                  "vkCreateFramebuffer") \
    X(VK64_fn_vkCreateGraphicsPipelines,            "vkCreateGraphicsPipelines") \
    X(VK64_fn_vkCreateComputePipelines,             "vkCreateComputePipelines") \
    X(VK64_fn_vkCreatePipelineLayout,               "vkCreatePipelineLayout") \
    X(VK64_fn_vkCreateDescriptorSetLayout,          "vkCreateDescriptorSetLayout") \
    X(VK64_fn_vkCreateDescriptorPool,               "vkCreateDescriptorPool") \
    X(VK64_fn_vkCreateShaderModule,                 "vkCreateShaderModule") \
    X(VK64_fn_vkCreatePipelineCache,                "vkCreatePipelineCache") \
    X(VK64_fn_vkCreateFence,                        "vkCreateFence")       \
    X(VK64_fn_vkCreateSemaphore,                    "vkCreateSemaphore")   \
    X(VK64_fn_vkAllocateMemory,                     "vkAllocateMemory")    \
    X(VK64_fn_vkAllocateCommandBuffers,             "vkAllocateCommandBuffers") \
    X(VK64_fn_vkAllocateDescriptorSets,             "vkAllocateDescriptorSets") \
    X(VK64_fn_vkBindBufferMemory,                   "vkBindBufferMemory")  \
    X(VK64_fn_vkBindImageMemory,                    "vkBindImageMemory")   \
    X(VK64_fn_vkUpdateDescriptorSets,               "vkUpdateDescriptorSets") \
    X(VK64_fn_vkMapMemory,                          "vkMapMemory")         \
    X(VK64_fn_vkUnmapMemory,                        "vkUnmapMemory")       \
    X(VK64_fn_vkDestroyInstance,                    "vkDestroyInstance")   \
    X(VK64_fn_vkDestroyDevice,                      "vkDestroyDevice")     \
    X(VK64_fn_vkDestroySurfaceKHR,                  "vkDestroySurfaceKHR") \
    X(VK64_fn_vkDestroySwapchainKHR,                "vkDestroySwapchainKHR") \
    X(VK64_fn_vkDestroyCommandPool,                 "vkDestroyCommandPool") \
    X(VK64_fn_vkDestroyBuffer,                      "vkDestroyBuffer")     \
    X(VK64_fn_vkDestroyImage,                       "vkDestroyImage")      \
    X(VK64_fn_vkDestroyImageView,                   "vkDestroyImageView")  \
    X(VK64_fn_vkDestroySampler,                     "vkDestroySampler")    \
    X(VK64_fn_vkDestroyRenderPass,                  "vkDestroyRenderPass") \
    X(VK64_fn_vkDestroyFramebuffer,                 "vkDestroyFramebuffer")\
    X(VK64_fn_vkDestroyPipeline,                    "vkDestroyPipeline")   \
    X(VK64_fn_vkDestroyPipelineLayout,              "vkDestroyPipelineLayout") \
    X(VK64_fn_vkDestroyDescriptorSetLayout,         "vkDestroyDescriptorSetLayout") \
    X(VK64_fn_vkDestroyDescriptorPool,              "vkDestroyDescriptorPool") \
    X(VK64_fn_vkDestroyShaderModule,                "vkDestroyShaderModule") \
    X(VK64_fn_vkDestroyPipelineCache,               "vkDestroyPipelineCache") \
    X(VK64_fn_vkDestroyFence,                       "vkDestroyFence")      \
    X(VK64_fn_vkDestroySemaphore,                   "vkDestroySemaphore")  \
    X(VK64_fn_vkFreeMemory,                         "vkFreeMemory")        \
    X(VK64_fn_vkBeginCommandBuffer,                 "vkBeginCommandBuffer")\
    X(VK64_fn_vkEndCommandBuffer,                   "vkEndCommandBuffer")  \
    X(VK64_fn_vkCmdBeginRenderPass,                 "vkCmdBeginRenderPass")\
    X(VK64_fn_vkCmdBeginRenderPass2,                "vkCmdBeginRenderPass2")\
    X(VK64_fn_vkCmdBeginRenderPass2KHR,             "vkCmdBeginRenderPass2KHR")\
    X(VK64_fn_vkCmdEndRenderPass2,                  "vkCmdEndRenderPass2")\
    X(VK64_fn_vkCmdEndRenderPass2KHR,               "vkCmdEndRenderPass2KHR")\
    X(VK64_fn_vkCmdPipelineBarrier2,               "vkCmdPipelineBarrier2")\
    X(VK64_fn_vkCmdPipelineBarrier2KHR,            "vkCmdPipelineBarrier2KHR")\
    X(VK64_fn_vkCmdPushConstants2,                 "vkCmdPushConstants2")\
    X(VK64_fn_vkCmdPushConstants2KHR,              "vkCmdPushConstants2KHR")\
    X(VK64_fn_vkCmdBindIndexBuffer2,               "vkCmdBindIndexBuffer2")\
    X(VK64_fn_vkCmdBindIndexBuffer2KHR,            "vkCmdBindIndexBuffer2KHR")\
    X(VK64_fn_vkCmdBeginRendering,                "vkCmdBeginRendering")\
    X(VK64_fn_vkCmdBeginRenderingKHR,             "vkCmdBeginRenderingKHR")\
    X(VK64_fn_vkCmdEndRendering,                  "vkCmdEndRendering")\
    X(VK64_fn_vkCmdEndRenderingKHR,               "vkCmdEndRenderingKHR")\
    X(VK64_fn_vkCmdUpdateBuffer,                  "vkCmdUpdateBuffer")\
    X(VK64_fn_vkCmdBindPipeline,                    "vkCmdBindPipeline")   \
    X(VK64_fn_vkCmdBindDescriptorSets,              "vkCmdBindDescriptorSets") \
    X(VK64_fn_vkCmdSetViewport,                     "vkCmdSetViewport")    \
    X(VK64_fn_vkCmdSetScissor,                      "vkCmdSetScissor")     \
    X(VK64_fn_vkCmdSetBlendConstants, "vkCmdSetBlendConstants") \
    X(VK64_fn_vkCmdSetStencilCompareMask, "vkCmdSetStencilCompareMask") \
    X(VK64_fn_vkCmdSetStencilWriteMask, "vkCmdSetStencilWriteMask") \
    X(VK64_fn_vkCmdSetStencilReference, "vkCmdSetStencilReference") \
    X(VK64_fn_vkCmdSetDepthBias, "vkCmdSetDepthBias") \
    X(VK64_fn_vkCmdSetCullMode, "vkCmdSetCullMode") \
    X(VK64_fn_vkCmdSetFrontFace, "vkCmdSetFrontFace") \
    X(VK64_fn_vkCmdSetEvent2, "vkCmdSetEvent2") \
    X(VK64_fn_vkCmdClearDepthStencilImage, "vkCmdClearDepthStencilImage") \
    X(VK64_fn_vkCmdWriteTimestamp, "vkCmdWriteTimestamp") \
    X(VK64_fn_vkCmdSetDepthBounds, "vkCmdSetDepthBounds") \
    X(VK64_fn_vkCmdCopyQueryPoolResults, "vkCmdCopyQueryPoolResults") \
    X(VK64_fn_vkCmdDraw,                            "vkCmdDraw")           \
    X(VK64_fn_vkCmdPipelineBarrier,                 "vkCmdPipelineBarrier")\
    X(VK64_fn_vkCmdCopyBufferToImage,               "vkCmdCopyBufferToImage") \
    X(VK64_fn_vkFlushMappedMemoryRanges,            "vkFlushMappedMemoryRanges") \
    X(VK64_fn_vkInvalidateMappedMemoryRanges,       "vkInvalidateMappedMemoryRanges") \
    X(VK64_fn_vkCmdEndRenderPass,                   "vkCmdEndRenderPass")  \
    X(VK64_fn_vkQueueSubmit,                        "vkQueueSubmit")       \
    X(VK64_fn_vkQueueSubmit2,                       "vkQueueSubmit2")      \
    X(VK64_fn_vkQueueSubmit2KHR,                    "vkQueueSubmit2KHR")   \
    X(VK64_fn_vkCreateDescriptorUpdateTemplate,     "vkCreateDescriptorUpdateTemplate") \
    X(VK64_fn_vkDestroyDescriptorUpdateTemplate,    "vkDestroyDescriptorUpdateTemplate") \
    X(VK64_fn_vkUpdateDescriptorSetWithTemplate,    "vkUpdateDescriptorSetWithTemplate") \
    X(VK64_fn_vkQueuePresentKHR,                    "vkQueuePresentKHR")   \
    X(VK64_fn_vkEnumerateInstanceVersion,           "vkEnumerateInstanceVersion") \
    X(VK64_fn_vkGetPhysicalDeviceProperties2,       "vkGetPhysicalDeviceProperties2") \
    X(VK64_fn_vkGetPhysicalDeviceFeatures2,         "vkGetPhysicalDeviceFeatures2") \
    X(VK64_fn_vkGetPhysicalDeviceMemoryProperties2, "vkGetPhysicalDeviceMemoryProperties2") \
    X(VK64_fn_vkGetPhysicalDeviceProperties2KHR,       "vkGetPhysicalDeviceProperties2KHR") \
    X(VK64_fn_vkGetPhysicalDeviceFeatures2KHR,         "vkGetPhysicalDeviceFeatures2KHR") \
    X(VK64_fn_vkGetPhysicalDeviceMemoryProperties2KHR, "vkGetPhysicalDeviceMemoryProperties2KHR") \
    X(VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties2,"vkGetPhysicalDeviceQueueFamilyProperties2") \
    X(VK64_fn_vkGetFenceStatus,                     "vkGetFenceStatus")    \
    X(VK64_fn_vkCreateEvent,                       "vkCreateEvent")       \
    X(VK64_fn_vkDestroyEvent,                      "vkDestroyEvent")      \
    X(VK64_fn_vkSetEvent,                          "vkSetEvent")          \
    X(VK64_fn_vkResetEvent,                        "vkResetEvent")        \
    X(VK64_fn_vkGetEventStatus,                    "vkGetEventStatus")    \
    X(VK64_fn_vkCmdSetEvent,                       "vkCmdSetEvent")       \
    X(VK64_fn_vkCmdResetEvent,                     "vkCmdResetEvent")     \
    X(VK64_fn_vkCmdWaitEvents,                     "vkCmdWaitEvents")     \
    X(VK64_fn_vkCreateQueryPool,                   "vkCreateQueryPool")   \
    X(VK64_fn_vkDestroyQueryPool,                  "vkDestroyQueryPool")  \
    X(VK64_fn_vkResetQueryPool,                    "vkResetQueryPool")    \
    X(VK64_fn_vkGetQueryPoolResults,               "vkGetQueryPoolResults") \
    X(VK64_fn_vkCmdResetQueryPool,                 "vkCmdResetQueryPool") \
    X(VK64_fn_vkCmdBeginQuery,                     "vkCmdBeginQuery")     \
    X(VK64_fn_vkCmdEndQuery,                       "vkCmdEndQuery")       \
    X(VK64_fn_vkResetCommandPool,                   "vkResetCommandPool")  \
    X(VK64_fn_vkQueueWaitIdle,                      "vkQueueWaitIdle")     \
    X(VK64_fn_vkCreateHeadlessSurfaceEXT,           "vkCreateHeadlessSurfaceEXT") \
    X(VK64_fn_vkCreateWin32SurfaceKHR,              "vkCreateWin32SurfaceKHR") \
    X(VK64_fn_vkGetSemaphoreCounterValue,           "vkGetSemaphoreCounterValue") \
    X(VK64_fn_vkWaitSemaphores,                     "vkWaitSemaphores")    \
    X(VK64_fn_vkSignalSemaphore,                    "vkSignalSemaphore")   \
    X(VK64_fn_vkCmdPushConstants,                   "vkCmdPushConstants") \
    X(VK64_fn_vkCreateXlibSurfaceKHR,               "vkCreateXlibSurfaceKHR") \
    X(VK64_fn_vkGetPhysicalDeviceXlibPresentationSupportKHR, "vkGetPhysicalDeviceXlibPresentationSupportKHR") \
    X(VK64_fn_vkGetPhysicalDeviceWin32PresentationSupportKHR, "vkGetPhysicalDeviceWin32PresentationSupportKHR") \
    X(VK64_fn_vkGetPhysicalDeviceImageFormatProperties2, "vkGetPhysicalDeviceImageFormatProperties2") \
    X(VK64_fn_vkGetPhysicalDeviceImageFormatProperties,  "vkGetPhysicalDeviceImageFormatProperties") \
    X(VK64_fn_vkGetPhysicalDeviceFormatProperties2,      "vkGetPhysicalDeviceFormatProperties2") \
    X(VK64_fn_vkCmdDrawIndexed,                          "vkCmdDrawIndexed") \
    X(VK64_fn_vkCmdBindVertexBuffers2,                    "vkCmdBindVertexBuffers2") \
    X(VK64_fn_vkCmdBindVertexBuffers,                     "vkCmdBindVertexBuffers") \
    X(VK64_fn_vkCmdBindIndexBuffer,                       "vkCmdBindIndexBuffer") \
    X(VK64_fn_vkBindBufferMemory2,                        "vkBindBufferMemory2") \
    X(VK64_fn_vkBindBufferMemory2KHR,                     "vkBindBufferMemory2KHR") \
    X(VK64_fn_vkBindImageMemory2,                         "vkBindImageMemory2") \
    X(VK64_fn_vkBindImageMemory2KHR,                      "vkBindImageMemory2KHR") \
    X(VK64_fn_vkCmdCopyImageToBuffer,                    "vkCmdCopyImageToBuffer") \
    X(VK64_fn_vkCmdCopyImage,                            "vkCmdCopyImage") \
    X(VK64_fn_vkCmdCopyImageToBuffer2,                   "vkCmdCopyImageToBuffer2") \
    X(VK64_fn_vkCmdCopyImageToBuffer2KHR,                "vkCmdCopyImageToBuffer2KHR") \
    X(VK64_fn_vkCmdCopyBuffer,                         "vkCmdCopyBuffer") \
    X(VK64_fn_vkCmdCopyBuffer2,                        "vkCmdCopyBuffer2") \
    X(VK64_fn_vkCmdCopyBuffer2KHR,                     "vkCmdCopyBuffer2KHR") \
    X(VK64_fn_vkCmdFillBuffer,                         "vkCmdFillBuffer") \
    X(VK64_fn_vkCmdClearColorImage,                    "vkCmdClearColorImage") \
    X(VK64_fn_vkCmdClearAttachments,                   "vkCmdClearAttachments") \
    X(VK64_fn_vkCmdClearAttachmentsKHR,                "vkCmdClearAttachmentsKHR") \
    X(VK64_fn_vkCmdCopyImage,                          "vkCmdCopyImage") \
    X(VK64_fn_vkCmdCopyImage2,                         "vkCmdCopyImage2") \
    X(VK64_fn_vkCmdCopyImage2KHR,                      "vkCmdCopyImage2KHR") \
    X(VK64_fn_vkCmdCopyBufferToImage2,                 "vkCmdCopyBufferToImage2") \
    X(VK64_fn_vkCmdCopyBufferToImage2KHR,              "vkCmdCopyBufferToImage2KHR") \
    X(VK64_fn_vkCmdBlitImage,                          "vkCmdBlitImage") \
    X(VK64_fn_vkCmdBlitImage2,                         "vkCmdBlitImage2") \
    X(VK64_fn_vkCmdBlitImage2KHR,                      "vkCmdBlitImage2KHR") \
    X(VK64_fn_vkCmdResolveImage,                       "vkCmdResolveImage") \
    X(VK64_fn_vkCmdResolveImageKHR,                    "vkCmdResolveImageKHR") \
    X(VK64_fn_vkCmdDrawIndirect,                       "vkCmdDrawIndirect") \
    X(VK64_fn_vkCmdDrawIndexedIndirect,                "vkCmdDrawIndexedIndirect") \
    X(VK64_fn_vkCmdDrawIndirectCount,                  "vkCmdDrawIndirectCount") \
    X(VK64_fn_vkCmdDrawIndirectCountKHR,               "vkCmdDrawIndirectCountKHR") \
    X(VK64_fn_vkCmdDrawIndexedIndirectCount,           "vkCmdDrawIndexedIndirectCount") \
    X(VK64_fn_vkCmdExecuteCommands,                    "vkCmdExecuteCommands") \
    X(VK64_fn_vkCmdDispatch,                           "vkCmdDispatch") \
    X(VK64_fn_vkCmdDispatchBase,                       "vkCmdDispatchBase") \
    X(VK64_fn_vkCmdDispatchIndirect,                   "vkCmdDispatchIndirect")
#define VK64_NAME_ENTRY(id, nm) { (U64)(id), nm },
const struct { U64 id; const char* name; } g_fnNames[] = { VK64_FN_LIST(VK64_NAME_ENTRY) };
#undef VK64_NAME_ENTRY
const char* fnName(U64 id) {
    for (const auto& e : g_fnNames) if (e.id == id) return e.name;
    return "?";
}

std::recursive_mutex g_vkMutex;   // one bridge per process; every trap serializes
KMemory* g_mem32 = nullptr;
KMemory64* g_mem = nullptr;       // guest memory of the calling thread's process
inline void readGuestBytes(void* out, U64 va, size_t count) {
    if (g_mem32) g_mem32->memcpyFromGuest(out, (U32)va, count);
    else if (g_mem) g_mem->memcpyFromGuest(out, va, count);
}
inline void writeGuestBytes(U64 va, const void* in, size_t count) {
    if (g_mem32) g_mem32->memcpyToGuest((U32)va, in, count);
    else if (g_mem) g_mem->memcpyToGuest(va, in, count);
}

static U64 g_bufDirtyTick = 1;      // buffer dirtiness generation counter

// ---------------------------------------------------------------------------
// "No silent drops". Every guard that discards a guest command, struct or array
// goes through VK64_DROP_IF, which records the drop before it takes its action:
// it logs the first 4 hits per (trap fn, source line), then the 8th, 16th, 32nd...
// and keeps a running total. Legitimate no-ops (zero counts or sizes, optional
// null pointers, destroying VK_NULL_HANDLE, de-duplication) stay plain `if`s on
// purpose. vk64BridgeDropCount() exposes the total to tests and diagnostics.
// ---------------------------------------------------------------------------
static thread_local U64 g_dropFn = 0;
static U64 g_dropTotal = 0;
U64 vk64BridgeDropCount() { return g_dropTotal; }
static void vk64DropLog(const char* cond, int line) {
    static std::mutex lock;
    static std::unordered_map<U64, U32> seen;
    std::lock_guard<std::mutex> guard(lock);
    ++g_dropTotal;
    U32& n = seen[((U64)g_dropFn << 32) | (U32)line];
    if (++n <= 4 || (n & (n - 1)) == 0)
        klog_fmt("vk64: DROPPED guest command in %s (vk64bridge.cpp:%d, x%u, %llu drops total): guard `%s`",
                 fnName(g_dropFn), line, n, (unsigned long long)g_dropTotal, cond);
}
#define VK64_DROP_IF(action, ...) do { if (__VA_ARGS__) { vk64DropLog(#__VA_ARGS__, __LINE__); action; } } while (0)

// ---------------------------------------------------------------------------
// "Rejected loudly". A guest asked for a Vulkan feature the WebGPU page tier
// cannot execute (geometry/tessellation stages, sparse binding, transform
// feedback, ...). The bridge answers with the correct VkResult where the entry
// point has one, and ALWAYS calls vk64Unsupported() so the request is greppable:
//     vk64: UNSUPPORTED <feature> (<detail>) in <fn> x<n> (<total> total)
// The first 4 hits per feature are logged, then the 8th, 16th, 32nd, ... Counts
// are queryable per feature (vk64BridgeUnsupportedCount) for tests and metrics.
// docs/known-gaps/vulkan.md lists every feature name that can appear here.
// ---------------------------------------------------------------------------
static U64 g_unsupportedTotal = 0;
static std::map<std::string, U32>& vk64UnsupportedCounts() {
    static std::map<std::string, U32> counts;
    return counts;
}
static std::mutex& vk64UnsupportedLock() { static std::mutex lock; return lock; }
U64 vk64BridgeUnsupportedTotal() {
    std::lock_guard<std::mutex> guard(vk64UnsupportedLock());
    return g_unsupportedTotal;
}
U32 vk64BridgeUnsupportedCount(const char* feature) {
    std::lock_guard<std::mutex> guard(vk64UnsupportedLock());
    auto it = vk64UnsupportedCounts().find(feature);
    return it == vk64UnsupportedCounts().end() ? 0 : it->second;
}
static void vk64Unsupported(const char* feature, const char* detail = "") {
    std::lock_guard<std::mutex> guard(vk64UnsupportedLock());
    ++g_unsupportedTotal;
    U32& n = vk64UnsupportedCounts()[feature];
    if (++n <= 4 || (n & (n - 1)) == 0)
        klog_fmt("vk64: UNSUPPORTED %s (%s) in %s x%u (%llu total)", feature, detail,
                 fnName(g_dropFn), n, (unsigned long long)g_unsupportedTotal);
}

// ---------------------------------------------------------------------------
// SPIR-V audit. vkCreateShaderModule scans each module once for the capabilities
// and resource shapes the WebGPU tier cannot honour and reports every one as
// "vk64: UNSUPPORTED spirv-<what>" (never rejects: the module may still be
// harmless, and the page tier decides whether translation fails). This is what
// backs the "exception" rows of source/vulkan/vk-capabilities.json for features
// that have no pipeline-state representation (cull distance, 64-bit types,
// storage-buffer stores, storage images, ...).
// ---------------------------------------------------------------------------
// Sampler state the WebGPU tier cannot honour. Reported once per sampler, not per draw.
static void vk64AuditSampler(U32 addrU, U32 addrV, U32 addrW, float mipLodBias, U32 borderColor) {
    if (addrU == 4 || addrV == 4 || addrW == 4)   // VK_SAMPLER_ADDRESS_MODE_MIRROR_CLAMP_TO_EDGE
        vk64Unsupported("mirror-clamp-to-edge", "WebGPU has no mirror-clamp; sampler uses mirror-repeat/clamp");
    if (addrU == 3 || addrV == 3 || addrW == 3)    // CLAMP_TO_BORDER
        vk64Unsupported("sampler-border-colour", ("WebGPU has no border addressing; clamp-to-edge used, borderColor=" + std::to_string(borderColor)).c_str());
    if (mipLodBias != 0.0f) vk64Unsupported("sampler-mip-lod-bias", "WebGPU samplers have no LOD bias; ignored");
}

static const char* vk64SpirvCapabilityName(U32 cap) {
    switch (cap) {
        case 2: return "Geometry";            case 3: return "Tessellation";
        case 9: return "Float16";             case 10: return "Float64";
        case 11: return "Int64";              case 12: return "Int64Atomics";
        case 22: return "Int16";              case 23: return "TessellationPointSize";
        case 24: return "GeometryPointSize";  case 25: return "ImageGatherExtended";
        case 27: return "StorageImageMultisample";
        case 33: return "CullDistance";       case 35: return "SampleRateShading";
        case 39: return "Int8";               case 41: return "SparseResidency";
        case 42: return "MinLod";             case 49: return "StorageImageExtendedFormats";
        case 52: return "InterpolationFunction";
        case 57: return "MultiViewport";
        case 4433: return "StorageBuffer16BitAccess";
        case 4434: return "UniformAndStorageBuffer16BitAccess";
        case 4435: return "StoragePushConstant16";
        case 4436: return "StorageInputOutput16";
        case 4439: return "MultiView";
        case 4441: return "VariablePointersStorageBuffer";
        case 4442: return "VariablePointers";
        case 4448: return "StorageBuffer8BitAccess";
        case 4449: return "UniformAndStorageBuffer8BitAccess";
        case 4450: return "StoragePushConstant8";
        case 5345: return "ShaderViewportIndexLayerEXT";
        case 5301: return nullptr;            // ShaderNonUniform: harmless on its own
        // Descriptor-indexing capabilities: the descriptor model keeps ONE resource per
        // binding number, so a shader that indexes an array of resources by a runtime
        // value cannot be honoured. These are the loud half of the descriptor-indexing
        // "exception" rows in vk-capabilities.json.
        case 5302: return "RuntimeDescriptorArray";
        case 5303: return "InputAttachmentArrayDynamicIndexing";
        case 5304: return "UniformTexelBufferArrayDynamicIndexing";
        case 5305: return "StorageTexelBufferArrayDynamicIndexing";
        case 5306: return "UniformBufferArrayNonUniformIndexing";
        case 5307: return "SampledImageArrayNonUniformIndexing";
        case 5308: return "StorageBufferArrayNonUniformIndexing";
        case 5309: return "StorageImageArrayNonUniformIndexing";
        default: return nullptr;
    }
}
static void vk64AuditSpirv(const std::vector<U8>& code) {
    if (code.size() < 20 || (code.size() & 3)) { vk64Unsupported("spirv-malformed", "size not a multiple of 4 or no header"); return; }
    const size_t nw = code.size() / 4;
    std::vector<U32> w(nw);
    memcpy(w.data(), code.data(), code.size());
    if (w[0] != 0x07230203u) { vk64Unsupported("spirv-malformed", "bad magic"); return; }
    struct Ptr { U32 sc, pointee; };
    std::unordered_map<U32, Ptr> ptrs;
    std::unordered_map<U32, U32> arrays;             // array / runtime-array id -> element type
    std::unordered_map<U32, U32> structMembers;      // struct id -> member count
    std::unordered_map<U32, std::set<U32>> memberNonWritable;
    std::set<U32> nonWritable, bufferBlock;
    struct Var { U32 id, type, sc; };
    std::vector<Var> vars;
    bool storageImage = false, compute = false;
    std::set<U32> caps;
    for (size_t i = 5; i < nw;) {
        const U32 op = w[i] & 0xFFFF, len = w[i] >> 16;
        if (!len || i + len > nw) { vk64Unsupported("spirv-malformed", "truncated instruction"); return; }
        switch (op) {
            case 17: if (len >= 2) caps.insert(w[i + 1]); break;                       // OpCapability
            case 15: if (len >= 2 && w[i + 1] == 5) compute = true; break;             // OpEntryPoint GLCompute
            case 30: if (len >= 2) structMembers[w[i + 1]] = len - 2; break;           // OpTypeStruct
            case 28: case 29: if (len >= 3) arrays[w[i + 1]] = w[i + 2]; break;        // OpTypeArray/RuntimeArray
            case 32: if (len >= 4) ptrs[w[i + 1]] = {w[i + 2], w[i + 3]}; break;       // OpTypePointer
            case 25: if (len >= 9 && w[i + 7] == 2) storageImage = true; break;        // OpTypeImage, Sampled==2
            case 59: if (len >= 4) vars.push_back({w[i + 2], w[i + 1], w[i + 3]}); break; // OpVariable
            case 71: if (len >= 3) {                                                   // OpDecorate
                if (w[i + 2] == 24) nonWritable.insert(w[i + 1]);
                if (w[i + 2] == 3) bufferBlock.insert(w[i + 1]);
            } break;
            case 72: if (len >= 4 && w[i + 3] == 24) memberNonWritable[w[i + 1]].insert(w[i + 2]); break; // OpMemberDecorate
            default: break;
        }
        i += len;
    }
    for (U32 c : caps) {
        const char* name = vk64SpirvCapabilityName(c);
        if (name) vk64Unsupported(("spirv-capability:" + std::string(name)).c_str(), "shader declares a capability the WebGPU tier lacks");
    }
    if (compute) vk64Unsupported("spirv-compute-entry", "compute entry point; no compute path in the V2 protocol");
    if (storageImage) vk64Unsupported("spirv-storage-image", "storage images are bound blank by the page tier");
    for (const Var& v : vars) {
        auto pt = ptrs.find(v.type);
        if (pt == ptrs.end()) continue;
        U32 t = pt->second.pointee;
        for (int d = 0; d < 4; d++) { auto a = arrays.find(t); if (a == arrays.end()) break; t = a->second; }
        const bool ssbo = v.sc == 12 || (v.sc == 2 && bufferBlock.count(t));
        if (!ssbo) continue;
        if (nonWritable.count(v.id)) continue;
        auto m = memberNonWritable.find(t);
        auto n = structMembers.find(t);
        if (m != memberNonWritable.end() && n != structMembers.end() && m->second.size() >= n->second) continue;
        vk64Unsupported("spirv-storage-buffer-write", "shader may store to a storage buffer; the page tier has no GPU->guest readback");
        break;
    }
}

// ---------------------------------------------------------------------------
// Guest memory helpers. EVERY guest pointer arrives here as a VA and is only
// ever read/written through these.
// ---------------------------------------------------------------------------
template <class T> inline bool readStruct(U64 addr, T& out) {
    if (!addr || !g_mem) return false;
    g_mem->memcpyFromGuest(&out, addr, sizeof(T));
    return true;
}
template <class T> inline void writeStruct(U64 addr, const T& in) {
    if (addr && g_mem) g_mem->memcpyToGuest(addr, &in, sizeof(T));
}
template <class T> inline void readArray(U64 addr, U32 count, std::vector<T>& out) {
    out.clear();
    if (!addr || !g_mem || !count) return;
    out.resize(count);
    g_mem->memcpyFromGuest(out.data(), addr, sizeof(T) * (size_t)count);
}
template <class T> inline void writeArray(U64 addr, const std::vector<T>& in, U32 count) {
    if (addr && g_mem && count) g_mem->memcpyToGuest(addr, in.data(), sizeof(T) * (size_t)count);
}
inline U32  rd32(U64 a) { return (a && g_mem) ? g_mem->readd(a) : 0; }
inline void wr32(U64 a, U32 v) { if (a && g_mem) g_mem->writed(a, v); }
inline U64  rd64(U64 a) { return (a && g_mem) ? g_mem->readq(a) : 0; }
inline void wr64(U64 a, U64 v) { if (a && g_mem) g_mem->writeq(a, v); }
inline std::string guestStr(U64 addr, size_t maxLen = 512) {
    std::string s;
    if (!addr || !g_mem) return s;
    for (size_t i = 0; i < maxLen; i++) {
        U8 c = g_mem->readb(addr + i);
        if (!c) break;
        s.push_back((char)c);
    }
    return s;
}

// ---------------------------------------------------------------------------
// Opaque handle registry.
//
// Vulkan handles are opaque to the guest, and P1 (which handed out real host
// pointers) could only validate them with a magic word read through a possibly
// freed pointer. Here every handle is an integer id into a live-object table,
// so a stale or bogus handle from the guest is a lookup miss — logged, benign
// return, no fault. (tasks/p1-final.md section 2.0 property 2.)
// ---------------------------------------------------------------------------
enum ObjKind : U32 {
    K_INSTANCE = 1, K_PHYSDEV, K_DEVICE, K_QUEUE, K_CMDPOOL, K_CMDBUF, K_MEMORY,
    K_BUFFER, K_IMAGE, K_IMAGEVIEW, K_BUFFERVIEW, K_SAMPLER, K_SHADER, K_SETLAYOUT, K_SET,
    K_PIPELAYOUT, K_PIPELINE, K_RENDERPASS, K_FRAMEBUFFER, K_PIPECACHE,
    K_DESCPOOL, K_FENCE, K_SEMAPHORE, K_SWAPCHAIN, K_SURFACE, K_TEMPLATE,
    K_EVENT, K_QUERYPOOL
};
struct Obj {
    virtual ~Obj() = default;
    U32 kind = 0;
    U64 magic = 0;
};
const U64 OBJ_MAGIC = 0x564B4F42ull;   // 'VKOB'

std::unordered_map<U64, Obj*> g_objs;
U64 g_nextId = 0x564B0100;
U64 g_nextDevVa = 0x4000000000ULL; // VA space for vkGetBufferDeviceAddress (256MB stride)

template <class T> T* createObj(U32 kind, U64& idOut) {
    T* o = new T();
    o->kind = kind;
    o->magic = OBJ_MAGIC;
    idOut = g_nextId++;
    g_objs[idOut] = o;
    return o;
}
Obj* findObj(U64 id, U32 kind) {
    if (!id) return nullptr;
    auto it = g_objs.find(id);
    if (it == g_objs.end()) return nullptr;
    Obj* o = it->second;
    if (!o || o->magic != OBJ_MAGIC || o->kind != kind) return nullptr;
    return o;
}
template <class T> T* objOf(U64 id, U32 kind, const char* who) {
    Obj* o = findObj(id, kind);
    if (!o && id) {
        // Rate-limit: the submit path looks up the same stale descriptors
        // every frame; klog_fmt+fflush costs ~7ms per call (~74ms/frame).
        // Log each distinct id once to preserve diagnostics.
        static std::unordered_set<U64> logged;
        if (logged.insert(id).second)
            klog_fmt("vk64: %s: bad/stale handle 0x%llx", who, (unsigned long long)id);
    }
    return (T*)o;
}
void dropObj(U64 id) {
    auto it = g_objs.find(id);
    VK64_DROP_IF(return, it == g_objs.end());
    delete it->second;
    g_objs.erase(it);
}

// ---------------------------------------------------------------------------
// Object bodies (ported from P1's DECL_NONDISP set; the create-info fields P1
// recorded are the same fields P2 records — that is what lets submit rebuild a
// complete frame manifest without the app re-stating anything).
// ---------------------------------------------------------------------------
const U32 VK64_MAX_SETS = 8, VK64_MAX_BINDINGS = 8, VK64_MAX_ATTACH = 4, VK64_MAX_DRAWS = 64;
const U32 VK64_MAX_CMDS = 4096;
const U32 VK64_MAX_VERT_BINDINGS = 4, VK64_MAX_VERT_ATTRIBS = 16;
const U32 VK64_MAX_PUSH_BYTES = 256;   // matches limits.maxPushConstantsSize

// dstArrayElement + count, so a descriptor ARRAY write is not silently collapsed
// to its first element (audit §f: DXVK writes texture arrays for descriptor
// indexing). The elements themselves stay in the flat Binding array — the
// per-element payload schema is P3 work — but the INDEX and COUNT are recorded
// now so the schema has something honest to read.
struct Binding { U32 binding = 0, type = 0, dstArrayElement = 0, count = 1; U64 obj = 0, range = 0;
                 U64 bufOff = 0; std::vector<U8> inlineBytes; }; // VkDescriptorBufferInfo::offset: DXVK sub-allocates
                 // uniforms (D3D9ConstantBuffer::Alloc) and binds each range with its own
                 // offset; dropping it made every uniform capture read from offset 0.

// Inline uniform pData is only valid during the update call. Keep the complete
// binding so byte-offset writes preserve portions written by previous updates.
static void updateInlineUniform(Binding& b, U32 offset, U64 data, U32 size) {
    if (!data || !size || size > UINT32_MAX - offset) return;
    const U32 end = offset + size;
    if (b.inlineBytes.size() < end) b.inlineBytes.resize(end);
    readGuestBytes(b.inlineBytes.data() + offset, data, size);
    b.obj = 0;
    b.bufOff = 0;
    b.dstArrayElement = 0;
    b.count = (U32)b.inlineBytes.size();
    b.range = b.inlineBytes.size();
}

struct Instance : Obj { U64 phys[4] = {0,0,0,0}; U32 physCount = 0; };
struct PhysDev  : Obj { U64 instance = 0; };
struct Device   : Obj { U64 physDev = 0, queue = 0; };
struct Queue    : Obj { U64 device = 0; };
struct CmdPool  : Obj { U64 device = 0; U32 queueFamily = 0; };
struct PipelineCache : Obj {};
struct DescPool  : Obj {};
struct Surface   : Obj {};
struct Fence     : Obj { int signalled = 0; };
struct Event     : Obj { int signalled = 0; };
struct QueryPool : Obj { U32 type = 0, count = 0, flags = 0; std::vector<U64> results; std::vector<U8> available; };
struct QueryPoolCreateInfo64Local {
    U32 sType, _pad0; U64 pNext; U32 flags, queryType, queryCount, pipelineStatistics;
};
struct QueryPoolCreateInfo32Local {
    U32 sType, pNext, flags, queryType, queryCount, pipelineStatistics;
};
static_assert(sizeof(QueryPoolCreateInfo64Local) == 32, "64-bit VkQueryPoolCreateInfo layout");
static_assert(sizeof(QueryPoolCreateInfo32Local) == 24, "32-bit VkQueryPoolCreateInfo layout");
// Timeline semaphores (audit P2-NOW item 3). DXVK puts a u64 wait/signal fence
// on EVERY command-list flush, so the counter is not optional bookkeeping: it is
// the only thing that makes vkQueueSubmit's VkTimelineSemaphoreSubmitInfo
// meaningful. Under immediate execution the counter is HONEST — everything a
// submit waits for has already run, and everything it signals is already done —
// so signal is a monotone max and a wait either succeeds (value reached) or,
// with timeout 0 against a value the queue has not reached, returns VK_TIMEOUT.
struct Semaphore : Obj {
    int signalled = 0;          /* binary semaphores keep the old flag */
    int isTimeline = 0;
    U64  counter = 0;
};
struct DeviceMemory : Obj { U64 size = 0, va = 0; U32 memIndex = 0; bool wasMapped = false; };
struct Buffer : Obj { U64 size = 0, mem = 0, memOff = 0; U32 usage = 0; U64 dirtyGen = 0; U64 devVa = 0; };
struct Image  : Obj {
    U32 w = 0, h = 0, d = 0, layers = 0, mips = 0, format = 0, tiling = 0, usage = 0, samples = 0;
    U64 mem = 0, memOff = 0, pixBytes = 0;
    int copied = 0;                 // a staging copyBufferToImage landed here
};
struct ImageView : Obj { U64 image = 0; U32 format = 0, aspect = 0; };
struct BufferView : Obj { U64 buffer = 0; U32 format = 0; U64 offset = 0, range = 0; };
struct Sampler : Obj {
    U32 mag = 0, min = 0, mipmap = 0, addrU = 0, addrV = 0, addrW = 0;
    U32 anisotropyEnable = 0, compareEnable = 0, compareOp = 7, borderColor = 0;
    float mipLodBias = 0.0f, maxAniso = 1.0f, minLod = 0.0f, maxLod = 32.0f;
};
struct ShaderModule : Obj {
    U32 stageHint = 0;
    U64 hash = 0;
    std::vector<U8> code;           // retained: vkcube destroys the module right
                                   // after pipeline creation (legal), so destroy
                                   // is a refcount drop (P1 section 1.2).
    int destroyed = 0;
};
struct DescSetLayout : Obj { U32 nbind = 0; Binding binds[VK64_MAX_BINDINGS]; };
struct DescSet : Obj { U64 layout = 0; U32 nbind = 0; Binding binds[VK64_MAX_BINDINGS]; };
struct TmplEntry { U32 binding = 0, arrayElement = 0, count = 0, type = 0; U64 offset = 0, stride = 0; };
struct DescTemplate : Obj { U32 nentry = 0; TmplEntry entries[VK64_MAX_BINDINGS]; };
struct PipelineLayout : Obj { U32 nsets = 0; U64 sets[VK64_MAX_SETS]; };
// The vertex input layout, recorded at pipeline creation (audit P2-NOW item 5).
// This is the one dropped-at-creation field that is not merely informative: with
// no binding/attribute descriptions there is no way to build a vertex buffer
// layout, so NOTHING rasterizes. DXVK needs it exactly as much as vkcube did.
// VkSpecializationInfo capture (2026-10-06): the guest may specialize
// shader constants via VkPipelineShaderStageCreateInfo.pSpecializationInfo.
// The bridge captures (constantID -> value bytes) per stage so the page tier
// can apply them instead of the SPIR-V defaults (spirvfix R2 folds to defaults).
struct SpecEntry { U32 constantID = 0; U32 size = 0; U8 value[8] = {0}; };
struct StageSpec { U32 n = 0; SpecEntry entries[16]; };
// Guest-side VkSpecializationInfo (pointers are U64 guest VAs).
struct Vk64SpecMapEntry { U32 constantID; U32 offset; U64 size; };
struct Vk64SpecInfo { U32 mapEntryCount; U32 _pad; U64 pMapEntries; U64 dataSize; U64 pData; };
static void captureStageSpec(U64 specVa, StageSpec& out) {
    if (!specVa) return;
    Vk64SpecInfo si = {};
    VK64_DROP_IF(return, !readStruct(specVa, si));
    VK64_DROP_IF(return, !si.mapEntryCount || si.mapEntryCount > 16);
    VK64_DROP_IF(return, !si.pMapEntries || !si.pData || !si.dataSize || si.dataSize > 256);
    std::vector<Vk64SpecMapEntry> mes;
    readArray(si.pMapEntries, si.mapEntryCount, mes);
    std::vector<U8> data((size_t)si.dataSize);
    g_mem->memcpyFromGuest(data.data(), si.pData, (size_t)si.dataSize);
    for (auto& me : mes) {
        VK64_DROP_IF(break, out.n >= 16);
        VK64_DROP_IF(continue, me.offset + me.size > si.dataSize || me.size > 8 || me.size == 0);
        // size must be 1, 2, 4, or 8 for scalar spec constants
        VK64_DROP_IF(continue, me.size != 1 && me.size != 2 && me.size != 4 && me.size != 8);
        SpecEntry& e = out.entries[out.n++];
        e.constantID = me.constantID;
        e.size = (U32)me.size;
        memcpy(e.value, data.data() + me.offset, (size_t)me.size);
    }
}
struct ColorBlend {
    U32 enable = 0, srcColor = 1, dstColor = 0, colorOp = 0;
    U32 srcAlpha = 1, dstAlpha = 0, alphaOp = 0, writeMask = 15;
};
struct StencilFace {
    U32 failOp = 0, passOp = 0, depthFailOp = 0, compareOp = 7;
    U32 compareMask = 255, writeMask = 255, reference = 0;
};
struct StencilState {
    U32 enabled = 0, dynamicMask = 0;
    StencilFace front, back;
};
struct Pipeline : Obj {
    U32 topology = 0, cull = 0, front = 0, depthTest = 0, depthWrite = 0, depthOp = 0, blend = 0;
    U32 primitiveRestartEnable = 0;
    U32 depthBiasEnable = 0, depthClampEnable = 0;
    bool depthBiasDynamic = false;
    bool cullDynamic = false, frontDynamic = false;   // VK_DYNAMIC_STATE_CULL_MODE / FRONT_FACE (DXVK declares both on every non-discard pipeline)
    float depthBiasConstantFactor = 0.0f, depthBiasClamp = 0.0f, depthBiasSlopeFactor = 0.0f;
    U32 colorBlendCount = 0; ColorBlend colorBlend[VK64_MAX_ATTACH];
    float blendConstants[4] = {};
    bool blendConstantsDynamic = false;
    StencilState stencil;
    StageSpec vsSpec, fsSpec;
    U32 nvb = 0, nva = 0;
    bool dynamicVertexStride = false;
    struct VertexStrideVariant { std::vector<U32> strides; U64 id; };
    // The page caches pipelines by ID across frames. Dynamic layouts require
    // distinct IDs; keep the exact keys with the originating pipeline lifetime.
    std::vector<VertexStrideVariant> vertexStrideVariants;
    U32 vbStride[VK64_MAX_VERT_BINDINGS] = {0};
    U32 vbInputRate[VK64_MAX_VERT_BINDINGS] = {0};    /* 0 = per vertex, 1 = per instance */
    U32 vaBinding[VK64_MAX_VERT_ATTRIBS] = {0};       /* VkVertexInputAttributeDescription */
    U32 vaFormat[VK64_MAX_VERT_ATTRIBS] = {0};
    U32 vaOffset[VK64_MAX_VERT_ATTRIBS] = {0};
    U32 vaLocation[VK64_MAX_VERT_ATTRIBS] = {0};
    U64 vs = 0, fs = 0, cs = 0, layout = 0, rp = 0;
};
template <class T>
static void captureColorBlend(Pipeline& pipe, const std::vector<T>& states) {
    pipe.colorBlendCount = std::min<U32>((U32)states.size(), VK64_MAX_ATTACH);
    for (U32 i = 0; i < pipe.colorBlendCount; ++i) {
        const auto& s = states[i];
        pipe.colorBlend[i] = {s.blendEnable, s.srcColorBlendFactor, s.dstColorBlendFactor,
                             s.colorBlendOp, s.srcAlphaBlendFactor, s.dstAlphaBlendFactor,
                             s.alphaBlendOp, s.colorWriteMask};
    }
    pipe.blend = pipe.colorBlendCount && pipe.colorBlend[0].enable ? 1 : 0;
}
template <class T>
static void captureStencil(Pipeline& pipe, const T& state) {
    pipe.stencil.enabled = state.stencilTestEnable;
    auto face = [](const auto& f) -> StencilFace {
        return {f.failOp, f.passOp, f.depthFailOp, f.compareOp,
                f.compareMask, f.writeMask, f.reference};
    };
    pipe.stencil.front = face(state.front);
    pipe.stencil.back = face(state.back);
}

// Everything vkCreateGraphicsPipelines does not model is reported here, once
// per feature (rate-limited, greppable "vk64: UNSUPPORTED <feature>"). Stages
// with no V2 representation cannot be approximated, so they fail the pipeline
// with VK_ERROR_FEATURE_NOT_PRESENT; state the page tier cannot honour only
// degrades rendering, so those are reported and the pipeline is still built.
struct PipelineAudit {
    U32 stageMask = 0;            // OR of VkShaderStageFlagBits
    U32 topology = 0;
    U32 polygonMode = 0;
    U32 viewportCount = 0;
    U32 sampleCount = 1;
    bool logicOp = false, depthBounds = false, sampleShading = false, alphaToOne = false;
    bool rasterizerDiscard = false;
    bool vertexDivisor = false;   // VkVertexInputBindingDivisorDescriptionEXT.divisor != 1
};
static const U32 kV2SupportedStageMask = 0x1u | 0x10u;     // VERTEX | FRAGMENT
// Returns true when the pipeline must be rejected with VK_ERROR_FEATURE_NOT_PRESENT.
static bool vk64AuditGraphicsPipeline(const PipelineAudit& a) {
    bool reject = false;
    const U32 extra = a.stageMask & ~kV2SupportedStageMask;
    if (extra & 0x2u)  { vk64Unsupported("tessellation-control-stage", "pipeline rejected"); reject = true; }
    if (extra & 0x4u)  { vk64Unsupported("tessellation-evaluation-stage", "pipeline rejected"); reject = true; }
    if (extra & 0x8u)  { vk64Unsupported("geometry-stage", "pipeline rejected"); reject = true; }
    if (extra & 0x40u) { vk64Unsupported("task-stage", "pipeline rejected"); reject = true; }
    if (extra & 0x80u) { vk64Unsupported("mesh-stage", "pipeline rejected"); reject = true; }
    if (extra & ~(0x2u | 0x4u | 0x8u | 0x40u | 0x80u)) { vk64Unsupported("unknown-graphics-stage", "pipeline rejected"); reject = true; }
    if (a.topology == 10) { vk64Unsupported("patch-list-topology", "pipeline rejected"); reject = true; }
    else if (a.topology >= 6) { vk64Unsupported("adjacency-topology", "pipeline rejected"); reject = true; }
    if (a.polygonMode != 0) vk64Unsupported(a.polygonMode == 1 ? "polygon-mode-line" : "polygon-mode-point", "rendered filled");
    if (a.logicOp) vk64Unsupported("logic-op", "ignored; blending state used instead");
    if (a.depthBounds) vk64Unsupported("depth-bounds-test", "ignored");
    if (a.sampleShading) vk64Unsupported("sample-shading", "ignored; per-fragment shading");
    if (a.alphaToOne) vk64Unsupported("alpha-to-one", "ignored");
    if (a.viewportCount > 1) vk64Unsupported("multi-viewport", "only viewport 0 is used");
    if (a.vertexDivisor) vk64Unsupported("vertex-attribute-divisor", "divisor != 1 rendered as per-instance step");
    if (a.sampleCount != 1 && a.sampleCount != 4) vk64Unsupported("sample-count", "WebGPU supports 1x and 4x only");
    return reject;
}
struct RenderPass : Obj {
    U32 natt = 0, formats[VK64_MAX_ATTACH] = {0}, loadOps[VK64_MAX_ATTACH] = {0},
        storeOps[VK64_MAX_ATTACH] = {0}, depthFmt = 0;
    U32 stencilLoadOps[VK64_MAX_ATTACH] = {0}, stencilStoreOps[VK64_MAX_ATTACH] = {0};
    U32 samples[VK64_MAX_ATTACH] = {0};   // v2: per-attachment MSAA sample count
};
struct Framebuffer : Obj { U64 rp = 0; U32 natt = 0; U64 views[VK64_MAX_ATTACH] = {0}; U32 w = 0, h = 0; };
struct Swapchain : Obj { U32 w = 0, h = 0, format = 0, nimg = 0, cur = 0; U64 imgs[8] = {0}; };

// --- command recording (P1's P1_Cmd / P1_CbState, with the copy record moved
// --- out of P1's process-global array into the command that recorded it, so a
// --- submit only executes the copies belonging to the command buffers it was
// --- given).
enum CmdKind : U32 {
    CMD_BEGIN_RP, CMD_END_RP, CMD_BIND_PIPE, CMD_BIND_SETS, CMD_VIEWPORT,
    CMD_SCISSOR, CMD_DRAW, CMD_DRAW_INDEXED, CMD_BARRIER, CMD_COPY_B2I, CMD_PUSH_CONST,
    CMD_BIND_VB, CMD_BIND_IB, CMD_UPDATE_BUF, CMD_COPY_I2B, CMD_DISPATCH, CMD_COPY_IMAGE,
    CMD_STENCIL_STATE, CMD_BLEND_CONSTANTS, CMD_CLEAR_ATTACHMENTS, CMD_COPY_QUERY_RESULTS, CMD_DEPTH_BIAS, CMD_BLIT_IMAGE,
    CMD_RASTER_STATE, CMD_CLEAR_DS_IMAGE, CMD_RESET_QUERY, CMD_WRITE_TIMESTAMP
};
struct Copy {
    U64 srcBuffer = 0, dstImage = 0, bufOff = 0;
    U32 rowLen = 0, imgH = 0; int32_t ox = 0, oy = 0; U32 w = 0, h = 0;
    U32 mip = 0, baseLayer = 0, layerCount = 1, depth = 1; int32_t oz = 0;
};
struct CopyI2B {
    U64 srcImage = 0, dstBuffer = 0, bufOff = 0;
    U32 rowLen = 0, imgH = 0; int32_t ox = 0, oy = 0; U32 w = 0, h = 0;
};
struct ImageCopy {
    U64 src = 0, dst = 0;
    U32 srcMip = 0, srcLayer = 0, dstMip = 0, dstLayer = 0;
    U32 srcOrigin[3] = {}, dstOrigin[3] = {}, extent[3] = {};
};
struct ImageBlit {
    U64 src = 0, dst = 0;
    U32 srcMip = 0, srcLayer = 0, dstMip = 0, dstLayer = 0, filter = 0;
    int32_t srcOffsets[2][3] = {}, dstOffsets[2][3] = {};
};
struct QueryCopy {
    U64 queryPool = 0, dstBuffer = 0, dstOffset = 0, stride = 0;
    U32 firstQuery = 0, queryCount = 0, flags = 0;
};
struct ClearAttachmentArg { U32 aspectMask = 0, colorAttachment = 0, value[4] = {}; };
struct ClearRectArg { int32_t x = 0, y = 0; U32 w = 0, h = 0, baseLayer = 0, layers = 1; };
static_assert(sizeof(ClearAttachmentArg) == 24 && sizeof(ClearRectArg) == 24, "clear ABI layout");
struct ClearRangeArg { U32 aspectMask = 0, baseMip = 0, levelCount = 0, baseLayer = 0, layerCount = 0; };  // VkImageSubresourceRange
static_assert(sizeof(ClearRangeArg) == 20, "VkImageSubresourceRange layout");
struct Cmd {
    U32 kind = 0;
    U64 a = 0, b = 0, c = 0, d = 0;
    float f[8] = {0};
    double clearDepth = 1.0;
    U32 clearStencil = 0;
    std::vector<ClearAttachmentArg> clearAttachments;
    std::vector<ClearRectArg> clearRects;
    std::vector<ClearRangeArg> clearRanges;   // CMD_CLEAR_DS_IMAGE: a = image, f[0] = depth, clearStencil
    U32 vbCount = 0;
    U64 vbBuffers[VK64_MAX_VERT_BINDINGS] = {0}, vbOffsets[VK64_MAX_VERT_BINDINGS] = {0};
    U64 sets[VK64_MAX_SETS] = {0};
    U32 vbStrides[VK64_MAX_VERT_BINDINGS] = {0};
    bool vbHasStride[VK64_MAX_VERT_BINDINGS] = {false};
    Copy  copy;
    CopyI2B copyI2B;
    ImageCopy imageCopy;
    ImageBlit imageBlit;
    QueryCopy queryCopy;
    // Push constants: the recorded bytes, byte-offset preserved. DXVK pushes
    // constants per-draw for essentially everything, so this is a hot path, not a
    // corner case.
    U32 pcOffset = 0, pcSize = 0, pcStageFlags = 0;
    U8  pcBytes[VK64_MAX_PUSH_BYTES] = {0};
    // vkCmdUpdateBuffer: transfer-write into a buffer. Payload copied out
    // of guest memory at record time; executed into guest memory at submit
    // (transfer semantics), before the frame is serialized, so
    // readGuestRange sees the updated bytes. DXVK uploads D3D9
    // fixed-function VS constants with this.
    U64 updBuf = 0, updOff = 0;
    std::vector<U8> updBytes;
    // Indexed draw + G2 replay capture: vertexOffset for the draw, and the
    // resolved NDC positions / linear RGBA colors (capCount vertices) baked
    // for the procedural replay shader when the layout is understood.
    int32_t diVertexOffset = 0;
    U32 capCount = 0;
    std::vector<float> capPos, capCol;
};
template <class T>
static void captureRenderPassClears(Cmd& command, const RenderPass* pass, const std::vector<T>& values) {
    VK64_DROP_IF(return, !pass);
    for (U32 i = 0; i < std::min<U32>(pass->natt, (U32)values.size()); ++i) {
        if (pass->formats[i] >= 124 && pass->formats[i] <= 130) {
            command.clearDepth = values[i].depthStencil.depth;
            command.clearStencil = values[i].depthStencil.stencil;
        } else if (i == 0) memcpy(command.f, values[i].color.float32, 16);
    }
}
struct CmdBuf : Obj {
    U64 pool = 0;
    std::vector<Cmd> cmds;
    U64  pipe = 0;
    U64  sets[VK64_MAX_SETS] = {0};
    float vp[6] = {0}, sci[4] = {0};
    bool haveVp = false, haveSci = false;
    // v2: vertex/index buffer tables (DXVK draws indexed almost exclusively).
    U64  vbs[VK64_MAX_VERT_BINDINGS] = {0};
    U64  vbOff[VK64_MAX_VERT_BINDINGS] = {0};
    U32 vbStride[VK64_MAX_VERT_BINDINGS] = {0};
    bool vbHasStride[VK64_MAX_VERT_BINDINGS] = {false};
    std::vector<U64> dynamicObjects; // Synthetic attachments live with the recording.
    ~CmdBuf() override { for (U64 id : dynamicObjects) dropObj(id); }
    U32  nvb = 0;
    U64  ib = 0, ibOff = 0;
    U32  ibType = 0;   // 0 = uint16, 1 = uint32
};
CmdBuf* cbOf(U64 id, const char* who) { return objOf<CmdBuf>(id, K_CMDBUF, who); }
void resetCmdBuf(CmdBuf* cb) {
    for (U64 id : cb->dynamicObjects) dropObj(id);
    cb->dynamicObjects.clear();
    cb->cmds.clear();
    cb->pipe = 0;
    cb->haveVp = cb->haveSci = false;
    memset(cb->sets, 0, sizeof(cb->sets));
    memset(cb->vp, 0, sizeof(cb->vp));
    memset(cb->sci, 0, sizeof(cb->sci));
    memset(cb->vbs, 0, sizeof(cb->vbs));
    memset(cb->vbOff, 0, sizeof(cb->vbOff));
    memset(cb->vbStride, 0, sizeof(cb->vbStride));
    memset(cb->vbHasStride, 0, sizeof(cb->vbHasStride));
    cb->nvb = 0;
    cb->ib = 0; cb->ibOff = 0; cb->ibType = 0;
}

// The frame serializer is defined above the switch; forward declaration so
// pushCmd can flag overflow.
bool g_frameOverflow = false;
void pushCmd(CmdBuf* st, const Cmd& c) {
    VK64_DROP_IF(return, !st);
    if (st->cmds.size() >= VK64_MAX_CMDS) { g_frameOverflow = true; return; }
    st->cmds.push_back(c);
}

// GPU render targets have no current pixels in guest backing memory. Preserve
// image transfers in command order so the page copies the rendered texture.
void recordImageCopy(CmdBuf* cb, U64 src, U64 dst, U32 srcMip, U32 srcLayer, U32 dstMip, U32 dstLayer,
                     int32_t sx, int32_t sy, int32_t sz, int32_t dx, int32_t dy, int32_t dz,
                     U32 w, U32 h, U32 d, U32 layers) {
    VK64_DROP_IF(return, !cb || !w || !h || !d || sx < 0 || sy < 0 || sz < 0 || dx < 0 || dy < 0 || dz < 0);
    for (U32 layer = 0; layer < layers && layer < VK64_MAX_CMDS; ++layer) {
        Cmd c; c.kind = CMD_COPY_IMAGE;
        c.imageCopy = { src, dst, srcMip, srcLayer + layer, dstMip, dstLayer + layer,
                       { (U32)sx, (U32)sy, (U32)sz }, { (U32)dx, (U32)dy, (U32)dz }, { w, h, d } };
        pushCmd(cb, c);
    }
}
void recordImageBlit(CmdBuf* cb, U64 src, U64 dst, U32 srcMip, U32 srcLayer, U32 dstMip, U32 dstLayer,
                     U32 filter, const int32_t srcOffsets[2][3], const int32_t dstOffsets[2][3], U32 layers) {
    VK64_DROP_IF(return, !cb || !srcOffsets || !dstOffsets);
    for (U32 layer = 0; layer < layers && layer < VK64_MAX_CMDS; ++layer) {
        Cmd c; c.kind = CMD_BLIT_IMAGE;
        c.imageBlit.src = src; c.imageBlit.dst = dst;
        c.imageBlit.srcMip = srcMip; c.imageBlit.srcLayer = srcLayer + layer;
        c.imageBlit.dstMip = dstMip; c.imageBlit.dstLayer = dstLayer + layer;
        c.imageBlit.filter = filter;
        memcpy(c.imageBlit.srcOffsets, srcOffsets, sizeof(c.imageBlit.srcOffsets));
        memcpy(c.imageBlit.dstOffsets, dstOffsets, sizeof(c.imageBlit.dstOffsets));
        pushCmd(cb, c);
    }
}

// ---------------------------------------------------------------------------
// Format helpers (P1's fmt_bpp / fmt_name, unchanged)
// ---------------------------------------------------------------------------
U32 fmt_bpp(U32 f) {
    switch (f) {
        case VK_FORMAT_R8_UNORM: case VK_FORMAT_R8_SNORM: case VK_FORMAT_R8_UINT:
        case VK_FORMAT_R8_SINT: case VK_FORMAT_S8_UINT: return 1;
        case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R16_UNORM: case VK_FORMAT_R16_SFLOAT:
        case VK_FORMAT_R5G6B5_UNORM_PACK16: case VK_FORMAT_B5G5R5A1_UNORM_PACK16:
        case VK_FORMAT_R4G4B4A4_UNORM_PACK16: case VK_FORMAT_D16_UNORM: return 2;
        case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_R8G8B8A8_SRGB:
        case VK_FORMAT_B8G8R8A8_UNORM: case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT:
        case VK_FORMAT_D24_UNORM_S8_UINT: case VK_FORMAT_D32_SFLOAT:
        case VK_FORMAT_B10G11R11_UFLOAT_PACK32: return 4;
        case VK_FORMAT_R16G16_SFLOAT: return 4;
        case VK_FORMAT_R16G16B16A16_UNORM: case VK_FORMAT_R16G16B16A16_SFLOAT: return 8;
        case VK_FORMAT_R32G32_SFLOAT: return 8;
        case VK_FORMAT_R32G32B32A32_SFLOAT: return 16;
        default: return 4;
    }
}
struct FormatBlock { U32 w, h, bytes; };
FormatBlock fmt_block(U32 f) {
    if (f >= 131 && f <= 146)
        return {4, 4, (f <= 134 || f == 139 || f == 140) ? 8U : 16U};
    return {1, 1, fmt_bpp(f)};
}
U64 imageMipBytes(const Image& im, U32 mip) {
    const auto b = fmt_block(im.format);
    const U32 w = std::max(1U, im.w >> mip), h = std::max(1U, im.h >> mip);
    return (U64)((w + b.w - 1) / b.w) * ((h + b.h - 1) / b.h)
        * std::max(1U, im.d >> mip) * b.bytes;
}
U64 imageLayerBytes(const Image& im) {
    U64 bytes = 0;
    for (U32 m = 0; m < std::max(1U, im.mips); ++m) bytes += imageMipBytes(im, m);
    return bytes;
}
U64 imageDataBytes(const Image& im) { return imageLayerBytes(im) * std::max(1U, im.layers); }
U64 imageSubresourceOffset(const Image& im, U32 mip, U32 layer) {
    U64 off = (U64)layer * imageLayerBytes(im);
    for (U32 m = 0; m < mip; ++m) off += imageMipBytes(im, m);
    return off;
}
const char* fmt_name(U32 f) {
    switch (f) {
        case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
        case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
        case VK_FORMAT_D16_UNORM: return "D16_UNORM";
        case VK_FORMAT_D24_UNORM_S8_UINT: return "D24S8";
        default: return "UNKNOWN";
    }
}
U64 blob_hash(const U8* p, size_t n) {
    U64 h = 5381;
    for (size_t i = 0; i < n; i++) h = h * 33 + p[i];
    return h;
}

// ---------------------------------------------------------------------------
// Frame manifest. P1's schema (tasks/p1-final.md section 1.3) with the sidecar
// payloads inlined base64: there is no filesystem between wasm and the page.
// ---------------------------------------------------------------------------
U32 g_frameNo = 0;
static U64 g_v2SubmitFrames = 0; // frames with draws serialized to v2 (dedup epoch)
std::string g_pendingFrame;          // built by submit, hopped by present
U32 g_lastPresented = 0;

std::string b64(const U8* data, size_t n) {
    static const char* T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((n + 2) / 3) * 4);
    size_t i = 0;
    for (; i + 2 < n; i += 3) {
        U32 v = ((U32)data[i] << 16) | ((U32)data[i+1] << 8) | data[i+2];
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63];
        out += T[(v >> 6) & 63];  out += T[v & 63];
    }
    if (i < n) {
        U32 v = (U32)data[i] << 16;
        bool two = (i + 1 < n);
        if (two) v |= (U32)data[i+1] << 8;
        out += T[(v >> 18) & 63]; out += T[(v >> 12) & 63];
        out += two ? T[(v >> 6) & 63] : '=';
        out += '=';
    }
    return out;
}
void jsonBlob(std::string& out, const char* key, const std::vector<U8>& bytes) {
    char num[32];
    snprintf(num, sizeof num, "%llu", (unsigned long long)bytes.size());
    out += '"'; out += key; out += "\":{\"size\":";
    out += num; out += ",\"b64\":\"";
    out += b64(bytes.data(), bytes.size());
    out += "\"}";
}
// P1 wrote a sidecar unconditionally (empty file when the source was missing);
// here an absent source is a null so the consumer can tell "no UBO" from
// "zero-length UBO".
void jsonBlobOrNull(std::string& out, const char* key, const std::vector<U8>& bytes, bool present) {
    if (!present) { out += '"'; out += key; out += "\":null"; return; }
    jsonBlob(out, key, bytes);
}
std::string jsonFloats(const float* v, int n) {
    char buf[64];
    std::string s = "[";
    for (int i = 0; i < n; i++) {
        if (i) s += ",";
        snprintf(buf, sizeof buf, "%g", (double)v[i]);
        s += buf;
    }
    return s + "]";
}

void hopFrameToPage(const std::string& json) {
#ifdef __EMSCRIPTEN__
    // One hop per frame. The page side (web/runtime.html window.bwVkFrame) is
    // not wired yet in this lane, so the JS body is defensive: a missing sink
    // must not fault the guest.
    MAIN_THREAD_EM_ASM({
        if (globalThis.window && window.bwVkFrame) {
            try { window.bwVkFrame(UTF8ToString($0 >>> 0)); } catch (e) { console.warn('vk64: bwVkFrame failed', e); }
        }
    }, json.c_str());
#else
    (void)json;
#endif
}

void emitFrameNow() {
    if (g_pendingFrame.empty()) return;
    std::string json = g_pendingFrame;
    g_pendingFrame.clear();
    g_lastPresented = g_frameNo ? g_frameNo - 1 : 0;
    hopFrameToPage(json);
    // Log the manifest. It is the frame payload a WebGPU consumer needs, so it is
    // worth having in the host log as evidence — and a SMALL frame is cheap
    // enough that it is always logged, with no env var and no frontend wiring.
    // Above the cap the base64 payloads are stripped (the scalars stay) because a
    // vkcube-sized texture would otherwise be ~360KB per frame; BW64_VKFRAME=1
    // logs big frames elided and =2 logs them whole.
    int want = vkDumpFrames();
    if ((int)json.size() <= (int)kManifestLogCap) want = 2;
    if (want) {
        if (want == 2 || json.size() <= kManifestLogCap) {
            klog_fmt("vk64: FRAME-JSON %s", json.c_str());
        } else {
            // Same schema, payload elided: enough to prove the frame crossed and
            // to read every scalar, without the megabytes.
            std::string elided = json;
            const char* keys[] = { "\"pixels\":{", "\"spv\":{" };
            for (const char* k : keys) {
                size_t at = 0;
                while ((at = elided.find(k, at)) != std::string::npos) {
                    size_t b64 = elided.find("\"b64\":\"", at);
                    if (b64 == std::string::npos) break;
                    size_t end = elided.find('"', b64 + 7);
                    if (end == std::string::npos) break;
                    elided.replace(b64 + 7, end - (b64 + 7), "<elided>");
                    at = end;
                }
            }
            klog_fmt("vk64: FRAME-JSON (payload elided, %zu bytes -> %zu; set BW64_VKFRAME=2 for the full frame) %s",
                     json.size(), elided.size(), elided.c_str());
        }
    }
    g_frameNo++;
}

// ---------------------------------------------------------------------------
// Eager copyBufferToImage execution (P1's exec_copies, per-command-buffer now).
// Source and destination are BOTH guest ranges, so this is one
// memcpyFromGuest/memcpyToGuest per row.
// ---------------------------------------------------------------------------
static U64 queryHostNanoseconds() {
    return (U64)std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void writeEncodedQueryResults(U64 dst, const vkquery::EncodedResults& encoded) {
    for (U32 i = 0; i < encoded.countWritten; ++i) {
        const U64 offset = (U64)i * encoded.strideBytes;
        writeGuestBytes(dst + offset, encoded.bytes.data() + offset, encoded.resultBytes);
    }
}

static VkResult writeQueryResults(QueryPool* pool, U32 first, U32 count,
                                  U64 dataSize, U64 dst, U64 stride, U32 flags) {
    if (!pool || !dst || (!g_mem && !g_mem32)) return VK_ERROR_INITIALIZATION_FAILED;
    if (first <= pool->count && count <= pool->count - first && !(flags & vkquery::VKQUERY_PARTIAL)) {
        for (U32 i = 0; i < count; ++i)
            if (first + i >= pool->available.size() || !pool->available[first + i]) return VK_NOT_READY;
    }
    constexpr U64 kMaxQueryResultBytes = 1ULL << 20;
    const U64 writeSize = std::min<U64>(dataSize, kMaxQueryResultBytes);
    vkquery::EncodedResults encoded = vkquery::encodeResults(
        pool->type, first, count, pool->count, (size_t)writeSize, stride, flags,
        1.0, queryHostNanoseconds(), pool->results, pool->available);
    if (!encoded.valid) return VK_ERROR_INITIALIZATION_FAILED;
    writeEncodedQueryResults(dst, encoded);
    return encoded.complete && writeSize == dataSize ? VK_SUCCESS : VK_INCOMPLETE;
}

static void execCopyQueryResults(const Cmd& command) {
    const QueryCopy& copy = command.queryCopy;
    QueryPool* pool = nullptr;
    Buffer* buffer = nullptr;
    DeviceMemory* memory = nullptr;
    if (g_mem32) {
        pool = objOf<QueryPool>(copy.queryPool & 0xFFFFFFFFULL, K_QUERYPOOL, "vkCmdCopyQueryPoolResults");
        buffer = objOf<Buffer>(copy.dstBuffer & 0xFFFFFFFFULL, K_BUFFER, "vkCmdCopyQueryPoolResults");
        if (buffer) memory = objOf<DeviceMemory>(buffer->mem & 0xFFFFFFFFULL, K_MEMORY, "vkCmdCopyQueryPoolResults");
    } else {
        pool = objOf<QueryPool>(copy.queryPool, K_QUERYPOOL, "vkCmdCopyQueryPoolResults");
        buffer = objOf<Buffer>(copy.dstBuffer, K_BUFFER, "vkCmdCopyQueryPoolResults");
        if (buffer) memory = objOf<DeviceMemory>(buffer->mem, K_MEMORY, "vkCmdCopyQueryPoolResults");
    }
    VK64_DROP_IF(return, !pool || !buffer || !memory || !memory->va || copy.dstOffset > buffer->size);
    const U64 memoryOffset = buffer->memOff + copy.dstOffset;
    VK64_DROP_IF(return, memoryOffset > memory->size);
    const U64 available = std::min<U64>(buffer->size - copy.dstOffset, memory->size - memoryOffset);
    const U64 size = std::min<U64>(available, 1ULL << 20);
    vkquery::EncodedResults encoded = vkquery::encodeResults(
        pool->type, copy.firstQuery, copy.queryCount, pool->count, (size_t)size,
        copy.stride, copy.flags, 1.0, queryHostNanoseconds(), pool->results, pool->available);
    VK64_DROP_IF(return, !encoded.valid);
    writeEncodedQueryResults(memory->va + memoryOffset, encoded);
    buffer->dirtyGen = g_bufDirtyTick++;
}

static void execQueryCommand(const Cmd& command) {
    if (command.kind == CMD_RESET_QUERY) {
        QueryPool* pool = objOf<QueryPool>(command.a, K_QUERYPOOL, "vkCmdResetQueryPool");
        VK64_DROP_IF(return, !pool || command.b > pool->count);
        const U32 count = std::min<U64>(command.c, pool->count - command.b);
        for (U32 i = 0; i < count; ++i) { pool->results[command.b + i] = 0; pool->available[command.b + i] = 0; }
    } else if (command.kind == CMD_WRITE_TIMESTAMP) {
        QueryPool* pool = objOf<QueryPool>(command.a, K_QUERYPOOL, "vkCmdWriteTimestamp");
        VK64_DROP_IF(return, !pool || pool->type != vkquery::VKQUERY_TIMESTAMP || command.b >= pool->count);
        pool->results[command.b] = vkquery::timestampTicks(queryHostNanoseconds(), 1.0);
        pool->available[command.b] = 1;
    }
}

// vkCmdUpdateBuffer execution: the transfer write lands in guest memory at
// submit time, exactly like a real implementation would make it visible.
// Bounds are clamped defensively; a short write still updates its prefix.
void execUpdateBuf(const Cmd& k) {
    Buffer* b = objOf<Buffer>(k.updBuf, K_BUFFER, "vkCmdUpdateBuffer");
    if (!b || !b->mem || k.updBytes.empty()) return;
    DeviceMemory* m = objOf<DeviceMemory>(b->mem, K_MEMORY, "vkCmdUpdateBuffer");
    VK64_DROP_IF(return, !m || !m->va);
    U64 off = b->memOff + k.updOff;
    U64 n = (U64)k.updBytes.size();
    VK64_DROP_IF(return, off >= m->size);
    if (off + n > m->size) n = m->size - off;
    writeGuestBytes(m->va + off, k.updBytes.data(), n);
    b->dirtyGen = g_bufDirtyTick++;
}
// Build-9 diagnostic: every early return of execCopy used to be silent, so a
// texture upload that never landed (all-zero sampled textures on the page) left
// no trace. Log the first few failures per reason and a running tally.
static void execCopyFail(const char* why, const Copy& r) {
    static std::unordered_map<std::string, U32> seen;
    static U32 total = 0;
    ++total;
    U32& n = seen[why];
    if (++n <= 4 || (n & (n - 1)) == 0)
        klog_fmt("vk64: execCopy B2I skipped (%s) x%u [total %u]: buf=0x%llx img=0x%llx mip=%u off=%d,%d,%d ext=%ux%ux%u rowLen=%u",
                 why, n, total, (unsigned long long)r.srcBuffer, (unsigned long long)r.dstImage, r.mip,
                 r.ox, r.oy, r.oz, r.w, r.h, r.depth, r.rowLen);
}
void execCopy(const Copy& r) {
    Buffer* src = objOf<Buffer>(r.srcBuffer, K_BUFFER, "copyBufferToImage");
    Image*  dst = objOf<Image>(r.dstImage, K_IMAGE, "copyBufferToImage");
    if (!src || !dst) { execCopyFail(!src ? "stale source buffer" : "stale destination image", r); return; }
    DeviceMemory* smem = objOf<DeviceMemory>(src->mem, K_MEMORY, "copyBufferToImage");
    DeviceMemory* dmem = objOf<DeviceMemory>(dst->mem, K_MEMORY, "copyBufferToImage");
    if (!smem || !dmem || !smem->va || !dmem->va) { execCopyFail(!smem || !smem->va ? "source memory unmapped" : "destination memory unmapped", r); return; }
    if (r.mip >= std::max(1U, dst->mips) || r.ox < 0 || r.oy < 0 || r.oz < 0 || !r.w || !r.h) { execCopyFail("bad mip/offset/extent", r); return; }
    const auto block = fmt_block(dst->format);
    const U32 mipW = std::max(1U, dst->w >> r.mip), mipH = std::max(1U, dst->h >> r.mip);
    const U32 mipD = std::max(1U, dst->d >> r.mip);
    if ((U32)r.ox + r.w > mipW || (U32)r.oy + r.h > mipH || (U32)r.oz + r.depth > mipD) { execCopyFail("region outside mip", r); return; }
    const U64 rowBytes = (U64)((r.w + block.w - 1) / block.w) * block.bytes;
    const U64 srcRow = (U64)(((r.rowLen ? r.rowLen : r.w) + block.w - 1) / block.w) * block.bytes;
    const U32 rows = (r.h + block.h - 1) / block.h;
    const U64 srcSlice = srcRow * (((r.imgH ? r.imgH : r.h) + block.h - 1) / block.h);
    const U64 dstRow = (U64)((mipW + block.w - 1) / block.w) * block.bytes;
    const U64 dstSlice = dstRow * ((mipH + block.h - 1) / block.h);
    if (srcRow < rowBytes || r.baseLayer + r.layerCount > std::max(1U, dst->layers)) { execCopyFail("row/layer mismatch", r); return; }
    std::vector<U8> tmp((size_t)rowBytes);
    for (U32 layer = 0; layer < r.layerCount; ++layer) {
        for (U32 z = 0; z < r.depth; ++z) {
            const U64 srcBase = src->memOff + r.bufOff + ((U64)layer * r.depth + z) * srcSlice;
            const U64 dstBase = dst->memOff + imageSubresourceOffset(*dst, r.mip, r.baseLayer + layer)
                + ((U64)r.oz + z) * dstSlice + ((U64)r.oy / block.h) * dstRow + ((U64)r.ox / block.w) * block.bytes;
            for (U32 y = 0; y < rows; ++y) {
                const U64 so = srcBase + (U64)y * srcRow, to = dstBase + (U64)y * dstRow;
                if (so > smem->size || rowBytes > smem->size - so || to > dmem->size || rowBytes > dmem->size - to) { execCopyFail("copy out of memory bounds", r); return; }
                readGuestBytes(tmp.data(), smem->va + so, rowBytes);
                writeGuestBytes(dmem->va + to, tmp.data(), rowBytes);
            }
        }
    }
    dst->copied = 1;
    // Tally uploads whose source bytes were entirely zero (first row only, cheap).
    static U32 okCopies = 0, zeroSrc = 0;
    ++okCopies;
    {
        const U64 so0 = src->memOff + r.bufOff;
        if (so0 + rowBytes <= smem->size) {
            std::vector<U8> probe((size_t)rowBytes);
            readGuestBytes(probe.data(), smem->va + so0, rowBytes);
            bool nz = false;
            for (U8 v : probe) if (v) { nz = true; break; }
            if (!nz) ++zeroSrc;
        }
    }
    if (okCopies <= 8 || (okCopies & (okCopies - 1)) == 0)
        klog_fmt("vk64: execCopy B2I ok #%u (first-row all-zero sources so far: %u): img=0x%llx fmt=%u %ux%u mip=%u",
                 okCopies, zeroSrc, (unsigned long long)r.dstImage, (unsigned)dst->format, r.w, r.h, r.mip);
}

std::vector<U8> readGuestRange(U64 memId, U64 memOff, U64 bytes) {
    std::vector<U8> out;
    DeviceMemory* m = objOf<DeviceMemory>(memId, K_MEMORY, "readGuestRange");
    if (!m || !m->va || !bytes) return out;
    if (memOff >= m->size) return out;
    if (memOff + bytes > m->size) bytes = m->size - memOff;
    out.resize((size_t)bytes);
    readGuestBytes(out.data(), m->va + memOff, bytes);
    return out;
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// G2 replay capture + procedural shader synthesis (schema-v2 stopgap).
//
// The page tier (web/vkwebgpu.mjs) replays each draw as
// pass.draw(vertexCount, 1, firstVertex, 0) with NO vertex buffers bound, so a
// guest vertex shader with @location inputs can never produce pixels there:
// pipeline creation itself rejects the missing buffers. Until the manifest
// schema carries vertex/index BYTES (schema v2, a separate tracked goal), the
// bridge resolves the draw vertices itself and bakes them into a procedural
// SPIR-V vertex shader: gl_VertexIndex selects among constant position/color
// pairs captured from the REAL vertex/index buffers of DXVK at draw time. The
// geometry and colors that rasterize are the guest own; only the fetch
// mechanism is replayed. A draw the synthesizer does not understand keeps the
// guest modules (current behaviour; the page tier then skips the frame).
// ---------------------------------------------------------------------------
const U32 kCapMaxVerts = 256;   // baked-vertex cap per draw; tri9 needs 3

bool captureIndexedDraw(CmdBuf* cb, Cmd& c) {
    U32 indexCount = (U32)c.a, firstIndex = (U32)c.c;
    static int capLogCount = 0;
    if (!indexCount || indexCount > kCapMaxVerts) {
        if (capLogCount < 30) { capLogCount++; klog_fmt("CAPFAIL: indexCount=%u", indexCount); }
        return false;
    }
    Pipeline* pipe = objOf<Pipeline>(cb->pipe, K_PIPELINE, "capDraw");
    if (!pipe || !pipe->nva) return false;
    // Position = location-0 float4 (D3D9 tri9) or float3 (D3D12 scene12).
    // Color = location-1 unorm8x4 packed (D3D9) or float4 (D3D12 scene12).
    int posAi = -1, colAi = -1;
    bool posIsVec3 = false, colIsFloat4 = false;
    for (U32 i = 0; i < pipe->nva; i++) {
        U32 f = pipe->vaFormat[i];
        if (pipe->vaLocation[i] == 0) {
            if (f == VK_FORMAT_R32G32B32A32_SFLOAT) { posAi = (int)i; posIsVec3 = false; }
            else if (f == VK_FORMAT_R32G32B32_SFLOAT) { posAi = (int)i; posIsVec3 = true; }
        }
        if (pipe->vaLocation[i] == 1) {
            if (f == VK_FORMAT_B8G8R8A8_UNORM || f == VK_FORMAT_R8G8B8A8_UNORM ||
                f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_R8G8B8A8_SRGB) {
                colAi = (int)i; colIsFloat4 = false;
            } else if (f == VK_FORMAT_R32G32B32A32_SFLOAT) {
                colAi = (int)i; colIsFloat4 = true;
            }
        }
    }
    if (posAi < 0 || colAi < 0) {
        if (capLogCount < 30) { capLogCount++; klog_fmt("CAPFAIL: posAi=%d colAi=%d nva=%u indexCount=%u", posAi, colAi, pipe->nva, indexCount); }
        return false;
    }
    U32 posBind = pipe->vaBinding[posAi], colBind = pipe->vaBinding[colAi];
    if (posBind >= VK64_MAX_VERT_BINDINGS || colBind >= VK64_MAX_VERT_BINDINGS) return false;
    Buffer* pbuf = objOf<Buffer>(cb->vbs[posBind], K_BUFFER, "capDraw");
    Buffer* cbuf = objOf<Buffer>(cb->vbs[colBind], K_BUFFER, "capDraw");
    Buffer* ibuf = objOf<Buffer>(cb->ib, K_BUFFER, "capDraw");
    if (!pbuf || !cbuf || !ibuf || !pbuf->mem || !cbuf->mem || !ibuf->mem) {
        if (capLogCount < 30) { capLogCount++; klog_fmt("CAPFAIL: buffers pbuf=%p cbuf=%p ibuf=%p indexCount=%u", (void*)pbuf, (void*)cbuf, (void*)ibuf, indexCount); }
        return false;
    }
    U32 idxSize = (cb->ibType == 1) ? 4 : 2;
    std::vector<U8> idxBytes = readGuestRange(ibuf->mem, ibuf->memOff + cb->ibOff + (U64)firstIndex * idxSize,
                                              (U64)indexCount * idxSize);
    if (idxBytes.size() < (size_t)indexCount * idxSize) return false;
    // Resolve vertex ids (firstIndex + vertexOffset) so the baked arrays are in
    // draw order and the replay vertex_index maps 1:1.
    U32 maxV = 0;
    std::vector<U32> vids(indexCount);
    for (U32 i = 0; i < indexCount; i++) {
        U32 ix = (idxSize == 4)
            ? (U32)(idxBytes[i*4] | ((U32)idxBytes[i*4+1] << 8) |
                    ((U32)idxBytes[i*4+2] << 16) | ((U32)idxBytes[i*4+3] << 24))
            : (U32)(idxBytes[i*2] | ((U32)idxBytes[i*2+1] << 8));
        int64_t v = (int64_t)ix + c.diVertexOffset;
        if (v < 0 || v > 1000000) return false;
        vids[i] = (U32)v;
        if ((U32)v > maxV) maxV = (U32)v;
    }
    // Stride may come from pipeline (static) or bind time (dynamic, v2 API).
    // Prefer bind-time stride if set, fall back to pipeline stride.
    U32 pstride = cb->vbStride[posBind] ? cb->vbStride[posBind] : pipe->vbStride[posBind];
    U32 cstride = cb->vbStride[colBind] ? cb->vbStride[colBind] : pipe->vbStride[colBind];
    if (!pstride || !cstride) {
        if (capLogCount < 30) { capLogCount++; klog_fmt("CAPFAIL: stride pstride=%u cstride=%u indexCount=%u", pstride, cstride, indexCount); }
        return false;
    }
    U32 posSize = posIsVec3 ? 12 : 16;
    U32 colSize = colIsFloat4 ? 16 : 4;
    U64 pNeed = (U64)maxV * pstride + pipe->vaOffset[posAi] + posSize;
    U64 cNeed = (U64)maxV * cstride + pipe->vaOffset[colAi] + colSize;
    std::vector<U8> pbytes = readGuestRange(pbuf->mem, pbuf->memOff + cb->vbOff[posBind], pNeed);
    std::vector<U8> cbytes = readGuestRange(cbuf->mem, cbuf->memOff + cb->vbOff[colBind], cNeed);
    if (pbytes.size() < pNeed || cbytes.size() < cNeed) {
        if (capLogCount < 30) { capLogCount++; klog_fmt("CAPFAIL: bytes pbytes=%zu/%llu cbytes=%zu/%llu indexCount=%u", pbytes.size(), (unsigned long long)pNeed, cbytes.size(), (unsigned long long)cNeed, indexCount); }
        return false;
    }
    // RHW pixel -> NDC bake needs the viewport DXVK set for this draw.
    // D3D12 vec3 positions are already in clip/NDC space and bypass this.
    float vpw = 0, vph = 0;
    if (!posIsVec3) {
        if (!cb->haveVp || cb->vp[2] <= 0 || cb->vp[3] <= 0) {
            return false;
        }
        vpw = cb->vp[2]; vph = cb->vp[3];
    }
    bool bgr = (pipe->vaFormat[colAi] == VK_FORMAT_B8G8R8A8_UNORM ||
                pipe->vaFormat[colAi] == VK_FORMAT_B8G8R8A8_SRGB);
    c.capPos.reserve((size_t)indexCount * 4);
    c.capCol.reserve((size_t)indexCount * 4);
    for (U32 i = 0; i < indexCount; i++) {
        U32 v = vids[i];
        float pf[4];
        if (posIsVec3) {
            memcpy(pf, &pbytes[(size_t)v * pstride + pipe->vaOffset[posAi]], 12);
            pf[3] = 1.0f;
        } else {
            memcpy(pf, &pbytes[(size_t)v * pstride + pipe->vaOffset[posAi]], 16);
        }
        float rhw = pf[3] != 0.0f ? pf[3] : 1.0f;
        // Vulkan and WebGPU share the top-left framebuffer origin: NDC y
        // follows the pixel row, no flip (the page tier documented rule).
        // D3D12 scene12 positions are already in NDC-like space; D3D9 tri9
        // uses RHW pixel coords requiring viewport normalization.
        if (posIsVec3) {
            // D3D12: position already in clip/NDC space, pass through.
            c.capPos.push_back(pf[0]);
            c.capPos.push_back(pf[1]);
            c.capPos.push_back(pf[2]);
            c.capPos.push_back(1.0f);
        } else {
            c.capPos.push_back(pf[0] / rhw / vpw * 2.0f - 1.0f);
            c.capPos.push_back(pf[1] / rhw / vph * 2.0f - 1.0f);
            c.capPos.push_back(pf[2] / rhw);
            c.capPos.push_back(1.0f);
        }
        if (colIsFloat4) {
            float cf[4];
            memcpy(cf, &cbytes[(size_t)v * cstride + pipe->vaOffset[colAi]], 16);
            c.capCol.push_back(cf[0]);
            c.capCol.push_back(cf[1]);
            c.capCol.push_back(cf[2]);
            c.capCol.push_back(cf[3]);
        } else {
            U32 ci;
            memcpy(&ci, &cbytes[(size_t)v * cstride + pipe->vaOffset[colAi]], 4);
            // D3DCOLOR is 0xAARRGGBB; the B8G8R8A8 byte order already matches it.
            float comp[4] = { (ci & 0xff) / 255.0f, ((ci >> 8) & 0xff) / 255.0f,
                              ((ci >> 16) & 0xff) / 255.0f, ((ci >> 24) & 0xff) / 255.0f };
            c.capCol.push_back(bgr ? comp[2] : comp[0]);
            c.capCol.push_back(comp[1]);
            c.capCol.push_back(bgr ? comp[0] : comp[2]);
            c.capCol.push_back(comp[3]);
        }
    }
    c.capCount = indexCount;
    if (capLogCount < 30) { capLogCount++; klog_fmt("CAPOK: indexCount=%u firstIndex=%u capCount=%u", indexCount, firstIndex, c.capCount); }
    return true;
}

// Non-indexed twin of captureIndexedDraw: bake vertexCount sequential vertices
// starting at firstVertex (vkCmdDraw has no vertexOffset; firstVertex is the
// absolute index). The baked arrays are in draw order so the replay
// vertex_index maps 1:1, exactly like the indexed path.
bool captureDraw(CmdBuf* cb, Cmd& c) {
    U32 vertexCount = (U32)c.a, firstVertex = (U32)c.c;
    static int capLogCountNI = 0;
    if (!vertexCount || vertexCount > kCapMaxVerts) {
        if (capLogCountNI < 30) { capLogCountNI++; klog_fmt("CAPFAIL-NI: vertexCount=%u", vertexCount); }
        return false;
    }
    Pipeline* pipe = objOf<Pipeline>(cb->pipe, K_PIPELINE, "capDrawNI");
    if (!pipe || !pipe->nva) return false;
    // Position = location-0 float4 (D3D9 tri9) or float3 (D3D12 scene12).
    // Color = location-1 unorm8x4 packed (D3D9) or float4 (D3D12 scene12).
    int posAi = -1, colAi = -1;
    bool posIsVec3 = false, colIsFloat4 = false;
    for (U32 i = 0; i < pipe->nva; i++) {
        U32 f = pipe->vaFormat[i];
        if (pipe->vaLocation[i] == 0) {
            if (f == VK_FORMAT_R32G32B32A32_SFLOAT) { posAi = (int)i; posIsVec3 = false; }
            else if (f == VK_FORMAT_R32G32B32_SFLOAT) { posAi = (int)i; posIsVec3 = true; }
        }
        if (pipe->vaLocation[i] == 1) {
            if (f == VK_FORMAT_B8G8R8A8_UNORM || f == VK_FORMAT_R8G8B8A8_UNORM ||
                f == VK_FORMAT_B8G8R8A8_SRGB || f == VK_FORMAT_R8G8B8A8_SRGB) {
                colAi = (int)i; colIsFloat4 = false;
            } else if (f == VK_FORMAT_R32G32B32A32_SFLOAT) {
                colAi = (int)i; colIsFloat4 = true;
            }
        }
    }
    if (posAi < 0 || colAi < 0) {
        if (capLogCountNI < 30) { capLogCountNI++; klog_fmt("CAPFAIL-NI: posAi=%d colAi=%d nva=%u vertexCount=%u", posAi, colAi, pipe->nva, vertexCount); }
        return false;
    }
    U32 posBind = pipe->vaBinding[posAi], colBind = pipe->vaBinding[colAi];
    if (posBind >= VK64_MAX_VERT_BINDINGS || colBind >= VK64_MAX_VERT_BINDINGS) return false;
    Buffer* pbuf = objOf<Buffer>(cb->vbs[posBind], K_BUFFER, "capDrawNI");
    Buffer* cbuf = objOf<Buffer>(cb->vbs[colBind], K_BUFFER, "capDrawNI");
    if (!pbuf || !cbuf || !pbuf->mem || !cbuf->mem) {
        if (capLogCountNI < 30) { capLogCountNI++; klog_fmt("CAPFAIL-NI: buffers pbuf=%p cbuf=%p vertexCount=%u", (void*)pbuf, (void*)cbuf, vertexCount); }
        return false;
    }
    // Stride may come from pipeline (static) or bind time (dynamic, v2 API).
    // Prefer bind-time stride if set, fall back to pipeline stride.
    U32 pstride = cb->vbStride[posBind] ? cb->vbStride[posBind] : pipe->vbStride[posBind];
    U32 cstride = cb->vbStride[colBind] ? cb->vbStride[colBind] : pipe->vbStride[colBind];
    if (!pstride || !cstride) {
        if (capLogCountNI < 30) { capLogCountNI++; klog_fmt("CAPFAIL-NI: stride pstride=%u cstride=%u vertexCount=%u", pstride, cstride, vertexCount); }
        return false;
    }
    U32 posSize = posIsVec3 ? 12 : 16;
    U32 colSize = colIsFloat4 ? 16 : 4;
    U32 maxV = firstVertex + vertexCount - 1;
    U64 pNeed = (U64)maxV * pstride + pipe->vaOffset[posAi] + posSize;
    U64 cNeed = (U64)maxV * cstride + pipe->vaOffset[colAi] + colSize;
    std::vector<U8> pbytes = readGuestRange(pbuf->mem, pbuf->memOff + cb->vbOff[posBind], pNeed);
    std::vector<U8> cbytes = readGuestRange(cbuf->mem, cbuf->memOff + cb->vbOff[colBind], cNeed);
    if (pbytes.size() < pNeed || cbytes.size() < cNeed) {
        if (capLogCountNI < 30) { capLogCountNI++; klog_fmt("CAPFAIL-NI: bytes pbytes=%zu/%llu cbytes=%zu/%llu vertexCount=%u", pbytes.size(), (unsigned long long)pNeed, cbytes.size(), (unsigned long long)cNeed, vertexCount); }
        return false;
    }
    // RHW pixel -> NDC bake needs the viewport DXVK set for this draw.
    // D3D12 vec3 positions are already in clip/NDC space and bypass this.
    float vpw = 0, vph = 0;
    if (!posIsVec3) {
        if (!cb->haveVp || cb->vp[2] <= 0 || cb->vp[3] <= 0) {
            return false;
        }
        vpw = cb->vp[2]; vph = cb->vp[3];
    }
    bool bgr = (pipe->vaFormat[colAi] == VK_FORMAT_B8G8R8A8_UNORM ||
                pipe->vaFormat[colAi] == VK_FORMAT_B8G8R8A8_SRGB);
    c.capPos.reserve((size_t)vertexCount * 4);
    c.capCol.reserve((size_t)vertexCount * 4);
    for (U32 i = 0; i < vertexCount; i++) {
        U32 v = firstVertex + i;
        float pf[4];
        if (posIsVec3) {
            memcpy(pf, &pbytes[(size_t)v * pstride + pipe->vaOffset[posAi]], 12);
            pf[3] = 1.0f;
        } else {
            memcpy(pf, &pbytes[(size_t)v * pstride + pipe->vaOffset[posAi]], 16);
        }
        float rhw = pf[3] != 0.0f ? pf[3] : 1.0f;
        // Vulkan and WebGPU share the top-left framebuffer origin: NDC y
        // follows the pixel row, no flip (the page tier documented rule).
        // D3D12 scene12 positions are already in NDC-like space; D3D9 tri9
        // uses RHW pixel coords requiring viewport normalization.
        if (posIsVec3) {
            // D3D12: position already in clip/NDC space, pass through.
            c.capPos.push_back(pf[0]);
            c.capPos.push_back(pf[1]);
            c.capPos.push_back(pf[2]);
            c.capPos.push_back(1.0f);
        } else {
            c.capPos.push_back(pf[0] / rhw / vpw * 2.0f - 1.0f);
            c.capPos.push_back(pf[1] / rhw / vph * 2.0f - 1.0f);
            c.capPos.push_back(pf[2] / rhw);
            c.capPos.push_back(1.0f);
        }
        if (colIsFloat4) {
            float cf[4];
            memcpy(cf, &cbytes[(size_t)v * cstride + pipe->vaOffset[colAi]], 16);
            c.capCol.push_back(cf[0]);
            c.capCol.push_back(cf[1]);
            c.capCol.push_back(cf[2]);
            c.capCol.push_back(cf[3]);
        } else {
            U32 ci;
            memcpy(&ci, &cbytes[(size_t)v * cstride + pipe->vaOffset[colAi]], 4);
            // D3DCOLOR is 0xAARRGGBB; the B8G8R8A8 byte order already matches it.
            float comp[4] = { (ci & 0xff) / 255.0f, ((ci >> 8) & 0xff) / 255.0f,
                              ((ci >> 16) & 0xff) / 255.0f, ((ci >> 24) & 0xff) / 255.0f };
            c.capCol.push_back(bgr ? comp[2] : comp[0]);
            c.capCol.push_back(comp[1]);
            c.capCol.push_back(bgr ? comp[0] : comp[2]);
            c.capCol.push_back(comp[3]);
        }
    }
    c.capCount = vertexCount;
    if (capLogCountNI < 30) { capLogCountNI++; klog_fmt("CAPOK-NI: vertexCount=%u firstVertex=%u capCount=%u", vertexCount, firstVertex, c.capCount); }
    return true;
}

// Minimal SPIR-V 1.0 word emitter for the replay shaders.
struct SpvEmit {
    std::vector<U32> w;
    U32 next = 1;
    U32 fresh() { return next++; }
    void inst(U32 opcode, std::initializer_list<U32> args) {
        w.push_back((((U32)args.size() + 1) << 16) | opcode);
        for (U32 a : args) w.push_back(a);
    }
    void entryPoint(U32 model, U32 id, const char* name, std::initializer_list<U32> iface) {
        size_t at = w.size();
        w.push_back(0);
        w.push_back(model); w.push_back(id);
        size_t n = strlen(name) + 1, words = (n + 3) / 4, base = w.size();
        w.resize(base + words, 0);
        memcpy(&w[base], name, n);
        for (U32 i : iface) w.push_back(i);
        w[at] = ((U32)(w.size() - at) << 16) | 15;   // OpEntryPoint
    }
    std::vector<U8> bytes() {
        w[3] = next;   // id bound
        std::vector<U8> out(w.size() * 4);
        memcpy(out.data(), w.data(), out.size());
        return out;
    }
};

// Vertex: gl_VertexIndex -> OpSwitch -> constant NDC position + linear color.
std::vector<U8> synthReplayVs(const std::vector<float>& pos, const std::vector<float>& col, U32 n) {
    SpvEmit s;
    s.w.reserve(700);
    s.w.insert(s.w.end(), {0x07230203, 0x00010000, 0, 0, 0});
    U32 tVoid = s.fresh(), tInt = s.fresh(), tFloat = s.fresh(), tV4 = s.fresh();
    U32 tFn = s.fresh(), pInInt = s.fresh(), pOutV4 = s.fresh();
    U32 vIdx = s.fresh(), vPos = s.fresh(), vCol = s.fresh(), fn = s.fresh();
    s.inst(17, {1});                        // OpCapability Shader
    s.inst(14, {0, 1});                     // OpMemoryModel Logical GLSL450
    s.entryPoint(0, fn, "main", {vIdx, vPos, vCol});
    s.inst(71, {vIdx, 11, 42});             // OpDecorate BuiltIn VertexIndex
    s.inst(71, {vPos, 11, 0});              // OpDecorate BuiltIn Position
    s.inst(71, {vCol, 30, 0});              // OpDecorate Location 0
    s.inst(19, {tVoid});                    // OpTypeVoid
    s.inst(21, {tInt, 32, 1});              // OpTypeInt 32 1
    s.inst(22, {tFloat, 32});               // OpTypeFloat 32
    s.inst(23, {tV4, tFloat, 4});           // OpTypeVector %float 4
    s.inst(33, {tFn, tVoid});               // OpTypeFunction %void
    s.inst(32, {pInInt, 1, tInt});          // OpTypePointer Input %int
    s.inst(32, {pOutV4, 3, tV4});           // OpTypePointer Output %v4f
    std::vector<U32> posId(n), colId(n);
    for (U32 i = 0; i < n; i++) {
        U32 pc[4], cc[4];
        for (int k = 0; k < 4; k++) {
            U32 b;
            pc[k] = s.fresh(); memcpy(&b, &pos[i*4+k], 4); s.inst(43, {tFloat, pc[k], b});
            cc[k] = s.fresh(); memcpy(&b, &col[i*4+k], 4); s.inst(43, {tFloat, cc[k], b});
        }
        posId[i] = s.fresh(); s.inst(44, {tV4, posId[i], pc[0], pc[1], pc[2], pc[3]});
        colId[i] = s.fresh(); s.inst(44, {tV4, colId[i], cc[0], cc[1], cc[2], cc[3]});
    }
    s.inst(59, {pInInt, vIdx, 1});          // OpVariable Input
    s.inst(59, {pOutV4, vPos, 3});          // OpVariable Output
    s.inst(59, {pOutV4, vCol, 3});
    s.inst(54, {tVoid, fn, 0, tFn});        // OpFunction
    U32 entry = s.fresh(), merge = s.fresh();
    s.inst(248, {entry});                   // OpLabel
    U32 idx = s.fresh();
    s.inst(61, {tInt, idx, vIdx});          // OpLoad VertexIndex
    std::vector<U32> caseLbl(n);
    for (U32 i = 0; i < n; i++) caseLbl[i] = s.fresh();
    U32 dflt = s.fresh();
    s.inst(247, {merge, 0});                // OpSelectionMerge (247)
    {
        size_t at = s.w.size();             // OpSwitch = 251
        s.w.push_back(0); s.w.push_back(idx); s.w.push_back(dflt);
        for (U32 i = 0; i < n; i++) { s.w.push_back(i); s.w.push_back(caseLbl[i]); }
        s.w[at] = ((U32)(s.w.size() - at) << 16) | 251;
    }
    for (U32 i = 0; i < n; i++) {
        s.inst(248, {caseLbl[i]});
        s.inst(62, {vPos, posId[i]});       // OpStore position/color
        s.inst(62, {vCol, colId[i]});
        s.inst(249, {merge});               // OpBranch
    }
    s.inst(248, {dflt});
    s.inst(249, {merge});
    s.inst(248, {merge});
    s.inst(253, {});                        // OpReturn
    s.inst(56, {});                         // OpFunctionEnd
    return s.bytes();
}

// Fragment: Location-0 color passthrough.
// Fragment: Location-0 color passthrough.
// Fragment: Location-0 color passthrough.
std::vector<U8> synthReplayFs() {
    SpvEmit s;
    s.w.reserve(80);
    s.w.insert(s.w.end(), {0x07230203, 0x00010000, 0, 0, 0});
    U32 tVoid = s.fresh(), tFloat = s.fresh(), tV4 = s.fresh();
    U32 tFn = s.fresh(), pInV4 = s.fresh(), pOutV4 = s.fresh();
    U32 vIn = s.fresh(), vOut = s.fresh(), fn = s.fresh();
    s.inst(17, {1});
    s.inst(14, {0, 1});
    s.entryPoint(4, fn, "main", {vIn, vOut});
    s.inst(16, {fn, 7});                    // OpExecutionMode OriginUpperLeft
    s.inst(71, {vIn, 30, 0});
    s.inst(71, {vOut, 30, 0});
    s.inst(19, {tVoid});
    s.inst(22, {tFloat, 32});
    s.inst(23, {tV4, tFloat, 4});
    s.inst(33, {tFn, tVoid});
    s.inst(32, {pInV4, 1, tV4});
    s.inst(32, {pOutV4, 3, tV4});
    s.inst(59, {pInV4, vIn, 1});
    s.inst(59, {pOutV4, vOut, 3});
    s.inst(54, {tVoid, fn, 0, tFn});
    U32 entry = s.fresh(), tmp = s.fresh();
    s.inst(248, {entry});
    s.inst(61, {tV4, tmp, vIn});
    s.inst(62, {vOut, tmp});
    s.inst(253, {});
    s.inst(56, {});
    return s.bytes();
}

void fillImageFormatProps(VkImageFormatProperties* p) {
    p->maxExtent = VkExtent3D{16384, 16384, 2048};
    p->maxMipLevels = 14;
    p->maxArrayLayers = 2048;
    p->sampleCounts = (VkSampleCountFlags)(VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_2_BIT |
                                           VK_SAMPLE_COUNT_4_BIT | VK_SAMPLE_COUNT_8_BIT);
    p->maxResourceSize = 2ull * 1024 * 1024 * 1024;
}

// Submit-time frame reconstruction (P1's vkQueueSubmit body, file I/O replaced
// by the in-memory manifest serializer).
// ---------------------------------------------------------------------------
void resolveFrame(CmdBuf* st, U64* uboOut, U64* uboSize, U64* viewOut, U64* sampOut) {
    *uboOut = 0; *uboSize = 0; *viewOut = 0; *sampOut = 0;
    DescSet* s0 = st->sets[0] ? objOf<DescSet>(st->sets[0], K_SET, "resolveFrame") : nullptr;
    VK64_DROP_IF(return, !s0);
    for (U32 i = 0; i < s0->nbind; i++) {
        if (s0->binds[i].binding == 0 && s0->binds[i].obj) { *uboOut = s0->binds[i].obj; *uboSize = s0->binds[i].range; }
        if (s0->binds[i].binding == 1 && s0->binds[i].obj) { *viewOut = s0->binds[i].obj; *sampOut = s0->binds[i].range; }
    }
}

void serializeSubmit(const std::vector<std::pair<CmdBuf*, U64>>& frames) {
    for (const auto& fr : frames) {
        CmdBuf* st = fr.first;
        // the recorded framebuffer id lives in the BEGIN_RP command
        U64 fbId = 0;
        for (const Cmd& k : st->cmds) if (k.kind == CMD_BEGIN_RP) { fbId = k.b; break; }
        Framebuffer* fb = objOf<Framebuffer>(fbId, K_FRAMEBUFFER, "vkQueueSubmit");
        if (!fb) { klog_fmt("vk64: submit: frame %u has no framebuffer, skipped", g_frameNo); continue; }

        Pipeline* pipe = nullptr;
        const float* vp = nullptr;
        const float* sci = nullptr;
        float clear[4] = {0, 0, 0, 1};
        double clearDepth = 1.0;
        std::vector<std::pair<U32, U32>> draws;   // vertexCount, firstVertex
        std::vector<const Cmd*> pushes;           // in order: offset + bytes matter
        std::vector<float> allCapPos; std::vector<float> allCapCol;            // first indexed draw with baked verts
        {
            // Debug: count cmd kinds
            U32 cntDraw = 0, cntDrawIdx = 0, cntOther = 0;
            for (const Cmd& k2 : st->cmds) {
                if (k2.kind == CMD_DRAW) cntDraw++;
                else if (k2.kind == CMD_DRAW_INDEXED) cntDrawIdx++;
                else cntOther++;
            }
        }
        for (const Cmd& k : st->cmds) {
            switch (k.kind) {
                case CMD_BEGIN_RP:
                    memcpy(clear, k.f, sizeof(clear));
                    clearDepth = k.clearDepth;
                    break;
                case CMD_BIND_PIPE:
                    pipe = objOf<Pipeline>(k.a, K_PIPELINE, "vkQueueSubmit");
                    break;
                case CMD_VIEWPORT: vp = k.f; break;
                case CMD_SCISSOR:  sci = k.f; break;
                case CMD_PUSH_CONST: pushes.push_back(&k); break;
                case CMD_DRAW:
                    // Baked in draw order like indexed: captured draws get
                    // firstVertex = current baked-verts offset for replay.
                    if (draws.size() < VK64_MAX_DRAWS) {
                        U32 vtxOff = (U32)k.c;
                        if (k.capCount) {
                            vtxOff = (U32)(allCapPos.size() / 4);
                            for (float f : k.capPos) allCapPos.push_back(f);
                            for (float f : k.capCol) allCapCol.push_back(f);
                        }
                        draws.push_back({(U32)k.a, vtxOff});
                    }
                    break;
                case CMD_DRAW_INDEXED:
                    // Baked in draw order. Concatenate all baked verts so the
                    // replay shader has every quad, with firstVertex offsets.
                    if (draws.size() < VK64_MAX_DRAWS) {
                        U32 vtxOff = 0;
                        if (k.capCount) {
                            // firstVertex = current total baked verts
                            vtxOff = (U32)(allCapPos.size() / 4);
                            for (float f : k.capPos) allCapPos.push_back(f);
                            for (float f : k.capCol) allCapCol.push_back(f);
                        }
                        draws.push_back({(U32)k.a, vtxOff});
                    }
                    break;
                default: break;
            }
        }
        if (!pipe || draws.empty()) {
            klog_fmt("vk64: submit: frame %u without draw (pipe=0x%llx draws=%u), skipped",
                     g_frameNo, (unsigned long long)(pipe ? 0 : 0), (unsigned)draws.size());
            continue;
        }

        U64 uboId = 0, uboSize = 0, viewId = 0, sampId = 0;
        resolveFrame(st, &uboId, &uboSize, &viewId, &sampId);
        Buffer* ubo = uboId ? objOf<Buffer>(uboId, K_BUFFER, "vkQueueSubmit") : nullptr;
        Sampler* samp = sampId ? objOf<Sampler>(sampId, K_SAMPLER, "vkQueueSubmit") : nullptr;
        ImageView* view = viewId ? objOf<ImageView>(viewId, K_IMAGEVIEW, "vkQueueSubmit") : nullptr;
        Image* tex = view ? objOf<Image>(view->image, K_IMAGE, "vkQueueSubmit") : nullptr;
        ShaderModule* vs = pipe->vs ? objOf<ShaderModule>(pipe->vs, K_SHADER, "vkQueueSubmit") : nullptr;
        ShaderModule* fs = pipe->fs ? objOf<ShaderModule>(pipe->fs, K_SHADER, "vkQueueSubmit") : nullptr;

        // UBO bytes: descriptor-set-0 binding 0, clamped to the allocation.
        std::vector<U8> uboBytes;
        bool haveUbo = false;
        if (ubo && ubo->mem) {
            uboBytes = readGuestRange(ubo->mem, ubo->memOff, uboSize ? uboSize : ubo->size);
            haveUbo = !uboBytes.empty();
        }
        // Texture bytes: the staging path (a recorded copy landed in the image's
        // bound memory) and the linear path (guest wrote them through
        // vkGetImageSubresourceLayout / vkMapMemory) are now the same guest
        // range; P1's 'copied' flag only decides which of the two it reports.
        std::vector<U8> texBytes;
        bool haveTex = false;
        U32 tw = 0, th = 0;
        if (tex) {
            tw = tex->w; th = tex->h;
            if (tex->mem) {
                texBytes = readGuestRange(tex->mem, tex->memOff, tex->pixBytes);
                haveTex = !texBytes.empty();
            }
        }

        std::string j = "{";
        char buf[512];
        snprintf(buf, sizeof buf, "\"frame\":%u,\"width\":%u,\"height\":%u,\"overflow\":%s,",
                 g_frameNo, fb->w, fb->h, g_frameOverflow ? "true" : "false");
        j += buf;
        snprintf(buf, sizeof buf, "\"colorFormat\":\"%s\",", fmt_name(fb->natt && view ? (U32)view->format : 0));
        j += buf;
        j += "\"clearColor\":" + jsonFloats(clear, 4) + ",";
        snprintf(buf, sizeof buf, "\"clearDepth\":%g,", clearDepth);
        j += buf;
        j += "\"viewport\":"; j += vp ? jsonFloats(vp, 6) : "null"; j += ",";
        j += "\"scissor\":";  j += sci ? jsonFloats(sci, 4) : "null"; j += ",";
        snprintf(buf, sizeof buf, "\"cull\":%u,\"front\":%u,\"depthTest\":%u,\"depthWrite\":%u,\"depthOp\":%u,",
                 pipe->cull, pipe->front, pipe->depthTest, pipe->depthWrite, pipe->depthOp);
        j += buf;
        // G2 replay: when an indexed draw baked its vertices, substitute the
        // procedural shaders so the page tier (no vertex buffers) rasterizes the
        // captured triangle. Otherwise the guest modules go out unchanged.
        std::vector<U8> repVs, repFs;
        bool haveReplay = false;
        U32 totalCapVerts = (U32)(allCapPos.size() / 4);
        if (totalCapVerts > 0) {
            repVs = synthReplayVs(allCapPos, allCapCol, totalCapVerts);
            repFs = synthReplayFs();
            haveReplay = !repVs.empty() && !repFs.empty();
            if (haveReplay)
                klog_fmt("vk64: replay shaders synthesized (%u verts, vs %zuB fs %zuB)",
                         totalCapVerts, repVs.size(), repFs.size());
        }
        if (haveReplay) {
            char sh[40];
            snprintf(sh, sizeof sh, "%016llx",
                     (unsigned long long)blob_hash(repVs.data(), repVs.size()));
            j += "\"vs\":{\"hash\":\""; j += sh; j += "\",";
            jsonBlob(j, "spv", repVs); j += ",";
            snprintf(sh, sizeof sh, "%016llx",
                     (unsigned long long)blob_hash(repFs.data(), repFs.size()));
            j += "\"fs\":{\"hash\":\""; j += sh; j += "\",";
            jsonBlob(j, "spv", repFs); j += ",";
        } else if (vs) {
            char sh[40];
            snprintf(sh, sizeof sh, "%016llx", (unsigned long long)vs->hash);
            j += "\"vs\":{\"hash\":\""; j += sh; j += "\",";
            jsonBlob(j, "spv", vs->code); j += ",";
        } else j += "\"vs\":null,";
        if (!haveReplay) {
            if (fs) {
                char sh[40];
                snprintf(sh, sizeof sh, "%016llx", (unsigned long long)fs->hash);
                j += "\"fs\":{\"hash\":\""; j += sh; j += "\",";
                jsonBlob(j, "spv", fs->code); j += ",";
            } else j += "\"fs\":null,";
        }
        jsonBlobOrNull(j, "ubo", uboBytes, haveUbo);
        j += ",";
        snprintf(buf, sizeof buf, "\"texture\":{\"w\":%u,\"h\":%u,\"format\":\"%s\",\"staged\":%s,",
                 tw, th, tex ? fmt_name(tex->format) : "UNKNOWN", (tex && tex->copied) ? "true" : "false");
        j += buf;
        jsonBlobOrNull(j, "pixels", texBytes, haveTex);
        j += "},";
        snprintf(buf, sizeof buf,
                 "\"sampler\":{\"mag\":%u,\"min\":%u,\"mipmap\":%u,\"addrU\":%u,\"addrV\":%u,\"maxAniso\":%g},",
                 samp ? samp->mag : 0, samp ? samp->min : 0, samp ? samp->mipmap : 0,
                 samp ? samp->addrU : 0, samp ? samp->addrV : 0, samp ? (double)samp->maxAniso : 1.0);
        j += buf;
        // The vertex input layout, straight off the recorded pipeline. Additive:
        // a consumer that predates these keys (host/p1render.mjs) ignores them and
        // vkcube's own frame still renders exactly as before.
        j += "\"vertexLayout\":{\"bindings\":[";
        for (U32 i = 0; i < pipe->nvb; i++) {
            if (i) j += ",";
            snprintf(buf, sizeof buf, "{\"binding\":%u,\"stride\":%u,\"inputRate\":%u}",
                     i, pipe->vbStride[i], pipe->vbInputRate[i]);
            j += buf;
        }
        j += "],\"attributes\":[";
        for (U32 i = 0; i < pipe->nva; i++) {
            if (i) j += ",";
            snprintf(buf, sizeof buf,
                     "{\"location\":%u,\"binding\":%u,\"format\":%u,\"offset\":%u}",
                     pipe->vaLocation[i], pipe->vaBinding[i], pipe->vaFormat[i], pipe->vaOffset[i]);
            j += buf;
        }
        j += "]},";
        // Push constants: one entry per recorded push, base64 bytes + the byte
        // offset they land at, so a consumer can overlay them per draw.
        j += "\"pushConstants\":[";
        for (size_t i = 0; i < pushes.size(); i++) {
            if (i) j += ",";
            snprintf(buf, sizeof buf, "{\"offset\":%u,\"size\":%u,\"stageFlags\":%u,",
                     pushes[i]->pcOffset, pushes[i]->pcSize, pushes[i]->pcStageFlags);
            j += buf;
            std::vector<U8> bytes(pushes[i]->pcBytes, pushes[i]->pcBytes + pushes[i]->pcSize);
            jsonBlob(j, "bytes", bytes);
            j += "}";
        }
        j += "],";
        j += "\"draws\":[";
        for (size_t i = 0; i < draws.size(); i++) {
            if (i) j += ",";
            snprintf(buf, sizeof buf, "{\"vertexCount\":%u,\"firstVertex\":%u}", draws[i].first, draws[i].second);
            j += buf;
        }
        j += "]}";

        // P1 wrote one manifest per submit-with-draw and let present.log tie the
        // present to it. Here the manifest is BUILT at submit and HOPPED at
        // present; if a second submit-with-draw arrives before any present, the
        // stale one is flushed first so no frame is silently dropped.
        if (!g_pendingFrame.empty()) emitFrameNow();
        g_pendingFrame = j;
        g_frameOverflow = false;
        klog_fmt("vk64: FRAME %u built: %ux%u draws=%u vs=%s fs=%s ubo=%zuB tex=%ux%u(%zuB) "
                 "vtxAttrs=%u pushes=%u json=%zuB",
                 g_frameNo, fb->w, fb->h, (unsigned)draws.size(),
                 vs ? "yes" : "no", fs ? "yes" : "no",
                 uboBytes.size(), tw, th, texBytes.size(), (unsigned)pipe->nva,
                 (unsigned)pushes.size(), j.size());
    }
}

// ---------------------------------------------------------------------------
// Schema v2: binary framed records (docs/vulkan-protocol.md sections 2-3).
//
// v1 hops one JSON string per present; v2 streams binary records across one
// MAIN_THREAD_EM_ASM per CHUNK (1 MiB cap). The hop moves (ptr,len,flags) —
// three ints — and the JS body copies the bytes once; there is no UTF-8
// decode, no JSON.parse, and no base64 on either side. FRAME_BEGIN is emitted
// at the first submit-with-draw after a present, FRAME_END seals at present,
// so a frame is present-to-present no matter how many submits it spans.
//
// BW64_VKSCHEMA=1 selects the v1 JSON emitter verbatim (serializeSubmit +
// window.bwVkFrame); the default is v2.
// ---------------------------------------------------------------------------
inline bool vkSchemaV2() {
    static int v = -1;
    if (v < 0) {
        const char* e = getenv("BW64_VKSCHEMA");
        v = (e && e[0] == '1' && e[1] == '\0') ? 0 : 1;
    }
    return v != 0;
}
inline bool vkDumpV2() {
    static int cached = -1;
    if (cached < 0) { const char* t = getenv("BW64_VKDUMP"); cached = (t && t[0] == '1' && t[1] == '\0') ? 1 : 0; }
    return cached != 0;
}

enum V2Rec : U32 {
    V2_FRAME_BEGIN = 0x01, V2_FRAME_END = 0x02,
    V2_RP_BEGIN = 0x03, V2_RP_END = 0x04,
    V2_SHADER = 0x05, V2_PIPELINE = 0x06,
    V2_BIND_SETS = 0x07, V2_DESC_SET = 0x08,
    V2_BUFFER_DATA = 0x09, V2_IMAGE_DATA = 0x0A, V2_SAMPLER = 0x0B,
    V2_PUSH = 0x0C, V2_VIEWPORT = 0x0D, V2_SCISSOR = 0x0E,
    V2_VERTEX_BIND = 0x0F, V2_INDEX_BIND = 0x10, V2_DRAW = 0x11,
    V2_INLINE_BYTES = 0x12, V2_READBACK = 0x13, V2_IMAGE_COPY = 0x14,
    V2_STENCIL_STATE = 0x15, V2_BLEND_CONSTANTS = 0x16, V2_CLEAR_ATTACHMENTS = 0x17,
    V2_DEPTH_BIAS = 0x18, V2_IMAGE_BLIT = 0x19,
    V2_RASTER_STATE = 0x1A, V2_IMAGE_CLEAR_DS = 0x1B,
};
const U32 V2_CHUNK_MAGIC = 0x46324B56u;   // 'VK2F' little-endian: bytes 56 4B 32 46
const U32 V2_CHUNK_VERSION = 2;
// A sealed chunk rides one MAIN_THREAD_EM_ASM hop; the page's ingest sanity cap
// is 16MB (V2_MAX_CHUNK_BYTES), so seal below that. 1MB used to serve as the
// buffer-payload cap too, which truncated every DXVK D3D9 staging upload
// (heap-sized VkBuffers of 16MB, bound ranges of 5-8MB) to 1MB and starved the
// page of vertex/texture data.
const size_t V2_CHUNK_CAP = 12 * 1024 * 1024;  // seal before a record that would overflow
// Soft warning threshold for one presentation's pending chunks. Complete
// submissions are never discarded: a dropped submission may carry the only
// copy of a buffer/image payload that later frames reference byte-less (the
// page has no upstream resend channel), which blacked out Portal's menu
// background. The queue is drained by the present hop.
const U32 V2_PRODUCER_CHUNK_RING = 8;
// Byte-payload cap for a single V2_BUFFER_DATA record. The page rejects any
// chunk over 16MB (V2_MAX_CHUNK_BYTES), so one record must stay well under it;
// 8MB leaves room for the rest of the frame's small records in the same chunk.
// DXVK 32-bit D3D9 backs vertex/index staging with 16MB heap buffers whose
// BOUND ranges reach 8MB, so the old 1MB cap dropped most of every upload.
// Descriptor uniforms are small (D3D9 constant blocks are <= 64KB); vertex and
// index ranges are additionally clamped to what the frame's draws actually
// touch (computeV2BufferUse below), which keeps the steady-state cost small.
const size_t V2_MAX_BUF_SEND = 8u << 20; // 8MB
// Full-resend period (frames) for buffer content dedup: heals any
// bridge/page desync caused by a dropped or rejected chunk (the page has no
// upstream resend channel).
const U32 V2_BUF_RESEND_PERIOD = 64;

struct ChunkWriter {
    std::vector<U8> b;
    void u8(U32 v)  { b.push_back((U8)v); }
    void u16(U32 v) { b.push_back((U8)v); b.push_back((U8)(v >> 8)); }
    void u32(U32 v) { for (int i = 0; i < 4; i++) b.push_back((U8)(v >> (8 * i))); }
    void u64(U64 v) { for (int i = 0; i < 8; i++) b.push_back((U8)(v >> (8 * i))); }
    void f32(float v) { U32 u; memcpy(&u, &v, 4); u32(u); }
    void bytes(const U8* p, size_t n) { if (p && n) b.insert(b.end(), p, p + n); }
    void bytes(const std::vector<U8>& v) { bytes(v.data(), v.size()); }
};
static void writeSamplerRecordPayload(ChunkWriter& p, U64 id, const Sampler& s) {
    p.u64(id);
    p.u32(s.mag); p.u32(s.min); p.u32(s.mipmap);
    p.u32(s.addrU); p.u32(s.addrV); p.f32(s.maxAniso);
    p.u32(0x314d4153u); // SAM1
    p.u32(s.addrW); p.f32(s.mipLodBias); p.u32(s.anisotropyEnable);
    p.u32(s.compareEnable); p.u32(s.compareOp); p.f32(s.minLod); p.f32(s.maxLod); p.u32(s.borderColor);
}
static void emitColorBlend(const Pipeline& pipe, ChunkWriter& p) {
    p.u32(0x31444e42u); // BND1: attachment blend equations and write masks
    p.u32(pipe.colorBlendCount);
    for (U32 i = 0; i < pipe.colorBlendCount; ++i) {
        const auto& b = pipe.colorBlend[i];
        p.u32(b.enable); p.u32(b.srcColor); p.u32(b.dstColor); p.u32(b.colorOp);
        p.u32(b.srcAlpha); p.u32(b.dstAlpha); p.u32(b.alphaOp); p.u32(b.writeMask);
    }
    for (float value : pipe.blendConstants) p.f32(value);
}


static void emitStencil(const Pipeline& pipe, ChunkWriter& p) {
    p.u32(0x314e5453u); // STN1
    p.u32(pipe.stencil.enabled); p.u32(pipe.stencil.dynamicMask); p.u32(pipe.blendConstantsDynamic ? 1 : 0);
    auto face = [&](const StencilFace& f) {
        p.u32(f.failOp); p.u32(f.passOp); p.u32(f.depthFailOp); p.u32(f.compareOp);
        p.u32(f.compareMask); p.u32(f.writeMask); p.u32(f.reference);
    };
    face(pipe.stencil.front); face(pipe.stencil.back);
}

static void emitDepthBias(ChunkWriter& p, U32 depthBiasEnable, U32 depthClampEnable,
                          U32 dynamic, float constantFactor, float clamp, float slopeFactor) {
    p.u32(0x31534244u); // DBS1
    p.u32(depthBiasEnable); p.u32(depthClampEnable); p.u32(dynamic);
    p.f32(constantFactor); p.f32(clamp); p.f32(slopeFactor);
}

// One frame under construction: an open chunk plus sealed chunks. Records never
// straddle chunks; a single oversize record gets a chunk of its own.
struct V2FrameAsm {
    ChunkWriter cur;
    std::vector<std::vector<U8>> sealed;
    U32* hist = nullptr;   // optional per-frame record-type histogram (BW64_VKDUMP=1)
    void openChunk(U32 seq) {
        cur.b.clear();
        // 16-byte header per docs/vulkan-protocol.md §2.3: magic, version, flags,
        // seq, reserved. (flags bit0 = MORE is patched at byte 6 in
        // v2QueueFrame.)
        cur.u32(V2_CHUNK_MAGIC); cur.u16(V2_CHUNK_VERSION); cur.u16(0); cur.u32(seq);
        cur.u32(0);
    }
    void sealChunk() {
        if (cur.b.size() > 16) sealed.push_back(cur.b);
        cur.b.clear();
    }
    void emitRec(U32 type, const std::vector<U8>& payload, U32& seq) {
        size_t need = 8 + payload.size();
        if (cur.b.size() + need > V2_CHUNK_CAP && cur.b.size() > 16) {
            sealChunk();
            openChunk(seq++);
        }
        cur.u16(type); cur.u16(0); cur.u32((U32)payload.size());
        cur.bytes(payload);
        if (hist && type < 32) hist[type]++;
    }
};

struct V2DoneFrame { std::vector<std::vector<U8>> chunks; };
std::deque<V2DoneFrame> g_v2Pending;   // complete frames awaiting the present hop
U32 g_v2ChunkSeq = 0;
U64 g_v2OversizeFrames = 0;

void v2QueueFrame(V2FrameAsm& fa) {
    fa.sealChunk();
    if (fa.sealed.empty()) return;
    // MORE flag on every chunk but the last (byte 6 = flags low byte).
    for (size_t i = 0; i + 1 < fa.sealed.size(); i++) fa.sealed[i][6] |= 1;
    size_t chunks = 0;
    for (const auto& f : g_v2Pending) chunks += f.chunks.size();
    if (chunks + fa.sealed.size() > V2_PRODUCER_CHUNK_RING) {
        // Retain every submission: the present hop drains the queue, and
        // dropping one here loses payload deltas the page cannot request again.
        if (g_v2OversizeFrames < 3 || (g_v2OversizeFrames % 256) == 0)
            klog_fmt("vk64: v2 producer holds %zu chunks (soft cap %u); keeping all payloads (oversize=%llu)",
                     chunks + fa.sealed.size(), (unsigned)V2_PRODUCER_CHUNK_RING,
                     (unsigned long long)g_v2OversizeFrames);
        g_v2OversizeFrames++;
    }
    V2DoneFrame done;
    done.chunks = std::move(fa.sealed);
    g_v2Pending.push_back(std::move(done));
}

// ---------------------------------------------------------------------------
// BW64_VKPROF frame profiler (additive, env-gated, zero cost when unset).
// Wall-clock breakdown of one guest frame, present-exit to present-exit:
//   wall      frame wall (this present exit minus previous present exit)
//   submit    vkQueueSubmit/2/2KHR handler wall (cmdbuf walk, readGuestRange
//             copies, fnv1a64 hashing, chunk emit, readback hop)
//   bufdata   emitBufferData portion of submit (+ bytes hashed / bytes sent)
//   imgdata   emitImageData portion of submit (+ bytes hashed / bytes sent)
//   trapOther all OTHER vk64 trap handlers (vkCmd* recording, acquire, ...)
//   present   vkQueuePresentKHR handler wall (struct reads + hopV2ToPage)
//   hop       MAIN_THREAD_EM_ASM chunk hops: chunks / bytes (inside present;
//             a readback hop inside submit is also counted here but its wall
//             stays inside submit)
//   guest     wall - submit - trapOther - present  (pure CPU64 interpreter)
// One klog line per presented frame. All state is touched only under g_vkMutex
// (already held by vk64Bridge), so no extra locking. Behavior unchanged when
// BW64_VKPROF is unset: a few emscripten_get_now() calls per trap.
// ---------------------------------------------------------------------------
inline double vkProfNow() {
#ifdef __EMSCRIPTEN__
    return emscripten_get_now();
#else
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
#endif
}
// VKPROF profiler enablement. Emscripten's getenv() cache is populated at
// runtime init before preRun ENV assignments, so ENV["BW64_VKPROF"] is
// ignored. Read the URL param directly via EM_ASM on first call, bypassing
// the environment entirely. The getenv fallback is preserved for native.
// VKPROF profiler enablement. Emscripten's getenv() cache is populated at
// runtime init before preRun ENV assignments, so ENV["BW64_VKPROF"] is
// ignored. Read the URL param directly via EM_ASM on first call, bypassing
// the environment entirely. The getenv fallback is preserved for native.
// BW64_VKPROF force-enable from the page. vkProfEnabled()'s EM_ASM reads
// window.location.search, but the entire runtime executes on a Web Worker
// (-pthread -sPROXY_TO_PTHREAD=1) where window doesn't exist, so the
// ?vkprof=1 URL param is unreadable from C. The launcher (main thread, where
// window.location is valid) calls bw64_vkprof_enable() via ccall during
// startup when the param is present, before the guest boots.
static bool g_vkProfForce = false;
// BW64_VKPROF first-frame gate: bw64_vkprof_enable() is called from the
// launcher onRuntimeInitialized, i.e. during WASM init while the bridge is
// still setting up. Enabling profiling that early races bridge setup and
// stalls the guest after 2 submits (2,649 -> 0 lit px with ?vkprof=1).
// Defer activation until the first vkQueuePresentKHR has fully handed its
// frame to the page; vkProfEnabled() stays false until then.
static bool g_vkProfFirstFrameDone = false;
extern "C"
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void bw64_vkprof_enable() { g_vkProfForce = true; }
// BW64_VKTRACE is not reachable from pthread workers via getenv(), so the
// launcher can force the trap log through this shared global instead.
extern "C"
#ifdef __EMSCRIPTEN__
EMSCRIPTEN_KEEPALIVE
#endif
void bw64_vktrace_enable() { g_traceEnv = 1; g_traceEnvInit = true; }
inline bool vkProfEnabled() {
    if (!g_vkProfFirstFrameDone) return false;
    if (g_vkProfForce) return true;
    static bool checked = false;
    static bool enabled = false;
    if (!checked) {
        checked = true;
#ifdef __EMSCRIPTEN__
        enabled = EM_ASM_INT({
            try { return window.location.search.indexOf("vkprof=1") !== -1 ? 1 : 0; }
            catch (e) { return 0; }
        }) != 0;
#endif
        if (!enabled && std::getenv("BW64_VKPROF") != nullptr) enabled = true;
    }
    return enabled;
}
// Live frame metrics (BW64_METRICS, default ON) reuse these same present-to-
// present timers, but never the per-frame VKPROF klog above: the panel wants
// 2 snapshots/second, not one 7 ms log line per frame. The first-frame gate is
// shared with VKPROF on purpose (enabling timers during WASM init raced bridge
// setup and blanked the canvas); metrics simply start at the second present.
inline bool vkMetricsEnabled() {
    if (!g_vkProfFirstFrameDone) return false;
    return bw64_metrics_enabled() != 0;
}
// True when either consumer needs the per-trap/per-stage timers accumulated.
inline bool vkTimingEnabled() { return vkProfEnabled() || vkMetricsEnabled(); }
struct VkProfAcc {
    bool haveFrame = false;
    U64 frameNo = 0;
    double lastPresentT = 0;
    double accSubmit = 0, accBufData = 0, accImgData = 0, accTrapOther = 0;
    double accHop = 0, accSer2 = 0, accQueue = 0;
    U64 accTrapCalls = 0;
    U64 accHopChunks = 0, accHopBytes = 0;
    U64 accBufHashed = 0, accBufSent = 0, accImgHashed = 0, accImgSent = 0;
    U64 accBufCalls = 0, accImgCalls = 0;
    double trapWall[256] = {}; // per-fnId wall for trapOther top-3 (fnIds < 256)
    U64 trapCnt[256] = {};
};
inline VkProfAcc& vkProfAcc() { static VkProfAcc a; return a; }
inline void vkProfReset(VkProfAcc& a) {
    a.accSubmit = a.accBufData = a.accImgData = a.accTrapOther = a.accHop = 0;
    a.accSer2 = a.accQueue = 0;
    a.accTrapCalls = a.accHopChunks = a.accHopBytes = 0;
    a.accBufHashed = a.accBufSent = a.accImgHashed = a.accImgSent = 0;
    a.accBufCalls = a.accImgCalls = 0;
    for (int i = 0; i < 256; i++) { a.trapWall[i] = 0; a.trapCnt[i] = 0; }
}
// RAII per-trap timer, constructed first in vk64Bridge(): attributes every
// trap's wall to accTrapOther EXCEPT submit/submit2/submit2KHR/present, which
// time themselves at finer granularity below.
struct VkProfTrapTimer {
    U64 fn; double t0;
    explicit VkProfTrapTimer(U64 f) : fn(f), t0(vkProfNow()) {}
    ~VkProfTrapTimer() {
        if (!vkTimingEnabled()) return;
        if (fn == VK64_fn_vkQueueSubmit || fn == VK64_fn_vkQueueSubmit2 ||
            fn == VK64_fn_vkQueueSubmit2KHR || fn == VK64_fn_vkQueuePresentKHR)
            return;
        double dt = vkProfNow() - t0;
        VkProfAcc& a = vkProfAcc();
        a.accTrapOther += dt;
        a.accTrapCalls++;
        if (fn < 256) { a.trapWall[fn] += dt; a.trapCnt[fn]++; }
    }
};
void hopV2ToPage() {
#ifdef __EMSCRIPTEN__
    double vkprofHopT0 = vkProfNow(); U64 vkprofHopChunks = 0, vkprofHopBytes = 0;
    // A present depends on all preceding submissions: scene rendering,
    // postprocessing and the swapchain copy. Payload size is unrelated to
    // which commands are required. The page replays this ordered batch and
    // updates its canvas only after the final submission.
    if (!g_v2Pending.empty()) {
        size_t submission = 0;
        for (const auto& f : g_v2Pending) {
            const U32 moreSubmissions = ++submission < g_v2Pending.size() ? 1 : 0;
            for (const auto& chunk : f.chunks) {
                const U8* ptr = chunk.data();
                U32 len = (U32)chunk.size();
                vkprofHopChunks++; vkprofHopBytes += len;
                MAIN_THREAD_EM_ASM({
                    if (globalThis.window && window.bwVkChunk) {
                        try {
                            // One memcpy: a subarray view is unsafe to retain because
                            // wasm memory growth detaches every live view.
                            // Use HEAPU8 directly (Module.HEAPU8 may not be in scope).
                            const bytes = new Uint8Array(HEAPU8.buffer, $0 >>> 0, $1 >>> 0).slice();
                            window.bwVkChunk(bytes, $2);
                        } catch (e) { console.warn('vk64: bwVkChunk failed', e); }
                    }
                }, ptr, len, moreSubmissions);
            }
        }
    }
    if (vkTimingEnabled()) { VkProfAcc& a = vkProfAcc(); a.accHop += vkProfNow() - vkprofHopT0; a.accHopChunks += vkprofHopChunks; a.accHopBytes += vkprofHopBytes; }
#else
    if (!g_v2Pending.empty())
        klog_fmt("vk64: v2 hop skipped (not an emscripten build): %u queued frames dropped",
                 (unsigned)g_v2Pending.size());
#endif
    g_v2Pending.clear();
}

// FNV-1a 64-bit content hash, shared with the page tier: the IMAGE_DATA
// record carries this hash, so the page keys its texture cache without
// hashing pixel bytes itself.
static U64 fnv1a64(const U8* data, size_t len) {
    U64 h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) { h ^= (U64)data[i]; h *= 1099511628211ULL; }
    return h;
}

// Cross-frame texture content hashes: VkImage id -> FNV-1a 64-bit of the last
// emitted pixel bytes. Image ids are monotonic (g_nextId, never recycled), so
// entries stay valid; vkDestroyImage erases them. When the hash is unchanged,
// emitImageData sends a pixel-less record (view->image link + hash only),
// saving wasm->JS bridge bandwidth for static textures.
static std::unordered_map<U64, U64> g_imageContentHash;
// Cross-frame buffer content hashes: (bufId,memOff,len) -> FNV-1a 64-bit.
// Same dedup as images: when the hash is unchanged, emitBufferData sends a
// byte-less record (ids + hash only), saving bridge bandwidth for static
// vertex/index/uniform buffers. Buffer ids are monotonic (g_nextId), so
// entries stay valid; vkDestroyBuffer erases them.
struct BufHashEntry { U64 hash = 0; U64 dirtyGen = 0; };
static std::map<std::tuple<U64,U64,U64>, BufHashEntry> g_bufferContentHash;
// Page buffers contain the bytes starting at a Vulkan binding offset. Give
// each slice an opaque replay ID so different offsets of one buffer coexist.
static std::map<std::pair<U64,U64>, U64> g_bufferSliceIds;
static U64 bufferSliceId(U64 buffer, U64 offset) {
    if (!offset) return buffer;
    static U64 next = 0xf000000000000000ULL;
    auto [it, inserted] = g_bufferSliceIds.emplace(std::make_pair(buffer, offset), 0);
    if (inserted) it->second = ++next;
    return it->second;
}
// Global generation counter: bumped whenever any buffer is modified via
// vkCmdCopyBuffer/vkCmdUpdateBuffer/vkCmdFillBuffer. Lets emitBufferData skip
// the expensive readGuestRange+hash for static buffers.

// Keep the WebGPU replay format support mirrored in web/vkwebgpu.mjs. These
// lists include Vulkan formats that the page converts to core WebGPU formats.
// BC formats are advertised because textureCompressionBC is enabled below;
// the page maps them only when its adapter exposes texture-compression-bc.
static const U32 kV2SupportedTextureFormats[] = {
    2, 3, 5, 7, 8, 9, 16, 37, 38, 41, 43, 44, 50, 64, 70, 76, 77, 83,
    91, 97, 98, 99, 100, 103, 109, 122, 124, 125, 126, 129, 130,
    131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146
};
static const U32 kV2SupportedVertexFormats[] = {
    16, 17, 18, 19, 20, 21, 23, 24, 25, 26, 27, 28, 37, 38, 39, 40, 41, 42,
    44, 64, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92,
    93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109
};
static const U32 kV2ColorAttachmentFormats[] = {
    2, 3, 5, 7, 8, 9, 16, 37, 41, 43, 44, 50, 64, 70, 76, 77, 83, 91, 97,
    98, 99, 100, 103, 109
};
static const U32 kV2ColorAttachmentBlendFormats[] = {
    2, 3, 5, 7, 8, 9, 16, 37, 43, 44, 50, 64, 70, 76, 77, 83, 91, 97
};
static const U32 kV2DepthStencilFormats[] = { 124, 125, 126, 129, 130 };
template <size_t N>
static bool v2FormatIn(U32 format, const U32 (&formats)[N]) {
    for (U32 supported : formats) if (format == supported) return true;
    return false;
}
static bool v2FormatIsColorAttachment(U32 format) {
    return v2FormatIn(format, kV2ColorAttachmentFormats);
}
static bool v2FormatIsBlendable(U32 format) {
    return v2FormatIn(format, kV2ColorAttachmentBlendFormats);
}
static bool v2FormatIsDepthStencil(U32 format) {
    return v2FormatIn(format, kV2DepthStencilFormats);
}
static VkFormatFeatureFlags v2FormatFeatureFlags(U32 format) {
    if (!v2FormatIn(format, kV2SupportedTextureFormats)) return 0;
    VkFormatFeatureFlags flags = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (v2FormatIsColorAttachment(format)) flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (v2FormatIsBlendable(format)) flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    if (v2FormatIsDepthStencil(format)) flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    return flags;
}
static VkFormatFeatureFlags v2FormatBufferFeatureFlags(U32 format) {
    VkFormatFeatureFlags flags = v2FormatIn(format, kV2SupportedVertexFormats) ? VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT : 0;
    return flags;
}
static VkFormatFeatureFlags2 v2FormatFeatureFlags2(U32 format) {
    if (!v2FormatIn(format, kV2SupportedTextureFormats)) return 0;
    // These legacy aliases use the same bit values as VkFormatFeatureFlags2.
    VkFormatFeatureFlags2 flags = VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
        VK_FORMAT_FEATURE_TRANSFER_SRC_BIT | VK_FORMAT_FEATURE_TRANSFER_DST_BIT;
    if (v2FormatIsColorAttachment(format)) flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT;
    if (v2FormatIsBlendable(format)) flags |= VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BLEND_BIT;
    if (v2FormatIsDepthStencil(format)) flags |= VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT;
    return flags;
}
static VkFormatFeatureFlags2 v2FormatBufferFeatureFlags2(U32 format) {
    return (VkFormatFeatureFlags2)v2FormatBufferFeatureFlags(format);   // the low 32 bits share their values
}
// Byte size of one vertex of a VkFormat the v2 emitter may see. Only the
// formats DXVK/D3D9 and the probes produce are listed; an unknown format falls
// back to 16 (conservative: keeps the clamp above the real value).
U32 v2VertexFormatBytes(U32 fmt) {
    switch (fmt) {
        case VK_FORMAT_R32_SFLOAT: case VK_FORMAT_R32_UINT: case VK_FORMAT_R32_SINT: return 4;
        case VK_FORMAT_R32G32_SFLOAT: case VK_FORMAT_R32G32_UINT: case VK_FORMAT_R32G32_SINT:
        case VK_FORMAT_R16G16B16A16_SFLOAT: case VK_FORMAT_R16G16B16A16_UNORM:
        case VK_FORMAT_R16G16B16A16_SNORM: case VK_FORMAT_R16G16B16A16_USCALED:
        case VK_FORMAT_R16G16B16A16_SSCALED: case VK_FORMAT_R16G16B16A16_UINT:
        case VK_FORMAT_R16G16B16A16_SINT: return 8;
        case VK_FORMAT_R16G16_UNORM: case VK_FORMAT_R16G16_SNORM:
        case VK_FORMAT_R16G16_USCALED: case VK_FORMAT_R16G16_SSCALED:
        case VK_FORMAT_R16G16_UINT: case VK_FORMAT_R16G16_SINT: case VK_FORMAT_R16G16_SFLOAT: return 4;
        case VK_FORMAT_R8G8_UNORM: case VK_FORMAT_R8G8_SNORM: case VK_FORMAT_R8G8_USCALED:
        case VK_FORMAT_R8G8_SSCALED: case VK_FORMAT_R8G8_UINT: case VK_FORMAT_R8G8_SINT: return 2;
        case VK_FORMAT_R32G32B32_SFLOAT: case VK_FORMAT_R32G32B32_UINT: case VK_FORMAT_R32G32B32_SINT: return 12;
        case VK_FORMAT_R16G16B16_UNORM: case VK_FORMAT_R16G16B16_SNORM:
        case VK_FORMAT_R16G16B16_USCALED: case VK_FORMAT_R16G16B16_SSCALED:
        case VK_FORMAT_R16G16B16_UINT: case VK_FORMAT_R16G16B16_SINT: case VK_FORMAT_R16G16B16_SFLOAT: return 6;
        case VK_FORMAT_R8G8B8_UNORM: case VK_FORMAT_R8G8B8_SNORM: case VK_FORMAT_R8G8B8_USCALED:
        case VK_FORMAT_R8G8B8_SSCALED: case VK_FORMAT_R8G8B8_UINT: case VK_FORMAT_R8G8B8_SINT: return 3;
        case VK_FORMAT_R32G32B32A32_SFLOAT: case VK_FORMAT_R32G32B32A32_UINT:
        case VK_FORMAT_R32G32B32A32_SINT: return 16;
        case VK_FORMAT_R8G8B8A8_UNORM: case VK_FORMAT_B8G8R8A8_UNORM:
        case VK_FORMAT_R8G8B8A8_SNORM: case VK_FORMAT_R8G8B8A8_USCALED: case VK_FORMAT_R8G8B8A8_SSCALED:
        case VK_FORMAT_R8G8B8A8_UINT: case VK_FORMAT_R8G8B8A8_SINT:
        case VK_FORMAT_R8G8B8A8_SRGB: case VK_FORMAT_B8G8R8A8_SRGB:
        case VK_FORMAT_A2B10G10R10_UNORM_PACK32: return 4;
        case VK_FORMAT_R8_UNORM: return 1;
        default: return 16;
    }
}

// Submit-time v2 frame reconstruction: the same walk as serializeSubmit, but
// the frame is a stream of binary records instead of one JSON string, and it
// covers the DXVK-shaped calls v1 never modelled (descriptor sets with arrays,
// indexed draws, vertex/index binds, per-attachment MSAA, the render-pass
// graph). One v2 frame per submit-with-draw, queued for the present hop.
struct ReadbackPending {
    U64 srcImage = 0, dstBuffer = 0, dstBufOff = 0;
    U32 w = 0, h = 0, heapPtr = 0, flagPtr = 0;
};
static std::vector<ReadbackPending> g_readbackPending;

static bool waitForReadbackFlag(U32 flagPtr, int timeoutMs) {
#ifdef __EMSCRIPTEN__
    int r = EM_ASM_INT({
        try {
            var view = new Int32Array(HEAPU8.buffer, $0 >>> 0, 1);
            var res = Atomics.wait(view, 0, 0, $1);
            return res === 'ok' ? 1 : 0;
        } catch (e) { return -1; }
    }, flagPtr, timeoutMs);
    return r == 1;
#else
    (void)flagPtr; (void)timeoutMs;
    return false;
#endif
}

static void hopV2LastToPage() {
#ifdef __EMSCRIPTEN__
    if (g_v2Pending.empty()) return;
    MAIN_THREAD_EM_ASM({ try { globalThis.bwVkHeapBuffer = HEAPU8.buffer; } catch (e) {} });
    const auto& f = g_v2Pending.back();
    for (const auto& chunk : f.chunks) {
        const U8* ptr = chunk.data();
        U32 len = (U32)chunk.size();
        MAIN_THREAD_EM_ASM({
            if (globalThis.window && window.bwVkChunk) {
                try {
                    const bytes = new Uint8Array(HEAPU8.buffer, $0 >>> 0, $1 >>> 0).slice();
                    window.bwVkChunk(bytes, $2);
                } catch (e) {}
            }
        }, ptr, len, (U32)0);
    }
    g_v2Pending.clear();
#else
    g_v2Pending.clear();
#endif
}

static bool setupReadbacks(const std::vector<std::pair<CmdBuf*, U64>>& frames) {
    g_readbackPending.clear();
    for (const auto& fr : frames) {
        CmdBuf* cb = fr.first;
        VK64_DROP_IF(continue, !cb);
        for (const Cmd& k : cb->cmds) {
            if (k.kind != CMD_COPY_I2B) continue;
            const CopyI2B& rb = k.copyI2B;
            VK64_DROP_IF(continue, !rb.w || !rb.h || rb.w > 4096 || rb.h > 4096);
            size_t pxBytes = (size_t)rb.w * rb.h * 4;
            U8* heap = (U8*)malloc(pxBytes + 4);
            VK64_DROP_IF(continue, !heap);
            memset(heap, 0, pxBytes + 4);
            ReadbackPending pend;
            pend.srcImage = rb.srcImage; pend.dstBuffer = rb.dstBuffer;
            pend.dstBufOff = rb.bufOff; pend.w = rb.w; pend.h = rb.h;
            pend.heapPtr = (U32)(uintptr_t)heap;
            pend.flagPtr = (U32)(uintptr_t)(heap + pxBytes);
            g_readbackPending.push_back(pend);
        }
    }
    return !g_readbackPending.empty();
}

static void completeReadbacks() {
    if (g_readbackPending.empty()) return;
    hopV2LastToPage();
    for (auto& pend : g_readbackPending) {
        VK64_DROP_IF(continue, !waitForReadbackFlag(pend.flagPtr, 10000));
        Buffer* buf = objOf<Buffer>(pend.dstBuffer, K_BUFFER, "readback");
        VK64_DROP_IF(continue, !buf || !buf->mem);
        DeviceMemory* mem = objOf<DeviceMemory>(buf->mem, K_MEMORY, "readback");
        VK64_DROP_IF(continue, !mem || !mem->va);
        size_t pxBytes = (size_t)pend.w * pend.h * 4;
        U64 dstVa = mem->va + buf->memOff + pend.dstBufOff;
        if (buf->memOff + pend.dstBufOff + pxBytes > mem->size) {
            VK64_DROP_IF(continue, buf->memOff + pend.dstBufOff >= mem->size);
            pxBytes = (size_t)(mem->size - buf->memOff - pend.dstBufOff);
        }
        writeGuestBytes(dstVa, (U8*)(uintptr_t)pend.heapPtr, pxBytes);
    }
    for (auto& pend : g_readbackPending) free((void*)(uintptr_t)pend.heapPtr);
    g_readbackPending.clear();
}


void serializeSubmitV2(const std::vector<std::pair<CmdBuf*, U64>>& frames) {
    double vkprofSer2T0 = vkProfNow(); // BW64_VKPROF (additive)
    // BW64_VKPROF accumulators (additive): time/bytes inside the two hot emit lambdas.
    double vkprofBufT = 0, vkprofImgT = 0;
    U64 vkprofBufHashed = 0, vkprofBufSent = 0, vkprofImgHashed = 0, vkprofImgSent = 0;
    U64 vkprofBufCalls = 0, vkprofImgCalls = 0;
    for (const auto& fr : frames) {
        CmdBuf* st = fr.first;
        U64 fbId = 0;
        for (const Cmd& k : st->cmds) if (k.kind == CMD_BEGIN_RP) { fbId = k.b; break; }
        Framebuffer* fb = objOf<Framebuffer>(fbId, K_FRAMEBUFFER, "vkQueueSubmit");
        if (!fb) {
            // No render pass: a pure transfer/compute command buffer (vkd3d-proton
            // copy queues, mip generation, depth clears). The decoder replays
            // these in an implicit pass, so they are sent as a 1x1 frame.
            // Command buffers with nothing the page can execute are legitimately
            // empty (barriers only) and stay skipped.
            bool relevant = false;
            for (const Cmd& k : st->cmds)
                if (k.kind == CMD_COPY_IMAGE || k.kind == CMD_BLIT_IMAGE || k.kind == CMD_CLEAR_DS_IMAGE ||
                    k.kind == CMD_COPY_I2B || k.kind == CMD_DISPATCH) { relevant = true; break; }
            if (!relevant) continue;
        }
        // A render-pass load clear changes its attachment without a draw.
        // Preserve it for later submissions that sample the cleared image.
        g_v2SubmitFrames++;

        V2FrameAsm fa;
        U32 hist[32] = {0};
        if (vkDumpV2()) fa.hist = hist;
        fa.openChunk(g_v2ChunkSeq++);

        auto emit = [&](U32 type, auto&& fill) {
            ChunkWriter p;
            fill(p);
            fa.emitRec(type, p.b, g_v2ChunkSeq);
        };

        // Per-frame dedup: a set bound twice or a buffer referenced by ten
        // draws crosses once.
        std::set<U64> shaderDone, samplerDone, imageDone;
        std::set<std::tuple<U64,U64,U64>> bufDone;

        auto emitShader = [&](ShaderModule* sm, U32 stage) {
            if (!sm || !sm->hash || !shaderDone.insert(sm->hash).second) return;
            emit(V2_SHADER, [&](ChunkWriter& p) {
                p.u8(stage); p.u8(0); p.u16(0);
                p.u64(sm->hash); p.u32((U32)sm->code.size()); p.bytes(sm->code);
            });
        };
        auto emitBufferDataInner = [&](U64 bufId, U64 memOff, U64 len, U64 replayId) {
            if (!bufId || !len) return;
            if (!bufDone.insert(std::make_tuple(bufId, memOff, len)).second) return;
            Buffer* b = objOf<Buffer>(bufId, K_BUFFER, "vkQueueSubmit");
            VK64_DROP_IF(return, !b || !b->mem);
            auto key = std::make_tuple(bufId, memOff, len);
            auto it = g_bufferContentHash.find(key);
            // Huge buffers: vkd3d-proton backs small resources with heap-sized
            // VkBuffers (a 16MB VkBuffer for an 84-byte VB). The page rejects
            // any chunk over 16MB, so cap the emitted prefix at 1MB — the draw
            // only touches the prefix. Dedup hash covers the emitted bytes.
            const bool tooLarge = len > V2_MAX_BUF_SEND;
            const U64 sendLen = tooLarge ? V2_MAX_BUF_SEND : len;
            // Periodic full resend: heals any bridge/page desync from a
            // dropped or rejected chunk. The first submitted frames always
            // send full bytes so startup never depends on a chunk the page
            // may have dropped. (g_frameNo counts presents since boot, which
            // is useless here; g_v2SubmitFrames counts real submits.)
            const bool forceFull =
                (g_v2SubmitFrames <= 3 || (g_v2SubmitFrames % V2_BUF_RESEND_PERIOD) == 0);
            // Fast path: buffer was hashed before and hasn't been modified
            // via any Vulkan command since (dirtyGen unchanged) and its
            // memory was never mapped (no CPU writes possible). Skip the
            // expensive readGuestRange + fnv1a64 entirely.
            DeviceMemory* bmem = objOf<DeviceMemory>(b->mem, K_MEMORY, "vkQueueSubmit");
            if (!forceFull && it != g_bufferContentHash.end() && it->second.dirtyGen == b->dirtyGen &&
                b->dirtyGen != 0 && bmem && !bmem->wasMapped) {
                emit(V2_BUFFER_DATA, [&](ChunkWriter& p) {
                    p.u64(replayId); p.u64(memOff); p.u64(it->second.hash);
                    p.u32(0);
                });
                return;
            }
            std::vector<U8> bytes = readGuestRange(b->mem, memOff, sendLen);
            if (bytes.empty()) return;
            // Cross-frame dedup: resend bytes only when content changed.
            U64 hash = fnv1a64(bytes.data(), bytes.size());
            bool sendBytes = true;
            if (!forceFull && it != g_bufferContentHash.end() && it->second.hash == hash) {
                sendBytes = false;
            } else {
            }
            if (tooLarge) {
                klog_fmt("vk64: BUFFER_DATA capped id=0x%llx len=%llu -> %llu bytes (vkd3d heap-sized buffer; page rejects chunks over 16MB)", (unsigned long long)bufId, (unsigned long long)len, (unsigned long long)sendLen);
            }
            g_bufferContentHash[key] = {hash, b->dirtyGen};
            emit(V2_BUFFER_DATA, [&](ChunkWriter& p) {
                p.u64(replayId); p.u64(memOff); p.u64(hash);
                p.u32(sendBytes ? (U32)bytes.size() : 0);
                if (sendBytes) p.bytes(bytes);
            });
            if (vkTimingEnabled()) { vkprofBufHashed += bytes.size(); if (sendBytes) vkprofBufSent += bytes.size(); }
        };
        // BW64_VKPROF wrapper: wall time per emitBufferData call (additive).
        auto emitBufferData = [&](U64 bufId, U64 memOff, U64 len, U64 replayId = 0) {
            double t0 = vkProfNow();
            ++vkprofBufCalls;
            emitBufferDataInner(bufId, memOff, len, replayId ? replayId : bufId);
            vkprofBufT += vkProfNow() - t0;
        };
        auto emitSampler = [&](U64 id) {
            if (!id || !samplerDone.insert(id).second) return;
            Sampler* s = objOf<Sampler>(id, K_SAMPLER, "vkQueueSubmit");
            VK64_DROP_IF(return, !s);
            emit(V2_SAMPLER, [&](ChunkWriter& p) {
                writeSamplerRecordPayload(p, id, *s);
            });
        };
        auto emitImageDataInner = [&](U64 viewId) {
            ImageView* view = objOf<ImageView>(viewId, K_IMAGEVIEW, "vkQueueSubmit");
            VK64_DROP_IF(return, !view || !view->image);
            // Descriptor bindings name views. Deduplicating by image drops
            // the view->image link for every other view of the same image.
            if (!imageDone.insert(viewId).second) return;
            Image* tex = objOf<Image>(view->image, K_IMAGE, "vkQueueSubmit");
            VK64_DROP_IF(return, !tex);
            std::vector<U8> bytes;
            if (tex->mem && tex->pixBytes) bytes = readGuestRange(tex->mem, tex->memOff, tex->pixBytes);
            // Cross-frame dedup: resend pixel bytes only when content changed.
            U64 hash = 0;
            bool sendPixels = true;
            if (!bytes.empty()) {
                hash = fnv1a64(bytes.data(), bytes.size());
                auto it = g_imageContentHash.find(view->image);
                if (it != g_imageContentHash.end() && it->second == hash &&
                    g_v2SubmitFrames > 3 && (g_v2SubmitFrames % V2_BUF_RESEND_PERIOD) != 0) {
                    sendPixels = false;
                } else {
                    g_imageContentHash[view->image] = hash;
                }
            }
            emit(V2_IMAGE_DATA, [&](ChunkWriter& p) {
                // viewId first: the page's DESC_SET bindings name image VIEWS,
                // while pixel content is deduped per image, so the record carries
                // the view->image link the page needs.
                // Sampling interprets the view's format, which may be the sRGB
                // alias of the image's linear storage. The bytes are identical.
                p.u64(viewId); p.u64(view->image); p.u32(tex->w); p.u32(tex->h); p.u32(view->format ? view->format : tex->format);
                p.u64(hash);
                p.u32(sendPixels ? (U32)bytes.size() : 0);
                if (sendPixels) p.bytes(bytes);
                p.u32(std::max(1U, tex->d));
                p.u32(std::max(1U, tex->layers));
                p.u32(std::max(1U, tex->mips));
            });
            if (vkTimingEnabled()) { vkprofImgHashed += bytes.size(); if (sendPixels) vkprofImgSent += bytes.size(); }
        };
        // BW64_VKPROF wrapper: wall time per emitImageData call (additive).
        auto emitImageData = [&](U64 viewId) {
            double t0 = vkProfNow();
            ++vkprofImgCalls;
            emitImageDataInner(viewId);
            vkprofImgT += vkProfNow() - t0;
        };
        auto emitDescSet = [&](U32 setIndex, U64 setId) {
            DescSet* s = objOf<DescSet>(setId, K_SET, "vkQueueSubmit");
            VK64_DROP_IF(return, !s);
            emit(V2_DESC_SET, [&](ChunkWriter& p) {
                p.u8(setIndex); p.u8(s->nbind); p.u16(0); p.u64(s->layout);
                for (U32 i = 0; i < s->nbind; i++) {
                    const Binding& b = s->binds[i];
                    U32 kind = 0;
                    switch (b.type) {
                        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER: kind = 1; break;
                        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
                        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER: kind = 2; break;
                        case VK_DESCRIPTOR_TYPE_SAMPLER: kind = 3; break;
                        case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK: kind = 4; break;
                        default: break;
                    }
                    // Absolute byte offset of this binding's range, matching
                    // V2_BUFFER_DATA's memOff. One VkBuffer may back several
                    // uniform bindings at different offsets (DXVK sub-allocates
                    // D3D9 constants); the page keys captures by (id, offset).
                    U64 absOff = 0;
                    if (kind == 1) {
                        Buffer* bbuf = objOf<Buffer>(b.obj, K_BUFFER, "vkQueueSubmit");
                        if (bbuf) absOff = bbuf->memOff + b.bufOff;
                    }
                    p.u32(b.binding); p.u32(b.type); p.u32(b.dstArrayElement); p.u32(b.count);
                    p.u8(kind); p.u8(0); p.u16(0);
                    p.u64(b.obj); p.u64(b.range); p.u64(absOff);
                }
            });
            for (U32 i = 0; i < s->nbind; i++) {
                const Binding& b = s->binds[i];
                if (b.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                    b.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                    Buffer* buf = objOf<Buffer>(b.obj, K_BUFFER, "vkQueueSubmit");
                    if (buf) emitBufferData(b.obj, buf->memOff + b.bufOff, b.range ? b.range : buf->size);
                } else if (b.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                           b.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
                           b.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    emitImageData(b.obj);    // b.obj = image view id (see vkUpdateDescriptorSets)
                    emitSampler(b.range);    // b.range = sampler id
                } else if (b.type == VK_DESCRIPTOR_TYPE_SAMPLER) {
                    emitSampler(b.obj);
                } else if (b.type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
                    if (!b.inlineBytes.empty()) {
                        emit(V2_INLINE_BYTES, [&](ChunkWriter& p) {
                            p.u8(setIndex); p.u8(0); p.u16(0);
                            p.u32(b.binding); p.u32(0); p.u32((U32)b.inlineBytes.size());
                            p.bytes(b.inlineBytes);
                        });
                    }
                }
            }
        };
        U64 activePipe = 0;
        U32 vertexStrides[VK64_MAX_VERT_BINDINGS] = {0};
        bool vertexHasStride[VK64_MAX_VERT_BINDINGS] = {false};
        auto emitPipeline = [&](U64 pipeId) {
            Pipeline* pipe = objOf<Pipeline>(pipeId, K_PIPELINE, "vkQueueSubmit");
            VK64_DROP_IF(return, !pipe);
            ShaderModule* vs = pipe->vs ? objOf<ShaderModule>(pipe->vs, K_SHADER, "vkQueueSubmit") : nullptr;
            ShaderModule* fs = pipe->fs ? objOf<ShaderModule>(pipe->fs, K_SHADER, "vkQueueSubmit") : nullptr;
            // Schema v2 replays the guest pipeline. The legacy procedural
            // capture only retains positions and colors, losing textures and
            // changing indexed draws into nonindexed draws.
            emitShader(vs, 0);
            emitShader(fs, 1);
            U64 serializedId = pipeId;
            std::vector<U32> effectiveStrides;
            for (U32 i = 0; i < pipe->nvb; ++i)
                effectiveStrides.push_back(pipe->dynamicVertexStride && vertexHasStride[i]
                    ? vertexStrides[i] : pipe->vbStride[i]);
            if (pipe->dynamicVertexStride) {
                auto variant = std::find_if(pipe->vertexStrideVariants.begin(), pipe->vertexStrideVariants.end(),
                    [&](const Pipeline::VertexStrideVariant& v) { return v.strides == effectiveStrides; });
                if (variant == pipe->vertexStrideVariants.end()) {
                    serializedId = g_nextId++; // Shared allocator prevents collisions with guest handles.
                    pipe->vertexStrideVariants.push_back({effectiveStrides, serializedId});
                } else serializedId = variant->id;
            }
            // WebGPU: pipeline depth state must match render pass attachments.
            // The JS side (getPipelineFor passHasDepth) handles validation.
            // Do NOT mask by fbHasDepth: V2 must carry the pipeline's true
            // depth intent. Masking causes pipelines needing depth (e.g.
            // indexed quads, depthOp=less-equal) to get 'always' and vanish.
            // (2026-10-09: fix effDepthTest masking bug)
            bool fbHasDepth = false;
            if (fb) {
                for (U32 i = 0; i < fb->natt; i++) {
                    ImageView* v = objOf<ImageView>(fb->views[i], K_IMAGEVIEW, "vkQueueSubmit");
                    if (v && (v->aspect & 0x6u)) { fbHasDepth = true; break; }
                }
            }
            (void)fbHasDepth; // V2 carries true intent; JS handles pass matching.
            U32 effDepthTest = pipe->depthTest;
            U32 effDepthWrite = pipe->depthWrite;
            emit(V2_PIPELINE, [&](ChunkWriter& p) {
                p.u64(serializedId);
                U64 vsHash = vs ? vs->hash : 0;
                U64 fsHash = fs ? fs->hash : 0;
                p.u64(vsHash); p.u64(fsHash);
                p.u32(pipe->topology); p.u32(pipe->cull); p.u32(pipe->front);
                p.u32(effDepthTest); p.u32(effDepthWrite); p.u32(pipe->depthOp);
                p.u32(pipe->blend);
                p.u32(pipe->nvb); p.u32(pipe->nva);
                for (U32 i = 0; i < pipe->nvb; i++) {
                    p.u32(i); p.u32(effectiveStrides[i]); p.u32(pipe->vbInputRate[i]);
                }
                for (U32 i = 0; i < pipe->nva; i++) {
                    p.u32(pipe->vaLocation[i]); p.u32(pipe->vaBinding[i]);
                    p.u32(pipe->vaFormat[i]); p.u32(pipe->vaOffset[i]);
                }
                // Append-only specialization tail (2026-10-06): per-stage
                // (constantID, size, value bytes). Decoders that predate this
                // tail stop at the va list; new decoders read it if present.
                auto emitSpec = [&](const StageSpec& s) {
                    p.u32(s.n);
                    for (U32 i = 0; i < s.n; i++) {
                        p.u32(s.entries[i].constantID);
                        p.u32(s.entries[i].size);
                        p.bytes(s.entries[i].value, s.entries[i].size);
                    }
                };
                emitSpec(pipe->vsSpec);
                emitSpec(pipe->fsSpec);
                emitColorBlend(*pipe, p);
                emitStencil(*pipe, p);
                emitDepthBias(p, pipe->depthBiasEnable, pipe->depthClampEnable, pipe->depthBiasDynamic ? 1 : 0,
                              pipe->depthBiasConstantFactor, pipe->depthBiasClamp, pipe->depthBiasSlopeFactor);
                p.u32(0x314e4146u); // FAN1: indexed triangle-fan primitive-restart state
                p.u32(pipe->primitiveRestartEnable);
                p.u32(0x31545352u); // RST1: which rasterizer state is dynamic (cull mode, front face)
                p.u32(pipe->cullDynamic ? 1 : 0); p.u32(pipe->frontDynamic ? 1 : 0);
            });
        };
        auto emitRpBegin = [&](const Cmd& k) {
            Framebuffer* rfb = objOf<Framebuffer>(k.b, K_FRAMEBUFFER, "vkQueueSubmit");
            RenderPass* rrp = rfb ? objOf<RenderPass>(rfb->rp, K_RENDERPASS, "vkQueueSubmit") : nullptr;
            VK64_DROP_IF(return, !rfb || !rrp);
            emit(V2_RP_BEGIN, [&](ChunkWriter& p) {
                p.u64(k.a); p.u64(k.b); p.u32(rfb->w); p.u32(rfb->h); p.u32(rfb->natt);
                for (U32 i = 0; i < rfb->natt; i++) {
                    ImageView* v = objOf<ImageView>(rfb->views[i], K_IMAGEVIEW, "vkQueueSubmit");
                    p.u32(rrp->formats[i]); p.u32(rrp->loadOps[i]); p.u32(rrp->storeOps[i]);
                    p.u32(rrp->samples[i] ? rrp->samples[i] : 1);
                    p.u32(v && (v->aspect & 0x6u) ? 1 : 0);
                }
                p.f32(k.f[0]); p.f32(k.f[1]); p.f32(k.f[2]); p.f32(k.f[3]);
                p.f32((float)k.clearDepth); p.u32(k.clearStencil);
                // Optional attachment identity tail, paired with the updated
                // decoder. Image identity links render and sampled view aliases.
                for (U32 i = 0; i < rfb->natt; ++i) {
                    auto* view = objOf<ImageView>(rfb->views[i], K_IMAGEVIEW, "vkQueueSubmit");
                    p.u64(rfb->views[i]); p.u64(view ? view->image : 0);
                }
                p.u32(0x31505453u); p.u32(rfb->natt); // STP1
                for (U32 i = 0; i < rfb->natt; ++i) {
                    auto* view = objOf<ImageView>(rfb->views[i], K_IMAGEVIEW, "vkQueueSubmit");
                    p.u32(rrp->stencilLoadOps[i]); p.u32(rrp->stencilStoreOps[i]); p.u32(view ? view->aspect : 0);
                }
            });
        };

        // FRAME_BEGIN. The overflow flag is consumed here, like v1.
        bool ovf = g_frameOverflow;
        g_frameOverflow = false;
        emit(V2_FRAME_BEGIN, [&](ChunkWriter& p) {
            p.u32(g_frameNo); p.u32(fb ? fb->w : 1); p.u32(fb ? fb->h : 1); p.u32(ovf ? 1 : 0);
        });

        U32 nDraw = 0, nPush = 0;
        // Determine the byte ranges the original draws touch before emitting
        // bound DXVK staging buffers, which can be much larger than live data.
        U64 v2MaxVbUse[VK64_MAX_VERT_BINDINGS] = {0};
        U64 v2MaxIbUse = 0;
        {
            U64 curPipe = 0;
            U64 curIb = 0, curIbOff = 0;
            U32 curIbType = 0;
            U32 curStride[VK64_MAX_VERT_BINDINGS] = {0};
            bool curHasStride[VK64_MAX_VERT_BINDINGS] = {false};
            for (const Cmd& k2 : st->cmds) {
                if (k2.kind == CMD_BIND_PIPE) {
                    curPipe = k2.a;
                } else if (k2.kind == CMD_BIND_IB) {
                    curIb = k2.a; curIbOff = k2.b; curIbType = (U32)k2.c;
                } else if (k2.kind == CMD_BIND_VB) {
                    for (U32 i = 0; i < k2.vbCount && i < VK64_MAX_VERT_BINDINGS; i++) {
                        curStride[i] = k2.vbStrides[i];
                        curHasStride[i] = k2.vbHasStride[i];
                    }
                } else if (k2.kind == CMD_DRAW_INDEXED || k2.kind == CMD_DRAW) {
                    U64 maxVertex = 0;
                    if (k2.kind == CMD_DRAW) {
                        maxVertex = (U64)k2.c + (k2.a ? k2.a - 1 : 0);
                    } else if (k2.a) {
                        // Highest vertex id referenced by the index range.
                        U32 idxSize = 2; // read from the bound index buffer below
                        Buffer* ib = objOf<Buffer>(curIb, K_BUFFER, "v2use");
                        if (ib && curIbType == 1) idxSize = 4;
                        if (ib && ib->mem) {
                            std::vector<U8> idx = readGuestRange(ib->mem,
                                ib->memOff + curIbOff + (U64)k2.c * idxSize,
                                (U64)k2.a * idxSize);
                            if (idx.size() >= (size_t)k2.a * idxSize) {
                                for (U32 i = 0; i < k2.a; i++) {
                                    U64 ix = (idxSize == 4)
                                        ? (U64)(idx[i*4] | ((U64)idx[i*4+1] << 8) |
                                                ((U64)idx[i*4+2] << 16) | ((U64)idx[i*4+3] << 24))
                                        : (U64)(idx[i*2] | ((U64)idx[i*2+1] << 8));
                                    if (ix > maxVertex) maxVertex = ix;
                                }
                                v2MaxIbUse = std::max(v2MaxIbUse, ((U64)k2.c + k2.a) * idxSize);
                            }
                        }
                        if (k2.a && maxVertex == 0) maxVertex = k2.a - 1;
                        // vertexOffset shifts every index; keep it unsigned-safe.
                        if (k2.diVertexOffset > 0) maxVertex += (U64)k2.diVertexOffset;
                    }
                    Pipeline* pipe = curPipe ? objOf<Pipeline>(curPipe, K_PIPELINE, "v2use") : nullptr;
                    if (pipe) {
                        for (U32 i = 0; i < pipe->nva && i < VK64_MAX_VERT_ATTRIBS; i++) {
                            U32 slot = pipe->vaBinding[i];
                            VK64_DROP_IF(continue, slot >= VK64_MAX_VERT_BINDINGS);
                            U32 stride = pipe->dynamicVertexStride && curHasStride[slot] && curStride[slot]
                                ? curStride[slot] : pipe->vbStride[slot];
                            if (!stride) continue;
                            U64 end = (maxVertex + 1) * stride +
                                (U64)pipe->vaOffset[i] + (U64)v2VertexFormatBytes(pipe->vaFormat[i]);
                            if (end > v2MaxVbUse[slot]) v2MaxVbUse[slot] = end;
                        }
                    }

                }
            }
        }
        for (const Cmd& k : st->cmds) {
            switch (k.kind) {
                case CMD_BEGIN_RP: emitRpBegin(k); break;
                case CMD_END_RP:   emit(V2_RP_END, [](ChunkWriter&) {}); break;
                case CMD_BIND_PIPE: activePipe = k.a; emitPipeline(k.a); break;
                case CMD_BIND_SETS: {
                    U32 first = (U32)k.a, count = (U32)k.b;
                    VK64_DROP_IF(break, first >= VK64_MAX_SETS);
                    if (first + count > VK64_MAX_SETS) count = VK64_MAX_SETS - first;
                    emit(V2_BIND_SETS, [&](ChunkWriter& p) {
                        p.u8(first); p.u8(count); p.u16(0);
                        for (U32 i = 0; i < count; i++) p.u64(k.sets[first + i]);
                    });
                    for (U32 i = 0; i < count; i++)
                        if (k.sets[first + i]) emitDescSet(first + i, k.sets[first + i]);
                    break;
                }
                case CMD_STENCIL_STATE:
                    emit(V2_STENCIL_STATE, [&](ChunkWriter& p) { p.u32(k.a); p.u32(k.b); p.u32(k.c); });
                    break;
                case CMD_BLEND_CONSTANTS:
                    emit(V2_BLEND_CONSTANTS, [&](ChunkWriter& p) { for (int i = 0; i < 4; ++i) p.f32(k.f[i]); });
                    break;
                case CMD_DEPTH_BIAS:
                    emit(V2_DEPTH_BIAS, [&](ChunkWriter& p) { p.f32(k.f[0]); p.f32(k.f[1]); p.f32(k.f[2]); });
                    break;
                case CMD_RASTER_STATE:
                    emit(V2_RASTER_STATE, [&](ChunkWriter& p) { p.u32((U32)k.a); p.u32((U32)k.b); });
                    break;
                case CMD_CLEAR_DS_IMAGE:
                    emit(V2_IMAGE_CLEAR_DS, [&](ChunkWriter& p) {
                        p.u64(k.a); p.f32(k.f[0]); p.u32(k.clearStencil);
                        p.u32((U32)k.clearRanges.size());
                        for (const auto& r : k.clearRanges) {
                            p.u32(r.aspectMask); p.u32(r.baseMip); p.u32(r.levelCount); p.u32(r.baseLayer); p.u32(r.layerCount);
                        }
                    });
                    break;
                case CMD_CLEAR_ATTACHMENTS:
                    emit(V2_CLEAR_ATTACHMENTS, [&](ChunkWriter& p) {
                        p.u32((U32)k.clearAttachments.size());
                        for (const auto& a : k.clearAttachments) {
                            p.u32(a.aspectMask); p.u32(a.colorAttachment);
                            for (U32 value : a.value) p.u32(value);
                            float depth = 1.0f;
                            if (a.aspectMask & 2) memcpy(&depth, a.value, 4);
                            p.f32(depth); p.u32(a.aspectMask & 4 ? a.value[1] : 0);
                        }
                        p.u32((U32)k.clearRects.size());
                        for (const auto& r : k.clearRects) {
                            p.u32((U32)r.x); p.u32((U32)r.y); p.u32(r.w); p.u32(r.h); p.u32(r.baseLayer); p.u32(r.layers);
                        }
                    });
                    break;
                case CMD_VIEWPORT:
                    emit(V2_VIEWPORT, [&](ChunkWriter& p) {
                        for (int i = 0; i < 6; i++) p.f32(k.f[i]);
                    });
                    break;
                case CMD_SCISSOR: {
                    // WebGPU: scissor must be contained in render area. Clamp to FB dims.
                    int32_t sx = (int32_t)k.f[0], sy = (int32_t)k.f[1];
                    int32_t sw = (int32_t)k.f[2], sh = (int32_t)k.f[3];
                    if (sx < 0) { sw += sx; sx = 0; }
                    if (sy < 0) { sh += sy; sy = 0; }
                    if (fb) {
                        if (sx + sw > (int32_t)fb->w) sw = (int32_t)fb->w - sx;
                        if (sy + sh > (int32_t)fb->h) sh = (int32_t)fb->h - sy;
                    }
                    if (sw < 0) sw = 0; if (sh < 0) sh = 0;
                    emit(V2_SCISSOR, [&](ChunkWriter& p) {
                        p.u32((U32)sx); p.u32((U32)sy);
                        p.u32((U32)sw); p.u32((U32)sh);
                    });
                    break;
                }
                case CMD_PUSH_CONST:
                    nPush++;
                    emit(V2_PUSH, [&](ChunkWriter& p) {
                        p.u32(k.pcOffset); p.u32(k.pcSize); p.u32(k.pcStageFlags);
                        p.bytes(k.pcBytes, k.pcSize);
                    });
                    break;
                case CMD_BIND_VB: {
                    emit(V2_VERTEX_BIND, [&](ChunkWriter& p) {
                        p.u8(k.vbCount); p.u8(0); p.u16(0);
                        for (U32 i = 0; i < k.vbCount; i++) {
                            p.u32(i); p.u64(bufferSliceId(k.vbBuffers[i], k.vbOffsets[i])); p.u64(0);
                        }
                    });
                    memcpy(vertexStrides, k.vbStrides, sizeof(vertexStrides));
                    memcpy(vertexHasStride, k.vbHasStride, sizeof(vertexHasStride));
                    for (U32 i = 0; i < k.vbCount; i++) {
                        Buffer* vb = objOf<Buffer>(k.vbBuffers[i], K_BUFFER, "vkQueueSubmit");
                        VK64_DROP_IF(continue, !vb || k.vbOffsets[i] >= vb->size);
                        // Clamp to what the frame's draws touch when the pre-pass
                        // resolved it: DXVK binds whole 16MB staging heaps whose
                        // live vertex data is a small prefix.
                        U64 bound = vb->size - k.vbOffsets[i];
                        if (i < VK64_MAX_VERT_BINDINGS && v2MaxVbUse[i] > 0 && v2MaxVbUse[i] < bound)
                            bound = v2MaxVbUse[i];
                        emitBufferData(k.vbBuffers[i], vb->memOff + k.vbOffsets[i], bound,
                            bufferSliceId(k.vbBuffers[i], k.vbOffsets[i]));
                    }
                    auto* pipe = activePipe ? objOf<Pipeline>(activePipe, K_PIPELINE, "vkQueueSubmit") : nullptr;
                    if (pipe && pipe->dynamicVertexStride) emitPipeline(activePipe);
                    break;
                }
                case CMD_BIND_IB: {
                    emit(V2_INDEX_BIND, [&](ChunkWriter& p) {
                        p.u64(bufferSliceId(k.a, k.b)); p.u64(0); p.u32(k.c);
                    });
                    Buffer* ib = objOf<Buffer>(k.a, K_BUFFER, "vkQueueSubmit");
                    if (ib && k.b < ib->size) {
                        U64 bound = ib->size - k.b;
                        if (v2MaxIbUse > 0 && v2MaxIbUse < bound) bound = v2MaxIbUse;
                        emitBufferData(k.a, ib->memOff + k.b, bound, bufferSliceId(k.a, k.b));
                    }
                    break;
                }
                case CMD_DRAW:
                    nDraw++;
                    emit(V2_DRAW, [&](ChunkWriter& p) {
                        p.u32((U32)k.a); p.u32((U32)k.b); p.u32((U32)k.c); p.u32((U32)k.d);
                        p.u8(0); p.u8(0); p.u16(0);
                        p.u32(0); p.u32(0); p.u32(0);
                    });
                    break;
                case CMD_DRAW_INDEXED:
                    nDraw++;
                    emit(V2_DRAW, [&](ChunkWriter& p) {
                        p.u32(0); p.u32((U32)k.b); p.u32(0); p.u32((U32)k.d);
                        p.u8(1); p.u8(0); p.u16(0);
                        p.u32((U32)k.a); p.u32((U32)k.c); p.u32((U32)(int32_t)k.copy.ox);
                    });
                    break;
                case CMD_COPY_IMAGE: {
                    const ImageCopy& copy = k.imageCopy;
                    emit(V2_IMAGE_COPY, [&](ChunkWriter& p) {
                        p.u64(copy.src); p.u64(copy.dst);
                        p.u32(copy.srcMip); p.u32(copy.srcLayer); p.u32(copy.dstMip); p.u32(copy.dstLayer);
                        for (U32 v : copy.srcOrigin) p.u32(v);
                        for (U32 v : copy.dstOrigin) p.u32(v);
                        for (U32 v : copy.extent) p.u32(v);
                    });
                    break;
                }
                case CMD_BLIT_IMAGE: {
                    const ImageBlit& blit = k.imageBlit;
                    emit(V2_IMAGE_BLIT, [&](ChunkWriter& p) {
                        p.u64(blit.src); p.u64(blit.dst);
                        p.u32(blit.srcMip); p.u32(blit.srcLayer); p.u32(blit.dstMip); p.u32(blit.dstLayer);
                        p.u32(blit.filter);
                        for (const auto& point : blit.srcOffsets) for (int32_t value : point) p.u32((U32)value);
                        for (const auto& point : blit.dstOffsets) for (int32_t value : point) p.u32((U32)value);
                    });
                    break;
                }
                case CMD_COPY_I2B: {
                    const CopyI2B& rb = k.copyI2B;
                    for (const auto& pend : g_readbackPending) {
                        if (pend.srcImage == rb.srcImage && pend.dstBuffer == rb.dstBuffer) {
                            emit(V2_READBACK, [&](ChunkWriter& p) {
                                p.u64(rb.srcImage); p.u32(rb.w); p.u32(rb.h);
                                p.u32(pend.heapPtr); p.u32(pend.flagPtr);
                            });
                            break;
                        }
                    }
                    break;
                }
                // Executed eagerly at record/submit time (see the CMD_* handlers
                // for copies, updates, query results and timestamps, or no-ops
                // in snapshot replay (BARRIER): nothing to put on the wire.
                case CMD_BARRIER:
                case CMD_COPY_B2I:
                case CMD_UPDATE_BUF:
                case CMD_COPY_QUERY_RESULTS:
                case CMD_RESET_QUERY:
                case CMD_WRITE_TIMESTAMP:
                    break;
                case CMD_DISPATCH:
                    // The V2 protocol is render-only: no compute pipeline or
                    // dispatch record exists. Never silently drop it.
                    vk64Unsupported("compute-dispatch", "vkCmdDispatch* has no V2 record; results are not produced");
                    break;
                default:
                    vk64Unsupported("unknown-cmd-kind", "submit emit loop met a command kind with no V2 record");
                    break;
            }
        }

        emit(V2_FRAME_END, [&](ChunkWriter& p) { p.u32(g_frameNo); });
        double vkprofQueueT0 = vkProfNow(); // BW64_VKPROF (additive)
        v2QueueFrame(fa);

        const V2DoneFrame& qd = g_v2Pending.back();
        size_t totalBytes = 0;
        for (const auto& c : qd.chunks) totalBytes += c.size();
        if (vkFrameLog())
            klog_fmt("vk64: FRAME %u built (v2): %ux%u draws=%u pushes=%u chunks=%u bytes=%zu",
                     g_frameNo, fb ? fb->w : 1, fb ? fb->h : 1, (unsigned)nDraw, (unsigned)nPush,
                     (unsigned)qd.chunks.size(), totalBytes);
        if (vkDumpV2()) {
            std::string dump = "vk64: FRAME " + std::to_string(g_frameNo) + " v2 records:";
            const char* names[] = { "?", "FRAME_BEGIN", "FRAME_END", "RP_BEGIN", "RP_END",
                "SHADER", "PIPELINE", "BIND_SETS", "DESC_SET", "BUFFER_DATA", "IMAGE_DATA",
                "SAMPLER", "PUSH", "VIEWPORT", "SCISSOR", "VERTEX_BIND", "INDEX_BIND",
                "DRAW", "INLINE_BYTES", "BUFFER_REUSE", "IMAGE_REUSE" };
            for (U32 t = 1; t < 21; t++)
                if (hist[t]) { dump += " "; dump += names[t]; dump += "=" + std::to_string(hist[t]); }
            klog_fmt("%s", dump.c_str());
        }
        if (vkTimingEnabled()) {
            VkProfAcc& pa = vkProfAcc();
            pa.accBufData += vkprofBufT; pa.accImgData += vkprofImgT;
            pa.accBufHashed += vkprofBufHashed; pa.accBufSent += vkprofBufSent;
            pa.accImgHashed += vkprofImgHashed; pa.accImgSent += vkprofImgSent;
            pa.accBufCalls += vkprofBufCalls; pa.accImgCalls += vkprofImgCalls;
            pa.accQueue += vkProfNow() - vkprofQueueT0;
            pa.accSer2 += vkProfNow() - vkprofSer2T0;
        }
        g_frameNo++;
    }
}

// ---------------------------------------------------------------------------
// Physical device: one == one WebGPU adapter (P1 section 1.2 table row 2).
// ---------------------------------------------------------------------------
const char* GPU_NAME = "VKWGPU WebGPU Virtual Device";
const U32  g_icd_api_version = VK_API_VERSION_1_3;

template <class Limits>
void fillLimits(Limits& L) {
    L.maxImageDimension1D = 16384; L.maxImageDimension2D = 16384; L.maxImageDimension3D = 2048;
    L.maxImageDimensionCube = 16384; L.maxImageArrayLayers = 2048;
    L.maxTexelBufferElements = 1 << 27; L.maxUniformBufferRange = 65536;
    L.maxStorageBufferRange = 1 << 30; L.maxPushConstantsSize = 256;
    L.maxMemoryAllocationCount = 4096; L.maxSamplerAllocationCount = 4000;
    L.bufferImageGranularity = 1; L.sparseAddressSpaceSize = 0;
    L.maxBoundDescriptorSets = 8; L.maxPerStageDescriptorSamplers = 16;
    L.maxPerStageDescriptorUniformBuffers = 16; L.maxPerStageDescriptorStorageBuffers = 4;
    L.maxPerStageDescriptorSampledImages = 16; L.maxPerStageDescriptorStorageImages = 4;
    L.maxPerStageDescriptorInputAttachments = 4;
    L.maxPerStageResources = 128; L.maxDescriptorSetSamplers = 16;
    L.maxDescriptorSetUniformBuffers = 16; L.maxDescriptorSetUniformBuffersDynamic = 8;
    L.maxDescriptorSetStorageBuffers = 4; L.maxDescriptorSetStorageBuffersDynamic = 4;
    L.maxDescriptorSetSampledImages = 16; L.maxDescriptorSetStorageImages = 4;
    L.maxDescriptorSetInputAttachments = 4;
    L.maxVertexInputAttributes = 16; L.maxVertexInputBindings = 16;
    L.maxVertexInputAttributeOffset = 2047; L.maxVertexInputBindingStride = 2048;
    L.maxVertexOutputComponents = 64; L.maxTessellationGenerationLevel = 64;
    L.maxTessellationPatchSize = 32;
    L.maxFragmentInputComponents = 64; L.maxFragmentOutputAttachments = 4;
    L.maxFragmentDualSrcAttachments = 0; L.maxFragmentCombinedOutputResources = 4;
    L.maxComputeSharedMemorySize = 16384;
    L.maxComputeWorkGroupCount[0] = L.maxComputeWorkGroupCount[1] = L.maxComputeWorkGroupCount[2] = 65535;
    L.maxComputeWorkGroupInvocations = 128;
    L.maxComputeWorkGroupSize[0] = 128; L.maxComputeWorkGroupSize[1] = 128; L.maxComputeWorkGroupSize[2] = 64;
    L.subPixelPrecisionBits = 8; L.subTexelPrecisionBits = 8; L.mipmapPrecisionBits = 8;
    L.maxDrawIndexedIndexValue = 0xFFFFFFFFu; L.maxDrawIndirectCount = 0xFFFFFFFFu;
    L.maxSamplerLodBias = 2.0f; L.maxSamplerAnisotropy = 16.0f;
    // G2: DXVK D3D9 DecodeMultiSampleType derives sampleCount from
    // framebuffer{Color,Depth}SampleCounts; zero here zeroes the backbuffer
    // sampleCount and fails CheckImageSupport. Advertise at least 1x.
    L.framebufferColorSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    L.framebufferDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    L.framebufferStencilSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    L.sampledImageColorSampleCounts = VK_SAMPLE_COUNT_1_BIT | VK_SAMPLE_COUNT_4_BIT;
    L.sampledImageDepthSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    L.storageImageSampleCounts = VK_SAMPLE_COUNT_1_BIT;
    L.maxViewports = 16;
    L.maxViewportDimensions[0] = 4096; L.maxViewportDimensions[1] = 4096;
    L.viewportBoundsRange[0] = -8192.0f; L.viewportBoundsRange[1] = 8191.0f;
    L.viewportSubPixelBits = 0; L.minMemoryMapAlignment = 4096;
    L.minTexelBufferOffsetAlignment = 256; L.minUniformBufferOffsetAlignment = 256;
    L.minStorageBufferOffsetAlignment = 256; L.minTexelOffset = -8; L.maxTexelOffset = 7;
    L.minTexelGatherOffset = -8; L.maxTexelGatherOffset = 7;
    L.minInterpolationOffset = -0.5f; L.maxInterpolationOffset = 0.5f;
    L.subPixelInterpolationOffsetBits = 4; L.maxFramebufferWidth = 16384; L.maxFramebufferHeight = 16384;
    L.maxFramebufferLayers = 2048; L.maxColorAttachments = 8; L.maxSampleMaskWords = 1;
    L.maxClipDistances = 8; L.maxCullDistances = 8; L.maxCombinedClipAndCullDistances = 8;
    L.discreteQueuePriorities = 2;
    L.pointSizeRange[0] = 1.0f; L.pointSizeRange[1] = 64.0f;
    L.lineWidthRange[0] = 1.0f; L.lineWidthRange[1] = 1.0f;
    L.pointSizeGranularity = 0.0f; L.lineWidthGranularity = 0.0f;
    L.strictLines = VK_TRUE; L.standardSampleLocations = VK_TRUE;
    L.optimalBufferCopyOffsetAlignment = 1; L.optimalBufferCopyRowPitchAlignment = 1;
    L.nonCoherentAtomSize = 64;
    L.timestampPeriod = 1.0f; // one Vulkan tick per host-clock nanosecond
}

void fillProperties(VkPhysicalDeviceProperties* p) {
    memset(p, 0, sizeof(*p));
    p->apiVersion = g_icd_api_version;
    p->driverVersion = 1;
    p->vendorID = 0x1A2B3C4D;      /* synthetic, as in P1 */
    p->deviceID = 0x0000ABCD;
    p->deviceType = VK_PHYSICAL_DEVICE_TYPE_OTHER;
    strncpy(p->deviceName, GPU_NAME, VK_MAX_PHYSICAL_DEVICE_NAME_SIZE - 1);
    /* llvmpipe-ish geometry: Boxedwine guests assume a real GPU */
    fillLimits(p->limits);
}

const char* const g_inst_exts[] = {
    "VK_KHR_surface", "VK_KHR_win32_surface", "VK_KHR_xcb_surface",
    "VK_KHR_xlib_surface", "VK_EXT_headless_surface",
};

// Only extensions whose exposed behavior works end-to-end are enumerated.
const char* const g_dev_exts[] = {
    "VK_KHR_swapchain",
    "VK_KHR_load_store_op_none",
    "VK_EXT_robustness2",           // HARD requirement (nullDescriptor + robustBufferAccess2)
    "VK_EXT_depth_clip_enable",
};

// Core bits reported to guests. Unsupported paths remain FALSE.
void fillCoreFeatures(VkPhysicalDeviceFeatures* f) {
    memset(f, 0, sizeof(*f));
    // Every TRUE below is listed as implemented in vk-capabilities.json; the
    // unit test checks assignments and manifest entries stay in sync.
    f->robustBufferAccess = VK_TRUE;
    f->fullDrawIndexUint32 = VK_TRUE;
    f->imageCubeArray = VK_TRUE;        /* D3D11 cube arrays */
    f->independentBlend = VK_TRUE;      /* D3D11 independent blend targets */
    f->shaderClipDistance = VK_TRUE;
    f->samplerAnisotropy = VK_TRUE;
    /* NOT advertised (nothing in the page tier implements them, and DXVK /
     * vkd3d degrade gracefully): shaderInt64, shaderFloat64,
     * shaderStorageImageExtendedFormats, shaderStorageImage{Read,Write}WithoutFormat
     * (storage images are bound blank), sparse*, wideLines, largePoints,
     * alphaToOne, depthBounds, logicOp, pipelineStatisticsQuery. */
    /* Only end-to-end implemented features are reported true. Unsupported
     * features remain false so DXVK/vkd3d cannot select an invalid path. */
    f->multiDrawIndirect = VK_TRUE;     /* vk64bridge expands draws into CMD_DRAW */
    f->drawIndirectFirstInstance = VK_TRUE; /* preserved in each expanded draw */
    f->depthBiasClamp = VK_TRUE;        /* replayed with WebGPU depthBiasClamp */
    f->depthClamp = VK_TRUE;            /* page requests depth-clip-control when available */
    f->textureCompressionBC = VK_TRUE;  /* needs browser texture-compression-bc */
}

// ---------------------------------------------------------------------------
// Features2 / Properties2 pNext walkers (audit P2-NOW items 1-2).
//
// DXVK does not call the core 1.0 getters when it wants to know what the device
// supports: it chains a VkPhysicalDeviceFeatures2 with ~15 feature structs and
// reads each VkBool32 back out of the SAME buffer it passed in. Before these
// walkers existed every chained struct stayed at whatever the guest left there
// (zero), so every required feature read "unsupported" even with the extension
// advertised. So the walkers are load-bearing, not decoration.
//
// Contract: unknown sTypes are SKIPPED, never rejected — a newer DXVK chaining a
// struct we do not model must still get a device. pNext sits at offset 8 in every
// VkStructureType-headed struct (sType u32 + 4 pad + ptr), which the generated
// header's static_asserts and probe_caps both pin.
// ---------------------------------------------------------------------------
// One arm of the Features2 walk: read the struct out of guest memory, let the
// fill function set the bits, write it back. The guest handed us this buffer, so
// writing it back is the only place an answer can go — and it is where DXVK
// reads it from.
#define SET_FEATURES(addr, fn, T)                                             \
    do {                                                                       \
        if ((addr) && g_mem) {                                                 \
            T f_ = {};                                                         \
            g_mem->memcpyFromGuest(&f_, (addr), sizeof(f_));                   \
            fn(f_);                                                            \
            g_mem->memcpyToGuest((addr), &f_, sizeof(f_));                     \
        }                                                                      \
    } while (0)
// Every TRUE bit has an `implemented` row in source/vulkan/vk-capabilities.json.
template <class T> inline void fillRobustness2(T& f) {
    f.robustBufferAccess2 = VK_TRUE;
    f.robustImageAccess2 = VK_TRUE;
    f.nullDescriptor = VK_TRUE;
}
template <class T> inline void fillMaintenance5(T& f) {
}
template <class T> inline void fillMaintenance6(T& f) {
}
template <class T> inline void fillTransformFeedback(T& f) {
}
// vk64_guest.h (the trimmed Vulkan header this TU builds against) does not
// define VkPhysicalDeviceVertexAttributeDivisorFeaturesEXT or
// VkPhysicalDeviceVulkan13Properties. Local mirrors with the exact canonical
// layout (source/vulkan/vk/vulkan_core.h); static_asserts pin it.
struct Vk64VertexAttributeDivisorFeaturesEXT {
    VkStructureType sType;
    U64             pNext; // uint64_t, not void*: matches the 64-bit guest layout (cf. vk64_guest.h)
    VkBool32        vertexAttributeInstanceRateDivisor;
    VkBool32        vertexAttributeInstanceRateZeroDivisor;
};
struct Vk64Vulkan13Properties {
    VkStructureType sType;
    U64             pNext; // uint64_t, not void*: matches the 64-bit guest layout (cf. vk64_guest.h)
    U32 minSubgroupSize, maxSubgroupSize, maxComputeWorkgroupSubgroups;
    U32 requiredSubgroupSizeStages; // VkShaderStageFlags
    U32 maxInlineUniformBlockSize, maxPerStageDescriptorInlineUniformBlocks;
    U32 maxPerStageDescriptorUpdateAfterBindInlineUniformBlocks;
    U32 maxDescriptorSetInlineUniformBlocks;
    U32 maxDescriptorSetUpdateAfterBindInlineUniformBlocks;
    U32 maxInlineUniformTotalSize;
    VkBool32 integerDotProduct8BitUnsignedAccelerated;
    VkBool32 integerDotProduct8BitSignedAccelerated;
    VkBool32 integerDotProduct8BitMixedSignednessAccelerated;
    VkBool32 integerDotProduct4x8BitPackedUnsignedAccelerated;
    VkBool32 integerDotProduct4x8BitPackedSignedAccelerated;
    VkBool32 integerDotProduct4x8BitPackedMixedSignednessAccelerated;
    VkBool32 integerDotProduct16BitUnsignedAccelerated;
    VkBool32 integerDotProduct16BitSignedAccelerated;
    VkBool32 integerDotProduct16BitMixedSignednessAccelerated;
    VkBool32 integerDotProduct32BitUnsignedAccelerated;
    VkBool32 integerDotProduct32BitSignedAccelerated;
    VkBool32 integerDotProduct32BitMixedSignednessAccelerated;
    VkBool32 integerDotProduct64BitUnsignedAccelerated;
    VkBool32 integerDotProduct64BitSignedAccelerated;
    VkBool32 integerDotProduct64BitMixedSignednessAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating8BitUnsignedAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating8BitSignedAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating8BitMixedSignednessAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating4x8BitPackedUnsignedAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating4x8BitPackedSignedAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating4x8BitPackedMixedSignednessAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating16BitUnsignedAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating16BitSignedAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating16BitMixedSignednessAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating32BitUnsignedAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating32BitSignedAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating32BitMixedSignednessAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating64BitUnsignedAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating64BitSignedAccelerated;
    VkBool32 integerDotProductAccumulatingSaturating64BitMixedSignednessAccelerated;
    U64 storageTexelBufferOffsetAlignmentBytes;      // VkDeviceSize
    VkBool32 storageTexelBufferOffsetSingleTexelAlignment;
    U64 uniformTexelBufferOffsetAlignmentBytes;      // VkDeviceSize
    VkBool32 uniformTexelBufferOffsetSingleTexelAlignment;
    U64 maxBufferSize;                               // VkDeviceSize
};
static_assert(sizeof(Vk64VertexAttributeDivisorFeaturesEXT) == 24,
              "divisor features mirror layout");
static_assert(sizeof(Vk64Vulkan13Properties) == 216,
              "1.3 properties mirror layout");
struct Vk32VertexAttributeDivisorFeaturesEXT {
    VkStructureType sType;
    U32             pNext;
    VkBool32        vertexAttributeInstanceRateDivisor;
    VkBool32        vertexAttributeInstanceRateZeroDivisor;
};
static_assert(sizeof(Vk32VertexAttributeDivisorFeaturesEXT) == 16, "32-bit divisor features mirror layout");
// vkd3d-proton device-creation gate (device.c:2495): requires BOTH divisor
// features. Exception: a pipeline with a divisor other than 1 is reported by
// vk64AuditGraphicsPipeline ("vertex-attribute-divisor") and rendered with 1.
template <class T> inline void fillVertexAttributeDivisor(T& f) {
}
// Exception group: descriptor-indexing bits are vkd3d-proton hard gates
// (state.c:8352). Descriptor sets are read at submit time so update-after-bind
// and partially-bound are real; runtime arrays / non-uniform indexing of image
// arrays need shader-side binding arrays the WebGPU tier lacks, so a shader
// that uses them fails at pipeline creation in the page tier (noteUnsupported).
template <class T> inline void fillDescriptorIndexing(T& f) {
}
// shaderFloat16 / shaderInt8 / 16-bit and 8-bit storage / scalar block layout
// are NOT advertised: WGSL has no 8-bit types and the f16 extension is not
// wired through the shader translator. DXVK and vkd3d fall back to 32-bit.
template <class T> inline void fillFloat16Int8(T&) {}
template <class T> inline void fillStorage16(T&) {}
template <class T> inline void fillStorage8(T&) {}
template <class T> inline void fillScalarBlockLayout(T&) {}
template <class T> inline void fillDepthClipEnable(T& f) {
    f.depthClipEnable = VK_TRUE;        // page requests depth-clip-control when the browser has it
}
template <class T> inline void fillVulkan12Features(T& f) {
    // timelineSemaphore is emulated honestly under immediate execution (u64
    // counter per semaphore; a wait for a value the queue already ran past
    // returns SUCCESS, and a wait for a FUTURE value with timeout 0 returns
    // VK_TIMEOUT). bufferDeviceAddress stays FALSE: nothing hands out addresses.
    f.timelineSemaphore = VK_TRUE;
    f.bufferDeviceAddress = VK_FALSE;
}
template <class T> inline void fillVulkan11Features(T& f) {
    // multiview, variablePointers*, 16-bit storage: not advertised (see above).
    f.shaderDrawParameters = VK_TRUE;       // exception: vkd3d-proton hard gate (device.c:2658)
}
template <class T> inline void fillVulkan13Features(T& f) {
    // DXVK D3D11 baseline hard-requires shaderDemoteToHelperInvocation
    // (d3d11_device.cpp GetDeviceFeatures). Exception: demote-vs-kill nuances.
    f.shaderDemoteToHelperInvocation = VK_TRUE;
}

void walkFeatures2(U64 pNextAddr) {
    for (U64 cur = pNextAddr; cur && g_mem;) {
        U32 sType = g_mem->readd(cur);
        switch (sType) {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT:
                SET_FEATURES(cur, fillRobustness2, VkPhysicalDeviceRobustness2FeaturesEXT); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES:
                SET_FEATURES(cur, fillMaintenance5, VkPhysicalDeviceMaintenance5Features); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES:
                SET_FEATURES(cur, fillMaintenance6, VkPhysicalDeviceMaintenance6Features); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT:
                SET_FEATURES(cur, fillTransformFeedback, VkPhysicalDeviceTransformFeedbackFeaturesEXT); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT:
                SET_FEATURES(cur, fillVertexAttributeDivisor, Vk64VertexAttributeDivisorFeaturesEXT); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES:
                SET_FEATURES(cur, fillDescriptorIndexing, VkPhysicalDeviceDescriptorIndexingFeatures); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES:
                SET_FEATURES(cur, fillFloat16Int8, VkPhysicalDeviceShaderFloat16Int8Features); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES:
                SET_FEATURES(cur, fillStorage16, VkPhysicalDevice16BitStorageFeatures); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES:
                SET_FEATURES(cur, fillStorage8, VkPhysicalDevice8BitStorageFeatures); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES:
                SET_FEATURES(cur, fillScalarBlockLayout, VkPhysicalDeviceScalarBlockLayoutFeatures); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT:
                SET_FEATURES(cur, fillDepthClipEnable, VkPhysicalDeviceDepthClipEnableFeaturesEXT); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
                SET_FEATURES(cur, fillVulkan12Features, VkPhysicalDeviceVulkan12Features); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
                SET_FEATURES(cur, fillVulkan11Features, VkPhysicalDeviceVulkan11Features); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
                SET_FEATURES(cur, fillVulkan13Features, VkPhysicalDeviceVulkan13Features); break;
            default:
                // Unknown sType: the struct's size is unknown, so it is left exactly
                // as the guest set it (the Vulkan contract is that the app zeroes
                // feature structs; blindly zeroing 512 bytes here used to clobber the
                // NEXT chained struct). Counted so a missing model is visible.
                vk64Unsupported("features2-struct", ("sType=" + std::to_string(sType) + " not modeled, left as-is").c_str());
                break;
        }
        cur = g_mem->readq(cur + 8);      /* pNext is at offset 8 in every struct */
    }
}

// Properties2 side. Zeros degrade gracefully (a consumer clamps to them), EXCEPT
// the maxDescriptorSetUpdateAfterBind* limits: DXVK refuses descriptor-indexing
// layouts above them, so advertising the features above with a zero limit would
// make the device self-contradictory.
void walkProperties2(U64 pNextAddr) {
    // A stable, non-zero device UUID. DXVK's per-adapter matching (and D3DKMT's
    // driverID workarounds) key off this; a constant is fine because there is
    // exactly one adapter, and it is stable across runs.
    static const U8 uuid[VK_UUID_SIZE] = {
        0x56, 0x4B, 0x57, 0x47, 0x50, 0x55, 0x21, 0x01,   /* 'VKWGPU' + version */
        0x56, 0x4B, 0x57, 0x47, 0x50, 0x55, 0x21, 0x02,
    };
    for (U64 cur = pNextAddr; cur && g_mem;) {
        U32 sType = g_mem->readd(cur);
        switch (sType) {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
                VkPhysicalDeviceIDProperties p = {};
                g_mem->memcpyFromGuest(&p, cur, sizeof(p));
                memcpy(p.deviceUUID, uuid, VK_UUID_SIZE);
                p.deviceNodeMask = 1;
                g_mem->memcpyToGuest(cur, &p, sizeof(p));
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
                VkPhysicalDeviceSubgroupProperties p = {};
                g_mem->memcpyFromGuest(&p, cur, sizeof(p));
                p.subgroupSize = 32;          /* warp-shaped, like every real ICD */
                p.supportedStages = 0xffffffffu;
                p.supportedOperations = 0xffffffffu;
                p.quadOperationsInAllStages = VK_TRUE;
                g_mem->memcpyToGuest(cur, &p, sizeof(p));
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_PROPERTIES: {
                VkPhysicalDeviceMaintenance5Properties p = {};
                g_mem->memcpyFromGuest(&p, cur, sizeof(p));
                p.earlyFragmentMultisampleCoverageAfterSampleCounting = VK_TRUE;
                p.earlyFragmentSampleMaskTestBeforeSampleCounting = VK_TRUE;
                p.polygonModePointSize = VK_TRUE;
                g_mem->memcpyToGuest(cur, &p, sizeof(p));
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES: {
                VkPhysicalDeviceDescriptorIndexingProperties p = {};
                g_mem->memcpyFromGuest(&p, cur, sizeof(p));
                p.maxUpdateAfterBindDescriptorsInAllPools = 1024;
                p.maxPerStageDescriptorUpdateAfterBindSamplers = 16;
                p.maxPerStageDescriptorUpdateAfterBindUniformBuffers = 16;
                /* vkd3d legacy bindless requires >= 1M (VKD3D_MIN_VIEW_DESCRIPTOR_COUNT);
                   same advertisement-only caveat as above */
                p.maxPerStageDescriptorUpdateAfterBindStorageBuffers = 1000000;
                p.maxPerStageDescriptorUpdateAfterBindSampledImages = 1000000;
                p.maxPerStageDescriptorUpdateAfterBindStorageImages = 1000000;
                p.maxPerStageDescriptorUpdateAfterBindInputAttachments = 4;
                p.maxPerStageUpdateAfterBindResources = 128;
                p.maxDescriptorSetUpdateAfterBindSamplers = 1024;
                p.maxDescriptorSetUpdateAfterBindUniformBuffers = 1024;
                p.maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 64;
                p.maxDescriptorSetUpdateAfterBindStorageBuffers = 1024;
                p.maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 64;
                p.maxDescriptorSetUpdateAfterBindSampledImages = 1024;
                p.maxDescriptorSetUpdateAfterBindStorageImages = 1024;
                p.maxDescriptorSetUpdateAfterBindInputAttachments = 16;
                g_mem->memcpyToGuest(cur, &p, sizeof(p));
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT: {
                VkPhysicalDeviceRobustness2PropertiesEXT p = {};
                g_mem->memcpyFromGuest(&p, cur, sizeof(p));
                // MUST be non-zero: DXVK D3D9DeviceEx divides constant-layout
                // sizes by these alignments; zero => guest SIGFPE.
                p.robustStorageBufferAccessSizeAlignment = 4;
                p.robustUniformBufferAccessSizeAlignment = 4;
                g_mem->memcpyToGuest(cur, &p, sizeof(p));
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT: {
                VkPhysicalDeviceTransformFeedbackPropertiesEXT p = {};
                g_mem->memcpyFromGuest(&p, cur, sizeof(p));
                p.maxTransformFeedbackStreams = 4;
                p.maxTransformFeedbackBuffers = 4;
                p.maxTransformFeedbackBufferSize = 1ull << 27;
                p.maxTransformFeedbackStreamDataSize = 4096;
                p.maxTransformFeedbackBufferDataSize = 4096;
                p.maxTransformFeedbackBufferDataStride = 256;
                p.transformFeedbackQueries = VK_TRUE;
                p.transformFeedbackStreamsLinesTriangles = VK_TRUE;
                p.transformFeedbackDraw = VK_TRUE;
                g_mem->memcpyToGuest(cur, &p, sizeof(p));
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: {
                VkPhysicalDeviceVulkan11Properties p = {};
                g_mem->memcpyFromGuest(&p, cur, sizeof(p));
                memcpy(p.deviceUUID, uuid, VK_UUID_SIZE);
                p.deviceNodeMask = 1;
                p.subgroupSize = 32;
                p.subgroupSupportedStages = 0xffffffffu;
                p.subgroupSupportedOperations = 0xffffffffu;
                p.subgroupQuadOperationsInAllStages = VK_TRUE;
                p.pointClippingBehavior = VK_POINT_CLIPPING_BEHAVIOR_ALL_CLIP_PLANES;
                p.maxMultiviewViewCount = 6;
                p.maxMultiviewInstanceIndex = 0xFFFFFFFFu;
                p.maxPerSetDescriptors = 1024;
                p.maxMemoryAllocationSize = 2ull * 1024 * 1024 * 1024;
                g_mem->memcpyToGuest(cur, &p, sizeof(p));
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES: {
                Vk64Vulkan13Properties p = {};
                g_mem->memcpyFromGuest(&p, cur, sizeof(p));
                // vkd3d-proton single-texel-alignment gate (device.c:2520):
                // storageTexelBufferOffsetSingleTexelAlignment ||
                // storageTexelBufferOffsetAlignmentBytes == 1 (same for uniform).
                p.storageTexelBufferOffsetSingleTexelAlignment = VK_TRUE;
                p.uniformTexelBufferOffsetSingleTexelAlignment = VK_TRUE;
                g_mem->memcpyToGuest(cur, &p, sizeof(p));
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES: {
                VkPhysicalDeviceVulkan12Properties p = {};
                g_mem->memcpyFromGuest(&p, cur, sizeof(p));
                p.driverID = VK_DRIVER_ID_MESA_LLVMPIPE;
                p.driverName[0] = 'b';
                p.driverName[1] = 'o';
                p.driverName[2] = 'x';
                p.driverName[3] = 'e';
                p.driverName[4] = 'd';
                p.driverName[5] = 'w';
                p.driverName[6] = 'i';
                p.driverName[7] = 'n';
                p.driverName[8] = 'e';
                p.driverInfo[0] = 'v';
                p.driverInfo[1] = 'k';
                p.driverInfo[2] = '6';
                p.driverInfo[3] = '4';
                /* descriptor-indexing limits, mirroring the properties2 arm */
                p.maxUpdateAfterBindDescriptorsInAllPools = 1024;
                p.maxPerStageDescriptorUpdateAfterBindSamplers = 16;
                p.maxPerStageDescriptorUpdateAfterBindUniformBuffers = 16;
                /* vkd3d legacy bindless requires >= 1M (VKD3D_MIN_VIEW_DESCRIPTOR_COUNT);
                   same advertisement-only caveat as above */
                p.maxPerStageDescriptorUpdateAfterBindStorageBuffers = 1000000;
                p.maxPerStageDescriptorUpdateAfterBindSampledImages = 1000000;
                p.maxPerStageDescriptorUpdateAfterBindStorageImages = 1000000;
                p.maxPerStageDescriptorUpdateAfterBindInputAttachments = 4;
                p.maxPerStageUpdateAfterBindResources = 128;
                p.maxDescriptorSetUpdateAfterBindSamplers = 1024;
                p.maxDescriptorSetUpdateAfterBindUniformBuffers = 1024;
                p.maxDescriptorSetUpdateAfterBindUniformBuffersDynamic = 64;
                p.maxDescriptorSetUpdateAfterBindStorageBuffers = 1024;
                p.maxDescriptorSetUpdateAfterBindStorageBuffersDynamic = 64;
                p.maxDescriptorSetUpdateAfterBindSampledImages = 1024;
                p.maxDescriptorSetUpdateAfterBindStorageImages = 1024;
                p.maxDescriptorSetUpdateAfterBindInputAttachments = 16;
                p.maxTimelineSemaphoreValueDifference = 0xFFFFFFFFull;
                p.framebufferIntegerColorSampleCounts = VK_SAMPLE_COUNT_1_BIT;
                g_mem->memcpyToGuest(cur, &p, sizeof(p));
                break;
            }
            default:
                klog_fmt("vk64: Properties2 pNext sType=%u not modeled, left as the guest set it",
                         (unsigned)sType);
                break;
        }
        cur = g_mem->readq(cur + 8);
    }
}

void fillMemoryProperties(VkPhysicalDeviceMemoryProperties* p) {
    memset(p, 0, sizeof(*p));
    p->memoryHeapCount = 2;
    p->memoryHeaps[0].size = 2ull * 1024 * 1024 * 1024;
    p->memoryHeaps[0].flags = VK_MEMORY_HEAP_DEVICE_LOCAL_BIT;
    p->memoryHeaps[1].size = 4ull * 1024 * 1024 * 1024;
    p->memoryHeaps[1].flags = 0;
    p->memoryTypeCount = 4;
    for (U32 i = 0; i < 4; i++) {
        p->memoryTypes[i].heapIndex = i < 2 ? 0 : 1;
        p->memoryTypes[i].propertyFlags =
            (i == 0) ? (VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                      : (i == 1 ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
                                : (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT));
    }
}

// Create a surface: there is no windowing system behind it (P1 section 1.4), so
// the handle is just an id. Both the xcb and the headless EXT path land here,
// which is what lets a guest run with no X server at all.
U64 createSurface() {
    U64 id = 0;
    createObj<Surface>(K_SURFACE, id);
    return id;
}



} // namespace

// ---------------------------------------------------------------------------
// Entry point from ksyscall64.
// ---------------------------------------------------------------------------
U64 vk64Bridge(CPU64* cpu, U64 fnId, U64 argsAddr) {
    std::lock_guard<std::recursive_mutex> lk(g_vkMutex);
    // NB: constructed AFTER the lock so the destructor (which touches the
    // profiler accumulators) also runs under g_vkMutex. BW64_VKPROF, no-op when unset.
    VkProfTrapTimer vkprofTrap(fnId);
    g_dropFn = fnId;
    g_mem32 = nullptr;
    g_mem = cpu ? cpu->memory : nullptr;

    if (fnId == VK64_fn_witness) {
        // The guest shim's constructor. Proves the guest dlopen()ed THIS
        // libvulkan.so.1 (tasks/p1-final.md unknown #1) before any real call.
        klog("vk64: FIRST trap - guest libvulkan.so.1 mapped (witness fnId=0)");
        return 0;
    }
    if (fnId == VK64_fn_unimplemented) {
        // (name*) — the guest shim's benign stub for a vk* we do not implement.
        // This is the P1 loader_tally worklist, still recording what a real guest
        // actually asks for.
        VK64Args a0 = {};
        if (argsAddr && g_mem) g_mem->memcpyFromGuest(&a0, argsAddr, sizeof(a0));
        klog_fmt("vk64: asked-for (unimplemented): %s", guestStr(a0.a[0]).c_str());
        vk64Unsupported(("fn:" + guestStr(a0.a[0])).c_str(), "entry point not implemented; returned VK_ERROR_FEATURE_NOT_PRESENT");
        return (U64)(int64_t)VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (fnId == VK64_fn_traceProc) {
        VK64Args a0 = {};
        if (argsAddr && g_mem) g_mem->memcpyFromGuest(&a0, argsAddr, sizeof(a0));
        klog_fmt("vk64: gipa %s: %s", a0.a[1] ? "HIT " : "MISS", guestStr(a0.a[0]).c_str());
        return 0;
    }

    if (vkTrace()) { klog_fmt("vk64: trap %s", fnName(fnId)); g_tracedCalls++; }

    VK64Args args = {};
    if (argsAddr && g_mem) g_mem->memcpyFromGuest(&args, argsAddr, sizeof(args));

    switch (fnId) {
    // =====================================================================
    // B. Queries and state answers (25 of the 85)
    // =====================================================================
    case VK64_fn_vkEnumerateInstanceVersion:
        wr32(args.a[0], g_icd_api_version);
        return VK_SUCCESS;

    case VK64_fn_vkEnumerateInstanceExtensionProperties: {
        // (layerName*, pCount*, pProperties*)
        const U32 n = (U32)(sizeof(g_inst_exts) / sizeof(g_inst_exts[0]));
        if (!args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        std::string layer = guestStr(args.a[0]);
        if (!layer.empty()) klog_fmt("vk64: layer '%s' requested; we have none", layer.c_str());
        if (!args.a[2]) { wr32(args.a[1], n); return VK_SUCCESS; }
        U32 want = rd32(args.a[1]);
        U32 c = want < n ? want : n;
        std::vector<VkExtensionProperties> props((size_t)(c ? c : 1));
        for (U32 i = 0; i < c; i++) {
            memset(&props[i], 0, sizeof(props[i]));
            strncpy(props[i].extensionName, g_inst_exts[i], VK_MAX_EXTENSION_NAME_SIZE - 1);
            props[i].specVersion = VK_API_VERSION_1_1;
        }
        writeArray(args.a[2], props, c);
        wr32(args.a[1], c);
        return c < n ? VK_INCOMPLETE : VK_SUCCESS;
    }
    case VK64_fn_vkEnumerateInstanceLayerProperties:
        if (!args.a[0]) return VK_ERROR_INITIALIZATION_FAILED;
        wr32(args.a[0], 0);
        return VK_SUCCESS;

    case VK64_fn_vkEnumeratePhysicalDevices: {
        // (instance, pCount*, pPhysDevs*)
        Instance* inst = objOf<Instance>(args.a[0], K_INSTANCE, "vkEnumeratePhysicalDevices");
        if (!inst || !args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 n = inst->physCount;
        if (!args.a[2]) { wr32(args.a[1], n); return VK_SUCCESS; }
        U32 want = rd32(args.a[1]);
        U32 c = want < n ? want : n;
        std::vector<U64> devs((size_t)(c ? c : 1));
        for (U32 i = 0; i < c; i++) devs[i] = inst->phys[i];
        writeArray(args.a[2], devs, c);
        wr32(args.a[1], c);
        return c < n ? VK_INCOMPLETE : VK_SUCCESS;
    }
    case VK64_fn_vkEnumerateDeviceExtensionProperties: {
        // (physDev, pLayerName*, out pCount*, out pProperties*)
        const U32 n = (U32)(sizeof(g_dev_exts) / sizeof(g_dev_exts[0]));
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        std::string layer = guestStr(args.a[1]);
        if (!layer.empty()) klog_fmt("vk64: device layer '%s' requested; we have none", layer.c_str());
        if (!args.a[3]) { wr32(args.a[2], n); return VK_SUCCESS; }
        U32 want = rd32(args.a[2]);
        U32 c = want < n ? want : n;

        std::vector<VkExtensionProperties> props((size_t)(c ? c : 1));
        for (U32 i = 0; i < c; i++) {
            memset(&props[i], 0, sizeof(props[i]));
            strncpy(props[i].extensionName, g_dev_exts[i], VK_MAX_EXTENSION_NAME_SIZE - 1);
            props[i].specVersion = VK_API_VERSION_1_3;
        }
        writeArray(args.a[3], props, c);
        wr32(args.a[2], c);
        return c < n ? VK_INCOMPLETE : VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceProperties: {
        // (physDev, out VkPhysicalDeviceProperties)  [real limits, as P1]
        if (!objOf<PhysDev>(args.a[0], K_PHYSDEV, "vkGetPhysicalDeviceProperties") || !args.a[1])
            return VK_ERROR_INITIALIZATION_FAILED;
        VkPhysicalDeviceProperties p;
        fillProperties(&p);
        writeStruct(args.a[1], p);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceProperties2: {
        if (!objOf<PhysDev>(args.a[0], K_PHYSDEV, "vkGetPhysicalDeviceProperties2") || !args.a[1])
            return VK_ERROR_INITIALIZATION_FAILED;
        VkPhysicalDeviceProperties2 p2 = {};
        if (!readStruct(args.a[1], p2)) return VK_ERROR_INITIALIZATION_FAILED;
        fillProperties(&p2.properties);
        writeStruct(args.a[1], p2);
        // The chained structs are filled IN PLACE, after the root struct is
        // written: the guest passes one buffer for the root and the chain, and
        // reads the chain back out of it.
        walkProperties2(p2.pNext);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceFeatures: {
        if (!args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        VkPhysicalDeviceFeatures f = {};
        fillCoreFeatures(&f);
        writeStruct(args.a[1], f);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceFeatures2: {
        VkPhysicalDeviceFeatures2 f2 = {};
        if (!readStruct(args.a[1], f2)) return VK_ERROR_INITIALIZATION_FAILED;
        fillCoreFeatures(&f2.features);
        writeStruct(args.a[1], f2);
        walkFeatures2(f2.pNext);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceMemoryProperties: {
        if (!args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        VkPhysicalDeviceMemoryProperties p;
        fillMemoryProperties(&p);
        writeStruct(args.a[1], p);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceMemoryProperties2: {
        VkPhysicalDeviceMemoryProperties2 p2 = {};
        if (!readStruct(args.a[1], p2)) return VK_ERROR_INITIALIZATION_FAILED;
        fillMemoryProperties(&p2.memoryProperties);
        writeStruct(args.a[1], p2);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties:
    case VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties2: {
        bool v2 = (fnId == VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties2);
        U64 countAddr = args.a[1], arrAddr = args.a[2];
        const U32 fams = 1;   /* one universal graphics+compute+transfer family */
        if (!countAddr) return VK_ERROR_INITIALIZATION_FAILED;
        if (!arrAddr) { wr32(countAddr, fams); return VK_SUCCESS; }
        U32 want = rd32(countAddr);
        U32 c = want < fams ? want : fams;
        VkQueueFamilyProperties q = {};
        q.queueFlags = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT;
        q.queueCount = 1;
        q.timestampValidBits = 64;
        q.minImageTransferGranularity = VkExtent3D{1, 1, 1};
        if (v2) {
            VkQueueFamilyProperties2 q2 = {};
            if (!readStruct(arrAddr, q2)) return VK_ERROR_INITIALIZATION_FAILED;
            q2.queueFamilyProperties = q;
            writeStruct(arrAddr, q2);
        } else {
            writeStruct(arrAddr, q);
        }
        wr32(countAddr, c);
        return c < fams ? VK_INCOMPLETE : VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceFormatProperties: {
        // (physDev, format, out VkFormatProperties)
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        const U32 format = (U32)args.a[1];
        VkFormatProperties p = {};
        p.linearTilingFeatures = v2FormatFeatureFlags(format);
        p.optimalTilingFeatures = p.linearTilingFeatures;
        p.bufferFeatures = v2FormatBufferFeatureFlags(format);
        writeStruct(args.a[2], p);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceFormatProperties2: {
        // (physDev, format, out VkFormatProperties2) + FormatProperties3 pNext.
        VK64_DROP_IF(return 0, !args.a[2]);
        const U32 format = (U32)args.a[1];
        VkFormatProperties2 p2 = {};
        VK64_DROP_IF(return 0, !readStruct(args.a[2], p2));
        const VkFormatFeatureFlags img = v2FormatFeatureFlags(format);
        p2.formatProperties.linearTilingFeatures = img;
        p2.formatProperties.optimalTilingFeatures = img;
        p2.formatProperties.bufferFeatures = v2FormatBufferFeatureFlags(format);
        writeStruct(args.a[2], p2);
        for (U64 cur = p2.pNext; cur && g_mem;) {
            if (g_mem->readd(cur) == VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3) {
                VkFormatProperties3 p3 = {};
                g_mem->memcpyFromGuest(&p3, cur, sizeof(p3));
                const VkFormatFeatureFlags2 img2 = v2FormatFeatureFlags2(format);
                p3.linearTilingFeatures = img2;
                p3.optimalTilingFeatures = img2;
                p3.bufferFeatures = v2FormatBufferFeatureFlags2(format);
                g_mem->memcpyToGuest(cur, &p3, sizeof(p3));
            }
            cur = g_mem->readq(cur + 8);
        }
        return 0;
    }
    case VK64_fn_vkGetPhysicalDeviceImageFormatProperties: {
        // (physDev, format, type, tiling, usage, flags, out VkImageFormatProperties)
        if (!args.a[6]) return VK_ERROR_INITIALIZATION_FAILED;
        VkImageFormatProperties pr = {};
        fillImageFormatProps(&pr);
        writeStruct(args.a[6], pr);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceImageFormatProperties2: {
        // (physDev, pInfo2*, pProps2*)
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        VkImageFormatProperties2 p2 = {};
        g_mem->memcpyFromGuest(&p2, args.a[2], sizeof(p2));
        VkPhysicalDeviceImageFormatInfo2 info = {};
        g_mem->memcpyFromGuest(&info, args.a[1], sizeof(info));
        fillImageFormatProps(&p2.imageFormatProperties);
        g_mem->memcpyToGuest(args.a[2], &p2, sizeof(p2));
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR: {
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        VkSurfaceCapabilitiesKHR c = {};
        c.minImageCount = 1; c.maxImageCount = 3;
        /* currentExtent undefined => the app's --width/--height are honoured */
        c.currentExtent = VkExtent2D{0xFFFFFFFFu, 0xFFFFFFFFu};
        c.minImageExtent = VkExtent2D{1, 1};
        c.maxImageExtent = VkExtent2D{16384, 16384};
        c.maxImageArrayLayers = 1;
        c.supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        c.currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        c.supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
        c.supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        writeStruct(args.a[2], c);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceSurfaceFormatsKHR: {
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        const VkSurfaceFormatKHR f = { VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR };
        if (!args.a[3]) { wr32(args.a[2], 1); return VK_SUCCESS; }
        if (rd32(args.a[2]) < 1) { wr32(args.a[2], 1); return VK_INCOMPLETE; }
        writeStruct(args.a[3], f);
        wr32(args.a[2], 1);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceSurfacePresentModesKHR: {
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!args.a[3]) { wr32(args.a[2], 1); return VK_SUCCESS; }
        if (rd32(args.a[2]) < 1) { wr32(args.a[2], 1); return VK_INCOMPLETE; }
        wr32(args.a[3], VK_PRESENT_MODE_FIFO_KHR);
        wr32(args.a[2], 1);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceSurfaceSupportKHR:
        wr32(args.a[3], VK_TRUE);
        return VK_SUCCESS;
    case VK64_fn_vkGetPhysicalDeviceXlibPresentationSupportKHR:
        // (physDev, qFam, dpy*, visual) -> VkBool32 in RAX. winex11's
        // wine_vk_init dlsym()s this (LOAD_FUNCPTR, hard fail) and X11DRV
        // calls it per device when picking a presentable one. TRUE: the page
        // tier presents every frame through the canvas; there is no
        // non-presentable device behind this shim.
        return VK_TRUE;

    case VK64_fn_vkGetPhysicalDeviceWin32PresentationSupportKHR:
        // (physDev, qFam) -> VkBool32 in RAX. dxgi's init_vk_funcs resolves
        // this from vulkan-1.dll during swapchain setup. TRUE: the page tier
        // presents every frame through the canvas; there is no non-presentable
        // device behind this shim.
        return VK_TRUE;

    case VK64_fn_vkGetDeviceQueue: {
        // (device, family, index, out VkQueue*)
        Device* dev = objOf<Device>(args.a[0], K_DEVICE, "vkGetDeviceQueue");
        if (!dev || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        wr64(args.a[3], dev->queue);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetBufferMemoryRequirements: {
        Buffer* b = objOf<Buffer>(args.a[1], K_BUFFER, "vkGetBufferMemoryRequirements");
        if (!b || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        VkMemoryRequirements r = {};
        r.size = b->size ? ((b->size + 255) & ~(U64)255) : 0;
        r.alignment = 256;
        r.memoryTypeBits = 0xF;
        writeStruct(args.a[2], r);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetBufferMemoryRequirements2: {
        // (dev, VkBufferMemoryRequirementsInfo2*, VkMemoryRequirements2*)
        if (!args.a[1] || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        VkBufferMemoryRequirementsInfo2 in2 = {};
        g_mem->memcpyFromGuest(&in2, args.a[1], sizeof(in2));
        Buffer* b = objOf<Buffer>((U64)in2.buffer, K_BUFFER, "vkGetBufferMemoryRequirements2");
        // G2: DXVK may query requirements for buffers created outside our
        // bridge (e.g., via winevulkan paths that bypass our vkCreateBuffer).
        // Return sensible defaults rather than failing, so the allocator
        // can proceed. The size is advisory; DXVK validates it.
        U64 bufSize = (b && b->size) ? b->size : 65536;
        VkMemoryRequirements2 out2 = {};
        g_mem->memcpyFromGuest(&out2, args.a[2], sizeof(out2));
        out2.memoryRequirements.size = (bufSize + 255) & ~(U64)255;
        out2.memoryRequirements.alignment = 256;
        out2.memoryRequirements.memoryTypeBits = 0xF;
        g_mem->memcpyToGuest(args.a[2], &out2, sizeof(out2));
        // Fill chained VkMemoryDedicatedRequirements in place.
        U64 pn = out2.pNext;
        while (pn) {
            U32 st = 0;
            g_mem->memcpyFromGuest(&st, pn, sizeof(st));
            if (st == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) {
                VkMemoryDedicatedRequirements dr = {};
                g_mem->memcpyFromGuest(&dr, pn, sizeof(dr));
                dr.prefersDedicatedAllocation = 0;
                dr.requiresDedicatedAllocation = 0;
                g_mem->memcpyToGuest(pn, &dr, sizeof(dr));
                break;
            }
            U64 nxt = 0;
            g_mem->memcpyFromGuest(&nxt, pn + 8, sizeof(nxt));
            pn = nxt;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetDeviceBufferMemoryRequirements: {
        // (dev, VkDeviceBufferMemoryRequirements*, VkMemoryRequirements2*)
        // Core in Vulkan 1.3 (maintenance4); DXVK uses this instead of
        // creating a temp buffer when available.
        if (!args.a[1] || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        VkDeviceBufferMemoryRequirements devInfo = {};
        g_mem->memcpyFromGuest(&devInfo, args.a[1], sizeof(devInfo));
        U64 createInfoSize = 0;
        if (devInfo.pCreateInfo) {
            VkBufferCreateInfo ci = {};
            g_mem->memcpyFromGuest(&ci, devInfo.pCreateInfo, sizeof(ci));
            createInfoSize = ci.size;
        }
        VkMemoryRequirements2 out2 = {};
        g_mem->memcpyFromGuest(&out2, args.a[2], sizeof(out2));
        out2.memoryRequirements.size = createInfoSize ? ((createInfoSize + 255) & ~(U64)255) : 256;
        out2.memoryRequirements.alignment = 256;
        out2.memoryRequirements.memoryTypeBits = 0xF;
        g_mem->memcpyToGuest(args.a[2], &out2, sizeof(out2));
        U64 pn = out2.pNext;
        while (pn) {
            U32 st = 0;
            g_mem->memcpyFromGuest(&st, pn, sizeof(st));
            if (st == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) {
                VkMemoryDedicatedRequirements dr = {};
                g_mem->memcpyFromGuest(&dr, pn, sizeof(dr));
                dr.prefersDedicatedAllocation = 0;
                dr.requiresDedicatedAllocation = 0;
                g_mem->memcpyToGuest(pn, &dr, sizeof(dr));
                break;
            }
            U64 nxt = 0;
            g_mem->memcpyFromGuest(&nxt, pn + 8, sizeof(nxt));
            pn = nxt;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetImageMemoryRequirements: {
        Image* im = objOf<Image>(args.a[1], K_IMAGE, "vkGetImageMemoryRequirements");
        if (!im || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 px = imageDataBytes(*im);
        VkMemoryRequirements r = {};
        r.size = (px + 255) & ~(U64)255;
        r.alignment = 256;
        r.memoryTypeBits = 0xF;
        writeStruct(args.a[2], r);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetImageMemoryRequirements2: {
        // (dev, VkImageMemoryRequirementsInfo2*, VkMemoryRequirements2*)
        if (!args.a[1] || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        VkImageMemoryRequirementsInfo2 in2 = {};
        g_mem->memcpyFromGuest(&in2, args.a[1], sizeof(in2));
        Image* im = objOf<Image>((U64)in2.image, K_IMAGE, "vkGetImageMemoryRequirements2");
        if (!im) return VK_ERROR_INITIALIZATION_FAILED;
        VkMemoryRequirements2 out2 = {};
        g_mem->memcpyFromGuest(&out2, args.a[2], sizeof(out2));
        U64 px = imageDataBytes(*im);
        out2.memoryRequirements.size = (px + 255) & ~(U64)255;
        out2.memoryRequirements.alignment = 256;
        out2.memoryRequirements.memoryTypeBits = 0xF;
        g_mem->memcpyToGuest(args.a[2], &out2, sizeof(out2));
        // Fill chained VkMemoryDedicatedRequirements in place (guest buffer).
        U64 pn = out2.pNext;
        while (pn) {
            U32 st = 0;
            g_mem->memcpyFromGuest(&st, pn, sizeof(st));
            if (st == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) {
                VkMemoryDedicatedRequirements dr = {};
                g_mem->memcpyFromGuest(&dr, pn, sizeof(dr));
                dr.prefersDedicatedAllocation = 0;
                dr.requiresDedicatedAllocation = 0;
                g_mem->memcpyToGuest(pn, &dr, sizeof(dr));
                break;
            }
            U64 nxt = 0;
            g_mem->memcpyFromGuest(&nxt, pn + 8, sizeof(nxt));
            pn = nxt;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetImageSubresourceLayout: {
        // Must report the GUEST address: the guest then addresses the mapped
        // range directly and its stores land in the page buffers, so the copy
        // happens later, once, where the GPU needs the pixels
        // (tasks/p1-final.md section 2.4).
        Image* im = objOf<Image>(args.a[1], K_IMAGE, "vkGetImageSubresourceLayout");
        if (!im || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        DeviceMemory* m = im->mem ? objOf<DeviceMemory>(im->mem, K_MEMORY, "vkGetImageSubresourceLayout") : nullptr;
        U32 bpp = fmt_bpp(im->format);
        VkSubresourceLayout l = {};
        l.offset = m ? (m->va + im->memOff) : 0;      /* VkDeviceSize: GUEST VA */
        l.size = (size_t)im->w * im->h * bpp;
        l.rowPitch = (size_t)im->w * bpp;
        l.arrayPitch = l.size;
        l.depthPitch = l.size;
        writeStruct(args.a[3], l);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetSwapchainImagesKHR: {
        Swapchain* sc = objOf<Swapchain>(args.a[1], K_SWAPCHAIN, "vkGetSwapchainImagesKHR");
        if (!sc || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!args.a[3]) { wr32(args.a[2], sc->nimg); return VK_SUCCESS; }
        U32 want = rd32(args.a[2]);
        U32 c = want < sc->nimg ? want : sc->nimg;
        std::vector<U64> imgs((size_t)(c ? c : 1));
        for (U32 i = 0; i < c; i++) imgs[i] = sc->imgs[i];
        writeArray(args.a[3], imgs, c);
        wr32(args.a[2], c);
        return c < sc->nimg ? VK_INCOMPLETE : VK_SUCCESS;
    }
    case VK64_fn_vkAcquireNextImageKHR: {
        // (device, swapchain, timeout, sem, fence, out u32*)
        Swapchain* sc = objOf<Swapchain>(args.a[1], K_SWAPCHAIN, "vkAcquireNextImageKHR");
        if (!sc) return VK_ERROR_INITIALIZATION_FAILED;
        sc->cur = (sc->cur + 1) % (sc->nimg ? sc->nimg : 1);
        wr32(args.a[5], sc->cur);
        if (args.a[3]) { Semaphore* s = objOf<Semaphore>(args.a[3], K_SEMAPHORE, "vkAcquireNextImageKHR"); if (s) s->signalled = 1; }
        if (args.a[4]) { Fence* f = objOf<Fence>(args.a[4], K_FENCE, "vkAcquireNextImageKHR"); if (f) f->signalled = 1; }
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPastPresentationTimingGOOGLE:
        if (args.a[2]) wr32(args.a[2], 0);   /* no timestamps */
        return VK_SUCCESS;

    case VK64_fn_vkResetFences: {
        U32 n = (U32)args.a[1];
        for (U32 i = 0; i < n; i++) {
            Fence* f = objOf<Fence>(rd64(args.a[2] + (U64)i * 8), K_FENCE, "vkResetFences");
            if (f) f->signalled = 0;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkWaitForFences: {
        /* Everything executes synchronously at submit, so a fence is always
         * already signalled (P1 section 1.2 "Sync"). That is what keeps an app's
         * Present wait loop from spinning forever. */
        U32 n = (U32)args.a[1];
        for (U32 i = 0; i < n; i++) {
            Fence* f = objOf<Fence>(rd64(args.a[2] + (U64)i * 8), K_FENCE, "vkWaitForFences");
            if (f) f->signalled = 1;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetFenceStatus: {
        Fence* f = objOf<Fence>(args.a[1], K_FENCE, "vkGetFenceStatus");
        if (!f) return VK_ERROR_INITIALIZATION_FAILED;
        return f->signalled ? VK_SUCCESS : VK_NOT_READY;
    }
    // D3D9 query sync: events are signalled inline (immediate execution), so
    // Set is visible to Get immediately. Occlusion returns one synthetic sample
    // and timestamps use the host monotonic clock.
    case VK64_fn_vkCreateEvent: {
        if (!args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        createObj<Event>(K_EVENT, id);
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkDestroyEvent:              dropObj(args.a[1]); return 0;
    case VK64_fn_vkSetEvent: {
        Event* e = objOf<Event>(args.a[1], K_EVENT, "vkSetEvent");
        if (!e) return VK_ERROR_INITIALIZATION_FAILED;
        e->signalled = 1;
        return VK_SUCCESS;
    }
    case VK64_fn_vkResetEvent: {
        Event* e = objOf<Event>(args.a[1], K_EVENT, "vkResetEvent");
        if (!e) return VK_ERROR_INITIALIZATION_FAILED;
        e->signalled = 0;
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetEventStatus: {
        Event* e = objOf<Event>(args.a[1], K_EVENT, "vkGetEventStatus");
        if (!e) return VK_ERROR_INITIALIZATION_FAILED;
        return e->signalled ? VK_EVENT_SET : VK_EVENT_RESET;
    }
    case VK64_fn_vkCmdSetEvent2:
    case VK64_fn_vkCmdSetEvent: {
        Event* e = objOf<Event>(args.a[1], K_EVENT, "vkCmdSetEvent");
        if (!e) return VK_ERROR_INITIALIZATION_FAILED;
        e->signalled = 1;
        return VK_SUCCESS;
    }
    case VK64_fn_vkCmdResetEvent: {
        Event* e = objOf<Event>(args.a[1], K_EVENT, "vkCmdResetEvent");
        if (!e) return VK_ERROR_INITIALIZATION_FAILED;
        e->signalled = 0;
        return VK_SUCCESS;
    }
    case VK64_fn_vkCmdWaitEvents:
        return VK_SUCCESS; // immediate execution: nothing to wait for
    case VK64_fn_vkCreateQueryPool: {
        QueryPoolCreateInfo64Local ci = {};
        if (!args.a[3] || !readStruct(args.a[1], ci)) return VK_ERROR_INITIALIZATION_FAILED;
        if (ci.queryType != vkquery::VKQUERY_TIMESTAMP) {
            vk64Unsupported(ci.queryType == vkquery::VKQUERY_OCCLUSION ? "occlusion-query" : "pipeline-statistics-query",
                            "query type is not implemented; VK_ERROR_FEATURE_NOT_PRESENT");
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        U64 id = 0;
        QueryPool* pool = createObj<QueryPool>(K_QUERYPOOL, id);
        pool->type = ci.queryType; pool->count = ci.queryCount; pool->flags = ci.flags;
        pool->results.resize(pool->count); pool->available.resize(pool->count);
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkDestroyQueryPool:          dropObj(args.a[1]); return 0;
    case VK64_fn_vkResetQueryPool: {
        QueryPool* pool = objOf<QueryPool>(args.a[1], K_QUERYPOOL, "vkResetQueryPool");
        if (!pool || args.a[2] > pool->count || args.a[3] > pool->count - args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 n = (U32)args.a[3];
        for (U32 i=0;i<n;i++) { pool->results[args.a[2]+i]=0; pool->available[args.a[2]+i]=0; }
        return VK_SUCCESS;
    }
    case VK64_fn_vkCmdResetQueryPool: {
        CmdBuf* cb=cbOf(args.a[0],"vkCmdResetQueryPool"); QueryPool* pool=objOf<QueryPool>(args.a[1],K_QUERYPOOL,"vkCmdResetQueryPool");
        VK64_DROP_IF(return 0,!cb||!pool||args.a[2]>pool->count||args.a[3]>pool->count-args.a[2]);
        Cmd c;c.kind=CMD_RESET_QUERY;c.a=args.a[1];c.b=args.a[2];c.c=args.a[3];pushCmd(cb,c);return 0;
    }
    case VK64_fn_vkCmdBeginQuery:
    case VK64_fn_vkCmdEndQuery: {
        vk64Unsupported("occlusion-query", "occlusion queries are not implemented; device feature bit is false");
        return 0;
    }
    case VK64_fn_vkCmdCopyQueryPoolResults: {
        CmdBuf* cb = cbOf(args.a[0], "vkCmdCopyQueryPoolResults");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_COPY_QUERY_RESULTS;
        c.queryCopy = { args.a[1], args.a[4], args.a[5], args.a[6],
                        (U32)args.a[2], (U32)args.a[3], (U32)args.a[7] };
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkGetQueryPoolResults: {
        QueryPool* q = objOf<QueryPool>(args.a[1], K_QUERYPOOL, "vkGetQueryPoolResults");
        if (!q || !args.a[5]) return VK_ERROR_INITIALIZATION_FAILED;
        return writeQueryResults(q, (U32)args.a[2], (U32)args.a[3], args.a[4],
                                 args.a[5], args.a[6], (U32)args.a[7]);
    }
    case VK64_fn_vkDeviceWaitIdle:
    case VK64_fn_vkQueueWaitIdle:
        return VK_SUCCESS;

    case VK64_fn_vkResetCommandBuffer: {
        CmdBuf* cb = cbOf(args.a[0], "vkResetCommandBuffer");
        if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
        resetCmdBuf(cb);
        return VK_SUCCESS;
    }
    case VK64_fn_vkResetCommandPool: {
        CmdPool* pool = objOf<CmdPool>(args.a[1], K_CMDPOOL, "vkResetCommandPool");
        if (!pool || pool->device != args.a[0]) return VK_ERROR_INITIALIZATION_FAILED;
        // Pools are independently reset by DXVK worker threads. Resetting every
        // CmdBuf erased draws still awaiting submission in a different pool.
        for (auto& kv : g_objs)
            if (kv.second->kind == K_CMDBUF && ((CmdBuf*)kv.second)->pool == args.a[1])
                resetCmdBuf((CmdBuf*)kv.second);
        return VK_SUCCESS;
    }
    case VK64_fn_vkFreeCommandBuffers: {
        U32 n = (U32)args.a[2];
        CmdPool* pool = objOf<CmdPool>(args.a[1], K_CMDPOOL, "vkFreeCommandBuffers");
        VK64_DROP_IF(return 0, !pool || pool->device != args.a[0]);
        for (U32 i = 0; i < n; i++) {
            U64 id = rd64(args.a[3] + (U64)i * 8);
            CmdBuf* cb = cbOf(id, "vkFreeCommandBuffers");
            if (cb && cb->pool == args.a[1]) dropObj(id);
        }
        return VK_SUCCESS;
    }

    // =====================================================================
    // C1. Creates (19 of the 85)
    // =====================================================================
    case VK64_fn_vkCreateInstance: {
        VkInstanceCreateInfo ci = {};
        if (!readStruct(args.a[0], ci) || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        // Lenient about extensions, exactly as P1 was: an unknown instance
        // extension is logged, not rejected, so a guest that asks for something
        // extra still gets an instance instead of an early exit.
        for (U32 i = 0; i < ci.enabledExtensionCount && ci.ppEnabledExtensionNames; i++)
            klog_fmt("vk64: vkCreateInstance: extension '%s'",
                     guestStr(rd64(ci.ppEnabledExtensionNames + (U64)i * 8)).c_str());
        VkApplicationInfo ai = {};
        readStruct(ci.pApplicationInfo, ai);
        U64 instId = 0;
        Instance* inst = createObj<Instance>(K_INSTANCE, instId);
        // One physical device per instance == one WebGPU adapter. It is owned by
        // the instance and torn down with it (P1 did the same).
        U64 pdId = 0;
        PhysDev* pd = createObj<PhysDev>(K_PHYSDEV, pdId);
        pd->instance = instId;
        inst->phys[0] = pdId;
        inst->physCount = 1;
        wr64(args.a[2], instId);
        klog_fmt("vk64: vkCreateInstance ok (apiVersion=%u, %u ext) instance=0x%llx",
                 (unsigned)ai.apiVersion, (unsigned)ci.enabledExtensionCount,
                 (unsigned long long)instId);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateDevice: {
        // (physDev, pCreateInfo*, pAllocator, out VkDevice*)
        VkDeviceCreateInfo ci = {};
        PhysDev* pd = objOf<PhysDev>(args.a[0], K_PHYSDEV, "vkCreateDevice");
        if (!pd || !readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!ci.queueCreateInfoCount) return VK_ERROR_INITIALIZATION_FAILED;
        U64 devId = 0, qId = 0;
        Device* dev = createObj<Device>(K_DEVICE, devId);
        // Rule: queues are created by vkCreateDevice; the getter only fetches.
        Queue* q = createObj<Queue>(K_QUEUE, qId);
        dev->physDev = args.a[0];
        dev->queue = qId;
        q->device = devId;
        wr64(args.a[3], devId);
        klog_fmt("vk64: vkCreateDevice ok (%u queue families requested)", (unsigned)ci.queueCreateInfoCount);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateXcbSurfaceKHR:
    case VK64_fn_vkCreateXlibSurfaceKHR:
    case VK64_fn_vkCreateWin32SurfaceKHR:
    case VK64_fn_vkCreateHeadlessSurfaceEXT: {
        // (instance, pCreateInfo*, pAllocator, out VkSurfaceKHR*)
        //
        // Win32 is the arm a DXVK-on-wine guest actually needs: d3d11.dll reaches
        // vulkan-1.dll, winevulkan calls dlopen("libvulkan.so.1") — which is us —
        // and then creates a WIN32 surface for the window winevulkan owns. Without
        // this arm there is no swapchain, period (audit §d).
        //
        // As with xcb and headless, the create-info is NOT read: the in-tree
        // vk/ headers carry no platform-surface declarations at all (no
        // VkWin32SurfaceCreateInfoKHR to widen), and there is no windowing system
        // behind the handle anyway — the id is the whole surface. Nothing needs
        // hwnd/pNext, and skipping the read is also what keeps the shim free of a
        // platform header.
        if (!objOf<Instance>(args.a[0], K_INSTANCE, "vkCreateSurfaceKHR") || !args.a[3])
            return VK_ERROR_INITIALIZATION_FAILED;
        wr64(args.a[3], createSurface());
        klog_fmt("vk64: %s -> surface id minted", fnName(fnId));
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateSwapchainKHR: {
        VkSwapchainCreateInfoKHR ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 scId = 0;
        Swapchain* sc = createObj<Swapchain>(K_SWAPCHAIN, scId);
        sc->w = ci.imageExtent.width;
        sc->h = ci.imageExtent.height;
        sc->format = ci.imageFormat;
        sc->nimg = ci.minImageCount < 2 ? 2 : (ci.minImageCount > 8 ? 8 : ci.minImageCount);
        sc->cur = 0;
        for (U32 i = 0; i < sc->nimg; i++) {
            U64 imId = 0;
            Image* im = createObj<Image>(K_IMAGE, imId);
            im->w = sc->w; im->h = sc->h; im->d = 1; im->layers = 1; im->mips = 1;
            im->format = sc->format; im->tiling = VK_IMAGE_TILING_OPTIMAL;
            im->usage = ci.imageUsage; im->samples = 1;
            sc->imgs[i] = imId;
        }
        wr64(args.a[3], scId);
        klog_fmt("vk64: vkCreateSwapchainKHR %ux%u fmt=%u nimg=%u", sc->w, sc->h, sc->format, sc->nimg);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateCommandPool: {
        VkCommandPoolCreateInfo ci = {};
        if (!objOf<Device>(args.a[0], K_DEVICE, "vkCreateCommandPool") || !readStruct(args.a[1], ci) || !args.a[3])
            return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        CmdPool* p = createObj<CmdPool>(K_CMDPOOL, id);
        p->device = args.a[0];
        p->queueFamily = ci.queueFamilyIndex;
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateBuffer: {
        VkBufferCreateInfo ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!ci.size) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Buffer* b = createObj<Buffer>(K_BUFFER, id);
        b->size = ci.size;
        b->usage = ci.usage;
        klog_fmt("vk64: vkCreateBuffer id=0x%llx size=%llu usage=0x%x", (unsigned long long)id, (unsigned long long)ci.size, ci.usage);
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetBufferDeviceAddress: {
        // (device, VkBufferDeviceAddressInfo*): vkd3d-proton uses the VA for
        // its internal GPU VA map (IASetVertexBuffers deref). There is no real
        // GPU behind the bridge, so the VA is just a unique non-zero identifier
        // per buffer. 256MB stride guarantees VA ranges never overlap, so
        // vkd3d's va_map range lookups (va + offset) stay unambiguous.
        struct { U64 sType; U64 pNext; U64 buffer; } info = {};
        VK64_DROP_IF(return 0, !args.a[1] || !g_mem);
        g_mem->memcpyFromGuest(&info, args.a[1], sizeof(info));
        Buffer* b = objOf<Buffer>(info.buffer, K_BUFFER, "vkGetBufferDeviceAddress");
        VK64_DROP_IF(return 0, !b);
        if (!b->devVa) {
            b->devVa = g_nextDevVa;
            g_nextDevVa += 0x10000000ULL;
        }
        return b->devVa;
    }
    case VK64_fn_vkCreateImage: {
        VkImageCreateInfo ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Image* im = createObj<Image>(K_IMAGE, id);
        im->w = ci.extent.width; im->h = ci.extent.height; im->d = ci.extent.depth;
        im->layers = ci.arrayLayers ? ci.arrayLayers : 1;
        im->mips = ci.mipLevels ? ci.mipLevels : 1;
        im->format = ci.format; im->tiling = ci.tiling; im->usage = ci.usage;
        im->samples = ci.samples;
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateImageView: {
        VkImageViewCreateInfo ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        Image* src = objOf<Image>(ci.image, K_IMAGE, "vkCreateImageView");
        if (!src) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        ImageView* v = createObj<ImageView>(K_IMAGEVIEW, id);
        v->image = ci.image;
        v->format = ci.format ? ci.format : src->format;
        v->aspect = ci.subresourceRange.aspectMask;
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateBufferView: {
        VkBufferViewCreateInfo ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        vk64Unsupported("texel-buffer-view", "formatted texel-buffer descriptors are not implemented; format properties omit texel-buffer features");
        wr64(args.a[3], 0);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    case VK64_fn_vkCreateSampler: {
        VkSamplerCreateInfo ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Sampler* s = createObj<Sampler>(K_SAMPLER, id);
        s->mag = ci.magFilter; s->min = ci.minFilter; s->mipmap = ci.mipmapMode;
        s->addrU = ci.addressModeU; s->addrV = ci.addressModeV; s->addrW = ci.addressModeW;
        s->mipLodBias = ci.mipLodBias; s->anisotropyEnable = ci.anisotropyEnable;
        s->maxAniso = ci.maxAnisotropy; s->compareEnable = ci.compareEnable; s->compareOp = ci.compareOp;
        s->minLod = ci.minLod; s->maxLod = ci.maxLod; s->borderColor = ci.borderColor;
        vk64AuditSampler(ci.addressModeU, ci.addressModeV, ci.addressModeW, ci.mipLodBias, ci.borderColor);
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateRenderPass: {
        VkRenderPassCreateInfo ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        RenderPass* rp = createObj<RenderPass>(K_RENDERPASS, id);
        rp->natt = ci.attachmentCount > VK64_MAX_ATTACH ? VK64_MAX_ATTACH : ci.attachmentCount;
        std::vector<VkAttachmentDescription> atts;
        readArray(ci.pAttachments, rp->natt, atts);
        for (U32 i = 0; i < atts.size(); i++) {
            rp->formats[i] = atts[i].format;
            rp->loadOps[i] = atts[i].loadOp;
            rp->storeOps[i] = atts[i].storeOp;
            rp->stencilLoadOps[i] = atts[i].stencilLoadOp;
            rp->stencilStoreOps[i] = atts[i].stencilStoreOp;
            rp->samples[i] = atts[i].samples;
        }
        if (atts.size() > 1) rp->depthFmt = atts[1].format;
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateFramebuffer: {
        VkFramebufferCreateInfo ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Framebuffer* fb = createObj<Framebuffer>(K_FRAMEBUFFER, id);
        fb->rp = ci.renderPass;
        fb->natt = ci.attachmentCount > VK64_MAX_ATTACH ? VK64_MAX_ATTACH : ci.attachmentCount;
        std::vector<VkImageView> views;
        readArray(ci.pAttachments, fb->natt, views);
        for (U32 i = 0; i < views.size(); i++) fb->views[i] = views[i];
        fb->w = ci.width; fb->h = ci.height;
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateGraphicsPipelines: {
        // (device, cache, n, pCreateInfos*, pAllocator, out VkPipeline*)
        U32 n = (U32)args.a[2];
        if (!objOf<Device>(args.a[0], K_DEVICE, "vkCreateGraphicsPipelines") || !n || !args.a[3] || !args.a[5])
            return VK_ERROR_INITIALIZATION_FAILED;
        std::vector<VkGraphicsPipelineCreateInfo> cis;
        readArray(args.a[3], n, cis);
        VkResult result = VK_SUCCESS;
        for (U32 i = 0; i < n; i++) {
            const VkGraphicsPipelineCreateInfo& ci = cis[i];
            U64 id = 0;
            Pipeline* p = createObj<Pipeline>(K_PIPELINE, id);
            // Each pXxxState is a guest VA: read the sub-state structs out one
            // at a time (they are small and fixed).
            VkPipelineInputAssemblyStateCreateInfo ia = {};
            VkPipelineRasterizationStateCreateInfo rs = {};
            VkPipelineDepthStencilStateCreateInfo ds = {};
            VkPipelineColorBlendStateCreateInfo cbs = {};
            readStruct(ci.pInputAssemblyState, ia);
            readStruct(ci.pRasterizationState, rs);
            readStruct(ci.pDepthStencilState, ds);
            readStruct(ci.pColorBlendState, cbs);
            p->topology = ia.topology;
            p->primitiveRestartEnable = ia.primitiveRestartEnable;
            p->cull = rs.cullMode;
            p->front = rs.frontFace;
            p->depthBiasEnable = rs.depthBiasEnable; p->depthClampEnable = rs.depthClampEnable;
            p->depthBiasConstantFactor = rs.depthBiasConstantFactor; p->depthBiasClamp = rs.depthBiasClamp;
            p->depthBiasSlopeFactor = rs.depthBiasSlopeFactor;
            p->depthTest = ds.depthTestEnable;
            p->depthWrite = ds.depthWriteEnable;
            p->depthOp = ds.depthCompareOp;
            captureStencil(*p, ds);
            // pVertexInputState: binding + attribute descriptions. Recorded
            // because the consumer cannot infer any of it — the draw call carries
            // no format, no stride and no offset — and without it there is no
            // vertex layout to build and nothing rasterizes.
            VkPipelineVertexInputStateCreateInfo vi = {};
            if (readStruct(ci.pVertexInputState, vi)) {
                p->nvb = vi.vertexBindingDescriptionCount > VK64_MAX_VERT_BINDINGS
                             ? VK64_MAX_VERT_BINDINGS : vi.vertexBindingDescriptionCount;
                p->nva = vi.vertexAttributeDescriptionCount > VK64_MAX_VERT_ATTRIBS
                             ? VK64_MAX_VERT_ATTRIBS : vi.vertexAttributeDescriptionCount;
                std::vector<VkVertexInputBindingDescription> vbs;
                readArray(vi.pVertexBindingDescriptions, p->nvb, vbs);
                for (U32 b = 0; b < vbs.size(); b++) {
                    p->vbStride[b] = vbs[b].stride;
                    p->vbInputRate[b] = vbs[b].inputRate;
                }
                std::vector<VkVertexInputAttributeDescription> vas;
                readArray(vi.pVertexAttributeDescriptions, p->nva, vas);
                for (U32 a = 0; a < vas.size(); a++) {
                    p->vaLocation[a] = vas[a].location;
                    p->vaBinding[a] = vas[a].binding;
                    p->vaFormat[a] = vas[a].format;
                    p->vaOffset[a] = vas[a].offset;
                }
            }
            VkPipelineDynamicStateCreateInfo dynamic = {};
            if (readStruct(ci.pDynamicState, dynamic)) {
                std::vector<VkDynamicState> states;
                readArray(dynamic.pDynamicStates, dynamic.dynamicStateCount, states);
                for (auto state : states) {
                    const U32 value = (U32)state;
                    if (value == (U32)VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE) p->dynamicVertexStride = true;
                    if (value == (U32)VK_DYNAMIC_STATE_DEPTH_BIAS) p->depthBiasDynamic = true;
                    if (value == 1000267000u) p->cullDynamic = true;    // VK_DYNAMIC_STATE_CULL_MODE(_EXT)
                    if (value == 1000267001u) p->frontDynamic = true;   // VK_DYNAMIC_STATE_FRONT_FACE(_EXT)
                    if (value >= 6 && value <= 8) p->stencil.dynamicMask |= 1u << (value - 6);
                    if (value == 4) p->blendConstantsDynamic = true;
                }
            }
            std::vector<VkPipelineColorBlendAttachmentState> blends;
            readArray(cbs.pAttachments, std::min<U32>(cbs.attachmentCount, VK64_MAX_ATTACH), blends);
            captureColorBlend(*p, blends);
            memcpy(p->blendConstants, cbs.blendConstants, sizeof(p->blendConstants));
            std::vector<VkPipelineShaderStageCreateInfo> stages;
            readArray(ci.pStages, ci.stageCount, stages);
            {
                PipelineAudit audit;
                for (const auto& st : stages) audit.stageMask |= (U32)st.stage;
                audit.topology = ia.topology;
                audit.polygonMode = rs.polygonMode;
                audit.rasterizerDiscard = rs.rasterizerDiscardEnable;
                audit.logicOp = cbs.logicOpEnable;
                audit.depthBounds = ds.depthBoundsTestEnable;
                if (readStruct(ci.pVertexInputState, vi))
                    for (U64 cur = vi.pNext; cur && g_mem; cur = g_mem->readq(cur + 8)) {
                        if (g_mem->readd(cur) != 1000190001u) continue;   // VkPipelineVertexInputDivisorStateCreateInfoEXT
                        const U32 cnt = std::min<U32>(g_mem->readd(cur + 16), 64);
                        const U64 arr = g_mem->readq(cur + 24);
                        for (U32 k = 0; k < cnt && arr; k++)
                            if (g_mem->readd(arr + (U64)k * 8 + 4) != 1) audit.vertexDivisor = true;
                    }
                VkPipelineViewportStateCreateInfo vp = {};
                if (readStruct(ci.pViewportState, vp)) audit.viewportCount = vp.viewportCount;
                VkPipelineMultisampleStateCreateInfo ms = {};
                if (readStruct(ci.pMultisampleState, ms)) {
                    audit.sampleCount = (U32)ms.rasterizationSamples;
                    audit.sampleShading = ms.sampleShadingEnable;
                    audit.alphaToOne = ms.alphaToOneEnable;
                }
                if (vk64AuditGraphicsPipeline(audit)) {
                    dropObj(id);
                    wr64(args.a[5] + (U64)i * 8, 0);
                    result = VK_ERROR_FEATURE_NOT_PRESENT;
                    continue;
                }
            }
            for (const VkPipelineShaderStageCreateInfo& st : stages) {
                if (st.stage == VK_SHADER_STAGE_VERTEX_BIT) {
                    p->vs = st.module;
                    captureStageSpec(st.pSpecializationInfo, p->vsSpec);
                }
                if (st.stage == VK_SHADER_STAGE_FRAGMENT_BIT) {
                    p->fs = st.module;
                    captureStageSpec(st.pSpecializationInfo, p->fsSpec);
                }
            }
            p->layout = ci.layout;
            p->rp = ci.renderPass;
            wr64(args.a[5] + (U64)i * 8, id);
        }
        return result;
    }
    case VK64_fn_vkCreateComputePipelines: {
        // (device, cache, n, pCreateInfos*, pAllocator, out VkPipeline*)
        // vkd3d-proton builds internal compute pipelines (clear-UAV ops) during
        // device creation; without this the benign stub returns -2 and
        // D3D12CreateDevice fails E_OUTOFMEMORY. Capture is minimal: the
        // compute shader module + layout, enough for the page tier to key on.
        U32 n = (U32)args.a[2];
        if (!objOf<Device>(args.a[0], K_DEVICE, "vkCreateComputePipelines") || !n || !args.a[3] || !args.a[5])
            return VK_ERROR_INITIALIZATION_FAILED;
        for (U32 i = 0; i < n; i++) {
            VkComputePipelineCreateInfo ci = {};
            g_mem->memcpyFromGuest(&ci, args.a[3] + (U64)i * sizeof(ci), sizeof(ci));
            U64 id = 0;
            Pipeline* pl = createObj<Pipeline>(K_PIPELINE, id);
            pl->cs = ci.stage.module;
            pl->layout = ci.layout;
            wr64(args.a[5] + (U64)i * 8, id);
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreatePipelineLayout: {
        VkPipelineLayoutCreateInfo ci = {};
        klog_fmt("vk64: vkCreatePipelineLayout trap reached (real wrapper, not benign)");
        if (!readStruct(args.a[1], ci) || !args.a[3]) {
            klog_fmt("vk64: vkCreatePipelineLayout FAILED: readOk=%d outNull=%d",
                     (int)(readStruct(args.a[1], ci)), (int)(!args.a[3]));
            return VK_ERROR_INITIALIZATION_FAILED;
        }
        U64 id = 0;
        PipelineLayout* l = createObj<PipelineLayout>(K_PIPELAYOUT, id);
        l->nsets = ci.setLayoutCount > VK64_MAX_SETS ? VK64_MAX_SETS : ci.setLayoutCount;
        for (U32 i = 0; i < l->nsets; i++) l->sets[i] = rd64(ci.pSetLayouts + (U64)i * 8);
        wr64(args.a[3], id);
        klog_fmt("vk64: vkCreatePipelineLayout OK: id=%llu nsets=%u", (unsigned long long)id, (unsigned)l->nsets);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateDescriptorSetLayout: {
        VkDescriptorSetLayoutCreateInfo ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        DescSetLayout* l = createObj<DescSetLayout>(K_SETLAYOUT, id);
        l->nbind = ci.bindingCount > VK64_MAX_BINDINGS ? VK64_MAX_BINDINGS : ci.bindingCount;
        std::vector<VkDescriptorSetLayoutBinding> binds;
        readArray(ci.pBindings, l->nbind, binds);
        for (U32 i = 0; i < binds.size(); i++) {
            l->binds[i].binding = binds[i].binding;
            l->binds[i].type = binds[i].descriptorType;
        }
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateDescriptorPool: {
        if (!objOf<Device>(args.a[0], K_DEVICE, "vkCreateDescriptorPool") || !args.a[3])
            return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        createObj<DescPool>(K_DESCPOOL, id);
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateShaderModule: {
        VkShaderModuleCreateInfo ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!ci.codeSize || !ci.pCode) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        ShaderModule* m = createObj<ShaderModule>(K_SHADER, id);
        // One memcpyFromGuest of the SPIR-V words; dedupe by the same djb2 hash
        // P1 used for its .spv filenames, and KEEP the words (destroy is a
        // refcount drop — vkcube destroys both modules right after pipeline
        // creation, which is legal).
        m->code.resize((size_t)ci.codeSize);
        g_mem->memcpyFromGuest(m->code.data(), ci.pCode, ci.codeSize);
        m->hash = blob_hash(m->code.data(), m->code.size());
        vk64AuditSpirv(m->code);
        wr64(args.a[3], id);
        klog_fmt("vk64: vkCreateShaderModule %llu bytes (hash %016llx)",
                 (unsigned long long)ci.codeSize, (unsigned long long)m->hash);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreatePipelineCache: {
        if (!objOf<Device>(args.a[0], K_DEVICE, "vkCreatePipelineCache") || !args.a[3])
            return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        createObj<PipelineCache>(K_PIPECACHE, id);
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateFence: {
        VkFenceCreateInfo ci = {};
        if (!readStruct(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Fence* f = createObj<Fence>(K_FENCE, id);
        f->signalled = (ci.flags & VK_FENCE_CREATE_SIGNALED_BIT) ? 1 : 0;
        wr64(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateSemaphore: {
        VkSemaphoreCreateInfo ci = {};
        if (!objOf<Device>(args.a[0], K_DEVICE, "vkCreateSemaphore") ||
            !readStruct(args.a[1], ci) || !args.a[3])
            return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Semaphore* s = createObj<Semaphore>(K_SEMAPHORE, id);
        // Timeline detection: VkSemaphoreTypeCreateInfo hangs off
        // VkSemaphoreCreateInfo::pNext, and the walker must not stop at the first
        // unknown sType (a guest may chain something else first).
        for (U64 cur = ci.pNext; cur && g_mem;) {
            if (g_mem->readd(cur) == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
                VkSemaphoreTypeCreateInfo ti = {};
                g_mem->memcpyFromGuest(&ti, cur, sizeof(ti));
                s->isTimeline = (ti.semaphoreType == VK_SEMAPHORE_TYPE_TIMELINE) ? 1 : 0;
                s->counter = ti.initialValue;
                break;
            }
            cur = g_mem->readq(cur + 8);
        }
        wr64(args.a[3], id);
        klog_fmt("vk64: vkCreateSemaphore%s%s",
                 s->isTimeline ? " (timeline" : "", s->isTimeline ? ")" : "");
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetSemaphoreCounterValue: {
        // (device, semaphore, out u64*)
        Semaphore* s = objOf<Semaphore>(args.a[1], K_SEMAPHORE, "vkGetSemaphoreCounterValue");
        if (!s || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        wr64(args.a[2], s->counter);
        return VK_SUCCESS;
    }
    case VK64_fn_vkWaitSemaphores: {
        // (device, pWaitInfo*, timeout)
        VkSemaphoreWaitInfo wi = {};
        if (!readStruct(args.a[1], wi)) return VK_ERROR_INITIALIZATION_FAILED;
        if (wi.semaphoreCount && (!wi.pSemaphores || !wi.pValues)) return VK_ERROR_INITIALIZATION_FAILED;
        bool any = false, all = true;
        for (U32 i = 0; i < wi.semaphoreCount; i++) {
            U64 semId = rd64(wi.pSemaphores + (U64)i * 8);
            U64 want  = rd64(wi.pValues + (U64)i * 8);
            Semaphore* s = objOf<Semaphore>(semId, K_SEMAPHORE, "vkWaitSemaphores");
            if (!s) return VK_ERROR_INITIALIZATION_FAILED;
            const bool reached = s->counter >= want;
            any |= reached; all &= reached;
        }
        return ((wi.flags & VK_SEMAPHORE_WAIT_ANY_BIT) ? any : all) ? VK_SUCCESS : VK_TIMEOUT;
    }
    case VK64_fn_vkSignalSemaphore: {
        // (device, pSignalInfo*)
        VkSemaphoreSignalInfo si = {};
        if (!readStruct(args.a[1], si)) return VK_ERROR_INITIALIZATION_FAILED;
        Semaphore* s = objOf<Semaphore>(si.semaphore, K_SEMAPHORE, "vkSignalSemaphore");
        if (!s) return VK_ERROR_INITIALIZATION_FAILED;
        if (si.value > s->counter) s->counter = si.value;   /* monotonic */
        return VK_SUCCESS;
    }

    // =====================================================================
    // C2. Allocation / binding / update / map (8 of the 85)
    // =====================================================================
    case VK64_fn_vkAllocateMemory: {
        VkMemoryAllocateInfo ai = {};
        if (!objOf<Device>(args.a[0], K_DEVICE, "vkAllocateMemory") || !readStruct(args.a[1], ai) || !args.a[3])
            return VK_ERROR_INITIALIZATION_FAILED;
        if (!ai.allocationSize) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        DeviceMemory* m = createObj<DeviceMemory>(K_MEMORY, id);
        m->size = ai.allocationSize;
        m->memIndex = ai.memoryTypeIndex;
        // GUEST memory, not host malloc: the guest's stores go through the
        // CPU64 store path into K64Page buffers and cannot write to a wasm-heap
        // address, so the only substrate a guest-visible map can use is the
        // kernel's own mapper (tasks/p1-final.md section 2.4).
        m->va = g_mem->mmapReserveAndMap(ai.allocationSize, K_PROT_READ | K_PROT_WRITE);
        wr64(args.a[3], id);
        klog_fmt("vk64: vkAllocateMemory %llu bytes -> guest va 0x%llx (type %u)",
                 (unsigned long long)m->size, (unsigned long long)m->va, (unsigned)m->memIndex);
        return VK_SUCCESS;
    }
    case VK64_fn_vkAllocateCommandBuffers: {
        VkCommandBufferAllocateInfo ai = {};
        if (!readStruct(args.a[1], ai) || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        CmdPool* pool = objOf<CmdPool>(ai.commandPool, K_CMDPOOL, "vkAllocateCommandBuffers");
        if (!pool || pool->device != args.a[0]) return VK_ERROR_INITIALIZATION_FAILED;
        for (U32 i = 0; i < ai.commandBufferCount; i++) {
            U64 id = 0;
            CmdBuf* cb = createObj<CmdBuf>(K_CMDBUF, id);
            cb->pool = ai.commandPool;
            wr64(args.a[2] + (U64)i * 8, id);
        }
        klog_fmt("vk64: vkAllocateCommandBuffers x%u", (unsigned)ai.commandBufferCount);
        return VK_SUCCESS;
    }
    case VK64_fn_vkAllocateDescriptorSets: {
        VkDescriptorSetAllocateInfo ai = {};
        if (!readStruct(args.a[1], ai) || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        for (U32 i = 0; i < ai.descriptorSetCount; i++) {
            U64 id = 0;
            DescSet* s = createObj<DescSet>(K_SET, id);
            s->layout = rd64(ai.pSetLayouts + (U64)i * 8);
            wr64(args.a[2] + (U64)i * 8, id);
        }
        return VK_SUCCESS;
    }
    // VkBindBufferMemoryInfo / VkBindImageMemoryInfo share this x86-64 layout.
    struct VkBindMemInfo2 {
        U32 sType;
        U32 _pad0;
        U64 pNext;
        U64 handle;       // buffer or image
        U64 memory;
        U64 memoryOffset;
    };
    static_assert(sizeof(VkBindMemInfo2) == 40, "VkBind*MemoryInfo layout");

    case VK64_fn_vkBindBufferMemory: {
        Buffer* b = objOf<Buffer>(args.a[1], K_BUFFER, "vkBindBufferMemory");
        DeviceMemory* m = objOf<DeviceMemory>(args.a[2], K_MEMORY, "vkBindBufferMemory");
        if (!b || !m) return VK_ERROR_INITIALIZATION_FAILED;
        if (args.a[3] >= m->size) return VK_ERROR_INITIALIZATION_FAILED;
        b->mem = args.a[2];
        b->memOff = args.a[3];
        return VK_SUCCESS;
    }
    case VK64_fn_vkBindImageMemory: {
        Image* im = objOf<Image>(args.a[1], K_IMAGE, "vkBindImageMemory");
        DeviceMemory* m = objOf<DeviceMemory>(args.a[2], K_MEMORY, "vkBindImageMemory");
        if (!im || !m) return VK_ERROR_INITIALIZATION_FAILED;
        im->mem = args.a[2];
        im->memOff = args.a[3];
        im->pixBytes = imageDataBytes(*im);
        return VK_SUCCESS;
    }
    case VK64_fn_vkBindBufferMemory2:
    case VK64_fn_vkBindBufferMemory2KHR: {
        // (device, bindInfoCount, pBindInfos*) — same record-only semantics as
        // vkBindBufferMemory; the host has no real Vulkan device to bind on.
        U32 n = (U32)args.a[1];
        std::vector<VkBindMemInfo2> infos;
        readArray(args.a[2], n, infos);
        for (const auto& info : infos) {
            Buffer* b = objOf<Buffer>(info.handle, K_BUFFER, "vkBindBufferMemory2");
            DeviceMemory* m = objOf<DeviceMemory>(info.memory, K_MEMORY, "vkBindBufferMemory2");
            if (!b || !m) return VK_ERROR_INITIALIZATION_FAILED;
            if (info.memoryOffset >= m->size) return VK_ERROR_INITIALIZATION_FAILED;
            b->mem = info.memory;
            b->memOff = info.memoryOffset;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkBindImageMemory2:
    case VK64_fn_vkBindImageMemory2KHR: {
        // (device, bindInfoCount, pBindInfos*) — same record-only semantics as
        // vkBindImageMemory.
        U32 n = (U32)args.a[1];
        std::vector<VkBindMemInfo2> infos;
        readArray(args.a[2], n, infos);
        for (const auto& info : infos) {
            Image* im = objOf<Image>(info.handle, K_IMAGE, "vkBindImageMemory2");
            DeviceMemory* m = objOf<DeviceMemory>(info.memory, K_MEMORY, "vkBindImageMemory2");
            if (!im || !m) return VK_ERROR_INITIALIZATION_FAILED;
            im->mem = info.memory;
            im->memOff = info.memoryOffset;
            im->pixBytes = imageDataBytes(*im);
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkUpdateDescriptorSets: {
        // (device, nWrite, pWrites*, nCopy, pCopies*)
        U32 nw = (U32)args.a[1];
        std::vector<VkWriteDescriptorSet> writes;
        readArray(args.a[2], nw, writes);
        for (U32 i = 0; i < nw; i++) {
            const VkWriteDescriptorSet& w = writes[i];
            DescSet* s = objOf<DescSet>(w.dstSet, K_SET, "vkUpdateDescriptorSets");
            VK64_DROP_IF(continue, !s || w.dstBinding >= VK64_MAX_BINDINGS);
            Binding* b = nullptr;
            for (U32 k = 0; k < s->nbind; k++)
                if (s->binds[k].binding == w.dstBinding) { b = &s->binds[k]; break; }
            if (!b) {
                VK64_DROP_IF(continue, s->nbind >= VK64_MAX_BINDINGS);
                b = &s->binds[s->nbind++];
                b->binding = w.dstBinding;
            }
            // Fidelity: the write's own descriptorType (not the set layout's) and
            // its ARRAY GEOMETRY. A DXVK caller writes descriptor arrays and
            // partial-overlap ranges (dstArrayElement/count), and collapsing those
            // to "element 0 of binding N" makes the recorded set a different set
            // from the one the app declared. The element payloads stay in the flat
            // Binding table for now (schema v2 is P3); index and count are honest
            // today so that table can grow without re-reading the guest.
            b->type = w.descriptorType;
            b->dstArrayElement = w.dstArrayElement;
            b->count = w.descriptorCount ? w.descriptorCount : 1;
            if (w.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                w.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                VkDescriptorBufferInfo bi = {};
                readStruct(w.pBufferInfo, bi);
                b->obj = bi.buffer;
                b->range = bi.range;
                b->bufOff = bi.offset;
            } else if (w.descriptorType == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
                // The caller's inline data must be consumed before this update returns.
                for (U64 cur = w.pNext; cur && g_mem;) {
                    if (g_mem->readd(cur) == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK) {
                        VkWriteDescriptorSetInlineUniformBlock ib = {};
                        g_mem->memcpyFromGuest(&ib, cur, sizeof(ib));
                        updateInlineUniform(*b, w.dstArrayElement, ib.pData, ib.dataSize);
                        break;
                    }
                    cur = g_mem->readq(cur + 8);
                }
            } else if (w.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                       w.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
                       w.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLER) {
                // image-only / sampler-only: keep the one handle each names, so a
                // storage image is not misreported as a sampled image
                VkDescriptorImageInfo ii = {};
                readStruct(w.pImageInfo, ii);
                b->obj = ii.imageView;
                b->range = ii.sampler;
            } else {
                // combined image sampler (and texel buffers, which share the
                // VkDescriptorBufferInfo shape)
                // FIX (2026-10-09): For COMBINED_IMAGE_SAMPLER, w.pBufferInfo is
                // non-null garbage (same as w.pImageInfo). Trust descriptorType,
                // always read VkDescriptorImageInfo.
                {
                    VkDescriptorImageInfo ii = {};
                    readStruct(w.pImageInfo, ii);
                    b->obj = ii.imageView;
                    b->range = ii.sampler;
                }
            }
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateDescriptorUpdateTemplate: {
        // (device, pCreateInfo*, pAllocator, out VkDescriptorUpdateTemplate*)
        VkDescriptorUpdateTemplateCreateInfo ci = {};
        if (!readStruct(args.a[1], ci)) return VK_ERROR_INITIALIZATION_FAILED;
        if (!args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        DescTemplate* t = createObj<DescTemplate>(K_TEMPLATE, id);
        std::vector<VkDescriptorUpdateTemplateEntry> entries;
        readArray(ci.pDescriptorUpdateEntries, ci.descriptorUpdateEntryCount, entries);
        for (auto& e : entries) {
            VK64_DROP_IF(break, t->nentry >= VK64_MAX_BINDINGS);
            TmplEntry& te = t->entries[t->nentry++];
            te.binding = e.dstBinding; te.arrayElement = e.dstArrayElement;
            te.count = e.descriptorCount; te.type = e.descriptorType;
            te.offset = e.offset; te.stride = e.stride;
        }
        wr64(args.a[3], id);
        klog_fmt("vk64: vkCreateDescriptorUpdateTemplate -> 0x%llx (%u entries)",
                 (unsigned long long)id, (unsigned)t->nentry);
        return VK_SUCCESS;
    }
    case VK64_fn_vkDestroyDescriptorUpdateTemplate: {
        // (device, template, pAllocator)
        dropObj(args.a[1]);
        return VK_SUCCESS;
    }
    case VK64_fn_vkUpdateDescriptorSetWithTemplate: {
        // (device, descriptorSet, descriptorUpdateTemplate, pData)
        DescSet* s = objOf<DescSet>(args.a[1], K_SET, "vkUpdateDescriptorSetWithTemplate");
        DescTemplate* t = objOf<DescTemplate>(args.a[2], K_TEMPLATE, "vkUpdateDescriptorSetWithTemplate");
        if (!s || !t || !args.a[3]) return VK_SUCCESS;
        U64 dataBase = args.a[3];
        for (U32 ei = 0; ei < t->nentry; ei++) {
            const TmplEntry& te = t->entries[ei];
            // Inline descriptorCount is a byte count; stride is ignored.
            const U32 elements = te.type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK ? 1 : te.count;
            for (U32 i = 0; i < elements; i++) {
                U64 dAddr = dataBase + te.offset + (U64)i * te.stride;
                U32 binding = te.binding;
                U32 elem = te.arrayElement + i;
                VK64_DROP_IF(continue, binding >= VK64_MAX_BINDINGS);
                Binding* b = nullptr;
                for (U32 k = 0; k < s->nbind; k++)
                    if (s->binds[k].binding == binding) { b = &s->binds[k]; break; }
                if (!b) {
                    VK64_DROP_IF(continue, s->nbind >= VK64_MAX_BINDINGS);
                    b = &s->binds[s->nbind++];
                    b->binding = binding;
                }
                b->type = te.type;
                b->dstArrayElement = elem;
                b->count = 1;
                if (te.type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
                    updateInlineUniform(*b, te.arrayElement, dAddr, te.count);
                } else if (te.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                    te.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                    U64 buf = rd64(dAddr); U64 off = rd64(dAddr + 8); U64 range = rd64(dAddr + 16);
                    b->obj = buf; b->range = range; b->bufOff = off;
                } else if (te.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                           te.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                    U64 view = rd64(dAddr + 8);
                    b->obj = view; b->range = 0;
                } else if (te.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    // VkDescriptorImageInfo: sampler @0, imageView @8.
                    // (2026-10-09: D3D9 textured draw was black — template path
                    // dropped the image view, so no IMAGE_DATA was emitted.)
                    U64 samp = rd64(dAddr);
                    U64 view = rd64(dAddr + 8);
                    b->obj = view; b->range = samp;
                } else if (te.type == VK_DESCRIPTOR_TYPE_SAMPLER) {
                    U64 samp = rd64(dAddr);
                    b->obj = samp; b->range = 0;
                } else if (te.type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
                           te.type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
                    U64 bv = rd64(dAddr);
                    b->obj = bv; b->range = 0;
                }
            }
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkMapMemory: {
        // (device, memory, offset, size, flags, out void**) -> the GUEST VA, so
        // the guest addresses its own pages directly.
        DeviceMemory* m = objOf<DeviceMemory>(args.a[1], K_MEMORY, "vkMapMemory");
        if (!m || !args.a[5]) return VK_ERROR_MEMORY_MAP_FAILED;
        if (args.a[2] > m->size) return VK_ERROR_MEMORY_MAP_FAILED;
        wr64(args.a[5], m->va + args.a[2]);
        // Mark as mapped: guest may write via CPU, so buffer dirty-tracking
        // cannot assume content is unchanged. emitBufferData will always
        // re-hash buffers on mapped memory (conservative, safe).
        m->wasMapped = true;
        return VK_SUCCESS;
    }
    case VK64_fn_vkFlushMappedMemoryRanges:
    case VK64_fn_vkInvalidateMappedMemoryRanges: {
        // All exposed host-visible memory is coherent guest memory. A valid
        // flush/invalidate has no extra work, but must not mask invalid ranges.
        if (!objOf<Device>(args.a[0], K_DEVICE, "vkMappedMemoryRanges") ||
            (args.a[1] && !args.a[2])) return VK_ERROR_MEMORY_MAP_FAILED;
        std::vector<VkMappedMemoryRange> ranges;
        readArray(args.a[2], (U32)args.a[1], ranges);
        for (const auto& range : ranges) {
            DeviceMemory* mem = objOf<DeviceMemory>(range.memory, K_MEMORY, "vkMappedMemoryRanges");
            if (!mem || !mem->va || mem->memIndex == 1 || range.offset >= mem->size)
                return VK_ERROR_MEMORY_MAP_FAILED;
            if (range.size != VK_WHOLE_SIZE &&
                (!range.size || range.size > mem->size - range.offset))
                return VK_ERROR_MEMORY_MAP_FAILED;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkUnmapMemory:
        return VK_SUCCESS;   /* nothing to release: it is ordinary guest memory */

    // =====================================================================
    // C3. Destroys (20 of the 85)
    // =====================================================================
    case VK64_fn_vkDestroyInstance: {
        Instance* inst = objOf<Instance>(args.a[0], K_INSTANCE, "vkDestroyInstance");
        if (inst) {
            for (U32 i = 0; i < inst->physCount; i++) dropObj(inst->phys[i]);
            dropObj(args.a[0]);
        }
        return 0;
    }
    case VK64_fn_vkDestroyDevice: {
        Device* dev = objOf<Device>(args.a[0], K_DEVICE, "vkDestroyDevice");
        if (dev) {
            U64 q = dev->queue;
            dropObj(args.a[0]);
            dropObj(q);
        }
        return 0;
    }
    case VK64_fn_vkDestroySurfaceKHR:      dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyCommandPool: {
        if (!args.a[1]) return 0;
        CmdPool* pool = objOf<CmdPool>(args.a[1], K_CMDPOOL, "vkDestroyCommandPool");
        VK64_DROP_IF(return 0, !pool || pool->device != args.a[0]);
        // Collect first: dropObj erases the shared object map.
        std::vector<U64> children;
        for (const auto& kv : g_objs)
            if (kv.second->kind == K_CMDBUF && ((CmdBuf*)kv.second)->pool == args.a[1])
                children.push_back(kv.first);
        for (U64 id : children) dropObj(id);
        dropObj(args.a[1]);
        return 0;
    }
    case VK64_fn_vkDestroyImage:           g_imageContentHash.erase(args.a[1]); dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyBuffer:          { U64 id = args.a[1]; for (auto it = g_bufferContentHash.begin(); it != g_bufferContentHash.end();) { if (std::get<0>(it->first) == id) it = g_bufferContentHash.erase(it); else ++it; } dropObj(id); return 0; }
    case VK64_fn_vkDestroyImageView:       dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroySampler:         dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyRenderPass:      dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyFramebuffer:     dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyPipeline:        dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyPipelineLayout:  dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyDescriptorSetLayout: dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyDescriptorPool:  dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyPipelineCache:   dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyFence:           dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroySemaphore:       dropObj(args.a[1]); return 0;
    case VK64_fn_vkDestroyShaderModule: {
        // Retain the SPIR-V words: pipelines created from this module are still
        // live and vkcube destroys the module immediately after
        // vkCreateGraphicsPipelines (legal). This is the bounded, deliberate
        // leak P1 documented; the bytes stay reachable through the pipeline.
        ShaderModule* m = objOf<ShaderModule>(args.a[1], K_SHADER, "vkDestroyShaderModule");
        if (m) m->destroyed++;
        return 0;
    }
    case VK64_fn_vkDestroySwapchainKHR: {
        Swapchain* sc = objOf<Swapchain>(args.a[1], K_SWAPCHAIN, "vkDestroySwapchainKHR");
        if (sc) {
            for (U32 i = 0; i < sc->nimg; i++) dropObj(sc->imgs[i]);
            dropObj(args.a[1]);
        }
        return 0;
    }
    case VK64_fn_vkFreeMemory: {
        DeviceMemory* m = objOf<DeviceMemory>(args.a[1], K_MEMORY, "vkFreeMemory");
        if (m) {
            if (m->va) g_mem->munmap(m->va, m->size);
            dropObj(args.a[1]);
        }
        return 0;
    }

    // =====================================================================
    // D. Command recording (11 of the 85)
    // =====================================================================
    case VK64_fn_vkBeginCommandBuffer: {
        CmdBuf* cb = cbOf(args.a[0], "vkBeginCommandBuffer");
        if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
        resetCmdBuf(cb);
        return VK_SUCCESS;
    }
    case VK64_fn_vkEndCommandBuffer: {
        CmdBuf* cb = cbOf(args.a[0], "vkEndCommandBuffer");
        if (vkTrace()) klog_fmt("vk64: vkEndCommandBuffer ncmd=%u", cb ? (unsigned)cb->cmds.size() : 0);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCmdBeginRenderPass: {
        // (cmdBuf, pRenderPassBegin* (clear values!), contents)
        klog_fmt("vk64: BEGIN_RP_CALLED cb=0x%llx", (unsigned long long)args.a[0]);
        CmdBuf* cb = cbOf(args.a[0], "vkCmdBeginRenderPass");
        VkRenderPassBeginInfo bi = {};
        if (!cb || !readStruct(args.a[1], bi)) { klog("vk64: BEGIN_RP_FAILED"); return 0; }
        klog_fmt("vk64: BEGIN_RP fb=0x%llx", (unsigned long long)bi.framebuffer);
        Cmd c;
        c.kind = CMD_BEGIN_RP;
        c.a = bi.renderPass;
        c.b = bi.framebuffer;
        // pClearValues is a guest VA: colour first, then depth/stencil (P1 read
        // index 0 as colour and index 1 as depth).
        std::vector<VkClearValue> clears;
        readArray(bi.pClearValues, std::min<U32>(bi.clearValueCount, VK64_MAX_ATTACH), clears);
        captureRenderPassClears(c, objOf<RenderPass>(bi.renderPass, K_RENDERPASS, "vkCmdBeginRenderPass"), clears);
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdEndRenderPass: {
        CmdBuf* cb = cbOf(args.a[0], "vkCmdEndRenderPass");
        Cmd c;
        c.kind = CMD_END_RP;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBeginRenderPass2:
    case VK64_fn_vkCmdBeginRenderPass2KHR: {
        // (cmdBuf, pRenderPassBegin*, pSubpassBeginInfo*) — v2 has extra dependency
        // info in pNext; the begin info itself is identical to v1.
        klog_fmt("vk64: BEGIN_RP2_CALLED cb=0x%llx", (unsigned long long)args.a[0]);
        CmdBuf* cb = cbOf(args.a[0], "vkCmdBeginRenderPass2");
        VkRenderPassBeginInfo bi = {};
        VK64_DROP_IF(return 0, !cb || !readStruct(args.a[1], bi));
        Cmd c;
        c.kind = CMD_BEGIN_RP;
        c.a = bi.renderPass;
        c.b = bi.framebuffer;
        std::vector<VkClearValue> clears;
        readArray(bi.pClearValues, std::min<U32>(bi.clearValueCount, VK64_MAX_ATTACH), clears);
        captureRenderPassClears(c, objOf<RenderPass>(bi.renderPass, K_RENDERPASS, "vkCmdBeginRenderPass"), clears);
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdEndRenderPass2:
    case VK64_fn_vkCmdEndRenderPass2KHR: {
        // (cmdBuf, pSubpassEndInfo*)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdEndRenderPass2");
        VK64_DROP_IF(return 0, !cb);
        Cmd c;
        c.kind = CMD_END_RP;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdPipelineBarrier2:
    case VK64_fn_vkCmdPipelineBarrier2KHR: {
        // (cmdBuf, pDependencyInfo*) — v2 barrier. For our purposes, barriers
        // are no-ops (we execute synchronously), but we record the call so the
        // command buffer is not empty and DXVK's state tracking works.
        CmdBuf* cb = cbOf(args.a[0], "vkCmdPipelineBarrier2");
        VK64_DROP_IF(return 0, !cb);
        Cmd c;
        c.kind = CMD_BARRIER;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdPushConstants2:
    case VK64_fn_vkCmdPushConstants2KHR: {
        // (cmdBuf, pPushConstantsInfo*) — VkPushConstantsInfo has layout,
        // stageFlags, offset, size, pValues. We extract and record like v1.
        CmdBuf* cb = cbOf(args.a[0], "vkCmdPushConstants2");
        VK64_DROP_IF(return 0, !cb || !args.a[1]);
        // VkPushConstantsInfo: sType(0), pNext(8), layout(16), stageFlags(24),
        // offset(28), size(32), pValues(40)
        U64 layout = rd64(args.a[1] + 16);
        U32 stageFlags = rd32(args.a[1] + 24);
        U32 offset = rd32(args.a[1] + 28);
        U32 size = rd32(args.a[1] + 32);
        U64 pValues = rd64(args.a[1] + 40);
        Cmd c;
        c.kind = CMD_PUSH_CONST;
        c.a = layout;
        c.pcStageFlags = stageFlags;
        c.pcOffset = offset;
        c.pcSize = size > VK64_MAX_PUSH_BYTES ? VK64_MAX_PUSH_BYTES : size;
        if (pValues && c.pcSize) g_mem->memcpyFromGuest(c.pcBytes, pValues, c.pcSize);
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBindIndexBuffer2:
    case VK64_fn_vkCmdBindIndexBuffer2KHR: {
        // (cmdBuf, buffer, offset, size, indexType) — v2 adds size param.
        // For our purposes, size is informational; we bind like v1.
        CmdBuf* cb = cbOf(args.a[0], "vkCmdBindIndexBuffer2");
        VK64_DROP_IF(return 0, !cb);
        cb->ib = args.a[1];
        cb->ibOff = args.a[2];
        // args.a[3] is size (ignored), args.a[4] is indexType
        cb->ibType = (U32)args.a[4] == 1 ? 1 : 0;  // 1 = UINT32, 0 = UINT16
        Cmd c;
        c.kind = CMD_BIND_IB;
        c.a = cb->ib;
        c.b = cb->ibOff;
        c.c = cb->ibType;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBeginRendering:
    case VK64_fn_vkCmdBeginRenderingKHR: {
        CmdBuf* cb = cbOf(args.a[0], "vkCmdBeginRendering");
        VkRenderingInfo ri = {};
        VK64_DROP_IF(return 0, !cb || !readStruct(args.a[1], ri));
        U64 rpId = 0, fbId = 0;
        auto* rp = createObj<RenderPass>(K_RENDERPASS, rpId);
        auto* fb = createObj<Framebuffer>(K_FRAMEBUFFER, fbId);
        cb->dynamicObjects.push_back(rpId);
        cb->dynamicObjects.push_back(fbId);
        fb->rp = rpId; fb->w = ri.renderAreaWidth; fb->h = ri.renderAreaHeight;
        Cmd c; c.kind = CMD_BEGIN_RP; c.a = rpId; c.b = fbId;
        auto attachment = [&](U64 address, bool depth) {
            VkRenderingAttachmentInfo att = {};
            VK64_DROP_IF(return, !address || fb->natt >= VK64_MAX_ATTACH || !readStruct(address, att));
            auto* view = objOf<ImageView>(att.imageView, K_IMAGEVIEW, "vkCmdBeginRendering");
            VK64_DROP_IF(return, !view);
            auto* image = objOf<Image>(view->image, K_IMAGE, "vkCmdBeginRendering");
            U32 i = fb->natt++;
            fb->views[i] = att.imageView;
            rp->formats[i] = view->format ? view->format : (image ? image->format : 0);
            rp->loadOps[i] = att.loadOp; rp->storeOps[i] = att.storeOp;
            rp->samples[i] = image && image->samples ? image->samples : 1;
            if (depth) {
                float value; memcpy(&value, att.clearValue, 4); c.clearDepth = value;
                rp->depthFmt = rp->formats[i];
            } else if (i == 0) memcpy(c.f, att.clearValue, 16);
        };
        U32 colors = std::min(ri.colorAttachmentCount, (U32)VK64_MAX_ATTACH);
        for (U32 i = 0; i < colors && ri.pColorAttachments; ++i)
            attachment(ri.pColorAttachments + (U64)i * sizeof(VkRenderingAttachmentInfo), false);
        attachment(ri.pDepthAttachment, true);
        // Packed depth/stencil shares one image view and one replay attachment.
        if (!ri.pDepthAttachment) attachment(ri.pStencilAttachment, true);
        if (ri.pStencilAttachment && fb->natt) {
            VkRenderingAttachmentInfo stencil = {};
            if (readStruct(ri.pStencilAttachment, stencil)) {
                U32 i = fb->natt - 1;
                rp->stencilLoadOps[i] = stencil.loadOp; rp->stencilStoreOps[i] = stencil.storeOp;
                memcpy(&c.clearStencil, stencil.clearValue + 4, 4);
            }
        }
        rp->natt = fb->natt;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdEndRendering:
    case VK64_fn_vkCmdEndRenderingKHR: {
        // (cmdBuf)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdEndRendering");
        VK64_DROP_IF(return 0, !cb);
        Cmd c;
        c.kind = CMD_END_RP;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBindPipeline: {
        // (cmdBuf, bindPoint, pipeline)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdBindPipeline");
        VK64_DROP_IF(return 0, !cb);
        Cmd c;
        c.kind = CMD_BIND_PIPE;
        c.a = args.a[2];
        cb->pipe = args.a[2];
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBindDescriptorSets: {
        // (cmdBuf, bindPoint, layout, firstSet, setCount, pSets*, nDyn, pDynOffsets*)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdBindDescriptorSets");
        VK64_DROP_IF(return 0, !cb);
        U32 firstSet = (U32)args.a[3];
        U32 setCount = (U32)args.a[4];
        if (setCount > VK64_MAX_SETS) setCount = VK64_MAX_SETS;
        for (U32 i = 0; i < setCount; i++) {
            U64 set = rd64(args.a[5] + (U64)i * 8);
            if (firstSet + i < VK64_MAX_SETS) cb->sets[firstSet + i] = set;
        }
        Cmd c;
        c.kind = CMD_BIND_SETS;
        memcpy(c.sets, cb->sets, sizeof(c.sets));
        c.a = firstSet;
        c.b = setCount;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdSetStencilCompareMask:
    case VK64_fn_vkCmdSetStencilWriteMask:
    case VK64_fn_vkCmdSetStencilReference: {
        CmdBuf* cb = cbOf(args.a[0], "dynamic raster state");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_STENCIL_STATE;
        c.a = fnId == VK64_fn_vkCmdSetStencilCompareMask ? 0 : fnId == VK64_fn_vkCmdSetStencilWriteMask ? 1 : 2;
        c.b = (U32)args.a[1]; c.c = (U32)args.a[2];
        pushCmd(cb, c); return 0;
    }
    case VK64_fn_vkCmdSetBlendConstants: {
        CmdBuf* cb = cbOf(args.a[0], "dynamic raster state");
        VK64_DROP_IF(return 0, !cb || !args.a[1]);
        Cmd c; c.kind = CMD_BLEND_CONSTANTS;
        g_mem->memcpyFromGuest(c.f, args.a[1], 16);
        pushCmd(cb, c); return 0;
    }
    case VK64_fn_vkCmdSetCullMode:
    case VK64_fn_vkCmdSetFrontFace: {
        CmdBuf* cb = cbOf(args.a[0], "dynamic raster state");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_RASTER_STATE;
        c.a = fnId == VK64_fn_vkCmdSetCullMode ? 0 : 1; c.b = (U32)args.a[1];
        pushCmd(cb, c); return 0;
    }
    // Timestamp query results are synthesized from the host clock at read time and
    // WebGPU has no depth-bounds test (the feature is not advertised), so both are
    // accepted without a record.
    case VK64_fn_vkCmdWriteTimestamp: {
        CmdBuf* cb=cbOf(args.a[0],"vkCmdWriteTimestamp");QueryPool* pool=objOf<QueryPool>(args.a[2],K_QUERYPOOL,"vkCmdWriteTimestamp");
        VK64_DROP_IF(return 0,!cb||!pool||pool->type!=vkquery::VKQUERY_TIMESTAMP||args.a[3]>=pool->count);
        Cmd c;c.kind=CMD_WRITE_TIMESTAMP;c.a=args.a[2];c.b=args.a[3];pushCmd(cb,c);return 0;
    }
    case VK64_fn_vkCmdSetDepthBounds:
        return 0;
    case VK64_fn_vkCmdClearDepthStencilImage: {
        // (cmdBuf, image, layout, pDepthStencil*, rangeCount, pRanges*): replayed on the
        // page as a render-pass clear of the matching GPU texture subresources.
        CmdBuf* cb = cbOf(args.a[0], "vkCmdClearDepthStencilImage");
        VK64_DROP_IF(return 0, !cb || !objOf<Image>(args.a[1], K_IMAGE, "vkCmdClearDepthStencilImage"));
        if (args.a[4] > 64) { g_frameOverflow = true; return 0; }
        struct { float depth; U32 stencil; } value = {1.0f, 0};
        if (args.a[3]) g_mem->memcpyFromGuest(&value, args.a[3], sizeof(value));
        Cmd c; c.kind = CMD_CLEAR_DS_IMAGE; c.a = args.a[1];
        c.f[0] = value.depth; c.clearStencil = value.stencil;
        readArray(args.a[5], (U32)args.a[4], c.clearRanges);
        if (!c.clearRanges.empty()) pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdSetDepthBias: {
        CmdBuf* cb = cbOf(args.a[0], "vkCmdSetDepthBias");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_DEPTH_BIAS;
        for (U32 i = 0; i < 3; ++i) { U32 bits = (U32)args.a[i + 1]; memcpy(&c.f[i], &bits, sizeof(bits)); }
        pushCmd(cb, c); return 0;
    }
    case VK64_fn_vkCmdSetViewport: {
        // (cmdBuf, first, count, pViewports*)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdSetViewport");
        std::vector<VkViewport> vps;
        readArray(args.a[3], (U32)args.a[2], vps);
        if (!cb || vps.empty()) return 0;
        Cmd c;
        c.kind = CMD_VIEWPORT;
        c.f[0] = vps[0].x; c.f[1] = vps[0].y; c.f[2] = vps[0].width; c.f[3] = vps[0].height;
        c.f[4] = vps[0].minDepth; c.f[5] = vps[0].maxDepth;
        memcpy(cb->vp, c.f, sizeof(cb->vp));
        cb->haveVp = true;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdSetScissor: {
        // (cmdBuf, first, count, pScissors*)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdSetScissor");
        std::vector<VkRect2D> scs;
        readArray(args.a[3], (U32)args.a[2], scs);
        if (!cb || scs.empty()) return 0;
        Cmd c;
        c.kind = CMD_SCISSOR;
        c.f[0] = (float)scs[0].offset.x; c.f[1] = (float)scs[0].offset.y;
        c.f[2] = (float)scs[0].extent.width; c.f[3] = (float)scs[0].extent.height;
        memcpy(cb->sci, c.f, sizeof(cb->sci));
        cb->haveSci = true;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdDraw: {
        // (cmdBuf, vertexCount, instanceCount, firstVertex, firstInstance)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdDraw");
        VK64_DROP_IF(return 0, !cb);
        Cmd c;
        c.kind = CMD_DRAW;
        c.a = args.a[1];   // vertexCount
        c.b = args.a[2];   // instanceCount
        c.c = args.a[3];   // firstVertex
        c.d = args.a[4];   // firstInstance
        if (!vkSchemaV2()) captureDraw(cb, c);
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBindVertexBuffers:
    case VK64_fn_vkCmdBindVertexBuffers2: {
        // (cmdBuf, firstBinding, bindingCount, pBuffers*, pOffsets*)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdBindVertexBuffers");
        VK64_DROP_IF(return 0, !cb);
        U32 first = (U32)args.a[1], count = (U32)args.a[2];
        if (!args.a[3] || !args.a[4] || !count || first >= VK64_MAX_VERT_BINDINGS) return 0;
        if (first + count > VK64_MAX_VERT_BINDINGS) count = VK64_MAX_VERT_BINDINGS - first;
        for (U32 i = 0; i < count; i++) {
            cb->vbs[first + i] = rd64(args.a[3] + (U64)i * 8);
            cb->vbOff[first + i] = rd64(args.a[4] + (U64)i * 8);
            if (fnId == VK64_fn_vkCmdBindVertexBuffers2 && args.a[6]) {
                cb->vbStride[first + i] = (U32)rd64(args.a[6] + (U64)i * 8);
                cb->vbHasStride[first + i] = true;
            }
        }
        U32 end = first + count;
        if (end > cb->nvb) cb->nvb = end;
        Cmd c;
        c.kind = CMD_BIND_VB;
        c.a = first; c.b = count;
        c.vbCount = cb->nvb;
        memcpy(c.vbBuffers, cb->vbs, sizeof(c.vbBuffers));
        memcpy(c.vbOffsets, cb->vbOff, sizeof(c.vbOffsets));
        memcpy(c.vbStrides, cb->vbStride, sizeof(c.vbStrides));
        memcpy(c.vbHasStride, cb->vbHasStride, sizeof(c.vbHasStride));
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBindIndexBuffer: {
        // (cmdBuf, buffer, offset, indexType)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdBindIndexBuffer");
        VK64_DROP_IF(return 0, !cb);
        cb->ib = args.a[1]; cb->ibOff = args.a[2]; cb->ibType = (U32)args.a[3];
        Cmd c;
        c.kind = CMD_BIND_IB;
        c.a = cb->ib; c.b = cb->ibOff; c.c = cb->ibType;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdDrawIndexed: {
        // (cmdBuf, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdDrawIndexed");
        VK64_DROP_IF(return 0, !cb);
        Cmd c;
        c.kind = CMD_DRAW_INDEXED;
        c.a = args.a[1];   // indexCount
        c.b = args.a[2];   // instanceCount
        c.c = args.a[3];   // firstIndex
        c.diVertexOffset = (int32_t)(U32)args.a[4];
        c.copy.ox = (int32_t)args.a[4];  // vertexOffset (signed; rides the Copy spare for v2)
        c.d = args.a[5];   // firstInstance
        if (!vkSchemaV2()) captureIndexedDraw(cb, c);
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdPushConstants: {
        /* (cmdBuf, layout, stageFlags, offset, size, pValues). DXVK pushes
         * constants per-draw for nearly everything, so this used to be a hard
         * stop: the call fell into the benign tail and the manifest carried no
         * per-draw constants at all. The bytes are copied out of guest memory now,
         * with the byte OFFSET kept, because a push is an overlay into a
         * push-constant range rather than a replacement of it. */
        CmdBuf* cb = cbOf(args.a[0], "vkCmdPushConstants");
        VK64_DROP_IF(return 0, !cb);
        U32 off = (U32)args.a[3], size = (U32)args.a[4];
        if (!args.a[5] || !size) return 0;
        if (size > VK64_MAX_PUSH_BYTES) {
            klog_fmt("vk64: vkCmdPushConstants %u bytes truncated to %u (maxPushConstantsSize)",
                     (unsigned)size, (unsigned)VK64_MAX_PUSH_BYTES);
            size = VK64_MAX_PUSH_BYTES;
        }
        Cmd c;
        c.kind = CMD_PUSH_CONST;
        c.pcOffset = off;
        c.pcSize = size;
        c.pcStageFlags = (U32)args.a[2];
        g_mem->memcpyFromGuest(c.pcBytes, args.a[5], size);
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdUpdateBuffer: {
        /* (cmdBuf, dstBuffer, dstOffset, dataSize, pData). DXVK uploads
         * small uniforms with this -- notably the D3D9 fixed-function VS
         * constant buffer -- so this used to be a hard stop: the call fell
         * into the benign tail and the manifest carried zeros for every
         * uniform updated this way. The payload is copied out of guest
         * memory at record time and written back at submit (transfer
         * semantics), before serialization reads the buffer. */
        CmdBuf* cb = cbOf(args.a[0], "vkCmdUpdateBuffer");
        VK64_DROP_IF(return 0, !cb);
        U64 size = args.a[3];
        if (!args.a[4] || !size) return 0;
        if (size > 65536) {
            klog_fmt("vk64: vkCmdUpdateBuffer %llu bytes truncated to 65536 (spec max)",
                     (unsigned long long)size);
            size = 65536;
        }
        Cmd c;
        c.kind = CMD_UPDATE_BUF;
        c.updBuf = args.a[1];
        c.updOff = args.a[2];
        c.updBytes.resize((size_t)size);
        g_mem->memcpyFromGuest(c.updBytes.data(), args.a[4], size);
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdPipelineBarrier: {
        /* WebGPU pass encoding makes colour/depth layout transitions implicit,
         * so the barrier is dropped — but an ordering marker is kept so submit
         * preserves relative order (P1 section 1.4). */
        CmdBuf* cb = cbOf(args.a[0], "vkCmdPipelineBarrier");
        Cmd c;
        c.kind = CMD_BARRIER;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdCopyBufferToImage: {
        // (cmdBuf, srcBuffer, dstImage, layout, nRegions, pRegions*)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdCopyBufferToImage");
        U32 nreg = (U32)args.a[4];
        // Defensive: garbage regionCount would cause over-allocation in readArray.
        VK64_DROP_IF(return 0, nreg > 1024);
        std::vector<VkBufferImageCopy> regs;
        readArray(args.a[5], nreg, regs);
        VK64_DROP_IF(return 0, !cb);
        for (U32 i = 0; i < regs.size(); i++) {
            Cmd c;
            c.kind = CMD_COPY_B2I;
            c.a = args.a[1]; c.b = args.a[2]; c.c = nreg;
            c.copy.srcBuffer = args.a[1];
            c.copy.dstImage = args.a[2];
            c.copy.bufOff = regs[i].bufferOffset;
            c.copy.rowLen = regs[i].bufferRowLength;
            c.copy.imgH = regs[i].bufferImageHeight;
            c.copy.ox = regs[i].imageOffset.x;
            c.copy.oy = regs[i].imageOffset.y;
            c.copy.w = regs[i].imageExtent.width;
            c.copy.h = regs[i].imageExtent.height;
            c.copy.mip = regs[i].imageSubresource.mipLevel;
            c.copy.baseLayer = regs[i].imageSubresource.baseArrayLayer;
            c.copy.layerCount = regs[i].imageSubresource.layerCount;
            c.copy.depth = regs[i].imageExtent.depth;
            c.copy.oz = regs[i].imageOffset.z;
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdCopyImageToBuffer: {
        CmdBuf* cb = cbOf(args.a[0], "vkCmdCopyImageToBuffer");
        U32 nreg = (U32)args.a[4];
        std::vector<VkBufferImageCopy> regs;
        readArray(args.a[5], nreg, regs);
        VK64_DROP_IF(return 0, !cb);
        for (U32 i = 0; i < regs.size(); i++) {
            Cmd c; c.kind = CMD_COPY_I2B;
            c.copyI2B.srcImage = args.a[1]; c.copyI2B.dstBuffer = args.a[3];
            c.copyI2B.bufOff = regs[i].bufferOffset;
            c.copyI2B.rowLen = regs[i].bufferRowLength;
            c.copyI2B.imgH = regs[i].bufferImageHeight;
            c.copyI2B.ox = regs[i].imageOffset.x; c.copyI2B.oy = regs[i].imageOffset.y;
            c.copyI2B.w = regs[i].imageExtent.width; c.copyI2B.h = regs[i].imageExtent.height;
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdCopyImageToBuffer2:
    case VK64_fn_vkCmdCopyImageToBuffer2KHR: {
        // VkBufferImageCopy2 has sType+pNext at start (unlike VkBufferImageCopy).
        // 64-bit layout: sType(0,4), pNext(8,8), bufferOffset(16,8), bufferRowLength(24,4),
        // bufferImageHeight(28,4), imageSubresource(32,16), imageOffset(48,12), imageExtent(60,12).
        struct VkBufferImageCopy2L {
            U32 sType; U32 _p0; U64 pNext;
            U64 bufferOffset; U32 bufferRowLength; U32 bufferImageHeight;
            U32 aspectMask; U32 mipLevel; U32 baseArrayLayer; U32 layerCount;
            int32_t ox; int32_t oy; int32_t oz;
            U32 w; U32 h; U32 d;
        };
        CmdBuf* cb = cbOf(args.a[0], "vkCmdCopyImageToBuffer2");
        U32 nreg = (U32)args.a[4];
        // Defensive: garbage regionCount would cause std::length_error in readArray.
        VK64_DROP_IF(return 0, nreg > 1024);
        std::vector<VkBufferImageCopy2L> regs;
        readArray(args.a[5], nreg, regs);
        VK64_DROP_IF(return 0, !cb);
        for (U32 i = 0; i < regs.size(); i++) {
            Cmd c; c.kind = CMD_COPY_I2B;
            c.copyI2B.srcImage = args.a[1]; c.copyI2B.dstBuffer = args.a[3];
            c.copyI2B.bufOff = regs[i].bufferOffset;
            c.copyI2B.rowLen = regs[i].bufferRowLength;
            c.copyI2B.imgH = regs[i].bufferImageHeight;
            c.copyI2B.ox = regs[i].ox; c.copyI2B.oy = regs[i].oy;
            c.copyI2B.w = regs[i].w; c.copyI2B.h = regs[i].h;
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdCopyBuffer: {
        // (cmdBuf, srcBuffer, dstBuffer, regionCount, pRegions*)
        // Immediate CPU memcpy: all Vulkan memory is guest-mapped, so the
        // copy is visible to subsequent reads (e.g. emitBufferData at submit).
        // This is what DXVK uses to upload vertex/index data from staging.
        struct VkBufferCopyL { U64 srcOffset; U64 dstOffset; U64 size; };
        U32 nreg = (U32)args.a[3];
        std::vector<VkBufferCopyL> regs;
        readArray(args.a[4], nreg, regs);
        Buffer* src = objOf<Buffer>(args.a[1], K_BUFFER, "vkCmdCopyBuffer");
        Buffer* dst = objOf<Buffer>(args.a[2], K_BUFFER, "vkCmdCopyBuffer");
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf<DeviceMemory>(src->mem, K_MEMORY, "vkCmdCopyBuffer");
        DeviceMemory* dmem = objOf<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdCopyBuffer");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        bool anyCopy = false;
        for (const auto& r : regs) {
            U64 sOff = smem->va + src->memOff + r.srcOffset;
            U64 dOff = dmem->va + dst->memOff + r.dstOffset;
            U64 n = r.size;
            VK64_DROP_IF(continue, r.srcOffset >= smem->size || r.dstOffset >= dmem->size);
            if (sOff + n > smem->va + smem->size) n = smem->va + smem->size - sOff;
            if (dOff + n > dmem->va + dmem->size) n = dmem->va + dmem->size - dOff;
            if (!n) continue;
            std::vector<U8> tmp((size_t)n);
            g_mem->memcpyFromGuest(tmp.data(), sOff, n);
            g_mem->memcpyToGuest(dOff, tmp.data(), n);
            anyCopy = true;
        }
        if (anyCopy) dst->dirtyGen = g_bufDirtyTick++;
        return 0;
    }
    case VK64_fn_vkCmdCopyBuffer2:
    case VK64_fn_vkCmdCopyBuffer2KHR: {
        // (cmdBuf, srcBuffer, dstBuffer, regionCount, pRegions*) — shim
        // unpacks VkCopyBufferInfo2; regions are VkBufferCopy2 (sType+pNext header).
        struct VkBufferCopy2L {
            U32 sType; U32 _p0; U64 pNext;
            U64 srcOffset; U64 dstOffset; U64 size;
        };
        U32 nreg = (U32)args.a[3];
        // Defensive: a garbage regionCount (e.g. 1447756043 observed from
        // mismatched shim args) would cause std::length_error in readArray.
        // No legitimate call needs more than 1024 regions.
        VK64_DROP_IF(return 0, nreg > 1024);
        std::vector<VkBufferCopy2L> regs;
        readArray(args.a[4], nreg, regs);
        Buffer* src = objOf<Buffer>(args.a[1], K_BUFFER, "vkCmdCopyBuffer2");
        Buffer* dst = objOf<Buffer>(args.a[2], K_BUFFER, "vkCmdCopyBuffer2");
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf<DeviceMemory>(src->mem, K_MEMORY, "vkCmdCopyBuffer2");
        DeviceMemory* dmem = objOf<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdCopyBuffer2");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        for (const auto& r : regs) {
            // Validate the bind-relative offset (including the buffer's bind
            // offset) BEFORE computing absolute addresses. The old check
            // (r.srcOffset >= smem->size) ignored src->memOff, so when
            // memOff+srcOffset exceeded the memory size the clamp below
            // (sEnd - sOff) underflowed U64 -> huge n -> std::length_error.
            // The wraparound checks also guard U64 addition overflow.
            U64 sRel = src->memOff + r.srcOffset;
            U64 dRel = dst->memOff + r.dstOffset;
            VK64_DROP_IF(continue, sRel < src->memOff || sRel >= smem->size);
            VK64_DROP_IF(continue, dRel < dst->memOff || dRel >= dmem->size);
            U64 sOff = smem->va + sRel;
            U64 dOff = dmem->va + dRel;
            U64 n = r.size;
            U64 sEnd = smem->va + smem->size;
            U64 dEnd = dmem->va + dmem->size;
            // sOff < sEnd and dOff < dEnd hold here, so no underflow.
            if (sOff + n > sEnd) n = sEnd - sOff;
            if (dOff + n > dEnd) n = dEnd - dOff;
            if (!n) continue;
            std::vector<U8> tmp((size_t)n);
            g_mem->memcpyFromGuest(tmp.data(), sOff, n);
            g_mem->memcpyToGuest(dOff, tmp.data(), n);
        }
        return 0;
    }

    // =====================================================================
// =====================================================================
// Game-breaking stub implementations (immediate CPU emulation)
// Insert after VK64_fn_vkCmdCopyBuffer2KHR handler, before Frame boundary
// =====================================================================
    case VK64_fn_vkCmdFillBuffer: {
        // (cmdBuf, dstBuffer, dstOffset, size, data)
        // Fill buffer range with 32-bit pattern repeated.
        Buffer* dst = objOf<Buffer>(args.a[1], K_BUFFER, "vkCmdFillBuffer");
        VK64_DROP_IF(return 0, !dst || !dst->mem);
        DeviceMemory* dmem = objOf<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdFillBuffer");
        VK64_DROP_IF(return 0, !dmem || !dmem->va);
        U64 offset = dst->memOff + args.a[2];
        U64 size = args.a[3];
        U32 data = (U32)args.a[4];
        VK64_DROP_IF(return 0, offset >= dmem->size);
        if (offset + size > dmem->size) size = dmem->size - offset;
        if (!size) return 0;
        U64 va = dmem->va + offset;
        // Fill with 4-byte pattern; handle trailing partial word
        U64 i = 0;
        for (; i + 4 <= size; i += 4) {
            g_mem->memcpyToGuest(va + i, &data, 4);
        }
        if (i < size) {
            g_mem->memcpyToGuest(va + i, &data, size - i);
        }
        dst->dirtyGen = g_bufDirtyTick++;
        return 0;
    }
    case VK64_fn_vkCmdClearColorImage: {
        // (cmdBuf, image, layout, pColor*, rangeCount, pRanges*)
        // Fill image with color. pColor is VkClearColorValue (16 bytes).
        Image* img = objOf<Image>(args.a[1], K_IMAGE, "vkCmdClearColorImage");
        VK64_DROP_IF(return 0, !img || !img->mem);
        DeviceMemory* dmem = objOf<DeviceMemory>(img->mem, K_MEMORY, "vkCmdClearColorImage");
        VK64_DROP_IF(return 0, !dmem || !dmem->va);
        U8 color[16] = {0};
        if (args.a[3]) g_mem->memcpyFromGuest(color, args.a[3], 16);
        // Calculate image size; fall back to full memory if unknown
        U64 imgSize = img->pixBytes * (U64)img->w * (U64)img->h;
        if (!imgSize) imgSize = dmem->size - img->memOff;
        VK64_DROP_IF(return 0, !imgSize);
        U64 va = dmem->va + img->memOff;
        if (va + imgSize > dmem->va + dmem->size) {
            imgSize = dmem->va + dmem->size - va;
        }
        // Fill by repeating the 16-byte color pattern
        const U64 CHUNK = 4096;
        U8 buf[CHUNK];
        for (U64 i = 0; i < 16; i++) buf[i % 16] = color[i % 16];
        for (U64 i = 16; i < CHUNK; i++) buf[i] = buf[i % 16];
        U64 done = 0;
        while (done < imgSize) {
            U64 n = imgSize - done;
            if (n > CHUNK) n = CHUNK;
            g_mem->memcpyToGuest(va + done, buf, n);
            done += n;
        }
        img->copied = 1;
        return 0;
    }
    case VK64_fn_vkCmdClearAttachments:
    case VK64_fn_vkCmdClearAttachmentsKHR: {
        CmdBuf* cb = cbOf(args.a[0], "vkCmdClearAttachments");
        VK64_DROP_IF(return 0, !cb);
        if (args.a[1] > 8 || args.a[3] > 4096) { g_frameOverflow = true; return 0; }
        Cmd c; c.kind = CMD_CLEAR_ATTACHMENTS;
        readArray(args.a[2], (U32)args.a[1], c.clearAttachments);
        readArray(args.a[4], (U32)args.a[3], c.clearRects);
        if (!c.clearAttachments.empty() && !c.clearRects.empty()) pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdCopyImage: {
        // (cmdBuf, srcImage, srcLayout, dstImage, dstLayout, regionCount, pRegions*)
        // Immediate CPU copy between images.
        struct VkImageCopyL {
            U32 srcAspect; U32 srcMip; U32 srcBaseLayer; U32 srcLayerCount;
            int32_t srcX; int32_t srcY; int32_t srcZ;
            U32 dstAspect; U32 dstMip; U32 dstBaseLayer; U32 dstLayerCount;
            int32_t dstX; int32_t dstY; int32_t dstZ;
            U32 w; U32 h; U32 d;
        };
        Image* src = objOf<Image>(args.a[1], K_IMAGE, "vkCmdCopyImage");
        Image* dst = objOf<Image>(args.a[3], K_IMAGE, "vkCmdCopyImage");
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf<DeviceMemory>(src->mem, K_MEMORY, "vkCmdCopyImage");
        DeviceMemory* dmem = objOf<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdCopyImage");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        U32 nreg = (U32)args.a[5];
        std::vector<VkImageCopyL> regs;
        readArray(args.a[6], nreg, regs);
        for (const auto& r : regs)
            recordImageCopy(cbOf(args.a[0], "vkCmdCopyImage"), args.a[1], args.a[3], r.srcMip, r.srcBaseLayer, r.dstMip, r.dstBaseLayer,
                r.srcX, r.srcY, r.srcZ, r.dstX, r.dstY, r.dstZ, r.w, r.h, r.d, std::min(r.srcLayerCount, r.dstLayerCount));
        U32 pxBytes = (U32)src->pixBytes;
        if (!pxBytes) pxBytes = 4;
        U32 srcRowPitch = src->w * pxBytes;
        U32 dstRowPitch = dst->w * pxBytes;
        for (const auto& r : regs) {
            // Simplified: copy row by row, handling offsets
            // For full-image copies this is exact; for subregions it's approximate
            U32 copyW = r.w ? r.w : src->w;
            U32 copyH = r.h ? r.h : src->h;
            for (U32 y = 0; y < copyH; y++) {
                U32 sy = (U32)r.srcY + y;
                U32 dy = (U32)r.dstY + y;
                VK64_DROP_IF(continue, sy >= src->h || dy >= dst->h);
                U64 sOff = smem->va + src->memOff + (U64)sy * srcRowPitch + (U64)r.srcX * pxBytes;
                U64 dOff = dmem->va + dst->memOff + (U64)dy * dstRowPitch + (U64)r.dstX * pxBytes;
                U64 sEnd = smem->va + smem->size;
                U64 dEnd = dmem->va + dmem->size;
                // If start is already out of bounds, skip: prevents U64
                // underflow in the clamp below (sEnd - sOff wraps if sOff>=sEnd).
                VK64_DROP_IF(continue, sOff >= sEnd || dOff >= dEnd);
                U64 n = (U64)copyW * pxBytes;
                if (sOff + n > sEnd) n = sEnd - sOff;
                if (dOff + n > dEnd) n = dEnd - dOff;
                if (!n) continue;
                std::vector<U8> tmp((size_t)n);
                g_mem->memcpyFromGuest(tmp.data(), sOff, n);
                g_mem->memcpyToGuest(dOff, tmp.data(), n);
            }
        }
        dst->copied = 1;
        return 0;
    }
    case VK64_fn_vkCmdCopyImage2:
    case VK64_fn_vkCmdCopyImage2KHR: {
        // (cmdBuf, srcImage, dstImage, regionCount, pRegions*) — shim unpacks VkCopyImageInfo2
        // VkImageCopy2 has sType+pNext header (72 bytes total vs 68 for VkImageCopy).
        struct VkImageCopy2L {
            U32 sType; U32 _p0; U64 pNext;
            U32 srcAspect; U32 srcMip; U32 srcBaseLayer; U32 srcLayerCount;
            int32_t srcX; int32_t srcY; int32_t srcZ;
            U32 dstAspect; U32 dstMip; U32 dstBaseLayer; U32 dstLayerCount;
            int32_t dstX; int32_t dstY; int32_t dstZ;
            U32 w; U32 h; U32 d;
        };
        Image* src = objOf<Image>(args.a[1], K_IMAGE, "vkCmdCopyImage2");
        Image* dst = objOf<Image>(args.a[2], K_IMAGE, "vkCmdCopyImage2");
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf<DeviceMemory>(src->mem, K_MEMORY, "vkCmdCopyImage2");
        DeviceMemory* dmem = objOf<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdCopyImage2");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        U32 nreg = (U32)args.a[3];
        std::vector<VkImageCopy2L> regs;
        readArray(args.a[4], nreg, regs);
        for (const auto& r : regs)
            recordImageCopy(cbOf(args.a[0], "vkCmdCopyImage2"), args.a[1], args.a[2], r.srcMip, r.srcBaseLayer, r.dstMip, r.dstBaseLayer,
                r.srcX, r.srcY, r.srcZ, r.dstX, r.dstY, r.dstZ, r.w, r.h, r.d, std::min(r.srcLayerCount, r.dstLayerCount));
        U32 pxBytes = (U32)src->pixBytes;
        if (!pxBytes) pxBytes = 4;
        U32 srcRowPitch = src->w * pxBytes;
        U32 dstRowPitch = dst->w * pxBytes;
        for (const auto& r : regs) {
            U32 copyW = r.w ? r.w : src->w;
            U32 copyH = r.h ? r.h : src->h;
            for (U32 y = 0; y < copyH; y++) {
                U32 sy = (U32)r.srcY + y;
                U32 dy = (U32)r.dstY + y;
                VK64_DROP_IF(continue, sy >= src->h || dy >= dst->h);
                U64 sOff = smem->va + src->memOff + (U64)sy * srcRowPitch + (U64)r.srcX * pxBytes;
                U64 dOff = dmem->va + dst->memOff + (U64)dy * dstRowPitch + (U64)r.dstX * pxBytes;
                U64 sEnd = smem->va + smem->size;
                U64 dEnd = dmem->va + dmem->size;
                // If start is already out of bounds, skip: prevents U64
                // underflow in the clamp below (sEnd - sOff wraps if sOff>=sEnd).
                VK64_DROP_IF(continue, sOff >= sEnd || dOff >= dEnd);
                U64 n = (U64)copyW * pxBytes;
                if (sOff + n > sEnd) n = sEnd - sOff;
                if (dOff + n > dEnd) n = dEnd - dOff;
                if (!n) continue;
                std::vector<U8> tmp((size_t)n);
                g_mem->memcpyFromGuest(tmp.data(), sOff, n);
                g_mem->memcpyToGuest(dOff, tmp.data(), n);
            }
        }
        dst->copied = 1;
        return 0;
    }
    case VK64_fn_vkCmdCopyBufferToImage2:
    case VK64_fn_vkCmdCopyBufferToImage2KHR: {
        // (cmdBuf, srcBuffer, dstImage, regionCount, pRegions*) — shim unpacks
        // VkBufferImageCopy2 has sType+pNext header (72 bytes).
        struct VkBufferImageCopy2L {
            U32 sType; U32 _p0; U64 pNext;
            U64 bufferOffset; U32 bufferRowLength; U32 bufferImageHeight;
            U32 aspectMask; U32 mipLevel; U32 baseArrayLayer; U32 layerCount;
            int32_t ox; int32_t oy; int32_t oz;
            U32 w; U32 h; U32 d;
        };
        CmdBuf* cb = cbOf(args.a[0], "vkCmdCopyBufferToImage2");
        U32 nreg = (U32)args.a[3];
        // Defensive: garbage regionCount (PE shim arg shift) would cause
        // massive over-allocation in readArray and millions of no-op copies.
        VK64_DROP_IF(return 0, nreg > 1024);
        std::vector<VkBufferImageCopy2L> regs;
        readArray(args.a[4], nreg, regs);
        VK64_DROP_IF(return 0, !cb);
        // Record as CMD_COPY_B2I like the v1 version; page handles the copy
        for (U32 i = 0; i < regs.size(); i++) {
            Cmd c;
            c.kind = CMD_COPY_B2I;
            c.copy.srcBuffer = args.a[1];
            c.copy.dstImage = args.a[2];
            c.copy.bufOff = regs[i].bufferOffset;
            c.copy.rowLen = regs[i].bufferRowLength;
            c.copy.imgH = regs[i].bufferImageHeight;
            c.copy.ox = regs[i].ox;
            c.copy.oy = regs[i].oy;
            c.copy.oz = regs[i].oz;
            c.copy.w = regs[i].w;
            c.copy.h = regs[i].h;
            c.copy.depth = regs[i].d;
            c.copy.mip = regs[i].mipLevel;
            c.copy.baseLayer = regs[i].baseArrayLayer;
            c.copy.layerCount = regs[i].layerCount;
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdBlitImage: {
        // (cmdBuf, srcImage, srcLayout, dstImage, dstLayout, regionCount, pRegions*, filter)
        // Blit = scaled copy. For NEAREST filter or matching extents, do a scaled copy.
        struct VkImageBlitL {
            U32 srcAspect; U32 srcMip; U32 srcBaseLayer; U32 srcLayerCount;
            int32_t srcX0; int32_t srcY0; int32_t srcZ0;
            int32_t srcX1; int32_t srcY1; int32_t srcZ1;
            U32 dstAspect; U32 dstMip; U32 dstBaseLayer; U32 dstLayerCount;
            int32_t dstX0; int32_t dstY0; int32_t dstZ0;
            int32_t dstX1; int32_t dstY1; int32_t dstZ1;
        };
        Image* src = objOf<Image>(args.a[1], K_IMAGE, "vkCmdBlitImage");
        Image* dst = objOf<Image>(args.a[3], K_IMAGE, "vkCmdBlitImage");
        VK64_DROP_IF(return 0, !src || !dst);
        U32 nreg = (U32)args.a[5];
        VK64_DROP_IF(return 0, nreg > 1024);
        std::vector<VkImageBlitL> regs;
        readArray(args.a[6], nreg, regs);
        for (const auto& r : regs) {
            const int32_t srcOffsets[2][3] = {{r.srcX0, r.srcY0, r.srcZ0}, {r.srcX1, r.srcY1, r.srcZ1}};
            const int32_t dstOffsets[2][3] = {{r.dstX0, r.dstY0, r.dstZ0}, {r.dstX1, r.dstY1, r.dstZ1}};
            recordImageBlit(cbOf(args.a[0], "vkCmdBlitImage"), args.a[1], args.a[3],
                r.srcMip, r.srcBaseLayer, r.dstMip, r.dstBaseLayer, (U32)args.a[7],
                srcOffsets, dstOffsets, r.srcLayerCount);
        }
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf<DeviceMemory>(src->mem, K_MEMORY, "vkCmdBlitImage");
        DeviceMemory* dmem = objOf<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdBlitImage");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        // U32 filter = (U32)args.a[7]; // 0=NEAREST, 1=LINEAR (currently unused, NEAREST assumed)
        U32 pxBytes = (U32)src->pixBytes;
        if (!pxBytes) pxBytes = 4;
        U32 srcRowPitch = src->w * pxBytes;
        U32 dstRowPitch = dst->w * pxBytes;
        for (const auto& r : regs) {
            int32_t srcW = r.srcX1 - r.srcX0;
            int32_t srcH = r.srcY1 - r.srcY0;
            int32_t dstW = r.dstX1 - r.dstX0;
            int32_t dstH = r.dstY1 - r.dstY0;
            VK64_DROP_IF(continue, srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0);
            // Nearest-neighbor scaled copy
            for (int32_t dy = 0; dy < dstH; dy++) {
                int32_t sy = r.srcY0 + (dy * srcH) / dstH;
                VK64_DROP_IF(continue, sy < 0 || (U32)sy >= src->h);
                U32 dyAbs = (U32)(r.dstY0 + dy);
                VK64_DROP_IF(continue, dyAbs >= dst->h);
                for (int32_t dx = 0; dx < dstW; dx++) {
                    int32_t sx = r.srcX0 + (dx * srcW) / dstW;
                    VK64_DROP_IF(continue, sx < 0 || (U32)sx >= src->w);
                    U32 dxAbs = (U32)(r.dstX0 + dx);
                    VK64_DROP_IF(continue, dxAbs >= dst->w);
                    U64 sOff = smem->va + src->memOff + (U64)sy * srcRowPitch + (U64)sx * pxBytes;
                    U64 dOff = dmem->va + dst->memOff + (U64)dyAbs * dstRowPitch + (U64)dxAbs * pxBytes;
                    U64 sEndB = smem->va + smem->size;
                    U64 dEndB = dmem->va + dmem->size;
                    VK64_DROP_IF(continue, sOff >= sEndB || dOff >= dEndB);
                    VK64_DROP_IF(continue, sOff + pxBytes > sEndB);
                    VK64_DROP_IF(continue, dOff + pxBytes > dEndB);
                    U8 pixel[16];
                    g_mem->memcpyFromGuest(pixel, sOff, pxBytes);
                    g_mem->memcpyToGuest(dOff, pixel, pxBytes);
                }
            }
        }
        dst->copied = 1;
        return 0;
    }
    case VK64_fn_vkCmdBlitImage2:
    case VK64_fn_vkCmdBlitImage2KHR: {
        // (cmdBuf, srcImage, dstImage, regionCount, pRegions*, filter) — shim unpacks
        // VkImageBlit2 has sType+pNext header.
        struct VkImageBlit2L {
            U32 sType; U32 _p0; U64 pNext;
            U32 srcAspect; U32 srcMip; U32 srcBaseLayer; U32 srcLayerCount;
            int32_t srcX0; int32_t srcY0; int32_t srcZ0;
            int32_t srcX1; int32_t srcY1; int32_t srcZ1;
            U32 dstAspect; U32 dstMip; U32 dstBaseLayer; U32 dstLayerCount;
            int32_t dstX0; int32_t dstY0; int32_t dstZ0;
            int32_t dstX1; int32_t dstY1; int32_t dstZ1;
        };
        Image* src = objOf<Image>(args.a[1], K_IMAGE, "vkCmdBlitImage2");
        Image* dst = objOf<Image>(args.a[2], K_IMAGE, "vkCmdBlitImage2");
        VK64_DROP_IF(return 0, !src || !dst);
        U32 nreg = (U32)args.a[3];
        VK64_DROP_IF(return 0, nreg > 1024);
        std::vector<VkImageBlit2L> regs;
        readArray(args.a[4], nreg, regs);
        for (const auto& r : regs) {
            const int32_t srcOffsets[2][3] = {{r.srcX0, r.srcY0, r.srcZ0}, {r.srcX1, r.srcY1, r.srcZ1}};
            const int32_t dstOffsets[2][3] = {{r.dstX0, r.dstY0, r.dstZ0}, {r.dstX1, r.dstY1, r.dstZ1}};
            recordImageBlit(cbOf(args.a[0], "vkCmdBlitImage2"), args.a[1], args.a[2],
                r.srcMip, r.srcBaseLayer, r.dstMip, r.dstBaseLayer, (U32)args.a[5],
                srcOffsets, dstOffsets, r.srcLayerCount);
        }
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf<DeviceMemory>(src->mem, K_MEMORY, "vkCmdBlitImage2");
        DeviceMemory* dmem = objOf<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdBlitImage2");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        U32 pxBytes = (U32)src->pixBytes;
        if (!pxBytes) pxBytes = 4;
        U32 srcRowPitch = src->w * pxBytes;
        U32 dstRowPitch = dst->w * pxBytes;
        for (const auto& r : regs) {
            int32_t srcW = r.srcX1 - r.srcX0;
            int32_t srcH = r.srcY1 - r.srcY0;
            int32_t dstW = r.dstX1 - r.dstX0;
            int32_t dstH = r.dstY1 - r.dstY0;
            VK64_DROP_IF(continue, srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0);
            for (int32_t dy = 0; dy < dstH; dy++) {
                int32_t sy = r.srcY0 + (dy * srcH) / dstH;
                VK64_DROP_IF(continue, sy < 0 || (U32)sy >= src->h);
                U32 dyAbs = (U32)(r.dstY0 + dy);
                VK64_DROP_IF(continue, dyAbs >= dst->h);
                for (int32_t dx = 0; dx < dstW; dx++) {
                    int32_t sx = r.srcX0 + (dx * srcW) / dstW;
                    VK64_DROP_IF(continue, sx < 0 || (U32)sx >= src->w);
                    U32 dxAbs = (U32)(r.dstX0 + dx);
                    VK64_DROP_IF(continue, dxAbs >= dst->w);
                    U64 sOff = smem->va + src->memOff + (U64)sy * srcRowPitch + (U64)sx * pxBytes;
                    U64 dOff = dmem->va + dst->memOff + (U64)dyAbs * dstRowPitch + (U64)dxAbs * pxBytes;
                    U64 sEndB = smem->va + smem->size;
                    U64 dEndB = dmem->va + dmem->size;
                    VK64_DROP_IF(continue, sOff >= sEndB || dOff >= dEndB);
                    VK64_DROP_IF(continue, sOff + pxBytes > sEndB);
                    VK64_DROP_IF(continue, dOff + pxBytes > dEndB);
                    U8 pixel[16];
                    g_mem->memcpyFromGuest(pixel, sOff, pxBytes);
                    g_mem->memcpyToGuest(dOff, pixel, pxBytes);
                }
            }
        }
        dst->copied = 1;
        return 0;
    }
    case VK64_fn_vkCmdResolveImage:
    case VK64_fn_vkCmdResolveImageKHR: {
        // (cmdBuf, srcImage, srcLayout, dstImage, dstLayout, regionCount, pRegions*)
        // MSAA resolve. If src is not multisampled, this is a straight copy.
        // Our bridge does not implement true MSAA storage, so treat as copy.
        struct VkImageResolveL {
            U32 srcAspect; U32 srcMip; U32 srcBaseLayer; U32 srcLayerCount;
            int32_t srcX; int32_t srcY; int32_t srcZ;
            U32 dstAspect; U32 dstMip; U32 dstBaseLayer; U32 dstLayerCount;
            int32_t dstX; int32_t dstY; int32_t dstZ;
            U32 w; U32 h; U32 d;
        };
        Image* src = objOf<Image>(args.a[1], K_IMAGE, "vkCmdResolveImage");
        Image* dst = objOf<Image>(args.a[3], K_IMAGE, "vkCmdResolveImage");
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf<DeviceMemory>(src->mem, K_MEMORY, "vkCmdResolveImage");
        DeviceMemory* dmem = objOf<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdResolveImage");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        U32 nreg = (U32)args.a[5];
        std::vector<VkImageResolveL> regs;
        readArray(args.a[6], nreg, regs);
        for (const auto& r : regs)
            recordImageCopy(cbOf(args.a[0], "vkCmdResolveImage"), args.a[1], args.a[3], r.srcMip, r.srcBaseLayer, r.dstMip, r.dstBaseLayer,
                r.srcX, r.srcY, r.srcZ, r.dstX, r.dstY, r.dstZ, r.w, r.h, r.d, std::min(r.srcLayerCount, r.dstLayerCount));
        U32 pxBytes = (U32)src->pixBytes;
        if (!pxBytes) pxBytes = 4;
        U32 srcRowPitch = src->w * pxBytes;
        U32 dstRowPitch = dst->w * pxBytes;
        for (const auto& r : regs) {
            U32 copyW = r.w ? r.w : src->w;
            U32 copyH = r.h ? r.h : src->h;
            for (U32 y = 0; y < copyH; y++) {
                U32 sy = (U32)r.srcY + y;
                U32 dy = (U32)r.dstY + y;
                VK64_DROP_IF(continue, sy >= src->h || dy >= dst->h);
                U64 sOff = smem->va + src->memOff + (U64)sy * srcRowPitch + (U64)r.srcX * pxBytes;
                U64 dOff = dmem->va + dst->memOff + (U64)dy * dstRowPitch + (U64)r.dstX * pxBytes;
                U64 sEnd = smem->va + smem->size;
                U64 dEnd = dmem->va + dmem->size;
                // If start is already out of bounds, skip: prevents U64
                // underflow in the clamp below (sEnd - sOff wraps if sOff>=sEnd).
                VK64_DROP_IF(continue, sOff >= sEnd || dOff >= dEnd);
                U64 n = (U64)copyW * pxBytes;
                if (sOff + n > sEnd) n = sEnd - sOff;
                if (dOff + n > dEnd) n = dEnd - dOff;
                if (!n) continue;
                std::vector<U8> tmp((size_t)n);
                g_mem->memcpyFromGuest(tmp.data(), sOff, n);
                g_mem->memcpyToGuest(dOff, tmp.data(), n);
            }
        }
        dst->copied = 1;
        return 0;
    }
    case VK64_fn_vkCmdDrawIndirect: {
        // (cmdBuf, buffer, offset, drawCount, stride)
        // Read VkDrawIndirectCommand structs and emit direct CMD_DRAW for each.
        struct VkDrawIndirectCommandL { U32 vertexCount; U32 instanceCount; U32 firstVertex; U32 firstInstance; };
        CmdBuf* cb = cbOf(args.a[0], "vkCmdDrawIndirect");
        VK64_DROP_IF(return 0, !cb);
        Buffer* buf = objOf<Buffer>(args.a[1], K_BUFFER, "vkCmdDrawIndirect");
        VK64_DROP_IF(return 0, !buf || !buf->mem);
        DeviceMemory* mem = objOf<DeviceMemory>(buf->mem, K_MEMORY, "vkCmdDrawIndirect");
        VK64_DROP_IF(return 0, !mem || !mem->va);
        U64 baseOff = mem->va + buf->memOff + args.a[2];
        U32 drawCount = (U32)args.a[3];
        U32 stride = (U32)args.a[4];
        if (!stride) stride = sizeof(VkDrawIndirectCommandL);
        // Cap drawCount to prevent runaway from garbage data
        if (drawCount > 1024) drawCount = 1024;
        for (U32 i = 0; i < drawCount; i++) {
            VkDrawIndirectCommandL cmd;
            g_mem->memcpyFromGuest(&cmd, baseOff + (U64)i * stride, sizeof(cmd));
            if (!cmd.vertexCount || !cmd.instanceCount) continue;
            Cmd c;
            c.kind = CMD_DRAW;
            c.a = cmd.vertexCount;
            c.b = cmd.instanceCount;
            c.c = cmd.firstVertex;
            c.d = cmd.firstInstance;
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdDrawIndexedIndirect: {
        // (cmdBuf, buffer, offset, drawCount, stride)
        struct VkDrawIndexedIndirectCommandL {
            U32 indexCount; U32 instanceCount; U32 firstIndex; int32_t vertexOffset; U32 firstInstance;
        };
        CmdBuf* cb = cbOf(args.a[0], "vkCmdDrawIndexedIndirect");
        VK64_DROP_IF(return 0, !cb);
        Buffer* buf = objOf<Buffer>(args.a[1], K_BUFFER, "vkCmdDrawIndexedIndirect");
        VK64_DROP_IF(return 0, !buf || !buf->mem);
        DeviceMemory* mem = objOf<DeviceMemory>(buf->mem, K_MEMORY, "vkCmdDrawIndexedIndirect");
        VK64_DROP_IF(return 0, !mem || !mem->va);
        U64 baseOff = mem->va + buf->memOff + args.a[2];
        U32 drawCount = (U32)args.a[3];
        U32 stride = (U32)args.a[4];
        if (!stride) stride = sizeof(VkDrawIndexedIndirectCommandL);
        if (drawCount > 1024) drawCount = 1024;
        for (U32 i = 0; i < drawCount; i++) {
            VkDrawIndexedIndirectCommandL cmd;
            g_mem->memcpyFromGuest(&cmd, baseOff + (U64)i * stride, sizeof(cmd));
            if (!cmd.indexCount || !cmd.instanceCount) continue;
            Cmd c;
            c.kind = CMD_DRAW_INDEXED;
            c.a = cmd.indexCount;
            c.b = cmd.instanceCount;
            c.c = cmd.firstIndex;
            c.diVertexOffset = cmd.vertexOffset;
            c.d = cmd.firstInstance;
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdDrawIndirectCount:
    case VK64_fn_vkCmdDrawIndirectCountKHR: {
        // (cmdBuf, buffer, offset, countBuffer, countOffset, maxDrawCount, stride)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdDrawIndirectCount");
        VK64_DROP_IF(return 0, !cb);
        Buffer* buf = objOf<Buffer>(args.a[1], K_BUFFER, "vkCmdDrawIndirectCount");
        Buffer* countBuf = objOf<Buffer>(args.a[3], K_BUFFER, "vkCmdDrawIndirectCount");
        VK64_DROP_IF(return 0, !buf || !buf->mem || !countBuf || !countBuf->mem);
        DeviceMemory* mem = objOf<DeviceMemory>(buf->mem, K_MEMORY, "vkCmdDrawIndirectCount");
        DeviceMemory* countMem = objOf<DeviceMemory>(countBuf->mem, K_MEMORY, "vkCmdDrawIndirectCount");
        VK64_DROP_IF(return 0, !mem || !mem->va || !countMem || !countMem->va);
        // Read actual draw count from count buffer
        U32 actualCount = 0;
        g_mem->memcpyFromGuest(&actualCount, countMem->va + countBuf->memOff + args.a[4], 4);
        U32 maxDraws = (U32)args.a[5];
        if (actualCount > maxDraws) actualCount = maxDraws;
        if (actualCount > 1024) actualCount = 1024;
        struct VkDrawIndirectCommandL { U32 vertexCount; U32 instanceCount; U32 firstVertex; U32 firstInstance; };
        U64 baseOff = mem->va + buf->memOff + args.a[2];
        U32 stride = (U32)args.a[6];
        if (!stride) stride = sizeof(VkDrawIndirectCommandL);
        for (U32 i = 0; i < actualCount; i++) {
            VkDrawIndirectCommandL cmd;
            g_mem->memcpyFromGuest(&cmd, baseOff + (U64)i * stride, sizeof(cmd));
            if (!cmd.vertexCount || !cmd.instanceCount) continue;
            Cmd c;
            c.kind = CMD_DRAW;
            c.a = cmd.vertexCount;
            c.b = cmd.instanceCount;
            c.c = cmd.firstVertex;
            c.d = cmd.firstInstance;
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdDrawIndexedIndirectCount: {
        // (cmdBuf, buffer, offset, countBuffer, countOffset, maxDrawCount, stride)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdDrawIndexedIndirectCount");
        VK64_DROP_IF(return 0, !cb);
        Buffer* buf = objOf<Buffer>(args.a[1], K_BUFFER, "vkCmdDrawIndexedIndirectCount");
        Buffer* countBuf = objOf<Buffer>(args.a[3], K_BUFFER, "vkCmdDrawIndexedIndirectCount");
        VK64_DROP_IF(return 0, !buf || !buf->mem || !countBuf || !countBuf->mem);
        DeviceMemory* mem = objOf<DeviceMemory>(buf->mem, K_MEMORY, "vkCmdDrawIndexedIndirectCount");
        DeviceMemory* countMem = objOf<DeviceMemory>(countBuf->mem, K_MEMORY, "vkCmdDrawIndexedIndirectCount");
        VK64_DROP_IF(return 0, !mem || !mem->va || !countMem || !countMem->va);
        U32 actualCount = 0;
        g_mem->memcpyFromGuest(&actualCount, countMem->va + countBuf->memOff + args.a[4], 4);
        U32 maxDraws = (U32)args.a[5];
        if (actualCount > maxDraws) actualCount = maxDraws;
        if (actualCount > 1024) actualCount = 1024;
        struct VkDrawIndexedIndirectCommandL {
            U32 indexCount; U32 instanceCount; U32 firstIndex; int32_t vertexOffset; U32 firstInstance;
        };
        U64 baseOff = mem->va + buf->memOff + args.a[2];
        U32 stride = (U32)args.a[6];
        if (!stride) stride = sizeof(VkDrawIndexedIndirectCommandL);
        for (U32 i = 0; i < actualCount; i++) {
            VkDrawIndexedIndirectCommandL cmd;
            g_mem->memcpyFromGuest(&cmd, baseOff + (U64)i * stride, sizeof(cmd));
            if (!cmd.indexCount || !cmd.instanceCount) continue;
            Cmd c;
            c.kind = CMD_DRAW_INDEXED;
            c.a = cmd.indexCount;
            c.b = cmd.instanceCount;
            c.c = cmd.firstIndex;
            c.diVertexOffset = cmd.vertexOffset;
            c.d = cmd.firstInstance;
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdExecuteCommands: {
        // (cmdBuf, commandBufferCount, pCommandBuffers*)
        // Merge secondary command buffer commands into primary.
        CmdBuf* primary = cbOf(args.a[0], "vkCmdExecuteCommands");
        VK64_DROP_IF(return 0, !primary);
        U32 count = (U32)args.a[1];
        if (!count) return 0;
        VK64_DROP_IF(return 0, !args.a[2]);
        for (U32 i = 0; i < count; i++) {
            U64 secId = rd64(args.a[2] + (U64)i * 8);
            CmdBuf* sec = cbOf(secId, "vkCmdExecuteCommands");
            VK64_DROP_IF(continue, !sec);
            // Copy all commands from secondary into primary
            for (const auto& c : sec->cmds) {
                pushCmd(primary, c);
            }
            // Merge vertex/index binding state
            if (sec->nvb > primary->nvb) {
                primary->nvb = sec->nvb;
                memcpy(primary->vbs, sec->vbs, sizeof(primary->vbs));
                memcpy(primary->vbOff, sec->vbOff, sizeof(primary->vbOff));
            }
            if (sec->ib) {
                primary->ib = sec->ib;
                primary->ibOff = sec->ibOff;
                primary->ibType = sec->ibType;
            }
            if (sec->pipe) primary->pipe = sec->pipe;
        }
        return 0;
    }
    case VK64_fn_vkCmdDispatch: {
        // (cmdBuf, groupCountX, groupCountY, groupCountZ)
        // Compute dispatch. Record for page-side handling.
        CmdBuf* cb = cbOf(args.a[0], "vkCmdDispatch");
        VK64_DROP_IF(return 0, !cb);
        Cmd c;
        c.kind = CMD_DISPATCH;
        c.a = args.a[1];
        c.b = args.a[2];
        c.c = args.a[3];
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdDispatchBase: {
        // (cmdBuf, baseGroupX, baseGroupY, baseGroupZ, groupCountX, groupCountY, groupCountZ)
        CmdBuf* cb = cbOf(args.a[0], "vkCmdDispatchBase");
        VK64_DROP_IF(return 0, !cb);
        Cmd c;
        c.kind = CMD_DISPATCH;
        c.a = args.a[4];
        c.b = args.a[5];
        c.c = args.a[6];
        c.d = args.a[1]; // baseX in spare
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdDispatchIndirect: {
        // (cmdBuf, buffer, offset)
        // Read VkDispatchIndirectCommand {x, y, z} from buffer.
        CmdBuf* cb = cbOf(args.a[0], "vkCmdDispatchIndirect");
        VK64_DROP_IF(return 0, !cb);
        Buffer* buf = objOf<Buffer>(args.a[1], K_BUFFER, "vkCmdDispatchIndirect");
        VK64_DROP_IF(return 0, !buf || !buf->mem);
        DeviceMemory* mem = objOf<DeviceMemory>(buf->mem, K_MEMORY, "vkCmdDispatchIndirect");
        VK64_DROP_IF(return 0, !mem || !mem->va);
        U32 xyz[3] = {0, 0, 0};
        g_mem->memcpyFromGuest(xyz, mem->va + buf->memOff + args.a[2], 12);
        Cmd c;
        c.kind = CMD_DISPATCH;
        c.a = xyz[0];
        c.b = xyz[1];
        c.c = xyz[2];
        pushCmd(cb, c);
        return 0;
    }

    // E. Frame boundary (2 of the 85)
    // =====================================================================
    case VK64_fn_vkQueueSubmit: {
        // (queue, nSubmits, pSubmits*)
        double vkprofSubmitT0 = vkProfNow(); // BW64_VKPROF (additive)
        U32 n = (U32)args.a[1];
        std::vector<VkSubmitInfo> sis;
        readArray(args.a[2], n, sis);
        std::vector<std::pair<CmdBuf*, U64>> frames;
        for (U32 i = 0; i < n; i++) {
            // VkTimelineSemaphoreSubmitInfo in pNext: the per-batch wait/signal
            // VALUES that make a u64 semaphore mean anything. DXVK writes one of
            // these on every flush (its tracking fence), so ignoring the chain
            // means the counter never moves and every later wait reads stale.
            for (U64 cur = sis[i].pNext; cur && g_mem;) {
                if (g_mem->readd(cur) == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
                    VkTimelineSemaphoreSubmitInfo ti = {};
                    g_mem->memcpyFromGuest(&ti, cur, sizeof(ti));
                    for (U32 w = 0; w < ti.waitSemaphoreValueCount; w++) {
                        U64 semId = rd64(sis[i].pWaitSemaphores + (U64)w * 8);
                        U64 want  = rd64(ti.pWaitSemaphoreValues + (U64)w * 8);
                        Semaphore* s = objOf<Semaphore>(semId, K_SEMAPHORE, "vkQueueSubmit");
                        if (s && s->counter < want) s->counter = want;
                    }
                    for (U32 sg = 0; sg < ti.signalSemaphoreValueCount; sg++) {
                        U64 semId = rd64(sis[i].pSignalSemaphores + (U64)sg * 8);
                        U64 val   = rd64(ti.pSignalSemaphoreValues + (U64)sg * 8);
                        Semaphore* s = objOf<Semaphore>(semId, K_SEMAPHORE, "vkQueueSubmit");
                        if (s && val > s->counter) s->counter = val;
                    }
                    break;
                }
                cur = g_mem->readq(cur + 8);
            }
            for (U32 c = 0; c < sis[i].commandBufferCount; c++) {
                U64 cbId = rd64(sis[i].pCommandBuffers + (U64)c * 8);
                CmdBuf* cb = cbOf(cbId, "vkQueueSubmit");
                VK64_DROP_IF(continue, !cb);
                // Eager copy execution, against the guest memory both sides of
                // the copy live in (P1 ran these first at submit too).
                for (const Cmd& k : cb->cmds) {
                    if (k.kind == CMD_COPY_B2I) execCopy(k.copy);
                    else if (k.kind == CMD_UPDATE_BUF) execUpdateBuf(k);
                    else if (k.kind == CMD_RESET_QUERY || k.kind == CMD_WRITE_TIMESTAMP) execQueryCommand(k);
                    else if (k.kind == CMD_COPY_QUERY_RESULTS) execCopyQueryResults(k);
                }
                frames.push_back({cb, cbId});
            }
        }
        if (vkTrace()) klog_fmt("vk64: vkQueueSubmit(%u batches, %u command buffers)", (unsigned)n, (unsigned)frames.size());
        bool hasWork = false;
        for (auto& fr : frames) if (fr.first && !fr.first->cmds.empty()) { hasWork = true; break; }
        // Fallback: DXVK sometimes records draws to CBs that are never
        // submitted (e.g., 70 draws to 3 CBs, but submits reference empty CBs).
        // Scan all CmdBufs for non-empty ones and include them.
        if (!hasWork) {
            for (auto& kv : g_objs) {
                Obj* o = kv.second;
                VK64_DROP_IF(continue, !o || o->kind != K_CMDBUF);
                CmdBuf* cb = (CmdBuf*)o;
                if (!cb->cmds.empty()) {
                    frames.push_back({cb, kv.first});
                    hasWork = true;
                    if (vkTrace()) klog_fmt("vk64: vkQueueSubmit2 fallback: found non-empty CB 0x%llx (%u cmds)",
                                             (unsigned long long)kv.first, (unsigned)cb->cmds.size());
                }
            }
        }
        if (hasWork) {
            if (vkSchemaV2()) {
                bool needRb = setupReadbacks(frames);
                serializeSubmitV2(frames);
                if (needRb) completeReadbacks();
            } else serializeSubmit(frames);
        }
        if (args.a[3]) { Fence* f = objOf<Fence>(args.a[3], K_FENCE, "vkQueueSubmit"); if (f) f->signalled = 1; }
        if (vkTimingEnabled()) vkProfAcc().accSubmit += vkProfNow() - vkprofSubmitT0;
        return VK_SUCCESS;
    }
    case VK64_fn_vkQueueSubmit2:
    case VK64_fn_vkQueueSubmit2KHR: {
        // (queue, nSubmits, pSubmitInfo2*, fence) — Vulkan 1.3 path DXVK uses.
        double vkprofSubmitT0 = vkProfNow(); // BW64_VKPROF (additive)
        // VkSubmitInfo2 carries semaphore values inline (no timeline pNext).
        U32 n = (U32)args.a[1];
        std::vector<VkSubmitInfo2> sis;
        readArray(args.a[2], n, sis);
        std::vector<std::pair<CmdBuf*, U64>> frames;
        for (U32 i = 0; i < n; i++) {
            std::vector<VkSemaphoreSubmitInfo> waits, signals;
            readArray(sis[i].pWaitSemaphoreInfos, sis[i].waitSemaphoreInfoCount, waits);
            readArray(sis[i].pSignalSemaphoreInfos, sis[i].signalSemaphoreInfoCount, signals);
            for (auto& w : waits) {
                Semaphore* s = objOf<Semaphore>((U64)w.semaphore, K_SEMAPHORE, "vkQueueSubmit2");
                if (s && s->counter < w.value) s->counter = w.value;
            }
            for (auto& sg : signals) {
                Semaphore* s = objOf<Semaphore>((U64)sg.semaphore, K_SEMAPHORE, "vkQueueSubmit2");
                if (s && sg.value > s->counter) s->counter = sg.value;
            }
            std::vector<VkCommandBufferSubmitInfo> cbis;
            readArray(sis[i].pCommandBufferInfos, sis[i].commandBufferInfoCount, cbis);
            for (auto& cbi : cbis) {
                CmdBuf* cb = cbOf((U64)cbi.commandBuffer, "vkQueueSubmit2");
                VK64_DROP_IF(continue, !cb);
                for (const Cmd& k : cb->cmds) {
                    if (k.kind == CMD_COPY_B2I) execCopy(k.copy);
                    else if (k.kind == CMD_UPDATE_BUF) execUpdateBuf(k);
                    else if (k.kind == CMD_RESET_QUERY || k.kind == CMD_WRITE_TIMESTAMP) execQueryCommand(k);
                    else if (k.kind == CMD_COPY_QUERY_RESULTS) execCopyQueryResults(k);
                }
                frames.push_back({cb, (U64)cbi.commandBuffer});
            }
        }
        if (vkTrace()) klog_fmt("vk64: vkQueueSubmit2(%u batches, %u command buffers)", (unsigned)n, (unsigned)frames.size());
        // Skip serialization if no command buffers have commands (common case
        // for DXVK's empty init submits). This avoids the frame manifest
        // overhead for submits that carry no rendering work.
        bool hasWork = false;
        for (auto& fr : frames) if (fr.first && !fr.first->cmds.empty()) { hasWork = true; break; }
        // Fallback: DXVK sometimes records draws to CBs that are never
        // submitted (e.g., 70 draws to 3 CBs, but submits reference empty CBs).
        // Scan all CmdBufs for non-empty ones and include them.
        if (!hasWork) {
            for (auto& kv : g_objs) {
                Obj* o = kv.second;
                VK64_DROP_IF(continue, !o || o->kind != K_CMDBUF);
                CmdBuf* cb = (CmdBuf*)o;
                if (!cb->cmds.empty()) {
                    frames.push_back({cb, kv.first});
                    hasWork = true;
                    if (vkTrace()) klog_fmt("vk64: vkQueueSubmit2 fallback: found non-empty CB 0x%llx (%u cmds)",
                                             (unsigned long long)kv.first, (unsigned)cb->cmds.size());
                }
            }
        }
        if (hasWork) {
            if (vkSchemaV2()) {
                bool needRb = setupReadbacks(frames);
                serializeSubmitV2(frames);
                if (needRb) completeReadbacks();
            } else serializeSubmit(frames);
        }
        if (args.a[3]) { Fence* f = objOf<Fence>(args.a[3], K_FENCE, "vkQueueSubmit2"); if (f) f->signalled = 1; }
        if (vkTimingEnabled()) vkProfAcc().accSubmit += vkProfNow() - vkprofSubmitT0;
        return VK_SUCCESS;
    }
    case VK64_fn_vkQueuePresentKHR: {
        // (queue, pPresentInfo*)
        double vkprofPresentT0 = vkProfNow(); // BW64_VKPROF (additive)
        VkPresentInfoKHR pi = {};
        if (!readStruct(args.a[1], pi)) return VK_ERROR_INITIALIZATION_FAILED;
        for (U32 i = 0; i < pi.swapchainCount; i++) {
            U64 scId = rd64(pi.pSwapchains + (U64)i * 8);
            Swapchain* sc = objOf<Swapchain>(scId, K_SWAPCHAIN, "vkQueuePresentKHR");
            klog_fmt("vk64: vkQueuePresentKHR swapchain=0x%llx image=%u %ux%u -> presents frame %u",
                     (unsigned long long)scId, (unsigned)rd32(pi.pImageIndices + (U64)i * 4),
                     sc ? sc->w : 0, sc ? sc->h : 0, g_frameNo ? g_frameNo - 1 : 0);
            // One result PER SWAPCHAIN, always VK_SUCCESS. Not writing this is
            // what produces a phantom device-loss cascade in a DXVK-shaped caller:
            // it reads the array back and finds whatever was on the stack. (And
            // write it even for a swapchain we did not resolve, so the count the
            // guest sees is still complete.)
            if (pi.pResults) wr32(pi.pResults + (U64)i * 4, VK_SUCCESS);
        }
        // THE frame handoff. v1: the one JSON manifest submit built, hopped as
        // a string. v2: every queued frame's chunks, one MAIN_THREAD_EM_ASM per
        // chunk; the producer ring already dropped the oldest frames past its
        // cap, so this never carries a backlog.
        if (vkSchemaV2()) hopV2ToPage(); else emitFrameNow();
        // ---- BW64_VKPROF / BW64_METRICS frame boundary (additive) ----
        // VKPROF keeps its one-line-per-frame klog (opt-in); the live metrics
        // path consumes the same numbers and is on by default. Both are gated
        // by the first-present flag below.
        if (vkTimingEnabled()) {
            double t1 = vkProfNow();
            VkProfAcc& pa = vkProfAcc();
            double bodyDt = t1 - vkprofPresentT0;
            if (pa.haveFrame) {
                double wall = t1 - pa.lastPresentT;
                double guest = wall - pa.accSubmit - pa.accTrapOther - bodyDt;
                if (vkMetricsEnabled()) {
                    Bw64VkFrame mf;
                    mf.frameNo = pa.frameNo;
                    mf.wallMs = wall;
                    mf.submitMs = pa.accSubmit;
                    mf.trapOtherMs = pa.accTrapOther;
                    mf.presentMs = bodyDt;
                    mf.hopMs = pa.accHop;
                    mf.bufMs = pa.accBufData;
                    mf.imgMs = pa.accImgData;
                    mf.guestMs = guest;
                    mf.trapCalls = pa.accTrapCalls;
                    mf.hopChunks = pa.accHopChunks;
                    mf.hopBytes = pa.accHopBytes;
                    bw64_metrics_vk_frame(&mf);
                }
                if (vkProfEnabled()) {
                // Top-3 trapOther contributors by wall time (additive).
                char topT[256] = {0};
                {
                    struct TE { U64 id; double w; U64 c; };
                    TE top[3] = {};
                    for (U64 i = 0; i < 256; i++) {
                        double w = pa.trapWall[i]; U64 c = pa.trapCnt[i];
                        VK64_DROP_IF(continue, w <= 0);
                        for (int k = 0; k < 3; k++) if (w > top[k].w) {
                            for (int m = 2; m > k; m--) top[m] = top[m-1];
                            top[k] = {i, w, c};
                            break;
                        }
                    }
                    char* pp = topT; size_t rem = sizeof(topT);
                    for (int k = 0; k < 3 && top[k].w > 0; k++) {
                        int nn = snprintf(pp, rem, "%s%s:%.2f(%llu)", k ? ";" : "",
                                          fnName(top[k].id), top[k].w,
                                          (unsigned long long)top[k].c);
                        if (nn > 0 && (size_t)nn < rem) { pp += nn; rem -= (size_t)nn; }
                    }
                }
                klog_fmt("vk64: VKPROF frame=%llu wall=%.3fms submit=%.3fms bufdata=%.3fms bufB=%llu/%llu imgdata=%.3fms imgB=%llu/%llu trapOther=%.3fms(%llu calls) present=%.3fms hop=%.3fms hopB=%lluch/%lluB guest=%.3fms bufC=%llu imgC=%llu ser2=%.3fms queue=%.3fms topT=%s",
                         (unsigned long long)pa.frameNo, wall, pa.accSubmit,
                         pa.accBufData, (unsigned long long)pa.accBufHashed, (unsigned long long)pa.accBufSent,
                         pa.accImgData, (unsigned long long)pa.accImgHashed, (unsigned long long)pa.accImgSent,
                         pa.accTrapOther, (unsigned long long)pa.accTrapCalls, bodyDt, pa.accHop,
                         (unsigned long long)pa.accHopChunks, (unsigned long long)pa.accHopBytes, guest,
                         (unsigned long long)pa.accBufCalls, (unsigned long long)pa.accImgCalls,
                         pa.accSer2, pa.accQueue, topT);
                if (ripSamplerProfileEnabled() && cpu && cpu->thread && cpu->thread->process)
                    ripSamplerProfDumpReset((int)cpu->thread->process->id);
                }
            } else if (vkProfEnabled()) {
                klog_fmt("vk64: VKPROF enabled: per-frame breakdown follows (wall/submit/bufdata/imgdata/trapOther/present/hop/guest, ms)");
            }
            pa.haveFrame = true; pa.lastPresentT = t1; pa.frameNo++;
            vkProfReset(pa);
        }
        // First-frame gate for BW64_VKPROF (see vkProfEnabled): profiling
        // activates only after the first present has fully completed, so
        // bridge setup during WASM init is never profiled.
        g_vkProfFirstFrameDone = true;
        return VK_SUCCESS;
    }

    default:
        // Benign-error tail. This is what turned P1's original
        // "Segmentation fault" into a clean exit 0 (P1 section 1.4) and it is
        // what keeps an un-ported entry point harmless here too.
        klog_fmt("vk64: unimplemented fnId=%llu (%s) -> benign error",
                 (unsigned long long)fnId, fnName(fnId));
        vk64Unsupported((std::string("fn:") + fnName(fnId)).c_str(), "no handler; returned VK_ERROR_FEATURE_NOT_PRESENT");
        return (U64)(int64_t)VK_ERROR_FEATURE_NOT_PRESENT;
    }
}

KThread* g_thread32 = nullptr;

// 32-bit guest Vulkan present boundary (live metrics only; see the present case).
static bool g_vk32HaveFrame = false;
static double g_vk32LastPresentT = 0;
static U64 g_vk32FrameNo = 0;

// ---------------------------------------------------------------------------
// 32-bit guest memory helpers. Mirror the 64-bit helpers.
// ---------------------------------------------------------------------------
template <class T> inline bool readStruct32(U32 addr, T& out) {
    if (!addr || !g_mem32) return false;
    g_mem32->memcpyFromGuest(&out, addr, sizeof(T));
    return true;
}
template <class T> inline void writeStruct32(U32 addr, const T& in) {
    if (addr && g_mem32) g_mem32->memcpyToGuest(addr, &in, sizeof(T));
}
template <class T> inline void readArray32(U32 addr, U32 count, std::vector<T>& out) {
    out.clear();
    if (!addr || !g_mem32 || !count) return;
    out.resize(count);
    g_mem32->memcpyFromGuest(out.data(), addr, sizeof(T) * (size_t)count);
}
template <class T> inline void writeArray32(U32 addr, const std::vector<T>& in, U32 count) {
    if (addr && g_mem32 && count) g_mem32->memcpyToGuest(addr, in.data(), sizeof(T) * (size_t)count);
}
inline U32 rd32_32(U32 a) { return (a && g_mem32) ? g_mem32->readd(a) : 0; }
inline void wr32_32(U32 a, U32 v) { if (a && g_mem32) g_mem32->writed(a, v); }
inline U64 rd64_32(U32 a) {
    if (!a || !g_mem32) return 0;
    U64 lo = g_mem32->readd(a);
    U64 hi = g_mem32->readd(a + 4);
    return lo | (hi << 32);
}
inline void wr64_32(U32 a, U64 v) {
    if (!a || !g_mem32) return;
    g_mem32->writed(a, (U32)v);
    g_mem32->writed(a + 4, (U32)(v >> 32));
}
inline std::string guestStr32(U32 addr, size_t maxLen = 512) {
    std::string s;
    if (!addr || !g_mem32) return s;
    for (size_t i = 0; i < maxLen; i++) {
        U8 c = g_mem32->readb(addr + (U32)i);
        if (!c) break;
        s.push_back((char)c);
    }
    return s;
}

// ---------------------------------------------------------------------------
// 32-bit handle minting. Disjoint range from the 64-bit allocator.
// ---------------------------------------------------------------------------
U64 g_nextId32 = VK32_HANDLE_BASE;
template <class T> T* createObj32(U32 kind, U64& idOut) {
    T* o = new T();
    o->kind = kind;
    o->magic = OBJ_MAGIC;
    idOut = g_nextId32++;
    if (idOut > 0xFFFFFFFFULL) klog("vk32: FATAL: 32-bit handle space exhausted");
    g_objs[idOut] = o;
    return o;
}
template <class T> T* objOf32(U64 id32, U32 kind, const char* who) {
    return objOf<T>(id32 & 0xFFFFFFFFULL, kind, who);
}

// The driver boundary uses Linux i386 structs. Wine converts these to the
// Windows ABI; using mingw offsets here corrupts limits and adjacent structs.
inline void vk32_writeLimits(U32 lim) {
    vk32abi::VkPhysicalDeviceLimits L = {};
    fillLimits(L);
    writeStruct32(lim, L);
}


// ---------------------------------------------------------------------------
// 32-bit Features2 / Properties2 pNext walkers.
//
// 32-bit guest chained-struct layout: sType@0 (u32), pNext@4 (u32), payload@8.
// The 64-bit walkers are load-bearing for DXVK (it chains ~15 feature structs
// into VkPhysicalDeviceFeatures2 and reads the VkBool32s back); the 32-bit
// bridge never had them, so DXVK saw every chained feature as unsupported.
// These mirror the 64-bit walkers with 32-bit offsets.
// ---------------------------------------------------------------------------
namespace {
// One arm of the 32-bit Features2 walk: same shape as SET_FEATURES, over the
// vk32abi (4-byte pNext) struct and the SAME shared fill function, so the 32-bit
// and 64-bit guests are answered identically.
#define SET_FEATURES32(addr, fn, T)                                           \
    do {                                                                       \
        if ((addr) && g_mem32) {                                               \
            T f_ = {};                                                         \
            readStruct32((addr), f_);                                          \
            fn(f_);                                                            \
            writeStruct32((addr), f_);                                         \
        }                                                                      \
    } while (0)
// 32-bit field writers: hostOff = offsetof(host struct, field); valid for
// fields before any uint64_t that carries host 8-alignment padding.
inline void vk32_w32f(U32 base, size_t hostOff, U32 v) {
    wr32_32(base + (U32)hostOff - 8, v);
}
inline void vk32_w64f(U32 base, size_t hostOff, U64 v) {
    wr64_32(base + (U32)hostOff - 8, v);
}

void vk32_walkFeatures2(U32 pNextAddr) {
    for (U32 cur = pNextAddr; cur && g_mem32;) {
        U32 sType = rd32_32(cur);
        switch (sType) {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_EXT:
                SET_FEATURES32(cur, fillRobustness2, vk32abi::VkPhysicalDeviceRobustness2FeaturesEXT); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_5_FEATURES:
                SET_FEATURES32(cur, fillMaintenance5, vk32abi::VkPhysicalDeviceMaintenance5Features); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_6_FEATURES:
                SET_FEATURES32(cur, fillMaintenance6, vk32abi::VkPhysicalDeviceMaintenance6Features); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_FEATURES_EXT:
                SET_FEATURES32(cur, fillTransformFeedback, vk32abi::VkPhysicalDeviceTransformFeedbackFeaturesEXT); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VERTEX_ATTRIBUTE_DIVISOR_FEATURES_EXT:
                SET_FEATURES32(cur, fillVertexAttributeDivisor, Vk32VertexAttributeDivisorFeaturesEXT); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES:
                SET_FEATURES32(cur, fillDescriptorIndexing, vk32abi::VkPhysicalDeviceDescriptorIndexingFeatures); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES:
                SET_FEATURES32(cur, fillFloat16Int8, vk32abi::VkPhysicalDeviceShaderFloat16Int8Features); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES:
                SET_FEATURES32(cur, fillStorage16, vk32abi::VkPhysicalDevice16BitStorageFeatures); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_8BIT_STORAGE_FEATURES:
                SET_FEATURES32(cur, fillStorage8, vk32abi::VkPhysicalDevice8BitStorageFeatures); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SCALAR_BLOCK_LAYOUT_FEATURES:
                SET_FEATURES32(cur, fillScalarBlockLayout, vk32abi::VkPhysicalDeviceScalarBlockLayoutFeatures); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DEPTH_CLIP_ENABLE_FEATURES_EXT:
                SET_FEATURES32(cur, fillDepthClipEnable, vk32abi::VkPhysicalDeviceDepthClipEnableFeaturesEXT); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES:
                SET_FEATURES32(cur, fillVulkan12Features, vk32abi::VkPhysicalDeviceVulkan12Features); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES:
                SET_FEATURES32(cur, fillVulkan11Features, vk32abi::VkPhysicalDeviceVulkan11Features); break;
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES:
                SET_FEATURES32(cur, fillVulkan13Features, vk32abi::VkPhysicalDeviceVulkan13Features); break;
            default:
                vk64Unsupported("features2-struct", ("sType=" + std::to_string(sType) + " not modeled, left as-is (32-bit)").c_str());
                break;
        }
        cur = rd32_32(cur + 4); // pNext at +4 in 32-bit layout
    }
}

void vk32_walkProperties2(U32 pNextAddr) {
    static const U8 uuid[16] = {
        0x56, 0x4B, 0x57, 0x47, 0x50, 0x55, 0x21, 0x01,
        0x56, 0x4B, 0x57, 0x47, 0x50, 0x55, 0x21, 0x02,
    };
    for (U32 cur = pNextAddr; cur && g_mem32;) {
        U32 sType = rd32_32(cur);
        switch (sType) {
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES: {
                for (int i = 0; i < 16; i++) g_mem32->writeb(cur + 8 + i, uuid[i]);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceIDProperties, deviceNodeMask), 1);
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES: {
                vk32_w32f(cur, offsetof(VkPhysicalDeviceSubgroupProperties, subgroupSize), 32);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceSubgroupProperties, supportedStages), 0xffffffffu);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceSubgroupProperties, supportedOperations), 0xffffffffu);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceSubgroupProperties, quadOperationsInAllStages), (U32)VK_TRUE);
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_PROPERTIES_EXT: {
                // MUST be non-zero: DXVK D3D9 divides by these; zero => SIGFPE.
                vk32_w64f(cur, offsetof(VkPhysicalDeviceRobustness2PropertiesEXT, robustStorageBufferAccessSizeAlignment), 4);
                vk32_w64f(cur, offsetof(VkPhysicalDeviceRobustness2PropertiesEXT, robustUniformBufferAccessSizeAlignment), 4);
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_PROPERTIES: {
                for (int i = 0; i < 16; i++) g_mem32->writeb(cur + 8 + i, uuid[i]);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan11Properties, deviceNodeMask), 1);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan11Properties, subgroupSize), 32);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan11Properties, subgroupSupportedStages), 0xffffffffu);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan11Properties, subgroupSupportedOperations), 0xffffffffu);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan11Properties, subgroupQuadOperationsInAllStages), (U32)VK_TRUE);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan11Properties, maxMultiviewViewCount), 6);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan11Properties, maxMultiviewInstanceIndex), 0xFFFFFFFFu);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan11Properties, maxPerSetDescriptors), 1024);
                vk32_w64f(cur, offsetof(VkPhysicalDeviceVulkan11Properties, maxMemoryAllocationSize), 2ull * 1024 * 1024 * 1024);
                break;
            }
            case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_PROPERTIES: {
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, driverID), (U32)VK_DRIVER_ID_MESA_LLVMPIPE);
                U32 dn = (U32)offsetof(VkPhysicalDeviceVulkan12Properties, driverName) - 8;
                const char* nm = "boxedwine";
                for (int i = 0; i < 8; i++) g_mem32->writeb(cur + dn + i, (U8)nm[i]);
                U32 di = (U32)offsetof(VkPhysicalDeviceVulkan12Properties, driverInfo) - 8;
                const char* inf = "vk64";
                for (int i = 0; i < 4; i++) g_mem32->writeb(cur + di + i, (U8)inf[i]);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxUpdateAfterBindDescriptorsInAllPools), 1024);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxPerStageDescriptorUpdateAfterBindSamplers), 16);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxPerStageDescriptorUpdateAfterBindUniformBuffers), 16);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxPerStageDescriptorUpdateAfterBindStorageBuffers), 1000000);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxPerStageDescriptorUpdateAfterBindSampledImages), 1000000);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxPerStageDescriptorUpdateAfterBindStorageImages), 1000000);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxPerStageDescriptorUpdateAfterBindInputAttachments), 4);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxPerStageUpdateAfterBindResources), 128);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxDescriptorSetUpdateAfterBindSamplers), 1024);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxDescriptorSetUpdateAfterBindUniformBuffers), 1024);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxDescriptorSetUpdateAfterBindStorageBuffers), 1024);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxDescriptorSetUpdateAfterBindSampledImages), 1024);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxDescriptorSetUpdateAfterBindStorageImages), 1024);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, maxDescriptorSetUpdateAfterBindInputAttachments), 16);
                vk32_w32f(cur, offsetof(VkPhysicalDeviceVulkan12Properties, framebufferIntegerColorSampleCounts), (U32)VK_SAMPLE_COUNT_1_BIT);
                break;
            }
            default:
                break; // unknown: leave as the guest set it
        }
        cur = rd32_32(cur + 4); // pNext at +4 in 32-bit layout
    }
}
} // namespace

// Wine allocates fresh output-chain structs before each requirements query.
// Initialize every supported output so allocator decisions never use stack bytes.
inline void vk32_writeRequirements(U32 out, U64 size) {
    wr64_32(out + 8, (size + 255) & ~(U64)255);
    wr64_32(out + 16, 256);
    wr32_32(out + 24, 1); // the single advertised memory type
    for (U32 cur = rd32_32(out + 4), n = 0; cur && n < 64; ++n) {
        if (rd32_32(cur) == VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS) {
            wr32_32(cur + 8, VK_FALSE);
            wr32_32(cur + 12, VK_FALSE);
        }
        cur = rd32_32(cur + 4);
    }
}

static void captureStageSpec32(U32 va, StageSpec& out) {
    vk32abi::VkSpecializationInfo si = {};
    if (!readStruct32(va, si) || !si.mapEntryCount || si.mapEntryCount > 16 ||
        !si.pMapEntries || !si.pData || !si.dataSize || si.dataSize > 256) return;
    std::vector<vk32abi::VkSpecializationMapEntry> entries;
    readArray32(si.pMapEntries, si.mapEntryCount, entries);
    std::vector<U8> data(si.dataSize);
    g_mem32->memcpyFromGuest(data.data(), si.pData, si.dataSize);
    for (const auto& me : entries) {
        VK64_DROP_IF(break, out.n >= 16);
        VK64_DROP_IF(continue, me.offset > si.dataSize || me.size > si.dataSize - me.offset);
        VK64_DROP_IF(continue, me.size != 1 && me.size != 2 && me.size != 4 && me.size != 8);
        auto& e = out.entries[out.n++]; e.constantID = me.constantID; e.size = me.size;
        memcpy(e.value, data.data() + me.offset, me.size);
    }
}

U64 vk32Bridge(CPU* cpu, U32 fnId, U32 argsAddr) {
    std::lock_guard<std::recursive_mutex> lk(g_vkMutex);    g_mem = nullptr;
    g_dropFn = fnId;
    g_mem32 = cpu ? cpu->memory : nullptr;
    g_thread32 = cpu ? cpu->thread : nullptr;

    if (fnId == VK64_fn_witness) {
        klog("vk32: FIRST trap - 32-bit guest libvulkan.so.1 mapped (witness fnId=0)");
        return 0;
    }
    if (fnId == VK64_fn_unimplemented) {
        VK32Args a0 = {};
        if (argsAddr && g_mem32) g_mem32->memcpyFromGuest(&a0, argsAddr, sizeof(a0));
        klog_fmt("vk32: asked-for (unimplemented): %s", guestStr32((U32)a0.a[0]).c_str());
        vk64Unsupported(("fn32:" + guestStr32((U32)a0.a[0])).c_str(), "entry point not implemented; returned VK_ERROR_FEATURE_NOT_PRESENT");
        return (U64)(int64_t)VK_ERROR_FEATURE_NOT_PRESENT;
    }
    if (fnId == VK64_fn_traceProc) {
        VK32Args a0 = {};
        if (argsAddr && g_mem32) g_mem32->memcpyFromGuest(&a0, argsAddr, sizeof(a0));
        klog_fmt("vk32: gipa %s: %s", a0.a[1] ? "HIT " : "MISS", guestStr32((U32)a0.a[0]).c_str());
        return 0;
    }

    if (vkTrace()) { klog_fmt("vk32: trap %s", fnName(fnId)); g_tracedCalls++; }

    VK32Args args = {};
    if (argsAddr && g_mem32) g_mem32->memcpyFromGuest(&args, argsAddr, sizeof(args));

    switch (fnId) {
    // ================= B. Queries =================
    case VK64_fn_vkEnumerateInstanceVersion:
        wr32_32((U32)args.a[0], g_icd_api_version);
        return VK_SUCCESS;

    case VK64_fn_vkEnumerateInstanceExtensionProperties: {
        const U32 n = (U32)(sizeof(g_inst_exts) / sizeof(g_inst_exts[0]));
        if (!args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!args.a[2]) { wr32_32((U32)args.a[1], n); return VK_SUCCESS; }
        U32 want = rd32_32((U32)args.a[1]);
        U32 c = want < n ? want : n;
        std::vector<VkExtensionProperties> props((size_t)(c ? c : 1));
        for (U32 i = 0; i < c; i++) {
            memset(&props[i], 0, sizeof(props[i]));
            strncpy(props[i].extensionName, g_inst_exts[i], VK_MAX_EXTENSION_NAME_SIZE - 1);
            props[i].specVersion = VK_API_VERSION_1_1;
        }
        writeArray32((U32)args.a[2], props, c);
        wr32_32((U32)args.a[1], c);
        return c < n ? VK_INCOMPLETE : VK_SUCCESS;
    }
    case VK64_fn_vkEnumerateInstanceLayerProperties:
        if (!args.a[0]) return VK_ERROR_INITIALIZATION_FAILED;
        wr32_32((U32)args.a[0], 0);
        return VK_SUCCESS;

    case VK64_fn_vkEnumeratePhysicalDevices: {
        Instance* inst = objOf32<Instance>(args.a[0], K_INSTANCE, "vkEnumeratePhysicalDevices");
        if (!inst || !args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 n = inst->physCount;
        if (!args.a[2]) { wr32_32((U32)args.a[1], n); return VK_SUCCESS; }
        U32 want = rd32_32((U32)args.a[1]);
        U32 c = want < n ? want : n;
        std::vector<U32> devs((size_t)(c ? c : 1));
        for (U32 i = 0; i < c; i++) devs[i] = (U32)inst->phys[i];
        writeArray32((U32)args.a[2], devs, c);
        wr32_32((U32)args.a[1], c);
        return c < n ? VK_INCOMPLETE : VK_SUCCESS;
    }
    case VK64_fn_vkEnumerateDeviceExtensionProperties: {
        const U32 n = (U32)(sizeof(g_dev_exts) / sizeof(g_dev_exts[0]));
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!args.a[3]) { wr32_32((U32)args.a[2], n); return VK_SUCCESS; }
        U32 want = rd32_32((U32)args.a[2]);
        U32 c = want < n ? want : n;
        std::vector<VkExtensionProperties> props((size_t)(c ? c : 1));
        for (U32 i = 0; i < c; i++) {
            memset(&props[i], 0, sizeof(props[i]));
            strncpy(props[i].extensionName, g_dev_exts[i], VK_MAX_EXTENSION_NAME_SIZE - 1);
            props[i].specVersion = VK_API_VERSION_1_1;
        }
        writeArray32((U32)args.a[3], props, c);
        wr32_32((U32)args.a[2], c);
        return c < n ? VK_INCOMPLETE : VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceProperties: {
        PhysDev* pd = objOf32<PhysDev>(args.a[0], K_PHYSDEV, "vkGetPhysicalDeviceProperties");
        VK64_DROP_IF(return 0, !pd || !args.a[1]);
        U32 dst = (U32)args.a[1];
        wr32_32(dst + 0, g_icd_api_version);
        wr32_32(dst + 4, 1);
        wr32_32(dst + 8, 0x1234);
        wr32_32(dst + 12, 0x5678);
        wr32_32(dst + 16, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
        const char* name = "Boxedwine64 WebGPU";
        for (int i = 0; i < 256 && name[i]; i++)
            g_mem32->writeb(dst + 20 + i, (U8)name[i]);
        vk32_writeLimits(dst + offsetof(vk32abi::VkPhysicalDeviceProperties, limits));
        return 0;
    }
    case VK64_fn_vkGetPhysicalDeviceFeatures: {
        VK64_DROP_IF(return 0, !args.a[1]);
        U32 dst = (U32)args.a[1];
        VkPhysicalDeviceFeatures f = {};
        fillCoreFeatures(&f);       // same answer as the 64-bit guest (all-VkBool32 struct, identical layout)
        static_assert(sizeof(f) == 220, "VkPhysicalDeviceFeatures is 55 VkBool32");
        g_mem32->memcpyToGuest(dst, &f, sizeof(f));
        return 0;
    }
    case VK64_fn_vkGetPhysicalDeviceMemoryProperties: {
        VK64_DROP_IF(return 0, !args.a[1]);
        U32 dst = (U32)args.a[1];
        wr32_32(dst + 0, 1);
        wr32_32(dst + 4 + 0, 0x0007);
        wr32_32(dst + 4 + 4, 0);
        U32 heapOff = 4 + 32 * 8;
        wr32_32(dst + heapOff, 1);
        wr64_32(dst + heapOff + 4, 0x100000000ULL);
        wr32_32(dst + heapOff + 4 + 8, 0x0001);
        return 0;
    }
    case VK64_fn_vkGetPhysicalDeviceProperties2:
    case VK64_fn_vkGetPhysicalDeviceProperties2KHR: {
        // 32-bit VkPhysicalDeviceProperties2: sType@0, pNext@4, properties@8.
        PhysDev* pd = objOf32<PhysDev>(args.a[0], K_PHYSDEV, "vkGetPhysicalDeviceProperties2");
        if (!pd || !args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 dst = (U32)args.a[1];
        U32 props = dst + 8;
        wr32_32(props + 0, g_icd_api_version);
        wr32_32(props + 4, 1);
        wr32_32(props + 8, 0x1234);
        wr32_32(props + 12, 0x5678);
        wr32_32(props + 16, VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU);
        const char* name = "Boxedwine64 WebGPU";
        for (int i = 0; i < 256 && name[i]; i++)
            g_mem32->writeb(props + 20 + i, (U8)name[i]);
        vk32_writeLimits(props + offsetof(vk32abi::VkPhysicalDeviceProperties, limits));

        vk32_walkProperties2(rd32_32(dst + 4));
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceFeatures2:
    case VK64_fn_vkGetPhysicalDeviceFeatures2KHR: {
        // 32-bit VkPhysicalDeviceFeatures2: sType@0, pNext@4, features@8 (220 bytes).
        if (!args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 dst = (U32)args.a[1];
        U32 feat = dst + 8;
        VkPhysicalDeviceFeatures f = {};
        fillCoreFeatures(&f);
        g_mem32->memcpyToGuest(feat, &f, sizeof(f));
        vk32_walkFeatures2(rd32_32(dst + 4));
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceMemoryProperties2:
    case VK64_fn_vkGetPhysicalDeviceMemoryProperties2KHR: {
        // 32-bit VkPhysicalDeviceMemoryProperties2: sType@0, pNext@4, memProps@8.
        if (!args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 dst = (U32)args.a[1] + 8;
        wr32_32(dst + 0, 1);
        wr32_32(dst + 4 + 0, 0x0007);
        wr32_32(dst + 4 + 4, 0);
        U32 heapOff = 4 + 32 * 8;
        wr32_32(dst + heapOff, 1);
        wr64_32(dst + heapOff + 4, 0x100000000ULL);
        wr32_32(dst + heapOff + 4 + 8, 0x0001);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceSurfaceCapabilitiesKHR: {
        // 32-bit VkSurfaceCapabilitiesKHR: all u32 fields, same layout as host.
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 dst = (U32)args.a[2];
        wr32_32(dst + 0, 1);   // minImageCount
        wr32_32(dst + 4, 3);   // maxImageCount
        wr32_32(dst + 8, 0xFFFFFFFFu);  // currentExtent.width (undefined)
        wr32_32(dst + 12, 0xFFFFFFFFu); // currentExtent.height
        wr32_32(dst + 16, 1);  // minImageExtent.width
        wr32_32(dst + 20, 1);  // minImageExtent.height
        wr32_32(dst + 24, 16384); // maxImageExtent.width
        wr32_32(dst + 28, 16384); // maxImageExtent.height
        wr32_32(dst + 32, 1);  // maxImageArrayLayers
        wr32_32(dst + 36, 1);  // supportedTransforms (IDENTITY_BIT)
        wr32_32(dst + 40, 1);  // currentTransform (IDENTITY_BIT)
        wr32_32(dst + 44, 1);  // supportedCompositeAlpha (OPAQUE_BIT)
        wr32_32(dst + 48, 0x19); // supportedUsageFlags (COLOR_ATTACHMENT|TRANSFER_DST)
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceSurfaceFormatsKHR: {
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!args.a[3]) { wr32_32((U32)args.a[2], 1); return VK_SUCCESS; }
        if (rd32_32((U32)args.a[2]) < 1) { wr32_32((U32)args.a[2], 1); return VK_INCOMPLETE; }
        U32 dst = (U32)args.a[3];
        wr32_32(dst + 0, 44); // VK_FORMAT_B8G8R8A8_UNORM
        wr32_32(dst + 4, 0);  // VK_COLOR_SPACE_SRGB_NONLINEAR_KHR
        wr32_32((U32)args.a[2], 1);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceSurfacePresentModesKHR: {
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!args.a[3]) { wr32_32((U32)args.a[2], 1); return VK_SUCCESS; }
        if (rd32_32((U32)args.a[2]) < 1) { wr32_32((U32)args.a[2], 1); return VK_INCOMPLETE; }
        wr32_32((U32)args.a[3], 1); // VK_PRESENT_MODE_FIFO_KHR
        wr32_32((U32)args.a[2], 1);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceSurfaceSupportKHR:
        wr32_32((U32)args.a[3], 1); // VK_TRUE
        return VK_SUCCESS;
    case VK64_fn_vkGetPhysicalDeviceFormatProperties: {
        // 32-bit VkFormatProperties: 3x u32 (linear@0, optimal@4, buffer@8).
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 dst = (U32)args.a[2];
        U32 format = (U32)args.a[1];
        U32 img = (U32)v2FormatFeatureFlags(format);
        wr32_32(dst + 0, img);
        wr32_32(dst + 4, img);
        wr32_32(dst + 8, (U32)v2FormatBufferFeatureFlags(format));
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceImageFormatProperties: {
        // 32-bit VkImageFormatProperties: maxExtent@0(12), maxMipLevels@12,
        // maxArrayLayers@16, sampleCounts@20, maxResourceSize@24(8).
        if (!args.a[6]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 dst = (U32)args.a[6];
        wr32_32(dst + 0, 16384); wr32_32(dst + 4, 16384); wr32_32(dst + 8, 2048);
        wr32_32(dst + 12, 14);
        wr32_32(dst + 16, 2048);
        wr32_32(dst + 20, 0x0F);
        wr64_32(dst + 24, 2ull * 1024 * 1024 * 1024);
        return VK_SUCCESS;
    }

    case VK64_fn_vkGetPhysicalDeviceFormatProperties2: {
        // 32-bit VkFormatProperties2: sType@0, pNext@4, formatProperties@8
        // (linear@8, optimal@12, buffer@16).
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 dst = (U32)args.a[2];
        U32 format = (U32)args.a[1];
        U32 img = (U32)v2FormatFeatureFlags(format);
        U32 buffer = (U32)v2FormatBufferFeatureFlags(format);
        wr32_32(dst + 8, img);
        wr32_32(dst + 12, img);
        wr32_32(dst + 16, buffer);
        // FormatProperties3 pNext chain (32-bit: sType@0, pNext@4, payload@8)
        for (U32 cur = rd32_32(dst + 4); cur && g_mem32;) {
            if (rd32_32(cur) == (U32)VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_3) {
                U64 img2 = (U64)v2FormatFeatureFlags2(format);
                wr64_32(cur + 8, img2);
                wr64_32(cur + 16, img2);
                wr64_32(cur + 24, (U64)v2FormatBufferFeatureFlags2(format));
            }
            cur = rd32_32(cur + 4);
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceImageFormatProperties2: {
        // 32-bit VkImageFormatProperties2: sType@0, pNext@4, imageFormatProperties@8.
        if (!args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 dst = (U32)args.a[2] + 8;
        wr32_32(dst + 0, 16384); // maxExtent.width
        wr32_32(dst + 4, 16384); // maxExtent.height
        wr32_32(dst + 8, 2048);  // maxExtent.depth
        wr32_32(dst + 12, 14);   // maxMipLevels
        wr32_32(dst + 16, 2048); // maxArrayLayers
        wr32_32(dst + 20, 0x0F); // sampleCounts (1|2|4|8)
        wr64_32(dst + 24, 2ull * 1024 * 1024 * 1024); // maxResourceSize
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetDeviceBufferMemoryRequirements: {
        // 32-bit: VkDeviceBufferMemoryRequirements (sType@0,pNext@4,pCreateInfo@8),
        // out VkMemoryRequirements2 (sType@0,pNext@4,memReq@8: size@8,align@16,typeBits@24).
        if (!args.a[1] || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 pCreateInfo = rd32_32((U32)args.a[1] + 8);
        U64 createSize = 0;
        if (pCreateInfo) createSize = rd64_32(pCreateInfo + 12); // VkBufferCreateInfo.size@12 (i386 guest, u64 4-aligned)
        U64 size = createSize ? ((createSize + 255) & ~(U64)255) : 256;
        vk32_writeRequirements((U32)args.a[2], size);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties: {
        if (!args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!args.a[2]) { wr32_32((U32)args.a[1], 1); return VK_SUCCESS; }
        U32 want = rd32_32((U32)args.a[1]);
        if (want >= 1) {
            U32 dst = (U32)args.a[2];
            wr32_32(dst + 0, 0x000F);
            wr32_32(dst + 4, 1);
            wr32_32(dst + 8, 0);
            wr32_32(dst + 12, 1); wr32_32(dst + 16, 1); wr32_32(dst + 20, 1);
            wr32_32((U32)args.a[1], 1);
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetDeviceQueue: {
        Device* dev = objOf32<Device>(args.a[0], K_DEVICE, "vkGetDeviceQueue");
        VK64_DROP_IF(return 0, !dev || !args.a[3]);
        wr32_32((U32)args.a[3], (U32)dev->queue);
        return 0;
    }
    case VK64_fn_vkGetBufferMemoryRequirements: {
        Buffer* b = objOf32<Buffer>(args.a[1], K_BUFFER, "vkGetBufferMemoryRequirements");
        VK64_DROP_IF(return 0, !b || !args.a[2]);
        U32 dst = (U32)args.a[2];
        wr64_32(dst + 0, b->size);
        wr64_32(dst + 8, 256);
        wr32_32(dst + 16, 1);
        return 0;
    }
    case VK64_fn_vkGetImageMemoryRequirements: {
        Image* im = objOf32<Image>(args.a[1], K_IMAGE, "vkGetImageMemoryRequirements");
        VK64_DROP_IF(return 0, !im || !args.a[2]);
        U32 dst = (U32)args.a[2];
        wr64_32(dst + 0, imageDataBytes(*im));
        wr64_32(dst + 8, 256);
        wr32_32(dst + 16, 1);
        return 0;
    }
    case VK64_fn_vkGetSwapchainImagesKHR: {
        Swapchain* sc = objOf32<Swapchain>(args.a[1], K_SWAPCHAIN, "vkGetSwapchainImagesKHR");
        if (!sc || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 n = sc->nimg;
        if (!args.a[3]) { wr32_32((U32)args.a[2], n); return VK_SUCCESS; }
        U32 want = rd32_32((U32)args.a[2]);
        U32 c = want < n ? want : n;
        std::vector<U64> imgs((size_t)(c ? c : 1));
        for (U32 i = 0; i < c; i++) imgs[i] = sc->imgs[i];
        writeArray32((U32)args.a[3], imgs, c);
        wr32_32((U32)args.a[2], c);
        return c < n ? VK_INCOMPLETE : VK_SUCCESS;
    }
    case VK64_fn_vkAcquireNextImageKHR: {
        Swapchain* sc = objOf32<Swapchain>(args.a[1], K_SWAPCHAIN, "vkAcquireNextImageKHR");
        if (!sc || !args.a[5]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 idx = sc->cur % (sc->nimg ? sc->nimg : 1);
        sc->cur++;
        wr32_32((U32)args.a[5], idx);
        return VK_SUCCESS;
    }
    case VK64_fn_vkDeviceWaitIdle:
    case VK64_fn_vkQueueWaitIdle:
        return VK_SUCCESS;
    case VK64_fn_vkResetFences: {
        U32 n = (U32)args.a[1];
        for (U32 i = 0; i < n && args.a[2]; i++) {
            Fence* f = objOf32<Fence>(rd64_32((U32)args.a[2] + i * 8), K_FENCE, "vkResetFences");
            if (f) f->signalled = 0;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkWaitForFences: {
        U32 n = (U32)args.a[1];
        for (U32 i = 0; i < n && args.a[2]; i++) {
            Fence* f = objOf32<Fence>(rd64_32((U32)args.a[2] + i * 8), K_FENCE, "vkWaitForFences");
            if (f) f->signalled = 1;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetFenceStatus: {
        Fence* f = objOf32<Fence>(args.a[1], K_FENCE, "vkGetFenceStatus");
        return (f && f->signalled) ? VK_SUCCESS : VK_NOT_READY;
    }
    // D3D9 query sync (32-bit mirrors of the 64-bit cases above).
    case VK64_fn_vkCreateEvent: {
        if (!args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        createObj32<Event>(K_EVENT, id);
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkSetEvent: {
        Event* e = objOf32<Event>(args.a[1], K_EVENT, "vkSetEvent");
        if (!e) return VK_ERROR_INITIALIZATION_FAILED;
        e->signalled = 1;
        return VK_SUCCESS;
    }
    case VK64_fn_vkResetEvent: {
        Event* e = objOf32<Event>(args.a[1], K_EVENT, "vkResetEvent");
        if (!e) return VK_ERROR_INITIALIZATION_FAILED;
        e->signalled = 0;
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetEventStatus: {
        Event* e = objOf32<Event>(args.a[1], K_EVENT, "vkGetEventStatus");
        if (!e) return VK_ERROR_INITIALIZATION_FAILED;
        return e->signalled ? VK_EVENT_SET : VK_EVENT_RESET;
    }
    case VK64_fn_vkCmdSetEvent2:
    case VK64_fn_vkCmdSetEvent: {
        Event* e = objOf32<Event>(args.a[1], K_EVENT, "vkCmdSetEvent");
        if (!e) return VK_ERROR_INITIALIZATION_FAILED;
        e->signalled = 1;
        return VK_SUCCESS;
    }
    case VK64_fn_vkCmdResetEvent: {
        Event* e = objOf32<Event>(args.a[1], K_EVENT, "vkCmdResetEvent");
        if (!e) return VK_ERROR_INITIALIZATION_FAILED;
        e->signalled = 0;
        return VK_SUCCESS;
    }
    case VK64_fn_vkCmdWaitEvents:
        return VK_SUCCESS; // immediate execution: nothing to wait for
    case VK64_fn_vkCreateQueryPool: {
        QueryPoolCreateInfo32Local ci = {};
        if (!args.a[3] || !readStruct32((U32)args.a[1], ci)) return VK_ERROR_INITIALIZATION_FAILED;
        if (ci.queryType != vkquery::VKQUERY_TIMESTAMP) {
            vk64Unsupported(ci.queryType == vkquery::VKQUERY_OCCLUSION ? "occlusion-query" : "pipeline-statistics-query",
                            "query type is not implemented; VK_ERROR_FEATURE_NOT_PRESENT");
            return VK_ERROR_FEATURE_NOT_PRESENT;
        }
        U64 id = 0;
        QueryPool* pool = createObj32<QueryPool>(K_QUERYPOOL, id);
        pool->type = ci.queryType; pool->count = ci.queryCount; pool->flags = ci.flags;
        pool->results.resize(pool->count); pool->available.resize(pool->count);
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkResetQueryPool: {
        auto* pool=objOf32<QueryPool>(args.a[1],K_QUERYPOOL,"vkResetQueryPool");
        if(!pool||args.a[2]>pool->count||args.a[3]>pool->count-args.a[2])return VK_ERROR_INITIALIZATION_FAILED;
        U32 n=(U32)args.a[3];
        for(U32 i=0;i<n;i++){pool->results[args.a[2]+i]=0;pool->available[args.a[2]+i]=0;}
        return VK_SUCCESS;
    }
    case VK64_fn_vkCmdResetQueryPool: {
        auto* cb=objOf32<CmdBuf>(args.a[0],K_CMDBUF,"vkCmdResetQueryPool");auto* pool=objOf32<QueryPool>(args.a[1],K_QUERYPOOL,"vkCmdResetQueryPool");
        VK64_DROP_IF(return 0,!cb||!pool||args.a[2]>pool->count||args.a[3]>pool->count-args.a[2]);
        Cmd c;c.kind=CMD_RESET_QUERY;c.a=args.a[1];c.b=args.a[2];c.c=args.a[3];pushCmd(cb,c);return 0;
    }
    case VK64_fn_vkCmdBeginQuery:
    case VK64_fn_vkCmdEndQuery: {
        vk64Unsupported("occlusion-query", "occlusion queries are not implemented; device feature bit is false");
        return 0;
    }
    case VK64_fn_vkCmdCopyQueryPoolResults: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdCopyQueryPoolResults");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_COPY_QUERY_RESULTS;
        c.queryCopy = { args.a[1], args.a[4], args.a[5], args.a[6],
                        (U32)args.a[2], (U32)args.a[3], (U32)args.a[7] };
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkGetQueryPoolResults: {
        QueryPool* q = objOf32<QueryPool>(args.a[1], K_QUERYPOOL, "vkGetQueryPoolResults");
        if (!q || !args.a[5]) return VK_ERROR_INITIALIZATION_FAILED;
        return writeQueryResults(q, (U32)args.a[2], (U32)args.a[3], args.a[4],
                                 (U32)args.a[5], args.a[6], (U32)args.a[7]);
    }
    case VK64_fn_vkResetCommandBuffer: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkResetCommandBuffer");
        if (cb) resetCmdBuf(cb);
        return VK_SUCCESS;
    }
    case VK64_fn_vkResetCommandPool:
        return VK_SUCCESS;
    case VK64_fn_vkFreeCommandBuffers: {
        U32 n = (U32)args.a[2];
        for (U32 i = 0; i < n && args.a[3]; i++)
            dropObj(rd32_32((U32)args.a[3] + i * 4));
        return 0;
    }

    // ================= C1. Creates =================
    case VK64_fn_vkCreateInstance: {
        vk32abi::VkInstanceCreateInfo ci = {};
        if (!readStruct32((U32)args.a[0], ci) || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 instId = 0;
        Instance* inst = createObj32<Instance>(K_INSTANCE, instId);
        U64 pdId = 0;
        PhysDev* pd = createObj32<PhysDev>(K_PHYSDEV, pdId);
        pd->instance = instId;
        inst->phys[0] = pdId;
        inst->physCount = 1;
        wr32_32((U32)args.a[2], (U32)instId);
        klog_fmt("vk32: vkCreateInstance ok instance=0x%x", (unsigned)instId);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateDevice: {
        vk32abi::VkDeviceCreateInfo ci = {};
        PhysDev* pd = objOf32<PhysDev>(args.a[0], K_PHYSDEV, "vkCreateDevice");
        if (!pd || !readStruct32((U32)args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!ci.queueCreateInfoCount) return VK_ERROR_INITIALIZATION_FAILED;
        U64 devId = 0, qId = 0;
        Device* dev = createObj32<Device>(K_DEVICE, devId);
        Queue* q = createObj32<Queue>(K_QUEUE, qId);
        dev->physDev = (U32)args.a[0];
        dev->queue = qId;
        q->device = devId;
        wr32_32((U32)args.a[3], (U32)devId);
        klog_fmt("vk32: vkCreateDevice ok device=0x%x", (unsigned)devId);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateXcbSurfaceKHR:
    case VK64_fn_vkCreateXlibSurfaceKHR:
    case VK64_fn_vkCreateWin32SurfaceKHR:
    case VK64_fn_vkCreateHeadlessSurfaceEXT: {
        if (!args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        createObj32<Surface>(K_SURFACE, id);
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateSwapchainKHR: {
        vk32abi::VkSwapchainCreateInfoKHR ci = {};
        if (!readStruct32((U32)args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Swapchain* sc = createObj32<Swapchain>(K_SWAPCHAIN, id);
        sc->nimg = ci.minImageCount ? ci.minImageCount : 2;
        if (sc->nimg > 8) sc->nimg = 8;
        sc->w = ci.imageExtent.width;
        sc->h = ci.imageExtent.height;
        sc->format = ci.imageFormat;
        for (U32 i = 0; i < sc->nimg; i++) {
            U64 imgId = 0;
            Image* im = createObj32<Image>(K_IMAGE, imgId);
            im->w = sc->w; im->h = sc->h; im->d = 1;
            im->layers = 1; im->mips = 1;
            im->format = sc->format;
            im->pixBytes = fmt_bpp(sc->format);
            sc->imgs[i] = imgId;
        }
        wr64_32((U32)args.a[3], id);
        klog_fmt("vk32: vkCreateSwapchainKHR ok %ux%u x%u", sc->w, sc->h, sc->nimg);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateCommandPool: {
        vk32abi::VkCommandPoolCreateInfo ci = {};
        if (!readStruct32((U32)args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        CmdPool* p = createObj32<CmdPool>(K_CMDPOOL, id);
        p->device = (U32)args.a[0];
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateBuffer: {
        vk32abi::VkBufferCreateInfo ci = {};
        if (!readStruct32((U32)args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        if (!ci.size) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Buffer* b = createObj32<Buffer>(K_BUFFER, id);
        b->size = ci.size;
        b->usage = ci.usage;
        { // TEMP-DBG: raw guest bytes to pin the true DXVK layout (see portal-dxvk-3 allocator blowup)
            U32 base = (U32)args.a[1];
            U32 tid = g_thread32 ? (U32)g_thread32->id : 0;
            klog_fmt("vk32: DBG-BUF tid=%u @%x: %08x %08x %08x %08x %08x %08x %08x %08x %08x => size=%llu",
                     tid, base, rd32_32(base), rd32_32(base+4), rd32_32(base+8), rd32_32(base+12),
                     rd32_32(base+16), rd32_32(base+20), rd32_32(base+24), rd32_32(base+28), rd32_32(base+32),
                     (unsigned long long)ci.size);
        }
        klog_fmt("vk32: vkCreateBuffer id=0x%x size=%llu usage=0x%x", (unsigned)id,
                 (unsigned long long)ci.size, ci.usage);
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateImage: {
        vk32abi::VkImageCreateInfo ci = {};
        if (!readStruct32((U32)args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Image* im = createObj32<Image>(K_IMAGE, id);
        im->w = ci.extent.width; im->h = ci.extent.height; im->d = ci.extent.depth;
        im->layers = ci.arrayLayers ? ci.arrayLayers : 1;
        im->mips = ci.mipLevels ? ci.mipLevels : 1;
        im->format = ci.format; im->tiling = ci.tiling; im->usage = ci.usage;
        im->samples = ci.samples;
        im->pixBytes = fmt_bpp(ci.format);
        klog_fmt("vk32: vkCreateImage id=0x%x %ux%ux%u layers=%u mips=%u fmt=%u bpp=%u",
                 (unsigned)id, im->w, im->h, im->d, im->layers, im->mips,
                 im->format, (unsigned)im->pixBytes);
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateImageView: {
        vk32abi::VkImageViewCreateInfo ci = {};
        if (!readStruct32((U32)args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        Image* src = objOf32<Image>(ci.image, K_IMAGE, "vkCreateImageView");
        if (!src) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        ImageView* v = createObj32<ImageView>(K_IMAGEVIEW, id);
        v->image = ci.image;
        v->format = ci.format;
        v->aspect = ci.subresourceRange.aspectMask;
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateSampler: {
        vk32abi::VkSamplerCreateInfo ci = {};
        if (!readStruct32(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Sampler* s = createObj32<Sampler>(K_SAMPLER, id);
        s->mag = ci.magFilter; s->min = ci.minFilter; s->mipmap = ci.mipmapMode;
        s->addrU = ci.addressModeU; s->addrV = ci.addressModeV; s->addrW = ci.addressModeW;
        s->mipLodBias = ci.mipLodBias; s->anisotropyEnable = ci.anisotropyEnable;
        s->maxAniso = ci.maxAnisotropy; s->compareEnable = ci.compareEnable; s->compareOp = ci.compareOp;
        s->minLod = ci.minLod; s->maxLod = ci.maxLod; s->borderColor = ci.borderColor;
        vk64AuditSampler(ci.addressModeU, ci.addressModeV, ci.addressModeW, ci.mipLodBias, ci.borderColor);
        wr64_32(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateShaderModule: {
        vk32abi::VkShaderModuleCreateInfo ci = {};
        if (!readStruct32((U32)args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        ShaderModule* m = createObj32<ShaderModule>(K_SHADER, id);
        if (ci.codeSize && ci.pCode) {
            m->code.resize(ci.codeSize);
            g_mem32->memcpyFromGuest(m->code.data(), ci.pCode, ci.codeSize);
            m->hash = blob_hash(m->code.data(), m->code.size());
            vk64AuditSpirv(m->code);
        }
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateGraphicsPipelines: {
        // (device, cache, n, pCreateInfos*, pAllocator, out vk32abi::VkPipeline*)
        U32 n = (U32)args.a[2];
        if (!objOf32<Device>(args.a[0], K_DEVICE, "vkCreateGraphicsPipelines") || !n || !args.a[3] || !args.a[5])
            return VK_ERROR_INITIALIZATION_FAILED;
        std::vector<vk32abi::VkGraphicsPipelineCreateInfo> cis;
        readArray32(args.a[3], n, cis);
        VkResult result = VK_SUCCESS;
        for (U32 i = 0; i < n; i++) {
            const vk32abi::VkGraphicsPipelineCreateInfo& ci = cis[i];
            U64 id = 0;
            Pipeline* p = createObj32<Pipeline>(K_PIPELINE, id);
            // Each pXxxState is a guest VA: read the sub-state structs out one
            // at a time (they are small and fixed).
            vk32abi::VkPipelineInputAssemblyStateCreateInfo ia = {};
            vk32abi::VkPipelineRasterizationStateCreateInfo rs = {};
            vk32abi::VkPipelineDepthStencilStateCreateInfo ds = {};
            vk32abi::VkPipelineColorBlendStateCreateInfo cbs = {};
            readStruct32(ci.pInputAssemblyState, ia);
            readStruct32(ci.pRasterizationState, rs);
            readStruct32(ci.pDepthStencilState, ds);
            readStruct32(ci.pColorBlendState, cbs);
            p->topology = ia.topology;
            p->primitiveRestartEnable = ia.primitiveRestartEnable;
            p->cull = rs.cullMode;
            p->front = rs.frontFace;
            p->depthBiasEnable = rs.depthBiasEnable; p->depthClampEnable = rs.depthClampEnable;
            p->depthBiasConstantFactor = rs.depthBiasConstantFactor; p->depthBiasClamp = rs.depthBiasClamp;
            p->depthBiasSlopeFactor = rs.depthBiasSlopeFactor;
            p->depthTest = ds.depthTestEnable;
            p->depthWrite = ds.depthWriteEnable;
            p->depthOp = ds.depthCompareOp;
            captureStencil(*p, ds);
            // pVertexInputState: binding + attribute descriptions. Recorded
            // because the consumer cannot infer any of it — the draw call carries
            // no format, no stride and no offset — and without it there is no
            // vertex layout to build and nothing rasterizes.
            vk32abi::VkPipelineVertexInputStateCreateInfo vi = {};
            if (readStruct32(ci.pVertexInputState, vi)) {
                p->nvb = vi.vertexBindingDescriptionCount > VK64_MAX_VERT_BINDINGS
                             ? VK64_MAX_VERT_BINDINGS : vi.vertexBindingDescriptionCount;
                p->nva = vi.vertexAttributeDescriptionCount > VK64_MAX_VERT_ATTRIBS
                             ? VK64_MAX_VERT_ATTRIBS : vi.vertexAttributeDescriptionCount;
                std::vector<vk32abi::VkVertexInputBindingDescription> vbs;
                readArray32(vi.pVertexBindingDescriptions, p->nvb, vbs);
                for (U32 b = 0; b < vbs.size(); b++) {
                    p->vbStride[b] = vbs[b].stride;
                    p->vbInputRate[b] = vbs[b].inputRate;
                }
                std::vector<vk32abi::VkVertexInputAttributeDescription> vas;
                readArray32(vi.pVertexAttributeDescriptions, p->nva, vas);
                for (U32 a = 0; a < vas.size(); a++) {
                    p->vaLocation[a] = vas[a].location;
                    p->vaBinding[a] = vas[a].binding;
                    p->vaFormat[a] = vas[a].format;
                    p->vaOffset[a] = vas[a].offset;
                }
            }
            vk32abi::VkPipelineDynamicStateCreateInfo dynamic = {};
            if (readStruct32(ci.pDynamicState, dynamic)) {
                std::vector<vk32abi::VkDynamicState> states;
                readArray32(dynamic.pDynamicStates, dynamic.dynamicStateCount, states);
                for (auto state : states) {
                    const U32 value = (U32)state;
                    if (value == (U32)VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE) p->dynamicVertexStride = true;
                    if (value == (U32)VK_DYNAMIC_STATE_DEPTH_BIAS) p->depthBiasDynamic = true;
                    if (value == 1000267000u) p->cullDynamic = true;    // VK_DYNAMIC_STATE_CULL_MODE(_EXT)
                    if (value == 1000267001u) p->frontDynamic = true;   // VK_DYNAMIC_STATE_FRONT_FACE(_EXT)
                    if (value >= 6 && value <= 8) p->stencil.dynamicMask |= 1u << (value - 6);
                    if (value == 4) p->blendConstantsDynamic = true;
                }
            }
            std::vector<vk32abi::VkPipelineColorBlendAttachmentState> blends;
            readArray32(cbs.pAttachments, std::min<U32>(cbs.attachmentCount, VK64_MAX_ATTACH), blends);
            captureColorBlend(*p, blends);
            memcpy(p->blendConstants, cbs.blendConstants, sizeof(p->blendConstants));
            std::vector<vk32abi::VkPipelineShaderStageCreateInfo> stages;
            readArray32(ci.pStages, ci.stageCount, stages);
            {
                PipelineAudit audit;
                for (const auto& st : stages) audit.stageMask |= (U32)st.stage;
                audit.topology = ia.topology;
                audit.polygonMode = rs.polygonMode;
                audit.rasterizerDiscard = rs.rasterizerDiscardEnable;
                audit.logicOp = cbs.logicOpEnable;
                audit.depthBounds = ds.depthBoundsTestEnable;
                {
                    vk32abi::VkPipelineVertexInputStateCreateInfo vi32 = {};
                    if (readStruct32(ci.pVertexInputState, vi32))
                        for (U32 cur = vi32.pNext; cur && g_mem32; cur = rd32_32(cur + 4)) {
                            if (rd32_32(cur) != 1000190001u) continue;
                            const U32 cnt = std::min<U32>(rd32_32(cur + 8), 64);
                            const U32 arr = rd32_32(cur + 12);
                            for (U32 k = 0; k < cnt && arr; k++)
                                if (rd32_32(arr + k * 8 + 4) != 1) audit.vertexDivisor = true;
                        }
                }
                vk32abi::VkPipelineViewportStateCreateInfo vp = {};
                if (readStruct32(ci.pViewportState, vp)) audit.viewportCount = vp.viewportCount;
                vk32abi::VkPipelineMultisampleStateCreateInfo ms = {};
                if (readStruct32(ci.pMultisampleState, ms)) {
                    audit.sampleCount = (U32)ms.rasterizationSamples;
                    audit.sampleShading = ms.sampleShadingEnable;
                    audit.alphaToOne = ms.alphaToOneEnable;
                }
                if (vk64AuditGraphicsPipeline(audit)) {
                    dropObj(id);
                    wr64_32(args.a[5] + (U64)i * 8, 0);
                    result = VK_ERROR_FEATURE_NOT_PRESENT;
                    continue;
                }
            }
            for (const vk32abi::VkPipelineShaderStageCreateInfo& st : stages) {
                if ((U32)st.stage == (U32)VK_SHADER_STAGE_VERTEX_BIT) {
                    p->vs = st.module;
                    captureStageSpec32(st.pSpecializationInfo, p->vsSpec);
                }
                if ((U32)st.stage == (U32)VK_SHADER_STAGE_FRAGMENT_BIT) {
                    p->fs = st.module;
                    captureStageSpec32(st.pSpecializationInfo, p->fsSpec);
                }
            }
            p->layout = ci.layout;
            p->rp = ci.renderPass;
            wr64_32(args.a[5] + (U64)i * 8, id);
        }
        return result;
    }
    case VK64_fn_vkCreatePipelineLayout: {
        vk32abi::VkPipelineLayoutCreateInfo ci = {};
        if (!readStruct32((U32)args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        PipelineLayout* pl = createObj32<PipelineLayout>(K_PIPELAYOUT, id);
        pl->nsets = ci.setLayoutCount;
        for (U32 i = 0; i < ci.setLayoutCount && i < VK64_MAX_SETS && ci.pSetLayouts; i++)
            pl->sets[i] = rd64_32(ci.pSetLayouts + i * 8);
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateRenderPass: {
        vk32abi::VkRenderPassCreateInfo ci = {};
        if (!readStruct32(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        RenderPass* rp = createObj32<RenderPass>(K_RENDERPASS, id);
        rp->natt = ci.attachmentCount > VK64_MAX_ATTACH ? VK64_MAX_ATTACH : ci.attachmentCount;
        std::vector<vk32abi::VkAttachmentDescription> atts;
        readArray32(ci.pAttachments, rp->natt, atts);
        for (U32 i = 0; i < atts.size(); i++) {
            rp->formats[i] = atts[i].format;
            rp->loadOps[i] = atts[i].loadOp;
            rp->storeOps[i] = atts[i].storeOp;
            rp->stencilLoadOps[i] = atts[i].stencilLoadOp;
            rp->stencilStoreOps[i] = atts[i].stencilStoreOp;
            rp->samples[i] = atts[i].samples;
        }
        if (atts.size() > 1) rp->depthFmt = atts[1].format;
        wr64_32(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateFramebuffer: {
        vk32abi::VkFramebufferCreateInfo ci = {};
        if (!readStruct32(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Framebuffer* fb = createObj32<Framebuffer>(K_FRAMEBUFFER, id);
        fb->rp = ci.renderPass;
        fb->natt = ci.attachmentCount > VK64_MAX_ATTACH ? VK64_MAX_ATTACH : ci.attachmentCount;
        std::vector<vk32abi::VkImageView> views;
        readArray32(ci.pAttachments, fb->natt, views);
        for (U32 i = 0; i < views.size(); i++) fb->views[i] = views[i];
        fb->w = ci.width; fb->h = ci.height;
        wr64_32(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateFence: {
        if (!args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        Fence* f = createObj32<Fence>(K_FENCE, id);
        vk32abi::VkFenceCreateInfo ci = {};
        if (readStruct32((U32)args.a[1], ci) && (ci.flags & VK_FENCE_CREATE_SIGNALED_BIT))
            f->signalled = 1;
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateSemaphore: {
        if (!args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        auto* semaphore = createObj32<Semaphore>(K_SEMAPHORE, id);
        vk32abi::VkSemaphoreCreateInfo ci = {};
        readStruct32(args.a[1], ci);
        for (U32 cur = ci.pNext, n = 0; cur && n < 64; ++n) {
            if (rd32_32(cur) == VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO) {
                vk32abi::VkSemaphoreTypeCreateInfo ti = {};
                readStruct32(cur, ti);
                semaphore->isTimeline = ti.semaphoreType == (U32)VK_SEMAPHORE_TYPE_TIMELINE;
                semaphore->counter = ti.initialValue;
            }
            cur = rd32_32(cur + 4);
        }
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetSemaphoreCounterValue: {
        auto* s = objOf32<Semaphore>(args.a[1], K_SEMAPHORE, "vkGetSemaphoreCounterValue");
        if (!s || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        wr64_32(args.a[2], s->counter);
        return VK_SUCCESS;
    }
    case VK64_fn_vkSignalSemaphore: {
        vk32abi::VkSemaphoreSignalInfo si = {};
        if (!readStruct32(args.a[1], si)) return VK_ERROR_INITIALIZATION_FAILED;
        auto* s = objOf32<Semaphore>(si.semaphore, K_SEMAPHORE, "vkSignalSemaphore");
        if (!s) return VK_ERROR_INITIALIZATION_FAILED;
        s->counter = std::max<U64>(s->counter, si.value);
        return VK_SUCCESS;
    }
    case VK64_fn_vkWaitSemaphores: {
        vk32abi::VkSemaphoreWaitInfo wi = {};
        if (!readStruct32(args.a[1], wi)) return VK_ERROR_INITIALIZATION_FAILED;
        if (wi.semaphoreCount && (!wi.pSemaphores || !wi.pValues)) return VK_ERROR_INITIALIZATION_FAILED;
        bool any = false, all = true;
        for (U32 i = 0; i < wi.semaphoreCount; ++i) {
            auto* s = objOf32<Semaphore>(rd64_32(wi.pSemaphores + i * 8), K_SEMAPHORE, "vkWaitSemaphores");
            if (!s) return VK_ERROR_INITIALIZATION_FAILED;
            bool reached = s->counter >= rd64_32(wi.pValues + i * 8);
            any |= reached; all &= reached;
        }
        return ((wi.flags & VK_SEMAPHORE_WAIT_ANY_BIT) ? any : all) ? VK_SUCCESS : VK_TIMEOUT;
    }

    // ================= C2. Allocation =================
    case VK64_fn_vkAllocateMemory: {
        vk32abi::VkMemoryAllocateInfo ai = {};
        if (!readStruct32((U32)args.a[1], ai) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        { // TEMP-DBG: single atomic line (defeats thread-interleave mis-pairing)
            U32 base = (U32)args.a[1];
            U32 tid = g_thread32 ? (U32)g_thread32->id : 0;
            U32 pn = rd32_32(base + 4);
            if (pn)
                klog_fmt("vk32: DBG-ALC-CHAIN tid=%u chain sType=%u d1=%x d2=%x",
                         tid, rd32_32(pn), rd32_32(pn + 4), rd32_32(pn + 8));
            klog_fmt("vk32: DBG-ALC tid=%u @%x: s=%08x pN=%08x szLo=%08x szHi=%08x ty=%08x nx=%08x => size=%llu",
                     tid, base, rd32_32(base), pn, rd32_32(base+8),
                     rd32_32(base+12), rd32_32(base+16), rd32_32(base+20),
                     (unsigned long long)ai.allocationSize);
        }
        U64 id = 0;
        DeviceMemory* m = createObj32<DeviceMemory>(K_MEMORY, id);
        m->size = ai.allocationSize;
        // 32-bit guard rails (portal-dxvk-3 allocator blowup): a structs
        // misread can gigasize ai.allocationSize (e.g. 8<<32 from a shifted
        // u64). Truncating to U32 yields len 0 -> mmap -EINVAL, which used to
        // be returned as SUCCESS with a garbage va, poisoning DXVK's budget
        // accounting (131GB phantom). Fail loud instead: DXVK handles OOM by
        // eviction/retry; silent bogus mappings fault downstream instead.
        if (!m->size || m->size > 0x40000000ULL) { // 1GB cap: 250x d3dconf9's max legit (4MB)
            klog_fmt("vk32: vkAllocateMemory REJECTED size=%llu (probable struct misread)",
                     (unsigned long long)m->size);
            dropObj(id & 0xFFFFFFFFULL);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        // Allocate guest VA via 32-bit mmap (addr=0 = find free range).
        // Mirrors KMemory64::mmapReserveAndMap used by the 64-bit path.
        m->va = g_mem32->mmap(g_thread32, 0, (U32)ai.allocationSize,
                             K_PROT_READ | K_PROT_WRITE,
                             K_MAP_ANONYMOUS | K_MAP_PRIVATE, -1, 0, false);
        if (m->va >= 0xFFFFF000U) { // -errno range: mmap failed
            klog_fmt("vk32: vkAllocateMemory mmap failed size=%llu va=0x%x",
                     (unsigned long long)m->size, (unsigned)m->va);
            dropObj(id & 0xFFFFFFFFULL);
            return VK_ERROR_OUT_OF_DEVICE_MEMORY;
        }
        klog_fmt("vk32: vkAllocateMemory id=0x%x size=%llu type=%u va=0x%x",
                 (unsigned)id, (unsigned long long)m->size, ai.memoryTypeIndex, (unsigned)m->va);
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkAllocateCommandBuffers: {
        vk32abi::VkCommandBufferAllocateInfo ai = {};
        if (!readStruct32((U32)args.a[1], ai) || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        for (U32 i = 0; i < ai.commandBufferCount; i++) {
            U64 id = 0;
            CmdBuf* cb = createObj32<CmdBuf>(K_CMDBUF, id);
            cb->pool = ai.commandPool;
            wr32_32((U32)args.a[2] + i * 4, (U32)id);
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkBindBufferMemory: {
        Buffer* b = objOf32<Buffer>(args.a[1], K_BUFFER, "vkBindBufferMemory");
        DeviceMemory* m = objOf32<DeviceMemory>(args.a[2], K_MEMORY, "vkBindBufferMemory");
        if (!b || !m) return VK_ERROR_INITIALIZATION_FAILED;
        b->mem = args.a[2];
        b->memOff = args.a[3];
        return VK_SUCCESS;
    }
    case VK64_fn_vkBindImageMemory: {
        Image* im = objOf32<Image>(args.a[1], K_IMAGE, "vkBindImageMemory");
        DeviceMemory* m = objOf32<DeviceMemory>(args.a[2], K_MEMORY, "vkBindImageMemory");
        if (!im || !m) return VK_ERROR_INITIALIZATION_FAILED;
        im->mem = args.a[2];
        im->memOff = args.a[3];
        im->pixBytes = imageDataBytes(*im);
        return VK_SUCCESS;
    }
    case VK64_fn_vkMapMemory: {
        DeviceMemory* m = objOf32<DeviceMemory>(args.a[1], K_MEMORY, "vkMapMemory");
        if (!m || !args.a[5]) return VK_ERROR_MEMORY_MAP_FAILED;
        wr32_32((U32)args.a[5], (U32)(m->va + args.a[2]));
        return VK_SUCCESS;
    }
    case VK64_fn_vkUnmapMemory:
    case VK64_fn_vkFlushMappedMemoryRanges:
    case VK64_fn_vkInvalidateMappedMemoryRanges:
        return VK_SUCCESS;

    // ================= C3. Destroys =================
    case VK64_fn_vkDestroyInstance:
    case VK64_fn_vkDestroyDevice:
        dropObj(args.a[0] & 0xFFFFFFFFULL);
        return 0;
    case VK64_fn_vkFreeMemory: {
        DeviceMemory* m = objOf32<DeviceMemory>(args.a[1], K_MEMORY, "vkFreeMemory");
        if (m) {
            if (m->va) g_mem32->unmap((U32)m->va, (U32)m->size);
            dropObj(args.a[1] & 0xFFFFFFFFULL);
        }
        return 0;
    }
    case VK64_fn_vkDestroySurfaceKHR:
    case VK64_fn_vkDestroySwapchainKHR:
    case VK64_fn_vkDestroyCommandPool:
    case VK64_fn_vkDestroyBuffer:
    case VK64_fn_vkDestroyImage:
    case VK64_fn_vkDestroyImageView:
    case VK64_fn_vkDestroySampler:
    case VK64_fn_vkDestroyRenderPass:
    case VK64_fn_vkDestroyFramebuffer:
    case VK64_fn_vkDestroyPipeline:
    case VK64_fn_vkDestroyPipelineLayout:
    case VK64_fn_vkDestroyDescriptorSetLayout:
    case VK64_fn_vkDestroyDescriptorPool:
    case VK64_fn_vkDestroyPipelineCache:
    case VK64_fn_vkDestroyFence:
    case VK64_fn_vkDestroySemaphore:
    case VK64_fn_vkDestroyDescriptorUpdateTemplate:
        dropObj(args.a[1] & 0xFFFFFFFFULL);
        return 0;

    case VK64_fn_vkDestroyShaderModule: {
        auto* module = objOf32<ShaderModule>(args.a[1], K_SHADER, "vkDestroyShaderModule");
        if (module) module->destroyed = 1;
        return 0;
    }

    // ================= D. Command recording =================
    case VK64_fn_vkBeginCommandBuffer: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkBeginCommandBuffer");
        if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
        resetCmdBuf(cb);
        return VK_SUCCESS;
    }
    case VK64_fn_vkEndCommandBuffer: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkEndCommandBuffer");
        if (!cb) return VK_ERROR_INITIALIZATION_FAILED;
        return VK_SUCCESS;
    }
    case VK64_fn_vkCmdBeginRenderPass2:
    case VK64_fn_vkCmdBeginRenderPass2KHR:
    case VK64_fn_vkCmdBeginRenderPass: {
        // (cmdBuf, pRenderPassBegin* (clear values!), contents)
        klog_fmt("vk32: BEGIN_RP_CALLED cb=0x%llx", (unsigned long long)args.a[0]);
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdBeginRenderPass");
        vk32abi::VkRenderPassBeginInfo bi = {};
        if (!cb || !readStruct32(args.a[1], bi)) { klog("vk32: BEGIN_RP_FAILED"); return 0; }
        klog_fmt("vk32: BEGIN_RP fb=0x%llx", (unsigned long long)bi.framebuffer);
        Cmd c;
        c.kind = CMD_BEGIN_RP;
        c.a = bi.renderPass;
        c.b = bi.framebuffer;
        // pClearValues is a guest VA: colour first, then depth/stencil (P1 read
        // index 0 as colour and index 1 as depth).
        std::vector<vk32abi::VkClearValue> clears;
        readArray32(bi.pClearValues, std::min<U32>(bi.clearValueCount, VK64_MAX_ATTACH), clears);
        captureRenderPassClears(c, objOf32<RenderPass>(bi.renderPass, K_RENDERPASS, "vkCmdBeginRenderPass"), clears);
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdEndRenderPass:
    case VK64_fn_vkCmdEndRenderPass2:
    case VK64_fn_vkCmdEndRenderPass2KHR:
    case VK64_fn_vkCmdEndRendering:
    case VK64_fn_vkCmdEndRenderingKHR: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdEndRenderPass");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_END_RP;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBindPipeline: {
        // (cmdBuf, bindPoint, pipeline)
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdBindPipeline");
        VK64_DROP_IF(return 0, !cb);
        Cmd c;
        c.kind = CMD_BIND_PIPE;
        c.a = args.a[2];
        cb->pipe = args.a[2];
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBindDescriptorSets: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdBindDescriptorSets");
        VK64_DROP_IF(return 0, !cb);
        U32 first = (U32)args.a[3], count = (U32)args.a[4];
        for (U32 i = 0; i < count && first + i < VK64_MAX_SETS && args.a[5]; i++)
            cb->sets[first + i] = rd64_32((U32)args.a[5] + i * 8);
        Cmd c; c.kind = CMD_BIND_SETS;
        memcpy(c.sets, cb->sets, sizeof(c.sets)); c.a = args.a[3]; c.b = args.a[4];
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdSetStencilCompareMask:
    case VK64_fn_vkCmdSetStencilWriteMask:
    case VK64_fn_vkCmdSetStencilReference: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "dynamic raster state");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_STENCIL_STATE;
        c.a = fnId == VK64_fn_vkCmdSetStencilCompareMask ? 0 : fnId == VK64_fn_vkCmdSetStencilWriteMask ? 1 : 2;
        c.b = (U32)args.a[1]; c.c = (U32)args.a[2];
        pushCmd(cb, c); return 0;
    }
    case VK64_fn_vkCmdSetBlendConstants: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "dynamic raster state");
        VK64_DROP_IF(return 0, !cb || !args.a[1]);
        Cmd c; c.kind = CMD_BLEND_CONSTANTS;
        g_mem32->memcpyFromGuest(c.f, (U32)args.a[1], 16);
        pushCmd(cb, c); return 0;
    }
    case VK64_fn_vkCmdSetCullMode:
    case VK64_fn_vkCmdSetFrontFace: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "dynamic raster state");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_RASTER_STATE;
        c.a = fnId == VK64_fn_vkCmdSetCullMode ? 0 : 1; c.b = (U32)args.a[1];
        pushCmd(cb, c); return 0;
    }
    // Timestamp query results are synthesized from the host clock at read time and
    // WebGPU has no depth-bounds test (the feature is not advertised), so both are
    // accepted without a record.
    case VK64_fn_vkCmdWriteTimestamp: {
        auto* cb=objOf32<CmdBuf>(args.a[0],K_CMDBUF,"vkCmdWriteTimestamp");auto* pool=objOf32<QueryPool>(args.a[2],K_QUERYPOOL,"vkCmdWriteTimestamp");
        VK64_DROP_IF(return 0,!cb||!pool||pool->type!=vkquery::VKQUERY_TIMESTAMP||args.a[3]>=pool->count);
        Cmd c;c.kind=CMD_WRITE_TIMESTAMP;c.a=args.a[2];c.b=args.a[3];pushCmd(cb,c);return 0;
    }
    case VK64_fn_vkCmdSetDepthBounds:
        return 0;
    case VK64_fn_vkCmdSetDepthBias: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdSetDepthBias");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_DEPTH_BIAS;
        for (U32 i = 0; i < 3; ++i) { U32 bits = (U32)args.a[i + 1]; memcpy(&c.f[i], &bits, sizeof(bits)); }
        pushCmd(cb, c); return 0;
    }
    case VK64_fn_vkCmdSetViewport: {
        // (cmdBuf, first, count, pViewports*)
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdSetViewport");
        std::vector<vk32abi::VkViewport> vps;
        readArray32(args.a[3], (U32)args.a[2], vps);
        if (!cb || vps.empty()) return 0;
        Cmd c;
        c.kind = CMD_VIEWPORT;
        c.f[0] = vps[0].x; c.f[1] = vps[0].y; c.f[2] = vps[0].width; c.f[3] = vps[0].height;
        c.f[4] = vps[0].minDepth; c.f[5] = vps[0].maxDepth;
        memcpy(cb->vp, c.f, sizeof(cb->vp));
        cb->haveVp = true;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdSetScissor: {
        // (cmdBuf, first, count, pScissors*)
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdSetScissor");
        std::vector<vk32abi::VkRect2D> scs;
        readArray32(args.a[3], (U32)args.a[2], scs);
        if (!cb || scs.empty()) return 0;
        Cmd c;
        c.kind = CMD_SCISSOR;
        c.f[0] = (float)scs[0].offset.x; c.f[1] = (float)scs[0].offset.y;
        c.f[2] = (float)scs[0].extent.width; c.f[3] = (float)scs[0].extent.height;
        memcpy(cb->sci, c.f, sizeof(cb->sci));
        cb->haveSci = true;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdDraw: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdDraw");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_DRAW;
        c.a = args.a[1]; c.b = args.a[2]; c.c = args.a[3]; c.d = args.a[4];
        if (!vkSchemaV2()) captureDraw(cb, c);
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdDrawIndexed: {
        // (cmdBuf, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance)
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdDrawIndexed");
        VK64_DROP_IF(return 0, !cb);
        Cmd c;
        c.kind = CMD_DRAW_INDEXED;
        c.a = args.a[1];   // indexCount
        c.b = args.a[2];   // instanceCount
        c.c = args.a[3];   // firstIndex
        c.diVertexOffset = (int32_t)(U32)args.a[4];
        c.copy.ox = (int32_t)args.a[4];  // vertexOffset (signed; rides the Copy spare for v2)
        c.d = args.a[5];   // firstInstance
        if (!vkSchemaV2()) captureIndexedDraw(cb, c);
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBindVertexBuffers:
    case VK64_fn_vkCmdBindVertexBuffers2: {
        // (cmdBuf, firstBinding, bindingCount, pBuffers*, pOffsets*)
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdBindVertexBuffers");
        VK64_DROP_IF(return 0, !cb);
        U32 first = (U32)args.a[1], count = (U32)args.a[2];
        if (!args.a[3] || !args.a[4] || !count || first >= VK64_MAX_VERT_BINDINGS) return 0;
        if (first + count > VK64_MAX_VERT_BINDINGS) count = VK64_MAX_VERT_BINDINGS - first;
        for (U32 i = 0; i < count; i++) {
            cb->vbs[first + i] = rd64_32(args.a[3] + (U64)i * 8);
            cb->vbOff[first + i] = rd64_32(args.a[4] + (U64)i * 8);
            if (fnId == VK64_fn_vkCmdBindVertexBuffers2 && args.a[6]) {
                cb->vbStride[first + i] = (U32)rd64_32(args.a[6] + (U64)i * 8);
                cb->vbHasStride[first + i] = true;
            }
        }
        U32 end = first + count;
        if (end > cb->nvb) cb->nvb = end;
        Cmd c;
        c.kind = CMD_BIND_VB;
        c.a = first; c.b = count;
        c.vbCount = cb->nvb;
        memcpy(c.vbBuffers, cb->vbs, sizeof(c.vbBuffers));
        memcpy(c.vbOffsets, cb->vbOff, sizeof(c.vbOffsets));
        memcpy(c.vbStrides, cb->vbStride, sizeof(c.vbStrides));
        memcpy(c.vbHasStride, cb->vbHasStride, sizeof(c.vbHasStride));
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBindIndexBuffer:
    case VK64_fn_vkCmdBindIndexBuffer2:
    case VK64_fn_vkCmdBindIndexBuffer2KHR: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdBindIndexBuffer");
        VK64_DROP_IF(return 0, !cb);
        cb->ib = args.a[1]; cb->ibOff = args.a[2];
        cb->ibType = (U32)args.a[fnId == VK64_fn_vkCmdBindIndexBuffer ? 3 : 4];
        Cmd c; c.kind = CMD_BIND_IB; c.a = cb->ib; c.b = cb->ibOff; c.c = cb->ibType;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdPipelineBarrier:
    case VK64_fn_vkCmdPipelineBarrier2:
    case VK64_fn_vkCmdPipelineBarrier2KHR: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdPipelineBarrier");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_BARRIER;
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdPushConstants:
    case VK64_fn_vkCmdPushConstants2:
    case VK64_fn_vkCmdPushConstants2KHR: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdPushConstants");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_PUSH_CONST;
        if (fnId == VK64_fn_vkCmdPushConstants) {
            c.pcOffset = (U32)args.a[3];
            c.pcSize = (U32)args.a[4];
            c.pcStageFlags = (U32)args.a[2];
            U32 n = c.pcSize > VK64_MAX_PUSH_BYTES ? VK64_MAX_PUSH_BYTES : c.pcSize;
            if (args.a[5] && n) g_mem32->memcpyFromGuest(c.pcBytes, (U32)args.a[5], n);
        }
        pushCmd(cb, c);
        return 0;
    }
    // ---- 23 game-breaking functions ----
    case VK64_fn_vkCmdFillBuffer: {
        Buffer* dst = objOf32<Buffer>(args.a[1], K_BUFFER, "vkCmdFillBuffer");
        VK64_DROP_IF(return 0, !dst || !dst->mem);
        DeviceMemory* dmem = objOf32<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdFillBuffer");
        VK64_DROP_IF(return 0, !dmem || !dmem->va);
        U64 offset = dst->memOff + args.a[2];
        U64 size = args.a[3];
        U32 data = (U32)args.a[4];
        VK64_DROP_IF(return 0, offset >= dmem->size);
        if (offset + size > dmem->size) size = dmem->size - offset;
        if (!size) return 0;
        U32 va = (U32)(dmem->va + offset);
        for (U64 i = 0; i + 4 <= size; i += 4)
            g_mem32->memcpyToGuest(va + (U32)i, &data, 4);
        if (size % 4) g_mem32->memcpyToGuest(va + (U32)(size & ~3ULL), &data, (U32)(size % 4));
        dst->dirtyGen = g_bufDirtyTick++;
        return 0;
    }
    case VK64_fn_vkCmdClearColorImage: {
        Image* im = objOf32<Image>(args.a[1], K_IMAGE, "vkCmdClearColorImage");
        VK64_DROP_IF(return 0, !im || !im->mem);
        DeviceMemory* dmem = objOf32<DeviceMemory>(im->mem, K_MEMORY, "vkCmdClearColorImage");
        VK64_DROP_IF(return 0, !dmem || !dmem->va);
        vk32abi::VkClearColorValue col = {};
        readStruct32((U32)args.a[3], col);
        U32 pxBytes = fmt_bpp(im->format);
        U32 pixel; memcpy(&pixel, col.float32, 4);
        U64 total = (U64)im->w * im->h * im->d * im->layers * pxBytes;
        VK64_DROP_IF(return 0, im->memOff >= dmem->size);
        total = std::min(total, dmem->size - im->memOff);
        U32 va = (U32)(dmem->va + im->memOff);
        for (U64 off = 0; off < total; off += 4)
            g_mem32->memcpyToGuest(va + (U32)off, &pixel, 4);
        return 0;
    }
    case VK64_fn_vkCmdClearAttachments:
    case VK64_fn_vkCmdClearAttachmentsKHR: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdClearAttachments");
        VK64_DROP_IF(return 0, !cb);
        if (args.a[1] > 8 || args.a[3] > 4096) { g_frameOverflow = true; return 0; }
        Cmd c; c.kind = CMD_CLEAR_ATTACHMENTS;
        readArray32(args.a[2], (U32)args.a[1], c.clearAttachments);
        readArray32(args.a[4], (U32)args.a[3], c.clearRects);
        if (!c.clearAttachments.empty() && !c.clearRects.empty()) pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdCopyBuffer: {
        Buffer* src = objOf32<Buffer>(args.a[1], K_BUFFER, "vkCmdCopyBuffer");
        Buffer* dst = objOf32<Buffer>(args.a[2], K_BUFFER, "vkCmdCopyBuffer");
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf32<DeviceMemory>(src->mem, K_MEMORY, "vkCmdCopyBuffer");
        DeviceMemory* dmem = objOf32<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdCopyBuffer");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        U32 nreg = (U32)args.a[3];
        U8 tmp[65536];
        for (U32 i = 0; i < nreg && args.a[4]; i++) {
            vk32abi::VkBufferCopy r = {};
            readStruct32((U32)args.a[4] + i * (U32)sizeof(r), r);
            U64 srcVa = smem->va + src->memOff + r.srcOffset;
            U64 dstVa = dmem->va + dst->memOff + r.dstOffset;
            U64 size = r.size;
            while (size > 0) {
                U32 chunk = size > sizeof(tmp) ? sizeof(tmp) : (U32)size;
                g_mem32->memcpyFromGuest(tmp, (U32)srcVa, chunk);
                g_mem32->memcpyToGuest((U32)dstVa, tmp, chunk);
                srcVa += chunk; dstVa += chunk; size -= chunk;
            }
        }
        dst->dirtyGen = g_bufDirtyTick++;
        return 0;
    }
    case VK64_fn_vkCmdCopyBuffer2:
    case VK64_fn_vkCmdCopyBuffer2KHR: {
        VK64_DROP_IF(return 0, !args.a[1]);
        U32 info = (U32)args.a[1];
        U64 src = rd64_32(info + 8);
        U64 dst = rd64_32(info + 16);
        U32 nreg = rd32_32(info + 24);
        U32 pReg = rd32_32(info + 28);
        Buffer* srcB = objOf32<Buffer>(src, K_BUFFER, "vkCmdCopyBuffer2");
        Buffer* dstB = objOf32<Buffer>(dst, K_BUFFER, "vkCmdCopyBuffer2");
        VK64_DROP_IF(return 0, !srcB || !dstB || !srcB->mem || !dstB->mem);
        DeviceMemory* smem = objOf32<DeviceMemory>(srcB->mem, K_MEMORY, "vkCmdCopyBuffer2");
        DeviceMemory* dmem = objOf32<DeviceMemory>(dstB->mem, K_MEMORY, "vkCmdCopyBuffer2");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        U8 tmp[65536];
        for (U32 i = 0; i < nreg && pReg; i++) {
            U32 r = pReg + i * 32;
            U64 srcOff = rd64_32(r + 8);
            U64 dstOff = rd64_32(r + 16);
            U64 size = rd64_32(r + 24);
            U64 srcVa = smem->va + srcB->memOff + srcOff;
            U64 dstVa = dmem->va + dstB->memOff + dstOff;
            while (size > 0) {
                U32 chunk = size > sizeof(tmp) ? sizeof(tmp) : (U32)size;
                g_mem32->memcpyFromGuest(tmp, (U32)srcVa, chunk);
                g_mem32->memcpyToGuest((U32)dstVa, tmp, chunk);
                srcVa += chunk; dstVa += chunk; size -= chunk;
            }
        }
        dstB->dirtyGen = g_bufDirtyTick++;
        return 0;
    }
    case VK64_fn_vkCmdCopyImage:
    case VK64_fn_vkCmdCopyImage2:
    case VK64_fn_vkCmdCopyImage2KHR: {
        Image* src = objOf32<Image>(args.a[1], K_IMAGE, "vkCmdCopyImage");
        Image* dst = nullptr;
        U32 nreg = 0, pReg = 0;
        if (fnId == VK64_fn_vkCmdCopyImage) {
            dst = objOf32<Image>(args.a[3], K_IMAGE, "vkCmdCopyImage");
            nreg = (U32)args.a[5]; pReg = (U32)args.a[6];
        } else {
            dst = objOf32<Image>(args.a[2], K_IMAGE, "vkCmdCopyImage2");
            nreg = (U32)args.a[3]; pReg = (U32)args.a[4];
        }
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf32<DeviceMemory>(src->mem, K_MEMORY, "vkCmdCopyImage");
        DeviceMemory* dmem = objOf32<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdCopyImage");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        U32 pxBytes = fmt_bpp(src->format);
        U8 tmp[65536];
        for (U32 i = 0; i < nreg && pReg; i++) {
            vk32abi::VkImageCopy r = {};
            const U32 header = fnId == VK64_fn_vkCmdCopyImage ? 0 : 8;
            readStruct32(pReg + i * ((U32)sizeof(r) + header) + header, r);
            recordImageCopy(objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdCopyImage"), args.a[1],
                fnId == VK64_fn_vkCmdCopyImage ? args.a[3] : args.a[2],
                r.srcSubresource.mipLevel, r.srcSubresource.baseArrayLayer, r.dstSubresource.mipLevel, r.dstSubresource.baseArrayLayer,
                r.srcOffset.x, r.srcOffset.y, r.srcOffset.z, r.dstOffset.x, r.dstOffset.y, r.dstOffset.z,
                r.extent.width, r.extent.height, r.extent.depth, std::min(r.srcSubresource.layerCount, r.dstSubresource.layerCount));
            U32 w = r.extent.width, h = r.extent.height;
            U32 srcRow = src->w * pxBytes, dstRow = dst->w * pxBytes;
            for (U32 y = 0; y < h; y++) {
                U32 sy = (U32)r.srcOffset.y + y, dy = (U32)r.dstOffset.y + y;
                VK64_DROP_IF(continue, sy >= src->h || dy >= dst->h);
                U64 srcVa = smem->va + src->memOff + (U64)sy * srcRow + (U64)(U32)r.srcOffset.x * pxBytes;
                U64 dstVa = dmem->va + dst->memOff + (U64)dy * dstRow + (U64)(U32)r.dstOffset.x * pxBytes;
                U32 rowBytes = w * pxBytes, done = 0;
                while (done < rowBytes) {
                    U32 chunk = rowBytes - done > sizeof(tmp) ? sizeof(tmp) : rowBytes - done;
                    g_mem32->memcpyFromGuest(tmp, (U32)(srcVa + done), chunk);
                    g_mem32->memcpyToGuest((U32)(dstVa + done), tmp, chunk);
                    done += chunk;
                }
            }
        }
        return 0;
    }
    case VK64_fn_vkCmdCopyBufferToImage:
    case VK64_fn_vkCmdCopyBufferToImage2:
    case VK64_fn_vkCmdCopyBufferToImage2KHR: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdCopyBufferToImage");
        VK64_DROP_IF(return 0, !cb);
        U64 src = args.a[1], dst = args.a[2];
        U32 count = args.a[4], regions = args.a[5];
        const bool copy2 = fnId != VK64_fn_vkCmdCopyBufferToImage;
        if (copy2) {
            vk32abi::VkCopyBufferToImageInfo2 info = {};
            VK64_DROP_IF(return 0, !readStruct32(args.a[1], info));
            src = info.srcBuffer; dst = info.dstImage;
            count = info.regionCount; regions = info.pRegions;
        }
        VK64_DROP_IF(return 0, count > 1024 || !regions);
        auto record = [&](const auto& r) {
            Cmd c; c.kind = CMD_COPY_B2I; c.a = src; c.b = dst;
            c.copy.srcBuffer = src; c.copy.dstImage = dst;
            c.copy.bufOff = r.bufferOffset; c.copy.rowLen = r.bufferRowLength;
            c.copy.imgH = r.bufferImageHeight;
            c.copy.ox = r.imageOffset.x; c.copy.oy = r.imageOffset.y; c.copy.oz = r.imageOffset.z;
            c.copy.w = r.imageExtent.width; c.copy.h = r.imageExtent.height; c.copy.depth = r.imageExtent.depth;
            c.copy.mip = r.imageSubresource.mipLevel;
            c.copy.baseLayer = r.imageSubresource.baseArrayLayer;
            c.copy.layerCount = r.imageSubresource.layerCount;
            pushCmd(cb, c);
        };
        for (U32 i = 0; i < count; ++i) {
            if (copy2) {
                vk32abi::VkBufferImageCopy2 r = {};
                readStruct32(regions + (U64)i * sizeof(r), r); record(r);
            } else {
                vk32abi::VkBufferImageCopy r = {};
                readStruct32(regions + (U64)i * sizeof(r), r); record(r);
            }
        }
        return 0;
    }
    case VK64_fn_vkCmdCopyImageToBuffer:
    case VK64_fn_vkCmdCopyImageToBuffer2:
    case VK64_fn_vkCmdCopyImageToBuffer2KHR: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdCopyImageToBuffer");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_COPY_I2B;
        c.a = args.a[1]; c.b = args.a[3];
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdBlitImage:
    case VK64_fn_vkCmdBlitImage2:
    case VK64_fn_vkCmdBlitImage2KHR: {
        Image* src = objOf32<Image>(args.a[1], K_IMAGE, "vkCmdBlitImage");
        Image* dst = nullptr;
        U32 nreg = 0, pReg = 0;
        if (fnId == VK64_fn_vkCmdBlitImage) {
            dst = objOf32<Image>(args.a[3], K_IMAGE, "vkCmdBlitImage");
            nreg = (U32)args.a[5]; pReg = (U32)args.a[6];
        } else {
            dst = objOf32<Image>(args.a[2], K_IMAGE, "vkCmdBlitImage2");
            nreg = (U32)args.a[3]; pReg = (U32)args.a[4];
        }
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf32<DeviceMemory>(src->mem, K_MEMORY, "vkCmdBlitImage");
        DeviceMemory* dmem = objOf32<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdBlitImage");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        U32 pxBytes = fmt_bpp(src->format);
        U32 srcRowPitch = src->w * pxBytes, dstRowPitch = dst->w * pxBytes;
        for (U32 i = 0; i < nreg && pReg; i++) {
            vk32abi::VkImageBlit r = {};
            readStruct32(pReg + i * (U32)sizeof(r), r);
            const int32_t srcOffsets[2][3] = {
                {r.srcOffsets[0].x, r.srcOffsets[0].y, r.srcOffsets[0].z},
                {r.srcOffsets[1].x, r.srcOffsets[1].y, r.srcOffsets[1].z},
            };
            const int32_t dstOffsets[2][3] = {
                {r.dstOffsets[0].x, r.dstOffsets[0].y, r.dstOffsets[0].z},
                {r.dstOffsets[1].x, r.dstOffsets[1].y, r.dstOffsets[1].z},
            };
            const U32 filter = fnId == VK64_fn_vkCmdBlitImage ? (U32)args.a[7] : (U32)args.a[5];
            recordImageBlit(objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdBlitImage"), args.a[1],
                fnId == VK64_fn_vkCmdBlitImage ? args.a[3] : args.a[2],
                r.srcSubresource.mipLevel, r.srcSubresource.baseArrayLayer,
                r.dstSubresource.mipLevel, r.dstSubresource.baseArrayLayer, filter,
                srcOffsets, dstOffsets, r.srcSubresource.layerCount);
            int32_t srcW = r.srcOffsets[1].x - r.srcOffsets[0].x;
            int32_t srcH = r.srcOffsets[1].y - r.srcOffsets[0].y;
            int32_t dstW = r.dstOffsets[1].x - r.dstOffsets[0].x;
            int32_t dstH = r.dstOffsets[1].y - r.dstOffsets[0].y;
            VK64_DROP_IF(continue, srcW <= 0 || srcH <= 0 || dstW <= 0 || dstH <= 0);
            for (int32_t dy = 0; dy < dstH; dy++) {
                int32_t sy = r.srcOffsets[0].y + (dy * srcH) / dstH;
                VK64_DROP_IF(continue, sy < 0 || (U32)sy >= src->h);
                U32 dyAbs = (U32)(r.dstOffsets[0].y + dy);
                VK64_DROP_IF(continue, dyAbs >= dst->h);
                for (int32_t dx = 0; dx < dstW; dx++) {
                    int32_t sx = r.srcOffsets[0].x + (dx * srcW) / dstW;
                    VK64_DROP_IF(continue, sx < 0 || (U32)sx >= src->w);
                    U32 dxAbs = (U32)(r.dstOffsets[0].x + dx);
                    VK64_DROP_IF(continue, dxAbs >= dst->w);
                    U32 spx = 0;
                    g_mem32->memcpyFromGuest(&spx, (U32)(smem->va + src->memOff + (U64)sy * srcRowPitch + (U64)sx * pxBytes), pxBytes);
                    g_mem32->memcpyToGuest((U32)(dmem->va + dst->memOff + (U64)dyAbs * dstRowPitch + (U64)dxAbs * pxBytes), &spx, pxBytes);
                }
            }
        }
        return 0;
    }
    case VK64_fn_vkCmdResolveImage:
    case VK64_fn_vkCmdResolveImageKHR: {
        Image* src = objOf32<Image>(args.a[1], K_IMAGE, "vkCmdResolveImage");
        Image* dst = objOf32<Image>(args.a[3], K_IMAGE, "vkCmdResolveImage");
        for (U32 i = 0; i < (U32)args.a[5] && args.a[6]; ++i) {
            vk32abi::VkImageResolve r = {};
            readStruct32((U32)args.a[6] + i * (U32)sizeof(r), r);
            recordImageCopy(objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdResolveImage"), args.a[1], args.a[3],
                r.srcSubresource.mipLevel, r.srcSubresource.baseArrayLayer, r.dstSubresource.mipLevel, r.dstSubresource.baseArrayLayer,
                r.srcOffset.x, r.srcOffset.y, r.srcOffset.z, r.dstOffset.x, r.dstOffset.y, r.dstOffset.z,
                r.extent.width, r.extent.height, r.extent.depth, std::min(r.srcSubresource.layerCount, r.dstSubresource.layerCount));
        }
        VK64_DROP_IF(return 0, !src || !dst || !src->mem || !dst->mem);
        DeviceMemory* smem = objOf32<DeviceMemory>(src->mem, K_MEMORY, "vkCmdResolveImage");
        DeviceMemory* dmem = objOf32<DeviceMemory>(dst->mem, K_MEMORY, "vkCmdResolveImage");
        VK64_DROP_IF(return 0, !smem || !dmem || !smem->va || !dmem->va);
        U32 pxBytes = fmt_bpp(src->format);
        U64 total = (U64)src->w * src->h * pxBytes;
        U64 sva = smem->va + src->memOff, dva = dmem->va + dst->memOff;
        U8 tmp[65536];
        while (total > 0) {
            U32 chunk = total > sizeof(tmp) ? sizeof(tmp) : (U32)total;
            g_mem32->memcpyFromGuest(tmp, (U32)sva, chunk);
            g_mem32->memcpyToGuest((U32)dva, tmp, chunk);
            sva += chunk; dva += chunk; total -= chunk;
        }
        return 0;
    }
    case VK64_fn_vkCmdDrawIndirect: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdDrawIndirect");
        Buffer* buf = objOf32<Buffer>(args.a[1], K_BUFFER, "vkCmdDrawIndirect");
        VK64_DROP_IF(return 0, !cb || !buf || !buf->mem);
        DeviceMemory* dmem = objOf32<DeviceMemory>(buf->mem, K_MEMORY, "vkCmdDrawIndirect");
        VK64_DROP_IF(return 0, !dmem || !dmem->va);
        U32 drawCount = (U32)args.a[3], stride = (U32)args.a[4];
        if (!stride) stride = 16;
        U32 base = (U32)(dmem->va + buf->memOff + args.a[2]);
        for (U32 i = 0; i < drawCount; i++) {
            Cmd c; c.kind = CMD_DRAW;
            c.a = rd32_32(base + i * stride + 0);
            c.b = rd32_32(base + i * stride + 4);
            c.c = rd32_32(base + i * stride + 8);
            c.d = rd32_32(base + i * stride + 12);
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdDrawIndexedIndirect: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdDrawIndexedIndirect");
        Buffer* buf = objOf32<Buffer>(args.a[1], K_BUFFER, "vkCmdDrawIndexedIndirect");
        VK64_DROP_IF(return 0, !cb || !buf || !buf->mem);
        DeviceMemory* dmem = objOf32<DeviceMemory>(buf->mem, K_MEMORY, "vkCmdDrawIndexedIndirect");
        VK64_DROP_IF(return 0, !dmem || !dmem->va);
        U32 drawCount = (U32)args.a[3], stride = (U32)args.a[4];
        if (!stride) stride = 20;
        U32 base = (U32)(dmem->va + buf->memOff + args.a[2]);
        for (U32 i = 0; i < drawCount; i++) {
            Cmd c; c.kind = CMD_DRAW_INDEXED;
            c.a = rd32_32(base + i * stride + 0);
            c.b = rd32_32(base + i * stride + 4);
            c.c = rd32_32(base + i * stride + 8);
            c.diVertexOffset = (int32_t)rd32_32(base + i * stride + 12);
            c.d = rd32_32(base + i * stride + 16);
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdDrawIndirectCount:
    case VK64_fn_vkCmdDrawIndirectCountKHR:
    case VK64_fn_vkCmdDrawIndexedIndirectCount: {
        bool indexed = (fnId == VK64_fn_vkCmdDrawIndexedIndirectCount);
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdDraw*IndirectCount");
        Buffer* buf = objOf32<Buffer>(args.a[1], K_BUFFER, "vkCmdDraw*IndirectCount");
        Buffer* cbuf = objOf32<Buffer>(args.a[3], K_BUFFER, "vkCmdDraw*IndirectCount");
        VK64_DROP_IF(return 0, !cb || !buf || !cbuf || !buf->mem || !cbuf->mem);
        DeviceMemory* dmem = objOf32<DeviceMemory>(buf->mem, K_MEMORY, "vkCmdDraw*IndirectCount");
        DeviceMemory* cmem = objOf32<DeviceMemory>(cbuf->mem, K_MEMORY, "vkCmdDraw*IndirectCount");
        VK64_DROP_IF(return 0, !dmem || !cmem || !dmem->va || !cmem->va);
        U32 drawCount = rd32_32((U32)(cmem->va + cbuf->memOff + args.a[4]));
        U32 maxDraws = (U32)args.a[5], stride = (U32)args.a[6];
        if (drawCount > maxDraws) drawCount = maxDraws;
        if (!stride) stride = indexed ? 20 : 16;
        U32 base = (U32)(dmem->va + buf->memOff + args.a[2]);
        for (U32 i = 0; i < drawCount; i++) {
            Cmd c;
            if (indexed) {
                c.kind = CMD_DRAW_INDEXED;
                c.a = rd32_32(base + i * stride + 0);
                c.b = rd32_32(base + i * stride + 4);
                c.c = rd32_32(base + i * stride + 8);
                c.diVertexOffset = (int32_t)rd32_32(base + i * stride + 12);
                c.d = rd32_32(base + i * stride + 16);
            } else {
                c.kind = CMD_DRAW;
                c.a = rd32_32(base + i * stride + 0);
                c.b = rd32_32(base + i * stride + 4);
                c.c = rd32_32(base + i * stride + 8);
                c.d = rd32_32(base + i * stride + 12);
            }
            pushCmd(cb, c);
        }
        return 0;
    }
    case VK64_fn_vkCmdExecuteCommands: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdExecuteCommands");
        VK64_DROP_IF(return 0, !cb);
        U32 n = (U32)args.a[1];
        if (n && !args.a[2]) { vk64DropLog("n && !pCommandBuffers", __LINE__); return 0; }
        for (U32 i = 0; i < n; i++) {
            CmdBuf* secondary = objOf32<CmdBuf>(rd32_32((U32)args.a[2] + i * 4), K_CMDBUF, "vkCmdExecuteCommands");
            VK64_DROP_IF(continue, !secondary);
            for (const auto& c : secondary->cmds) pushCmd(cb, c);
            if (secondary->nvb > cb->nvb) {
                cb->nvb = secondary->nvb;
                memcpy(cb->vbs, secondary->vbs, sizeof(cb->vbs));
                memcpy(cb->vbOff, secondary->vbOff, sizeof(cb->vbOff));
            }
            if (secondary->ib) {
                cb->ib = secondary->ib; cb->ibOff = secondary->ibOff; cb->ibType = secondary->ibType;
            }
            if (secondary->pipe) cb->pipe = secondary->pipe;
        }
        return 0;
    }
    case VK64_fn_vkCmdDispatch:
    case VK64_fn_vkCmdDispatchBase:
    case VK64_fn_vkCmdDispatchIndirect: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdDispatch");
        VK64_DROP_IF(return 0, !cb);
        Cmd c; c.kind = CMD_DISPATCH;
        if (fnId == VK64_fn_vkCmdDispatch) {
            c.a = args.a[1]; c.b = args.a[2]; c.c = args.a[3];
        } else if (fnId == VK64_fn_vkCmdDispatchBase) {
            c.a = args.a[4]; c.b = args.a[5]; c.c = args.a[6];
        } else {
            Buffer* buf = objOf32<Buffer>(args.a[1], K_BUFFER, "vkCmdDispatchIndirect");
            if (buf && buf->mem) {
                DeviceMemory* dmem = objOf32<DeviceMemory>(buf->mem, K_MEMORY, "vkCmdDispatchIndirect");
                if (dmem && dmem->va) {
                    U32 base = (U32)(dmem->va + buf->memOff + args.a[2]);
                    c.a = rd32_32(base + 0); c.b = rd32_32(base + 4); c.c = rd32_32(base + 8);
                }
            }
        }
        pushCmd(cb, c);
        return 0;
    }
    case VK64_fn_vkCmdUpdateBuffer: {
        /* (cmdBuf, dstBuffer, dstOffset, dataSize, pData). DXVK uploads
         * small uniforms with this -- notably the D3D9 fixed-function VS
         * constant buffer -- so this used to be a hard stop: the call fell
         * into the benign tail and the manifest carried zeros for every
         * uniform updated this way. The payload is copied out of guest
         * memory at record time and written back at submit (transfer
         * semantics), before serialization reads the buffer. */
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdUpdateBuffer");
        VK64_DROP_IF(return 0, !cb);
        U64 size = args.a[3];
        if (!args.a[4] || !size) return 0;
        if (size > 65536) {
            klog_fmt("vk32: vkCmdUpdateBuffer %llu bytes truncated to 65536 (spec max)",
                     (unsigned long long)size);
            size = 65536;
        }
        Cmd c;
        c.kind = CMD_UPDATE_BUF;
        c.updBuf = args.a[1];
        c.updOff = args.a[2];
        c.updBytes.resize((size_t)size);
        g_mem32->memcpyFromGuest(c.updBytes.data(), args.a[4], size);
        pushCmd(cb, c);
        return 0;
    }


    case VK64_fn_vkCmdBeginRendering:
    case VK64_fn_vkCmdBeginRenderingKHR: {
        CmdBuf* cb = objOf32<CmdBuf>(args.a[0], K_CMDBUF, "vkCmdBeginRendering");
        vk32abi::VkRenderingInfo ri = {};
        VK64_DROP_IF(return 0, !cb || !readStruct32(args.a[1], ri));
        U64 rpId = 0, fbId = 0;
        auto* rp = createObj32<RenderPass>(K_RENDERPASS, rpId);
        auto* fb = createObj32<Framebuffer>(K_FRAMEBUFFER, fbId);
        cb->dynamicObjects.push_back(rpId);
        cb->dynamicObjects.push_back(fbId);
        fb->rp = rpId; fb->w = ri.renderArea.extent.width; fb->h = ri.renderArea.extent.height;
        Cmd c; c.kind = CMD_BEGIN_RP; c.a = rpId; c.b = fbId;
        auto attachment = [&](U64 address, bool depth) {
            vk32abi::VkRenderingAttachmentInfo att = {};
            VK64_DROP_IF(return, !address || fb->natt >= VK64_MAX_ATTACH || !readStruct32(address, att));
            auto* view = objOf32<ImageView>(att.imageView, K_IMAGEVIEW, "vkCmdBeginRendering");
            VK64_DROP_IF(return, !view);
            auto* image = objOf32<Image>(view->image, K_IMAGE, "vkCmdBeginRendering");
            U32 i = fb->natt++;
            fb->views[i] = att.imageView;
            rp->formats[i] = view->format ? view->format : (image ? image->format : 0);
            rp->loadOps[i] = att.loadOp; rp->storeOps[i] = att.storeOp;
            rp->samples[i] = image && image->samples ? image->samples : 1;
            if (depth) {
                float value; memcpy(&value, &att.clearValue, 4); c.clearDepth = value;
                rp->depthFmt = rp->formats[i];
            } else if (i == 0) memcpy(c.f, &att.clearValue, 16);
        };
        U32 colors = std::min(ri.colorAttachmentCount, (U32)VK64_MAX_ATTACH);
        for (U32 i = 0; i < colors && ri.pColorAttachments; ++i)
            attachment(ri.pColorAttachments + (U64)i * sizeof(vk32abi::VkRenderingAttachmentInfo), false);
        attachment(ri.pDepthAttachment, true);
        // Packed depth/stencil shares one image view and one replay attachment.
        if (!ri.pDepthAttachment) attachment(ri.pStencilAttachment, true);
        if (ri.pStencilAttachment && fb->natt) {
            vk32abi::VkRenderingAttachmentInfo stencil = {};
            if (readStruct32(ri.pStencilAttachment, stencil)) {
                U32 i = fb->natt - 1;
                rp->stencilLoadOps[i] = stencil.loadOp; rp->stencilStoreOps[i] = stencil.storeOp;
                memcpy(&c.clearStencil, ((const U8*)&stencil.clearValue) + 4, 4);
            }
        }
        rp->natt = fb->natt;
        pushCmd(cb, c);
        return 0;
    }

    // ================= E. Frame boundary =================
    case VK64_fn_vkQueueSubmit:
    case VK64_fn_vkQueueSubmit2:
    case VK64_fn_vkQueueSubmit2KHR: {
        Queue* q = objOf32<Queue>(args.a[0], K_QUEUE, "vkQueueSubmit");
        if (!q) return VK_ERROR_INITIALIZATION_FAILED;
        U32 nsub = (U32)args.a[1];
        std::vector<std::pair<CmdBuf*, U64>> frames;
        const bool submit2 = fnId != VK64_fn_vkQueueSubmit;
        for (U32 s = 0; s < nsub && args.a[2]; s++) {
            U32 cmdCount = 0, pCmd = 0;
            if (submit2) {
                vk32abi::VkSubmitInfo2 si = {};
                readStruct32(args.a[2] + (U64)s * sizeof(si), si);
                cmdCount = si.commandBufferInfoCount; pCmd = si.pCommandBufferInfos;
                for (U32 i = 0; i < si.signalSemaphoreInfoCount; ++i) {
                    vk32abi::VkSemaphoreSubmitInfo signal = {};
                    readStruct32(si.pSignalSemaphoreInfos + (U64)i * sizeof(signal), signal);
                    auto* sem = objOf32<Semaphore>(signal.semaphore, K_SEMAPHORE, "vkQueueSubmit2");
                    if (sem) sem->counter = std::max<U64>(sem->counter, signal.value);
                }
            } else {
                vk32abi::VkSubmitInfo si = {};
                readStruct32(args.a[2] + (U64)s * sizeof(si), si);
                cmdCount = si.commandBufferCount; pCmd = si.pCommandBuffers;
                for (U32 cur = si.pNext, n = 0; cur && n < 64; ++n) {
                    if (rd32_32(cur) == VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO) {
                        vk32abi::VkTimelineSemaphoreSubmitInfo ti = {};
                        readStruct32(cur, ti);
                        for (U32 i = 0; i < std::min(ti.signalSemaphoreValueCount, si.signalSemaphoreCount); ++i) {
                            auto* sem = objOf32<Semaphore>(rd64_32(si.pSignalSemaphores + i * 8), K_SEMAPHORE, "vkQueueSubmit");
                            if (sem) sem->counter = std::max(sem->counter, rd64_32(ti.pSignalSemaphoreValues + i * 8));
                        }
                    }
                    cur = rd32_32(cur + 4);
                }
            }
            for (U32 i = 0; i < cmdCount && pCmd; i++) {
                U32 cbId = 0;
                if (submit2) {
                    vk32abi::VkCommandBufferSubmitInfo ci = {};
                    readStruct32(pCmd + (U64)i * sizeof(ci), ci);
                    cbId = ci.commandBuffer;
                } else cbId = rd32_32(pCmd + i * 4);
                CmdBuf* cb = objOf32<CmdBuf>(cbId, K_CMDBUF, "vkQueueSubmit");
                VK64_DROP_IF(continue, !cb);
                for (const Cmd& k : cb->cmds) {
                    if (k.kind == CMD_COPY_B2I) execCopy(k.copy);
                    else if (k.kind == CMD_UPDATE_BUF) execUpdateBuf(k);
                    else if (k.kind == CMD_RESET_QUERY || k.kind == CMD_WRITE_TIMESTAMP) execQueryCommand(k);
                    else if (k.kind == CMD_COPY_QUERY_RESULTS) execCopyQueryResults(k);
                }
                frames.push_back({cb, cbId});
            }
        }
        bool hasWork = false;
        for (auto& fr : frames) if (fr.first && !fr.first->cmds.empty()) { hasWork = true; break; }
        if (hasWork) {
            if (vkSchemaV2()) {
                bool needRb = setupReadbacks(frames);
                serializeSubmitV2(frames);
                if (needRb) completeReadbacks();
            } else serializeSubmit(frames);
        }
        if (args.a[3]) {
            Fence* f = objOf32<Fence>(args.a[3], K_FENCE, "vkQueueSubmit");
            if (f) f->signalled = 1;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkQueuePresentKHR: {
        Queue* q = objOf32<Queue>(args.a[0], K_QUEUE, "vkQueuePresentKHR");
        if (!q || !args.a[1]) return VK_ERROR_INITIALIZATION_FAILED;
        // VkPresentInfoKHR 32-bit: 32 bytes. swapchainCount at +16,
        // pSwapchains at +20, pImageIndices at +24, pResults at +28.
        U32 pi = (U32)args.a[1];
        U32 scCount = rd32_32(pi + 16);
        U32 pSc = rd32_32(pi + 20);
        U32 pIdx = rd32_32(pi + 24);
        U32 pRes = rd32_32(pi + 28);
        for (U32 i = 0; i < scCount && pSc; i++) {
            U64 scId = rd64_32(pSc + i * 8);
            Swapchain* sc = objOf32<Swapchain>(scId, K_SWAPCHAIN, "vkQueuePresentKHR");
            klog_fmt("vk32: vkQueuePresentKHR swapchain=0x%llx image=%u %ux%u",
                     (unsigned long long)scId,
                     pIdx ? rd32_32(pIdx + i * 4) : 0,
                     sc ? sc->w : 0, sc ? sc->h : 0);
            if (pRes) wr32_32(pRes + i * 4, VK_SUCCESS);
        }
        if (vkSchemaV2()) hopV2ToPage(); else emitFrameNow();
        // Live metrics for the 32-bit guest Vulkan path: present count and the
        // present-to-present wall ONLY. vk32Bridge has no per-trap timers, so
        // submit/trap/hop/guest stay unavailable (never zero) and the panel
        // says why.
        if (vkMetricsEnabled()) {
            double t1 = vkProfNow();
            if (g_vk32HaveFrame) {
                Bw64VkFrame mf = {};
                mf.frameNo = ++g_vk32FrameNo;
                mf.wallMs = t1 - g_vk32LastPresentT;
                mf.breakdown = 0;
                mf.arch = 1;
                bw64_metrics_vk_frame(&mf);
            }
            g_vk32HaveFrame = true;
            g_vk32LastPresentT = t1;
        }
        return VK_SUCCESS;
    }


    case VK64_fn_vkGetPhysicalDeviceQueueFamilyProperties2: {
        U32 countAddr = (U32)args.a[1], arrAddr = (U32)args.a[2];
        const U32 fams = 1;
        if (!countAddr) return VK_ERROR_INITIALIZATION_FAILED;
        if (!arrAddr) { wr32_32(countAddr, fams); return VK_SUCCESS; }
        U32 want = rd32_32(countAddr);
        U32 c = want < fams ? want : fams;
        wr32_32(countAddr, c);
        if (c > 0) {
            U32 base = arrAddr + 8;
            wr32_32(base + 0, VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT);
            wr32_32(base + 4, 1);
            wr32_32(base + 8, 64);
            wr32_32(base + 12, 1); wr32_32(base + 16, 1); wr32_32(base + 20, 1);
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetBufferMemoryRequirements2: {
        if (!args.a[1] || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 infoAddr = (U32)args.a[1], outAddr = (U32)args.a[2];
        U32 bufHandle = (U32)rd64_32(infoAddr + 8);
        Buffer* b = objOf32<Buffer>(bufHandle, K_BUFFER, "vkGetBufferMemoryRequirements2");
        U64 bufSize = (b && b->size) ? b->size : 65536;
        vk32_writeRequirements(outAddr, bufSize);
        return VK_SUCCESS;
    }
    case VK64_fn_vkGetImageMemoryRequirements2: {
        if (!args.a[1] || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 infoAddr = (U32)args.a[1], outAddr = (U32)args.a[2];
        U32 imgHandle = (U32)rd64_32(infoAddr + 8);
        Image* im = objOf32<Image>(imgHandle, K_IMAGE, "vkGetImageMemoryRequirements2");
        { // TEMP-DIAG d3d9-32 font-alloc blowup: atomic pre/post dump
            U32 tid = g_thread32 ? (U32)g_thread32->id : 0;
            klog_fmt("vk32: DBG-REQ2 tid=%u info=%x out=%x sType=%u pNext=%x img=%x found=%u outPre=(%x %x %x %x %x %x)",
                     tid, infoAddr, outAddr, rd32_32(infoAddr), rd32_32(infoAddr + 4),
                     imgHandle, im ? 1 : 0,
                     rd32_32(outAddr + 8), rd32_32(outAddr + 12), rd32_32(outAddr + 16),
                     rd32_32(outAddr + 20), rd32_32(outAddr + 24), rd32_32(outAddr + 28));
            if (im) klog_fmt("vk32: DBG-REQ2 im w=%u h=%u d=%u layers=%u fmt=%u bpp=%u px=%llu",
                     im->w, im->h, im->d, im->layers, im->format, fmt_bpp(im->format),
                     (unsigned long long)(imageDataBytes(*im)));
        }
        if (!im) return VK_ERROR_INITIALIZATION_FAILED;
        U64 px = imageDataBytes(*im);
        vk32_writeRequirements(outAddr, px);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateDescriptorSetLayout: {
        vk32abi::VkDescriptorSetLayoutCreateInfo ci = {};
        if (!readStruct32(args.a[1], ci) || !args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        DescSetLayout* l = createObj32<DescSetLayout>(K_SETLAYOUT, id);
        l->nbind = ci.bindingCount > VK64_MAX_BINDINGS ? VK64_MAX_BINDINGS : ci.bindingCount;
        std::vector<vk32abi::VkDescriptorSetLayoutBinding> binds;
        readArray32(ci.pBindings, l->nbind, binds);
        for (U32 i = 0; i < binds.size(); i++) {
            l->binds[i].binding = binds[i].binding;
            l->binds[i].type = binds[i].descriptorType;
        }
        wr64_32(args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateDescriptorPool: {
        if (!objOf32<Device>((U32)args.a[0], K_DEVICE, "vkCreateDescriptorPool") || !args.a[3])
            return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        createObj32<DescPool>(K_DESCPOOL, id);
        wr64_32((U32)args.a[3], id);
        return VK_SUCCESS;
    }
    case VK64_fn_vkAllocateDescriptorSets: {
        vk32abi::VkDescriptorSetAllocateInfo ai = {};
        if (!readStruct32(args.a[1], ai) || !args.a[2]) return VK_ERROR_INITIALIZATION_FAILED;
        for (U32 i = 0; i < ai.descriptorSetCount; i++) {
            U64 id = 0;
            DescSet* s = createObj32<DescSet>(K_SET, id);
            s->layout = rd64_32(ai.pSetLayouts + (U64)i * 8);
            wr64_32(args.a[2] + (U64)i * 8, id);
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkUpdateDescriptorSets: {
        // (device, nWrite, pWrites*, nCopy, pCopies*)
        U32 nw = (U32)args.a[1];
        std::vector<vk32abi::VkWriteDescriptorSet> writes;
        readArray32(args.a[2], nw, writes);
        for (U32 i = 0; i < nw; i++) {
            const vk32abi::VkWriteDescriptorSet& w = writes[i];
            DescSet* s = objOf32<DescSet>(w.dstSet, K_SET, "vkUpdateDescriptorSets");
            VK64_DROP_IF(continue, !s || w.dstBinding >= VK64_MAX_BINDINGS);
            Binding* b = nullptr;
            for (U32 k = 0; k < s->nbind; k++)
                if (s->binds[k].binding == w.dstBinding) { b = &s->binds[k]; break; }
            if (!b) {
                VK64_DROP_IF(continue, s->nbind >= VK64_MAX_BINDINGS);
                b = &s->binds[s->nbind++];
                b->binding = w.dstBinding;
            }
            // Fidelity: the write's own descriptorType (not the set layout's) and
            // its ARRAY GEOMETRY. A DXVK caller writes descriptor arrays and
            // partial-overlap ranges (dstArrayElement/count), and collapsing those
            // to "element 0 of binding N" makes the recorded set a different set
            // from the one the app declared. The element payloads stay in the flat
            // Binding table for now (schema v2 is P3); index and count are honest
            // today so that table can grow without re-reading the guest.
            b->type = w.descriptorType;
            b->dstArrayElement = w.dstArrayElement;
            b->count = w.descriptorCount ? w.descriptorCount : 1;
            if ((U32)w.descriptorType == (U32)VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                (U32)w.descriptorType == (U32)VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                vk32abi::VkDescriptorBufferInfo bi = {};
                readStruct32(w.pBufferInfo, bi);
                b->obj = bi.buffer;
                b->range = bi.range;
                b->bufOff = bi.offset;
            } else if ((U32)w.descriptorType == (U32)VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
                // The caller's inline data must be consumed before this update returns.
                for (U64 cur = w.pNext; cur && g_mem32;) {
                    if (g_mem32->readd(cur) == VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_INLINE_UNIFORM_BLOCK) {
                        vk32abi::VkWriteDescriptorSetInlineUniformBlock ib = {};
                        g_mem32->memcpyFromGuest(&ib, cur, sizeof(ib));
                        updateInlineUniform(*b, w.dstArrayElement, ib.pData, ib.dataSize);
                        break;
                    }
                    cur = g_mem32->readd((U32)cur + 4);
                }
            } else if ((U32)w.descriptorType == (U32)VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                       (U32)w.descriptorType == (U32)VK_DESCRIPTOR_TYPE_STORAGE_IMAGE ||
                       (U32)w.descriptorType == (U32)VK_DESCRIPTOR_TYPE_SAMPLER) {
                // image-only / sampler-only: keep the one handle each names, so a
                // storage image is not misreported as a sampled image
                vk32abi::VkDescriptorImageInfo ii = {};
                readStruct32(w.pImageInfo, ii);
                b->obj = ii.imageView;
                b->range = ii.sampler;
            } else {
                // combined image sampler (and texel buffers, which share the
                // vk32abi::VkDescriptorBufferInfo shape)
                // FIX (2026-10-09): For COMBINED_IMAGE_SAMPLER, w.pBufferInfo is
                // non-null garbage (same as w.pImageInfo). Trust descriptorType,
                // always read vk32abi::VkDescriptorImageInfo.
                {
                    vk32abi::VkDescriptorImageInfo ii = {};
                    readStruct32(w.pImageInfo, ii);
                    b->obj = ii.imageView;
                    b->range = ii.sampler;
                }
            }
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateDescriptorUpdateTemplate: {
        // (device, pCreateInfo*, pAllocator, out vk32abi::VkDescriptorUpdateTemplate*)
        vk32abi::VkDescriptorUpdateTemplateCreateInfo ci = {};
        if (!readStruct32(args.a[1], ci)) return VK_ERROR_INITIALIZATION_FAILED;
        if (!args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        U64 id = 0;
        DescTemplate* t = createObj32<DescTemplate>(K_TEMPLATE, id);
        std::vector<vk32abi::VkDescriptorUpdateTemplateEntry> entries;
        readArray32(ci.pDescriptorUpdateEntries, ci.descriptorUpdateEntryCount, entries);
        for (auto& e : entries) {
            VK64_DROP_IF(break, t->nentry >= VK64_MAX_BINDINGS);
            TmplEntry& te = t->entries[t->nentry++];
            te.binding = e.dstBinding; te.arrayElement = e.dstArrayElement;
            te.count = e.descriptorCount; te.type = e.descriptorType;
            te.offset = e.offset; te.stride = e.stride;
        }
        wr64_32(args.a[3], id);
        klog_fmt("vk32: vkCreateDescriptorUpdateTemplate -> 0x%llx (%u entries)",
                 (unsigned long long)id, (unsigned)t->nentry);
        return VK_SUCCESS;
    }
    case VK64_fn_vkUpdateDescriptorSetWithTemplate: {
        // (device, descriptorSet, descriptorUpdateTemplate, pData)
        DescSet* s = objOf32<DescSet>(args.a[1], K_SET, "vkUpdateDescriptorSetWithTemplate");
        DescTemplate* t = objOf32<DescTemplate>(args.a[2], K_TEMPLATE, "vkUpdateDescriptorSetWithTemplate");
        if (!s || !t || !args.a[3]) return VK_SUCCESS;
        U64 dataBase = args.a[3];
        for (U32 ei = 0; ei < t->nentry; ei++) {
            const TmplEntry& te = t->entries[ei];
            // Inline descriptorCount is a byte count; stride is ignored.
            const U32 elements = te.type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK ? 1 : te.count;
            for (U32 i = 0; i < elements; i++) {
                U64 dAddr = dataBase + te.offset + (U64)i * te.stride;
                U32 binding = te.binding;
                U32 elem = te.arrayElement + i;
                VK64_DROP_IF(continue, binding >= VK64_MAX_BINDINGS);
                Binding* b = nullptr;
                for (U32 k = 0; k < s->nbind; k++)
                    if (s->binds[k].binding == binding) { b = &s->binds[k]; break; }
                if (!b) {
                    VK64_DROP_IF(continue, s->nbind >= VK64_MAX_BINDINGS);
                    b = &s->binds[s->nbind++];
                    b->binding = binding;
                }
                b->type = te.type;
                b->dstArrayElement = elem;
                b->count = 1;
                if (te.type == VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK) {
                    updateInlineUniform(*b, te.arrayElement, dAddr, te.count);
                } else if (te.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER ||
                    te.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER) {
                    U64 buf = rd64_32(dAddr); U64 off = rd64_32(dAddr + 8); U64 range = rd64_32(dAddr + 16);
                    b->obj = buf; b->range = range; b->bufOff = off;
                } else if (te.type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ||
                           te.type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE) {
                    U64 view = rd64_32(dAddr + 8);
                    b->obj = view; b->range = 0;
                } else if (te.type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER) {
                    // vk32abi::VkDescriptorImageInfo: sampler @0, imageView @8.
                    // (2026-10-09: D3D9 textured draw was black — template path
                    // dropped the image view, so no IMAGE_DATA was emitted.)
                    U64 samp = rd64_32(dAddr);
                    U64 view = rd64_32(dAddr + 8);
                    b->obj = view; b->range = samp;
                } else if (te.type == VK_DESCRIPTOR_TYPE_SAMPLER) {
                    U64 samp = rd64_32(dAddr);
                    b->obj = samp; b->range = 0;
                } else if (te.type == VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER ||
                           te.type == VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER) {
                    U64 bv = rd64_32(dAddr);
                    b->obj = bv; b->range = 0;
                }
            }
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkBindBufferMemory2:
    case VK64_fn_vkBindBufferMemory2KHR: {
        std::vector<vk32abi::VkBindBufferMemoryInfo> infos;
        readArray32((U32)args.a[2], (U32)args.a[1], infos);
        for (const auto& info : infos) {
            auto* o = objOf32<Buffer>(info.buffer, K_BUFFER, "vkBindBufferMemory2");
            auto* m = objOf32<DeviceMemory>(info.memory, K_MEMORY, "vkBindBufferMemory2");
            if (!o || !m || info.memoryOffset >= m->size) return VK_ERROR_INITIALIZATION_FAILED;
            o->mem = info.memory; o->memOff = info.memoryOffset;
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkBindImageMemory2:
    case VK64_fn_vkBindImageMemory2KHR: {
        std::vector<vk32abi::VkBindImageMemoryInfo> infos;
        readArray32((U32)args.a[2], (U32)args.a[1], infos);
        for (const auto& info : infos) {
            auto* o = objOf32<Image>(info.image, K_IMAGE, "vkBindImageMemory2");
            auto* m = objOf32<DeviceMemory>(info.memory, K_MEMORY, "vkBindImageMemory2");
            if (!o || !m || info.memoryOffset >= m->size) return VK_ERROR_INITIALIZATION_FAILED;
            o->mem = info.memory; o->memOff = info.memoryOffset;
            o->pixBytes = imageDataBytes(*o);
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateComputePipelines: {
        if (!args.a[4]) return VK_ERROR_INITIALIZATION_FAILED;
        U32 count = (U32)args.a[2];
        U32 outAddr = (U32)args.a[4];
        for (U32 i = 0; i < count; i++) {
            U64 id = 0;
            createObj32<Pipeline>(K_PIPELINE, id);
            wr64_32(outAddr + i * 8, id);
        }
        return VK_SUCCESS;
    }
    case VK64_fn_vkCreateBufferView: {
        if (!args.a[3]) return VK_ERROR_INITIALIZATION_FAILED;
        vk64Unsupported("texel-buffer-view", "formatted texel-buffer descriptors are not implemented; format properties omit texel-buffer features");
        wr64_32((U32)args.a[3], 0);
        return VK_ERROR_FORMAT_NOT_SUPPORTED;
    }
    case VK64_fn_vkGetBufferDeviceAddress: {
        VK64_DROP_IF(return 0, !args.a[1]);
        U32 infoAddr = (U32)args.a[1];
        U32 bufHandle = (U32)rd64_32(infoAddr + 8);
        Buffer* b = objOf32<Buffer>(bufHandle, K_BUFFER, "vkGetBufferDeviceAddress");
        VK64_DROP_IF(return 0, !b);
        if (!b->devVa) {
            b->devVa = g_nextDevVa;
            g_nextDevVa += 0x10000000ULL;
        }
        return b->devVa;
    }

    default:
        klog_fmt("vk32: unimplemented fn id %u (%s)", fnId, fnName(fnId));
        vk64Unsupported((std::string("fn32:") + fnName(fnId)).c_str(), "no handler; returned VK_ERROR_FEATURE_NOT_PRESENT");
        return (U64)(int64_t)VK_ERROR_FEATURE_NOT_PRESENT;
    }
}


#endif // BOXEDWINE_VULKAN64
