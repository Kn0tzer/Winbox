import { test } from 'node:test';
import assert from 'node:assert/strict';
import { blendFactor, convertIndices, packUniforms, stateKey, uniformLayout } from '../../../gl-webgpu-utils.mjs';

test('uniform layout and packing honor std140 vector, matrix, and array strides', () => {
  const layout = uniformLayout([
    { name: 'tint', type: 'vec3' },
    { name: 'enabled', type: 'bool' },
    { name: 'transform', type: 'mat3' },
    { name: 'weights', type: 'float', arrayLength: 2 },
  ]);
  assert.deepEqual(layout.fields.map(field => field.offset), [0, 12, 16, 64]);
  assert.equal(layout.size, 96);
  const bytes = packUniforms(layout, {
    tint: [0.25, 0.5, 0.75], enabled: true,
    transform: [1, 2, 3, 4, 5, 6, 7, 8, 9], weights: [0.125, 0.875],
  });
  const view = new DataView(bytes.buffer);
  assert.equal(view.getFloat32(0, true), 0.25);
  assert.equal(view.getInt32(12, true), 1);
  assert.equal(view.getFloat32(16, true), 1);
  assert.equal(view.getFloat32(24, true), 3);
  assert.equal(view.getFloat32(32, true), 4);
  assert.equal(view.getFloat32(64, true), 0.125);
  assert.equal(view.getFloat32(80, true), 0.875);
});

test('uint8 indices widen for WebGPU and wider index types preserve values', () => {
  assert.deepEqual([...convertIndices(new Uint8Array([0, 2, 255]), 0x1401)], [0, 2, 255]);
  assert.ok(convertIndices(new Uint8Array([1]), 0x1401) instanceof Uint16Array);
  assert.deepEqual([...convertIndices(new Uint16Array([3, 4]), 0x1403)], [3, 4]);
  assert.deepEqual([...convertIndices(new Uint32Array([5]), 0x1405)], [5]);
  assert.throws(() => convertIndices(new Uint8Array([0]), 0x1405), /Uint32Array/);
});

test('OpenGL blend factors map to WebGPU blend component names', () => {
  const cases = [
    [0x0000, 'zero'], [0x0001, 'one'], [0x0300, 'src'], [0x0301, 'one-minus-src'],
    [0x0302, 'src-alpha'], [0x0303, 'one-minus-src-alpha'], [0x0304, 'dst-alpha'],
    [0x0305, 'one-minus-dst-alpha'], [0x0306, 'dst'], [0x0307, 'one-minus-dst'],
    [0x0308, 'src-alpha-saturated'], [0x8001, 'constant'], [0x8004, 'one-minus-constant'],
  ];
  for (const [gl, gpu] of cases) assert.equal(blendFactor(gl), gpu);
  assert.throws(() => blendFactor(0xdead), /Unsupported blend factor/);
});

test('state hashing is stable across object key order and changes with state', () => {
  assert.equal(stateKey({ depth: true, blend: { src: 1, dst: 0 } }), stateKey({ blend: { dst: 0, src: 1 }, depth: true }));
  assert.notEqual(stateKey({ depth: true }), stateKey({ depth: false }));
});
