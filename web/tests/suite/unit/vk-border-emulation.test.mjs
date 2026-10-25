// CLAMP_TO_BORDER shader-tier emulation (docs/known-gaps/vulkan.md row citing
// vk64bridge.cpp:472). WebGPU GPUSamplers have no border addressing, so the
// protocol decoder maps the mode to clamp-to-edge for the GPUSampler and
// retains the raw modes; when a border sampler is bound, the renderer builds
// a pipeline variant whose WGSL wraps sampled-image calls on that sampler
// with a select of the Vulkan border colour for out-of-[0,1] coordinates.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  borderPlanForDecls, borderPlanSignature, rewriteBorderSamplersWGSL, scanWgslResources,
} from '../../../wgsl-utils.mjs';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';
import { applySpirvFix } from '../../../../tools/spirvfix/spirvfix.mjs';
import * as naga from '../../../vendor/naga/node.js';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const BORDER_REC = { addrModeU: 3, addrModeV: 3, addrModeW: 2, borderColor: 2, compareOp: 7 };
const EDGE_REC = { addrModeU: 2, addrModeV: 2, addrModeW: 2, borderColor: 0, compareOp: 7 };

test('plan: only CLAMP_TO_BORDER axes on resolved samplers enter the plan', () => {
  const decls = [
    { group: 0, binding: 2, name: 's', type: 'sampler', arrayCount: 1 },
    { group: 0, binding: 3, name: 'plain', type: 'sampler', arrayCount: 1 },
    { group: 1, binding: 0, name: 'cmp', type: 'sampler_comparison', arrayCount: 1 },
    { group: 0, binding: 1, name: 'tex', type: 'texture_2d<f32>', arrayCount: 1 },
  ];
  const recs = {
    s: { addrModeU: 3, addrModeV: 2, addrModeW: 3, borderColor: 4, compareOp: 7 },
    plain: EDGE_REC,
    cmp: { addrModeU: 3, addrModeV: 3, addrModeW: 2, borderColor: 2, compareOp: 1 },
  };
  const plan = borderPlanForDecls(decls, d => recs[d.name] || null);
  assert.deepEqual([...plan.keys()].sort(), ['cmp', 's']);
  assert.equal(plan.get('s').axes, 0b101); // U and W border, V clamp-to-edge
  assert.deepEqual(plan.get('s').color, [1, 1, 1, 1]); // FLOAT_OPAQUE_WHITE
  assert.equal(plan.get('cmp').comparison, true);
  assert.equal(plan.get('cmp').compareOp, 1);
  assert.match(borderPlanSignature(plan), /g0b2=5:4/);
  assert.match(borderPlanSignature(plan), /g1b0=3:2:c1/);
  // Records decoded before raw modes were retained carry no border axes.
  assert.equal(borderPlanForDecls(decls, () => ({ borderColor: 2 })).size, 0);
  // Unresolved declarations never enter the plan.
  assert.equal(borderPlanForDecls(decls, () => null).size, 0);
});

const HEAD = `@group(0) @binding(0) var tex: texture_2d<f32>;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var other: sampler;
`;
const planFor = (rec = BORDER_REC, name = 'samp') =>
  borderPlanForDecls([{ group: 0, binding: 1, name, type: 'sampler', arrayCount: 1 }], () => rec);

test('rewrite: textureSample/Level/Bias/Grad on a border sampler are wrapped', () => {
  const wgsl = HEAD + `fn f(uv: vec2<f32>) -> vec4<f32> {
  let a = textureSample(tex, samp, uv);
  let b = textureSampleLevel(tex, samp, uv, 1.0);
  let c = textureSampleBias(tex, samp, uv, 0.5);
  let d = textureSampleGrad(tex, samp, uv, vec2<f32>(1.0), vec2<f32>(0.0));
  let e = textureSample(tex, other, uv);
  return a + b + c + d + e;
}`;
  const out = rewriteBorderSamplersWGSL(wgsl, planFor());
  const border = 'vec4<f32>(0.0, 0.0, 0.0, 1.0)'; // FLOAT_OPAQUE_BLACK
  const outside = '((uv).x < 0.0 || (uv).x > 1.0) || ((uv).y < 0.0 || (uv).y > 1.0)';
  for (const callText of [
    'textureSample(tex, samp, uv)',
    'textureSampleLevel(tex, samp, uv, 1.0)',
    'textureSampleBias(tex, samp, uv, 0.5)',
    'textureSampleGrad(tex, samp, uv, vec2<f32>(1.0), vec2<f32>(0.0))',
  ]) assert.ok(out.includes(`select(${callText}, ${border}, ${outside})`), callText + ' wrapped');
  assert.ok(out.includes('let e = textureSample(tex, other, uv);'), 'non-border sampler untouched');
});

test('rewrite: gather, cube and W-only-on-2D calls keep the approximation', () => {
  const wgsl = HEAD + `@group(0) @binding(3) var cube: texture_cube<f32>;
fn f(uv: vec2<f32>, dir: vec3<f32>) -> vec4<f32> {
  let g = textureGather(0, tex, samp, uv);
  let c = textureSample(cube, samp, dir);
  return g + c;
}`;
  assert.equal(rewriteBorderSamplersWGSL(wgsl, planFor()), wgsl);
  const wOnly = planFor({ ...BORDER_REC, addrModeU: 2, addrModeV: 2, addrModeW: 3 });
  const flat = HEAD + 'fn f(uv: vec2<f32>) -> vec4<f32> { return textureSample(tex, samp, uv); }';
  assert.equal(rewriteBorderSamplersWGSL(flat, wOnly), flat, 'a 2D texture has no W axis to border');
});

test('rewrite: axis mask, element type, array and 3D coordinates are honoured', () => {
  const uOnly = planFor({ ...BORDER_REC, addrModeV: 2 });
  const flat = HEAD + 'fn f(uv: vec2<f32>) -> vec4<f32> { return textureSample(tex, samp, uv); }';
  const out = rewriteBorderSamplersWGSL(flat, uOnly);
  assert.ok(out.includes('((uv).x < 0.0 || (uv).x > 1.0)'));
  assert.ok(!out.includes('(uv).y'), 'V is not a border axis here');

  const ints = `@group(0) @binding(0) var itex: texture_2d<i32>;
@group(0) @binding(1) var samp: sampler;
fn f(uv: vec2<f32>) -> vec4<i32> { return textureSample(itex, samp, uv); }`;
  assert.ok(rewriteBorderSamplersWGSL(ints, planFor()).includes('vec4<i32>(0, 0, 0, 1)'));

  const arr = `@group(0) @binding(0) var atex: texture_2d_array<f32>;
@group(0) @binding(1) var samp: sampler;
fn f(uv: vec2<f32>) -> vec4<f32> { return textureSample(atex, samp, uv, 2); }`;
  const arrOut = rewriteBorderSamplersWGSL(arr, planFor());
  assert.ok(arrOut.includes('select(textureSample(atex, samp, uv, 2), vec4<f32>(0.0, 0.0, 0.0, 1.0),'));
  assert.ok(!arrOut.includes('(2).x'), 'the array index is not a coordinate');

  const vol = `@group(0) @binding(0) var vtex: texture_3d<f32>;
@group(0) @binding(1) var samp: sampler;
fn f(p: vec3<f32>) -> vec4<f32> { return textureSample(vtex, samp, p); }`;
  const volOut = rewriteBorderSamplersWGSL(vol, planFor({ ...BORDER_REC, addrModeW: 3 }));
  assert.ok(volOut.includes('((p).z < 0.0 || (p).z > 1.0)'), 'W axis checked on 3D textures');
});

test('rewrite: comparison samplers compare the border depth against the reference', () => {
  const wgsl = `@group(0) @binding(0) var shadow: texture_depth_2d;
@group(0) @binding(1) var csamp: sampler_comparison;
fn f(uv: vec2<f32>) -> f32 {
  let a = textureSampleCompare(shadow, csamp, uv, 0.5);
  let b = textureSampleCompareLevel(shadow, csamp, uv, 0.25, 0.0);
  return a + b;
}`;
  const plan = borderPlanForDecls(
    [{ group: 0, binding: 1, name: 'csamp', type: 'sampler_comparison', arrayCount: 1 }],
    () => ({ addrModeU: 3, addrModeV: 3, addrModeW: 2, borderColor: 2, compareOp: 1 })); // LESS, opaque black
  const out = rewriteBorderSamplersWGSL(wgsl, plan);
  assert.ok(out.includes('select(textureSampleCompare(shadow, csamp, uv, 0.5), select(0.0, 1.0, (0.0 < (0.5))),'));
  assert.ok(out.includes('select(textureSampleCompareLevel(shadow, csamp, uv, 0.25, 0.0), select(0.0, 1.0, (0.0 < (0.25))),'));

  const depth = `@group(0) @binding(0) var dtex: texture_depth_2d;
@group(0) @binding(1) var samp: sampler;
fn f(uv: vec2<f32>) -> f32 { return textureSample(dtex, samp, uv); }`;
  const depthOut = rewriteBorderSamplersWGSL(depth, planFor({ ...BORDER_REC, borderColor: 4 }));
  assert.ok(depthOut.includes('select(textureSample(dtex, samp, uv), 1.0,'), 'depth border is the red channel as a scalar');
});

test('rewrite: empty plan and unbalanced input are byte-identical passthroughs', () => {
  const wgsl = HEAD + 'fn f(uv: vec2<f32>) -> vec4<f32> { return textureSample(tex, samp, uv); }';
  assert.equal(rewriteBorderSamplersWGSL(wgsl, new Map()), wgsl);
  const broken = wgsl.replace('textureSample(tex, samp, uv);', 'textureSample(tex, samp, uv;');
  assert.equal(rewriteBorderSamplersWGSL(broken, planFor()), broken);
});

function samplerPayload(id, { addrU = 3, addrV = 3, addrW = 2, borderColor = 2 } = {}) {
  const b = new Uint8Array(68);
  const d = new DataView(b.buffer);
  d.setBigUint64(0, BigInt(id), true);
  d.setUint32(8, 1, true); d.setUint32(12, 1, true); d.setUint32(16, 1, true); // mag/min/mipmap = linear
  d.setUint32(20, addrU, true); d.setUint32(24, addrV, true);
  d.setFloat32(28, 1, true); // maxAniso
  d.setUint32(32, 0x314d4153, true); // SAM1 tail
  d.setUint32(36, addrW, true); d.setFloat32(40, 0, true); // addrW, mipLodBias
  d.setUint32(44, 0, true); d.setUint32(48, 0, true); d.setUint32(52, 7, true); // aniso, compare, compareOp
  d.setFloat32(56, 0, true); d.setFloat32(60, 32, true); // minLod, maxLod
  d.setUint32(64, borderColor, true);
  return b;
}

test('protocol: SAMPLER decode retains the raw address modes and border colour', () => {
  const u32s = (...v) => { const b = new Uint8Array(v.length * 4); const d = new DataView(b.buffer); v.forEach((x, i) => d.setUint32(i * 4, x, true)); return b; };
  const f = decodeV2Frame([
    { type: V2_REC.FRAME_BEGIN, payload: u32s(1, 64, 64, 0) },
    { type: V2_REC.SAMPLER, payload: samplerPayload(0x2a) },
    { type: V2_REC.FRAME_END, payload: u32s(1) },
  ]);
  const s = [...f.samplers.values()][0];
  assert.equal(s.addrModeU, 3); assert.equal(s.addrModeV, 3); assert.equal(s.addrModeW, 2);
  assert.equal(s.addrU, 'clamp-to-edge', 'the GPUSampler mode is still the collapsed mapping');
  assert.equal(s.borderColor, 2);
});

test('real naga output: a DXVK lighting shader wraps its combined sampler', async () => {
  await naga.init();
  const spv = new Uint8Array(readFileSync(join(repoRoot, 'testdata/spirv/p8-lighting.frag.spv')));
  const wgsl = naga.translate({ from: 'spirv', to: 'wgsl', source: applySpirvFix(spv, {}).bytes });
  const samplerDecl = scanWgslResources(wgsl).find(d => d.type === 'sampler');
  assert.ok(samplerDecl, 'fixture declares a split combined sampler');
  const plan = borderPlanForDecls([samplerDecl], () => BORDER_REC);
  assert.equal(plan.size, 1);
  const out = rewriteBorderSamplersWGSL(wgsl, plan);
  assert.match(out, /select\(textureSample\(albedoMap, bw_combined_g0_b1_v\d+_, /);
  assert.match(out, /vec4<f32>\(0\.0, 0\.0, 0\.0, 1\.0\)/);
  assert.equal(rewriteBorderSamplersWGSL(wgsl, borderPlanForDecls([samplerDecl], () => EDGE_REC)), wgsl);
});

const FS_BORDER = `@group(0) @binding(0) var tex: texture_2d<f32>;
@group(0) @binding(1) var bw_combined_g0_b0_v33_: sampler;
@fragment fn main(@location(0) uv: vec2<f32>) -> @location(0) vec4<f32> { return textureSample(tex, bw_combined_g0_b0_v33_, uv); }`;
const VS_PLAIN = '@vertex fn main() -> @builtin(position) vec4<f32> { return vec4<f32>(0.0); }';

async function renderWithSampler(samplerId, record) {
  const { canvas, pipelines, restore } = createValidationDevice();
  try {
    const r = await createVkRenderer2(canvas, { translate: async (from, bytes, stage) => stage === 'fragment' ? FS_BORDER : VS_PLAIN });
    const pipe = { pipeId: 'bord', topology: 3, vb: [], va: [], vsHash: 'vs', fsHash: 'fs' };
    const frame = { frameNo: 1, width: 4, height: 4, shaders: new Map([['vs', { code: new Uint8Array(4) }], ['fs', { code: new Uint8Array(4) }]]),
      images: new Map([['00000000000000a1', { w: 4, h: 4, mips: 1, format: 44, hash: 'img', pixels: null }]]),
      views: new Map([['v1', '00000000000000a1']]),
      samplers: new Map([[samplerId, record]]), buffers: new Map(), bufHashes: new Map(), readbacks: [],
      passes: [{ w: 4, h: 4, clearColor: [0, 0, 0, 1], clearDepth: 1,
        attachments: [{ imageId: null, isDepth: false, samples: 1, loadOp: 1, storeOp: 0 }],
        ops: [{ op: 'bindPipe', pipe },
          { op: 'descSet', setIndex: 0, bindings: [{ binding: 0, kind: 2, obj: 'v1', range: samplerId, dstArrayElement: 0, count: 1 }] },
          { op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0 }] }] };
    const result = await r.render(frame);
    r.destroy();
    return { result, pipelines };
  } finally { restore(); }
}

const decodedSampler = modes => ({
  mag: 'linear', min: 'linear', mipmap: 'linear',
  addrU: 'clamp-to-edge', addrV: 'clamp-to-edge', addrW: 'clamp-to-edge',
  addrModeU: modes, addrModeV: modes, addrModeW: 2,
  maxAnisotropy: 1, anisotropyEnable: false, compareEnable: false, compareOp: 7,
  mipLodBias: 0, minLod: 0, maxLod: 32, borderColor: 2,
});

test('renderer: a bound border sampler produces a wrapped pipeline variant', async () => {
  const { result, pipelines } = await renderWithSampler('00000000000000b1', decodedSampler(3));
  assert.equal(result.rendered, true);
  assert.equal(pipelines.length, 1);
  const code = pipelines[0].fragment.module.code;
  assert.ok(code.includes('select(textureSample(tex, bw_combined_g0_b0_v33_, uv), vec4<f32>(0.0, 0.0, 0.0, 1.0),'), code);
});

test('renderer: the same pipeline without a border sampler stays unwrapped', async () => {
  const { result, pipelines } = await renderWithSampler('00000000000000b2', decodedSampler(2));
  assert.equal(result.rendered, true);
  assert.equal(pipelines.length, 1);
  assert.ok(!pipelines[0].fragment.module.code.includes('select(textureSample'), pipelines[0].fragment.module.code);
});
