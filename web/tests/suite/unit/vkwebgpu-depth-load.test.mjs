import test from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

test('later passes preserve depth with Vulkan LOAD and honor depth discard',async()=>{
 const {device,canvas,restore}=createValidationDevice();
 const passes=[];
 const create=device.createCommandEncoder.bind(device);
 device.createCommandEncoder=()=>{const e=create();const begin=e.beginRenderPass.bind(e);e.beginRenderPass=d=>{passes.push(d);return begin(d);};return e;};
 try{
  const renderer=await createVkRenderer2(canvas,{translate:async()=> 'fn main() {}'});
  const pass=(loadOp,storeOp)=>({w:4,h:4,clearColor:[0,0,0,1],clearDepth:0.25,ops:[],attachments:[{imageId:'0000000000005678',isDepth:false,samples:1,loadOp:1,storeOp:0},{imageId:'0000000000001234',isDepth:true,samples:1,loadOp,storeOp}]});
  const frame={frameNo:1,width:4,height:4,shaders:new Map(),images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],passes:[pass(1,0),pass(0,0),pass(2,1)]};
  assert.equal((await renderer.render(frame)).rendered,true);
  assert.deepEqual(passes.map(p=>[p.depthStencilAttachment.depthLoadOp,p.depthStencilAttachment.depthStoreOp]),[['clear','store'],['load','store'],['clear','discard']]);
  assert.equal(passes[0].depthStencilAttachment.view.texture,passes[1].depthStencilAttachment.view.texture);
  renderer.destroy();
 }finally{restore();}
});
