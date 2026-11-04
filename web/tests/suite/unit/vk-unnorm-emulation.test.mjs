// Unnormalised-coordinate sampler emulation (unnormalizedCoordinates).
// WebGPU samplers always take normalised [0,1] coordinates; a guest
// sampler created with unnormalizedCoordinates = VK_TRUE receives
// texel-space coordinates instead (DXVK's swapchain blitter samples
// src_offset + src_extent * uv through one, with CLAMP_TO_BORDER). The
// bridge forwards the flag on the SAMPLER record; when such a sampler
// is bound, the renderer builds a pipeline variant whose WGSL divides
// the coordinates of sample calls on that sampler by the sampled
// texture's dimensions. Applied before the border rewrite, whose [0,1]
// range test then sees normalised coordinates.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  unnormPlanForDecls, unnormPlanSignature, rewriteUnnormalizedSamplersWGSL,
  rewriteBorderSamplersWGSL, borderPlanForDecls, scanWgslResources,
} from '../../../wgsl-utils.mjs';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';
import { applySpirvFix } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../vendor/naga/node.js';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const UNNORM_REC = { unnorm: true };
const NORM_REC = { unnorm: false };

test('plan: only unnormalised samplers enter the plan', () => {
  const decls = [
    { group: 0, binding: 2, name: 's', type: 'sampler', arrayCount: 1 },
    { group: 0, binding: 3, name: 'plain', type: 'sampler', arrayCount: 1 },
    { group: 1, binding: 0, name: 'arr', type: 'sampler', arrayCount: 4 },
    { group: 0, binding: 1, name: 'tex', type: 'texture_2d<f32>', arrayCount: 1 },
  ];
  const recs = { s: UNNORM_REC, plain: NORM_REC, arr: UNNORM_REC };
  const plan = unnormPlanForDecls(decls, d => recs[d.name] || null);
  assert.deepEqual([...plan.keys()], ['s']); // arrayed sampler decls are left unrewritten
  assert.equal(unnormPlanSignature(plan), 'g0b2');
  // Records decoded before the flag was forwarded carry no unnorm field.
  assert.equal(unnormPlanForDecls(decls, () => ({})).size, 0);
  assert.equal(unnormPlanForDecls(decls, () => null).size, 0);
});

const HEAD = `@group(0) @binding(0) var tex: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var other: sampler;
`;
const planFor = (name = 'samp') =>
  unnormPlanForDecls([{ group: 0, binding: 1, name, type: 'sampler', arrayCount: 1 }], () => UNNORM_REC);

test('rewrite: sample calls on an unnormalised sampler divide by the texture size', () => {
  const wgsl = HEAD + `fn f(uv: vec2<f32>) -> vec4<f32> {
  let a = textureSample(tex, samp, uv);
  let b = textureSampleLevel(tex, samp, uv, 0f);
  let c = textureSampleBias(tex, samp, uv, 0.5);
  let d = textureSampleGrad(tex, samp, uv, vec2<f32>(1.0), vec2<f32>(2.0));
  let e = textureSample(tex, other, uv);
  let g = textureGather(tex, samp, uv);
  let h = textureLoad(tex, vec2<i32>(3, 4), 0);
  return a + b + c + d + e + g + h;
}`;
  const out = rewriteUnnormalizedSamplersWGSL(wgsl, planFor());
  assert.match(out, /textureSample\(tex, samp, \(\(uv\) \/ vec2<f32>\(textureDimensions\(tex\)\)\)\)/);
  assert.match(out, /textureSampleLevel\(tex, samp, \(\(uv\) \/ vec2<f32>\(textureDimensions\(tex, u32\(0f\)\)\)\), 0f\)/);
  assert.match(out, /textureSampleBias\(tex, samp, \(\(uv\) \/ vec2<f32>\(textureDimensions\(tex\)\)\), 0\.5\)/);
  assert.match(out, /textureSampleGrad\(tex, samp, \(\(uv\) \/ vec2<f32>\(textureDimensions\(tex\)\)\), \(\(vec2<f32>\(1\.0\)\) \/ vec2<f32>\(textureDimensions\(tex\)\)\), \(\(vec2<f32>\(2\.0\)\) \/ vec2<f32>\(textureDimensions\(tex\)\)\)\)/);
  // Other samplers, gathers and loads are byte-identical.
  assert.ok(out.includes('textureSample(tex, other, uv)'));
  assert.ok(out.includes('textureGather(tex, samp, uv)'));
  assert.ok(out.includes('textureLoad(tex, vec2<i32>(3, 4), 0)'));
});

test('rewrite: array, 1d and 3d textures divide by their own dimension shape', () => {
  const wgsl = `@group(0) @binding(0) var arr: texture_2d_array<f32>;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var one: texture_1d<f32>;
@group(0) @binding(3) var vol: texture_3d<f32>;
fn f(uv: vec2<f32>) -> vec4<f32> {
  let a = textureSampleLevel(arr, samp, uv, 2u, 1f);
  let b = textureSample(one, samp, 17.0);
  let c = textureSample(vol, samp, vec3<f32>(1.0, 2.0, 3.0));
  return a + b + c;
}`;
  const out = rewriteUnnormalizedSamplersWGSL(wgsl, planFor());
  assert.match(out, /textureSampleLevel\(arr, samp, \(\(uv\) \/ vec2<f32>\(textureDimensions\(arr, u32\(1f\)\)\)\), 2u, 1f\)/);
  assert.match(out, /textureSample\(one, samp, \(\(17\.0\) \/ f32\(textureDimensions\(one\)\)\)\)/);
  assert.match(out, /textureSample\(vol, samp, \(\(vec3<f32>\(1\.0, 2\.0, 3\.0\)\) \/ vec3<f32>\(textureDimensions\(vol\)\)\)\)/);
});

test('rewrite: cube textures and empty/unbalanced inputs are passthroughs', () => {
  const cube = `@group(0) @binding(0) var cube: texture_cube<f32>;
@group(0) @binding(1) var samp: sampler;
fn f(dir: vec3<f32>) -> vec4<f32> { return textureSample(cube, samp, dir); }`;
  assert.equal(rewriteUnnormalizedSamplersWGSL(cube, planFor()), cube);
  const wgsl = HEAD + 'fn f(uv: vec2<f32>) -> vec4<f32> { return textureSample(tex, samp, uv); }';
  assert.equal(rewriteUnnormalizedSamplersWGSL(wgsl, new Map()), wgsl);
  const broken = wgsl.replace('textureSample(tex, samp, uv);', 'textureSample(tex, samp, uv;');
  assert.equal(rewriteUnnormalizedSamplersWGSL(broken, planFor()), broken);
});

test('composition: the border range test sees the divided coordinates', () => {
  const wgsl = HEAD + 'fn f(uv: vec2<f32>) -> vec4<f32> { return textureSampleLevel(tex, samp, uv, 0f); }';
  const divided = rewriteUnnormalizedSamplersWGSL(wgsl, planFor());
  const borderPlan = borderPlanForDecls(
    [{ group: 0, binding: 1, name: 'samp', type: 'sampler', arrayCount: 1 }],
    () => ({ addrModeU: 3, addrModeV: 3, addrModeW: 2, borderColor: 0, compareOp: 7 }));
  const out = rewriteBorderSamplersWGSL(divided, borderPlan);
  assert.match(out, /select\(textureSampleLevel\(tex, samp, \(\(uv\) \/ vec2<f32>\(textureDimensions\(tex, u32\(0f\)\)\)\), 0f\), vec4<f32>\(0\.0, 0\.0, 0\.0, 0\.0\), /);
  assert.match(out, /\(\(uv\) \/ vec2<f32>\(textureDimensions\(tex, u32\(0f\)\)\)\)\)\.x > 1\.0/);
});

function samplerPayload(id, { unnorm = null } = {}) {
  const b = new Uint8Array(unnorm === null ? 68 : 72);
  const d = new DataView(b.buffer);
  d.setBigUint64(0, BigInt(id), true);
  d.setUint32(8, 1, true); d.setUint32(12, 1, true); d.setUint32(16, 1, true); // mag/min/mipmap = linear
  d.setUint32(20, 3, true); d.setUint32(24, 3, true); // addrU/V = CLAMP_TO_BORDER
  d.setFloat32(28, 1, true); // maxAniso
  d.setUint32(32, 0x314d4153, true); // SAM1 tail
  d.setUint32(36, 3, true); d.setFloat32(40, 0, true); // addrW, mipLodBias
  d.setUint32(44, 0, true); d.setUint32(48, 0, true); d.setUint32(52, 7, true); // aniso, compare, compareOp
  d.setFloat32(56, 0, true); d.setFloat32(60, 0, true); // minLod, maxLod
  d.setUint32(64, 0, true); // borderColor = transparent black
  if (unnorm !== null) d.setUint32(68, unnorm ? 1 : 0, true);
  return b;
}

test('protocol: SAMPLER decode reads the appended unnormalised flag; old records default to false', () => {
  const u32s = (...v) => { const b = new Uint8Array(v.length * 4); const d = new DataView(b.buffer); v.forEach((x, i) => d.setUint32(i * 4, x, true)); return b; };
  const decode = payload => decodeV2Frame([
    { type: V2_REC.FRAME_BEGIN, payload: u32s(1, 64, 64, 0) },
    { type: V2_REC.SAMPLER, payload },
    { type: V2_REC.FRAME_END, payload: u32s(1) },
  ]);
  assert.equal([...decode(samplerPayload(0x2a, { unnorm: true })).samplers.values()][0].unnorm, true);
  assert.equal([...decode(samplerPayload(0x2b, { unnorm: false })).samplers.values()][0].unnorm, false);
  assert.equal([...decode(samplerPayload(0x2c)).samplers.values()][0].unnorm, false);
});

test('real naga output: an unnormalised plan rewrites a translated module', async () => {
  await naga.init();
  const spv = new Uint8Array(readFileSync(join(repoRoot, 'testdata/spirv/p8-lighting.frag.spv')));
  const wgsl = naga.translate({ from: 'spirv', to: 'wgsl', source: applySpirvFix(spv, {}).bytes });
  const samplerDecl = scanWgslResources(wgsl).find(d => d.type === 'sampler');
  assert.ok(samplerDecl, 'fixture declares a split combined sampler');
  const plan = unnormPlanForDecls([samplerDecl], () => UNNORM_REC);
  assert.equal(plan.size, 1);
  const out = rewriteUnnormalizedSamplersWGSL(wgsl, plan);
  assert.match(out, /textureDimensions\(albedoMap\)/);
  assert.equal(rewriteUnnormalizedSamplersWGSL(wgsl, unnormPlanForDecls([samplerDecl], () => NORM_REC)), wgsl);
});
