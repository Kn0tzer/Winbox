import { test } from 'node:test';
import assert from 'node:assert/strict';
import { WebGPUGL } from '../../../gl-webgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';
import * as naga from '../../../vendor/naga/node.js';

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

test('program-zero fixed-function draws sample both enabled texture units with separate UV arrays', async () => {
  const gpu = createValidationDevice();
  try {
    gpu.device.queue.writeBuffer = () => {};
    const sampled = new Map();
    gpu.device.createBindGroup = descriptor => {
      for (const entry of descriptor.entries) {
        if (entry.binding === 2 || entry.binding === 4) sampled.set(entry.binding, entry.resource.texture);
      }
      return descriptor;
    };
    const createPipeline = gpu.device.createRenderPipeline;
    gpu.device.createRenderPipeline = descriptor => {
      descriptor.getBindGroupLayout = () => ({});
      return createPipeline(descriptor);
    };
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    const draws = [];
    const draw = gl._draw.bind(gl);
    gl._draw = async (...args) => { draws.push(args[0]); return draw(...args); };
    const textures = [];
    for (const pixel of [[255, 0, 0, 255], [128, 128, 255, 255]]) {
      gl.activeTexture(0x84c0 + textures.length);
      gl.enable(0x0de1);
      assert.equal(gl.isEnabled(0x0de1), true, 'texture enable state belongs to the active server unit');
      const texture = gl.createTexture();
      textures.push(texture);
      gl.bindTexture(0x0de1, texture);
      gl.texImage2D(0x0de1, 0, 0x1908, 1, 1, 0, 0x1908, 0x1401, new Uint8Array(pixel));
    }
    gl.activeTexture(0x84c1);
    gl.disable(0x0de1);
    assert.equal(gl.isEnabled(0x0de1), false);
    gl.activeTexture(0x84c0);
    assert.equal(gl.isEnabled(0x0de1), true, 'disabling unit 1 must preserve unit 0 state');
    gl.activeTexture(0x84c1);
    gl.enable(0x0de1);
    gl.clientActiveTexture(0x84c0);
    gl.multiTexCoord2f(0x84c1, 0.25, 0.75);
    gl.activeTexture(0x84c0);
    gl.texEnvi(0x2300, 0x2200, 0x1e01);
    gl.activeTexture(0x84c1);
    gl.texEnvi(0x2300, 0x2200, 0x2100);

    const arrays = {
      vertex: { enabled: true, configured: true, size: 2, type: 0x1406, stride: 0 },
      color: { enabled: false },
      texCoord: { enabled: true, configured: true, size: 2, type: 0x1406, stride: 0 },
      texCoord1: { enabled: true, configured: true, size: 2, type: 0x1406, stride: 0 },
      vertexData: new Float32Array([-1, -1, 1, -1, 0, 1]),
      texCoordData: new Float32Array([0, 0, 1, 0, 0.5, 1]),
      texCoordData1: new Float32Array([0.25, 0.75, 0.75, 0.75, 0.5, 0.25]),
    };
    assert.equal(gl.fixedFunctionDrawArrays(4, 0, 3, arrays), true);
    await gl._ops;

    const program = [...gl._fixedFunctionProgramCache.values()][0];
    assert.equal(draws.length, 1);
    assert.deepEqual([...new Float32Array(draws[0].attributes.get(2).buffer.data.buffer),
      ...new Float32Array(draws[0].attributes.get(3).buffer.data.buffer)],
    [0, 0, 1, 0, 0.5, 1, 0.25, 0.75, 0.75, 0.75, 0.5, 0.25],
    'unit 0 and unit 1 arrays must remain separate through bridge staging');
    assert.match(gpu.pipelines[0].vertex.module.code, /@location\(3\) texCoord1/);
    assert.match(gpu.pipelines[0].fragment.module.code, /textureSample\(fixedTexture0/);
    assert.match(gpu.pipelines[0].fragment.module.code, /textureSample\(fixedTexture1/);
    assert.match(gpu.pipelines[0].fragment.module.code, /color = texel0;/);
    assert.match(gpu.pipelines[0].fragment.module.code, /color = color \* texel1;/);
    assert.equal(program.stages.fragment.samplers.length, 2);
    assert.doesNotThrow(() => naga.validate(naga.parseWgsl(program.stages.vertex.wgsl)), 'two-unit vertex WGSL must validate');
    assert.doesNotThrow(() => naga.validate(naga.parseWgsl(program.stages.fragment.wgsl)), 'two-unit fragment WGSL must validate');
    assert.equal(sampled.get(2), textures[0].gpu);
    assert.equal(sampled.get(4), textures[1].gpu);
    assert.deepEqual(gl._fixedFunction.currentTexCoords[1], [0.25, 0.75, 0, 1]);
    const center = (2 * gpu.canvas.width + 2) * 4;
    assert.deepEqual(Array.from(gl._pixels.subarray(center, center + 4)), [128, 0, 0, 255],
      'the synchronous compositor mirror must combine lightmap texture unit 1');
  } finally { gpu.restore(); }
});

test('immediate multi-texture coordinates are captured per unit at each vertex', async () => {
  const gpu = createValidationDevice();
  try {
    gpu.device.queue.writeBuffer = () => {};
    gpu.device.createBindGroup = descriptor => descriptor;
    const createPipeline = gpu.device.createRenderPipeline;
    gpu.device.createRenderPipeline = descriptor => {
      descriptor.getBindGroupLayout = () => ({});
      return createPipeline(descriptor);
    };
    const gl = new WebGPUGL(gpu.canvas);
    await gl.ready;
    gl.activeTexture(0x84c0); gl.enable(0x0de1);
    gl.activeTexture(0x84c1); gl.enable(0x0de1);
    const draws = [];
    const draw = gl._draw.bind(gl);
    gl._draw = async (...args) => { draws.push(args[0]); return draw(...args); };
    gl.fixedFunctionImmediateDraw(4, new Float32Array([
      2, 0x84c0, 0.2, 0.3, 0, 2, 0x84c1, 0.8, 0.7, 0,
      1, -1, -1, 0, 1,
      2, 0x84c0, 0.4, 0.5, 0, 2, 0x84c1, 0.6, 0.9, 0,
      1, 1, -1, 0, 1,
      1, 0, 1, 0, 1,
    ]));
    await gl._ops;
    assert.equal(draws.length, 1);
    const coords = slot => [...new Float32Array(draws[0].attributes.get(slot).buffer.data.buffer)]
      .map(value => Math.round(value * 1e6) / 1e6);
    assert.deepEqual(coords(2),
      [0.2, 0.3, 0.4, 0.5, 0.4, 0.5]);
    assert.deepEqual(coords(3),
      [0.8, 0.7, 0.6, 0.9, 0.6, 0.9]);
  } finally { gpu.restore(); }
});
