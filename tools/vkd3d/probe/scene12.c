/*
 * scene12.c — Complex D3D12 scene for pipeline profiling.
 *
 * Significantly more complex than tri12draw.c (1 PSO, 1 draw, no depth):
 *  - 3 PSOs (different cull/fill/depth/blend states)
 *  - 60 draws per frame (20 indexed quads + 20 triangles + 20 wireframe)
 *  - Depth buffer (D32_FLOAT) with depth testing
 *  - 64x64 checkerboard texture bound via SRV descriptor heap
 *  - 20 frames
 *
 * Machine-readable verdict on stderr: "scene12: RESULT 0" or "scene12: RESULT 1".
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o scene12.exe scene12.c -lole32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <stdio.h>
#include <math.h>

#include "triangle_vs.h"
#include "triangle_ps.h"

#define SCENE12_FRAMES 20
#define SCENE12_W 480
#define SCENE12_H 360
#define QUADS 20
#define TRIS 20
#define WIRES 20

static void dbg(const char* s) { fprintf(stderr, "%s", s); fflush(stderr); }
#define FAIL(...) do { char b[512]; wsprintfA(b, __VA_ARGS__); dbg(b); \
    dbg("scene12: RESULT 1\n"); return 1; } while (0)
#define HRF(hr, what) do { if (FAILED(hr)) { char b[512]; \
    wsprintfA(b, "scene12: FAIL %s hr=0x%08X\n", what, (unsigned)(hr)); dbg(b); \
    dbg("scene12: RESULT 1\n"); return 1; } } while (0)

typedef HRESULT (WINAPI *PFN_D3D12_CREATE_DEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT (WINAPI *PFN_CREATE_DXGI_FACTORY2)(UINT, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12_SERIALIZE_ROOT_SIG)(const D3D12_ROOT_SIGNATURE_DESC*,
        D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    return DefWindowProcA(h, m, w, l);
}

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

struct vertex { float pos[3]; float col[4]; };

/* Grid positions for quads: 5x4 grid, varying depths */
static void quad_verts(struct vertex* v, int qi)
{
    int gx = qi % 5, gy = qi / 5;
    float x0 = -0.9f + gx * 0.38f, x1 = x0 + 0.32f;
    float y0 = 0.85f - gy * 0.38f, y1 = y0 - 0.32f;
    float z = 0.1f + (qi % 7) * 0.12f; /* varying depth for depth testing */
    float r = (qi % 3) == 0 ? 1.0f : 0.2f;
    float g = (qi % 3) == 1 ? 1.0f : 0.2f;
    float b = (qi % 3) == 2 ? 1.0f : 0.2f;
    v[0].pos[0]=x0; v[0].pos[1]=y0; v[0].pos[2]=z; v[0].col[0]=r; v[0].col[1]=g; v[0].col[2]=b; v[0].col[3]=1;
    v[1].pos[0]=x1; v[1].pos[1]=y0; v[1].pos[2]=z; v[1].col[0]=r; v[1].col[1]=0.5f; v[1].col[2]=b; v[1].col[3]=1;
    v[2].pos[0]=x1; v[2].pos[1]=y1; v[2].pos[2]=z; v[2].col[0]=0.5f; v[2].col[1]=g; v[2].col[2]=b; v[2].col[3]=1;
    v[3].pos[0]=x0; v[3].pos[1]=y1; v[3].pos[2]=z; v[3].col[0]=r; v[3].col[1]=g; v[3].col[2]=0.5f; v[3].col[3]=1;
}

static void tri_verts(struct vertex* v, int ti)
{
    float cx = -0.8f + (ti % 5) * 0.4f;
    float cy = -0.85f + (ti / 5) * 0.42f;
    float z = 0.05f + (ti % 5) * 0.15f;
    float s = 0.14f;
    v[0].pos[0]=cx;    v[0].pos[1]=cy+s; v[0].pos[2]=z; v[0].col[0]=1; v[0].col[1]=1; v[0].col[2]=0; v[0].col[3]=1;
    v[1].pos[0]=cx+s;  v[1].pos[1]=cy-s; v[1].pos[2]=z; v[1].col[0]=1; v[1].col[1]=0; v[1].col[2]=1; v[1].col[3]=1;
    v[2].pos[0]=cx-s;  v[2].pos[1]=cy-s; v[2].pos[2]=z; v[2].col[0]=0; v[2].col[1]=1; v[2].col[2]=1; v[2].col[3]=1;
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hInst; (void)hPrev; (void)cmd; (void)show;
    dbg("scene12: WinMain start (complex D3D12 scene)\n");

    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    if (!d3d12) FAIL("scene12: FAIL LoadLibrary(d3d12.dll) err=%u\n", GetLastError());
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    if (!dxgi) FAIL("scene12: FAIL LoadLibrary(dxgi.dll) err=%u\n", GetLastError());

    PFN_D3D12_CREATE_DEVICE pD3D12CreateDevice =
        (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
    if (!pD3D12CreateDevice) FAIL("scene12: FAIL GetProcAddress(D3D12CreateDevice)\n");
    PFN_CREATE_DXGI_FACTORY2 pCreateDXGIFactory2 =
        (PFN_CREATE_DXGI_FACTORY2)GetProcAddress(dxgi, "CreateDXGIFactory2");
    if (!pCreateDXGIFactory2) FAIL("scene12: FAIL GetProcAddress(CreateDXGIFactory2)\n");
    PFN_D3D12_SERIALIZE_ROOT_SIG pSerializeRootSig =
        (PFN_D3D12_SERIALIZE_ROOT_SIG)GetProcAddress(d3d12, "D3D12SerializeRootSignature");
    if (!pSerializeRootSig) FAIL("scene12: FAIL GetProcAddress(D3D12SerializeRootSignature)\n");

    ID3D12Device* device = NULL;
    HRESULT hr = pD3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0,
                                   &IID_ID3D12Device, (void**)&device);
    if (FAILED(hr)) FAIL("scene12: FAIL D3D12CreateDevice hr=0x%08X\n", (unsigned)hr);
    dbg("scene12: D3D12CreateDevice OK\n");
    dbg("scene12: window+queue next\n");

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc;
    wc.hInstance = hInst;
    wc.lpszClassName = "SCENE12";
    if (!RegisterClassA(&wc)) FAIL("scene12: FAIL RegisterClass err=%u\n", GetLastError());
    HWND hwnd = CreateWindowExA(0, "SCENE12", "scene12",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, SCENE12_W, SCENE12_H,
                                NULL, NULL, hInst, NULL);
    if (!hwnd) FAIL("scene12: FAIL CreateWindowEx err=%u\n", GetLastError());

    D3D12_COMMAND_QUEUE_DESC qd = {0};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = NULL;
    HRF(ID3D12Device_CreateCommandQueue(device, &qd, &IID_ID3D12CommandQueue, (void**)&queue),
        "CreateCommandQueue");

    IDXGIFactory2* factory = NULL;
    HRF(pCreateDXGIFactory2(0, &IID_IDXGIFactory2, (void**)&factory), "CreateDXGIFactory2");

    DXGI_SWAP_CHAIN_DESC1 scd = {0};
    scd.BufferCount = 2;
    scd.Width = SCENE12_W; scd.Height = SCENE12_H;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;
    IDXGISwapChain1* sc1 = NULL;
    HRF(IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown*)queue, hwnd,
        &scd, NULL, NULL, &sc1), "CreateSwapChainForHwnd");
    IDXGIFactory2_Release(factory);
    IDXGISwapChain3* sc3 = NULL;
    HRF(IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void**)&sc3),
        "QI(IDXGISwapChain3)");

    /* RTV heap */
    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    hd.NumDescriptors = 2;
    ID3D12DescriptorHeap* rtvHeap = NULL;
    HRF(ID3D12Device_CreateDescriptorHeap(device, &hd, &IID_ID3D12DescriptorHeap,
        (void**)&rtvHeap), "CreateDescriptorHeap(RTV)");
    UINT rtvSize = ID3D12Device_GetDescriptorHandleIncrementSize(device,
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    ID3D12Resource* targets[2] = {NULL, NULL};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    heap_start(rtvHeap, &rtv);
    for (int i = 0; i < 2; i++) {
        HRF(IDXGISwapChain1_GetBuffer(sc1, i, &IID_ID3D12Resource, (void**)&targets[i]),
            "GetBuffer");
        ID3D12Device_CreateRenderTargetView(device, targets[i], NULL, rtv);
        rtv.ptr += rtvSize;
    }

    /* DSV heap + depth buffer */
    D3D12_DESCRIPTOR_HEAP_DESC dhd = {0};
    dhd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
    dhd.NumDescriptors = 1;
    ID3D12DescriptorHeap* dsvHeap = NULL;
    HRF(ID3D12Device_CreateDescriptorHeap(device, &dhd, &IID_ID3D12DescriptorHeap,
        (void**)&dsvHeap), "CreateDescriptorHeap(DSV)");
    D3D12_HEAP_PROPERTIES hpd = {0};
    hpd.Type = D3D12_HEAP_TYPE_DEFAULT;
    D3D12_RESOURCE_DESC rdd = {0};
    rdd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rdd.Width = SCENE12_W; rdd.Height = SCENE12_H;
    rdd.DepthOrArraySize = 1; rdd.MipLevels = 1;
    rdd.Format = DXGI_FORMAT_D32_FLOAT;
    rdd.SampleDesc.Count = 1;
    rdd.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
    D3D12_CLEAR_VALUE dcv = {0};
    dcv.Format = DXGI_FORMAT_D32_FLOAT;
    dcv.DepthStencil.Depth = 1.0f;
    ID3D12Resource* depthBuf = NULL;
    HRF(ID3D12Device_CreateCommittedResource(device, &hpd, D3D12_HEAP_FLAG_NONE, &rdd,
        D3D12_RESOURCE_STATE_DEPTH_WRITE, &dcv,
        &IID_ID3D12Resource, (void**)&depthBuf), "CreateCommittedResource(DS)");
    D3D12_CPU_DESCRIPTOR_HANDLE dsv;
    heap_start(dsvHeap, &dsv);
    ID3D12Device_CreateDepthStencilView(device, depthBuf, NULL, dsv);
    dbg("scene12: depth buffer created\n");
    dbg("scene12: srv heap next\n");

    /* SRV heap + 64x64 checkerboard texture */
    D3D12_DESCRIPTOR_HEAP_DESC shd = {0};
    shd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
    shd.NumDescriptors = 1;
    shd.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ID3D12DescriptorHeap* srvHeap = NULL;
    HRF(ID3D12Device_CreateDescriptorHeap(device, &shd, &IID_ID3D12DescriptorHeap,
        (void**)&srvHeap), "CreateDescriptorHeap(SRV)");
    D3D12_RESOURCE_DESC rtd = {0};
    rtd.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
    rtd.Width = 64; rtd.Height = 64;
    rtd.DepthOrArraySize = 1; rtd.MipLevels = 1;
    rtd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    rtd.SampleDesc.Count = 1;
    ID3D12Resource* tex = NULL;
    HRF(ID3D12Device_CreateCommittedResource(device, &hpd, D3D12_HEAP_FLAG_NONE, &rtd,
        D3D12_RESOURCE_STATE_COPY_DEST, NULL,
        &IID_ID3D12Resource, (void**)&tex), "CreateCommittedResource(Tex)");
    /* Upload checkerboard via upload heap */
    unsigned char pixels[64*64*4];
    for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) {
        int c = ((x/8) + (y/8)) % 2 ? 255 : 32;
        pixels[(y*64+x)*4+0] = c; pixels[(y*64+x)*4+1] = (unsigned char)(255-c);
        pixels[(y*64+x)*4+2] = 128; pixels[(y*64+x)*4+3] = 255;
    }
    D3D12_HEAP_PROPERTIES hpu = {0};
    hpu.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rdu = {0};
    rdu.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
    rdu.Width = sizeof(pixels);
    rdu.Height = 1; rdu.DepthOrArraySize = 1; rdu.MipLevels = 1;
    rdu.SampleDesc.Count = 1;
    rdu.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* texUpload = NULL;
    HRF(ID3D12Device_CreateCommittedResource(device, &hpu, D3D12_HEAP_FLAG_NONE, &rdu,
        D3D12_RESOURCE_STATE_GENERIC_READ, NULL,
        &IID_ID3D12Resource, (void**)&texUpload), "CreateCommittedResource(TexUp)");
    void* tmapped = NULL;
    D3D12_RANGE trr = {0, 0};
    HRF(ID3D12Resource_Map(texUpload, 0, &trr, &tmapped), "Map(TexUp)");
    memcpy(tmapped, pixels, sizeof(pixels));
    ID3D12Resource_Unmap(texUpload, 0, NULL);
    D3D12_CPU_DESCRIPTOR_HANDLE srv;
    heap_start(srvHeap, &srv);
    D3D12_SHADER_RESOURCE_VIEW_DESC srvd = {0};
    srvd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    srvd.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srvd.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
    srvd.Texture2D.MipLevels = 1;
    ID3D12Device_CreateShaderResourceView(device, tex, &srvd, srv);
    dbg("scene12: texture created\n");
    dbg("scene12: root sig next\n");

    /* Root signature: one SRV descriptor table */
    D3D12_DESCRIPTOR_RANGE rng = {0};
    rng.RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
    rng.NumDescriptors = 1;
    rng.BaseShaderRegister = 0;
    D3D12_ROOT_PARAMETER rp = {0};
    rp.ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
    rp.DescriptorTable.NumDescriptorRanges = 1;
    rp.DescriptorTable.pDescriptorRanges = &rng;
    rp.ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    rsd.NumParameters = 1;
    rsd.pParameters = &rp;
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* rsBlob = NULL;
    ID3DBlob* rsErr = NULL;
    hr = pSerializeRootSig(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    if (FAILED(hr)) {
        if (rsErr) dbg((char*)ID3D10Blob_GetBufferPointer(rsErr));
        FAIL("scene12: FAIL D3D12SerializeRootSignature hr=0x%08X\n", (unsigned)hr);
    }
    ID3D12RootSignature* rootSig = NULL;
    HRF(ID3D12Device_CreateRootSignature(device, 0, ID3D10Blob_GetBufferPointer(rsBlob),
        ID3D10Blob_GetBufferSize(rsBlob), &IID_ID3D12RootSignature, (void**)&rootSig),
        "CreateRootSignature");
    ID3D10Blob_Release(rsBlob);

    D3D12_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };

    /* Helper to build a PSO with given state */
    ID3D12PipelineState* psos[3];
    {
        D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {0};
        pso.InputLayout.pInputElementDescs = il;
        pso.InputLayout.NumElements = 2;
        pso.pRootSignature = rootSig;
        pso.VS.pShaderBytecode = g_vs_main;
        pso.VS.BytecodeLength = sizeof(g_vs_main);
        pso.PS.pShaderBytecode = g_ps_main;
        pso.PS.BytecodeLength = sizeof(g_ps_main);
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        pso.RasterizerState.FrontCounterClockwise = FALSE;
        pso.RasterizerState.DepthClipEnable = TRUE;
        pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
        pso.SampleMask = UINT_MAX;
        pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
        pso.NumRenderTargets = 1;
        pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
        pso.DSVFormat = DXGI_FORMAT_D32_FLOAT;
        pso.SampleDesc.Count = 1;
        /* PSO 0: solid, back-cull, depth LESS */
        pso.DepthStencilState.DepthEnable = TRUE;
        pso.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS;
        HRF(ID3D12Device_CreateGraphicsPipelineState(device, &pso,
            &IID_ID3D12PipelineState, (void**)&psos[0]), "CreateGraphicsPipelineState(0)");
        /* PSO 1: solid, no cull, depth LESS_EQUAL */
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
        pso.DepthStencilState.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;
        HRF(ID3D12Device_CreateGraphicsPipelineState(device, &pso,
            &IID_ID3D12PipelineState, (void**)&psos[1]), "CreateGraphicsPipelineState(1)");
        /* PSO 2: wireframe, back-cull, no depth */
        pso.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
        pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
        pso.DepthStencilState.DepthEnable = FALSE;
        HRF(ID3D12Device_CreateGraphicsPipelineState(device, &pso,
            &IID_ID3D12PipelineState, (void**)&psos[2]), "CreateGraphicsPipelineState(2)");
    }
    dbg("scene12: 3 PSOs created\n");
    dbg("scene12: alloc/list next\n");

    ID3D12CommandAllocator* alloc = NULL;
    HRF(ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
        &IID_ID3D12CommandAllocator, (void**)&alloc), "CreateCommandAllocator");
    ID3D12GraphicsCommandList* list = NULL;
    HRF(ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
        alloc, psos[0], &IID_ID3D12GraphicsCommandList, (void**)&list),
        "CreateCommandList");

    /* Fence for upload/frame synchronization (created early so the upload
     * command list can be closed, executed and waited on before the frame
     * loop: Reset(alloc) fails while a list from this allocator is open). */
    ID3D12Fence* fence = NULL;
    HRF(ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
        &IID_ID3D12Fence, (void**)&fence), "CreateFence");
    HANDLE evt = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!evt) FAIL("scene12: FAIL CreateEvent\n");
    UINT64 fenceVal = 1;

    /* Texture upload: manual footprint (GetCopyableFootprints equivalent).
     * 64x64 R8G8B8A8_UNORM: row = 64*4 = 256 bytes = D3D12_TEXTURE_DATA_PITCH_ALIGNMENT,
     * so RowPitch=256 with no padding. This is exactly what GetCopyableFootprints
     * would return for this texture. */
    {
        D3D12_TEXTURE_COPY_LOCATION srcLoc = {0};
        srcLoc.pResource = texUpload;
        srcLoc.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
        srcLoc.PlacedFootprint.Offset = 0;
        srcLoc.PlacedFootprint.Footprint.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
        srcLoc.PlacedFootprint.Footprint.Width = 64;
        srcLoc.PlacedFootprint.Footprint.Height = 64;
        srcLoc.PlacedFootprint.Footprint.Depth = 1;
        srcLoc.PlacedFootprint.Footprint.RowPitch = 256;

        D3D12_TEXTURE_COPY_LOCATION dstLoc = {0};
        dstLoc.pResource = tex;
        dstLoc.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        dstLoc.SubresourceIndex = 0;

        ID3D12GraphicsCommandList_CopyTextureRegion(list, &dstLoc, 0, 0, 0, &srcLoc, NULL);

        D3D12_RESOURCE_BARRIER tb = {0};
        tb.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        tb.Transition.pResource = tex;
        tb.Transition.Subresource = 0;
        tb.Transition.StateBefore = D3D12_RESOURCE_STATE_COPY_DEST;
        tb.Transition.StateAfter = D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &tb);
        dbg("scene12: texture uploaded (manual footprint)\n");
    }

    /* The upload list is still open here: close, execute and wait for it
     * before the frame loop (Reset(alloc) fails with an open list). */
    HRF(ID3D12GraphicsCommandList_Close(list), "Close(upload)");
    {
        ID3D12CommandList* lists[1];
        lists[0] = (ID3D12CommandList*)list;
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
    }
    HRF(ID3D12CommandQueue_Signal(queue, fence, fenceVal), "Signal(upload)");
    if (ID3D12Fence_GetCompletedValue(fence) < fenceVal) {
        HRF(ID3D12Fence_SetEventOnCompletion(fence, fenceVal, evt),
            "SetEventOnCompletion(upload)");
        WaitForSingleObject(evt, 10000);
    }
    fenceVal++;
    dbg("scene12: upload list executed and waited\n");


    /* Vertex buffers: one big VB with all quads + one with all tris */
    struct vertex quadData[QUADS*4];
    for (int i = 0; i < QUADS; i++) quad_verts(&quadData[i*4], i);
    unsigned short quadIdx[QUADS*6];
    for (int i = 0; i < QUADS; i++) {
        quadIdx[i*6+0]=i*4+0; quadIdx[i*6+1]=i*4+1; quadIdx[i*6+2]=i*4+2;
        quadIdx[i*6+3]=i*4+0; quadIdx[i*6+4]=i*4+2; quadIdx[i*6+5]=i*4+3;
    }
    struct vertex triData[TRIS*3];
    for (int i = 0; i < TRIS; i++) tri_verts(&triData[i*3], i);

    ID3D12Resource *vbQuads=NULL, *ibQuads=NULL, *vbTris=NULL;
    D3D12_VERTEX_BUFFER_VIEW vbvQ, vbvT;
    D3D12_INDEX_BUFFER_VIEW ibvQ;
    {
        void* mapped; D3D12_RANGE rr = {0,0};
        rdu.Width = sizeof(quadData);
        HRF(ID3D12Device_CreateCommittedResource(device, &hpu, D3D12_HEAP_FLAG_NONE, &rdu,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&vbQuads),
            "CreateCommittedResource(vbQ)");
        HRF(ID3D12Resource_Map(vbQuads, 0, &rr, &mapped), "Map(vbQ)");
        memcpy(mapped, quadData, sizeof(quadData));
        ID3D12Resource_Unmap(vbQuads, 0, NULL);
        vbvQ.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbQuads);
        vbvQ.SizeInBytes = sizeof(quadData);
        vbvQ.StrideInBytes = sizeof(struct vertex);

        rdu.Width = sizeof(quadIdx);
        HRF(ID3D12Device_CreateCommittedResource(device, &hpu, D3D12_HEAP_FLAG_NONE, &rdu,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&ibQuads),
            "CreateCommittedResource(ibQ)");
        HRF(ID3D12Resource_Map(ibQuads, 0, &rr, &mapped), "Map(ibQ)");
        memcpy(mapped, quadIdx, sizeof(quadIdx));
        ID3D12Resource_Unmap(ibQuads, 0, NULL);
        ibvQ.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(ibQuads);
        ibvQ.SizeInBytes = sizeof(quadIdx);
        ibvQ.Format = DXGI_FORMAT_R16_UINT;

        rdu.Width = sizeof(triData);
        HRF(ID3D12Device_CreateCommittedResource(device, &hpu, D3D12_HEAP_FLAG_NONE, &rdu,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&vbTris),
            "CreateCommittedResource(vbT)");
        HRF(ID3D12Resource_Map(vbTris, 0, &rr, &mapped), "Map(vbT)");
        memcpy(mapped, triData, sizeof(triData));
        ID3D12Resource_Unmap(vbTris, 0, NULL);
        vbvT.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vbTris);
        vbvT.SizeInBytes = sizeof(triData);
        vbvT.StrideInBytes = sizeof(struct vertex);
    }
    dbg("scene12: geometry uploaded\n");
    dbg("scene12: entering frame loop\n");

    D3D12_VIEWPORT vp = {0, 0, (float)SCENE12_W, (float)SCENE12_H, 0.0f, 1.0f};
    D3D12_RECT sc = {0, 0, SCENE12_W, SCENE12_H};

    float clearColor[4] = {0.05f, 0.05f, 0.12f, 1.0f};
    ID3D12DescriptorHeap* heaps[1] = { srvHeap };
    D3D12_GPU_DESCRIPTOR_HANDLE srvGpu;
    heap_start_gpu(srvHeap, &srvGpu);

    for (int f = 0; f < SCENE12_FRAMES; f++) {
        UINT backIdx = IDXGISwapChain3_GetCurrentBackBufferIndex(sc3);
        HRF(ID3D12CommandAllocator_Reset(alloc), "Reset(alloc)");
        HRF(ID3D12GraphicsCommandList_Reset(list, alloc, psos[0]), "Reset(list)");
        ID3D12GraphicsCommandList_SetDescriptorHeaps(list, 1, heaps);
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, rootSig);
        ID3D12GraphicsCommandList_SetGraphicsRootDescriptorTable(list, 0, srvGpu);
        ID3D12GraphicsCommandList_RSSetViewports(list, 1, &vp);
        ID3D12GraphicsCommandList_RSSetScissorRects(list, 1, &sc);

        D3D12_RESOURCE_BARRIER b0 = {0};
        b0.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b0.Transition.pResource = targets[backIdx];
        b0.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b0.Transition.StateBefore = D3D12_RESOURCE_STATE_PRESENT;
        b0.Transition.StateAfter = D3D12_RESOURCE_STATE_RENDER_TARGET;
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b0);

        heap_start(rtvHeap, &rtv);
        rtv.ptr += backIdx * rtvSize;
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, &dsv);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, clearColor, 0, NULL);
        ID3D12GraphicsCommandList_ClearDepthStencilView(list, dsv,
            D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, NULL);

        /* 20 indexed quads with PSO 0 */
        ID3D12GraphicsCommandList_SetPipelineState(list, psos[0]);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbvQ);
        ID3D12GraphicsCommandList_IASetIndexBuffer(list, &ibvQ);
        for (int i = 0; i < QUADS; i++)
            ID3D12GraphicsCommandList_DrawIndexedInstanced(list, 6, 1, i*6, 0, 0);

        /* 20 triangles with PSO 1 */
        ID3D12GraphicsCommandList_SetPipelineState(list, psos[1]);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbvT);
        for (int i = 0; i < TRIS; i++)
            ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, i*3, 0);

        /* 20 wireframe triangles with PSO 2 */
        ID3D12GraphicsCommandList_SetPipelineState(list, psos[2]);
        for (int i = 0; i < WIRES; i++)
            ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, (i*3) % (TRIS*3), 0);

        D3D12_RESOURCE_BARRIER b1 = {0};
        b1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b1.Transition.pResource = targets[backIdx];
        b1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b1.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b1.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b1);

        HRF(ID3D12GraphicsCommandList_Close(list), "Close(list)");
        ID3D12CommandList* lists[1] = {(ID3D12CommandList*)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        HRF(IDXGISwapChain1_Present(sc1, 1, 0), "Present");
        { MSG msg; while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg); }

        HRF(ID3D12CommandQueue_Signal(queue, fence, fenceVal), "Signal");
        if (ID3D12Fence_GetCompletedValue(fence) < fenceVal) {
            HRF(ID3D12Fence_SetEventOnCompletion(fence, fenceVal, evt), "SetEventOnCompletion");
            WaitForSingleObject(evt, 10000);
        }
        fenceVal++;
    }
    { char b[160]; wsprintfA(b, "scene12: %d frames, %d draws/frame, 3 PSOs, depth+texture\nscene12: RESULT 0\n",
                             SCENE12_FRAMES, QUADS+TRIS+WIRES); dbg(b); }
    return 0;
}
