/*
 * root12.c -- D3D12 root-descriptor probe (Route A / R15 lane, 2026-10-27).
 *
 * Exercises the three root-descriptor kinds SEPARATELY (no descriptor
 * tables): a root CBV, a root SRV and a root UAV, each with its own root
 * signature + PSO, one
 * fullscreen-triangle draw, and an in-guest readback verification:
 *   cbv: PS returns col * mul + add with mul/add from the root CBV.
 *   srv: PS returns asfloat(words[0..2]) loaded from the root SRV.
 *   uav: PS Stores 0xA5000000|idx at word idx of the root UAV; the
 *        buffer is then copied to a readback buffer and checked.
 * SRV/UAV flavor note (2026-10-27): the natural HLSL for these cases is
 * ByteAddressBuffer / RWByteAddressBuffer, but Wine 10's builtin
 * d3dcompiler_47 HLSL front end cannot parse those type names at all
 * ("E5000: syntax error, unexpected NEW_IDENTIFIER" at 1:1), and the only
 * genuine Microsoft d3dcompiler_47.dll on this box is a 32-bit build a
 * 64-bit guest cannot load. The cases therefore use the typed forms
 * Buffer<uint> / RWBuffer<uint>: a root SRV/UAV is a raw GPU VA in the
 * root signature regardless of the shader's declared buffer flavor, so
 * vkd3d compiles the same root-descriptor (physical pointer) code path;
 * only the pointee element type differs.
 * Per-case verdict lines are prefixed "root12: CASE <name>"; the final
 * "root12: RESULT 0" requires all three cases to create their PSO,
 * execute, and match readback. PSO-creation failure (today's expected
 * landing point: the bridge audit rejects genuinely-used cap 5347)
 * prints the failing HRESULT per case and RESULT 1.
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o root12.exe root12.c -lole32
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
    dbg("root12: RESULT 1\n"); return 1; } while (0)

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
        wsprintfA(b, "root12: FAIL D3DCompile(%s, %s) hr=0x%08X err=%s\n", what, target, (unsigned)hr,
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
static void res_desc(ID3D12Resource* res, D3D12_RESOURCE_DESC* out)
{
    ((void (STDMETHODCALLTYPE *)(ID3D12Resource*, D3D12_RESOURCE_DESC*))
        res->lpVtbl->GetDesc)(res, out);
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

#define TW 4
#define TH 4

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
    if (FAILED(hr)) { char b[256]; wsprintfA(b, "root12: FAIL CreateCommittedResource(%s) hr=0x%08X\n", what, (unsigned)hr); dbg(b); return NULL; }
    return res;
}

static ID3D12Resource* make_upload_filled(ID3D12Device* dev, const void* data, UINT64 size, const char* what)
{
    ID3D12Resource* res = make_buffer(dev, D3D12_HEAP_TYPE_UPLOAD, size,
                                      D3D12_RESOURCE_STATE_GENERIC_READ, 0, what);
    if (!res) return NULL;
    void* mapped = NULL; D3D12_RANGE rr = {0, 0};
    HRESULT hr = ID3D12Resource_Map(res, 0, &rr, &mapped);
    if (FAILED(hr)) { ID3D12Resource_Release(res); return NULL; }
    memcpy(mapped, data, (size_t)size);
    ID3D12Resource_Unmap(res, 0, NULL);
    return res;
}

struct draw_ctx {
    ID3D12Device* dev; ID3D12CommandQueue* queue; ID3D12CommandAllocator* alloc;
    ID3D12Fence* fence; UINT64* fenceValue;
    ID3DBlob* vsBlob;
    PFN_D3D12_SERIALIZE_ROOT_SIG pSerialize;
};

/* Draw the fullscreen triangle with (rootsig, psBlob); bindRoot(list) sets
 * the case's root argument. Returns the RT readback buffer (caller
 * releases) or NULL on failure; per-step failures print root12 lines. */
static ID3D12Resource* draw_and_readback(struct draw_ctx* cx, ID3D12RootSignature* rootsig,
                                         ID3DBlob* psBlob,
                                         void (*bindRoot)(ID3D12GraphicsCommandList*, void*), void* bindCtx,
                                         const char* name, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* outFp)
{
    char b[512];
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {0};
    pso.pRootSignature = rootsig;
    pso.VS.pShaderBytecode = ID3D10Blob_GetBufferPointer(cx->vsBlob);
    pso.VS.BytecodeLength = ID3D10Blob_GetBufferSize(cx->vsBlob);
    pso.PS.pShaderBytecode = ID3D10Blob_GetBufferPointer(psBlob);
    pso.PS.BytecodeLength = ID3D10Blob_GetBufferSize(psBlob);
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
    HRESULT hr = ID3D12Device_CreateGraphicsPipelineState(cx->dev, &pso, &IID_ID3D12PipelineState, (void**)&pipe);
    if (FAILED(hr)) {
        wsprintfA(b, "root12: CASE %-4s FAIL (CreateGraphicsPipelineState hr=0x%08X)\n", name, (unsigned)hr);
        dbg(b); return NULL;
    }
    wsprintfA(b, "root12: CASE %-4s PSO OK\n", name); dbg(b);

    ID3D12Resource* rtTex = NULL;
    {
        D3D12_HEAP_PROPERTIES heap = {0}; heap.Type = D3D12_HEAP_TYPE_DEFAULT;
        D3D12_RESOURCE_DESC rd = {0};
        rd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
        rd.Width = TW; rd.Height = TH; rd.DepthOrArraySize = 1; rd.MipLevels = 1;
        rd.Format = DXGI_FORMAT_R8G8B8A8_UNORM; rd.SampleDesc.Count = 1;
        rd.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        hr = ID3D12Device_CreateCommittedResource(cx->dev, &heap, D3D12_HEAP_FLAG_NONE, &rd,
                                                  D3D12_RESOURCE_STATE_RENDER_TARGET, NULL,
                                                  &IID_ID3D12Resource, (void**)&rtTex);
        if (FAILED(hr)) { ID3D12PipelineState_Release(pipe); return NULL; }
    }
    D3D12_DESCRIPTOR_HEAP_DESC rth = {0};
    rth.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; rth.NumDescriptors = 1;
    ID3D12DescriptorHeap* rtHeap = NULL;
    hr = ID3D12Device_CreateDescriptorHeap(cx->dev, &rth, &IID_ID3D12DescriptorHeap, (void**)&rtHeap);
    if (FAILED(hr)) { ID3D12Resource_Release(rtTex); ID3D12PipelineState_Release(pipe); return NULL; }
    D3D12_CPU_DESCRIPTOR_HANDLE rtCpu; heap_start(rtHeap, &rtCpu);
    ID3D12Device_CreateRenderTargetView(cx->dev, rtTex, NULL, rtCpu);

    static const float verts[21] = {
        -1.0f, -1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
        -1.0f,  3.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
         3.0f, -1.0f, 0.0f,  1.0f, 1.0f, 1.0f, 1.0f,
    };
    ID3D12Resource* vb = make_upload_filled(cx->dev, verts, sizeof(verts), "vb");
    if (!vb) { ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rtTex); ID3D12PipelineState_Release(pipe); return NULL; }

    D3D12_PLACED_SUBRESOURCE_FOOTPRINT rtFp = {0}; UINT rtRows = 0; UINT64 rtRowBytes = 0, rtTotal = 0;
    { D3D12_RESOURCE_DESC d; res_desc(rtTex, &d);
      ID3D12Device_GetCopyableFootprints(cx->dev, &d, 0, 1, 0, &rtFp, &rtRows, &rtRowBytes, &rtTotal); }
    ID3D12Resource* readback = make_buffer(cx->dev, D3D12_HEAP_TYPE_READBACK, rtTotal,
                                           D3D12_RESOURCE_STATE_COPY_DEST, 0, "rt readback");
    if (!readback) { ID3D12Resource_Release(vb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rtTex); ID3D12PipelineState_Release(pipe); return NULL; }

    ID3D12GraphicsCommandList* list = NULL;
    hr = ID3D12Device_CreateCommandList(cx->dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT, cx->alloc, pipe,
                                        &IID_ID3D12GraphicsCommandList, (void**)&list);
    if (FAILED(hr)) { ID3D12Resource_Release(readback); ID3D12Resource_Release(vb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rtTex); ID3D12PipelineState_Release(pipe); return NULL; }
    ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtCpu, FALSE, NULL);
    D3D12_VIEWPORT vp = {0}; vp.Width = TW; vp.Height = TH; vp.MaxDepth = 1.0f;
    ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp);
    D3D12_RECT sc = {0, 0, TW, TH};
    ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc);
    const float clearCol[4] = { 1.0f, 0.0f, 1.0f, 1.0f };
    ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtCpu, clearCol, 0, NULL);
    ID3D12GraphicsCommandList_SetPipelineState(list, pipe);
    ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, rootsig);
    bindRoot(list, bindCtx);
    ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    { D3D12_VERTEX_BUFFER_VIEW vbv = {0};
      vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
      vbv.SizeInBytes = sizeof(verts); vbv.StrideInBytes = 7 * sizeof(float);
      ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv); }
    ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
    D3D12_RESOURCE_BARRIER bar = {0};
    bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    bar.Transition.pResource = rtTex;
    bar.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
    bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
    bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
    ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &bar);
    D3D12_TEXTURE_COPY_LOCATION rbDst = {0}, rtSrc = {0};
    rbDst.pResource = readback; rbDst.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT; rbDst.PlacedFootprint = rtFp;
    rtSrc.pResource = rtTex; rtSrc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX; rtSrc.SubresourceIndex = 0;
    ID3D12GraphicsCommandList_CopyTextureRegion(list, &rbDst, 0, 0, 0, &rtSrc, NULL);
    hr = ID3D12GraphicsCommandList_Close(list);
    if (FAILED(hr)) { ID3D12GraphicsCommandList_Release(list); ID3D12Resource_Release(readback); ID3D12Resource_Release(vb); ID3D12DescriptorHeap_Release(rtHeap); ID3D12Resource_Release(rtTex); ID3D12PipelineState_Release(pipe); return NULL; }
    { ID3D12CommandList* lists[1] = { (ID3D12CommandList*)list };
      ID3D12CommandQueue_ExecuteCommandLists(cx->queue, 1, lists); }
    int fenceOk = wait_fence(cx->queue, cx->fence, cx->fenceValue);
    ID3D12GraphicsCommandList_Release(list);
    ID3D12Resource_Release(vb);
    ID3D12DescriptorHeap_Release(rtHeap);
    ID3D12Resource_Release(rtTex);
    ID3D12PipelineState_Release(pipe);
    if (!fenceOk) { wsprintfA(b, "root12: CASE %-4s FAIL (fence wait)\n", name); dbg(b); ID3D12Resource_Release(readback); return NULL; }
    *outFp = rtFp;
    return readback;
}

static int check_rt(ID3D12Resource* readback, D3D12_PLACED_SUBRESOURCE_FOOTPRINT* fp,
                    const unsigned char want[4], const char* name)
{
    char b[512];
    void* mapped = NULL; D3D12_RANGE rr = {0, (SIZE_T)fp->Footprint.RowPitch * TH};
    HRESULT hr = ID3D12Resource_Map(readback, 0, &rr, &mapped);
    if (FAILED(hr)) { wsprintfA(b, "root12: CASE %-4s FAIL (RT Map hr=0x%08X)\n", name, (unsigned)hr); dbg(b); return 0; }
    int bad = 0, fi = -1; unsigned char fg[4] = {0, 0, 0, 0};
    for (int y = 0; y < TH; y++) for (int x = 0; x < TW; x++) {
        const unsigned char* px = (const unsigned char*)mapped + fp->Footprint.RowPitch * y + x * 4;
        for (int ch = 0; ch < 4; ch++)
            if (abs((int)px[ch] - (int)want[ch]) > 2) { bad++; if (fi < 0) { fi = x + y * TW; memcpy(fg, px, 4); } }
    }
    ID3D12Resource_Unmap(readback, 0, NULL);
    if (bad) {
        wsprintfA(b, "root12: CASE %-4s FAIL (RT mismatch bad=%d first px idx=%d got=[%d %d %d %d] want=[%d %d %d %d])\n",
                  name, bad, fi, fg[0], fg[1], fg[2], fg[3], want[0], want[1], want[2], want[3]);
        dbg(b); return 0;
    }
    wsprintfA(b, "root12: CASE %-4s OK (RT all %d px match [%d %d %d %d])\n",
              name, TW * TH, want[0], want[1], want[2], want[3]);
    dbg(b); return 1;
}

static ID3D12RootSignature* make_rootsig(struct draw_ctx* cx, D3D12_ROOT_PARAMETER_TYPE type,
                                         UINT reg, const char* name)
{
    char b[512];
    D3D12_ROOT_PARAMETER param = {0};
    param.ParameterType = type;
    param.Descriptor.ShaderRegister = reg;
    param.Descriptor.RegisterSpace = 0;
    param.ShaderVisibility = D3D12_SHADER_VISIBILITY_ALL;
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    rsd.NumParameters = 1; rsd.pParameters = &param;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* sigBlob = NULL; ID3DBlob* sigErr = NULL;
    HRESULT hr = cx->pSerialize(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &sigBlob, &sigErr);
    if (FAILED(hr)) { wsprintfA(b, "root12: CASE %-4s FAIL (serialize root sig hr=0x%08X)\n", name, (unsigned)hr); dbg(b); return NULL; }
    ID3D12RootSignature* rs = NULL;
    hr = ID3D12Device_CreateRootSignature(cx->dev, 0, ID3D10Blob_GetBufferPointer(sigBlob),
                                          ID3D10Blob_GetBufferSize(sigBlob), &IID_ID3D12RootSignature, (void**)&rs);
    ID3D10Blob_Release(sigBlob); if (sigErr) ID3D10Blob_Release(sigErr);
    if (FAILED(hr)) { wsprintfA(b, "root12: CASE %-4s FAIL (CreateRootSignature hr=0x%08X)\n", name, (unsigned)hr); dbg(b); return NULL; }
    return rs;
}

static void bind_cbv(ID3D12GraphicsCommandList* l, void* c)
{ ID3D12GraphicsCommandList_SetGraphicsRootConstantBufferView(l, 0, ID3D12Resource_GetGPUVirtualAddress((ID3D12Resource*)c)); }
static void bind_srv(ID3D12GraphicsCommandList* l, void* c)
{ ID3D12GraphicsCommandList_SetGraphicsRootShaderResourceView(l, 0, ID3D12Resource_GetGPUVirtualAddress((ID3D12Resource*)c)); }
static void bind_uav(ID3D12GraphicsCommandList* l, void* c)
{ ID3D12GraphicsCommandList_SetGraphicsRootUnorderedAccessView(l, 0, ID3D12Resource_GetGPUVirtualAddress((ID3D12Resource*)c)); }

static int case_cbv(struct draw_ctx* cx)
{
    static const char* PS =
        "cbuffer RootCb : register(b0) { float4 gMul; float4 gAdd; };\n"
        "struct PSIn { float4 pos : SV_Position; float4 col : COLOR; };\n"
        "float4 main(PSIn i) : SV_Target { return i.col * gMul + gAdd; }\n";
    ID3DBlob* ps = compile_shader(PS, "ps_5_0", "cbv-ps");
    if (!ps) return 0;
    ID3D12RootSignature* rs = make_rootsig(cx, D3D12_ROOT_PARAMETER_TYPE_CBV, 0, "cbv");
    if (!rs) { ID3D10Blob_Release(ps); return 0; }
    /* 256-byte CBV placement: mul=(0.5,0.25,0.125,1), add=(0.25,0.5,0.75,0);
     * input color (1,1,1,1) -> (0.75, 0.75, 0.875, 1) -> bytes (191,191,223,255). */
    static float cbdata[64];
    cbdata[0] = 0.5f; cbdata[1] = 0.25f; cbdata[2] = 0.125f; cbdata[3] = 1.0f;
    cbdata[4] = 0.25f; cbdata[5] = 0.5f; cbdata[6] = 0.75f; cbdata[7] = 0.0f;
    ID3D12Resource* buf = make_upload_filled(cx->dev, cbdata, sizeof(cbdata), "cbv buffer");
    if (!buf) { ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps); return 0; }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {0};
    ID3D12Resource* rb = draw_and_readback(cx, rs, ps, bind_cbv, buf, "cbv", &fp);
    int ok = 0;
    if (rb) { static const unsigned char want[4] = {191, 191, 223, 255}; ok = check_rt(rb, &fp, want, "cbv"); ID3D12Resource_Release(rb); }
    ID3D12Resource_Release(buf);
    ID3D12RootSignature_Release(rs);
    ID3D10Blob_Release(ps);
    return ok;
}

static int case_srv(struct draw_ctx* cx)
{
    static const char* PS =
        "Buffer<uint> gSrv : register(t0);\n"
        "struct PSIn { float4 pos : SV_Position; float4 col : COLOR; };\n"
        "float4 main(PSIn i) : SV_Target { return float4(asfloat(gSrv[0]), asfloat(gSrv[1]), asfloat(gSrv[2]), 1.0); }\n";
    ID3DBlob* ps = compile_shader(PS, "ps_5_0", "srv-ps");
    if (!ps) return 0;
    ID3D12RootSignature* rs = make_rootsig(cx, D3D12_ROOT_PARAMETER_TYPE_SRV, 0, "srv");
    if (!rs) { ID3D10Blob_Release(ps); return 0; }
    /* words = float bits of (1.0, 0.5, 0.25) + pad -> bytes (255,128,64,255). */
    static const unsigned int words[4] = { 0x3F800000u, 0x3F000000u, 0x3E800000u, 0xDEADBEEFu };
    ID3D12Resource* buf = make_upload_filled(cx->dev, words, sizeof(words), "srv buffer");
    if (!buf) { ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps); return 0; }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {0};
    ID3D12Resource* rb = draw_and_readback(cx, rs, ps, bind_srv, buf, "srv", &fp);
    int ok = 0;
    if (rb) { static const unsigned char want[4] = {255, 128, 64, 255}; ok = check_rt(rb, &fp, want, "srv"); ID3D12Resource_Release(rb); }
    ID3D12Resource_Release(buf);
    ID3D12RootSignature_Release(rs);
    ID3D10Blob_Release(ps);
    return ok;
}

static int case_uav(struct draw_ctx* cx)
{
    static const char* PS =
        "RWBuffer<uint> gUav : register(u0);\n"
        "struct PSIn { float4 pos : SV_Position; float4 col : COLOR; };\n"
        "float4 main(PSIn i) : SV_Target { uint idx = (uint)i.pos.x + 4u * (uint)i.pos.y; gUav[idx] = 0xA5000000u + idx; return float4(0.25, 0.5, 0.75, 1.0); }\n";
    ID3DBlob* ps = compile_shader(PS, "ps_5_0", "uav-ps");
    if (!ps) return 0;
    ID3D12RootSignature* rs = make_rootsig(cx, D3D12_ROOT_PARAMETER_TYPE_UAV, 0, "uav");
    if (!rs) { ID3D10Blob_Release(ps); return 0; }
    ID3D12Resource* buf = make_buffer(cx->dev, D3D12_HEAP_TYPE_DEFAULT, 64,
                                      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
                                      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, "uav buffer");
    if (!buf) { ID3D12RootSignature_Release(rs); ID3D10Blob_Release(ps); return 0; }
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp = {0};
    ID3D12Resource* rb = draw_and_readback(cx, rs, ps, bind_uav, buf, "uav", &fp);
    int ok = 0;
    if (rb) {
        static const unsigned char wantRt[4] = {64, 128, 191, 255};
        int rtOk = check_rt(rb, &fp, wantRt, "uav");
        ID3D12Resource_Release(rb);
        /* Second list: barrier UAV -> COPY_SOURCE, copy the buffer to a
         * readback buffer, verify word[idx] == 0xA5000000 + idx. */
        ID3D12Resource* brb = make_buffer(cx->dev, D3D12_HEAP_TYPE_READBACK, 64,
                                          D3D12_RESOURCE_STATE_COPY_DEST, 0, "uav readback");
        ID3D12GraphicsCommandList* list = NULL;
        HRESULT hr = brb ? ID3D12Device_CreateCommandList(cx->dev, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                                          cx->alloc, NULL, &IID_ID3D12GraphicsCommandList, (void**)&list) : E_FAIL;
        if (SUCCEEDED(hr)) {
            D3D12_RESOURCE_BARRIER bar = {0};
            bar.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
            bar.Transition.pResource = buf;
            bar.Transition.StateBefore = D3D12_RESOURCE_STATE_UNORDERED_ACCESS;
            bar.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
            bar.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
            ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &bar);
            ID3D12GraphicsCommandList_CopyBufferRegion(list, brb, 0, buf, 0, 64);
            hr = ID3D12GraphicsCommandList_Close(list);
            if (SUCCEEDED(hr)) {
                ID3D12CommandList* lists[1] = { (ID3D12CommandList*)list };
                ID3D12CommandQueue_ExecuteCommandLists(cx->queue, 1, lists);
                if (wait_fence(cx->queue, cx->fence, cx->fenceValue)) {
                    void* mapped = NULL; D3D12_RANGE rr = {0, 64};
                    hr = ID3D12Resource_Map(brb, 0, &rr, &mapped);
                    if (SUCCEEDED(hr)) {
                        const unsigned int* w = (const unsigned int*)mapped;
                        int bad = 0, fi = -1; unsigned int fg = 0;
                        for (int i = 0; i < 16; i++)
                            if (w[i] != 0xA5000000u + (unsigned)i) { bad++; if (fi < 0) { fi = i; fg = w[i]; } }
                        ID3D12Resource_Unmap(brb, 0, NULL);
                        char b[512];
                        if (bad) { wsprintfA(b, "root12: CASE uav  FAIL (buffer mismatch bad=%d first word=%d got=0x%08X want=0x%08X)\n", bad, fi, fg, 0xA5000000u + (fi < 0 ? 0 : fi)); dbg(b); }
                        else { wsprintfA(b, "root12: CASE uav  OK (all 16 stored words match)\n"); dbg(b); ok = rtOk; }
                    }
                }
            }
            ID3D12GraphicsCommandList_Release(list);
        }
        if (brb) ID3D12Resource_Release(brb);
    }
    ID3D12Resource_Release(buf);
    ID3D12RootSignature_Release(rs);
    ID3D10Blob_Release(ps);
    return ok;
}

int main(void)
{
    char b[512];
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (!d3d12) FAIL("root12: FAIL LoadLibrary(d3d12.dll) err=%lu\n", (unsigned long)GetLastError());
    HMODULE compiler = LoadLibraryA("d3dcompiler_47.dll");
    if (!compiler) FAIL("root12: FAIL LoadLibrary(d3dcompiler_47.dll) err=%lu\n", (unsigned long)GetLastError());
    pD3DCompile = (PFN_D3D_COMPILE)GetProcAddress(compiler, "D3DCompile");
    if (!pD3DCompile) FAIL("root12: FAIL GetProcAddress(D3DCompile)\n");
    PFN_D3D12_CREATE_DEVICE pCreate =
        (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
    PFN_D3D12_SERIALIZE_ROOT_SIG pSerialize =
        (PFN_D3D12_SERIALIZE_ROOT_SIG)GetProcAddress(d3d12, "D3D12SerializeRootSignature");
    if (!pCreate || !pSerialize) FAIL("root12: FAIL GetProcAddress(d3d12 exports)\n");

    ID3D12Device* dev = NULL;
    HRESULT hr = pCreate(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&dev);
    if (FAILED(hr)) FAIL("root12: FAIL D3D12CreateDevice hr=0x%08X\n", (unsigned)hr);
    dbg("root12: D3D12CreateDevice OK\n");

    D3D12_COMMAND_QUEUE_DESC qd = {0};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = NULL;
    hr = ID3D12Device_CreateCommandQueue(dev, &qd, &IID_ID3D12CommandQueue, (void**)&queue);
    if (FAILED(hr)) FAIL("root12: FAIL CreateCommandQueue hr=0x%08X\n", (unsigned)hr);
    ID3D12CommandAllocator* alloc = NULL;
    hr = ID3D12Device_CreateCommandAllocator(dev, D3D12_COMMAND_LIST_TYPE_DIRECT,
                                             &IID_ID3D12CommandAllocator, (void**)&alloc);
    if (FAILED(hr)) FAIL("root12: FAIL CreateCommandAllocator hr=0x%08X\n", (unsigned)hr);
    ID3D12Fence* fence = NULL;
    hr = ID3D12Device_CreateFence(dev, 0, D3D12_FENCE_FLAG_NONE, &IID_ID3D12Fence, (void**)&fence);
    if (FAILED(hr)) FAIL("root12: FAIL CreateFence hr=0x%08X\n", (unsigned)hr);
    UINT64 fenceValue = 0;

    static const char* VS_SRC =
        "struct VSIn { float3 pos : POSITION; float4 col : COLOR; };\n"
        "struct VSOut { float4 pos : SV_Position; float4 col : COLOR; };\n"
        "VSOut main(VSIn i) { VSOut o; o.pos = float4(i.pos, 1.0); o.col = i.col; return o; }\n";
    ID3DBlob* vsBlob = compile_shader(VS_SRC, "vs_5_0", "fullscreen-vs");
    if (!vsBlob) FAIL("root12: FAIL fullscreen VS compile\n");

    struct draw_ctx cx = { dev, queue, alloc, fence, &fenceValue, vsBlob, pSerialize };
    int cbvOk = case_cbv(&cx); ID3D12CommandAllocator_Reset(alloc);
    int srvOk = case_srv(&cx); ID3D12CommandAllocator_Reset(alloc);
    int uavOk = case_uav(&cx); ID3D12CommandAllocator_Reset(alloc);

    wsprintfA(b, "root12: SUMMARY cbv=%s srv=%s uav=%s\n",
              cbvOk ? "OK" : "FAIL", srvOk ? "OK" : "FAIL", uavOk ? "OK" : "FAIL");
    dbg(b);
    ID3D10Blob_Release(vsBlob);
    ID3D12Fence_Release(fence);
    ID3D12CommandAllocator_Release(alloc);
    ID3D12CommandQueue_Release(queue);
    ID3D12Device_Release(dev);
    if (cbvOk && srvOk && uavOk) { dbg("root12: RESULT 0\n"); return 0; }
    dbg("root12: RESULT 1\n");
    return 1;
}
