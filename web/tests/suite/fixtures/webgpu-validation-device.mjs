import assert from 'node:assert/strict';
export function createValidationDevice() {
  const saved = new Map(['navigator', 'GPUTextureUsage', 'GPUBufferUsage', 'GPUMapMode', 'GPUShaderStage'].map(k => [k, Object.getOwnPropertyDescriptor(globalThis, k)]));
  const pipelines = [], bound = [], writes = [], textures = [];
  const texture = d => ({ ...d, width: d.size[0], height: d.size[1], createView(options = {}) { return { texture: this, dimension: options.dimension || '2d' }; }, destroy() { this.destroyed=true; } });
  const device = {
    addEventListener() {}, lost: new Promise(() => {}), pushErrorScope() {}, async popErrorScope() { return null; },
    createTexture(d) { const t=texture(d);textures.push(t);return t; },
    createBuffer(d) { const bytes = new ArrayBuffer(d.size); return { ...d, getMappedRange: () => bytes, async mapAsync() {}, unmap() {}, destroy() {} }; },
    createBindGroupLayout: d => d, createPipelineLayout: d => d, createShaderModule: d => d,
    createBindGroup(d) { for(const e of d.entries) { const l=d.layout.entries.find(l=>l.binding===e.binding);if(l.texture) assert.equal(e.resource.dimension,l.texture.viewDimension); }return d; },
    createRenderPipeline(d) { pipelines.push(d); return d; },
    createCommandEncoder() { return {
      beginRenderPass(d) { let currentPipeline, indexFormat; return {
        setPipeline(p) {
          assert.equal(!!p.depthStencil, !!d.depthStencilAttachment, 'pipeline must match the current pass depth attachment');
          const view = d.colorAttachments[0]?.view || d.depthStencilAttachment.view;
          assert.equal(p.multisample.count, view.texture.sampleCount || 1, 'pipeline must match the current pass sample count');
          assert.equal(p.fragment.targets.length, d.colorAttachments.length, 'pipeline must match the current pass color attachments');
          currentPipeline = p; bound.push(p);
        }, draw() {}, setBindGroup() {}, setIndexBuffer(buffer, format) { indexFormat = format; },
        drawIndexed() { if (currentPipeline.primitive.topology.endsWith('strip')) assert.equal(currentPipeline.primitive.stripIndexFormat, indexFormat); }, end() {},
      }; }, copyTextureToBuffer() {}, copyTextureToTexture() {}, finish() { return {}; },
    }; }, queue: { submit() {}, writeTexture(d,data,layout,size) { writes.push({d,data:new Uint8Array(data),layout,size}); }, async onSubmittedWorkDone() {} }, destroy() {},
  };
  const canvas = { width: 4, height: 4, getContext: () => ({ configure() {}, unconfigure() {}, getCurrentTexture: () => texture({ size: [canvas.width, canvas.height] }) }) };
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: { gpu: {
    async requestAdapter() { return { features: new Set(), async requestDevice() { return device; } }; }, getPreferredCanvasFormat: () => 'rgba8unorm',
  } } });
  for (const k of ['GPUTextureUsage', 'GPUBufferUsage', 'GPUMapMode', 'GPUShaderStage']) Object.defineProperty(globalThis, k, { configurable: true, value: new Proxy({}, { get: () => 1 }) });
  return {device,canvas,pipelines,bound,writes,textures,restore() {for (const [k, d] of saved) { if (d) Object.defineProperty(globalThis, k, d); else delete globalThis[k]; }}};
}
