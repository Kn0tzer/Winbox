/* FD-168 draw-down 4 proof fixture: call the three win32u exports
 * implemented by the wine8-win32u-fd168-slice4 backports patch and
 * verify each one answers with real, cross-checked behaviour.
 * Every win32u call runs under a vectored exception handler guard: on
 * a build where the export is still a spec stub, the call raises the
 * Wine stub exception, the guard records the check as failed, and the
 * run continues so the log shows the full per-function picture on
 * both builds.
 *
 * Cross-checks (agreement with the user32/gdi32 counterpart is what
 * is asserted throughout, never an absolute value the tree is free
 * to choose differently):
 *  - NtUserGetTopLevelWindow against NtUserGetAncestor(GA_ROOT), the
 *    walk it forwards to, on a top-level window, a child, a
 *    grandchild, a second top-level window, the desktop, and NULL.
 *  - NtGdiGetTextExtent against gdi32 GetTextExtentPoint32W
 *    (flags 0) and GetTextExtentPointI (flags 1, glyph indices), plus
 *    the count 0 / count -1 / invalid-DC edges.
 *  - NtGdiGetGlyphIndicesWInternal against gdi32 GetGlyphIndicesW in
 *    both modes and both subset values, plus the count < 0 /
 *    count == 0 / all-NULL-probe / invalid-DC edges. The subset flag
 *    is ignored by the font engine (ReactOS carries it as an
 *    unimplemented FIXME), so subset TRUE must agree with FALSE.
 */
#define _WIN32_WINNT 0x0602
#define WINVER 0x0602
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <wchar.h>
#include <setjmp.h>

#ifndef GGI_MARK_NONEXISTING_GLYPHS
#define GGI_MARK_NONEXISTING_GLYPHS 0x00000001
#endif

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
    emit("win32u-fd168s4: CHECK %s ok=%d %s\n", name, ok, detail ? detail : "");
    if (!ok) failures++;
}

/* ---- local declarations for the win32u exports under test ---- */
typedef HWND  (WINAPI *fn_GetTopLevelWindow)(HWND);
typedef BOOL  (WINAPI *fn_GetTextExtent)(HDC, LPCWSTR, INT, SIZE *, UINT);
typedef DWORD (WINAPI *fn_GetGlyphIndicesWInternal)(HDC, LPCWSTR, INT, WORD *, DWORD, BOOL);

static fn_GetTopLevelWindow pGetTopLevelWindow;
static fn_GetTextExtent pGetTextExtent;
static fn_GetGlyphIndicesWInternal pGetGlyphIndicesWInternal;

/* scratch holders written from inside GUARD regions */
static BOOL g_b1, g_b2;
static HWND g_h1, g_h2;
static DWORD g_d1, g_d2;
static SIZE g_s1, g_s2;

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

/* a memory DC over a fresh white 32bpp DIB section */
typedef struct { HDC dc; HBITMAP bmp; DWORD *bits; int w, h; } SURF;

static int surf_create(SURF *s, int w, int h)
{
    BITMAPINFO bi;
    HDC screen;
    memset(&bi, 0, sizeof(bi));
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = w;
    bi.bmiHeader.biHeight = -h;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    screen = GetDC(NULL);
    s->dc = CreateCompatibleDC(screen);
    ReleaseDC(NULL, screen);
    if (!s->dc) return 0;
    s->bmp = CreateDIBSection(s->dc, &bi, DIB_RGB_COLORS, (void **)&s->bits, NULL, 0);
    if (!s->bmp) return 0;
    SelectObject(s->dc, s->bmp);
    s->w = w; s->h = h;
    memset(s->bits, 0xFF, (size_t)w * h * 4);
    return 1;
}

static void surf_destroy(SURF *s)
{
    if (s->bmp) DeleteObject(s->bmp);
    if (s->dc) DeleteDC(s->dc);
    s->bmp = NULL; s->dc = NULL; s->bits = NULL;
}

int main(void)
{
    char detail[256];
    HWND hwnd, child, grandchild, top2, desk;
    HMODULE u;
    WNDCLASSA cls = {0};
    SURF s;
    HFONT font, oldfont;
    int i, ok;

    AddVectoredExceptionHandler(1, guard_veh);

    u = LoadLibraryA("win32u.dll");
    if (!u) { emit("win32u-fd168s4: RESULT 99 no win32u\n"); return 99; }

    pGetTopLevelWindow = (fn_GetTopLevelWindow)GetProcAddress(u, "NtUserGetTopLevelWindow");
    pGetTextExtent = (fn_GetTextExtent)GetProcAddress(u, "NtGdiGetTextExtent");
    pGetGlyphIndicesWInternal = (fn_GetGlyphIndicesWInternal)GetProcAddress(u, "NtGdiGetGlyphIndicesWInternal");
    if (!pGetTopLevelWindow || !pGetTextExtent || !pGetGlyphIndicesWInternal)
    {
        emit("win32u-fd168s4: RESULT 98 missing exports\n");
        return 98;
    }

    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "Slice4Class";
    cls.hIcon = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassA(&cls)) { emit("win32u-fd168s4: RESULT 96 no class\n"); return 96; }

    hwnd = CreateWindowExA(0, "Slice4Class", "Slice4 Top", WS_OVERLAPPEDWINDOW,
                           120, 120, 420, 320, NULL, NULL, cls.hInstance, NULL);
    if (!hwnd) { emit("win32u-fd168s4: RESULT 95 no window\n"); return 95; }
    ShowWindow(hwnd, SW_SHOW);
    child = CreateWindowExA(0, "BUTTON", "kid", WS_CHILD | WS_VISIBLE,
                            30, 40, 120, 60, hwnd, NULL, cls.hInstance, NULL);
    if (!child) { emit("win32u-fd168s4: RESULT 94 no child\n"); return 94; }
    grandchild = CreateWindowExA(0, "STATIC", "grandkid", WS_CHILD | WS_VISIBLE,
                                 10, 10, 60, 30, child, NULL, cls.hInstance, NULL);
    if (!grandchild) emit("win32u-fd168s4: NOTE grandchild not created, skipping its check\n");
    top2 = CreateWindowExA(0, "Slice4Class", "Slice4 Top2", WS_OVERLAPPEDWINDOW,
                           200, 200, 420, 320, NULL, NULL, cls.hInstance, NULL);
    if (!top2) { emit("win32u-fd168s4: RESULT 93 no second top-level\n"); return 93; }
    ShowWindow(top2, SW_SHOW);
    pump_messages();

    /* ============ NtUserGetTopLevelWindow vs GetAncestor(GA_ROOT) ============ */
    stub_hit = 0;
    GUARD(g_h1 = pGetTopLevelWindow(hwnd));
    g_h2 = GetAncestor(hwnd, GA_ROOT);
    snprintf(detail, sizeof(detail), "nt=%p ancestor=%p stub=%d", g_h1, g_h2, stub_hit);
    check("toplevel_self", !stub_hit && g_h1 == hwnd && g_h1 == g_h2, detail);

    stub_hit = 0;
    GUARD(g_h1 = pGetTopLevelWindow(child));
    g_h2 = GetAncestor(child, GA_ROOT);
    snprintf(detail, sizeof(detail), "nt=%p ancestor=%p parent=%p stub=%d", g_h1, g_h2, hwnd, stub_hit);
    check("toplevel_child", !stub_hit && g_h1 == hwnd && g_h1 == g_h2, detail);

    if (grandchild)
    {
        stub_hit = 0;
        GUARD(g_h1 = pGetTopLevelWindow(grandchild));
        g_h2 = GetAncestor(grandchild, GA_ROOT);
        snprintf(detail, sizeof(detail), "nt=%p ancestor=%p parent=%p stub=%d", g_h1, g_h2, hwnd, stub_hit);
        check("toplevel_grandchild", !stub_hit && g_h1 == hwnd && g_h1 == g_h2, detail);
    }

    stub_hit = 0;
    GUARD(g_h1 = pGetTopLevelWindow(top2));
    g_h2 = GetAncestor(top2, GA_ROOT);
    snprintf(detail, sizeof(detail), "nt=%p ancestor=%p top2=%p stub=%d", g_h1, g_h2, top2, stub_hit);
    check("toplevel_second", !stub_hit && g_h1 == top2 && g_h1 == g_h2, detail);

    desk = GetDesktopWindow();
    stub_hit = 0;
    GUARD(g_h1 = pGetTopLevelWindow(desk));
    g_h2 = GetAncestor(desk, GA_ROOT);
    snprintf(detail, sizeof(detail), "nt=%p ancestor=%p desk=%p stub=%d", g_h1, g_h2, desk, stub_hit);
    check("toplevel_desktop", !stub_hit && g_h1 == g_h2, detail);

    stub_hit = 0;
    GUARD(g_h1 = pGetTopLevelWindow(NULL));
    g_h2 = GetAncestor(NULL, GA_ROOT);
    snprintf(detail, sizeof(detail), "nt=%p ancestor=%p stub=%d", g_h1, g_h2, stub_hit);
    check("toplevel_null", !stub_hit && g_h1 == g_h2, detail);

    /* ============ NtGdiGetTextExtent vs GetTextExtentPoint32W/I ============ */
    if (!surf_create(&s, 200, 60)) { emit("win32u-fd168s4: RESULT 92 no surface\n"); return 92; }
    font = CreateFontA(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, "Arial");
    oldfont = font ? SelectObject(s.dc, font) : NULL;
    {
        static const WCHAR *strings[4] = { L"Hello", L"Hello, World!", L"WWWW", L"iiii" };
        for (i = 0; i < 4; i++)
        {
            int len = (int)wcslen(strings[i]);
            g_b1 = GetTextExtentPoint32W(s.dc, strings[i], len, &g_s1);
            stub_hit = 0;
            GUARD(g_b2 = pGetTextExtent(s.dc, strings[i], len, &g_s2, 0));
            ok = !stub_hit && g_b1 == g_b2 && g_s1.cx == g_s2.cx && g_s1.cy == g_s2.cy &&
                 g_s1.cx > 0 && g_s1.cy > 0;
            snprintf(detail, sizeof(detail), "ret_ref=%d ret_nt=%d stub=%d size=%ldx%ld font=%d",
                     g_b1, g_b2, stub_hit, g_s1.cx, g_s1.cy, font != NULL);
            check("extent_agree", ok, detail);
            if (!ok) break;
        }
    }
    /* flags 1 is the glyph-index form: compare against GetTextExtentPointI
     * fed with the glyph indices GetGlyphIndicesW returns for the string */
    {
        static const WCHAR str[] = L"Hello, World!";
        WORD idx[64];
        int len = (int)wcslen(str);
        DWORD n = GetGlyphIndicesW(s.dc, str, len, idx, 0);
        g_b1 = (n != GDI_ERROR) && GetTextExtentPointI(s.dc, idx, len, &g_s1);
        stub_hit = 0;
        /* flags 1 takes the glyph-index array itself, not the text: the
         * tree's ExW treats the buffer as indices when flags is set, and
         * GetTextExtentExPointI passes its index array the same way. */
        GUARD(g_b2 = pGetTextExtent(s.dc, (LPCWSTR)idx, len, &g_s2, 1));
        ok = !stub_hit && g_b1 == g_b2 && g_s1.cx == g_s2.cx && g_s1.cy == g_s2.cy && g_s1.cx > 0;
        snprintf(detail, sizeof(detail), "ret_ref=%d ret_nt=%d stub=%d size_ref=%ldx%ld size_nt=%ldx%ld glyphs=%lu",
                 g_b1, g_b2, stub_hit, g_s1.cx, g_s1.cy, g_s2.cx, g_s2.cy, n);
        check("extent_glyphindex_agree", ok, detail);
    }
    /* count 0: agreement only -- the tree's own answer is the reference */
    {
        static const WCHAR str[] = L"Hello";
        g_b1 = GetTextExtentPoint32W(s.dc, str, 0, &g_s1);
        stub_hit = 0;
        GUARD(g_b2 = pGetTextExtent(s.dc, str, 0, &g_s2, 0));
        snprintf(detail, sizeof(detail), "ret_ref=%d ret_nt=%d stub=%d size_ref=%ldx%ld size_nt=%ldx%ld",
                 g_b1, g_b2, stub_hit, g_s1.cx, g_s1.cy, g_s2.cx, g_s2.cy);
        check("extent_count0", !stub_hit && g_b1 == g_b2 && g_s1.cx == g_s2.cx && g_s1.cy == g_s2.cy, detail);
    }
    /* count -1: both must fail */
    {
        static const WCHAR str[] = L"Hello";
        g_b1 = GetTextExtentPoint32W(s.dc, str, -1, &g_s1);
        stub_hit = 0;
        GUARD(g_b2 = pGetTextExtent(s.dc, str, -1, &g_s2, 0));
        snprintf(detail, sizeof(detail), "ret_ref=%d ret_nt=%d stub=%d", g_b1, g_b2, stub_hit);
        check("extent_count_neg", !stub_hit && !g_b1 && !g_b2, detail);
    }
    /* invalid DC: both must fail */
    {
        static const WCHAR str[] = L"Hello";
        g_b1 = GetTextExtentPoint32W((HDC)(UINT_PTR)0x1234, str, 5, &g_s1);
        stub_hit = 0;
        GUARD(g_b2 = pGetTextExtent((HDC)(UINT_PTR)0x1234, str, 5, &g_s2, 0));
        snprintf(detail, sizeof(detail), "ret_ref=%d ret_nt=%d stub=%d", g_b1, g_b2, stub_hit);
        check("extent_invalid_dc", !stub_hit && !g_b1 && !g_b2, detail);
    }
    if (oldfont) SelectObject(s.dc, oldfont);
    if (font) DeleteObject(font);
    surf_destroy(&s);

    /* ============ NtGdiGetGlyphIndicesWInternal vs GetGlyphIndicesW ============ */
    if (!surf_create(&s, 200, 60)) { emit("win32u-fd168s4: RESULT 91 no surface\n"); return 91; }
    font = CreateFontA(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, ANSI_CHARSET,
                       OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
                       DEFAULT_PITCH | FF_DONTCARE, "Arial");
    oldfont = font ? SelectObject(s.dc, font) : NULL;
    {
        static const WCHAR str[] = L"Hello, World! Test123";
        WORD a[64], b[64];
        int len = (int)wcslen(str);
        DWORD n;
        /* mode 0, subset FALSE: exact agreement, non-vacuous indices */
        n = GetGlyphIndicesW(s.dc, str, len, a, 0);
        stub_hit = 0;
        GUARD(g_d1 = pGetGlyphIndicesWInternal(s.dc, str, len, b, 0, FALSE));
        ok = !stub_hit && n != GDI_ERROR && g_d1 == n && memcmp(a, b, (size_t)len * sizeof(WORD)) == 0 &&
             a[0] != a[1] && a[0] != 0;
        snprintf(detail, sizeof(detail), "ret_ref=%lu ret_nt=%lu stub=%d idx0=%u idx1=%u",
                 n, g_d1, stub_hit, a[0], a[1]);
        check("glyph_mode0_agree", ok, detail);
        /* mode 1 (mark nonexisting glyphs) with a private-use char mixed in */
        {
            static const WCHAR str2[] = L"AB\xe000""C";
            WORD c[8], d[8];
            int len2 = (int)wcslen(str2);
            DWORD n2 = GetGlyphIndicesW(s.dc, str2, len2, c, GGI_MARK_NONEXISTING_GLYPHS);
            stub_hit = 0;
            GUARD(g_d1 = pGetGlyphIndicesWInternal(s.dc, str2, len2, d, GGI_MARK_NONEXISTING_GLYPHS, FALSE));
            ok = !stub_hit && n2 != GDI_ERROR && g_d1 == n2 && memcmp(c, d, (size_t)len2 * sizeof(WORD)) == 0;
            snprintf(detail, sizeof(detail), "ret_ref=%lu ret_nt=%lu stub=%d pua_ref=%u pua_nt=%u",
                     n2, g_d1, stub_hit, c[2], d[2]);
            check("glyph_mode1_agree", ok, detail);
        }
        /* subset TRUE must agree with subset FALSE (flag has no effect) */
        stub_hit = 0;
        GUARD(g_d1 = pGetGlyphIndicesWInternal(s.dc, str, len, b, 0, TRUE));
        ok = !stub_hit && g_d1 == n && memcmp(a, b, (size_t)len * sizeof(WORD)) == 0;
        snprintf(detail, sizeof(detail), "ret_subset=%lu ret_ref=%lu stub=%d", g_d1, n, stub_hit);
        check("glyph_subset_agree", ok, detail);
    }
    /* count < 0: GDI_ERROR */
    {
        static const WCHAR str[] = L"Hello";
        WORD buf[8];
        stub_hit = 0;
        GUARD(g_d1 = pGetGlyphIndicesWInternal(s.dc, str, -1, buf, 0, FALSE));
        snprintf(detail, sizeof(detail), "ret=%lu stub=%d", g_d1, stub_hit);
        check("glyph_count_neg", !stub_hit && g_d1 == GDI_ERROR, detail);
    }
    /* count == 0 with valid pointers: GDI_ERROR */
    {
        static const WCHAR str[] = L"Hello";
        WORD buf[8];
        stub_hit = 0;
        GUARD(g_d1 = pGetGlyphIndicesWInternal(s.dc, str, 0, buf, 0, FALSE));
        snprintf(detail, sizeof(detail), "ret=%lu stub=%d", g_d1, stub_hit);
        check("glyph_count0", !stub_hit && g_d1 == GDI_ERROR, detail);
    }
    /* all-NULL probe: must agree with the sibling called the same way */
    {
        stub_hit = 0;
        GUARD(g_d1 = pGetGlyphIndicesWInternal(s.dc, NULL, 0, NULL, 0, FALSE));
        {
            int stub2 = stub_hit;
            stub_hit = 0;
            GUARD(g_d2 = GetGlyphIndicesW(s.dc, NULL, 0, NULL, 0));
            snprintf(detail, sizeof(detail), "ret_nt=%lu ret_ref=%lu stub=%d stub2=%d",
                     g_d1, g_d2, stub_hit, stub2);
            check("glyph_null_probe", !stub_hit && !stub2 && g_d1 == g_d2, detail);
        }
    }
    /* invalid DC: GDI_ERROR, agreeing with the sibling */
    {
        static const WCHAR str[] = L"Hello";
        WORD buf[8];
        g_d2 = GetGlyphIndicesW((HDC)(UINT_PTR)0x1234, str, 5, buf, 0);
        stub_hit = 0;
        GUARD(g_d1 = pGetGlyphIndicesWInternal((HDC)(UINT_PTR)0x1234, str, 5, buf, 0, FALSE));
        snprintf(detail, sizeof(detail), "ret_nt=%lu ret_ref=%lu stub=%d", g_d1, g_d2, stub_hit);
        check("glyph_invalid_dc", !stub_hit && g_d1 == GDI_ERROR && g_d1 == g_d2, detail);
    }
    if (oldfont) SelectObject(s.dc, oldfont);
    if (font) DeleteObject(font);
    surf_destroy(&s);

    if (grandchild) DestroyWindow(grandchild);
    DestroyWindow(child);
    DestroyWindow(top2);
    DestroyWindow(hwnd);

    emit("win32u-fd168s4: RESULT %d\n", failures);
    return failures;
}
