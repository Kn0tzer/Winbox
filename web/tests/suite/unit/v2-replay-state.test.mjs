import test from 'node:test';
import assert from 'node:assert/strict';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';

test('replay carries a pipeline bound before the render pass, preserves push constants and sizes attachments', async () => {
  const saved = new Map(['navigator', 'GPUBufferUsage', 'GPUTextureUsage', 'GPUShaderStage', 'GPUMapMode']
    .map(k => [k, Object.getOwnPropertyDescriptor(globalThis, k)]));
  const draws = [], textures = [], pipelineFormats = [], views = [];
  let groups = new Map();
  let validationError = null;
  const device = {
    lost: new Promise(() => {}), addEventListener() {}, pushErrorScope() {}, async popErrorScope() {
      const error = validationError; validationError = null; return error;
    },
    createBuffer(d) { if (d.mappedAtCreation) assert.equal(d.size % 4, 0, 'mapped buffers require four-byte allocation alignment');
      return { size: d.size, data: new ArrayBuffer(d.size), getMappedRange() { return this.data; },
      unmap() {}, async mapAsync() {}, destroy() {} }; },
    createTexture(d) { const t = { label: d.label, width: d.size[0], height: d.size[1], format: d.format,
      createView(v = {}) { views.push({ texture: this, format: v.format || this.format });
        if (v.format && v.format !== this.format) assert.ok(d.viewFormats?.includes(v.format));
        return { texture: this }; }, destroy() {} }; textures.push(t); return t; },
    createBindGroupLayout: d => d, createPipelineLayout: d => d, createShaderModule: d => d,
    createRenderPipeline(d) {
      if (d.fragment?.targets.length) pipelineFormats.push(d.fragment.targets.map(t => t.format));
      if (!d.fragment?.targets.length && !d.depthStencil)
        validationError = new Error('No attachment was specified.');
      return d;
    }, createBindGroup: d => d,
    queue: { writeBuffer(b, o, bytes) { new Uint8Array(b.data).set(bytes, o); },
      submit() {}, async onSubmittedWorkDone() {} },
    createCommandEncoder() { return {
      beginRenderPass() { groups = new Map(); return { setPipeline() {}, setBindGroup(g, b) { groups.set(g, b); },
        setIndexBuffer(buffer) {
          assert.equal(buffer.size, 8);
          assert.deepEqual([...new Uint8Array(buffer.data)], [0, 0, 1, 0, 2, 0, 0, 0]);
        },
        drawIndexed() { this.draw(); },
        draw() {
          draws.push(groups.get(3).entries[0].resource.buffer);
          assert.equal(groups.get(2).entries[0].resource.buffer.size, 4352);
          assert.equal(groups.get(1).entries[0].resource.buffer.size, 3840);
        }, end() {} }; },
      copyTextureToBuffer() {}, copyTextureToTexture(src, dst, size) {
        assert.ok(src.texture.width >= size[0] && src.texture.height >= size[1]);
        assert.ok(dst.texture.width >= size[0] && dst.texture.height >= size[1]);
      }, finish() { return {}; },
    }; },
  };
  const adapter = { features: new Set(['clip-distances']), async requestDevice(d) {
    assert.ok(d.requiredFeatures.includes('clip-distances')); return device;
  } };
  const canvas = { width: 64, height: 32, getContext: () => context };
  const context = { configure() {}, unconfigure() {}, getCurrentTexture() { return { width: canvas.width, height: canvas.height }; } };
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: {
      gpu: { async requestAdapter() { return adapter; }, getPreferredCanvasFormat() { return 'bgra8unorm'; } },
  } });
  for (const [k, v] of Object.entries({ GPUBufferUsage: { UNIFORM: 1, COPY_DST: 2, VERTEX: 4, MAP_READ: 8, INDEX: 16 },
    GPUTextureUsage: { RENDER_ATTACHMENT: 1, COPY_SRC: 2, COPY_DST: 4, TEXTURE_BINDING: 8 },
    GPUShaderStage: { VERTEX: 1, FRAGMENT: 2 }, GPUMapMode: { READ: 1 } }))
    Object.defineProperty(globalThis, k, { configurable: true, value: v });
  try {
    const renderer = await createVkRenderer2(canvas, {
      translate: async (_from, _source, stage) => stage === 'vertex'
        ? 'struct Constants { data: array<vec4<f32>, 272>, }; @group(2) @binding(0) var<uniform> constants: Constants; struct Push { value: vec4<f32>, }; var<immediate> push: Push; @vertex fn main() -> @builtin(position) vec4<f32> { return push.value; }'
        : 'struct Constants { data: array<vec4<f32>, 240>, }; @group(1) @binding(0) var<uniform> constants: Constants; @fragment fn main() -> @location(0) vec4<f32> { return vec4<f32>(1.0); }',
    });
    const pipe = { pipeId: 'p', vsHash: 'v', fsHash: 'f', topology: 3, vb: [], va: [] };
    const draw = { op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0 };
    const pass = (h, value) => ({ w: 64, h, attachments: [{ imageId: 'image', samples: 1, isDepth: 0, loadOp: 1, storeOp: 0 }],
      clearColor: [0, 0, 0, 1], ops: [{ op: 'descSet', setIndex: 1, bindings: [{ binding: 0, kind: 1, obj: 'uniform', count: 1, bufOff: '0', range: 768 }] }, { op: 'descSet', setIndex: 2, bindings: [{ binding: 0, kind: 1, obj: 'uniform', count: 1, bufOff: '0', range: 768 }] }, { op: 'push', offset: 0, bytes: new Uint8Array([value]) }, draw] });
    const frame = { frameNo: 1, width: 64, height: 32,
      shaders: new Map([['v', { code: new Uint8Array(4) }], ['f', { code: new Uint8Array(4) }]]),
      images: new Map(), views: new Map(), samplers: new Map(), buffers: new Map([['uniform', new Uint8Array(768)]]), bufHashes: new Map(),
      passes: [{w:64,h:32,attachments:[],clearColor:[0,0,0,1],ops:[{op:'bindPipe',pipe}]}, pass(32, 1), pass(64, 2)] };
    frame.passes[1].attachments[0].format = 50; // BGRA sRGB write, then linear alias.
    frame.passes[2].attachments[0].format = 44;
    const result = await renderer.render(frame);
    assert.equal(result.rendered, true);
    assert.notEqual(draws[0], draws[1]);
    assert.deepEqual(draws.map(b => new Uint8Array(b.data)[0]), [1, 2]);
    assert.equal(textures.find(t => t.label === 'img:image').height, 64);
    assert.deepEqual(pipelineFormats, [['bgra8unorm-srgb'], ['bgra8unorm']]);
    const image = textures.find(t => t.label === 'img:image');
    assert.equal(views.find(v => v.format === 'bgra8unorm-srgb')?.texture, image);
    assert.equal(views.find(v => v.format === 'bgra8unorm')?.texture, image);
    // Portal also presents draws with an odd number of 16-bit indices.
    frame.buffers.set('indices', new Uint8Array([0, 0, 1, 0, 2, 0]));
    draw.indexed = true; draw.indexCount = 3;
    frame.passes[0].ops.push({ op: 'ibind', buf: 'indices', offset: 0, indexType: 0 });
    assert.equal((await renderer.render(frame)).rendered, true);
  } finally {
    for (const [k, descriptor] of saved) {
      if (descriptor) Object.defineProperty(globalThis, k, descriptor); else delete globalThis[k];
    }
  }
});
