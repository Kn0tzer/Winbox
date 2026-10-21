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
    assert.equal([...gl._fixedFunctionProgramCache.values()][0].attributeInfo[1].name, 'a_color');
  } finally {
    gpu.restore();
  }
});
