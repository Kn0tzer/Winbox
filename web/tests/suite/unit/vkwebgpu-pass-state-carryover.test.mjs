// Gang Beasts (Unity/DXVK) rejected every frame with "Attachment state of
// RenderPipeline is not compatible with RenderPassEncoder" and "Scissor rect
// ... is not contained in the render target": replay state carried from one
// render pass into the next. The pipeline *description* and scissor persist
// across passes (Vulkan allows that); the resolved WebGPU pipeline and the
// scissor extent must be re-derived for each pass.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

test('a clearAttachments in a later pass does not re-set the previous pass pipeline and clamps the scissor', async () => {
  const { canvas, scissors, restore } = createValidationDevice();
  try {
    const r = await createVkRenderer2(canvas, { translate: async () => 'fn main() {}' });
    const pipe = { pipeId: 'pipe', topology: 3, vb: [], va: [], vsHash: 'vs', fsHash: 'fs', depthTest: true, depthWrite: true, depthOp: 3 };
    const draw = { op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0 };
    const attachments = depth => [{ imageId: null, isDepth: false, samples: 1, loadOp: 1, storeOp: 0 }, ...(depth ? [{ imageId: null, isDepth: true, samples: 1 }] : [])];
    const clear = { op: 'clearAttachments', rects: [{ x: 0, y: 0, w: 4, h: 4, baseLayer: 0, layers: 1 }],
      attachments: [{ aspectMask: 1, colorAttachment: 0, colorBits: [0, 0, 0, 0], depth: 0, stencil: 0 }] };
    const frame = { frameNo: 1, width: 8, height: 8, shaders: new Map([['vs', { code: new Uint8Array(4) }], ['fs', { code: new Uint8Array(4) }]]),
      images: new Map(), views: new Map(), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
      passes: [
        { w: 8, h: 8, clearColor: [0, 0, 0, 1], clearDepth: 1, attachments: attachments(true),
          ops: [{ op: 'bindPipe', pipe }, { op: 'scissor', s: [0, 0, 8, 8] }, draw] },
        { w: 4, h: 4, clearColor: [0, 0, 0, 1], clearDepth: 1, attachments: attachments(false), ops: [clear] },
      ] };
    assert.equal((await r.render(frame)).rendered, true);
    assert.ok(scissors.length > 0);
    for (const [x, y, w, h] of scissors.slice(-2)) assert.ok(x + w <= 8 && y + h <= 8);
    assert.deepEqual(scissors.at(-1), [0, 0, 4, 4], 'scissor restored after the clear is clamped to the 4x4 pass');
    r.destroy();
  } finally { restore(); }
});
