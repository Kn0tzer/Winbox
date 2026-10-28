import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { decodeV2Frame, createVkRenderer2 } from '../../../vkwebgpu.mjs';
import { V2_REC, V2FrameQueue } from '../../../vk-protocol.mjs';

// Page-side buffer->image application (campaign section 4.33): the bridge
// executes vkCmdCopyBufferToImage eagerly into the image's guest memory,
// but the page tier's own GPUTexture for the image (a render target, or a
// texture materialised from an earlier upload) is a separate copy of the
// content. B2I_UPLOAD streams the copy to the page in command order so
// the tier applies it to that texture; without it a
// rendered-then-overwritten image reads back and samples stale.

const IMG = '00000000000000aa', BUF = '00000000000000bb';

function b2iPayload(o = {}) {
  const b = new Uint8Array(72), v = new DataView(b.buffer);
  v.setBigUint64(0, 0xaan, true); v.setBigUint64(8, 0xbbn, true); v.setBigUint64(16, BigInt(o.srcMemOff ?? 0), true);
  v.setUint32(24, o.format ?? 37, true);
  v.setUint32(28, o.mip ?? 0, true); v.setUint32(32, o.baseLayer ?? 0, true); v.setUint32(36, o.layerCount ?? 1, true);
  v.setUint32(40, o.ox ?? 0, true); v.setUint32(44, o.oy ?? 0, true); v.setUint32(48, o.oz ?? 0, true);
  v.setUint32(52, o.w ?? 4, true); v.setUint32(56, o.h ?? 4, true); v.setUint32(60, o.depth ?? 1, true);
  v.setUint32(64, o.srcRowBytes ?? 16, true); v.setUint32(68, o.srcRowsPerSlice ?? 4, true);
  return b;
}
function frameOf(chunks) {
  const header = new Uint8Array(16), end = new Uint8Array(4);
  new DataView(header.buffer).setUint32(4, 16, true); new DataView(header.buffer).setUint32(8, 16, true);
  return decodeV2Frame([{ type: V2_REC.FRAME_BEGIN, payload: header }, ...chunks, { type: V2_REC.FRAME_END, payload: end }]);
}

test('B2I_UPLOAD decodes into a bufferToImage op carrying the exact region and source layout', () => {
  assert.equal(V2_REC.B2I_UPLOAD, 0x21);
  const f = frameOf([{ type: V2_REC.B2I_UPLOAD, payload: b2iPayload({ ox: 1, oy: 2, w: 2, h: 2, srcRowBytes: 8, srcRowsPerSlice: 2 }) }]);
  assert.equal(f.passes[0].attachments.length, 0);
  assert.deepEqual(f.passes[0].ops[0], { op: 'bufferToImage', dstImage: IMG, srcBuffer: BUF, srcMemOff: '0',
    format: 37, mip: 0, baseLayer: 0, layerCount: 1, origin: [1, 2, 0], w: 2, h: 2, depth: 1, srcRowBytes: 8, srcRowsPerSlice: 2 });
});

test('B2I_UPLOAD rejects empty and out-of-range regions by name', () => {
  assert.throws(() => frameOf([{ type: V2_REC.B2I_UPLOAD, payload: b2iPayload({ w: 0 }) }]), /B2I_UPLOAD/);
  assert.throws(() => frameOf([{ type: V2_REC.B2I_UPLOAD, payload: b2iPayload({ srcRowBytes: 0 }) }]), /B2I_UPLOAD/);
  assert.throws(() => frameOf([{ type: V2_REC.B2I_UPLOAD, payload: b2iPayload({ mip: 40 }) }]), /B2I_UPLOAD/);
});

test('the bridge streams B2I copies to the page and serializes copy-only command buffers', () => {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  assert.match(bridge, /V2_B2I_UPLOAD = 0x21/);
  assert.match(bridge, /emit\(V2_B2I_UPLOAD/);
  assert.match(bridge, /CMD_COPY_B2I \|\| k\.kind == CMD_COPY_I2B/);
  assert.match(bridge, /vk64Unsupported\("b2i-page-apply"/);
  const page = readFileSync(new URL('../../../vkwebgpu.mjs', import.meta.url), 'utf8');
  assert.match(page, /case 'bufferToImage':/);
  assert.match(page, /function applyBufferToImage/);
  assert.match(page, /v2queue\.takeForPump\(\)/);
});

test('takeForPump drains in order through the first readback batch, latest-wins otherwise', () => {
  const plain = id => ({ frameNo: id, readbacks: [], bufReadbacks: [], passes: [] });
  const withRb = id => ({ frameNo: id, readbacks: [{ srcImage: IMG }], bufReadbacks: [], passes: [] });
  const q = new V2FrameQueue(10);
  assert.deepEqual(q.takeForPump(), []);
  q.push(plain(1)); q.push(plain(2));
  assert.deepEqual(q.takeForPump().map(f => f.frameNo), [2]); // latest wins; 1 dropped
  assert.equal(q.size, 0);
  q.push(plain(3)); q.push(withRb(4)); q.push(plain(5));
  assert.deepEqual(q.takeForPump().map(f => f.frameNo), [3, 4]); // producers render before the consumer
  assert.equal(q.size, 1);
  assert.deepEqual(q.takeForPump().map(f => f.frameNo), [5]);
  // Batched submissions: readbacks on any submission of the batch count.
  const batch = { frameNo: 6, submissions: [{ readbacks: [], bufReadbacks: [] }, { readbacks: [], bufReadbacks: [{ srcBuffer: BUF }] }] };
  q.push(plain(7)); q.push(batch);
  assert.deepEqual(q.takeForPump().map(f => f.frameNo), [7, 6]);
});

// Mock-device replay: a render pass creates the page's texture for the
// image (its render target); a later bufferToImage op must write into
// THAT texture, in stream order, with the op's exact region and layout.
function mockWorld() {
  const names = ['navigator', 'GPUBufferUsage', 'GPUTextureUsage', 'GPUShaderStage', 'GPUMapMode'];
  const saved = new Map(names.map(k => [k, Object.getOwnPropertyDescriptor(globalThis, k)]));
  const textures = [], buffers = [], copies = [];
  const device = {
    lost: new Promise(() => {}), features: new Set(), addEventListener() {}, pushErrorScope() {}, async popErrorScope() { return null; },
    createBuffer(d) { const b = { size: d.size, usage: d.usage, data: new ArrayBuffer(d.size), getMappedRange() { return this.data; }, unmap() {}, async mapAsync() {}, destroy() {} }; buffers.push(b); return b; },
    createTexture(d) { const t = { label: d.label, width: d.size[0], height: d.size[1], depthOrArrayLayers: d.size[2] || 1,
      mipLevelCount: d.mipLevelCount || 1, format: d.format, usage: d.usage, dimension: d.dimension || '2d',
      createView() { return { texture: this }; }, destroy() {} }; textures.push(t); return t; },
    createCommandEncoder() { return {
      beginRenderPass() { return { setPipeline() {}, setBindGroup() {}, setVertexBuffer() {}, setIndexBuffer() {}, draw() {}, drawIndexed() {}, end() {} }; },
      copyBufferToTexture(src, dst, size) { copies.push({ src, dst, size }); },
      copyTextureToBuffer() {}, copyTextureToTexture() {}, finish() { return {}; },
    }; },
    queue: { submit() {}, writeBuffer() {}, writeTexture() {}, async onSubmittedWorkDone() {} },
  };
  const context = { configure() {}, unconfigure() {}, getCurrentTexture() { return { width: 4, height: 4, createView() { return {}; } }; } };
  Object.defineProperty(globalThis, 'navigator', { configurable: true, value: { gpu: {
    async requestAdapter() { return { features: new Set(), async requestDevice() { return device; } }; },
    getPreferredCanvasFormat() { return 'rgba8unorm'; },
  } } });
  for (const [k, v] of Object.entries({ GPUBufferUsage: { VERTEX: 1, COPY_DST: 2, MAP_READ: 4, COPY_SRC: 8, UNIFORM: 16, STORAGE: 32, INDEX: 64 },
    GPUTextureUsage: { RENDER_ATTACHMENT: 1, COPY_SRC: 2, COPY_DST: 4, TEXTURE_BINDING: 8, STORAGE_BINDING: 16 },
    GPUShaderStage: { VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 }, GPUMapMode: { READ: 1 } }))
    Object.defineProperty(globalThis, k, { configurable: true, value: v });
  const restore = () => { for (const [k, d] of saved) { if (d) Object.defineProperty(globalThis, k, d); else delete globalThis[k]; } };
  return { textures, buffers, copies, context, restore };
}

const baseFrame = (over = {}) => ({ frameNo: 1, width: 4, height: 4, shaders: new Map(), images: new Map(),
  views: new Map(), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), passes: [], ...over });
const imageRec = { w: 4, h: 4, d: 1, layers: 1, mips: 1, format: 37 };

async function rendererWithTarget(w) {
  const renderer = await createVkRenderer2({ width: 4, height: 4, getContext: () => w.context });
  await renderer.render(baseFrame({ images: new Map([[IMG, imageRec]]),
    passes: [{ w: 4, h: 4, attachments: [{ imageId: IMG, samples: 1, isDepth: 0, loadOp: 1, storeOp: 0 }], clearColor: [1, 0, 1, 1], ops: [] }] }), { present: false });
  return renderer;
}

test('a buffer->image upload lands on the page texture that holds the rendered content', async () => {
  const w = mockWorld();
  try {
    const renderer = await rendererWithTarget(w);
    const target = w.textures.find(t => t.label === 'img:' + IMG);
    assert.ok(target, 'render pass created the image target');
    const pattern = new Uint8Array(4 * 4 * 4); for (let i = 0; i < pattern.length; i++) pattern[i] = i;
    const op = { op: 'bufferToImage', dstImage: IMG, srcBuffer: BUF, srcMemOff: '0', format: 37, mip: 0,
      baseLayer: 0, layerCount: 1, origin: [0, 0, 0], w: 4, h: 4, depth: 1, srcRowBytes: 16, srcRowsPerSlice: 4 };
    await renderer.render(baseFrame({ buffers: new Map([[BUF + ':0', pattern]]),
      passes: [{ w: 4, h: 4, attachments: [], ops: [op] }] }), { present: false });
    assert.equal(w.copies.length, 1);
    const c = w.copies[0];
    assert.equal(c.dst.texture, target);
    assert.equal(c.dst.mipLevel, 0); assert.deepEqual(c.dst.origin, { x: 0, y: 0, z: 0 });
    assert.deepEqual(c.size, { width: 4, height: 4, depthOrArrayLayers: 1 });
    assert.equal(c.src.bytesPerRow, 16); assert.equal(c.src.rowsPerImage, 4);
    assert.deepEqual(new Uint8Array(c.src.buffer.data), pattern);
  } finally { w.restore(); }
});

test('a partial-region upload keeps its origin and footprint', async () => {
  const w = mockWorld();
  try {
    const renderer = await rendererWithTarget(w);
    const pattern = new Uint8Array(2 * 2 * 4); for (let i = 0; i < pattern.length; i++) pattern[i] = 200 - i;
    const op = { op: 'bufferToImage', dstImage: IMG, srcBuffer: BUF, srcMemOff: '0', format: 37, mip: 0,
      baseLayer: 0, layerCount: 1, origin: [1, 1, 0], w: 2, h: 2, depth: 1, srcRowBytes: 8, srcRowsPerSlice: 2 };
    await renderer.render(baseFrame({ buffers: new Map([[BUF + ':0', pattern]]),
      passes: [{ w: 4, h: 4, attachments: [], ops: [op] }] }), { present: false });
    assert.equal(w.copies.length, 1);
    assert.deepEqual(w.copies[0].dst.origin, { x: 1, y: 1, z: 0 });
    assert.deepEqual(w.copies[0].size, { width: 2, height: 2, depthOrArrayLayers: 1 });
    assert.equal(w.copies[0].src.bytesPerRow, 8);
  } finally { w.restore(); }
});

test('unservable uploads decline without a copy: depth format, missing source bytes, no page texture', async () => {
  const w = mockWorld();
  try {
    const renderer = await rendererWithTarget(w);
    const pattern = new Uint8Array(4 * 4 * 4);
    const mk = over => ({ op: 'bufferToImage', dstImage: IMG, srcBuffer: BUF, srcMemOff: '0', format: 37, mip: 0,
      baseLayer: 0, layerCount: 1, origin: [0, 0, 0], w: 4, h: 4, depth: 1, srcRowBytes: 16, srcRowsPerSlice: 4, ...over });
    // Depth-format upload (126 = VK_FORMAT_D32_SFLOAT): named decline.
    await renderer.render(baseFrame({ buffers: new Map([[BUF + ':0', pattern]]),
      passes: [{ w: 4, h: 4, attachments: [], ops: [mk({ format: 126 })] }] }), { present: false });
    // Source bytes absent from the frame: named decline.
    await renderer.render(baseFrame({ passes: [{ w: 4, h: 4, attachments: [], ops: [mk({})] }] }), { present: false });
    // Image the page has never materialised: guest memory already holds
    // the content; nothing to apply, and no copy may be invented.
    await renderer.render(baseFrame({ buffers: new Map([[BUF + ':0', pattern]]),
      passes: [{ w: 4, h: 4, attachments: [], ops: [mk({ dstImage: '00000000000000cc' })] }] }), { present: false });
    assert.equal(w.copies.length, 0);
  } finally { w.restore(); }
});
