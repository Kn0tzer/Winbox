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

test('texture framebuffer render attachments select exactly one mip level', async () => {
  const gpu = createValidationDevice();
  try {
    const createEncoder = gpu.device.createCommandEncoder;
    gpu.device.createCommandEncoder = () => {
      const encoder = createEncoder();
      const beginPass = encoder.beginRenderPass;
      encoder.beginRenderPass = descriptor => {
        for (const attachment of descriptor.colorAttachments) {
          assert.equal(attachment.view.options.mipLevelCount, 1, 'render attachments cannot include a texture mip chain');
        }
        return beginPass(descriptor);
      };
      return encoder;
    };
    const logs = [];
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texImage2D(0x0de1, 0, 0x1908, 80, 60, 0, 0x1908, 0x1401, null);
    gl.bindFramebuffer(0x8d40, gl.createFramebuffer());
    gl.framebufferTexture2D(0x8d40, 0x8ce0, 0x0de1, texture, 0);
    gl.clearColor(1, 0, 0, 1);
    gl.clear(0x4000);
    await gl._ops;
    assert.deepEqual(logs, []);
    assert.equal(gpu.passes.length, 1);
    const pixel = new Uint8Array(4);
    gl.readPixels(40, 30, 1, 1, 0x1908, 0x1401, pixel);
    assert.deepEqual([...pixel], [255, 0, 0, 255]);
  } finally { gpu.restore(); }
});

test('RGBA framebuffer blits convert into a BGRA default target without an incompatible copy', async () => {
  const gpu = createValidationDevice();
  try {
    navigator.gpu.getPreferredCanvasFormat = () => 'bgra8unorm';
    gpu.device.queue.writeBuffer = () => {};
    gpu.device.createBindGroup = descriptor => descriptor;
    gpu.device.createShaderModule = descriptor => {
      naga.translate({ from: 'wgsl', to: 'wgsl', source: descriptor.code });
      return descriptor;
    };
    const createEncoder = gpu.device.createCommandEncoder;
    gpu.device.createCommandEncoder = () => {
      const encoder = createEncoder();
      const copy = encoder.copyTextureToTexture;
      encoder.copyTextureToTexture = (source, destination, size) => {
        assert.equal(source.texture.format, destination.texture.format, 'copy formats must be compatible');
        return copy(source, destination, size);
      };
      return encoder;
    };
    const logs = [];
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texImage2D(0x0de1, 0, 0x1908, 2, 2, 0, 0x1908, 0x1401, null);
    const framebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(0x8d40, framebuffer);
    gl.framebufferTexture2D(0x8d40, 0x8ce0, 0x0de1, texture, 0);
    gl.clearColor(1, 0, 0, 1); gl.clear(0x4000);
    await gl._ops;
    gl.bindFramebuffer(0x8ca9, null);
    gl.blitFramebuffer(0, 0, 2, 2, 1, 1, 3, 3, 0x4000, 0x2600);
    await gl._ops;
    assert.deepEqual(logs, []);
    assert.equal(gpu.draws.length, 1, 'format conversion must execute on the GPU');
    assert.equal(gpu.pipelines[0].fragment.targets[0].format, 'bgra8unorm');
  } finally { gpu.restore(); }
});
