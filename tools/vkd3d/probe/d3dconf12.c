/*
 * d3dconf12.c -- D3D12 conformance matrix (64-bit, vkd3d-proton d3d12.dll).
 *
 * Adapted from tri12draw.c with per-op machine-readable logging:
 *   d3dconf12: OP <name> RESULT <OK|FAIL|SKIP> hr=0x%08X ms=%u
 *   d3dconf12: DONE ok=N fail=M skip=K
 *
 * Ops: D3D12CreateDevice at FL 12_0 (recorded, non-fatal) and 11_0,
 * command queue/allocator/list, swapchain, RTV heap, root signature,
 * PSO, vertex buffer upload, draw+present loop.
 * Depends on triangle_vs.h / triangle_ps.h (place next to this file).
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o d3dconf12.exe d3dconf12.c -lole32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d12.h>
#include <dxgi1_2.h>
#include <dxgi1_4.h>
#include <stdio.h>

#include "triangle_vs.h"
#include "triangle_ps.h"

#define FRAMES 10
#define W 480
#define H 360

static int g_ok = 0, g_fail = 0, g_skip = 0;

static void op(const char* name, int ok, HRESULT hr, DWORD ms) {
    char b[256];
    wsprintfA(b, "d3dconf12: OP %-22s RESULT %s hr=0x%08X ms=%u\n",
              name, ok ? "OK" : "FAIL", (unsigned)hr, (unsigned)ms);
    fprintf(stderr, "%s", b); fflush(stderr);
    OutputDebugStringA(b);
    if (ok) g_ok++; else g_fail++;
}
#define OPFATAL(hrv, what) do { if (FAILED(hrv)) { \
    op(what, 0, hrv, 0); goto done; } } while (0)

typedef HRESULT (WINAPI *PFN_D3D12_CREATE_DEVICE)(IUnknown*, D3D_FEATURE_LEVEL, REFIID, void**);
typedef HRESULT (WINAPI *PFN_CREATE_DXGI_FACTORY2)(UINT, REFIID, void**);
typedef HRESULT (WINAPI *PFN_D3D12_SERIALIZE_ROOT_SIG)(const D3D12_ROOT_SIGNATURE_DESC*,
        D3D_ROOT_SIGNATURE_VERSION, ID3DBlob**, ID3DBlob**);

static LRESULT CALLBACK wndproc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcA(h, m, w, l);
}

/* COBJMACROS aggregate-return wrapper is broken in this mingw; call the
 * vtable slot directly for GetCPUDescriptorHandleForHeapStart. */
static void heap_start(ID3D12DescriptorHeap* heap, D3D12_CPU_DESCRIPTOR_HANDLE* out) {
    ((void (STDMETHODCALLTYPE *)(ID3D12DescriptorHeap*, D3D12_CPU_DESCRIPTOR_HANDLE*))
        heap->lpVtbl->GetCPUDescriptorHandleForHeapStart)(heap, out);
}

struct vertex { float pos[3]; float col[4]; };

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hInst; (void)hPrev; (void)cmd; (void)show;
    DWORD t0; HRESULT hr;
    fprintf(stderr, "d3dconf12: start\n"); fflush(stderr);

    t0 = GetTickCount();
    HMODULE d3d12 = LoadLibraryA("d3d12.dll");
    op("load_d3d12", d3d12 != NULL, d3d12 ? S_OK : E_FAIL, GetTickCount() - t0);
    t0 = GetTickCount();
    HMODULE dxgi = LoadLibraryA("dxgi.dll");
    op("load_dxgi", dxgi != NULL, dxgi ? S_OK : E_FAIL, GetTickCount() - t0);
    if (!d3d12 || !dxgi) goto done;

    PFN_D3D12_CREATE_DEVICE pD3D12CreateDevice =
        (PFN_D3D12_CREATE_DEVICE)GetProcAddress(d3d12, "D3D12CreateDevice");
    PFN_CREATE_DXGI_FACTORY2 pCreateDXGIFactory2 =
        (PFN_CREATE_DXGI_FACTORY2)GetProcAddress(dxgi, "CreateDXGIFactory2");
    PFN_D3D12_SERIALIZE_ROOT_SIG pSerializeRootSig =
        (PFN_D3D12_SERIALIZE_ROOT_SIG)GetProcAddress(d3d12, "D3D12SerializeRootSignature");
    op("getprocaddrs", pD3D12CreateDevice && pCreateDXGIFactory2 && pSerializeRootSig,
       (pD3D12CreateDevice && pCreateDXGIFactory2 && pSerializeRootSig) ? S_OK : E_FAIL, 0);
    if (!pD3D12CreateDevice || !pCreateDXGIFactory2 || !pSerializeRootSig) goto done;

    /* FL 12_0 attempt: recorded, non-fatal. */
    ID3D12Device* dev12 = NULL;
    t0 = GetTickCount();
    hr = pD3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_12_0, &IID_ID3D12Device, (void**)&dev12);
    op("device_FL_12_0", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (dev12) { ID3D12Device_Release(dev12); dev12 = NULL; }

    ID3D12Device* device = NULL;
    t0 = GetTickCount();
    hr = pD3D12CreateDevice(NULL, D3D_FEATURE_LEVEL_11_0, &IID_ID3D12Device, (void**)&device);
    op("device_FL_11_0", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (FAILED(hr)) goto done;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = wndproc; wc.hInstance = hInst; wc.lpszClassName = "D3DCONF12";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowExA(0, "D3DCONF12", "d3dconf12",
                                WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                                0, 0, W, H, NULL, NULL, hInst, NULL);
    op("create_window", hwnd != NULL, hwnd ? S_OK : E_FAIL, 0);
    if (!hwnd) goto done;

    D3D12_COMMAND_QUEUE_DESC qd = {0};
    qd.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    ID3D12CommandQueue* queue = NULL;
    t0 = GetTickCount();
    hr = ID3D12Device_CreateCommandQueue(device, &qd, &IID_ID3D12CommandQueue, (void**)&queue);
    op("create_cmdqueue", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (FAILED(hr)) goto done;

    IDXGIFactory2* factory = NULL;
    t0 = GetTickCount();
    hr = pCreateDXGIFactory2(0, &IID_IDXGIFactory2, (void**)&factory);
    op("dxgi_factory2", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (FAILED(hr)) goto done;

    DXGI_SWAP_CHAIN_DESC1 scd = {0};
    scd.BufferCount = 2; scd.Width = W; scd.Height = H;
    scd.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.SampleDesc.Count = 1;
    IDXGISwapChain1* sc1 = NULL;
    t0 = GetTickCount();
    hr = IDXGIFactory2_CreateSwapChainForHwnd(factory, (IUnknown*)queue, hwnd,
                                             &scd, NULL, NULL, &sc1);
    op("swapchain_hwnd", SUCCEEDED(hr), hr, GetTickCount() - t0);
    IDXGIFactory2_Release(factory);
    if (FAILED(hr)) goto done;
    IDXGISwapChain3* sc3 = NULL;
    t0 = GetTickCount();
    hr = IDXGISwapChain1_QueryInterface(sc1, &IID_IDXGISwapChain3, (void**)&sc3);
    op("qi_swapchain3", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (FAILED(hr)) goto done;

    D3D12_DESCRIPTOR_HEAP_DESC hd = {0};
    hd.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; hd.NumDescriptors = 2;
    ID3D12DescriptorHeap* rtvHeap = NULL;
    t0 = GetTickCount();
    hr = ID3D12Device_CreateDescriptorHeap(device, &hd, &IID_ID3D12DescriptorHeap,
                                           (void**)&rtvHeap);
    op("rtv_heap", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (FAILED(hr)) goto done;
    UINT rtvSize = ID3D12Device_GetDescriptorHandleIncrementSize(device,
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);

    ID3D12Resource* targets[2] = {NULL, NULL};
    D3D12_CPU_DESCRIPTOR_HANDLE rtv;
    heap_start(rtvHeap, &rtv);
    t0 = GetTickCount();
    hr = S_OK;
    for (int i = 0; i < 2; i++) {
        hr = IDXGISwapChain1_GetBuffer(sc1, i, &IID_ID3D12Resource, (void**)&targets[i]);
        if (FAILED(hr)) break;
        ID3D12Device_CreateRenderTargetView(device, targets[i], NULL, rtv);
        rtv.ptr += rtvSize;
    }
    op("getbuffer_rtvs", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (FAILED(hr)) goto done;

    D3D12_ROOT_SIGNATURE_DESC rsd = {0};
    rsd.Flags = D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;
    ID3DBlob* rsBlob = NULL; ID3DBlob* rsErr = NULL;
    t0 = GetTickCount();
    hr = pSerializeRootSig(&rsd, D3D_ROOT_SIGNATURE_VERSION_1, &rsBlob, &rsErr);
    if (FAILED(hr) && rsErr)
        fprintf(stderr, "d3dconf12: rs serialize err: %s\n",
                (char*)ID3D10Blob_GetBufferPointer(rsErr));
    if (rsErr) ID3D10Blob_Release(rsErr);
    op("serialize_rootsig", SUCCEEDED(hr), hr, GetTickCount() - t0);
    ID3D12RootSignature* rootSig = NULL;
    t0 = GetTickCount();
    hr = rsBlob ? ID3D12Device_CreateRootSignature(device, 0,
            ID3D10Blob_GetBufferPointer(rsBlob), ID3D10Blob_GetBufferSize(rsBlob),
            &IID_ID3D12RootSignature, (void**)&rootSig) : E_FAIL;
    op("create_rootsig", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (rsBlob) ID3D10Blob_Release(rsBlob);
    if (FAILED(hr)) goto done;

    D3D12_INPUT_ELEMENT_DESC il[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
    };
    D3D12_GRAPHICS_PIPELINE_STATE_DESC pso = {0};
    pso.InputLayout.pInputElementDescs = il; pso.InputLayout.NumElements = 2;
    pso.pRootSignature = rootSig;
    pso.VS.pShaderBytecode = g_vs_main; pso.VS.BytecodeLength = sizeof(g_vs_main);
    pso.PS.pShaderBytecode = g_ps_main; pso.PS.BytecodeLength = sizeof(g_ps_main);
    pso.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
    pso.RasterizerState.CullMode = D3D12_CULL_MODE_BACK;
    pso.RasterizerState.FrontCounterClockwise = FALSE;
    pso.RasterizerState.DepthClipEnable = TRUE;
    pso.RasterizerState.ConservativeRaster = D3D12_CONSERVATIVE_RASTERIZATION_MODE_OFF;
    pso.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_ZERO;
    pso.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
    pso.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
    pso.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
    pso.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
    pso.SampleMask = UINT_MAX;
    pso.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    pso.NumRenderTargets = 1;
    pso.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    pso.SampleDesc.Count = 1;
    ID3D12PipelineState* pipeline = NULL;
    t0 = GetTickCount();
    hr = ID3D12Device_CreateGraphicsPipelineState(device, &pso,
            &IID_ID3D12PipelineState, (void**)&pipeline);
    op("create_pso", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (FAILED(hr)) goto done;

    ID3D12CommandAllocator* alloc = NULL;
    t0 = GetTickCount();
    hr = ID3D12Device_CreateCommandAllocator(device, D3D12_COMMAND_LIST_TYPE_DIRECT,
            &IID_ID3D12CommandAllocator, (void**)&alloc);
    op("create_cmdalloc", SUCCEEDED(hr), hr, GetTickCount() - t0);
    ID3D12GraphicsCommandList* list = NULL;
    if (SUCCEEDED(hr))
        hr = ID3D12Device_CreateCommandList(device, 0, D3D12_COMMAND_LIST_TYPE_DIRECT,
                alloc, pipeline, &IID_ID3D12GraphicsCommandList, (void**)&list);
    if (SUCCEEDED(hr)) hr = ID3D12GraphicsCommandList_Close(list);
    if (FAILED(hr)) { op("create_cmdlist", 0, hr, 0); goto done; }
    op("create_cmdlist", 1, S_OK, 0);

    struct vertex verts[3] = {
        {{ 0.0f,  0.5f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}},
        {{ 0.5f, -0.5f, 0.0f}, {0.0f, 1.0f, 0.0f, 1.0f}},
        {{-0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f, 1.0f}},
    };
    D3D12_HEAP_PROPERTIES hpu = {0}; hpu.Type = D3D12_HEAP_TYPE_UPLOAD;
    D3D12_RESOURCE_DESC rdu = {0};
    rdu.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER; rdu.Width = sizeof(verts);
    rdu.Height = 1; rdu.DepthOrArraySize = 1; rdu.MipLevels = 1;
    rdu.SampleDesc.Count = 1; rdu.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
    ID3D12Resource* vb = NULL;
    t0 = GetTickCount();
    hr = ID3D12Device_CreateCommittedResource(device, &hpu, D3D12_HEAP_FLAG_NONE, &rdu,
            D3D12_RESOURCE_STATE_GENERIC_READ, NULL, &IID_ID3D12Resource, (void**)&vb);
    if (SUCCEEDED(hr)) {
        void* mapped = NULL; D3D12_RANGE rr = {0, 0};
        hr = ID3D12Resource_Map(vb, 0, &rr, &mapped);
        if (SUCCEEDED(hr)) { memcpy(mapped, verts, sizeof(verts));
            ID3D12Resource_Unmap(vb, 0, NULL); }
    }
    op("vb_upload", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (FAILED(hr)) goto done;
    D3D12_VERTEX_BUFFER_VIEW vbv;
    vbv.BufferLocation = ID3D12Resource_GetGPUVirtualAddress(vb);
    vbv.SizeInBytes = sizeof(verts); vbv.StrideInBytes = sizeof(struct vertex);

    ID3D12Fence* fence = NULL;
    t0 = GetTickCount();
    hr = ID3D12Device_CreateFence(device, 0, D3D12_FENCE_FLAG_NONE,
                                  &IID_ID3D12Fence, (void**)&fence);
    op("create_fence", SUCCEEDED(hr), hr, GetTickCount() - t0);
    HANDLE evt = CreateEventA(NULL, FALSE, FALSE, NULL);
    if (!evt) { op("create_event", 0, E_FAIL, 0); goto done; }

    D3D12_VIEWPORT vp = {0, 0, (float)W, (float)H, 0.0f, 1.0f};
    D3D12_RECT sc = {0, 0, W, H};
    float clearColor[4] = {0.4f, 0.2f, 0.0f, 1.0f};
    UINT64 fenceVal = 1;
    t0 = GetTickCount();
    hr = S_OK; int f;
    for (f = 0; f < FRAMES; f++) {
        UINT backIdx = IDXGISwapChain3_GetCurrentBackBufferIndex(sc3);
        hr = ID3D12CommandAllocator_Reset(alloc); if (FAILED(hr)) break;
        hr = ID3D12GraphicsCommandList_Reset(list, alloc, pipeline); if (FAILED(hr)) break;
        ID3D12GraphicsCommandList_SetGraphicsRootSignature(list, rootSig);
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
        ID3D12GraphicsCommandList_OMSetRenderTargets(list, 1, &rtv, FALSE, NULL);
        ID3D12GraphicsCommandList_ClearRenderTargetView(list, rtv, clearColor, 0, NULL);
        ID3D12GraphicsCommandList_IASetPrimitiveTopology(list, D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        ID3D12GraphicsCommandList_IASetVertexBuffers(list, 0, 1, &vbv);
        ID3D12GraphicsCommandList_DrawInstanced(list, 3, 1, 0, 0);
        D3D12_RESOURCE_BARRIER b1 = {0};
        b1.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
        b1.Transition.pResource = targets[backIdx];
        b1.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
        b1.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
        b1.Transition.StateAfter = D3D12_RESOURCE_STATE_PRESENT;
        ID3D12GraphicsCommandList_ResourceBarrier(list, 1, &b1);
        hr = ID3D12GraphicsCommandList_Close(list); if (FAILED(hr)) break;
        ID3D12CommandList* lists[1] = {(ID3D12CommandList*)list};
        ID3D12CommandQueue_ExecuteCommandLists(queue, 1, lists);
        hr = IDXGISwapChain1_Present(sc1, 1, 0); if (FAILED(hr)) break;
        { MSG msg; while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) DispatchMessageA(&msg); }
        hr = ID3D12CommandQueue_Signal(queue, fence, fenceVal); if (FAILED(hr)) break;
        if (ID3D12Fence_GetCompletedValue(fence) < fenceVal) {
            hr = ID3D12Fence_SetEventOnCompletion(fence, fenceVal, evt); if (FAILED(hr)) break;
            WaitForSingleObject(evt, 10000);
        }
        fenceVal++;
    }
    { char b[128]; wsprintfA(b, "d3dconf12: draw_present_%df frames=%d total_ms=%u\n",
                             FRAMES, f, (unsigned)(GetTickCount() - t0));
      fprintf(stderr, "%s", b); fflush(stderr); }
    op("draw_present_10f", SUCCEEDED(hr) && f == FRAMES, hr, GetTickCount() - t0);

done:
    { char b[128]; wsprintfA(b, "d3dconf12: DONE ok=%d fail=%d skip=%d\n",
                             g_ok, g_fail, g_skip);
      fprintf(stderr, "%s", b); fflush(stderr); OutputDebugStringA(b); }
    return g_fail ? 1 : 0;
}
