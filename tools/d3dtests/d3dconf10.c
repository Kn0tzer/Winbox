/*
 * d3dconf10.c -- D3D10 conformance matrix (64-bit, DXVK d3d10 on d3d11).
 *
 * D3D10CreateDevice, device+swapchain, D3DCompile shaders, input layout,
 * vertex buffer, draw + present loop.
 *
 * Machine-readable log on stderr:
 *   d3dconf10: OP <name> RESULT <OK|FAIL|SKIP> hr=0x%08X ms=%u
 *   d3dconf10: DONE ok=N fail=M skip=K
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o d3dconf10.exe d3dconf10.c \
 *     -ld3d10 -ld3dcompiler -ldxgi -lgdi32 -luser32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h> /* define the IIDs d3d10.h declares */
#include <d3d10.h>
#include <d3dcompiler.h>
#include <stdio.h>

static int g_ok = 0, g_fail = 0, g_skip = 0;

static void op(const char* name, int ok, HRESULT hr, DWORD ms) {
    char b[256];
    wsprintfA(b, "d3dconf10: OP %-22s RESULT %s hr=0x%08X ms=%u\n",
              name, ok ? "OK" : "FAIL", (unsigned)hr, (unsigned)ms);
    fprintf(stderr, "%s", b); fflush(stderr);
    OutputDebugStringA(b);
    if (ok) g_ok++; else g_fail++;
}
static void opskip(const char* name, const char* why) {
    char b[256];
    wsprintfA(b, "d3dconf10: OP %-22s RESULT SKIP (%s)\n", name, why);
    fprintf(stderr, "%s", b); fflush(stderr);
    OutputDebugStringA(b);
    g_skip++;
}

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
    DWORD t0, t1; HRESULT hr;
    fprintf(stderr, "d3dconf10: start\n"); fflush(stderr);

    /* NOTE: bare D3D10CreateDevice (no swapchain) tested separately via
     * d10bare.exe (hr=0 OK). It is intentionally NOT done here first:
     * creating+releasing a device before the swapchain device may perturb
     * the render path under test. */

    WNDCLASSA wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = "d3dconf10";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("d3dconf10", "d3dconf10", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 480, 360, NULL, NULL, hi, NULL);
    if (!hwnd) { op("create_window", 0, E_FAIL, 0); goto done; }

    DXGI_SWAP_CHAIN_DESC scd; memset(&scd, 0, sizeof(scd));
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 480; scd.BufferDesc.Height = 360;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd; scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    ID3D10Device* dev = NULL; IDXGISwapChain* swap = NULL;
    t0 = GetTickCount();
    hr = D3D10CreateDeviceAndSwapChain(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
                                       D3D10_SDK_VERSION, &scd, &swap, &dev);
    t1 = GetTickCount() - t0;
    op("device_swapchain", SUCCEEDED(hr), hr, t1);
    if (FAILED(hr)) goto done;

    ID3D10Texture2D* backBuf = NULL;
    IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D10Texture2D, (void**)&backBuf);
    ID3D10RenderTargetView* rtv = NULL;
    t0 = GetTickCount();
    hr = backBuf ? ID3D10Device_CreateRenderTargetView(dev, (ID3D10Resource*)backBuf,
                                                       NULL, &rtv) : E_FAIL;
    op("create_rtv", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (backBuf) ID3D10Texture2D_Release(backBuf);
    if (FAILED(hr)) goto done;

    ID3DBlob* vsBlob = NULL; ID3DBlob* psBlob = NULL; ID3DBlob* errBlob = NULL;
    t0 = GetTickCount();
    hr = D3DCompile(vs_src, strlen(vs_src), NULL, NULL, NULL, "main", "vs_4_0",
                    0, 0, &vsBlob, &errBlob);
    if (FAILED(hr) && errBlob) {
        fprintf(stderr, "d3dconf10: vs compile err: %s\n",
                (char*)ID3D10Blob_GetBufferPointer(errBlob)); fflush(stderr);
    }
    if (errBlob) ID3D10Blob_Release(errBlob); errBlob = NULL;
    HRESULT hr2 = D3DCompile(ps_src, strlen(ps_src), NULL, NULL, NULL, "main", "ps_4_0",
                             0, 0, &psBlob, &errBlob);
    if (FAILED(hr2)) hr = hr2;
    if (FAILED(hr2) && errBlob) {
        fprintf(stderr, "d3dconf10: ps compile err: %s\n",
                (char*)ID3D10Blob_GetBufferPointer(errBlob)); fflush(stderr);
    }
    if (errBlob) ID3D10Blob_Release(errBlob);
    op("compile_vs_ps_4_0", SUCCEEDED(hr) && vsBlob && psBlob, hr, GetTickCount() - t0);
    if (!vsBlob || !psBlob) goto done;

    ID3D10VertexShader* vs = NULL; ID3D10PixelShader* ps = NULL;
    ID3D10InputLayout* il = NULL;
    t0 = GetTickCount();
    hr = ID3D10Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsBlob),
                                         ID3D10Blob_GetBufferSize(vsBlob), &vs);
    if (SUCCEEDED(hr))
        hr = ID3D10Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psBlob),
                                            ID3D10Blob_GetBufferSize(psBlob), &ps);
    D3D10_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D10_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D10_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (SUCCEEDED(hr))
        hr = ID3D10Device_CreateInputLayout(dev, layout, 2,
                ID3D10Blob_GetBufferPointer(vsBlob),
                ID3D10Blob_GetBufferSize(vsBlob), &il);
    op("shaders_inputlayout", SUCCEEDED(hr), hr, GetTickCount() - t0);
    ID3D10Blob_Release(vsBlob); ID3D10Blob_Release(psBlob);
    if (FAILED(hr)) goto done;

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
    t0 = GetTickCount();
    hr = ID3D10Device_CreateBuffer(dev, &bd, &sd, &vb);
    op("vb_create", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (FAILED(hr)) goto done;

    D3D10_VIEWPORT vp = { 0, 0, 480, 360, 0.0f, 1.0f };
    ID3D10Device_RSSetViewports(dev, 1, &vp);
    ID3D10Device_IASetInputLayout(dev, il);
    ID3D10Device_IASetPrimitiveTopology(dev, D3D10_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(struct V), offset = 0;
    ID3D10Device_IASetVertexBuffers(dev, 0, 1, &vb, &stride, &offset);
    ID3D10Device_VSSetShader(dev, vs);
    ID3D10Device_PSSetShader(dev, ps);
    ID3D10Device_OMSetRenderTargets(dev, 1, &rtv, NULL);

    float clearColor[4] = { 0.1f, 0.1f, 0.3f, 1.0f };
    t0 = GetTickCount();
    hr = S_OK; int f;
    for (f = 0; f < 30; f++) {
        ID3D10Device_ClearRenderTargetView(dev, rtv, clearColor);
        ID3D10Device_Draw(dev, 3, 0);
        hr = IDXGISwapChain_Present(swap, 0, 0);
        if (FAILED(hr)) break;
        { MSG msg; while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto ploop_done;
            TranslateMessage(&msg); DispatchMessage(&msg); } }
    }
ploop_done:
    t1 = GetTickCount() - t0;
    { char b[128]; wsprintfA(b, "d3dconf10: draw_present_30f frames=%d total_ms=%u\n",
                             f, (unsigned)t1);
      fprintf(stderr, "%s", b); fflush(stderr); }
    op("draw_present_30f", SUCCEEDED(hr) && f == 30, hr, t1);

    if (vb) ID3D10Buffer_Release(vb);
    if (il) ID3D10InputLayout_Release(il);
    if (vs) ID3D10VertexShader_Release(vs);
    if (ps) ID3D10PixelShader_Release(ps);
    if (rtv) ID3D10RenderTargetView_Release(rtv);
    if (swap) IDXGISwapChain_Release(swap);
    if (dev) ID3D10Device_Release(dev);

    /* Bare D3D10CreateDevice AFTER the render path: creating+releasing a
     * device before the swapchain device may perturb rendering, so the
     * render test above runs first. (d10bare.exe verified hr=0 standalone.) */
    t0 = GetTickCount();
    ID3D10Device* dev0 = NULL;
    hr = D3D10CreateDevice(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
                           D3D10_SDK_VERSION, &dev0);
    op("device_10_0", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (dev0) ID3D10Device_Release(dev0);
done:
    { char b[128]; wsprintfA(b, "d3dconf10: DONE ok=%d fail=%d skip=%d\n",
                             g_ok, g_fail, g_skip);
      fprintf(stderr, "%s", b); fflush(stderr); OutputDebugStringA(b); }
    return g_fail ? 1 : 0;
}
