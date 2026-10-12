import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';
test('indexed strips follow 16/32-bit index bindings even when they change after pipeline binding',async()=>{
 const {canvas,pipelines,restore}=createValidationDevice();try {
  const r=await createVkRenderer2(canvas,{translate:async()=> 'fn main() {}'});
  const pipe={pipeId:'strip',topology:4,vb:[],va:[],vsHash:'vs',fsHash:'fs'};
  const draw={op:'draw',indexed:true,indexCount:4,instanceCount:1,firstIndex:0,vertexOffset:0,firstInstance:0};
  const frame={frameNo:1,width:4,height:4,shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map([['indices',new Uint8Array(16)]]),bufHashes:new Map(),readbacks:[],passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,attachments:[{imageId:null,isDepth:false,samples:1,loadOp:1,storeOp:0}],ops:[{op:'bindPipe',pipe},{op:'ibind',buf:'indices',offset:0,indexType:0},draw,{op:'ibind',buf:'indices',offset:0,indexType:1},draw]}]};
  assert.equal((await r.render(frame)).rendered,true);assert.deepEqual(pipelines.map(p=>p.primitive.stripIndexFormat),['uint16','uint32']);r.destroy();
 }finally {restore();}
});
