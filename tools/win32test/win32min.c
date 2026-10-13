// Minimal 32-bit Win32 window app: NO D3D9, NO Vulkan. Pure user32.
#include <windows.h>
#include <stdio.h>
static const char CLASS_NAME[] = "Bw64MinWin32";
LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    fprintf(stdout, "WIN32MIN: START\n"); fflush(stdout);
    fprintf(stderr, "WIN32MIN: START-ERR\n"); fflush(stderr);
    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = CLASS_NAME;
    if (!RegisterClassA(&wc)) {
        fprintf(stdout, "WIN32MIN: REGISTERCLASS-FAILED %lu\n", GetLastError()); fflush(stdout);
        return 1;
    }
    fprintf(stdout, "WIN32MIN: REGISTERCLASS-OK\n"); fflush(stdout);
    HWND hwnd = CreateWindowExA(0, CLASS_NAME, "bw64-min32", WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 320, 240, NULL, NULL, hInst, NULL);
    if (!hwnd) {
        fprintf(stdout, "WIN32MIN: CREATEWINDOW-FAILED %lu\n", GetLastError()); fflush(stdout);
        return 2;
    }
    fprintf(stdout, "WIN32MIN: CREATEWINDOW-OK hwnd=%p\n", (void*)hwnd); fflush(stdout);
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    fprintf(stdout, "WIN32MIN: SHOWN\n"); fflush(stdout);
    DWORD start = GetTickCount();
    MSG msg;
    int frames = 0;
    while (GetTickCount() - start < 5000) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        frames++;
        Sleep(16);
    }
done:
    fprintf(stdout, "WIN32MIN: DONE frames=%d\n", frames); fflush(stdout);
    fprintf(stderr, "WIN32MIN: DONE-ERR\n"); fflush(stderr);
    DestroyWindow(hwnd);
    return 0;
}
