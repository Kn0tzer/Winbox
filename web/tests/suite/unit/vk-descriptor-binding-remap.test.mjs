// web/tests/suite/unit/vk-descriptor-binding-remap.test.mjs — page-tier
// descriptor-binding remap, campaign section 4.25.
//
// R15b re-roots root descriptors at synthesized bindings of
// 4096 + pushByteOff/8, which Dawn rejects at layout creation:
// "binding number (4096) exceeds the maxBindingsPerBindGroup limit
// (1000)". The page tier now compacts each non-push group's bindings
// below the device cap at pipeline build, keeping the original binding
// for draw-time set lookups. This test drives the real pipeline builder
// through the mock renderer (same harness as v2-replay-state) and
// asserts on the bind-group layout descriptors it actually creates:
// the re-rooted binding must arrive remapped, never as 4096.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';

test('synthesized high bindings are remapped below the device cap at layout creation', async () => {
  const saved = new Map(['navigator', 'GPUBufferUsage', 'GPUTextureUsage', 'GPUShaderStage', 'GPUMapMode']
    .map(k => [k, Object.getOwnPropertyDescriptor(globalThis, k)]));
  const layouts = [];
  const boundGroups = [];
  let draws = 0;
  let validationError = null;
  const device = {
    features: new Set(),
    lost: new Promise(() => {}), addEventListener() {}, pushErrorScope() {}, async popErrorScope() {
      const error = validationError; validationError = null; return error;
    },
    createBuffer(d) { if (d.mappedAtCreation) assert.equal(d.size % 4, 0, 'mapped buffers require four-byte allocation alignment');
      return { size: d.size, data: new ArrayBuffer(d.size), getMappedRange() { return this.data; },
      unmap() {}, async mapAsync() {}, destroy() {} }; },
    createTexture(d) { const t = { label: d.label, width: d.size[0], height: d.size[1], format: d.format,
      createView(v = {}) { return { texture: this, format: v.format || this.format }; }, destroy() {} }; return t; },
    createBindGroupLayout: d => { layouts.push(d); return d; },
    createPipelineLayout: d => d, createShaderModule: d => d,
    createRenderPipeline(d) { return d; }, createBindGroup: d => d, createSampler: () => ({}),
    queue: { writeBuffer(b, o, bytes) { new Uint8Array(b.data).set(bytes, o); },
      submit() {}, async onSubmittedWorkDone() {} },
    createCommandEncoder() { return {
      beginRenderPass() { const groups = new Map(); return { setPipeline() {}, setBindGroup(g, b) { groups.set(g, b); boundGroups.push(g); },
        setIndexBuffer() {}, setVertexBuffer() {},
        drawIndexed() { draws++; }, draw() { draws++; }, end() {} }; },
      copyTextureToBuffer() {}, copyTextureToTexture() {}, finish() { return {}; },
    }; },
  };
  const limits = {
    maxTextureDimension1D: 16384, maxTextureDimension2D: 16384, maxTextureDimension3D: 2048,
    maxTextureArrayLayers: 2048, maxBindGroups: 4, maxSampledTexturesPerShaderStage: 16,
    maxSamplersPerShaderStage: 16, maxUniformBuffersPerShaderStage: 12, maxStorageBuffersPerShaderStage: 8,
    maxUniformBufferBindingSize: 65536, maxStorageBufferBindingSize: 134217728, maxBufferSize: 268435456,
    maxVertexBuffers: 8, maxVertexAttributes: 16, maxColorAttachments: 8, maxInterStageShaderVariables: 16,
  };
  const adapter = { features: new Set(['clip-distances']), limits, async requestDevice(d) {
    assert.ok(d.requiredFeatures.includes('clip-distances'));
    assert.deepEqual(d.requiredLimits, limits);
    return device;
  } };
  const canvas = { width: 64, height: 32, getContext: () => context };
  const context = { configure() {}, unconfigure() {}, getCurrentTexture() { return { width: canvas.width, height: canvas.height }; } };
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: {
      gpu: { async requestAdapter() { return adapter; }, getPreferredCanvasFormat() { return 'bgra8unorm'; } },
  } });
  for (const [k, v] of Object.entries({ GPUBufferUsage: { UNIFORM: 1, COPY_DST: 2, VERTEX: 4, MAP_READ: 8, INDEX: 16, STORAGE: 128 },
    GPUTextureUsage: { RENDER_ATTACHMENT: 1, COPY_SRC: 2, COPY_DST: 4, TEXTURE_BINDING: 8, STORAGE_BINDING: 64 },
    GPUShaderStage: { VERTEX: 1, FRAGMENT: 2 }, GPUMapMode: { READ: 1 } }))
    Object.defineProperty(globalThis, k, { configurable: true, value: v });
  try {
    const renderer = await createVkRenderer2(canvas, {
      translate: async (_from, _source, stage) => stage === 'vertex'
        ? 'struct Push { value: vec4<f32>, }; var<immediate> push: Push; @vertex fn main() -> @builtin(position) vec4<f32> { return push.value; }'
        : 'struct Push { value: vec4<f32>, }; var<immediate> push: Push; struct RootCb { gMul: vec4<f32>, gAdd: vec4<f32>, }; @group(0) @binding(4096) var<storage> bw_root_p0: RootCb; @fragment fn main() -> @location(0) vec4<f32> { return bw_root_p0.gMul; }',
    });
    const pipe = { pipeId: 'p', vsHash: 'v', fsHash: 'f', topology: 3, vb: [], va: [] };
    const draw = { op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0 };
    const frame = { frameNo: 1, width: 64, height: 32,
      shaders: new Map([['v', { code: new Uint8Array(4) }], ['f', { code: new Uint8Array(4) }]]),
      images: new Map(), views: new Map(), samplers: new Map(),
      buffers: new Map([['rootbuf', new Uint8Array(256)]]), bufHashes: new Map(),
      passes: [
        { w: 64, h: 32, attachments: [], clearColor: [0, 0, 0, 1], ops: [{ op: 'bindPipe', pipe }] },
        { w: 64, h: 32, attachments: [{ imageId: 'image', samples: 1, isDepth: 0, loadOp: 1, storeOp: 0, format: 44 }],
          clearColor: [0, 0, 0, 1],
          ops: [
            { op: 'push', offset: 0, bytes: new Uint8Array(16) },
            { op: 'rootDescs', entries: new Map([[0, { obj: 'rootbuf', absOff: '0', len: '256' }]]) },
            draw,
          ] },
      ] };
    const result = await renderer.render(frame);
    assert.equal(result.rendered, true);
    assert.ok(draws >= 1, 'the draw must execute');
    const entries = layouts.flatMap(l => l.entries || []);
    assert.ok(!entries.some(e => e.binding === 4096), 'no layout entry may keep the synthesized 4096 binding');
    const root = entries.find(e => e.buffer && e.buffer.type === 'read-only-storage');
    assert.ok(root, 'the re-rooted storage buffer must appear in a layout');
    assert.equal(root.binding, 0, 'the re-rooted binding compacts to the first free slot in its group');
    assert.ok(entries.some(e => e.binding === 0 && e.buffer && e.buffer.type === 'uniform'), 'the push uniform at group 3 binding 0 stays put');
    assert.ok(boundGroups.includes(0), 'the remapped group 0 bind group is bound at draw time');
    assert.ok(boundGroups.includes(3), 'the push group 3 bind group is bound at draw time');
  } finally {
    for (const [k, descriptor] of saved) {
      if (descriptor) Object.defineProperty(globalThis, k, descriptor); else delete globalThis[k];
    }
  }
});

test('binding remap machinery is present and fail-closed over the cap', () => {
  const tier = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.ok(tier.includes('const maxBinding = device.limits?.maxBindingsPerBindGroup || 1000;'), 'the binding cap comes from the device limit');
  assert.ok(tier.includes("noteUnsupported('binding-count-limit'"), 'a group needing more bindings than the cap fails loudly');
  assert.ok(tier.includes("return fail('binding-count-limit')"), 'an over-cap group fails the pipeline');
  assert.ok(tier.includes('d.origBinding = d.binding;'), 'remapped decls keep their original binding for set lookups');
  assert.ok(tier.includes('bindingForElement(bindings, d.origBinding ?? d.binding, declElement(st, d, k))'), 'draw-time lookups use the original binding');
});
