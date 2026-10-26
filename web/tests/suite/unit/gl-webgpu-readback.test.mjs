import { test } from 'node:test';
import assert from 'node:assert/strict';
import { WebGPUGL } from '../../../gl-webgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

test('resizing targets preserves the overlapping CPU shadow using the old row stride', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    gl._pixels.set([1, 2, 3, 4], (1 * 4 + 2) * 4);
    gl._ensureTargets(6, 5);
    assert.deepEqual([...gl._pixels.slice((1 * 6 + 2) * 4, (1 * 6 + 2) * 4 + 4)], [1, 2, 3, 4]);
    assert.deepEqual([...gl._pixels.slice(4, 8)], [0, 0, 0, 0], 'the first row remains at the beginning');
  } finally { gpu.restore(); }
});

test('readPixels converts RGB, BGRA, and normalized float output', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    gl._pixels.set([10, 20, 30, 40], 0);
    const rgb = new Uint8Array(4);
    gl.readPixels(0, 0, 1, 1, 0x1907, 0x1401, rgb);
    assert.deepEqual([...rgb], [10, 20, 30, 0], 'RGB output observes PACK_ALIGNMENT padding');
    const bgra = new Uint8Array(4);
    gl.readPixels(0, 0, 1, 1, 0x80e1, 0x1401, bgra);
    assert.deepEqual([...bgra], [30, 20, 10, 40]);
    const rgbaFloat = new Float32Array(4);
    gl.readPixels(0, 0, 1, 1, 0x1908, 0x1406, rgbaFloat);
    assert.deepEqual(rgbaFloat, new Float32Array([10 / 255, 20 / 255, 30 / 255, 40 / 255]));
  } finally { gpu.restore(); }
});

test('unsupported depth and pixel-pack-buffer reads log once and latch GL errors', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.readPixels(0, 0, 1, 1, 0x1902, 0x1403, new Uint16Array(1));
    assert.equal(gl.getError(), 0x0502, 'depth readback reports INVALID_OPERATION');
    gl.readPixels(0, 0, 1, 1, 0x1908, 0x1401, 0);
    assert.equal(gl.getError(), 0x0502, 'PBO offsets report INVALID_OPERATION');
    gl.readPixels(0, 0, 1, 1, 0x1908, 0x1401, 0);
    assert.equal(logs.filter(line => line.includes('pixel pack buffer destination')).length, 1, 'unsupported warnings are rate-limited by feature');
  } finally { gpu.restore(); }
});

test('pixel-pack buffers accept readPixels offsets and enforce buffer bounds', async () => {
  const gpu = createValidationDevice();
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: () => {} });
    await gl.ready;
    gl._pixels.set([11, 22, 33, 44], 0);
    const pbo = gl.createBuffer();
    gl.bindBuffer(0x88eb, pbo);
    gl.bufferData(0x88eb, 8);
    assert.equal(gl.getParameter(0x88ed), pbo);
    gl.readPixels(0, 0, 1, 1, 0x1908, 0x1401, 2);
    assert.deepEqual([...pbo.data], [0, 0, 11, 22, 33, 44, 0, 0]);
    assert.equal(gl.getError(), 0);
    gl.readPixels(0, 0, 1, 1, 0x1908, 0x1401, 6);
    assert.equal(gl.getError(), 0x0502, 'a pack offset beyond the buffer raises INVALID_OPERATION');
  } finally { gpu.restore(); }
});

test('GPU-written FBO readback fails loudly until asynchronous FBO staging exists', async () => {
  const gpu = createValidationDevice(), logs = [];
  try {
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    const texture = gl.createTexture();
    gl.bindTexture(0x0de1, texture);
    gl.texImage2D(0x0de1, 0, 0x1908, 2, 2, 0, 0x1908, 0x1401, new Uint8Array(16));
    const framebuffer = gl.createFramebuffer();
    gl.bindFramebuffer(0x8d40, framebuffer);
    gl.framebufferTexture2D(0x8d40, 0x8ce0, 0x0de1, texture, 0);
    gl._gpuWrittenFramebuffers.add(framebuffer);
    gl.readBuffer(0x8ce0);
    const pixel = new Uint8Array(4);
    gl.readPixels(0, 0, 1, 1, 0x1908, 0x1401, pixel);
    assert.equal(gl.getError(), 0x0502);
    assert.ok(logs.some(line => line.includes('GPU-backed framebuffer readPixels')));
  } finally { gpu.restore(); }
});

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
    assert.equal(copies.length, 0, 'an unchanged frame skips GPU readback');
    gl.clearColor(1, 0, 0, 1);
    gl.clear(0x4000);
    gl.flush();
    await gl._ops;
    await new Promise(resolve => setTimeout(resolve, 0));
    assert.equal(copies.length, 1, 'a present copies the colour target into a staging buffer');
    assert.ok(copies[0].destination.bytesPerRow % 256 === 0, 'WebGPU requires 256-byte aligned rows');
    gl.readPixels(0, 0, 1, 1, 0x1908, 0x1401, pixel);
    assert.deepEqual([...pixel], [255, 0, 0, 255], 'GL row 0 is the bottom of the image, BGRA becomes RGBA');
    gl.readPixels(0, gl._height - 1, 1, 1, 0x1908, 0x1401, pixel);
    assert.deepEqual([...pixel], [30, 20, 10, 255], 'the top row reads back last in GL order');
    for (let frame = 0; frame < 3; frame++) {
      gl.clear(0x4000);
      gl.flush();
      await gl._ops;
      await new Promise(resolve => setTimeout(resolve, 0));
    }
    assert.equal(staging.length, 2, 'readback staging reuses two buffers instead of allocating per frame');
  } finally { gpu.restore(); }
});
