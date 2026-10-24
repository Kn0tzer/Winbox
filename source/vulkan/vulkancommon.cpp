/*
 *  Copyright (C) 2012-2025  The BoxedWine Team
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
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include "boxedwine.h"
#ifdef BOXEDWINE_VULKAN
#include "knativesystem.h"
#include "../x11/x11.h"
#include "vk_host.h"
#include "vkdef.h"
#include "kvulkan.h"
#include <SDL_vulkan.h>
#include <set>

static PFN_vkGetInstanceProcAddr pvkGetInstanceProcAddr = nullptr;

static U32 vulkanPtrCount;
static U32 vulkanPtrHighMark;

#ifdef _DEBUG
static VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData) {

    std::cerr << "validation layer: " << pCallbackData->pMessage << std::endl;

    return VK_FALSE;
}
#endif

U32 createVulkanPtr(KMemory* memory, void* value, BoxedVulkanInfo* info) {
    KProcessPtr process = KThread::currentThread()->process;
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(process->freeVulkanPtrMutex);
    U32 result = 0;

    if (value == nullptr) {
        return 0;
    }
    if (process->vulkanPtrMap.get(value, result)) {
        return result;
    }
    if (!process->vulkanFreePtrAddress) {
        KThread* thread = KThread::currentThread();
        U32 address = thread->memory->mmap(thread, 0, K_PAGE_SIZE, K_PROT_READ | K_PROT_WRITE, K_MAP_ANONYMOUS | K_MAP_PRIVATE | K_MAP_BOXEDWINE, -1, 0);
        for (U32 i = 0; i < K_PAGE_SIZE; i += 16) {
            memory->writed(address + i, process->vulkanFreePtrAddress);
            process->vulkanFreePtrAddress = address + i;
        }
    }
    result = process->vulkanFreePtrAddress;
    process->vulkanFreePtrAddress = memory->readd(process->vulkanFreePtrAddress);
    memory->writeq(result, (U64)value);

    if (!info) {
        info = new BoxedVulkanInfo();
        if (!pvkGetInstanceProcAddr) {
            pvkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
        }
#undef VKFUNC
#undef VKFUNC_INSTANCE
#define VKFUNC_INSTANCE(f) info->pvk##f = (PFN_vk##f)pvkGetInstanceProcAddr((VkInstance)value, "vk"#f); if (!info->pvk##f) {kwarn("Boxedwine: Failed to load vk"#f);} else {info->functionAddressByName[B("vk"#f)]=1;}
#define VKFUNC(f)
#include "vkfuncs.h" 
        info->instance = (VkInstance)value;

#ifdef _DEBUG1
        KNativeSystem::getScreen()->showWindow(true);
        PFN_vkCreateDebugUtilsMessengerEXT debugFunc = (PFN_vkCreateDebugUtilsMessengerEXT)pvkGetInstanceProcAddr((VkInstance)value, "vkCreateDebugUtilsMessengerEXT");
        VkDebugUtilsMessengerCreateInfoEXT createInfo;
        memset(&createInfo, 0, sizeof(VkDebugUtilsMessengerCreateInfoEXT));
        createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
        createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_VERBOSE_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
        createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
        createInfo.pfnUserCallback = debugCallback;
        createInfo.pUserData = nullptr;

        if (debugFunc) {
            debugFunc(info->instance, &createInfo, nullptr, &info->debugMessenger);
        } else {
            klog("Vulkan debug function not found");
        }
#endif
    }
    memory->writeq(result + 8, (U64)info);
    vulkanPtrCount++;
    vulkanPtrHighMark = std::max(vulkanPtrHighMark, vulkanPtrCount);
    process->vulkanPtrMap.set(value, result);
    return result;
}

BoxedVulkanInfo* getInfoFromHandle(KMemory* memory, U32 address) {
    return (BoxedVulkanInfo*)memory->readq(address+8);
}

static void hasProcAddress(CPU* cpu) {
    U32 handle = cpu->peek32(1);
    if (!handle) {
        EAX = 1;
    } else {
        BoxedVulkanInfo* pBoxedInfo = getInfoFromHandle(cpu->memory, handle);
        BString name = cpu->memory->readString(cpu->peek32(2));

        if (name == "vkMapMemory2KHR" || name == "vkUnmapMemory2KHR") {
            EAX = 0;
        } else if (pBoxedInfo->functionAddressByName.count(name) || name == "vkGetDeviceProcAddr" || name == "vkCreateXlibSurfaceKHR") {
            EAX = 1;
        }
        else {
            EAX = 0;
        }
    }
}

void freeVulkanPtr(KMemory* memory, U32 p) {
    KProcessPtr process = KThread::currentThread()->process;
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(process->freeVulkanPtrMutex);
    void* address = getVulkanPtr(memory, p);
    process->vulkanPtrMap.remove(address);
    memory->writed(p, process->vulkanFreePtrAddress);
    process->vulkanFreePtrAddress = p;
    vulkanPtrCount--;
}

void* getVulkanPtr(KMemory* memory, U32 address) {
    return (void*)(memory->readq(address));
}

class VMemory {
public:
    VkDeviceMemory memory;
    VkDeviceSize size;
    VkDeviceSize mappedLen;
    U32 mappedAddress;
};

std::unordered_map<VkDeviceMemory, std::shared_ptr<VMemory>> vmemory;
BOXEDWINE_MUTEX vmemoryMutex;

std::shared_ptr<VMemory> getVMemory(VkDeviceMemory memory) {
    if (vmemory.count(memory))
        return vmemory[memory];
    return NULL;
}

void registerVkMemoryAllocation(VkDeviceMemory memory, VkDeviceSize size) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(vmemoryMutex);
    std::shared_ptr<VMemory> m = std::make_shared<VMemory>();
    m->memory = memory;
    m->size = size;
    m->mappedLen = 0;
    m->mappedAddress = 0;
    vmemory[memory] = m;
}

void unregisterVkMemoryAllocation(VkDeviceMemory memory) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(vmemoryMutex);
    std::shared_ptr<VMemory> m = getVMemory(memory);
    if (m) {
        vmemory.erase(memory);
    }
}

// Returns the guest address of the mapping, or 0 when the request cannot be
// honoured (the caller then reports VK_ERROR_MEMORY_MAP_FAILED).
U32 mapVkMemory(VkDeviceMemory memory, void* pData, VkDeviceSize offset, VkDeviceSize len) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(vmemoryMutex);
    std::shared_ptr<VMemory> m = getVMemory(memory);
    const char* why = nullptr;
    if (!m) {
        why = "memory was not registered by vkAllocateMemory";
    } else if (m->mappedAddress) {
        why = "memory is already mapped (Vulkan forbids a double map)";
    } else if (offset > m->size) {
        why = "offset exceeds allocation size";
    } else {
        if (len == VK_WHOLE_SIZE) {
            len = m->size - offset;
        }
        if (len > m->size - offset) {
            why = "range exceeds allocation size";
        }
    }
    if (why) {
        static bool shown;
        if (!shown) { klog_fmt("vk: UNSUPPORTED mapVkMemory: %s, returning VK_ERROR_MEMORY_MAP_FAILED", why); shown = true; }
        return 0;
    }
    m->mappedLen = len;
    m->mappedAddress = KThread::currentThread()->memory->mapNativeMemory(pData, (U32)len);
    return m->mappedAddress;
}

void unmapVkMemory(VkDeviceMemory memory) {
    BOXEDWINE_CRITICAL_SECTION_WITH_MUTEX(vmemoryMutex);
    std::shared_ptr<VMemory> m = getVMemory(memory);
    if (!m) {
        static bool shown;
        if (!shown) { klog("vk: UNSUPPORTED unmapVkMemory: memory was not registered by vkAllocateMemory (ignored)"); shown = true; }
        return;
    }
    if (!m->mappedAddress) {
        klog("unmapVkMemory called, but no record of being mapped");
        return;
    }
    KThread::currentThread()->memory->unmapNativeMemory(m->mappedAddress, (U32)m->mappedLen);
    m->mappedAddress = 0;
    m->mappedLen = 0;
}

#define ARG1 cpu->peek32(1)
#define ARG2 cpu->peek32(2)
#define ARG3 cpu->peek32(3)
#define ARG4 cpu->peek32(4)

/*
typedef struct VkXlibSurfaceCreateInfoKHR {
    VkStructureType                sType;
    const void* pNext;
    VkFlags    flags;
    Display* dpy;
    Window                         window;
} VkXlibSurfaceCreateInfoKHR;
*/

// VkResult vkCreateXlibSurfaceKHR( VkInstance instance, const VkXlibSurfaceCreateInfoKHR* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface  ) const
static void BOXED_vkCreateXlibSurfaceKHR(CPU* cpu) {
    //VkInstance instance,
    //const VkWin32SurfaceCreateInfoKHR* create_info,
    //const VkAllocationCallbacks* allocator, 
    // VkSurfaceKHR* surface

    KVulkanPtr vulkanWnd = KNativeSystem::getVulkan();

    void* instance = getVulkanPtr(cpu->memory, cpu->peek32(1));
    // window is the 5th 32-bit variable in VkXlibSurfaceCreateInfoKHR
    U32 windowId = cpu->memory->readd(ARG2 + 4 * sizeof(U32));
    XWindowPtr xWindow = XServer::getServer()->getWindow(windowId);
    void* surface = vulkanWnd->createVulkanSurface(xWindow, instance);
    if (!surface) {
        EAX = VK_ERROR_OUT_OF_HOST_MEMORY;
    } else {
        EAX = VK_SUCCESS;
        // VK_DEFINE_NON_DISPATCHABLE_HANDLE (always 64 bit)
        cpu->memory->writeq(cpu->peek32(4), (U64)surface);
        XServer::getServer()->setFakeFullScreenWindow(xWindow);
    }
}

#include "vk_host_marshal.h"

bool initVulkan();
void vk_CreateInstance(CPU* cpu) {
    if (!initVulkan()) { EAX = (U32)VK_ERROR_INITIALIZATION_FAILED; return; }
    MarshalVkInstanceCreateInfo local_pCreateInfo(nullptr, cpu->memory, ARG1);
    VkInstanceCreateInfo* pCreateInfo = &local_pCreateInfo.s;
    static bool shown; if (!shown && ARG2) { klog("vkCreateInstance:VkAllocationCallbacks not implemented"); shown = true; }
    VkAllocationCallbacks* pAllocator = NULL;
    VkInstance pInstance;
    bool containsDebug = false;
    for (U32 i = 0; i < pCreateInfo->enabledExtensionCount; i++) {
        if (strstr(pCreateInfo->ppEnabledExtensionNames[i], VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) {
            containsDebug = true;
        } else if (strstr(pCreateInfo->ppEnabledExtensionNames[i], "VK_KHR_xlib_surface")) {
            delete[] pCreateInfo->ppEnabledExtensionNames[i];
#ifdef __MACH__
            const char* platformSurface = "VK_MVK_macos_surface";
#else
            const char* platformSurface = "VK_KHR_win32_surface";
#endif
            char** p = new char* [pCreateInfo->enabledExtensionCount];
            memcpy(p, pCreateInfo->ppEnabledExtensionNames, sizeof(char*) * (pCreateInfo->enabledExtensionCount));
            p[i] = new char[strlen(platformSurface) + 1];
            strcpy(p[i], platformSurface);
            delete[] pCreateInfo->ppEnabledExtensionNames;
            pCreateInfo->ppEnabledExtensionNames = p;
        }
    }
#ifdef _DEBUG1
    if (!containsDebug) {
        char** p = new char*[pCreateInfo->enabledExtensionCount + 1];
        memcpy(p, pCreateInfo->ppEnabledExtensionNames, sizeof(char*) * (pCreateInfo->enabledExtensionCount));
        p[pCreateInfo->enabledExtensionCount] = new char[strlen(VK_EXT_DEBUG_UTILS_EXTENSION_NAME) + 1];
        strcpy(p[pCreateInfo->enabledExtensionCount], VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        delete[] pCreateInfo->ppEnabledExtensionNames;
        pCreateInfo->ppEnabledExtensionNames = p;
        pCreateInfo->enabledExtensionCount++;
    }
    pCreateInfo->enabledLayerCount = 1;
    char** names = new char*[2];
    names[0] = (char*)"VK_LAYER_KHRONOS_validation";
    names[1] = (char*)"VK_LAYER_LUNARG_api_dump";
    pCreateInfo->ppEnabledLayerNames = names;
#endif
    EAX = pvkCreateInstance(pCreateInfo, pAllocator, &pInstance);
    if (EAX == VK_SUCCESS) {
        cpu->memory->writed(ARG3, createVulkanPtr(cpu->memory, pInstance, NULL));
    }
}

void vk_DestroyInstance2(CPU* cpu) {
    if (!ARG1) {
        return;
    }
    VkInstance instance = (VkInstance)getVulkanPtr(cpu->memory, ARG1);
    BoxedVulkanInfo* pBoxedInfo = getInfoFromHandle(cpu->memory, ARG1);
    static bool shown; if (!shown && ARG2) { klog("vkDestroyInstance:VkAllocationCallbacks not implemented"); shown = true; }
    VkAllocationCallbacks* pAllocator = NULL;

#ifdef _DEBUG
    PFN_vkDestroyDebugUtilsMessengerEXT debugFunc = (PFN_vkDestroyDebugUtilsMessengerEXT)pvkGetInstanceProcAddr(pBoxedInfo->instance, "vkDestroyDebugUtilsMessengerEXT");

    if (debugFunc) {
        debugFunc(pBoxedInfo->instance, pBoxedInfo->debugMessenger, nullptr);
    } else {
        klog("Vulkan debug function not found");
    }
#endif

    pBoxedInfo->pvkDestroyInstance(instance, pAllocator);
    freeVulkanPtr(cpu->memory, ARG1);
}

void vk_EnumerateInstanceExtensionProperties(CPU* cpu) {
    if (!initVulkan()) { EAX = (U32)VK_ERROR_INITIALIZATION_FAILED; return; }
    U32 len = 0;
    char* pLayerName = nullptr;
    if (ARG1) {
        len = cpu->memory->strlen(ARG1) + 1;
        pLayerName = new char[len];
        cpu->memory->memcpy(pLayerName, ARG1, (U32)len * sizeof(char));
    }
    uint32_t tmp_pPropertyCount = (uint32_t)cpu->memory->readd(ARG2);
    uint32_t* pPropertyCount = &tmp_pPropertyCount;
    VkExtensionProperties* pProperties = nullptr;
    if (ARG3) {
        pProperties = new VkExtensionProperties[*pPropertyCount];
        cpu->memory->memcpy(pProperties, ARG3, (U32)*pPropertyCount * sizeof(VkExtensionProperties));
    }
    EAX = pvkEnumerateInstanceExtensionProperties(pLayerName, pPropertyCount, pProperties);
    delete[] pLayerName;
    cpu->memory->writed(ARG2, (U32)tmp_pPropertyCount);
    if (pProperties) {
        for (U32 i = 0; i < tmp_pPropertyCount; i++) {
            if (!strcmp(pProperties[i].extensionName, "VK_MVK_macos_surface") || !strcmp(pProperties[i].extensionName, "VK_EXT_metal_surface") || !strcmp(pProperties[i].extensionName, "VK_KHR_win32_surface")) {
                strcpy(pProperties[i].extensionName, "VK_KHR_xlib_surface");
            }            
        }
        cpu->memory->memcpy(ARG3, pProperties, (U32)*pPropertyCount * sizeof(VkExtensionProperties));
    }
    delete[] pProperties;
}

VkBool32 VKAPI_PTR boxed_vkDebugReportCallbackEXT(VkDebugReportFlagsEXT flags, VkDebugReportObjectTypeEXT objectType, uint64_t object, size_t location, int32_t messageCode, const char* pLayerPrefix, const char* pMessage, void* pUserData) {
    if (pMessage) {
        klog(pMessage);
    } else {
        klog_fmt("vkDebugReportCallbackEXT %d", messageCode);
    }
    return VK_TRUE;
}

VkBool32 VKAPI_PTR boxed_vkDebugUtilsMessengerCallbackEXT(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity, VkDebugUtilsMessageTypeFlagsEXT messageTypes, const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData, void* pUserData) {
    if (pCallbackData && pCallbackData->pMessage) {
        klog(pCallbackData->pMessage);
    } else {
        klog("vkDebugUtilsMessengerCallbackEXT");
    }
    return VK_TRUE;
}

// ported/inspired by https://github.com/KhronosGroup/Vulkan-ValidationLayers/blob/main/layers/core_checks/cc_descriptor.cpp
U32 calculateUpdateDescriptorSetWithTemplateDataSize(BoxedVulkanInfo* pBoxedInfo, VkDescriptorUpdateTemplate descriptorUpdateTemplate) {
    auto found = pBoxedInfo->descriptorUpdateTemplateCreateInfo.find((U64)descriptorUpdateTemplate);
    if (found == pBoxedInfo->descriptorUpdateTemplateCreateInfo.end() || !found->second) {
        static bool shown;
        if (!shown) { klog("vk: UNSUPPORTED descriptor-update-template: no cached create info, update data size 0"); shown = true; }
        return 0;
    }
    std::shared_ptr<MarshalVkDescriptorUpdateTemplateCreateInfo> pCreateInfo = found->second;

    // The data blob must cover, for every entry, the last element's offset plus
    // that element's payload. The payload is at least the descriptor struct
    // even when the application passes a smaller stride.
    size_t size = 0;
    for (uint32_t i = 0; i < pCreateInfo->s.descriptorUpdateEntryCount; i++) {
        const VkDescriptorUpdateTemplateEntry& e = pCreateInfo->s.pDescriptorUpdateEntries[i];
        size_t payload = 0;
        switch (e.descriptorType) {
        case VK_DESCRIPTOR_TYPE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER:
        case VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
        case VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:
        case VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT:
            payload = sizeof(VkDescriptorImageInfo);
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:
        case VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC:
        case VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC:
            payload = sizeof(VkDescriptorBufferInfo);
            break;
        case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR:
        case VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_NV:
            payload = sizeof(uint64_t); // non-dispatchable handle
            break;
        case VK_DESCRIPTOR_TYPE_INLINE_UNIFORM_BLOCK:
            // descriptorCount is a byte count and the data is contiguous
            size = std::max(size, (size_t)e.offset + e.descriptorCount);
            continue;
        default:
        {
            static bool showOnce;
            if (!showOnce) {
                kwarn_fmt("calculateUpdateDescriptorSetWithTemplateDataSize unexpected descriptorType %x", e.descriptorType);
                showOnce = true;
            }
        }
            payload = e.stride;
            break;
        }
        if (e.descriptorCount) {
            size = std::max(size, (size_t)e.offset + (size_t)(e.descriptorCount - 1) * e.stride + payload);
        }
    }
    return (U32)size;
}

#include "../vulkan/vk_host.h"

static bool vulkanInitialized;
static bool vulkanLoadFailed;

bool initVulkan() {
    if (!vulkanInitialized) {
        BOXEDWINE_CRITICAL_SECTION;
        if (vulkanInitialized) {
            return true;
        }

        if (SDL_Vulkan_LoadLibrary(NULL)) {
            // Leave every pvk* null: vk_CreateInstance and the proc-address
            // paths check pvkCreateInstance and return VK_ERROR_INITIALIZATION_FAILED.
            if (!vulkanLoadFailed) {
                klog_fmt("vk: UNSUPPORTED host Vulkan loader: %s (instance creation will fail with VK_ERROR_INITIALIZATION_FAILED)", SDL_GetError());
                vulkanLoadFailed = true;
            }
            return false;
        }
        pvkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr)SDL_Vulkan_GetVkGetInstanceProcAddr();
        pvkCreateInstance = (PFN_vkCreateInstance)pvkGetInstanceProcAddr(VK_NULL_HANDLE, "vkCreateInstance");
        pvkEnumerateInstanceExtensionProperties = (PFN_vkEnumerateInstanceExtensionProperties)pvkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceExtensionProperties");
#undef VKFUNC_INSTANCE
#define VKFUNC_INSTANCE(f)
#undef VKFUNC
#define VKFUNC(f) pvk##f = (PFN_vk##f)pvkGetInstanceProcAddr(VK_NULL_HANDLE, "vk"#f); if (!pvk##f) {kwarn("Boxedwine: Failed to load vk"#f);}
#include "../vulkan/vkfuncs.h"
#undef LOAD_FUNCPTR
        vulkanInitialized = true;
    }
    return true;
}

Int99Callback int9ACallback[VK_LAST_VALUE+1];
U32 int9ACallbackSize;

void vulkan_init() {
    int9ACallbackSize = VK_LAST_VALUE+1;

#undef VKFUNC
#undef VKFUNC_INSTANCE
#define VKFUNC(name) int9ACallback[name] = vk_##name;
#define VKFUNC_INSTANCE(name) int9ACallback[name] = vk_##name;
#include "vkfuncs.h"      

    int9ACallback[CreateXlibSurfaceKHR] = BOXED_vkCreateXlibSurfaceKHR;
    int9ACallback[GetDeviceProcAddr] = hasProcAddress;
    int9ACallback[GetInstanceProcAddr] = hasProcAddress;
    int9ACallback[CreateInstance] = vk_CreateInstance;
    int9ACallback[DestroyInstance] = vk_DestroyInstance2;
    int9ACallback[EnumerateInstanceExtensionProperties] = vk_EnumerateInstanceExtensionProperties;
}

#endif

// A guest call into a function the host cannot serve returns
// VK_ERROR_FEATURE_NOT_PRESENT and logs once per function id.
static void reportMissingVulkanFunction(CPU* cpu, U32 index, const char* why) {
    static std::set<U32> reported;
    if (reported.insert(index).second) {
        klog_fmt("vk: UNSUPPORTED function id %d: %s, returning VK_ERROR_FEATURE_NOT_PRESENT", index, why);
    }
    EAX = (U32)-8; /* VK_ERROR_FEATURE_NOT_PRESENT; the Vulkan headers are absent when BOXEDWINE_VULKAN is off */
}

void callVulkan(CPU* cpu, U32 index) {
#ifdef BOXEDWINE_VULKAN
    if (index < int9ACallbackSize) {
        if (int9ACallback[index]) {
            int9ACallback[index](cpu);
        } else {
            reportMissingVulkanFunction(cpu, index, "no handler for this entry point");
        }
    } else 
#endif
    {
        reportMissingVulkanFunction(cpu, index, "Vulkan not compiled into Boxedwine");
    }
}
