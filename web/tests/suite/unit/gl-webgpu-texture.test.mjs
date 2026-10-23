import { test } from 'node:test';
import assert from 'node:assert/strict';
import { WebGPUGL } from '../../../gl-webgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

test('legacy alpha, luminance, luminance-alpha, red, and RG textures expand channels correctly', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    const cases = [
      [0x1906, new Uint8Array([64]), [255, 255, 255, 64]], // ALPHA
      [0x1909, new Uint8Array([64]), [64, 64, 64, 255]], // LUMINANCE
      [0x190a, new Uint8Array([64, 128]), [64, 64, 64, 128]], // LUMINANCE_ALPHA
      [0x1903, new Uint8Array([64]), [64, 0, 0, 255]], // RED
      [0x8227, new Uint8Array([64, 128]), [64, 128, 0, 255]], // RG
    ];
    for (const [format, data, expected] of cases) {
      const texture = gl.createTexture();
      gl.bindTexture(0x0de1, texture);
      gl.texImage2D(0x0de1, 0, format, 1, 1, 0, format, 0x1401, data);
      assert.deepEqual([...texture.pixels], expected, `format 0x${format.toString(16)}`);
    }
  } finally { gpu.restore(); }
});

test('texSubImage2D applies the same legacy single-channel expansion as texImage2D', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texImage2D(0x0de1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, new Uint8Array([0, 0, 0, 0]));
    gl.texSubImage2D(0x0de1, 0, 0, 0, 1, 1, 0x1909, 0x1401, new Uint8Array([96]));
    assert.deepEqual([...texture.pixels], [96, 96, 96, 255]);
  } finally { gpu.restore(); }
});
