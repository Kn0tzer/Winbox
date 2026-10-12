import test from 'node:test';
import assert from 'node:assert/strict';
import {V2PresentAssembler,V2FrameQueue,createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

const frame=(number,loadOp=1)=>({frameNo:number,width:4,height:4,shaders:new Map(),images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],
 passes:[{w:4,h:4,clearColor:[1,0,0,1],clearDepth:1,ops:[],attachments:[{imageId:'0000000000000007',isDepth:false,samples:1,loadOp,storeOp:0}]}]});

test('all submissions for one present stay ordered instead of selecting the largest payload',()=>{
 const a=new V2PresentAssembler(),world=frame(1),ui=frame(1,0);
 assert.equal(a.push(world,1),null);
 const batch=a.push(ui,0);
 assert.deepEqual(batch.submissions,[world,ui]);assert.equal(batch.passes.length,2);
 assert.equal(a.push(frame(2),0).submissions,undefined);
});

test('dropped presentation batches retain image deltas from their earlier submissions',()=>{
 const a=new V2PresentAssembler(),q=new V2FrameQueue(1),world=frame(1),ui=frame(1,0);
 world.images.set('image',{hash:'a',pixels:new Uint8Array([1,2,3,4])});
 a.push(world,1);q.push(a.push(ui,0));
 const later=frame(2);later.images.set('image',{hash:'a',pixels:null});q.push(later);
 assert.deepEqual(q.takeLatest().images.get('image').pixels,new Uint8Array([1,2,3,4]));
});

test('world and UI submissions both render before a single canvas presentation',async()=>{
 const {device,canvas,restore}=createValidationDevice(),copies=[],passes=[];
 const create=device.createCommandEncoder.bind(device);
 device.createCommandEncoder=()=>{const e=create(),begin=e.beginRenderPass.bind(e);e.beginRenderPass=d=>{passes.push(d);return begin(d);};e.copyTextureToTexture=(...args)=>copies.push(args);return e;};
 try{
  const r=await createVkRenderer2(canvas,{translate:async()=> 'fn main() {}'}),a=new V2PresentAssembler();
  a.push(frame(1),1);assert.equal((await r.render(a.push(frame(1,0),0))).rendered,true);
  assert.equal(passes.length,2);assert.equal(copies.length,1);
  assert.equal(passes[0].colorAttachments[0].view.texture,passes[1].colorAttachments[0].view.texture);
  assert.equal(passes[1].colorAttachments[0].loadOp,'load');r.destroy();
 }finally{restore();}
});
