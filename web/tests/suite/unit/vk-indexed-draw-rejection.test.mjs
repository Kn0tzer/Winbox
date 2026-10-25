import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

test('indexed Vulkan draws never degrade into non-indexed draws when index data is absent', async()=>{
  const gpu=createValidationDevice();
  const warnings=[], oldWarn=console.warn;
  console.warn=(...args)=>warnings.push(args.join(' '));
  try {
    const renderer=await createVkRenderer2(gpu.canvas,{translate:async()=> 'fn main() {}'});
    for (const {topology, bind} of [{topology:3,bind:false},{topology:3,bind:true},{topology:5,bind:false}]) {
      const pipe={pipeId:'indexed'+topology+bind,topology,vb:[],va:[],vsHash:'vs',fsHash:'fs'};
      const ops=[{op:'bindPipe',pipe}];
      if(bind) ops.push({op:'ibind',buf:'missing-index',offset:0,indexType:0});
      ops.push({op:'draw',indexed:true,indexCount:3,vertexCount:3,instanceCount:1,firstIndex:0,vertexOffset:0,firstVertex:0,firstInstance:0});
      const result=await renderer.render({frameNo:1,width:4,height:4,
        shaders:new Map([['vs',{code:new Uint8Array(4)}],['fs',{code:new Uint8Array(4)}]]),
        images:new Map(),views:new Map(),samplers:new Map(),buffers:new Map(),bufHashes:new Map(),readbacks:[],
        passes:[{w:4,h:4,clearColor:[0,0,0,1],clearDepth:1,attachments:[{format:37,isDepth:0,samples:1,loadOp:1,storeOp:0}],ops}]});
      assert.equal(result.rendered,true);
    }
    assert.equal(gpu.indexedDraws.length,0);
    assert.equal(gpu.draws.length,0);
    assert.ok(warnings.some(w=>w.includes('draw-indexed-without-index-buffer')));
    assert.ok(warnings.some(w=>w.includes('draw-index-buffer-missing')));
    assert.ok(warnings.some(w=>w.includes('draw-fan-indexed-without-index-buffer')));
    renderer.destroy();
  } finally {console.warn=oldWarn;gpu.restore();}
  const source=readFileSync(new URL('../../../vkwebgpu.mjs',import.meta.url),'utf8');
  assert.match(source,/draw-index-buffer-missing/);
  assert.match(source,/draw-indexed-without-index-buffer/);
  assert.doesNotMatch(source,/draw-index-buffer-missing-as-nonindexed|draw-indexed-without-index-buffer-as-nonindexed/);
});
