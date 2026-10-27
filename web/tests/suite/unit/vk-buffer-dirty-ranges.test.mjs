// Portal-perf round 2: the buffer emit skip in emitBufferDataInner must be
// PROVABLY equivalent to a full re-emit, per emit RANGE. Round 1 compared
// the whole buffer's dirtyGen against the snapshot, but emit keys are
// per-range: DXVK writes dynamic data into one range of a buffer on every
// submit while other ranges (static geometry, constant blocks) never change,
// and every submit re-hashed all of them (~15 MB/frame on Portal). The skip
// now consults the backing allocation's ranged write log
// (vk64MemRangeCleanSince): a snapshot proves an emit range current iff no
// logged write since the snapshot intersects the range.
//
// This test extracts the REAL emitBufferDataInner lambda from
// source/vulkan/vk64bridge.cpp (same idiom as
// vk-image-dirty-generation.test.mjs) and compiles it against stubs whose
// guest bytes can be mutated through simulated write paths:
//
//   - disjoint write into the same VkBuffer (bumps its dirtyGen + logs a
//     non-intersecting range) -> the emit MUST still skip (round-2 fix)
//   - intersecting write into the emit range -> MUST re-read and resend
//   - write through a DIFFERENT VkBuffer aliasing the same allocation
//     range (this buffer's dirtyGen does not move) -> MUST re-read
//   - mapped memory -> never skip; resend cadence -> always full
//
// The invariant asserted on every submit: the emitted record's hash equals
// the hash of the CURRENT guest bytes, and a skip (byte-less record with
// zero guest reads) happens only when the range provably did not change.
import test from 'node:test';
import assert from 'node:assert/strict';
import childProcess from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const unitDir = path.dirname(fileURLToPath(import.meta.url));
const repoRoot = path.resolve(unitDir, '../../../..');
const bridgeSource = fs.readFileSync(path.join(repoRoot, 'source/vulkan/vk64bridge.cpp'), 'utf8');

function extractEmitLambda() {
  const beginMarker = '        auto emitBufferDataInner =';
  const endMarker = '        auto emitBufferData =';
  const begin = bridgeSource.indexOf(beginMarker);
  const end = bridgeSource.indexOf(endMarker, begin);
  assert.ok(begin >= 0 && end > begin, 'emitBufferDataInner lambda markers are present');
  return bridgeSource.slice(begin, end).trimEnd();
}

const cppPrelude = String.raw`
#include <cassert>
#include <cstdint>
#include <cstring>
#include <tuple>
#define VK64_DROP_IF(action, ...) do { if (__VA_ARGS__) { action; } } while (0)
#include <set>
#include <map>
#include <vector>
using U8=uint8_t; using U32=uint32_t; using U64=uint64_t;
struct MemWriteRange{U64 epoch=0,off=0,len=0;};
struct DeviceMemory{U64 writeEpoch=0;bool wasMapped=false;std::vector<MemWriteRange> writes;U64 writesFence=0;};
struct Buffer{U64 mem=1,memOff=0,size=16,dirtyGen=1;};
Buffer buffer; DeviceMemory memory;
std::vector<U8> guest = {1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16};
int guestReads = 0;
enum{K_BUFFER,K_MEMORY,V2_BUFFER_DATA};
template<class T>T* objOf(U64 id,int kind,const char*){
  if(kind==K_BUFFER) return (T*)&buffer;
  if(kind==K_MEMORY) return (T*)&memory;
  return nullptr;
}
std::vector<U8> readGuestRange(U64,U64 off,U64 len){ ++guestReads; return std::vector<U8>(guest.begin()+off, guest.begin()+off+len); }
U64 fnv1a64(const U8* data,size_t len){ U64 h=14695981039346656037ULL; size_t i=0; for(;i+8<=len;i+=8){ U64 w; memcpy(&w,data+i,8); h^=w; h*=1099511628211ULL; } for(;i<len;i++){ h^=(U64)data[i]; h*=1099511628211ULL; } h^=(U64)len; h*=1099511628211ULL; h^=h>>33; h*=0xff51afd7ed558ccdULL; h^=h>>33; return h; }
inline bool vk64MemRangeCleanSince(const DeviceMemory* m,U64 epoch,U64 off,U64 len){
  if(!m) return false;
  if(epoch < m->writesFence) return false;
  const auto& w = m->writes;
  size_t lo=0, hi=w.size();
  while(lo<hi){ size_t mid=(lo+hi)/2; if(w[mid].epoch<=epoch) lo=mid+1; else hi=mid; }
  for(size_t i=lo;i<w.size();i++) if(w[i].off < off+len && off < w[i].off+w[i].len) return false;
  return true;
}
bool vkTimingEnabled(){return false;}
template<class... A> void klog_fmt(A&&...) {}
struct BufHashEntry{U64 hash=0,memEpoch=0,mem=0;};
std::map<std::tuple<U64,U64,U64>, BufHashEntry> g_bufferContentHash;
U64 g_v2SubmitFrames=10; const U32 V2_BUF_RESEND_PERIOD=64; const U64 V2_MAX_BUF_SEND=1<<20;
struct Rec{U64 hash=0;U32 pixLen=0;};
std::vector<Rec> records;
struct ChunkWriter{
  std::vector<U64> u64s; U32 lastU32=0;
  void u64(U64 v){u64s.push_back(v);} void u32(U32 v){lastU32=v;}
  void bytes(const std::vector<U8>&){}
};
`;

const cppMain = String.raw`
int main(){
  std::set<std::tuple<U64,U64,U64>> bufDone; U64 vkprofBufHashed=0, vkprofBufSent=0;
  auto emit=[&](int,const auto& write){ChunkWriter p;write(p);Rec r;r.hash=p.u64s[2];r.pixLen=p.lastU32;records.push_back(r);};
/*__LAMBDA__*/
  U64 tick = 1000;
  auto curHash = [&](){ return fnv1a64(guest.data(), 8); };
  auto submit = [&](U64 frame){
    g_v2SubmitFrames=frame; bufDone.clear();
    size_t before = records.size();
    emitBufferDataInner(7, 0, 8, 7);
    assert(records.size() == before + 1);          // the record always goes out
    assert(records.back().hash == curHash());      // and its hash is never stale
    return records.back();
  };
  Rec r = submit(10);
  assert(r.pixLen == 8 && guestReads == 1);        // first emit: full read + bytes
  r = submit(11);
  assert(r.pixLen == 0 && guestReads == 1);        // unchanged: range skip, no read
  // Disjoint write into the SAME buffer (its dirtyGen moves, the logged
  // range [12,13) does not intersect the emit range [0,8)).
  guest[12]=99; memory.writeEpoch=++tick; memory.writes.push_back({tick, 12, 1}); buffer.dirtyGen=++tick;
  r = submit(12);
  assert(r.pixLen == 0 && guestReads == 1);        // STILL skipped (round-2 fix;
                                                   // round 1 re-read + re-hashed)
  // Intersecting write into the emit range.
  guest[3]=42; memory.writeEpoch=++tick; memory.writes.push_back({tick, 3, 1}); buffer.dirtyGen=++tick;
  r = submit(13);
  assert(r.pixLen == 8 && guestReads == 2);        // re-read + resend
  r = submit(14);
  assert(r.pixLen == 0 && guestReads == 2);        // skip re-engages
  // Write through a DIFFERENT buffer aliasing the same allocation range:
  // this buffer's dirtyGen does NOT move; only the ranged log sees it.
  guest[5]=77; memory.writeEpoch=++tick; memory.writes.push_back({tick, 5, 2});
  r = submit(15);
  assert(r.pixLen == 8 && guestReads == 3);        // re-read + resend
  r = submit(16);
  assert(r.pixLen == 0 && guestReads == 3);
  memory.wasMapped = true;                         // mapped memory: guest CPU
  r = submit(17);                                  // writes are invisible
  assert(guestReads == 4);                         // -> never skip
  memory.wasMapped = false;
  r = submit(18);
  assert(r.pixLen == 0 && guestReads == 4);        // skip re-engages
  r = submit(64);                                  // periodic full resend frame
  assert(r.pixLen == 8 && guestReads == 5);
  r = submit(65);
  assert(r.pixLen == 0 && guestReads == 5);
  // Log collapse past the snapshot: the fence forbids the range skip once.
  memory.writeEpoch = ++tick; memory.writes.clear(); memory.writesFence = tick;
  memory.writes.push_back({tick, 1024, 4});
  r = submit(66);
  assert(guestReads == 6);                         // fence: forced re-read
  r = submit(67);
  assert(r.pixLen == 0 && guestReads == 6);        // skip re-engages past the fence
  return 0;
}
`;

test('buffer range skip never serves a stale hash (extracted emitBufferDataInner)', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'bw64-vk-bufgen-'));
  const sourcePath = path.join(dir, 'main.cpp');
  const exePath = path.join(dir, 'main');
  const lambda = extractEmitLambda();
  fs.writeFileSync(sourcePath, cppPrelude + cppMain.replace('/*__LAMBDA__*/', () => lambda));
  const compile = childProcess.spawnSync('g++', ['-std=c++17', '-O0', '-o', exePath, sourcePath],
    { encoding: 'utf8' });
  assert.equal(compile.status, 0, compile.stderr);
  const run = childProcess.spawnSync(exePath, [], { encoding: 'utf8' });
  assert.equal(run.status, 0, run.stderr || run.stdout);
});

test('the ranged consult itself (extracted vk64MemRangeCleanSince)', () => {
  // The emit tests above stub this helper in their prelude, so the REAL
  // helper gets its own extraction test: a broken intersection test or a
  // dropped fence check in the bridge must turn this red.
  const beginMarker = 'inline bool vk64MemRangeCleanSince(';
  const begin = bridgeSource.indexOf(beginMarker);
  assert.ok(begin >= 0, 'vk64MemRangeCleanSince present in bridge');
  const end = bridgeSource.indexOf('\n}\n', begin) + 3;
  const helper = bridgeSource.slice(begin, end);
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'bw64-vk-rangeclean-'));
  const sourcePath = path.join(dir, 'main.cpp');
  const exePath = path.join(dir, 'main');
  const src = String.raw`
#include <cassert>
#include <cstdint>
#include <vector>
using U64=uint64_t;
struct MemWriteRange{ U64 epoch = 0; U64 off = 0; U64 len = 0; };
struct DeviceMemory { std::vector<MemWriteRange> writes; U64 writesFence = 0; };
/*__HELPER__*/
int main(){
  DeviceMemory m;
  assert(vk64MemRangeCleanSince(&m, 100, 0, 8));            // empty log
  m.writes.push_back({50, 0, 8});
  assert(vk64MemRangeCleanSince(&m, 100, 0, 8));            // write predates snapshot
  m.writes.push_back({150, 1024, 4});
  assert(vk64MemRangeCleanSince(&m, 100, 0, 8));            // disjoint write after
  m.writes.push_back({160, 8, 4});
  assert(vk64MemRangeCleanSince(&m, 100, 0, 8));            // adjacent [8,12) is disjoint (half-open)
  m.writes.push_back({170, 7, 1});
  assert(!vk64MemRangeCleanSince(&m, 100, 0, 8));           // [7,8) intersects
  assert(vk64MemRangeCleanSince(&m, 100, 100, 4));          // other ranges still clean
  assert(!vk64MemRangeCleanSince(&m, 100, 6, 4));           // straddling write
  DeviceMemory f; f.writesFence = 200; f.writes.push_back({250, 1024, 4});
  assert(!vk64MemRangeCleanSince(&f, 100, 0, 8));           // snapshot predates the fence
  assert(vk64MemRangeCleanSince(&f, 200, 0, 8));            // snapshot at the fence: only newer writes count
  assert(!vk64MemRangeCleanSince(nullptr, 0, 0, 8));        // no allocation: never clean
  return 0;
}
`;
  fs.writeFileSync(sourcePath, src.replace('/*__HELPER__*/', () => helper));
  const compile = childProcess.spawnSync('g++', ['-std=c++17', '-O0', '-o', exePath, sourcePath],
    { encoding: 'utf8' });
  assert.equal(compile.status, 0, compile.stderr);
  const run = childProcess.spawnSync(exePath, [], { encoding: 'utf8' });
  assert.equal(run.status, 0, run.stderr || run.stdout);
});

test('buffer emit consult uses the ranged log, not the whole-buffer generation', () => {
  const lambda = extractEmitLambda();
  for (const gate of ['vk64MemRangeCleanSince(bmem, it->second.memEpoch, memOff, len)',
    'it->second.mem == b->mem', 'b->dirtyGen != 0', '!bmem->wasMapped']) {
    assert.ok(lambda.includes(gate), `emitBufferDataInner consult gate present: ${gate}`);
  }
  assert.ok(!lambda.includes('it->second.dirtyGen == b->dirtyGen'),
    'the round-1 whole-buffer dirtyGen equality is gone');
});
