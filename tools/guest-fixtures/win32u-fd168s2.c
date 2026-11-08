/* FD-168 draw-down 2 proof fixture: call the five win32u syscall
 * exports implemented by the wine8-win32u-fd168-slice2 backports patch
 * and verify each one answers with real, cross-checked behaviour.
 * Every win32u call runs under a vectored exception handler guard: on
 * a build where the export is still a spec stub, the call raises the
 * Wine stub exception, the guard records the check as failed, and the
 * run continues so the log shows the full per-function picture on
 * both builds.
 */
#define _WIN32_WINNT 0x0602
#define WINVER 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winternl.h>
#include <wingdi.h>
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
    fputs(text, stdout);
    fflush(stdout);
}

static void check(const char *name, int ok, const char *detail)
{
    emit("win32u-fd168s2: CHECK %s ok=%d %s\n", name, ok, detail ? detail : "");
    if (!ok) failures++;
}

/* ---- local declarations for the win32u exports under test ---- */
typedef DWORD (WINAPI *fn_GetQueueStatus)(UINT);
typedef DWORD (WINAPI *fn_GetQueueStatusReadonly)(UINT);
typedef BOOL  (WINAPI *fn_IsTopLevelWindow)(HWND);
typedef BOOL  (WINAPI *fn_CombineTransform)(XFORM *, const XFORM *, const XFORM *);
typedef UINT  (WINAPI *fn_GetCharSet)(HDC);
typedef BOOL  (WINAPI *fn_PolyTextOutW)(HDC, const POLYTEXTW *, INT);

static fn_GetQueueStatus pGetQueueStatus;
static fn_GetQueueStatusReadonly pGetQueueStatusReadonly;
static fn_IsTopLevelWindow pIsTopLevelWindow;
static fn_CombineTransform pCombineTransform;
static fn_GetCharSet pGetCharSet;
static fn_PolyTextOutW pPolyTextOutW;

/* scratch holders written from inside GUARD regions */
static DWORD g_d1, g_d2, g_d3, g_d4, g_err;
static BOOL g_b1, g_b2, g_b3;
static UINT g_u1, g_u2;
static XFORM g_x1, g_x2, g_x3;

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void pump_messages(void)
{
    MSG msg;
    while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
}

int main(void)
{
    char detail[256];
    HWND hwnd, child, popup;
    HMODULE u;
    WNDCLASSA cls = {0};

    AddVectoredExceptionHandler(1, guard_veh);

    u = LoadLibraryA("win32u.dll");
    if (!u) { emit("win32u-fd168s2: RESULT 99 no win32u\n"); return 99; }

    pGetQueueStatus = (fn_GetQueueStatus)GetProcAddress(u, "NtUserGetQueueStatus");
    pGetQueueStatusReadonly = (fn_GetQueueStatusReadonly)GetProcAddress(u, "NtUserGetQueueStatusReadonly");
    pIsTopLevelWindow = (fn_IsTopLevelWindow)GetProcAddress(u, "NtUserIsTopLevelWindow");
    pCombineTransform = (fn_CombineTransform)GetProcAddress(u, "NtGdiCombineTransform");
    pGetCharSet = (fn_GetCharSet)GetProcAddress(u, "NtGdiGetCharSet");
    pPolyTextOutW = (fn_PolyTextOutW)GetProcAddress(u, "NtGdiPolyTextOutW");
    if (!pGetQueueStatus || !pGetQueueStatusReadonly || !pIsTopLevelWindow ||
        !pCombineTransform || !pGetCharSet || !pPolyTextOutW)
    {
        emit("win32u-fd168s2: RESULT 98 missing exports\n");
        return 98;
    }

    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "FD168S2Wnd";
    RegisterClassA(&cls);
    hwnd = CreateWindowExA(0, "FD168S2Wnd", "fd168s2", WS_OVERLAPPEDWINDOW,
                           10, 10, 320, 200, NULL, NULL, cls.hInstance, NULL);
    if (!hwnd) { emit("win32u-fd168s2: RESULT 97 no window\n"); return 97; }
    ShowWindow(hwnd, SW_SHOW);
    UpdateWindow(hwnd);
    pump_messages();

    /* ---- NtUserGetQueueStatusReadonly ---- */
    {
        /* consume any changed bits left over from window creation */
        pGetQueueStatus(QS_ALLINPUT);
        pump_messages();
        pGetQueueStatus(QS_ALLINPUT);

        g_d1 = 0; stub_hit = 0; stub_code = 0;
        GUARD(g_d1 = pGetQueueStatusReadonly(QS_ALLINPUT));
        PostMessageA(hwnd, WM_USER + 0x41, 0, 0);
        g_d2 = 0; g_d3 = 0; g_d4 = 0;
        GUARD(g_d2 = pGetQueueStatusReadonly(QS_ALLINPUT);
              g_d3 = pGetQueueStatusReadonly(QS_ALLINPUT));
        {
            DWORD clearing = pGetQueueStatus(QS_ALLINPUT);
            GUARD(g_d4 = pGetQueueStatusReadonly(QS_ALLINPUT));
            snprintf(detail, sizeof(detail),
                     "stub=%d code=%08lx idle_chg=%04lx post_chg=%04lx post_wake=%04lx again_chg=%04lx clearing_chg=%04lx after_chg=%04lx",
                     stub_hit, (unsigned long)stub_code,
                     (unsigned long)LOWORD(g_d1),
                     (unsigned long)(LOWORD(g_d2) & QS_POSTMESSAGE),
                     (unsigned long)(HIWORD(g_d2) & QS_POSTMESSAGE),
                     (unsigned long)(LOWORD(g_d3) & QS_POSTMESSAGE),
                     (unsigned long)(LOWORD(clearing) & QS_POSTMESSAGE),
                     (unsigned long)(LOWORD(g_d4) & QS_POSTMESSAGE));
            check("GetQueueStatusReadonly",
                  !stub_hit &&
                  !(LOWORD(g_d1) & QS_POSTMESSAGE) &&   /* idle: nothing posted yet */
                  (LOWORD(g_d2) & QS_POSTMESSAGE) &&    /* posted bit reported */
                  (HIWORD(g_d2) & QS_POSTMESSAGE) &&    /* ... in the wake word too */
                  (LOWORD(g_d3) & QS_POSTMESSAGE) &&    /* readonly did NOT clear it */
                  (LOWORD(clearing) & QS_POSTMESSAGE) &&/* the clearing call sees it */
                  !(LOWORD(g_d4) & QS_POSTMESSAGE),     /* ... and consumes it */
                  detail);
        }
        pump_messages();

        g_d1 = 0xdead; g_err = 0; stub_hit = 0; stub_code = 0;
        GUARD(SetLastError(0);
              g_d1 = pGetQueueStatusReadonly(0x20000000);
              g_err = GetLastError());
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx ret=%08lx err=%lu",
                 stub_hit, (unsigned long)stub_code, (unsigned long)g_d1,
                 (unsigned long)g_err);
        check("GetQueueStatusReadonly(invalid-flags)",
              !stub_hit && g_d1 == 0 && g_err == ERROR_INVALID_FLAGS, detail);
    }

    /* ---- NtUserIsTopLevelWindow ---- */
    {
        child = CreateWindowExA(0, "BUTTON", "b", WS_CHILD | WS_VISIBLE,
                                5, 5, 50, 20, hwnd, NULL, cls.hInstance, NULL);
        popup = CreateWindowExA(0, "FD168S2Wnd", "p", WS_POPUP | WS_VISIBLE,
                                30, 30, 80, 60, hwnd, NULL, cls.hInstance, NULL);
        g_b1 = FALSE; g_b2 = TRUE; g_b3 = FALSE; stub_hit = 0; stub_code = 0;
        GUARD(g_b1 = pIsTopLevelWindow(hwnd);
              g_b2 = pIsTopLevelWindow(child);
              g_b3 = pIsTopLevelWindow(popup));
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx top=%d child=%d popup=%d child_hwnd=%p",
                 stub_hit, (unsigned long)stub_code, g_b1, g_b2, g_b3, (void *)child);
        check("IsTopLevelWindow", !stub_hit && g_b1 && !g_b2 && g_b3, detail);

        g_b1 = TRUE; g_b2 = TRUE; stub_hit = 0; stub_code = 0;
        GUARD(g_b1 = pIsTopLevelWindow(NULL);
              g_b2 = pIsTopLevelWindow((HWND)(UINT_PTR)0xDEAD));
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx null=%d dead=%d",
                 stub_hit, (unsigned long)stub_code, g_b1, g_b2);
        check("IsTopLevelWindow(invalid)", !stub_hit && !g_b1 && !g_b2, detail);
        if (popup) DestroyWindow(popup);
        if (child) DestroyWindow(child);
    }

    /* ---- NtGdiCombineTransform ---- */
    {
        static const XFORM ident = {1, 0, 0, 1, 0, 0};
        static const XFORM m = {2, 0.5f, -1, 3, 7, -4};
        static const XFORM a = {2, 0, 0, 3, 5, 7};
        static const XFORM b = {0, -1, 1, 0, 10, 20};
        static const XFORM expect_ab = {0, -2, 3, 0, 17, 15};
        int ok = 1;

        memset(&g_x1, 0, sizeof(g_x1)); g_b1 = FALSE; stub_hit = 0; stub_code = 0;
        GUARD(g_b1 = pCombineTransform(&g_x1, &ident, &m));
        ok = ok && g_b1 && !memcmp(&g_x1, &m, sizeof(XFORM));
        memset(&g_x1, 0, sizeof(g_x1)); g_b1 = FALSE;
        GUARD(g_b1 = pCombineTransform(&g_x1, &m, &ident));
        ok = ok && g_b1 && !memcmp(&g_x1, &m, sizeof(XFORM));
        memset(&g_x1, 0, sizeof(g_x1)); g_b1 = FALSE;
        GUARD(g_b1 = pCombineTransform(&g_x1, &a, &b));
        ok = ok && g_b1 && !memcmp(&g_x1, &expect_ab, sizeof(XFORM));
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx exact=%d last={%g %g %g %g %g %g}",
                 stub_hit, (unsigned long)stub_code, ok,
                 (double)g_x1.eM11, (double)g_x1.eM12, (double)g_x1.eM21,
                 (double)g_x1.eM22, (double)g_x1.eDx, (double)g_x1.eDy);
        check("CombineTransform(exact)", !stub_hit && ok, detail);

        /* cross-check against gdi32 over a table, incl. aliasing */
        {
            static const XFORM pairs[][2] = {
                {{1.5f, 0.25f, -0.5f, 2, 3.5f, -1.25f}, {0.75f, -2, 1.25f, 0.5f, -4, 8}},
                {{-3, 1.5f, 2.25f, -0.75f, 0.5f, 0.5f}, {2, -1, 0.25f, 4, -2.5f, 1}},
                {{0.5f, 0.5f, 0.5f, 0.5f, 0, 0}, {1, -1, 1, -1, 2, -2}},
                {{1, 0, 0, 1, -3.5f, 2.75f}, {-0.125f, 4, 2.5f, -1.5f, 0.25f, -0.5f}},
            };
            unsigned i;
            int sweep_ok = 1, alias_ok = 1;
            for (i = 0; i < sizeof(pairs) / sizeof(pairs[0]); i++)
            {
                XFORM via_gdi;
                memset(&via_gdi, 0, sizeof(via_gdi));
                memset(&g_x1, 0, sizeof(g_x1));
                g_b1 = FALSE; g_b2 = FALSE;
                {
                    BOOL gok = CombineTransform(&via_gdi, &pairs[i][0], &pairs[i][1]);
                    if (!gok) sweep_ok = 0;
                }
                GUARD(g_b1 = pCombineTransform(&g_x1, &pairs[i][0], &pairs[i][1]));
                if (stub_hit || !g_b1 || memcmp(&g_x1, &via_gdi, sizeof(XFORM))) sweep_ok = 0;
            }
            /* result aliases xform1, then xform2, then all three */
            g_x2 = pairs[0][0]; g_x3 = pairs[0][1];
            {
                XFORM expect;
                CombineTransform(&expect, &pairs[0][0], &pairs[0][1]);
                g_b1 = FALSE;
                GUARD(g_b1 = pCombineTransform(&g_x2, &g_x2, &g_x3));
                if (stub_hit || !g_b1 || memcmp(&g_x2, &expect, sizeof(XFORM))) alias_ok = 0;
                g_x2 = pairs[0][0]; g_x3 = pairs[0][1];
                g_b1 = FALSE;
                GUARD(g_b1 = pCombineTransform(&g_x3, &g_x2, &g_x3));
                if (stub_hit || !g_b1 || memcmp(&g_x3, &expect, sizeof(XFORM))) alias_ok = 0;
                g_x2 = a; /* scale (2,3) composed with itself = scale (4,9), offset (5*2+5, 7*3+7) = (15,28) */
                g_b1 = FALSE;
                GUARD(g_b1 = pCombineTransform(&g_x2, &g_x2, &g_x2));
                {
                    static const XFORM expect_self = {4, 0, 0, 9, 15, 28};
                    if (stub_hit || !g_b1 || memcmp(&g_x2, &expect_self, sizeof(XFORM))) alias_ok = 0;
                }
            }
            snprintf(detail, sizeof(detail), "stub=%d code=%08lx sweep=%d alias=%d",
                     stub_hit, (unsigned long)stub_code, sweep_ok, alias_ok);
            check("CombineTransform(matches-gdi32)", !stub_hit && sweep_ok && alias_ok, detail);
        }

        g_b1 = TRUE; g_b2 = TRUE; g_b3 = TRUE; stub_hit = 0; stub_code = 0;
        GUARD(g_b1 = pCombineTransform(NULL, &m, &m);
              g_b2 = pCombineTransform(&g_x1, NULL, &m);
              g_b3 = pCombineTransform(&g_x1, &m, NULL));
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx r1=%d r2=%d r3=%d",
                 stub_hit, (unsigned long)stub_code, g_b1, g_b2, g_b3);
        check("CombineTransform(null)", !stub_hit && !g_b1 && !g_b2 && !g_b3, detail);
    }

    /* ---- NtGdiGetCharSet ---- */
    {
        HDC dc = CreateCompatibleDC(NULL);
        LOGFONTW lf;
        HFONT font;
        int agree = 1;

        g_u1 = 0xFFFF; g_u2 = 0xFFFE; stub_hit = 0; stub_code = 0;
        GUARD(g_u1 = pGetCharSet(dc));
        g_u2 = GetTextCharset(dc);
        if (stub_hit || g_u1 != g_u2) agree = 0;
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx nt=%u gdi=%u",
                 stub_hit, (unsigned long)stub_code, g_u1, g_u2);
        check("GetCharSet(default-font)", !stub_hit && agree, detail);

        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -16;
        lf.lfCharSet = ANSI_CHARSET;
        font = CreateFontIndirectW(&lf);
        if (font) SelectObject(dc, font);
        g_u1 = 0xFFFF; g_u2 = 0xFFFE; stub_hit = 0; stub_code = 0;
        GUARD(g_u1 = pGetCharSet(dc));
        g_u2 = GetTextCharset(dc);
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx nt=%u gdi=%u",
                 stub_hit, (unsigned long)stub_code, g_u1, g_u2);
        check("GetCharSet(ansi-font)", !stub_hit && g_u1 == g_u2, detail);
        if (font) { SelectObject(dc, GetStockObject(SYSTEM_FONT)); DeleteObject(font); }

        memset(&lf, 0, sizeof(lf));
        lf.lfHeight = -16;
        lf.lfCharSet = SYMBOL_CHARSET;
        font = CreateFontIndirectW(&lf);
        if (font) SelectObject(dc, font);
        g_u1 = 0xFFFF; g_u2 = 0xFFFE; stub_hit = 0; stub_code = 0;
        GUARD(g_u1 = pGetCharSet(dc));
        g_u2 = GetTextCharset(dc);
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx nt=%u gdi=%u",
                 stub_hit, (unsigned long)stub_code, g_u1, g_u2);
        check("GetCharSet(symbol-font)", !stub_hit && g_u1 == g_u2, detail);
        if (font) { SelectObject(dc, GetStockObject(SYSTEM_FONT)); DeleteObject(font); }

        g_u1 = 0xFFFF; stub_hit = 0; stub_code = 0;
        GUARD(g_u1 = pGetCharSet((HDC)(UINT_PTR)0xDEAD));
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx nt=%u want=%d",
                 stub_hit, (unsigned long)stub_code, g_u1, DEFAULT_CHARSET);
        check("GetCharSet(invalid-dc)", !stub_hit && g_u1 == DEFAULT_CHARSET, detail);
        DeleteDC(dc);
    }

    /* ---- NtGdiPolyTextOutW ---- */
    {
        BITMAPINFO bmi = {0};
        void *bits_a = NULL, *bits_b = NULL;
        HBITMAP bmp_a, bmp_b;
        HDC dca, dcb;
        int dx1[2] = {12, 16};
        POLYTEXTW entries[2];
        static unsigned char saved[96 * 48 * 4];
        int render_ok = 1;

        bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth = 96;
        bmi.bmiHeader.biHeight = -48;
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        dca = CreateCompatibleDC(NULL);
        dcb = CreateCompatibleDC(NULL);
        bmp_a = CreateDIBSection(dca, &bmi, DIB_RGB_COLORS, &bits_a, NULL, 0);
        bmp_b = CreateDIBSection(dcb, &bmi, DIB_RGB_COLORS, &bits_b, NULL, 0);
        if (!bmp_a || !bmp_b || !bits_a || !bits_b)
        {
            emit("win32u-fd168s2: RESULT 96 no dib\n");
            return 96;
        }
        SelectObject(dca, bmp_a);
        SelectObject(dcb, bmp_b);
        {
            RECT rc = {0, 0, 96, 48};
            FillRect(dca, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
            FillRect(dcb, &rc, (HBRUSH)GetStockObject(WHITE_BRUSH));
        }
        SetBkColor(dca, RGB(255, 0, 0));
        SetBkColor(dcb, RGB(255, 0, 0));
        SetTextColor(dca, RGB(0, 0, 0));
        SetTextColor(dcb, RGB(0, 0, 0));

        memset(entries, 0, sizeof(entries));
        entries[0].x = 2; entries[0].y = 2;
        entries[0].lpstr = L"AB"; entries[0].n = 2;
        entries[0].uiFlags = 0;
        entries[0].rcl.left = 2; entries[0].rcl.top = 2;
        entries[0].rcl.right = 40; entries[0].rcl.bottom = 16;
        entries[0].pdx = dx1;
        entries[1].x = 2; entries[1].y = 24;
        entries[1].lpstr = L"C"; entries[1].n = 1;
        entries[1].uiFlags = ETO_OPAQUE;
        entries[1].rcl.left = 2; entries[1].rcl.top = 24;
        entries[1].rcl.right = 30; entries[1].rcl.bottom = 40;
        entries[1].pdx = NULL;

        g_b1 = FALSE; g_b2 = FALSE; stub_hit = 0; stub_code = 0;
        g_b1 = PolyTextOutW(dca, entries, 2);
        GUARD(g_b2 = pPolyTextOutW(dcb, entries, 2));
        GdiFlush();
        if (!g_b1 || stub_hit || !g_b2) render_ok = 0;
        if (render_ok && memcmp(bits_a, bits_b, 96 * 48 * 4)) render_ok = 0;
        /* the comparison must not be vacuous: some pixel must be non-white */
        if (render_ok)
        {
            const unsigned char *px = (const unsigned char *)bits_a;
            long i, nonwhite = 0;
            for (i = 0; i < 96 * 48 * 4; i += 4)
                if (px[i] != 0xFF || px[i + 1] != 0xFF || px[i + 2] != 0xFF) nonwhite++;
            if (!nonwhite) render_ok = 0;
        }
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx gdi=%d nt=%d match_nonwhite=%d",
                 stub_hit, (unsigned long)stub_code, g_b1, g_b2, render_ok);
        check("PolyTextOutW(matches-gdi32)", !stub_hit && render_ok, detail);

        /* zero entries: success, and not a single pixel changes */
        memcpy(saved, bits_b, sizeof(saved));
        g_b1 = FALSE; stub_hit = 0; stub_code = 0;
        GUARD(g_b1 = pPolyTextOutW(dcb, entries, 0));
        GdiFlush();
        snprintf(detail, sizeof(detail), "stub=%d code=%08lx ret=%d unchanged=%d",
                 stub_hit, (unsigned long)stub_code, g_b1,
                 !memcmp(saved, bits_b, sizeof(saved)));
        check("PolyTextOutW(zero-count)", !stub_hit && g_b1 &&
              !memcmp(saved, bits_b, sizeof(saved)), detail);

        DeleteObject(bmp_a);
        DeleteObject(bmp_b);
        DeleteDC(dca);
        DeleteDC(dcb);
    }

    emit("win32u-fd168s2: RESULT %d\n", failures);
    return failures;
}
