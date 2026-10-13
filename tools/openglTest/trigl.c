/*
 * trigl.c — 64-bit OpenGL triangle test for Boxedwine64.
 *
 * Modeled on tools/dxvk/tri9/tri9.c but uses WGL + OpenGL instead of D3D9.
 * Creates a window, sets up a WGL context, clears and draws a triangle
 * with immediate-mode GL, presents via SwapBuffers.
 *
 * Build (mingw-w64):
 *   x86_64-w64-mingw32-gcc -O2 -o trigl.exe trigl.c -lopengl32 -lgdi32 -luser32
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>

#define TRIGL_FRAMES 70
#define TRIGL_W 480
#define TRIGL_H 360

static void dbg(const char* s) {
    fprintf(stderr, "%s", s); fflush(stderr);
    OutputDebugStringA(s);
}
#define FAIL(...) do { char b[512]; wsprintfA(b, __VA_ARGS__); dbg(b); \
    dbg("trigl: RESULT 1\n"); return 1; } while (0)

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE || m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE hInst, HINSTANCE hPrev, LPSTR cmd, int show) {
    (void)hPrev; (void)cmd; (void)show;
    dbg("trigl: WinMain start (WGL + OpenGL triangle)\n");

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.lpszClassName = "trigl";
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    if (!RegisterClassA(&wc)) FAIL("trigl: RegisterClass failed %lu\n", GetLastError());

    HWND hwnd = CreateWindowExA(0, "trigl", "trigl",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, TRIGL_W, TRIGL_H,
        NULL, NULL, hInst, NULL);
    if (!hwnd) FAIL("trigl: CreateWindow failed %lu\n", GetLastError());
    dbg("trigl: window created\n");

    HDC hdc = GetDC(hwnd);
    if (!hdc) FAIL("trigl: GetDC failed\n");

    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 16;
    pfd.iLayerType = PFD_MAIN_PLANE;

    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf) FAIL("trigl: ChoosePixelFormat failed\n");
    if (!SetPixelFormat(hdc, pf, &pfd)) FAIL("trigl: SetPixelFormat failed %lu\n", GetLastError());
    dbg("trigl: pixel format set\n");

    HGLRC hglrc = wglCreateContext(hdc);
    if (!hglrc) FAIL("trigl: wglCreateContext failed %lu\n", GetLastError());
    if (!wglMakeCurrent(hdc, hglrc)) FAIL("trigl: wglMakeCurrent failed %lu\n", GetLastError());
    dbg("trigl: GL context created and current\n");

    const char* ver = (const char*)glGetString(GL_VERSION);
    const char* ven = (const char*)glGetString(GL_VENDOR);
    const char* ren = (const char*)glGetString(GL_RENDERER);
    {
        char b[512];
        wsprintfA(b, "trigl: GL_VERSION=%s VENDOR=%s RENDERER=%s\n",
            ver ? ver : "(null)", ven ? ven : "(null)", ren ? ren : "(null)");
        dbg(b);
    }
    if (!ver) FAIL("trigl: glGetString(GL_VERSION) returned NULL\n");

    glViewport(0, 0, TRIGL_W, TRIGL_H);

    for (int f = 0; f < TRIGL_FRAMES; f++) {
        glClearColor(0.1f, 0.1f, 0.3f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);

        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        glOrtho(-1, 1, -1, 1, -1, 1);
        glMatrixMode(GL_MODELVIEW);
        glLoadIdentity();

        glBegin(GL_TRIANGLES);
        glColor3f(1.0f, 0.0f, 0.0f); glVertex2f( 0.0f,  0.8f);
        glColor3f(0.0f, 1.0f, 0.0f); glVertex2f(-0.8f, -0.8f);
        glColor3f(0.0f, 0.0f, 1.0f); glVertex2f( 0.8f, -0.8f);
        glEnd();

        GLenum err = glGetError();
        if (err != GL_NO_ERROR) FAIL("trigl: GL error 0x%x at frame %d\n", err, f);

        if (!SwapBuffers(hdc)) FAIL("trigl: SwapBuffers failed at frame %d\n", f);

        MSG msg;
        while (PeekMessageA(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto done;
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }
done:
    {
        char b[128];
        wsprintfA(b, "trigl: rendered %d frames\n", TRIGL_FRAMES);
        dbg(b);
    }
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(hglrc);
    dbg("trigl: RESULT 0\n");
    return 0;
}
