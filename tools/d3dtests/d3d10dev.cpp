#include <windows.h>
#include <d3d10.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hInst; (void)hPrev; (void)cmd; (void)show;
    printf("d3d10dev: start\n"); fflush(stdout);

    ID3D10Device* dev = NULL;
    HRESULT hr = D3D10CreateDevice(NULL, D3D10_DRIVER_TYPE_HARDWARE, NULL, 0,
        D3D10_SDK_VERSION, &dev);
    printf("d3d10dev: D3D10CreateDevice hr=0x%08lx\n", (unsigned long)hr); fflush(stdout);
    if (SUCCEEDED(hr)) {


        dev->Release();
        printf("d3d10dev: RESULT 0\n");
    } else {
        printf("d3d10dev: RESULT 1\n");
    }
    fflush(stdout);
    return SUCCEEDED(hr) ? 0 : 1;
}
