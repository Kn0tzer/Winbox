import { test } from 'node:test';
import assert from 'node:assert/strict';
import { WebGPUGL } from '../../../gl-webgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

test('program-zero fixed-function indexed draws build a textured alpha-test pipeline', async () => {
  const gpu = createValidationDevice();
  const logs = [];
  try {
    gpu.device.queue.writeBuffer = () => {};
    gpu.device.createBindGroup = descriptor => descriptor;
    const createPipeline = gpu.device.createRenderPipeline;
    gpu.device.createRenderPipeline = descriptor => {
      descriptor.getBindGroupLayout = () => ({});
      return createPipeline(descriptor);
    };
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.matrixMode(0x1701);
    gl.loadIdentity();
    gl.ortho(-1, 1, -1, 1, -1, 1);
    gl.matrixMode(0x1700);
    gl.translatef(0.25, 0, 0);
    gl.enable(0x0de1);
    gl.enable(0x0bc0);
    gl.alphaFunc(0x0204, 0.5);
    gl.texEnvi(0x2300, 0x2200, 0x1e01);

    const arrays = {
      vertex: { enabled: true, configured: true, size: 3, type: 0x1406, stride: 0 },
      color: { enabled: true, configured: true, size: 4, type: 0x1401, stride: 4 },
      texCoord: { enabled: true, configured: true, size: 2, type: 0x1406, stride: 0 },
      vertexData: new Float32Array([-0.5, -0.5, 0, 0.5, -0.5, 0, 0, 0.5, 0]),
      colorData: new Uint8Array([255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 128]),
      texCoordData: new Float32Array([0, 0, 1, 0, 0.5, 1]),
    };
    assert.equal(gl.fixedFunctionDrawElements(0x0004, 3, 0x1401, 0, new Uint8Array([0, 1, 2]), arrays), true);
    await gl._ops;
    assert.deepEqual(logs, []);

    assert.equal(gpu.indexedDraws.length, 1);
    assert.equal(gpu.pipelines.length, 1);
    assert.ok(gl._pixels.some(value => value > 0), 'fixed-function transform must populate synchronous mirror pixels');
    const fragment = gpu.pipelines[0].fragment.module.code;
    assert.match(fragment, /textureSample/);
    assert.match(fragment, /discard/);
    assert.deepEqual([...gl._fixedFunctionProgramCache.values()][0].uniformValues.get('u_mvp').slice(12, 15), new Float32Array([0.25, 0, 0]));
  } finally {
    gpu.restore();
  }
});

test('program-zero fixed-function array draws use the current constant color', async () => {
  const gpu = createValidationDevice();
  const logs = [];
  try {
    gpu.device.queue.writeBuffer = () => {};
    gpu.device.createBindGroup = descriptor => descriptor;
    const createPipeline = gpu.device.createRenderPipeline;
    gpu.device.createRenderPipeline = descriptor => {
      descriptor.getBindGroupLayout = () => ({});
      return createPipeline(descriptor);
    };
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.color4f(0.2, 0.4, 0.6, 0.8);
    const arrays = {
      vertex: { enabled: true, configured: true, size: 2, type: 0x1406, stride: 0 },
      color: { enabled: false }, texCoord: { enabled: false },
      vertexData: new Float32Array([-1, -1, 1, -1, 0, 1]),
    };
    assert.equal(gl.fixedFunctionDrawArrays(0x0004, 0, 3, arrays), true);
    await gl._ops;
    assert.deepEqual(logs, []);
    assert.equal(gpu.draws.length, 1);
    assert.ok(gl._pixels.some(value => value > 0), 'constant color must populate synchronous mirror pixels');
    assert.equal([...gl._fixedFunctionProgramCache.values()][0].attributeInfo[1].name, 'a_color');
  } finally {
    gpu.restore();
  }
});

test('program-zero fixed-function draws advance buffer-backed pointers by the staged base vertex', async () => {
  const gpu = createValidationDevice();
  try {
    gpu.device.queue.writeBuffer = () => {};
    gpu.device.createBindGroup = descriptor => descriptor;
    const createPipeline = gpu.device.createRenderPipeline;
    gpu.device.createRenderPipeline = descriptor => {
      descriptor.getBindGroupLayout = () => ({ });
      return createPipeline(descriptor);
    };
    const offsets = [];
    const createEncoder = gpu.device.createCommandEncoder;
    gpu.device.createCommandEncoder = () => {
      const encoder = createEncoder();
      const beginPass = encoder.beginRenderPass;
      encoder.beginRenderPass = descriptor => {
        const pass = beginPass(descriptor);
        const setVertexBuffer = pass.setVertexBuffer;
        pass.setVertexBuffer = (slot, buffer, offset) => {
          offsets.push({ slot, offset });
          setVertexBuffer(slot, buffer, offset);
        };
        return pass;
      };
      return encoder;
    };
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    const buffer = gl.createBuffer();
    gl.bindBuffer(0x8892, buffer);
    gl.bufferData(0x8892, new Float32Array([0, 0, 0, 0, 1, 1, 1, 0, -1, 1, 1, 0]), 0x88e4);
    gl.fixedFunctionPointer(0, 3, 0x1406, 16, 0);
    const arrays = {
      vertex: { enabled: true, configured: true, size: 3, type: 0x1406, stride: 16 },
      color: { enabled: false }, texCoord: { enabled: false }, baseVertex: 2,
    };
    assert.equal(gl.fixedFunctionDrawArrays(0x0004, 0, 3, arrays), true);
    await gl._ops;
    assert.equal(offsets.find(binding => binding.slot === 0)?.offset, 32);
  } finally {
    gpu.restore();
  }
});

test('immediate-mode vertices retain their individual colors and use the fixed-function matrices', async () => {
  const gpu = createValidationDevice();
  try {
    gpu.device.queue.writeBuffer = () => {};
    gpu.device.createBindGroup = descriptor => descriptor;
    const createPipeline = gpu.device.createRenderPipeline;
    gpu.device.createRenderPipeline = descriptor => {
      descriptor.getBindGroupLayout = () => ({});
      return createPipeline(descriptor);
    };
    const logs = [];
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    gl.matrixMode(0x1701);
    gl.ortho(0, 4, 0, 4, -1, 1);
    // Commands are [kind, x/r, y/g, z/b, w/a]; 0=color, 1=vertex.
    gl.fixedFunctionImmediateDraw(4, new Float32Array([
      0, 1, 0, 0, 1, 1, 0, 0, 0, 1,
      0, 0, 1, 0, 1, 1, 4, 0, 0, 1,
      0, 0, 0, 1, 1, 1, 0, 4, 0, 1,
    ]));
    await gl._ops;
    assert.deepEqual(logs, []);
    assert.equal(gpu.draws.length, 1);
    assert.ok(gl._pixels.some((value, i) => i % 4 === 0 && value > 100), 'red must survive immediate-mode recording and projection');
    assert.deepEqual(gl._fixedFunction.color, [0, 0, 1, 1], 'the last immediate color remains current');
  } finally { gpu.restore(); }
});

test('queued fixed-function GPU draws retain the texture bound at each draw', async () => {
  const gpu = createValidationDevice();
  try {
    gpu.device.queue.writeBuffer = () => {};
    const sampled = [];
    gpu.device.createBindGroup = descriptor => {
      sampled.push(descriptor.entries.find(entry => entry.binding === 2)?.resource.texture);
      return descriptor;
    };
    const createPipeline = gpu.device.createRenderPipeline;
    gpu.device.createRenderPipeline = descriptor => {
      descriptor.getBindGroupLayout = () => ({});
      return createPipeline(descriptor);
    };
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    gl.enable(0x0de1);
    const arrays = {
      vertex: { enabled: true, size: 2, type: 0x1406, stride: 0 },
      color: { enabled: false }, texCoord: { enabled: false },
      vertexData: new Float32Array([-1, -1, 1, -1, 0, 1]),
    };
    const textures = [];
    for (const pixel of [[255, 0, 0, 255], [0, 255, 0, 255]]) {
      const texture = gl.createTexture(); textures.push(texture);
      gl.bindTexture(0x0de1, texture);
      gl.texImage2D(0x0de1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, new Uint8Array(pixel));
      gl.fixedFunctionDrawArrays(4, 0, 3, arrays);
    }
    await gl._ops;
    assert.equal(sampled.length, 2);
    assert.equal(sampled[0], textures[0].gpu, 'first GPU draw must sample the first texture');
    assert.equal(sampled[1], textures[1].gpu, 'second GPU draw must sample the second texture');
  } finally { gpu.restore(); }
});

test('odd uint16 index counts use aligned GPU uploads without drawing padding', async () => {
  const gpu = createValidationDevice();
  try {
    gpu.device.queue.writeBuffer = (buffer, offset, data) => {
      assert.equal(data.byteLength % 4, 0, 'WebGPU writeBuffer byte count must be aligned');
    };
    gpu.device.createBindGroup = descriptor => descriptor;
    const createPipeline = gpu.device.createRenderPipeline;
    gpu.device.createRenderPipeline = descriptor => {
      descriptor.getBindGroupLayout = () => ({});
      return createPipeline(descriptor);
    };
    const logs = [];
    const gl = new WebGPUGL(gpu.canvas, { log: message => logs.push(message) });
    await gl.ready;
    const arrays = { vertex: { enabled: true, size: 2, type: 0x1406, stride: 0 },
      color: { enabled: false }, texCoord: { enabled: false },
      vertexData: new Float32Array([-1, -1, 1, -1, 0, 1]) };
    gl.fixedFunctionDrawElements(4, 3, 0x1403, 0, new Uint16Array([0, 1, 2]), arrays);
    await gl._ops;
    assert.deepEqual(logs, []);
    assert.equal(gpu.indexedDraws.length, 1);
    assert.equal(gpu.indexedDraws[0][0], 3, 'padding must not add an index');
  } finally { gpu.restore(); }
});
