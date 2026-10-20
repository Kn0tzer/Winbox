import test from 'node:test';
import assert from 'node:assert/strict';
import { decodeV2Frame, V2_REC, v2IndexedTriangleFanIndices, v2PrimitiveState, v2TriangleFanIndices } from '../../../vkwebgpu.mjs';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

class Writer {
  constructor(){this.bytes=[];}
  u32(n){for(let i=0;i<4;i++)this.bytes.push((n>>>(8*i))&255);return this;}
  u64(n){this.u32(n);this.u32(0);return this;}
  f32(n){this.bytes.push(...new Uint8Array(new Float32Array([n]).buffer));return this;}
  buf(){return new Uint8Array(this.bytes);}
}
function pipelinePayload(restart=1){
  const w=new Writer().u64(1).u64(2).u64(3);
  [5,0,0,0,0,7,0,0,0].forEach(n=>w.u32(n));
  w.u32(0).u32(0).u32(0x31444e42).u32(0);[0,0,0,0].forEach(n=>w.f32(n));
  w.u32(0x314e4146).u32(restart);return w.buf();
}

test('fan topology is rendered as a triangle list with generated indices',()=>{
  assert.deepEqual([...v2TriangleFanIndices(5,3)],[3,4,5,3,5,6,3,6,7]);
  const indices=new Uint16Array([10,11,12,13,0xffff,20,21,22]);
  assert.deepEqual([...v2IndexedTriangleFanIndices(new Uint8Array(indices.buffer),0,0,8,true)],[10,11,12,10,12,13,20,21,22]);
  const index32=new Uint32Array([1,2,3,4,0xffffffff,8,9,10]);
  assert.deepEqual([...v2IndexedTriangleFanIndices(new Uint8Array(index32.buffer),1,0,8,true)],[1,2,3,1,3,4,8,9,10]);
  assert.equal(v2PrimitiveState({topology:5,cull:0,front:0}).topology,'triangle-list');
});

test('PIPELINE FAN1 extension round-trips primitive restart for older compatible records',()=>{
  const frame=decodeV2Frame([
    {type:V2_REC.FRAME_BEGIN,payload:new Uint8Array(new Uint32Array([1,4,4,0]).buffer)},
    {type:V2_REC.PIPELINE,payload:pipelinePayload(1)},
    {type:V2_REC.FRAME_END,payload:new Uint8Array(new Uint32Array([1]).buffer)},
  ]);
  const pipe=frame.pipelines.get('0000000000000001');
  assert.equal(pipe.topology,5);assert.equal(pipe.primitiveRestartEnable,true);
  const older=pipelinePayload().subarray(0,pipelinePayload().length-8);
  const previous=decodeV2Frame([{type:V2_REC.FRAME_BEGIN,payload:new Uint8Array(new Uint32Array([1,4,4,0]).buffer)},
    {type:V2_REC.PIPELINE,payload:older},{type:V2_REC.FRAME_END,payload:new Uint8Array(new Uint32Array([1]).buffer)}]);
  assert.equal(previous.pipelines.get('0000000000000001').primitiveRestartEnable,false);
});

test('indexed and non-indexed fan draws use cached triangle-list index buffers',async()=>{
  const gpu=createValidationDevice();
  try {
    const renderer=await createVkRenderer2(gpu.canvas,{translate:async()=> 'fn main() {}'});
    const indices=new Uint16Array([0xaaaa,0xbbbb,10,11,12,13,0xffff,20,21,22]);
    const pipe={pipeId:'fan',topology:5,primitiveRestartEnable:true,vb:[],va:[],vsHash:'vs',fsHash:'fs'};
    const frame={frameNo:1,width:4,height:4,shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map([['ib',new Uint8Array(indices.buffer)]]),bufHashes:new Map(),readbacks:[],passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,attachments:[{imageId:'color',format:37,isDepth:false,samples:1,loadOp:1,storeOp:0}],ops:[
      {op:'bindPipe',pipe},{op:'draw',indexed:false,vertexCount:5,instanceCount:1,firstVertex:3,firstInstance:0},
      {op:'ibind',buf:'ib',offset:4,indexType:0},
      {op:'draw',indexed:true,indexCount:8,instanceCount:1,firstIndex:0,vertexOffset:2,firstInstance:0},
      {op:'draw',indexed:true,indexCount:8,instanceCount:1,firstIndex:0,vertexOffset:2,firstInstance:0},
    ]}]};
    assert.equal((await renderer.render(frame)).rendered,true);
    assert.deepEqual(gpu.indexedDraws,[[9,1,0,0,0],[9,1,0,2,0],[9,1,0,2,0]]);
    const generated=gpu.indexBindings.map(x=>[...new Uint32Array(x.buffer.data)]);
    assert.deepEqual(generated[0],[3,4,5,3,5,6,3,6,7]);
    assert.deepEqual(generated[1],[10,11,12,10,12,13,20,21,22]);
    assert.equal(gpu.indexBindings[1].buffer,gpu.indexBindings[2].buffer,'the same index range reuses its generated GPU buffer');
    assert.equal(gpu.pipelines[0].primitive.topology,'triangle-list');
    renderer.destroy();
  } finally {gpu.restore();}
});
