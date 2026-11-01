import test from 'node:test';
import assert from 'node:assert/strict';
import { WebGPUGL } from '../../../gl-webgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

// FD-15(a): TEXTURE_MIN_LOD / TEXTURE_MAX_LOD and TEXTURE_COMPARE_MODE /
// TEXTURE_COMPARE_FUNC are forwarded by the C++ bridge as supported
// pnames, but the WebGPU GL backend used to reject them with
// INVALID_ENUM and never wired them into the sampler. Now they are
// accepted, stored, and honored by _samplerFor. Fails on the old code
// (INVALID_ENUM on set; lodMinClamp hardcoded 0; no compare).

test('FD-15: LOD clamp and compare tex parameters are accepted and reach the sampler', async () => {
  const gpu = createValidationDevice();
  try {
    const logs = [];
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    const depth = gl.createTexture();
    gl.bindTexture(0x0de1, depth);
    gl.texStorage2D(0x0de1, 1, 0x8cac, 8, 8); // DEPTH_COMPONENT32F
    await gl._ops;
    assert.equal(gl.getError(), 0);
    assert.equal(depth.gpu.format, 'depth32float');

    gl.texParameteri(0x0de1, 0x813a, 2.5);  // TEXTURE_MIN_LOD (via int entry: value passes through)
    gl.texParameterf(0x0de1, 0x813b, 4.0);  // TEXTURE_MAX_LOD
    gl.texParameteri(0x0de1, 0x884c, 0x884e); // TEXTURE_COMPARE_MODE = COMPARE_REF_TO_TEXTURE
    gl.texParameteri(0x0de1, 0x884d, 0x0201); // TEXTURE_COMPARE_FUNC = LESS
    assert.equal(gl.getError(), 0, 'honored pnames must not raise INVALID_ENUM');
    assert.equal(depth.params.get(0x813a), 2.5);
    assert.equal(depth.params.get(0x813b), 4.0);
    assert.equal(depth.params.get(0x884c), 0x884e);
    assert.equal(depth.params.get(0x884d), 0x0201);

    const sampler = gl._samplerFor(depth);
    assert.equal(sampler.lodMinClamp, 2.5, 'MIN_LOD reaches the sampler');
    assert.equal(sampler.lodMaxClamp, 4, 'MAX_LOD reaches the sampler');
    assert.equal(sampler.compare, 'less', 'compare func reaches the sampler for depth textures');

    // Invalid values still fail honestly.
    gl.texParameteri(0x0de1, 0x884d, 0x9999);
    assert.equal(gl.getError(), 0x0500, 'bad COMPARE_FUNC is INVALID_ENUM');
    gl.texParameterf(0x0de1, 0x813a, -1);
    assert.equal(gl.getError(), 0x0501, 'negative MIN_LOD is INVALID_VALUE');

    // A color texture must NOT get a compare sampler (WebGPU would
    // reject it); the params are stored but only depth samplers compare.
    const color = gl.createTexture();
    gl.bindTexture(0x0de1, color);
    gl.texStorage2D(0x0de1, 1, 0x8058, 8, 8); // RGBA8
    gl.texParameteri(0x0de1, 0x884c, 0x884e);
    gl.texParameteri(0x0de1, 0x884d, 0x0203);
    assert.equal(gl.getError(), 0);
    const colorSampler = gl._samplerFor(color);
    assert.equal(colorSampler.compare, undefined);
    assert.deepEqual(logs, []);
  } finally {
    gpu.restore();
  }
});
