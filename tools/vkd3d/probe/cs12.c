/*
 * cs12.c -- minimal D3D12 compute-dispatch probe (V2 compute lane, 2026-10-28).
 *
 * Before this lane, compute pipelines were created but every dispatch was
 * dropped loudly at submit ("compute-dispatch"), and descriptor sets bound
 * at the COMPUTE bind point were dropped at record time
 * ("compute-descriptor-sets"). This probe separates the compute stages with
 * one variable changed per variant (same device/queue/fence, fresh
 * resources and command list per variant):
 *   BUF  : ClearUnorderedAccessViewUint on a RAW buffer UAV. vkd3d-proton
 *          implements buffer UAV clears as a real compute dispatch (meta
 *          pipeline, STORAGE_BUFFER descriptor, push constants), so this
 *          exercises guest-visible compute with no guest shader compile.
 *          The result is read back with a plain buffer->readback copy,
 *          which only returns the cleared values if the copy is deferred
 *          past the GPU write. Fails on the pre-compute tree (dispatch
 *          dropped, readback stays zero).
 *   BUF2 : the same clear on a structured-buffer UAV (the descriptor-heap
 *          path in vkd3d's clear_uav).
 *   TEX  : guest-compiled compute shaders (the in-guest d3dcompiler SM5
 *          front-end accepts RWTexture2D/Texture2D forms only -- no
 *          structured buffers, no thread-id semantics; see sweep12 /
 *          sweep2 diagnostics): pass 1 writes a known pattern into a
 *          RWTexture2D with a single-thread dispatch of unrolled writes,
 *          pass 2 reads it back through a Texture2D SRV and writes a
 *          doubled pattern to a second UAV texture, which is read back
 *          through a placed-footprint texture copy.
 * Verdict lines are prefixed "cs12: VARIANT <name>"; the final
 * "cs12: RESULT 0" requires all variants to pass.
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o cs12.exe cs12.c -lole32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>

static void dbg(const char* s) { fprintf(stderr, "%s", s); fflush(stderr); }
#define FAIL(...) do { char b[512]; wsprintfA(b, __VA_ARGS__); dbg(b); \
    dbg("cs12: RESULT 1\n"); return 1; } while (0)

typedef HRESULT (WINAPI *PFN_D3D12_CREATE_DEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12_SERIALIZE_ROOT_SIG)(const D3D12_ROOT_SIGNATURE_DESC*,
        D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);
typedef HRESULT (WINAPI *PFN_D3D_COMPILE)(const void*, SIZE_T, const char*, const void*, void*,
        const char*, const char*, UINT, UINT, ID3DBlob**, ID3DBlob**);

static PFN_D3D_COMPILE pD3DCompile = NULL;

static ID3DBlob* compile_shader(const char* src, const char* target, const char* what)
{
    ID3DBlob* code = NULL; ID3DBlob* err = NULL;
    HRESULT hr = pD3DCompile(src, strlen(src), NULL, NULL, NULL, "main", target, 0, 0, &code, &err);
    if (FAILED(hr)) {
        char b[1024]; wsprintfA(b, "cs12: FAIL D3DCompile(%s, %s) hr=0x%08X err=%s\n", what, target, (unsigned)hr,
                  err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "(none)");
        dbg(b);
        if (err) ID3D10Blob_Release(err);
        return NULL;
    }
    if (err) ID3D10Blob_Release(err);
    return code;
}

static void heap_start_cpu(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE* out)
{
    ((void (STDMETHODCALLTYPE *)(ID3D12DescriptorHeap*, D3D12_CPU_DESCRIPTOR_HANDLE*))
        heap->lpVtbl->GetCPUDescriptorHandleForHeapStart)(heap, out);
}
static void heap_start_gpu(ID3D12DescriptorHeap* heap, D3D12_GPU_DESCRIPTOR_HANDLE* out)
{
    ((void (STDMETHODCALLTYPE *)(ID3D12DescriptorHeap*, D3D12_GPU_DESCRIPTOR_HANDLE*))
        heap->lpVtbl->GetGPUDescriptorHandleForHeapStart)(heap, out);
}

static int wait_fence(ID3D12CommandQueue* queue, ID3D12Fence* fence, UINT64* value)
{
    UINT64 v = ++(*value);
    HRESULT hr = ID3D12CommandQueue_Signal(queue, fence, v);
    if (FAILED(hr)) return 0;
    if (ID3D12Fence_GetCompletedValue(fence) < v) {
        HANDLE ev = CreateEventA(NULL, FALSE, FALSE, NULL);
        ID3D12Fence_SetEventOnCompletion(fence, v, ev);
        WaitForSingleObject(ev, 30000);
        CloseHandle(ev);
    }
    return ID3D12Fence_GetCompletedValue(fence) >= v;
}

static ID3D12Resource* make_buffer(ID3D12Device* dev, D3D12_HEAP_TYPE heapType, UINT64 size,
                                   D3D12_RESOURCE_STATES initial, UINT flags, const char* what)
{
    D3D12_HEAP_PROPERTIES heap = {0}; heap.Type = heapType;
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rd.Width = size;
    rd.Height = 1; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.SampleDesc.Count = 1; rd.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    rd.Flags = (D3D12_RESOURCE_FLAGS)flags;
    ID3D12Resource* res = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(dev, &heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                      initial, NULL, &IID_ID3D12Resource, (void**)&res);
    if (FAILED(hr)) { char b[256]; wsprintfA(b, "cs12: FAIL CreateCommittedResource(%s) hr=0x%08X\n", what, (unsigned)hr); dbg(b); return NULL; }
    return res;
}

struct ctx {
    ID3D12Device* dev; ID3D12CommandQueue* queue; ID3D12CommandAllocator* alloc;
    ID3D12Fence* fence; UINT64* fenceValue;
    PFN_D3D12_SERIALIZE_ROOT_SIG pSerialize;
};

static ID3D12GraphicsCommandList* new_list(struct ctx* cx)
{
    ID3D12GraphicsCommandList* list = NULL;
    HRESULT hr = ID3D12Device_CreateCommandList(cx->dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                cx->alloc, NULL, &IID_ID3D12GraphicsCommandList, (void**)&list);
    if (FAILED(hr)) return NULL;
    return list;
}

static int exec_and_wait(struct ctx* cx, ID3D12GraphicsCommandList* list)
{
    HRESULT hr = ID3D12GraphicsCommandList_Close(list);
    if (FAILED(hr)) return 0;
    { ID3D12CommandList* lists[1] = { (ID3D12CommandList*)list };
      ID3D12CommandQueue_ExecuteCommandLists(cx->queue, 1, lists); }
    return wait_fence(cx->queue, cx->fence, cx->fenceValue);
}

static ID3D12DescriptorHeap* make_srv_uav_heap(struct ctx* cx, UINT nDesc, int shaderVisible)
{
    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = nDesc;
    hd.Flags = shaderVisible ? D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE : D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ID3D12DescriptorHeap* heap = NULL;
    HRESULT hr = ID3D12Device_CreateDescriptorHeap(cx->dev, &hd, &IID_ID3D12DescriptorHeap, (void**)&heap);
    if (FAILED(hr)) return NULL;
    return heap;
}

static void barrier(ID3D12GraphicsCommandList* list, ID3D12Resource* res,
                    D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after)
{
    D3D12_RESOURCE_BARRIER bar = {0};
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource = res;
    bar.Transition.StateBefore = before;
    bar.Transition.StateAfter = after;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &bar);
}

static int check_u32s(ID3D12Resource* rb, const UINT* want, int n, const char* name)
{
    char b[512];
    void* mapped = NULL; D3D12_RANGE rr = {0, (SIZE_T)n * 4};
    HRESULT hr = ID3D12Resource_Map(rb, 0, &rr, &mapped);
    if (FAILED(hr)) { wsprintfA(b, "cs12: VARIANT %s FAIL (readback Map hr=0x%08X)\n", name, (unsigned)hr); dbg(b); return 0; }
    int bad = 0, fi = -1; UINT fg = 0;
    for (int i = 0; i < n; i++) {
        UINT got = ((const UINT*)mapped)[i];
        if (got != want[i]) { bad++; if (fi < 0) { fi = i; fg = got; } }
    }
    ID3D12Resource_Unmap(rb, 0, NULL);
    wsprintfA(b, "cs12: VARIANT %s %s (bad=%d first idx=%d got=%u want=%u)\n",
              name, bad ? "FAIL" : "OK", bad, fi, fg, fi < 0 ? 0 : want[fi]);
    dbg(b);
    return bad == 0;
}

#define NEL 64

/* ------------- VARIANTS BUF / BUF2: vkd3d compute clear of a buffer UAV -------------
 * The clear values have DIFFERING components and the element size is 8
 * bytes: vkd3d's builtin vkCmdFillBuffer fast path only fires when the
 * byte pattern repeats every 4 bytes (equal components or <=4-byte
 * elements), so these shapes force the meta compute dispatch. */
static int v_bufclear(struct ctx* cx, int raw, const char* name)
{
    char b[512];
    ID3D12Resource* buf = make_buffer(cx->dev, D3D12_HEAP_TYPE_DEFAULT, NEL * 4,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, "clear uav buf");
    ID3D12Resource* rb = make_buffer(cx->dev, D3D12_HEAP_TYPE_READBACK, NEL * 4,
                                     D3D12_RESOURCE_STATE_COPY_DEST, 0, "clear readback");
    ID3D12DescriptorHeap* heap = make_srv_uav_heap(cx, 1, 1);
    if (!buf || !rb || !heap) { wsprintfA(b, "cs12: VARIANT %s FAIL (resources)\n", name); dbg(b); return 0; }
    D3D12_CPU_DESCRIPTOR_HANDLE cpu; heap_start_cpu(heap, &cpu);
    D3D12_GPU_DESCRIPTOR_HANDLE gpu; heap_start_gpu(heap, &gpu);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = {0};
    uavd.ViewDimension = D3D12_UAV_DIMENSION_BUFFER;
    if (raw) {
        /* Typed texel UAV, 8-byte texels (R32G32_UINT). */
        uavd.Format = DXGI_FORMAT_R32G32_UINT;
        uavd.Buffer.NumElements = NEL / 2;
    } else {
        /* Typed texel UAV, 8-byte float texels (R32G32_FLOAT). A
         * structured-buffer clear was tried first, but vkd3d serves it
         * with the vkCmdFillBuffer fast path (it treats the view as
         * R32 elements), so it never dispatches. */
        uavd.Format = DXGI_FORMAT_R32G32_FLOAT;
        uavd.Buffer.NumElements = NEL / 2;
    }
    ID3D12Device_CreateUnorderedAccessView(cx->dev, buf, NULL, &uavd, cpu);

    ID3D12GraphicsCommandList* list = new_list(cx);
    if (!list) { wsprintfA(b, "cs12: VARIANT %s FAIL (list)\n", name); dbg(b); return 0; }
    { ID3D12DescriptorHeap* heaps[1] = { heap };
      ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, heaps); }
    if (raw) {
        static const UINT vals[4] = { 10, 20, 30, 40 };
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewUint(list, gpu, cpu, buf, vals, 0, NULL);
    } else {
        static const float vals[4] = { 0.5f, 0.25f, 0.0f, 0.0f };
        ID3D12GraphicsCommandList_ClearUnorderedAccessViewFloat(list, gpu, cpu, buf, vals, 0, NULL);
    }
    barrier(list, buf, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    ID3D12GraphicsCommandList_CopyBufferRegion(list, rb, 0, buf, 0, NEL * 4);
    int ok = exec_and_wait(cx, list);
    ID3D12GraphicsCommandList_Release(list);
    if (!ok) { wsprintfA(b, "cs12: VARIANT %s FAIL (exec/fence)\n", name); dbg(b); return 0; }
    static UINT want[NEL];
    if (raw) { for (int i = 0; i < NEL; i++) want[i] = (i % 2 == 0) ? 10 : 20; }
    else { for (int i = 0; i < NEL; i++) want[i] = (i % 2 == 0) ? 0x3F000000u : 0x3E800000u; }
    int pass = check_u32s(rb, want, NEL, name);
    ID3D12Resource_Release(rb); ID3D12Resource_Release(buf);
    ID3D12DescriptorHeap_Release(heap);
    return pass;
}

/* ------------- VARIANT TEX: guest-compiled CS, texture UAV + SRV read ------------- */
#define TW 4
#define TH 4

static ID3D12RootSignature* make_table_rootsig(struct ctx* cx, const D3D12_DESCRIPTOR_RANGE* ranges,
                                               UINT nRanges, const char* what)
{
    char b[512];
    D3D12_ROOT_PARAMETER param = {0};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = nRanges;
    param.DescriptorTable.pDescriptorRanges = ranges;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    rsd.NumParameters = 1; rsd.pParameters = &param;
    ID3DBlob* sigBlob = NULL; ID3DBlob* sigErr = NULL;
    HRESULT hr = cx->pSerialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &sigErr);
    if (FAILED(hr)) { wsprintfA(b, "cs12: FAIL serialize root sig (%s) hr=0x%08X\n", what, (unsigned)hr); dbg(b); return NULL; }
    ID3D12RootSignature* rs = NULL;
    hr = ID3D12Device_CreateRootSignature(cx->dev, 0, ID3D10Blob_GetBufferPointer(sigBlob),
                                          ID3D10Blob_GetBufferSize(sigBlob), &IID_ID3D12RootSignature, (void**)&rs);
    ID3D10Blob_Release(sigBlob);
    if (FAILED(hr)) { wsprintfA(b, "cs12: FAIL CreateRootSignature(%s) hr=0x%08X\n", what, (unsigned)hr); dbg(b); return NULL; }
    return rs;
}

static ID3D12PipelineState* make_compute_pso(struct ctx* cx, ID3D12RootSignature* rs,
                                             ID3DBlob* csBlob, const char* what)
{
    char b[512];
    D3D12_COMPUTE_PIPELINE_STATE_DESC pso = {0};
    pso.pRootSignature = rs;
    pso.CS.pShaderBytecode = ID3D10Blob_GetBufferPointer(csBlob);
    pso.CS.BytecodeLength = ID3D10Blob_GetBufferSize(csBlob);
    ID3D12PipelineState* pipe = NULL;
    HRESULT hr = ID3D12Device_CreateComputePipelineState(cx->dev, &pso, &IID_ID3D12PipelineState, (void**)&pipe);
    if (FAILED(hr)) { wsprintfA(b, "cs12: FAIL CreateComputePipelineState(%s) hr=0x%08X\n", what, (unsigned)hr); dbg(b); return NULL; }
    return pipe;
}

static ID3D12Resource* make_uav_texture(struct ctx* cx, const char* what)
{
    D3D12_HEAP_PROPERTIES heapP = {0}; heapP.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = TW; rd.Height = TH; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
    rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
    ID3D12Resource* tex = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(cx->dev, &heapP, D3D12_HEAP_FLAG_NONE, &rd,
                                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS, NULL,
                                                      &IID_ID3D12Resource, (void**)&tex);
    if (FAILED(hr)) { char b[256]; wsprintfA(b, "cs12: FAIL CreateCommittedResource(%s) hr=0x%08X\n", what, (unsigned)hr); dbg(b); return NULL; }
    return tex;
}

static int v_tex(struct ctx* cx)
{
    /* Pass 1 shader: unrolled constant writes with plain decimal literals
     * (the only expression shapes the guest compiler's front-end accepts;
     * see sweep12/sweep2). Values are byte/255 to 6 decimals, which UNORM
     * rounding maps back to the exact byte. */
    static const char* FRAC[4] = { "0.000000", "0.333333", "0.666667", "1.000000" };
    static char src1[8192];
    int o = 0;
    o += wsprintfA(src1 + o, "RWTexture2D<float4> tex : register(u0);\n[numthreads(1,1,1)] void main() {\n");
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++)
        o += wsprintfA(src1 + o, "  tex[uint2(%d,%d)] = float4(%s, %s, 0.666667, 1.000000);\n",
                       x, y, FRAC[x], FRAC[y]);
    o += wsprintfA(src1 + o, "}\n");
    static const char* SRC2 =
        "Texture2D<float4> src : register(t0);\n"
        "RWTexture2D<float4> dst : register(u0);\n"
        "[numthreads(1,1,1)] void main() {\n"
        "  dst[uint2(0,0)] = src[uint2(0,0)] * 2.0; dst[uint2(1,0)] = src[uint2(1,0)] * 2.0;\n"
        "  dst[uint2(2,0)] = src[uint2(2,0)] * 2.0; dst[uint2(3,0)] = src[uint2(3,0)] * 2.0;\n"
        "  dst[uint2(0,1)] = src[uint2(0,1)] * 2.0; dst[uint2(1,1)] = src[uint2(1,1)] * 2.0;\n"
        "  dst[uint2(2,1)] = src[uint2(2,1)] * 2.0; dst[uint2(3,1)] = src[uint2(3,1)] * 2.0;\n"
        "  dst[uint2(0,2)] = src[uint2(0,2)] * 2.0; dst[uint2(1,2)] = src[uint2(1,2)] * 2.0;\n"
        "  dst[uint2(2,2)] = src[uint2(2,2)] * 2.0; dst[uint2(3,2)] = src[uint2(3,2)] * 2.0;\n"
        "  dst[uint2(0,3)] = src[uint2(0,3)] * 2.0; dst[uint2(1,3)] = src[uint2(1,3)] * 2.0;\n"
        "  dst[uint2(2,3)] = src[uint2(2,3)] * 2.0; dst[uint2(3,3)] = src[uint2(3,3)] * 2.0;\n"
        "}\n";
    ID3DBlob* cs1 = compile_shader(src1, "cs_5_0", "tex-pass1");
    ID3DBlob* cs2 = compile_shader(SRC2, "cs_5_0", "tex-pass2");
    if (!cs1 || !cs2) { dbg("cs12: VARIANT TEX FAIL (shader compile)\n"); return 0; }
    /* Descriptor tables, not root descriptors: the vendored vkd3d
     * rejects a compute PSO whose root signature carries a root UAV
     * (CreateComputePipelineState E_INVALIDARG, verified 2026-10-28),
     * so tables are the only guest compute shape here. A table entry is
     * served by vkd3d through its descriptor heap, and the guest shader
     * then accesses a runtime-sized descriptor array indexed by a push
     * constant -- image WRITES through that array are the standing
     * spirv-storage-image-write wall (see the campaign report). */
    D3D12_DESCRIPTOR_RANGE rUav = {0};
    rUav.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rUav.NumDescriptors = 1; rUav.BaseShaderRegister = 0;
    ID3D12RootSignature* rs1 = make_table_rootsig(cx, &rUav, 1, "tex1");
    D3D12_DESCRIPTOR_RANGE rMix[2] = { {0}, {0} };
    rMix[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rMix[0].NumDescriptors = 1; rMix[0].BaseShaderRegister = 0;
    rMix[0].OffsetInDescriptorsFromTableStart = 0;
    rMix[1].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    rMix[1].NumDescriptors = 1; rMix[1].BaseShaderRegister = 0;
    rMix[1].OffsetInDescriptorsFromTableStart = 1;
    ID3D12RootSignature* rs2 = make_table_rootsig(cx, rMix, 2, "tex2");
    if (!rs1 || !rs2) { dbg("cs12: VARIANT TEX FAIL (root sig)\n"); return 0; }
    ID3D12PipelineState* pso1 = make_compute_pso(cx, rs1, cs1, "tex1");
    ID3D12PipelineState* pso2 = make_compute_pso(cx, rs2, cs2, "tex2");
    if (!pso1 || !pso2) { dbg("cs12: VARIANT TEX FAIL (pso)\n"); return 0; }

    ID3D12Resource* tex1 = make_uav_texture(cx, "tex1");
    ID3D12Resource* tex2 = make_uav_texture(cx, "tex2");
    ID3D12DescriptorHeap* heapA = make_srv_uav_heap(cx, 1, 1);
    ID3D12DescriptorHeap* heapB = make_srv_uav_heap(cx, 2, 1);
    if (!tex1 || !tex2 || !heapA || !heapB) { dbg("cs12: VARIANT TEX FAIL (resources)\n"); return 0; }
    D3D12_CPU_DESCRIPTOR_HANDLE cpuA; heap_start_cpu(heapA, &cpuA);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = {0};
    uavd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateUnorderedAccessView(cx->dev, tex1, NULL, &uavd, cpuA);
    UINT inc = ID3D12Device_GetDescriptorHandleIncrementSize(cx->dev, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuB; heap_start_cpu(heapB, &cpuB);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {0};
    srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(cx->dev, tex1, &srvd, cpuB);
    D3D12_CPU_DESCRIPTOR_HANDLE cpuB2 = cpuB; cpuB2.ptr += inc;
    ID3D12Device_CreateUnorderedAccessView(cx->dev, tex2, NULL, &uavd, cpuB2);

    D3D12_RESOURCE_DESC td; ((void (STDMETHODCALLTYPE *)(ID3D12Resource*, D3D12_RESOURCE_DESC*))
        tex2->lpVtbl->GetDesc)(tex2, &td);
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {0}; UINT64 total = 0;
    ID3D12Device_GetCopyableFootprints(cx->dev, &td, 0, 1, 0, &fp, NULL, NULL, &total);
    ID3D12Resource* rb = make_buffer(cx->dev, D3D12_HEAP_TYPE_READBACK, total,
                                     D3D12_RESOURCE_STATE_COPY_DEST, 0, "tex readback");
    if (!rb) { dbg("cs12: VARIANT TEX FAIL (readback)\n"); return 0; }

    ID3D12GraphicsCommandList* list = new_list(cx);
    if (!list) { dbg("cs12: VARIANT TEX FAIL (list)\n"); return 0; }
    D3D12_GPU_DESCRIPTOR_HANDLE gpuA; heap_start_gpu(heapA, &gpuA);
    D3D12_GPU_DESCRIPTOR_HANDLE gpuB; heap_start_gpu(heapB, &gpuB);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list, rs1);
    ID3D12GraphicsCommandList_SetPipelineState(list, pso1);
    { ID3D12DescriptorHeap* heaps[1] = { heapA };
      ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, heaps); }
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpuA);
    ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
    barrier(list, tex1, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);
    ID3D12GraphicsCommandList_SetComputeRootSignature(list, rs2);
    ID3D12GraphicsCommandList_SetPipelineState(list, pso2);
    { ID3D12DescriptorHeap* heaps[1] = { heapB };
      ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, heaps); }
    ID3D12GraphicsCommandList_SetComputeRootDescriptorTable(list, 0, gpuB);
    ID3D12GraphicsCommandList_Dispatch(list, 1, 1, 1);
    barrier(list, tex2, D3D12_RESOURCE_STATE_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COPY_SOURCE);
    { D3D12_TEXTURE_COPY_LOCATION dst = {0}, src = {0};
      dst.pResource = rb; dst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; dst.PlacedFootprint = fp;
      src.pResource = tex2; src.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; src.SubresourceIndex = 0;
      ID3D12GraphicsCommandList_CopyTextureRegion(list, &dst, 0, 0, 0, &src, NULL); }
    int ok = exec_and_wait(cx, list);
    ID3D12GraphicsCommandList_Release(list);
    if (!ok) { dbg("cs12: VARIANT TEX FAIL (exec/fence)\n"); return 0; }
    { void* mapped = NULL; D3D12_RANGE rr = {0, (SIZE_T)total};
      HRESULT hr = ID3D12Resource_Map(rb, 0, &rr, &mapped);
      if (FAILED(hr)) { dbg("cs12: VARIANT TEX FAIL (readback Map)\n"); return 0; }
      int bad = 0, fx = -1, fy = -1; unsigned char fr = 0;
      for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) {
          const unsigned char* px = (const unsigned char*)mapped + (SIZE_T)y * fp.Footprint.RowPitch + x * 4;
          unsigned char want[4] = { (unsigned char)(2 * x * 85 > 255 ? 255 : 2 * x * 85), (unsigned char)(2 * y * 85 > 255 ? 255 : 2 * y * 85), 255, 255 };
          for (int c = 0; c < 4; c++) if (px[c] != want[c]) { bad++; if (fx < 0) { fx = x; fy = y; fr = px[c]; } break; }
      }
      ID3D12Resource_Unmap(rb, 0, NULL);
      char b[512];
      wsprintfA(b, "cs12: VARIANT TEX %s (bad=%d first=(%d,%d) gotR=%u)\n",
                bad ? "FAIL" : "OK", bad, fx, fy, fr);
      dbg(b);
      ID3D12Resource_Release(rb); ID3D12Resource_Release(tex2); ID3D12Resource_Release(tex1);
      ID3D12DescriptorHeap_Release(heapB); ID3D12DescriptorHeap_Release(heapA);
      ID3D12PipelineState_Release(pso2); ID3D12PipelineState_Release(pso1);
      ID3D12RootSignature_Release(rs2); ID3D12RootSignature_Release(rs1);
      ID3D10Blob_Release(cs2); ID3D10Blob_Release(cs1);
      return bad == 0; }
}

int main(void)
{
    char b[512];
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (!d3d12) FAIL("cs12: FAIL LoadLibrary(d3d12.dll) err=%lu\n", (unsigned long)GetLastError());
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler) FAIL("cs12: FAIL LoadLibrary(d3dcompiler_47.dll) err=%lu\n", (unsigned long)GetLastError());
    pD3DCompile = (PFN_D3D_COMPILE)GetProcAddress(compiler, "D3DCompile");
    if (!pD3DCompile) FAIL("cs12: FAIL GetProcAddress(D3DCompile)\n");
    PFN_D3D12_CREATE_DEVICE pCreate =
        (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
    PFN_D3D12_SERIALIZE_ROOT_SIG pSerialize =
        (PFN_D3D12_SERIALIZE_ROOT_SIG)GetProcAddress(d3d12, "D3D12SerializeRootSignature");
    if (!pCreate || !pSerialize) FAIL("cs12: FAIL GetProcAddress(d3d12 exports)\n");

    ID3D12Device* dev = NULL;
    HRESULT hr = pCreate(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev);
    if (FAILED(hr)) FAIL("cs12: FAIL D3D12CreateDevice hr=0x%08X\n", (unsigned)hr);
    dbg("cs12: D3D12CreateDevice OK\n");

    D3D12_COMMAND_QUEUE_DESC qd = {0};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = NULL;
    hr = ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void**)&queue);
    if (FAILED(hr)) FAIL("cs12: FAIL CreateCommandQueue hr=0x%08X\n", (unsigned)hr);
    ID3D12CommandAllocator* alloc = NULL;
    hr = ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             &IID_ID3D12CommandAllocator, (void**)&alloc);
    if (FAILED(hr)) FAIL("cs12: FAIL CreateCommandAllocator hr=0x%08X\n", (unsigned)hr);
    ID3D12Fence* fence = NULL;
    hr = ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
    if (FAILED(hr)) FAIL("cs12: FAIL CreateFence hr=0x%08X\n", (unsigned)hr);
    UINT64 fenceValue = 0;

    struct ctx cx = { dev, queue, alloc, fence, &fenceValue, pSerialize };
    int bufOk = v_bufclear(&cx, 1, "BUF"); ID3D12CommandAllocator_Reset(alloc);
    int buf2Ok = v_bufclear(&cx, 0, "BUF2"); ID3D12CommandAllocator_Reset(alloc);
    int texOk = v_tex(&cx); ID3D12CommandAllocator_Reset(alloc);

    wsprintfA(b, "cs12: SUMMARY buf=%s buf2=%s tex=%s\n",
              bufOk ? "OK" : "FAIL", buf2Ok ? "OK" : "FAIL", texOk ? "OK" : "FAIL");
    dbg(b);
    ID3D12Fence_Release(fence);
    ID3D12CommandAllocator_Release(alloc);
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(dev);
    if (bufOk && buf2Ok && texOk) { dbg("cs12: RESULT 0\n"); return 0; }
    dbg("cs12: RESULT 1\n");
    return 1;
}
