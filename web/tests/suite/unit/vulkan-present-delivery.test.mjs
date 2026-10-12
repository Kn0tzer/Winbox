import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('present delivers every queued submission in order with an explicit final boundary',()=>{
 const src=readFileSync(join(root,'source/vulkan/vk64bridge.cpp'),'utf8');
 let method=src.match(/void hopV2ToPage\(\) \{[\s\S]*?\n\}/)?.[0];assert.ok(method);
 method=method.replace(/MAIN_THREAD_EM_ASM\(\{[\s\S]*?\}, ptr, len, ([^;]+)\);/g,'fakeHop(ptr,len,$1);');
 const dir=mkdtempSync(join(tmpdir(),'vulkan-present-delivery-'));
 try{
  writeFileSync(join(dir,'check.cpp'),`
#define __EMSCRIPTEN__ 1
#include <cassert>
#include <cstdint>
#include <vector>
#include <deque>
using U8=uint8_t;using U32=uint32_t;using U64=uint64_t;
struct V2DoneFrame{std::vector<std::vector<U8>> chunks;};std::deque<V2DoneFrame> g_v2Pending;
struct VkProfAcc{double accHop=0;U64 accHopChunks=0,accHopBytes=0;};
double vkProfNow(){return 0;}bool vkProfEnabled(){return false;}VkProfAcc& vkProfAcc(){static VkProfAcc a;return a;}
struct Hop{U8 marker;U32 flags;};std::vector<Hop> hops;
void fakeHop(const U8* p,U32,U32 flags){hops.push_back({*p,flags});}
${method}
int main(){g_v2Pending.push_back({{{11},{12}}});g_v2Pending.push_back({{{22,22,22}}});hopV2ToPage();
assert(hops.size()==3);assert(hops[0].marker==11 && hops[1].marker==12 && hops[2].marker==22);assert(hops[0].flags==1 && hops[1].flags==1 && hops[2].flags==0);assert(g_v2Pending.empty());}
`);
  execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
