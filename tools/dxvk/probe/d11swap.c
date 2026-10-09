/* d11swap bisect: FL sweep then D3D11CreateDeviceAndSwapChain at best FL.
 * Logs each step; isolates the clean-exit(0) seen in d3dconf11. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <initguid.h>
#include <d3d11.h>
#include <dxgi.h>
#include <stdio.h>

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE || m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp; (void)cmd; (void)show;
    fprintf(stderr, "d11swap: start\n"); fflush(stderr);
    OutputDebugStringA("d11swap: start\n");
    D3D_FEATURE_LEVEL fls[] = {
        D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0,
        D3D_FEATURE_LEVEL_9_3, D3D_FEATURE_LEVEL_9_2, D3D_FEATURE_LEVEL_9_1 };
    D3D_FEATURE_LEVEL best = 0;
    for (int i = 0; i < 6; i++) {
        ID3D11Device* d = NULL; ID3D11DeviceContext* c = NULL; D3D_FEATURE_LEVEL out = 0;
        HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                                       &fls[i], 1, D3D11_SDK_VERSION, &d, &out, &c);
        char b[160];
        wsprintfA(b, "d11swap: fl=0x%X hr=0x%08X out=0x%X\n",
                  (unsigned)fls[i], (unsigned)hr, (unsigned)out);
        fprintf(stderr, "%s", b); fflush(stderr); OutputDebugStringA(b);
        if (SUCCEEDED(hr)) { if (out > best) best = out;
            ID3D11DeviceContext_Release(c); ID3D11Device_Release(d); }
    }
    { char b[64]; wsprintfA(b, "d11swap: best=0x%X, creating window\n", (unsigned)best);
      fprintf(stderr, "%s", b); fflush(stderr); OutputDebugStringA(b); }
    WNDCLASSA wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = WndProc; wc.hInstance = hi; wc.lpszClassName = "d11swap";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("d11swap", "d11swap", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 480, 360, NULL, NULL, hi, NULL);
    fprintf(stderr, "d11swap: window %p, calling CreateDeviceAndSwapChain\n", hwnd);
    fflush(stderr); OutputDebugStringA("d11swap: calling CreateDeviceAndSwapChain\n");
    DXGI_SWAP_CHAIN_DESC scd; memset(&scd, 0, sizeof(scd));
    scd.BufferCount = 1;
    scd.BufferDesc.Width = 480; scd.BufferDesc.Height = 360;
    scd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    scd.OutputWindow = hwnd; scd.SampleDesc.Count = 1; scd.Windowed = TRUE;
    ID3D11Device* dev = NULL; ID3D11DeviceContext* ctx = NULL;
    IDXGISwapChain* swap = NULL; D3D_FEATURE_LEVEL outfl = 0;
    HRESULT hr = D3D11CreateDeviceAndSwapChain(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                                               &best, 1, D3D11_SDK_VERSION, &scd,
                                               &swap, &dev, &outfl, &ctx);
    { char b[128]; wsprintfA(b, "d11swap: CreateDeviceAndSwapChain hr=0x%08X fl=0x%X\n",
                             (unsigned)hr, (unsigned)outfl);
      fprintf(stderr, "%s", b); fflush(stderr); OutputDebugStringA(b); }
    fprintf(stderr, "d11swap: DONE\n"); fflush(stderr);
    OutputDebugStringA("d11swap: DONE\n");
    return 0;
}
