#!/usr/bin/env python3
"""generate vk32_guest.h from the in-tree Khronos vulkan_core.h.

32-bit (i386) GUEST-layout Vulkan types for the 32-bit Vulkan bridge
(source/vulkan/vk64bridge.cpp's vk32Bridge).

This is the 32-bit analogue of gen_vk64_guest.py. Key differences from the
64-bit version:
  - Pointers are 4 bytes (uint32_t), not 8.
  - Dispatchable handles (VK_DEFINE_HANDLE -> struct X_T*) are 4 bytes
    (uint32_t). On i386, a Vulkan dispatchable handle is a 32-bit pointer.
  - Non-dispatchable handles (VK_DEFINE_NON_DISPATCHABLE_HANDLE) stay 8 bytes
    (uint64_t) per the Vulkan spec, even on 32-bit.
  - i386 alignment: 8-byte types (uint64_t, double) are 4-byte aligned, NOT
    8-byte aligned. The generated structs are wrapped in
    #pragma pack(push, 4) / #pragma pack(pop) to reproduce this exactly.
    Without it, a wasm32/clang host would 8-align uint64_t members and every
    field after the first uint64_t would be read from the wrong offset.
  - size_t is 4 bytes on i386 (uint32_t), not 8.

Usage:  python3 tools/vulkan/gen/gen_vk32_guest.py source/vulkan/vk/vulkan_core.h \
            > source/vulkan/vk32_guest.h
"""
import re
import sys

# Same struct list as gen_vk64_guest.py — the 32-bit bridge touches the same
# entry points, so it needs the same structs.
STRUCTS = """
VkExtent2D VkExtent3D VkOffset2D VkOffset3D VkRect2D VkViewport VkClearColorValue
VkClearDepthStencilValue VkClearValue VkClearAttachment VkClearRect
VkApplicationInfo VkInstanceCreateInfo VkDeviceQueueCreateInfo VkDeviceCreateInfo
VkExtensionProperties VkPhysicalDeviceLimits VkPhysicalDeviceSparseProperties
VkPhysicalDeviceProperties VkPhysicalDeviceProperties2 VkPhysicalDeviceFeatures
VkPhysicalDeviceFeatures2 VkPhysicalDeviceMemoryProperties
VkPhysicalDeviceMemoryProperties2 VkQueueFamilyProperties VkQueueFamilyProperties2
VkFormatProperties VkMemoryRequirements VkSubresourceLayout VkSurfaceFormatKHR
VkSurfaceCapabilitiesKHR VkMemoryAllocateInfo VkMappedMemoryRange VkMappedMemoryRange
VkMemoryRequirements2 VkMemoryAllocateFlagsInfo VkMemoryDedicatedAllocateInfo
VkBufferCreateInfo VkBufferViewCreateInfo VkImageCreateInfo VkImageSubresource
VkImageSubresourceRange VkImageSubresourceLayers VkComponentMapping VkImageViewCreateInfo
VkSamplerCreateInfo VkRenderPassCreateInfo VkAttachmentDescription VkAttachmentReference VkSubpassDependency
VkSubpassDescription VkRenderPassCreateInfo2 VkFramebufferCreateInfo VkRenderPassBeginInfo
VkPipelineVertexInputStateCreateInfo VkPipelineInputAssemblyStateCreateInfo
VkPipelineTessellationStateCreateInfo VkPipelineViewportStateCreateInfo
VkPipelineRasterizationStateCreateInfo VkPipelineMultisampleStateCreateInfo
VkPipelineDepthStencilStateCreateInfo VkPipelineColorBlendAttachmentState
VkPipelineColorBlendStateCreateInfo VkPipelineDynamicStateCreateInfo
VkPipelineTessellationStateCreateInfo2 VkGraphicsPipelineCreateInfo VkComputePipelineCreateInfo
VkPipelineShaderStageCreateInfo VkPipelineLayoutCreateInfo
VkPipelineCacheCreateInfo VkPipelineCacheDataInfo VkPipelineRasterizationLineStateCreateInfoEXT
VkDescriptorSetLayoutBinding VkDescriptorSetLayoutCreateInfo VkDescriptorPoolSize
VkDescriptorPoolCreateInfo VkDescriptorSetAllocateInfo VkDescriptorBufferInfo
VkDescriptorImageInfo VkWriteDescriptorSet VkCopyDescriptorSet
VkShaderModuleCreateInfo VkPipelineShaderStageCreateInfo VkFenceCreateInfo
VkSemaphoreCreateInfo VkEventCreateInfo VkCommandPoolCreateInfo
VkSemaphoreTypeCreateInfo VkSemaphoreWaitInfo VkSemaphoreSignalInfo VkTimelineSemaphoreSubmitInfo
VkCommandBufferAllocateInfo VkCommandBufferInheritanceInfo VkCommandBufferBeginInfo
VkCommandBufferResetFlags VkBufferImageCopy VkBufferCopy VkImageCopy
VkCopyBufferToImageInfo2 VkBufferImageCopy2
VkImageBlit VkImageResolve VkImageSubresourceRange VkSubmitInfo VkSubmitInfo2
VkCommandBufferSubmitInfo VkSemaphoreSubmitInfo VkPresentInfoKHR
VkSwapchainCreateInfo VkSwapchainCreateInfoKHR VkSwapchainKHR VkSurfaceKHR
VkPhysicalDeviceImageFormatInfo2 VkImageFormatProperties2 VkSurfaceFormat2KHR
VkPhysicalDeviceSurfaceInfo2KHR VkSurfaceCapabilities2KHR VkImageSubresourceRange2
VkImageFormatProperties VkImageFormatProperties2 VkPipelineStatisticsCreateInfo VkMemoryType VkMemoryHeap VkStencilOpState
VkVertexInputBindingDescription VkVertexInputAttributeDescription
VkPipelineViewportStateCreateInfo2 VkPhysicalDeviceVulkan11Features VkPhysicalDeviceVulkan12Features
VkPhysicalDeviceVulkan13Features VkPhysicalDeviceVulkan14Features VkPhysicalDeviceFeatures2
VkPhysicalDeviceRobustness2FeaturesEXT VkPhysicalDeviceMaintenance5Features
VkPhysicalDeviceMaintenance6Features VkPhysicalDeviceTransformFeedbackFeaturesEXT
VkPhysicalDeviceDescriptorIndexingFeatures VkPhysicalDeviceDescriptorIndexingProperties
VkPhysicalDeviceShaderFloat16Int8Features VkPhysicalDevice16BitStorageFeatures
VkPhysicalDevice8BitStorageFeatures VkPhysicalDeviceScalarBlockLayoutFeatures
VkPhysicalDeviceDepthClipEnableFeaturesEXT VkPhysicalDeviceMaintenance5Properties
VkPhysicalDeviceVulkan11Properties VkPhysicalDeviceVulkan12Properties
VkPhysicalDeviceTransformFeedbackPropertiesEXT
VkPhysicalDeviceSubgroupProperties VkPhysicalDeviceIDProperties VkConformanceVersion
VkSemaphoreTypeCreateInfo VkTimelineSemaphoreSubmitInfo VkSemaphoreWaitInfo
VkSemaphoreSignalInfo
VkWriteDescriptorSetInlineUniformBlock VkDescriptorUpdateTemplateCreateInfo
VkDescriptorUpdateTemplateEntry VkRenderingInfo VkRenderingAttachmentInfo
VkBindBufferMemoryInfo VkBindImageMemoryInfo VkMemoryDedicatedRequirements
VkSpecializationInfo VkSpecializationMapEntry
""".split()

# Filled in main(): dispatchable handle names (struct X_T*) and non-dispatchable
# handle names (VK_DEFINE_NON_DISPATCHABLE_HANDLE).
_dispatchable = set()
_nondispatchable = set()


def narrow(decl: str) -> str:
    """Replace every pointer member with uint32_t (the i386 guest width).

    Handles every pointer spelling the Khronos headers use: `T* name`,
    `const T* name`, `const T* const* name`, `T* name[N]`. The trailing `const`
    on the pointee has to go too, otherwise the member becomes const and the
    whole struct stops being assignable.
    """
    if '*' not in decl:
        return decl
    m = re.match(r'\s*(.*?)\*\s*([A-Za-z_][A-Za-z0-9_]*)(\s*\[\s*\d+\s*\])?\s*;?\s*$', decl)
    if not m:
        return decl
    name = m.group(2)
    arr = m.group(3) or ''
    return 'uint32_t %s%s;' % (name, arr)


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else 'source/vulkan/vk/vulkan_core.h'
    src = open(path).read()
    src = re.sub(r'\\\n[ \t]*', ' ', src)
    beta = src.find('#ifdef VK_ENABLE_BETA_EXTENSIONS')
    if beta > 0:
        src = re.sub(r'#ifdef VK_ENABLE_BETA_EXTENSIONS.*?#endif', '', src, flags=re.S)

    out = []
    out.append('/* GENERATED by tools/vulkan/gen/gen_vk32_guest.py from source/vulkan/vk/vulkan_core.h.')
    out.append(' * Do not edit by hand; re-run the generator instead.')
    out.append(' *')
    out.append(' * i386 (32-bit) GUEST-layout Vulkan types for the 32-bit Vulkan bridge.')
    out.append(' *')
    out.append(' * Every pointer and every DISPATCHABLE handle member is uint32_t, which is')
    out.append(' * what the 32-bit guest ABI uses. NON-DISPATCHABLE handles stay uint64_t per')
    out.append(' * the Vulkan spec (they are 64-bit even on 32-bit). This header therefore')
    out.append(' * does NOT include vulkan_core.h.')
    out.append(' *')
    out.append(' * Linux i386 driver ABI: Wine converts Windows structs before calling this')
    out.append(' * boundary. Structs are wrapped in #pragma pack(push, 4) to reproduce the')
    out.append(' * i386 layout exactly (without it, uint64_t members would be 8-aligned and')
    out.append(' * every field after the first uint64_t would be read from the wrong offset).')
    out.append(' */')
    out.append('')
    out.append('#ifndef __VK32_GUEST_H__')
    out.append('#define __VK32_GUEST_H__')
    out.append('')
    out.append('#include <stdint.h>')
    out.append('#include <stddef.h>')
    out.append('')
    out.append('// --- plain typedefs (scalars + VkXxxFlags bitmask aliases) ---')
    scalars = {'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t',
               'int8_t', 'int16_t', 'int32_t', 'int64_t',
               'float', 'double', 'VkFlags', 'VkFlags64', 'VkDeviceSize',
               'VkDeviceAddress', 'VkBool32'}
    seen_typedef = set()
    for m in re.finditer(r'^typedef\s+([A-Za-z_][A-Za-z0-9_]*)\s+([A-Za-z_][A-Za-z0-9_]*)\s*;', src, re.M):
        a, b = m.group(1), m.group(2)
        if a not in scalars or 'PFN' in b or (a, b) in seen_typedef:
            continue
        seen_typedef.add((a, b))
        out.append('typedef %s %s;' % (a, b))
    out.append('')

    # handles: dispatchable -> uint32_t, non-dispatchable -> uint64_t
    disp = set(re.findall(r'^VK_DEFINE_HANDLE\(([A-Za-z0-9_]+)\)', src, re.M))
    nondisp = set(re.findall(r'^VK_DEFINE_NON_DISPATCHABLE_HANDLE\(([A-Za-z0-9_]+)\)', src, re.M))
    disp = {h[:-2] if h.endswith('_T') else h for h in disp}
    for h in sorted(disp):
        out.append('typedef uint32_t %s;  // dispatchable: 32-bit on i386' % h)
    for h in sorted(nondisp):
        out.append('typedef uint64_t %s;  // non-dispatchable: 64-bit even on i386' % h)
    out.append('')

    # every #define constant
    seen = set()
    for m in re.finditer(r'^#define\s+(VK_[A-Za-z0-9_]+)(\([^)]*\))?[ \t]+(.+?)[ \t]*$', src, re.M):
        name, params, val = m.group(1), m.group(2) or '', m.group(3)
        if name in seen:
            continue
        seen.add(name)
        out.append('#define %s%s %s' % (name, params, val))
    out.append('')

    # enums
    for m in re.finditer(r'typedef enum ([A-Za-z0-9_]+) \{(.*?)\n\} \1;', src, re.S):
        body = m.group(2)
        if 'PFN_' in body:
            continue
        out.append('typedef enum %s {%s\n} %s;' % (m.group(1), body.rstrip(), m.group(1)))
    out.append('')

    # structs / unions, narrowed to i386 layout, under pack(4)
    out.append('#pragma pack(push, 4)  // i386: max natural alignment is 4')
    out.append('')
    for m in re.finditer(r'typedef (struct|union) ([A-Za-z0-9_]+) \{(.*?)\n\} \2;', src, re.S):
        kind, name, body = m.group(1), m.group(2), m.group(3)
        if name not in STRUCTS:
            continue
        lines = []
        for line in body.split('\n'):
            stripped = line.strip()
            if not stripped or stripped.startswith('//') or stripped.startswith('/*') or stripped.startswith('*'):
                continue
            if 'PFN_' in stripped or 'typedef' in stripped:
                continue
            if '[' in stripped and '*' not in stripped and re.search(r'\w\s+\w+\[\d+\]', stripped):
                lines.append(stripped)
                continue
            lines.append(narrow(stripped).rstrip(';') + ';')
        if not lines:
            continue
        out.append('typedef %s %s {' % (kind, name))
        for l in lines:
            out.append('    ' + l)
        out.append('} %s;' % name)
        out.append('')
    out.append('#pragma pack(pop)')
    out.append('')

    out.append('// --- layout guards: any drift in the source header breaks the boundary ---')
    out.append('// Sizes below are the i386 ABI values (4-byte max alignment).')
    # Spot-checks computed from the i386 ABI rules. These are the structs the
    # 32-bit bridge reads most often; a wrong size here means silent corruption.
    guards = [
        # VkExtent2D: 2xu32 = 8
        ('VkExtent2D', 'sizeof', 8),
        # VkExtent3D: 3xu32 = 12
        ('VkExtent3D', 'sizeof', 12),
        # VkOffset2D: 2xi32 = 8
        ('VkOffset2D', 'sizeof', 8),
        # VkViewport: 6xfloat = 24
        ('VkViewport', 'sizeof', 24),
        # VkRect2D: VkOffset2D(8) + VkExtent2D(8) = 16
        ('VkRect2D', 'sizeof', 16),
        # VkClearValue: union of VkClearColorValue(16), VkClearDepthStencilValue(8)
        # -> 16
        ('VkClearValue', 'sizeof', 16),
        # VkClearColorValue: 4xfloat = 16
        ('VkClearColorValue', 'sizeof', 16),
        # VkFormatProperties: 3xu32 = 12
        ('VkFormatProperties', 'sizeof', 12),
        # VkMemoryRequirements: u32,u32,u64(4-aligned) = 4+4+8 = 16? No:
        #   struct { VkDeviceSize size(8); VkDeviceSize alignment(8);
        #            uint32_t memoryTypeBits; } -> 8+8+4 = 20
        ('VkMemoryRequirements', 'sizeof', 20),
        # VkExtensionProperties: char[256] + u32 = 260
        ('VkExtensionProperties', 'sizeof', 260),
        # VkApplicationInfo: u32 sType(4) + u32 pNext(4) + u32 pAppName(4) +
        #   u32 appVer(4) + u32 pEngineName(4) + u32 engineVer(4) +
        #   u32 apiVer(4) = 28
        ('VkApplicationInfo', 'sizeof', 28),
        # VkInstanceCreateInfo: u32 sType + u32 pNext + u32 flags +
        #   u32 pAppInfo + u32 layerCount + u32 ppLayerNames +
        #   u32 extCount + u32 ppExtNames = 8xu32 = 32
        ('VkInstanceCreateInfo', 'sizeof', 32),
        # VkDeviceQueueCreateInfo: u32 sType + u32 pNext + u32 flags +
        #   u32 familyIndex + u32 count + u32 pPriorities = 6xu32 = 24
        ('VkDeviceQueueCreateInfo', 'sizeof', 24),
        # VkBufferCreateInfo: u32 sType(4) + u32 pNext(4) + u32 flags(4) +
        #   u64 size(8, 4-aligned at 12) + u32 usage(4) + u32 sharingMode(4) +
        #   u32 queueFamilyIndexCount(4) + u32 pQueueFamilyIndices(4)
        #   = 4+4+4+8+4+4+4+4 = 36
        ('VkBufferCreateInfo', 'sizeof', 36),
        # VkMemoryAllocateInfo: u32 sType + u32 pNext + u64 allocSize(4-al at 8) +
        #   u32 memTypeIndex = 4+4+8+4 = 20
        ('VkMemoryAllocateInfo', 'sizeof', 20),
        # VkShaderModuleCreateInfo: u32 sType + u32 pNext + u32 flags +
        #   u64 codeSize(4-al at 12) + u32 pCode = 4+4+4+8+4 = 24
        ('VkShaderModuleCreateInfo', 'sizeof', 20),
        # VkCommandBufferAllocateInfo: u32 sType + u32 pNext + u32 pool(handle) +
        #   u32 level + u32 count = 5xu32 = 20
        ('VkCommandBufferAllocateInfo', 'sizeof', 24),
        # VkDescriptorBufferInfo: u64 buffer(ndh) + u64 offset + u64 range
        #   = 8+8+8 = 24 (4-aligned, no padding needed)
        ('VkDescriptorBufferInfo', 'sizeof', 24),
        # VkDescriptorImageInfo: u64 sampler + u64 imageView + u32 layout
        #   = 8+8+4 = 20
        ('VkDescriptorImageInfo', 'sizeof', 20),
        # VkImageSubresourceLayers: u32 aspect + u32 mip + u32 baseLayer +
        #   u32 layerCount = 16
        ('VkImageSubresourceLayers', 'sizeof', 16),
        # VkBufferCopy: u64 srcOff + u64 dstOff + u64 size = 24
        ('VkBufferCopy', 'sizeof', 24),
        # VkImageCopy: VkImageSubresourceLayers(16) + VkOffset3D(12) +
        #   VkImageSubresourceLayers(16) + VkOffset3D(12) + VkExtent3D(12)
        #   = 16+12+16+12+12 = 68
        ('VkImageCopy', 'sizeof', 68),
        # VkSubmitInfo: u32 sType + u32 pNext + u32 waitSemCount +
        #   u32 pWaitSems + u32 pWaitDstMask + u32 cmdBufCount +
        #   u32 pCmdBufs + u32 sigSemCount + u32 pSigSems
        #   = 9xu32 = 36
        ('VkSubmitInfo', 'sizeof', 36),
        # VkSurfaceCapabilitiesKHR: u32 minImg + u32 maxImg + VkExtent2D cur(8) +
        #   VkExtent2D min(8) + VkExtent2D max(8) + u32 maxArrLayers +
        #   u32 transforms + u32 curTransform + u32 compositeAlpha +
        #   u32 imgUsage = 4+4+8+8+8+4+4+4+4+4 = 52
        ('VkSurfaceCapabilitiesKHR', 'sizeof', 52),
        # VkPipelineShaderStageCreateInfo: u32 sType + u32 pNext + u32 flags +
        #   u32 stage + u64 module(4-aligned at 16) + u32 pName +
        #   u32 pSpecializationInfo = 32; module offset is load-bearing
        #   (every pipeline creation reads the shader handle through it).
        ('VkPipelineShaderStageCreateInfo', 'sizeof', 32),
        ('VkPipelineShaderStageCreateInfo', 'offsetof:module', 16),
        # VkComputePipelineCreateInfo: u32 sType + u32 pNext + u32 flags +
        #   VkPipelineShaderStageCreateInfo stage(32, at 12) +
        #   u64 layout(4-aligned at 44) + u64 basePipelineHandle(52) +
        #   i32 basePipelineIndex(60) = 64. Added by lane-vk32compute; the
        #   layout was pinned by lane-vk32fix after an offsetof probe
        #   compiled against this header (host g++ and i686-w64-mingw32-g++
        #   agree: 64 / stage 12 / layout 44 / handle 52 / index 60).
        ('VkComputePipelineCreateInfo', 'sizeof', 64),
        ('VkComputePipelineCreateInfo', 'offsetof:stage', 12),
        ('VkComputePipelineCreateInfo', 'offsetof:layout', 44),
        ('VkComputePipelineCreateInfo', 'offsetof:basePipelineHandle', 52),
        ('VkComputePipelineCreateInfo', 'offsetof:basePipelineIndex', 60),
    ]
    for t, kind, v in guards:
        if kind == 'sizeof':
            out.append('static_assert(sizeof(%s) == %d, "%s layout drifted from i386 guest ABI");' % (t, v, t))
        else:  # kind == 'offsetof:<field>'
            field = kind.split(':', 1)[1]
            out.append('static_assert(offsetof(%s, %s) == %d, "%s.%s layout drifted from i386 guest ABI");' % (t, field, v, t, field))
    out.append('')
    out.append('#endif // __VK32_GUEST_H__')
    text = '\n'.join(out)
    # size_t is 4 bytes on i386 (the ONE field type whose width follows the ABI,
    # VkPhysicalDeviceLimits::minMemoryMapAlignment).
    text = re.sub(r'\bsize_t\b', 'uint32_t', text)
    print(text)
    return 0


if __name__ == '__main__':
    sys.exit(main())
