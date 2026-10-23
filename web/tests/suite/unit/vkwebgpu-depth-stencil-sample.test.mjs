// Gang Beasts sampled a depth/stencil render target in a later pass and Dawn
// rejected the bind group: "Multiple aspects (Depth|Stencil) selected in
// TextureView". A sampled view of a combined depth/stencil texture must pick
// one aspect.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

test('sampling a depth24plus-stencil8 target binds a depth-only view', async () => {
  const { canvas, device, restore } = createValidationDevice();
  const bound = [];
  const createBindGroup = device.createBindGroup.bind(device);
  device.createBindGroup = d => { bound.push(...d.entries.map(e => e.resource)); return createBindGroup(d); };
  try {
    const r = await createVkRenderer2(canvas, { translate: async (from, bytes, stage) => stage === 'fragment' ? '@group(0) @binding(0) var image: texture_depth_2d; fn main() {}' : 'fn main() {}' });
    const pipe = { pipeId: 'sd', topology: 3, vb: [], va: [], vsHash: 'vs', fsHash: 'fs' };
    const color = { imageId: null, isDepth: false, samples: 1, loadOp: 1, storeOp: 0 };
    const depth = { imageId: '00000000000000d1', isDepth: true, samples: 1, format: 129, loadOp: 1, storeOp: 0 };
    const frame = { frameNo: 1, width: 4, height: 4, shaders: new Map([['vs', { code: new Uint8Array(4) }], ['fs', { code: new Uint8Array(4) }]]),
      images: new Map([['00000000000000d1', { w: 4, h: 4, mips: 1, format: 129, hash: 'depth', pixels: null }]]), views: new Map([['dv', '00000000000000d1']]),
      samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
      passes: [
        { w: 4, h: 4, clearColor: [0, 0, 0, 1], clearDepth: 1, attachments: [color, depth], ops: [] },
        { w: 4, h: 4, clearColor: [0, 0, 0, 1], clearDepth: 1, attachments: [color],
          ops: [{ op: 'bindPipe', pipe }, { op: 'descSet', setIndex: 0, bindings: [{ binding: 0, kind: 2, obj: 'dv', dstArrayElement: 0, count: 1 }] },
            { op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0 }] },
      ] };
    assert.equal((await r.render(frame)).rendered, true);
    const stencilViews = bound.filter(v => v.texture?.format?.includes('stencil'));
    assert.ok(stencilViews.length > 0, 'a depth/stencil texture was bound');
    for (const v of stencilViews) { assert.equal(v.options.aspect, 'depth-only'); assert.equal(v.options.format, undefined, 'a single-aspect view must not override the format'); }
    r.destroy();
  } finally { restore(); }
});
