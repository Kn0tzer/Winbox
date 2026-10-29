// Native boundary test: real shim wrappers and bridge, with byte-addressed guest
// memory replacing the emulator. No Vulkan loader, browser, or GPU is involved.
#define __BOXEDWINE_H__
#define __CPU64_H__
#define __KMEMORY64_H__
#define __KPROCESS_H__
// log.h is suppressed too: drvinittrace.h (campaign section 4.35) includes
// it for klog_fmt, which this file already stubs below; the rest of log.h
// declares BString-by-value functions from the suppressed emulator
// environment, so it can never parse in this translation unit.
#define __LOG_H__
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
void vkCmdSetDepthTestEnable(U64, U32);
void vkCmdSetStencilOp(U64, U32, U32, U32, U32, U32);
void vkCmdSetPrimitiveTopology(U64, U32);
void vkCmdSetRasterizerDiscardEnable(U64, U32);
void vkCmdSetDepthBoundsTestEnable(U64, U32);
void vkCmdSetDepthBounds(U64, float, float);
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
void vkGetDeviceImageMemoryRequirements(U64, const void*, void*);
void vkGetDeviceQueue2(U64, const void*, U64*);
void vkGetRenderAreaGranularity(U64, U64, U32*);
void vkGetImageSparseMemoryRequirements(U64, U64, U32*, void*);
void vkGetDeviceImageSparseMemoryRequirements(U64, const void*, U32*, void*);
void vkGetDeviceMemoryCommitment(U64, U64, U64*);
void vkGetDeviceGroupPeerMemoryFeatures(U64, U32, U32, U32, U32*);
int vkGetPipelineCacheData(U64, U64, size_t*, void*);
int vkMergePipelineCaches(U64, U64, U32, const U64*);
void vkDestroyBufferView(U64, U64, const void*);
void vkGetBufferMemoryRequirements2KHR(U64, const void*, void*);
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
    { Binding& sb = sampledSet->els[DescKey(0, 0)]; sb.type = VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
      sb.obj = sampledViewId; sb.count = 1; }
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
                assert(len == 132); // Existing fields, one view/image identity pair, the STP1 stencil tail, and the CLR1 per-attachment clears.
                { U32 tag; memcpy(&tag, chunk.data() + at + 8 + 108, 4); assert(tag == 0x31524c43u);
                  assert(memcmp(chunk.data() + at + 8 + 116, clear, 16) == 0); }
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
    // ---- Extended dynamic state (core 1.3 / EXT_extended_dynamic_state{,2}): the pipeline
    // declares which states come from commands (DYN1 tail), each command becomes a
    // RASTER_STATE record in order, and values the replay cannot honour are loud.
    {
        const U32 states[4] = {1000267006u, 1000267011u, 1000267002u, 1000377001u};   // depth test, stencil op, topology, discard
        memory.memcpyToGuest(7000, states, sizeof(states));
        VkPipelineDynamicStateCreateInfo di{}; di.dynamicStateCount = 4; di.pDynamicStates = 7000;
        memory.memcpyToGuest(7040, &di, sizeof(di));
        VkPipelineInputAssemblyStateCreateInfo iaDyn{}; iaDyn.topology = (VkPrimitiveTopology)10;   // static value is ignored: topology is dynamic
        memory.memcpyToGuest(7168, &iaDyn, sizeof(iaDyn));
        VkGraphicsPipelineCreateInfo gpDyn{}; gpDyn.pDynamicState = 7040; gpDyn.pInputAssemblyState = 7168;
        memory.memcpyToGuest(7296, &gpDyn, sizeof(gpDyn));
        memory.writeq(7424, 0);
        assert(vkCreateGraphicsPipelines(deviceId, 0, 1, (void*)7296, nullptr, (void*)7424) == VK_SUCCESS);
        U64 dynPipeId = memory.readq(7424);
        auto* dynPipe = objOf<Pipeline>(dynPipeId, K_PIPELINE, "test");
        assert(dynPipe->dynMask == ((1u << VK64_DYN_DEPTH_TEST) | (1u << VK64_DYN_STENCIL_OP) | (1u << VK64_DYN_TOPOLOGY) | (1u << VK64_DYN_RASTERIZER_DISCARD)));
        assert(!dynPipe->rasterizerDiscard);
        // A state with no V2 representation fails the pipeline, loudly, with no leak.
        const size_t objectsBeforeReject = g_objs.size();
        const U32 badStates[2] = {1000267002u, 1000381000u};   // topology (keeps the static patch-list quiet), VK_DYNAMIC_STATE_COLOR_WRITE_ENABLE_EXT
        memory.memcpyToGuest(7000, badStates, sizeof(badStates));
        di.dynamicStateCount = 2; memory.memcpyToGuest(7040, &di, sizeof(di));
        memory.writeq(7424, 0xdeadbeefULL);
        assert(vk64BridgeUnsupportedCount("dynamic-state-1000381000") == 0);
        assert((S64)vkCreateGraphicsPipelines(deviceId, 0, 1, (void*)7296, nullptr, (void*)7424) == VK_ERROR_FEATURE_NOT_PRESENT);
        assert(memory.readq(7424) == 0 && vk64BridgeUnsupportedCount("dynamic-state-1000381000") == 1);
        assert(g_objs.size() == objectsBeforeReject);
        // Commands: recorded in order; adjacency topology and depth-bounds-enable are reported and dropped.
        const U64 adjBefore = vk64BridgeUnsupportedCount("adjacency-topology");
        const U64 boundsBefore = vk64BridgeUnsupportedCount("depth-bounds-test");
        const U64 rangeBefore = vk64BridgeUnsupportedCount("depth-bounds-range");
        resetCmdBuf(dynamicCb);
        vkCmdBeginRendering(dynamicCbId, (void*)1664);
        vkCmdBindPipeline(dynamicCbId, 0, dynPipeId);
        vkCmdSetDepthTestEnable(dynamicCbId, 1);
        vkCmdSetStencilOp(dynamicCbId, 3, 1, 2, 3, 5);
        vkCmdSetPrimitiveTopology(dynamicCbId, 1);
        vkCmdSetPrimitiveTopology(dynamicCbId, 6);          // adjacency: dropped, loud
        vkCmdSetRasterizerDiscardEnable(dynamicCbId, 0);
        vkCmdSetDepthBoundsTestEnable(dynamicCbId, 1);      // depthBounds feature is off: dropped, loud
        vkCmdSetDepthBoundsTestEnable(dynamicCbId, 0);
        vkCmdSetDepthBounds(dynamicCbId, 0.0f, 1.0f);       // the only valid range: quiet
        vkCmdSetDepthBounds(dynamicCbId, 0.25f, 0.75f);     // out of range: loud
        vkCmdDraw(dynamicCbId, 3, 1, 0, 0); vkCmdEndRendering(dynamicCbId);
        assert(vk64BridgeUnsupportedCount("adjacency-topology") == adjBefore + 1);
        assert(vk64BridgeUnsupportedCount("depth-bounds-test") == boundsBefore + 1);
        assert(vk64BridgeUnsupportedCount("depth-bounds-range") == rangeBefore + 1);
        std::vector<std::pair<U64, U64>> recorded;
        for (const auto& c : dynamicCb->cmds) if (c.kind == CMD_RASTER_STATE) recorded.push_back({c.a, c.b});
        const U64 packedOp = 3u | (1u << 4) | (2u << 8) | (3u << 12) | (5u << 16);
        assert((recorded == std::vector<std::pair<U64, U64>>{{3, 1}, {10, packedOp}, {2, 1}, {8, 0}, {11, 0}}));
        serializeSubmitV2({{dynamicCb, dynamicCbId}});
        std::vector<std::pair<U32, U32>> wire;
        bool sawDyn1 = false;
        for (const auto& chunk : g_v2Pending.back().chunks) {
            for (size_t at = 16; at < chunk.size();) {
                U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
                const U8* p = chunk.data() + at + 8;
                if (type == V2_RASTER_STATE) { U32 w, v; memcpy(&w, p, 4); memcpy(&v, p + 4, 4); wire.push_back({w, v}); }
                if (type == V2_PIPELINE && len >= 12) {
                    U32 tag, mask, discard; memcpy(&tag, p + len - 12, 4); memcpy(&mask, p + len - 8, 4); memcpy(&discard, p + len - 4, 4);
                    if (tag == 0x314e5944u) { sawDyn1 = true; assert(mask == dynPipe->dynMask && discard == 0); }
                }
                at += 8 + len;
            }
        }
        assert(sawDyn1);
        assert((wire == std::vector<std::pair<U32, U32>>{{3, 1}, {10, (U32)packedOp}, {2, 1}, {8, 0}, {11, 0}}));
        resetCmdBuf(dynamicCb);
        g_v2Pending.clear();
        puts("PASS: extended dynamic state (DYN1 tail, ordered RASTER_STATE records, unknown dynamic state and adjacency/depth-bounds rejected loudly)");
    }
    {
        // Every format the bridge accepts for images must have a true texel size: fmt_bpp used to
        // fall back silently to 4 bytes (wrong for B4G4R4A4, B5G6R5, A1R5G5B5 and D32S8).
        // Expected sizes come from the Vulkan spec, independent of fmt_bpp's own table.
        const std::map<U32, U32> spec = {
            {2, 2}, {3, 2}, {5, 2}, {7, 2}, {8, 2}, {9, 1}, {10, 1}, {13, 1}, {14, 1}, {16, 2},
            {17, 2}, {20, 2}, {21, 2}, {37, 4}, {38, 4}, {41, 4}, {42, 4}, {43, 4}, {44, 4}, {50, 4},
            {64, 4}, {70, 2}, {74, 2}, {75, 2}, {76, 2}, {77, 4}, {81, 4}, {82, 4}, {83, 4}, {91, 8},
            {95, 8}, {96, 8}, {97, 8}, {98, 4}, {99, 4}, {100, 4}, {101, 8}, {102, 8}, {103, 8},
            {107, 16}, {108, 16}, {109, 16}, {122, 4}, {124, 2}, {125, 4}, {126, 4}, {129, 4}, {130, 8}};
        // PRODUCT FINDING, fixed (fmtbpp lane, 2026-10-28): fmt_bpp used to have no case
        // for 13 integer formats v2FormatIsSupportedImage() accepts (17, 20, 21, 74, 75,
        // 81, 82, 95, 96, 101, 102, 107, 108), so each call landed in the loud default
        // (returned 4, counted "format-bpp") and mis-sized image memory math -- 4 B
        // instead of 16 B for the RGBA32 integer pair. The pins that asserted that
        // fallback while it was live (vk64 harness repair, 2026-10-28) are gone: fmt_bpp
        // now has explicit cases for all 48 non-BC supported formats, so every format
        // below asserts its spec size at full strength and no supported format may
        // count "format-bpp".
        const U64 before = vk64BridgeUnsupportedCount("format-bpp");
        for (U32 f : kV2SupportedTextureFormats) {
            if (v2FormatIsBC(f)) continue;     // block-compressed sizes come from fmt_block
            auto it = spec.find(f);
            assert(it != spec.end());
            assert(fmt_bpp(f) == it->second);
        }
        assert(vk64BridgeUnsupportedCount("format-bpp") == before);
        assert(fmt_block(131).bytes == 8 && fmt_block(133).bytes == 8 && fmt_block(135).bytes == 16);
        const U64 beforeUnknown = vk64BridgeUnsupportedCount("format-bpp");
        assert(fmt_bpp(0x7ffffff0u) == 4 && vk64BridgeUnsupportedCount("format-bpp") == beforeUnknown + 1);   // unknown -> loud
        for (U32 f : kV2SupportedVertexFormats) {
            const U64 vb = vk64BridgeUnsupportedCount("vertex-format-size");
            assert(v2VertexFormatBytes(f) > 0 && v2VertexFormatBytes(f) <= 16);
            assert(vk64BridgeUnsupportedCount("vertex-format-size") == vb);
        }
        puts("PASS: every accepted image/vertex format has a real size; unknown formats are loud");
    }
    {
        // Fixed-size bridge tables never truncate quietly: 8 attachments / 16 vertex bindings fit
        // (the wire limit and the advertised maxVertexInputBindings); anything past that is reported.
        VkAttachmentDescription ad[9] = {};
        memory.memcpyToGuest(6144, ad, sizeof(ad));
        VkRenderPassCreateInfo rpci = {};
        rpci.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO; rpci.pAttachments = 6144;
        for (U32 count : {8u, 9u}) {
            const U64 before = vk64BridgeUnsupportedCount("renderpass-attachments");
            rpci.attachmentCount = count;
            memory.memcpyToGuest(6000, &rpci, sizeof(rpci));
            VK64Args rpa{}; rpa.a[1] = 6000; rpa.a[3] = 6100;
            memory.memcpyToGuest(128, &rpa, sizeof(rpa));
            assert(vk64Bridge(&cpu, VK64_fn_vkCreateRenderPass, 128) == VK_SUCCESS);
            auto* rp = objOf<RenderPass>(memory.readq(6100), K_RENDERPASS, "test");
            assert(rp && rp->natt == 8);
            assert(vk64BridgeUnsupportedCount("renderpass-attachments") == before + (count > 8 ? 1 : 0));
        }
        U64 vbCbId; createObj<CmdBuf>(K_CMDBUF, vbCbId);
        { U8 zeros[64] = {}; memory.memcpyToGuest(6400, zeros, sizeof(zeros)); }
        const U64 vbBefore = vk64BridgeUnsupportedCount("vertex-bindings");
        VK64Args vba{}; vba.a[0] = vbCbId; vba.a[1] = 12; vba.a[2] = 4; vba.a[3] = 6400; vba.a[4] = 6432;
        memory.memcpyToGuest(128, &vba, sizeof(vba));
        vk64Bridge(&cpu, VK64_fn_vkCmdBindVertexBuffers, 128);
        assert(vk64BridgeUnsupportedCount("vertex-bindings") == vbBefore);        // 12..15 fits
        vba.a[1] = 14; memory.memcpyToGuest(128, &vba, sizeof(vba));
        vk64Bridge(&cpu, VK64_fn_vkCmdBindVertexBuffers, 128);
        assert(vk64BridgeUnsupportedCount("vertex-bindings") == vbBefore + 1);    // 14..17 overflows
        vba.a[1] = 16; memory.memcpyToGuest(128, &vba, sizeof(vba));
        vk64Bridge(&cpu, VK64_fn_vkCmdBindVertexBuffers, 128);
        assert(vk64BridgeUnsupportedCount("vertex-bindings") == vbBefore + 2);    // first past the end
        // A command buffer past VK64_MAX_CMDS flags the frame and says so, once per feature.
        U64 ovId; auto* ovCb = createObj<CmdBuf>(K_CMDBUF, ovId);
        g_frameOverflow = false;
        Cmd filler; filler.kind = CMD_DRAW;
        for (U32 i = 0; i < VK64_MAX_CMDS; ++i) pushCmd(ovCb, filler);
        assert(!g_frameOverflow && vk64BridgeUnsupportedCount("frame-overflow") == 0);
        pushCmd(ovCb, filler);
        assert(g_frameOverflow && ovCb->cmds.size() == VK64_MAX_CMDS && vk64BridgeUnsupportedCount("frame-overflow") == 1);
        g_frameOverflow = false;
        puts("PASS: attachment and vertex-binding overflow is reported, 8 attachments / 16 bindings fit");
    }
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
                assert(len == 156 + 8 + 2 * 16); // 28 + 2*20 + 24 + 2*16 + STP1 (8 + 2*12) + CLR1 (8 + 2*16).
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
    assert(offSet->els.size() == 1);
    const Binding& ob = offSet->els.at(DescKey(0, 0));
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
    {
        // Compute pipelines are recorded as real objects (vkd3d-proton
        // creates internal compute pipelines during D3D12 device creation
        // and treats creation failure as device-fatal). The guest shim
        // marshals the standard layout -- create infos in a[3], allocator
        // in a[4] (null here), out array in a[5] -- and the bridge records
        // the stage module, layout, specialization and root census on the
        // object, so dispatches recorded on this ABI are served at submit
        // (section 4.40; the full serving guard is in the descriptor
        // section below). NOTE for the merge: integ's section 4.40
        // (d3d12-32) corrected this same block to the shim packing after
        // fixing the handler's out-slot read; this tree's version is the
        // same packing -- keep integ's text if they differ.
        U64 dev32Id; createObj32<Device>(K_DEVICE, dev32Id);
        vk32abi::VkComputePipelineCreateInfo cci32{};
        cci32.sType = vk32abi::VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        cci32.stage.sType = vk32abi::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        cci32.stage.stage = vk32abi::VK_SHADER_STAGE_COMPUTE_BIT;
        memory32.memcpyToGuest(1024, &cci32, sizeof(cci32));
        memory32.writeq(704, 0xdeadbeefull);
        VK32Args cp{}; cp.a[0] = dev32Id; cp.a[2] = 1; cp.a[3] = 1024; cp.a[5] = 704;
        assert((S64)call32(VK64_fn_vkCreateComputePipelines, cp) == VK_SUCCESS);
        assert(memory32.readq(704) != 0);
    }
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
    // ---- 32-bit bridge parity with the 64-bit one: functions the i386 shim used
    // to stub (vkCmdFillBuffer, vkGetBufferDeviceAddress, ...) and cases the 32-bit
    // switch lacked (vkDestroyEvent, vkGetImageSubresourceLayout, ...).
    {
        auto call32b = [&](U32 fn, const VK32Args& args) {
            memory32.memcpyToGuest(128, &args, sizeof(args));
            return vk32Bridge(&cpu32, fn, 128);
        };
        VK32Args am{}; am.a[1] = 512; am.a[3] = 640;
        allocation32.allocationSize = 4096;
        memory32.memcpyToGuest(512, &allocation32, sizeof(allocation32));
        assert(call32b(VK64_fn_vkAllocateMemory, am) == VK_SUCCESS);
        U64 memId = memory32.readq(640);
        auto* dm = objOf32<DeviceMemory>(memId, K_MEMORY, "test");
        assert(dm && dm->va);
        U64 bufId; auto* buf = createObj32<Buffer>(K_BUFFER, bufId);
        buf->size = 4096; buf->mem = memId; buf->memOff = 0;
        // vkCmdFillBuffer: the pattern lands in guest memory, a 64-bit size is honored.
        VK32Args fill{}; fill.a[1] = bufId; fill.a[2] = 16; fill.a[3] = 32; fill.a[4] = 0xDEADBEEFu;
        assert(call32b(VK64_fn_vkCmdFillBuffer, fill) == 0);
        for (U32 i = 0; i < 8; ++i) assert(memory32.readd((U32)dm->va + 16 + 4 * i) == 0xDEADBEEFu);
        assert(memory32.readd((U32)dm->va + 12) != 0xDEADBEEFu && memory32.readd((U32)dm->va + 48) != 0xDEADBEEFu);
        // vkGetBufferDeviceAddress: the i386 trap returns only EAX, so the full
        // 64-bit address must come back through the out slot in arg 2.
        U64 info32[2] = {0, bufId};                 // sType,pNext | buffer @8
        memory32.memcpyToGuest(1024, info32, sizeof(info32));
        memory32.writeq(1088, 0);
        VK32Args bda{}; bda.a[1] = 1024; bda.a[2] = 1088;
        U64 lowOnly = call32b(VK64_fn_vkGetBufferDeviceAddress, bda);
        U64 full = memory32.readq(1088);
        assert(full == buf->devVa && (full >> 32) != 0 && (U32)lowOnly == (U32)full);
        // vkDestroyEvent / vkDestroyQueryPool drop the object.
        VK32Args ev{}; ev.a[3] = 1152;
        assert(call32b(VK64_fn_vkCreateEvent, ev) == VK_SUCCESS);
        U64 evId = memory32.readq(1152);
        assert(findObj(evId, K_EVENT));
        VK32Args dev{}; dev.a[1] = evId;
        assert(call32b(VK64_fn_vkDestroyEvent, dev) == 0);
        assert(!findObj(evId, K_EVENT));
        // vkGetImageSubresourceLayout: five 8-byte VkDeviceSize fields, guest VA offset.
        U64 imgId; auto* img = createObj32<Image>(K_IMAGE, imgId);
        img->w = 8; img->h = 4; img->format = 37 /* R8G8B8A8_UNORM */; img->mem = memId; img->memOff = 64;
        VK32Args sl{}; sl.a[1] = imgId; sl.a[3] = 1216;
        assert(call32b(VK64_fn_vkGetImageSubresourceLayout, sl) == VK_SUCCESS);
        assert(memory32.readq(1216) == dm->va + 64 && memory32.readq(1224) == 8ULL * 4 * 4 &&
               memory32.readq(1232) == 8ULL * 4);
        // The presentation-support queries are answered, not -8.
        VK32Args ps{};
        assert(call32b(VK64_fn_vkGetPhysicalDeviceWin32PresentationSupportKHR, ps) == VK_TRUE);
        assert(call32b(VK64_fn_vkGetPhysicalDeviceXlibPresentationSupportKHR, ps) == VK_TRUE);
        VK32Args fr{}; fr.a[1] = memId;
        assert(call32b(VK64_fn_vkFreeMemory, fr) == VK_SUCCESS);
        g_mem32 = nullptr; g_mem = &memory;
        puts("PASS: Vulkan32 bridge parity (fill, 64-bit device address, destroy, subresource layout)");
    }
    // ---- Entry points that used to be loud stubs: sizing from a create info,
    // shim-local answers (queue2, granularity, sparse, pipeline cache) and the
    // 64-bit vkDestroyBufferView.
    {
        // vkGetDeviceImageMemoryRequirements: size comes from the create info, and the
        // chained VkMemoryDedicatedRequirements is written (not left as stale bytes).
        VkImageCreateInfo ici{};
        ici.imageType = VK_IMAGE_TYPE_2D; ici.format = (VkFormat)37; // R8G8B8A8_UNORM
        ici.extent = {16, 16, 1}; ici.mipLevels = 1; ici.arrayLayers = 1;
        memory.memcpyToGuest(5100, &ici, sizeof(ici));
        U64 devImgInfo[4] = {0, 0, 5100, 0};       // sType,pNext | pCreateInfo@16
        memory.memcpyToGuest(5000, devImgInfo, sizeof(devImgInfo));
        VkMemoryDedicatedRequirements ded{};
        ded.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS;
        ded.prefersDedicatedAllocation = 0xFFFFFFFFu; ded.requiresDedicatedAllocation = 0xFFFFFFFFu;
        memory.memcpyToGuest(5500, &ded, sizeof(ded));
        VkMemoryRequirements2 mr{};
        mr.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2; mr.pNext = 5500;
        memory.memcpyToGuest(5400, &mr, sizeof(mr));
        vkGetDeviceImageMemoryRequirements(deviceId, (void*)5000, (void*)5400);
        assert(lastTrap == VK64_fn_vkGetDeviceImageMemoryRequirements);
        memory.memcpyFromGuest(&mr, 5400, sizeof(mr));
        assert(mr.memoryRequirements.size == 16 * 16 * 4 && mr.memoryRequirements.alignment == 256 &&
               mr.memoryRequirements.memoryTypeBits != 0);
        memory.memcpyFromGuest(&ded, 5500, sizeof(ded));
        assert(ded.prefersDedicatedAllocation == 0 && ded.requiresDedicatedAllocation == 0);
        // A buffer query chained to a dedicated struct is also written back.
        U64 bufId2; auto* buf2 = createObj<Buffer>(K_BUFFER, bufId2); buf2->size = 1000;
        U64 bufInfo[3] = {0, 0, bufId2};
        memory.memcpyToGuest(5200, bufInfo, sizeof(bufInfo));
        ded.prefersDedicatedAllocation = 0xFFFFFFFFu; ded.requiresDedicatedAllocation = 0xFFFFFFFFu;
        memory.memcpyToGuest(5500, &ded, sizeof(ded));
        vkGetBufferMemoryRequirements2KHR(deviceId, (void*)5200, (void*)5400);
        memory.memcpyFromGuest(&mr, 5400, sizeof(mr));
        memory.memcpyFromGuest(&ded, 5500, sizeof(ded));
        assert(mr.memoryRequirements.size == 1024 && ded.prefersDedicatedAllocation == 0 && ded.requiresDedicatedAllocation == 0);
        // vkGetDeviceQueue2: flags == 0 maps to vkGetDeviceQueue, a protected queue does not exist.
        U32 qinfo[8] = {0, 0, 0, 0, /*flags*/ 0, /*family*/ 0, /*index*/ 0, 0};
        objOf<Device>(deviceId, K_DEVICE, "test")->queue = 0x1234;
        memory.writeq(2048, 0xDEADull);
        vkGetDeviceQueue2(deviceId, qinfo, (U64*)2048);
        assert(memory.readq(2048) == 0x1234);
        qinfo[4] = 1;                                   // VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT
        U64 protectedQueue = 0xDEADull;
        vkGetDeviceQueue2(deviceId, qinfo, &protectedQueue);
        assert(protectedQueue == 0);
        U32 gran[2] = {7, 7}; vkGetRenderAreaGranularity(deviceId, 1, gran);
        assert(gran[0] == 1 && gran[1] == 1);
        U32 sparse = 9; vkGetImageSparseMemoryRequirements(deviceId, 1, &sparse, nullptr);
        assert(sparse == 0);
        sparse = 9; vkGetDeviceImageSparseMemoryRequirements(deviceId, nullptr, &sparse, nullptr);
        assert(sparse == 0);
        U64 committed = 9; vkGetDeviceMemoryCommitment(deviceId, 1, &committed);
        assert(committed == 0);
        U32 peer = 9; vkGetDeviceGroupPeerMemoryFeatures(deviceId, 0, 0, 1, &peer);
        assert(peer == 0);
        size_t cacheSize = 77;
        assert(vkGetPipelineCacheData(deviceId, 1, &cacheSize, nullptr) == VK_SUCCESS && cacheSize == 0);
        assert(vkMergePipelineCaches(deviceId, 1, 0, nullptr) == VK_SUCCESS);
        // 64-bit vkDestroyBufferView drops the view handle (was a -8 stub that leaked it).
        U64 viewId; createObj<BufferView>(K_BUFFERVIEW, viewId);
        assert(findObj(viewId, K_BUFFERVIEW));
        vkDestroyBufferView(deviceId, viewId, nullptr);
        assert(lastTrap == VK64_fn_vkDestroyBuffer && !findObj(viewId, K_BUFFERVIEW));
        // 32-bit twin of the device-image sizing query.
        vk32abi::VkImageCreateInfo ici32{}; ici32.imageType = vk32abi::VK_IMAGE_TYPE_2D;
        ici32.format = (vk32abi::VkFormat)37; ici32.extent = {8, 8, 1}; ici32.mipLevels = 1; ici32.arrayLayers = 1;
        memory32.memcpyToGuest(2200, &ici32, sizeof(ici32));
        U32 info32b[4] = {0, 0, 2200, 0};
        memory32.memcpyToGuest(2100, info32b, sizeof(info32b));
        VK32Args dr{}; dr.a[1] = 2100; dr.a[2] = 2400;
        assert(vk32Bridge(&cpu32, VK64_fn_vkGetDeviceImageMemoryRequirements, (memory32.memcpyToGuest(128, &dr, sizeof(dr)), 128)) == VK_SUCCESS);
        assert(memory32.readq(2408) == 8 * 8 * 4);
        // 32-bit extended dynamic state: command records and the pipeline-create dynamic list.
        {
            U64 cb32Id; auto* cb32 = createObj32<CmdBuf>(K_CMDBUF, cb32Id);
            VK32Args ed{}; ed.a[0] = cb32Id; ed.a[1] = 3; ed.a[2] = 1;               // depth test enable
            assert(vk32Bridge(&cpu32, VK64_fn_vkCmdSetExtendedDynamicState, (memory32.memcpyToGuest(128, &ed, sizeof(ed)), 128)) == 0);
            ed.a[1] = 2; ed.a[2] = 6;                                                // adjacency topology: dropped, loud
            const U64 adj32 = vk64BridgeUnsupportedCount("adjacency-topology");
            assert(vk32Bridge(&cpu32, VK64_fn_vkCmdSetExtendedDynamicState, (memory32.memcpyToGuest(128, &ed, sizeof(ed)), 128)) == 0);
            assert(vk64BridgeUnsupportedCount("adjacency-topology") == adj32 + 1);
            ed.a[1] = 99; ed.a[2] = 0;                                                // unknown id: dropped, loud
            assert(vk32Bridge(&cpu32, VK64_fn_vkCmdSetExtendedDynamicState, (memory32.memcpyToGuest(128, &ed, sizeof(ed)), 128)) == 0);
            assert(vk64BridgeUnsupportedCount("dynamic-state-command") >= 1);
            assert(cb32->cmds.size() == 1 && cb32->cmds[0].kind == CMD_RASTER_STATE && cb32->cmds[0].a == 3 && cb32->cmds[0].b == 1);
            U64 dev32Id; createObj32<Device>(K_DEVICE, dev32Id);
            U32 st32[2] = {1000267008u, 1000377004u};                                 // depth compare op, primitive restart enable
            memory32.memcpyToGuest(3000, st32, sizeof(st32));
            vk32abi::VkPipelineDynamicStateCreateInfo di32{}; di32.dynamicStateCount = 2; di32.pDynamicStates = 3000;
            memory32.memcpyToGuest(3040, &di32, sizeof(di32));
            vk32abi::VkGraphicsPipelineCreateInfo gp32{}; gp32.pDynamicState = 3040;
            memory32.memcpyToGuest(3200, &gp32, sizeof(gp32));
            VK32Args cp{}; cp.a[0] = dev32Id; cp.a[2] = 1; cp.a[3] = 3200; cp.a[5] = 3400;
            assert(vk32Bridge(&cpu32, VK64_fn_vkCreateGraphicsPipelines, (memory32.memcpyToGuest(128, &cp, sizeof(cp)), 128)) == VK_SUCCESS);
            auto* p32 = objOf32<Pipeline>(memory32.readq(3400), K_PIPELINE, "test");
            assert(p32 && p32->dynMask == ((1u << VK64_DYN_DEPTH_OP) | (1u << VK64_DYN_PRIMITIVE_RESTART)));
            st32[1] = 1000377003u + 100;                                              // unknown state: rejected
            memory32.memcpyToGuest(3000, st32, sizeof(st32));
            assert((S64)(S32)vk32Bridge(&cpu32, VK64_fn_vkCreateGraphicsPipelines, (memory32.memcpyToGuest(128, &cp, sizeof(cp)), 128)) == VK_ERROR_FEATURE_NOT_PRESENT);
            assert(memory32.readq(3400) == 0);
        }
        g_mem32 = nullptr; g_mem = &memory;
        puts("PASS: former stubs (device image sizing, queue2, granularity, sparse, pipeline cache, buffer-view destroy)");
    }
    // ---- Descriptor model: arrays, dynamic offsets, immutable samplers, copies,
    // pools, push descriptors, 32-bit strides. Each assertion fails if the
    // corresponding behaviour regresses to the old 8-binding / element-0 model.
    {
        std::set<U64> idsBefore; for (auto& kv : g_objs) idsBefore.insert(kv.first);
        struct WireEl { U32 binding, type, elem, count; U8 kind; U64 obj, len, off; };
        struct WireSet { U32 setIndex, nWire; U64 layout; std::vector<WireEl> els; };
        auto runCb = [&](CmdBuf* cb, U64 cbId) {
            g_v2Pending.clear();
            serializeSubmitV2({{cb, cbId}});
            std::vector<WireSet> out;
            assert(!g_v2Pending.empty());
            for (const auto& chunk : g_v2Pending.back().chunks) {
                for (size_t at = 16; at < chunk.size();) {
                    U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
                    if (type == V2_DESC_SET) {
                        const U8* p = chunk.data() + at + 8;
                        WireSet ws; ws.setIndex = p[0]; uint16_t n; memcpy(&n, p + 2, 2); ws.nWire = n;
                        memcpy(&ws.layout, p + 4, 8);
                        p += 12;
                        for (U32 i = 0; i < n; i++, p += 44) {
                            WireEl e; memcpy(&e.binding, p, 4); memcpy(&e.type, p + 4, 4);
                            memcpy(&e.elem, p + 8, 4); memcpy(&e.count, p + 12, 4); e.kind = p[16];
                            memcpy(&e.obj, p + 20, 8); memcpy(&e.len, p + 28, 8); memcpy(&e.off, p + 36, 8);
                            ws.els.push_back(e);
                        }
                        out.push_back(ws);
                    }
                    at += 8 + len;
                }
            }
            return out;
        };
        auto drawWith = [&](U64 cbId, U64 setId, U32 ndyn, const U32* dyn) {
            memory.memcpyToGuest(1664, rendering, sizeof(rendering));
            vkCmdBeginRendering(cbId, (void*)1664);
            memory.writeq(1792, setId);
            if (ndyn) memory.memcpyToGuest(1824, dyn, ndyn * 4);
            vkCmdBindDescriptorSets(cbId, 0, 0, 0, 1, (U64*)1792, ndyn, ndyn ? (const U32*)1824 : nullptr);
            vkCmdDraw(cbId, 3, 1, 0, 0); vkCmdEndRendering(cbId);
        };
        auto mkLayout = [&](std::initializer_list<std::pair<U32, LayoutBinding>> bs, U64& id) {
            auto* l = createObj<DescSetLayout>(K_SETLAYOUT, id);
            for (auto& b : bs) l->binds[b.first] = b.second;
            finishDescLayout(*l);
            return l;
        };
        auto lb = [](U32 type, U32 count) { LayoutBinding b; b.type = type; b.count = count; return b; };

        // 1. A 16-binding layout is not truncated, and an array write lands on its own elements.
        U64 lay16Id; auto* lay16 = mkLayout({}, lay16Id);
        for (U32 i = 0; i < 16; i++) lay16->binds[i] = lb(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1);
        lay16->binds[20] = lb(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4);
        finishDescLayout(*lay16);
        U64 setId; auto* set = createObj<DescSet>(K_SET, setId); set->layout = lay16Id;
        U64 smpId; createObj<Sampler>(K_SAMPLER, smpId);
        U64 v[4];
        for (int i = 0; i < 4; i++) { createObj<ImageView>(K_IMAGEVIEW, v[i]); }
        VkDescriptorImageInfo ii[3] = {};
        for (int i = 0; i < 3; i++) { ii[i].sampler = smpId; ii[i].imageView = v[i + 1]; ii[i].imageLayout = (VkImageLayout)5; }
        memory.memcpyToGuest(4096, ii, sizeof(ii));
        VkWriteDescriptorSet w{}; w.dstSet = setId; w.dstBinding = 20; w.dstArrayElement = 1; w.descriptorCount = 3;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w.pImageInfo = 4096;
        memory.memcpyToGuest(4224, &w, sizeof(w));
        vkUpdateDescriptorSets(deviceId, 1, (void*)4224, 0, nullptr);
        assert(set->els.size() == 3);
        assert(!set->els.count(DescKey(20, 0)));
        for (U32 i = 0; i < 3; i++) {
            const Binding& e = set->els.at(DescKey(20, 1 + i));
            assert(e.obj == v[i + 1] && e.range == smpId && e.dstArrayElement == 1 + i && e.count == 1);
        }
        U64 cbId; auto* cb = createObj<CmdBuf>(K_CMDBUF, cbId);
        drawWith(cbId, setId, 0, nullptr);
        auto ws = runCb(cb, cbId);
        assert(ws.size() == 1 && ws[0].nWire == 3 && ws[0].layout == lay16Id);
        for (U32 i = 0; i < 3; i++) {
            assert(ws[0].els[i].binding == 20 && ws[0].els[i].elem == 1 + i && ws[0].els[i].kind == 2 &&
                   ws[0].els[i].obj == v[i + 1] && ws[0].els[i].type == VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
        }
        puts("PASS: descriptor arrays keep per-element bindings and >8-binding layouts");

        // 2. Dynamic offsets: consumed in binding order then element order; added to the buffer offset.
        U64 dynLayId; mkLayout({{0, lb(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1)},
                                {1, lb(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1)},
                                {2, lb(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, 2)}}, dynLayId);
        assert(objOf<DescSetLayout>(dynLayId, K_SETLAYOUT, "t")->dynCount == 3);
        assert(objOf<DescSetLayout>(dynLayId, K_SETLAYOUT, "t")->binds.at(2).dynBase == 1);
        U64 dsId; auto* ds = createObj<DescSet>(K_SET, dsId); ds->layout = dynLayId;
        VkDescriptorBufferInfo bi[2] = {}; bi[0].buffer = offBufId; bi[0].offset = 16; bi[0].range = 8;
        bi[1].buffer = offBufId; bi[1].offset = 0; bi[1].range = VK_WHOLE_SIZE;
        memory.memcpyToGuest(4352, bi, sizeof(bi));
        VkWriteDescriptorSet dw{}; dw.dstSet = dsId; dw.dstBinding = 0; dw.descriptorCount = 1;
        dw.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; dw.pBufferInfo = 4352;
        VkWriteDescriptorSet dw2 = dw; dw2.dstBinding = 2; dw2.descriptorCount = 2;
        dw2.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
        VkWriteDescriptorSet dws[2] = {dw, dw2};
        memory.memcpyToGuest(4480, dws, sizeof(dws));
        vkUpdateDescriptorSets(deviceId, 2, (void*)4480, 0, nullptr);
        U64 cb2Id; auto* cb2 = createObj<CmdBuf>(K_CMDBUF, cb2Id);
        const U32 dyn[3] = {32, 64, 128};
        drawWith(cb2Id, dsId, 3, dyn);
        assert(cb2->cmds.size() >= 2);
        ws = runCb(cb2, cb2Id);
        assert(ws.size() == 1 && ws[0].els.size() == 3);
        const U64 base = offBuf->memOff;
        assert(ws[0].els[0].binding == 0 && ws[0].els[0].type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER &&
               ws[0].els[0].kind == 1 && ws[0].els[0].off == base + 16 + 32 && ws[0].els[0].len == 8);
        assert(ws[0].els[1].binding == 2 && ws[0].els[1].elem == 0 && ws[0].els[1].type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER &&
               ws[0].els[1].off == base + 16 + 64);        // dynBase 1 -> second offset
        assert(ws[0].els[2].elem == 1 && ws[0].els[2].off == base + 0 + 128 && ws[0].els[2].len == 256 - 128); // WHOLE_SIZE from dyn offset
        // Fewer offsets than dynamic descriptors is reported, not silently zeroed.
        const U32 missing0 = vk64BridgeUnsupportedCount("dynamic-offset-missing");
        U64 cb3Id; auto* cb3 = createObj<CmdBuf>(K_CMDBUF, cb3Id);
        drawWith(cb3Id, dsId, 1, dyn);
        runCb(cb3, cb3Id);
        assert(vk64BridgeUnsupportedCount("dynamic-offset-missing") > missing0);
        puts("PASS: dynamic offsets are applied in layout order (UBO/SSBO dynamic, WHOLE_SIZE)");

        // 3. Immutable samplers are the descriptor, even when the app writes no sampler.
        U64 immSmp; createObj<Sampler>(K_SAMPLER, immSmp);
        U64 immLayId; auto* immLay = mkLayout({{0, lb(VK_DESCRIPTOR_TYPE_SAMPLER, 2)}}, immLayId);
        immLay->binds[0].immutable = {immSmp, immSmp + 0};
        U64 immSetId; auto* immSet = createObj<DescSet>(K_SET, immSetId); immSet->layout = immLayId;
        U64 cb4Id; auto* cb4 = createObj<CmdBuf>(K_CMDBUF, cb4Id);
        drawWith(cb4Id, immSetId, 0, nullptr);
        ws = runCb(cb4, cb4Id);
        assert(ws.size() == 1 && ws[0].els.size() == 2 && ws[0].els[0].kind == 3 && ws[0].els[0].obj == immSmp &&
               ws[0].els[1].elem == 1);
        puts("PASS: immutable samplers populate the set without app writes");

        // 4. Sampler-only writes keep the sampler where the emitter reads it.
        U64 sOnlyLay; mkLayout({{0, lb(VK_DESCRIPTOR_TYPE_SAMPLER, 1)}}, sOnlyLay);
        U64 sOnlyId; auto* sOnly = createObj<DescSet>(K_SET, sOnlyId); sOnly->layout = sOnlyLay;
        VkDescriptorImageInfo si{}; si.sampler = smpId;
        memory.memcpyToGuest(4608, &si, sizeof(si));
        VkWriteDescriptorSet sw{}; sw.dstSet = sOnlyId; sw.descriptorCount = 1;
        sw.descriptorType = VK_DESCRIPTOR_TYPE_SAMPLER; sw.pImageInfo = 4608;
        memory.memcpyToGuest(4736, &sw, sizeof(sw));
        vkUpdateDescriptorSets(deviceId, 1, (void*)4736, 0, nullptr);
        assert(sOnly->els.at(DescKey(0, 0)).obj == smpId);
        puts("PASS: sampler-only descriptor writes are emitted with the sampler");

        // 5. Copies move elements (and holes) between sets and within a set.
        U64 dstSetId; auto* dstSet = createObj<DescSet>(K_SET, dstSetId); dstSet->layout = lay16Id;
        dstSet->els[DescKey(20, 0)].binding = 20;            // a stale element that the copy must clear
        VkCopyDescriptorSet cp{}; cp.srcSet = setId; cp.srcBinding = 20; cp.srcArrayElement = 0;
        cp.dstSet = dstSetId; cp.dstBinding = 20; cp.dstArrayElement = 0; cp.descriptorCount = 4;
        memory.memcpyToGuest(4864, &cp, sizeof(cp));
        vkUpdateDescriptorSets(deviceId, 0, nullptr, 1, (void*)4864);
        assert(!dstSet->els.count(DescKey(20, 0)));       // src element 0 was never written
        for (U32 i = 1; i < 4; i++) assert(dstSet->els.at(DescKey(20, i)).obj == v[i]);
        assert(!dstSet->els.count(DescKey(20, 4)));
        puts("PASS: vkUpdateDescriptorSets copies are applied");

        // 6. Pools: maxSets, free, reset.
        auto call64d = [&](U32 fn, U64 a0, U64 a1, U64 a2, U64 a3) -> U64 {
            VK64Args a{}; a.a[0] = a0; a.a[1] = a1; a.a[2] = a2; a.a[3] = a3;
            memory.memcpyToGuest(128, &a, sizeof(a));
            return vk64Bridge(&cpu, fn, 128);
        };
        U64 poolId; auto* pool = createObj<DescPool>(K_DESCPOOL, poolId); pool->maxSets = 2;
        VkDescriptorSetAllocateInfo ai{}; ai.descriptorPool = poolId; ai.descriptorSetCount = 2; ai.pSetLayouts = 5120;
        memory.writeq(5120, lay16Id); memory.writeq(5128, lay16Id);
        memory.memcpyToGuest(5200, &ai, sizeof(ai));
        assert(call64d(VK64_fn_vkAllocateDescriptorSets, deviceId, 5200, 5300, 0) == VK_SUCCESS);
        const U64 s0 = memory.readq(5300), s1 = memory.readq(5308);
        assert(s0 && s1 && s0 != s1 && pool->sets.size() == 2);
        ai.descriptorSetCount = 1; memory.memcpyToGuest(5200, &ai, sizeof(ai));
        assert((S64)call64d(VK64_fn_vkAllocateDescriptorSets, deviceId, 5200, 5400, 0) == VK_ERROR_OUT_OF_POOL_MEMORY);
        assert(memory.readq(5400) == 0);
        assert(call64d(VK64_fn_vkFreeDescriptorSets, deviceId, poolId, 1, 5300) == VK_SUCCESS);
        assert(pool->sets.size() == 1 && !g_objs.count(s0) && g_objs.count(s1));
        assert(call64d(VK64_fn_vkAllocateDescriptorSets, deviceId, 5200, 5400, 0) == VK_SUCCESS);
        assert(call64d(VK64_fn_vkResetDescriptorPool, deviceId, poolId, 0, 0) == VK_SUCCESS);
        assert(pool->sets.empty() && !g_objs.count(s1) && !g_objs.count(memory.readq(5400)));
        puts("PASS: descriptor pools enforce maxSets and free/reset release sets");

        // 7. Push descriptors write a command-buffer-owned set that overlays the previous push.
        U64 plId; auto* pl = createObj<PipelineLayout>(K_PIPELAYOUT, plId);
        pl->nsets = 1; pl->sets[0] = lay16Id;
        U64 cb5Id; auto* cb5 = createObj<CmdBuf>(K_CMDBUF, cb5Id);
        memory.memcpyToGuest(1664, rendering, sizeof(rendering));
        vkCmdBeginRendering(cb5Id, (void*)1664);
        VkWriteDescriptorSet pw{}; pw.dstBinding = 20; pw.dstArrayElement = 2; pw.descriptorCount = 1;
        pw.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; pw.pImageInfo = 4096;
        memory.memcpyToGuest(5504, &pw, sizeof(pw));
        {
            VK64Args a{}; a.a[0] = cb5Id; a.a[1] = 0; a.a[2] = plId; a.a[3] = 0; a.a[4] = 1; a.a[5] = 5504;
            memory.memcpyToGuest(128, &a, sizeof(a));
            vk64Bridge(&cpu, VK64_fn_vkCmdPushDescriptorSetKHR, 128);
        }
        vkCmdDraw(cb5Id, 3, 1, 0, 0); vkCmdEndRendering(cb5Id);
        ws = runCb(cb5, cb5Id);
        bool sawPush = false;
        for (auto& s : ws) for (auto& e : s.els) if (e.binding == 20 && e.elem == 2 && e.obj == v[1]) sawPush = true;
        assert(sawPush);
        puts("PASS: vkCmdPushDescriptorSetKHR records a bound descriptor set");

        // 8. 32-bit guests: VkDescriptorImageInfo is 20 bytes (u64 aligned to 4).
        assert(descWriteStride(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, true) == 20);
        assert(descWriteStride(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, false) == 24);
        assert(descWriteStride(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, true) == 24);
        {
            // Real i386 call: three 20-byte image infos must each land on their own element.
            U64 s32Id; auto* s32 = createObj32<DescSet>(K_SET, s32Id); s32->layout = lay16Id;
            U8 infos[60] = {};
            for (int i = 0; i < 3; i++) {
                U64 smp = smpId, view = v[i];
                memcpy(infos + i * 20, &smp, 8); memcpy(infos + i * 20 + 8, &view, 8);
            }
            memory32.memcpyToGuest(2048, infos, sizeof(infos));
            vk32abi::VkWriteDescriptorSet w32{}; w32.dstSet = s32Id; w32.dstBinding = 20; w32.descriptorCount = 3;
            w32.descriptorType = (vk32abi::VkDescriptorType)VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w32.pImageInfo = 2048;
            memory32.memcpyToGuest(2176, &w32, sizeof(w32));
            VK32Args u32{}; u32.a[1] = 1; u32.a[2] = 2176;
            memory32.memcpyToGuest(128, &u32, sizeof(u32));
            vk32Bridge(&cpu32, VK64_fn_vkUpdateDescriptorSets, 128);
            g_mem32 = nullptr; g_mem = &memory;
            assert(s32->els.size() == 3);
            for (U32 i = 0; i < 3; i++) assert(s32->els.at(DescKey(20, i)).obj == v[i] && s32->els.at(DescKey(20, i)).range == smpId);
        }
        // 9. 32-bit compute serving (section 4.40): the full guest path --
        // shader module, compute pipeline, descriptor set holding a storage
        // buffer, COMPUTE binds, dispatch, and a copy out of the written
        // range -- is recorded on this ABI and served by the shared V2
        // compute records, exactly like the 64-bit path. Before this lane
        // the pipeline recorded no stage, both descriptor bind gates
        // dropped COMPUTE binds loudly ("compute-descriptor-sets"),
        // dispatches were never write-marked, and buffer copies never
        // deferred: every 32-bit dispatch died at submit as
        // "compute-dispatch" and readbacks would have served stale bytes.
        {
            U64 dev32Id; createObj32<Device>(K_DEVICE, dev32Id);
            // One 256 KiB allocation; the source buffer is suballocated at
            // memOff 8192 (NONZERO -- a write mark keyed buffer-relative
            // instead of memory-absolute passes memOff 0 and fails here),
            // the destination at 128 KiB.
            vk32abi::VkMemoryAllocateInfo ai32{};
            ai32.sType = vk32abi::VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
            ai32.allocationSize = 256 * 1024;
            memory32.memcpyToGuest(600000, &ai32, sizeof(ai32));
            memory32.writeq(600064, 0);
            { VK32Args a{}; a.a[0] = dev32Id; a.a[1] = 600000; a.a[3] = 600064;
              assert((S64)call32(VK64_fn_vkAllocateMemory, a) == VK_SUCCESS); }
            const U64 mem32Id = memory32.readq(600064);
            auto* mem32o = objOf32<DeviceMemory>(mem32Id, K_MEMORY, "test");
            assert(mem32o && mem32o->va);
            const U64 memVa = mem32o->va;
            auto mkBuf32 = [&](U64 size, U64 memOff) {
                vk32abi::VkBufferCreateInfo bci{};
                bci.sType = vk32abi::VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
                bci.size = size;
                bci.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
                memory32.memcpyToGuest(600128, &bci, sizeof(bci));
                memory32.writeq(600192, 0);
                VK32Args a{}; a.a[0] = dev32Id; a.a[1] = 600128; a.a[3] = 600192;
                assert((S64)call32(VK64_fn_vkCreateBuffer, a) == VK_SUCCESS);
                U64 id = memory32.readq(600192);
                assert(id != 0);
                VK32Args b{}; b.a[0] = dev32Id; b.a[1] = id; b.a[2] = mem32Id; b.a[3] = memOff;
                assert((S64)call32(VK64_fn_vkBindBufferMemory, b) == VK_SUCCESS);
                return id;
            };
            const U64 srcBuf32 = mkBuf32(4096, 8192);
            const U64 dstBuf32 = mkBuf32(4096, 128 * 1024);
            // Compute shader module: the same minimal GLCompute module the
            // 64-bit served-dispatch guard uses.
            const U32 csWords[] = {
                0x07230203u, 0x00010500u, 0, 64, 0,
                (2u << 16) | 17u, 1u,
                (3u << 16) | 14u, 0u, 1u,
                (5u << 16) | 15u, 5u, 6u, 0x6e69616du, 0u,
                (6u << 16) | 16u, 6u, 17u, 1u, 1u, 1u,
                (2u << 16) | 19u, 1u,
                (3u << 16) | 33u, 2u, 1u,
                (5u << 16) | 54u, 1u, 6u, 0u, 2u,
                (2u << 16) | 248u, 7u,
                (1u << 16) | 253u,
                (1u << 16) | 56u,
            };
            memory32.memcpyToGuest(600256, csWords, sizeof(csWords));
            vk32abi::VkShaderModuleCreateInfo smci32{};
            smci32.sType = vk32abi::VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smci32.codeSize = sizeof(csWords); smci32.pCode = 600256;
            memory32.memcpyToGuest(600512, &smci32, sizeof(smci32));
            memory32.writeq(600576, 0);
            { VK32Args a{}; a.a[0] = dev32Id; a.a[1] = 600512; a.a[3] = 600576;
              assert((S64)call32(VK64_fn_vkCreateShaderModule, a) == VK_SUCCESS); }
            const U64 csMod32 = memory32.readq(600576);
            assert(csMod32 != 0);
            // Descriptor set layout: one storage buffer at binding 0.
            vk32abi::VkDescriptorSetLayoutBinding lb32{};
            lb32.binding = 0; lb32.descriptorType = (vk32abi::VkDescriptorType)VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            lb32.descriptorCount = 1; lb32.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
            memory32.memcpyToGuest(600640, &lb32, sizeof(lb32));
            vk32abi::VkDescriptorSetLayoutCreateInfo lci32{};
            lci32.sType = vk32abi::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
            lci32.bindingCount = 1; lci32.pBindings = 600640;
            memory32.memcpyToGuest(600704, &lci32, sizeof(lci32));
            memory32.writeq(600768, 0);
            { VK32Args a{}; a.a[0] = dev32Id; a.a[1] = 600704; a.a[3] = 600768;
              assert((S64)call32(VK64_fn_vkCreateDescriptorSetLayout, a) == VK_SUCCESS); }
            const U64 lay32 = memory32.readq(600768);
            assert(lay32 != 0);
            // Pipeline layout over that set layout.
            memory32.writeq(600832, lay32);
            vk32abi::VkPipelineLayoutCreateInfo plci32{};
            plci32.sType = vk32abi::VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
            plci32.setLayoutCount = 1; plci32.pSetLayouts = 600832;
            memory32.memcpyToGuest(600896, &plci32, sizeof(plci32));
            memory32.writeq(600960, 0);
            { VK32Args a{}; a.a[0] = dev32Id; a.a[1] = 600896; a.a[3] = 600960;
              assert((S64)call32(VK64_fn_vkCreatePipelineLayout, a) == VK_SUCCESS); }
            const U64 pl32 = memory32.readq(600960);
            assert(pl32 != 0);
            // Compute pipeline, packed per the guest shim (allocator slot
            // a[4] left null, out array in a[5]): the stage module and
            // layout land on the recorded object.
            memory32.memcpyToGuest(601024, "main", 5);
            vk32abi::VkComputePipelineCreateInfo cci32{};
            cci32.sType = vk32abi::VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cci32.stage.sType = vk32abi::VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cci32.stage.stage = vk32abi::VK_SHADER_STAGE_COMPUTE_BIT;
            cci32.stage.module = csMod32;
            cci32.stage.pName = 601024;
            cci32.layout = pl32;
            memory32.memcpyToGuest(601088, &cci32, sizeof(cci32));
            memory32.writeq(601216, 0);
            { VK32Args a{}; a.a[0] = dev32Id; a.a[2] = 1; a.a[3] = 601088; a.a[5] = 601216;
              assert((S64)call32(VK64_fn_vkCreateComputePipelines, a) == VK_SUCCESS); }
            const U64 pipe32 = memory32.readq(601216);
            assert(pipe32 != 0);
            auto* pipe32o = objOf<Pipeline>(pipe32, K_PIPELINE, "test");
            assert(pipe32o && pipe32o->cs == csMod32 && pipe32o->layout == pl32);
            // Descriptor pool + set, updated to srcBuf32 range [128, 512).
            vk32abi::VkDescriptorPoolSize psz32{};
            psz32.type = (vk32abi::VkDescriptorType)VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; psz32.descriptorCount = 1;
            memory32.memcpyToGuest(601280, &psz32, sizeof(psz32));
            vk32abi::VkDescriptorPoolCreateInfo pci32{};
            pci32.sType = vk32abi::VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
            pci32.maxSets = 1; pci32.poolSizeCount = 1; pci32.pPoolSizes = 601280;
            memory32.memcpyToGuest(601344, &pci32, sizeof(pci32));
            memory32.writeq(601408, 0);
            { VK32Args a{}; a.a[0] = dev32Id; a.a[1] = 601344; a.a[3] = 601408;
              assert((S64)call32(VK64_fn_vkCreateDescriptorPool, a) == VK_SUCCESS); }
            const U64 pool32 = memory32.readq(601408);
            assert(pool32 != 0);
            memory32.writeq(601472, lay32);
            vk32abi::VkDescriptorSetAllocateInfo sai32{};
            sai32.sType = vk32abi::VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            sai32.descriptorPool = (U32)pool32; sai32.descriptorSetCount = 1; sai32.pSetLayouts = 601472;
            memory32.memcpyToGuest(601536, &sai32, sizeof(sai32));
            memory32.writeq(601600, 0);
            { VK32Args a{}; a.a[0] = dev32Id; a.a[1] = 601536; a.a[2] = 601600;
              assert((S64)call32(VK64_fn_vkAllocateDescriptorSets, a) == VK_SUCCESS); }
            const U64 set32 = memory32.readq(601600);
            assert(set32 != 0);
            vk32abi::VkDescriptorBufferInfo dbi32{};
            dbi32.buffer = srcBuf32; dbi32.offset = 128; dbi32.range = 512;
            memory32.memcpyToGuest(601664, &dbi32, sizeof(dbi32));
            vk32abi::VkWriteDescriptorSet w32{};
            w32.dstSet = set32; w32.dstBinding = 0; w32.descriptorCount = 1;
            w32.descriptorType = (vk32abi::VkDescriptorType)VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            w32.pBufferInfo = 601664;
            memory32.memcpyToGuest(601728, &w32, sizeof(w32));
            { VK32Args a{}; a.a[0] = dev32Id; a.a[1] = 1; a.a[2] = 601728;
              call32(VK64_fn_vkUpdateDescriptorSets, a); }
            auto* set32o = objOf<DescSet>(set32, K_SET, "test");
            assert(set32o && set32o->els.size() == 1);
            // Record: bind pipeline + set at the COMPUTE bind point,
            // dispatch, then copy out of the written range.
            U64 cb32Id; auto* cb32 = createObj32<CmdBuf>(K_CMDBUF, cb32Id);
            { VK32Args a{}; a.a[0] = cb32Id; a.a[1] = VK_PIPELINE_BIND_POINT_COMPUTE; a.a[2] = pipe32;
              call32(VK64_fn_vkCmdBindPipeline, a); }
            memory32.writeq(601792, set32);
            const U32 dropBefore = vk64BridgeUnsupportedCount("compute-descriptor-sets");
            const U32 bindDropBefore = vk64BridgeUnsupportedCount("descriptor-bind-point");
            { VK32Args a{}; a.a[0] = cb32Id; a.a[1] = VK_PIPELINE_BIND_POINT_COMPUTE; a.a[2] = pl32;
              a.a[3] = 0; a.a[4] = 1; a.a[5] = 601792;
              call32(VK64_fn_vkCmdBindDescriptorSets, a); }
            assert(vk64BridgeUnsupportedCount("compute-descriptor-sets") == dropBefore);
            assert(vk64BridgeUnsupportedCount("descriptor-bind-point") == bindDropBefore);
            bool sawBindSets = false;
            for (const Cmd& k : cb32->cmds) if (k.kind == CMD_BIND_SETS) sawBindSets = true;
            assert(sawBindSets);
            // Push descriptors at the COMPUTE bind point record too (the
            // meta-bind path): the gate change covers both bind entry
            // points, not just vkCmdBindDescriptorSets.
            vk32abi::VkWriteDescriptorSet pw32{};
            pw32.dstBinding = 0; pw32.descriptorCount = 1;
            pw32.descriptorType = (vk32abi::VkDescriptorType)VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            pw32.pBufferInfo = 601664;
            memory32.memcpyToGuest(601920, &pw32, sizeof(pw32));
            { VK32Args a{}; a.a[0] = cb32Id; a.a[1] = VK_PIPELINE_BIND_POINT_COMPUTE; a.a[2] = pl32;
              a.a[3] = 0; a.a[4] = 1; a.a[5] = 601920;
              call32(VK64_fn_vkCmdPushDescriptorSetKHR, a); }
            assert(vk64BridgeUnsupportedCount("descriptor-bind-point") == bindDropBefore);
            assert(cb32->pushSets[0] != 0);
            auto* pushSet32 = objOf<DescSet>(cb32->pushSets[0], K_SET, "test");
            assert(pushSet32 && pushSet32->els.count(DescKey(0, 0)) &&
                   pushSet32->els.at(DescKey(0, 0)).obj == srcBuf32);
            { VK32Args a{}; a.a[0] = cb32Id; a.a[1] = 2; a.a[2] = 3; a.a[3] = 4;
              call32(VK64_fn_vkCmdDispatch, a); }
            // The dispatch marked the storage range memory-absolute:
            // memOff 8192 + bufOff 128, exactly 512 bytes; the relative
            // range carries no mark.
            assert(vk64FindGpuWrite(srcBuf32, 8192 + 128, 512) != nullptr);
            assert(vk64FindGpuWrite(srcBuf32, 128, 512) == nullptr);
            // Sentinels: the destination is poisoned; a deferred copy
            // leaves it untouched at record time.
            for (U32 i = 0; i < 512; i++) memory32.bytes[memVa + 128 * 1024 + i] = 0xEE;
            vk32abi::VkBufferCopy reg32{};
            reg32.srcOffset = 128; reg32.dstOffset = 0; reg32.size = 512;
            memory32.memcpyToGuest(601856, &reg32, sizeof(reg32));
            const size_t cmdsBefore = cb32->cmds.size();
            { VK32Args a{}; a.a[0] = cb32Id; a.a[1] = srcBuf32; a.a[2] = dstBuf32; a.a[3] = 1; a.a[4] = 601856;
              call32(VK64_fn_vkCmdCopyBuffer, a); }
            assert(cb32->cmds.size() == cmdsBefore + 1);
            const Cmd& rbCmd = cb32->cmds.back();
            assert(rbCmd.kind == CMD_COPY_B2B_RB && rbCmd.copyB2BRb.srcBuffer == srcBuf32 &&
                   rbCmd.copyB2BRb.dstBuffer == dstBuf32 && rbCmd.copyB2BRb.srcOff == 128 &&
                   rbCmd.copyB2BRb.dstOff == 0 && rbCmd.copyB2BRb.size == 512);
            for (U32 i = 0; i < 512; i++) assert(memory32.bytes[memVa + 128 * 1024 + i] == 0xEE);
            // The *2 copy entry point defers the same way: a second region
            // inside the marked range, copied via vkCmdCopyBuffer2KHR's
            // hand-parsed info struct (offsets per the handler).
            for (U32 i = 0; i < 256; i++) memory32.bytes[memVa + 128 * 1024 + 1024 + i] = 0xDD;
            memory32.writeq(601984 + 8, srcBuf32);
            memory32.writeq(601984 + 16, dstBuf32);
            memory32.writed(601984 + 24, 1);
            memory32.writed(601984 + 28, 602048);
            memory32.writeq(602048 + 8, 256);
            memory32.writeq(602048 + 16, 1024);
            memory32.writeq(602048 + 24, 256);
            const size_t cmdsBefore2 = cb32->cmds.size();
            { VK32Args a{}; a.a[0] = cb32Id; a.a[1] = 601984;
              call32(VK64_fn_vkCmdCopyBuffer2KHR, a); }
            assert(cb32->cmds.size() == cmdsBefore2 + 1);
            const Cmd& rbCmd2 = cb32->cmds.back();
            assert(rbCmd2.kind == CMD_COPY_B2B_RB && rbCmd2.copyB2BRb.srcOff == 256 &&
                   rbCmd2.copyB2BRb.dstOff == 1024 && rbCmd2.copyB2BRb.size == 256);
            for (U32 i = 0; i < 256; i++) assert(memory32.bytes[memVa + 128 * 1024 + 1024 + i] == 0xDD);
            // Submit side: the shared serializer emits the compute records
            // for the 32-bit-recorded stream -- PIPELINE_COMPUTE, DISPATCH
            // with the recorded group counts, and the BUFFER_READBACK the
            // deferred copy planned.
            g_mem = &memory; g_mem32 = &memory32;
            assert(setupReadbacks({{cb32, cb32Id}}));
            g_v2Pending.clear();
            const U32 csDropBefore = vk64BridgeUnsupportedCount("compute-dispatch");
            serializeSubmitV2({{cb32, cb32Id}});
            assert(vk64BridgeUnsupportedCount("compute-dispatch") == csDropBefore);
            bool sawPipe = false, sawDisp = false, sawRb = false;
            for (const auto& chunk : g_v2Pending.back().chunks) {
                for (size_t at = 16; at < chunk.size();) {
                    U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
                    if (type == 0x1E) sawPipe = true;
                    if (type == 0x1F) {
                        sawDisp = true;
                        U32 gx, gy, gz;
                        memcpy(&gx, chunk.data() + at + 8, 4); memcpy(&gy, chunk.data() + at + 12, 4); memcpy(&gz, chunk.data() + at + 16, 4);
                        assert(gx == 2 && gy == 3 && gz == 4);
                    }
                    if (type == 0x20) sawRb = true;
                    at += 8 + len;
                }
            }
            assert(sawPipe && sawDisp && sawRb);
            g_v2Pending.clear(); g_bufReadbacks.clear(); g_readbackPending.clear();
            // DispatchBase with a non-zero base group: the base is
            // recorded, so the submit emitter declines it loudly instead
            // of silently serving the dispatch at base zero.
            U64 cbB32Id; auto* cbB32 = createObj32<CmdBuf>(K_CMDBUF, cbB32Id);
            { VK32Args a{}; a.a[0] = cbB32Id; a.a[1] = VK_PIPELINE_BIND_POINT_COMPUTE; a.a[2] = pipe32;
              call32(VK64_fn_vkCmdBindPipeline, a); }
            { VK32Args a{}; a.a[0] = cbB32Id; a.a[1] = 5; a.a[2] = 0; a.a[3] = 0; a.a[4] = 1; a.a[5] = 1; a.a[6] = 1;
              call32(VK64_fn_vkCmdDispatchBase, a); }
            assert(cbB32->cmds.back().kind == CMD_DISPATCH && cbB32->cmds.back().d == 5);
            g_v2Pending.clear();
            serializeSubmitV2({{cbB32, cbB32Id}});
            assert(vk64BridgeUnsupportedCount("compute-dispatch") == csDropBefore + 1);
            for (const auto& chunk : g_v2Pending.back().chunks) {
                for (size_t at = 16; at < chunk.size();) {
                    U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
                    assert(type != 0x1F);
                    at += 8 + len;
                }
            }
            g_v2Pending.clear();
            g_mem32 = nullptr; g_mem = &memory;
        }
        puts("PASS: 32-bit compute dispatch is recorded, marked, deferred and served (V2 compute records)");
        // dropObj cascades (pool -> sets, command buffer -> dynamic objects), so snapshot the ids first.
        std::vector<U64> toDrop;
        for (auto& kv : g_objs) if (!idsBefore.count(kv.first)) toDrop.push_back(kv.first);
        for (U64 id : toDrop) if (g_objs.count(id)) dropObj(id);
        g_v2Pending.clear();
        puts("PASS: descriptor strides differ between i386 and x86-64");
    }
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
        // Pipeline with a geometry stage: RECORDED since the 2026-10-26 vkd3d disposition (meta variants must create; draws under a geometry stage are dropped loudly at submit).
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
        assert((S64)vkCreateGraphicsPipelines(devId, 0, 1, (void*)3456, nullptr, (void*)3840) == VK_SUCCESS);
        assert(memory.readq(3840) != 0);
        assert(vk64BridgeUnsupportedCount("geometry-stage") == 1);
        assert(g_objs.size() == objectsBefore + 1);
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
        // Compute pipelines are recorded as real objects (vkd3d-proton
        // creates internal compute pipelines during D3D12 device creation
        // and treats creation failure as device-fatal). Dispatches are
        // served by the V2 compute records (section 4.27); a dispatch
        // with no compute pipeline bound stays loud at submit (below).
        {
            memory.writeq(3960, 0xdeadbeefull);
            VK64Args cp{}; cp.a[0] = devId; cp.a[2] = 1; cp.a[3] = 3456; cp.a[5] = 3960;
            memory.memcpyToGuest(128, &cp, sizeof(cp));
            const size_t objsBefore = g_objs.size();
            assert((S64)vk64Bridge(&cpu, VK64_fn_vkCreateComputePipelines, 128) == VK_SUCCESS);
            assert(memory.readq(3960) != 0 && g_objs.size() == objsBefore + 1);
        }
        // A dispatch with no compute pipeline bound cannot be served:
        // submitting one is reported, never dropped quietly.
        U64 cbDispatchId; auto* cbDispatch = createObj<CmdBuf>(K_CMDBUF, cbDispatchId);
        vkCmdDispatch(cbDispatchId, 1, 1, 1);
        // Relative, not absolute: the 32-bit compute guard above already
        // exercised one loud DispatchBase decline on this shared counter.
        const U32 csNoPipeBefore = vk64BridgeUnsupportedCount("compute-dispatch");
        serializeSubmitV2({{cbDispatch, cbDispatchId}});
        assert(vk64BridgeUnsupportedCount("compute-dispatch") == csNoPipeBefore + 1);
        g_v2Pending.clear();
        // Compute dispatch SERVED (section 4.27): under a bound compute
        // pipeline the submit emits PIPELINE_COMPUTE + DISPATCH records
        // and the drop counter does not move; a DispatchBase-shaped
        // dispatch (non-zero base group, carried in d) stays loud and
        // emits no DISPATCH record.
        {
            const U32 csWords[] = {
                0x07230203u, 0x00010500u, 0, 64, 0,
                (2u << 16) | 17u, 1u,                    // OpCapability Shader
                (3u << 16) | 14u, 0u, 1u,                // OpMemoryModel Logical GLSL450
                (5u << 16) | 15u, 5u, 6u, 0x6e69616du, 0u, // OpEntryPoint GLCompute %6 "main"
                (6u << 16) | 16u, 6u, 17u, 1u, 1u, 1u,   // OpExecutionMode %6 LocalSize 1 1 1
                (2u << 16) | 19u, 1u,                    // OpTypeVoid
                (3u << 16) | 33u, 2u, 1u,                // OpTypeFunction %2 = fn(%1)
                (5u << 16) | 54u, 1u, 6u, 0u, 2u,        // OpFunction %6
                (2u << 16) | 248u, 7u,                   // OpLabel
                (1u << 16) | 253u,                       // OpReturn
                (1u << 16) | 56u,                        // OpFunctionEnd
            };
            memory.memcpyToGuest(4400, csWords, sizeof(csWords));
            VkShaderModuleCreateInfo smci{}; smci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            smci.codeSize = sizeof(csWords); smci.pCode = 4400;
            memory.memcpyToGuest(4200, &smci, sizeof(smci));
            memory.writeq(4600, 0);
            VK64Args sm{}; sm.a[0] = devId; sm.a[1] = 4200; sm.a[3] = 4600;
            memory.memcpyToGuest(128, &sm, sizeof(sm));
            assert((S64)vk64Bridge(&cpu, VK64_fn_vkCreateShaderModule, 128) == VK_SUCCESS);
            const U64 csModId = memory.readq(4600);
            assert(csModId != 0);
            memory.memcpyToGuest(5050, "main", 5);
            VkComputePipelineCreateInfo cci{};
            cci.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
            cci.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            cci.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            cci.stage.module = (VkShaderModule)csModId;
            cci.stage.pName = 5050;
            memory.memcpyToGuest(4800, &cci, sizeof(cci));
            memory.writeq(5100, 0);
            VK64Args cp2{}; cp2.a[0] = devId; cp2.a[2] = 1; cp2.a[3] = 4800; cp2.a[5] = 5100;
            memory.memcpyToGuest(128, &cp2, sizeof(cp2));
            assert((S64)vk64Bridge(&cpu, VK64_fn_vkCreateComputePipelines, 128) == VK_SUCCESS);
            const U64 csPipeId = memory.readq(5100);
            assert(csPipeId != 0);

            const U32 csCountBefore = vk64BridgeUnsupportedCount("compute-dispatch");
            U64 cbCsId; auto* cbCs = createObj<CmdBuf>(K_CMDBUF, cbCsId);
            Cmd bindPipe; bindPipe.kind = CMD_BIND_PIPE; bindPipe.a = csPipeId; pushCmd(cbCs, bindPipe);
            Cmd disp; disp.kind = CMD_DISPATCH; disp.a = 2; disp.b = 3; disp.c = 4; pushCmd(cbCs, disp);
            g_v2Pending.clear();
            serializeSubmitV2({{cbCs, cbCsId}});
            assert(vk64BridgeUnsupportedCount("compute-dispatch") == csCountBefore);
            bool sawComputePipe = false, sawDispatch = false;
            for (const auto& chunk : g_v2Pending.back().chunks) {
                for (size_t at = 16; at < chunk.size();) {
                    U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
                    if (type == 0x1E) sawComputePipe = true;
                    if (type == 0x1F) {
                        sawDispatch = true;
                        U32 gx, gy, gz;
                        memcpy(&gx, chunk.data() + at + 8, 4); memcpy(&gy, chunk.data() + at + 12, 4); memcpy(&gz, chunk.data() + at + 16, 4);
                        assert(gx == 2 && gy == 3 && gz == 4);
                    }
                    at += 8 + len;
                }
            }
            assert(sawComputePipe && sawDispatch);
            g_v2Pending.clear();
            U64 cbBaseId; auto* cbBase = createObj<CmdBuf>(K_CMDBUF, cbBaseId);
            pushCmd(cbBase, bindPipe);
            Cmd dispBase; dispBase.kind = CMD_DISPATCH; dispBase.a = 1; dispBase.b = 1; dispBase.c = 1; dispBase.d = 5; pushCmd(cbBase, dispBase);
            serializeSubmitV2({{cbBase, cbBaseId}});
            assert(vk64BridgeUnsupportedCount("compute-dispatch") == csCountBefore + 1);
            for (const auto& chunk : g_v2Pending.back().chunks) {
                for (size_t at = 16; at < chunk.size();) {
                    U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
                    assert(type != 0x1F);
                    at += 8 + len;
                }
            }
            g_v2Pending.clear();
        }
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

    // GUARD (advertised-but-unimplemented + 32/64 disagreement): the core feature
    // block is ONE function for both guests, the unimplemented bits are FALSE, and
    // the chained-struct fills are shared so a 32-bit guest is answered like a
    // 64-bit one.
    {
        auto call64 = [&](U32 fn, U64 a0 = 0, U64 a1 = 0, U64 a2 = 0, U64 a3 = 0) -> U64 {
            VK64Args a{}; a.a[0] = a0; a.a[1] = a1; a.a[2] = a2; a.a[3] = a3;
            memory.memcpyToGuest(128, &a, sizeof(a));
            return vk64Bridge(&cpu, fn, 128);
        };
        VkPhysicalDeviceFeatures f64{};
        assert(call64(VK64_fn_vkGetPhysicalDeviceFeatures, 0, 8192) == VK_SUCCESS);
        memory.memcpyFromGuest(&f64, 8192, sizeof(f64));
        assert(!f64.geometryShader && !f64.tessellationShader && f64.imageCubeArray);
        // textureCompressionBC mirrors the page-published flag (v2WebGpuBcSupported):
        // FALSE in this native run (no page), TRUE in the browser once the device
        // publishes BC support. Assert the product's own rule, not a constant, so the
        // gate is checked in both environments.
        assert(f64.textureCompressionBC == (v2WebGpuBcSupported() ? VK_TRUE : VK_FALSE));
        assert(!f64.shaderInt64 && !f64.shaderFloat64 && !f64.shaderStorageImageExtendedFormats);
        // shaderStorageImageReadWithoutFormat is TRUE (typed-UAV work: typed storage
        // reads are served end to end; register row IMPLEMENTED). The WRITE form,
        // whose modules reject at audit, stays FALSE.
        assert(f64.shaderStorageImageReadWithoutFormat && !f64.shaderStorageImageWriteWithoutFormat);
        assert(!f64.sparseBinding && !f64.wideLines && !f64.logicOp);
        // 32-bit guest: previously only robustBufferAccess was set.
        VkPhysicalDeviceFeatures f32{};
        VK32Args a32{}; a32.a[1] = 8192;
        memory32.memcpyToGuest(128, &a32, sizeof(a32));
        assert(vk32Bridge(&cpu32, VK64_fn_vkGetPhysicalDeviceFeatures, 128) == 0);
        memory32.memcpyFromGuest(&f32, 8192, sizeof(f32));
        assert(memcmp(&f32, &f64, sizeof(f32)) == 0);
        // Chained Vulkan12 / 11 / 13 / FP16 structs: same answers on both sides.
        auto chain64 = [&]() {
            VkPhysicalDeviceVulkan12Features v12{}; v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            VkPhysicalDeviceVulkan11Features v11{}; v11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            VkPhysicalDeviceShaderFloat16Int8Features fp{}; fp.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
            v12.pNext = 16384; v11.pNext = 20480;
            memory.memcpyToGuest(12288, &v12, sizeof(v12));
            memory.memcpyToGuest(16384, &v11, sizeof(v11));
            memory.memcpyToGuest(20480, &fp, sizeof(fp));
            VkPhysicalDeviceFeatures2 f2{}; f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; f2.pNext = 12288;
            memory.memcpyToGuest(10240, &f2, sizeof(f2));
            assert(call64(VK64_fn_vkGetPhysicalDeviceFeatures2, 0, 10240) == VK_SUCCESS);
            memory.memcpyFromGuest(&v12, 12288, sizeof(v12));
            memory.memcpyFromGuest(&v11, 16384, sizeof(v11));
            memory.memcpyFromGuest(&fp, 20480, sizeof(fp));
            assert(v12.timelineSemaphore && !v12.bufferDeviceAddress && v12.samplerMirrorClampToEdge);
            assert(v11.shaderDrawParameters && !v11.multiview && !v11.variablePointers && !v11.storageBuffer16BitAccess);
            assert(!fp.shaderFloat16 && !fp.shaderInt8);
        };
        chain64();
        {
            vk32abi::VkPhysicalDeviceVulkan12Features v12{}; v12.sType = vk32abi::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
            vk32abi::VkPhysicalDeviceVulkan11Features v11{}; v11.sType = vk32abi::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
            vk32abi::VkPhysicalDeviceShaderFloat16Int8Features fp{}; fp.sType = vk32abi::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES;
            v12.pNext = 16384; v11.pNext = 20480;
            memory32.memcpyToGuest(12288, &v12, sizeof(v12));
            memory32.memcpyToGuest(16384, &v11, sizeof(v11));
            memory32.memcpyToGuest(20480, &fp, sizeof(fp));
            U8 f2[8 + 220] = {}; U32 st = vk32abi::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; memcpy(f2, &st, 4);
            U32 next = 12288; memcpy(f2 + 4, &next, 4);
            memory32.memcpyToGuest(10240, f2, sizeof(f2));
            VK32Args b32{}; b32.a[1] = 10240;
            memory32.memcpyToGuest(128, &b32, sizeof(b32));
            assert(vk32Bridge(&cpu32, VK64_fn_vkGetPhysicalDeviceFeatures2, 128) == 0);
            memory32.memcpyFromGuest(&v12, 12288, sizeof(v12));
            memory32.memcpyFromGuest(&v11, 16384, sizeof(v11));
            memory32.memcpyFromGuest(&fp, 20480, sizeof(fp));
            assert(v12.timelineSemaphore && !v12.bufferDeviceAddress && v12.samplerMirrorClampToEdge);
            assert(v11.shaderDrawParameters && !v11.multiview && !v11.variablePointers && !v11.storageBuffer16BitAccess);
            assert(!fp.shaderFloat16 && !fp.shaderInt8);
        }
        // Unknown sType: left exactly as the guest set it (used to zero 512 bytes
        // past the struct, clobbering the next chained struct) and counted.
        {
            U32 unknown[8] = {0x7fff0000u, 0, 20480 + 0, 0, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu};
            memory.memcpyToGuest(24576, unknown, sizeof(unknown));
            VkPhysicalDeviceFeatures2 f2{}; f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2; f2.pNext = 24576;
            memory.memcpyToGuest(10240, &f2, sizeof(f2));
            const U32 before = vk64BridgeUnsupportedCount("features2-struct");
            memory.writeq(24576 + 8, 0);
            assert(call64(VK64_fn_vkGetPhysicalDeviceFeatures2, 0, 10240) == VK_SUCCESS);
            assert(memory.readd(24576 + 16) == 0xAAAAAAAAu && memory.readd(24576 + 28) == 0xAAAAAAAAu);
            assert(vk64BridgeUnsupportedCount("features2-struct") == before + 1);
        }
        // Same for Properties2 chains: unknown sTypes are counted, not silently skipped.
        {
            U32 unknown[8] = {0x7fff0001u, 0, 0, 0, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu, 0xAAAAAAAAu};
            memory.memcpyToGuest(24576, unknown, sizeof(unknown));
            VkPhysicalDeviceProperties2 p2{}; p2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; p2.pNext = 24576;
            memory.memcpyToGuest(10240, &p2, sizeof(p2));
            const U32 before = vk64BridgeUnsupportedCount("properties2-struct");
            U64 pdId; createObj<PhysDev>(K_PHYSDEV, pdId);
            assert(call64(VK64_fn_vkGetPhysicalDeviceProperties2, pdId, 10240) == 0);
            assert(memory.readd(24576 + 16) == 0xAAAAAAAAu);
            assert(vk64BridgeUnsupportedCount("properties2-struct") == before + 1);
            // 32-bit layout: sType u32 @0, pNext u32 @4.
            U32 unknown32[4] = {0x7fff0001u, 0, 0xAAAAAAAAu, 0xAAAAAAAAu};
            memory32.memcpyToGuest(24576, unknown32, sizeof(unknown32));
            U8 q2[8 + 800] = {}; U32 qst = vk32abi::VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2; memcpy(q2, &qst, 4);
            U32 qnext = 24576; memcpy(q2 + 4, &qnext, 4);
            memory32.memcpyToGuest(10240, q2, sizeof(q2));
            U64 pdId32; createObj32<PhysDev>(K_PHYSDEV, pdId32);
            VK32Args c32{}; c32.a[0] = pdId32; c32.a[1] = 10240;
            memory32.memcpyToGuest(128, &c32, sizeof(c32));
            vk32Bridge(&cpu32, VK64_fn_vkGetPhysicalDeviceProperties2, 128);
            assert(vk64BridgeUnsupportedCount("properties2-struct") == before + 2);
        }
    {
        // Image views keep their real subresource range and type; non-identity swizzle and
        // render-target mip/layer selection are reported loudly instead of silently dropped.
        Image img; img.mips = 5; img.layers = 6;
        ImageView v;
        const U32 ident[4] = {0, 0, 0, 0}, explicitIdent[4] = {3, 4, 5, 6}, swapped[4] = {5, 4, 3, 6};
        const U64 swBefore = vk64BridgeUnsupportedCount("image-view-swizzle");
        vk64InitImageView(&v, &img, 5, ident, 1, 0xFFFFFFFFu, 2, 0xFFFFFFFFu);
        assert(v.viewType == 5 && v.baseMip == 1 && v.levelCount == 4 && v.baseLayer == 2 && v.layerCount == 4);
        vk64InitImageView(&v, &img, 1, explicitIdent, 0, 1, 0, 1);
        assert(v.levelCount == 1 && v.layerCount == 1);
        assert(vk64BridgeUnsupportedCount("image-view-swizzle") == swBefore);
        vk64InitImageView(&v, &img, 1, swapped, 0, 1, 0, 1);
        assert(vk64BridgeUnsupportedCount("image-view-swizzle") == swBefore + 1);
        const U64 rtBefore = vk64BridgeUnsupportedCount("render-target-subresource");
        vk64AuditAttachmentView(&v);
        assert(vk64BridgeUnsupportedCount("render-target-subresource") == rtBefore);
        v.baseMip = 2;
        vk64AuditAttachmentView(&v);
        v.baseMip = 0; v.baseLayer = 1;
        vk64AuditAttachmentView(&v);
        assert(vk64BridgeUnsupportedCount("render-target-subresource") == rtBefore + 2);
        puts("PASS: image view ranges are kept; swizzle and render-target subresources are loud");
    }
    {
        // Multiple render targets keep their own clear colours; integer formats clear with integer values.
        RenderPass rp; rp.natt = 3; rp.formats[0] = 37; rp.formats[1] = 130; rp.formats[2] = 99;
        std::vector<VkClearValue> values(3);
        values[0].color.float32[0] = 0.25f; values[0].color.float32[3] = 1.0f;
        values[1].depthStencil.depth = 0.5f; values[1].depthStencil.stencil = 7;
        values[2].color.int32[0] = -5; values[2].color.int32[1] = 9;
        Cmd c; captureRenderPassClears(c, &rp, values);
        assert(c.attClear.size() == 12);
        assert(c.attClear[0] == 0.25f && c.attClear[3] == 1.0f && c.f[0] == 0.25f);
        assert(c.clearDepth == 0.5 && c.clearStencil == 7);
        assert(c.attClear[8] == -5.0f && c.attClear[9] == 9.0f);
        puts("PASS: per-attachment colour clears (MRT, integer formats) are captured");
    }
    {
        // Subpasses: each one is its own RP_BEGIN/RP_END with only its own attachments; load/store ops
        // apply on first/last use; a colour resolve target is not an attachment but an IMAGE_COPY.
        U64 imgIds[4], viewIds[4];
        for (U32 i = 0; i < 4; ++i) {
            auto* im = createObj<Image>(K_IMAGE, imgIds[i]); im->format = 37; im->w = 64; im->h = 32; im->samples = i == 0 ? 4 : 1;
            auto* vw = createObj<ImageView>(K_IMAGEVIEW, viewIds[i]); vw->image = imgIds[i]; vw->format = 37; vw->aspect = 1;
        }
        U64 rpId, fbId, cbId;
        auto* srp = createObj<RenderPass>(K_RENDERPASS, rpId);
        srp->natt = 4; for (U32 i = 0; i < 4; ++i) { srp->formats[i] = 37; srp->loadOps[i] = 1; srp->storeOps[i] = (i == 0 || i == 2) ? 1 : 0; srp->samples[i] = i == 0 ? 4 : 1; }
        srp->nsub = 2;
        srp->sub[0].nColor = 1; srp->sub[0].color[0] = 0; srp->sub[0].resolve[0] = 1;     // MSAA 0 -> resolve 1
        srp->sub[1].nColor = 2; srp->sub[1].color[0] = 2; srp->sub[1].color[1] = 0;        // reuses attachment 0
        auto* sfb = createObj<Framebuffer>(K_FRAMEBUFFER, fbId);
        sfb->rp = rpId; sfb->natt = 4; sfb->w = 64; sfb->h = 32; for (U32 i = 0; i < 4; ++i) sfb->views[i] = viewIds[i];
        auto* scb = createObj<CmdBuf>(K_CMDBUF, cbId);
        Cmd b; b.kind = CMD_BEGIN_RP; b.a = rpId; b.b = fbId; pushCmd(scb, b);
        Cmd n; n.kind = CMD_NEXT_SUBPASS; pushCmd(scb, n);
        Cmd e; e.kind = CMD_END_RP; pushCmd(scb, e);
        const U64 overflowBefore = vk64BridgeUnsupportedCount("next-subpass-overflow");
        pushCmd(scb, n);   // past the last subpass: loud, not silently ignored
        serializeSubmitV2({{scb, cbId}});
        struct Rec { U32 type, len; std::vector<U8> body; };
        std::vector<Rec> recs;
        for (const auto& chunk : g_v2Pending.back().chunks)
            for (size_t at = 16; at < chunk.size();) {
                Rec r; memcpy(&r.type, chunk.data() + at, 4); memcpy(&r.len, chunk.data() + at + 4, 4);
                r.body.assign(chunk.data() + at + 8, chunk.data() + at + 8 + r.len); recs.push_back(r); at += 8 + r.len;
            }
        std::vector<U32> seq; for (const auto& r : recs) if (r.type == V2_RP_BEGIN || r.type == V2_RP_END || r.type == V2_IMAGE_COPY) seq.push_back(r.type);
        // sub0: begin, end, resolve copy (0 -> 1); sub1: begin, end (no resolves)
        assert((seq == std::vector<U32>{V2_RP_BEGIN, V2_RP_END, V2_IMAGE_COPY, V2_RP_BEGIN, V2_RP_END}));
        std::vector<const Rec*> begins, copies;
        for (const auto& r : recs) { if (r.type == V2_RP_BEGIN) begins.push_back(&r); if (r.type == V2_IMAGE_COPY) copies.push_back(&r); }
        auto u32at = [](const Rec* r, size_t off) { U32 v; memcpy(&v, r->body.data() + off, 4); return v; };
        auto u64at = [](const Rec* r, size_t off) { U64 v; memcpy(&v, r->body.data() + off, 8); return v; };
        assert(u32at(begins[0], 24) == 1 && u32at(begins[1], 24) == 2);                  // attachment counts
        // Attachment 0 is shared: it clears on its first use, is forced to STORE while a later subpass
        // still needs it, loads (never clears) on re-use, and takes its real store op on the last use.
        assert(u32at(begins[0], 28) == 37 && u32at(begins[0], 32) == 1 && u32at(begins[0], 36) == 0);
        assert(u32at(begins[1], 28 + 0 * 20) == 37 && u32at(begins[1], 32 + 0 * 20) == 1 && u32at(begins[1], 36 + 0 * 20) == 1);   // attachment 2: clear, DONT_CARE
        assert(u32at(begins[1], 32 + 1 * 20) == 0 && u32at(begins[1], 36 + 1 * 20) == 1);                                       // attachment 0: LOAD, real store op
        assert(u64at(copies[0], 0) == imgIds[0] && u64at(copies[0], 8) == imgIds[1]);
        assert(u32at(copies[0], 56) == 64 && u32at(copies[0], 60) == 32 && u32at(copies[0], 64) == 1);
        assert(vk64BridgeUnsupportedCount("next-subpass-overflow") == overflowBefore + 1);
        g_v2Pending.clear();
        puts("PASS: render pass subpasses split into passes with resolve copies; extra NextSubpass is loud");
    }
    {
        // Dynamic rendering: the colour resolve target becomes a post-pass IMAGE_COPY, not a second
        // attachment; depth resolve, viewMask and a render-area offset are reported instead of ignored.
        U64 msaaImg, resImg, msaaView, resView, cbId;
        auto* mi = createObj<Image>(K_IMAGE, msaaImg); mi->format = 37; mi->w = 16; mi->h = 8; mi->samples = 4;
        auto* ri2 = createObj<Image>(K_IMAGE, resImg); ri2->format = 37; ri2->w = 16; ri2->h = 8; ri2->samples = 1;
        auto* mv = createObj<ImageView>(K_IMAGEVIEW, msaaView); mv->image = msaaImg; mv->format = 37; mv->aspect = 1;
        auto* rv = createObj<ImageView>(K_IMAGEVIEW, resView); rv->image = resImg; rv->format = 37; rv->aspect = 1;
        auto* dcb = createObj<CmdBuf>(K_CMDBUF, cbId);
        U8 att[72] = {}, info[72] = {};
        auto p32 = [](U8* b, size_t at, U32 v) { memcpy(b + at, &v, 4); };
        auto p64 = [](U8* b, size_t at, U64 v) { memcpy(b + at, &v, 8); };
        p64(att, 16, msaaView); p32(att, 28, 2); p64(att, 32, resView); p32(att, 44, 1); p32(att, 48, 0);
        p32(info, 28, 16); p32(info, 32, 8); p32(info, 36, 1); p32(info, 44, 1); p64(info, 48, 7168);
        p32(info, 20, 3);        // renderArea.offset.x != 0
        p32(info, 40, 0x3);      // viewMask
        memory.memcpyToGuest(7168, att, sizeof(att));
        memory.memcpyToGuest(7296, info, sizeof(info));
        const U64 mvBefore = vk64BridgeUnsupportedCount("multiview-render-pass"), offBefore = vk64BridgeUnsupportedCount("render-area-offset");
        vkCmdBeginRendering(cbId, (void*)7296);
        vkCmdEndRendering(cbId);
        assert(vk64BridgeUnsupportedCount("multiview-render-pass") == mvBefore + 1 && vk64BridgeUnsupportedCount("render-area-offset") == offBefore + 1);
        auto* drp = objOf<RenderPass>(dcb->cmds[0].a, K_RENDERPASS, "test");
        assert(drp->nsub == 1 && drp->sub[0].nColor == 1 && drp->sub[0].color[0] == 0 && drp->sub[0].resolve[0] == 1);
        serializeSubmitV2({{dcb, cbId}});
        unsigned begins = 0, copies = 0; U32 attCount = 0; U64 copySrc = 0, copyDst = 0;
        for (const auto& chunk : g_v2Pending.back().chunks)
            for (size_t at = 16; at < chunk.size();) {
                U32 type, len; memcpy(&type, chunk.data() + at, 4); memcpy(&len, chunk.data() + at + 4, 4);
                if (type == V2_RP_BEGIN) { ++begins; memcpy(&attCount, chunk.data() + at + 8 + 24, 4); }
                if (type == V2_IMAGE_COPY) { ++copies; memcpy(&copySrc, chunk.data() + at + 8, 8); memcpy(&copyDst, chunk.data() + at + 16, 8); }
                at += 8 + len;
            }
        assert(begins == 1 && attCount == 1 && copies == 1 && copySrc == msaaImg && copyDst == resImg);
        g_v2Pending.clear();
        puts("PASS: dynamic rendering colour resolve is an image copy; viewMask and render-area offset are loud");
    }
    {
        // vkCreateRenderPass2 (API 1.2 core; DXVK 2.x and vkd3d use it): subpass colour/resolve/depth
        // references are read through both guest ABIs, and the unsupported parts are counted.
        const U64 inBefore = vk64BridgeUnsupportedCount("subpass-input-attachment");
        // ---- 64-bit
        RpAttachmentDesc2_64 ad[3] = {}; for (auto& a : ad) a.sType = 1000109000;
        ad[0].format = 37; ad[0].samples = 4; ad[1].format = 37; ad[1].samples = 1; ad[2].format = 126; ad[2].samples = 4;
        RpAttachmentRef2_64 colorRef = {}, resolveRef = {}, depthRef = {}, inputRef = {};
        colorRef.attachment = 0; resolveRef.attachment = 1; depthRef.attachment = 2; inputRef.attachment = 1;
        memory.memcpyToGuest(8192, ad, sizeof(ad));
        memory.memcpyToGuest(8448, &colorRef, sizeof(colorRef)); memory.memcpyToGuest(8512, &resolveRef, sizeof(resolveRef));
        memory.memcpyToGuest(8576, &depthRef, sizeof(depthRef)); memory.memcpyToGuest(8640, &inputRef, sizeof(inputRef));
        RpSubpassDesc2_64 sd = {}; sd.colorAttachmentCount = 1; sd.pColorAttachments = 8448; sd.pResolveAttachments = 8512; sd.pDepthStencilAttachment = 8576;
        sd.inputAttachmentCount = 1; sd.pInputAttachments = 8640;
        memory.memcpyToGuest(8704, &sd, sizeof(sd));
        VkRenderPassCreateInfo2 ci2 = {}; ci2.attachmentCount = 3; ci2.pAttachments = 8192; ci2.subpassCount = 1; ci2.pSubpasses = 8704;
        memory.memcpyToGuest(8832, &ci2, sizeof(ci2));
        VK64Args a{}; a.a[1] = 8832; a.a[3] = 9000;
        memory.memcpyToGuest(128, &a, sizeof(a));
        assert(vk64Bridge(&cpu, VK64_fn_vkCreateRenderPass2, 128) == VK_SUCCESS);
        auto* rp2 = objOf<RenderPass>(memory.readq(9000), K_RENDERPASS, "test");
        assert(rp2 && rp2->natt == 3 && rp2->samples[0] == 4 && rp2->nsub == 1);
        assert(rp2->sub[0].nColor == 1 && rp2->sub[0].color[0] == 0 && rp2->sub[0].resolve[0] == 1 && rp2->sub[0].depth == 2);
        assert(vk64BridgeUnsupportedCount("subpass-input-attachment") == inBefore + 1);
        // ---- 32-bit
        RpAttachmentDesc2_32 ad32[3] = {}; for (auto& x : ad32) x.sType = 1000109000;
        ad32[0].format = 37; ad32[0].samples = 4; ad32[1].format = 37; ad32[1].samples = 1; ad32[2].format = 126; ad32[2].samples = 4;
        RpAttachmentRef2_32 c32 = {}, r32 = {}, d32 = {}; c32.attachment = 0; r32.attachment = 1; d32.attachment = 2;
        memory32.memcpyToGuest(8192, ad32, sizeof(ad32));
        memory32.memcpyToGuest(8448, &c32, sizeof(c32)); memory32.memcpyToGuest(8512, &r32, sizeof(r32)); memory32.memcpyToGuest(8576, &d32, sizeof(d32));
        RpSubpassDesc2_32 sd32 = {}; sd32.colorAttachmentCount = 1; sd32.pColorAttachments = 8448; sd32.pResolveAttachments = 8512; sd32.pDepthStencilAttachment = 8576;
        memory32.memcpyToGuest(8704, &sd32, sizeof(sd32));
        vk32abi::VkRenderPassCreateInfo2 ci32 = {}; ci32.attachmentCount = 3; ci32.pAttachments = 8192; ci32.subpassCount = 1; ci32.pSubpasses = 8704;
        memory32.memcpyToGuest(8832, &ci32, sizeof(ci32));
        VK32Args b{}; b.a[1] = 8832; b.a[3] = 9000;
        memory32.memcpyToGuest(128, &b, sizeof(b));
        assert(vk32Bridge(&cpu32, VK64_fn_vkCreateRenderPass2, 128) == VK_SUCCESS);
        auto* rp32 = objOf32<RenderPass>(memory32.readd(9000), K_RENDERPASS, "test");
        assert(rp32 && rp32->natt == 3 && rp32->nsub == 1);
        assert(rp32->sub[0].nColor == 1 && rp32->sub[0].resolve[0] == 1 && rp32->sub[0].depth == 2);
        puts("PASS: vkCreateRenderPass2 subpass references parse in 64-bit and 32-bit guests");
    }
        puts("PASS: GUARD advertised features are truthful and identical for 32-bit and 64-bit guests");

        // GUARD (silent capability use): SPIR-V that declares capabilities or
        // resources the page tier cannot honour is reported at vkCreateShaderModule.
        auto module = [&](const std::vector<U32>& body) {
            std::vector<U32> code = {0x07230203u, 0x00010500u, 0, 64, 0};
            code.insert(code.end(), body.begin(), body.end());
            memory.memcpyToGuest(28672, code.data(), code.size() * 4);
            VkShaderModuleCreateInfo ci{}; ci.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
            ci.codeSize = code.size() * 4; ci.pCode = 28672;
            memory.memcpyToGuest(26624, &ci, sizeof(ci));
            U64 devId; createObj<Device>(K_DEVICE, devId);
            return call64(VK64_fn_vkCreateShaderModule, devId, 26624, 0, 27648);
        };
        const U32 f64Before = vk64BridgeUnsupportedCount("spirv-capability:Float64");
        const U32 cullBefore = vk64BridgeUnsupportedCount("spirv-capability:CullDistance");
        assert(module({(2u << 16) | 17u, 10u, (2u << 16) | 17u, 33u, (2u << 16) | 17u, 1u}) == VK_ERROR_FEATURE_NOT_PRESENT);
        assert(vk64BridgeUnsupportedCount("spirv-capability:Float64") == f64Before + 1);
        // CullDistance DECLARED but unused is not counted: DXVK declares it
        // unconditionally and the tier converts it as-is. Only the USED form
        // (a BuiltIn CullDistance member decoration) rejects, exercised next.
        assert(vk64BridgeUnsupportedCount("spirv-capability:CullDistance") == cullBefore);
        // OpCapability CullDistance; %1 = OpTypeStruct; OpMemberDecorate %1 0 BuiltIn CullDistance.
        assert(module({(2u << 16) | 17u, 33u, (2u << 16) | 30u, 1u, (5u << 16) | 72u, 1u, 0u, 11u, 4u}) == VK_ERROR_FEATURE_NOT_PRESENT);
        assert(vk64BridgeUnsupportedCount("spirv-capability:CullDistance") == cullBefore + 1);
        // %1 = OpTypeStruct; %2 = OpTypePointer StorageBuffer %1; %3 = OpVariable %2 StorageBuffer
        const U32 sbBefore = vk64BridgeUnsupportedCount("spirv-storage-buffer-write");
        assert(module({(2u << 16) | 30u, 1u, (4u << 16) | 32u, 2u, 12u, 1u, (4u << 16) | 59u, 2u, 3u, 12u}) == VK_ERROR_FEATURE_NOT_PRESENT);
        assert(vk64BridgeUnsupportedCount("spirv-storage-buffer-write") == sbBefore + 1);
        // ... the same variable decorated NonWritable is read-only and is not reported.
        assert(module({(3u << 16) | 71u, 3u, 24u, (2u << 16) | 30u, 1u, (4u << 16) | 32u, 2u, 12u, 1u,
                       (4u << 16) | 59u, 2u, 3u, 12u}) == VK_SUCCESS);
        assert(vk64BridgeUnsupportedCount("spirv-storage-buffer-write") == sbBefore + 1);
        const U32 badBefore = vk64BridgeUnsupportedCount("spirv-malformed");
        assert(module({(0u << 16) | 17u}) == VK_ERROR_INVALID_SHADER_NV);
        assert(vk64BridgeUnsupportedCount("spirv-malformed") == badBefore + 1);
        puts("PASS: GUARD SPIR-V capabilities and storage-buffer stores are reported at vkCreateShaderModule");

        // GUARD: sampler state WebGPU cannot honour is reported at vkCreateSampler.
        {
            VkSamplerCreateInfo sc{}; sc.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
            sc.addressModeU = (VkSamplerAddressMode)4; sc.addressModeV = (VkSamplerAddressMode)3; sc.mipLodBias = 0.5f;
            sc.borderColor = (VkBorderColor)2;
            memory.memcpyToGuest(30720, &sc, sizeof(sc));
            U64 dev2; createObj<Device>(K_DEVICE, dev2);
            const U32 m0 = vk64BridgeUnsupportedCount("mirror-clamp-to-edge");
            const U32 b0 = vk64BridgeUnsupportedCount("sampler-border-colour");
            const U32 l0 = vk64BridgeUnsupportedCount("sampler-mip-lod-bias");
            // mirror-clamp is advertised (vkd3d-proton device gate) and served by the
            // page tier as mirror-repeat: creation succeeds; the approximation is
            // reported, like border colour and LOD bias.
            assert(call64(VK64_fn_vkCreateSampler, dev2, 30720, 0, 31744) == VK_SUCCESS);
            assert(vk64BridgeUnsupportedCount("mirror-clamp-to-edge") == m0 + 1);
            assert(vk64BridgeUnsupportedCount("sampler-border-colour") == b0 + 1);
            assert(vk64BridgeUnsupportedCount("sampler-mip-lod-bias") == l0 + 1);
            sc.addressModeU = (VkSamplerAddressMode)0; sc.addressModeV = (VkSamplerAddressMode)3; sc.mipLodBias = 0.5f;
            memory.memcpyToGuest(30720, &sc, sizeof(sc));
            assert(call64(VK64_fn_vkCreateSampler, dev2, 30720, 0, 31744) == VK_SUCCESS);   // core border + bias still create
            assert(vk64BridgeUnsupportedCount("sampler-border-colour") == b0 + 2);
            assert(vk64BridgeUnsupportedCount("sampler-mip-lod-bias") == l0 + 2);
            sc.addressModeU = sc.addressModeV = (VkSamplerAddressMode)0; sc.mipLodBias = 0;
            memory.memcpyToGuest(30720, &sc, sizeof(sc));
            assert(call64(VK64_fn_vkCreateSampler, dev2, 30720, 0, 31744) == VK_SUCCESS);
            assert(vk64BridgeUnsupportedCount("mirror-clamp-to-edge") == m0 + 1);
            puts("PASS: GUARD unsupported sampler state is reported at vkCreateSampler");
        }
        // ---- Query pools: creation succeeds and begin/end/timestamp complete.
        // Regression guard for the Portal load crash: vkCreateQueryPool used to
        // return VK_ERROR_FEATURE_NOT_PRESENT with a null handle. DXVK's
        // DxvkGpuQueryAllocator answers that with query handles whose allocator
        // pointer is null, and DxvkGpuQuery's destructor dereferences it (an SRW
        // lock at null+0x10), killing the guest at map load. These blocks drive
        // the exact DXVK sequence (create pool -> reset/begin/end in a command
        // buffer -> execute at submit -> vkGetQueryPoolResults) on both ABIs.
        {
            auto callQ = [&](U64 fn, const VK64Args& args) {
                memory.memcpyToGuest(128, &args, sizeof(args));
                return vk64Bridge(&cpu, fn, 128);
            };
            QueryPoolCreateInfo64Local ci{};
            ci.queryType = vkquery::VKQUERY_OCCLUSION; ci.queryCount = 4;
            memory.memcpyToGuest(2048, &ci, sizeof(ci));
            memory.writeq(2080, 0);
            VK64Args cr{}; cr.a[0] = deviceId; cr.a[1] = 2048; cr.a[3] = 2080;
            assert(callQ(VK64_fn_vkCreateQueryPool, cr) == VK_SUCCESS);
            U64 poolId = memory.readq(2080);
            assert(poolId != 0);
            auto* pool = objOf<QueryPool>(poolId, K_QUERYPOOL, "test");
            assert(pool && pool->type == vkquery::VKQUERY_OCCLUSION && pool->count == 4
                   && pool->available.size() == 4);
            VK64Args gr{}; gr.a[0] = deviceId; gr.a[1] = poolId; gr.a[2] = 0; gr.a[3] = 1;
            gr.a[4] = 8; gr.a[5] = 2112; gr.a[6] = 8; gr.a[7] = vkquery::VKQUERY_64_BIT;
            assert(callQ(VK64_fn_vkGetQueryPoolResults, gr) == VK_NOT_READY);
            U64 qcbId; CmdBuf* qcb = createObj<CmdBuf>(K_CMDBUF, qcbId);
            VK64Args rs{}; rs.a[0] = qcbId; rs.a[1] = poolId; rs.a[2] = 0; rs.a[3] = 4;
            assert(callQ(VK64_fn_vkCmdResetQueryPool, rs) == 0);
            VK64Args bq{}; bq.a[0] = qcbId; bq.a[1] = poolId; bq.a[2] = 2;
            assert(callQ(VK64_fn_vkCmdBeginQuery, bq) == 0);
            VK64Args eq{}; eq.a[0] = qcbId; eq.a[1] = poolId; eq.a[2] = 2;
            assert(callQ(VK64_fn_vkCmdEndQuery, eq) == 0);
            assert(qcb->cmds.size() == 3 && qcb->cmds[0].kind == CMD_RESET_QUERY
                   && qcb->cmds[1].kind == CMD_BEGIN_QUERY && qcb->cmds[2].kind == CMD_END_QUERY);
            for (const Cmd& k : qcb->cmds) execQueryCommand(k);
            gr.a[2] = 2; memory.writeq(2112, 0);
            assert(callQ(VK64_fn_vkGetQueryPoolResults, gr) == VK_SUCCESS);
            assert(memory.readq(2112) == 1); // one sample passed: not fully occluded
            gr.a[2] = 0;
            assert(callQ(VK64_fn_vkGetQueryPoolResults, gr) == VK_NOT_READY);
            QueryPoolCreateInfo64Local tci{};
            tci.queryType = vkquery::VKQUERY_TIMESTAMP; tci.queryCount = 2;
            memory.memcpyToGuest(2048, &tci, sizeof(tci));
            memory.writeq(2080, 0);
            assert(callQ(VK64_fn_vkCreateQueryPool, cr) == VK_SUCCESS);
            U64 tsPoolId = memory.readq(2080);
            assert(tsPoolId != 0 && tsPoolId != poolId);
            U64 tcbId; CmdBuf* tcb = createObj<CmdBuf>(K_CMDBUF, tcbId);
            VK64Args wt{}; wt.a[0] = tcbId; wt.a[1] = 1; wt.a[2] = tsPoolId; wt.a[3] = 0;
            assert(callQ(VK64_fn_vkCmdWriteTimestamp, wt) == 0);
            wt.a[3] = 1;
            assert(callQ(VK64_fn_vkCmdWriteTimestamp, wt) == 0);
            assert(tcb->cmds.size() == 2 && tcb->cmds[0].kind == CMD_WRITE_TIMESTAMP
                   && tcb->cmds[1].kind == CMD_WRITE_TIMESTAMP);
            for (const Cmd& k : tcb->cmds) execQueryCommand(k);
            VK64Args tg{}; tg.a[0] = deviceId; tg.a[1] = tsPoolId; tg.a[2] = 0; tg.a[3] = 2;
            tg.a[4] = 16; tg.a[5] = 2112; tg.a[6] = 8; tg.a[7] = vkquery::VKQUERY_64_BIT;
            assert(callQ(VK64_fn_vkGetQueryPoolResults, tg) == VK_SUCCESS);
            assert(memory.readq(2112) != 0 && memory.readq(2120) > memory.readq(2112));
            VK64Args de{}; de.a[0] = deviceId; de.a[1] = poolId;
            assert(callQ(VK64_fn_vkDestroyQueryPool, de) == 0);
            assert(!findObj(poolId, K_QUERYPOOL));
            de.a[1] = tsPoolId;
            assert(callQ(VK64_fn_vkDestroyQueryPool, de) == 0);
            assert(!findObj(tsPoolId, K_QUERYPOOL));
            puts("PASS: query pools create, record, complete and destroy (64-bit)");
        }
        {
            KMemory memory32; CPU cpu32{&memory32};
            g_mem32 = &memory32;
            auto call32q = [&](U32 fn, const VK32Args& args) {
                memory32.memcpyToGuest(128, &args, sizeof(args));
                return vk32Bridge(&cpu32, fn, 128);
            };
            QueryPoolCreateInfo32Local ci{};
            ci.queryType = vkquery::VKQUERY_OCCLUSION; ci.queryCount = 16384; // DXVK occlusion allocator size
            memory32.memcpyToGuest(2048, &ci, sizeof(ci));
            memory32.writeq(2080, 0);
            VK32Args cr{}; cr.a[0] = 1; cr.a[1] = 2048; cr.a[3] = 2080;
            assert(call32q(VK64_fn_vkCreateQueryPool, cr) == VK_SUCCESS);
            U64 poolId = memory32.readq(2080);
            assert(poolId != 0);
            auto* pool = objOf32<QueryPool>(poolId, K_QUERYPOOL, "test");
            assert(pool && pool->count == 16384 && pool->available.size() == 16384);
            U64 qcbId; CmdBuf* qcb = createObj32<CmdBuf>(K_CMDBUF, qcbId);
            VK32Args bq{}; bq.a[0] = (U32)qcbId; bq.a[1] = (U32)poolId; bq.a[2] = 7;
            assert(call32q(VK64_fn_vkCmdBeginQuery, bq) == 0);
            VK32Args eq{}; eq.a[0] = (U32)qcbId; eq.a[1] = (U32)poolId; eq.a[2] = 7;
            assert(call32q(VK64_fn_vkCmdEndQuery, eq) == 0);
            assert(qcb->cmds.size() == 2 && qcb->cmds[0].kind == CMD_BEGIN_QUERY
                   && qcb->cmds[1].kind == CMD_END_QUERY);
            for (const Cmd& k : qcb->cmds) execQueryCommand(k);
            VK32Args gr{}; gr.a[1] = (U32)poolId; gr.a[2] = 7; gr.a[3] = 1;
            gr.a[4] = 8; gr.a[5] = 2112; gr.a[6] = 8; gr.a[7] = vkquery::VKQUERY_64_BIT;
            assert(call32q(VK64_fn_vkGetQueryPoolResults, gr) == VK_SUCCESS);
            assert(memory32.readq(2112) == 1);
            VK32Args de{}; de.a[1] = (U32)poolId;
            assert(call32q(VK64_fn_vkDestroyQueryPool, de) == 0);
            assert(!findObj(poolId, K_QUERYPOOL));
            g_mem32 = nullptr;
            puts("PASS: query pools create, record, complete and destroy (32-bit)");
        }
        while (!g_objs.empty()) dropObj(g_objs.begin()->first);
    }
}
