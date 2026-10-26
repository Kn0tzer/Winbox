import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

test('image uploads preserve BC mip/layer offsets, row padding, and allocation bounds', () => {
  const source = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const helpers = extractSourceSlice(source, 'struct FormatBlock', '// Fills a guest VkMemoryRequirements2', { sourceName: 'source/vulkan/vk64bridge.cpp' })
    + extractSourceSlice(source, 'U64 imageSubresourceOffset(', 'const char* fmt_name', { sourceName: 'source/vulkan/vk64bridge.cpp' });
  const copy = extractSourceSlice(source, 'static void execCopyFail(', 'std::vector<U8> readGuestRange', { sourceName: 'source/vulkan/vk64bridge.cpp' });
  const dir = mkdtempSync(join(tmpdir(), 'vk-image-transfer-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstring>
#include <cstdint>
#include <algorithm>
#include <vector>
#include <string>
#include <unordered_map>
#define klog_fmt(...) ((void)0)
using U32 = uint32_t; using U64 = uint64_t; using U8 = uint8_t;
enum {K_BUFFER, K_IMAGE, K_MEMORY};
struct Image { U32 w=8,h=8,d=1,mips=3,layers=2,format=133,copied=0; U64 mem=4,memOff=0,dirtyGen=0; } image;
struct Buffer { U64 mem=3,memOff=0; } buffer;
struct DeviceMemory { U64 va=0,size=0,writeEpoch=0; } srcMemory{1024,128}, dstMemory{2048,96};
static U64 g_bufDirtyTick=1; static void vk64TouchMem(DeviceMemory* m){ if(m) m->writeEpoch=g_bufDirtyTick++; }
struct Copy { U64 srcBuffer=1,dstImage=2,bufOff=0; U32 rowLen=0,imgH=0; int32_t ox=0,oy=0; U32 w=0,h=0,mip=0,baseLayer=0,layerCount=1,depth=1; int32_t oz=0; };
unsigned char memory[4096];
template<class T> T* objOf(U64 id,int,const char*) { return reinterpret_cast<T*>(id==1?(void*)&buffer:id==2?(void*)&image:id==3?(void*)&srcMemory:id==4?(void*)&dstMemory:nullptr); }
U32 fmt_bpp(U32) {return 4;}
void readGuestBytes(void* dst,U64 at,U64 n) {assert(at+n<=sizeof(memory));memcpy(dst,memory+at,n);}
void writeGuestBytes(U64 at,const void* src,U64 n) {assert(at+n<=sizeof(memory));memcpy(memory+at,src,n);}
${helpers}
${copy}
int main() {
  assert(imageMipBytes(image,0)==32);
  assert(imageMipBytes(image,1)==8);
  assert(imageMipBytes(image,2)==8);
  assert(imageDataBytes(image)==96);
  assert(imageSubresourceOffset(image,1,1)==80);
  memset(memory,0xa5,sizeof(memory));
  for(unsigned i=0;i<8;i++) memory[1040+i]=i;
  Copy r; r.bufOff=16; r.mip=1; r.baseLayer=1; r.w=4; r.h=4;
  execCopy(r);
  for(unsigned i=0;i<96;i++) assert(memory[2048+i]==(i>=80&&i<88?i-80:0xa5));
  image={}; image.w=2; image.h=2; image.mips=1; image.layers=1; image.format=37;
  dstMemory.size=16;
  memset(memory+2048,0xa5,96);
  for(unsigned i=0;i<32;i++) memory[1024+i]=i;
  r={}; r.w=2; r.h=2; r.rowLen=4;
  execCopy(r);
  for(unsigned i=0;i<16;i++) assert(memory[2048+i]==(i<8?i:i+8));
  for(unsigned i=16;i<96;i++) assert(memory[2048+i]==0xa5);
  memset(memory+2048,0xa5,96);
  dstMemory.size=4; // Short destination cannot receive even the first row.
  execCopy(r);
  for(unsigned i=0;i<96;i++) assert(memory[2048+i]==0xa5);
}
`);
    execFileSync('c++', ['-std=c++17', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
    assert.ok(helpers && copy);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
