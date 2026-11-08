import test from 'node:test';
import assert from 'node:assert/strict';
import { WebGPUGL } from '../../../gl-webgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

// FD-39: the desktop-only texture settings the WebGPU backend used to
// reject with INVALID_ENUM (LOD bias, GENERATE_MIPMAP, depth-texture
// mode, texture swizzle) are now validated and stored, GENERATE_MIPMAP
// actually regenerates the mip chain when the base level is redefined,
// and CLAMP_TO_BORDER is accepted and stored (the sampler decodes
// it to clamp-to-edge: WebGPU has no border addressing). A valid
// set must leave the error slot clean: wined3d polls it while creating
// its device, which is why these could not simply be refused. Every
// behavioral check below fails on the pre-FD-39 backend.

test('FD-39: WebGPU backend stores the desktop texparam set and honors GENERATE_MIPMAP', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    const tex = gl.createTexture();
    gl.bindTexture(0x0de1, tex); // TEXTURE_2D
    const px = new Uint8Array(4 * 4 * 4);
    for (let i = 0; i < px.length; i++) px[i] = (i * 7) % 256;
    gl.texImage2D(0x0de1, 0, 0x8058, 4, 4, 0, 0x1908, 0x1401, px); // RGBA8 <- RGBA/UNSIGNED_BYTE
    assert.equal(gl.getError(), 0);
    assert.equal(tex.levels.has(1), false, 'no mip chain before GENERATE_MIPMAP is set');

    // GENERATE_MIPMAP: accepted, stored, and honored on redefinition.
    gl.texParameteri(0x0de1, 0x8c55, 1);
    assert.equal(gl.getError(), 0, 'GENERATE_MIPMAP set must not raise');
    assert.equal(tex.params.get(0x8c55), 1);
    gl.texImage2D(0x0de1, 0, 0x8058, 4, 4, 0, 0x1908, 0x1401, px);
    assert.equal(gl.getError(), 0, 'regenerating upload must not raise');
    assert.equal(tex.levels.has(1), true, 'mip level 1 generated');
    assert.equal(tex.levels.has(2), true, 'mip level 2 generated');
    assert.equal(tex.levels.get(1).width, 2);
    assert.equal(tex.levels.get(2).width, 1);

    // The rest of the set: accepted with a clean error slot, stored.
    gl.texParameterf(0x0de1, 0x8501, 1.5); // TEXTURE_LOD_BIAS
    assert.equal(gl.getError(), 0, 'LOD_BIAS set must not raise');
    assert.equal(tex.params.get(0x8501), 1.5);
    gl.texParameteri(0x0de1, 0x884b, 0x1903); // DEPTH_TEXTURE_MODE = RED
    assert.equal(gl.getError(), 0, 'DEPTH_TEXTURE_MODE set must not raise');
    assert.equal(tex.params.get(0x884b), 0x1903);
    gl.texParameteri(0x0de1, 0x8e42, 0x0001); // SWIZZLE_R = ONE
    gl.texParameteri(0x0de1, 0x8e45, 0x1906); // SWIZZLE_A = ALPHA
    assert.equal(gl.getError(), 0, 'swizzle sets must not raise');
    assert.equal(tex.params.get(0x8e42), 0x0001);
    assert.equal(tex.params.get(0x8e45), 0x1906);

    // Invalid values still fail honestly, exactly as a driver would.
    gl.texParameteri(0x0de1, 0x884b, 0x9999);
    assert.equal(gl.getError(), 0x0500, 'bad DEPTH_TEXTURE_MODE is INVALID_ENUM');
    gl.texParameteri(0x0de1, 0x8e42, 0x9999);
    assert.equal(gl.getError(), 0x0500, 'bad swizzle component is INVALID_ENUM');
    gl.texParameterf(0x0de1, 0x8501, NaN);
    assert.equal(gl.getError(), 0x0501, 'NaN LOD_BIAS is INVALID_VALUE');

    // CLAMP_TO_BORDER is accepted and stored; WebGPU has no border
    // addressing (GPUAddressMode is clamp-to-edge / repeat /
    // mirror-repeat only), so the sampler decodes it to clamp-to-edge
    // and the raw mode stays in the params map. The FD-39 lane asserted
    // 'clamp-to-border' here -- a value a real GPUDevice rejects; the
    // recording device this test drives cannot see the difference.
    gl.texParameteri(0x0de1, 0x2802, 0x812d); // WRAP_S = CLAMP_TO_BORDER
    assert.equal(gl.getError(), 0, 'CLAMP_TO_BORDER set must not raise');
    assert.equal(tex.params.get(0x2802), 0x812d, 'raw wrap mode retained in the params map');
    const sampler = gl._samplerFor(tex);
    assert.equal(sampler.addressModeU, 'clamp-to-edge');
  } finally {
    gpu.restore();
  }
});
