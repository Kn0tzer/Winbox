import test from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

test('unchanged scaled vertex data converts once and invalidates on content or binding-offset changes',async()=>{
 const {device,canvas,restore}=createValidationDevice(),bound=[];
 const create=device.createCommandEncoder.bind(device);
 device.createCommandEncoder=()=>{const e=create(),begin=e.beginRenderPass.bind(e);e.beginRenderPass=d=>{const p=begin(d);p.setVertexBuffer=(slot,buffer)=>bound.push(buffer);return p;};return e;};
 const bytes=new Uint8Array(16);let conversions=0;
 bytes.subarray=function(...args){conversions++;return Uint8Array.prototype.subarray.apply(this,args);};
 try{
  const r=await createVkRenderer2(canvas,{translate:async()=> 'fn main() {}'});
  const pipe={pipeId:'p',topology:3,vsHash:'vs',fsHash:'fs',vb:[{binding:0,stride:4,inputRate:0}],va:[{binding:0,location:0,offset:0,format:39}]};
  const draw={op:'draw',vertexCount:3,instanceCount:1,firstVertex:0,firstInstance:0};
  const frame=(hash,offset=0)=>({frameNo:1,width:4,height:4,shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map([['v',bytes]]),bufHashes:new Map([['v',hash]]),readbacks:[],passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,attachments:[{isDepth:false,samples:1,loadOp:1,storeOp:0}],ops:[{op:'bindPipe',pipe},{op:'vbind',binds:[{binding:0,buf:'v',offset}]},draw,draw]}]});
  assert.equal((await r.render(frame('a'))).rendered,true);
  assert.equal((await r.render(frame('a'))).rendered,true);
  assert.equal(conversions,1);assert.equal(new Set(bound).size,1);
  await r.render(frame('b'));assert.equal(conversions,2);
  await r.render(frame('b',4));assert.equal(conversions,3);assert.equal(new Set(bound).size,3);r.destroy();
 }finally{restore();}
});
