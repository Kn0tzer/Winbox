import { test } from 'node:test';
import assert from 'node:assert/strict';
import * as naga from '../../../vendor/naga/node.js';
import { WebGPUGL } from '../../../gl-webgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

for (const [name, mask] of [['color', 0x4000], ['depth', 0x0100], ['color and depth', 0x4100]]) {
  test(`scissored ${name} clear generates valid WGSL and reaches the GPU`, async () => {
    const gpu = createValidationDevice();
    try {
      gpu.device.createShaderModule = descriptor => {
        naga.translate({ from: 'wgsl', to: 'wgsl', source: descriptor.code });
        return descriptor;
      };
      const logs = [];
      const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
      await gl.ready;
      gl.enable(0x0c11);
      gl.scissor(1, 1, 2, 2);
      gl.clearColor(0.2, 0.4, 0.6, 1);
      gl.clearDepth(0.5);
      gl.clear(mask);
      await gl._ops;
      assert.deepEqual(logs, [], 'a clear must not fail validation or switch the GL backend');
      assert.equal(gpu.draws.length, 1);
      assert.equal(gpu.pipelines[0].depthStencil.depthWriteEnabled, Boolean(mask & 0x0100));
      assert.deepEqual(gpu.scissors[0], [1, 1, 2, 2]);
    } finally {
      gpu.restore();
    }
  });
}
