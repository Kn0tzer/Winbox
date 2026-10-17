import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';
test('fallback textures remain alive while persistent bind groups can reuse them',async()=>{
 const {canvas,textures,restore}=createValidationDevice();
 try{
  const r=await createVkRenderer2(canvas,{translate:async(from,bytes,stage)=>stage==='fragment'?'@group(0) @binding(0) var image: texture_2d<f32>; fn main() {}':'fn main() {}'});
  const pipe={pipeId:'fallback',topology:3,vb:[],va:[],vsHash:'vs',fsHash:'fs',depthTest:false,depthWrite:false,depthOp:3};
  const frame={frameNo:1,width:4,height:4,shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,attachments:[{imageId:null,isDepth:false,samples:1,loadOp:1,storeOp:0}],ops:[{op:'bindPipe',pipe},{op:'draw',vertexCount:3,instanceCount:1,firstVertex:0,firstInstance:0}]}]};
  assert.equal((await r.render(frame)).rendered,true);await Promise.resolve();
  const fallback=textures.filter(t=>t.size[0]===1&&t.size[1]===1);
  assert.equal(fallback.length,1);assert.ok(!fallback[0].destroyed,'cached bind groups still reference this texture');
  assert.equal((await r.render({...frame,frameNo:2})).rendered,true);await Promise.resolve();
  assert.equal(textures.filter(t=>t.size[0]===1&&t.size[1]===1).length,1,'reuse the live fallback across frames');
  assert.ok(!fallback[0].destroyed);r.destroy();
 }finally{restore();}
});
