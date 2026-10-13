/* Win32 input witness: no graphics API, real window messages and RAWINPUT. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>

static void emit(const char* format, ...) {
    char message[256]; va_list arguments;
    va_start(arguments, format); vsnprintf(message, sizeof(message), format, arguments); va_end(arguments);
    OutputDebugStringA(message);
}

static unsigned raw_sequence;
static LRESULT CALLBACK wndproc(HWND window, UINT message, WPARAM w, LPARAM l) {
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS) {
        emit("input-witness: FOCUS %u\n", (unsigned)message);
    } else if (message == WM_MOUSEMOVE) {
        emit("input-witness: MOTION x=%d y=%d\n", (short)LOWORD(l), (short)HIWORD(l));
    } else if (message == WM_LBUTTONDOWN || message == WM_LBUTTONUP) {
        emit("input-witness: BUTTON %s\n", message == WM_LBUTTONDOWN ? "DOWN" : "UP");
    }
    if (message == WM_KEYDOWN || message == WM_KEYUP) {
        emit("input-witness: KEY %s vk=%u\n", message == WM_KEYDOWN ? "DOWN" : "UP", (unsigned)w);
    } else if (message == WM_INPUT) {
        RAWINPUT input; UINT size = sizeof(input);
        if (GetRawInputData((HRAWINPUT)l, RID_INPUT, &input, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1 && input.header.dwType == RIM_TYPEMOUSE) {
            emit("input-witness: RAW seq=%u flags=%u dx=%ld dy=%ld\n", ++raw_sequence,
                input.data.mouse.usFlags, input.data.mouse.lLastX, input.data.mouse.lLastY);
        }
    } else if (message == WM_CLOSE) {
        DestroyWindow(window); return 0;
    } else if (message == WM_DESTROY) {
        PostQuitMessage(0); return 0;
    }
    return DefWindowProcA(window, message, w, l);
}

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR arguments, int show) {
    (void)previous; (void)arguments; (void)show;
    WNDCLASSA cls = {0}; cls.lpfnWndProc = wndproc; cls.hInstance = instance; cls.lpszClassName = "InputWitness";
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassA(&cls)) return 1;
    HWND window = CreateWindowA(cls.lpszClassName, "Input witness", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        0, 0, 640, 480, NULL, NULL, instance, NULL);
    if (!window) return 2;
    RAWINPUTDEVICE device = {1, 2, RIDEV_INPUTSINK, window};
    if (!RegisterRawInputDevices(&device, 1, sizeof(device))) {
        emit("input-witness: FAIL raw registration %lu\n", GetLastError()); return 3;
    }
    SetForegroundWindow(window); SetFocus(window);
    UpdateWindow(window);
    int ready = 0;
    DWORD started = GetTickCount();
    DWORD heartbeat = started;
    while (GetTickCount() - started < 180000) {
        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) return 0;
            TranslateMessage(&msg); DispatchMessageA(&msg);
        }
        // CreateWindow returns before the initial map/focus messages have
        // settled. Announce readiness after the message pump services them.
        if (!ready) {
            SetForegroundWindow(window); SetFocus(window);
            emit("input-witness: READY window=%p foreground=%p focus=%p\n", window, GetForegroundWindow(), GetFocus());
            ready = 1;
        }
        if (GetTickCount() - heartbeat > 5000) {
            heartbeat = GetTickCount();
            emit("input-witness: ALIVE foreground=%p focus=%p\n", GetForegroundWindow(), GetFocus());
        }
        Sleep(5);
    }
    return 0;
}
