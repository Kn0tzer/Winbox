/* FD-168 proof fixture: call the fourteen win32u syscall exports that
 * Wine 8.0 shipped as spec stubs and verify each one answers with real,
 * cross-checked behaviour. Every win32u call runs under a vectored
 * exception handler guard: on a build where the export is still a
 * stub, the call raises the Wine stub exception, the guard records the
 * check as failed, and the run continues so the log shows the full
 * per-function picture on both builds.
 */
#define _WIN32_WINNT 0x0602
#define WINVER 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <wingdi.h>
#include <imm.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <setjmp.h>

static int failures;
static int stub_hit;
static DWORD stub_code;

static jmp_buf guard_env;
static volatile int guard_armed;

static LONG WINAPI guard_veh(EXCEPTION_POINTERS *ep)
{
    if (guard_armed)
    {
        guard_armed = 0;
        stub_code = ep->ExceptionRecord->ExceptionCode;
        longjmp(guard_env, 1);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

#define GUARD(stmt) do { \
    if (setjmp(guard_env) == 0) { guard_armed = 1; stmt; guard_armed = 0; } \
    else stub_hit = 1; \
} while (0)

static void emit(const char *format, ...)
{
    char text[512];
    va_list args;
    va_start(args, format);
    vsnprintf(text, sizeof(text), format, args);
    va_end(args);
    /* the suite harness captures guest stdout (the dbwin-echo channel);
     * OutputDebugStringA goes nowhere without a session listener */
    fputs(text, stdout);
    fflush(stdout);
}

static void check(const char *name, int ok, const char *detail)
{
    emit("win32u-fd168: CHECK %s ok=%d %s\n", name, ok, detail ? detail : "");
    if (!ok) failures++;
}

/* ---- local declarations for the win32u exports under test ---- */
struct ntuser_name_list { ULONG size; ULONG count; WCHAR strings[1]; };
struct ntuser_property_list { UINT64 data; ATOM atom; BOOLEAN string; };
typedef enum { WindowProcess, WindowProcess2, WindowThread, WindowActiveWindow,
               WindowFocusWindow, WindowIsHung, WindowClientBase,
               WindowIsForegroundThread, WindowDefaultImeWindow,
               WindowDefaultInputContext } WINDOWINFOCLASS_;
typedef enum { UserThreadStateFocusWindow, UserThreadStateActiveWindow,
               UserThreadStateCaptureWindow, UserThreadStateDefaultImeWindow,
               UserThreadStateDefaultInputContext, UserThreadStateInputState,
               UserThreadStateCursor, UserThreadStateExtraInfo,
               UserThreadStateInSendMessage, UserThreadStateMessageTime,
               UserThreadStateIsForeground } USERTHREADSTATECLASS_;

typedef NTSTATUS (WINAPI *fn_BuildNameList)(HWINSTA, ULONG, struct ntuser_name_list *, ULONG *);
typedef NTSTATUS (WINAPI *fn_BuildPropList)(HWND, ULONG, struct ntuser_property_list *, ULONG *);
typedef NTSTATUS (WINAPI *fn_BuildHimcList)(UINT, UINT, HIMC *, UINT *);
typedef HANDLE   (WINAPI *fn_QueryWindow)(HWND, WINDOWINFOCLASS_);
typedef LONG     (WINAPI *fn_QueryDisplayConfig)(UINT32, UINT32 *, DISPLAYCONFIG_PATH_INFO *, UINT32 *, DISPLAYCONFIG_MODE_INFO *, DISPLAYCONFIG_TOPOLOGY_ID *);
typedef ULONG_PTR (WINAPI *fn_GetThreadState)(USERTHREADSTATECLASS_);
typedef ATOM     (WINAPI *fn_RegisterWindowMessage)(UNICODE_STRING *);
typedef BOOL     (WINAPI *fn_GetClipCursor)(RECT *);
typedef BOOL     (WINAPI *fn_GetPointerType)(UINT32, POINTER_INPUT_TYPE *);
typedef ULONG    (WINAPI *fn_AlterWindowStyle)(HWND, UINT, UINT);
typedef BOOL     (WINAPI *fn_GetMiterLimit)(HDC, FLOAT *);
typedef BOOL     (WINAPI *fn_SetMiterLimit)(HDC, DWORD, FLOAT *);
typedef BOOL     (WINAPI *fn_IcmBrushInfo)(HDC, HBRUSH, BITMAPINFO *, void *, ULONG *, UINT *, BOOL *, UINT);
typedef BOOL     (WINAPI *fn_GetCurrentInputMessageSource)(INPUT_MESSAGE_SOURCE *);

static fn_BuildNameList pBuildNameList;
static fn_BuildPropList pBuildPropList;
static fn_BuildHimcList pBuildHimcList;
static fn_QueryWindow pQueryWindow;
static fn_QueryDisplayConfig pQueryDisplayConfig;
static fn_GetThreadState pGetThreadState;
static fn_RegisterWindowMessage pRegisterWindowMessage;
static fn_GetClipCursor pGetClipCursor;
static fn_GetPointerType pGetPointerType;
static fn_AlterWindowStyle pAlterWindowStyle;
static fn_GetMiterLimit pGetMiterLimit;
static fn_SetMiterLimit pSetMiterLimit;
static fn_IcmBrushInfo pIcmBrushInfo;
static fn_GetCurrentInputMessageSource pGetCurrentInputMessageSource;

/* scratch holders written from inside GUARD regions */
static NTSTATUS g_status, g_status2;
static HANDLE g_handle, g_handle2;
static ATOM g_atom, g_atom2;
static BOOL g_bool, g_bool2, g_unk;
static ULONG g_ulong, g_ret, g_bits_size;
static ULONG_PTR g_uptr, g_uptr2, g_uptr3;
static FLOAT g_float, g_float2;
static UINT g_uint, g_usage;
static LONG g_long;
static DWORD g_dword;
static UINT32 g_np2, g_nm2;
static RECT g_rect;
static POINTER_INPUT_TYPE g_ptype;
static struct ntuser_property_list g_props[16];
static ULONG g_count;
static HIMC g_himc_list[16];
static INPUT_MESSAGE_SOURCE g_imsrc;
static BOOL g_imsrc_ok;
static UINT g_size;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_APP + 7) { g_imsrc_ok = pGetCurrentInputMessageSource(&g_imsrc); return 0; }
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int main(void)
{
    char detail[256];
    HWND hwnd;
    HMODULE u;
    WNDCLASSA cls = {0};

    AddVectoredExceptionHandler(1, guard_veh);

    u = LoadLibraryA("win32u.dll");
    if (!u) { emit("win32u-fd168: RESULT 99 no win32u\n"); return 99; }

    pBuildNameList = (fn_BuildNameList)GetProcAddress(u, "NtUserBuildNameList");
    pBuildPropList = (fn_BuildPropList)GetProcAddress(u, "NtUserBuildPropList");
    pBuildHimcList = (fn_BuildHimcList)GetProcAddress(u, "NtUserBuildHimcList");
    pQueryWindow = (fn_QueryWindow)GetProcAddress(u, "NtUserQueryWindow");
    pQueryDisplayConfig = (fn_QueryDisplayConfig)GetProcAddress(u, "NtUserQueryDisplayConfig");
    pGetThreadState = (fn_GetThreadState)GetProcAddress(u, "NtUserGetThreadState");
    pRegisterWindowMessage = (fn_RegisterWindowMessage)GetProcAddress(u, "NtUserRegisterWindowMessage");
    pGetClipCursor = (fn_GetClipCursor)GetProcAddress(u, "NtUserGetClipCursor");
    pGetPointerType = (fn_GetPointerType)GetProcAddress(u, "NtUserGetPointerType");
    pAlterWindowStyle = (fn_AlterWindowStyle)GetProcAddress(u, "NtUserAlterWindowStyle");
    pGetMiterLimit = (fn_GetMiterLimit)GetProcAddress(u, "NtGdiGetMiterLimit");
    pSetMiterLimit = (fn_SetMiterLimit)GetProcAddress(u, "NtGdiSetMiterLimit");
    pIcmBrushInfo = (fn_IcmBrushInfo)GetProcAddress(u, "NtGdiIcmBrushInfo");
    pGetCurrentInputMessageSource = (fn_GetCurrentInputMessageSource)GetProcAddress(u, "NtUserGetCurrentInputMessageSource");
    if (!pBuildNameList || !pBuildPropList || !pBuildHimcList || !pQueryWindow ||
        !pQueryDisplayConfig || !pGetThreadState || !pRegisterWindowMessage ||
        !pGetClipCursor || !pGetPointerType || !pAlterWindowStyle ||
        !pGetMiterLimit || !pSetMiterLimit || !pIcmBrushInfo || !pGetCurrentInputMessageSource)
    {
        emit("win32u-fd168: RESULT 98 missing exports\n");
        return 98;
    }

    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "FD168Wnd";
    RegisterClassA(&cls);
    hwnd = CreateWindowExA(0, "FD168Wnd", "fd168", WS_OVERLAPPEDWINDOW,
                           10, 10, 320, 200, NULL, NULL, cls.hInstance, NULL);
    if (!hwnd) { emit("win32u-fd168: RESULT 97 no window\n"); return 97; }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);

    /* ---- NtUserBuildNameList ---- */
    {
        static WCHAR buf[8192];
        struct ntuser_name_list *list = (struct ntuser_name_list *)buf;
        int found_winsta0 = 0, found_default = 0;

        g_status = (NTSTATUS)0xdeadbeef; g_ret = 0; stub_hit = 0; stub_code = 0;
        GUARD(g_status = pBuildNameList(0, sizeof(buf), list, &g_ret));
        if (!stub_hit && g_status == 0 && list->count >= 1)
        {
            WCHAR *s = list->strings;
            ULONG i;
            for (i = 0; i < list->count; i++)
            {
                if (!wcscmp(s, L"WinSta0")) found_winsta0 = 1;
                while (*s) s++;
                s++;
            }
        }
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx status=%08lx count=%lu winsta0=%d",
                 stub_hit, (unsigned long)stub_code, (unsigned long)g_status,
                 stub_hit ? 0UL : (unsigned long)list->count, found_winsta0);
        check("BuildNameList(winstations)", !stub_hit && g_status == 0 && found_winsta0, detail);

        g_status = (NTSTATUS)0xdeadbeef; g_ret = 0; stub_hit = 0; stub_code = 0;
        memset(buf, 0, sizeof(buf));
        GUARD(g_status = pBuildNameList(GetProcessWindowStation(), sizeof(buf), list, &g_ret));
        if (!stub_hit && g_status == 0 && list->count >= 1)
        {
            WCHAR *s = list->strings;
            ULONG i;
            for (i = 0; i < list->count; i++)
            {
                if (!wcscmp(s, L"Default")) found_default = 1;
                while (*s) s++;
                s++;
            }
        }
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx status=%08lx default=%d",
                 stub_hit, (unsigned long)stub_code, (unsigned long)g_status, found_default);
        check("BuildNameList(desktops)", !stub_hit && g_status == 0 && found_default, detail);

        g_status2 = (NTSTATUS)0xdeadbeef; g_ret = 0; stub_hit = 0; stub_code = 0;
        GUARD(g_status2 = pBuildNameList(0, 12, list, &g_ret));
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx status=%08lx ret=%lu",
                 stub_hit, (unsigned long)stub_code, (unsigned long)g_status2, g_ret);
        check("BuildNameList(too-small)", !stub_hit && g_status2 == (NTSTATUS)0xC0000023L && g_ret > 12, detail);
    }

    /* ---- NtUserRegisterWindowMessage ---- */
    {
        WCHAR namebuf[64];
        UNICODE_STRING us;
        UINT via_user32 = 0;
        wcscpy(namebuf, L"FD168.Probe.Msg");
        us.Buffer = namebuf;
        us.Length = (USHORT)(wcslen(namebuf) * sizeof(WCHAR));
        us.MaximumLength = sizeof(namebuf);
        g_atom = 0; g_atom2 = 0; stub_hit = 0; stub_code = 0;
        GUARD(g_atom = pRegisterWindowMessage(&us); g_atom2 = pRegisterWindowMessage(&us));
        via_user32 = RegisterWindowMessageA("FD168.Probe.Msg");
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx a1=%u a2=%u user32=%u",
                 stub_hit, (unsigned long)stub_code, g_atom, g_atom2, via_user32);
        check("RegisterWindowMessage", !stub_hit && g_atom >= 0xC000 && g_atom == g_atom2 &&
              g_atom == (ATOM)via_user32, detail);
    }

    /* ---- NtUserQueryWindow ---- */
    {
        DWORD pid = 0;
        DWORD tid = GetWindowThreadProcessId(hwnd, &pid);
        g_handle = 0; g_handle2 = 0; stub_hit = 0; stub_code = 0;
        GUARD(g_handle = pQueryWindow(hwnd, WindowProcess);
              g_handle2 = pQueryWindow(hwnd, WindowThread));
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx pid %lu vs %lu, tid %lu vs %lu",
                 stub_hit, (unsigned long)stub_code,
                 (unsigned long)(ULONG_PTR)g_handle, (unsigned long)pid,
                 (unsigned long)(ULONG_PTR)g_handle2, (unsigned long)tid);
        check("QueryWindow", !stub_hit && (DWORD)(ULONG_PTR)g_handle == pid &&
              (DWORD)(ULONG_PTR)g_handle2 == tid, detail);
    }

    /* ---- NtUserBuildPropList ---- */
    {
        int found = 0;
        ULONG i;
        SetPropA(hwnd, "FD168Prop", (HANDLE)(UINT_PTR)0x1234);
        g_status = (NTSTATUS)0xdeadbeef; g_count = 0xdead; stub_hit = 0; stub_code = 0;
        GUARD(g_status = pBuildPropList(hwnd, 16, g_props, &g_count));
        if (!stub_hit && g_status == 0)
            for (i = 0; i < g_count && i < 16; i++)
                if (g_props[i].data == 0x1234) found = 1;
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx status=%08lx count=%lu found=%d",
                 stub_hit, (unsigned long)stub_code, (unsigned long)g_status, g_count, found);
        check("BuildPropList", !stub_hit && g_status == 0 && found, detail);
        RemovePropA(hwnd, "FD168Prop");
    }

    /* ---- NtUserBuildHimcList ---- */
    {
        HIMC himc = ImmCreateContext();
        int found = 0;
        UINT i;
        g_status = (NTSTATUS)0xdeadbeef; g_status2 = (NTSTATUS)0xdeadbeef; g_size = 0xdead;
        stub_hit = 0; stub_code = 0;
        GUARD(g_status = pBuildHimcList(GetCurrentThreadId(), 16, g_himc_list, &g_size);
              g_status2 = pBuildHimcList(GetCurrentThreadId(), 16, NULL, &g_size));
        if (!stub_hit && g_status == 0 && himc)
            for (i = 0; i < g_size && i < 16; i++)
                if (g_himc_list[i] == himc) found = 1;
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx status=%08lx null=%08lx size=%u himc=%p found=%d",
                 stub_hit, (unsigned long)stub_code, (unsigned long)g_status,
                 (unsigned long)g_status2, g_size, (void *)himc, found);
        check("BuildHimcList", !stub_hit && g_status == 0 &&
              g_status2 == (NTSTATUS)0xC0000001L && found, detail);
        if (himc) ImmDestroyContext(himc);
    }

    /* ---- NtUserGetThreadState ---- */
    {
        LPARAM old = SetMessageExtraInfo((LPARAM)0x1BADB002);
        SetFocus(hwnd);
        g_uptr = 0; g_uptr2 = 0; g_uptr3 = 0; stub_hit = 0; stub_code = 0;
        GUARD(g_uptr = pGetThreadState(UserThreadStateExtraInfo);
              g_uptr2 = pGetThreadState(UserThreadStateFocusWindow);
              g_uptr3 = pGetThreadState(UserThreadStateCursor));
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx extra=%llx focus=%p getfocus=%p cursor=%p getcursor=%p",
                 stub_hit, (unsigned long)stub_code, (unsigned long long)g_uptr,
                 (void *)g_uptr2, (void *)GetFocus(), (void *)g_uptr3, (void *)GetCursor());
        check("GetThreadState", !stub_hit && g_uptr == (ULONG_PTR)0x1BADB002 &&
              g_uptr2 == (ULONG_PTR)GetFocus() && g_uptr3 == (ULONG_PTR)GetCursor(), detail);
        SetMessageExtraInfo(old);
    }

    /* ---- NtUserGetCurrentInputMessageSource ---- */
    {
        memset(&g_imsrc, 0xAA, sizeof(g_imsrc)); g_imsrc_ok = FALSE; stub_hit = 0; stub_code = 0;
        GUARD(SendMessageA(hwnd, WM_APP + 7, 0, 0));
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx ok=%d device=%d origin=%d",
                 stub_hit, (unsigned long)stub_code, g_imsrc_ok,
                 (int)g_imsrc.deviceType, (int)g_imsrc.originId);
        check("GetCurrentInputMessageSource", !stub_hit && g_imsrc_ok &&
              g_imsrc.deviceType == IMDT_UNAVAILABLE && g_imsrc.originId == IMO_UNAVAILABLE, detail);
    }

    /* ---- NtUserGetClipCursor ---- */
    {
        RECT want = {3, 4, 333, 244};
        ClipCursor(&want);
        g_bool = FALSE; memset(&g_rect, 0, sizeof(g_rect)); stub_hit = 0; stub_code = 0;
        GUARD(g_bool = pGetClipCursor(&g_rect));
        ClipCursor(NULL);
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx ok=%d rect=%ld,%ld,%ld,%ld",
                 stub_hit, (unsigned long)stub_code, g_bool,
                 g_rect.left, g_rect.top, g_rect.right, g_rect.bottom);
        check("GetClipCursor", !stub_hit && g_bool && !memcmp(&want, &g_rect, sizeof(RECT)), detail);
    }

    /* ---- NtUserGetPointerType ---- */
    {
        g_ptype = 0; g_bool = FALSE; g_bool2 = TRUE; g_dword = 0; stub_hit = 0; stub_code = 0;
        GUARD(g_bool = pGetPointerType(1, &g_ptype);
              SetLastError(0);
              g_bool2 = pGetPointerType(999, &g_ptype);
              g_dword = GetLastError());
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx ok1=%d type=%d ok2=%d err=%lu",
                 stub_hit, (unsigned long)stub_code, g_bool, (int)g_ptype, g_bool2,
                 (unsigned long)g_dword);
        check("GetPointerType", !stub_hit && g_bool && g_ptype == PT_MOUSE && !g_bool2 &&
              g_dword == ERROR_INVALID_PARAMETER, detail);
    }

    /* ---- NtUserAlterWindowStyle ---- */
    {
        DWORD before = GetWindowLongA(hwnd, GWL_STYLE);
        DWORD after_set = 0, after_clear = 0;
        g_ulong = 0; stub_hit = 0; stub_code = 0;
        GUARD(g_ulong = pAlterWindowStyle(hwnd, WS_VSCROLL, WS_VSCROLL));
        after_set = GetWindowLongA(hwnd, GWL_STYLE);
        GUARD(pAlterWindowStyle(hwnd, WS_VSCROLL, 0));
        after_clear = GetWindowLongA(hwnd, GWL_STYLE);
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx r=%lu before=%08lx set=%08lx clear=%08lx",
                 stub_hit, (unsigned long)stub_code, (unsigned long)g_ulong,
                 (unsigned long)before, (unsigned long)after_set, (unsigned long)after_clear);
        check("AlterWindowStyle", !stub_hit && !(before & WS_VSCROLL) &&
              (after_set & WS_VSCROLL) && !(after_clear & WS_VSCROLL), detail);
    }

    /* ---- NtGdi MiterLimit pair ---- */
    {
        HDC dc = CreateCompatibleDC(NULL);
        union { FLOAT f; DWORD d; } u75;
        u75.f = 7.5f;
        g_bool = FALSE; g_bool2 = FALSE; g_float = 0; g_float2 = 0; stub_hit = 0; stub_code = 0;
        GUARD(g_bool = pSetMiterLimit(dc, u75.d, &g_float2);
              g_bool2 = pGetMiterLimit(dc, &g_float));
        {
            FLOAT via_gdi = 0;
            BOOL okg2 = GetMiterLimit(dc, &via_gdi);
            snprintf(detail, sizeof(detail), "stub=%d code=%08lx set=%d get=%d f=%f gdi=%f okg2=%d",
                     stub_hit, (unsigned long)stub_code, g_bool, g_bool2, (double)g_float,
                     (double)via_gdi, okg2);
            check("MiterLimit(nt->gdi)", !stub_hit && g_bool && g_bool2 && g_float == 7.5f &&
                  okg2 && via_gdi == 7.5f, detail);
        }
        SetMiterLimit(dc, 3.25f, NULL);
        g_float = 0; g_bool2 = FALSE; stub_hit = 0; stub_code = 0;
        GUARD(g_bool2 = pGetMiterLimit(dc, &g_float));
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx get=%d f=%f",
                 stub_hit, (unsigned long)stub_code, g_bool2, (double)g_float);
        check("MiterLimit(gdi->nt)", !stub_hit && g_bool2 && g_float == 3.25f, detail);
        DeleteDC(dc);
    }

    /* ---- NtGdiIcmBrushInfo ---- */
    {
        BITMAPINFO bmi = {0};
        unsigned char pixels[4 * 4 * 4];
        void *bits_ptr = NULL;
        HBITMAP dib;
        HBRUSH brush;
        static unsigned char info_buf[sizeof(BITMAPINFO) + 256];
        static unsigned char bits_buf[sizeof(pixels)];
        int i;
        for (i = 0; i < (int)sizeof(pixels); i++) pixels[i] = (unsigned char)(i * 7 + 1);
        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 4;
        bmi.bmiHeader.biHeight = 4;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        bmi.bmiHeader.biSizeImage = sizeof(pixels);
        dib = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, &bits_ptr, NULL, 0);
        if (dib && bits_ptr) memcpy(bits_ptr, pixels, sizeof(pixels));
        brush = dib ? CreatePatternBrush(dib) : NULL;
        g_bool = FALSE; g_bits_size = 0; g_usage = 0xdead; g_unk = FALSE; stub_hit = 0; stub_code = 0;
        if (brush)
            GUARD(g_bool = pIcmBrushInfo(NULL, brush, (BITMAPINFO *)info_buf, bits_buf,
                                         &g_bits_size, &g_usage, &g_unk, 0));
        {
            BITMAPINFO *got = (BITMAPINFO *)info_buf;
            int bits_match = g_bool && !memcmp(bits_buf, pixels, sizeof(pixels));
            snprintf(detail, sizeof(detail), "stub=%d code=%08lx ok=%d size=%lu usage=%u w=%ld h=%ld bitsmatch=%d",
                     stub_hit, (unsigned long)stub_code, g_bool, g_bits_size, g_usage,
                     g_bool ? got->bmiHeader.biWidth : 0L, g_bool ? got->bmiHeader.biHeight : 0L,
                     bits_match);
            check("IcmBrushInfo", !stub_hit && g_bool && g_bits_size == sizeof(pixels) &&
                  g_usage == DIB_RGB_COLORS && bits_match &&
                  got->bmiHeader.biWidth == 4 && got->bmiHeader.biHeight == 4, detail);
        }
        if (brush) DeleteObject(brush);
        if (dib) DeleteObject(dib);
    }

    /* ---- NtUserQueryDisplayConfig ---- */
    {
        UINT32 np = 0, nm = 0;
        int found_source = 0;
        DISPLAYCONFIG_PATH_INFO *paths;
        DISPLAYCONFIG_MODE_INFO *modes;
        GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &np, &nm);
        paths = (DISPLAYCONFIG_PATH_INFO *)malloc(sizeof(DISPLAYCONFIG_PATH_INFO) * (np ? np : 1));
        modes = (DISPLAYCONFIG_MODE_INFO *)malloc(sizeof(DISPLAYCONFIG_MODE_INFO) * (nm ? nm : 1));
        g_np2 = np; g_nm2 = nm; g_long = (LONG)0xdeadbeef; stub_hit = 0; stub_code = 0;
        GUARD(g_long = pQueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &g_np2, paths, &g_nm2, modes, NULL));
        if (!stub_hit && g_long == 0)
        {
            int cx = GetSystemMetrics(SM_CXSCREEN);
            UINT32 i;
            for (i = 0; i < g_nm2; i++)
                if (modes[i].infoType == DISPLAYCONFIG_MODE_INFO_TYPE_SOURCE &&
                    (int)modes[i].sourceMode.width == cx)
                    found_source = 1;
        }
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx ret=%ld paths=%u modes=%u srcmatch=%d bufsizes=%u/%u",
                 stub_hit, (unsigned long)stub_code, (long)g_long, g_np2, g_nm2, found_source, np, nm);
        check("QueryDisplayConfig", !stub_hit && g_long == 0 && g_np2 >= 1 && found_source, detail);
        free(paths);
        free(modes);
    }

    emit("win32u-fd168: RESULT %d\n", failures);
    return failures;
}
