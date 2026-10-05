// Native boundary test: real shim wrappers and bridge, with byte-addressed guest
// memory replacing the emulator. No Vulkan loader, browser, or GPU is involved.
#define __BOXEDWINE_H__
#define __CPU64_H__
#define __KMEMORY64_H__
#define __KPROCESS_H__
#define BOXEDWINE_VULKAN64
#include <cstdint>
#include <cstring>
#include <cassert>
#include <algorithm>
#include <set>
#include <deque>
#include <unordered_set>
#include <vector>
#include <cstdio>
using U8 = uint8_t; using U32 = uint32_t; using U64 = uint64_t;
using S32 = int32_t; using S64 = int64_t;
#define K_PROT_READ 1
#define K_PROT_WRITE 2
void klog(const char*) {}
void klog_fmt(const char*, ...) {}
class KMemory64 {
public:
    std::vector<U8> bytes = std::vector<U8>(1024 * 1024);
    U64 next = 4096;
    U64 mmapReserveAndMap(U64 size, U32) {
        U64 addr = next; next += (size + 4095) & ~4095ULL;
        assert(next < bytes.size()); return addr;
    }
    void munmap(U64, U64) {}
    void memcpyFromGuest(void* out, U64 addr, U64 size) {
        assert(addr <= bytes.size() && size <= bytes.size() - addr);
        std::memcpy(out, bytes.data() + addr, size);
    }
    void memcpyToGuest(U64 addr, const void* in, U64 size) {
        assert(addr <= bytes.size() && size <= bytes.size() - addr);
        std::memcpy(bytes.data() + addr, in, size);
    }
    U8 readb(U64 addr) { U8 out; memcpyFromGuest(&out, addr, 1); return out; }
    U32 readd(U64 addr) { U32 out; memcpyFromGuest(&out, addr, 4); return out; }
    U64 readq(U64 addr) { U64 out; memcpyFromGuest(&out, addr, 8); return out; }
    void writed(U64 addr, U32 value) { memcpyToGuest(addr, &value, 4); }
    void writeq(U64 addr, U64 value) { memcpyToGuest(addr, &value, 8); }
};
class CPU64 { public: KMemory64* memory; };
#include "../../../source/vulkan/vk64bridge.cpp"

static KMemory64 memory;
static CPU64 cpu{&memory};
static U64 lastTrap;
extern "C" U64 vk64_test_trap(U64 fn, VK64Args* args) {
    lastTrap = fn;
    if (fn == VK64_fn_witness || fn == VK64_fn_traceProc) return 0;
    if (fn == VK64_fn_unimplemented) return (U64)(S64)VK_ERROR_FEATURE_NOT_PRESENT;
    memory.memcpyToGuest(128, args, sizeof(*args));
    return vk64Bridge(&cpu, fn, 128);
}
extern "C" {
void vkCmdSetViewportWithCount(U64, U32, const void*);
void vkCmdSetViewportWithCountEXT(U64, U32, const void*);
void vkCmdSetScissorWithCount(U64, U32, const void*);
void vkCmdSetScissorWithCountEXT(U64, U32, const void*);
int vkFlushMappedMemoryRanges(U64, U32, const void*);
int vkInvalidateMappedMemoryRanges(U64, U32, const void*);
void (*vkGetDeviceProcAddr(U64, const char*))();
int vkCreateCommandPool(U64, const void*, const void*, void*);
int vkAllocateCommandBuffers(U64, const void*, void*);
int vkResetCommandPool(U64, U64, U32);
void vkDestroyCommandPool(U64, U64, const void*);
void vkFreeCommandBuffers(U64, U64, U32, const U64*);
int vkBeginCommandBuffer(U64, const void*);
void vkCmdDraw(U64, U32, U32, U32, U32);
}
int main() {
    g_mem = &memory;
    assert(vkGetDeviceProcAddr(0, "vkCmdSetViewportWithCount") ==
           reinterpret_cast<void(*)()>(vkCmdSetViewportWithCount));
    assert(vkGetDeviceProcAddr(0, "vkCmdSetScissorWithCountEXT") ==
           reinterpret_cast<void(*)()>(vkCmdSetScissorWithCountEXT));
    assert(vkGetDeviceProcAddr(0, "vkFlushMappedMemoryRanges") ==
           reinterpret_cast<void(*)()>(vkFlushMappedMemoryRanges));
    U64 cbId; CmdBuf* cb = createObj<CmdBuf>(K_CMDBUF, cbId);
    VkViewport viewport{2, 3, 640, 480, 0.25f, 0.75f};
    memory.memcpyToGuest(512, &viewport, sizeof(viewport));
    vkCmdSetViewportWithCount(cbId, 1, (void*)512);
    assert(lastTrap == VK64_fn_vkCmdSetViewport && cb->haveVp);
    assert(cb->vp[0] == 2 && cb->vp[2] == 640 && cb->vp[5] == 0.75f);
    viewport.width = 320; memory.memcpyToGuest(512, &viewport, sizeof(viewport));
    vkCmdSetViewportWithCountEXT(cbId, 1, (void*)512);
    assert(cb->vp[2] == 320);
    VkRect2D scissor{{4, 5}, {300, 200}};
    memory.memcpyToGuest(512, &scissor, sizeof(scissor));
    vkCmdSetScissorWithCount(cbId, 1, (void*)512);
    assert(lastTrap == VK64_fn_vkCmdSetScissor && cb->haveSci && cb->sci[2] == 300);
    scissor.extent.width = 100; memory.memcpyToGuest(512, &scissor, sizeof(scissor));
    vkCmdSetScissorWithCountEXT(cbId, 1, (void*)512);
    assert(cb->sci[2] == 100);
    U64 deviceId; createObj<Device>(K_DEVICE, deviceId);
    U64 memId; auto* mem = createObj<DeviceMemory>(K_MEMORY, memId);
    mem->size = 8192; mem->va = memory.mmapReserveAndMap(mem->size, 3);
    VkMappedMemoryRange range{}; range.memory = memId; range.offset = 4096;
    range.size = VK_WHOLE_SIZE;
    memory.memcpyToGuest(512, &range, sizeof(range));
    memory.bytes[mem->va + 4096] = 42;
    assert(vkFlushMappedMemoryRanges(deviceId, 1, (void*)512) == VK_SUCCESS);
    assert(vkInvalidateMappedMemoryRanges(deviceId, 1, (void*)512) == VK_SUCCESS);
    assert(memory.bytes[mem->va + 4096] == 42);
    range.size = 4097; memory.memcpyToGuest(512, &range, sizeof(range));
    assert(vkFlushMappedMemoryRanges(deviceId, 1, (void*)512) != VK_SUCCESS);
    range.offset = UINT64_MAX; range.size = 2;
    memory.memcpyToGuest(512, &range, sizeof(range));
    assert(vkInvalidateMappedMemoryRanges(deviceId, 1, (void*)512) != VK_SUCCESS);
    assert(vkFlushMappedMemoryRanges(deviceId, 1, nullptr) != VK_SUCCESS);
    VkCommandPoolCreateInfo poolInfo{};
    memory.memcpyToGuest(512, &poolInfo, sizeof(poolInfo));
    assert(vkCreateCommandPool(deviceId, (void*)512, nullptr, (void*)640) == VK_SUCCESS);
    U64 poolA = memory.readq(640);
    assert(vkCreateCommandPool(deviceId, (void*)512, nullptr, (void*)640) == VK_SUCCESS);
    U64 poolB = memory.readq(640);
    auto allocate = [&](U64 pool) {
        VkCommandBufferAllocateInfo info{}; info.commandPool = pool; info.commandBufferCount = 1;
        memory.memcpyToGuest(512, &info, sizeof(info));
        assert(vkAllocateCommandBuffers(deviceId, (void*)512, (void*)640) == VK_SUCCESS);
        return memory.readq(640);
    };
    U64 bufferA = allocate(poolA), bufferB = allocate(poolB);
    vkCmdDraw(bufferA, 3, 1, 0, 0); vkCmdDraw(bufferB, 7, 1, 0, 0);
    assert(vkResetCommandPool(deviceId, poolA, 0) == VK_SUCCESS);
    auto* a = cbOf(bufferA, "test"); auto* b = cbOf(bufferB, "test");
    assert(a->cmds.empty());
    assert(b->cmds.size() == 1 && b->cmds[0].a == 7);
    assert(cb->cmds.size() == 4); // Unrelated buffer must keep its recorded state.
    a->pipe = 1; a->sets[0] = 2; a->vbs[0] = 3; a->ib = 4; a->haveVp = true;
    assert(vkResetCommandPool(deviceId, poolA, 0) == VK_SUCCESS);
    assert(!a->pipe && !a->sets[0] && !a->vbs[0] && !a->ib && !a->haveVp);
    vkCmdDraw(bufferA, 11, 1, 0, 0);
    assert(vkBeginCommandBuffer(bufferA, nullptr) == VK_SUCCESS);
    assert(a->cmds.empty() && b->cmds.size() == 1);
    vkDestroyCommandPool(deviceId, poolA, nullptr);
    assert(!findObj(poolA, K_CMDPOOL) && !findObj(bufferA, K_CMDBUF));
    assert(findObj(poolB, K_CMDPOOL) && findObj(bufferB, K_CMDBUF));
    assert(vkResetCommandPool(deviceId + 1, poolB, 0) != VK_SUCCESS);
    assert(b->cmds.size() == 1);
    memory.writeq(512, bufferB);
    vkFreeCommandBuffers(deviceId, 0, 1, (const U64*)512);
    assert(findObj(bufferB, K_CMDBUF));
    vkFreeCommandBuffers(deviceId, poolB, 1, (const U64*)512);
    assert(!findObj(bufferB, K_CMDBUF));
    vkDestroyCommandPool(deviceId, poolB, nullptr);
    struct Token {
        int* count;
        ~Token() { ++*count; }
    };
    struct Tracked : Obj { std::vector<Token> tokens; };
    int destroyedMembers = 0; U64 trackedId;
    auto* tracked = createObj<Tracked>(K_CMDBUF, trackedId);
    tracked->tokens.emplace_back(); tracked->tokens[0].count = &destroyedMembers;
    dropObj(trackedId);
    assert(destroyedMembers == 1); // Base deletion must reclaim derived vector elements.
    while (!g_objs.empty()) dropObj(g_objs.begin()->first);
    puts("PASS: command-pool isolation, child destruction, full reset, virtual destruction");
    puts("PASS: WithCount viewport/scissor core+EXT and coherent mapped ranges");
}
