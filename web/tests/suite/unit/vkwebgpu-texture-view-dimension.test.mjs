import {test} from 'node:test';
import assert from 'node:assert/strict';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {wgslTextureViewDimension} from '../../../wgsl-utils.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

// What WGSL requires of the layout entry and of the view bound to it.
const EXPECTED = {
  'texture_1d<f32>': '1d', 'texture_2d<f32>': '2d', 'texture_2d_array<f32>': '2d-array', 'texture_3d<f32>': '3d',
  'texture_cube<f32>': 'cube', 'texture_cube_array<f32>': 'cube-array',
  'texture_depth_2d': '2d', 'texture_depth_2d_array': '2d-array', 'texture_depth_cube': 'cube', 'texture_depth_cube_array': 'cube-array',
  'texture_multisampled_2d<f32>': '2d', 'texture_depth_multisampled_2d': '2d',
};

test('wgslTextureViewDimension maps every sampled texture type and rejects the rest', () => {
  for (const [type, dim] of Object.entries(EXPECTED)) assert.equal(wgslTextureViewDimension(type), dim, type);
  assert.equal(wgslTextureViewDimension('texture_storage_2d<rgba8unorm, write>'), null);
  assert.equal(wgslTextureViewDimension('sampler'), null);
  assert.equal(wgslTextureViewDimension('texture_3d_array<f32>'), null);
});

for (const type of Object.keys(EXPECTED).filter(t => !/multisampled|1d/.test(t))) {
  test(`${type}: layout entry, bound view and placeholder agree with the shader`, async () => {
    const {device, canvas, restore} = createValidationDevice();
    const layouts = [], views = [];
    const makeLayout = device.createBindGroupLayout, makeGroup = device.createBindGroup;
    device.createBindGroupLayout = d => { layouts.push(...d.entries.filter(e => e.texture)); return makeLayout(d); };
    device.createBindGroup = d => { views.push(...d.entries.map(e => e.resource).filter(v => v?.options)); return makeGroup(d); };
    try {
      const r = await createVkRenderer2(canvas, {translate: async (from, bytes, stage) => stage === 'fragment' ? `@group(0) @binding(11) var image: ${type}; fn main() {}` : 'fn main() {}'});
      const pipe = {pipeId: 'dim', topology: 3, vb: [], va: [], vsHash: 'vs', fsHash: 'fs'};
      const frame = {frameNo: 1, width: 4, height: 4, shaders: new Map([['vs', {code: new Uint8Array(4)}], ['fs', {code: new Uint8Array(4)}]]),
        images: new Map([['ci', {w: 2, h: 2, mips: 1, format: 44, hash: 'image', layers: 1, pixels: new Uint8Array(16)}]]), views: new Map([['cv', 'ci']]), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
        passes: [{w: 4, h: 4, clearColor: [0, 0, 0, 1], clearDepth: 1, attachments: [{imageId: null, isDepth: false, samples: 1, loadOp: 1, storeOp: 0}],
          ops: [{op: 'bindPipe', pipe}, {op: 'descSet', setIndex: 0, bindings: [{binding: 11, kind: 2, obj: 'cv', dstArrayElement: 0, count: 1}]}, {op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0}]}]};
      const result = await r.render(frame);
      assert.equal(result.rendered, true, JSON.stringify(result));
      r.destroy();
    } finally { restore(); }
    const want = EXPECTED[type];
    assert.deepEqual(layouts.map(e => [e.binding, e.texture.viewDimension]), [[11, want]]);
    assert.deepEqual(views.map(v => v.dimension), [want]);
  });
}
