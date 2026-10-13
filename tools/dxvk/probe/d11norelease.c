/* d11norelease: 25x D3D11CreateDevice WITHOUT release at FL 9_3 in one process.
 * Bisects the nondeterministic clean exit(0) seen on repeated
 * create/release cycles (d11swap: 3rd call, d3dconf11: 7th call).
 * Logs every cycle; success = "d11norelease: DONE" after cycle 24.
 * Build: x86_64-w64-mingw32-gcc -O2 -o d11norelease.exe d11norelease.c -ld3d11 -lgdi32 -luser32
 */
#define COBJMACROS
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <stdio.h>

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmd, int show) {
    (void)hp; (void)cmd; (void)show; (void)hi;
    fprintf(stderr, "d11norelease: start\n"); fflush(stderr);
    OutputDebugStringA("d11norelease: start\n");
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_9_3;
    for (int i = 0; i < 25; i++) {
        ID3D11Device* d = NULL; ID3D11DeviceContext* c = NULL;
        D3D_FEATURE_LEVEL out = 0;
        HRESULT hr = D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
                                       &fl, 1, D3D11_SDK_VERSION, &d, &out, &c);
        char b[128];
        wsprintfA(b, "d11norelease: cycle %d create hr=0x%08X\n", i, (unsigned)hr);
        fprintf(stderr, "%s", b); fflush(stderr); OutputDebugStringA(b);
        if (SUCCEEDED(hr)) {
            /* NOT releasing */
            wsprintfA(b, "d11norelease: cycle %d leaked\n", i);
            fprintf(stderr, "%s", b); fflush(stderr); OutputDebugStringA(b);
        } else {
            wsprintfA(b, "d11norelease: cycle %d CREATE FAILED, continuing\n", i);
            fprintf(stderr, "%s", b); fflush(stderr); OutputDebugStringA(b);
        }
    }
    fprintf(stderr, "d11norelease: DONE\n"); fflush(stderr);
    OutputDebugStringA("d11norelease: DONE\n");
    return 0;
}
