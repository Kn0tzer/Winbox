import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

// FD-15: the GL bridge's fabricated / silently dropped answers.
// (d) packed pixel types 4_4_4_4 / 5_5_5_1 had no unpack model and were
//     uploaded still packed;
// (c) unknown legacy client-array types got component size 0, which
//     fabricated zeroed vertex data; HALF_FLOAT / FIXED were unmodelled;
// (e) glMultiTexCoord2fARB vanished on the legacy-GL path (never recorded,
//     and the block replay did not re-emit it);
// (b) unknown GLX query attributes and unknown glGetSynciv pnames were
//     answered with a fabricated 0.
// This harness extracts the REAL functions and dispatcher case bodies from
// source/opengl/gl64bridge.cpp (same pattern as gl-query-bookkeeping),
// compiles them against the real GLGuestMemory shim and host stubs, and
// drives the guest-visible behaviour. Every phase fails on the pre-FD-15
// code and passes on the fixed code.

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

// For functions this fix ADDS: on the pre-fix code they are absent, and
// the harness substitutes a stub with the old observable behaviour (no
// refusal, no decoding) so the behavioural CHECKs fail individually
// instead of the whole file aborting during setup.
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

function extractCase(label, continuationLabel) {
  const marker = `case ${label}:`;
  const start = src.indexOf(marker);
  assert.ok(start >= 0, `dispatcher case ${label} is present`);
  let from = start + marker.length;
  if (continuationLabel) {
    const contMarker = `case ${continuationLabel}:`;
    const cont = src.indexOf(contMarker, from);
    assert.ok(cont >= 0, `continuation case ${continuationLabel} is present`);
    from = cont + contMarker.length;
  }
  const rest = src.slice(from);
  const m = rest.search(/\n        case GL64_fn_|\n#endif/);
  assert.ok(m >= 0, `dispatcher case ${label} terminates`);
  return src.slice(start, from + m);
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
  GL_FLOAT=0x1406, GL_DOUBLE=0x140A, GL_RGB=0x1907, GL_RGBA=0x1908;
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
enum {
  GL64_fn_glXGetFBConfigAttrib=9, GL64_fn_glXGetConfig=10, GL64_fn_glXQueryDrawable=27,
  GL64_fn_glXQueryContext=29, GL64_fn_glColor3f=217, GL64_fn_glColor4f=218,
  GL64_fn_glNormal3f=295, GL64_fn_glVertex2f=322, GL64_fn_glVertex3f=323,
  GL64_fn_glGetSynciv=605, GL64_fn_glMultiTexCoord2fARB=884
};

// XWireServer stub: only the surface glXQueryDrawable touches.
struct XWireWindow { U32 width=0, height=0; };
struct XWireServer {
  std::mutex regMutex;
  std::map<U32, XWireWindow> windows;
  static XWireServer& instance() { static XWireServer s; return s; }
  U32 resolveGlxDrawable(U32 d) { return d; }
};

// Host GL stubs for the immediate-mode replay: record every call.
static std::vector<std::string> g_hostCalls;
static void rec(const char* name, std::initializer_list<double> vals) {
  std::string s = name;
  char buf[64];
  for (double v : vals) { snprintf(buf, sizeof buf, " %g", v); s += buf; }
  g_hostCalls.push_back(s);
}
static void glColor4f(GLfloat r, GLfloat g, GLfloat b, GLfloat a) { rec("color", {r, g, b, a}); }
static void glNormal3f(GLfloat x, GLfloat y, GLfloat z) { rec("normal", {x, y, z}); }
static void glVertex2f(GLfloat x, GLfloat y) { rec("v2", {x, y}); }
static void glVertex3f(GLfloat x, GLfloat y, GLfloat z) { rec("v3", {x, y, z}); }
// Emscripten has no glMultiTexCoord2f symbol in this build; the bridge
// expresses per-unit texcoords as client-active-unit + glTexCoord2f.
static U32 g_stubActiveUnit = 0x84C0;
static void glClientActiveTexture(GLenum t) { g_stubActiveUnit = t; }
static void glTexCoord2f(GLfloat s, GLfloat t) { rec("tc", {(double)g_stubActiveUnit, s, t}); }

GLenum g_allocationError = GL_NO_ERROR;
`;

const EXTRA = [
  extractFunction('static bool gl64NoteOnce('),
  extractFunction('void recordBridgeError('),
  extractSlice('struct GlxPbufferInfo {', 'std::map<U32, U64> g_glxEventMasks;'),
  extractSlice('struct LegacyClientArray {', 'GLuint g_legacyElementArrayBuffer = 0;'),
  extractFunction('size_t legacyTypeSize(GLenum type) {'),
  extractFunctionOr('bool legacyArrayTypeUnmodelled(const LegacyClientArray& array) {',
    'bool legacyArrayTypeUnmodelled(const LegacyClientArray&) { return false; }'),
  extractFunctionOr('bool legacyDrawUnmodelledType() {',
    'bool legacyDrawUnmodelledType() { return false; }'),
  extractFunction('bool copyLegacyArray(GLGuestCPU* cpu, const LegacyClientArray& array,'),
  extractFunctionOr('float legacyHalfToFloat(U16 h) {',
    'float legacyHalfToFloat(U16) { return 0; }'),
  extractFunction('double legacyVertexComponent(const std::vector<U8>& data, const LegacyClientArray& array,'),
  extractFunction('void glesTexFormatFixup(GLint& ifmt, GLenum& fmt, GLenum& type, std::vector<U8>& pix) {'),
  extractSlice('struct ImmOp {', '};'),
  extractFunctionOr('inline void hostMultiTexCoord2f(GLenum target, GLfloat s, GLfloat t) {',
    'inline void hostMultiTexCoord2f(GLenum, GLfloat, GLfloat) {}'),
  extractFunction('inline void replayImmBlock('),
].join('\n');

const CASES = [
  extractCase('GL64_fn_glXGetFBConfigAttrib', 'GL64_fn_glXGetConfig'),
  extractCase('GL64_fn_glXQueryDrawable'),
  extractCase('GL64_fn_glXQueryContext'),
  extractCase('GL64_fn_glGetSynciv'),
].join('\n');

const MAIN = `
static GL64Args A(U64 a0=0,U64 a1=0,U64 a2=0,U64 a3=0,U64 a4=0){ GL64Args r{}; r.a[0]=a0; r.a[1]=a1; r.a[2]=a2; r.a[3]=a3; r.a[4]=a4; return r; }
static constexpr U64 OUT=0x1800, OUT2=0x1900;
static constexpr U32 SENTINEL=0xDEADBEEF;

int main(){
  KMemory64 mem; GLGuestMemory gm(&mem); GLGuestCPU cpu{&gm};
  auto call=[&](U64 fn,const GL64Args& a){ return dispatch(&cpu,a,fn); };

  // ---- (d) packed pixel types are unpacked, not uploaded packed --------
  {
    GLint ifmt=0x8058; GLenum fmt=GL_RGBA, type=0x8033; // UNSIGNED_SHORT_4_4_4_4
    std::vector<U8> pix={0xCD,0xAB}; // one texel, word 0xABCD
    glesTexFormatFixup(ifmt, fmt, type, pix);
    CHECK(type==GL_UNSIGNED_BYTE && fmt==GL_RGBA);
    CHECK(pix.size()==4 && pix[0]==170 && pix[1]==187 && pix[2]==204 && pix[3]==221);
  }
  {
    GLint ifmt=0x8058; GLenum fmt=GL_RGBA, type=0x8034; // UNSIGNED_SHORT_5_5_5_1
    std::vector<U8> pix={0x00,0xF8, 0xFF,0xFF}; // (31,0,0,0) and (31,31,31,1)
    glesTexFormatFixup(ifmt, fmt, type, pix);
    CHECK(type==GL_UNSIGNED_BYTE && fmt==GL_RGBA);
    CHECK(pix.size()==8 && pix[0]==255 && pix[1]==0 && pix[2]==0 && pix[3]==0);
    CHECK(pix[4]==255 && pix[5]==255 && pix[6]==255 && pix[7]==255);
  }
  {
    // Regression: the REV variant that already worked still unpacks.
    GLint ifmt=0x8058; GLenum fmt=GL_RGBA, type=0x8365; // 4_4_4_4_REV
    std::vector<U8> pix={0x34,0x12}; // word 0x1234
    glesTexFormatFixup(ifmt, fmt, type, pix);
    CHECK(pix.size()==4 && pix[0]==68 && pix[1]==51 && pix[2]==34 && pix[3]==17);
  }

  // ---- (c) legacy array types ------------------------------------------
  CHECK(legacyTypeSize(0x140B)==2);  // GL_HALF_FLOAT
  CHECK(legacyTypeSize(0x140C)==4);  // GL_FIXED
  CHECK(legacyTypeSize(GL_FLOAT)==4);
  CHECK(legacyTypeSize(0x9999)==0);  // genuinely unknown: still no model
  {
    LegacyClientArray halfArr{1, 0x140B, 0, 0, true, false, true};
    std::vector<U8> data={0x00,0x3C, 0x00,0xC0}; // 1.0, -2.0
    CHECK(std::fabs(legacyVertexComponent(data, halfArr, 0, 0, 0)-1.0)<1e-6);
    CHECK(std::fabs(legacyVertexComponent(data, halfArr, 0, 1, 0)+2.0)<1e-6);
    LegacyClientArray fixedArr{1, 0x140C, 0, 0, true, false, true};
    std::vector<U8> fdata={0x00,0x80,0x01,0x00}; // 1.5 in 16.16 fixed
    CHECK(std::fabs(legacyVertexComponent(fdata, fixedArr, 0, 0, 0)-1.5)<1e-9);
  }
  {
    // Copies of the newly modelled types carry the guest's real bytes.
    LegacyClientArray halfArr{1, 0x140B, 0, 0x2000, true, false, true};
    mem.bytes[0x2000]=0x00; mem.bytes[0x2001]=0x3C; mem.bytes[0x2002]=0x00; mem.bytes[0x2003]=0xC0;
    std::vector<U8> copy;
    CHECK(copyLegacyArray(&cpu, halfArr, 0, 1, copy));
    CHECK(copy.size()==4 && copy[1]==0x3C && copy[3]==0xC0);
    // An unmodelled type cannot be copied...
    LegacyClientArray badArr{2, 0x9999, 0, 0x2000, true, false, true};
    std::vector<U8> copy2;
    CHECK(!copyLegacyArray(&cpu, badArr, 0, 1, copy2));
    // ...and a draw using it is refused by the gate the draw cases consult.
    g_legacyVertexArray = badArr;
    CHECK(legacyDrawUnmodelledType());
    g_legacyVertexArray = LegacyClientArray{3, GL_FLOAT, 0, 0x2000, true, false, true};
    CHECK(!legacyDrawUnmodelledType());
    g_legacyVertexArray.enabled = false;
    g_legacyColorArray = badArr;
    g_legacyColorArray.enabled = false;
    CHECK(!legacyDrawUnmodelledType());      // disabled arrays do not block
    g_legacyColorArray.enabled = true;
    g_legacyColorArray.bufferBacked = true;
    CHECK(!legacyDrawUnmodelledType());      // buffer-backed: host validates
    g_legacyColorArray = LegacyClientArray{};
  }

  // ---- (e) immediate-mode replay re-emits texcoords per vertex ---------
  {
    g_hostCalls.clear();
    std::vector<ImmOp> ops;
    ops.push_back(ImmOp{(U16)GL64_fn_glColor3f, {1, 0, 0, 0}});
    ops.push_back(ImmOp{(U16)GL64_fn_glMultiTexCoord2fARB, {(float)0x84C0, 0.25f, 0.75f, 0}});
    ops.push_back(ImmOp{(U16)GL64_fn_glVertex3f, {1, 2, 3, 0}});
    ops.push_back(ImmOp{(U16)GL64_fn_glMultiTexCoord2fARB, {(float)0x84C1, 0.5f, 0.5f, 0}});
    ops.push_back(ImmOp{(U16)GL64_fn_glVertex2f, {4, 5, 0, 0}});
    replayImmBlock(ops);
    std::vector<std::string> want={
      "color 1 0 0 1", "tc 33984 0.25 0.75", "v3 1 2 3",
      "color 1 0 0 1", "tc 33984 0.25 0.75", "tc 33985 0.5 0.5", "v2 4 5"};
    CHECK(g_hostCalls==want);
    if (g_hostCalls!=want) { for (auto& s : g_hostCalls) fprintf(stderr,"host call: %s\\n", s.c_str()); }
  }

  // ---- (b) GLX + sync queries ------------------------------------------
  // Known FBConfig attribute still answers its real value, rc Success.
  mem.writed(OUT, SENTINEL);
  CHECK(call(GL64_fn_glXGetFBConfigAttrib, A(0, 8, OUT))==0);
  CHECK(mem.readd(OUT)==8);
  // Unknown attribute: GLX_BAD_ATTRIBUTE (2), guest memory untouched.
  mem.writed(OUT, SENTINEL);
  CHECK(call(GL64_fn_glXGetFBConfigAttrib, A(0, 0x7777, OUT))==2);
  CHECK(mem.readd(OUT)==SENTINEL);
  // glXQueryContext: same disposition.
  mem.writed(OUT, SENTINEL);
  CHECK(call(GL64_fn_glXQueryContext, A(0, 0x8013, OUT))==0);
  CHECK(mem.readd(OUT)==0x5100);
  mem.writed(OUT, SENTINEL);
  CHECK(call(GL64_fn_glXQueryContext, A(0, 0x7777, OUT))==2);
  CHECK(mem.readd(OUT)==SENTINEL);
  // glXQueryDrawable on a pbuffer: known answers, unknown untouched.
  g_glxPbuffers[77]={640,480};
  mem.writed(OUT, SENTINEL);
  call(GL64_fn_glXQueryDrawable, A(77, 0x801D, OUT));
  CHECK(mem.readd(OUT)==640);
  mem.writed(OUT, SENTINEL);
  call(GL64_fn_glXQueryDrawable, A(77, 0x7777, OUT));
  CHECK(mem.readd(OUT)==SENTINEL);
  // glXQueryDrawable on a window: live size answers, unknown untouched.
  XWireServer::instance().windows[55]={111,222};
  mem.writed(OUT, SENTINEL);
  call(GL64_fn_glXQueryDrawable, A(55, 0x801E, OUT));
  CHECK(mem.readd(OUT)==222);
  mem.writed(OUT, SENTINEL);
  call(GL64_fn_glXQueryDrawable, A(55, 0x7777, OUT));
  CHECK(mem.readd(OUT)==SENTINEL);
  // glGetSynciv: known pname answers; unknown pname is INVALID_ENUM and
  // writes neither the value nor the length.
  mem.writed(OUT, SENTINEL); mem.writed(OUT2, SENTINEL);
  call(GL64_fn_glGetSynciv, A(1, 0x9114, 1, OUT2, OUT));
  CHECK(mem.readd(OUT)==0x9119);
  CHECK(mem.readd(OUT2)==1);
  CHECK(g_allocationError==GL_NO_ERROR);
  mem.writed(OUT, SENTINEL); mem.writed(OUT2, SENTINEL);
  call(GL64_fn_glGetSynciv, A(1, 0x9999, 1, OUT2, OUT));
  CHECK(g_allocationError==GL_INVALID_ENUM);
  CHECK(mem.readd(OUT)==SENTINEL);
  CHECK(mem.readd(OUT2)==SENTINEL);

  printf("FD15 HARNESS: %d checks, %d failures\\n", g_checks, g_failures);
  return g_failures ? 1 : 0;
}
`;

function compileAndRun(cpp) {
  const dir = mkdtempSync(join(tmpdir(), 'gl-fd15-'));
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

test('FD-15: bridge answers are honest (packed unpack, legacy types, texcoord replay, GLX/sync refusals)', () => {
  const cpp = `${PREAMBLE}\n${EXTRA}\nstatic U64 dispatch(GLGuestCPU* cpu, const GL64Args& args, U64 fnId){\n  switch (fnId) {\n${CASES}\n    default: return 0;\n  }\n}\n${MAIN}\n`;
  const out = compileAndRun(cpp);
  assert.match(out, /0 failures/);
});

test('FD-15: draw entry points consult the unmodelled-type refusal', () => {
  const gates = src.match(/if \(legacyDrawUnmodelledType\(\)\) return 0;/g) || [];
  assert.equal(gates.length, 3, 'glDrawArrays, glDrawElements and glDrawRangeElements all refuse');
});

test('FD-15: glMultiTexCoord2fARB is recorded on every path, not compiled out on the WASM legacy path', () => {
  assert.ok(!src.includes('#ifndef __EMSCRIPTEN__\n            if (g_glContext) RECORD_IMM(GL64_fn_glMultiTexCoord2fARB'),
    'the pre-FD-15 guarded (WASM-vanishing) form is gone');
  assert.ok(src.includes('if (g_glContext) RECORD_IMM(GL64_fn_glMultiTexCoord2fARB'),
    'the unguarded record/run form is present');
});
