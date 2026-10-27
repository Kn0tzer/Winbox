/*
 * uav12.c -- D3D12 Typed UAV Load probe (Typed-UAV-Load caps lane, 2026-10-26).
 *
 * Phase 1: CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS) and print
 *   TypedUAVLoadAdditionalFormats -- the flag Godot's D3D12 driver requires
 *   in _check_capabilities (rendering_device_driver_d3d12.cpp:6166).
 * Phase 2: CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT) for the 18
 *   formats of vkd3d's d3d12_device_determine_additional_typed_uav_support
 *   set; print Support2 + the UAV_TYPED_LOAD bit for each.
 * Phase 3: end-to-end typed UAV loads through the PIXEL shader (the path
 *   that executes today; compute dispatch is still loud-dropped bridge-wide):
 *   a texture is filled with a known per-texel pattern via an upload copy,
 *   a fullscreen triangle's pixel shader Load()s texels from a UAV and
 *   writes them to a render target, and the RT is copied to a readback
 *   buffer and verified in-guest. Six formats, one per class:
 *   RGBA8_UNORM, RGBA32_FLOAT, RGBA16_FLOAT, R16_FLOAT, R32_UINT, R8_UINT.
 *
 * Machine-readable verdict on stderr: per-phase lines prefixed "uav12:",
 * "uav12: RESULT 0" iff the OPTIONS flag is TRUE, all 18 formats report
 * UAV_TYPED_LOAD, and every phase-3 execution check matches.
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o uav12.exe uav12.c -lole32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <stdio.h>
#include <string.h>
#include <math.h>

static void dbg(const char* s) { fprintf(stderr, "%s", s); fflush(stderr); }
#define FAIL(...) do { char b[512]; wsprintfA(b, __VA_ARGS__); dbg(b); \
    dbg("uav12: RESULT 1\n"); return 1; } while (0)

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
        char b[1024];
        wsprintfA(b, "uav12: FAIL D3DCompile(%s, %s) hr=0x%08X err=%s\n", what, target, (unsigned)hr,
                  err ? (const char*)ID3D10Blob_GetBufferPointer(err) : "(none)");
        dbg(b);
        if (err) ID3D10Blob_Release(err);
        return NULL;
    }
    if (err) ID3D10Blob_Release(err);
    return code;
}

/* COBJMACROS aggregate-return wrapper is broken in this mingw (see tri12.c). */
static void heap_start(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE* out)
{
    ((void (STDMETHODCALLTYPE *)(ID3D12DescriptorHeap*, D3D12_CPU_DESCRIPTOR_HANDLE*))
        heap->lpVtbl->GetCPUDescriptorHandleForHeapStart)(heap, out);
}
static void heap_start_gpu(ID3D12DescriptorHeap* heap, D3D12_GPU_DESCRIPTOR_HANDLE* out)
{
    ((void (STDMETHODCALLTYPE *)(ID3D12DescriptorHeap*, D3D12_GPU_DESCRIPTOR_HANDLE*))
        heap->lpVtbl->GetGPUDescriptorHandleForHeapStart)(heap, out);
}

static void res_desc(ID3D12Resource* res, D3D12_RESOURCE_DESC* out)
{
    ((void (STDMETHODCALLTYPE *)(ID3D12Resource*, D3D12_RESOURCE_DESC*))
        res->lpVtbl->GetDesc)(res, out);
}

static unsigned short f32_to_f16(float f)
{
    unsigned int x; memcpy(&x, &f, 4);
    unsigned int sign = (x >> 16) & 0x8000;
    int exp = (int)((x >> 23) & 0xFF) - 112;
    unsigned int mant = x & 0x7FFFFF;
    if (exp <= 0) return (unsigned short)sign;              /* flush tiny to zero (patterns avoid) */
    if (exp >= 31) return (unsigned short)(sign | 0x7BFF);  /* clamp */
    return (unsigned short)(sign | ((unsigned int)exp << 10) | (mant >> 13));
}

#define TW 4
#define TH 4
#define NIDX 16

enum { KIND_F4, KIND_F1, KIND_U1 };
struct exec_case {
    DXGI_FORMAT format; const char* name; int kind; int bpp;
    const char* ps_src;
};

static const char PS_F4[] =
    "RWTexture2D<float4> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { return uav[int2(pos.xy)]; }\n";
static const char PS_F1[] =
    "RWTexture2D<float> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { float v = uav[int2(pos.xy)]; return float4(v, v, v, 1.0); }\n";
static const char PS_U1[] =
    "RWTexture2D<uint> uav : register(u1);\n"
    "float4 main(float4 pos : SV_Position) : SV_Target { uint v = uav[int2(pos.xy)]; return float4(float(v) * (1.0/255.0), 0.0, 0.0, 1.0); }\n";

static const struct exec_case EXEC_CASES[] = {
    { DXGI_FORMAT_R8G8B8A8_UNORM,        "R8G8B8A8_UNORM",        KIND_F4, 4,  PS_F4 },
    { DXGI_FORMAT_R32G32B32A32_FLOAT,    "R32G32B32A32_FLOAT",    KIND_F4, 16, PS_F4 },
    { DXGI_FORMAT_R16G16B16A16_FLOAT,    "R16G16B16A16_FLOAT",    KIND_F4, 8,  PS_F4 },
    { DXGI_FORMAT_R16_FLOAT,             "R16_FLOAT",             KIND_F1, 2,  PS_F1 },
    { DXGI_FORMAT_R32_UINT,              "R32_UINT",              KIND_U1, 4,  PS_U1 },
    { DXGI_FORMAT_R8_UINT,               "R8_UINT",               KIND_U1, 1,  PS_U1 },
};

/* Expected RT byte (RGBA8 render target) for texel idx of a case, channel ch. */
static int expected_byte(const struct exec_case* c, int idx, int ch)
{
    switch (c->format) {
    case DXGI_FORMAT_R8G8B8A8_UNORM: {
        int v = ch == 0 ? 16 + idx * 13 : ch == 1 ? 250 - idx * 11 : ch == 2 ? 30 + idx * 7 : 255;
        return v; }
    case DXGI_FORMAT_R32G32B32A32_FLOAT:
    case DXGI_FORMAT_R16G16B16A16_FLOAT: {
        float v = ch == 0 ? idx * 0.0625f : ch == 1 ? 0.875f - idx * 0.03125f
                : ch == 2 ? 0.125f + idx * 0.015625f : 1.0f;
        return (int)(v * 255.0f + 0.5f); }
    case DXGI_FORMAT_R16_FLOAT: {
        float v = 0.0625f + idx * 0.0390625f;
        return ch == 3 ? 255 : (int)(v * 255.0f + 0.5f); }
    case DXGI_FORMAT_R32_UINT:
        return ch == 0 ? 9 + idx * 16 : ch == 3 ? 255 : 0;
    case DXGI_FORMAT_R8_UINT:
        return ch == 0 ? 5 + idx * 16 : ch == 3 ? 255 : 0;
    default: return 0;
    }
}
static int tolerance(const struct exec_case* c)
{
    return c->format == DXGI_FORMAT_R8G8B8A8_UNORM ? 1 : 2;
}

/* Fill one row of pattern bytes (texel size c->bpp) for row y. */
static void fill_row(const struct exec_case* c, int y, unsigned char* dst)
{
    for (int x = 0; x < TW; x++) {
        int idx = x + y * TW;
        unsigned char* t = dst + x * c->bpp;
        switch (c->format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            t[0] = (unsigned char)(16 + idx * 13); t[1] = (unsigned char)(250 - idx * 11);
            t[2] = (unsigned char)(30 + idx * 7);  t[3] = 255; break;
        case DXGI_FORMAT_R32G32B32A32_FLOAT: {
            float v[4] = { idx * 0.0625f, 0.875f - idx * 0.03125f, 0.125f + idx * 0.015625f, 1.0f };
            memcpy(t, v, 16); break; }
        case DXGI_FORMAT_R16G16B16A16_FLOAT: {
            float v[4] = { idx * 0.0625f, 0.875f - idx * 0.03125f, 0.125f + idx * 0.015625f, 1.0f };
            for (int k = 0; k < 4; k++) { unsigned short h = f32_to_f16(v[k]); memcpy(t + k * 2, &h, 2); }
            break; }
        case DXGI_FORMAT_R16_FLOAT: {
            unsigned short h = f32_to_f16(0.0625f + idx * 0.0390625f); memcpy(t, &h, 2); break; }
        case DXGI_FORMAT_R32_UINT: {
            unsigned int v = 9 + idx * 16; memcpy(t, &v, 4); break; }
        case DXGI_FORMAT_R8_UINT:
            t[0] = (unsigned char)(5 + idx * 16); break;
        default: break;
        }
    }
}

static ID3D12Resource* make_tex2d(ID3D12Device* dev, DXGI_FORMAT fmt, UINT flags,
                                  D3D12_RESOURCE_STATES initial, const char* what, HRESULT* outHr)
{
    D3D12_HEAP_PROPERTIES heap = {0};
    heap.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rd = {0};
    rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rd.Width = TW; rd.Height = TH; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
    rd.Format = fmt; rd.SampleDesc.Count = 1;
    rd.Flags = (D3D12_RESOURCE_FLAGS)flags;
    ID3D12Resource* res = NULL;
    HRESULT hr = ID3D12Device_CreateCommittedResource(dev, &heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                       initial, NULL, &IID_ID3D12Resource, (void**)&res);
    if (outHr) *outHr = hr;
    if (FAILED(hr)) { char b[256]; wsprintfA(b, "uav12: FAIL CreateCommittedResource(%s) hr=0x%08X\n", what, (unsigned)hr); dbg(b); }
    return res;
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

static int run_exec_case(ID3D12Device* dev, ID3D12CommandQueue* queue,
                         ID3D12CommandAllocator* alloc, ID3D12Fence* fence, UINT64* fenceValue,
                         ID3DBlob* vsBlob, PFN_D3D12_SERIALIZE_ROOT_SIG pSerialize,
                         const struct exec_case* c)
{
    char b[512];
    int ok = 1;
    ID3D12Resource* vb = NULL;
    ID3DBlob* psBlob = compile_shader(c->ps_src, "ps_5_0", c->name);
    if (!psBlob) { wsprintfA(b, "uav12: EXEC %-20s FAIL (shader compile)\n", c->name); dbg(b); return 0; }

    /* Root signature: one descriptor table, one UAV range at u0, pixel-visible. */
    D3D12_DESCRIPTOR_RANGE range = {0};
    range.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_UAV;
    range.NumDescriptors = 1; range.BaseShaderRegister = 1; /* u1: the HLSL compiler requires UAV indices above the render-target index this shader writes */ range.RegisterSpace = 0;
    range.OffsetInDescriptorsFromTableStart = 0;
    D3D12_ROOT_PARAMETER param = {0};
    param.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    param.DescriptorTable.NumDescriptorRanges = 1;
    param.DescriptorTable.pDescriptorRanges = &range;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    rsd.NumParameters = 1; rsd.pParameters = &param;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* sigBlob = NULL; ID3DBlob* sigErr = NULL;
    HRESULT hr = pSerialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &sigErr);
    if (FAILED(hr)) { wsprintfA(b, "uav12: EXEC %-20s FAIL (serialize root sig hr=0x%08X)\n", c->name, (unsigned)hr); dbg(b); ok = 0; goto out_ps; }
    ID3D12RootSignature* rootsig = NULL;
    hr = ID3D12Device_CreateRootSignature(dev, 0, ID3D10Blob_GetBufferPointer(sigBlob),
                                          ID3D10Blob_GetBufferSize(sigBlob), &IID_ID3D12RootSignature, (void**)&rootsig);
    if (FAILED(hr)) { wsprintfA(b, "uav12: EXEC %-20s FAIL (CreateRootSignature hr=0x%08X)\n", c->name, (unsigned)hr); dbg(b); ok = 0; goto out_sig; }

    /* PSO */
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {0};
    pso.pRootSignature = rootsig;
    pso.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(vsBlob);
    pso.VS.BytecodeLength = ID3D10Blob_GetBufferSize(vsBlob);
    pso.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(psBlob);
    pso.PS.BytecodeLength = ID3D10Blob_GetBufferSize(psBlob);
    /* Two creation gates in this vkd3d-proton build, bisected 2026-10-26
     * with the uav12x/uav12y/uav12z/uav12w/uav12w2/uav12w3 diagnostic
     * probes (matrix in the campaign report, section 4.17):
     * (1) a vertex shader whose input signature holds only SV_VertexID
     *     is rejected with E_INVALIDARG, with or without an input layout;
     *     a VS declaring POSITION/COLOR inputs matched by this layout is
     *     accepted -- hence the pass-through VS and the real vertex
     *     buffer bound in the draw below (the tri12draw shape).
     * (2) a root signature carrying a descriptor table makes creation
     *     fail one layer deeper (E_FAIL from VK_ERROR_FEATURE_NOT_PRESENT
     *     inside the bridge/page pipeline path) even with the accepted
     *     shader pair; an empty root signature accepts the same pair.
     *     Gate (2) is the current blocker for the exec phase. */
    static const D3D12_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    pso.InputLayout.pInputElementDescs = il;
    pso.InputLayout.NumElements = 2;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    ID3D12PipelineState* pipe = NULL;
    hr = ID3D12Device_CreateGraphicsPipelineState(dev, &pso, &IID_ID3D12PipelineState, (void**)&pipe);
    if (FAILED(hr)) { wsprintfA(b, "uav12: EXEC %-20s FAIL (CreateGraphicsPipelineState hr=0x%08X)\n", c->name, (unsigned)hr); dbg(b); ok = 0; goto out_root; }

    /* Resources */
    HRESULT hr2;
    ID3D12Resource* uavTex = make_tex2d(dev, c->format, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
                                        D3D12_RESOURCE_STATE_COPY_DEST, "uav texture", &hr2);
    if (!uavTex) { ok = 0; goto out_pipe; }
    ID3D12Resource* rtTex = make_tex2d(dev, DXGI_FORMAT_R8G8B8A8_UNORM, D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                                       D3D12_RESOURCE_STATE_RENDER_TARGET, "render target", &hr2);
    if (!rtTex) { ok = 0; goto out_uav; }

    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; hd.NumDescriptors = 1;
    hd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* uavHeap = NULL;
    hr = ID3D12Device_CreateDescriptorHeap(dev, &hd, &IID_ID3D12DescriptorHeap, (void**)&uavHeap);
    if (FAILED(hr)) { ok = 0; goto out_rt; }
    D3D12_CPU_DESCRIPTOR_HANDLE uavCpu; heap_start(uavHeap, &uavCpu);
    D3D12_UNORDERED_ACCESS_VIEW_DESC uavd = {0};
    uavd.Format = c->format; uavd.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
    ID3D12Device_CreateUnorderedAccessView(dev, uavTex, NULL, &uavd, uavCpu);

    D3D12_DESCRIPTOR_HEAP_DESC rth = {0};
    rth.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rth.NumDescriptors = 1;
    ID3D12DescriptorHeap* rtHeap = NULL;
    hr = ID3D12Device_CreateDescriptorHeap(dev, &rth, &IID_ID3D12DescriptorHeap, (void**)&rtHeap);
    if (FAILED(hr)) { ok = 0; goto out_heap; }
    D3D12_CPU_DESCRIPTOR_HANDLE rtCpu; heap_start(rtHeap, &rtCpu);
    ID3D12Device_CreateRenderTargetView(dev, rtTex, NULL, rtCpu);

    /* Vertex buffer for the pass-through VS: one fullscreen triangle,
     * POSITION float3 + COLOR float4 per vertex (the layout above). */
    {
        static const float verts[21] = {
            -1.0f, -1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
            -1.0f,  3.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
             3.0f, -1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
        };
        D3D12_HEAP_PROPERTIES vbHeap = {0}; vbHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
        D3D12_RESOURCE_DESC vbDesc = {0};
        vbDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; vbDesc.Width = sizeof(verts);
        vbDesc.Height = 1; vbDesc.DepthOrArraySize = 1; vbDesc.MipLevels = 1;
        vbDesc.SampleDesc.Count = 1; vbDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
        hr = ID3D12Device_CreateCommittedResource(dev, &vbHeap, D3D12_HEAP_FLAG_NONE, &vbDesc,
                                                  D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                                  &IID_ID3D12Resource, (void**)&vb);
        if (FAILED(hr)) { ok = 0; goto out_heap; }
        void* vbMapped = NULL; D3D12_RANGE vbRr = {0, 0};
        hr = ID3D12Resource_Map(vb, 0, &vbRr, &vbMapped);
        if (FAILED(hr)) { ok = 0; goto out_heap; }
        memcpy(vbMapped, verts, sizeof(verts));
        ID3D12Resource_Unmap(vb, 0, NULL);
    }

    /* Upload buffer sized by footprints. */
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT uavFp = {0}; UINT uavRows = 0; UINT64 uavRowBytes = 0, uavTotal = 0;
    { D3D12_RESOURCE_DESC d; res_desc(uavTex, &d);
      ID3D12Device_GetCopyableFootprints(dev, &d, 0, 1, 0, &uavFp, &uavRows, &uavRowBytes, &uavTotal); }
    D3D12_HEAP_PROPERTIES upHeap = {0}; upHeap.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC bufDesc = {0};
    bufDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; bufDesc.Width = uavTotal;
    bufDesc.Height = 1; bufDesc.DepthOrArraySize = 1; bufDesc.MipLevels = 1;
    bufDesc.SampleDesc.Count = 1; bufDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* upload = NULL;
    hr = ID3D12Device_CreateCommittedResource(dev, &upHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                              D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
                                              &IID_ID3D12Resource, (void**)&upload);
    if (FAILED(hr)) { ok = 0; goto out_rtheap; }
    {
        void* mapped = NULL; D3D12_RANGE rr = {0, 0};
        hr = ID3D12Resource_Map(upload, 0, &rr, &mapped);
        if (FAILED(hr)) { ok = 0; goto out_upload; }
        for (int y = 0; y < TH; y++)
            fill_row(c, y, (unsigned char*)mapped + uavFp.Footprint.RowPitch * y);
        ID3D12Resource_Unmap(upload, 0, NULL);
    }

    /* Readback buffer for the RT. */
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT rtFp = {0}; UINT rtRows = 0; UINT64 rtRowBytes = 0, rtTotal = 0;
    { D3D12_RESOURCE_DESC d; res_desc(rtTex, &d);
      ID3D12Device_GetCopyableFootprints(dev, &d, 0, 1, 0, &rtFp, &rtRows, &rtRowBytes, &rtTotal); }
    D3D12_HEAP_PROPERTIES rbHeap = {0}; rbHeap.Type = D3D12_HEAP_TYPE_READBACK;
    bufDesc.Width = rtTotal;
    ID3D12Resource* readback = NULL;
    hr = ID3D12Device_CreateCommittedResource(dev, &rbHeap, D3D12_HEAP_FLAG_NONE, &bufDesc,
                                               D3D12_RESOURCE_STATE_COPY_DEST, NULL,
                                               &IID_ID3D12Resource, (void**)&readback);
    if (FAILED(hr)) { ok = 0; goto out_upload; }

    /* Command list: upload copy, barrier, draw, barrier, readback copy. */
    ID3D12GraphicsCommandList* list = NULL;
    hr = ID3D12Device_CreateCommandList(dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, alloc, pipe,
                                        &IID_ID3D12GraphicsCommandList, (void**)&list);
    if (FAILED(hr)) { ok = 0; goto out_rb; }
    D3D12_TEXTURE_COPY_LOCATION dstLoc = {0}, srcLoc = {0};
    dstLoc.pResource = uavTex; dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; dstLoc.SubresourceIndex = 0;
    srcLoc.pResource = upload; srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; srcLoc.PlacedFootprint = uavFp;
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &dstLoc, 0, 0, 0, &srcLoc, NULL);
    D3D12_RESOURCE_BARRIER bar = {0};
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource = uavTex;
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &bar);
    ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtCpu, FALSE, NULL);
    D3D12_VIEWPORT vp = {0}; vp.Width = TW; vp.Height = TH; vp.MaxDepth = 1.0f;
    ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp);
    D3D12_RECT sc = {0, 0, TW, TH};
    ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc);
    const float clearCol[4] = { 1.0f, 0.0f, 1.0f, 1.0f };  /* magenta: never an expected value */
    ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtCpu, clearCol, 0, NULL);
    ID3D12GraphicsCommandList_SetPipelineState(list, pipe);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, rootsig);
    { ID3D12DescriptorHeap* heaps[1] = { uavHeap };
      ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, heaps); }
    D3D12_GPU_DESCRIPTOR_HANDLE uavGpu; heap_start_gpu(uavHeap, &uavGpu);
    ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, uavGpu);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    { D3D12_VERTEX_BUFFER_VIEW vbv = {0};
      vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
      vbv.SizeInBytes = 21 * sizeof(float); vbv.StrideInBytes = 7 * sizeof(float);
      ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv); }
    ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
    bar.Transition.pResource = rtTex;
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &bar);
    D3D12_TEXTURE_COPY_LOCATION rbDst = {0}, rtSrc = {0};
    rbDst.pResource = readback; rbDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; rbDst.PlacedFootprint = rtFp;
    rtSrc.pResource = rtTex; rtSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; rtSrc.SubresourceIndex = 0;
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &rbDst, 0, 0, 0, &rtSrc, NULL);
    hr = ID3D12GraphicsCommandList_Close(list);
    if (FAILED(hr)) { ok = 0; goto out_list; }
    { ID3D12CommandList* lists[1] = { (ID3D12CommandList*)list };
      ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists); }
    if (!wait_fence(queue, fence, fenceValue)) {
        wsprintfA(b, "uav12: EXEC %-20s FAIL (fence wait)\n", c->name); dbg(b);
        ok = 0; goto out_list;
    }
    {
        void* mapped = NULL; D3D12_RANGE rr = {0, (SIZE_T)rtTotal};
        hr = ID3D12Resource_Map(readback, 0, &rr, &mapped);
        if (FAILED(hr)) { wsprintfA(b, "uav12: EXEC %-20s FAIL (readback Map hr=0x%08X)\n", c->name, (unsigned)hr); dbg(b); ok = 0; goto out_list; }
        int mismatches = 0, firstIdx = -1, firstCh = 0, firstGot = 0, firstWant = 0;
        const unsigned char* base = (const unsigned char*)mapped;
        for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) {
            int idx = x + y * TW;
            const unsigned char* px = base + rtFp.Footprint.RowPitch * y + x * 4;
            for (int ch = 0; ch < 4; ch++) {
                int want = expected_byte(c, idx, ch), got = px[ch];
                if (abs(got - want) > tolerance(c)) {
                    mismatches++;
                    if (firstIdx < 0) { firstIdx = idx; firstCh = ch; firstGot = got; firstWant = want; }
                }
            }
        }
        ID3D12Resource_Unmap(readback, 0, NULL);
        if (mismatches) {
            wsprintfA(b, "uav12: EXEC %-20s FAIL mismatches=%d/%d first idx=%d ch=%d got=%d want=%d\n",
                      c->name, mismatches, NIDX * 4, firstIdx, firstCh, firstGot, firstWant);
            dbg(b); ok = 0;
        } else {
            wsprintfA(b, "uav12: EXEC %-20s OK (all %d texels match)\n", c->name, NIDX);
            dbg(b);
        }
    }
out_list: ID3D12GraphicsCommandList_Release(list);
out_rb: ID3D12Resource_Release(readback);
out_upload: ID3D12Resource_Release(upload);
out_rtheap: ID3D12DescriptorHeap_Release(rtHeap);
out_heap: ID3D12DescriptorHeap_Release(uavHeap);
out_rt: ID3D12Resource_Release(rtTex);
out_uav: ID3D12Resource_Release(uavTex);
    if (vb) ID3D12Resource_Release(vb);
out_pipe: ID3D12PipelineState_Release(pipe);
out_root: ID3D12RootSignature_Release(rootsig);
out_sig: ID3D10Blob_Release(sigBlob); if (sigErr) ID3D10Blob_Release(sigErr);
out_ps: ID3D10Blob_Release(psBlob);
    return ok;
}

static const DXGI_FORMAT SWEEP[] = {
    DXGI_FORMAT_R32_FLOAT, DXGI_FORMAT_R32_UINT, DXGI_FORMAT_R32_SINT,
    DXGI_FORMAT_R32G32B32A32_FLOAT, DXGI_FORMAT_R32G32B32A32_UINT, DXGI_FORMAT_R32G32B32A32_SINT,
    DXGI_FORMAT_R16G16B16A16_FLOAT, DXGI_FORMAT_R16G16B16A16_UINT, DXGI_FORMAT_R16G16B16A16_SINT,
    DXGI_FORMAT_R8G8B8A8_UNORM, DXGI_FORMAT_R8G8B8A8_UINT, DXGI_FORMAT_R8G8B8A8_SINT,
    DXGI_FORMAT_R16_FLOAT, DXGI_FORMAT_R16_UINT, DXGI_FORMAT_R16_SINT,
    DXGI_FORMAT_R8_UNORM, DXGI_FORMAT_R8_UINT, DXGI_FORMAT_R8_SINT,
};

int main(void)
{
    char b[512];
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (!d3d12) FAIL("uav12: FAIL LoadLibrary(d3d12.dll) err=%lu\n", (unsigned long)GetLastError());
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler) FAIL("uav12: FAIL LoadLibrary(d3dcompiler_47.dll) err=%lu\n", (unsigned long)GetLastError());
    pD3DCompile = (PFN_D3D_COMPILE)GetProcAddress(compiler, "D3DCompile");
    if (!pD3DCompile) FAIL("uav12: FAIL GetProcAddress(D3DCompile)\n");
    PFN_D3D12_CREATE_DEVICE pCreate =
        (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
    PFN_D3D12_SERIALIZE_ROOT_SIG pSerialize =
        (PFN_D3D12_SERIALIZE_ROOT_SIG)GetProcAddress(d3d12, "D3D12SerializeRootSignature");
    if (!pCreate || !pSerialize) FAIL("uav12: FAIL GetProcAddress(d3d12 exports)\n");

    ID3D12Device* dev = NULL;
    HRESULT hr = pCreate(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev);
    if (FAILED(hr)) FAIL("uav12: FAIL D3D12CreateDevice hr=0x%08X\n", (unsigned)hr);
    dbg("uav12: D3D12CreateDevice OK\n");

    /* Phase 1: the OPTIONS flag. */
    D3D12_FEATURE_DATA_D3D12_OPTIONS opts = {0};
    hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_D3D12_OPTIONS, &opts, sizeof(opts));
    if (FAILED(hr)) FAIL("uav12: FAIL CheckFeatureSupport(D3D12_OPTIONS) hr=0x%08X\n", (unsigned)hr);
    wsprintfA(b, "uav12: TypedUAVLoadAdditionalFormats = %s\n", opts.TypedUAVLoadAdditionalFormats ? "TRUE" : "FALSE");
    dbg(b);
    int flagOk = opts.TypedUAVLoadAdditionalFormats != 0;

    /* Phase 2: per-format support sweep. */
    int sweepOk = 1;
    for (unsigned i = 0; i < sizeof(SWEEP) / sizeof(SWEEP[0]); i++) {
        D3D12_FEATURE_DATA_FORMAT_SUPPORT fs = {0};
        fs.Format = SWEEP[i];
        hr = ID3D12Device_CheckFeatureSupport(dev, D3D12_FEATURE_FORMAT_SUPPORT, &fs, sizeof(fs));
        int typed = SUCCEEDED(hr) && (fs.Support2 & D3D12_FORMAT_SUPPORT2_UAV_TYPED_LOAD) != 0;
        if (!typed) sweepOk = 0;
        wsprintfA(b, "uav12: FORMAT %-22u Support1=0x%08X Support2=0x%08X UAV_TYPED_LOAD=%s\n",
                  (unsigned)SWEEP[i], (unsigned)fs.Support1, (unsigned)fs.Support2, typed ? "YES" : "NO");
        dbg(b);
    }

    /* Phase 3: execution. */
    D3D12_COMMAND_QUEUE_DESC qd = {0};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = NULL;
    hr = ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void**)&queue);
    if (FAILED(hr)) FAIL("uav12: FAIL CreateCommandQueue hr=0x%08X\n", (unsigned)hr);
    ID3D12CommandAllocator* alloc = NULL;
    hr = ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             &IID_ID3D12CommandAllocator, (void**)&alloc);
    if (FAILED(hr)) FAIL("uav12: FAIL CreateCommandAllocator hr=0x%08X\n", (unsigned)hr);
    ID3D12Fence* fence = NULL;
    hr = ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
    if (FAILED(hr)) FAIL("uav12: FAIL CreateFence hr=0x%08X\n", (unsigned)hr);
    UINT64 fenceValue = 0;

    static const char* VS_SRC =
        "struct VSIn { float3 pos : POSITION; float4 col : COLOR; };\n"
        "struct VSOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
        "VSOut main(VSIn i) { VSOut o; o.pos = float4(i.pos, 1.0); o.col = i.col; return o; }\n";
    ID3DBlob* vsBlob = compile_shader(VS_SRC, "vs_5_0", "fullscreen-vs");
    int execOk = 1;
    if (!vsBlob) execOk = 0;
    else {
        for (unsigned i = 0; i < sizeof(EXEC_CASES) / sizeof(EXEC_CASES[0]); i++) {
            if (!run_exec_case(dev, queue, alloc, fence, &fenceValue, vsBlob, pSerialize, &EXEC_CASES[i]))
                execOk = 0;
            ID3D12CommandAllocator_Reset(alloc);
        }
        ID3D10Blob_Release(vsBlob);
    }

    wsprintfA(b, "uav12: SUMMARY flag=%s sweep=%s exec=%s\n",
              flagOk ? "TRUE" : "FALSE", sweepOk ? "ALL18" : "INCOMPLETE", execOk ? "ALL6" : "FAILED");
    dbg(b);
    ID3D12Fence_Release(fence);
    ID3D12CommandAllocator_Release(alloc);
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(dev);
    if (flagOk && sweepOk && execOk) { dbg("uav12: RESULT 0\n"); return 0; }
    dbg("uav12: RESULT 1\n");
    return 1;
}
