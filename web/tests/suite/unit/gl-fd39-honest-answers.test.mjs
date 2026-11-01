import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

// FD-39: the texture settings FD-15 left dropped, plus the packed
// pixel format it left unmodelled.
//  - GL_UNSIGNED_SHORT_5_6_5_REV (0x8364) was uploaded still packed;
//    the real glesTexFormatFixup now unpacks it (the _REV layout packs
//    components from the least significant bit, like the _REV entries
//    FD-15 already pinned).
//  - LOD bias / GENERATE_MIPMAP / border color / depth-texture mode /
//    texture swizzle were dropped so a refusal could not poison the
//    error slot wined3d polls. The bridge now owns that closed set:
//    sets are validated and stored, queries answer from the store with
//    spec defaults, and a valid set never reaches the host or the
//    error slot. The checks below drive the REAL extracted functions
//    and dispatcher cases and fail on the pre-FD-39 code.

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const bridgePath = join(root, 'source/opengl/gl64bridge.cpp');
const guestMemPath = join(root, 'source/opengl/glguestmemory.h');
const src = readFileSync(bridgePath, 'utf8');

function extractFunction(signature) {
  const start = src.indexOf(signature);
  assert.ok(start >= 0, `function ${signature} is present`);
  const open = src.indexOf('{', start);
  let depth = 0;
  for (let j = open; j < src.length; j++) {
    if (src[j] === '{') depth++;
    else if (src[j] === '}') { depth--; if (!depth) return src.slice(start, j + 1); }
  }
  assert.fail(`unterminated function ${signature}`);
}

function extractFunctionOr(signature, fallback) {
  try { return extractFunction(signature); } catch { return fallback; }
}

function extractSlice(startMarker, endMarker) {
  const start = src.indexOf(startMarker);
  assert.ok(start >= 0, `slice start ${startMarker} is present`);
  const end = src.indexOf(endMarker, start);
  assert.ok(end >= 0, `slice end ${endMarker} is present`);
  return src.slice(start, end + endMarker.length);
}

function extractCase(label) {
  const marker = `case ${label}:`;
  const start = src.indexOf(marker);
  assert.ok(start >= 0, `dispatcher case ${label} is present`);
  const rest = src.slice(start + marker.length);
  const m = rest.search(/\n        case GL64_fn_|\n#endif/);
  assert.ok(m >= 0, `dispatcher case ${label} terminates`);
  return src.slice(start, start + marker.length + m);
}

const PREAMBLE = `
#include <cstdint>
#include <cstddef>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <tuple>
#include <vector>
using U8=uint8_t; using U16=uint16_t; using U32=uint32_t; using U64=uint64_t;
using S8=int8_t; using S16=int16_t; using S32=int32_t;
using GLenum=uint32_t; using GLuint=uint32_t; using GLint=int32_t;
using GLsizei=int32_t; using GLfloat=float; using GLboolean=uint8_t;
constexpr GLenum GL_NO_ERROR=0x0000;
constexpr GLenum GL_INVALID_ENUM=0x0500;
constexpr GLenum GL_INVALID_VALUE=0x0501;
constexpr GLenum GL_INVALID_OPERATION=0x0502;
constexpr GLenum GL_BYTE=0x1400, GL_UNSIGNED_BYTE=0x1401, GL_SHORT=0x1402,
  GL_UNSIGNED_SHORT=0x1403, GL_INT=0x1404, GL_UNSIGNED_INT=0x1405,
  GL_FLOAT=0x1406, GL_DOUBLE=0x140A, GL_RGB=0x1907, GL_RGBA=0x1908,
  GL_TEXTURE_2D=0x0DE1;
static int g_checks=0, g_failures=0;
#define CHECK(cond) do{ g_checks++; if(!(cond)){ g_failures++; fprintf(stderr,"CHECK FAILED line %d: %s\\n",__LINE__,#cond); } } while(0)
static void klog_fmt(const char*, ...) {}

struct KMemory {
  U8 bytes[65536]{};
  void memcpyFromGuest(void* out,U64 p,U64 n){ if(p+n>sizeof bytes){ fprintf(stderr,"guest read out of bounds\\n"); exit(3);} memcpy(out,bytes+p,(size_t)n); }
  void memcpyToGuest(U64 p,const void* in,U64 n){ if(p+n>sizeof bytes){ fprintf(stderr,"guest write out of bounds\\n"); exit(3);} memcpy(bytes+p,in,(size_t)n); }
  void writed(U64 p,U32 v){ memcpyToGuest(p,&v,4); }
  void writeq(U64 p,U64 v){ memcpyToGuest(p,&v,8); }
  U32 readd(U64 p) const { U32 v; memcpy(&v,bytes+p,4); return v; }
  U64 readq(U64 p) const { U64 v; memcpy(&v,bytes+p,8); return v; }
};
struct KMemory64 : KMemory {};
#include "${guestMemPath}"

struct GL64Args { U64 a[16]; };
inline U64 ai(const GL64Args& a, int i) { return a.a[i]; }
inline float af(const GL64Args& a, int i) { float f; U32 u=(U32)a.a[i]; memcpy(&f,&u,4); return f; }
static int g_glContextObj;
static void* g_glContext = &g_glContextObj;
#define GL_MT(stmt) do { stmt; } while (0)
#define glOnMain(fn) do { (fn)(); } while (0)
enum {
  GL64_fn_glTexParameteri=241, GL64_fn_glTexParameterf=242,
  GL64_fn_glGetTexParameteriv=372
};

// The store keys on the bound texture id; the harness pins it.
static GLuint g_boundStub = 7;
GLuint boundTexture(GLenum) { return g_boundStub; }

// Host GL stubs: record forwarding; the query stub answers a marker
// no stored value uses, so tests can tell who answered.
static int g_hostTexParamCalls = 0;
static void glTexParameteri(GLenum, GLenum, GLint) { g_hostTexParamCalls++; }
static void glTexParameterf(GLenum, GLenum, GLfloat) { g_hostTexParamCalls++; }
static void glGetTexParameteriv(GLenum, GLenum, GLint* p) { *p = 0x777; }

GLenum g_allocationError = GL_NO_ERROR;
`;

const EXTRA = [
  extractFunction('static bool gl64NoteOnce('),
  extractFunction('void recordBridgeError('),
  extractFunction('GLenum hostTextureTarget(GLenum target) {'),
  extractFunction('bool texParamSupported(GLenum pname) {'),
  extractFunction('void glesTexFormatFixup(GLint& ifmt, GLenum& fmt, GLenum& type, std::vector<U8>& pix) {'),
].join('\n');

// The FD-39 store block. Absent on the pre-fix code: substitute the old
// observable behaviour (nothing is classified as stored, nothing
// round-trips) so the behavioural CHECKs fail individually instead of
// the file aborting during setup.
const FD39_SRC = (() => {
  try {
    return extractSlice('// FD-39: texture parameters neither backend', '// Component bit widths of a sized internal format');
  } catch {
    return `
      bool texParamStored(GLenum) { return false; }
      GLenum validateStoredTexParam(GLenum, double) { return GL_INVALID_ENUM; }
      void setStoredTexParam(GLenum, GLenum, double) {}
      void getStoredTexParam(GLenum, GLenum, GLint& out) { out = -777; }
    `;
  }
})();

const CASES = [
  extractCase('GL64_fn_glTexParameteri'),
  extractCase('GL64_fn_glTexParameterf'),
  extractCase('GL64_fn_glGetTexParameteriv'),
].join('\n');

const MAIN = `
static GL64Args A(U64 a0=0,U64 a1=0,U64 a2=0,U64 a3=0){ GL64Args r{}; r.a[0]=a0; r.a[1]=a1; r.a[2]=a2; r.a[3]=a3; return r; }
static GL64Args AF(U64 a0, U64 a1, float f){ GL64Args r{}; r.a[0]=a0; r.a[1]=a1; U32 u; memcpy(&u,&f,4); r.a[2]=u; return r; }
static constexpr U64 OUT=0x1800;

int main(){
  KMemory64 mem; GLGuestMemory gm(&mem); GLGuestCPU cpu{&gm};
  auto call=[&](U64 fn,const GL64Args& a){ return dispatch(&cpu,a,fn); };

  // ---- 5_6_5_REV unpacks with the _REV layout -------------------------
  {
    GLint ifmt=0x8051; GLenum fmt=GL_RGB, type=0x8364; // RGB8 <- RGB / UNSIGNED_SHORT_5_6_5_REV
    // texels (LE words): 0x001F red, 0x07E0 green, 0xF800 blue, 0xFFFF white, 0x0010 half red
    std::vector<U8> pix={0x1F,0x00, 0xE0,0x07, 0x00,0xF8, 0xFF,0xFF, 0x10,0x00};
    glesTexFormatFixup(ifmt, fmt, type, pix);
    CHECK(type==GL_UNSIGNED_BYTE && fmt==GL_RGB && ifmt==0x8051);
    CHECK(pix.size()==15);
    CHECK(pix[0]==255 && pix[1]==0 && pix[2]==0);
    CHECK(pix[3]==0 && pix[4]==255 && pix[5]==0);
    CHECK(pix[6]==0 && pix[7]==0 && pix[8]==255);
    CHECK(pix[9]==255 && pix[10]==255 && pix[11]==255);
    CHECK(pix[12]==132 && pix[13]==0 && pix[14]==0);
  }
  {
    // Control: the same bytes under the non-REV type mean something
    // else (0x001F is blue there) -- the REV model is genuinely distinct.
    GLint ifmt=0x8051; GLenum fmt=GL_RGB, type=0x8363;
    std::vector<U8> pix={0x1F,0x00};
    glesTexFormatFixup(ifmt, fmt, type, pix);
    CHECK(pix.size()==3 && pix[0]==0 && pix[1]==0 && pix[2]==255);
  }

  // ---- the stored set: classification, validation, defaults -----------
  CHECK(texParamStored(0x8501)); CHECK(texParamStored(0x8c55));
  CHECK(texParamStored(0x100d)); CHECK(texParamStored(0x884b));
  CHECK(texParamStored(0x8e42)); CHECK(texParamStored(0x8e46));
  CHECK(!texParamStored(0x2800)); CHECK(!texParamStored(0x9999));
  CHECK(validateStoredTexParam(0x884b, 0x1903)==GL_NO_ERROR);
  CHECK(validateStoredTexParam(0x884b, 0x9999)==GL_INVALID_ENUM);
  CHECK(validateStoredTexParam(0x8e42, 1)==GL_NO_ERROR);
  CHECK(validateStoredTexParam(0x8e42, 0x9999)==GL_INVALID_ENUM);
  CHECK(validateStoredTexParam(0x8501, -3.5)==GL_NO_ERROR);
  CHECK(validateStoredTexParam(0x8c55, 7)==GL_NO_ERROR);
  {
    GLint v=-1;
    getStoredTexParam(0x0DE1, 0x8e43, v); CHECK(v==0x1904); // SWIZZLE_G default GREEN
    getStoredTexParam(0x0DE1, 0x884b, v); CHECK(v==0x1909); // DEPTH_TEXTURE_MODE default LUMINANCE
    getStoredTexParam(0x0DE1, 0x8c55, v); CHECK(v==0);      // GENERATE_MIPMAP default FALSE
    setStoredTexParam(0x0DE1, 0x8e43, 0.0);
    getStoredTexParam(0x0DE1, 0x8e43, v); CHECK(v==0);
    setStoredTexParam(0x0DE1, 0x884b, 0x1903);
    getStoredTexParam(0x0DE1, 0x884b, v); CHECK(v==0x1903);
    setStoredTexParam(0x0DE1, 0x8c55, 1.0);
    getStoredTexParam(0x0DE1, 0x8c55, v); CHECK(v==1);
    setStoredTexParam(0x0DE1, 0x8501, 2.0);
    getStoredTexParam(0x0DE1, 0x8501, v); CHECK(v==2);
    g_boundStub=9; // a different texture still sees spec defaults
    getStoredTexParam(0x0DE1, 0x884b, v); CHECK(v==0x1909);
    g_boundStub=7;
  }

  // ---- dispatcher: stored sets never touch host or error slot ---------
  g_allocationError=GL_NO_ERROR; g_hostTexParamCalls=0;
  call(GL64_fn_glTexParameteri, A(0x0DE1, 0x884b, 0x9999));
  CHECK(g_allocationError==GL_INVALID_ENUM);
  CHECK(g_hostTexParamCalls==0);
  g_allocationError=GL_NO_ERROR;
  call(GL64_fn_glTexParameteri, A(0x0DE1, 0x884b, 0x1903));
  CHECK(g_allocationError==GL_NO_ERROR);
  CHECK(g_hostTexParamCalls==0);
  call(GL64_fn_glGetTexParameteriv, A(0x0DE1, 0x884b, OUT));
  CHECK((GLint)mem.readd(OUT)==0x1903);
  // Forwarded pnames behave exactly as before: set reaches the host,
  // query is answered by the host (marker 0x777).
  call(GL64_fn_glTexParameteri, A(0x0DE1, 0x2800, 0x2601));
  CHECK(g_hostTexParamCalls==1);
  call(GL64_fn_glGetTexParameteriv, A(0x0DE1, 0x2800, OUT));
  CHECK((GLint)mem.readd(OUT)==0x777);
  // Float entry point: LOD bias stores through it, host untouched.
  g_allocationError=GL_NO_ERROR;
  call(GL64_fn_glTexParameterf, AF(0x0DE1, 0x8501, 1.5f));
  CHECK(g_allocationError==GL_NO_ERROR);
  CHECK(g_hostTexParamCalls==1);
  call(GL64_fn_glGetTexParameteriv, A(0x0DE1, 0x8501, OUT));
  CHECK((GLint)mem.readd(OUT)==1);

  printf("FD39 HARNESS: %d checks, %d failures\\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
`;

function compileAndRun(cpp) {
  const dir = mkdtempSync(join(tmpdir(), 'gl-fd39-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), cpp);
    try {
      execFileSync('c++', ['-std=c++17', '-Wall', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8' });
    } catch (e) {
      assert.fail(`harness compile failed:\n${e.stdout || ''}\n${e.stderr || ''}`);
    }
    return execFileSync(join(dir, 'check'), { encoding: 'utf8' });
  } finally { rmSync(dir, { recursive: true, force: true }); }
}

test('FD-39: 5_6_5_REV unpacks and the dropped texparam set is stored, validated, and queryable', () => {
  const cpp = `${PREAMBLE}\n${EXTRA}\n${FD39_SRC}\nstatic U64 dispatch(GLGuestCPU* cpu, const GL64Args& args, U64 fnId){\n  switch (fnId) {\n${CASES}\n    default: return 0;\n  }\n}\n${MAIN}\n`;
  const out = compileAndRun(cpp);
  assert.match(out, /0 failures/);
});
