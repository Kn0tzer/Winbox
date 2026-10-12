import test from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2,decodeV2Frame,V2_REC} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

test('volume lookup textures upload every depth slice and use a matching fallback view',async()=>{
 const {canvas,writes,textures,restore}=createValidationDevice();
 try{
  const r=await createVkRenderer2(canvas,{translate:async(from,bytes,stage)=>stage==='fragment'?'@group(0) @binding(0) var image: texture_3d<f32>; fn main() {}':'fn main() {}'});
  const pixels=Uint8Array.from({length:32},(_,i)=>i),pipe={pipeId:'3d',topology:3,vb:[],va:[],vsHash:'vs',fsHash:'fs'};
  const frame={frameNo:1,width:4,height:4,shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),images:new Map([['i',{w:2,h:2,d:2,format:37,hash:'image',pixels}]]),views:new Map([['v','i']]),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,attachments:[{isDepth:false,samples:1,loadOp:1,storeOp:0}],ops:[{op:'bindPipe',pipe},{op:'descSet',setIndex:0,bindings:[{binding:0,kind:2,obj:'v',dstArrayElement:0,count:1}]},{op:'draw',vertexCount:3,instanceCount:1,firstVertex:0,firstInstance:0}]}]};
  assert.equal((await r.render(frame)).rendered,true);
  const upload=writes.find(w=>w.d.texture.dimension==='3d');assert.deepEqual(upload.size,[2,2,2]);assert.deepEqual(upload.data,pixels);
  frame.images.clear();frame.views.clear();assert.equal((await r.render({...frame,frameNo:2})).rendered,true);
  assert.ok(textures.some(t=>t.dimension==='3d' && t.size[0]===1));r.destroy();
 }finally{restore();}
});

test('image depth metadata is optional for existing chunks and validated for new volume records',()=>{
 const words=(...values)=>{const a=new Uint8Array(values.length*4),d=new DataView(a.buffer);values.forEach((v,i)=>d.setUint32(i*4,v,true));return a;};
 const image=words(1,0,2,0,32,32,37,3,0,0);
 const frame=tail=>decodeV2Frame([{type:V2_REC.FRAME_BEGIN,payload:words(1,32,32,0)},{type:V2_REC.IMAGE_DATA,payload:Uint8Array.from([...image,...tail])},{type:V2_REC.FRAME_END,payload:words(1)}]);
 assert.equal(frame([]).images.get('0000000000000002').d,1);
 const volume=frame(words(32,1,1)).images.get('0000000000000002');assert.equal(volume.d,32);assert.equal(volume.layers,1);assert.equal(volume.mips,1);
 assert.throws(()=>frame(words(0,1,1)),/depth\/layers\/mips/);
});
