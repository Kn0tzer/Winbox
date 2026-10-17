/* Exercise Wine's fullscreen message pump and real clipped RAWINPUT. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static void emit(const char* format, ...) {
    char text[256]; va_list args;
    va_start(args, format); vsnprintf(text, sizeof(text), format, args); va_end(args);
    OutputDebugStringA(text);
}
static unsigned raw_sequence;
static LRESULT CALLBACK window_proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_INPUT) {
        RAWINPUT input; UINT size = sizeof(input);
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1
            && input.header.dwType == RIM_TYPEMOUSE)
            emit("cursor-clip: RAW seq=%u flags=%u dx=%ld dy=%ld\n", ++raw_sequence, input.data.mouse.usFlags,
                input.data.mouse.lLastX, input.data.mouse.lLastY);
    } else if (message == WM_LBUTTONUP) {
        emit("cursor-clip: BUTTON UP\n");
    } else if (message == WM_KEYDOWN || message == WM_KEYUP) {
        emit("cursor-clip: KEY %s vk=%u\n", message == WM_KEYDOWN ? "DOWN" : "UP", (unsigned)w);
    } else if (message == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(window, message, w, l);
}
int main(void) {
    WNDCLASSA cls = {0}; cls.lpfnWndProc = window_proc; cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "CursorClipWitness"; cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassA(&cls)) return 1;
    HWND window = CreateWindowA(cls.lpszClassName, "Fullscreen cursor witness", WS_POPUP | WS_VISIBLE,
        0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), NULL, NULL, cls.hInstance, NULL);
    if (!window) return 2;
    RAWINPUTDEVICE mouse = {1, 2, RIDEV_INPUTSINK, window};
    if (!RegisterRawInputDevices(&mouse, 1, sizeof(mouse))) return 3;
    SetForegroundWindow(window); SetFocus(window); UpdateWindow(window);
    emit("cursor-clip: MAPPED\n");
    DWORD started = GetTickCount(), heartbeat = started;
    int ready = 0;
    while (GetTickCount() - started < 90000) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        if (!ready) {
            RECT clip; GetWindowRect(window, &clip); InflateRect(&clip, -1, -1);
            if (!ClipCursor(&clip)) return 4;
            emit("cursor-clip: READY\n"); ready = 1;
        }
        if (GetTickCount() - heartbeat > 1000) {
            RECT clip; GetClipCursor(&clip); heartbeat = GetTickCount();
            emit("cursor-clip: ALIVE clip=%ld,%ld,%ld,%ld\n", clip.left, clip.top, clip.right, clip.bottom);
        }
        Sleep(5);
    }
    ClipCursor(NULL); DestroyWindow(window);
    return 0;
}
