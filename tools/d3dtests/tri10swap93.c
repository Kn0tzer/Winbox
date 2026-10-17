// tri10swap.c — D3D10 swapchain triangle for screenshot verification.
// Creates window + swapchain, renders simple colored triangle, presents in a loop.
// Reports: tri10swap: READY when presenting.
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>
#include <d3dcompiler.h>
#include <stdio.h>
DEFINE_GUID(IID_ID3D10Texture2D, 0x9b7e4c00, 0x342c, 0x4106, 0xa1, 0x9f, 0x4f, 0x27, 0x04, 0xf6, 0x89, 0xf0);

static const char* vs_src =
    "struct VSIn { float3 pos : POSITION; float4 col : COLOR; };\n"
    "struct PSIn { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "PSIn main(VSIn i) { PSIn o; o.pos = float4(i.pos, 1.0); o.col = i.col; return o; }\n";
static const char* ps_src =
    "struct PSIn { float4 pos : SV_Position; float4 col : COLOR; };\n"
    "float4 main(PSIn i) : SV_Target { return i.col; }\n";

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE || m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp; (void)cmd; (void)show;
    fprintf(stderr, "tri10swap: start\n");

    WNDCLASSA wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = "tri10swap";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("tri10swap", "tri10swap", WS_OVERLAPPEDWINDOW,
        0, 0, 640, 480, NULL, NULL, hi, NULL);
    if (!hwnd) { fprintf(stderr, "tri10swap: FAIL window\n"); return 1; }
    ShowWindow(hwnd, SW_SHOW);

    DXGI_SWAP_CHAIN_DESC scd; memset(&scd, 0, sizeof(scd));
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 640; scd.BufferDesc.Height = 480;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd; scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    ID3D10Device* dev = NULL; IDXGISwapChain* swap = NULL;
    HRESULT hr = D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_HARDWARE,
        NULL, 0, D3D10_SDK_VERSION, &scd, &swap, &dev);
    if (FAILED(hr)) {
        fprintf(stderr, "tri10swap: FAIL swapchain hr=0x%08X\n", (unsigned)hr);
        return 1;
    }
    fprintf(stderr, "tri10swap: device+swapchain OK\n");

    ID3DBlob* vsBlob = NULL; ID3DBlob* psBlob = NULL;
    D3DCompile(vs_src, strlen(vs_src), NULL, NULL, NULL, "main", "vs_4_0_level_9_3", 0, 0, &vsBlob, NULL);
    D3DCompile(ps_src, strlen(ps_src), NULL, NULL, NULL, "main", "ps_4_0_level_9_3", 0, 0, &psBlob, NULL);
    ID3D10VertexShader* vs = NULL; ID3D10PixelShader* ps = NULL;
    ID3D10Device_CreateVertexShader(dev, vsBlob->lpVtbl->GetBufferPointer(vsBlob),
        vsBlob->lpVtbl->GetBufferSize(vsBlob), &vs);
    ID3D10Device_CreatePixelShader(dev, psBlob->lpVtbl->GetBufferPointer(psBlob),
        psBlob->lpVtbl->GetBufferSize(psBlob), &ps);

    D3D10_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0 },
    };
    ID3D10InputLayout* il = NULL;
    ID3D10Device_CreateInputLayout(dev, layout, 2,
        vsBlob->lpVtbl->GetBufferPointer(vsBlob), vsBlob->lpVtbl->GetBufferSize(vsBlob), &il);

    struct V { float x, y, z, r, g, b, a; } verts[] = {
        {  0.0f,  0.5f, 0.5f,  1.0f, 0.0f, 0.0f, 1.0f },
        {  0.5f, -0.5f, 0.5f,  0.0f, 1.0f, 0.0f, 1.0f },
        { -0.5f, -0.5f, 0.5f,  0.0f, 0.0f, 1.0f, 1.0f },
    };
    D3D10_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = sizeof(verts); bd.Usage = D3D10_USAGE_DEFAULT;
    bd.BindFlags = D3D10_BIND_VERTEX_BUFFER;
    D3D10_SUBRESOURCE_DATA sd; memset(&sd, 0, sizeof(sd)); sd.pSysMem = verts;
    ID3D10Buffer* vb = NULL;
    ID3D10Device_CreateBuffer(dev, &bd, &sd, &vb);

    ID3D10Texture2D* backBuf = NULL;
    IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D10Texture2D, (void**)&backBuf);
    ID3D10RenderTargetView* rtv = NULL;
    ID3D10Device_CreateRenderTargetView(dev, (ID3D10Resource*)backBuf, NULL, &rtv);
    backBuf->lpVtbl->Release(backBuf);

    D3D10_VIEWPORT vp = { 0, 0, 640, 480, 0.0f, 1.0f };
    ID3D10Device_RSSetViewports(dev, 1, &vp);
    ID3D10Device_IASetInputLayout(dev, il);
    ID3D10Device_IASetPrimitiveTopology(dev, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(struct V), offset = 0;
    ID3D10Device_IASetVertexBuffers(dev, 0, 1, &vb, &stride, &offset);
    ID3D10Device_VSSetShader(dev, vs);
    ID3D10Device_PSSetShader(dev, ps);
    ID3D10Device_OMSetRenderTargets(dev, 1, &rtv, NULL);

    float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
    for (int f = 0; f < 300; f++) {
        ID3D10Device_ClearRenderTargetView(dev, rtv, clearColor);
        ID3D10Device_Draw(dev, 3, 0);
        IDXGISwapChain_Present(swap, 0, 0);
        if (f == 10) { fprintf(stderr, "tri10swap: READY\n"); fflush(stderr); }
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
    }
done:
    fprintf(stderr, "tri10swap: RESULT 0\n");
    return 0;
}
