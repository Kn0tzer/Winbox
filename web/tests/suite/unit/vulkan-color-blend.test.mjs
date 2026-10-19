import test from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2,decodeV2Frame} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

const shadow={enable:1,srcColor:0,dstColor:2,colorOp:0,srcAlpha:1,dstAlpha:0,alphaOp:0,writeMask:7};
const mask={...shadow,enable:0,writeMask:0};
function frame(pipe,count=1){return {frameNo:1,width:4,height:4,shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,attachments:Array.from({length:count},(_,i)=>({imageId:'color'+i,format:37,isDepth:false,samples:1,loadOp:1,storeOp:0})),ops:[{op:'bindPipe',pipe},{op:'draw',vertexCount:3,instanceCount:1,firstVertex:0,firstInstance:0}]}]};}
const base=()=>({pipeId:'pipe',topology:3,vb:[],va:[],vsHash:'vs',fsHash:'fs',depthTest:0,blend:1});

test('multiplicative shadows and per-attachment write masks preserve the Vulkan equations',async()=>{
 const gpu=createValidationDevice();try{const r=await createVkRenderer2(gpu.canvas,{translate:async()=> 'fn main() {}'});await r.render(frame({...base(),colorBlend:[shadow,mask]},2));
 const targets=gpu.pipelines.find(p=>p.fragment.targets.length===2).fragment.targets;
 assert.deepEqual(targets[0].blend,{color:{srcFactor:'zero',dstFactor:'src',operation:'add'},alpha:{srcFactor:'one',dstFactor:'zero',operation:'add'}});
 assert.equal(targets[0].writeMask,7);assert.equal(targets[1].writeMask,0);assert.equal(targets[1].blend,undefined);r.destroy();}finally{gpu.restore();}
});

test('old captured frames retain the previous alpha blending fallback',async()=>{
 const gpu=createValidationDevice();try{const r=await createVkRenderer2(gpu.canvas,{translate:async()=> 'fn main() {}'});await r.render(frame(base()));assert.equal(gpu.pipelines[0].fragment.targets[0].blend.color.srcFactor,'src-alpha');r.destroy();}finally{gpu.restore();}
});

function payload(blends){const bytes=[];const u32=n=>{for(let i=0;i<4;i++)bytes.push((n>>>(i*8))&255);};const u64=n=>{u32(n);u32(0);};const f32=n=>{const b=new Uint8Array(new Float32Array([n]).buffer);bytes.push(...b);};u64(1);u64(2);u64(3);[3,0,0,0,0,7,1,0,0,0,0].forEach(u32);u32(0x31444e42);u32(blends.length);for(const b of blends)[b.enable,b.srcColor,b.dstColor,b.colorOp,b.srcAlpha,b.dstAlpha,b.alphaOp,b.writeMask].forEach(u32);[0.25,0.5,0.75,1].forEach(f32);return new Uint8Array(bytes);}
function decode(p){return decodeV2Frame([{type:1,payload:new Uint8Array(new Uint32Array([1,4,4,0]).buffer)},{type:6,payload:p},{type:2,payload:new Uint8Array(new Uint32Array([1]).buffer)}]).pipelines.values().next().value;}
test('pipeline records retain each attachment blend state and static constants',()=>{const pipe=decode(payload([shadow,mask]));assert.deepEqual(pipe.colorBlend,[shadow,mask]);assert.deepEqual(pipe.blendConstants,[0.25,0.5,0.75,1]);assert.throws(()=>decode(payload(Array(5).fill(shadow))),/blend count/);});

test('constant alpha factors use the alpha value for every colour component',async()=>{
 const gpu=createValidationDevice();try{const r=await createVkRenderer2(gpu.canvas,{translate:async()=> 'fn main() {}'});await r.render(frame({...base(),colorBlend:[{...shadow,srcColor:12,dstColor:13}],blendConstants:[0.1,0.2,0.3,0.75]}));assert.deepEqual(gpu.blendConstants,[[0.75,0.75,0.75,0.75]]);r.destroy();}finally{gpu.restore();}
});

test('native pipeline capture emits all attachment fields into the decoder contract',async()=>{
 const {readFileSync,writeFileSync,mkdtempSync,rmSync}=await import('node:fs');const {execFileSync}=await import('node:child_process');const {join}=await import('node:path');const {tmpdir}=await import('node:os');
 const src=readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp',import.meta.url),'utf8');
 const structure=src.match(/struct ColorBlend \{[\s\S]*?\n\};/)?.[0];
 const capture=src.match(/template <class T>\nstatic void captureColorBlend\([\s\S]*?\n\}/)?.[0];
 const emit=src.match(/static void emitColorBlend\([\s\S]*?\n\}/)?.[0];
 assert.ok(structure&&capture&&emit);assert.equal((src.match(/captureColorBlend\(\*p, blends\)/g)||[]).length,2,'both guest ABIs capture attachment state');
 const dir=mkdtempSync(join(tmpdir(),'bw64-colour-blend-'));try{writeFileSync(join(dir,'check.cpp'),`
#include <cstdint>
#include <cstring>
#include <vector>
#include <algorithm>
#include <cstdio>
using U32=uint32_t;const U32 VK64_MAX_ATTACH=4;
${structure}
struct Pipeline{U32 colorBlendCount=0,blend=0;ColorBlend colorBlend[4];float blendConstants[4]={0.25f,0.5f,0.75f,1};};
${capture}
struct ChunkWriter{std::vector<unsigned char>b;void u32(U32 n){for(int i=0;i<4;++i)b.push_back(n>>(i*8));}void f32(float f){U32 n;memcpy(&n,&f,4);u32(n);}};
${emit}
struct Native{U32 blendEnable,srcColorBlendFactor,dstColorBlendFactor,colorBlendOp,srcAlphaBlendFactor,dstAlphaBlendFactor,alphaBlendOp,colorWriteMask;};
int main(){Pipeline p;captureColorBlend(p,std::vector<Native>{{1,0,2,0,1,0,0,7},{0,0,2,0,1,0,0,0}});ChunkWriter w;emitColorBlend(p,w);fwrite(w.b.data(),1,w.b.size(),stdout);}
`);execFileSync('c++',['-std=c++17',join(dir,'check.cpp'),'-o',join(dir,'check')]);const tail=execFileSync(join(dir,'check'));const prefix=payload([]).subarray(0,68);const pipe=decode(new Uint8Array(Buffer.concat([prefix,tail])));assert.deepEqual(pipe.colorBlend,[shadow,mask]);assert.deepEqual(pipe.blendConstants,[0.25,0.5,0.75,1]);}finally{rmSync(dir,{recursive:true,force:true});}
});
