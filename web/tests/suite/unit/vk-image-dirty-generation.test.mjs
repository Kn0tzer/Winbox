// Vulkan row 377 (Tier 1): the image dirty-generation skip in emitImageDataInner
// must be PROVABLY equivalent to a full re-emit. This test extracts the REAL
// emitImageDataInner lambda from source/vulkan/vk64bridge.cpp (same idiom as
// vulkan-image-view-aliases.test.mjs) and compiles it against stubs whose guest
// bytes can be mutated through simulated write paths:
//
//   - image-addressed write  -> bumps Image::dirtyGen AND the allocation epoch
//   - buffer-addressed write -> bumps ONLY the allocation epoch (the aliasing
//     case: a mutation that never touches the image's own signal MUST still
//     be caught, via DeviceMemory::writeEpoch)
//
// The correctness invariant asserted on every submit: the emitted record's
// hash equals the hash of the CURRENT guest bytes, and a skip (pixel-less
// record with zero guest reads) happens only when nothing changed.
//
// The companion census test pins the write side in the bridge source: every
// guest-memory write path that can reach an image's bytes (or its allocation)
// bumps the generation signal, every dirtyGen bump is paired with a
// vk64TouchMem, and the total count of raw guest-write primitives is pinned --
// a new write path added anywhere in the bridge turns this test red until it
// is tied into the generation signal or dispositioned as an exception below.
//
// Written-out exception classes for raw guest writes (never device-memory
// image/buffer payloads, so they cannot desync an emit): the writeGuest*
// helper definitions themselves, Vulkan out-parameter struct writers
// (physical-device properties/features, enumeration arrays, present out
// values), and the WRITE_GUEST struct-field macro.
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
  const beginMarker = '        auto emitImageDataInner =';
  const endMarker = '        auto emitImageData =';
  const begin = bridgeSource.indexOf(beginMarker);
  const end = bridgeSource.indexOf(endMarker, begin);
  assert.ok(begin >= 0 && end > begin, 'emitImageDataInner lambda markers are present');
  return bridgeSource.slice(begin, end).trimEnd();
}

const cppPrelude = String.raw`
#include <cassert>
#include <cstdint>
#include <cstring>
#define VK64_DROP_IF(action, ...) do { if (__VA_ARGS__) { action; } } while (0)
#include <set>
#include <map>
#include <vector>
using U8=uint8_t; using U32=uint32_t; using U64=uint64_t;
struct ImageView{U64 image=9;U32 format=0,baseMip=0,levelCount=1,baseLayer=0,layerCount=1,viewType=1,aspect=1;};
struct Image{U64 mem=1,memOff=0,pixBytes=4;U64 dirtyGen=0;U32 w=1,h=1,d=1,layers=1,mips=1,format=37;};
struct DeviceMemory{U64 writeEpoch=0;bool wasMapped=false;};
ImageView view; Image image; DeviceMemory memory;
std::vector<U8> guest = {1,2,3,4};
int guestReads = 0;
enum{K_IMAGEVIEW,K_IMAGE,K_MEMORY,V2_IMAGE_DATA};
template<class T>T* objOf(U64 id,int kind,const char*){
  if(kind==K_IMAGEVIEW) return (T*)&view;
  if(kind==K_IMAGE) return (T*)&image;
  if(kind==K_MEMORY) return (T*)&memory;
  return nullptr;
}
std::vector<U8> readGuestRange(U64,U64,U64){ ++guestReads; return guest; }
U64 fnv1a64(const U8* data,size_t len){ U64 h=14695981039346656037ULL; for(size_t i=0;i<len;i++){ h^=data[i]; h*=1099511628211ULL; } return h; }
bool vkProfEnabled(){return false;}
namespace vkencode{enum Stage{ImageView,ImageDedup,ImageLookup,ImageRead,ImageHash,ImageRecord};}
struct VkEncodeProfileStub{};struct VkEncodeScope{VkEncodeScope(VkEncodeProfileStub&,int,bool){}void stop(){}};
struct VkProfAccStub{VkEncodeProfileStub encode;};VkProfAccStub& vkProfAcc(){static VkProfAccStub a;return a;}
bool detailed=false;
bool vkMetricsEnabled(){return false;}bool vkTimingEnabled(){return vkProfEnabled()||vkMetricsEnabled();}
struct ImgHashEntry{U64 hash=0,imgGen=0,memEpoch=0,mem=0,memOff=0,pixBytes=0;};
std::map<U64,ImgHashEntry> g_imageContentHash;
U64 g_v2SubmitFrames=10; const U32 V2_BUF_RESEND_PERIOD=64;
struct Rec{U64 hash=0;U32 pixLen=0;};
std::vector<Rec> records;
struct ChunkWriter{
  std::vector<U64> u64s;
  void u64(U64 v){u64s.push_back(v);} void u32(U32){}
  void bytes(const std::vector<U8>& b){ lastPix=(U32)b.size(); }
  U32 lastPix=0;
};
`;

const cppMain = String.raw`
int main(){
  std::set<U64> imageDone; U64 vkprofImgHashed=0, vkprofImgSent=0;
  auto emit=[&](int,const auto& write){ChunkWriter p;write(p);Rec r;r.hash=p.u64s[2];r.pixLen=p.lastPix;records.push_back(r);};
/*__LAMBDA__*/
  U64 tick = 1000;
  auto imgWrite = [&](U8 v){ guest[0]=v; image.dirtyGen=++tick; memory.writeEpoch=tick; };
  auto bufWrite = [&](U8 v){ guest[0]=v; memory.writeEpoch=++tick; };
  auto curHash = [&](){ return fnv1a64(guest.data(), guest.size()); };
  auto submit = [&](U64 frame){
    g_v2SubmitFrames=frame; imageDone.clear();
    size_t before = records.size();
    emitImageDataInner(1);
    assert(records.size() == before + 1);          // the record always goes out
    assert(records.back().hash == curHash());      // and its hash is never stale
    return records.back();
  };
  imgWrite(1);                                     // initial upload (execCopy path)
  Rec r = submit(10);
  assert(r.pixLen == 4 && guestReads == 1);        // first emit: full read + pixels
  r = submit(11);
  assert(r.pixLen == 0 && guestReads == 1);        // unchanged: generation skip, no read
  imgWrite(9);                                     // image-addressed rewrite
  r = submit(12);
  assert(r.pixLen == 4 && guestReads == 2);        // dirtyGen bump forces a re-read
  r = submit(13);
  assert(r.pixLen == 0 && guestReads == 2);        // skip re-engages
  bufWrite(7);                                     // buffer-addressed write into the
                                                   // same allocation: image.dirtyGen
                                                   // does NOT move
  r = submit(14);
  assert(r.pixLen == 4 && guestReads == 3);        // epoch bump alone forces a re-read
  r = submit(15);
  assert(r.pixLen == 0 && guestReads == 3);
  memory.wasMapped = true;                         // mapped memory: guest CPU can
  r = submit(16);                                  // write invisibly -> never skip
  assert(guestReads == 4);
  memory.wasMapped = false;                        // (harness liberty; the real flag
  r = submit(17);                                  // is sticky for the allocation life)
  assert(r.pixLen == 0 && guestReads == 4);
  image.memOff = 8;                                // rebind: snapshot mismatch
  r = submit(18);
  assert(guestReads == 5);
  image.memOff = 0;
  r = submit(19);
  assert(guestReads == 6);
  r = submit(20);
  assert(r.pixLen == 0 && guestReads == 6);
  r = submit(64);                                  // periodic full resend frame
  assert(r.pixLen == 4 && guestReads == 7);        // resends pixels despite same hash
  r = submit(65);
  assert(r.pixLen == 0 && guestReads == 7);
  image.dirtyGen = 0;                              // an image whose writes never went
  r = submit(66);                                  // through a tracked path must
  assert(guestReads == 8);                         // never skip
  image.dirtyGen = ++tick; memory.writeEpoch = tick;  // same-content rewrite
  r = submit(67);
  assert(r.pixLen == 0 && guestReads == 9);        // re-read, hash dedups the pixels
  r = submit(68);
  assert(r.pixLen == 0 && guestReads == 9);        // snapshot was refreshed on the
                                                   // dedup path, so the skip engages
  return 0;
}
`;

test('image generation skip never serves a stale hash (extracted emitImageDataInner)', () => {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'bw64-vk-imggen-'));
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

test('every bridge write into image memory bumps the generation signal (census)', () => {
  const lines = bridgeSource.split('\n');
  // (a) every dirtyGen bump is paired with a vk64TouchMem within 3 lines.
  const bumpIdx = lines
    .map((l, i) => (/->dirtyGen = g_bufDirtyTick\+\+/.test(l) ? i : -1))
    .filter((i) => i >= 0);
  assert.equal(bumpIdx.length, 20, 'pinned count of dirtyGen bump sites');
  for (const i of bumpIdx) {
    assert.ok(lines.slice(i, i + 4).join('\n').includes('vk64TouchMem('),
      `dirtyGen bump at line ${i + 1} lacks a paired vk64TouchMem`);
  }
  // (b) per-handler region pins, split by ABI half.
  const abiSplit = bridgeSource.indexOf('U64 vk32Bridge(');
  assert.ok(abiSplit > 0, 'vk32Bridge present');
  const half64 = bridgeSource.slice(0, abiSplit);
  const half32 = bridgeSource.slice(abiSplit);
  const regionHas = (half, caseName, needle) => {
    const begin = half.indexOf(`case VK64_fn_${caseName}:`);
    assert.ok(begin >= 0, `${caseName} case present`);
    // Handlers may stack fall-through labels (vkCmdCopyImage2: /
    // vkCmdCopyImage2KHR: {), so the region runs to the next handler's
    // closing brace + case label, not the next label line.
    const end = half.indexOf('\n    }\n    case VK64_fn_', begin);
    const region = half.slice(begin, end > begin ? end : undefined);
    assert.ok(region.includes(needle), `${caseName} region contains ${needle}`);
    return region;
  };
  const imgHandlers = ['vkCmdClearColorImage', 'vkCmdCopyImage', 'vkCmdCopyImage2',
    'vkCmdBlitImage', 'vkCmdBlitImage2', 'vkCmdResolveImage'];
  for (const h of [...imgHandlers, 'vkCmdFillBuffer', 'vkCmdCopyBuffer', 'vkCmdCopyBuffer2']) {
    regionHas(half64, h, '->dirtyGen = g_bufDirtyTick++');
    regionHas(half64, h, 'vk64TouchMem(');
  }
  for (const h of [...imgHandlers,
    'vkCmdFillBuffer', 'vkCmdCopyBuffer', 'vkCmdCopyBuffer2']) {
    regionHas(half32, h, '->dirtyGen = g_bufDirtyTick++');
    regionHas(half32, h, 'vk64TouchMem(');
  }
  // (c) submit-time executors bump too; execCopy bumps BEFORE its copy loops
  // (a mid-copy bounds failure must not leave a partially written image clean).
  for (const fn of ['void execUpdateBuf(', 'static void execCopyQueryResults(', 'static void completeReadbacks(']) {
    const begin = bridgeSource.indexOf(fn);
    assert.ok(begin >= 0, `${fn} present`);
    assert.ok(bridgeSource.slice(begin, begin + 2600).includes('vk64TouchMem('),
      `${fn} bumps the allocation epoch`);
  }
  const execCopyBegin = bridgeSource.indexOf('void execCopy(const Copy& r)');
  const execCopy = bridgeSource.slice(execCopyBegin, execCopyBegin + 4000);
  assert.ok(execCopy.indexOf('dst->dirtyGen = g_bufDirtyTick++') >= 0 &&
    execCopy.indexOf('dst->dirtyGen = g_bufDirtyTick++') < execCopy.indexOf('dst->copied = 1'),
  'execCopy bumps dst dirtyGen before the copy loops complete');
  // (d) both ABIs erase the content-hash entry on image destroy.
  assert.ok(half64.includes('case VK64_fn_vkDestroyImage:') &&
    half64.slice(half64.indexOf('case VK64_fn_vkDestroyImage:'),
      half64.indexOf('case VK64_fn_vkDestroyImage:') + 200).includes('g_imageContentHash.erase'),
  '64-bit vkDestroyImage erases the hash entry');
  assert.ok(half32.slice(half32.indexOf('case VK64_fn_vkDestroyImage:'),
      half32.indexOf('case VK64_fn_vkDestroyImage:') + 200).includes('g_imageContentHash.erase'),
  '32-bit vkDestroyImage erases the hash entry');
  // (e) the consult gates on every signal it claims to check.
  const lambda = extractEmitLambda();
  for (const gate of ['it->second.memEpoch == imem->writeEpoch', '!imem->wasMapped',
    'tex->dirtyGen != 0', 'it->second.memOff == tex->memOff', 'it->second.imgGen == tex->dirtyGen',
    'it->second.pixBytes == tex->pixBytes']) {
    assert.ok(lambda.includes(gate), `emit consult gate present: ${gate}`);
  }
  // (f) raw guest-write primitive count pin: a new write path anywhere in the
  // bridge changes this count and must be dispositioned (bump or exception).
  // 51 since the portal-perf blit fast path (2026-10-26): the 3 chunk-copy
  // memcpyToGuest lines in the vkCmdBlitImage/2 handlers (both ABIs) write the
  // same destination images as the per-pixel loops they accelerate and are
  // covered by each handler's existing post-loop dirtyGen bump + vk64TouchMem
  // (asserted per-handler in (b) above).
  const writePrims = lines.filter((l) => /memcpyToGuest\(|writeGuestBytes\(/.test(l)).length;
  assert.equal(writePrims, 51, 'pinned count of raw guest-write primitive lines');
});
