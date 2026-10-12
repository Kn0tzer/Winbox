import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';

test('replayed Vulkan pipelines match each pass depth attachment and sample count', async () => {
  const saved = new Map(['navigator', 'GPUTextureUsage', 'GPUBufferUsage', 'GPUMapMode', 'GPUShaderStage'].map(k => [k, Object.getOwnPropertyDescriptor(globalThis, k)]));
  const pipelines = [], bound = [];
  const texture = d => ({ ...d, width: d.size[0], height: d.size[1], createView() { return { texture: this }; }, destroy() {} });
  const device = {
    addEventListener() {}, lost: new Promise(() => {}), pushErrorScope() {}, async popErrorScope() { return null; },
    createTexture: texture,
    createBuffer(d) { const bytes = new ArrayBuffer(d.size); return { ...d, getMappedRange: () => bytes, async mapAsync() {}, unmap() {}, destroy() {} }; },
    createBindGroupLayout: d => d, createPipelineLayout: d => d, createShaderModule: d => d,
    createRenderPipeline(d) { pipelines.push(d); return d; },
    createCommandEncoder() { return {
      beginRenderPass(d) { return {
        setPipeline(p) {
          assert.equal(!!p.depthStencil, !!d.depthStencilAttachment, 'pipeline must match the current pass depth attachment');
          const view = d.colorAttachments[0]?.view || d.depthStencilAttachment.view;
          assert.equal(p.multisample.count, view.texture.sampleCount || 1, 'pipeline must match the current pass sample count');
          bound.push(p);
        }, draw() {}, end() {},
      }; }, copyTextureToBuffer() {}, copyTextureToTexture() {}, finish() { return {}; },
    }; }, queue: { submit() {}, async onSubmittedWorkDone() {} }, destroy() {},
  };
  const canvas = { width: 4, height: 4, getContext: () => ({ configure() {}, unconfigure() {}, getCurrentTexture: () => texture({ size: [4, 4] }) }) };
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: { gpu: {
    async requestAdapter() { return { features: new Set(), async requestDevice() { return device; } }; }, getPreferredCanvasFormat: () => 'rgba8unorm',
  } } });
  for (const k of ['GPUTextureUsage', 'GPUBufferUsage', 'GPUMapMode', 'GPUShaderStage']) Object.defineProperty(globalThis, k, { configurable: true, value: new Proxy({}, { get: () => 1 }) });
  try {
    const r = await createVkRenderer2(canvas, { translate: async () => 'fn main() {}' });
    const pipe = { pipeId: 'pipe', topology: 3, vb: [], va: [], vsHash: 'vs', fsHash: 'fs', depthTest: true, depthWrite: true, depthOp: 3 };
    const draw = { op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0 };
    const pass = (depth, samples, ops) => ({ w: 4, h: 4, clearColor: [0, 0, 0, 1], clearDepth: 1, ops,
      attachments: [{ imageId: null, isDepth: false, samples, loadOp: 1, storeOp: 0 }, ...(depth ? [{ imageId: null, isDepth: true, samples }] : [])] });
    const frame = { frameNo: 1, width: 4, height: 4, shaders: new Map([['vs', { code: new Uint8Array(4) }], ['fs', { code: new Uint8Array(4) }]]),
      images: new Map(), views: new Map(), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
      passes: [pass(true, 4, [{ op: 'bindPipe', pipe }, draw]), pass(false, 1, [draw]), pass(true, 1, [draw])] };
    assert.equal((await r.render(frame)).rendered, true);
    assert.equal(bound.length, 3);assert.deepEqual(pipelines.map(p => [!!p.depthStencil, p.multisample.count]), [[true, 4], [false, 1], [true, 1]]);
    r.destroy();
  } finally { for (const [k, d] of saved) { if (d) Object.defineProperty(globalThis, k, d); else delete globalThis[k]; } }
});
