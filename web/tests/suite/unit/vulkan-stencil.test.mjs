import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {decodeV2Frame,V2_REC} from '../../../vk-protocol.mjs';

function writer(){const a=[];const w={u32(n){for(let i=0;i<4;i++)a.push((n>>>(8*i))&255);return w;},u64(n){return w.u32(n).u32(0);},f32(n){const b=new Uint8Array(new Float32Array([n]).buffer);a.push(...b);return w;},bytes(){return new Uint8Array(a);}};return w;}
function frame(records){return decodeV2Frame([{type:1,payload:writer().u32(1).u32(4).u32(4).u32(0).bytes()},...records,{type:2,payload:writer().u32(1).bytes()}]);}
const face={failOp:0,passOp:2,depthFailOp:0,compareOp:2,compareMask:255,writeMask:255,reference:1};
function pipeline(){const w=writer().u64(1).u64(2).u64(3);[3,0,0,1,1,3,0,0,0,0,0].forEach(n=>w.u32(n));w.u32(0x31444e42).u32(0);[0,0,0,0].forEach(n=>w.f32(n));w.u32(0x314e5453).u32(1).u32(7).u32(1);for(let i=0;i<2;i++)Object.values(face).forEach(n=>w.u32(n));return w.bytes();}

test('pipeline transport retains stencil operations, masks, reference and dynamic flags',()=>{const p=frame([{type:6,payload:pipeline()}]).pipelines.values().next().value;assert.deepEqual(p.stencil,{enabled:1,dynamicMask:7,front:face,back:face});assert.equal(p.blendConstantsDynamic,true);});

test('dynamic stencil, blend constants and rectangle clears preserve command order',()=>{
 assert.equal(V2_REC.STENCIL_STATE,0x15);assert.equal(V2_REC.BLEND_CONSTANTS,0x16);assert.equal(V2_REC.CLEAR_ATTACHMENTS,0x17);
 const clear=writer().u32(1).u32(4).u32(0);[0,0,0,0].forEach(n=>clear.u32(n));clear.f32(1).u32(7).u32(1);[1,2,3,4,0,1].forEach(n=>clear.u32(n));
 const ops=frame([{type:0x15,payload:writer().u32(2).u32(3).u32(7).bytes()},{type:0x16,payload:writer().f32(0.25).f32(0.5).f32(0.75).f32(1).bytes()},{type:0x17,payload:clear.bytes()}]).passes[0].ops;
 assert.deepEqual(ops[0],{op:'stencil',kind:2,faces:3,value:7});assert.deepEqual(ops[1],{op:'blendConstants',values:[0.25,0.5,0.75,1]});assert.equal(ops[2].op,'clearAttachments');assert.equal(ops[2].attachments[0].stencil,7);assert.deepEqual(ops[2].rects,[{x:1,y:2,w:3,h:4,baseLayer:0,layers:1}]);
 assert.throws(()=>frame([{type:0x15,payload:writer().u32(3).u32(3).u32(0).bytes()}]),/stencil/);
 assert.throws(()=>frame([{type:0x17,payload:writer().u32(9).bytes()}]),/clear attachment count/);
});

test('render passes retain independent stencil load/store and attachment aspects',()=>{
 const w=writer().u64(1).u64(2).u32(4).u32(4).u32(2);[37,1,0,1,0,129,0,0,1,1].forEach(n=>w.u32(n));[0,0,0,1,1].forEach(n=>w.f32(n));w.u32(3);[10,11,12,13].forEach(n=>w.u64(n));w.u32(0x31505453).u32(2);[0,0,1,1,0,6].forEach(n=>w.u32(n));const p=frame([{type:3,payload:w.bytes()}]).passes[0];assert.equal(p.clearStencil,3);assert.equal(p.attachments[1].stencilLoadOp,1);assert.equal(p.attachments[1].stencilStoreOp,0);assert.equal(p.attachments[1].aspectMask,6);
});

function ids(source){const first=source.indexOf('VK64_fn_'),start=source.lastIndexOf('enum',first),end=source.indexOf('VK64_fn__MAX',first);const body=source.slice(start,end).replace(/\/\*[\s\S]*?\*\//g,'').replace(/\/\/[^\n]*/g,'');const map=new Map();let value=-1;for(const part of body.split(',')){const m=part.match(/(VK64_fn_\w+)\s*(?:=\s*(\d+))?/);if(!m)continue;value=m[2]===undefined?value+1:Number(m[2]);map.set(m[1],value);}return map;}
test('both guest shims expose appended dynamic-state traps without renumbering old calls',()=>{
 const paths=['source/vulkan/vk64bridge_abi.h','tools/rootfs64/libvk32/libvk32.c','tools/rootfs64/libvk64/libvk64.c'];const names=['vkCmdSetBlendConstants','vkCmdSetStencilCompareMask','vkCmdSetStencilWriteMask','vkCmdSetStencilReference'];
 for(const path of paths){const source=readFileSync(new URL('../../../../'+path,import.meta.url),'utf8'),table=ids(source);assert.equal(table.get('VK64_fn_vkCreateInstance'),200,path);assert.equal(table.get('VK64_fn_vkBeginCommandBuffer'),500,path);for(let i=0;i<names.length;i++)assert.equal(table.get('VK64_fn_'+names[i]),900+i,path+':'+names[i]);if(path.endsWith('.c'))for(const name of names){assert.ok(source.includes('API void '+name+'('),name);assert.ok(source.includes('E('+name+','),name);assert.ok(!source.includes('TAIL('+name+')'),name);}}
});

test('native stencil capture and emission round-trip into the decoder',async()=>{
 const {writeFileSync,mkdtempSync,rmSync}=await import('node:fs');const {execFileSync}=await import('node:child_process');const {join}=await import('node:path');const {tmpdir}=await import('node:os');
 const src=readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp',import.meta.url),'utf8');
 const faceStruct=src.match(/struct StencilFace \{[\s\S]*?\n\};/)[0],stateStruct=src.match(/struct StencilState \{[\s\S]*?\n\};/)[0];
 const capture=src.match(/template <class T>\nstatic void captureStencil\([\s\S]*?\n\}/)[0],emit=src.match(/static void emitStencil\([\s\S]*?\n\}/)[0];
 assert.equal((src.match(/captureStencil\(\*p, ds\)/g)||[]).length,2);
 const dir=mkdtempSync(join(tmpdir(),'bw64-stencil-'));try{writeFileSync(join(dir,'check.cpp'),`
#include <cstdint>
#include <cstdio>
#include <vector>
using U32=uint32_t;
${faceStruct}
${stateStruct}
struct Pipeline{StencilState stencil;bool blendConstantsDynamic=true;};
${capture}
struct ChunkWriter{std::vector<unsigned char>b;void u32(U32 n){for(int i=0;i<4;i++)b.push_back(n>>(8*i));}};
${emit}
struct NativeFace{U32 failOp,passOp,depthFailOp,compareOp,compareMask,writeMask,reference;};
struct Native{U32 stencilTestEnable;NativeFace front,back;};
int main(){Pipeline p;captureStencil(p,Native{1,{0,2,0,2,255,255,1},{0,2,0,2,255,255,1}});p.stencil.dynamicMask=7;ChunkWriter w;emitStencil(p,w);fwrite(w.b.data(),1,w.b.size(),stdout);}
`);execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);const tail=execFileSync(join(dir,'check'));const complete=pipeline();const bytes=new Uint8Array(Buffer.concat([complete.subarray(0,complete.length-tail.length),tail]));assert.deepEqual(frame([{type:6,payload:bytes}]).pipelines.values().next().value.stencil,{enabled:1,dynamicMask:7,front:face,back:face});}finally{rmSync(dir,{recursive:true,force:true});}
});
