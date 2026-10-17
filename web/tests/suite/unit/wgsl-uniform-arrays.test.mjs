import test from 'node:test';
import assert from 'node:assert/strict';
import { wgslUniformSizes } from '../../../vkwebgpu.mjs';

test('uniform sizes preserve commas inside DXVK constant arrays and nested types', () => {
  const wgsl = `struct Constants { floats: array<vec4<f32>, 240>, };
    struct Nested { rows: array<array<vec4<f32>, 3>, 2>, extra: vec4<f32>, };
    @group(1) @binding(0) var<uniform> constants: Constants;
    @group(2) @binding(1) var<uniform> nested: Nested;`;
  const sizes = wgslUniformSizes(wgsl);
  assert.equal(sizes.get('1:0'), 3840);
  assert.equal(sizes.get('2:1'), 112);
});
