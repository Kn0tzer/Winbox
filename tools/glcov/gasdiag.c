/* gasdiag: minimal repro for the glGetAttachedShaders page-worker crash.
 * Creates a program, attaches+links 2 shaders, then calls glGetAttachedShaders.
 * Also exercises the invalid-program and empty-program edge cases.
 * Prints GASDIAG lines to stderr; exits 0 on success (no crash). */
#include <windows.h>
#include <stdio.h>
#include <GL/gl.h>

typedef char GLchar;
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_INVALID_VALUE 0x0501

typedef GLuint (APIENTRY *PFN_glCreateShader)(GLenum);
typedef void (APIENTRY *PFN_glShaderSource)(GLuint, GLsizei, const GLchar *const *, const GLint *);
typedef void (APIENTRY *PFN_glCompileShader)(GLuint);
typedef void (APIENTRY *PFN_glGetShaderiv)(GLuint, GLenum, GLint *);
typedef GLuint (APIENTRY *PFN_glCreateProgram)(void);
typedef void (APIENTRY *PFN_glAttachShader)(GLuint, GLuint);
typedef void (APIENTRY *PFN_glLinkProgram)(GLuint);
typedef void (APIENTRY *PFN_glGetProgramiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PFN_glGetAttachedShaders)(GLuint, GLsizei, GLsizei *, GLuint *);
typedef GLenum (APIENTRY *PFN_glGetError)(void);

#define LOAD(n) p##n = (PFN_##n)(void*)GetProcAddress(hgl, #n)

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcA(h, m, w, l);
}

int main(void) {
    WNDCLASSA wc = {0};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "gasdiag";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("gasdiag", "gasdiag", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 320, 240,
                              0, 0, wc.hInstance, 0);
    if (!hwnd) { fprintf(stderr, "GASDIAG: RESULT 1 (no window)\n"); return 1; }
    HDC hdc = GetDC(hwnd);
    PIXELFORMATDESCRIPTOR pfd = {0};
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 24;
    pfd.cDepthBits = 24;
    pfd.iLayerType = PFD_MAIN_PLANE;
    int pf = ChoosePixelFormat(hdc, &pfd);
    if (!pf || !SetPixelFormat(hdc, pf, &pfd)) {
        fprintf(stderr, "GASDIAG: RESULT 1 (no pixelformat)\n"); return 1;
    }
    HGLRC rc = wglCreateContext(hdc);
    if (!rc || !wglMakeCurrent(hdc, rc)) {
        fprintf(stderr, "GASDIAG: RESULT 1 (no context)\n"); return 1;
    }
    HMODULE hgl = GetModuleHandleA("opengl32.dll");
    PFN_glCreateShader pglCreateShader = 0;
    PFN_glShaderSource pglShaderSource = 0;
    PFN_glCompileShader pglCompileShader = 0;
    PFN_glGetShaderiv pglGetShaderiv = 0;
    PFN_glCreateProgram pglCreateProgram = 0;
    PFN_glAttachShader pglAttachShader = 0;
    PFN_glLinkProgram pglLinkProgram = 0;
    PFN_glGetProgramiv pglGetProgramiv = 0;
    PFN_glGetAttachedShaders pglGetAttachedShaders = 0;
    PFN_glGetError pglGetError = 0;
    LOAD(glCreateShader); LOAD(glShaderSource); LOAD(glCompileShader);
    LOAD(glGetShaderiv); LOAD(glCreateProgram); LOAD(glAttachShader);
    LOAD(glLinkProgram); LOAD(glGetProgramiv); LOAD(glGetAttachedShaders);
    LOAD(glGetError);
    if (!pglCreateProgram || !pglGetAttachedShaders || !pglGetError) {
        fprintf(stderr, "GASDIAG: RESULT 1 (missing procs)\n"); return 1;
    }

    const GLchar *vs_src = "attribute vec4 pos; void main() { gl_Position = pos; }\n";
    const GLchar *fs_src = "precision mediump float; void main() { gl_FragColor = vec4(1.0); }\n";
    GLuint vs = pglCreateShader(GL_VERTEX_SHADER);
    pglShaderSource(vs, 1, &vs_src, NULL);
    pglCompileShader(vs);
    GLuint fs = pglCreateShader(GL_FRAGMENT_SHADER);
    pglShaderSource(fs, 1, &fs_src, NULL);
    pglCompileShader(fs);
    GLint ok = 0;
    if (pglGetShaderiv) {
        pglGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
        fprintf(stderr, "GASDIAG: vs=%u compile=%d\n", vs, ok);
        pglGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
        fprintf(stderr, "GASDIAG: fs=%u compile=%d\n", fs, ok);
    }
    GLuint prog = pglCreateProgram();
    pglAttachShader(prog, vs);
    pglAttachShader(prog, fs);
    pglLinkProgram(prog);
    if (pglGetProgramiv) {
        pglGetProgramiv(prog, GL_LINK_STATUS, &ok);
        fprintf(stderr, "GASDIAG: prog=%u link=%d\n", prog, ok);
    }
    fflush(stderr);

    /* The crashing call (glcov run1 died here). */
    GLsizei count = -1;
    GLuint shaders[8] = {0};
    pglGetAttachedShaders(prog, 8, &count, shaders);
    GLenum e1 = pglGetError();
    fprintf(stderr, "GASDIAG: attached prog=%u count=%d shaders=%u,%u err=%u\n",
            prog, count, shaders[0], shaders[1], e1);
    fflush(stderr);

    /* Edge: invalid program id must not crash (desktop: INVALID_VALUE). */
    count = -1;
    pglGetAttachedShaders(0x00DEADu, 8, &count, shaders);
    GLenum e2 = pglGetError();
    fprintf(stderr, "GASDIAG: invalid-prog count=%d err=%u (want 1280)\n", count, e2);
    fflush(stderr);

    /* Edge: valid program, no shaders attached. */
    GLuint empty = pglCreateProgram();
    count = -1;
    pglGetAttachedShaders(empty, 8, &count, shaders);
    GLenum e3 = pglGetError();
    fprintf(stderr, "GASDIAG: empty-prog=%u count=%d err=%u\n", empty, count, e3);
    fflush(stderr);

    fprintf(stderr, "GASDIAG: RESULT 0\n");
    return 0;
}
