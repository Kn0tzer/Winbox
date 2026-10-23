// WebGPU rejects a pipeline whose fragment input has fewer components than the
// vertex output at the same location ("component count (4) ... is different
// from ... (3)"); Gang Beasts' D3D11 shaders do that routinely.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { matchFragmentInputsToVertexOutputs as match } from '../../../wgsl-utils.mjs';

const vs = `struct VertexOutput {
    @location(0) @interpolate(flat) member: i32,
    @location(1) member_1: vec4<f32>,
    @location(2) member_2: vec4<f32>,
    @builtin(position) gl_Position: vec4<f32>,
}
@vertex 
fn main(@builtin(vertex_index) i: u32) -> VertexOutput { return VertexOutput(1i, vec4<f32>(), vec4<f32>(), vec4<f32>()); }`;

test('direct fragment parameters are widened and swizzled back', () => {
  const fs = `var<private> a_1: vec3<f32>;
var<private> b_1: vec2<f32>;
@fragment 
fn main(@location(1) a: vec3<f32>, @location(2) b: vec2<f32>) -> @location(0) vec4<f32> {
    a_1 = a;
    b_1 = b;
    return vec4<f32>(a_1, b_1.x);
}`;
  const out = match(vs, fs);
  assert.match(out, /@location\(1\) a: vec4<f32>/);
  assert.match(out, /@location\(2\) b: vec4<f32>/);
  assert.match(out, /a_1 = a\.xyz;/);
  assert.match(out, /b_1 = b\.xy;/);
  assert.match(out, /-> @location\(0\) vec4<f32>/);
});

test('struct fragment inputs are widened; outputs and matching inputs are untouched', () => {
  const fs = `struct FragmentOutput { @location(1) o: vec2<f32>, }
struct FragmentInput { @location(1) member_1: vec2<f32>, @location(2) member_2: vec4<f32>, }
var<private> a_1: vec2<f32>;
@fragment 
fn main(fragmentinput: FragmentInput) -> FragmentOutput {
    a_1 = fragmentinput.member_1;
    return FragmentOutput(a_1);
}`;
  const out = match(vs, fs);
  assert.match(out, /@location\(1\) member_1: vec4<f32>/);
  assert.match(out, /a_1 = fragmentinput\.member_1\.xy;/);
  assert.match(out, /@location\(1\) o: vec2<f32>/);
  assert.match(out, /@location\(2\) member_2: vec4<f32>/);
});

test('leaves shaders alone when the copy cannot be rewritten', () => {
  const fs = `@fragment 
fn main(@location(1) a: vec3<f32>) -> @location(0) vec4<f32> { return vec4<f32>(a, 1f); }`;
  assert.equal(match(vs, fs), fs);
  assert.equal(match('fn x() {}', fs), fs);
});
