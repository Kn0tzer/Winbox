/*
 * fd270-present.c - proof for FD-270 (default-framebuffer readback).
 *
 * Same draws as fd267-present, but the guest NEVER binds its read target
 * before reading back: no glBindFramebuffer(GL_READ_FRAMEBUFFER, 0) anywhere.
 * On the unfixed build the first readback returns black with no GL error
 * (READBACK-UNAVAILABLE, exit 2); on the fixed build every read returns the
 * drawn pixels and the run passes end to end.
 *
 * A Windows guest program that draws two triangles (left = vertices 0-2,
 * right = vertices 3-5) through each of the four base-vertex entry
 * points and checks, via glReadPixels, that the pixels landed where the
 * baseVertex says they should. Drawing indices {0,1,2} with baseVertex 3
 * must paint the RIGHT triangle; if baseVertex were ignored, the LEFT
 * triangle would paint instead.
 *
 * Each test clears to black, draws once, reads one pixel at each
 * triangle's centroid, and prints:
 *   FD270: <name> err=<glGetError> A=(r,g,b) B=(r,g,b) PASS|FAIL
 * A control draw with plain glDrawElements runs first; if it cannot see
 * pixels, the run reports READBACK-UNAVAILABLE (exit 2) instead of a
 * false failure.
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -o fd270-present.exe fd270-present.c \
 *          -lopengl32 -lgdi32 -luser32
 * Run in-browser with web/tests/fd270-present-probe.mjs (stages this exe
 * + libGL.so.1, same pattern as the glcov probe).
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

/* ---- manual GL types (no GL/gl.h), same style as tools/glcov/glcov.c ---- */
typedef unsigned int GLenum;
typedef unsigned char GLboolean;
typedef unsigned int GLbitfield;
typedef void GLvoid;
typedef signed char GLbyte;
typedef short GLshort;
typedef int GLint;
typedef unsigned char GLubyte;
typedef unsigned short GLushort;
typedef unsigned int GLuint;
typedef float GLfloat;
typedef double GLdouble;
typedef int GLsizei;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef char GLchar;
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif

#define GL_TRIANGLES 0x0004
#define GL_UNSIGNED_SHORT 0x1403
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_INT 0x1405
#define GL_FLOAT 0x1406
#define GL_RGBA 0x1908
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_STATIC_DRAW 0x88E4
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_NO_ERROR 0
#define GL_READ_FRAMEBUFFER 0x8CA8
#define GL_FALSE 0
#define GL_TRUE 1

typedef void (APIENTRY *PFN_glClearColor)(GLfloat, GLfloat, GLfloat, GLfloat);
typedef void (APIENTRY *PFN_glClear)(GLbitfield);
typedef void (APIENTRY *PFN_glViewport)(GLint, GLint, GLsizei, GLsizei);
typedef GLuint (APIENTRY *PFN_glCreateShader)(GLenum);
typedef void (APIENTRY *PFN_glShaderSource)(GLuint, GLsizei, const GLchar * const *, const GLint *);
typedef void (APIENTRY *PFN_glCompileShader)(GLuint);
typedef void (APIENTRY *PFN_glGetShaderiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PFN_glGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef GLuint (APIENTRY *PFN_glCreateProgram)(void);
typedef void (APIENTRY *PFN_glAttachShader)(GLuint, GLuint);
typedef void (APIENTRY *PFN_glBindAttribLocation)(GLuint, GLuint, const GLchar *);
typedef void (APIENTRY *PFN_glLinkProgram)(GLuint);
typedef void (APIENTRY *PFN_glGetProgramiv)(GLuint, GLenum, GLint *);
typedef void (APIENTRY *PFN_glGetProgramInfoLog)(GLuint, GLsizei, GLsizei *, GLchar *);
typedef void (APIENTRY *PFN_glUseProgram)(GLuint);
typedef void (APIENTRY *PFN_glGenBuffers)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glBindBuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glBufferData)(GLenum, GLsizeiptr, const void *, GLenum);
typedef void (APIENTRY *PFN_glGenVertexArrays)(GLsizei, GLuint *);
typedef void (APIENTRY *PFN_glBindVertexArray)(GLuint);
typedef void (APIENTRY *PFN_glVertexAttribPointer)(GLuint, GLint, GLenum, GLboolean, GLsizei, const void *);
typedef void (APIENTRY *PFN_glEnableVertexAttribArray)(GLuint);
typedef void (APIENTRY *PFN_glDrawElements)(GLenum, GLsizei, GLenum, const void *);
typedef void (APIENTRY *PFN_glReadPixels)(GLint, GLint, GLsizei, GLsizei, GLenum, GLenum, void *);
typedef GLenum (APIENTRY *PFN_glGetError)(void);
typedef const GLubyte * (APIENTRY *PFN_glGetString)(GLenum);
typedef void (APIENTRY *PFN_glBindFramebuffer)(GLenum, GLuint);
typedef void (APIENTRY *PFN_glDrawElementsBaseVertex)(GLenum, GLsizei, GLenum, const void *, GLint);
typedef void (APIENTRY *PFN_glDrawElementsInstancedBaseVertex)(GLenum, GLsizei, GLenum, const void *, GLsizei, GLint);
typedef void (APIENTRY *PFN_glDrawElementsInstancedBaseVertexBaseInstance)(GLenum, GLsizei, GLenum, const void *, GLsizei, GLint, GLuint);
typedef void (APIENTRY *PFN_glMultiDrawElementsBaseVertex)(GLenum, const GLsizei *, GLenum, const void * const *, GLsizei, const GLint *);

static HMODULE g_opengl;
static PFN_glClearColor pglClearColor;
static PFN_glClear pglClear;
static PFN_glViewport pglViewport;
static PFN_glCreateShader pglCreateShader;
static PFN_glShaderSource pglShaderSource;
static PFN_glCompileShader pglCompileShader;
static PFN_glGetShaderiv pglGetShaderiv;
static PFN_glGetShaderInfoLog pglGetShaderInfoLog;
static PFN_glCreateProgram pglCreateProgram;
static PFN_glAttachShader pglAttachShader;
static PFN_glBindAttribLocation pglBindAttribLocation;
static PFN_glLinkProgram pglLinkProgram;
static PFN_glGetProgramiv pglGetProgramiv;
static PFN_glGetProgramInfoLog pglGetProgramInfoLog;
static PFN_glUseProgram pglUseProgram;
static PFN_glGenBuffers pglGenBuffers;
static PFN_glBindBuffer pglBindBuffer;
static PFN_glBufferData pglBufferData;
static PFN_glGenVertexArrays pglGenVertexArrays;
static PFN_glBindVertexArray pglBindVertexArray;
static PFN_glVertexAttribPointer pglVertexAttribPointer;
static PFN_glEnableVertexAttribArray pglEnableVertexAttribArray;
static PFN_glDrawElements pglDrawElements;
static PFN_glBindFramebuffer pglBindFramebuffer;
static PFN_glReadPixels pglReadPixels;
static PFN_glGetError pglGetError;
static PFN_glGetString pglGetString;
static PFN_glDrawElementsBaseVertex pglDrawElementsBaseVertex;
static PFN_glDrawElementsInstancedBaseVertex pglDrawElementsInstancedBaseVertex;
static PFN_glDrawElementsInstancedBaseVertexBaseInstance pglDrawElementsInstancedBaseVertexBaseInstance;
static PFN_glMultiDrawElementsBaseVertex pglMultiDrawElementsBaseVertex;

/* BaseInstance is a GL 4.2 entry point; the loader correctly withholds
 * it at the shipped 3.3 profile (ARB_base_instance is not advertised,
 * and base-instance drawing is not a GLES 3.0 capability — see the
 * FD-209 record). Its load is therefore optional: the test notes the
 * skip and still proves the other three entry points. The handler
 * itself is implemented (same rebasing helper as the proven forms). */
#define LOAD_OPTIONAL(n) do { \
    p##n = (PFN_##n)(void*)wglGetProcAddress(#n); \
    if (!p##n || (uintptr_t)p##n <= 4 || p##n == (PFN_##n)(intptr_t)-1) \
        p##n = (PFN_##n)(void*)GetProcAddress(g_opengl, #n); \
    if (!p##n || (uintptr_t)p##n <= 4 || p##n == (PFN_##n)(intptr_t)-1) { \
        p##n = 0; fprintf(stderr, "fd270-present: SKIP no proc " #n " (correctly absent at 3.3)\n"); } } while (0)

#define LOAD(n) do { \
    p##n = (PFN_##n)(void*)wglGetProcAddress(#n); \
    if (!p##n || (uintptr_t)p##n <= 4 || p##n == (PFN_##n)(intptr_t)-1) \
        p##n = (PFN_##n)(void*)GetProcAddress(g_opengl, #n); \
    if (!p##n || (uintptr_t)p##n <= 4 || p##n == (PFN_##n)(intptr_t)-1) { \
        fprintf(stderr, "fd270-present: no proc " #n "\n"); return 1; } } while (0)

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    return DefWindowProcA(h, m, w, l);
}

/* Two triangles in NDC: A on the left (vertices 0-2), B on the right (3-5). */
static GLfloat verts[18] = {
    -0.9f, -0.5f, 0.0f,  -0.1f, -0.5f, 0.0f,  -0.5f, 0.5f, 0.0f,
     0.1f, -0.5f, 0.0f,   0.9f, -0.5f, 0.0f,   0.5f, 0.5f, 0.0f,
};
static GLushort idx6[6] = { 0, 1, 2, 3, 4, 5 };
static GLushort idx3[3] = { 0, 1, 2 };

static const char vs_text[] =
    "attribute vec3 pos;\n"
    "void main(){ gl_Position=vec4(pos,1.0); }\n";
static const char fs_text[] =
    "precision mediump float;\n"
    "void main(){ gl_FragColor=vec4(1.0,0.0,0.0,1.0); }\n";

/* Centroid pixels in the 480x360 window (glReadPixels is bottom-up):
 * A (-0.5,-1/6) -> (120,150); B (0.5,-1/6) -> (360,150). */
static int is_red(GLubyte *px) { return px[0] > 200 && px[1] < 50 && px[2] < 50; }
static int is_black(GLubyte *px) { return px[0] < 50 && px[1] < 50 && px[2] < 50; }

static int failures = 0;

static void check(const char *name, int want_a_red, int want_b_red) {
    GLubyte pa[4] = {0, 0, 0, 0}, pb[4] = {0, 0, 0, 0};
    GLenum e = pglGetError();
    /* Route the read through the guest's default-framebuffer binding:
     * the bridge keeps the guest's draws on its own offscreen FBO, and
     * its readback handler reads whatever READ binding is current. */
    pglReadPixels(120, 150, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pa);
    pglReadPixels(360, 150, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pb);
    int ok = (e == GL_NO_ERROR)
        && (want_a_red ? is_red(pa) : is_black(pa))
        && (want_b_red ? is_red(pb) : is_black(pb));
    if (!ok) failures++;
    fprintf(stderr, "FD270: %s err=%u A=(%u,%u,%u) B=(%u,%u,%u) %s\n",
            name, (unsigned)e, pa[0], pa[1], pa[2], pb[0], pb[1], pb[2],
            ok ? "PASS" : "FAIL");
}

int main(void) {
    WNDCLASSA wc = {0};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "fd270-present";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("fd270-present", "FD-267 base-vertex present",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 480, 360,
                              0, 0, wc.hInstance, 0);
    if (!hwnd) { fprintf(stderr, "fd270-present: RESULT 1\n"); return 1; }
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
        fprintf(stderr, "fd270-present: RESULT 1\n"); return 1;
    }
    HGLRC rc = wglCreateContext(hdc);
    if (!rc || !wglMakeCurrent(hdc, rc)) {
        fprintf(stderr, "fd270-present: RESULT 1\n"); return 1;
    }

    g_opengl = GetModuleHandleA("opengl32.dll");
    LOAD(glClearColor); LOAD(glClear); LOAD(glViewport);
    LOAD(glCreateShader); LOAD(glShaderSource); LOAD(glCompileShader);
    LOAD(glGetShaderiv); LOAD(glGetShaderInfoLog);
    LOAD(glCreateProgram); LOAD(glAttachShader); LOAD(glBindAttribLocation);
    LOAD(glLinkProgram); LOAD(glGetProgramiv); LOAD(glGetProgramInfoLog);
    LOAD(glUseProgram);
    LOAD(glGenBuffers); LOAD(glBindBuffer); LOAD(glBufferData);
    LOAD(glGenVertexArrays); LOAD(glBindVertexArray);
    LOAD(glVertexAttribPointer); LOAD(glEnableVertexAttribArray);
    LOAD(glDrawElements); LOAD(glReadPixels); LOAD(glGetError); LOAD(glGetString);
    LOAD(glBindFramebuffer);
    LOAD(glDrawElementsBaseVertex);
    LOAD(glDrawElementsInstancedBaseVertex);
    LOAD_OPTIONAL(glDrawElementsInstancedBaseVertexBaseInstance);
    LOAD(glMultiDrawElementsBaseVertex);
    fprintf(stderr, "fd270-present: version=%s\n", pglGetString(0x1F02));

    pglViewport(0, 0, 480, 360);
    const GLchar *vs_src[1] = { vs_text };
    const GLchar *fs_src[1] = { fs_text };
    GLuint vs = pglCreateShader(GL_VERTEX_SHADER);
    pglShaderSource(vs, 1, vs_src, NULL); pglCompileShader(vs);
    GLint ok = 0; pglGetShaderiv(vs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0}; pglGetShaderInfoLog(vs, sizeof(log), NULL, log);
        fprintf(stderr, "fd270-present: vs compile failed: %s\n", log);
        fprintf(stderr, "fd270-present: RESULT 1\n"); return 1;
    }
    GLuint fs = pglCreateShader(GL_FRAGMENT_SHADER);
    pglShaderSource(fs, 1, fs_src, NULL); pglCompileShader(fs);
    pglGetShaderiv(fs, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0}; pglGetShaderInfoLog(fs, sizeof(log), NULL, log);
        fprintf(stderr, "fd270-present: fs compile failed: %s\n", log);
        fprintf(stderr, "fd270-present: RESULT 1\n"); return 1;
    }
    GLuint prog = pglCreateProgram();
    pglAttachShader(prog, vs); pglAttachShader(prog, fs);
    pglBindAttribLocation(prog, 0, "pos");
    pglLinkProgram(prog);
    pglGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024] = {0}; pglGetProgramInfoLog(prog, sizeof(log), NULL, log);
        fprintf(stderr, "fd270-present: link failed: %s\n", log);
        fprintf(stderr, "fd270-present: RESULT 1\n"); return 1;
    }
    pglUseProgram(prog);

    GLuint vbo = 0, ebo = 0, vao = 0;
    pglGenBuffers(1, &vbo); pglBindBuffer(GL_ARRAY_BUFFER, vbo);
    pglBufferData(GL_ARRAY_BUFFER, sizeof(verts), verts, GL_STATIC_DRAW);
    pglGenBuffers(1, &ebo); pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    pglBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(idx6), idx6, GL_STATIC_DRAW);
    pglGenVertexArrays(1, &vao); pglBindVertexArray(vao);
    pglBindBuffer(GL_ARRAY_BUFFER, vbo);
    pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (const void *)0);
    pglEnableVertexAttribArray(0);
    /* The VAO captured the EBO binding above (bind order: EBO bound
     * before glBindVertexArray in this sequence binds it into the VAO
     * only if bound while the VAO is bound — rebind explicitly). */
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    pglGetError(); /* drain setup errors */

    /* Bisection: clear to blue and read back before any draw. If the
     * pixels are not blue, the readback path itself is broken and no
     * draw result can be trusted; if they are blue, readback works and
     * any later black means the draw did not paint. */
    pglClearColor(0, 0, 1, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    {
        GLubyte pa[4] = {0, 0, 0, 0}, pb[4] = {0, 0, 0, 0};
        pglReadPixels(120, 150, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pa);
        pglReadPixels(360, 150, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pb);
        fprintf(stderr, "FD270: clearcheck A=(%u,%u,%u) B=(%u,%u,%u) %s\n",
                pa[0], pa[1], pa[2], pb[0], pb[1], pb[2],
                (pa[2] > 200 && pb[2] > 200) ? "PASS" : "FAIL");
    }

    /* 0. Control: plain glDrawElements of triangle B directly
     * (indices {3,4,5} at EBO offset 6). If this cannot see red at B,
     * readback itself is unavailable and the run is inconclusive. */
    pglClearColor(0, 0, 0, 1);
    pglClear(GL_COLOR_BUFFER_BIT);
    pglDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void *)6);
    {
        GLubyte pa[4] = {0, 0, 0, 0}, pb[4] = {0, 0, 0, 0};
        GLenum e = pglGetError();
        pglReadPixels(120, 150, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pa);
        pglReadPixels(360, 150, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, pb);
        fprintf(stderr, "FD270: control err=%u A=(%u,%u,%u) B=(%u,%u,%u) %s\n",
                (unsigned)e, pa[0], pa[1], pa[2], pb[0], pb[1], pb[2],
                (e == GL_NO_ERROR && is_black(pa) && is_red(pb)) ? "PASS" : "FAIL");
        if (!(e == GL_NO_ERROR && is_black(pa) && is_red(pb))) {
            fprintf(stderr, "fd270-present: READBACK-UNAVAILABLE\n");
            fprintf(stderr, "fd270-present: RESULT 2\n");
            return 2;
        }
    }

    /* 1. glDrawElementsBaseVertex, EBO bound, indices {0,1,2} + base 3. */
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
    pglClear(GL_COLOR_BUFFER_BIT);
    pglDrawElementsBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void *)0, 3);
    check("DrawElementsBaseVertex/ebo", 0, 1);

    /* 2. glDrawElementsBaseVertex, client-memory indices, no EBO. */
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    pglClear(GL_COLOR_BUFFER_BIT);
    pglDrawElementsBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, idx3, 3);
    check("DrawElementsBaseVertex/client", 0, 1);
    pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);

    /* 3. glDrawElementsInstancedBaseVertex, EBO, 2 instances, base 3. */
    pglClear(GL_COLOR_BUFFER_BIT);
    pglDrawElementsInstancedBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void *)0, 2, 3);
    check("DrawElementsInstancedBaseVertex/ebo", 0, 1);

    /* 4. ...BaseInstance, EBO, 2 instances, base 3, baseInstance 5.
     * Skipped when the entry point does not resolve (see LOAD_OPTIONAL):
     * the loader correctly withholds this GL 4.2 name at 3.3. */
    if (pglDrawElementsInstancedBaseVertexBaseInstance) {
        pglClear(GL_COLOR_BUFFER_BIT);
        pglDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void *)0, 2, 3, 5);
        check("DrawElementsInstancedBaseVertexBaseInstance/ebo", 0, 1);
    } else {
        fprintf(stderr, "FD270: DrawElementsInstancedBaseVertexBaseInstance/ebo SKIP\n");
    }

    /* 5. glMultiDrawElementsBaseVertex, EBO: batch 0 draws A (base 0),
     * batch 1 draws B (base 3). Both centroids must be red. */
    {
        GLsizei counts[2] = { 3, 3 };
        const void *ix[2] = { (const void *)0, (const void *)0 };
        GLint bases[2] = { 0, 3 };
        pglClear(GL_COLOR_BUFFER_BIT);
        pglMultiDrawElementsBaseVertex(GL_TRIANGLES, counts, GL_UNSIGNED_SHORT, ix, 2, bases);
        check("MultiDrawElementsBaseVertex/ebo", 1, 1);
    }

    /* 6. glMultiDrawElementsBaseVertex, client-memory indices, base 3. */
    {
        GLsizei counts[1] = { 3 };
        const void *ix[1] = { idx3 };
        GLint bases[1] = { 3 };
        pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
        pglClear(GL_COLOR_BUFFER_BIT);
        pglMultiDrawElementsBaseVertex(GL_TRIANGLES, counts, GL_UNSIGNED_SHORT, ix, 1, bases);
        check("MultiDrawElementsBaseVertex/client", 0, 1);
    }

    wglMakeCurrent(0, 0);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    fprintf(stderr, "fd270-present: RESULT %d\n", failures ? 1 : 0);
    return failures ? 1 : 0;
}
