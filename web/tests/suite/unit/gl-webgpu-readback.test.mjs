import { test } from 'node:test';
import assert from 'node:assert/strict';
import { WebGPUGL } from '../../../gl-webgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

// Programmable GL draws (FNA, Mindustry) only reach the X11 present through a
// GPU readback, so glReadPixels on the default framebuffer must serve the
// frame copied back at the previous present, not the CPU clear shadow.
test('default framebuffer glReadPixels serves the GPU readback after a present', async () => {
  const gpu = createValidationDevice();
  try {
    navigator.gpu.getPreferredCanvasFormat = () => 'bgra8unorm';
    const staging = [];
    gpu.device.createBuffer = descriptor => {
      const bytes = new ArrayBuffer(descriptor.size);
      const buffer = { ...descriptor, getMappedRange: () => bytes, async mapAsync() {}, unmap() {}, destroy() {} };
      staging.push(buffer);
      return buffer;
    };
    const copies = [];
    const createEncoder = gpu.device.createCommandEncoder;
    gpu.device.createCommandEncoder = () => {
      const encoder = createEncoder();
      encoder.copyTextureToBuffer = (source, destination, extent) => {
        copies.push({ destination, extent });
        // Row 0 (top) is blue-ish BGRA 10,20,30,255; the last row is red.
        const bytes = new Uint8Array(destination.buffer.getMappedRange());
        const row = destination.bytesPerRow;
        for (let x = 0; x < extent[0]; x++) bytes.set([10, 20, 30, 255], x * 4);
        for (let x = 0; x < extent[0]; x++) bytes.set([0, 0, 255, 255], (extent[1] - 1) * row + x * 4);
      };
      return encoder;
    };
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    const pixel = new Uint8Array(4);
    gl.readPixels(0, 0, 1, 1, 0x1908, 0x1401, pixel);
    assert.deepEqual([...pixel], [0, 0, 0, 0], 'before any present the CPU shadow is served');
    gl.flush();
    await gl._ops;
    await new Promise(resolve => setTimeout(resolve, 0));
    assert.equal(copies.length, 1, 'a present copies the colour target into a staging buffer');
    assert.ok(copies[0].destination.bytesPerRow % 256 === 0, 'WebGPU requires 256-byte aligned rows');
    gl.readPixels(0, 0, 1, 1, 0x1908, 0x1401, pixel);
    assert.deepEqual([...pixel], [255, 0, 0, 255], 'GL row 0 is the bottom of the image, BGRA becomes RGBA');
    gl.readPixels(0, gl._height - 1, 1, 1, 0x1908, 0x1401, pixel);
    assert.deepEqual([...pixel], [30, 20, 10, 255], 'the top row reads back last in GL order');
  } finally { gpu.restore(); }
});
