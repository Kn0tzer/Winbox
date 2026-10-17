import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';
test('cube descriptors upload all six base faces while skipping each face mip tail', async()=>{
 const {canvas,writes,textures,restore}=createValidationDevice();
 try {
  const pixels=new Uint8Array(120);for(let i=0;i<6;i++){pixels.fill(i+1,i*20,i*20+16);pixels.fill(255,i*20+16,i*20+20);}
  const r=await createVkRenderer2(canvas,{translate:async(from,bytes,stage)=>stage==='fragment'?'@group(0) @binding(0) var image: texture_cube<f32>; fn main() {}':'fn main() {}'});
  const pipe={pipeId:'cube',topology:3,vb:[],va:[],vsHash:'vs',fsHash:'fs'};
  const frame={frameNo:1,width:4,height:4,shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),images:new Map([['ci',{w:2,h:2,format:44,hash:'image',pixels}]]),views:new Map([['cv','ci']]),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,attachments:[{imageId:null,isDepth:false,samples:1,loadOp:1,storeOp:0}],ops:[{op:'bindPipe',pipe},{op:'descSet',setIndex:0,bindings:[{binding:0,kind:2,obj:'cv',dstArrayElement:0,count:1}]},{op:'draw',vertexCount:3,instanceCount:1,firstVertex:0,firstInstance:0}]}]};
  assert.equal((await r.render(frame)).rendered,true);
  const upload=writes.find(w=>w.size[2]===6);assert.ok(upload);assert.deepEqual(upload.size,[2,2,6]);assert.equal(upload.data.length,96);
  for(let i=0;i<6;i++)assert.deepEqual([...upload.data.subarray(i*16,i*16+16)],Array(16).fill(i+1));
  assert.ok(textures.some(t=>t.size[2]===6));r.destroy();
 } finally {restore();}
});
