/* Read-only Win32 window/GUI-thread inspection for live game probes. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

static BOOL CALLBACK inspect(HWND window, LPARAM context) {
    (void)context;
    char title[160] = {0}, cls[80] = {0}, line[512];
    RECT outer = {0}, client = {0}; DWORD pid = 0;
    DWORD tid = GetWindowThreadProcessId(window, &pid);
    GetWindowTextA(window, title, sizeof(title)); GetClassNameA(window, cls, sizeof(cls));
    GetWindowRect(window, &outer); GetClientRect(window, &client);
    GUITHREADINFO gui = {0}; gui.cbSize = sizeof(gui);
    BOOL queried = GetGUIThreadInfo(tid, &gui);
    snprintf(line, sizeof(line),
        "window-state: hwnd=%p title=%s class=%s pid=%lu tid=%lu visible=%u style=%08lx rect=%ld,%ld,%ld,%ld client=%ld,%ld gui=%u flags=%lu active=%p focus=%p\n",
        window, title, cls, pid, tid, IsWindowVisible(window), (unsigned long)GetWindowLongA(window, GWL_STYLE),
        outer.left, outer.top, outer.right, outer.bottom, client.right, client.bottom,
        queried, gui.flags, gui.hwndActive, gui.hwndFocus);
    OutputDebugStringA(line);
    return TRUE;
}

int main(void) {
    if (!EnumWindows(inspect, 0)) return 1;
    OutputDebugStringA("window-state: RESULT 0\n");
    return 0;
}
