import test from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

test('overlapping attributes and zero-stride vertex bindings retain the guest stride',async()=>{
 const gpu=createValidationDevice();const create=gpu.device.createCommandEncoder.bind(gpu.device);gpu.device.createCommandEncoder=()=>{const e=create(),begin=e.beginRenderPass.bind(e);e.beginRenderPass=d=>{const p=begin(d);p.setVertexBuffer=()=>{};return p;};return e;};
 try{const r=await createVkRenderer2(gpu.canvas,{translate:async()=> 'fn main() {}'});
 for(const stride of [32,0]){const pipe={pipeId:'stride'+stride,topology:3,vb:[{binding:0,stride,inputRate:0}],va:[{location:0,binding:0,format:106,offset:0},{location:1,binding:0,format:109,offset:32}],vsHash:'vs',fsHash:'fs'};
 await r.render({frameNo:1,width:4,height:4,shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,attachments:[{format:37,isDepth:0,samples:1,loadOp:1,storeOp:0}],ops:[{op:'bindPipe',pipe},{op:'draw',vertexCount:3,instanceCount:1,firstVertex:0,firstInstance:0}]}]});}
 assert.deepEqual(gpu.pipelines.map(p=>p.vertex.buffers.map(b=>b.arrayStride)),[[32,32],[0]]);assert.equal(gpu.pipelines[0].vertex.buffers[0].attributes[0].offset,0);r.destroy();}finally{gpu.restore();}
});
