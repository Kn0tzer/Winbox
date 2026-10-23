// DXVK resolve/present shaders declare texture_multisampled_2d_array, which
// WebGPU cannot express (Dawn: "unresolved type"). The page tier binds the
// already-resolved texture, so the binding and per-sample loads are retyped.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { rewriteMultisampledTextures as rewrite } from '../../../wgsl-utils.mjs';

test('multisampled 2d-array loads become level-0 loads of a 2d-array texture', () => {
  const wgsl = `@group(0) @binding(0) 
var s_image: texture_multisampled_2d_array<f32>;
fn f(coord: vec3<i32>, i: i32) -> vec4<f32> {
    let n = textureNumSamples(s_image);
    return textureLoad(s_image, vec2<i32>(coord.x, coord.y), i32(coord.z), i);
}`;
  const out = rewrite(wgsl);
  assert.match(out, /var s_image: texture_2d_array<f32>;/);
  assert.match(out, /textureLoad\(s_image, vec2<i32>\(coord\.x, coord\.y\), i32\(coord\.z\), 0i\)/);
  assert.match(out, /let n = 1u;/);
  assert.ok(!out.includes('multisampled'));
});

test('multisampled 2d loads drop the sample index; other textures are untouched', () => {
  const wgsl = `var a: texture_multisampled_2d<f32>;
var b: texture_2d<f32>;
fn f() -> vec4<f32> { return textureLoad(a, vec2<i32>(1, 2), 3) + textureLoad(b, vec2<i32>(1, 2), 3); }`;
  const out = rewrite(wgsl);
  assert.match(out, /var a: texture_2d<f32>;/);
  assert.match(out, /textureLoad\(a, vec2<i32>\(1, 2\), 0i\) \+ textureLoad\(b, vec2<i32>\(1, 2\), 3\)/);
  const plain = 'var c: texture_2d<f32>;';
  assert.equal(rewrite(plain), plain);
});
