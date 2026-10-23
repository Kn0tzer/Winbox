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
#include <map>
#include <deque>
#include <unordered_set>
#include <vector>
#include <cstdio>
using U8 = uint8_t; using U32 = uint32_t; using U64 = uint64_t;
using S32 = int32_t; using S64 = int64_t;
#define K_PROT_READ 1
#define K_PROT_WRITE 2
#define K_MAP_PRIVATE 0x02
#define K_MAP_ANONYMOUS 0x20
#define EMSCRIPTEN_KEEPALIVE
void klog(const char*) {}
void klog_fmt(const char*, ...) {}
// Metrics are an optional diagnostic boundary; keep them off under test.
// Profiling is an optional diagnostic boundary, not a Vulkan semantic under
// test. Keep it disabled while compiling the current shared 32/64 bridge.
bool ripSamplerProfileEnabled() { return false; }
void ripSamplerProfDumpReset(int) {}
class KProcess { public: U32 id = 0; };
class KThread { public: U32 id = 0; KProcess* process = nullptr; };
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
    void writeb(U64 addr, U8 value) { memcpyToGuest(addr, &value, 1); }
};
// The shared bridge now compiles both architectures. Its 32-bit memory
// adapter uses the same bounds-checked byte storage; no bridge code is stubbed.
class KMemory : public KMemory64 {
public:
    U32 mmap(KThread*, U32 addr, U32 len, S32 prot, S32 flags, S32 fd, U64 off, bool remap) {
        assert(addr == 0 && prot == (K_PROT_READ | K_PROT_WRITE));
        assert(flags == (K_MAP_ANONYMOUS | K_MAP_PRIVATE) && fd == -1 && off == 0 && !remap);
        if (!len || len > bytes.size() - next) return (U32)-12; // -ENOMEM from this fixture's finite memory.
        return (U32)mmapReserveAndMap(len, (U32)prot);
    }
    void unmap(U32 addr, U32 len) { munmap(addr, len); }
};
class CPU64 { public: KMemory64* memory; KThread* thread = nullptr; };
class CPU { public: KMemory* memory; KThread* thread = nullptr; };
#include "../../../source/vulkan/vk64bridge.cpp"
// Metrics are an optional diagnostic boundary; keep them off under test.
extern "C" int bw64_metrics_enabled(void) { return 0; }
extern "C" void bw64_metrics_vk_frame(const Bw64VkFrame*) {}

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
void vkCmdBeginRendering(U64, const void*);
void vkCmdEndRendering(U64);
void vkCmdBindPipeline(U64, U32, U64);
void vkCmdBindDescriptorSets(U64, U32, U64, U32, U32, const U64*, U32, const U32*);
int vkCreateGraphicsPipelines(U64, U64, U32, const void*, const void*, void*);
void vkCmdBindVertexBuffers2(U64, U32, U32, const U64*, const U64*, const U64*, const U64*);
void vkCmdUpdateBuffer(U64, U64, U64, U64, const void*);
void vkUpdateDescriptorSets(U64, U32, const void*, U32, const void*);
void vkCmdBindVertexBuffers2EXT(U64, U32, U32, const U64*, const U64*, const U64*, const U64*);
void vkCmdDispatch(U64, U32, U32, U32);
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
    // Build the producer input using Vulkan x86-64 byte offsets, rather than
    // the bridge typedefs: this catches an internally consistent but wrong ABI.
    U64 dynamicCbId; auto* dynamicCb = createObj<CmdBuf>(K_CMDBUF, dynamicCbId);
    U64 imageId; auto* image = createObj<Image>(K_IMAGE, imageId);
    image->format = 44; image->samples = 4;
    U64 viewId; auto* view = createObj<ImageView>(K_IMAGEVIEW, viewId);
    view->image = imageId; view->format = 44; view->aspect = 1;
    U64 sampledViewId; auto* sampledView = createObj<ImageView>(K_IMAGEVIEW, sampledViewId);
    sampledView->image = imageId; sampledView->format = 44; sampledView->aspect = 1;
    U64 sampledSetId; auto* sampledSet = createObj<DescSet>(K_SET, sampledSetId);
    sampledSet->nbind = 1; sampledSet->binds[0].type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    sampledSet->binds[0].obj = sampledViewId; sampledSet->binds[0].count = 1;
    U8 attachment[72] = {}, rendering[72] = {};
    auto put32 = [](U8* bytes, size_t at, U32 value) { memcpy(bytes + at, &value, 4); };
    auto put64 = [](U8* bytes, size_t at, U64 value) { memcpy(bytes + at, &value, 8); };
    put64(attachment, 16, viewId); put32(attachment, 44, 1); put32(attachment, 48, 0);
    float clear[4] = {0.125f, 0.25f, 0.5f, 1.0f};
    memcpy(attachment + 52, clear, sizeof(clear));
    put32(rendering, 28, 480); put32(rendering, 32, 360);
    put32(rendering, 36, 1); put32(rendering, 44, 1); put64(rendering, 48, 1536);
    memory.memcpyToGuest(1536, attachment, sizeof(attachment));
    memory.memcpyToGuest(1664, rendering, sizeof(rendering));
    vkCmdBeginRendering(dynamicCbId, (void*)1664);
    memory.writeq(1792, sampledSetId);
    vkCmdBindDescriptorSets(dynamicCbId, 0, 0, 0, 1, (U64*)1792, 0, nullptr);
    vkCmdDraw(dynamicCbId, 3, 1, 0, 0); vkCmdEndRendering(dynamicCbId);
    serializeSubmitV2({{dynamicCb, dynamicCbId}});
    assert(!g_v2Pending.empty());
    unsigned rpBegins = 0, aliasRecords = 0;
    for (const auto& chunk : g_v2Pending.back().chunks) {
        for (size_t at = 16; at < chunk.size();) {
            U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
            assert(at + 8 + len <= chunk.size());
            if (type == V2_RP_BEGIN) {
                ++rpBegins;
                assert(len == 108); // Existing fields, one view/image identity pair, and the STP1 stencil tail.
                U32 count, format, load, store, samples;
                memcpy(&count, chunk.data() + at + 8 + 24, 4);
                memcpy(&format, chunk.data() + at + 8 + 28, 4);
                memcpy(&load, chunk.data() + at + 8 + 32, 4);
                memcpy(&store, chunk.data() + at + 8 + 36, 4);
                memcpy(&samples, chunk.data() + at + 8 + 40, 4);
                assert(count == 1 && format == 44 && load == 1 && store == 0 && samples == 4);
                assert(memcmp(chunk.data() + at + 8 + 48, clear, 16) == 0);
                U64 attachmentView, attachmentImage;
                memcpy(&attachmentView, chunk.data() + at + 8 + 72, 8);
                memcpy(&attachmentImage, chunk.data() + at + 8 + 80, 8);
                assert(attachmentView == viewId && attachmentImage == imageId);
            }
            if (type == V2_IMAGE_DATA) {
                ++aliasRecords;
                U64 sampledView, sampledImage;
                memcpy(&sampledView, chunk.data() + at + 8, 8);
                memcpy(&sampledImage, chunk.data() + at + 16, 8);
                assert(sampledView == sampledViewId && sampledView != viewId && sampledImage == imageId);
            }
            at += 8 + len;
        }
    }
    assert(rpBegins == 1 && aliasRecords == 1); // Views differ, image identity joins them.
    // A render-pass clear changes the sampled image even without a draw. DXVK
    // submits this before its separate swapchain presentation command buffer.
    CmdBuf clearOnly;
    for (const Cmd& command : dynamicCb->cmds)
        if (command.kind == CMD_BEGIN_RP || command.kind == CMD_END_RP) clearOnly.cmds.push_back(command);
    const size_t beforeClear = g_v2Pending.size();
    serializeSubmitV2({{&clearOnly, dynamicCbId}});
    assert(g_v2Pending.size() == beforeClear + 1);
    unsigned clearPasses = 0;
    for (const auto& chunk : g_v2Pending.back().chunks) for (size_t at = 16; at < chunk.size();) {
        U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
        assert(at + 8 + len <= chunk.size());
        if (type == V2_RP_BEGIN) {
            ++clearPasses;
            assert(memcmp(chunk.data() + at + 8 + 48, clear, 16) == 0);
        }
        assert(type != V2_DRAW); // indexed draws use the same record with a flag
        at += 8 + len;
    }
    assert(clearPasses == 1);
    puts("PASS: clear-only render passes survive submission serialization");
    g_v2Pending.clear();
    const auto syntheticIds = dynamicCb->dynamicObjects;
    resetCmdBuf(dynamicCb);
    for (auto id : syntheticIds) assert(g_objs.count(id) == 0);
    assert(vkGetDeviceProcAddr(0, "vkCmdBindVertexBuffers2") ==
           reinterpret_cast<void(*)()>(vkCmdBindVertexBuffers2));
    assert(vkGetDeviceProcAddr(0, "vkCmdBindVertexBuffers2EXT") ==
           reinterpret_cast<void(*)()>(vkCmdBindVertexBuffers2EXT));
    U64 vertexMemoryId; auto* vertexMemory = createObj<DeviceMemory>(K_MEMORY, vertexMemoryId);
    vertexMemory->size = 128; vertexMemory->va = memory.mmapReserveAndMap(128, 3);
    U64 vertexId; auto* vertex = createObj<Buffer>(K_BUFFER, vertexId);
    vertex->size = 128; vertex->mem = vertexMemoryId;
    for (U32 i = 0; i < 128; ++i) memory.bytes[vertexMemory->va + i] = i;
    VkVertexInputBindingDescription binding{0, 0, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attribute{0, 0, (VkFormat)109, 0};
    VkPipelineVertexInputStateCreateInfo vertexInfo{};
    vertexInfo.vertexBindingDescriptionCount = 1; vertexInfo.pVertexBindingDescriptions = 1920;
    vertexInfo.vertexAttributeDescriptionCount = 1; vertexInfo.pVertexAttributeDescriptions = 1952;
    VkPipelineDynamicStateCreateInfo dynamicInfo{};
    dynamicInfo.dynamicStateCount = 1; dynamicInfo.pDynamicStates = 1984;
    VkDynamicState dynamicState = VK_DYNAMIC_STATE_VERTEX_INPUT_BINDING_STRIDE;
    VkGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.pVertexInputState = 2048; pipelineInfo.pDynamicState = 2176;
    memory.memcpyToGuest(1920, &binding, sizeof(binding));
    memory.memcpyToGuest(1952, &attribute, sizeof(attribute));
    memory.memcpyToGuest(1984, &dynamicState, sizeof(dynamicState));
    memory.memcpyToGuest(2048, &vertexInfo, sizeof(vertexInfo));
    memory.memcpyToGuest(2176, &dynamicInfo, sizeof(dynamicInfo));
    memory.memcpyToGuest(2304, &pipelineInfo, sizeof(pipelineInfo));
    assert(vkCreateGraphicsPipelines(deviceId, 0, 1, (void*)2304, nullptr, (void*)2560) == VK_SUCCESS);
    U64 pipelineId = memory.readq(2560);
    auto* pipeline = objOf<Pipeline>(pipelineId, K_PIPELINE, "test");
    assert(pipeline->dynamicVertexStride && pipeline->nvb == 1 && pipeline->nva == 1);
    vkCmdBeginRendering(dynamicCbId, (void*)1664);
    memory.writeq(1800, vertexId); memory.writeq(1816, 8); memory.writeq(1832, 20);
    vkCmdBindVertexBuffers2(dynamicCbId, 0, 1, (U64*)1800, (U64*)1816, nullptr, (U64*)1832);
    vkCmdBindPipeline(dynamicCbId, 0, pipelineId);
    vkCmdDraw(dynamicCbId, 3, 1, 0, 0);
    memory.writeq(1816, 16); memory.writeq(1832, 24);
    vkCmdBindVertexBuffers2EXT(dynamicCbId, 0, 1, (U64*)1800, (U64*)1816, nullptr, (U64*)1832);
    vkCmdDraw(dynamicCbId, 3, 1, 0, 0); vkCmdEndRendering(dynamicCbId);
    serializeSubmitV2({{dynamicCb, dynamicCbId}});
    std::vector<U32> emittedStrides;
    std::vector<U64> emittedPipelineIds;
    std::vector<U64> emittedOffsets;
    std::vector<U64> emittedBindings;
    std::vector<U64> emittedBufferIds;
    std::vector<U64> emittedMemoryOffsets;
    unsigned bufferRecords = 0;
    for (const auto& chunk : g_v2Pending.back().chunks) {
        for (size_t at = 16; at < chunk.size();) {
            U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
            const U8* p = chunk.data() + at + 8;
            if (type == V2_PIPELINE) {
                U32 stride; memcpy(&stride, p + 64, 4); emittedStrides.push_back(stride);
                U64 id; memcpy(&id, p, 8); emittedPipelineIds.push_back(id);
            }
            if (type == V2_VERTEX_BIND) {
                U64 id; memcpy(&id, p + 8, 8); emittedBindings.push_back(id);
                U64 offset; memcpy(&offset, p + 16, 8); emittedOffsets.push_back(offset);
            }
            if (type == V2_BUFFER_DATA) {
                ++bufferRecords;
                // Current wire schema: replay ID, guest memory offset, hash,
                // byte length, payload. Different binding offsets are rebased
                // to independent slices; the draw-use prepass bounds this
                // fixture to 3 vertices * largest stride 24 + attribute 16.
                U64 id, offset, hash; U32 byteLength;
                memcpy(&id, p, 8); memcpy(&offset, p + 8, 8);
                memcpy(&hash, p + 16, 8); memcpy(&byteLength, p + 24, 4);
                assert(offset == 8 || offset == 16);
                assert(byteLength == 88 && len == 28 + byteLength);
                const U8* expected = memory.bytes.data() + vertexMemory->va + offset;
                assert(memcmp(p + 28, expected, byteLength) == 0);
                assert(hash == fnv1a64(expected, byteLength));
                emittedBufferIds.push_back(id); emittedMemoryOffsets.push_back(offset);
            }
            at += 8 + len;
        }
    }
    assert((emittedStrides == std::vector<U32>{20, 24}));
    assert((emittedOffsets == std::vector<U64>{0, 0}));
    assert((emittedMemoryOffsets == std::vector<U64>{8, 16}));
    assert(emittedBindings == emittedBufferIds);
    assert(emittedBufferIds.size() == 2 && emittedBufferIds[0] != emittedBufferIds[1]);
    assert(bufferRecords == 2); // Each unique slice transfers once with its own replay identity.
    assert(emittedPipelineIds.size() == 2 && emittedPipelineIds[0] != emittedPipelineIds[1]);
    assert(emittedPipelineIds[0] != pipelineId && emittedPipelineIds[1] != pipelineId);
    auto readPipelineIds = [&]() {
        std::vector<U64> ids;
        for (const auto& chunk : g_v2Pending.back().chunks) {
            for (size_t at = 16; at < chunk.size();) {
                U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
                if (type == V2_PIPELINE) { U64 id; memcpy(&id, chunk.data() + at + 8, 8); ids.push_back(id); }
                at += 8 + len;
            }
        }
        return ids;
    };
    // Reusing a recording in a future frame must reuse both identities, so
    // consumer pipeline caches stay correct and bounded by the layouts used.
    serializeSubmitV2({{dynamicCb, dynamicCbId}});
    assert(readPipelineIds() == emittedPipelineIds);
    memory.writeq(1832, 20);
    vkCmdBindVertexBuffers2(dynamicCbId, 0, 1, (U64*)1800, (U64*)1816, nullptr, (U64*)1832);
    vkCmdDraw(dynamicCbId, 3, 1, 0, 0);
    serializeSubmitV2({{dynamicCb, dynamicCbId}});
    assert((readPipelineIds() == std::vector<U64>{emittedPipelineIds[0], emittedPipelineIds[1], emittedPipelineIds[0]}));
    pipeline->dynamicVertexStride = false;
    serializeSubmitV2({{dynamicCb, dynamicCbId}});
    assert((readPipelineIds() == std::vector<U64>{pipelineId}));
    pipeline->dynamicVertexStride = true;
    resetCmdBuf(dynamicCb);
    assert(!dynamicCb->vbHasStride[0] && !dynamicCb->vbStride[0]);
    g_v2Pending.clear();
    // Depth attachment has its own load/store and clear value, rather than
    // borrowing color metadata. It must occupy an explicit depth entry.
    U64 depthImageId; auto* depthImage = createObj<Image>(K_IMAGE, depthImageId);
    depthImage->format = 126; depthImage->samples = 4;
    U64 depthViewId; auto* depthView = createObj<ImageView>(K_IMAGEVIEW, depthViewId);
    depthView->image = depthImageId; depthView->format = 126; depthView->aspect = 2;
    U8 depthAttachment[72] = {};
    put64(depthAttachment, 16, depthViewId); put32(depthAttachment, 44, 1); put32(depthAttachment, 48, 1);
    float depthClear = 0.375f; memcpy(depthAttachment + 52, &depthClear, 4);
    put64(rendering, 56, 2688);
    memory.memcpyToGuest(2688, depthAttachment, sizeof(depthAttachment));
    memory.memcpyToGuest(1664, rendering, sizeof(rendering));
    vkCmdBeginRendering(dynamicCbId, (void*)1664);
    auto& begin = dynamicCb->cmds.back();
    auto* dynamicFb = objOf<Framebuffer>(begin.b, K_FRAMEBUFFER, "test");
    auto* dynamicRp = objOf<RenderPass>(begin.a, K_RENDERPASS, "test");
    assert(dynamicFb->natt == 2 && dynamicFb->views[1] == depthViewId);
    assert(dynamicRp->formats[1] == 126 && dynamicRp->samples[1] == 4);
    assert(begin.clearDepth == 0.375 && dynamicRp->loadOps[1] == 1 && dynamicRp->storeOps[1] == 1);
    vkCmdDraw(dynamicCbId, 3, 1, 0, 0); vkCmdEndRendering(dynamicCbId);
    serializeSubmitV2({{dynamicCb, dynamicCbId}});
    unsigned depthPasses = 0;
    for (const auto& chunk : g_v2Pending.back().chunks) {
        for (size_t at = 16; at < chunk.size();) {
            U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
            if (type == V2_RP_BEGIN) {
                ++depthPasses;
                assert(len == 156); // 28 + 2*20 + 24 + 2*16 + STP1 (8 + 2*12).
                const U8* tail = chunk.data() + at + 8 + 92;
                U64 ids[4]; memcpy(ids, tail, sizeof(ids));
                assert(ids[0] == viewId && ids[1] == imageId && ids[2] == depthViewId && ids[3] == depthImageId);
            }
            at += 8 + len;
        }
    }
    assert(depthPasses == 1);
    g_v2Pending.clear();
    // vkCmdUpdateBuffer: DXVK uploads small uniforms with this (notably the
    // D3D9 fixed-function VS constant buffer). The payload must be captured
    // at record time and written into guest memory at submit, so
    // readGuestRange -- and the BUFFER_DATA manifest record -- carries real
    // data instead of zeros.
    assert(vkGetDeviceProcAddr(0, "vkCmdUpdateBuffer") ==
           reinterpret_cast<void(*)()>(vkCmdUpdateBuffer));
    U64 uniformMemoryId; auto* uniformMemory = createObj<DeviceMemory>(K_MEMORY, uniformMemoryId);
    uniformMemory->size = 256; uniformMemory->va = memory.mmapReserveAndMap(256, 3);
    U64 uniformId; auto* uniform = createObj<Buffer>(K_BUFFER, uniformId);
    uniform->size = 256; uniform->mem = uniformMemoryId; uniform->memOff = 0;
    U64 updateCbId; auto* updateCb = createObj<CmdBuf>(K_CMDBUF, updateCbId);
    U8 upayload[16]; for (U32 i = 0; i < 16; ++i) upayload[i] = (U8)(0xA0 + i);
    memory.memcpyToGuest(3072, upayload, sizeof(upayload));
    vkCmdUpdateBuffer(updateCbId, uniformId, 32, sizeof(upayload), (void*)3072);
    assert(lastTrap == VK64_fn_vkCmdUpdateBuffer);
    assert(updateCb->cmds.size() == 1);
    const Cmd& uc = updateCb->cmds.back();
    assert(uc.kind == CMD_UPDATE_BUF && uc.updBuf == uniformId && uc.updOff == 32);
    assert(uc.updBytes.size() == sizeof(upayload) &&
           memcmp(uc.updBytes.data(), upayload, sizeof(upayload)) == 0);
    // Clobber the guest source after recording: the command holds its own copy.
    memset(upayload, 0, sizeof(upayload));
    memory.memcpyToGuest(3072, upayload, sizeof(upayload));
    assert(memory.bytes[uniformMemory->va + 32] == 0); // untouched until submit-time execution
    execUpdateBuf(uc);
    for (U32 i = 0; i < 16; ++i) assert(memory.bytes[uniformMemory->va + 32 + i] == (U8)(0xA0 + i));
    assert(memory.bytes[uniformMemory->va + 31] == 0 && memory.bytes[uniformMemory->va + 48] == 0);
    auto ub = readGuestRange(uniformMemoryId, 0, 256); // the manifest path
    assert(ub.size() == 256);
    for (U32 i = 0; i < 16; ++i) assert(ub[32 + i] == (U8)(0xA0 + i));
    puts("PASS: vkCmdUpdateBuffer record-time capture and submit-time write");
    // Descriptor buffer offset: VkDescriptorBufferInfo::offset must be honored
    // when capturing uniform data. DXVK sub-allocates D3D9 constants inside one
    // VkBuffer (D3D9ConstantBuffer::Alloc) and binds each range with its own
    // offset; reading from offset 0 captured zeros for every uniform.
    U64 offMemId; auto* offMem = createObj<DeviceMemory>(K_MEMORY, offMemId);
    offMem->size = 256; offMem->va = memory.mmapReserveAndMap(256, 3);
    U64 offBufId; auto* offBuf = createObj<Buffer>(K_BUFFER, offBufId);
    offBuf->size = 256; offBuf->mem = offMemId; offBuf->memOff = 0;
    for (U32 i = 0; i < 256; ++i) memory.bytes[offMem->va + i] = (U8)i;
    U64 offSetId; auto* offSet = createObj<DescSet>(K_SET, offSetId);
    VkDescriptorBufferInfo dbi{}; dbi.buffer = offBufId; dbi.offset = 64; dbi.range = 32;
    memory.memcpyToGuest(3072, &dbi, sizeof(dbi));
    VkWriteDescriptorSet wds{};
    wds.dstSet = offSetId; wds.dstBinding = 0; wds.descriptorCount = 1;
    wds.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    wds.pBufferInfo = 3072;
    memory.memcpyToGuest(3136, &wds, sizeof(wds));
    vkUpdateDescriptorSets(deviceId, 1, (void*)3136, 0, nullptr);
    assert(offSet->nbind == 1);
    const Binding& ob = offSet->binds[0];
    assert(ob.obj == offBufId && ob.range == 32 && ob.bufOff == 64);
    // The serialize path reads buf->memOff + b.bufOff: verify those bytes.
    auto ob2 = readGuestRange(offMemId, offBuf->memOff + ob.bufOff, ob.range);
    assert(ob2.size() == 32);
    for (U32 i = 0; i < 32; ++i) assert(ob2[i] == (U8)(64 + i));
    puts("PASS: descriptor buffer offset honored in uniform capture");
    // Exercise the newly required 32-bit adapter through the real shared
    // bridge, including its i386 allocation layout and mmap-error cleanup.
    KMemory memory32; CPU cpu32{&memory32};
    auto call32 = [&](U32 fn, const VK32Args& args) {
        memory32.memcpyToGuest(128, &args, sizeof(args));
        return vk32Bridge(&cpu32, fn, 128);
    };
    vk32abi::VkMemoryAllocateInfo allocation32{};
    allocation32.sType = vk32abi::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation32.allocationSize = 4096;
    memory32.memcpyToGuest(512, &allocation32, sizeof(allocation32));
    VK32Args args32{}; args32.a[1] = 512; args32.a[3] = 640;
    assert(call32(VK64_fn_vkAllocateMemory, args32) == VK_SUCCESS);
    U64 allocationId32 = memory32.readq(640);
    auto* allocation = objOf32<DeviceMemory>(allocationId32, K_MEMORY, "test");
    assert(allocation && allocation->size == 4096 && allocation->va == 4096);
    memory32.writeq(672, 0xA5A5A5A5FFFFFFFFULL);
    VK32Args map32{}; map32.a[1] = allocationId32; map32.a[2] = 32;
    map32.a[3] = VK_WHOLE_SIZE; map32.a[5] = 672;
    assert(call32(VK64_fn_vkMapMemory, map32) == VK_SUCCESS);
    assert(memory32.readd(672) == allocation->va + 32);
    assert(memory32.readd(676) == 0xA5A5A5A5); // Out-pointer is exactly four bytes.
    memory32.writeq(640, 0xA5A5A5A5A5A5A5A5ULL);
    const size_t objectsBeforeReject = g_objs.size();
    for (U64 size : {0ULL, 0x40000001ULL, 2ULL * 1024 * 1024}) {
        allocation32.allocationSize = size;
        memory32.memcpyToGuest(512, &allocation32, sizeof(allocation32));
        assert((S64)call32(VK64_fn_vkAllocateMemory, args32) == VK_ERROR_OUT_OF_DEVICE_MEMORY);
        assert(memory32.readq(640) == 0xA5A5A5A5A5A5A5A5ULL);
        assert(g_objs.size() == objectsBeforeReject); // Failed allocations cannot leak handles.
    }
    VK32Args free32{}; free32.a[1] = allocationId32;
    assert(call32(VK64_fn_vkFreeMemory, free32) == VK_SUCCESS);
    assert(!findObj(allocationId32, K_MEMORY));
    g_mem32 = nullptr; g_mem = &memory;
    puts("PASS: Vulkan32 allocation layout, four-byte map output and failure cleanup");
    auto cleanupIds = dynamicCb->dynamicObjects;
    dropObj(dynamicCbId);
    for (auto id : cleanupIds) assert(!g_objs.count(id));
    while (!g_objs.empty()) dropObj(g_objs.begin()->first);
    puts("PASS: dynamic rendering attachment ABI, manifest metadata, stride/binding snapshots, cleanup");
    puts("PASS: command-pool isolation, child destruction, full reset, virtual destruction");
    puts("PASS: WithCount viewport/scissor core+EXT and coherent mapped ranges");

    // GUARD (silent drop): features the bridge cannot honour are reported by
    // vk64Unsupported() under a stable feature name and answered with the
    // right VkResult. A test that finds these counters at zero means the
    // feature went silent again.
    {
        U64 devId; createObj<Device>(K_DEVICE, devId);
        const size_t objectsBefore = g_objs.size();
        // Pipeline with a geometry stage: rejected, handle written as 0, no leak.
        VkPipelineShaderStageCreateInfo stages[2] = {};
        stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO; stages[1].stage = VK_SHADER_STAGE_GEOMETRY_BIT;
        memory.memcpyToGuest(3072, stages, sizeof(stages));
        VkPipelineInputAssemblyStateCreateInfo ia{}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        memory.memcpyToGuest(3328, &ia, sizeof(ia));
        VkGraphicsPipelineCreateInfo gp{}; gp.stageCount = 2; gp.pStages = 3072; gp.pInputAssemblyState = 3328;
        memory.memcpyToGuest(3456, &gp, sizeof(gp));
        memory.writeq(3840, 0xdeadbeefULL);
        assert(vk64BridgeUnsupportedCount("geometry-stage") == 0);
        assert((S64)vkCreateGraphicsPipelines(devId, 0, 1, (void*)3456, nullptr, (void*)3840) == VK_ERROR_FEATURE_NOT_PRESENT);
        assert(memory.readq(3840) == 0);
        assert(vk64BridgeUnsupportedCount("geometry-stage") == 1);
        assert(g_objs.size() == objectsBefore);
        // Tessellation topology (patch list) is also rejected, vertex-only stays accepted.
        ia.topology = (VkPrimitiveTopology)10; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        memory.memcpyToGuest(3072, stages, sizeof(stages)); memory.memcpyToGuest(3328, &ia, sizeof(ia));
        assert((S64)vkCreateGraphicsPipelines(devId, 0, 1, (void*)3456, nullptr, (void*)3840) == VK_ERROR_FEATURE_NOT_PRESENT);
        assert(vk64BridgeUnsupportedCount("patch-list-topology") == 1);
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; memory.memcpyToGuest(3328, &ia, sizeof(ia));
        assert(vkCreateGraphicsPipelines(devId, 0, 1, (void*)3456, nullptr, (void*)3840) == VK_SUCCESS);
        assert(memory.readq(3840) != 0);
        // Unimplemented entry point reached through the bridge is counted per function name.
        memory.memcpyToGuest(3900, "vkFooBar", 9);
        VK64Args unimpl{}; unimpl.a[0] = 3900;
        memory.memcpyToGuest(128, &unimpl, sizeof(unimpl));
        assert((S64)vk64Bridge(&cpu, VK64_fn_unimplemented, 128) == VK_ERROR_FEATURE_NOT_PRESENT);
        assert(vk64BridgeUnsupportedCount("fn:vkFooBar") == 1);
        // Compute dispatch has no V2 record: submitting one is reported, never dropped quietly.
        U64 cbDispatchId; auto* cbDispatch = createObj<CmdBuf>(K_CMDBUF, cbDispatchId);
        vkCmdDispatch(cbDispatchId, 1, 1, 1);
        assert(vk64BridgeUnsupportedCount("compute-dispatch") == 0);
        serializeSubmitV2({{cbDispatch, cbDispatchId}});
        assert(vk64BridgeUnsupportedCount("compute-dispatch") == 1);
        g_v2Pending.clear();
        // GUARD (silent drop): a command buffer with no render pass but with an image
        // copy used to be skipped ("no framebuffer, skipped"); it now becomes a 1x1
        // frame carrying the copy, while a barrier-only command buffer stays empty.
        g_v2Pending.clear();
        U64 cbCopyId; auto* cbCopy = createObj<CmdBuf>(K_CMDBUF, cbCopyId);
        Cmd copyCmd; copyCmd.kind = CMD_COPY_IMAGE; copyCmd.imageCopy.src = 11; copyCmd.imageCopy.dst = 12;
        copyCmd.imageCopy.extent[0] = 4; copyCmd.imageCopy.extent[1] = 4; copyCmd.imageCopy.extent[2] = 1;
        pushCmd(cbCopy, copyCmd);
        serializeSubmitV2({{cbCopy, cbCopyId}});
        assert(g_v2Pending.size() == 1);
        bool sawBegin = false, sawCopy = false;
        for (const auto& chunk : g_v2Pending.back().chunks) {
            for (size_t at = 16; at < chunk.size();) {
                U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
                if (type == V2_FRAME_BEGIN) {
                    U32 w, h; memcpy(&w, chunk.data() + at + 12, 4); memcpy(&h, chunk.data() + at + 16, 4);
                    assert(w == 1 && h == 1); sawBegin = true;
                }
                if (type == V2_IMAGE_COPY) sawCopy = true;
                at += 8 + len;
            }
        }
        assert(sawBegin && sawCopy);
        g_v2Pending.clear();
        U64 cbBarrierId; auto* cbBarrier = createObj<CmdBuf>(K_CMDBUF, cbBarrierId);
        Cmd barrierCmd; barrierCmd.kind = CMD_BARRIER; pushCmd(cbBarrier, barrierCmd);
        serializeSubmitV2({{cbBarrier, cbBarrierId}});
        assert(g_v2Pending.empty());
        while (!g_objs.empty()) dropObj(g_objs.begin()->first);
        puts("PASS: GUARD unsupported features are rejected loudly (geometry/tess pipelines, unimplemented fn, compute dispatch, renderless submit)");
    }
}
