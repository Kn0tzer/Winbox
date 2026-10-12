import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

test('replayed Vulkan pipelines match each pass depth attachment and sample count', async () => {
  const { canvas, pipelines, bound, restore } = createValidationDevice();
  try {
    const r = await createVkRenderer2(canvas, { translate: async () => 'fn main() {}' });
    const pipe = { pipeId: 'pipe', topology: 3, vb: [], va: [], vsHash: 'vs', fsHash: 'fs', depthTest: true, depthWrite: true, depthOp: 3 };
    const draw = { op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0 };
    const pass = (depth, samples, ops) => ({ w: 4, h: 4, clearColor: [0, 0, 0, 1], clearDepth: 1, ops,
      attachments: [{ imageId: null, isDepth: false, samples, loadOp: 1, storeOp: 0 }, ...(depth ? [{ imageId: null, isDepth: true, samples }] : [])] });
    const frame = { frameNo: 1, width: 4, height: 4, shaders: new Map([['vs', { code: new Uint8Array(4) }], ['fs', { code: new Uint8Array(4) }]]),
      images: new Map(), views: new Map(), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
      passes: [pass(true, 4, [{ op: 'bindPipe', pipe }, draw]), pass(false, 1, [draw]), pass(true, 1, [draw])] };
    assert.equal((await r.render(frame)).rendered, true);
    assert.equal(bound.length, 3);assert.deepEqual(pipelines.map(p => [!!p.depthStencil, p.multisample.count]), [[true, 4], [false, 1], [true, 1]]);
    r.destroy();
  } finally { restore(); }
});
