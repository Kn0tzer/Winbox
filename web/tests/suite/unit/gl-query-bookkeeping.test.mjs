import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

// Behavioural coverage for the GL bridge's query bookkeeping and its
// glTransformFeedbackVaryings guest-pointer reads. Both used to be
// compile-verified only. This harness extracts the REAL dispatcher case
// bodies (plus GLQueryStatus/g_glQueryStatus/g_glActiveQueries,
// recordBridgeError and readGuestCStr) from source/opengl/gl64bridge.cpp,
// compiles them against the real GLGuestMemory shim and a spec-faithful
// host-GL stub, and drives the guest-visible state machine: lifecycle,
// availability, current-query coherence with the host, rejection of
// invalid begin/counter operations, delete/recycle semantics, and
// transform-feedback name reads at both guest pointer strides.

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const bridgePath = join(root, 'source/opengl/gl64bridge.cpp');
const guestMemPath = join(root, 'source/opengl/glguestmemory.h');
const src = readFileSync(bridgePath, 'utf8');

function extractBookkeeping() {
  const start = src.indexOf('struct GLQueryStatus');
  assert.ok(start >= 0, 'GLQueryStatus bookkeeping struct is present');
  const endMarker = 'static std::map<GLenum, GLuint> g_glActiveQueries;';
  const end = src.indexOf(endMarker, start);
  assert.ok(end >= 0, 'g_glActiveQueries declaration is present');
  return src.slice(start, end + endMarker.length);
}

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
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <string>
#include <vector>
using U8=uint8_t; using U32=uint32_t; using U64=uint64_t;
using GLenum=uint32_t; using GLuint=uint32_t; using GLsizei=int32_t; using GLchar=char;
constexpr GLenum GL_NO_ERROR=0x0000;
constexpr GLenum GL_INVALID_ENUM=0x0500;
constexpr GLenum GL_INVALID_OPERATION=0x0502;
static int g_checks=0, g_failures=0;
#define CHECK(cond) do{ g_checks++; if(!(cond)){ g_failures++; fprintf(stderr,"CHECK FAILED line %d: %s\\n",__LINE__,#cond); } } while(0)

// Guest memory models for the real GLGuestMemory shim (same shape as
// gl-guest-memory.test.mjs): one flat space per architecture.
struct KMemory {
  U8 bytes[65536]{};
  U64 fromGuestCalls=0;
  void memcpyFromGuest(void* out,U64 p,U64 n){ if(p+n>sizeof bytes){ fprintf(stderr,"guest read out of bounds\\n"); exit(3);} memcpy(out,bytes+p,(size_t)n); fromGuestCalls++; }
  void memcpyToGuest(U64 p,const void* in,U64 n){ if(p+n>sizeof bytes){ fprintf(stderr,"guest write out of bounds\\n"); exit(3);} memcpy(bytes+p,in,(size_t)n); }
  void writed(U64 p,U32 v){ memcpyToGuest(p,&v,4); }
  void writeq(U64 p,U64 v){ memcpyToGuest(p,&v,8); }
  U32 readd(U64 p) const { U32 v; memcpy(&v,bytes+p,4); return v; }
  U64 readq(U64 p) const { U64 v; memcpy(&v,bytes+p,8); return v; }
  void putStr(U64 p,const char* s){ memcpyToGuest(p,s,strlen(s)+1); }
};
struct KMemory64 : KMemory {};
#include "${guestMemPath}"

// Host GL stub: models the spec state machine a real GL keeps, so the
// test can check the bridge bookkeeping never diverges from the host.
struct HostGL {
  GLuint nextId=1;
  std::vector<GLuint> freeList;
  std::set<GLuint> live;
  std::map<GLenum,GLuint> active;
  GLenum error=GL_NO_ERROR;
  int genCalls=0, delCalls=0, beginCalls=0, endCalls=0, tfbCalls=0;
  GLuint tfbProg=0; GLenum tfbMode=0; std::vector<std::string> tfbNames;
} H;
void glGenQueries(GLsizei n, GLuint* p){ H.genCalls++; for(GLsizei i=0;i<n;i++){ GLuint id; if(!H.freeList.empty()){ id=H.freeList.back(); H.freeList.pop_back(); } else id=H.nextId++; p[i]=id; H.live.insert(id);} }
void glDeleteQueries(GLsizei n, const GLuint* p){ H.delCalls++; for(GLsizei i=0;i<n;i++){ GLuint id=p[i]; if(!H.live.erase(id)) continue; for(auto it=H.active.begin(); it!=H.active.end();){ if(it->second==id) it=H.active.erase(it); else ++it; } H.freeList.push_back(id);} }
void glBeginQuery(GLenum t, GLuint id){
  H.beginCalls++;
  if(!id || H.active.count(t)){ H.error=GL_INVALID_OPERATION; return; }
  for(auto& kv : H.active) if(kv.second==id){ H.error=GL_INVALID_OPERATION; return; }
  H.live.insert(id); H.active[t]=id;
}
void glEndQuery(GLenum t){ H.endCalls++; if(!H.active.erase(t)) H.error=GL_INVALID_OPERATION; }
void glTransformFeedbackVaryings(GLuint prog, GLsizei count, const GLchar* const* names, GLenum mode){
  H.tfbCalls++; H.tfbProg=prog; H.tfbMode=mode; H.tfbNames.clear();
  for(GLsizei i=0;i<count;i++) H.tfbNames.emplace_back(names[i]);
}

// Bridge glue: the argument block and helpers the extracted cases use.
struct GL64Args { U64 a[16]; };
inline U64 ai(const GL64Args& a, int i) { return a.a[i]; }
static int g_glContextObj;
static void* g_glContext = &g_glContextObj;
#define GL_MT(stmt) do { stmt; } while (0)
enum {
  GL64_fn_glGenQueries=610, GL64_fn_glDeleteQueries=611, GL64_fn_glIsQuery=612,
  GL64_fn_glBeginQuery=613, GL64_fn_glEndQuery=614, GL64_fn_glGetQueryiv=615,
  GL64_fn_glGetQueryObjectiv=616, GL64_fn_glGetQueryObjectuiv=617,
  GL64_fn_glGetQueryObjectui64v=618, GL64_fn_glQueryCounter=619,
  GL64_fn_glTransformFeedbackVaryings=620
};
`;

function buildHarness(extra, cases, mainBody) {
  return `${PREAMBLE}
${extra}
static U64 dispatch(GLGuestCPU* cpu, const GL64Args& args, U64 fnId){
  switch (fnId) {
${cases}
    default: return 0;
  }
}
${mainBody}
`;
}

function compileAndRun(name, cpp) {
  const dir = mkdtempSync(join(tmpdir(), `gl-query-${name}-`));
  try {
    writeFileSync(join(dir, 'check.cpp'), cpp);
    try {
      execFileSync('c++', ['-std=c++17', '-Wall', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8' });
    } catch (e) {
      assert.fail(`harness compile failed:\n${e.stdout || ''}\n${e.stderr || ''}`);
    }
    try {
      return execFileSync(join(dir, 'check'), { encoding: 'utf8' });
    } catch (e) {
      assert.fail(`compiled bridge harness failed (status ${e.status}):\n${e.stdout || ''}\n${e.stderr || ''}`);
    }
  } finally { rmSync(dir, { recursive: true, force: true }); }
}

const QUERY_MAIN = `
static GL64Args A(U64 a0=0,U64 a1=0,U64 a2=0,U64 a3=0){ GL64Args r{}; r.a[0]=a0; r.a[1]=a1; r.a[2]=a2; r.a[3]=a3; return r; }
static constexpr GLenum GL_SAMPLES_PASSED=0x8914, GL_TIME_ELAPSED=0x88BF, GL_TIMESTAMP=0x8E28;
static constexpr GLenum GL_QUERY_RESULT=0x8866, GL_QUERY_RESULT_AVAILABLE=0x8867, GL_CURRENT_QUERY=0x8865;
static constexpr U64 IDS=0x1000, OUT32=0x1800, OUT64=0x1900;

int main(){
  KMemory64 mem; GLGuestMemory gm(&mem); GLGuestCPU cpu{&gm};
  auto call=[&](U64 fn,const GL64Args& a){ return dispatch(&cpu,a,fn); };
  auto isQuery=[&](GLuint id){ return call(GL64_fn_glIsQuery, A(id)); };
  auto availIv=[&](GLuint id){ call(GL64_fn_glGetQueryObjectiv, A(id,GL_QUERY_RESULT_AVAILABLE,OUT32)); return mem.readd(OUT32); };
  auto resultIv=[&](GLuint id){ call(GL64_fn_glGetQueryObjectiv, A(id,GL_QUERY_RESULT,OUT32)); return mem.readd(OUT32); };
  auto currentQ=[&](GLenum t){ call(GL64_fn_glGetQueryiv, A(t,GL_CURRENT_QUERY,OUT32)); return mem.readd(OUT32); };

  // ---- Phase A: gen -> begin -> end -> results -------------------------
  call(GL64_fn_glGenQueries, A(3, IDS));
  CHECK(H.genCalls==1);
  GLuint id1=mem.readd(IDS), id2=mem.readd(IDS+4), id3=mem.readd(IDS+8);
  CHECK(id1 && id2 && id3);
  CHECK(id1!=id2 && id2!=id3 && id1!=id3);
  // A generated-but-unused name is not a query object yet (spec).
  CHECK(isQuery(id1)==0 && isQuery(id2)==0 && isQuery(id3)==0);
  CHECK(availIv(id1)==0); CHECK(g_allocationError==GL_NO_ERROR);
  CHECK(resultIv(id1)==0);
  call(GL64_fn_glBeginQuery, A(GL_SAMPLES_PASSED, id1));
  CHECK(H.beginCalls==1);
  CHECK(isQuery(id1)==1);
  CHECK(currentQ(GL_SAMPLES_PASSED)==id1);
  CHECK(availIv(id1)==0);                    // active: result not available
  call(GL64_fn_glEndQuery, A(GL_SAMPLES_PASSED));
  CHECK(availIv(id1)==1);
  call(GL64_fn_glGetQueryObjectuiv, A(id1,GL_QUERY_RESULT_AVAILABLE,OUT32));
  CHECK(mem.readd(OUT32)==1);
  call(GL64_fn_glGetQueryObjectui64v, A(id1,GL_QUERY_RESULT_AVAILABLE,OUT64));
  CHECK(mem.readq(OUT64)==1);
  call(GL64_fn_glGetQueryObjectui64v, A(id1,GL_QUERY_RESULT,OUT64));
  CHECK(mem.readq(OUT64)==0);
  CHECK(currentQ(GL_SAMPLES_PASSED)==0);
  CHECK(isQuery(id2)==0);

  // ---- Phase B: glQueryCounter stamps an immediate result --------------
  call(GL64_fn_glQueryCounter, A(id2, GL_TIMESTAMP));
  CHECK(availIv(id2)==1);
  CHECK(isQuery(id2)==1);
  CHECK(currentQ(GL_TIMESTAMP)==0);         // a counter never becomes current
  CHECK(g_allocationError==GL_NO_ERROR);

  // ---- Phase C: begin on a busy target is rejected, state untouched ---
  call(GL64_fn_glBeginQuery, A(GL_SAMPLES_PASSED, id3));
  CHECK(currentQ(GL_SAMPLES_PASSED)==id3);
  int hostBeginCalls=H.beginCalls;
  call(GL64_fn_glBeginQuery, A(GL_SAMPLES_PASSED, id1));
  CHECK(g_allocationError==GL_INVALID_OPERATION);
  CHECK(H.beginCalls==hostBeginCalls);      // not forwarded to a host that would reject it
  CHECK(currentQ(GL_SAMPLES_PASSED)==id3);  // bookkeeping stays with the host's active query
  CHECK(availIv(id1)==1);                   // id1's completed result is untouched
  g_allocationError=GL_NO_ERROR;
  call(GL64_fn_glEndQuery, A(GL_SAMPLES_PASSED));
  CHECK(availIv(id3)==1);
  // The displaced query can still be begun cleanly afterwards.
  call(GL64_fn_glBeginQuery, A(GL_SAMPLES_PASSED, id1));
  CHECK(g_allocationError==GL_NO_ERROR);
  call(GL64_fn_glEndQuery, A(GL_SAMPLES_PASSED));
  CHECK(availIv(id1)==1);

  // ---- Phase C2: an active id cannot migrate targets; id 0 is invalid -
  call(GL64_fn_glBeginQuery, A(GL_SAMPLES_PASSED, id3));
  hostBeginCalls=H.beginCalls;
  call(GL64_fn_glBeginQuery, A(GL_TIME_ELAPSED, id3));
  CHECK(g_allocationError==GL_INVALID_OPERATION);
  CHECK(H.beginCalls==hostBeginCalls);
  CHECK(currentQ(GL_TIME_ELAPSED)==0);
  CHECK(currentQ(GL_SAMPLES_PASSED)==id3);
  g_allocationError=GL_NO_ERROR;
  call(GL64_fn_glBeginQuery, A(GL_TIME_ELAPSED, 0));
  CHECK(g_allocationError==GL_INVALID_OPERATION);
  CHECK(isQuery(0)==0);
  g_allocationError=GL_NO_ERROR;
  call(GL64_fn_glEndQuery, A(GL_SAMPLES_PASSED));

  // ---- Phase D: a typed query object cannot change target -------------
  call(GL64_fn_glBeginQuery, A(GL_TIME_ELAPSED, id2));   // id2 is a TIMESTAMP counter object
  CHECK(g_allocationError==GL_INVALID_OPERATION);
  CHECK(currentQ(GL_TIME_ELAPSED)==0);
  g_allocationError=GL_NO_ERROR;
  call(GL64_fn_glQueryCounter, A(id1, GL_TIMESTAMP));   // id1 is typed GL_SAMPLES_PASSED
  CHECK(g_allocationError==GL_INVALID_OPERATION);
  g_allocationError=GL_NO_ERROR;
  call(GL64_fn_glQueryCounter, A(id2, GL_TIMESTAMP));   // matching target: fine
  CHECK(g_allocationError==GL_NO_ERROR);
  CHECK(availIv(id2)==1);

  // ---- Phase D2: glQueryCounter accepts GL_TIMESTAMP only ------------
  call(GL64_fn_glQueryCounter, A(id3, GL_SAMPLES_PASSED));
  CHECK(g_allocationError==GL_INVALID_ENUM);
  g_allocationError=GL_NO_ERROR;
  CHECK(availIv(id3)==1);                   // id3 state untouched
  CHECK(currentQ(GL_SAMPLES_PASSED)==0);

  // ---- Phase E: glQueryCounter must not clobber an active query -------
  call(GL64_fn_glBeginQuery, A(GL_SAMPLES_PASSED, id1));
  call(GL64_fn_glQueryCounter, A(id1, GL_TIMESTAMP));
  CHECK(g_allocationError==GL_INVALID_OPERATION);
  CHECK(availIv(id1)==0);                   // still mid-query: NOT available
  CHECK(currentQ(GL_SAMPLES_PASSED)==id1);
  g_allocationError=GL_NO_ERROR;
  call(GL64_fn_glEndQuery, A(GL_SAMPLES_PASSED));
  CHECK(availIv(id1)==1);

  // ---- Phase F: end with nothing active -------------------------------
  call(GL64_fn_glEndQuery, A(GL_TIME_ELAPSED));
  CHECK(H.error==GL_INVALID_OPERATION);    // the host produces this error, as on real GL
  CHECK(currentQ(GL_TIME_ELAPSED)==0);
  CHECK(g_allocationError==GL_NO_ERROR);    // the bridge records nothing of its own

  // ---- Phase G: delete semantics and name recycling -------------------
  call(GL64_fn_glBeginQuery, A(GL_SAMPLES_PASSED, id3));
  CHECK(currentQ(GL_SAMPLES_PASSED)==id3);
  { GLuint tmp=id3; mem.memcpyToGuest(IDS,&tmp,4); }
  call(GL64_fn_glDeleteQueries, A(1, IDS));
  CHECK(isQuery(id3)==0);
  CHECK(currentQ(GL_SAMPLES_PASSED)==0);
  // Querying a deleted name is a guest error and must be loud.
  call(GL64_fn_glGetQueryObjectiv, A(id3,GL_QUERY_RESULT_AVAILABLE,OUT32));
  CHECK(g_allocationError==GL_INVALID_OPERATION);
  CHECK(mem.readd(OUT32)==0);               // benign zero still written
  g_allocationError=GL_NO_ERROR;
  call(GL64_fn_glGetQueryObjectuiv, A(9999,GL_QUERY_RESULT,OUT32));
  CHECK(g_allocationError==GL_INVALID_OPERATION);
  g_allocationError=GL_NO_ERROR;
  CHECK(isQuery(9999)==0);
  CHECK(g_allocationError==GL_NO_ERROR);    // glIsQuery itself never errors
  // ...and the probe must not have created bookkeeping for the garbage id:
  // asking for its result still produces the loud error.
  call(GL64_fn_glGetQueryObjectiv, A(9999,GL_QUERY_RESULT_AVAILABLE,OUT32));
  CHECK(g_allocationError==GL_INVALID_OPERATION);
  g_allocationError=GL_NO_ERROR;
  // The host recycles the deleted name; it starts from a clean slate.
  call(GL64_fn_glGenQueries, A(1, IDS));
  GLuint recycled=mem.readd(IDS);
  CHECK(recycled==id3);
  CHECK(isQuery(recycled)==0);
  CHECK(availIv(recycled)==0);
  CHECK(g_allocationError==GL_NO_ERROR);
  call(GL64_fn_glBeginQuery, A(GL_SAMPLES_PASSED, recycled));
  call(GL64_fn_glEndQuery, A(GL_SAMPLES_PASSED));
  CHECK(availIv(recycled)==1);

  // ---- Phase H: without a context nothing is recorded or written -----
  g_glContext=nullptr;
  { U32 guard=0xCAFEBABEu; mem.memcpyToGuest(IDS,&guard,4); }
  int hostGenCalls=H.genCalls;
  call(GL64_fn_glGenQueries, A(2, IDS));
  CHECK(mem.readd(IDS)==0xCAFEBABEu);
  CHECK(H.genCalls==hostGenCalls);
  call(GL64_fn_glBeginQuery, A(GL_SAMPLES_PASSED, id1));
  g_glContext=&g_glContextObj;
  CHECK(currentQ(GL_SAMPLES_PASSED)==0);
  CHECK(availIv(id1)==1);                   // earlier state untouched

  if(g_failures){ fprintf(stderr,"%d of %d checks FAILED\\n",g_failures,g_checks); return 1; }
  printf("query bookkeeping harness: all %d checks passed\\n",g_checks);
  return 0;
}
`;

const TFB_MAIN = `
static constexpr U64 ARR=0x2000, STR=0x3000;
int main(){
  // ---- 64-bit guest: 8-byte pointer stride ----------------------------
  KMemory64 mem; GLGuestMemory gm(&mem); GLGuestCPU cpu{&gm};
  auto tfb=[&](GLGuestCPU* c, GLuint prog, GLsizei count, U64 arr, GLenum mode){
    GL64Args a{}; a.a[0]=prog; a.a[1]=(U64)(uint32_t)count; a.a[2]=arr; a.a[3]=mode;
    return dispatch(c,a,GL64_fn_glTransformFeedbackVaryings);
  };
  mem.putStr(STR+0x000, "gl_Position");
  mem.putStr(STR+0x100, "outColor");
  std::string longName(300,'v');
  mem.memcpyToGuest(STR+0x200, longName.c_str(), longName.size()+1);
  mem.putStr(STR+0x400, "");
  { U64 ptrs[4]={STR, STR+0x100, STR+0x200, STR+0x400};
    mem.memcpyToGuest(ARR, ptrs, sizeof ptrs); }
  tfb(&cpu, 77, 4, ARR, 0x8C8C);
  CHECK(H.tfbCalls==1); CHECK(H.tfbProg==77); CHECK(H.tfbMode==0x8C8C);
  CHECK(H.tfbNames.size()==4);
  CHECK(H.tfbNames[0]=="gl_Position");
  CHECK(H.tfbNames[1]=="outColor");
  CHECK(H.tfbNames[2]==longName);           // crosses readGuestCStr's 256-byte chunks
  CHECK(H.tfbNames[3]=="");
  // A NULL entry inside the array reads as an empty name.
  { U64 ptrs[3]={STR, 0, STR+0x100};
    mem.memcpyToGuest(ARR, ptrs, sizeof ptrs); }
  tfb(&cpu, 78, 3, ARR, 0x8C8D);
  CHECK(H.tfbCalls==2);
  CHECK(H.tfbNames.size()==3 && H.tfbNames[0]=="gl_Position" && H.tfbNames[1]=="" && H.tfbNames[2]=="outColor");
  // No-op edges: nothing reaches the host and nothing is read.
  U64 readsBefore=mem.fromGuestCalls;
  tfb(&cpu, 79, 0, ARR, 0x8C8C);
  tfb(&cpu, 79, -3, ARR, 0x8C8C);
  tfb(&cpu, 79, 5000, ARR, 0x8C8C);         // over the bridge's 4096 cap
  tfb(&cpu, 79, 3, 0, 0x8C8C);              // NULL array
  CHECK(H.tfbCalls==2);
  CHECK(mem.fromGuestCalls==readsBefore);
  // ---- 32-bit guest: 4-byte pointer stride (the readq regression) -----
  KMemory m32; GLGuestMemory gm32(&m32); GLGuestCPU cpu32{&gm32};
  m32.putStr(STR+0x000, "tf32_a");
  m32.putStr(STR+0x100, "tf32_b");
  { U32 ptrs[2]={(U32)STR, (U32)(STR+0x100)};
    m32.memcpyToGuest(ARR, ptrs, sizeof ptrs); }
  tfb(&cpu32, 80, 2, ARR, 0x8C8C);
  CHECK(H.tfbCalls==3);
  CHECK(H.tfbNames.size()==2 && H.tfbNames[0]=="tf32_a" && H.tfbNames[1]=="tf32_b");

  if(g_failures){ fprintf(stderr,"%d of %d checks FAILED\\n",g_failures,g_checks); return 1; }
  printf("transform feedback harness: all %d checks passed\\n",g_checks);
  return 0;
}
`;

test('GL bridge source keeps the extraction anchors and the readPointers fix', () => {
  for (const label of [
    'GL64_fn_glGenQueries', 'GL64_fn_glDeleteQueries', 'GL64_fn_glIsQuery',
    'GL64_fn_glBeginQuery', 'GL64_fn_glEndQuery', 'GL64_fn_glQueryCounter',
    'GL64_fn_glGetQueryiv', 'GL64_fn_glGetQueryObjectiv',
    'GL64_fn_glGetQueryObjectuiv', 'GL64_fn_glGetQueryObjectui64v',
    'GL64_fn_glTransformFeedbackVaryings',
  ]) assert.ok(src.includes(`case ${label}:`), `dispatcher case ${label} is present`);
  const tfb = extractCase('GL64_fn_glTransformFeedbackVaryings');
  assert.match(tfb, /readPointers\(args\.a\[2\], \(size_t\)count\)/,
    'transform feedback varying pointers are read at the guest stride');
  assert.doesNotMatch(tfb, /readq\(/, 'no nonexistent readq member comes back');
});

test('GL query bookkeeping follows the host/spec state machine', () => {
  const cases = [
    extractCase('GL64_fn_glGenQueries'),
    extractCase('GL64_fn_glDeleteQueries'),
    extractCase('GL64_fn_glIsQuery'),
    extractCase('GL64_fn_glBeginQuery'),
    extractCase('GL64_fn_glEndQuery'),
    extractCase('GL64_fn_glQueryCounter'),
    extractCase('GL64_fn_glGetQueryiv'),
    extractCase('GL64_fn_glGetQueryObjectiv', 'GL64_fn_glGetQueryObjectuiv'),
    extractCase('GL64_fn_glGetQueryObjectui64v'),
  ].join('\n');
  const extra = `
${extractBookkeeping()}
GLenum g_allocationError = GL_NO_ERROR;
${extractFunction('void recordBridgeError(GLenum error)')}
`;
  const out = compileAndRun('query', buildHarness(extra, cases, QUERY_MAIN));
  assert.match(out, /all \d+ checks passed/);
});

test('glTransformFeedbackVaryings reads guest pointer arrays at the guest stride', () => {
  const extra = extractFunction('std::string readGuestCStr(GLGuestCPU* cpu, U64 guestAddr, U64 maxLen)');
  const cases = extractCase('GL64_fn_glTransformFeedbackVaryings');
  const out = compileAndRun('tfb', buildHarness(extra, cases, TFB_MAIN));
  assert.match(out, /all \d+ checks passed/);
});
