/* Minimal: bare D3D10CreateDevice only. Tests whether it returns or hangs. */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d10.h>
#include <stdio.h>
int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp; (void)cmd; (void)show; (void)hi;
    fprintf(stderr, "d10bare: start\n"); fflush(stderr);
    OutputDebugStringA("d10bare: start\n");
    DWORD t0 = GetTickCount();
    ID3D10Device* dev = NULL;
    HRESULT hr = D3D10CreateDevice(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
                                   D3D10_SDK_VERSION, &dev);
    DWORD ms = GetTickCount() - t0;
    char b[128];
    wsprintfA(b, "d10bare: D3D10CreateDevice hr=0x%08X ms=%u dev=%p\n",
              (unsigned)hr, (unsigned)ms, dev);
    fprintf(stderr, "%s", b); fflush(stderr);
    OutputDebugStringA(b);
    fprintf(stderr, "d10bare: DONE\n"); fflush(stderr);
    OutputDebugStringA("d10bare: DONE\n");
    return 0;
}
