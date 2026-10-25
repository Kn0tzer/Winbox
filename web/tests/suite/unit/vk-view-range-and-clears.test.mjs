// Register evidence (docs/known-gaps/vulkan.md "image views" and "render pass clears"):
//  * IMAGE_DATA carries the view's real subresource range/type, and the replay samples exactly that range.
//  * RP_BEGIN carries one clear colour per attachment (CLR1), and the replay clears each target with its own value.
import test from 'node:test';
import assert from 'node:assert/strict';
import {readFileSync} from 'node:fs';
import {decodeV2Frame, V2_REC} from '../../../vk-protocol.mjs';
import {createVkRenderer2} from '../../../vkwebgpu.mjs';
import {createValidationDevice} from '../fixtures/webgpu-validation-device.mjs';

class W {
  constructor() { this.parts = []; }
  u32(v) { const b = new Uint8Array(4); new DataView(b.buffer).setUint32(0, v, true); this.parts.push(b); return this; }
  u64(v) { const b = new Uint8Array(8); new DataView(b.buffer).setBigUint64(0, BigInt(v), true); this.parts.push(b); return this; }
  f32(v) { const b = new Uint8Array(4); new DataView(b.buffer).setFloat32(0, v, true); this.parts.push(b); return this; }
  done() { const n = this.parts.reduce((a, p) => a + p.length, 0), out = new Uint8Array(n); let o = 0; for (const p of this.parts) { out.set(p, o); o += p.length; } return out; }
}
const rec = (type, w) => ({type, payload: w.done()});
const frameOf = middle => [rec(V2_REC.FRAME_BEGIN, new W().u32(1).u32(8).u32(8).u32(0)), ...middle, rec(V2_REC.FRAME_END, new W().u32(1))];

test('clear replay guards keep Vulkan command preconditions explicit',()=>{
  const source=readFileSync(new URL('../../../../web/vkwebgpu.mjs',import.meta.url),'utf8');
  const bridge=readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp',import.meta.url),'utf8');
  assert.match(source,/case 'clearAttachments': \{\s*if\(!passEnc\) \{noteUnsupported\('clear-attachments-outside-pass'\)/);
  assert.match(source,/case 'imageClearDS': \{[\s\S]*?if \(passEnc\) \{ noteUnsupported\('clear-ds-inside-pass'\)/);
  assert.match(source,/if\(\(\(aspect&1\)&&attachment\.colorAttachment>=passColorFormats\.length\)/);
  assert.equal((bridge.match(/vk64Unsupported\("layered-framebuffer"/g)||[]).length,2);
  assert.equal((bridge.match(/if \(ci\.layers > 1\) \{[\s\S]{0,220}VK_ERROR_FEATURE_NOT_PRESENT;/g)||[]).length,2,
    'both guest ABIs must reject layered framebuffers before recording clear rectangles');
});

test('IMAGE_DATA view-range tail is decoded; older records without it still decode', () => {
  const image = (tail) => {
    const w = new W().u64(0x11).u64(0x22).u32(4).u32(4).u32(37).u64(0).u32(0).u32(1).u32(6).u32(5);
    if (tail) w.u32(2).u32(3).u32(1).u32(4).u32(5);
    return rec(V2_REC.IMAGE_DATA, w);
  };
  const withTail = decodeV2Frame(frameOf([image(true)]));
  assert.deepEqual(withTail.viewRanges.get('0000000000000011'), {baseMip: 2, levelCount: 3, baseLayer: 1, layerCount: 4, viewType: 5});
  const without = decodeV2Frame(frameOf([image(false)]));
  assert.equal(without.viewRanges.size, 0);
  assert.equal(without.images.get('0000000000000022').layers, 6);
});

test('RP_BEGIN CLR1 tail gives each attachment its own clear colour', () => {
  const w = new W().u64(1).u64(2).u32(8).u32(8).u32(2);
  for (let i = 0; i < 2; i++) w.u32(37).u32(1).u32(0).u32(1).u32(0);
  w.f32(1).f32(0).f32(0).f32(1).f32(1).u32(0);
  w.u64(10).u64(20).u64(11).u64(21);
  w.u32(0x31505453).u32(2); for (let i = 0; i < 2; i++) w.u32(0).u32(0).u32(1);
  w.u32(0x31524c43).u32(2).f32(1).f32(0).f32(0).f32(1).f32(0).f32(0.5).f32(1).f32(0.25);
  const frame = decodeV2Frame(frameOf([rec(V2_REC.RP_BEGIN, w), rec(V2_REC.RP_END, new W())]));
  assert.deepEqual(frame.passes[0].attachments.map(a => a.clearColor), [[1, 0, 0, 1], [0, 0.5, 1, 0.25]]);
});

test('malformed CLR1 tail is rejected, not skipped', () => {
  const w = new W().u64(1).u64(2).u32(8).u32(8).u32(1).u32(37).u32(1).u32(0).u32(1).u32(0);
  w.f32(1).f32(0).f32(0).f32(1).f32(1).u32(0).u64(10).u64(20).u32(0x31505453).u32(1).u32(0).u32(0).u32(1);
  w.u32(0x31524c43).u32(3);
  assert.throws(() => decodeV2Frame(frameOf([rec(V2_REC.RP_BEGIN, w)])), /invalid clear tail/);
});

const shaders = () => new Map([['vs', {code: new Uint8Array(4)}], ['fs', {code: new Uint8Array(4)}]]);
async function replay(frame) {
  const gpu = createValidationDevice();
  try {
    const r = await createVkRenderer2(gpu.canvas, {translate: async () => 'fn main() {}'});
    await r.render(frame);
    r.destroy();
    return gpu;
  } finally { gpu.restore(); }
}

test('replay clears every colour target with its own value', async () => {
  const att = (imageId, clearColor) => ({format: 37, isDepth: 0, samples: 1, loadOp: 1, storeOp: 0, imageId, viewId: imageId, clearColor});
  const frame = {frameNo: 1, width: 4, height: 4, shaders: shaders(), images: new Map(), views: new Map(), viewRanges: new Map(), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
    passes: [{w: 4, h: 4, clearColor: [9, 9, 9, 9], clearDepth: 1, ops: [], attachments: [att('a', [1, 0, 0, 1]), att('b', [0, 0.5, 1, 0.25])]}]};
  const gpu = await replay(frame);
  const clears = gpu.passes[0].colorAttachments.map(a => a.clearValue);
  assert.deepEqual(clears, [{r: 1, g: 0, b: 0, a: 1}, {r: 0, g: 0.5, b: 1, a: 0.25}]);
});

test('replay falls back to the pass clear colour when the attachment has none (older bridges)', async () => {
  const frame = {frameNo: 1, width: 4, height: 4, shaders: shaders(), images: new Map(), views: new Map(), viewRanges: new Map(), samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
    passes: [{w: 4, h: 4, clearColor: [0.5, 0.5, 0.5, 1], clearDepth: 1, ops: [], attachments: [{format: 37, isDepth: 0, samples: 1, loadOp: 1, storeOp: 0}]}]};
  const gpu = await replay(frame);
  assert.deepEqual(gpu.passes[0].colorAttachments[0].clearValue, {r: 0.5, g: 0.5, b: 0.5, a: 1});
});

test('sampled views honour the view subresource range (base mip/layer, counts)', async () => {
  const gpu = createValidationDevice();
  try {
    const groups = [], create = gpu.device.createBindGroup;
    gpu.device.createBindGroup = d => { groups.push(d); return create.call(gpu.device, d); };
    const r = await createVkRenderer2(gpu.canvas, {translate: async (from, bytes, stage) => stage === 'fragment' ? '@group(0) @binding(0) var image: texture_2d_array<f32>; fn main() {}' : 'fn main() {}'});
    const pipe = {pipeId: 'arr', topology: 3, vb: [], va: [], vsHash: 'vs', fsHash: 'fs'};
    const pixels = new Uint8Array(4 * (16 + 4 + 1) * 4);
    const frame = {frameNo: 1, width: 4, height: 4, shaders: shaders(), images: new Map([['ai', {w: 4, h: 4, mips: 3, layers: 4, format: 37, hash: 'arr', pixels}]]), views: new Map([['av', 'ai']]),
      viewRanges: new Map([['av', {baseMip: 1, levelCount: 2, baseLayer: 1, layerCount: 2, viewType: 5}]]),
      samplers: new Map(), buffers: new Map(), bufHashes: new Map(), readbacks: [],
      passes: [{w: 4, h: 4, clearColor: [0, 0, 0, 1], clearDepth: 1, attachments: [{format: 37, imageId: null, isDepth: 0, samples: 1, loadOp: 1, storeOp: 0}],
        ops: [{op: 'bindPipe', pipe}, {op: 'descSet', setIndex: 0, bindings: [{binding: 0, kind: 2, obj: 'av', dstArrayElement: 0, count: 1}]}, {op: 'draw', vertexCount: 3, instanceCount: 1, firstVertex: 0, firstInstance: 0}]}]};
    assert.equal((await r.render(frame)).rendered, true);
    const views = groups.flatMap(g => g.entries.map(e => e.resource?.options).filter(Boolean));
    assert.ok(views.some(o => o.baseMipLevel === 1 && o.mipLevelCount === 2 && o.baseArrayLayer === 1 && o.arrayLayerCount === 2), JSON.stringify(views));
    r.destroy();
  } finally { gpu.restore(); }
});
