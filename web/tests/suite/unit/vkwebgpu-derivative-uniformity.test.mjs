// Gang Beasts (Unity/DXVK) pipelines were rejected with "textureSample must
// only be called from uniform control flow": Vulkan permits implicit-derivative
// sampling in non-uniform flow, WGSL's analysis does not. Fragment modules are
// created with the derivative_uniformity diagnostic switched off.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

test('fragment shader modules disable the derivative_uniformity diagnostic', async () => {
  const { canvas, pipelines, restore } = createValidationDevice();
  try {
    const r = await createVkRenderer2(canvas, { translate: async () => '@fragment fn main() {}' });
    const pipe = { pipeId: 'pipe', topology: 3, vb: [], va: [], vsHash: 'vs', fsHash: 'fs' };
    const frame = { frameNo: 1, width: 4, height: 4, shaders: new Map([['vs', { code: new Uint8Array(4) }], ['fs', { code: new Uint8Array(4) }]]),
      images: new Map(), views: new Map(), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
      passes: [{ w: 4, h: 4, clearColor: [0, 0, 0, 1], clearDepth: 1, attachments: [{ imageId: null, isDepth: false, samples: 1, loadOp: 1, storeOp: 0 }],
        ops: [{ op: 'bindPipe', pipe }, { op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0 }] }] };
    assert.equal((await r.render(frame)).rendered, true);
    assert.ok(pipelines.length > 0);
    const fs = pipelines[0].fragment.module.code;
    assert.match(fs, /^diagnostic\(off, derivative_uniformity\);/);
    assert.ok(!pipelines[0].vertex.module.code.includes('derivative_uniformity'));
    r.destroy();
  } finally { restore(); }
});
