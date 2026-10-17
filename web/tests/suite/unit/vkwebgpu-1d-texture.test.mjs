import test from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

test('one-dimensional lookup textures retain their shader and GPU binding dimensions',async()=>{
 const {device,canvas,writes,textures,restore}=createValidationDevice();
 const create=device.createRenderPipeline.bind(device);device.createRenderPipeline=d=>{assert.equal(d.layout.bindGroupLayouts[0].entries[0].texture.viewDimension,'1d');return create(d);};
 try{
  const r=await createVkRenderer2(canvas,{translate:async(from,bytes,stage)=>stage==='fragment'?'@group(0) @binding(0) var image: texture_1d<f32>; fn main() {}':'fn main() {}'});
  const pipe={pipeId:'1d',topology:3,vb:[],va:[],vsHash:'vs',fsHash:'fs'};
  const frame={frameNo:1,width:4,height:4,shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),images:new Map([['i',{w:256,h:1,format:37,hash:'image',pixels:new Uint8Array(1024)}]]),views:new Map([['v','i']]),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,attachments:[{isDepth:false,samples:1,loadOp:1,storeOp:0}],ops:[{op:'bindPipe',pipe},{op:'descSet',setIndex:0,bindings:[{binding:0,kind:2,obj:'v',dstArrayElement:0,count:1}]},{op:'draw',vertexCount:3,instanceCount:1,firstVertex:0,firstInstance:0}]}]};
  assert.equal((await r.render(frame)).rendered,true);
  const uploaded=textures.find(t=>t.size[0]===256);assert.equal(uploaded.dimension,'1d');assert.equal(writes.find(w=>w.size[0]===256).size[1],1);
  frame.images.clear();frame.views.clear();assert.equal((await r.render({...frame,frameNo:2})).rendered,true);
  assert.ok(textures.some(t=>t.dimension==='1d' && t.size[0]===1));r.destroy();
 }finally{restore();}
});
