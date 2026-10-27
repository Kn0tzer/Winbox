// kbprobe.c -- guest-side keyboard/input observer for the Winbox rig.
// Creates a real top-level window, then logs every input signal a Windows
// game could consume: WM_KEYDOWN/UP (+lParam detail), WM_SYSKEY*, WM_CHAR,
// WM_INPUT raw keyboard/mouse, GetAsyncKeyState edges, focus changes, and
// XInput controller presence. Output goes to stdout (guest stdio reaches
// the page console in this emulator), prefix "kbprobe:".
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

static unsigned long long g_seq = 0;
static HWND g_hwnd = NULL;
static DWORD g_keydownCount[256];
static DWORD g_keyupCount[256];
static DWORD g_rawCount = 0, g_charCount = 0, g_setfocus = 0, g_killfocus = 0;
static DWORD g_mousedown = 0;

static void klog(const char* fmt, ...) {
    char buf[512];
    va_list ap; va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    printf("kbprobe: [%llu t=%lu] %s\n", ++g_seq, (unsigned long)GetTickCount(), buf);
    fflush(stdout);
}

// ---- async-state edge polling -------------------------------------------
struct VkWatch { int vk; const char* name; int prev; };
static struct VkWatch g_watch[] = {
    {VK_RETURN,"RETURN",0},{VK_SPACE,"SPACE",0},{VK_ESCAPE,"ESCAPE",0},
    {VK_UP,"UP",0},{VK_DOWN,"DOWN",0},{VK_LEFT,"LEFT",0},{VK_RIGHT,"RIGHT",0},
    {'A',"A",0},{'W',"W",0},{'S',"S",0},{'D',"D",0},{'M',"M",0},
    {VK_LSHIFT,"LSHIFT",0},{VK_RSHIFT,"RSHIFT",0},{VK_SHIFT,"SHIFT",0},
    {VK_LCONTROL,"LCONTROL",0},{VK_RCONTROL,"RCONTROL",0},{VK_CONTROL,"CONTROL",0},
    {VK_LMENU,"LALT",0},{VK_RMENU,"RALT",0},
    {VK_F1,"F1",0},{VK_F11,"F11",0},{VK_F12,"F12",0},
    {VK_PRIOR,"PAGEUP",0},{VK_NEXT,"PAGEDOWN",0},
    {VK_INSERT,"INSERT",0},{VK_DELETE,"DELETE",0},{VK_HOME,"HOME",0},{VK_END,"END",0},
    {VK_TAB,"TAB",0},{VK_BACK,"BACKSPACE",0},{VK_CAPITAL,"CAPSLOCK",0},
    {VK_NUMLOCK,"NUMLOCK",0},{VK_SCROLL,"SCROLLLOCK",0},{VK_SNAPSHOT,"PRINTSCREEN",0},
    {VK_PAUSE,"PAUSE",0},{VK_NUMPAD0,"NUMPAD0",0},{VK_NUMPAD1,"NUMPAD1",0},
    {VK_LWIN,"LWIN",0},{VK_RWIN,"RWIN",0},{VK_APPS,"APPS",0},
    {0,NULL,0}
};
static void pollAsync(void) {
    for (int i = 0; g_watch[i].name; i++) {
        int down = (GetAsyncKeyState(g_watch[i].vk) & 0x8000) ? 1 : 0;
        if (down != g_watch[i].prev) {
            klog("ASYNC %s %s (vk=0x%02X)", g_watch[i].name, down ? "DOWN" : "UP", g_watch[i].vk);
            g_watch[i].prev = down;
        }
    }
}

static HWND g_lastFocus = (HWND)1, g_lastFg = (HWND)1;
static void pollFocus(void) {
    HWND f = GetFocus(), fg = GetForegroundWindow();
    if (f != g_lastFocus || fg != g_lastFg) {
        klog("FOCUSSTATE focus=%p foreground=%p self=%p", f, fg, g_hwnd);
        g_lastFocus = f; g_lastFg = fg;
    }
}

// ---- XInput ---------------------------------------------------------------
typedef DWORD (WINAPI *XInputGetStateFn)(DWORD, void*);
static XInputGetStateFn g_xinput = NULL;
static void probeXInput(const char* when) {
    if (!g_xinput) { klog("XINPUT unavailable (xinput dll/getstate missing) at %s", when); return; }
    for (DWORD i = 0; i < 4; i++) {
        BYTE packet[64]; memset(packet, 0, sizeof(packet));
        DWORD rc = g_xinput(i, packet);
        klog("XINPUT slot %lu rc=%lu %s at %s", (unsigned long)i, (unsigned long)rc,
             rc == 0 /*ERROR_SUCCESS*/ ? "CONNECTED" : "absent", when);
    }
}

// ---- window proc ------------------------------------------------------------
static LRESULT CALLBACK wndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
        case WM_SETFOCUS: g_setfocus++; klog("WM_SETFOCUS (count=%lu)", (unsigned long)g_setfocus); break;
        case WM_KILLFOCUS: g_killfocus++; klog("WM_KILLFOCUS"); break;
        case WM_MOUSEACTIVATE: klog("WM_MOUSEACTIVATE"); return MA_ACTIVATE;
        case WM_LBUTTONDOWN: g_mousedown++; klog("WM_LBUTTONDOWN x=%d y=%d (count=%lu)", (int)(short)LOWORD(lp), (int)(short)HIWORD(lp), (unsigned long)g_mousedown); break;
        case WM_RBUTTONDOWN: klog("WM_RBUTTONDOWN x=%d y=%d", (int)(short)LOWORD(lp), (int)(short)HIWORD(lp)); break;
        case WM_KEYDOWN: case WM_KEYUP: case WM_SYSKEYDOWN: case WM_SYSKEYUP: {
            int vk = (int)wp;
            unsigned scan = (unsigned)((lp >> 16) & 0xFF);
            int ext = (lp & (1L << 24)) ? 1 : 0;
            int prev = (lp & (1L << 30)) ? 1 : 0;
            unsigned rep = (unsigned)(lp & 0xFFFF);
            const char* nm = msg == WM_KEYDOWN ? "WM_KEYDOWN" : msg == WM_KEYUP ? "WM_KEYUP"
                           : msg == WM_SYSKEYDOWN ? "WM_SYSKEYDOWN" : "WM_SYSKEYUP";
            if (msg == WM_KEYDOWN && vk >= 0 && vk < 256) g_keydownCount[vk]++;
            if (msg == WM_KEYUP && vk >= 0 && vk < 256) g_keyupCount[vk]++;
            klog("%s vk=0x%02X scan=0x%02X ext=%d prev=%d repeat=%u", nm, vk, scan, ext, prev, rep);
            return 0;
        }
        case WM_CHAR: g_charCount++; klog("WM_CHAR cp=0x%04X", (unsigned)wp); return 0;
        case WM_INPUT: {
            UINT size = 0;
            GetRawInputData((HRAWINPUT)lp, RID_INPUT, NULL, &size, sizeof(RAWINPUTHEADER));
            BYTE buf[128];
            if (size <= sizeof(buf) && GetRawInputData((HRAWINPUT)lp, RID_INPUT, buf, &size, sizeof(RAWINPUTHEADER)) != (UINT)-1) {
                RAWINPUT* ri = (RAWINPUT*)buf;
                if (ri->header.dwType == RIM_TYPEKEYBOARD) {
                    g_rawCount++;
                    klog("RAWKEY vkey=0x%02X make=0x%02X flags=0x%04X", ri->data.keyboard.VKey,
                         ri->data.keyboard.MakeCode, ri->data.keyboard.Flags);
                } else if (ri->header.dwType == RIM_TYPEMOUSE) {
                    if (ri->data.mouse.usButtonFlags) klog("RAWMOUSE buttons=0x%04X dx=%ld dy=%ld",
                        ri->data.mouse.usButtonFlags, ri->data.mouse.lLastX, ri->data.mouse.lLastY);
                }
            }
            return 0;
        }
        case WM_TIMER: pollAsync(); pollFocus(); return 0;
        case WM_CLOSE: klog("WM_CLOSE"); DestroyWindow(hwnd); return 0;
        case WM_DESTROY: {
            klog("SUMMARY keydown-by-vk:");
            for (int v = 0; v < 256; v++) if (g_keydownCount[v])
                klog("SUMMARY vk=0x%02X down=%lu up=%lu", v, (unsigned long)g_keydownCount[v], (unsigned long)g_keyupCount[v]);
            klog("SUMMARY raw=%lu char=%lu setfocus=%lu killfocus=%lu mousedown=%lu",
                 (unsigned long)g_rawCount, (unsigned long)g_charCount, (unsigned long)g_setfocus,
                 (unsigned long)g_killfocus, (unsigned long)g_mousedown);
            probeXInput("exit");
            klog("DONE");
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(int argc, char** argv) {
    int runSecs = argc > 1 ? atoi(argv[1]) : 600;
    klog("START kbprobe x64 runSecs=%d", runSecs);
    // XInput presence
    HMODULE xi = LoadLibraryA("xinput1_3.dll");
    if (!xi) xi = LoadLibraryA("xinput1_4.dll");
    if (!xi) xi = LoadLibraryA("xinput9_1_0.dll");
    if (xi) g_xinput = (XInputGetStateFn)GetProcAddress(xi, "XInputGetState");
    klog("XINPUT dll=%p getstate=%p", xi, (void*)g_xinput);
    probeXInput("start");

    UINT ndev = 0; GetRawInputDeviceList(NULL, &ndev, sizeof(RAWINPUTDEVICELIST));
    klog("RAWINPUTDEVICES count=%u", ndev);

    WNDCLASSA wc; memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = wndProc; wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "KbProbeWnd"; wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassA(&wc)) { klog("FATAL RegisterClass gle=%lu", (unsigned long)GetLastError()); return 1; }
    g_hwnd = CreateWindowExA(0, "KbProbeWnd", "kbprobe", WS_OVERLAPPEDWINDOW,
                             100, 100, 640, 360, NULL, NULL, wc.hInstance, NULL);
    if (!g_hwnd) { klog("FATAL CreateWindow gle=%lu", (unsigned long)GetLastError()); return 1; }
    ShowWindow(g_hwnd, SW_SHOW);
    UpdateWindow(g_hwnd);
    BOOL fg = SetForegroundWindow(g_hwnd);
    SetFocus(g_hwnd);
    klog("WINDOW hwnd=%p SetForegroundWindow=%d GetFocus=%p GetForeground=%p", g_hwnd, (int)fg, GetFocus(), GetForegroundWindow());

    RAWINPUTDEVICE rid[2];
    rid[0].usUsagePage = 0x01; rid[0].usUsage = 0x06; rid[0].dwFlags = RIDEV_INPUTSINK; rid[0].hwndTarget = g_hwnd;
    rid[1].usUsagePage = 0x01; rid[1].usUsage = 0x02; rid[1].dwFlags = RIDEV_INPUTSINK; rid[1].hwndTarget = g_hwnd;
    klog("RegisterRawInputDevices=%d", (int)RegisterRawInputDevices(rid, 2, sizeof(RAWINPUTDEVICE)));
    SetTimer(g_hwnd, 1, 200, NULL);
    klog("READY");

    DWORD start = GetTickCount();
    MSG msg;
    for (;;) {
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) { klog("QUIT"); return 0; }
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
        if ((int)(GetTickCount() - start) > runSecs * 1000) {
            klog("TIMEUP");
            PostMessageA(g_hwnd, WM_CLOSE, 0, 0);
        }
        Sleep(5);
    }
}
