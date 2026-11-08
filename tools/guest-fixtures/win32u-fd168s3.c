/* FD-168 draw-down 3 proof fixture: call the four win32u exports
 * implemented by the wine8-win32u-fd168-slice3 backports patch and
 * verify each one answers with real, cross-checked behaviour.
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
    emit("win32u-fd168s3: CHECK %s ok=%d %s\n", name, ok, detail ? detail : "");
    if (!ok) failures++;
}

/* ---- local declarations for the win32u exports under test ---- */
/* POLYPATBLT layout (public SDK): four INTs then the brush handle. */
typedef struct { INT nXLeft, nYLeft, nWidth, nHeight; HBRUSH hBrush; } PPB;

typedef BOOL (WINAPI *fn_DrawCaption)(HWND, HDC, const RECT *, UINT);
typedef BOOL (WINAPI *fn_DrawCaptionTemp)(HWND, HDC, const RECT *, HFONT, HICON, LPCWSTR, UINT);
typedef INT  (WINAPI *fn_DrawEscape)(HDC, INT, INT, const char *);
typedef BOOL (WINAPI *fn_PolyPatBlt)(HDC, DWORD, const PPB *, DWORD, DWORD);
typedef HWND (WINAPI *fn_WindowFromPhysicalPoint)(POINT);

static fn_DrawCaption pDrawCaption;
static fn_DrawCaptionTemp pDrawCaptionTemp;
static fn_DrawEscape pDrawEscape;
static fn_PolyPatBlt pPolyPatBlt;
static fn_WindowFromPhysicalPoint pWindowFromPhysicalPoint;

/* scratch holders written from inside GUARD regions */
static BOOL g_b1, g_b2;
static INT g_i1, g_i2;
static HWND g_h1;

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

static int surf_nonwhite(const SURF *s)
{
    int i, n = 0;
    for (i = 0; i < s->w * s->h; i++) if (s->bits[i] != 0xFFFFFFFFu) n++;
    return n;
}

static int surf_equal(const SURF *a, const SURF *b)
{
    return a->w == b->w && a->h == b->h &&
           memcmp(a->bits, b->bits, (size_t)a->w * a->h * 4) == 0;
}

static int surf_diff(const SURF *a, const SURF *b)
{
    int i, n = 0;
    for (i = 0; i < a->w * a->h; i++) if (a->bits[i] != b->bits[i]) n++;
    return n;
}

int main(void)
{
    char detail[256];
    HWND hwnd, child, top2;
    HMODULE u;
    WNDCLASSA cls = {0};
    SURF a, b;
    RECT rc;
    HDC screen_dc;
    HBRUSH red, blue, green, yellow, magenta, white, pat;
    HBITMAP patbmp;
    SURF ps, pb;
    DWORD *save;
    int i, ok;

    AddVectoredExceptionHandler(1, guard_veh);

    u = LoadLibraryA("win32u.dll");
    if (!u) { emit("win32u-fd168s3: RESULT 99 no win32u\n"); return 99; }

    pDrawCaption = (fn_DrawCaption)GetProcAddress(u, "NtUserDrawCaption");
    pDrawCaptionTemp = (fn_DrawCaptionTemp)GetProcAddress(u, "NtUserDrawCaptionTemp");
    pDrawEscape = (fn_DrawEscape)GetProcAddress(u, "NtGdiDrawEscape");
    pPolyPatBlt = (fn_PolyPatBlt)GetProcAddress(u, "NtGdiPolyPatBlt");
    pWindowFromPhysicalPoint = (fn_WindowFromPhysicalPoint)GetProcAddress(u, "NtUserWindowFromPhysicalPoint");
    if (!pDrawCaption || !pDrawCaptionTemp || !pDrawEscape || !pPolyPatBlt || !pWindowFromPhysicalPoint)
    {
        emit("win32u-fd168s3: RESULT 98 missing exports\n");
        return 98;
    }
    if (sizeof(PPB) != 24)
    {
        emit("win32u-fd168s3: RESULT 97 bad PPB layout %d\n", (int)sizeof(PPB));
        return 97;
    }

    cls.lpfnWndProc = wndproc;
    cls.hInstance = GetModuleHandleA(NULL);
    cls.lpszClassName = "Slice3Class";
    cls.hIcon = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
    cls.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    if (!RegisterClassA(&cls)) { emit("win32u-fd168s3: RESULT 96 no class\n"); return 96; }

    hwnd = CreateWindowExA(0, "Slice3Class", "Slice3 Caption", WS_OVERLAPPEDWINDOW,
                           120, 120, 420, 320, NULL, NULL, cls.hInstance, NULL);
    if (!hwnd) { emit("win32u-fd168s3: RESULT 95 no window\n"); return 95; }
    ShowWindow(hwnd, SW_SHOW);
    child = CreateWindowExA(0, "BUTTON", "kid", WS_CHILD | WS_VISIBLE,
                            30, 40, 120, 60, hwnd, NULL, cls.hInstance, NULL);
    pump_messages();

    /* ============ NtUserDrawCaption vs user32 DrawCaption ============ */
    /* Return values: in this environment the tree's caption painter
     * (NtUserDrawCaptionTemp, and with it user32 DrawCaption) paints
     * correctly but returns 0 -- measured identically on the previous
     * shipped runtime, so the nonzero return the Windows documentation
     * promises is not this tree's behaviour today, and demanding it
     * would demand the new export DISAGREE with its reference. These
     * checks therefore assert agreement: returns equal, pixels
     * identical, caption actually painted. The return-value divergence
     * itself is filed on the defect register (FD-201 on this branch,
     * a lane-local number that renumbers when the batch composes). */
    rc.left = 0; rc.top = 0; rc.right = 300; rc.bottom = 40;
    {
        static const struct { const char *name; UINT flags; } cases[3] = {
            { "caption_active_pixels",   DC_TEXT | DC_ACTIVE | DC_ICON | DC_GRADIENT },
            { "caption_inactive_pixels", DC_TEXT | DC_ICON },
            { "caption_inbutton_pixels", DC_TEXT | DC_INBUTTON },
        };
        for (i = 0; i < 3; i++)
        {
            surf_create(&a, 300, 40);
            surf_create(&b, 300, 40);
            g_b1 = DrawCaption(hwnd, a.dc, &rc, cases[i].flags);
            stub_hit = 0;
            GUARD(g_b2 = pDrawCaption(hwnd, b.dc, &rc, cases[i].flags));
            ok = !stub_hit && g_b1 == g_b2 && surf_equal(&a, &b) && surf_nonwhite(&b) > 100;
            snprintf(detail, sizeof(detail), "ret_user=%d ret_nt=%d stub=%d nonwhite=%d nonwhite_a=%d diff=%d",
                     g_b1, g_b2, stub_hit, surf_nonwhite(&b), surf_nonwhite(&a), surf_diff(&a, &b));
            check(cases[i].name, ok, detail);
            surf_destroy(&a);
            surf_destroy(&b);
        }
    }
    /* sibling agreement: NtUserDrawCaptionTemp with the window's own
     * font/icon/text (NULL/0 arguments) draws the same caption */
    surf_create(&a, 300, 40);
    surf_create(&b, 300, 40);
    stub_hit = 0;
    GUARD(g_b1 = pDrawCaption(hwnd, a.dc, &rc, DC_TEXT | DC_ACTIVE));
    stub_hit = 0;
    {
        int stub2 = 0;
        GUARD(g_b2 = pDrawCaptionTemp(hwnd, b.dc, &rc, 0, 0, NULL, DC_TEXT | DC_ACTIVE));
        stub2 = stub_hit;
        ok = !stub2 && g_b1 == g_b2 && surf_equal(&a, &b);
        snprintf(detail, sizeof(detail), "ret_cap=%d ret_temp=%d stub=%d diff=%d", g_b1, g_b2, stub2, surf_diff(&a, &b));
        check("caption_temp_agree", ok, detail);
    }
    surf_destroy(&a);
    surf_destroy(&b);

    /* ============ NtGdiDrawEscape vs gdi32 ExtEscape ============ */
    screen_dc = GetDC(NULL);
    {
        INT code = 6789; /* X11DRV_ESCAPE: the display driver's private escape */
        INT ext = ExtEscape(screen_dc, QUERYESCSUPPORT, sizeof(code), (const char *)&code, 0, NULL);
        snprintf(detail, sizeof(detail), "extescape_6789=%d", ext);
        check("escape_support_found", ext != 0, detail);

        stub_hit = 0;
        GUARD(g_i1 = pDrawEscape(screen_dc, QUERYESCSUPPORT, sizeof(code), (const char *)&code));
        snprintf(detail, sizeof(detail), "draw=%d ext=%d stub=%d", g_i1, ext, stub_hit);
        check("escape_agree_supported", !stub_hit && g_i1 == ext && g_i1 != 0, detail);

        {
            static const INT subs[5] = { 6789, 1, 8, 0x10000, 0x7FFF };
            int all = 1;
            for (i = 0; i < 5; i++)
            {
                INT c = subs[i];
                INT e = ExtEscape(screen_dc, QUERYESCSUPPORT, sizeof(c), (const char *)&c, 0, NULL);
                stub_hit = 0;
                GUARD(g_i2 = pDrawEscape(screen_dc, QUERYESCSUPPORT, sizeof(c), (const char *)&c));
                if (stub_hit || g_i2 != e) all = 0;
            }
            check("escape_sweep_agree", all, "5 QUERYESCSUPPORT subcodes compared");
        }

        g_i1 = ExtEscape((HDC)(UINT_PTR)0x1234, QUERYESCSUPPORT, sizeof(code), (const char *)&code, 0, NULL);
        stub_hit = 0;
        GUARD(g_i2 = pDrawEscape((HDC)(UINT_PTR)0x1234, QUERYESCSUPPORT, sizeof(code), (const char *)&code));
        snprintf(detail, sizeof(detail), "ext=%d draw=%d stub=%d", g_i1, g_i2, stub_hit);
        check("escape_invalid_dc", !stub_hit && g_i1 == g_i2, detail);
    }
    ReleaseDC(NULL, screen_dc);

    /* ============ NtGdiPolyPatBlt ============ */
    red = CreateSolidBrush(RGB(255, 0, 0));
    blue = CreateSolidBrush(RGB(0, 0, 255));
    green = CreateSolidBrush(RGB(0, 200, 0));
    yellow = CreateSolidBrush(RGB(255, 255, 0));
    magenta = CreateSolidBrush(RGB(255, 0, 255));
    white = CreateSolidBrush(RGB(255, 255, 255));

    surf_create(&ps, 200, 120);
    surf_create(&pb, 200, 120);
    {
        PPB entries[2];
        HGDIOBJ prev;
        entries[0].nXLeft = 10;  entries[0].nYLeft = 10; entries[0].nWidth = 80; entries[0].nHeight = 50; entries[0].hBrush = red;
        entries[1].nXLeft = 100; entries[1].nYLeft = 20; entries[1].nWidth = 60; entries[1].nHeight = 80; entries[1].hBrush = blue;
        stub_hit = 0;
        GUARD(g_b1 = pPolyPatBlt(ps.dc, PATCOPY, entries, 2, 0));
        prev = SelectObject(pb.dc, red);
        PatBlt(pb.dc, 10, 10, 80, 50, PATCOPY);
        SelectObject(pb.dc, blue);
        PatBlt(pb.dc, 100, 20, 60, 80, PATCOPY);
        SelectObject(pb.dc, prev);
        ok = !stub_hit && g_b1 && surf_equal(&ps, &pb) && surf_nonwhite(&ps) > 5000;
        snprintf(detail, sizeof(detail), "ret=%d stub=%d nonwhite=%d", g_b1, stub_hit, surf_nonwhite(&ps));
        check("polypat_pixels", ok, detail);
    }
    /* the rop argument is honoured, not hardcoded: BLACKNESS with a
     * white brush must paint black, like a manual PatBlt */
    memset(ps.bits, 0xFF, (size_t)ps.w * ps.h * 4);
    memset(pb.bits, 0xFF, (size_t)pb.w * pb.h * 4);
    {
        PPB e;
        e.nXLeft = 20; e.nYLeft = 20; e.nWidth = 50; e.nHeight = 50; e.hBrush = white;
        stub_hit = 0;
        GUARD(g_b1 = pPolyPatBlt(ps.dc, BLACKNESS, &e, 1, 0));
        SelectObject(pb.dc, white);
        PatBlt(pb.dc, 20, 20, 50, 50, BLACKNESS);
        ok = !stub_hit && g_b1 && surf_equal(&ps, &pb) && ps.bits[25 * ps.w + 25] == 0;
        snprintf(detail, sizeof(detail), "ret=%d stub=%d center=%08lx", g_b1, stub_hit,
                 (unsigned long)ps.bits[25 * ps.w + 25]);
        check("polypat_rop", ok, detail);
    }
    /* an entry with an invalid brush is skipped; the rest still paint */
    memset(ps.bits, 0xFF, (size_t)ps.w * ps.h * 4);
    memset(pb.bits, 0xFF, (size_t)pb.w * pb.h * 4);
    {
        PPB e[3];
        HGDIOBJ prev;
        e[0].nXLeft = 0;   e[0].nYLeft = 0;   e[0].nWidth = 40; e[0].nHeight = 40; e[0].hBrush = green;
        e[1].nXLeft = 50;  e[1].nYLeft = 0;   e[1].nWidth = 40; e[1].nHeight = 40; e[1].hBrush = (HBRUSH)(UINT_PTR)0x777;
        e[2].nXLeft = 100; e[2].nYLeft = 0;   e[2].nWidth = 40; e[2].nHeight = 40; e[2].hBrush = yellow;
        stub_hit = 0;
        GUARD(g_b1 = pPolyPatBlt(ps.dc, PATCOPY, e, 3, 0));
        prev = SelectObject(pb.dc, green);
        PatBlt(pb.dc, 0, 0, 40, 40, PATCOPY);
        SelectObject(pb.dc, yellow);
        PatBlt(pb.dc, 100, 0, 40, 40, PATCOPY);
        SelectObject(pb.dc, prev);
        ok = !stub_hit && g_b1 && surf_equal(&ps, &pb);
        snprintf(detail, sizeof(detail), "ret=%d stub=%d", g_b1, stub_hit);
        check("polypat_badbrush_skip", ok, detail);
    }
    /* the DC's selected brush is unchanged by the call */
    {
        PPB e;
        HGDIOBJ before, after;
        e.nXLeft = 0; e.nYLeft = 60; e.nWidth = 30; e.nHeight = 30; e.hBrush = red;
        SelectObject(ps.dc, magenta);
        before = GetCurrentObject(ps.dc, OBJ_BRUSH);
        stub_hit = 0;
        GUARD(g_b1 = pPolyPatBlt(ps.dc, PATCOPY, &e, 1, 0));
        after = GetCurrentObject(ps.dc, OBJ_BRUSH);
        ok = !stub_hit && g_b1 && before == magenta && after == before;
        snprintf(detail, sizeof(detail), "ret=%d stub=%d before=%p after=%p", g_b1, stub_hit, before, after);
        check("polypat_brush_preserved", ok, detail);
    }
    /* count zero is a TRUE no-op */
    save = malloc((size_t)ps.w * ps.h * 4);
    memcpy(save, ps.bits, (size_t)ps.w * ps.h * 4);
    {
        PPB e;
        e.nXLeft = 0; e.nYLeft = 0; e.nWidth = 10; e.nHeight = 10; e.hBrush = red;
        stub_hit = 0;
        GUARD(g_b1 = pPolyPatBlt(ps.dc, PATCOPY, &e, 0, 0));
        ok = !stub_hit && g_b1 && memcmp(save, ps.bits, (size_t)ps.w * ps.h * 4) == 0;
        snprintf(detail, sizeof(detail), "ret=%d stub=%d", g_b1, stub_hit);
        check("polypat_count0", ok, detail);
    }
    free(save);
    /* a pattern brush entry paints its pattern */
    {
        SURF pat_s;
        HBITMAP tile;
        DWORD tilebits[64];
        PPB e;
        HGDIOBJ prev;
        for (i = 0; i < 64; i++) tilebits[i] = (i % 2) ? 0xFF000000u : 0xFFFFFFFFu;
        tile = CreateBitmap(8, 8, 1, 32, tilebits);
        pat = CreatePatternBrush(tile);
        surf_create(&pat_s, 200, 120);
        e.nXLeft = 150; e.nYLeft = 60; e.nWidth = 32; e.nHeight = 32; e.hBrush = pat;
        stub_hit = 0;
        GUARD(g_b1 = pPolyPatBlt(ps.dc, PATCOPY, &e, 1, 0));
        prev = SelectObject(pat_s.dc, pat);
        PatBlt(pat_s.dc, 150, 60, 32, 32, PATCOPY);
        SelectObject(pat_s.dc, prev);
        ok = !stub_hit && g_b1 && memcmp(&ps.bits[60 * ps.w + 150], &pat_s.bits[60 * pat_s.w + 150],
                                         32 * 4) == 0 && surf_nonwhite(&pat_s) > 100;
        snprintf(detail, sizeof(detail), "ret=%d stub=%d patnonwhite=%d", g_b1, stub_hit, surf_nonwhite(&pat_s));
        check("polypat_pattern", ok, detail);
        surf_destroy(&pat_s);
        DeleteObject(pat);
        DeleteObject(tile);
    }
    /* invalid DC fails cleanly */
    {
        PPB e;
        e.nXLeft = 0; e.nYLeft = 0; e.nWidth = 10; e.nHeight = 10; e.hBrush = red;
        stub_hit = 0;
        GUARD(g_b1 = pPolyPatBlt((HDC)(UINT_PTR)0x1234, PATCOPY, &e, 1, 0));
        snprintf(detail, sizeof(detail), "ret=%d stub=%d", g_b1, stub_hit);
        check("polypat_invalid_dc", !stub_hit && !g_b1, detail);
    }
    surf_destroy(&ps);
    surf_destroy(&pb);

    /* ============ NtUserWindowFromPhysicalPoint ============ */
    top2 = CreateWindowExA(0, "Slice3Class", "Slice3 Top2", WS_OVERLAPPEDWINDOW,
                           200, 200, 420, 320, NULL, NULL, cls.hInstance, NULL);
    ShowWindow(top2, SW_SHOW);
    BringWindowToTop(top2);
    pump_messages();
    {
        RECT pr, cr, tr;
        POINT pt;
        HWND via_point, via_user;
        GetWindowRect(child, &cr);
        GetWindowRect(hwnd, &pr);
        GetWindowRect(top2, &tr);

        pt.x = (cr.left + cr.right) / 2; pt.y = (cr.top + cr.bottom) / 2;
        stub_hit = 0;
        GUARD(g_h1 = pWindowFromPhysicalPoint(pt));
        via_point = WindowFromPoint(pt);
        via_user = WindowFromPhysicalPoint(pt);
        snprintf(detail, sizeof(detail), "nt=%p point=%p user=%p child=%p stub=%d",
                 g_h1, via_point, via_user, child, stub_hit);
        check("wfp_child", !stub_hit && g_h1 == child && via_point == child && via_user == child, detail);

        pt.x = pr.left + 5; pt.y = pr.top + 5;
        stub_hit = 0;
        GUARD(g_h1 = pWindowFromPhysicalPoint(pt));
        via_point = WindowFromPoint(pt);
        via_user = WindowFromPhysicalPoint(pt);
        snprintf(detail, sizeof(detail), "nt=%p point=%p user=%p parent=%p stub=%d",
                 g_h1, via_point, via_user, hwnd, stub_hit);
        check("wfp_parent", !stub_hit && g_h1 == via_point && g_h1 == via_user, detail);

        pt.x = 2; pt.y = 2;
        stub_hit = 0;
        GUARD(g_h1 = pWindowFromPhysicalPoint(pt));
        via_point = WindowFromPoint(pt);
        via_user = WindowFromPhysicalPoint(pt);
        snprintf(detail, sizeof(detail), "nt=%p point=%p user=%p stub=%d", g_h1, via_point, via_user, stub_hit);
        check("wfp_outside", !stub_hit && g_h1 == via_point && g_h1 == via_user, detail);

        pt.x = (tr.left + 250); pt.y = (tr.top + 150);
        stub_hit = 0;
        GUARD(g_h1 = pWindowFromPhysicalPoint(pt));
        via_point = WindowFromPoint(pt);
        via_user = WindowFromPhysicalPoint(pt);
        snprintf(detail, sizeof(detail), "nt=%p point=%p user=%p top2=%p stub=%d",
                 g_h1, via_point, via_user, top2, stub_hit);
        check("wfp_overlap", !stub_hit && g_h1 == via_point && g_h1 == via_user && g_h1 == top2, detail);
    }

    DeleteObject(red); DeleteObject(blue); DeleteObject(green);
    DeleteObject(yellow); DeleteObject(magenta); DeleteObject(white);
    DestroyWindow(child);
    DestroyWindow(top2);
    DestroyWindow(hwnd);

    emit("win32u-fd168s3: RESULT %d\n", failures);
    return failures;
}
