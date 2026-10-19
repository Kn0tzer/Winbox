import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('descriptor aliases retain every image view even when views share image pixels',()=>{
 const source=readFileSync(join(root,'source/vulkan/vk64bridge.cpp'),'utf8');
 const method=source.match(/auto emitImageDataInner = \[&\]\(U64 viewId\) \{[\s\S]*?\n        \};/)?.[0];assert.ok(method);
 const dir=mkdtempSync(join(tmpdir(),'vulkan-view-aliases-'));
 try{
  writeFileSync(join(dir,'check.cpp'),`
#include <cassert>
#include <cstdint>
#include <set>
#include <map>
#include <vector>
using U8=uint8_t;using U32=uint32_t;using U64=uint64_t;
struct ImageView{U64 image=9;U32 format=37;};struct Image{U64 mem=1,memOff=0,pixBytes=4;U32 w=1,h=1,d=1,layers=1,mips=1,format=37;};
ImageView views[2];Image image;enum{K_IMAGEVIEW,K_IMAGE,V2_IMAGE_DATA};
template<class T>T* objOf(U64 id,int kind,const char*){if(kind==K_IMAGEVIEW)return (T*)(id==1?&views[0]:id==2?&views[1]:nullptr);return id==9?(T*)&image:nullptr;}
std::vector<U8> readGuestRange(U64,U64,U64){return {1,2,3,4};}
U64 fnv1a64(const U8*,size_t){return 123;}
bool vkProfEnabled(){return false;}
// Live metrics share these timers (see web/tests/suite/unit/frame-metrics.test.mjs).
bool vkMetricsEnabled(){return false;}bool vkTimingEnabled(){return vkProfEnabled()||vkMetricsEnabled();}
struct ChunkWriter{std::vector<U64> words;std::vector<U32> integers;void u64(U64 v){words.push_back(v);}void u32(U32 v){integers.push_back(v);}void bytes(const std::vector<U8>&){}};
std::map<U64,U64> g_imageContentHash;U32 g_v2SubmitFrames=7,V2_BUF_RESEND_PERIOD=64;
int main(){views[0].format=43;std::set<U64> imageDone;U64 vkprofImgHashed=0,vkprofImgSent=0;std::vector<ChunkWriter> records;
auto emit=[&](int,const auto& write){ChunkWriter p;write(p);records.push_back(p);};
${method}
emitImageDataInner(1);emitImageDataInner(2);emitImageDataInner(1);
assert(records.size()==2);assert(records[0].words[0]==1 && records[1].words[0]==2);assert(records[0].words[1]==9 && records[1].words[1]==9);assert(records[0].words[2]==records[1].words[2]);
assert(records[0].integers[2]==43 && records[1].integers[2]==37);}
`);
  execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
