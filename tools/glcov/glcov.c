/*
 * glcov.c — systematic OpenGL bridge API coverage harness for Boxedwine64.
 *
 * Calls every gl64bridge handler (306 entry points incl. glX* and the
 * internal traceProc) in-browser through wglGetProcAddress, times each call
 * (2 runs), and reports machine-readable results:
 *   GLCOV: <name>=<err1>,<err2>:<us1>:<us2>
 *   GLCOV-TRY: <name>          (before each call; crash repro marker)
 *   GLCOV-NOTE: ...            (return values, generated ids, missing procs)
 *   glcov: RESULT 0|1
 *
 * err = glGetError() drained after the call (0 = clean). A missing proc
 * (wglGetProcAddress NULL, incl. sentinel 1/2/3/-1, and no opengl32.dll
 * export) is reported as GLCOV: <name>=-1,-1:0:0 plus a NOTE.
 *
 * Buckets (analyzer-assigned, mirroring vkcov):
 *   working : err==0 on both runs
 *   broken  : err!=0, missing proc, or crash (no RESULT; last TRY = repro)
 *
 * No GL headers are used; every function-pointer typedef is declared
 * manually (see reasoning in header comment of the generator).
 *
 * Build: x86_64-w64-mingw32-gcc -O2 -o glcov.exe glcov.c \
 *          -lopengl32 -lgdi32 -luser32
 */
#include <windows.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

/* ---- manual GL types (no GL/gl.h) ---- */
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
typedef float GLclampf;
typedef double GLdouble;
typedef double GLclampd;
typedef int64_t GLint64;
typedef uint64_t GLuint64;
typedef int GLsizei;
typedef ptrdiff_t GLsizeiptr;
typedef ptrdiff_t GLintptr;
typedef char GLchar;
typedef struct __GLsync *GLsync;
#ifndef APIENTRY
#define APIENTRY __stdcall
#endif
typedef void (APIENTRY *GLDEBUGPROC)(GLenum source, GLenum type, GLuint id,
    GLenum severity, GLsizei length, const GLchar *message, const void *userParam);

/* ---- GL constants used by the harness ---- */
#define GL_FALSE 0
#define GL_TRUE 1
#define GL_NONE 0
#define GL_ZERO 0
#define GL_ONE 1
#define GL_TRIANGLES 0x0004
#define GL_CCW 0x0901
#define GL_FRONT 0x0404
#define GL_BACK 0x0405
#define GL_FRONT_AND_BACK 0x0408
#define GL_DEPTH_TEST 0x0B71
#define GL_BLEND 0x0BE2
#define GL_SMOOTH 0x1D01
#define GL_FILL 0x1B02
#define GL_MODELVIEW 0x1700
#define GL_AMBIENT 0x1200
#define GL_AMBIENT_AND_DIFFUSE 0x1602
#define GL_SHININESS 0x1601
#define GL_LIGHT0 0x4000
#define GL_LESS 0x0201
#define GL_ALWAYS 0x0207
#define GL_KEEP 0x1E00
#define GL_FUNC_ADD 0x8006
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_COLOR_BUFFER_BIT 0x00004000
#define GL_DEPTH_BUFFER_BIT 0x00000100
#define GL_ARRAY_BUFFER 0x8892
#define GL_ELEMENT_ARRAY_BUFFER 0x8893
#define GL_COPY_WRITE_BUFFER 0x8F37
#define GL_TRANSFORM_FEEDBACK_BUFFER 0x8C8E
#define GL_TEXTURE_BUFFER 0x8C2A
#define GL_STATIC_DRAW 0x88E4
#define GL_MAP_READ_BIT 0x0001
#define GL_BUFFER_SIZE 0x8764
#define GL_TEXTURE_1D 0x0DE0
#define GL_TEXTURE_2D 0x0DE1
#define GL_TEXTURE_3D 0x806F
#define GL_TEXTURE_2D_MULTISAMPLE 0x9100
#define GL_TEXTURE_2D_MULTISAMPLE_ARRAY 0x9102
#define GL_TEXTURE0 0x84C0
#define GL_RGBA 0x1908
#define GL_RGBA8 0x8058
#define GL_RGBA32F 0x8814
#define GL_UNSIGNED_BYTE 0x1401
#define GL_UNSIGNED_SHORT 0x1403
#define GL_FLOAT 0x1406
#define GL_INT 0x1404
#define GL_TEXTURE_MIN_FILTER 0x2801
#define GL_TEXTURE_MAG_FILTER 0x2800
#define GL_TEXTURE_MIN_LOD 0x813A
#define GL_TEXTURE_MAX_LOD 0x813B
#define GL_LINEAR 0x2601
#define GL_NEAREST 0x2600
#define GL_TEXTURE_WIDTH 0x1000
#define GL_COMPRESSED_RGB_S3TC_DXT1_EXT 0x83F1
#define GL_FRAMEBUFFER 0x8D40
#define GL_RENDERBUFFER 0x8D41
#define GL_COLOR_ATTACHMENT0 0x8CE0
#define GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE 0x8CD0
#define GL_FRAMEBUFFER_COMPLETE 0x8CD5
#define GL_RENDERBUFFER_WIDTH 0x8D42
#define GL_VERTEX_SHADER 0x8B31
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define GL_SAMPLES_PASSED 0x8914
#define GL_QUERY_RESULT_AVAILABLE 0x8867
#define GL_QUERY_COUNTER_BITS 0x8864
#define GL_TIMESTAMP 0x8E28
#define GL_SYNC_GPU_COMMANDS_COMPLETE 0x9117
#define GL_SYNC_STATUS 0x9114
#define GL_MAX_TEXTURE_SIZE 0x0D33
#define GL_MAX_VIEWPORT_DIMS 0x0D3A
#define GL_VIEWPORT 0x0BA2
#define GL_VERSION 0x1F02
#define GL_EXTENSIONS 0x1F03
#define GL_PERSPECTIVE_CORRECTION_HINT 0x0C50
#define GL_NICEST 0x1102
#define GL_UNPACK_ALIGNMENT 0x0CF5
#define GL_POINT_SIZE_MIN 0x8126
#define GL_POINT_DISTANCE_ATTENUATION 0x8129
#define GL_POINT_SPRITE_COORD_ORIGIN 0x8CA0
#define GL_LOWER_LEFT 0x8CA1
#define GL_SAMPLE_POSITION 0x8E50
#define GL_UNIFORM_BLOCK_DATA_SIZE 0x8A40
#define GL_DONT_CARE 0x1100
#define GL_DEBUG_SOURCE_APPLICATION 0x824A
#define GL_DEBUG_TYPE_MARKER 0x8268
#define GL_DEBUG_SEVERITY_NOTIFICATION 0x826B
#define GL_DEPTH_STENCIL 0x84F9
#define GL_COLOR 0x1800
#define GL_INTERLEAVED_ATTRIBS 0x8C8C

static uint64_t qpf_us(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c);
    return (uint64_t)c.QuadPart * 1000000ULL / (uint64_t)f.QuadPart;
}
static void rep(const char* name, long long e1, long long e2,
                uint64_t us1, uint64_t us2) {
    fprintf(stderr, "GLCOV: %s=%lld,%lld:%llu:%llu\n", name, e1, e2,
            (unsigned long long)us1, (unsigned long long)us2);
    fflush(stderr);
}
static void trybegin(const char* name) {
    fprintf(stderr, "GLCOV-TRY: %s\n", name); fflush(stderr);
}
static void note(const char* fmt, ...) {
    char b[512]; va_list ap; va_start(ap, fmt);
    wvsprintfA(b, fmt, ap); va_end(ap);
    fprintf(stderr, "GLCOV-NOTE: %s\n", b); fflush(stderr);
}

static HMODULE g_gl32 = 0;
#define LOADGL(n) do { \
    p##n = (PFN_##n)(void*)wglGetProcAddress(#n); \
    if (!p##n || (uintptr_t)p##n <= 4 || p##n == (PFN_##n)(intptr_t)-1) \
        p##n = (PFN_##n)(void*)GetProcAddress(g_gl32, #n); \
    if (!p##n || (uintptr_t)p##n <= 4 || p##n == (PFN_##n)(intptr_t)-1) { \
        p##n = 0; note("no export: %s", #n); \
    } } while(0)

/* gen 2+2 ids (two timed runs), then delete all 4 */
#define GENDEL(gen, del) do { \
    GLuint _o[4] = {0,0,0,0}; \
    if (p##gen && p##del) { \
        trybegin(#gen); pglGetError(); \
        uint64_t _g0=qpf_us(); p##gen(2,_o); GLenum _ge1=pglGetError(); uint64_t _g1=qpf_us(); \
        trybegin(#gen); \
        uint64_t _g2=qpf_us(); p##gen(2,_o+2); GLenum _ge2=pglGetError(); uint64_t _g3=qpf_us(); \
        note(#gen " ids=%u,%u,%u,%u", _o[0], _o[1], _o[2], _o[3]); \
        rep(#gen,(long long)_ge1,(long long)_ge2,_g1-_g0,_g3-_g2); \
        trybegin(#del); \
        uint64_t _d0=qpf_us(); p##del(4,_o); GLenum _de1=pglGetError(); uint64_t _d1=qpf_us(); \
        rep(#del,(long long)_de1,(long long)_de1,_d1-_d0,0); \
    } else { \
        trybegin(#gen); note("no proc: "#gen); rep(#gen,-1,-1,0,0); \
        trybegin(#del); note("no proc: "#del); rep(#del,-1,-1,0,0); \
    } } while(0)

/* ---- shared GL objects (valid handles for the coverage rows) ---- */
static GLuint g_tex=0, g_tex1d=0, g_tex3d=0, g_buf=0, g_ibuf=0;
static GLuint g_prog=0, g_vs=0, g_fs=0, g_fbo=0, g_rb=0, g_vao=0;
static GLuint g_qry=0, g_qry2=0, g_smp=0;
static GLuint s_qry2[2] = {0,0};
static GLsync g_sync=0;

/* ---- scratch buffers for pointer args ---- */
static GLfloat tri[9] = {-0.5f,-0.5f,0.0f, 0.5f,-0.5f,0.0f, 0.0f,0.5f,0.0f};
static GLushort idx6[6] = {0,1,2, 0,2,1};
static GLubyte s_px[64*64*4], s_px3d[8*8*8*4], s_tmp[64];
static GLubyte dxt[8] = {0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88};
static GLubyte dxt3[64] = {0};
static GLfloat clearc[4] = {0.0f,0.0f,0.0f,1.0f};
static GLint cleari[4] = {0,0,0,0};
static GLuint clearu[4] = {0,0,0,0};
static GLenum dbb[1] = { GL_BACK };
static GLenum mde_mode[1] = { GL_TRIANGLES };
static GLsizei mde_count[1] = { 3 };
static const void *mde_idx[1] = { (const void*)0 };
static GLint mde_base[1] = { 0 };
static GLint s_int=0, s_int2=0, s_len=0, s_size=0;
static GLuint s_u=0, s_ids[4]={0,0,0,0}, s_sh[8]={0,0,0,0,0,0,0,0};
static GLint s_i4[4]={0,0,0,0}, s_i3[3]={0,0,0}, s_i2[2]={0,0}, s_i1=0;
static GLuint s_u4[4]={0,0,0,0}, s_u3[3]={0,0,0}, s_u2[2]={0,0};
static GLfloat s_f=0.0f, s_f2[2]={0,0}, s_f3[3]={0,0,0}, s_f4[4]={0,0,0,0};
static GLfloat s_f9[9]={0,0,0,0,0,0,0,0,0};
static GLfloat s_m16[16]={1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
static GLdouble s_d1=0, s_d2[2]={0,0}, s_d3[3]={0,0,0}, s_d4[4]={0,0,0,0};
static GLboolean s_b=0;
static GLenum s_type=0, s_src[4]={0,0,0,0}, s_typ[4]={0,0,0,0}, s_sev[4]={0,0,0,0};
static GLsizei s_slen[4]={0,0,0,0};
static GLchar s_name[64]={0}, s_log[256]={0};
static GLuint64 s_u64=0;
static const char vs_text[] =
    "attribute vec3 pos;\n"
    "uniform mat4 mvp;\n"
    "varying vec3 vC;\n"
    "void main(){ vC=pos; gl_Position=mvp*vec4(pos,1.0); }\n";
static const char fs_text[] =
    "precision mediump float;\n"
    "uniform vec4 u;\n"
    "varying vec3 vC;\n"
    "void main(){ gl_FragColor=u+vec4(vC,1.0); }\n";
static const GLchar *vs_src[1] = { vs_text };
static const GLchar *fs_src[1] = { fs_text };
static const GLchar *tf_vary[1] = { "vC" };

/* glUnmapBuffer is NOT a bridge handler; loaded quietly for test hygiene
 * (unmap between the two timed glMapBufferRange runs). No TRY/rep row. */
typedef GLboolean (APIENTRY *PFN_glUnmapBuffer)(GLenum target);
static PFN_glUnmapBuffer pglUnmapBuffer = 0;

static void APIENTRY dbgcb(GLenum src, GLenum type, GLuint id, GLenum sev,
                           GLsizei len, const GLchar *msg, const void *up) {
    (void)src; (void)type; (void)id; (void)sev; (void)len; (void)msg; (void)up;
}

typedef void (APIENTRY *PFN_glActiveTexture)(GLenum texture);
static PFN_glActiveTexture pglActiveTexture = 0;
typedef void (APIENTRY *PFN_glAttachShader)(GLuint program, GLuint shader);
static PFN_glAttachShader pglAttachShader = 0;
typedef void (APIENTRY *PFN_glBegin)(GLenum mode);
static PFN_glBegin pglBegin = 0;
typedef void (APIENTRY *PFN_glBeginQuery)(GLenum target, GLuint id);
static PFN_glBeginQuery pglBeginQuery = 0;
typedef void (APIENTRY *PFN_glBeginTransformFeedback)(GLenum primitiveMode);
static PFN_glBeginTransformFeedback pglBeginTransformFeedback = 0;
typedef void (APIENTRY *PFN_glBindAttribLocation)(GLuint program, GLuint index, const GLchar *name);
static PFN_glBindAttribLocation pglBindAttribLocation = 0;
typedef void (APIENTRY *PFN_glBindBuffer)(GLenum target, GLuint buffer);
static PFN_glBindBuffer pglBindBuffer = 0;
typedef void (APIENTRY *PFN_glBindBufferBase)(GLenum target, GLuint index, GLuint buffer);
static PFN_glBindBufferBase pglBindBufferBase = 0;
typedef void (APIENTRY *PFN_glBindBufferRange)(GLenum target, GLuint index, GLuint buffer, GLintptr offset, GLsizeiptr size);
static PFN_glBindBufferRange pglBindBufferRange = 0;
typedef void (APIENTRY *PFN_glBindFragDataLocation)(GLuint program, GLuint colorNumber, const GLchar *name);
static PFN_glBindFragDataLocation pglBindFragDataLocation = 0;
typedef void (APIENTRY *PFN_glBindFramebuffer)(GLenum target, GLuint framebuffer);
static PFN_glBindFramebuffer pglBindFramebuffer = 0;
typedef void (APIENTRY *PFN_glBindRenderbuffer)(GLenum target, GLuint renderbuffer);
static PFN_glBindRenderbuffer pglBindRenderbuffer = 0;
typedef void (APIENTRY *PFN_glBindSampler)(GLuint unit, GLuint sampler);
static PFN_glBindSampler pglBindSampler = 0;
typedef void (APIENTRY *PFN_glBindTexture)(GLenum target, GLuint texture);
static PFN_glBindTexture pglBindTexture = 0;
typedef void (APIENTRY *PFN_glBindVertexArray)(GLuint array);
static PFN_glBindVertexArray pglBindVertexArray = 0;
typedef void (APIENTRY *PFN_glBlendColor)(GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha);
static PFN_glBlendColor pglBlendColor = 0;
typedef void (APIENTRY *PFN_glBlendEquation)(GLenum mode);
static PFN_glBlendEquation pglBlendEquation = 0;
typedef void (APIENTRY *PFN_glBlendEquationSeparate)(GLenum modeRGB, GLenum modeAlpha);
static PFN_glBlendEquationSeparate pglBlendEquationSeparate = 0;
typedef void (APIENTRY *PFN_glBlendEquationSeparatei)(GLuint buf, GLenum modeRGB, GLenum modeAlpha);
static PFN_glBlendEquationSeparatei pglBlendEquationSeparatei = 0;
typedef void (APIENTRY *PFN_glBlendEquationi)(GLuint buf, GLenum mode);
static PFN_glBlendEquationi pglBlendEquationi = 0;
typedef void (APIENTRY *PFN_glBlendFunc)(GLenum sfactor, GLenum dfactor);
static PFN_glBlendFunc pglBlendFunc = 0;
typedef void (APIENTRY *PFN_glBlendFuncSeparate)(GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
static PFN_glBlendFuncSeparate pglBlendFuncSeparate = 0;
typedef void (APIENTRY *PFN_glBlendFuncSeparatei)(GLuint buf, GLenum sfactorRGB, GLenum dfactorRGB, GLenum sfactorAlpha, GLenum dfactorAlpha);
static PFN_glBlendFuncSeparatei pglBlendFuncSeparatei = 0;
typedef void (APIENTRY *PFN_glBlendFunci)(GLuint buf, GLenum sfactor, GLenum dfactor);
static PFN_glBlendFunci pglBlendFunci = 0;
typedef void (APIENTRY *PFN_glBlitFramebuffer)(GLint sx0, GLint sy0, GLint sx1, GLint sy1, GLint dx0, GLint dy0, GLint dx1, GLint dy1, GLbitfield mask, GLenum filter);
static PFN_glBlitFramebuffer pglBlitFramebuffer = 0;
typedef void (APIENTRY *PFN_glBufferData)(GLenum target, GLsizeiptr size, const void *data, GLenum usage);
static PFN_glBufferData pglBufferData = 0;
typedef void (APIENTRY *PFN_glBufferStorage)(GLenum target, GLsizeiptr size, const void *data, GLbitfield flags);
static PFN_glBufferStorage pglBufferStorage = 0;
typedef void (APIENTRY *PFN_glBufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, const void *data);
static PFN_glBufferSubData pglBufferSubData = 0;
typedef GLenum (APIENTRY *PFN_glCheckFramebufferStatus)(GLenum target);
static PFN_glCheckFramebufferStatus pglCheckFramebufferStatus = 0;
typedef void (APIENTRY *PFN_glClear)(GLbitfield mask);
static PFN_glClear pglClear = 0;
typedef void (APIENTRY *PFN_glClearBufferfi)(GLenum buffer, GLint drawbuffer, GLfloat depth, GLint stencil);
static PFN_glClearBufferfi pglClearBufferfi = 0;
typedef void (APIENTRY *PFN_glClearBufferfv)(GLenum buffer, GLint drawbuffer, const GLfloat *value);
static PFN_glClearBufferfv pglClearBufferfv = 0;
typedef void (APIENTRY *PFN_glClearBufferiv)(GLenum buffer, GLint drawbuffer, const GLint *value);
static PFN_glClearBufferiv pglClearBufferiv = 0;
typedef void (APIENTRY *PFN_glClearBufferuiv)(GLenum buffer, GLint drawbuffer, const GLuint *value);
static PFN_glClearBufferuiv pglClearBufferuiv = 0;
typedef void (APIENTRY *PFN_glClearColor)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
static PFN_glClearColor pglClearColor = 0;
typedef void (APIENTRY *PFN_glClearDepth)(GLclampd depth);
static PFN_glClearDepth pglClearDepth = 0;
typedef GLenum (APIENTRY *PFN_glClientWaitSync)(GLsync sync, GLbitfield flags, GLuint64 timeout);
static PFN_glClientWaitSync pglClientWaitSync = 0;
typedef void (APIENTRY *PFN_glColor3f)(GLfloat r, GLfloat g, GLfloat b);
static PFN_glColor3f pglColor3f = 0;
typedef void (APIENTRY *PFN_glColor4f)(GLfloat r, GLfloat g, GLfloat b, GLfloat a);
static PFN_glColor4f pglColor4f = 0;
typedef void (APIENTRY *PFN_glColorMask)(GLboolean r, GLboolean g, GLboolean b, GLboolean a);
static PFN_glColorMask pglColorMask = 0;
typedef void (APIENTRY *PFN_glColorMaski)(GLuint index, GLboolean r, GLboolean g, GLboolean b, GLboolean a);
static PFN_glColorMaski pglColorMaski = 0;
typedef void (APIENTRY *PFN_glColorMaterial)(GLenum face, GLenum mode);
static PFN_glColorMaterial pglColorMaterial = 0;
typedef void (APIENTRY *PFN_glCompileShader)(GLuint shader);
static PFN_glCompileShader pglCompileShader = 0;
typedef void (APIENTRY *PFN_glCompressedTexImage2D)(GLenum target, GLint level, GLenum internalformat, GLsizei width, GLsizei height, GLint border, GLsizei imageSize, const void *data);
static PFN_glCompressedTexImage2D pglCompressedTexImage2D = 0;
typedef void (APIENTRY *PFN_glCompressedTexImage3D)(GLenum target, GLint level, GLenum internalformat, GLsizei w, GLsizei h, GLsizei d, GLint border, GLsizei imageSize, const void *data);
static PFN_glCompressedTexImage3D pglCompressedTexImage3D = 0;
typedef void (APIENTRY *PFN_glCompressedTexSubImage2D)(GLenum target, GLint level, GLint xoff, GLint yoff, GLsizei w, GLsizei h, GLenum format, GLsizei imageSize, const void *data);
static PFN_glCompressedTexSubImage2D pglCompressedTexSubImage2D = 0;
typedef void (APIENTRY *PFN_glCompressedTexSubImage3D)(GLenum target, GLint level, GLint xoff, GLint yoff, GLint zoff, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLsizei imageSize, const void *data);
static PFN_glCompressedTexSubImage3D pglCompressedTexSubImage3D = 0;
typedef void (APIENTRY *PFN_glCopyBufferSubData)(GLenum readTarget, GLenum writeTarget, GLintptr readOffset, GLintptr writeOffset, GLsizeiptr size);
static PFN_glCopyBufferSubData pglCopyBufferSubData = 0;
typedef GLuint (APIENTRY *PFN_glCreateProgram)(void);
static PFN_glCreateProgram pglCreateProgram = 0;
typedef GLuint (APIENTRY *PFN_glCreateShader)(GLenum type);
static PFN_glCreateShader pglCreateShader = 0;
typedef void (APIENTRY *PFN_glCullFace)(GLenum mode);
static PFN_glCullFace pglCullFace = 0;
typedef void (APIENTRY *PFN_glDebugMessageCallback)(GLDEBUGPROC callback, const void *userParam);
static PFN_glDebugMessageCallback pglDebugMessageCallback = 0;
typedef void (APIENTRY *PFN_glDebugMessageControl)(GLenum source, GLenum type, GLenum severity, GLsizei count, const GLuint *ids, GLboolean enabled);
static PFN_glDebugMessageControl pglDebugMessageControl = 0;
typedef void (APIENTRY *PFN_glDebugMessageInsert)(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar *buf);
static PFN_glDebugMessageInsert pglDebugMessageInsert = 0;
typedef void (APIENTRY *PFN_glDeleteProgram)(GLuint program);
static PFN_glDeleteProgram pglDeleteProgram = 0;
typedef void (APIENTRY *PFN_glDeleteShader)(GLuint shader);
static PFN_glDeleteShader pglDeleteShader = 0;
typedef void (APIENTRY *PFN_glDeleteSync)(GLsync sync);
static PFN_glDeleteSync pglDeleteSync = 0;
typedef void (APIENTRY *PFN_glDepthFunc)(GLenum func);
static PFN_glDepthFunc pglDepthFunc = 0;
typedef void (APIENTRY *PFN_glDepthMask)(GLboolean flag);
static PFN_glDepthMask pglDepthMask = 0;
typedef void (APIENTRY *PFN_glDepthRange)(GLclampd n, GLclampd f);
static PFN_glDepthRange pglDepthRange = 0;
typedef void (APIENTRY *PFN_glDetachShader)(GLuint program, GLuint shader);
static PFN_glDetachShader pglDetachShader = 0;
typedef void (APIENTRY *PFN_glDisable)(GLenum cap);
static PFN_glDisable pglDisable = 0;
typedef void (APIENTRY *PFN_glDisableVertexAttribArray)(GLuint index);
static PFN_glDisableVertexAttribArray pglDisableVertexAttribArray = 0;
typedef void (APIENTRY *PFN_glDisablei)(GLenum target, GLuint index);
static PFN_glDisablei pglDisablei = 0;
typedef void (APIENTRY *PFN_glDrawArrays)(GLenum mode, GLint first, GLsizei count);
static PFN_glDrawArrays pglDrawArrays = 0;
typedef void (APIENTRY *PFN_glDrawArraysInstanced)(GLenum mode, GLint first, GLsizei count, GLsizei instancecount);
static PFN_glDrawArraysInstanced pglDrawArraysInstanced = 0;
typedef void (APIENTRY *PFN_glDrawArraysInstancedBaseInstance)(GLenum mode, GLint first, GLsizei count, GLsizei instancecount, GLuint baseinstance);
static PFN_glDrawArraysInstancedBaseInstance pglDrawArraysInstancedBaseInstance = 0;
typedef void (APIENTRY *PFN_glDrawBuffers)(GLsizei n, const GLenum *bufs);
static PFN_glDrawBuffers pglDrawBuffers = 0;
typedef void (APIENTRY *PFN_glDrawElements)(GLenum mode, GLsizei count, GLenum type, const void *indices);
static PFN_glDrawElements pglDrawElements = 0;
typedef void (APIENTRY *PFN_glDrawElementsBaseVertex)(GLenum mode, GLsizei count, GLenum type, const void *indices, GLint basevertex);
static PFN_glDrawElementsBaseVertex pglDrawElementsBaseVertex = 0;
typedef void (APIENTRY *PFN_glDrawElementsInstanced)(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount);
static PFN_glDrawElementsInstanced pglDrawElementsInstanced = 0;
typedef void (APIENTRY *PFN_glDrawElementsInstancedBaseVertex)(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount, GLint basevertex);
static PFN_glDrawElementsInstancedBaseVertex pglDrawElementsInstancedBaseVertex = 0;
typedef void (APIENTRY *PFN_glDrawElementsInstancedBaseVertexBaseInstance)(GLenum mode, GLsizei count, GLenum type, const void *indices, GLsizei instancecount, GLint basevertex, GLuint baseinstance);
static PFN_glDrawElementsInstancedBaseVertexBaseInstance pglDrawElementsInstancedBaseVertexBaseInstance = 0;
typedef void (APIENTRY *PFN_glDrawRangeElements)(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices);
static PFN_glDrawRangeElements pglDrawRangeElements = 0;
typedef void (APIENTRY *PFN_glDrawRangeElementsBaseVertex)(GLenum mode, GLuint start, GLuint end, GLsizei count, GLenum type, const void *indices, GLint basevertex);
static PFN_glDrawRangeElementsBaseVertex pglDrawRangeElementsBaseVertex = 0;
typedef void (APIENTRY *PFN_glEnable)(GLenum cap);
static PFN_glEnable pglEnable = 0;
typedef void (APIENTRY *PFN_glEnableVertexAttribArray)(GLuint index);
static PFN_glEnableVertexAttribArray pglEnableVertexAttribArray = 0;
typedef void (APIENTRY *PFN_glEnablei)(GLenum target, GLuint index);
static PFN_glEnablei pglEnablei = 0;
typedef void (APIENTRY *PFN_glEnd)(void);
static PFN_glEnd pglEnd = 0;
typedef void (APIENTRY *PFN_glEndQuery)(GLenum target);
static PFN_glEndQuery pglEndQuery = 0;
typedef void (APIENTRY *PFN_glEndTransformFeedback)(void);
static PFN_glEndTransformFeedback pglEndTransformFeedback = 0;
typedef GLsync (APIENTRY *PFN_glFenceSync)(GLenum condition, GLbitfield flags);
static PFN_glFenceSync pglFenceSync = 0;
typedef void (APIENTRY *PFN_glFinalCombinerInputNV)(GLenum stage, GLenum portion, GLenum variable, GLenum input);
static PFN_glFinalCombinerInputNV pglFinalCombinerInputNV = 0;
typedef void (APIENTRY *PFN_glFinish)(void);
static PFN_glFinish pglFinish = 0;
typedef void (APIENTRY *PFN_glFlush)(void);
static PFN_glFlush pglFlush = 0;
typedef void (APIENTRY *PFN_glFramebufferRenderbuffer)(GLenum target, GLenum attachment, GLenum renderbuffertarget, GLuint renderbuffer);
static PFN_glFramebufferRenderbuffer pglFramebufferRenderbuffer = 0;
typedef void (APIENTRY *PFN_glFramebufferTexture)(GLenum target, GLenum attachment, GLuint texture, GLint level);
static PFN_glFramebufferTexture pglFramebufferTexture = 0;
typedef void (APIENTRY *PFN_glFramebufferTexture1D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
static PFN_glFramebufferTexture1D pglFramebufferTexture1D = 0;
typedef void (APIENTRY *PFN_glFramebufferTexture2D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level);
static PFN_glFramebufferTexture2D pglFramebufferTexture2D = 0;
typedef void (APIENTRY *PFN_glFramebufferTexture3D)(GLenum target, GLenum attachment, GLenum textarget, GLuint texture, GLint level, GLint zoffset);
static PFN_glFramebufferTexture3D pglFramebufferTexture3D = 0;
typedef void (APIENTRY *PFN_glFramebufferTextureLayer)(GLenum target, GLenum attachment, GLuint texture, GLint level, GLint layer);
static PFN_glFramebufferTextureLayer pglFramebufferTextureLayer = 0;
typedef void (APIENTRY *PFN_glFrontFace)(GLenum mode);
static PFN_glFrontFace pglFrontFace = 0;
typedef void (APIENTRY *PFN_glFrustum)(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
static PFN_glFrustum pglFrustum = 0;
typedef void (APIENTRY *PFN_glGenBuffers)(GLsizei n, GLuint *ids);
static PFN_glGenBuffers pglGenBuffers = 0;
typedef void (APIENTRY *PFN_glDeleteBuffers)(GLsizei n, const GLuint *ids);
static PFN_glDeleteBuffers pglDeleteBuffers = 0;
typedef void (APIENTRY *PFN_glGenFramebuffers)(GLsizei n, GLuint *ids);
static PFN_glGenFramebuffers pglGenFramebuffers = 0;
typedef void (APIENTRY *PFN_glDeleteFramebuffers)(GLsizei n, const GLuint *ids);
static PFN_glDeleteFramebuffers pglDeleteFramebuffers = 0;
typedef void (APIENTRY *PFN_glGenQueries)(GLsizei n, GLuint *ids);
static PFN_glGenQueries pglGenQueries = 0;
typedef void (APIENTRY *PFN_glDeleteQueries)(GLsizei n, const GLuint *ids);
static PFN_glDeleteQueries pglDeleteQueries = 0;
typedef void (APIENTRY *PFN_glGenRenderbuffers)(GLsizei n, GLuint *ids);
static PFN_glGenRenderbuffers pglGenRenderbuffers = 0;
typedef void (APIENTRY *PFN_glDeleteRenderbuffers)(GLsizei n, const GLuint *ids);
static PFN_glDeleteRenderbuffers pglDeleteRenderbuffers = 0;
typedef void (APIENTRY *PFN_glGenSamplers)(GLsizei n, GLuint *ids);
static PFN_glGenSamplers pglGenSamplers = 0;
typedef void (APIENTRY *PFN_glDeleteSamplers)(GLsizei n, const GLuint *ids);
static PFN_glDeleteSamplers pglDeleteSamplers = 0;
typedef void (APIENTRY *PFN_glGenTextures)(GLsizei n, GLuint *ids);
static PFN_glGenTextures pglGenTextures = 0;
typedef void (APIENTRY *PFN_glDeleteTextures)(GLsizei n, const GLuint *ids);
static PFN_glDeleteTextures pglDeleteTextures = 0;
typedef void (APIENTRY *PFN_glGenVertexArrays)(GLsizei n, GLuint *ids);
static PFN_glGenVertexArrays pglGenVertexArrays = 0;
typedef void (APIENTRY *PFN_glDeleteVertexArrays)(GLsizei n, const GLuint *ids);
static PFN_glDeleteVertexArrays pglDeleteVertexArrays = 0;
typedef void (APIENTRY *PFN_glGenerateMipmap)(GLenum target);
static PFN_glGenerateMipmap pglGenerateMipmap = 0;
typedef void (APIENTRY *PFN_glGetActiveUniform)(GLuint program, GLuint index, GLsizei bufSize, GLsizei *length, GLint *size, GLenum *type, GLchar *name);
static PFN_glGetActiveUniform pglGetActiveUniform = 0;
typedef void (APIENTRY *PFN_glGetActiveUniformBlockName)(GLuint program, GLuint uniformBlockIndex, GLsizei bufSize, GLsizei *length, GLchar *uniformBlockName);
static PFN_glGetActiveUniformBlockName pglGetActiveUniformBlockName = 0;
typedef void (APIENTRY *PFN_glGetActiveUniformBlockiv)(GLuint program, GLuint uniformBlockIndex, GLenum pname, GLint *params);
static PFN_glGetActiveUniformBlockiv pglGetActiveUniformBlockiv = 0;
typedef void (APIENTRY *PFN_glGetAttachedShaders)(GLuint program, GLsizei maxCount, GLsizei *count, GLuint *shaders);
static PFN_glGetAttachedShaders pglGetAttachedShaders = 0;
typedef GLint (APIENTRY *PFN_glGetAttribLocation)(GLuint program, const GLchar *name);
static PFN_glGetAttribLocation pglGetAttribLocation = 0;
typedef void (APIENTRY *PFN_glGetBooleani_v)(GLenum target, GLuint index, GLboolean *data);
static PFN_glGetBooleani_v pglGetBooleani_v = 0;
typedef void (APIENTRY *PFN_glGetBufferParameteriv)(GLenum target, GLenum pname, GLint *params);
static PFN_glGetBufferParameteriv pglGetBufferParameteriv = 0;
typedef void (APIENTRY *PFN_glGetBufferSubData)(GLenum target, GLintptr offset, GLsizeiptr size, void *data);
static PFN_glGetBufferSubData pglGetBufferSubData = 0;
typedef void (APIENTRY *PFN_glGetCompressedTexImage)(GLenum target, GLint level, void *img);
static PFN_glGetCompressedTexImage pglGetCompressedTexImage = 0;
typedef GLuint (APIENTRY *PFN_glGetDebugMessageLog)(GLuint count, GLsizei bufSize, GLenum *sources, GLenum *types, GLuint *ids, GLenum *severities, GLsizei *lengths, GLchar *messageLog);
static PFN_glGetDebugMessageLog pglGetDebugMessageLog = 0;
typedef GLenum (APIENTRY *PFN_glGetError)(void);
static PFN_glGetError pglGetError = 0;
typedef void (APIENTRY *PFN_glGetFloatv)(GLenum pname, GLfloat *data);
static PFN_glGetFloatv pglGetFloatv = 0;
typedef GLint (APIENTRY *PFN_glGetFragDataIndex)(GLuint program, const GLchar *name);
static PFN_glGetFragDataIndex pglGetFragDataIndex = 0;
typedef void (APIENTRY *PFN_glGetFramebufferAttachmentParameteriv)(GLenum target, GLenum attachment, GLenum pname, GLint *params);
static PFN_glGetFramebufferAttachmentParameteriv pglGetFramebufferAttachmentParameteriv = 0;
typedef void (APIENTRY *PFN_glGetIntegeri_v)(GLenum target, GLuint index, GLint *data);
static PFN_glGetIntegeri_v pglGetIntegeri_v = 0;
typedef void (APIENTRY *PFN_glGetIntegerv)(GLenum pname, GLint *data);
static PFN_glGetIntegerv pglGetIntegerv = 0;
typedef void (APIENTRY *PFN_glGetMultisamplefv)(GLenum pname, GLuint index, GLfloat *val);
static PFN_glGetMultisamplefv pglGetMultisamplefv = 0;
typedef void (APIENTRY *PFN_glGetProgramInfoLog)(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
static PFN_glGetProgramInfoLog pglGetProgramInfoLog = 0;
typedef void (APIENTRY *PFN_glGetProgramiv)(GLuint program, GLenum pname, GLint *params);
static PFN_glGetProgramiv pglGetProgramiv = 0;
typedef void (APIENTRY *PFN_glGetQueryObjectiv)(GLuint id, GLenum pname, GLint *params);
static PFN_glGetQueryObjectiv pglGetQueryObjectiv = 0;
typedef void (APIENTRY *PFN_glGetQueryObjectui64v)(GLuint id, GLenum pname, GLuint64 *params);
static PFN_glGetQueryObjectui64v pglGetQueryObjectui64v = 0;
typedef void (APIENTRY *PFN_glGetQueryObjectuiv)(GLuint id, GLenum pname, GLuint *params);
static PFN_glGetQueryObjectuiv pglGetQueryObjectuiv = 0;
typedef void (APIENTRY *PFN_glGetQueryiv)(GLenum target, GLenum pname, GLint *params);
static PFN_glGetQueryiv pglGetQueryiv = 0;
typedef void (APIENTRY *PFN_glGetRenderbufferParameteriv)(GLenum target, GLenum pname, GLint *params);
static PFN_glGetRenderbufferParameteriv pglGetRenderbufferParameteriv = 0;
typedef void (APIENTRY *PFN_glGetSamplerParameterIiv)(GLuint sampler, GLenum pname, GLint *params);
static PFN_glGetSamplerParameterIiv pglGetSamplerParameterIiv = 0;
typedef void (APIENTRY *PFN_glGetSamplerParameterIuiv)(GLuint sampler, GLenum pname, GLuint *params);
static PFN_glGetSamplerParameterIuiv pglGetSamplerParameterIuiv = 0;
typedef void (APIENTRY *PFN_glGetSamplerParameterfv)(GLuint sampler, GLenum pname, GLfloat *params);
static PFN_glGetSamplerParameterfv pglGetSamplerParameterfv = 0;
typedef void (APIENTRY *PFN_glGetSamplerParameteriv)(GLuint sampler, GLenum pname, GLint *params);
static PFN_glGetSamplerParameteriv pglGetSamplerParameteriv = 0;
typedef void (APIENTRY *PFN_glGetShaderInfoLog)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog);
static PFN_glGetShaderInfoLog pglGetShaderInfoLog = 0;
typedef void (APIENTRY *PFN_glGetShaderSource)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source);
static PFN_glGetShaderSource pglGetShaderSource = 0;
typedef void (APIENTRY *PFN_glGetShaderSourceImpl)(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *source);
static PFN_glGetShaderSourceImpl pglGetShaderSourceImpl = 0;
typedef void (APIENTRY *PFN_glGetShaderiv)(GLuint shader, GLenum pname, GLint *params);
static PFN_glGetShaderiv pglGetShaderiv = 0;
typedef const GLubyte * (APIENTRY *PFN_glGetString)(GLenum name);
static PFN_glGetString pglGetString = 0;
typedef const GLubyte * (APIENTRY *PFN_glGetStringi)(GLenum name, GLuint index);
static PFN_glGetStringi pglGetStringi = 0;
typedef void (APIENTRY *PFN_glGetSynciv)(GLsync sync, GLenum pname, GLsizei count, GLsizei *length, GLint *values);
static PFN_glGetSynciv pglGetSynciv = 0;
typedef void (APIENTRY *PFN_glGetTexImage)(GLenum target, GLint level, GLenum format, GLenum type, void *pixels);
static PFN_glGetTexImage pglGetTexImage = 0;
typedef void (APIENTRY *PFN_glGetTexLevelParameteriv)(GLenum target, GLint level, GLenum pname, GLint *params);
static PFN_glGetTexLevelParameteriv pglGetTexLevelParameteriv = 0;
typedef void (APIENTRY *PFN_glGetTexParameteriv)(GLenum target, GLenum pname, GLint *params);
static PFN_glGetTexParameteriv pglGetTexParameteriv = 0;
typedef void (APIENTRY *PFN_glGetTextureLevelParameteriv)(GLuint texture, GLint level, GLenum pname, GLint *params);
static PFN_glGetTextureLevelParameteriv pglGetTextureLevelParameteriv = 0;
typedef void (APIENTRY *PFN_glGetTextureParameteriv)(GLuint texture, GLenum pname, GLint *params);
static PFN_glGetTextureParameteriv pglGetTextureParameteriv = 0;
typedef GLuint (APIENTRY *PFN_glGetUniformBlockIndex)(GLuint program, const GLchar *uniformBlockName);
static PFN_glGetUniformBlockIndex pglGetUniformBlockIndex = 0;
typedef GLint (APIENTRY *PFN_glGetUniformLocation)(GLuint program, const GLchar *name);
static PFN_glGetUniformLocation pglGetUniformLocation = 0;
typedef void (APIENTRY *PFN_glGetUniformfv)(GLuint program, GLint location, GLfloat *params);
static PFN_glGetUniformfv pglGetUniformfv = 0;
typedef void (APIENTRY *PFN_glGetUniformiv)(GLuint program, GLint location, GLint *params);
static PFN_glGetUniformiv pglGetUniformiv = 0;
typedef void (APIENTRY *PFN_glGetUniformuiv)(GLuint program, GLint location, GLuint *params);
static PFN_glGetUniformuiv pglGetUniformuiv = 0;
typedef void (APIENTRY *PFN_glHint)(GLenum target, GLenum mode);
static PFN_glHint pglHint = 0;
typedef GLboolean (APIENTRY *PFN_glIsEnabledi)(GLenum target, GLuint index);
static PFN_glIsEnabledi pglIsEnabledi = 0;
typedef GLboolean (APIENTRY *PFN_glIsFramebuffer)(GLuint framebuffer);
static PFN_glIsFramebuffer pglIsFramebuffer = 0;
typedef GLboolean (APIENTRY *PFN_glIsQuery)(GLuint id);
static PFN_glIsQuery pglIsQuery = 0;
typedef GLboolean (APIENTRY *PFN_glIsRenderbuffer)(GLuint renderbuffer);
static PFN_glIsRenderbuffer pglIsRenderbuffer = 0;
typedef GLboolean (APIENTRY *PFN_glIsSampler)(GLuint sampler);
static PFN_glIsSampler pglIsSampler = 0;
typedef GLboolean (APIENTRY *PFN_glIsSync)(GLsync sync);
static PFN_glIsSync pglIsSync = 0;
typedef void (APIENTRY *PFN_glLightf)(GLenum light, GLenum pname, GLfloat param);
static PFN_glLightf pglLightf = 0;
typedef void (APIENTRY *PFN_glLightfv)(GLenum light, GLenum pname, const GLfloat *params);
static PFN_glLightfv pglLightfv = 0;
typedef void (APIENTRY *PFN_glLineWidth)(GLfloat width);
static PFN_glLineWidth pglLineWidth = 0;
typedef void (APIENTRY *PFN_glLinkProgram)(GLuint program);
static PFN_glLinkProgram pglLinkProgram = 0;
typedef void (APIENTRY *PFN_glLoadIdentity)(void);
static PFN_glLoadIdentity pglLoadIdentity = 0;
typedef void * (APIENTRY *PFN_glMapBufferRange)(GLenum target, GLintptr offset, GLsizeiptr length, GLbitfield access);
static PFN_glMapBufferRange pglMapBufferRange = 0;
typedef void (APIENTRY *PFN_glMaterialf)(GLenum face, GLenum pname, GLfloat param);
static PFN_glMaterialf pglMaterialf = 0;
typedef void (APIENTRY *PFN_glMaterialfv)(GLenum face, GLenum pname, const GLfloat *params);
static PFN_glMaterialfv pglMaterialfv = 0;
typedef void (APIENTRY *PFN_glMatrixMode)(GLenum mode);
static PFN_glMatrixMode pglMatrixMode = 0;
typedef void (APIENTRY *PFN_glMinSampleShading)(GLfloat value);
static PFN_glMinSampleShading pglMinSampleShading = 0;
typedef void (APIENTRY *PFN_glMultMatrixf)(const GLfloat *m);
static PFN_glMultMatrixf pglMultMatrixf = 0;
typedef void (APIENTRY *PFN_glMultiDrawElementsBaseVertex)(const GLenum *mode, const GLsizei *count, GLenum type, const void *const *indices, GLsizei drawcount, const GLint *basevertex);
static PFN_glMultiDrawElementsBaseVertex pglMultiDrawElementsBaseVertex = 0;
typedef void (APIENTRY *PFN_glNormal3f)(GLfloat nx, GLfloat ny, GLfloat nz);
static PFN_glNormal3f pglNormal3f = 0;
typedef void (APIENTRY *PFN_glOrtho)(GLdouble l, GLdouble r, GLdouble b, GLdouble t, GLdouble n, GLdouble f);
static PFN_glOrtho pglOrtho = 0;
typedef void (APIENTRY *PFN_glPixelStorei)(GLenum pname, GLint param);
static PFN_glPixelStorei pglPixelStorei = 0;
typedef void (APIENTRY *PFN_glPointParameterf)(GLenum pname, GLfloat param);
static PFN_glPointParameterf pglPointParameterf = 0;
typedef void (APIENTRY *PFN_glPointParameterfv)(GLenum pname, const GLfloat *params);
static PFN_glPointParameterfv pglPointParameterfv = 0;
typedef void (APIENTRY *PFN_glPointParameteri)(GLenum pname, GLint param);
static PFN_glPointParameteri pglPointParameteri = 0;
typedef void (APIENTRY *PFN_glPointParameteriv)(GLenum pname, const GLint *params);
static PFN_glPointParameteriv pglPointParameteriv = 0;
typedef void (APIENTRY *PFN_glPolygonMode)(GLenum face, GLenum mode);
static PFN_glPolygonMode pglPolygonMode = 0;
typedef void (APIENTRY *PFN_glPolygonOffset)(GLfloat factor, GLfloat units);
static PFN_glPolygonOffset pglPolygonOffset = 0;
typedef void (APIENTRY *PFN_glPolygonOffsetClamp)(GLfloat factor, GLfloat units, GLfloat clamp);
static PFN_glPolygonOffsetClamp pglPolygonOffsetClamp = 0;
typedef void (APIENTRY *PFN_glPopMatrix)(void);
static PFN_glPopMatrix pglPopMatrix = 0;
typedef void (APIENTRY *PFN_glPushMatrix)(void);
static PFN_glPushMatrix pglPushMatrix = 0;
typedef void (APIENTRY *PFN_glQueryCounter)(GLuint id, GLenum target);
static PFN_glQueryCounter pglQueryCounter = 0;
typedef void (APIENTRY *PFN_glReadBuffer)(GLenum src);
static PFN_glReadBuffer pglReadBuffer = 0;
typedef void (APIENTRY *PFN_glReadPixels)(GLint x, GLint y, GLsizei width, GLsizei height, GLenum format, GLenum type, void *pixels);
static PFN_glReadPixels pglReadPixels = 0;
typedef void (APIENTRY *PFN_glRenderbufferStorage)(GLenum target, GLenum internalformat, GLsizei width, GLsizei height);
static PFN_glRenderbufferStorage pglRenderbufferStorage = 0;
typedef void (APIENTRY *PFN_glRenderbufferStorageMultisample)(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height);
static PFN_glRenderbufferStorageMultisample pglRenderbufferStorageMultisample = 0;
typedef void (APIENTRY *PFN_glRotatef)(GLfloat angle, GLfloat x, GLfloat y, GLfloat z);
static PFN_glRotatef pglRotatef = 0;
typedef void (APIENTRY *PFN_glSampleCoverage)(GLfloat value, GLboolean invert);
static PFN_glSampleCoverage pglSampleCoverage = 0;
typedef void (APIENTRY *PFN_glSamplerParameterIiv)(GLuint sampler, GLenum pname, const GLint *param);
static PFN_glSamplerParameterIiv pglSamplerParameterIiv = 0;
typedef void (APIENTRY *PFN_glSamplerParameterIuiv)(GLuint sampler, GLenum pname, const GLuint *param);
static PFN_glSamplerParameterIuiv pglSamplerParameterIuiv = 0;
typedef void (APIENTRY *PFN_glSamplerParameterf)(GLuint sampler, GLenum pname, GLfloat param);
static PFN_glSamplerParameterf pglSamplerParameterf = 0;
typedef void (APIENTRY *PFN_glSamplerParameterfv)(GLuint sampler, GLenum pname, const GLfloat *param);
static PFN_glSamplerParameterfv pglSamplerParameterfv = 0;
typedef void (APIENTRY *PFN_glSamplerParameteri)(GLuint sampler, GLenum pname, GLint param);
static PFN_glSamplerParameteri pglSamplerParameteri = 0;
typedef void (APIENTRY *PFN_glSamplerParameteriv)(GLuint sampler, GLenum pname, const GLint *param);
static PFN_glSamplerParameteriv pglSamplerParameteriv = 0;
typedef void (APIENTRY *PFN_glScalef)(GLfloat x, GLfloat y, GLfloat z);
static PFN_glScalef pglScalef = 0;
typedef void (APIENTRY *PFN_glScissor)(GLint x, GLint y, GLsizei width, GLsizei height);
static PFN_glScissor pglScissor = 0;
typedef void (APIENTRY *PFN_glShadeModel)(GLenum mode);
static PFN_glShadeModel pglShadeModel = 0;
typedef void (APIENTRY *PFN_glShaderSource)(GLuint shader, GLsizei count, const GLchar *const *string, const GLint *length);
static PFN_glShaderSource pglShaderSource = 0;
typedef void (APIENTRY *PFN_glStencilFunc)(GLenum func, GLint ref, GLuint mask);
static PFN_glStencilFunc pglStencilFunc = 0;
typedef void (APIENTRY *PFN_glStencilFuncSeparate)(GLenum face, GLenum func, GLint ref, GLuint mask);
static PFN_glStencilFuncSeparate pglStencilFuncSeparate = 0;
typedef void (APIENTRY *PFN_glStencilMask)(GLuint mask);
static PFN_glStencilMask pglStencilMask = 0;
typedef void (APIENTRY *PFN_glStencilMaskSeparate)(GLenum face, GLuint mask);
static PFN_glStencilMaskSeparate pglStencilMaskSeparate = 0;
typedef void (APIENTRY *PFN_glStencilOp)(GLenum sfail, GLenum dpfail, GLenum dppass);
static PFN_glStencilOp pglStencilOp = 0;
typedef void (APIENTRY *PFN_glStencilOpSeparate)(GLenum face, GLenum sfail, GLenum dpfail, GLenum dppass);
static PFN_glStencilOpSeparate pglStencilOpSeparate = 0;
typedef void (APIENTRY *PFN_glTexBuffer)(GLenum target, GLenum internalformat, GLuint buffer);
static PFN_glTexBuffer pglTexBuffer = 0;
typedef void (APIENTRY *PFN_glTexBufferARB)(GLenum target, GLenum internalformat, GLuint buffer);
static PFN_glTexBufferARB pglTexBufferARB = 0;
typedef void (APIENTRY *PFN_glTexBufferRange)(GLenum target, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size);
static PFN_glTexBufferRange pglTexBufferRange = 0;
typedef void (APIENTRY *PFN_glTexBufferRangeARB)(GLenum target, GLenum internalformat, GLuint buffer, GLintptr offset, GLsizeiptr size);
static PFN_glTexBufferRangeARB pglTexBufferRangeARB = 0;
typedef void (APIENTRY *PFN_glTexImage2D)(GLenum target, GLint level, GLint internalformat, GLsizei width, GLsizei height, GLint border, GLenum format, GLenum type, const void *pixels);
static PFN_glTexImage2D pglTexImage2D = 0;
typedef void (APIENTRY *PFN_glTexImage2DMultisample)(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations);
static PFN_glTexImage2DMultisample pglTexImage2DMultisample = 0;
typedef void (APIENTRY *PFN_glTexImage3D)(GLenum target, GLint level, GLint internalformat, GLsizei w, GLsizei h, GLsizei d, GLint border, GLenum format, GLenum type, const void *pixels);
static PFN_glTexImage3D pglTexImage3D = 0;
typedef void (APIENTRY *PFN_glTexImage3DMultisample)(GLenum target, GLsizei samples, GLenum internalformat, GLsizei w, GLsizei h, GLsizei d, GLboolean fixedsamplelocations);
static PFN_glTexImage3DMultisample pglTexImage3DMultisample = 0;
typedef void (APIENTRY *PFN_glTexParameterf)(GLenum target, GLenum pname, GLfloat param);
static PFN_glTexParameterf pglTexParameterf = 0;
typedef void (APIENTRY *PFN_glTexParameteri)(GLenum target, GLenum pname, GLint param);
static PFN_glTexParameteri pglTexParameteri = 0;
typedef void (APIENTRY *PFN_glTexStorage1D)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width);
static PFN_glTexStorage1D pglTexStorage1D = 0;
typedef void (APIENTRY *PFN_glTexStorage2D)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height);
static PFN_glTexStorage2D pglTexStorage2D = 0;
typedef void (APIENTRY *PFN_glTexStorage2DMultisample)(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations);
static PFN_glTexStorage2DMultisample pglTexStorage2DMultisample = 0;
typedef void (APIENTRY *PFN_glTexStorage3D)(GLenum target, GLsizei levels, GLenum internalformat, GLsizei width, GLsizei height, GLsizei depth);
static PFN_glTexStorage3D pglTexStorage3D = 0;
typedef void (APIENTRY *PFN_glTexStorage3DMultisample)(GLenum target, GLsizei samples, GLenum internalformat, GLsizei w, GLsizei h, GLsizei d, GLboolean fixedsamplelocations);
static PFN_glTexStorage3DMultisample pglTexStorage3DMultisample = 0;
typedef void (APIENTRY *PFN_glTexSubImage2D)(GLenum target, GLint level, GLint xoff, GLint yoff, GLsizei w, GLsizei h, GLenum format, GLenum type, const void *pixels);
static PFN_glTexSubImage2D pglTexSubImage2D = 0;
typedef void (APIENTRY *PFN_glTexSubImage3D)(GLenum target, GLint level, GLint xoff, GLint yoff, GLint zoff, GLsizei w, GLsizei h, GLsizei d, GLenum format, GLenum type, const void *pixels);
static PFN_glTexSubImage3D pglTexSubImage3D = 0;
typedef void (APIENTRY *PFN_glTextureBarrier)(void);
static PFN_glTextureBarrier pglTextureBarrier = 0;
typedef void (APIENTRY *PFN_glTextureBarrierNV)(void);
static PFN_glTextureBarrierNV pglTextureBarrierNV = 0;
typedef void (APIENTRY *PFN_glTransformFeedbackVaryings)(GLuint program, GLsizei count, const GLchar *const *varyings, GLenum bufferMode);
static PFN_glTransformFeedbackVaryings pglTransformFeedbackVaryings = 0;
typedef void (APIENTRY *PFN_glTranslatef)(GLfloat x, GLfloat y, GLfloat z);
static PFN_glTranslatef pglTranslatef = 0;
typedef void (APIENTRY *PFN_glUniform1f)(GLint location, GLfloat v0);
static PFN_glUniform1f pglUniform1f = 0;
typedef void (APIENTRY *PFN_glUniform1fv)(GLint location, GLsizei count, const GLfloat *value);
static PFN_glUniform1fv pglUniform1fv = 0;
typedef void (APIENTRY *PFN_glUniform1i)(GLint location, GLint v0);
static PFN_glUniform1i pglUniform1i = 0;
typedef void (APIENTRY *PFN_glUniform1iv)(GLint location, GLsizei count, const GLint *value);
static PFN_glUniform1iv pglUniform1iv = 0;
typedef void (APIENTRY *PFN_glUniform1uiv)(GLint location, GLsizei count, const GLuint *value);
static PFN_glUniform1uiv pglUniform1uiv = 0;
typedef void (APIENTRY *PFN_glUniform2f)(GLint location, GLfloat v0, GLfloat v1);
static PFN_glUniform2f pglUniform2f = 0;
typedef void (APIENTRY *PFN_glUniform2fv)(GLint location, GLsizei count, const GLfloat *value);
static PFN_glUniform2fv pglUniform2fv = 0;
typedef void (APIENTRY *PFN_glUniform2i)(GLint location, GLint v0, GLint v1);
static PFN_glUniform2i pglUniform2i = 0;
typedef void (APIENTRY *PFN_glUniform2iv)(GLint location, GLsizei count, const GLint *value);
static PFN_glUniform2iv pglUniform2iv = 0;
typedef void (APIENTRY *PFN_glUniform2uiv)(GLint location, GLsizei count, const GLuint *value);
static PFN_glUniform2uiv pglUniform2uiv = 0;
typedef void (APIENTRY *PFN_glUniform3f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2);
static PFN_glUniform3f pglUniform3f = 0;
typedef void (APIENTRY *PFN_glUniform3fv)(GLint location, GLsizei count, const GLfloat *value);
static PFN_glUniform3fv pglUniform3fv = 0;
typedef void (APIENTRY *PFN_glUniform3i)(GLint location, GLint v0, GLint v1, GLint v2);
static PFN_glUniform3i pglUniform3i = 0;
typedef void (APIENTRY *PFN_glUniform3iv)(GLint location, GLsizei count, const GLint *value);
static PFN_glUniform3iv pglUniform3iv = 0;
typedef void (APIENTRY *PFN_glUniform3uiv)(GLint location, GLsizei count, const GLuint *value);
static PFN_glUniform3uiv pglUniform3uiv = 0;
typedef void (APIENTRY *PFN_glUniform4f)(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3);
static PFN_glUniform4f pglUniform4f = 0;
typedef void (APIENTRY *PFN_glUniform4fv)(GLint location, GLsizei count, const GLfloat *value);
static PFN_glUniform4fv pglUniform4fv = 0;
typedef void (APIENTRY *PFN_glUniform4i)(GLint location, GLint v0, GLint v1, GLint v2, GLint v3);
static PFN_glUniform4i pglUniform4i = 0;
typedef void (APIENTRY *PFN_glUniform4iv)(GLint location, GLsizei count, const GLint *value);
static PFN_glUniform4iv pglUniform4iv = 0;
typedef void (APIENTRY *PFN_glUniform4uiv)(GLint location, GLsizei count, const GLuint *value);
static PFN_glUniform4uiv pglUniform4uiv = 0;
typedef void (APIENTRY *PFN_glUniformBlockBinding)(GLuint program, GLuint uniformBlockIndex, GLuint uniformBlockBinding);
static PFN_glUniformBlockBinding pglUniformBlockBinding = 0;
typedef void (APIENTRY *PFN_glUniformMatrix2fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
static PFN_glUniformMatrix2fv pglUniformMatrix2fv = 0;
typedef void (APIENTRY *PFN_glUniformMatrix3fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
static PFN_glUniformMatrix3fv pglUniformMatrix3fv = 0;
typedef void (APIENTRY *PFN_glUniformMatrix4fv)(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value);
static PFN_glUniformMatrix4fv pglUniformMatrix4fv = 0;
typedef void (APIENTRY *PFN_glUseProgram)(GLuint program);
static PFN_glUseProgram pglUseProgram = 0;
typedef void (APIENTRY *PFN_glValidateProgram)(GLuint program);
static PFN_glValidateProgram pglValidateProgram = 0;
typedef GLint (APIENTRY *PFN_glVersionMode)(void);
static PFN_glVersionMode pglVersionMode = 0;
typedef void (APIENTRY *PFN_glVertex2f)(GLfloat x, GLfloat y);
static PFN_glVertex2f pglVertex2f = 0;
typedef void (APIENTRY *PFN_glVertex3f)(GLfloat x, GLfloat y, GLfloat z);
static PFN_glVertex3f pglVertex3f = 0;
typedef void (APIENTRY *PFN_glVertexAttrib1d)(GLuint index, GLdouble x);
static PFN_glVertexAttrib1d pglVertexAttrib1d = 0;
typedef void (APIENTRY *PFN_glVertexAttrib1dv)(GLuint index, const GLdouble *v);
static PFN_glVertexAttrib1dv pglVertexAttrib1dv = 0;
typedef void (APIENTRY *PFN_glVertexAttrib1f)(GLuint index, GLfloat x);
static PFN_glVertexAttrib1f pglVertexAttrib1f = 0;
typedef void (APIENTRY *PFN_glVertexAttrib1fv)(GLuint index, const GLfloat *v);
static PFN_glVertexAttrib1fv pglVertexAttrib1fv = 0;
typedef void (APIENTRY *PFN_glVertexAttrib2d)(GLuint index, GLdouble x, GLdouble y);
static PFN_glVertexAttrib2d pglVertexAttrib2d = 0;
typedef void (APIENTRY *PFN_glVertexAttrib2dv)(GLuint index, const GLdouble *v);
static PFN_glVertexAttrib2dv pglVertexAttrib2dv = 0;
typedef void (APIENTRY *PFN_glVertexAttrib2f)(GLuint index, GLfloat x, GLfloat y);
static PFN_glVertexAttrib2f pglVertexAttrib2f = 0;
typedef void (APIENTRY *PFN_glVertexAttrib2fv)(GLuint index, const GLfloat *v);
static PFN_glVertexAttrib2fv pglVertexAttrib2fv = 0;
typedef void (APIENTRY *PFN_glVertexAttrib3d)(GLuint index, GLdouble x, GLdouble y, GLdouble z);
static PFN_glVertexAttrib3d pglVertexAttrib3d = 0;
typedef void (APIENTRY *PFN_glVertexAttrib3dv)(GLuint index, const GLdouble *v);
static PFN_glVertexAttrib3dv pglVertexAttrib3dv = 0;
typedef void (APIENTRY *PFN_glVertexAttrib3f)(GLuint index, GLfloat x, GLfloat y, GLfloat z);
static PFN_glVertexAttrib3f pglVertexAttrib3f = 0;
typedef void (APIENTRY *PFN_glVertexAttrib3fv)(GLuint index, const GLfloat *v);
static PFN_glVertexAttrib3fv pglVertexAttrib3fv = 0;
typedef void (APIENTRY *PFN_glVertexAttrib4d)(GLuint index, GLdouble x, GLdouble y, GLdouble z, GLdouble w);
static PFN_glVertexAttrib4d pglVertexAttrib4d = 0;
typedef void (APIENTRY *PFN_glVertexAttrib4dv)(GLuint index, const GLdouble *v);
static PFN_glVertexAttrib4dv pglVertexAttrib4dv = 0;
typedef void (APIENTRY *PFN_glVertexAttrib4f)(GLuint index, GLfloat x, GLfloat y, GLfloat z, GLfloat w);
static PFN_glVertexAttrib4f pglVertexAttrib4f = 0;
typedef void (APIENTRY *PFN_glVertexAttrib4fv)(GLuint index, const GLfloat *v);
static PFN_glVertexAttrib4fv pglVertexAttrib4fv = 0;
typedef void (APIENTRY *PFN_glVertexAttribDivisor)(GLuint index, GLuint divisor);
static PFN_glVertexAttribDivisor pglVertexAttribDivisor = 0;
typedef void (APIENTRY *PFN_glVertexAttribI4i)(GLuint index, GLint x, GLint y, GLint z, GLint w);
static PFN_glVertexAttribI4i pglVertexAttribI4i = 0;
typedef void (APIENTRY *PFN_glVertexAttribI4iv)(GLuint index, const GLint *v);
static PFN_glVertexAttribI4iv pglVertexAttribI4iv = 0;
typedef void (APIENTRY *PFN_glVertexAttribI4ui)(GLuint index, GLuint x, GLuint y, GLuint z, GLuint w);
static PFN_glVertexAttribI4ui pglVertexAttribI4ui = 0;
typedef void (APIENTRY *PFN_glVertexAttribI4uiv)(GLuint index, const GLuint *v);
static PFN_glVertexAttribI4uiv pglVertexAttribI4uiv = 0;
typedef void (APIENTRY *PFN_glVertexAttribIPointer)(GLuint index, GLint size, GLenum type, GLsizei stride, const void *pointer);
static PFN_glVertexAttribIPointer pglVertexAttribIPointer = 0;
typedef void (APIENTRY *PFN_glVertexAttribPointer)(GLuint index, GLint size, GLenum type, GLboolean normalized, GLsizei stride, const void *pointer);
static PFN_glVertexAttribPointer pglVertexAttribPointer = 0;
typedef void (APIENTRY *PFN_glViewport)(GLint x, GLint y, GLsizei width, GLsizei height);
static PFN_glViewport pglViewport = 0;
typedef void (APIENTRY *PFN_glWaitSync)(GLsync sync, GLbitfield flags, GLuint64 timeout);
static PFN_glWaitSync pglWaitSync = 0;
typedef void * (APIENTRY *PFN_glXChooseFBConfig)(void *dpy, int screen, const int *attrib_list, int *nelements);
static PFN_glXChooseFBConfig pglXChooseFBConfig = 0;
typedef void * (APIENTRY *PFN_glXChooseVisual)(void *dpy, int screen, int *attribList);
static PFN_glXChooseVisual pglXChooseVisual = 0;
typedef void * (APIENTRY *PFN_glXCreateContext)(void *dpy, void *vis, void *shareList, int direct);
static PFN_glXCreateContext pglXCreateContext = 0;
typedef void * (APIENTRY *PFN_glXCreateContextAttribsARB)(void *dpy, void *config, void *share_context, int direct, const int *attrib_list);
static PFN_glXCreateContextAttribsARB pglXCreateContextAttribsARB = 0;
typedef void (APIENTRY *PFN_glXDestroyContext)(void *dpy, void *ctx);
static PFN_glXDestroyContext pglXDestroyContext = 0;
typedef const char * (APIENTRY *PFN_glXGetClientString)(void *dpy, int name);
static PFN_glXGetClientString pglXGetClientString = 0;
typedef int (APIENTRY *PFN_glXGetConfig)(void *dpy, void *vis, int attribute, int *value);
static PFN_glXGetConfig pglXGetConfig = 0;
typedef void * (APIENTRY *PFN_glXGetCurrentContext)(void);
static PFN_glXGetCurrentContext pglXGetCurrentContext = 0;
typedef unsigned long (APIENTRY *PFN_glXGetCurrentDrawable)(void);
static PFN_glXGetCurrentDrawable pglXGetCurrentDrawable = 0;
typedef int (APIENTRY *PFN_glXGetFBConfigAttrib)(void *dpy, void *config, int attribute, int *value);
static PFN_glXGetFBConfigAttrib pglXGetFBConfigAttrib = 0;
typedef void * (APIENTRY *PFN_glXGetFBConfigs)(void *dpy, int screen, int *nelements);
static PFN_glXGetFBConfigs pglXGetFBConfigs = 0;
typedef void * (APIENTRY *PFN_glXGetVisualFromFBConfig)(void *dpy, void *config);
static PFN_glXGetVisualFromFBConfig pglXGetVisualFromFBConfig = 0;
typedef int (APIENTRY *PFN_glXIsDirect)(void *dpy, void *ctx);
static PFN_glXIsDirect pglXIsDirect = 0;
typedef int (APIENTRY *PFN_glXMakeContextCurrent)(void *dpy, unsigned long draw, unsigned long read, void *ctx);
static PFN_glXMakeContextCurrent pglXMakeContextCurrent = 0;
typedef int (APIENTRY *PFN_glXMakeCurrent)(void *dpy, unsigned long drawable, void *ctx);
static PFN_glXMakeCurrent pglXMakeCurrent = 0;
typedef int (APIENTRY *PFN_glXQueryExtension)(void *dpy, int *errorBase, int *eventBase);
static PFN_glXQueryExtension pglXQueryExtension = 0;
typedef const char * (APIENTRY *PFN_glXQueryExtensionsString)(void *dpy, int screen);
static PFN_glXQueryExtensionsString pglXQueryExtensionsString = 0;
typedef const char * (APIENTRY *PFN_glXQueryServerString)(void *dpy, int screen, int name);
static PFN_glXQueryServerString pglXQueryServerString = 0;
typedef int (APIENTRY *PFN_glXQueryVersion)(void *dpy, int *major, int *minor);
static PFN_glXQueryVersion pglXQueryVersion = 0;
typedef void (APIENTRY *PFN_glXSwapBuffers)(void *dpy, unsigned long drawable);
static PFN_glXSwapBuffers pglXSwapBuffers = 0;
typedef int (APIENTRY *PFN_glXSwapIntervalEXT)(void *dpy, unsigned long drawable, int interval);
static PFN_glXSwapIntervalEXT pglXSwapIntervalEXT = 0;
typedef void (APIENTRY *PFN_glXWaitGL)(void);
static PFN_glXWaitGL pglXWaitGL = 0;
typedef void (APIENTRY *PFN_glXWaitX)(void);
static PFN_glXWaitX pglXWaitX = 0;
typedef long long (APIENTRY *PFN_traceProc)(const char *name, long long hit);
static PFN_traceProc ptraceProc = 0;

static void load_all(void) {
    LOADGL(glActiveTexture);
    LOADGL(glAttachShader);
    LOADGL(glBegin);
    LOADGL(glBeginQuery);
    LOADGL(glBeginTransformFeedback);
    LOADGL(glBindAttribLocation);
    LOADGL(glBindBuffer);
    LOADGL(glBindBufferBase);
    LOADGL(glBindBufferRange);
    LOADGL(glBindFragDataLocation);
    LOADGL(glBindFramebuffer);
    LOADGL(glBindRenderbuffer);
    LOADGL(glBindSampler);
    LOADGL(glBindTexture);
    LOADGL(glBindVertexArray);
    LOADGL(glBlendColor);
    LOADGL(glBlendEquation);
    LOADGL(glBlendEquationSeparate);
    LOADGL(glBlendEquationSeparatei);
    LOADGL(glBlendEquationi);
    LOADGL(glBlendFunc);
    LOADGL(glBlendFuncSeparate);
    LOADGL(glBlendFuncSeparatei);
    LOADGL(glBlendFunci);
    LOADGL(glBlitFramebuffer);
    LOADGL(glBufferData);
    LOADGL(glBufferStorage);
    LOADGL(glBufferSubData);
    LOADGL(glCheckFramebufferStatus);
    LOADGL(glClear);
    LOADGL(glClearBufferfi);
    LOADGL(glClearBufferfv);
    LOADGL(glClearBufferiv);
    LOADGL(glClearBufferuiv);
    LOADGL(glClearColor);
    LOADGL(glClearDepth);
    LOADGL(glClientWaitSync);
    LOADGL(glColor3f);
    LOADGL(glColor4f);
    LOADGL(glColorMask);
    LOADGL(glColorMaski);
    LOADGL(glColorMaterial);
    LOADGL(glCompileShader);
    LOADGL(glCompressedTexImage2D);
    LOADGL(glCompressedTexImage3D);
    LOADGL(glCompressedTexSubImage2D);
    LOADGL(glCompressedTexSubImage3D);
    LOADGL(glCopyBufferSubData);
    LOADGL(glCreateProgram);
    LOADGL(glCreateShader);
    LOADGL(glCullFace);
    LOADGL(glDebugMessageCallback);
    LOADGL(glDebugMessageControl);
    LOADGL(glDebugMessageInsert);
    LOADGL(glDeleteProgram);
    LOADGL(glDeleteShader);
    LOADGL(glDeleteSync);
    LOADGL(glDepthFunc);
    LOADGL(glDepthMask);
    LOADGL(glDepthRange);
    LOADGL(glDetachShader);
    LOADGL(glDisable);
    LOADGL(glDisableVertexAttribArray);
    LOADGL(glDisablei);
    LOADGL(glDrawArrays);
    LOADGL(glDrawArraysInstanced);
    LOADGL(glDrawArraysInstancedBaseInstance);
    LOADGL(glDrawBuffers);
    LOADGL(glDrawElements);
    LOADGL(glDrawElementsBaseVertex);
    LOADGL(glDrawElementsInstanced);
    LOADGL(glDrawElementsInstancedBaseVertex);
    LOADGL(glDrawElementsInstancedBaseVertexBaseInstance);
    LOADGL(glDrawRangeElements);
    LOADGL(glDrawRangeElementsBaseVertex);
    LOADGL(glEnable);
    LOADGL(glEnableVertexAttribArray);
    LOADGL(glEnablei);
    LOADGL(glEnd);
    LOADGL(glEndQuery);
    LOADGL(glEndTransformFeedback);
    LOADGL(glFenceSync);
    LOADGL(glFinalCombinerInputNV);
    LOADGL(glFinish);
    LOADGL(glFlush);
    LOADGL(glFramebufferRenderbuffer);
    LOADGL(glFramebufferTexture);
    LOADGL(glFramebufferTexture1D);
    LOADGL(glFramebufferTexture2D);
    LOADGL(glFramebufferTexture3D);
    LOADGL(glFramebufferTextureLayer);
    LOADGL(glFrontFace);
    LOADGL(glFrustum);
    LOADGL(glGenBuffers);
    LOADGL(glDeleteBuffers);
    LOADGL(glGenFramebuffers);
    LOADGL(glDeleteFramebuffers);
    LOADGL(glGenQueries);
    LOADGL(glDeleteQueries);
    LOADGL(glGenRenderbuffers);
    LOADGL(glDeleteRenderbuffers);
    LOADGL(glGenSamplers);
    LOADGL(glDeleteSamplers);
    LOADGL(glGenTextures);
    LOADGL(glDeleteTextures);
    LOADGL(glGenVertexArrays);
    LOADGL(glDeleteVertexArrays);
    LOADGL(glGenerateMipmap);
    LOADGL(glGetActiveUniform);
    LOADGL(glGetActiveUniformBlockName);
    LOADGL(glGetActiveUniformBlockiv);
    LOADGL(glGetAttachedShaders);
    LOADGL(glGetAttribLocation);
    LOADGL(glGetBooleani_v);
    LOADGL(glGetBufferParameteriv);
    LOADGL(glGetBufferSubData);
    LOADGL(glGetCompressedTexImage);
    LOADGL(glGetDebugMessageLog);
    LOADGL(glGetError);
    LOADGL(glGetFloatv);
    LOADGL(glGetFragDataIndex);
    LOADGL(glGetFramebufferAttachmentParameteriv);
    LOADGL(glGetIntegeri_v);
    LOADGL(glGetIntegerv);
    LOADGL(glGetMultisamplefv);
    LOADGL(glGetProgramInfoLog);
    LOADGL(glGetProgramiv);
    LOADGL(glGetQueryObjectiv);
    LOADGL(glGetQueryObjectui64v);
    LOADGL(glGetQueryObjectuiv);
    LOADGL(glGetQueryiv);
    LOADGL(glGetRenderbufferParameteriv);
    LOADGL(glGetSamplerParameterIiv);
    LOADGL(glGetSamplerParameterIuiv);
    LOADGL(glGetSamplerParameterfv);
    LOADGL(glGetSamplerParameteriv);
    LOADGL(glGetShaderInfoLog);
    LOADGL(glGetShaderSource);
    LOADGL(glGetShaderSourceImpl);
    LOADGL(glGetShaderiv);
    LOADGL(glGetString);
    LOADGL(glGetStringi);
    LOADGL(glGetSynciv);
    LOADGL(glGetTexImage);
    LOADGL(glGetTexLevelParameteriv);
    LOADGL(glGetTexParameteriv);
    LOADGL(glGetTextureLevelParameteriv);
    LOADGL(glGetTextureParameteriv);
    LOADGL(glGetUniformBlockIndex);
    LOADGL(glGetUniformLocation);
    LOADGL(glGetUniformfv);
    LOADGL(glGetUniformiv);
    LOADGL(glGetUniformuiv);
    LOADGL(glHint);
    LOADGL(glIsEnabledi);
    LOADGL(glIsFramebuffer);
    LOADGL(glIsQuery);
    LOADGL(glIsRenderbuffer);
    LOADGL(glIsSampler);
    LOADGL(glIsSync);
    LOADGL(glLightf);
    LOADGL(glLightfv);
    LOADGL(glLineWidth);
    LOADGL(glLinkProgram);
    LOADGL(glLoadIdentity);
    LOADGL(glMapBufferRange);
    LOADGL(glMaterialf);
    LOADGL(glMaterialfv);
    LOADGL(glMatrixMode);
    LOADGL(glMinSampleShading);
    LOADGL(glMultMatrixf);
    LOADGL(glMultiDrawElementsBaseVertex);
    LOADGL(glNormal3f);
    LOADGL(glOrtho);
    LOADGL(glPixelStorei);
    LOADGL(glPointParameterf);
    LOADGL(glPointParameterfv);
    LOADGL(glPointParameteri);
    LOADGL(glPointParameteriv);
    LOADGL(glPolygonMode);
    LOADGL(glPolygonOffset);
    LOADGL(glPolygonOffsetClamp);
    LOADGL(glPopMatrix);
    LOADGL(glPushMatrix);
    LOADGL(glQueryCounter);
    LOADGL(glReadBuffer);
    LOADGL(glReadPixels);
    LOADGL(glRenderbufferStorage);
    LOADGL(glRenderbufferStorageMultisample);
    LOADGL(glRotatef);
    LOADGL(glSampleCoverage);
    LOADGL(glSamplerParameterIiv);
    LOADGL(glSamplerParameterIuiv);
    LOADGL(glSamplerParameterf);
    LOADGL(glSamplerParameterfv);
    LOADGL(glSamplerParameteri);
    LOADGL(glSamplerParameteriv);
    LOADGL(glScalef);
    LOADGL(glScissor);
    LOADGL(glShadeModel);
    LOADGL(glShaderSource);
    LOADGL(glStencilFunc);
    LOADGL(glStencilFuncSeparate);
    LOADGL(glStencilMask);
    LOADGL(glStencilMaskSeparate);
    LOADGL(glStencilOp);
    LOADGL(glStencilOpSeparate);
    LOADGL(glTexBuffer);
    LOADGL(glTexBufferARB);
    LOADGL(glTexBufferRange);
    LOADGL(glTexBufferRangeARB);
    LOADGL(glTexImage2D);
    LOADGL(glTexImage2DMultisample);
    LOADGL(glTexImage3D);
    LOADGL(glTexImage3DMultisample);
    LOADGL(glTexParameterf);
    LOADGL(glTexParameteri);
    LOADGL(glTexStorage1D);
    LOADGL(glTexStorage2D);
    LOADGL(glTexStorage2DMultisample);
    LOADGL(glTexStorage3D);
    LOADGL(glTexStorage3DMultisample);
    LOADGL(glTexSubImage2D);
    LOADGL(glTexSubImage3D);
    LOADGL(glTextureBarrier);
    LOADGL(glTextureBarrierNV);
    LOADGL(glTransformFeedbackVaryings);
    LOADGL(glTranslatef);
    LOADGL(glUniform1f);
    LOADGL(glUniform1fv);
    LOADGL(glUniform1i);
    LOADGL(glUniform1iv);
    LOADGL(glUniform1uiv);
    LOADGL(glUniform2f);
    LOADGL(glUniform2fv);
    LOADGL(glUniform2i);
    LOADGL(glUniform2iv);
    LOADGL(glUniform2uiv);
    LOADGL(glUniform3f);
    LOADGL(glUniform3fv);
    LOADGL(glUniform3i);
    LOADGL(glUniform3iv);
    LOADGL(glUniform3uiv);
    LOADGL(glUniform4f);
    LOADGL(glUniform4fv);
    LOADGL(glUniform4i);
    LOADGL(glUniform4iv);
    LOADGL(glUniform4uiv);
    LOADGL(glUniformBlockBinding);
    LOADGL(glUniformMatrix2fv);
    LOADGL(glUniformMatrix3fv);
    LOADGL(glUniformMatrix4fv);
    LOADGL(glUseProgram);
    LOADGL(glValidateProgram);
    LOADGL(glVersionMode);
    LOADGL(glVertex2f);
    LOADGL(glVertex3f);
    LOADGL(glVertexAttrib1d);
    LOADGL(glVertexAttrib1dv);
    LOADGL(glVertexAttrib1f);
    LOADGL(glVertexAttrib1fv);
    LOADGL(glVertexAttrib2d);
    LOADGL(glVertexAttrib2dv);
    LOADGL(glVertexAttrib2f);
    LOADGL(glVertexAttrib2fv);
    LOADGL(glVertexAttrib3d);
    LOADGL(glVertexAttrib3dv);
    LOADGL(glVertexAttrib3f);
    LOADGL(glVertexAttrib3fv);
    LOADGL(glVertexAttrib4d);
    LOADGL(glVertexAttrib4dv);
    LOADGL(glVertexAttrib4f);
    LOADGL(glVertexAttrib4fv);
    LOADGL(glVertexAttribDivisor);
    LOADGL(glVertexAttribI4i);
    LOADGL(glVertexAttribI4iv);
    LOADGL(glVertexAttribI4ui);
    LOADGL(glVertexAttribI4uiv);
    LOADGL(glVertexAttribIPointer);
    LOADGL(glVertexAttribPointer);
    LOADGL(glViewport);
    LOADGL(glWaitSync);
    LOADGL(glXChooseFBConfig);
    LOADGL(glXChooseVisual);
    LOADGL(glXCreateContext);
    LOADGL(glXCreateContextAttribsARB);
    LOADGL(glXDestroyContext);
    LOADGL(glXGetClientString);
    LOADGL(glXGetConfig);
    LOADGL(glXGetCurrentContext);
    LOADGL(glXGetCurrentDrawable);
    LOADGL(glXGetFBConfigAttrib);
    LOADGL(glXGetFBConfigs);
    LOADGL(glXGetVisualFromFBConfig);
    LOADGL(glXIsDirect);
    LOADGL(glXMakeContextCurrent);
    LOADGL(glXMakeCurrent);
    LOADGL(glXQueryExtension);
    LOADGL(glXQueryExtensionsString);
    LOADGL(glXQueryServerString);
    LOADGL(glXQueryVersion);
    LOADGL(glXSwapBuffers);
    LOADGL(glXSwapIntervalEXT);
    LOADGL(glXWaitGL);
    LOADGL(glXWaitX);
    LOADGL(traceProc);
    LOADGL(glUnmapBuffer); /* hygiene only, not a bridge handler */
}
static void setup_gl(void) {
    if (pglGenTextures && pglBindTexture && pglTexImage2D && pglTexParameteri) {
        pglGenTextures(1,&g_tex); pglBindTexture(GL_TEXTURE_2D,g_tex);
        pglTexImage2D(GL_TEXTURE_2D,0,GL_RGBA8,64,64,0,GL_RGBA,GL_UNSIGNED_BYTE,s_px);
        pglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
        pglTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
        pglGenTextures(1,&g_tex1d); pglBindTexture(GL_TEXTURE_1D,g_tex1d);
        if (pglTexStorage1D) pglTexStorage1D(GL_TEXTURE_1D,1,GL_RGBA8,64);
        pglGenTextures(1,&g_tex3d); pglBindTexture(GL_TEXTURE_3D,g_tex3d);
        if (pglTexImage3D) pglTexImage3D(GL_TEXTURE_3D,0,GL_RGBA8,8,8,8,0,GL_RGBA,GL_UNSIGNED_BYTE,s_px3d);
        pglBindTexture(GL_TEXTURE_2D,g_tex);
    }
    if (pglGenBuffers && pglBindBuffer && pglBufferData) {
        pglGenBuffers(1,&g_buf); pglBindBuffer(GL_ARRAY_BUFFER,g_buf);
        pglBufferData(GL_ARRAY_BUFFER,sizeof(tri),tri,GL_STATIC_DRAW);
        pglGenBuffers(1,&g_ibuf); pglBindBuffer(GL_ELEMENT_ARRAY_BUFFER,g_ibuf);
        pglBufferData(GL_ELEMENT_ARRAY_BUFFER,sizeof(idx6),idx6,GL_STATIC_DRAW);
    }
    if (pglCreateShader && pglShaderSource && pglCompileShader &&
        pglCreateProgram && pglAttachShader && pglLinkProgram) {
        g_vs=pglCreateShader(GL_VERTEX_SHADER);
        pglShaderSource(g_vs,1,vs_src,NULL); pglCompileShader(g_vs);
        g_fs=pglCreateShader(GL_FRAGMENT_SHADER);
        pglShaderSource(g_fs,1,fs_src,NULL); pglCompileShader(g_fs);
        g_prog=pglCreateProgram();
        pglAttachShader(g_prog,g_vs); pglAttachShader(g_prog,g_fs);
        if (pglBindAttribLocation) pglBindAttribLocation(g_prog,0,"pos");
        pglLinkProgram(g_prog);
        if (pglUseProgram) pglUseProgram(g_prog);
    }
    if (pglGenVertexArrays && pglBindVertexArray && pglVertexAttribPointer &&
        pglEnableVertexAttribArray) {
        pglGenVertexArrays(1,&g_vao); pglBindVertexArray(g_vao);
        if (pglBindBuffer) pglBindBuffer(GL_ARRAY_BUFFER,g_buf);
        pglVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,0,(const void*)0);
        pglEnableVertexAttribArray(0);
        pglBindVertexArray(0);
    }
    if (pglGenFramebuffers) pglGenFramebuffers(1,&g_fbo);
    if (pglGenRenderbuffers && pglBindRenderbuffer && pglRenderbufferStorage) {
        pglGenRenderbuffers(1,&g_rb); pglBindRenderbuffer(GL_RENDERBUFFER,g_rb);
        pglRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,64,64);
        pglBindRenderbuffer(GL_RENDERBUFFER,0);
    }
    if (pglGenQueries) { pglGenQueries(2,s_qry2); g_qry=s_qry2[0]; g_qry2=s_qry2[1]; }
    if (pglGenSamplers) pglGenSamplers(1,&g_smp);
    if (pglFenceSync) g_sync=pglFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE,0);
    if (pglGetError) pglGetError(); /* drain setup errors */
    note("setup: tex=%u buf=%u prog=%u vs=%u fs=%u fbo=%u rb=%u vao=%u qry=%u smp=%u sync=%p",
         g_tex,g_buf,g_prog,g_vs,g_fs,g_fbo,g_rb,g_vao,g_qry,g_smp,(void*)g_sync);
}
static void teardown_gl(void) {
    if (pglDeleteSync && g_sync) pglDeleteSync(g_sync);
    if (pglDeleteSamplers && g_smp) pglDeleteSamplers(1,&g_smp);
    if (pglDeleteQueries && g_qry) { GLuint q[2]={g_qry,g_qry2}; pglDeleteQueries(2,q); }
    if (pglDeleteRenderbuffers && g_rb) pglDeleteRenderbuffers(1,&g_rb);
    if (pglDeleteFramebuffers && g_fbo) pglDeleteFramebuffers(1,&g_fbo);
    if (pglDeleteVertexArrays && g_vao) pglDeleteVertexArrays(1,&g_vao);
    if (pglDeleteProgram && g_prog) pglDeleteProgram(g_prog);
    if (pglDeleteShader && g_vs) pglDeleteShader(g_vs);
    if (pglDeleteShader && g_fs) pglDeleteShader(g_fs);
    if (pglDeleteBuffers && g_ibuf) pglDeleteBuffers(1,&g_ibuf);
    if (pglDeleteBuffers && g_buf) pglDeleteBuffers(1,&g_buf);
    if (pglDeleteTextures && g_tex3d) pglDeleteTextures(1,&g_tex3d);
    if (pglDeleteTextures && g_tex1d) pglDeleteTextures(1,&g_tex1d);
    if (pglDeleteTextures && g_tex) pglDeleteTextures(1,&g_tex);
}
static void run_all(void) {
    /* glActiveTexture */
    if (pglActiveTexture) {
        trybegin("glActiveTexture");
        pglGetError();
        uint64_t _t0=qpf_us(); pglActiveTexture(GL_TEXTURE0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglActiveTexture(GL_TEXTURE0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glActiveTexture",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glActiveTexture: no proc */
    trybegin("glActiveTexture"); note("no proc: glActiveTexture"); rep("glActiveTexture",-1,-1,0,0);
    }
    /* glAttachShader */
    if (pglAttachShader) {
        trybegin("glAttachShader");
        pglGetError();
        uint64_t _t0=qpf_us(); pglAttachShader(g_prog, g_vs); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglAttachShader(g_prog, g_vs); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glAttachShader",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glAttachShader: no proc */
    trybegin("glAttachShader"); note("no proc: glAttachShader"); rep("glAttachShader",-1,-1,0,0);
    }
    /* legacy immediate-mode block: glBegin/glEnd pair with vertices */
    if (pglBegin && pglEnd) {
        if (pglBegin) {
            trybegin("glBegin");
            pglGetError();
            uint64_t _t0=qpf_us(); pglBegin(GL_TRIANGLES); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
            rep("glBegin",(long long)_e1,(long long)_e1,_t1-_t0,0);
        } else { trybegin("glBegin"); note("no proc: glBegin"); rep("glBegin",-1,-1,0,0); }
        if (pglColor3f) {
            trybegin("glColor3f");
            pglGetError();
            uint64_t _t0=qpf_us(); pglColor3f(1.0f, 0.0f, 0.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
            rep("glColor3f",(long long)_e1,(long long)_e1,_t1-_t0,0);
        } else { trybegin("glColor3f"); note("no proc: glColor3f"); rep("glColor3f",-1,-1,0,0); }
        if (pglVertex3f) {
            trybegin("glVertex3f");
            pglGetError();
            uint64_t _t0=qpf_us(); pglVertex3f(0.5f, -0.5f, 0.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
            rep("glVertex3f",(long long)_e1,(long long)_e1,_t1-_t0,0);
        } else { trybegin("glVertex3f"); note("no proc: glVertex3f"); rep("glVertex3f",-1,-1,0,0); }
        if (pglColor4f) {
            trybegin("glColor4f");
            pglGetError();
            uint64_t _t0=qpf_us(); pglColor4f(0.0f, 1.0f, 0.0f, 1.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
            rep("glColor4f",(long long)_e1,(long long)_e1,_t1-_t0,0);
        } else { trybegin("glColor4f"); note("no proc: glColor4f"); rep("glColor4f",-1,-1,0,0); }
        if (pglVertex2f) {
            trybegin("glVertex2f");
            pglGetError();
            uint64_t _t0=qpf_us(); pglVertex2f(-0.5f, -0.5f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
            rep("glVertex2f",(long long)_e1,(long long)_e1,_t1-_t0,0);
        } else { trybegin("glVertex2f"); note("no proc: glVertex2f"); rep("glVertex2f",-1,-1,0,0); }
        if (pglNormal3f) {
            trybegin("glNormal3f");
            pglGetError();
            uint64_t _t0=qpf_us(); pglNormal3f(0.0f, 0.0f, 1.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
            rep("glNormal3f",(long long)_e1,(long long)_e1,_t1-_t0,0);
        } else { trybegin("glNormal3f"); note("no proc: glNormal3f"); rep("glNormal3f",-1,-1,0,0); }
        if (pglEnd) {
            trybegin("glEnd");
            pglGetError();
            uint64_t _t0=qpf_us(); pglEnd(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
            rep("glEnd",(long long)_e1,(long long)_e1,_t1-_t0,0);
        } else { trybegin("glEnd"); note("no proc: glEnd"); rep("glEnd",-1,-1,0,0); }
    }

    /* glBeginQuery/glEndQuery paired: begin+end x2, each timed */
    if (pglBeginQuery && pglEndQuery) {
        trybegin("glBeginQuery"); pglGetError();
        uint64_t _t0=qpf_us(); pglBeginQuery(GL_SAMPLES_PASSED, g_qry); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        trybegin("glEndQuery");
        uint64_t _t2=qpf_us(); pglEndQuery(GL_SAMPLES_PASSED); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        trybegin("glBeginQuery");
        uint64_t _t4=qpf_us(); pglBeginQuery(GL_SAMPLES_PASSED, g_qry); GLenum _e3=pglGetError(); uint64_t _t5=qpf_us();
        trybegin("glEndQuery");
        uint64_t _t6=qpf_us(); pglEndQuery(GL_SAMPLES_PASSED); GLenum _e4=pglGetError(); uint64_t _t7=qpf_us();
        rep("glBeginQuery",(long long)_e1,(long long)_e3,_t1-_t0,_t5-_t4);
        rep("glEndQuery",(long long)_e2,(long long)_e4,_t3-_t2,_t7-_t6);
        pglGetError();
    } else {
        trybegin("glBeginQuery"); note("no proc: glBeginQuery"); rep("glBeginQuery",-1,-1,0,0);
        trybegin("glEndQuery"); note("no proc: glEndQuery"); rep("glEndQuery",-1,-1,0,0);
    }

    /* glBeginTransformFeedback/glEndTransformFeedback paired x2 */
    if (pglBeginTransformFeedback && pglEndTransformFeedback) {
        trybegin("glBeginTransformFeedback"); pglGetError();
        uint64_t _t0=qpf_us(); pglBeginTransformFeedback(GL_TRIANGLES); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        trybegin("glEndTransformFeedback");
        uint64_t _t2=qpf_us(); pglEndTransformFeedback(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        trybegin("glBeginTransformFeedback");
        uint64_t _t4=qpf_us(); pglBeginTransformFeedback(GL_TRIANGLES); GLenum _e3=pglGetError(); uint64_t _t5=qpf_us();
        trybegin("glEndTransformFeedback");
        uint64_t _t6=qpf_us(); pglEndTransformFeedback(); GLenum _e4=pglGetError(); uint64_t _t7=qpf_us();
        rep("glBeginTransformFeedback",(long long)_e1,(long long)_e3,_t1-_t0,_t5-_t4);
        rep("glEndTransformFeedback",(long long)_e2,(long long)_e4,_t3-_t2,_t7-_t6);
        pglGetError();
    } else {
        trybegin("glBeginTransformFeedback"); note("no proc: glBeginTransformFeedback"); rep("glBeginTransformFeedback",-1,-1,0,0);
        trybegin("glEndTransformFeedback"); note("no proc: glEndTransformFeedback"); rep("glEndTransformFeedback",-1,-1,0,0);
    }
    /* glBindAttribLocation */
    if (pglBindAttribLocation) {
        trybegin("glBindAttribLocation");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBindAttribLocation(g_prog, 0, "pos"); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBindAttribLocation(g_prog, 0, "pos"); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBindAttribLocation",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBindAttribLocation: no proc */
    trybegin("glBindAttribLocation"); note("no proc: glBindAttribLocation"); rep("glBindAttribLocation",-1,-1,0,0);
    }
    /* glBindBuffer */
    if (pglBindBuffer) {
        trybegin("glBindBuffer");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBindBuffer(GL_ARRAY_BUFFER, g_buf); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBindBuffer(GL_ARRAY_BUFFER, g_buf); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBindBuffer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBindBuffer: no proc */
    trybegin("glBindBuffer"); note("no proc: glBindBuffer"); rep("glBindBuffer",-1,-1,0,0);
    }
    /* glBindBufferBase */
    if (pglBindBufferBase) {
        trybegin("glBindBufferBase");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, g_buf); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER, 0, g_buf); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBindBufferBase",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBindBufferBase: no proc */
    trybegin("glBindBufferBase"); note("no proc: glBindBufferBase"); rep("glBindBufferBase",-1,-1,0,0);
    }
    /* glBindBufferRange */
    if (pglBindBufferRange) {
        trybegin("glBindBufferRange");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, g_buf, 0, 64); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBindBufferRange(GL_TRANSFORM_FEEDBACK_BUFFER, 0, g_buf, 0, 64); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBindBufferRange",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBindBufferRange: no proc */
    trybegin("glBindBufferRange"); note("no proc: glBindBufferRange"); rep("glBindBufferRange",-1,-1,0,0);
    }
    /* glBindFragDataLocation */
    if (pglBindFragDataLocation) {
        trybegin("glBindFragDataLocation");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBindFragDataLocation(g_prog, 0, "outColor"); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBindFragDataLocation(g_prog, 0, "outColor"); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBindFragDataLocation",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBindFragDataLocation: no proc */
    trybegin("glBindFragDataLocation"); note("no proc: glBindFragDataLocation"); rep("glBindFragDataLocation",-1,-1,0,0);
    }
    /* glBindFramebuffer */
    if (pglBindFramebuffer) {
        trybegin("glBindFramebuffer");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBindFramebuffer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
    /* glBindFramebuffer: no proc */
    trybegin("glBindFramebuffer"); note("no proc: glBindFramebuffer"); rep("glBindFramebuffer",-1,-1,0,0);
    }
    /* glBindRenderbuffer */
    if (pglBindRenderbuffer) {
        trybegin("glBindRenderbuffer");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBindRenderbuffer(GL_RENDERBUFFER, g_rb); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBindRenderbuffer(GL_RENDERBUFFER, g_rb); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBindRenderbuffer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindRenderbuffer(GL_RENDERBUFFER, 0);
    } else {
    /* glBindRenderbuffer: no proc */
    trybegin("glBindRenderbuffer"); note("no proc: glBindRenderbuffer"); rep("glBindRenderbuffer",-1,-1,0,0);
    }
    /* glBindSampler */
    if (pglBindSampler) {
        trybegin("glBindSampler");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBindSampler(0, g_smp); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBindSampler(0, g_smp); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBindSampler",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindSampler(0, 0);
    } else {
    /* glBindSampler: no proc */
    trybegin("glBindSampler"); note("no proc: glBindSampler"); rep("glBindSampler",-1,-1,0,0);
    }
    /* glBindTexture */
    if (pglBindTexture) {
        trybegin("glBindTexture");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBindTexture(GL_TEXTURE_2D, g_tex); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBindTexture(GL_TEXTURE_2D, g_tex); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBindTexture",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBindTexture: no proc */
    trybegin("glBindTexture"); note("no proc: glBindTexture"); rep("glBindTexture",-1,-1,0,0);
    }
    /* glBindVertexArray */
    if (pglBindVertexArray) {
        trybegin("glBindVertexArray");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBindVertexArray(g_vao); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBindVertexArray(g_vao); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBindVertexArray",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindVertexArray(0);
    } else {
    /* glBindVertexArray: no proc */
    trybegin("glBindVertexArray"); note("no proc: glBindVertexArray"); rep("glBindVertexArray",-1,-1,0,0);
    }
    /* glBlendColor */
    if (pglBlendColor) {
        trybegin("glBlendColor");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBlendColor(0.1f, 0.2f, 0.3f, 0.4f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBlendColor(0.1f, 0.2f, 0.3f, 0.4f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBlendColor",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBlendColor: no proc */
    trybegin("glBlendColor"); note("no proc: glBlendColor"); rep("glBlendColor",-1,-1,0,0);
    }
    /* glBlendEquation */
    if (pglBlendEquation) {
        trybegin("glBlendEquation");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBlendEquation(GL_FUNC_ADD); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBlendEquation(GL_FUNC_ADD); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBlendEquation",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBlendEquation: no proc */
    trybegin("glBlendEquation"); note("no proc: glBlendEquation"); rep("glBlendEquation",-1,-1,0,0);
    }
    /* glBlendEquationSeparate */
    if (pglBlendEquationSeparate) {
        trybegin("glBlendEquationSeparate");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBlendEquationSeparate(GL_FUNC_ADD, GL_FUNC_ADD); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBlendEquationSeparate",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBlendEquationSeparate: no proc */
    trybegin("glBlendEquationSeparate"); note("no proc: glBlendEquationSeparate"); rep("glBlendEquationSeparate",-1,-1,0,0);
    }
    /* glBlendEquationSeparatei */
    if (pglBlendEquationSeparatei) {
        trybegin("glBlendEquationSeparatei");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBlendEquationSeparatei(0, GL_FUNC_ADD, GL_FUNC_ADD); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBlendEquationSeparatei(0, GL_FUNC_ADD, GL_FUNC_ADD); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBlendEquationSeparatei",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBlendEquationSeparatei: no proc */
    trybegin("glBlendEquationSeparatei"); note("no proc: glBlendEquationSeparatei"); rep("glBlendEquationSeparatei",-1,-1,0,0);
    }
    /* glBlendEquationi */
    if (pglBlendEquationi) {
        trybegin("glBlendEquationi");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBlendEquationi(0, GL_FUNC_ADD); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBlendEquationi(0, GL_FUNC_ADD); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBlendEquationi",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBlendEquationi: no proc */
    trybegin("glBlendEquationi"); note("no proc: glBlendEquationi"); rep("glBlendEquationi",-1,-1,0,0);
    }
    /* glBlendFunc */
    if (pglBlendFunc) {
        trybegin("glBlendFunc");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBlendFunc",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBlendFunc: no proc */
    trybegin("glBlendFunc"); note("no proc: glBlendFunc"); rep("glBlendFunc",-1,-1,0,0);
    }
    /* glBlendFuncSeparate */
    if (pglBlendFuncSeparate) {
        trybegin("glBlendFuncSeparate");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBlendFuncSeparate",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBlendFuncSeparate: no proc */
    trybegin("glBlendFuncSeparate"); note("no proc: glBlendFuncSeparate"); rep("glBlendFuncSeparate",-1,-1,0,0);
    }
    /* glBlendFuncSeparatei */
    if (pglBlendFuncSeparatei) {
        trybegin("glBlendFuncSeparatei");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBlendFuncSeparatei(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBlendFuncSeparatei(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ZERO); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBlendFuncSeparatei",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBlendFuncSeparatei: no proc */
    trybegin("glBlendFuncSeparatei"); note("no proc: glBlendFuncSeparatei"); rep("glBlendFuncSeparatei",-1,-1,0,0);
    }
    /* glBlendFunci */
    if (pglBlendFunci) {
        trybegin("glBlendFunci");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBlendFunci(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBlendFunci(0, GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBlendFunci",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBlendFunci: no proc */
    trybegin("glBlendFunci"); note("no proc: glBlendFunci"); rep("glBlendFunci",-1,-1,0,0);
    }
    /* glBlitFramebuffer */
    if (pglBlitFramebuffer) {
        trybegin("glBlitFramebuffer");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBlitFramebuffer(0,0,64,64, 0,0,64,64, GL_COLOR_BUFFER_BIT, GL_NEAREST); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBlitFramebuffer(0,0,64,64, 0,0,64,64, GL_COLOR_BUFFER_BIT, GL_NEAREST); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBlitFramebuffer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBlitFramebuffer: no proc */
    trybegin("glBlitFramebuffer"); note("no proc: glBlitFramebuffer"); rep("glBlitFramebuffer",-1,-1,0,0);
    }
    /* glBufferData */
    if (pglBufferData) {
        trybegin("glBufferData");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBufferData(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_STATIC_DRAW); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBufferData",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBufferData: no proc */
    trybegin("glBufferData"); note("no proc: glBufferData"); rep("glBufferData",-1,-1,0,0);
    }
    /* glBufferStorage */
    if (pglBufferStorage) {
        trybegin("glBufferStorage");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBufferStorage(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_MAP_READ_BIT); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBufferStorage(GL_ARRAY_BUFFER, sizeof(tri), tri, GL_MAP_READ_BIT); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBufferStorage",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBufferStorage: no proc */
    trybegin("glBufferStorage"); note("no proc: glBufferStorage"); rep("glBufferStorage",-1,-1,0,0);
    }
    /* glBufferSubData */
    if (pglBufferSubData) {
        trybegin("glBufferSubData");
        pglGetError();
        uint64_t _t0=qpf_us(); pglBufferSubData(GL_ARRAY_BUFFER, 0, 12, tri); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglBufferSubData(GL_ARRAY_BUFFER, 0, 12, tri); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glBufferSubData",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glBufferSubData: no proc */
    trybegin("glBufferSubData"); note("no proc: glBufferSubData"); rep("glBufferSubData",-1,-1,0,0);
    }
    /* glCheckFramebufferStatus */
    if (pglCheckFramebufferStatus) {
        pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        trybegin("glCheckFramebufferStatus");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglCheckFramebufferStatus(GL_FRAMEBUFFER)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglCheckFramebufferStatus(GL_FRAMEBUFFER)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glCheckFramebufferStatus=0x%llx", (unsigned long long)(uintptr_t)_r1);
        note("glCheckFramebufferStatus=0x%x", (unsigned)_r1);
        rep("glCheckFramebufferStatus",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
    /* glCheckFramebufferStatus: no proc */
    trybegin("glCheckFramebufferStatus"); note("no proc: glCheckFramebufferStatus"); rep("glCheckFramebufferStatus",-1,-1,0,0);
    }
    /* glClear */
    if (pglClear) {
        trybegin("glClear");
        pglGetError();
        uint64_t _t0=qpf_us(); pglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glClear",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glClear: no proc */
    trybegin("glClear"); note("no proc: glClear"); rep("glClear",-1,-1,0,0);
    }
    /* glClearBufferfi */
    if (pglClearBufferfi) {
        trybegin("glClearBufferfi");
        pglGetError();
        uint64_t _t0=qpf_us(); pglClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglClearBufferfi(GL_DEPTH_STENCIL, 0, 1.0f, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glClearBufferfi",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glClearBufferfi: no proc */
    trybegin("glClearBufferfi"); note("no proc: glClearBufferfi"); rep("glClearBufferfi",-1,-1,0,0);
    }
    /* glClearBufferfv */
    if (pglClearBufferfv) {
        trybegin("glClearBufferfv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglClearBufferfv(GL_COLOR, 0, clearc); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglClearBufferfv(GL_COLOR, 0, clearc); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glClearBufferfv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glClearBufferfv: no proc */
    trybegin("glClearBufferfv"); note("no proc: glClearBufferfv"); rep("glClearBufferfv",-1,-1,0,0);
    }
    /* glClearBufferiv */
    if (pglClearBufferiv) {
        trybegin("glClearBufferiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglClearBufferiv(GL_COLOR, 0, cleari); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglClearBufferiv(GL_COLOR, 0, cleari); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glClearBufferiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glClearBufferiv: no proc */
    trybegin("glClearBufferiv"); note("no proc: glClearBufferiv"); rep("glClearBufferiv",-1,-1,0,0);
    }
    /* glClearBufferuiv */
    if (pglClearBufferuiv) {
        trybegin("glClearBufferuiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglClearBufferuiv(GL_COLOR, 0, clearu); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglClearBufferuiv(GL_COLOR, 0, clearu); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glClearBufferuiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glClearBufferuiv: no proc */
    trybegin("glClearBufferuiv"); note("no proc: glClearBufferuiv"); rep("glClearBufferuiv",-1,-1,0,0);
    }
    /* glClearColor */
    if (pglClearColor) {
        trybegin("glClearColor");
        pglGetError();
        uint64_t _t0=qpf_us(); pglClearColor(0.1f, 0.2f, 0.3f, 1.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglClearColor(0.1f, 0.2f, 0.3f, 1.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glClearColor",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glClearColor: no proc */
    trybegin("glClearColor"); note("no proc: glClearColor"); rep("glClearColor",-1,-1,0,0);
    }
    /* glClearDepth */
    if (pglClearDepth) {
        trybegin("glClearDepth");
        pglGetError();
        uint64_t _t0=qpf_us(); pglClearDepth(1.0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglClearDepth(1.0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glClearDepth",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glClearDepth: no proc */
    trybegin("glClearDepth"); note("no proc: glClearDepth"); rep("glClearDepth",-1,-1,0,0);
    }
    /* glClientWaitSync */
    if (pglClientWaitSync) {
        trybegin("glClientWaitSync");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglClientWaitSync(g_sync, 0, 1000)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglClientWaitSync(g_sync, 0, 1000)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glClientWaitSync=0x%llx", (unsigned long long)(uintptr_t)_r1);
        note("glClientWaitSync=0x%x", (unsigned)_r1);
        rep("glClientWaitSync",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glClientWaitSync: no proc */
    trybegin("glClientWaitSync"); note("no proc: glClientWaitSync"); rep("glClientWaitSync",-1,-1,0,0);
    }
    /* glColorMask */
    if (pglColorMask) {
        trybegin("glColorMask");
        pglGetError();
        uint64_t _t0=qpf_us(); pglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glColorMask",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glColorMask: no proc */
    trybegin("glColorMask"); note("no proc: glColorMask"); rep("glColorMask",-1,-1,0,0);
    }
    /* glColorMaski */
    if (pglColorMaski) {
        trybegin("glColorMaski");
        pglGetError();
        uint64_t _t0=qpf_us(); pglColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glColorMaski",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glColorMaski: no proc */
    trybegin("glColorMaski"); note("no proc: glColorMaski"); rep("glColorMaski",-1,-1,0,0);
    }
    /* glColorMaterial */
    if (pglColorMaterial) {
        trybegin("glColorMaterial");
        pglGetError();
        uint64_t _t0=qpf_us(); pglColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glColorMaterial",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glColorMaterial: no proc */
    trybegin("glColorMaterial"); note("no proc: glColorMaterial"); rep("glColorMaterial",-1,-1,0,0);
    }
    /* glCompileShader */
    if (pglCompileShader) {
        trybegin("glCompileShader");
        pglGetError();
        uint64_t _t0=qpf_us(); pglCompileShader(g_vs); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglCompileShader(g_vs); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glCompileShader",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glCompileShader: no proc */
    trybegin("glCompileShader"); note("no proc: glCompileShader"); rep("glCompileShader",-1,-1,0,0);
    }
    /* glCompressedTexImage2D */
    if (pglCompressedTexImage2D) {
        trybegin("glCompressedTexImage2D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 0, 8, dxt); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglCompressedTexImage2D(GL_TEXTURE_2D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 0, 8, dxt); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glCompressedTexImage2D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glCompressedTexImage2D: no proc */
    trybegin("glCompressedTexImage2D"); note("no proc: glCompressedTexImage2D"); rep("glCompressedTexImage2D",-1,-1,0,0);
    }
    /* glCompressedTexImage3D */
    if (pglCompressedTexImage3D) {
        trybegin("glCompressedTexImage3D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglCompressedTexImage3D(GL_TEXTURE_3D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 4, 0, 64, dxt3); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglCompressedTexImage3D(GL_TEXTURE_3D, 0, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 4, 4, 4, 0, 64, dxt3); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glCompressedTexImage3D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glCompressedTexImage3D: no proc */
    trybegin("glCompressedTexImage3D"); note("no proc: glCompressedTexImage3D"); rep("glCompressedTexImage3D",-1,-1,0,0);
    }
    /* glCompressedTexSubImage2D */
    if (pglCompressedTexSubImage2D) {
        trybegin("glCompressedTexSubImage2D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 8, dxt); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglCompressedTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 4, 4, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 8, dxt); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glCompressedTexSubImage2D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glCompressedTexSubImage2D: no proc */
    trybegin("glCompressedTexSubImage2D"); note("no proc: glCompressedTexSubImage2D"); rep("glCompressedTexSubImage2D",-1,-1,0,0);
    }
    /* glCompressedTexSubImage3D */
    if (pglCompressedTexSubImage3D) {
        trybegin("glCompressedTexSubImage3D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglCompressedTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 4, 4, 4, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 64, dxt3); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglCompressedTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 4, 4, 4, GL_COMPRESSED_RGB_S3TC_DXT1_EXT, 64, dxt3); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glCompressedTexSubImage3D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glCompressedTexSubImage3D: no proc */
    trybegin("glCompressedTexSubImage3D"); note("no proc: glCompressedTexSubImage3D"); rep("glCompressedTexSubImage3D",-1,-1,0,0);
    }
    /* glCopyBufferSubData */
    if (pglCopyBufferSubData) {
        trybegin("glCopyBufferSubData");
        pglGetError();
        uint64_t _t0=qpf_us(); pglCopyBufferSubData(GL_ARRAY_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, 16); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglCopyBufferSubData(GL_ARRAY_BUFFER, GL_COPY_WRITE_BUFFER, 0, 0, 16); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glCopyBufferSubData",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glCopyBufferSubData: no proc */
    trybegin("glCopyBufferSubData"); note("no proc: glCopyBufferSubData"); rep("glCopyBufferSubData",-1,-1,0,0);
    }
    /* glCreateProgram: temp program, deleted after */
    if (pglCreateProgram && pglDeleteProgram) {
        trybegin("glCreateProgram"); pglGetError();
        uint64_t _t0=qpf_us(); GLuint _tp1=pglCreateProgram(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); GLuint _tp2=pglCreateProgram(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glCreateProgram ids=%u,%u", _tp1, _tp2);
        rep("glCreateProgram",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglDeleteProgram(_tp1); pglDeleteProgram(_tp2);
    } else {
        trybegin("glCreateProgram"); note("no proc: glCreateProgram"); rep("glCreateProgram",-1,-1,0,0);
    }
    /* glCreateShader: temp shaders, deleted after */
    if (pglCreateShader && pglDeleteShader) {
        trybegin("glCreateShader"); pglGetError();
        uint64_t _t0=qpf_us(); GLuint _ts1=pglCreateShader(GL_VERTEX_SHADER); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); GLuint _ts2=pglCreateShader(GL_FRAGMENT_SHADER); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glCreateShader ids=%u,%u", _ts1, _ts2);
        rep("glCreateShader",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglDeleteShader(_ts1); pglDeleteShader(_ts2);
    } else {
        trybegin("glCreateShader"); note("no proc: glCreateShader"); rep("glCreateShader",-1,-1,0,0);
    }
    /* glCullFace */
    if (pglCullFace) {
        trybegin("glCullFace");
        pglGetError();
        uint64_t _t0=qpf_us(); pglCullFace(GL_BACK); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglCullFace(GL_BACK); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glCullFace",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glCullFace: no proc */
    trybegin("glCullFace"); note("no proc: glCullFace"); rep("glCullFace",-1,-1,0,0);
    }
    /* glDebugMessageCallback */
    if (pglDebugMessageCallback) {
        trybegin("glDebugMessageCallback");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDebugMessageCallback(dbgcb, NULL); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDebugMessageCallback(dbgcb, NULL); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDebugMessageCallback",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDebugMessageCallback: no proc */
    trybegin("glDebugMessageCallback"); note("no proc: glDebugMessageCallback"); rep("glDebugMessageCallback",-1,-1,0,0);
    }
    /* glDebugMessageControl */
    if (pglDebugMessageControl) {
        trybegin("glDebugMessageControl");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDebugMessageControl",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDebugMessageControl: no proc */
    trybegin("glDebugMessageControl"); note("no proc: glDebugMessageControl"); rep("glDebugMessageControl",-1,-1,0,0);
    }
    /* glDebugMessageInsert */
    if (pglDebugMessageInsert) {
        trybegin("glDebugMessageInsert");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 1, GL_DEBUG_SEVERITY_NOTIFICATION, -1, "glcov"); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDebugMessageInsert(GL_DEBUG_SOURCE_APPLICATION, GL_DEBUG_TYPE_MARKER, 1, GL_DEBUG_SEVERITY_NOTIFICATION, -1, "glcov"); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDebugMessageInsert",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDebugMessageInsert: no proc */
    trybegin("glDebugMessageInsert"); note("no proc: glDebugMessageInsert"); rep("glDebugMessageInsert",-1,-1,0,0);
    }
    /* glDeleteProgram: create temp, delete it */
    if (pglCreateProgram && pglDeleteProgram) {
        GLuint _dp=pglCreateProgram(); pglGetError();
        trybegin("glDeleteProgram");
        uint64_t _t0=qpf_us(); pglDeleteProgram(_dp); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        rep("glDeleteProgram",(long long)_e1,(long long)_e1,_t1-_t0,0);
    } else {
        trybegin("glDeleteProgram"); note("no proc: glDeleteProgram"); rep("glDeleteProgram",-1,-1,0,0);
    }
    /* glDeleteShader: create temp, delete it */
    if (pglCreateShader && pglDeleteShader) {
        GLuint _ds=pglCreateShader(GL_VERTEX_SHADER); pglGetError();
        trybegin("glDeleteShader");
        uint64_t _t0=qpf_us(); pglDeleteShader(_ds); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        rep("glDeleteShader",(long long)_e1,(long long)_e1,_t1-_t0,0);
    } else {
        trybegin("glDeleteShader"); note("no proc: glDeleteShader"); rep("glDeleteShader",-1,-1,0,0);
    }
    /* glDeleteSync: create temp sync, delete it */
    if (pglFenceSync && pglDeleteSync) {
        GLsync _dy=pglFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); pglGetError();
        trybegin("glDeleteSync");
        uint64_t _t0=qpf_us(); pglDeleteSync(_dy); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        rep("glDeleteSync",(long long)_e1,(long long)_e1,_t1-_t0,0);
    } else {
        trybegin("glDeleteSync"); note("no proc: glDeleteSync"); rep("glDeleteSync",-1,-1,0,0);
    }
    /* glDepthFunc */
    if (pglDepthFunc) {
        trybegin("glDepthFunc");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDepthFunc(GL_LESS); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDepthFunc(GL_LESS); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDepthFunc",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDepthFunc: no proc */
    trybegin("glDepthFunc"); note("no proc: glDepthFunc"); rep("glDepthFunc",-1,-1,0,0);
    }
    /* glDepthMask */
    if (pglDepthMask) {
        trybegin("glDepthMask");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDepthMask(GL_TRUE); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDepthMask(GL_TRUE); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDepthMask",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDepthMask: no proc */
    trybegin("glDepthMask"); note("no proc: glDepthMask"); rep("glDepthMask",-1,-1,0,0);
    }
    /* glDepthRange */
    if (pglDepthRange) {
        trybegin("glDepthRange");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDepthRange(0.0, 1.0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDepthRange(0.0, 1.0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDepthRange",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDepthRange: no proc */
    trybegin("glDepthRange"); note("no proc: glDepthRange"); rep("glDepthRange",-1,-1,0,0);
    }
    /* glDetachShader: detach then re-attach to preserve state */
    if (pglDetachShader && pglAttachShader) {
        trybegin("glDetachShader"); pglGetError();
        uint64_t _t0=qpf_us(); pglDetachShader(g_prog, g_vs); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDetachShader(g_prog, g_fs); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDetachShader",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglAttachShader(g_prog, g_vs); pglAttachShader(g_prog, g_fs);
        pglGetError();
    } else {
        trybegin("glDetachShader"); note("no proc: glDetachShader"); rep("glDetachShader",-1,-1,0,0);
    }
    /* glDisable */
    if (pglDisable) {
        trybegin("glDisable");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDisable(GL_DEPTH_TEST); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDisable(GL_DEPTH_TEST); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDisable",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDisable: no proc */
    trybegin("glDisable"); note("no proc: glDisable"); rep("glDisable",-1,-1,0,0);
    }
    /* glDisableVertexAttribArray */
    if (pglDisableVertexAttribArray) {
        trybegin("glDisableVertexAttribArray");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDisableVertexAttribArray(0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDisableVertexAttribArray(0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDisableVertexAttribArray",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDisableVertexAttribArray: no proc */
    trybegin("glDisableVertexAttribArray"); note("no proc: glDisableVertexAttribArray"); rep("glDisableVertexAttribArray",-1,-1,0,0);
    }
    /* glDisablei */
    if (pglDisablei) {
        trybegin("glDisablei");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDisablei(GL_BLEND, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDisablei(GL_BLEND, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDisablei",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDisablei: no proc */
    trybegin("glDisablei"); note("no proc: glDisablei"); rep("glDisablei",-1,-1,0,0);
    }
    /* glDrawArrays */
    if (pglDrawArrays) {
        trybegin("glDrawArrays");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawArrays(GL_TRIANGLES, 0, 3); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawArrays(GL_TRIANGLES, 0, 3); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawArrays",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawArrays: no proc */
    trybegin("glDrawArrays"); note("no proc: glDrawArrays"); rep("glDrawArrays",-1,-1,0,0);
    }
    /* glDrawArraysInstanced */
    if (pglDrawArraysInstanced) {
        trybegin("glDrawArraysInstanced");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawArraysInstanced(GL_TRIANGLES, 0, 3, 2); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawArraysInstanced(GL_TRIANGLES, 0, 3, 2); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawArraysInstanced",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawArraysInstanced: no proc */
    trybegin("glDrawArraysInstanced"); note("no proc: glDrawArraysInstanced"); rep("glDrawArraysInstanced",-1,-1,0,0);
    }
    /* glDrawArraysInstancedBaseInstance */
    if (pglDrawArraysInstancedBaseInstance) {
        trybegin("glDrawArraysInstancedBaseInstance");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawArraysInstancedBaseInstance(GL_TRIANGLES, 0, 3, 2, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawArraysInstancedBaseInstance(GL_TRIANGLES, 0, 3, 2, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawArraysInstancedBaseInstance",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawArraysInstancedBaseInstance: no proc */
    trybegin("glDrawArraysInstancedBaseInstance"); note("no proc: glDrawArraysInstancedBaseInstance"); rep("glDrawArraysInstancedBaseInstance",-1,-1,0,0);
    }
    /* glDrawBuffers */
    if (pglDrawBuffers) {
        trybegin("glDrawBuffers");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawBuffers(1, dbb); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawBuffers(1, dbb); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawBuffers",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawBuffers: no proc */
    trybegin("glDrawBuffers"); note("no proc: glDrawBuffers"); rep("glDrawBuffers",-1,-1,0,0);
    }
    /* glDrawElements */
    if (pglDrawElements) {
        trybegin("glDrawElements");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void*)0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawElements(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void*)0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawElements",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawElements: no proc */
    trybegin("glDrawElements"); note("no proc: glDrawElements"); rep("glDrawElements",-1,-1,0,0);
    }
    /* glDrawElementsBaseVertex */
    if (pglDrawElementsBaseVertex) {
        trybegin("glDrawElementsBaseVertex");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawElementsBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void*)0, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawElementsBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void*)0, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawElementsBaseVertex",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawElementsBaseVertex: no proc */
    trybegin("glDrawElementsBaseVertex"); note("no proc: glDrawElementsBaseVertex"); rep("glDrawElementsBaseVertex",-1,-1,0,0);
    }
    /* glDrawElementsInstanced */
    if (pglDrawElementsInstanced) {
        trybegin("glDrawElementsInstanced");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawElementsInstanced(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void*)0, 2); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawElementsInstanced(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void*)0, 2); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawElementsInstanced",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawElementsInstanced: no proc */
    trybegin("glDrawElementsInstanced"); note("no proc: glDrawElementsInstanced"); rep("glDrawElementsInstanced",-1,-1,0,0);
    }
    /* glDrawElementsInstancedBaseVertex */
    if (pglDrawElementsInstancedBaseVertex) {
        trybegin("glDrawElementsInstancedBaseVertex");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawElementsInstancedBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void*)0, 2, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawElementsInstancedBaseVertex(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void*)0, 2, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawElementsInstancedBaseVertex",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawElementsInstancedBaseVertex: no proc */
    trybegin("glDrawElementsInstancedBaseVertex"); note("no proc: glDrawElementsInstancedBaseVertex"); rep("glDrawElementsInstancedBaseVertex",-1,-1,0,0);
    }
    /* glDrawElementsInstancedBaseVertexBaseInstance */
    if (pglDrawElementsInstancedBaseVertexBaseInstance) {
        trybegin("glDrawElementsInstancedBaseVertexBaseInstance");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void*)0, 2, 0, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawElementsInstancedBaseVertexBaseInstance(GL_TRIANGLES, 3, GL_UNSIGNED_SHORT, (const void*)0, 2, 0, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawElementsInstancedBaseVertexBaseInstance",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawElementsInstancedBaseVertexBaseInstance: no proc */
    trybegin("glDrawElementsInstancedBaseVertexBaseInstance"); note("no proc: glDrawElementsInstancedBaseVertexBaseInstance"); rep("glDrawElementsInstancedBaseVertexBaseInstance",-1,-1,0,0);
    }
    /* glDrawRangeElements */
    if (pglDrawRangeElements) {
        trybegin("glDrawRangeElements");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawRangeElements(GL_TRIANGLES, 0, 2, 3, GL_UNSIGNED_SHORT, (const void*)0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawRangeElements(GL_TRIANGLES, 0, 2, 3, GL_UNSIGNED_SHORT, (const void*)0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawRangeElements",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawRangeElements: no proc */
    trybegin("glDrawRangeElements"); note("no proc: glDrawRangeElements"); rep("glDrawRangeElements",-1,-1,0,0);
    }
    /* glDrawRangeElementsBaseVertex */
    if (pglDrawRangeElementsBaseVertex) {
        trybegin("glDrawRangeElementsBaseVertex");
        pglGetError();
        uint64_t _t0=qpf_us(); pglDrawRangeElementsBaseVertex(GL_TRIANGLES, 0, 2, 3, GL_UNSIGNED_SHORT, (const void*)0, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglDrawRangeElementsBaseVertex(GL_TRIANGLES, 0, 2, 3, GL_UNSIGNED_SHORT, (const void*)0, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glDrawRangeElementsBaseVertex",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glDrawRangeElementsBaseVertex: no proc */
    trybegin("glDrawRangeElementsBaseVertex"); note("no proc: glDrawRangeElementsBaseVertex"); rep("glDrawRangeElementsBaseVertex",-1,-1,0,0);
    }
    /* glEnable */
    if (pglEnable) {
        trybegin("glEnable");
        pglGetError();
        uint64_t _t0=qpf_us(); pglEnable(GL_DEPTH_TEST); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglEnable(GL_DEPTH_TEST); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glEnable",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glEnable: no proc */
    trybegin("glEnable"); note("no proc: glEnable"); rep("glEnable",-1,-1,0,0);
    }
    /* glEnableVertexAttribArray */
    if (pglEnableVertexAttribArray) {
        trybegin("glEnableVertexAttribArray");
        pglGetError();
        uint64_t _t0=qpf_us(); pglEnableVertexAttribArray(0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglEnableVertexAttribArray(0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glEnableVertexAttribArray",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glEnableVertexAttribArray: no proc */
    trybegin("glEnableVertexAttribArray"); note("no proc: glEnableVertexAttribArray"); rep("glEnableVertexAttribArray",-1,-1,0,0);
    }
    /* glEnablei */
    if (pglEnablei) {
        trybegin("glEnablei");
        pglGetError();
        uint64_t _t0=qpf_us(); pglEnablei(GL_BLEND, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglEnablei(GL_BLEND, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glEnablei",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glEnablei: no proc */
    trybegin("glEnablei"); note("no proc: glEnablei"); rep("glEnablei",-1,-1,0,0);
    }
    /* glFenceSync: temp syncs, waited on then deleted */
    if (pglFenceSync && pglDeleteSync && pglWaitSync) {
        trybegin("glFenceSync"); pglGetError();
        uint64_t _t0=qpf_us(); GLsync _fy1=pglFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); GLsync _fy2=pglFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glFenceSync=%p,%p", _fy1, _fy2);
        rep("glFenceSync",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglWaitSync(_fy1, 0, 0); pglWaitSync(_fy2, 0, 0);
        pglDeleteSync(_fy1); pglDeleteSync(_fy2);
        pglGetError();
    } else {
        trybegin("glFenceSync"); note("no proc: glFenceSync"); rep("glFenceSync",-1,-1,0,0);
    }
    /* glFinalCombinerInputNV */
    if (pglFinalCombinerInputNV) {
        trybegin("glFinalCombinerInputNV");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFinalCombinerInputNV(0, 0, 0, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFinalCombinerInputNV(0, 0, 0, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFinalCombinerInputNV",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glFinalCombinerInputNV: no proc */
    trybegin("glFinalCombinerInputNV"); note("no proc: glFinalCombinerInputNV"); rep("glFinalCombinerInputNV",-1,-1,0,0);
    }
    /* glFinish */
    if (pglFinish) {
        trybegin("glFinish");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFinish(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFinish(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFinish",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glFinish: no proc */
    trybegin("glFinish"); note("no proc: glFinish"); rep("glFinish",-1,-1,0,0);
    }
    /* glFlush */
    if (pglFlush) {
        trybegin("glFlush");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFlush(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFlush(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFlush",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glFlush: no proc */
    trybegin("glFlush"); note("no proc: glFlush"); rep("glFlush",-1,-1,0,0);
    }
    /* glFramebufferRenderbuffer */
    if (pglFramebufferRenderbuffer) {
        pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        trybegin("glFramebufferRenderbuffer");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_rb); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, g_rb); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFramebufferRenderbuffer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
    /* glFramebufferRenderbuffer: no proc */
    trybegin("glFramebufferRenderbuffer"); note("no proc: glFramebufferRenderbuffer"); rep("glFramebufferRenderbuffer",-1,-1,0,0);
    }
    /* glFramebufferTexture */
    if (pglFramebufferTexture) {
        pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        trybegin("glFramebufferTexture");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, g_tex, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFramebufferTexture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, g_tex, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFramebufferTexture",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
    /* glFramebufferTexture: no proc */
    trybegin("glFramebufferTexture"); note("no proc: glFramebufferTexture"); rep("glFramebufferTexture",-1,-1,0,0);
    }
    /* glFramebufferTexture1D */
    if (pglFramebufferTexture1D) {
        pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        trybegin("glFramebufferTexture1D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFramebufferTexture1D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_1D, g_tex1d, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFramebufferTexture1D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_1D, g_tex1d, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFramebufferTexture1D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
    /* glFramebufferTexture1D: no proc */
    trybegin("glFramebufferTexture1D"); note("no proc: glFramebufferTexture1D"); rep("glFramebufferTexture1D",-1,-1,0,0);
    }
    /* glFramebufferTexture2D */
    if (pglFramebufferTexture2D) {
        pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        trybegin("glFramebufferTexture2D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, g_tex, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFramebufferTexture2D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
    /* glFramebufferTexture2D: no proc */
    trybegin("glFramebufferTexture2D"); note("no proc: glFramebufferTexture2D"); rep("glFramebufferTexture2D",-1,-1,0,0);
    }
    /* glFramebufferTexture3D */
    if (pglFramebufferTexture3D) {
        pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        trybegin("glFramebufferTexture3D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFramebufferTexture3D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_3D, g_tex3d, 0, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFramebufferTexture3D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_3D, g_tex3d, 0, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFramebufferTexture3D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
    /* glFramebufferTexture3D: no proc */
    trybegin("glFramebufferTexture3D"); note("no proc: glFramebufferTexture3D"); rep("glFramebufferTexture3D",-1,-1,0,0);
    }
    /* glFramebufferTextureLayer */
    if (pglFramebufferTextureLayer) {
        pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        trybegin("glFramebufferTextureLayer");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, g_tex, 0, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFramebufferTextureLayer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, g_tex, 0, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFramebufferTextureLayer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
    /* glFramebufferTextureLayer: no proc */
    trybegin("glFramebufferTextureLayer"); note("no proc: glFramebufferTextureLayer"); rep("glFramebufferTextureLayer",-1,-1,0,0);
    }
    /* glFrontFace */
    if (pglFrontFace) {
        trybegin("glFrontFace");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFrontFace(GL_CCW); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFrontFace(GL_CCW); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFrontFace",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glFrontFace: no proc */
    trybegin("glFrontFace"); note("no proc: glFrontFace"); rep("glFrontFace",-1,-1,0,0);
    }
    /* glFrustum */
    if (pglFrustum) {
        trybegin("glFrustum");
        pglGetError();
        uint64_t _t0=qpf_us(); pglFrustum(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglFrustum(-1.0, 1.0, -1.0, 1.0, 1.0, 10.0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glFrustum",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glFrustum: no proc */
    trybegin("glFrustum"); note("no proc: glFrustum"); rep("glFrustum",-1,-1,0,0);
    }
    GENDEL(glGenBuffers, glDeleteBuffers);
    GENDEL(glGenFramebuffers, glDeleteFramebuffers);
    GENDEL(glGenQueries, glDeleteQueries);
    GENDEL(glGenRenderbuffers, glDeleteRenderbuffers);
    GENDEL(glGenSamplers, glDeleteSamplers);
    GENDEL(glGenTextures, glDeleteTextures);
    GENDEL(glGenVertexArrays, glDeleteVertexArrays);
    /* glGenerateMipmap */
    if (pglGenerateMipmap) {
        trybegin("glGenerateMipmap");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGenerateMipmap(GL_TEXTURE_2D); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGenerateMipmap(GL_TEXTURE_2D); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGenerateMipmap",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGenerateMipmap: no proc */
    trybegin("glGenerateMipmap"); note("no proc: glGenerateMipmap"); rep("glGenerateMipmap",-1,-1,0,0);
    }
    /* glGetActiveUniform */
    if (pglGetActiveUniform) {
        trybegin("glGetActiveUniform");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetActiveUniform(g_prog, 0, 64, &s_len, &s_size, &s_type, s_name); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetActiveUniform(g_prog, 0, 64, &s_len, &s_size, &s_type, s_name); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetActiveUniform",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetActiveUniform: no proc */
    trybegin("glGetActiveUniform"); note("no proc: glGetActiveUniform"); rep("glGetActiveUniform",-1,-1,0,0);
    }
    /* glGetActiveUniformBlockName */
    if (pglGetActiveUniformBlockName) {
        trybegin("glGetActiveUniformBlockName");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetActiveUniformBlockName(g_prog, 0, 64, &s_len, s_name); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetActiveUniformBlockName(g_prog, 0, 64, &s_len, s_name); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetActiveUniformBlockName",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetActiveUniformBlockName: no proc */
    trybegin("glGetActiveUniformBlockName"); note("no proc: glGetActiveUniformBlockName"); rep("glGetActiveUniformBlockName",-1,-1,0,0);
    }
    /* glGetActiveUniformBlockiv */
    if (pglGetActiveUniformBlockiv) {
        trybegin("glGetActiveUniformBlockiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetActiveUniformBlockiv(g_prog, 0, GL_UNIFORM_BLOCK_DATA_SIZE, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetActiveUniformBlockiv(g_prog, 0, GL_UNIFORM_BLOCK_DATA_SIZE, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetActiveUniformBlockiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetActiveUniformBlockiv: no proc */
    trybegin("glGetActiveUniformBlockiv"); note("no proc: glGetActiveUniformBlockiv"); rep("glGetActiveUniformBlockiv",-1,-1,0,0);
    }
    /* glGetAttachedShaders SKIPPED 2026-10-09: crashes the guest (page worker
       TypeError reading length of null); TRY line is the repro. -2 = crashed. */
    note("SKIP glGetAttachedShaders: known crasher");
    trybegin("glGetAttachedShaders"); rep("glGetAttachedShaders",-2,-2,0,0);
    if (0) { /* orig: if (pglGetAttachedShaders) { */
        trybegin("glGetAttachedShaders");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetAttachedShaders(g_prog, 8, &s_len, s_sh); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetAttachedShaders(g_prog, 8, &s_len, s_sh); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetAttachedShaders",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetAttachedShaders: no proc */
    trybegin("glGetAttachedShaders"); note("no proc: glGetAttachedShaders"); rep("glGetAttachedShaders",-1,-1,0,0);
    }
    /* glGetAttribLocation */
    if (pglGetAttribLocation) {
        trybegin("glGetAttribLocation");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglGetAttribLocation(g_prog, "pos")); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglGetAttribLocation(g_prog, "pos")); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glGetAttribLocation ret=%lld", (long long)(intptr_t)_r1);
        note("glGetAttribLocation pos=%d", (int)_r1);
        rep("glGetAttribLocation",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetAttribLocation: no proc */
    trybegin("glGetAttribLocation"); note("no proc: glGetAttribLocation"); rep("glGetAttribLocation",-1,-1,0,0);
    }
    /* glGetBooleani_v */
    if (pglGetBooleani_v) {
        trybegin("glGetBooleani_v");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetBooleani_v(GL_BLEND, 0, &s_b); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetBooleani_v(GL_BLEND, 0, &s_b); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetBooleani_v",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetBooleani_v: no proc */
    trybegin("glGetBooleani_v"); note("no proc: glGetBooleani_v"); rep("glGetBooleani_v",-1,-1,0,0);
    }
    /* glGetBufferParameteriv */
    if (pglGetBufferParameteriv) {
        trybegin("glGetBufferParameteriv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetBufferParameteriv(GL_ARRAY_BUFFER, GL_BUFFER_SIZE, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetBufferParameteriv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetBufferParameteriv: no proc */
    trybegin("glGetBufferParameteriv"); note("no proc: glGetBufferParameteriv"); rep("glGetBufferParameteriv",-1,-1,0,0);
    }
    /* glGetBufferSubData */
    if (pglGetBufferSubData) {
        trybegin("glGetBufferSubData");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetBufferSubData(GL_ARRAY_BUFFER, 0, 16, s_tmp); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetBufferSubData(GL_ARRAY_BUFFER, 0, 16, s_tmp); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetBufferSubData",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetBufferSubData: no proc */
    trybegin("glGetBufferSubData"); note("no proc: glGetBufferSubData"); rep("glGetBufferSubData",-1,-1,0,0);
    }
    /* glGetCompressedTexImage */
    if (pglGetCompressedTexImage) {
        trybegin("glGetCompressedTexImage");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetCompressedTexImage(GL_TEXTURE_2D, 0, s_px); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetCompressedTexImage(GL_TEXTURE_2D, 0, s_px); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetCompressedTexImage",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetCompressedTexImage: no proc */
    trybegin("glGetCompressedTexImage"); note("no proc: glGetCompressedTexImage"); rep("glGetCompressedTexImage",-1,-1,0,0);
    }
    /* glGetDebugMessageLog */
    if (pglGetDebugMessageLog) {
        trybegin("glGetDebugMessageLog");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglGetDebugMessageLog(4, 256, s_src, s_typ, s_ids, s_sev, s_slen, s_log)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglGetDebugMessageLog(4, 256, s_src, s_typ, s_ids, s_sev, s_slen, s_log)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glGetDebugMessageLog ret=%lld", (long long)(intptr_t)_r1);
        rep("glGetDebugMessageLog",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetDebugMessageLog: no proc */
    trybegin("glGetDebugMessageLog"); note("no proc: glGetDebugMessageLog"); rep("glGetDebugMessageLog",-1,-1,0,0);
    }
    /* glGetError */
    if (pglGetError) {
        trybegin("glGetError");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglGetError()); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglGetError()); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glGetError ret=%lld", (long long)(intptr_t)_r1);
        rep("glGetError",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetError: no proc */
    trybegin("glGetError"); note("no proc: glGetError"); rep("glGetError",-1,-1,0,0);
    }
    /* glGetFloatv */
    if (pglGetFloatv) {
        trybegin("glGetFloatv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetFloatv(GL_VIEWPORT, s_f4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetFloatv(GL_VIEWPORT, s_f4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetFloatv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetFloatv: no proc */
    trybegin("glGetFloatv"); note("no proc: glGetFloatv"); rep("glGetFloatv",-1,-1,0,0);
    }
    /* glGetFragDataIndex */
    if (pglGetFragDataIndex) {
        trybegin("glGetFragDataIndex");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglGetFragDataIndex(g_prog, "outColor")); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglGetFragDataIndex(g_prog, "outColor")); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glGetFragDataIndex ret=%lld", (long long)(intptr_t)_r1);
        note("glGetFragDataIndex outColor=%d", (int)_r1);
        rep("glGetFragDataIndex",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetFragDataIndex: no proc */
    trybegin("glGetFragDataIndex"); note("no proc: glGetFragDataIndex"); rep("glGetFragDataIndex",-1,-1,0,0);
    }
    /* glGetFramebufferAttachmentParameteriv */
    if (pglGetFramebufferAttachmentParameteriv) {
        pglBindFramebuffer(GL_FRAMEBUFFER, g_fbo);
        trybegin("glGetFramebufferAttachmentParameteriv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetFramebufferAttachmentParameteriv(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_FRAMEBUFFER_ATTACHMENT_OBJECT_TYPE, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetFramebufferAttachmentParameteriv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
    /* glGetFramebufferAttachmentParameteriv: no proc */
    trybegin("glGetFramebufferAttachmentParameteriv"); note("no proc: glGetFramebufferAttachmentParameteriv"); rep("glGetFramebufferAttachmentParameteriv",-1,-1,0,0);
    }
    /* glGetIntegeri_v */
    if (pglGetIntegeri_v) {
        trybegin("glGetIntegeri_v");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetIntegeri_v(GL_MAX_VIEWPORT_DIMS, 0, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetIntegeri_v(GL_MAX_VIEWPORT_DIMS, 0, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetIntegeri_v",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetIntegeri_v: no proc */
    trybegin("glGetIntegeri_v"); note("no proc: glGetIntegeri_v"); rep("glGetIntegeri_v",-1,-1,0,0);
    }
    /* glGetIntegerv */
    if (pglGetIntegerv) {
        trybegin("glGetIntegerv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetIntegerv(GL_MAX_TEXTURE_SIZE, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetIntegerv(GL_MAX_TEXTURE_SIZE, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glGetIntegerv MAX_TEXTURE_SIZE=%d", s_int);
        rep("glGetIntegerv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetIntegerv: no proc */
    trybegin("glGetIntegerv"); note("no proc: glGetIntegerv"); rep("glGetIntegerv",-1,-1,0,0);
    }
    /* glGetMultisamplefv */
    if (pglGetMultisamplefv) {
        trybegin("glGetMultisamplefv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetMultisamplefv(GL_SAMPLE_POSITION, 0, s_f2); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetMultisamplefv(GL_SAMPLE_POSITION, 0, s_f2); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetMultisamplefv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetMultisamplefv: no proc */
    trybegin("glGetMultisamplefv"); note("no proc: glGetMultisamplefv"); rep("glGetMultisamplefv",-1,-1,0,0);
    }
    /* glGetProgramInfoLog */
    if (pglGetProgramInfoLog) {
        trybegin("glGetProgramInfoLog");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetProgramInfoLog(g_prog, 256, &s_len, s_log); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetProgramInfoLog(g_prog, 256, &s_len, s_log); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetProgramInfoLog",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetProgramInfoLog: no proc */
    trybegin("glGetProgramInfoLog"); note("no proc: glGetProgramInfoLog"); rep("glGetProgramInfoLog",-1,-1,0,0);
    }
    /* glGetProgramiv */
    if (pglGetProgramiv) {
        trybegin("glGetProgramiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetProgramiv(g_prog, GL_LINK_STATUS, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetProgramiv(g_prog, GL_LINK_STATUS, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetProgramiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetProgramiv: no proc */
    trybegin("glGetProgramiv"); note("no proc: glGetProgramiv"); rep("glGetProgramiv",-1,-1,0,0);
    }
    /* glGetQueryObjectiv */
    if (pglGetQueryObjectiv) {
        trybegin("glGetQueryObjectiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetQueryObjectiv(g_qry, GL_QUERY_RESULT_AVAILABLE, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetQueryObjectiv(g_qry, GL_QUERY_RESULT_AVAILABLE, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetQueryObjectiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetQueryObjectiv: no proc */
    trybegin("glGetQueryObjectiv"); note("no proc: glGetQueryObjectiv"); rep("glGetQueryObjectiv",-1,-1,0,0);
    }
    /* glGetQueryObjectui64v */
    if (pglGetQueryObjectui64v) {
        trybegin("glGetQueryObjectui64v");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetQueryObjectui64v(g_qry, GL_QUERY_RESULT_AVAILABLE, &s_u64); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetQueryObjectui64v(g_qry, GL_QUERY_RESULT_AVAILABLE, &s_u64); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetQueryObjectui64v",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetQueryObjectui64v: no proc */
    trybegin("glGetQueryObjectui64v"); note("no proc: glGetQueryObjectui64v"); rep("glGetQueryObjectui64v",-1,-1,0,0);
    }
    /* glGetQueryObjectuiv */
    if (pglGetQueryObjectuiv) {
        trybegin("glGetQueryObjectuiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetQueryObjectuiv(g_qry, GL_QUERY_RESULT_AVAILABLE, &s_u); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetQueryObjectuiv(g_qry, GL_QUERY_RESULT_AVAILABLE, &s_u); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetQueryObjectuiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetQueryObjectuiv: no proc */
    trybegin("glGetQueryObjectuiv"); note("no proc: glGetQueryObjectuiv"); rep("glGetQueryObjectuiv",-1,-1,0,0);
    }
    /* glGetQueryiv */
    if (pglGetQueryiv) {
        trybegin("glGetQueryiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetQueryiv(GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetQueryiv(GL_SAMPLES_PASSED, GL_QUERY_COUNTER_BITS, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetQueryiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetQueryiv: no proc */
    trybegin("glGetQueryiv"); note("no proc: glGetQueryiv"); rep("glGetQueryiv",-1,-1,0,0);
    }
    /* glGetRenderbufferParameteriv */
    if (pglGetRenderbufferParameteriv) {
        trybegin("glGetRenderbufferParameteriv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetRenderbufferParameteriv(GL_RENDERBUFFER, GL_RENDERBUFFER_WIDTH, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetRenderbufferParameteriv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetRenderbufferParameteriv: no proc */
    trybegin("glGetRenderbufferParameteriv"); note("no proc: glGetRenderbufferParameteriv"); rep("glGetRenderbufferParameteriv",-1,-1,0,0);
    }
    /* glGetSamplerParameterIiv */
    if (pglGetSamplerParameterIiv) {
        trybegin("glGetSamplerParameterIiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetSamplerParameterIiv(g_smp, GL_TEXTURE_MIN_FILTER, s_i4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetSamplerParameterIiv(g_smp, GL_TEXTURE_MIN_FILTER, s_i4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetSamplerParameterIiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetSamplerParameterIiv: no proc */
    trybegin("glGetSamplerParameterIiv"); note("no proc: glGetSamplerParameterIiv"); rep("glGetSamplerParameterIiv",-1,-1,0,0);
    }
    /* glGetSamplerParameterIuiv */
    if (pglGetSamplerParameterIuiv) {
        trybegin("glGetSamplerParameterIuiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetSamplerParameterIuiv(g_smp, GL_TEXTURE_MIN_FILTER, s_u4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetSamplerParameterIuiv(g_smp, GL_TEXTURE_MIN_FILTER, s_u4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetSamplerParameterIuiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetSamplerParameterIuiv: no proc */
    trybegin("glGetSamplerParameterIuiv"); note("no proc: glGetSamplerParameterIuiv"); rep("glGetSamplerParameterIuiv",-1,-1,0,0);
    }
    /* glGetSamplerParameterfv */
    if (pglGetSamplerParameterfv) {
        trybegin("glGetSamplerParameterfv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetSamplerParameterfv(g_smp, GL_TEXTURE_MIN_LOD, &s_f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetSamplerParameterfv(g_smp, GL_TEXTURE_MIN_LOD, &s_f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetSamplerParameterfv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetSamplerParameterfv: no proc */
    trybegin("glGetSamplerParameterfv"); note("no proc: glGetSamplerParameterfv"); rep("glGetSamplerParameterfv",-1,-1,0,0);
    }
    /* glGetSamplerParameteriv */
    if (pglGetSamplerParameteriv) {
        trybegin("glGetSamplerParameteriv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetSamplerParameteriv(g_smp, GL_TEXTURE_MIN_FILTER, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetSamplerParameteriv(g_smp, GL_TEXTURE_MIN_FILTER, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetSamplerParameteriv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetSamplerParameteriv: no proc */
    trybegin("glGetSamplerParameteriv"); note("no proc: glGetSamplerParameteriv"); rep("glGetSamplerParameteriv",-1,-1,0,0);
    }
    /* glGetShaderInfoLog */
    if (pglGetShaderInfoLog) {
        trybegin("glGetShaderInfoLog");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetShaderInfoLog(g_vs, 256, &s_len, s_log); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetShaderInfoLog(g_vs, 256, &s_len, s_log); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetShaderInfoLog",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetShaderInfoLog: no proc */
    trybegin("glGetShaderInfoLog"); note("no proc: glGetShaderInfoLog"); rep("glGetShaderInfoLog",-1,-1,0,0);
    }
    /* glGetShaderSource */
    if (pglGetShaderSource) {
        trybegin("glGetShaderSource");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetShaderSource(g_vs, 256, &s_len, s_log); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetShaderSource(g_vs, 256, &s_len, s_log); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetShaderSource",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetShaderSource: no proc */
    trybegin("glGetShaderSource"); note("no proc: glGetShaderSource"); rep("glGetShaderSource",-1,-1,0,0);
    }
    /* glGetShaderSourceImpl */
    if (pglGetShaderSourceImpl) {
        trybegin("glGetShaderSourceImpl");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetShaderSourceImpl(g_vs, 256, &s_len, s_log); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetShaderSourceImpl(g_vs, 256, &s_len, s_log); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetShaderSourceImpl",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetShaderSourceImpl: no proc */
    trybegin("glGetShaderSourceImpl"); note("no proc: glGetShaderSourceImpl"); rep("glGetShaderSourceImpl",-1,-1,0,0);
    }
    /* glGetShaderiv */
    if (pglGetShaderiv) {
        trybegin("glGetShaderiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetShaderiv(g_vs, GL_COMPILE_STATUS, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetShaderiv(g_vs, GL_COMPILE_STATUS, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetShaderiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetShaderiv: no proc */
    trybegin("glGetShaderiv"); note("no proc: glGetShaderiv"); rep("glGetShaderiv",-1,-1,0,0);
    }
    /* glGetString */
    if (pglGetString) {
        trybegin("glGetString");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglGetString(GL_VERSION)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglGetString(GL_VERSION)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glGetString=%.64s", _p1 ? (const char*)_p1 : "(null)");
        rep("glGetString",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetString: no proc */
    trybegin("glGetString"); note("no proc: glGetString"); rep("glGetString",-1,-1,0,0);
    }
    /* glGetStringi */
    if (pglGetStringi) {
        trybegin("glGetStringi");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglGetStringi(GL_EXTENSIONS, 0)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglGetStringi(GL_EXTENSIONS, 0)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glGetStringi=%.64s", _p1 ? (const char*)_p1 : "(null)");
        rep("glGetStringi",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetStringi: no proc */
    trybegin("glGetStringi"); note("no proc: glGetStringi"); rep("glGetStringi",-1,-1,0,0);
    }
    /* glGetSynciv */
    if (pglGetSynciv) {
        trybegin("glGetSynciv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetSynciv(g_sync, GL_SYNC_STATUS, 1, &s_len, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetSynciv(g_sync, GL_SYNC_STATUS, 1, &s_len, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetSynciv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetSynciv: no proc */
    trybegin("glGetSynciv"); note("no proc: glGetSynciv"); rep("glGetSynciv",-1,-1,0,0);
    }
    /* glGetTexImage */
    if (pglGetTexImage) {
        trybegin("glGetTexImage");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_px); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_px); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetTexImage",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetTexImage: no proc */
    trybegin("glGetTexImage"); note("no proc: glGetTexImage"); rep("glGetTexImage",-1,-1,0,0);
    }
    /* glGetTexLevelParameteriv */
    if (pglGetTexLevelParameteriv) {
        trybegin("glGetTexLevelParameteriv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetTexLevelParameteriv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetTexLevelParameteriv: no proc */
    trybegin("glGetTexLevelParameteriv"); note("no proc: glGetTexLevelParameteriv"); rep("glGetTexLevelParameteriv",-1,-1,0,0);
    }
    /* glGetTexParameteriv */
    if (pglGetTexParameteriv) {
        trybegin("glGetTexParameteriv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetTexParameteriv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetTexParameteriv: no proc */
    trybegin("glGetTexParameteriv"); note("no proc: glGetTexParameteriv"); rep("glGetTexParameteriv",-1,-1,0,0);
    }
    /* glGetTextureLevelParameteriv */
    if (pglGetTextureLevelParameteriv) {
        trybegin("glGetTextureLevelParameteriv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetTextureLevelParameteriv(g_tex, 0, GL_TEXTURE_WIDTH, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetTextureLevelParameteriv(g_tex, 0, GL_TEXTURE_WIDTH, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetTextureLevelParameteriv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetTextureLevelParameteriv: no proc */
    trybegin("glGetTextureLevelParameteriv"); note("no proc: glGetTextureLevelParameteriv"); rep("glGetTextureLevelParameteriv",-1,-1,0,0);
    }
    /* glGetTextureParameteriv */
    if (pglGetTextureParameteriv) {
        trybegin("glGetTextureParameteriv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetTextureParameteriv(g_tex, GL_TEXTURE_MIN_FILTER, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetTextureParameteriv(g_tex, GL_TEXTURE_MIN_FILTER, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetTextureParameteriv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetTextureParameteriv: no proc */
    trybegin("glGetTextureParameteriv"); note("no proc: glGetTextureParameteriv"); rep("glGetTextureParameteriv",-1,-1,0,0);
    }
    /* glGetUniformBlockIndex */
    if (pglGetUniformBlockIndex) {
        trybegin("glGetUniformBlockIndex");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglGetUniformBlockIndex(g_prog, "Block")); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglGetUniformBlockIndex(g_prog, "Block")); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glGetUniformBlockIndex ret=%lld", (long long)(intptr_t)_r1);
        note("glGetUniformBlockIndex Block=%u", (unsigned)_r1);
        rep("glGetUniformBlockIndex",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetUniformBlockIndex: no proc */
    trybegin("glGetUniformBlockIndex"); note("no proc: glGetUniformBlockIndex"); rep("glGetUniformBlockIndex",-1,-1,0,0);
    }
    /* glGetUniformLocation */
    if (pglGetUniformLocation) {
        trybegin("glGetUniformLocation");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglGetUniformLocation(g_prog, "u")); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglGetUniformLocation(g_prog, "u")); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glGetUniformLocation ret=%lld", (long long)(intptr_t)_r1);
        note("glGetUniformLocation u=%d", (int)_r1);
        rep("glGetUniformLocation",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetUniformLocation: no proc */
    trybegin("glGetUniformLocation"); note("no proc: glGetUniformLocation"); rep("glGetUniformLocation",-1,-1,0,0);
    }
    /* glGetUniformfv */
    if (pglGetUniformfv) {
        trybegin("glGetUniformfv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetUniformfv(g_prog, 0, s_f4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetUniformfv(g_prog, 0, s_f4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetUniformfv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetUniformfv: no proc */
    trybegin("glGetUniformfv"); note("no proc: glGetUniformfv"); rep("glGetUniformfv",-1,-1,0,0);
    }
    /* glGetUniformiv */
    if (pglGetUniformiv) {
        trybegin("glGetUniformiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetUniformiv(g_prog, 0, s_i4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetUniformiv(g_prog, 0, s_i4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetUniformiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetUniformiv: no proc */
    trybegin("glGetUniformiv"); note("no proc: glGetUniformiv"); rep("glGetUniformiv",-1,-1,0,0);
    }
    /* glGetUniformuiv */
    if (pglGetUniformuiv) {
        trybegin("glGetUniformuiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglGetUniformuiv(g_prog, 0, s_u4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglGetUniformuiv(g_prog, 0, s_u4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glGetUniformuiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glGetUniformuiv: no proc */
    trybegin("glGetUniformuiv"); note("no proc: glGetUniformuiv"); rep("glGetUniformuiv",-1,-1,0,0);
    }
    /* glHint */
    if (pglHint) {
        trybegin("glHint");
        pglGetError();
        uint64_t _t0=qpf_us(); pglHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glHint",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glHint: no proc */
    trybegin("glHint"); note("no proc: glHint"); rep("glHint",-1,-1,0,0);
    }
    /* glIsEnabledi */
    if (pglIsEnabledi) {
        trybegin("glIsEnabledi");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglIsEnabledi(GL_BLEND, 0)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglIsEnabledi(GL_BLEND, 0)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glIsEnabledi ret=%lld", (long long)(intptr_t)_r1);
        rep("glIsEnabledi",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glIsEnabledi: no proc */
    trybegin("glIsEnabledi"); note("no proc: glIsEnabledi"); rep("glIsEnabledi",-1,-1,0,0);
    }
    /* glIsFramebuffer */
    if (pglIsFramebuffer) {
        trybegin("glIsFramebuffer");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglIsFramebuffer(g_fbo)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglIsFramebuffer(g_fbo)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glIsFramebuffer ret=%lld", (long long)(intptr_t)_r1);
        rep("glIsFramebuffer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glIsFramebuffer: no proc */
    trybegin("glIsFramebuffer"); note("no proc: glIsFramebuffer"); rep("glIsFramebuffer",-1,-1,0,0);
    }
    /* glIsQuery */
    if (pglIsQuery) {
        trybegin("glIsQuery");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglIsQuery(g_qry)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglIsQuery(g_qry)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glIsQuery ret=%lld", (long long)(intptr_t)_r1);
        rep("glIsQuery",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glIsQuery: no proc */
    trybegin("glIsQuery"); note("no proc: glIsQuery"); rep("glIsQuery",-1,-1,0,0);
    }
    /* glIsRenderbuffer */
    if (pglIsRenderbuffer) {
        trybegin("glIsRenderbuffer");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglIsRenderbuffer(g_rb)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglIsRenderbuffer(g_rb)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glIsRenderbuffer ret=%lld", (long long)(intptr_t)_r1);
        rep("glIsRenderbuffer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glIsRenderbuffer: no proc */
    trybegin("glIsRenderbuffer"); note("no proc: glIsRenderbuffer"); rep("glIsRenderbuffer",-1,-1,0,0);
    }
    /* glIsSampler */
    if (pglIsSampler) {
        trybegin("glIsSampler");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglIsSampler(g_smp)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglIsSampler(g_smp)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glIsSampler ret=%lld", (long long)(intptr_t)_r1);
        rep("glIsSampler",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glIsSampler: no proc */
    trybegin("glIsSampler"); note("no proc: glIsSampler"); rep("glIsSampler",-1,-1,0,0);
    }
    /* glIsSync */
    if (pglIsSync) {
        trybegin("glIsSync");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglIsSync(g_sync)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglIsSync(g_sync)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glIsSync ret=%lld", (long long)(intptr_t)_r1);
        rep("glIsSync",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glIsSync: no proc */
    trybegin("glIsSync"); note("no proc: glIsSync"); rep("glIsSync",-1,-1,0,0);
    }
    /* glLightf */
    if (pglLightf) {
        trybegin("glLightf");
        pglGetError();
        uint64_t _t0=qpf_us(); pglLightf(GL_LIGHT0, GL_AMBIENT, 0.5f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglLightf(GL_LIGHT0, GL_AMBIENT, 0.5f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glLightf",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glLightf: no proc */
    trybegin("glLightf"); note("no proc: glLightf"); rep("glLightf",-1,-1,0,0);
    }
    /* glLightfv */
    if (pglLightfv) {
        trybegin("glLightfv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglLightfv(GL_LIGHT0, GL_AMBIENT, s_f4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglLightfv(GL_LIGHT0, GL_AMBIENT, s_f4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glLightfv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glLightfv: no proc */
    trybegin("glLightfv"); note("no proc: glLightfv"); rep("glLightfv",-1,-1,0,0);
    }
    /* glLineWidth */
    if (pglLineWidth) {
        trybegin("glLineWidth");
        pglGetError();
        uint64_t _t0=qpf_us(); pglLineWidth(1.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglLineWidth(1.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glLineWidth",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glLineWidth: no proc */
    trybegin("glLineWidth"); note("no proc: glLineWidth"); rep("glLineWidth",-1,-1,0,0);
    }
    /* glLinkProgram */
    if (pglLinkProgram) {
        trybegin("glLinkProgram");
        pglGetError();
        uint64_t _t0=qpf_us(); pglLinkProgram(g_prog); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglLinkProgram(g_prog); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glLinkProgram",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glLinkProgram: no proc */
    trybegin("glLinkProgram"); note("no proc: glLinkProgram"); rep("glLinkProgram",-1,-1,0,0);
    }
    /* glLoadIdentity */
    if (pglLoadIdentity) {
        trybegin("glLoadIdentity");
        pglGetError();
        uint64_t _t0=qpf_us(); pglLoadIdentity(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglLoadIdentity(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glLoadIdentity",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glLoadIdentity: no proc */
    trybegin("glLoadIdentity"); note("no proc: glLoadIdentity"); rep("glLoadIdentity",-1,-1,0,0);
    }

    /* glMapBufferRange: map+unmap x2 (unmap via quiet proc, not a handler) */
    if (pglMapBufferRange) {
        trybegin("glMapBufferRange"); pglGetError();
        uint64_t _t0=qpf_us(); const void *_m1=(const void*)pglMapBufferRange(GL_ARRAY_BUFFER, 0, 64, GL_MAP_READ_BIT); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        if (pglUnmapBuffer) pglUnmapBuffer(GL_ARRAY_BUFFER);
        uint64_t _t2=qpf_us(); const void *_m2=(const void*)pglMapBufferRange(GL_ARRAY_BUFFER, 0, 64, GL_MAP_READ_BIT); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        if (pglUnmapBuffer) pglUnmapBuffer(GL_ARRAY_BUFFER);
        note("glMapBufferRange=%p,%p", _m1, _m2);
        rep("glMapBufferRange",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglGetError();
    } else {
        trybegin("glMapBufferRange"); note("no proc: glMapBufferRange"); rep("glMapBufferRange",-1,-1,0,0);
    }
    /* glMaterialf */
    if (pglMaterialf) {
        trybegin("glMaterialf");
        pglGetError();
        uint64_t _t0=qpf_us(); pglMaterialf(GL_FRONT, GL_SHININESS, 50.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglMaterialf(GL_FRONT, GL_SHININESS, 50.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glMaterialf",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glMaterialf: no proc */
    trybegin("glMaterialf"); note("no proc: glMaterialf"); rep("glMaterialf",-1,-1,0,0);
    }
    /* glMaterialfv */
    if (pglMaterialfv) {
        trybegin("glMaterialfv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglMaterialfv(GL_FRONT, GL_SHININESS, &s_f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglMaterialfv(GL_FRONT, GL_SHININESS, &s_f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glMaterialfv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glMaterialfv: no proc */
    trybegin("glMaterialfv"); note("no proc: glMaterialfv"); rep("glMaterialfv",-1,-1,0,0);
    }
    /* glMatrixMode */
    if (pglMatrixMode) {
        trybegin("glMatrixMode");
        pglGetError();
        uint64_t _t0=qpf_us(); pglMatrixMode(GL_MODELVIEW); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglMatrixMode(GL_MODELVIEW); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glMatrixMode",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glMatrixMode: no proc */
    trybegin("glMatrixMode"); note("no proc: glMatrixMode"); rep("glMatrixMode",-1,-1,0,0);
    }
    /* glMinSampleShading */
    if (pglMinSampleShading) {
        trybegin("glMinSampleShading");
        pglGetError();
        uint64_t _t0=qpf_us(); pglMinSampleShading(0.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglMinSampleShading(0.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glMinSampleShading",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glMinSampleShading: no proc */
    trybegin("glMinSampleShading"); note("no proc: glMinSampleShading"); rep("glMinSampleShading",-1,-1,0,0);
    }
    /* glMultMatrixf */
    if (pglMultMatrixf) {
        trybegin("glMultMatrixf");
        pglGetError();
        uint64_t _t0=qpf_us(); pglMultMatrixf(s_m16); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglMultMatrixf(s_m16); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glMultMatrixf",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glMultMatrixf: no proc */
    trybegin("glMultMatrixf"); note("no proc: glMultMatrixf"); rep("glMultMatrixf",-1,-1,0,0);
    }
    /* glMultiDrawElementsBaseVertex */
    if (pglMultiDrawElementsBaseVertex) {
        trybegin("glMultiDrawElementsBaseVertex");
        pglGetError();
        uint64_t _t0=qpf_us(); pglMultiDrawElementsBaseVertex(mde_mode, mde_count, GL_UNSIGNED_SHORT, mde_idx, 1, mde_base); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglMultiDrawElementsBaseVertex(mde_mode, mde_count, GL_UNSIGNED_SHORT, mde_idx, 1, mde_base); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glMultiDrawElementsBaseVertex",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glMultiDrawElementsBaseVertex: no proc */
    trybegin("glMultiDrawElementsBaseVertex"); note("no proc: glMultiDrawElementsBaseVertex"); rep("glMultiDrawElementsBaseVertex",-1,-1,0,0);
    }
    /* glOrtho */
    if (pglOrtho) {
        trybegin("glOrtho");
        pglGetError();
        uint64_t _t0=qpf_us(); pglOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglOrtho(-1.0, 1.0, -1.0, 1.0, -1.0, 1.0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glOrtho",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glOrtho: no proc */
    trybegin("glOrtho"); note("no proc: glOrtho"); rep("glOrtho",-1,-1,0,0);
    }
    /* glPixelStorei */
    if (pglPixelStorei) {
        trybegin("glPixelStorei");
        pglGetError();
        uint64_t _t0=qpf_us(); pglPixelStorei(GL_UNPACK_ALIGNMENT, 4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglPixelStorei(GL_UNPACK_ALIGNMENT, 4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glPixelStorei",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glPixelStorei: no proc */
    trybegin("glPixelStorei"); note("no proc: glPixelStorei"); rep("glPixelStorei",-1,-1,0,0);
    }
    /* glPointParameterf */
    if (pglPointParameterf) {
        trybegin("glPointParameterf");
        pglGetError();
        uint64_t _t0=qpf_us(); pglPointParameterf(GL_POINT_SIZE_MIN, 1.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglPointParameterf(GL_POINT_SIZE_MIN, 1.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glPointParameterf",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glPointParameterf: no proc */
    trybegin("glPointParameterf"); note("no proc: glPointParameterf"); rep("glPointParameterf",-1,-1,0,0);
    }
    /* glPointParameterfv */
    if (pglPointParameterfv) {
        trybegin("glPointParameterfv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, s_f3); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglPointParameterfv(GL_POINT_DISTANCE_ATTENUATION, s_f3); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glPointParameterfv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glPointParameterfv: no proc */
    trybegin("glPointParameterfv"); note("no proc: glPointParameterfv"); rep("glPointParameterfv",-1,-1,0,0);
    }
    /* glPointParameteri */
    if (pglPointParameteri) {
        trybegin("glPointParameteri");
        pglGetError();
        uint64_t _t0=qpf_us(); pglPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_LOWER_LEFT); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglPointParameteri(GL_POINT_SPRITE_COORD_ORIGIN, GL_LOWER_LEFT); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glPointParameteri",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glPointParameteri: no proc */
    trybegin("glPointParameteri"); note("no proc: glPointParameteri"); rep("glPointParameteri",-1,-1,0,0);
    }
    /* glPointParameteriv */
    if (pglPointParameteriv) {
        trybegin("glPointParameteriv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglPointParameteriv(GL_POINT_SPRITE_COORD_ORIGIN, &s_i1); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglPointParameteriv(GL_POINT_SPRITE_COORD_ORIGIN, &s_i1); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glPointParameteriv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glPointParameteriv: no proc */
    trybegin("glPointParameteriv"); note("no proc: glPointParameteriv"); rep("glPointParameteriv",-1,-1,0,0);
    }
    /* glPolygonMode */
    if (pglPolygonMode) {
        trybegin("glPolygonMode");
        pglGetError();
        uint64_t _t0=qpf_us(); pglPolygonMode(GL_FRONT_AND_BACK, GL_FILL); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglPolygonMode(GL_FRONT_AND_BACK, GL_FILL); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glPolygonMode",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glPolygonMode: no proc */
    trybegin("glPolygonMode"); note("no proc: glPolygonMode"); rep("glPolygonMode",-1,-1,0,0);
    }
    /* glPolygonOffset */
    if (pglPolygonOffset) {
        trybegin("glPolygonOffset");
        pglGetError();
        uint64_t _t0=qpf_us(); pglPolygonOffset(1.0f, 1.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglPolygonOffset(1.0f, 1.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glPolygonOffset",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glPolygonOffset: no proc */
    trybegin("glPolygonOffset"); note("no proc: glPolygonOffset"); rep("glPolygonOffset",-1,-1,0,0);
    }
    /* glPolygonOffsetClamp */
    if (pglPolygonOffsetClamp) {
        trybegin("glPolygonOffsetClamp");
        pglGetError();
        uint64_t _t0=qpf_us(); pglPolygonOffsetClamp(1.0f, 1.0f, 0.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglPolygonOffsetClamp(1.0f, 1.0f, 0.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glPolygonOffsetClamp",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glPolygonOffsetClamp: no proc */
    trybegin("glPolygonOffsetClamp"); note("no proc: glPolygonOffsetClamp"); rep("glPolygonOffsetClamp",-1,-1,0,0);
    }
    /* glPopMatrix */
    if (pglPopMatrix) {
        trybegin("glPopMatrix");
        pglGetError();
        uint64_t _t0=qpf_us(); pglPopMatrix(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglPopMatrix(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glPopMatrix",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glPopMatrix: no proc */
    trybegin("glPopMatrix"); note("no proc: glPopMatrix"); rep("glPopMatrix",-1,-1,0,0);
    }
    /* glPushMatrix */
    if (pglPushMatrix) {
        trybegin("glPushMatrix");
        pglGetError();
        uint64_t _t0=qpf_us(); pglPushMatrix(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglPushMatrix(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glPushMatrix",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glPushMatrix: no proc */
    trybegin("glPushMatrix"); note("no proc: glPushMatrix"); rep("glPushMatrix",-1,-1,0,0);
    }
    /* glQueryCounter */
    if (pglQueryCounter) {
        trybegin("glQueryCounter");
        pglGetError();
        uint64_t _t0=qpf_us(); pglQueryCounter(g_qry2, GL_TIMESTAMP); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglQueryCounter(g_qry2, GL_TIMESTAMP); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glQueryCounter",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glQueryCounter: no proc */
    trybegin("glQueryCounter"); note("no proc: glQueryCounter"); rep("glQueryCounter",-1,-1,0,0);
    }
    /* glReadBuffer */
    if (pglReadBuffer) {
        trybegin("glReadBuffer");
        pglGetError();
        uint64_t _t0=qpf_us(); pglReadBuffer(GL_BACK); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglReadBuffer(GL_BACK); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glReadBuffer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glReadBuffer: no proc */
    trybegin("glReadBuffer"); note("no proc: glReadBuffer"); rep("glReadBuffer",-1,-1,0,0);
    }
    /* glReadPixels */
    if (pglReadPixels) {
        trybegin("glReadPixels");
        pglGetError();
        uint64_t _t0=qpf_us(); pglReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, s_px); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglReadPixels(0, 0, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, s_px); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glReadPixels",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glReadPixels: no proc */
    trybegin("glReadPixels"); note("no proc: glReadPixels"); rep("glReadPixels",-1,-1,0,0);
    }
    /* glRenderbufferStorage */
    if (pglRenderbufferStorage) {
        trybegin("glRenderbufferStorage");
        pglGetError();
        uint64_t _t0=qpf_us(); pglRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 64, 64); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglRenderbufferStorage(GL_RENDERBUFFER, GL_RGBA8, 64, 64); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glRenderbufferStorage",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glRenderbufferStorage: no proc */
    trybegin("glRenderbufferStorage"); note("no proc: glRenderbufferStorage"); rep("glRenderbufferStorage",-1,-1,0,0);
    }
    /* glRenderbufferStorageMultisample */
    if (pglRenderbufferStorageMultisample) {
        trybegin("glRenderbufferStorageMultisample");
        pglGetError();
        uint64_t _t0=qpf_us(); pglRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, 64, 64); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglRenderbufferStorageMultisample(GL_RENDERBUFFER, 4, GL_RGBA8, 64, 64); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glRenderbufferStorageMultisample",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glRenderbufferStorageMultisample: no proc */
    trybegin("glRenderbufferStorageMultisample"); note("no proc: glRenderbufferStorageMultisample"); rep("glRenderbufferStorageMultisample",-1,-1,0,0);
    }
    /* glRotatef */
    if (pglRotatef) {
        trybegin("glRotatef");
        pglGetError();
        uint64_t _t0=qpf_us(); pglRotatef(45.0f, 0.0f, 0.0f, 1.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglRotatef(45.0f, 0.0f, 0.0f, 1.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glRotatef",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glRotatef: no proc */
    trybegin("glRotatef"); note("no proc: glRotatef"); rep("glRotatef",-1,-1,0,0);
    }
    /* glSampleCoverage */
    if (pglSampleCoverage) {
        trybegin("glSampleCoverage");
        pglGetError();
        uint64_t _t0=qpf_us(); pglSampleCoverage(1.0f, GL_FALSE); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglSampleCoverage(1.0f, GL_FALSE); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glSampleCoverage",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glSampleCoverage: no proc */
    trybegin("glSampleCoverage"); note("no proc: glSampleCoverage"); rep("glSampleCoverage",-1,-1,0,0);
    }
    /* glSamplerParameterIiv */
    if (pglSamplerParameterIiv) {
        trybegin("glSamplerParameterIiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglSamplerParameterIiv(g_smp, GL_TEXTURE_MIN_FILTER, s_i4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglSamplerParameterIiv(g_smp, GL_TEXTURE_MIN_FILTER, s_i4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glSamplerParameterIiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glSamplerParameterIiv: no proc */
    trybegin("glSamplerParameterIiv"); note("no proc: glSamplerParameterIiv"); rep("glSamplerParameterIiv",-1,-1,0,0);
    }
    /* glSamplerParameterIuiv */
    if (pglSamplerParameterIuiv) {
        trybegin("glSamplerParameterIuiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglSamplerParameterIuiv(g_smp, GL_TEXTURE_MIN_FILTER, s_u4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglSamplerParameterIuiv(g_smp, GL_TEXTURE_MIN_FILTER, s_u4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glSamplerParameterIuiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glSamplerParameterIuiv: no proc */
    trybegin("glSamplerParameterIuiv"); note("no proc: glSamplerParameterIuiv"); rep("glSamplerParameterIuiv",-1,-1,0,0);
    }
    /* glSamplerParameterf */
    if (pglSamplerParameterf) {
        trybegin("glSamplerParameterf");
        pglGetError();
        uint64_t _t0=qpf_us(); pglSamplerParameterf(g_smp, GL_TEXTURE_MIN_LOD, 0.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglSamplerParameterf(g_smp, GL_TEXTURE_MIN_LOD, 0.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glSamplerParameterf",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glSamplerParameterf: no proc */
    trybegin("glSamplerParameterf"); note("no proc: glSamplerParameterf"); rep("glSamplerParameterf",-1,-1,0,0);
    }
    /* glSamplerParameterfv */
    if (pglSamplerParameterfv) {
        trybegin("glSamplerParameterfv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglSamplerParameterfv(g_smp, GL_TEXTURE_MIN_LOD, &s_f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglSamplerParameterfv(g_smp, GL_TEXTURE_MIN_LOD, &s_f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glSamplerParameterfv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glSamplerParameterfv: no proc */
    trybegin("glSamplerParameterfv"); note("no proc: glSamplerParameterfv"); rep("glSamplerParameterfv",-1,-1,0,0);
    }
    /* glSamplerParameteri */
    if (pglSamplerParameteri) {
        trybegin("glSamplerParameteri");
        pglGetError();
        uint64_t _t0=qpf_us(); pglSamplerParameteri(g_smp, GL_TEXTURE_MIN_FILTER, GL_LINEAR); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglSamplerParameteri(g_smp, GL_TEXTURE_MIN_FILTER, GL_LINEAR); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glSamplerParameteri",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glSamplerParameteri: no proc */
    trybegin("glSamplerParameteri"); note("no proc: glSamplerParameteri"); rep("glSamplerParameteri",-1,-1,0,0);
    }
    /* glSamplerParameteriv */
    if (pglSamplerParameteriv) {
        trybegin("glSamplerParameteriv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglSamplerParameteriv(g_smp, GL_TEXTURE_MIN_FILTER, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglSamplerParameteriv(g_smp, GL_TEXTURE_MIN_FILTER, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glSamplerParameteriv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glSamplerParameteriv: no proc */
    trybegin("glSamplerParameteriv"); note("no proc: glSamplerParameteriv"); rep("glSamplerParameteriv",-1,-1,0,0);
    }
    /* glScalef */
    if (pglScalef) {
        trybegin("glScalef");
        pglGetError();
        uint64_t _t0=qpf_us(); pglScalef(1.0f, 1.0f, 1.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglScalef(1.0f, 1.0f, 1.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glScalef",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glScalef: no proc */
    trybegin("glScalef"); note("no proc: glScalef"); rep("glScalef",-1,-1,0,0);
    }
    /* glScissor */
    if (pglScissor) {
        trybegin("glScissor");
        pglGetError();
        uint64_t _t0=qpf_us(); pglScissor(0, 0, 64, 64); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglScissor(0, 0, 64, 64); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glScissor",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glScissor: no proc */
    trybegin("glScissor"); note("no proc: glScissor"); rep("glScissor",-1,-1,0,0);
    }
    /* glShadeModel */
    if (pglShadeModel) {
        trybegin("glShadeModel");
        pglGetError();
        uint64_t _t0=qpf_us(); pglShadeModel(GL_SMOOTH); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglShadeModel(GL_SMOOTH); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glShadeModel",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glShadeModel: no proc */
    trybegin("glShadeModel"); note("no proc: glShadeModel"); rep("glShadeModel",-1,-1,0,0);
    }
    /* glShaderSource */
    if (pglShaderSource) {
        trybegin("glShaderSource");
        pglGetError();
        uint64_t _t0=qpf_us(); pglShaderSource(g_vs, 1, vs_src, NULL); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglShaderSource(g_vs, 1, vs_src, NULL); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glShaderSource",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glShaderSource: no proc */
    trybegin("glShaderSource"); note("no proc: glShaderSource"); rep("glShaderSource",-1,-1,0,0);
    }
    /* glStencilFunc */
    if (pglStencilFunc) {
        trybegin("glStencilFunc");
        pglGetError();
        uint64_t _t0=qpf_us(); pglStencilFunc(GL_ALWAYS, 0, 0xFF); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglStencilFunc(GL_ALWAYS, 0, 0xFF); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glStencilFunc",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glStencilFunc: no proc */
    trybegin("glStencilFunc"); note("no proc: glStencilFunc"); rep("glStencilFunc",-1,-1,0,0);
    }
    /* glStencilFuncSeparate */
    if (pglStencilFuncSeparate) {
        trybegin("glStencilFuncSeparate");
        pglGetError();
        uint64_t _t0=qpf_us(); pglStencilFuncSeparate(GL_FRONT, GL_ALWAYS, 0, 0xFF); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglStencilFuncSeparate(GL_FRONT, GL_ALWAYS, 0, 0xFF); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glStencilFuncSeparate",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glStencilFuncSeparate: no proc */
    trybegin("glStencilFuncSeparate"); note("no proc: glStencilFuncSeparate"); rep("glStencilFuncSeparate",-1,-1,0,0);
    }
    /* glStencilMask */
    if (pglStencilMask) {
        trybegin("glStencilMask");
        pglGetError();
        uint64_t _t0=qpf_us(); pglStencilMask(0xFF); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglStencilMask(0xFF); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glStencilMask",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glStencilMask: no proc */
    trybegin("glStencilMask"); note("no proc: glStencilMask"); rep("glStencilMask",-1,-1,0,0);
    }
    /* glStencilMaskSeparate */
    if (pglStencilMaskSeparate) {
        trybegin("glStencilMaskSeparate");
        pglGetError();
        uint64_t _t0=qpf_us(); pglStencilMaskSeparate(GL_FRONT, 0xFF); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglStencilMaskSeparate(GL_FRONT, 0xFF); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glStencilMaskSeparate",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glStencilMaskSeparate: no proc */
    trybegin("glStencilMaskSeparate"); note("no proc: glStencilMaskSeparate"); rep("glStencilMaskSeparate",-1,-1,0,0);
    }
    /* glStencilOp */
    if (pglStencilOp) {
        trybegin("glStencilOp");
        pglGetError();
        uint64_t _t0=qpf_us(); pglStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglStencilOp(GL_KEEP, GL_KEEP, GL_KEEP); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glStencilOp",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glStencilOp: no proc */
    trybegin("glStencilOp"); note("no proc: glStencilOp"); rep("glStencilOp",-1,-1,0,0);
    }
    /* glStencilOpSeparate */
    if (pglStencilOpSeparate) {
        trybegin("glStencilOpSeparate");
        pglGetError();
        uint64_t _t0=qpf_us(); pglStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_KEEP); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglStencilOpSeparate(GL_FRONT, GL_KEEP, GL_KEEP, GL_KEEP); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glStencilOpSeparate",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glStencilOpSeparate: no proc */
    trybegin("glStencilOpSeparate"); note("no proc: glStencilOpSeparate"); rep("glStencilOpSeparate",-1,-1,0,0);
    }
    /* glTexBuffer */
    if (pglTexBuffer) {
        trybegin("glTexBuffer");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, g_buf); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA32F, g_buf); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexBuffer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexBuffer: no proc */
    trybegin("glTexBuffer"); note("no proc: glTexBuffer"); rep("glTexBuffer",-1,-1,0,0);
    }
    /* glTexBufferARB */
    if (pglTexBufferARB) {
        trybegin("glTexBufferARB");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexBufferARB(GL_TEXTURE_BUFFER, GL_RGBA32F, g_buf); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexBufferARB(GL_TEXTURE_BUFFER, GL_RGBA32F, g_buf); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexBufferARB",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexBufferARB: no proc */
    trybegin("glTexBufferARB"); note("no proc: glTexBufferARB"); rep("glTexBufferARB",-1,-1,0,0);
    }
    /* glTexBufferRange */
    if (pglTexBufferRange) {
        trybegin("glTexBufferRange");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexBufferRange(GL_TEXTURE_BUFFER, GL_RGBA32F, g_buf, 0, 64); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexBufferRange(GL_TEXTURE_BUFFER, GL_RGBA32F, g_buf, 0, 64); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexBufferRange",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexBufferRange: no proc */
    trybegin("glTexBufferRange"); note("no proc: glTexBufferRange"); rep("glTexBufferRange",-1,-1,0,0);
    }
    /* glTexBufferRangeARB */
    if (pglTexBufferRangeARB) {
        trybegin("glTexBufferRangeARB");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexBufferRangeARB(GL_TEXTURE_BUFFER, GL_RGBA32F, g_buf, 0, 64); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexBufferRangeARB(GL_TEXTURE_BUFFER, GL_RGBA32F, g_buf, 0, 64); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexBufferRangeARB",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexBufferRangeARB: no proc */
    trybegin("glTexBufferRangeARB"); note("no proc: glTexBufferRangeARB"); rep("glTexBufferRangeARB",-1,-1,0,0);
    }
    /* glTexImage2D */
    if (pglTexImage2D) {
        trybegin("glTexImage2D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_px); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 64, 64, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_px); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexImage2D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexImage2D: no proc */
    trybegin("glTexImage2D"); note("no proc: glTexImage2D"); rep("glTexImage2D",-1,-1,0,0);
    }
    /* glTexImage2DMultisample */
    if (pglTexImage2DMultisample) {
        trybegin("glTexImage2DMultisample");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, 64, 64, GL_TRUE); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexImage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, 64, 64, GL_TRUE); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexImage2DMultisample",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexImage2DMultisample: no proc */
    trybegin("glTexImage2DMultisample"); note("no proc: glTexImage2DMultisample"); rep("glTexImage2DMultisample",-1,-1,0,0);
    }
    /* glTexImage3D */
    if (pglTexImage3D) {
        trybegin("glTexImage3D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_px3d); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8, 8, 8, 8, 0, GL_RGBA, GL_UNSIGNED_BYTE, s_px3d); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexImage3D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexImage3D: no proc */
    trybegin("glTexImage3D"); note("no proc: glTexImage3D"); rep("glTexImage3D",-1,-1,0,0);
    }
    /* glTexImage3DMultisample */
    if (pglTexImage3DMultisample) {
        trybegin("glTexImage3DMultisample");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexImage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 4, GL_RGBA8, 8, 8, 4, GL_TRUE); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexImage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 4, GL_RGBA8, 8, 8, 4, GL_TRUE); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexImage3DMultisample",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexImage3DMultisample: no proc */
    trybegin("glTexImage3DMultisample"); note("no proc: glTexImage3DMultisample"); rep("glTexImage3DMultisample",-1,-1,0,0);
    }
    /* glTexParameterf */
    if (pglTexParameterf) {
        trybegin("glTexParameterf");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 4.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_LOD, 4.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexParameterf",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexParameterf: no proc */
    trybegin("glTexParameterf"); note("no proc: glTexParameterf"); rep("glTexParameterf",-1,-1,0,0);
    }
    /* glTexParameteri */
    if (pglTexParameteri) {
        trybegin("glTexParameteri");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexParameteri",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexParameteri: no proc */
    trybegin("glTexParameteri"); note("no proc: glTexParameteri"); rep("glTexParameteri",-1,-1,0,0);
    }
    /* glTexStorage1D */
    if (pglTexStorage1D) {
        trybegin("glTexStorage1D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexStorage1D(GL_TEXTURE_1D, 1, GL_RGBA8, 64); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexStorage1D(GL_TEXTURE_1D, 1, GL_RGBA8, 64); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexStorage1D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexStorage1D: no proc */
    trybegin("glTexStorage1D"); note("no proc: glTexStorage1D"); rep("glTexStorage1D",-1,-1,0,0);
    }
    /* glTexStorage2D */
    if (pglTexStorage2D) {
        trybegin("glTexStorage2D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 64, 64); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexStorage2D(GL_TEXTURE_2D, 1, GL_RGBA8, 64, 64); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexStorage2D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexStorage2D: no proc */
    trybegin("glTexStorage2D"); note("no proc: glTexStorage2D"); rep("glTexStorage2D",-1,-1,0,0);
    }
    /* glTexStorage2DMultisample */
    if (pglTexStorage2DMultisample) {
        trybegin("glTexStorage2DMultisample");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, 64, 64, GL_TRUE); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexStorage2DMultisample(GL_TEXTURE_2D_MULTISAMPLE, 4, GL_RGBA8, 64, 64, GL_TRUE); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexStorage2DMultisample",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexStorage2DMultisample: no proc */
    trybegin("glTexStorage2DMultisample"); note("no proc: glTexStorage2DMultisample"); rep("glTexStorage2DMultisample",-1,-1,0,0);
    }
    /* glTexStorage3D */
    if (pglTexStorage3D) {
        trybegin("glTexStorage3D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, 8, 8, 8); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexStorage3D(GL_TEXTURE_3D, 1, GL_RGBA8, 8, 8, 8); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexStorage3D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexStorage3D: no proc */
    trybegin("glTexStorage3D"); note("no proc: glTexStorage3D"); rep("glTexStorage3D",-1,-1,0,0);
    }
    /* glTexStorage3DMultisample */
    if (pglTexStorage3DMultisample) {
        trybegin("glTexStorage3DMultisample");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexStorage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 4, GL_RGBA8, 8, 8, 4, GL_TRUE); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexStorage3DMultisample(GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 4, GL_RGBA8, 8, 8, 4, GL_TRUE); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexStorage3DMultisample",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexStorage3DMultisample: no proc */
    trybegin("glTexStorage3DMultisample"); note("no proc: glTexStorage3DMultisample"); rep("glTexStorage3DMultisample",-1,-1,0,0);
    }
    /* glTexSubImage2D */
    if (pglTexSubImage2D) {
        trybegin("glTexSubImage2D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, s_px); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 8, 8, GL_RGBA, GL_UNSIGNED_BYTE, s_px); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexSubImage2D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexSubImage2D: no proc */
    trybegin("glTexSubImage2D"); note("no proc: glTexSubImage2D"); rep("glTexSubImage2D",-1,-1,0,0);
    }
    /* glTexSubImage3D */
    if (pglTexSubImage3D) {
        trybegin("glTexSubImage3D");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 4, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, s_px3d); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTexSubImage3D(GL_TEXTURE_3D, 0, 0, 0, 0, 4, 4, 4, GL_RGBA, GL_UNSIGNED_BYTE, s_px3d); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTexSubImage3D",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTexSubImage3D: no proc */
    trybegin("glTexSubImage3D"); note("no proc: glTexSubImage3D"); rep("glTexSubImage3D",-1,-1,0,0);
    }
    /* glTextureBarrier */
    if (pglTextureBarrier) {
        trybegin("glTextureBarrier");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTextureBarrier(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTextureBarrier(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTextureBarrier",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTextureBarrier: no proc */
    trybegin("glTextureBarrier"); note("no proc: glTextureBarrier"); rep("glTextureBarrier",-1,-1,0,0);
    }
    /* glTextureBarrierNV */
    if (pglTextureBarrierNV) {
        trybegin("glTextureBarrierNV");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTextureBarrierNV(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTextureBarrierNV(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTextureBarrierNV",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTextureBarrierNV: no proc */
    trybegin("glTextureBarrierNV"); note("no proc: glTextureBarrierNV"); rep("glTextureBarrierNV",-1,-1,0,0);
    }
    /* glTransformFeedbackVaryings */
    if (pglTransformFeedbackVaryings) {
        trybegin("glTransformFeedbackVaryings");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTransformFeedbackVaryings(g_prog, 1, tf_vary, GL_INTERLEAVED_ATTRIBS); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTransformFeedbackVaryings(g_prog, 1, tf_vary, GL_INTERLEAVED_ATTRIBS); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTransformFeedbackVaryings",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTransformFeedbackVaryings: no proc */
    trybegin("glTransformFeedbackVaryings"); note("no proc: glTransformFeedbackVaryings"); rep("glTransformFeedbackVaryings",-1,-1,0,0);
    }
    /* glTranslatef */
    if (pglTranslatef) {
        trybegin("glTranslatef");
        pglGetError();
        uint64_t _t0=qpf_us(); pglTranslatef(1.0f, 2.0f, 3.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglTranslatef(1.0f, 2.0f, 3.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glTranslatef",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glTranslatef: no proc */
    trybegin("glTranslatef"); note("no proc: glTranslatef"); rep("glTranslatef",-1,-1,0,0);
    }
    /* glUniform1f */
    if (pglUniform1f) {
        trybegin("glUniform1f");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform1f(0, 1.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform1f(0, 1.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform1f",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform1f: no proc */
    trybegin("glUniform1f"); note("no proc: glUniform1f"); rep("glUniform1f",-1,-1,0,0);
    }
    /* glUniform1fv */
    if (pglUniform1fv) {
        trybegin("glUniform1fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform1fv(0, 1, &s_f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform1fv(0, 1, &s_f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform1fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform1fv: no proc */
    trybegin("glUniform1fv"); note("no proc: glUniform1fv"); rep("glUniform1fv",-1,-1,0,0);
    }
    /* glUniform1i */
    if (pglUniform1i) {
        trybegin("glUniform1i");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform1i(0, 1); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform1i(0, 1); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform1i",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform1i: no proc */
    trybegin("glUniform1i"); note("no proc: glUniform1i"); rep("glUniform1i",-1,-1,0,0);
    }
    /* glUniform1iv */
    if (pglUniform1iv) {
        trybegin("glUniform1iv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform1iv(0, 1, &s_int); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform1iv(0, 1, &s_int); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform1iv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform1iv: no proc */
    trybegin("glUniform1iv"); note("no proc: glUniform1iv"); rep("glUniform1iv",-1,-1,0,0);
    }
    /* glUniform1uiv */
    if (pglUniform1uiv) {
        trybegin("glUniform1uiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform1uiv(0, 1, &s_u); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform1uiv(0, 1, &s_u); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform1uiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform1uiv: no proc */
    trybegin("glUniform1uiv"); note("no proc: glUniform1uiv"); rep("glUniform1uiv",-1,-1,0,0);
    }
    /* glUniform2f */
    if (pglUniform2f) {
        trybegin("glUniform2f");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform2f(0, 1.0f, 2.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform2f(0, 1.0f, 2.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform2f",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform2f: no proc */
    trybegin("glUniform2f"); note("no proc: glUniform2f"); rep("glUniform2f",-1,-1,0,0);
    }
    /* glUniform2fv */
    if (pglUniform2fv) {
        trybegin("glUniform2fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform2fv(0, 1, s_f2); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform2fv(0, 1, s_f2); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform2fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform2fv: no proc */
    trybegin("glUniform2fv"); note("no proc: glUniform2fv"); rep("glUniform2fv",-1,-1,0,0);
    }
    /* glUniform2i */
    if (pglUniform2i) {
        trybegin("glUniform2i");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform2i(0, 1, 2); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform2i(0, 1, 2); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform2i",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform2i: no proc */
    trybegin("glUniform2i"); note("no proc: glUniform2i"); rep("glUniform2i",-1,-1,0,0);
    }
    /* glUniform2iv */
    if (pglUniform2iv) {
        trybegin("glUniform2iv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform2iv(0, 1, s_i2); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform2iv(0, 1, s_i2); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform2iv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform2iv: no proc */
    trybegin("glUniform2iv"); note("no proc: glUniform2iv"); rep("glUniform2iv",-1,-1,0,0);
    }
    /* glUniform2uiv */
    if (pglUniform2uiv) {
        trybegin("glUniform2uiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform2uiv(0, 1, s_u2); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform2uiv(0, 1, s_u2); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform2uiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform2uiv: no proc */
    trybegin("glUniform2uiv"); note("no proc: glUniform2uiv"); rep("glUniform2uiv",-1,-1,0,0);
    }
    /* glUniform3f */
    if (pglUniform3f) {
        trybegin("glUniform3f");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform3f(0, 1.0f, 2.0f, 3.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform3f(0, 1.0f, 2.0f, 3.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform3f",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform3f: no proc */
    trybegin("glUniform3f"); note("no proc: glUniform3f"); rep("glUniform3f",-1,-1,0,0);
    }
    /* glUniform3fv */
    if (pglUniform3fv) {
        trybegin("glUniform3fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform3fv(0, 1, s_f3); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform3fv(0, 1, s_f3); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform3fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform3fv: no proc */
    trybegin("glUniform3fv"); note("no proc: glUniform3fv"); rep("glUniform3fv",-1,-1,0,0);
    }
    /* glUniform3i */
    if (pglUniform3i) {
        trybegin("glUniform3i");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform3i(0, 1, 2, 3); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform3i(0, 1, 2, 3); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform3i",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform3i: no proc */
    trybegin("glUniform3i"); note("no proc: glUniform3i"); rep("glUniform3i",-1,-1,0,0);
    }
    /* glUniform3iv */
    if (pglUniform3iv) {
        trybegin("glUniform3iv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform3iv(0, 1, s_i3); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform3iv(0, 1, s_i3); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform3iv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform3iv: no proc */
    trybegin("glUniform3iv"); note("no proc: glUniform3iv"); rep("glUniform3iv",-1,-1,0,0);
    }
    /* glUniform3uiv */
    if (pglUniform3uiv) {
        trybegin("glUniform3uiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform3uiv(0, 1, s_u3); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform3uiv(0, 1, s_u3); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform3uiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform3uiv: no proc */
    trybegin("glUniform3uiv"); note("no proc: glUniform3uiv"); rep("glUniform3uiv",-1,-1,0,0);
    }
    /* glUniform4f */
    if (pglUniform4f) {
        trybegin("glUniform4f");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform4f(0, 1.0f, 2.0f, 3.0f, 4.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform4f(0, 1.0f, 2.0f, 3.0f, 4.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform4f",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform4f: no proc */
    trybegin("glUniform4f"); note("no proc: glUniform4f"); rep("glUniform4f",-1,-1,0,0);
    }
    /* glUniform4fv */
    if (pglUniform4fv) {
        trybegin("glUniform4fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform4fv(0, 1, s_f4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform4fv(0, 1, s_f4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform4fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform4fv: no proc */
    trybegin("glUniform4fv"); note("no proc: glUniform4fv"); rep("glUniform4fv",-1,-1,0,0);
    }
    /* glUniform4i */
    if (pglUniform4i) {
        trybegin("glUniform4i");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform4i(0, 1, 2, 3, 4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform4i(0, 1, 2, 3, 4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform4i",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform4i: no proc */
    trybegin("glUniform4i"); note("no proc: glUniform4i"); rep("glUniform4i",-1,-1,0,0);
    }
    /* glUniform4iv */
    if (pglUniform4iv) {
        trybegin("glUniform4iv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform4iv(0, 1, s_i4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform4iv(0, 1, s_i4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform4iv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform4iv: no proc */
    trybegin("glUniform4iv"); note("no proc: glUniform4iv"); rep("glUniform4iv",-1,-1,0,0);
    }
    /* glUniform4uiv */
    if (pglUniform4uiv) {
        trybegin("glUniform4uiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniform4uiv(0, 1, s_u4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniform4uiv(0, 1, s_u4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniform4uiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniform4uiv: no proc */
    trybegin("glUniform4uiv"); note("no proc: glUniform4uiv"); rep("glUniform4uiv",-1,-1,0,0);
    }
    /* glUniformBlockBinding */
    if (pglUniformBlockBinding) {
        trybegin("glUniformBlockBinding");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniformBlockBinding(g_prog, 0, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniformBlockBinding(g_prog, 0, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniformBlockBinding",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniformBlockBinding: no proc */
    trybegin("glUniformBlockBinding"); note("no proc: glUniformBlockBinding"); rep("glUniformBlockBinding",-1,-1,0,0);
    }
    /* glUniformMatrix2fv */
    if (pglUniformMatrix2fv) {
        trybegin("glUniformMatrix2fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniformMatrix2fv(0, 1, GL_FALSE, s_f4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniformMatrix2fv(0, 1, GL_FALSE, s_f4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniformMatrix2fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniformMatrix2fv: no proc */
    trybegin("glUniformMatrix2fv"); note("no proc: glUniformMatrix2fv"); rep("glUniformMatrix2fv",-1,-1,0,0);
    }
    /* glUniformMatrix3fv */
    if (pglUniformMatrix3fv) {
        trybegin("glUniformMatrix3fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniformMatrix3fv(0, 1, GL_FALSE, s_f9); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniformMatrix3fv(0, 1, GL_FALSE, s_f9); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniformMatrix3fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniformMatrix3fv: no proc */
    trybegin("glUniformMatrix3fv"); note("no proc: glUniformMatrix3fv"); rep("glUniformMatrix3fv",-1,-1,0,0);
    }
    /* glUniformMatrix4fv */
    if (pglUniformMatrix4fv) {
        trybegin("glUniformMatrix4fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUniformMatrix4fv(0, 1, GL_FALSE, s_m16); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUniformMatrix4fv(0, 1, GL_FALSE, s_m16); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUniformMatrix4fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glUniformMatrix4fv: no proc */
    trybegin("glUniformMatrix4fv"); note("no proc: glUniformMatrix4fv"); rep("glUniformMatrix4fv",-1,-1,0,0);
    }
    /* glUseProgram */
    if (pglUseProgram) {
        trybegin("glUseProgram");
        pglGetError();
        uint64_t _t0=qpf_us(); pglUseProgram(g_prog); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglUseProgram(g_prog); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glUseProgram",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
        pglUseProgram(g_prog);
    } else {
    /* glUseProgram: no proc */
    trybegin("glUseProgram"); note("no proc: glUseProgram"); rep("glUseProgram",-1,-1,0,0);
    }
    /* glValidateProgram */
    if (pglValidateProgram) {
        trybegin("glValidateProgram");
        pglGetError();
        uint64_t _t0=qpf_us(); pglValidateProgram(g_prog); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglValidateProgram(g_prog); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glValidateProgram",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glValidateProgram: no proc */
    trybegin("glValidateProgram"); note("no proc: glValidateProgram"); rep("glValidateProgram",-1,-1,0,0);
    }
    /* glVersionMode */
    if (pglVersionMode) {
        trybegin("glVersionMode");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglVersionMode()); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglVersionMode()); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glVersionMode ret=%lld", (long long)(intptr_t)_r1);
        note("glVersionMode=%d", (int)_r1);
        rep("glVersionMode",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVersionMode: no proc */
    trybegin("glVersionMode"); note("no proc: glVersionMode"); rep("glVersionMode",-1,-1,0,0);
    }
    /* glVertexAttrib1d */
    if (pglVertexAttrib1d) {
        trybegin("glVertexAttrib1d");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib1d(0, 1.0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib1d(0, 1.0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib1d",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib1d: no proc */
    trybegin("glVertexAttrib1d"); note("no proc: glVertexAttrib1d"); rep("glVertexAttrib1d",-1,-1,0,0);
    }
    /* glVertexAttrib1dv */
    if (pglVertexAttrib1dv) {
        trybegin("glVertexAttrib1dv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib1dv(0, &s_d1); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib1dv(0, &s_d1); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib1dv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib1dv: no proc */
    trybegin("glVertexAttrib1dv"); note("no proc: glVertexAttrib1dv"); rep("glVertexAttrib1dv",-1,-1,0,0);
    }
    /* glVertexAttrib1f */
    if (pglVertexAttrib1f) {
        trybegin("glVertexAttrib1f");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib1f(0, 1.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib1f(0, 1.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib1f",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib1f: no proc */
    trybegin("glVertexAttrib1f"); note("no proc: glVertexAttrib1f"); rep("glVertexAttrib1f",-1,-1,0,0);
    }
    /* glVertexAttrib1fv */
    if (pglVertexAttrib1fv) {
        trybegin("glVertexAttrib1fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib1fv(0, &s_f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib1fv(0, &s_f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib1fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib1fv: no proc */
    trybegin("glVertexAttrib1fv"); note("no proc: glVertexAttrib1fv"); rep("glVertexAttrib1fv",-1,-1,0,0);
    }
    /* glVertexAttrib2d */
    if (pglVertexAttrib2d) {
        trybegin("glVertexAttrib2d");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib2d(0, 1.0, 2.0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib2d(0, 1.0, 2.0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib2d",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib2d: no proc */
    trybegin("glVertexAttrib2d"); note("no proc: glVertexAttrib2d"); rep("glVertexAttrib2d",-1,-1,0,0);
    }
    /* glVertexAttrib2dv */
    if (pglVertexAttrib2dv) {
        trybegin("glVertexAttrib2dv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib2dv(0, s_d2); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib2dv(0, s_d2); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib2dv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib2dv: no proc */
    trybegin("glVertexAttrib2dv"); note("no proc: glVertexAttrib2dv"); rep("glVertexAttrib2dv",-1,-1,0,0);
    }
    /* glVertexAttrib2f */
    if (pglVertexAttrib2f) {
        trybegin("glVertexAttrib2f");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib2f(0, 1.0f, 2.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib2f(0, 1.0f, 2.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib2f",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib2f: no proc */
    trybegin("glVertexAttrib2f"); note("no proc: glVertexAttrib2f"); rep("glVertexAttrib2f",-1,-1,0,0);
    }
    /* glVertexAttrib2fv */
    if (pglVertexAttrib2fv) {
        trybegin("glVertexAttrib2fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib2fv(0, s_f2); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib2fv(0, s_f2); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib2fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib2fv: no proc */
    trybegin("glVertexAttrib2fv"); note("no proc: glVertexAttrib2fv"); rep("glVertexAttrib2fv",-1,-1,0,0);
    }
    /* glVertexAttrib3d */
    if (pglVertexAttrib3d) {
        trybegin("glVertexAttrib3d");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib3d(0, 1.0, 2.0, 3.0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib3d(0, 1.0, 2.0, 3.0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib3d",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib3d: no proc */
    trybegin("glVertexAttrib3d"); note("no proc: glVertexAttrib3d"); rep("glVertexAttrib3d",-1,-1,0,0);
    }
    /* glVertexAttrib3dv */
    if (pglVertexAttrib3dv) {
        trybegin("glVertexAttrib3dv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib3dv(0, s_d3); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib3dv(0, s_d3); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib3dv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib3dv: no proc */
    trybegin("glVertexAttrib3dv"); note("no proc: glVertexAttrib3dv"); rep("glVertexAttrib3dv",-1,-1,0,0);
    }
    /* glVertexAttrib3f */
    if (pglVertexAttrib3f) {
        trybegin("glVertexAttrib3f");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib3f(0, 1.0f, 2.0f, 3.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib3f(0, 1.0f, 2.0f, 3.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib3f",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib3f: no proc */
    trybegin("glVertexAttrib3f"); note("no proc: glVertexAttrib3f"); rep("glVertexAttrib3f",-1,-1,0,0);
    }
    /* glVertexAttrib3fv */
    if (pglVertexAttrib3fv) {
        trybegin("glVertexAttrib3fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib3fv(0, s_f3); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib3fv(0, s_f3); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib3fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib3fv: no proc */
    trybegin("glVertexAttrib3fv"); note("no proc: glVertexAttrib3fv"); rep("glVertexAttrib3fv",-1,-1,0,0);
    }
    /* glVertexAttrib4d */
    if (pglVertexAttrib4d) {
        trybegin("glVertexAttrib4d");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib4d(0, 1.0, 2.0, 3.0, 4.0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib4d(0, 1.0, 2.0, 3.0, 4.0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib4d",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib4d: no proc */
    trybegin("glVertexAttrib4d"); note("no proc: glVertexAttrib4d"); rep("glVertexAttrib4d",-1,-1,0,0);
    }
    /* glVertexAttrib4dv */
    if (pglVertexAttrib4dv) {
        trybegin("glVertexAttrib4dv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib4dv(0, s_d4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib4dv(0, s_d4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib4dv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib4dv: no proc */
    trybegin("glVertexAttrib4dv"); note("no proc: glVertexAttrib4dv"); rep("glVertexAttrib4dv",-1,-1,0,0);
    }
    /* glVertexAttrib4f */
    if (pglVertexAttrib4f) {
        trybegin("glVertexAttrib4f");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib4f(0, 1.0f, 2.0f, 3.0f, 4.0f); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib4f(0, 1.0f, 2.0f, 3.0f, 4.0f); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib4f",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib4f: no proc */
    trybegin("glVertexAttrib4f"); note("no proc: glVertexAttrib4f"); rep("glVertexAttrib4f",-1,-1,0,0);
    }
    /* glVertexAttrib4fv */
    if (pglVertexAttrib4fv) {
        trybegin("glVertexAttrib4fv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttrib4fv(0, s_f4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttrib4fv(0, s_f4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttrib4fv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttrib4fv: no proc */
    trybegin("glVertexAttrib4fv"); note("no proc: glVertexAttrib4fv"); rep("glVertexAttrib4fv",-1,-1,0,0);
    }
    /* glVertexAttribDivisor */
    if (pglVertexAttribDivisor) {
        trybegin("glVertexAttribDivisor");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttribDivisor(0, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttribDivisor(0, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttribDivisor",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttribDivisor: no proc */
    trybegin("glVertexAttribDivisor"); note("no proc: glVertexAttribDivisor"); rep("glVertexAttribDivisor",-1,-1,0,0);
    }
    /* glVertexAttribI4i */
    if (pglVertexAttribI4i) {
        trybegin("glVertexAttribI4i");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttribI4i(0, 1, 2, 3, 4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttribI4i(0, 1, 2, 3, 4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttribI4i",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttribI4i: no proc */
    trybegin("glVertexAttribI4i"); note("no proc: glVertexAttribI4i"); rep("glVertexAttribI4i",-1,-1,0,0);
    }
    /* glVertexAttribI4iv */
    if (pglVertexAttribI4iv) {
        trybegin("glVertexAttribI4iv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttribI4iv(0, s_i4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttribI4iv(0, s_i4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttribI4iv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttribI4iv: no proc */
    trybegin("glVertexAttribI4iv"); note("no proc: glVertexAttribI4iv"); rep("glVertexAttribI4iv",-1,-1,0,0);
    }
    /* glVertexAttribI4ui */
    if (pglVertexAttribI4ui) {
        trybegin("glVertexAttribI4ui");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttribI4ui(0, 1, 2, 3, 4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttribI4ui(0, 1, 2, 3, 4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttribI4ui",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttribI4ui: no proc */
    trybegin("glVertexAttribI4ui"); note("no proc: glVertexAttribI4ui"); rep("glVertexAttribI4ui",-1,-1,0,0);
    }
    /* glVertexAttribI4uiv */
    if (pglVertexAttribI4uiv) {
        trybegin("glVertexAttribI4uiv");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttribI4uiv(0, s_u4); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttribI4uiv(0, s_u4); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttribI4uiv",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttribI4uiv: no proc */
    trybegin("glVertexAttribI4uiv"); note("no proc: glVertexAttribI4uiv"); rep("glVertexAttribI4uiv",-1,-1,0,0);
    }
    /* glVertexAttribIPointer */
    if (pglVertexAttribIPointer) {
        trybegin("glVertexAttribIPointer");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttribIPointer(0, 1, GL_INT, 0, (const void*)0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttribIPointer(0, 1, GL_INT, 0, (const void*)0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttribIPointer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttribIPointer: no proc */
    trybegin("glVertexAttribIPointer"); note("no proc: glVertexAttribIPointer"); rep("glVertexAttribIPointer",-1,-1,0,0);
    }
    /* glVertexAttribPointer */
    if (pglVertexAttribPointer) {
        trybegin("glVertexAttribPointer");
        pglGetError();
        uint64_t _t0=qpf_us(); pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (const void*)0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 0, (const void*)0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glVertexAttribPointer",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glVertexAttribPointer: no proc */
    trybegin("glVertexAttribPointer"); note("no proc: glVertexAttribPointer"); rep("glVertexAttribPointer",-1,-1,0,0);
    }
    /* glViewport */
    if (pglViewport) {
        trybegin("glViewport");
        pglGetError();
        uint64_t _t0=qpf_us(); pglViewport(0, 0, 480, 360); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglViewport(0, 0, 480, 360); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glViewport",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glViewport: no proc */
    trybegin("glViewport"); note("no proc: glViewport"); rep("glViewport",-1,-1,0,0);
    }
    /* glWaitSync */
    if (pglWaitSync) {
        trybegin("glWaitSync");
        pglGetError();
        uint64_t _t0=qpf_us(); pglWaitSync(g_sync, 0, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglWaitSync(g_sync, 0, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glWaitSync",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glWaitSync: no proc */
    trybegin("glWaitSync"); note("no proc: glWaitSync"); rep("glWaitSync",-1,-1,0,0);
    }
    /* glXChooseFBConfig */
    if (pglXChooseFBConfig) {
        trybegin("glXChooseFBConfig");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglXChooseFBConfig(NULL, 0, NULL, &s_int)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglXChooseFBConfig(NULL, 0, NULL, &s_int)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXChooseFBConfig=%p", _p1);
        rep("glXChooseFBConfig",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXChooseFBConfig: no proc */
    trybegin("glXChooseFBConfig"); note("no proc: glXChooseFBConfig"); rep("glXChooseFBConfig",-1,-1,0,0);
    }
    /* glXChooseVisual */
    if (pglXChooseVisual) {
        trybegin("glXChooseVisual");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglXChooseVisual(NULL, 0, NULL)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglXChooseVisual(NULL, 0, NULL)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXChooseVisual=%p", _p1);
        rep("glXChooseVisual",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXChooseVisual: no proc */
    trybegin("glXChooseVisual"); note("no proc: glXChooseVisual"); rep("glXChooseVisual",-1,-1,0,0);
    }
    /* glXCreateContext */
    if (pglXCreateContext) {
        trybegin("glXCreateContext");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglXCreateContext(NULL, NULL, NULL, 0)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglXCreateContext(NULL, NULL, NULL, 0)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXCreateContext=%p", _p1);
        rep("glXCreateContext",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXCreateContext: no proc */
    trybegin("glXCreateContext"); note("no proc: glXCreateContext"); rep("glXCreateContext",-1,-1,0,0);
    }
    /* glXCreateContextAttribsARB */
    if (pglXCreateContextAttribsARB) {
        trybegin("glXCreateContextAttribsARB");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglXCreateContextAttribsARB(NULL, NULL, NULL, 0, NULL)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglXCreateContextAttribsARB(NULL, NULL, NULL, 0, NULL)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXCreateContextAttribsARB=%p", _p1);
        rep("glXCreateContextAttribsARB",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXCreateContextAttribsARB: no proc */
    trybegin("glXCreateContextAttribsARB"); note("no proc: glXCreateContextAttribsARB"); rep("glXCreateContextAttribsARB",-1,-1,0,0);
    }
    /* glXDestroyContext */
    if (pglXDestroyContext) {
        trybegin("glXDestroyContext");
        pglGetError();
        uint64_t _t0=qpf_us(); pglXDestroyContext(NULL, NULL); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglXDestroyContext(NULL, NULL); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glXDestroyContext",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXDestroyContext: no proc */
    trybegin("glXDestroyContext"); note("no proc: glXDestroyContext"); rep("glXDestroyContext",-1,-1,0,0);
    }
    /* glXGetClientString */
    if (pglXGetClientString) {
        trybegin("glXGetClientString");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglXGetClientString(NULL, 0)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglXGetClientString(NULL, 0)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXGetClientString=%.64s", _p1 ? (const char*)_p1 : "(null)");
        rep("glXGetClientString",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXGetClientString: no proc */
    trybegin("glXGetClientString"); note("no proc: glXGetClientString"); rep("glXGetClientString",-1,-1,0,0);
    }
    /* glXGetConfig */
    if (pglXGetConfig) {
        trybegin("glXGetConfig");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglXGetConfig(NULL, NULL, 0, &s_int)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglXGetConfig(NULL, NULL, 0, &s_int)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXGetConfig ret=%lld", (long long)(intptr_t)_r1);
        rep("glXGetConfig",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXGetConfig: no proc */
    trybegin("glXGetConfig"); note("no proc: glXGetConfig"); rep("glXGetConfig",-1,-1,0,0);
    }
    /* glXGetCurrentContext */
    if (pglXGetCurrentContext) {
        trybegin("glXGetCurrentContext");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglXGetCurrentContext()); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglXGetCurrentContext()); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXGetCurrentContext=%p", _p1);
        rep("glXGetCurrentContext",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXGetCurrentContext: no proc */
    trybegin("glXGetCurrentContext"); note("no proc: glXGetCurrentContext"); rep("glXGetCurrentContext",-1,-1,0,0);
    }
    /* glXGetCurrentDrawable */
    if (pglXGetCurrentDrawable) {
        trybegin("glXGetCurrentDrawable");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglXGetCurrentDrawable()); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglXGetCurrentDrawable()); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXGetCurrentDrawable ret=%lld", (long long)(intptr_t)_r1);
        rep("glXGetCurrentDrawable",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXGetCurrentDrawable: no proc */
    trybegin("glXGetCurrentDrawable"); note("no proc: glXGetCurrentDrawable"); rep("glXGetCurrentDrawable",-1,-1,0,0);
    }
    /* glXGetFBConfigAttrib */
    if (pglXGetFBConfigAttrib) {
        trybegin("glXGetFBConfigAttrib");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglXGetFBConfigAttrib(NULL, NULL, 0, &s_int)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglXGetFBConfigAttrib(NULL, NULL, 0, &s_int)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXGetFBConfigAttrib ret=%lld", (long long)(intptr_t)_r1);
        rep("glXGetFBConfigAttrib",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXGetFBConfigAttrib: no proc */
    trybegin("glXGetFBConfigAttrib"); note("no proc: glXGetFBConfigAttrib"); rep("glXGetFBConfigAttrib",-1,-1,0,0);
    }
    /* glXGetFBConfigs */
    if (pglXGetFBConfigs) {
        trybegin("glXGetFBConfigs");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglXGetFBConfigs(NULL, 0, &s_int)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglXGetFBConfigs(NULL, 0, &s_int)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXGetFBConfigs=%p", _p1);
        rep("glXGetFBConfigs",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXGetFBConfigs: no proc */
    trybegin("glXGetFBConfigs"); note("no proc: glXGetFBConfigs"); rep("glXGetFBConfigs",-1,-1,0,0);
    }
    /* glXGetVisualFromFBConfig */
    if (pglXGetVisualFromFBConfig) {
        trybegin("glXGetVisualFromFBConfig");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglXGetVisualFromFBConfig(NULL, NULL)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglXGetVisualFromFBConfig(NULL, NULL)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXGetVisualFromFBConfig=%p", _p1);
        rep("glXGetVisualFromFBConfig",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXGetVisualFromFBConfig: no proc */
    trybegin("glXGetVisualFromFBConfig"); note("no proc: glXGetVisualFromFBConfig"); rep("glXGetVisualFromFBConfig",-1,-1,0,0);
    }
    /* glXIsDirect */
    if (pglXIsDirect) {
        trybegin("glXIsDirect");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglXIsDirect(NULL, NULL)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglXIsDirect(NULL, NULL)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXIsDirect ret=%lld", (long long)(intptr_t)_r1);
        rep("glXIsDirect",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXIsDirect: no proc */
    trybegin("glXIsDirect"); note("no proc: glXIsDirect"); rep("glXIsDirect",-1,-1,0,0);
    }
    /* glXMakeContextCurrent */
    if (pglXMakeContextCurrent) {
        trybegin("glXMakeContextCurrent");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglXMakeContextCurrent(NULL, 0, 0, NULL)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglXMakeContextCurrent(NULL, 0, 0, NULL)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXMakeContextCurrent ret=%lld", (long long)(intptr_t)_r1);
        rep("glXMakeContextCurrent",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXMakeContextCurrent: no proc */
    trybegin("glXMakeContextCurrent"); note("no proc: glXMakeContextCurrent"); rep("glXMakeContextCurrent",-1,-1,0,0);
    }
    /* glXMakeCurrent */
    if (pglXMakeCurrent) {
        trybegin("glXMakeCurrent");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglXMakeCurrent(NULL, 0, NULL)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglXMakeCurrent(NULL, 0, NULL)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXMakeCurrent ret=%lld", (long long)(intptr_t)_r1);
        rep("glXMakeCurrent",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXMakeCurrent: no proc */
    trybegin("glXMakeCurrent"); note("no proc: glXMakeCurrent"); rep("glXMakeCurrent",-1,-1,0,0);
    }
    /* glXQueryExtension */
    if (pglXQueryExtension) {
        trybegin("glXQueryExtension");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglXQueryExtension(NULL, &s_int, &s_int2)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglXQueryExtension(NULL, &s_int, &s_int2)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXQueryExtension ret=%lld", (long long)(intptr_t)_r1);
        rep("glXQueryExtension",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXQueryExtension: no proc */
    trybegin("glXQueryExtension"); note("no proc: glXQueryExtension"); rep("glXQueryExtension",-1,-1,0,0);
    }
    /* glXQueryExtensionsString */
    if (pglXQueryExtensionsString) {
        trybegin("glXQueryExtensionsString");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglXQueryExtensionsString(NULL, 0)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglXQueryExtensionsString(NULL, 0)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXQueryExtensionsString=%.64s", _p1 ? (const char*)_p1 : "(null)");
        rep("glXQueryExtensionsString",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXQueryExtensionsString: no proc */
    trybegin("glXQueryExtensionsString"); note("no proc: glXQueryExtensionsString"); rep("glXQueryExtensionsString",-1,-1,0,0);
    }
    /* glXQueryServerString */
    if (pglXQueryServerString) {
        trybegin("glXQueryServerString");
        pglGetError();
        uint64_t _t0=qpf_us(); const void *_p1=(const void*)(pglXQueryServerString(NULL, 0, 0)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); const void *_p2=(const void*)(pglXQueryServerString(NULL, 0, 0)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXQueryServerString=%.64s", _p1 ? (const char*)_p1 : "(null)");
        rep("glXQueryServerString",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXQueryServerString: no proc */
    trybegin("glXQueryServerString"); note("no proc: glXQueryServerString"); rep("glXQueryServerString",-1,-1,0,0);
    }
    /* glXQueryVersion */
    if (pglXQueryVersion) {
        trybegin("glXQueryVersion");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglXQueryVersion(NULL, &s_int, &s_int2)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglXQueryVersion(NULL, &s_int, &s_int2)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXQueryVersion ret=%lld", (long long)(intptr_t)_r1);
        rep("glXQueryVersion",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXQueryVersion: no proc */
    trybegin("glXQueryVersion"); note("no proc: glXQueryVersion"); rep("glXQueryVersion",-1,-1,0,0);
    }
    /* glXSwapBuffers */
    if (pglXSwapBuffers) {
        trybegin("glXSwapBuffers");
        pglGetError();
        uint64_t _t0=qpf_us(); pglXSwapBuffers(NULL, 0); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglXSwapBuffers(NULL, 0); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glXSwapBuffers",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXSwapBuffers: no proc */
    trybegin("glXSwapBuffers"); note("no proc: glXSwapBuffers"); rep("glXSwapBuffers",-1,-1,0,0);
    }
    /* glXSwapIntervalEXT */
    if (pglXSwapIntervalEXT) {
        trybegin("glXSwapIntervalEXT");
        pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)(pglXSwapIntervalEXT(NULL, 0, 0)); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); long long _r2=(long long)(intptr_t)(pglXSwapIntervalEXT(NULL, 0, 0)); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        note("glXSwapIntervalEXT ret=%lld", (long long)(intptr_t)_r1);
        rep("glXSwapIntervalEXT",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXSwapIntervalEXT: no proc */
    trybegin("glXSwapIntervalEXT"); note("no proc: glXSwapIntervalEXT"); rep("glXSwapIntervalEXT",-1,-1,0,0);
    }
    /* glXWaitGL */
    if (pglXWaitGL) {
        trybegin("glXWaitGL");
        pglGetError();
        uint64_t _t0=qpf_us(); pglXWaitGL(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglXWaitGL(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glXWaitGL",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXWaitGL: no proc */
    trybegin("glXWaitGL"); note("no proc: glXWaitGL"); rep("glXWaitGL",-1,-1,0,0);
    }
    /* glXWaitX */
    if (pglXWaitX) {
        trybegin("glXWaitX");
        pglGetError();
        uint64_t _t0=qpf_us(); pglXWaitX(); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        uint64_t _t2=qpf_us(); pglXWaitX(); GLenum _e2=pglGetError(); uint64_t _t3=qpf_us();
        rep("glXWaitX",(long long)_e1,(long long)_e2,_t1-_t0,_t3-_t2);
    } else {
    /* glXWaitX: no proc */
    trybegin("glXWaitX"); note("no proc: glXWaitX"); rep("glXWaitX",-1,-1,0,0);
    }
    /* traceProc: internal bridge tracer (no guest entry point expected) */
    if (ptraceProc) {
        trybegin("traceProc"); pglGetError();
        uint64_t _t0=qpf_us(); long long _r1=(long long)(intptr_t)ptraceProc("glcov", 1); GLenum _e1=pglGetError(); uint64_t _t1=qpf_us();
        note("traceProc ret=%lld", _r1);
        rep("traceProc",(long long)_e1,(long long)_e1,_t1-_t0,0);
    } else {
        trybegin("traceProc"); note("no proc: traceProc (internal bridge tracer)"); rep("traceProc",-1,-1,0,0);
    }
}
static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_CLOSE || m == WM_DESTROY) { PostQuitMessage(0); return 0; }
    return DefWindowProc(h, m, w, l);
}
int main(void) {
    WNDCLASSA wc = {0};
    wc.style = CS_OWNDC;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = GetModuleHandleA(NULL);
    wc.lpszClassName = "glcov";
    RegisterClassA(&wc);
    HWND hwnd = CreateWindowA("glcov", "Boxedwine64 GL coverage",
                              WS_OVERLAPPEDWINDOW | WS_VISIBLE,
                              CW_USEDEFAULT, CW_USEDEFAULT, 480, 360,
                              0, 0, wc.hInstance, 0);
    if (!hwnd) { fprintf(stderr, "glcov: RESULT 1\n"); return 1; }
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
        fprintf(stderr, "glcov: RESULT 1\n"); return 1;
    }
    HGLRC rc = wglCreateContext(hdc);
    if (!rc || !wglMakeCurrent(hdc, rc)) {
        fprintf(stderr, "glcov: RESULT 1\n"); return 1;
    }
    g_gl32 = GetModuleHandleA("opengl32.dll");
    load_all();
    if (!pglGetError) { fprintf(stderr, "glcov: RESULT 1\n"); return 1; }
    setup_gl();
    run_all();
    teardown_gl();
    wglMakeCurrent(0, 0);
    wglDeleteContext(rc);
    ReleaseDC(hwnd, hdc);
    DestroyWindow(hwnd);
    fprintf(stderr, "glcov: RESULT 0\n");
    return 0;
}
