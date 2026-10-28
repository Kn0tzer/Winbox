// web/tests/suite/unit/vk-sampled-storage-serving.test.mjs — sampled-texture
// serving for read-only storage reads, the serving path campaign section
// 4.26 named for the storage-format-access rejection.
//
// Dawn will not serve r16float or r8uint as storage textures for reads,
// but both are ordinary sampled formats, and a read-only storage read is
// a level-0 sampled load of the same texels over the same GPUTexture and
// view range. rewriteReadOnlyStorageToSampled (web/wgsl-utils.mjs)
// retypes exactly the provable shapes -- a texture_storage_2d<FORMAT,
// read> declaration in one of the two formats whose every use is a
// two-argument textureLoad or a textureDimensions -- and leaves every
// other shape on the storage path, where the named
// storage-format-access rejection (vk-storage-format-access.test.mjs)
// still applies. These guards pin the served set, the rewrite, the
// decline reasons, and the page-tier wiring in both pipeline builders.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { rewriteReadOnlyStorageToSampled, SAMPLED_STORAGE_FORMATS } from '../../../wgsl-utils.mjs';
import { V2_STORAGE_FORMAT, V2_STORAGE_USAGE_FORBIDDEN } from '../../../vk-protocol.mjs';
import { applySpirvFix } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../vendor/naga/node.js';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('the served format set is exactly the two probe-proven formats', () => {
  assert.deepEqual(SAMPLED_STORAGE_FORMATS, { r16float: 'f32', r8uint: 'u32' });
});

// The shape naga emits for the captured uav12 heap-array module with an
// r16float view bound (spirvfix R15a clone): attributes and var on
// separate lines, trailing underscore in the generated name.
const R16_MODULE = `@group(5) @binding(0)
var bw_arr_g5_b0_p0_a0_: texture_storage_2d<r16float,read>;
@fragment fn main(@builtin(position) p: vec4<f32>) -> @location(0) vec4<f32> {
  let _e9 = p.x;
  let _e11 = p.y;
  let v = textureLoad(bw_arr_g5_b0_p0_a0_, vec2<u32>(u32(_e9), u32(_e11)));
  let d = textureDimensions(bw_arr_g5_b0_p0_a0_);
  return vec4<f32>(v.x + f32(d.x), v.y, v.z, v.w);
}`;

test('r16float read-only storage is served as texture_2d<f32>', () => {
  const r = rewriteReadOnlyStorageToSampled(R16_MODULE);
  assert.equal(r.declined.length, 0);
  assert.deepEqual(r.served, [{ name: 'bw_arr_g5_b0_p0_a0_', format: 'r16float', group: 5, binding: 0, elem: 'f32' }]);
  assert.ok(r.code.includes('var bw_arr_g5_b0_p0_a0_: texture_2d<f32>;'), 'declaration retyped');
  assert.ok(r.code.includes('textureLoad(bw_arr_g5_b0_p0_a0_, vec2<u32>(u32(_e9), u32(_e11)), 0i)'), 'load gains level 0');
  assert.ok(r.code.includes('textureDimensions(bw_arr_g5_b0_p0_a0_)'), 'dimensions call is unchanged');
  assert.ok(!r.code.includes('texture_storage'), 'no storage type remains');
});

test('r8uint read-only storage is served as texture_2d<u32>', () => {
  const src = `@group(2) @binding(1) var u: texture_storage_2d<r8uint, read>;
@fragment fn main() -> @location(0) vec4<f32> { let v = textureLoad(u, vec2<u32>(3u, 4u)); return vec4<f32>(f32(v.x) / 255.0, 0.0, 0.0, 1.0); }`;
  const r = rewriteReadOnlyStorageToSampled(src);
  assert.deepEqual(r.served.map(s => s.elem), ['u32']);
  assert.ok(r.code.includes('var u: texture_2d<u32>;'));
  assert.ok(r.code.includes('textureLoad(u, vec2<u32>(3u, 4u), 0i)'));
});

test('every load of a served variable gains the level, nested coords included', () => {
  const src = `@group(0) @binding(0) var t: texture_storage_2d<r16float, read>;
@fragment fn main() -> @location(0) vec4<f32> {
  let a = textureLoad(t, vec2<i32>(0, 0));
  let b = textureLoad(t, vec2<i32>(i32(textureDimensions(t).x) - 1, 0));
  return a + b;
}`;
  const r = rewriteReadOnlyStorageToSampled(src);
  assert.equal(r.served.length, 1);
  assert.equal((r.code.match(/, 0i\)/g) || []).length, 2);
});

test('unprovable shapes decline with named reasons and stay byte-identical', () => {
  const cases = [
    ['access-not-readonly', `@group(0) @binding(0) var t: texture_storage_2d<r16float, read_write>;
@compute @workgroup_size(1) fn main() { textureStore(t, vec2<i32>(0, 0), vec4<f32>(1.0)); }`],
    ['access-not-readonly', `@group(0) @binding(0) var t: texture_storage_2d<r8uint, write>;
@compute @workgroup_size(1) fn main() { textureStore(t, vec2<i32>(0, 0), vec4<u32>(1u)); }`],
    ['format-not-served', `@group(0) @binding(0) var t: texture_storage_2d<rgba8unorm, read>;
@fragment fn main() -> @location(0) vec4<f32> { return textureLoad(t, vec2<i32>(0, 0)); }`],
    ['format-not-served', `@group(0) @binding(0) var t: texture_storage_2d<rgba16float, read>;
@fragment fn main() -> @location(0) vec4<f32> { return textureLoad(t, vec2<i32>(0, 0)); }`],
    ['dimension-not-2d', `@group(0) @binding(0) var t: texture_storage_2d_array<r16float, read>;
@fragment fn main() -> @location(0) vec4<f32> { return textureLoad(t, vec2<i32>(0, 0), 0i); }`],
    ['unrecognized-use', `@group(0) @binding(0) var t: texture_storage_2d<r8uint, read>;
@compute @workgroup_size(1) fn main() { let v = textureLoad(t, vec2<i32>(0, 0)); textureStore(t, vec2<i32>(1, 1), v); }`],
    ['load-arity', `@group(0) @binding(0) var t: texture_storage_2d<r16float, read>;
@fragment fn main() -> @location(0) vec4<f32> { return textureLoad(t, vec2<i32>(0, 0), 0i); }`],
    ['dimensions-arity', `@group(0) @binding(0) var t: texture_storage_2d<r16float, read>;
@fragment fn main() -> @location(0) vec4<f32> { let d = textureDimensions(t, 0i); return textureLoad(t, vec2<i32>(0, 0)) + vec4<f32>(f32(d.x)); }`],
    ['type-appears-elsewhere', `@group(0) @binding(0) var t: texture_storage_2d<r8uint, read>;
fn peek(x: texture_storage_2d<r8uint, read>) -> vec4<u32> { return textureLoad(x, vec2<i32>(0, 0)); }
@fragment fn main() -> @location(0) vec4<f32> { let v = peek(t); return vec4<f32>(f32(v.x), 0.0, 0.0, 1.0); }`],
  ];
  for (const [reason, src] of cases) {
    const r = rewriteReadOnlyStorageToSampled(src);
    assert.equal(r.served.length, 0, reason + ': nothing served');
    assert.equal(r.declined.length, 1, reason + ': one decline');
    assert.equal(r.declined[0].reason, reason);
    assert.equal(r.code, src, reason + ': module byte-identical');
  }
});

test('a mixed module serves the provable variable and keeps the other', () => {
  const src = `@group(0) @binding(0) var a: texture_storage_2d<r16float, read>;
@group(0) @binding(1) var b: texture_storage_2d<rgba16float, read_write>;
@compute @workgroup_size(1) fn main() {
  let v = textureLoad(a, vec2<i32>(0, 0));
  textureStore(b, vec2<i32>(0, 0), v);
}`;
  const r = rewriteReadOnlyStorageToSampled(src);
  assert.deepEqual(r.served.map(s => s.name), ['a']);
  assert.deepEqual(r.declined.map(d => d.name + ':' + d.reason), ['b:format-not-served']);
  assert.ok(r.code.includes('var a: texture_2d<f32>;'));
  assert.ok(r.code.includes('var b: texture_storage_2d<rgba16float, read_write>;'), 'unserved declaration untouched');
  assert.ok(r.code.includes('textureStore(b, vec2<i32>(0, 0), v);'), 'unserved uses untouched');
});

test('modules without storage textures are returned identical', () => {
  const src = `@group(0) @binding(0) var t: texture_2d<f32>;
@group(0) @binding(1) var s: sampler;
@fragment fn main() -> @location(0) vec4<f32> { return textureSample(t, s, vec2<f32>(0.5, 0.5)); }`;
  const r = rewriteReadOnlyStorageToSampled(src);
  assert.equal(r.code, src);
  assert.deepEqual(r.served, []);
  assert.deepEqual(r.declined, []);
});

test('the real captured uav12 module serves r16float end to end', async () => {
  await naga.init();
  const spv = new Uint8Array(readFileSync(join(repoRoot, 'testdata/spirv/d3d12-ps-uav-heap-array.spv')));
  const wgsl = String(naga.translate({ from: 'spirv', to: 'wgsl', source: applySpirvFix(spv, { storageFormats: '5:0=9' }).bytes }));
  assert.ok(wgsl.includes('texture_storage_2d<r16float,read>'), 'fixture translates to the walled shape');
  const r = rewriteReadOnlyStorageToSampled(wgsl);
  assert.equal(r.served.length, 1);
  assert.equal(r.served[0].format, 'r16float');
  assert.ok(r.code.includes('texture_2d<f32>'));
  assert.ok(!r.code.includes('texture_storage'), 'storage type fully rewritten');
  // The rewritten module must still validate in naga's WGSL frontend.
  String(naga.translate({ from: 'wgsl', to: 'wgsl', source: r.code }));
  // A Dawn-served format on the same module keeps the storage path.
  const served8 = String(naga.translate({ from: 'spirv', to: 'wgsl', source: applySpirvFix(spv, { storageFormats: '5:0=4' }).bytes }));
  const r8 = rewriteReadOnlyStorageToSampled(served8);
  assert.equal(r8.served.length, 0);
  assert.equal(r8.code, served8);
});

test('both pipeline builders serve sampled storage before scanning declarations', () => {
  const tier = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.ok(tier.includes('rewriteReadOnlyStorageToSampled,'), 'the rewrite is imported');
  assert.ok(tier.includes('const serveSampledStorage = (holder) => {'), 'the serving helper exists');
  assert.ok(tier.includes('vsR = serveSampledStorage(vsR);'), 'graphics vertex stage served');
  assert.ok(tier.includes('fsR = serveSampledStorage(fsR);'), 'graphics fragment stage served');
  assert.ok(tier.includes('csR = serveSampledStorage(csR);'), 'compute stage served');
  const serveAt = tier.indexOf('csR = serveSampledStorage(csR);');
  const scanAt = tier.indexOf('const decls = scanWgslResources(csR.code);', serveAt);
  assert.ok(scanAt > serveAt, 'compute serving happens before declarations are scanned');
  assert.ok(tier.includes("stats.sampledStorageServed = (stats.sampledStorageServed || 0) + r.served.length;"), 'servings are counted in renderer stats');
});

test('storage usage is withheld only from Dawn-forbidden formats, all of them sampled-served', () => {
  // gpuTextureFor must not request STORAGE_BINDING for a format Dawn
  // refuses to create storage-capable textures for: the texture itself
  // becomes invalid and poisons sampled binds too (uav12 evidence,
  // campaign section 4.34). The set is keyed by VkFormat and every
  // member must be a format whose reads are served sampled instead --
  // a forbidden format that is not sampled-served would be bindable
  // neither way.
  assert.deepEqual([...V2_STORAGE_USAGE_FORBIDDEN], [76, 13]);
  for (const vkFormat of V2_STORAGE_USAGE_FORBIDDEN)
    assert.ok(SAMPLED_STORAGE_FORMATS[V2_STORAGE_FORMAT[vkFormat].wgsl],
      'VkFormat ' + vkFormat + ' (' + V2_STORAGE_FORMAT[vkFormat].wgsl + ') must be sampled-served');
  const tier = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.ok(tier.includes('V2_STORAGE_FORMAT[img.format] && !V2_STORAGE_USAGE_FORBIDDEN.has(img.format)'),
    'gpuTextureFor conditions STORAGE_BINDING on the forbidden set');
});
