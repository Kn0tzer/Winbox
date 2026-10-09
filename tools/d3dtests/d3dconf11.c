/*
 * d3dconf11.c -- D3D11 conformance matrix (64-bit, DXVK d3d11.dll).
 *
 * D3D11CreateDevice across feature levels 11_0/10_1/10_0/9_3/9_2/9_1
 * (each recorded -- 11_0+ is NOT assumed impossible), then full pipeline
 * (device+swapchain, buffer, D3DCompile shaders, input layout, draw,
 * present loop) at the highest successful feature level.
 *
 * Machine-readable log on stderr:
 *   d3dconf11: OP <name> RESULT <OK|FAIL|SKIP> hr=0x%08X ms=%u [fl=0x%X]
 *   d3dconf11: DONE ok=N fail=M skip=K
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o d3dconf11.exe d3dconf11.c \
 *     -ld3d11 -ldxgi -ld3dcompiler -lgdi32 -luser32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h> /* define the IIDs d3d11.h declares */
#include <d3d11.h>
#include <dxgi.h>
#include <d3dcompiler.h>
#include <stdio.h>

static int g_ok = 0, g_fail = 0, g_skip = 0;

static void opfl(const char* name, int ok, HRESULT hr, DWORD ms, D3D_FEATURE_LEVEL fl) {
    char b[256];
    wsprintfA(b, "d3dconf11: OP %-22s RESULT %s hr=0x%08X ms=%u fl=0x%X\n",
              name, ok ? "OK" : "FAIL", (unsigned)hr, (unsigned)ms, (unsigned)fl);
    fprintf(stderr, "%s", b); fflush(stderr);
    OutputDebugStringA(b);
    if (ok) g_ok++; else g_fail++;
}
static void op(const char* name, int ok, HRESULT hr, DWORD ms) {
    opfl(name, ok, hr, ms, 0);
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
    fprintf(stderr, "d3dconf11: start\n"); fflush(stderr);

    struct { const char* name; D3D_FEATURE_LEVEL fl; } fls[] = {
        { "fl_11_0", D3D_FEATURE_LEVEL_11_0 },
        { "fl_10_1", D3D_FEATURE_LEVEL_10_1 },
        { "fl_10_0", D3D_FEATURE_LEVEL_10_0 },
        { "fl_9_3",  D3D_FEATURE_LEVEL_9_3 },
        { "fl_9_2",  D3D_FEATURE_LEVEL_9_2 },
        { "fl_9_1",  D3D_FEATURE_LEVEL_9_1 },
    };
    D3D_FEATURE_LEVEL best = 0;
    int i;
    for (i = 0; i < 6; i++) {
        ID3D11Device* d = NULL; ID3D11DeviceContext* c = NULL;
        D3D_FEATURE_LEVEL out = 0;
        t0 = GetTickCount();
        hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                               &fls[i].fl, 1, D3D11_SDK_VERSION, &d, &out, &c);
        t1 = GetTickCount() - t0;
        opfl(fls[i].name, SUCCEEDED(hr), hr, t1, out);
        if (SUCCEEDED(hr)) {
            if (out > best) best = out;
            ID3D11DeviceContext_Release(c);
            ID3D11Device_Release(d);
        }
    }
    if (!best) {
        fprintf(stderr, "d3dconf11: no feature level available, aborting\n");
        fflush(stderr);
        goto done;
    }
    { char b[128]; wsprintfA(b, "d3dconf11: best FL=0x%X\n", (unsigned)best);
      fprintf(stderr, "%s", b); fflush(stderr); }

    WNDCLASSA wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = "d3dconf11";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("d3dconf11", "d3dconf11", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 480, 360, NULL, NULL, hi, NULL);
    if (!hwnd) { op("create_window", 0, E_FAIL, 0); goto done; }

    DXGI_SWAP_CHAIN_DESC scd; memset(&scd, 0, sizeof(scd));
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 480; scd.BufferDesc.Height = 360;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd; scd.SampleDesc.Count = 1;
    scd.Windowed = TRUE;

    ID3D11Device* dev = NULL; ID3D11DeviceContext* ctx = NULL;
    IDXGISwapChain* swap = NULL; D3D_FEATURE_LEVEL outfl = 0;
    t0 = GetTickCount();
    hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                                       &best, 1, D3D11_SDK_VERSION, &scd,
                                       &swap, &dev, &outfl, &ctx);
    t1 = GetTickCount() - t0;
    opfl("device_swapchain", SUCCEEDED(hr), hr, t1, outfl);
    if (FAILED(hr)) goto done;

    ID3D11Texture2D* backBuf = NULL;
    IDXGISwapChain_GetBuffer(swap, 0, &IID_ID3D11Texture2D, (void**)&backBuf);
    ID3D11RenderTargetView* rtv = NULL;
    t0 = GetTickCount();
    hr = backBuf ? ID3D11Device_CreateRenderTargetView(dev, (ID3D11Resource*)backBuf,
                                                       NULL, &rtv) : E_FAIL;
    op("create_rtv", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (backBuf) ID3D11Texture2D_Release(backBuf);
    if (FAILED(hr)) goto done;

    ID3DBlob* vsBlob = NULL; ID3DBlob* psBlob = NULL; ID3DBlob* errBlob = NULL;
    t0 = GetTickCount();
    hr = D3DCompile(vs_src, strlen(vs_src), NULL, NULL, NULL, "main", "vs_4_0",
                    0, 0, &vsBlob, &errBlob);
    if (FAILED(hr) && errBlob) {
        fprintf(stderr, "d3dconf11: vs compile err: %s\n",
                (char*)ID3D10Blob_GetBufferPointer(errBlob)); fflush(stderr);
    }
    if (errBlob) ID3D10Blob_Release(errBlob); errBlob = NULL;
    HRESULT hr2 = D3DCompile(ps_src, strlen(ps_src), NULL, NULL, NULL, "main", "ps_4_0",
                             0, 0, &psBlob, &errBlob);
    if (FAILED(hr2)) hr = hr2;
    if (FAILED(hr2) && errBlob) {
        fprintf(stderr, "d3dconf11: ps compile err: %s\n",
                (char*)ID3D10Blob_GetBufferPointer(errBlob)); fflush(stderr);
    }
    if (errBlob) ID3D10Blob_Release(errBlob);
    op("compile_vs_ps_4_0", SUCCEEDED(hr) && vsBlob && psBlob, hr, GetTickCount() - t0);
    if (!vsBlob || !psBlob) goto done;

    ID3D11VertexShader* vs = NULL; ID3D11PixelShader* ps = NULL;
    ID3D11InputLayout* il = NULL;
    t0 = GetTickCount();
    hr = ID3D11Device_CreateVertexShader(dev, ID3D10Blob_GetBufferPointer(vsBlob),
                                         ID3D10Blob_GetBufferSize(vsBlob), NULL, &vs);
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreatePixelShader(dev, ID3D10Blob_GetBufferPointer(psBlob),
                                            ID3D10Blob_GetBufferSize(psBlob), NULL, &ps);
    D3D11_INPUT_ELEMENT_DESC layout[] = {
        { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        { "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0 },
    };
    if (SUCCEEDED(hr))
        hr = ID3D11Device_CreateInputLayout(dev, layout, 2,
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
    D3D11_BUFFER_DESC bd; memset(&bd, 0, sizeof(bd));
    bd.ByteWidth = sizeof(verts); bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    D3D11_SUBRESOURCE_DATA sd; memset(&sd, 0, sizeof(sd)); sd.pSysMem = verts;
    ID3D11Buffer* vb = NULL;
    t0 = GetTickCount();
    hr = ID3D11Device_CreateBuffer(dev, &bd, &sd, &vb);
    op("vb_create", SUCCEEDED(hr), hr, GetTickCount() - t0);
    if (FAILED(hr)) goto done;

    D3D11_VIEWPORT vp; memset(&vp, 0, sizeof(vp));
    vp.Width = 480; vp.Height = 360; vp.MaxDepth = 1.0f;
    ID3D11DeviceContext_RSSetViewports(ctx, 1, &vp);
    ID3D11DeviceContext_IASetInputLayout(ctx, il);
    ID3D11DeviceContext_IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    UINT stride = sizeof(struct V), offset = 0;
    ID3D11DeviceContext_IASetVertexBuffers(ctx, 0, 1, &vb, &stride, &offset);
    ID3D11DeviceContext_VSSetShader(ctx, vs, NULL, 0);
    ID3D11DeviceContext_PSSetShader(ctx, ps, NULL, 0);
    ID3D11DeviceContext_OMSetRenderTargets(ctx, 1, &rtv, NULL);

    float clearColor[4] = { 0.2f, 0.0f, 0.4f, 1.0f };
    t0 = GetTickCount();
    hr = S_OK; int f;
    for (f = 0; f < 30; f++) {
        ID3D11DeviceContext_ClearRenderTargetView(ctx, rtv, clearColor);
        ID3D11DeviceContext_Draw(ctx, 3, 0);
        hr = IDXGISwapChain_Present(swap, 0, 0);
        if (FAILED(hr)) break;
        { MSG msg; while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto ploop_done;
            TranslateMessage(&msg); DispatchMessage(&msg); } }
    }
ploop_done:
    t1 = GetTickCount() - t0;
    { char b[128]; wsprintfA(b, "d3dconf11: draw_present_30f frames=%d total_ms=%u\n",
                             f, (unsigned)t1);
      fprintf(stderr, "%s", b); fflush(stderr); }
    op("draw_present_30f", SUCCEEDED(hr) && f == 30, hr, t1);

    if (vb) ID3D11Buffer_Release(vb);
    if (il) ID3D11InputLayout_Release(il);
    if (vs) ID3D11VertexShader_Release(vs);
    if (ps) ID3D11PixelShader_Release(ps);
    if (rtv) ID3D11RenderTargetView_Release(rtv);
    if (swap) IDXGISwapChain_Release(swap);
    if (ctx) ID3D11DeviceContext_Release(ctx);
    if (dev) ID3D11Device_Release(dev);
done:
    { char b[128]; wsprintfA(b, "d3dconf11: DONE ok=%d fail=%d skip=%d\n",
                             g_ok, g_fail, g_skip);
      fprintf(stderr, "%s", b); fflush(stderr); OutputDebugStringA(b); }
    return g_fail ? 1 : 0;
}
