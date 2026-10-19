import test from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';
const face={failOp:0,passOp:2,depthFailOp:0,compareOp:7,compareMask:255,writeMask:255,reference:1};
const pipe=(extra={})=>({pipeId:'mask',topology:3,cull:0,front:0,vb:[],va:[],vsHash:'vs',fsHash:'fs',depthTest:0,depthWrite:0,blend:0,stencil:{enabled:1,dynamicMask:0,front:face,back:face},...extra});
const draw={op:'draw',vertexCount:3,instanceCount:1,firstVertex:0,firstInstance:0};
function frame(ops){return {frameNo:1,width:4,height:4,shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,clearStencil:3,attachments:[{imageId:'color',format:37,isDepth:false,samples:1,loadOp:1,storeOp:0},{imageId:'depth',format:129,isDepth:true,samples:1,loadOp:1,storeOp:0,stencilLoadOp:0,stencilStoreOp:0,aspectMask:6}],ops}]};}
async function check(run){const gpu=createValidationDevice();try{const r=await createVkRenderer2(gpu.canvas,{translate:async()=> 'fn main() {}'});try{await run(gpu,r);}finally{r.destroy();}}finally{gpu.restore();}}
test('portal mask and equal-tested view retain stencil storage and reference across passes',()=>check(async(g,r)=>{
 const f=frame([{op:'bindPipe',pipe:pipe({colorBlend:[{enable:0,writeMask:0}]})},draw]);const second=structuredClone(f.passes[0]);second.attachments.forEach(a=>a.loadOp=0);second.ops=[{op:'bindPipe',pipe:pipe({pipeId:'view',stencil:{enabled:1,dynamicMask:0,front:{...face,passOp:0,compareOp:2,writeMask:0},back:{...face,passOp:0,compareOp:2,writeMask:0}}})},draw];f.passes.push(second);await r.render(f);
 assert.equal(g.pipelines[0].depthStencil.format,'depth24plus-stencil8');assert.equal(g.pipelines[0].depthStencil.stencilFront.passOp,'replace');assert.equal(g.pipelines[1].depthStencil.stencilFront.compare,'equal');assert.equal(g.pipelines[0].fragment.targets[0].writeMask,0);assert.deepEqual(g.stencilReferences,[1,1]);assert.equal(g.passes[0].depthStencilAttachment.stencilLoadOp,'load');assert.equal(g.passes[0].depthStencilAttachment.stencilClearValue,3);assert.equal(g.passes[0].depthStencilAttachment.view.texture,g.passes[1].depthStencilAttachment.view.texture);
}));
test('dynamic stencil state is applied at draw time and culled face values are ignored',()=>check(async(g,r)=>{
 const p=pipe({cull:2,stencil:{enabled:1,dynamicMask:7,front:face,back:face}});
 await r.render(frame([{op:'bindPipe',pipe:p},{op:'stencil',kind:0,faces:1,value:15},{op:'stencil',kind:1,faces:1,value:3},{op:'stencil',kind:2,faces:1,value:7},draw,{op:'stencil',kind:2,faces:1,value:9},draw]));
 assert.equal(g.pipelines[0].depthStencil.stencilReadMask,15);assert.equal(g.pipelines[0].depthStencil.stencilWriteMask,3);assert.deepEqual(g.stencilReferences,[7,9]);assert.equal(g.draws.length,2);
}));
test('incompatible active face masks are reported instead of silently dropping stencil',()=>check(async(g,r)=>{
 const p=pipe({stencil:{enabled:1,dynamicMask:0,front:face,back:{...face,writeMask:0}}});await r.render(frame([{op:'bindPipe',pipe:p},draw]));assert.equal(g.draws.length,0);assert.ok(r.snapshot().unsupported>0);
}));
test('dynamic blend constants use the most recent command',()=>check(async(g,r)=>{
 const p=pipe({stencil:{enabled:0},colorBlend:[{enable:1,srcColor:10,dstColor:11,colorOp:0,srcAlpha:1,dstAlpha:0,alphaOp:0,writeMask:15}],blendConstantsDynamic:true});await r.render(frame([{op:'bindPipe',pipe:p},{op:'blendConstants',values:[.25,.5,.75,1]},draw]));assert.deepEqual(g.blendConstants,[[.25,.5,.75,1]]);
}));
test('stencil rectangle clears preserve depth and colour and restore draw state',()=>check(async(g,r)=>{
 const p=pipe();await r.render(frame([{op:'bindPipe',pipe:p},{op:'viewport',v:[0,0,4,4,0,1]},{op:'scissor',s:[0,0,4,4]},draw,{op:'clearAttachments',attachments:[{aspectMask:4,colorAttachment:0,colorBits:[0,0,0,0],depth:1,stencil:0}],rects:[{x:1,y:2,w:2,h:1,baseLayer:0,layers:1}]},draw]));
 const clear=g.draws[1];assert.equal(clear.fragment.targets[0].writeMask,0);assert.equal(clear.depthStencil.depthWriteEnabled,false);assert.equal(clear.depthStencil.stencilFront.passOp,'replace');assert.deepEqual(g.stencilReferences,[1,0,1]);assert.deepEqual(g.scissors,[[0,0,4,4],[1,2,2,1],[0,0,4,4],[0,0,4,4]]);assert.equal(g.draws[0],g.draws[2]);
}));
