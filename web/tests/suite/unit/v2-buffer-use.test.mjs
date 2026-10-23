import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync,mkdtempSync,writeFileSync,rmSync} from 'node:fs';
import {execFileSync} from 'node:child_process';
import {join,resolve} from 'node:path';
import {tmpdir} from 'node:os';
import {fileURLToPath} from 'node:url';
const root=resolve(fileURLToPath(new URL('../../../../',import.meta.url)));
test('geometry upload bounds use the index buffer bound at each draw before later UI bindings',()=>{
 const src=readFileSync(join(root,'source/vulkan/vk64bridge.cpp'),'utf8');
 const start=src.indexOf('U64 v2MaxVbUse[VK64_MAX_VERT_BINDINGS]');
 const end=src.indexOf('        for (const Cmd& k : st->cmds)',start);
 assert.ok(start>0&&end>start);const block=src.slice(start,end);
 const dir=mkdtempSync(join(tmpdir(),'v2-buffer-use-'));
 try{
  writeFileSync(join(dir,'check.cpp'),`
#include <cassert>
#include <cstdint>
#define VK64_DROP_IF(action, ...) do { if (__VA_ARGS__) { action; } } while (0) // bridge macro minus logging
#include <vector>
#include <algorithm>
#include <cstring>
using U8=uint8_t;using U32=uint32_t;using U64=uint64_t;
enum {CMD_BIND_PIPE,CMD_BIND_VB,CMD_BIND_IB,CMD_DRAW_INDEXED,CMD_DRAW,K_BUFFER,K_PIPELINE};
const U32 VK64_MAX_VERT_BINDINGS=4,VK64_MAX_VERT_ATTRIBS=16;
struct Buffer{U64 mem=0,memOff=0;};Buffer world{101,0},ui{102,0};
struct Pipeline{bool dynamicVertexStride=false;U32 nva=1,vaBinding[16]={0},vbStride[4]={12},vaOffset[16]={0},vaFormat[16]={1};} pipeline;
template<class T>T* objOf(U64 id,int kind,const char*){if(kind==K_PIPELINE)return (T*)&pipeline;return id==11?(T*)&world:id==12?(T*)&ui:nullptr;}
U32 v2VertexFormatBytes(U32){return 12;}
std::vector<U8> readGuestRange(U64 mem,U64 off,U64 len){std::vector<U8> all;if(mem==101){U32 a[]={0,0,0,400,800,2000};all.assign((U8*)a,(U8*)a+sizeof(a));}else{U32 a[]={0x00010000,2};all.assign((U8*)a,(U8*)a+6);}if(off>=all.size())return {};return std::vector<U8>(all.begin()+off,all.begin()+std::min<U64>(off+len,all.size()));}
struct Cmd{U32 kind;U64 a=0,b=0,c=0;int32_t diVertexOffset=0;U32 vbCount=0,vbStrides[4]={};bool vbHasStride[4]={};};
struct CmdBuf{U64 ib=12,ibOff=0;U32 ibType=0;std::vector<Cmd> cmds;} cb;
int main(){cb.cmds={{CMD_BIND_PIPE,1},{CMD_BIND_IB,11,8,1},{CMD_DRAW_INDEXED,3,1,1,16},{CMD_BIND_IB,12,0,0},{CMD_DRAW_INDEXED,3,1,0,0}};auto* st=&cb;
${block}
assert(v2MaxVbUse[0]>=24204);assert(v2MaxIbUse==16);}
`);
  execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);execFileSync(join(dir,'check'));
 }finally{rmSync(dir,{recursive:true,force:true});}
});
