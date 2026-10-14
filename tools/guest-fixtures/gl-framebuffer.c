// WGL contract: viewport changes, offscreen rendering and split framebuffer blits.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <GL/gl.h>
#include <stdio.h>

#define FRAMEBUFFER 0x8D40
#define READ_FRAMEBUFFER 0x8CA8
#define DRAW_FRAMEBUFFER 0x8CA9
#define COLOR_ATTACHMENT0 0x8CE0
#define FRAMEBUFFER_COMPLETE 0x8CD5
#define READ_FRAMEBUFFER_BINDING 0x8CAA
#define DRAW_FRAMEBUFFER_BINDING 0x8CA6
typedef void (APIENTRY *GenFramebuffers)(GLsizei, GLuint*);
typedef void (APIENTRY *BindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *FramebufferTexture2D)(GLenum, GLenum, GLenum, GLuint, GLint);
typedef GLenum (APIENTRY *CheckFramebufferStatus)(GLenum);
typedef void (APIENTRY *BlitFramebuffer)(GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLint, GLbitfield, GLenum);
static void emit(const char* text) { fprintf(stderr, "%s\n", text); fflush(stderr); OutputDebugStringA(text); }
#define REQUIRE(condition, text) do { if (!(condition)) { emit("gl-framebuffer: FAIL " text); emit("gl-framebuffer: RESULT 1"); return 1; } } while (0)
static LRESULT CALLBACK windowProc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h,m,w,l); }

int WINAPI WinMain(HINSTANCE instance, HINSTANCE previous, LPSTR args, int show) {
    WNDCLASSA cls = {0}; cls.lpfnWndProc = windowProc; cls.hInstance = instance; cls.lpszClassName = "gl-framebuffer";
    REQUIRE(RegisterClassA(&cls), "register window");
    HWND window = CreateWindowA(cls.lpszClassName, cls.lpszClassName, WS_POPUP | WS_VISIBLE,
        0,0,320,240,NULL,NULL,instance,NULL);
    REQUIRE(window, "create window");
    HDC dc = GetDC(window);
    PIXELFORMATDESCRIPTOR format = {0}; format.nSize = sizeof(format); format.nVersion = 1;
    format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    format.iPixelType = PFD_TYPE_RGBA; format.cColorBits = 32; format.cDepthBits = 16;
    int chosen = ChoosePixelFormat(dc, &format);
    REQUIRE(chosen && SetPixelFormat(dc, chosen, &format), "pixel format");
    HGLRC context = wglCreateContext(dc);
    REQUIRE(context && wglMakeCurrent(dc, context), "make current");
    GenFramebuffers gen = (GenFramebuffers)wglGetProcAddress("glGenFramebuffers");
    BindFramebuffer bind = (BindFramebuffer)wglGetProcAddress("glBindFramebuffer");
    FramebufferTexture2D attach = (FramebufferTexture2D)wglGetProcAddress("glFramebufferTexture2D");
    CheckFramebufferStatus status = (CheckFramebufferStatus)wglGetProcAddress("glCheckFramebufferStatus");
    BlitFramebuffer blit = (BlitFramebuffer)wglGetProcAddress("glBlitFramebuffer");
    REQUIRE(gen && bind && attach && status && blit, "framebuffer entry points");

    glViewport(0,0,320,240); glClearColor(0,1,0,1); glClear(GL_COLOR_BUFFER_BIT);
    GLuint texture, offscreen;
    glGenTextures(1,&texture); glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA,80,60,0,GL_RGBA,GL_UNSIGNED_BYTE,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    gen(1,&offscreen); bind(FRAMEBUFFER,offscreen);
    attach(FRAMEBUFFER,COLOR_ATTACHMENT0,GL_TEXTURE_2D,texture,0);
    REQUIRE(status(FRAMEBUFFER) == FRAMEBUFFER_COMPLETE, "offscreen completeness");
    glViewport(0,0,80,60); glClearColor(1,0,0,1); glClear(GL_COLOR_BUFFER_BIT);
    unsigned char red[4] = {0}; glReadPixels(40,30,1,1,GL_RGBA,GL_UNSIGNED_BYTE,red);
    REQUIRE(red[0] > 240 && red[1] < 10 && red[2] < 10, "viewport changed the offscreen render target");
    emit("gl-framebuffer: SOURCE_RED");

    bind(READ_FRAMEBUFFER,offscreen); bind(DRAW_FRAMEBUFFER,0);
    GLint readBound = -1, drawBound = -1;
    glGetIntegerv(READ_FRAMEBUFFER_BINDING,&readBound);
    glGetIntegerv(DRAW_FRAMEBUFFER_BINDING,&drawBound);
    REQUIRE((GLuint)readBound == offscreen && drawBound == 0, "split framebuffer guest names");
    glViewport(0,0,320,240);
    blit(0,0,80,60,40,40,120,100,GL_COLOR_BUFFER_BIT,GL_NEAREST);
    REQUIRE(glGetError() == GL_NO_ERROR, "split framebuffer blit");
    bind(READ_FRAMEBUFFER,0); glReadBuffer(GL_BACK);
    REQUIRE(glGetError() == GL_NO_ERROR, "default framebuffer back-buffer selection");
    REQUIRE(SwapBuffers(dc), "swap buffers");
    emit("gl-framebuffer: CAPTURE_READY");
    DWORD until = GetTickCount() + 10000;
    while ((LONG)(until - GetTickCount()) > 0) {
        MSG message;
        while (PeekMessageA(&message,NULL,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageA(&message); }
        Sleep(10);
    }
    emit("gl-framebuffer: RESULT 0");
    return 0;
}
