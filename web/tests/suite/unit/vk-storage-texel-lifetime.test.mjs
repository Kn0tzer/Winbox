// web/tests/suite/unit/vk-storage-texel-lifetime.test.mjs — storage texel
// buffer lifetime rules (metabindfix lane, campaign section 4.32;
// frontier review findings 1, 2, 3, 6).
//
// The metabind serving path (section 4.31) materialised a kind-6 view
// as a storage texture keyed by the hash of its seed bytes and kept a
// readback registry with no lifetime rule. The review convicted three
// silent-wrong-data shapes, none of which a run-each-case-once probe
// (cs12) can catch:
//   1. a guest re-upload of the original bytes cache-hit the texture
//      still holding earlier GPU writes (and a CPU-side byte change
//      reseeded over GPU writes) -- the content hash is not an identity;
//   2. the readback registry took the FIRST covering view (two views
//      over one buffer served the wrong texture, byte-exact wrongness)
//      and had no freshness or ambiguity rule;
//   3. a raw storage binding of the same buffer outranked the texel
//      texture at readback, so texel writes were lost when the buffer
//      was also bound raw.
// These tests replay whole submission batches against a mock device
// whose storage textures are byte stores and whose dispatch applies a
// scripted shader write, and assert what lands in the guest heap --
// including the SECOND and THIRD uses cs12 never exercises.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createVkRenderer2 } from '../../../vkwebgpu.mjs';

const hex = (n) => n.toString(16).padStart(16, '0');
const BUF = hex(0x17), VIEW1 = hex(0x2a), VIEW2 = hex(0x2b);
const KEY = BUF + ':0'; // BUFFER_DATA key: bufId + ':' + memOff(decimal)
const pat = (v, n = 32) => Uint8Array.from({ length: n }, (_, i) => (v + i) & 0xff);
const HASH_A = hex(0xa1), HASH_B = hex(0xb2);

// A minimal valid storage-texel compute module (OpLoad + OpImageWrite),
// hand-built in the spirvfix fixture idiom: the page tier runs its real
// shader pipeline over these bytes; only the WGSL translation is stubbed.
function spvWords(insns) {
  const words = [0x07230203, 0x00010000, 0, 32, 0];
  for (const [op, ...operands] of insns) words.push(((operands.length + 1) << 16) | op, ...operands);
  const b = new Uint8Array(words.length * 4);
  const d = new DataView(b.buffer);
  words.forEach((w, i) => d.setUint32(i * 4, w, true));
  return b;
}
const CS_BYTES = spvWords([
  [17, 1], [17, 47], [14, 0, 1], [15, 5, 15, 0x6e69616d, 0], [16, 15, 17, 1, 1, 1],
  [71, 5, 34, 0], [71, 5, 33, 0],
  [21, 1, 32, 0], [21, 2, 32, 1], [25, 3, 1, 5, 0, 0, 0, 2, 34], [32, 4, 0, 3],
  [19, 7], [33, 8, 7], [24, 9, 1, 4], [59, 4, 5, 0],
  [43, 2, 10, 0], [43, 1, 11, 1], [44, 9, 14, 11, 11, 11, 11],
  [54, 7, 15, 0, 8], [248, 16], [61, 3, 17, 5], [99, 17, 10, 14], [253], [56],
]);

const WGSL_TEX = (fmt) =>
  `@group(0) @binding(0) var dst: texture_storage_2d<${fmt}, write>; ` +
  '@compute @workgroup_size(1) fn main() { textureStore(dst, vec2<i32>(0, 0), vec4<u32>(0u, 0u, 0u, 0u)); }';
const WGSL_TEX2 =
  '@group(0) @binding(0) var dst0: texture_storage_2d<rg32uint, write>; ' +
  '@group(0) @binding(1) var dst1: texture_storage_2d<r32uint, write>; ' +
  '@compute @workgroup_size(1) fn main() { textureStore(dst0, vec2<i32>(0, 0), vec4<u32>(0u)); textureStore(dst1, vec2<i32>(0, 0), vec4<u32>(0u)); }';
const WGSL_TEX_SSBO =
  '@group(0) @binding(0) var dst: texture_storage_2d<rg32uint, write>; ' +
  '@group(0) @binding(1) var<storage, read_write> ssbo: array<u32>; ' +
  '@compute @workgroup_size(1) fn main() { textureStore(dst, vec2<i32>(0, 0), vec4<u32>(0u)); ssbo[0] = 0u; }';

const BPP = { rg32uint: 8, r32uint: 4, rgba32uint: 16, r16float: 2, r8uint: 1 };

function makeHarness({ maxDim = 8192 } = {}) {
  const createdTextures = [];
  let onDispatch = () => {};
  let wgsl = WGSL_TEX('rg32uint');
  const device = {
    features: new Set(),
    limits: { maxTextureDimension2D: maxDim, maxBufferSize: 268435456, maxBindGroups: 4 },
    lost: new Promise(() => {}), addEventListener() {},
    pushErrorScope() {}, async popErrorScope() { return null; },
    createBuffer(d) {
      return { size: d.size, data: new ArrayBuffer(d.size),
        getMappedRange() { return this.data; }, unmap() {},
        async mapAsync() {}, destroy() {} };
    },
    createTexture(d) {
      const t = { width: d.size[0], height: d.size[1], format: d.format, destroyed: false,
        store: new Uint8Array(d.size[0] * d.size[1] * (BPP[d.format] || 4)),
        createView() { return { texture: this }; }, destroy() { this.destroyed = true; } };
      createdTextures.push(t);
      return t;
    },
    createBindGroupLayout: (d) => d, createPipelineLayout: (d) => d, createShaderModule: (d) => d,
    createComputePipeline: (d) => d, createRenderPipeline: (d) => d, createBindGroup: (d) => d,
    queue: {
      writeBuffer(b, off, bytes) { new Uint8Array(b.data).set(bytes, off); },
      writeTexture({ texture }, data, layout, size) {
        const bpp = BPP[texture.format] || 4, w = Array.isArray(size) ? size[0] : size.width,
          h = Array.isArray(size) ? size[1] : size.height;
        for (let y = 0; y < h; y++)
          texture.store.set(data.subarray(y * layout.bytesPerRow, y * layout.bytesPerRow + w * bpp), y * texture.width * bpp);
      },
      submit() {}, async onSubmittedWorkDone() {},
    },
    createCommandEncoder() {
      return {
        beginComputePass() {
          const groups = new Map();
          return { setPipeline() {}, setBindGroup(g, bg) { groups.set(g, bg); },
            dispatchWorkgroups() {
              for (const bg of groups.values())
                for (const e of bg.entries || [])
                  if (e.resource && e.resource.texture) onDispatch(e.resource.texture);
            }, end() {} };
        },
        beginRenderPass() { return { setPipeline() {}, setBindGroup() {}, draw() {}, end() {} }; },
        copyTextureToBuffer(src, dst, size) {
          const tex = src.texture, bpp = BPP[tex.format] || 4, w = size[0], rows = size[1];
          const out = new Uint8Array(dst.buffer.data);
          for (let y = 0; y < rows; y++)
            out.set(tex.store.subarray(y * tex.width * bpp, y * tex.width * bpp + w * bpp), y * dst.bytesPerRow);
        },
        copyBufferToBuffer(src, so, dst, doff, n) {
          new Uint8Array(dst.data).set(new Uint8Array(src.data).subarray(so, so + n), doff);
        },
        finish() { return {}; },
      };
    },
  };
  const limits = device.limits;
  const adapter = { features: new Set(), limits, async requestDevice() { return device; } };
  const context = { configure() {}, unconfigure() {},
    getCurrentTexture() { return { width: 64, height: 64, createView: () => ({}) }; } };
  const canvas = { width: 64, height: 64, getContext: () => context };
  const saved = new Map(['navigator', 'GPUBufferUsage', 'GPUTextureUsage', 'GPUShaderStage', 'GPUMapMode']
    .map(k => [k, Object.getOwnPropertyDescriptor(globalThis, k)]));
  Object.defineProperty(globalThis, 'navigator', { configurable: true,
    value: { gpu: { async requestAdapter() { return adapter; }, getPreferredCanvasFormat() { return 'bgra8unorm'; } } } });
  for (const [k, v] of Object.entries({
    GPUBufferUsage: { MAP_READ: 1, MAP_WRITE: 2, COPY_SRC: 4, COPY_DST: 8, INDEX: 16, VERTEX: 32, UNIFORM: 64, STORAGE: 128, INDIRECT: 256, QUERY_RESOLVE: 512 },
    GPUTextureUsage: { COPY_SRC: 1, COPY_DST: 2, TEXTURE_BINDING: 4, STORAGE_BINDING: 8, RENDER_ATTACHMENT: 16 },
    GPUShaderStage: { VERTEX: 1, FRAGMENT: 2, COMPUTE: 4 }, GPUMapMode: { READ: 1, WRITE: 2 },
  })) Object.defineProperty(globalThis, k, { configurable: true, value: v });
  const heap = new SharedArrayBuffer(8192);
  globalThis.bwVkHeapBuffer = heap;
  return {
    createdTextures, heap,
    set onDispatch(fn) { onDispatch = fn; },
    set wgsl(v) { wgsl = v; },
    async renderer() {
      return createVkRenderer2(canvas, { translate: async () => wgsl });
    },
    heapBytes(off, n) { return [...new Uint8Array(heap, off, n)]; },
    fillHeap(v) { new Uint8Array(heap).fill(v); },
    restore() {
      for (const [k, descriptor] of saved) {
        if (descriptor) Object.defineProperty(globalThis, k, descriptor); else delete globalThis[k];
      }
      delete globalThis.bwVkHeapBuffer;
    },
  };
}

const k6 = (binding, obj) => ({ binding, type: 5, dstArrayElement: 0, count: 1, kind: 6, obj, range: '32', bufOff: '0' });
function frame({ frameNo, bytes = null, hash = null, views = new Map(), bindings = null, readback = null, csHash = hex(0xc5) }) {
  const ops = [];
  if (bindings) ops.push({ op: 'descSet', setIndex: 0, layoutId: '0', bindings },
    { op: 'bindComputePipe', cpipe: { pipeId: hex(0xaa), csHash, csSpec: [] } },
    { op: 'dispatch', x: 1, y: 1, z: 1 });
  return {
    frameNo, width: 64, height: 64,
    passes: [{ rpId: '0', fbId: '0', w: 64, h: 64, attachments: [], clearColor: [0, 0, 0, 1], clearDepth: 1, clearStencil: 0, ops }],
    shaders: new Map([[csHash, { code: CS_BYTES, stage: 2 }]]),
    pipelines: new Map(), images: new Map(), views: new Map(), samplers: new Map(),
    buffers: bytes ? new Map([[KEY, bytes]]) : new Map(),
    bufHashes: hash ? new Map([[KEY, hash]]) : new Map(),
    bufferViews: views, viewRanges: new Map(), viewFormats: new Map(),
    readbacks: [], computePipelines: new Map(),
    bufReadbacks: readback ? [readback] : [],
  };
}
const rb = (off, size, heapPtr = 0) => ({ srcBuffer: BUF, srcAbsOff: BigInt(off), size: BigInt(size), heapPtr, flagPtr: 4096 });
const view = (id, format, len) => [id, { bufferId: BUF, format, absOff: '0', len: String(len) }];

test('findings 1+2: repeat binds, re-uploads and CPU writes follow stream order, not the seed hash', async () => {
  const h = makeHarness();
  try {
    const renderer = await h.renderer();
    const views = new Map([view(VIEW1, 101, 32)]);
    const seed = pat(0x10);
    // Batch 1: seed + dispatch-write + readback (the cs12 shape).
    h.onDispatch = (tex) => tex.store.set(pat(0xa0), 0);
    await renderer.render(frame({ frameNo: 1, bytes: seed, hash: HASH_A, views, bindings: [k6(0, VIEW1)], readback: rb(0, 32) }));
    assert.deepEqual(h.heapBytes(0, 32), [...pat(0xa0)], 'batch 1: the dispatch write reads back');
    // Batch 2: the meta clear AGAIN. The bind group comes from the
    // fingerprint cache (bindGroupEntries never runs); the readback
    // must still resolve and serve the new write, not zeros.
    h.onDispatch = (tex) => tex.store.set(pat(0xb0), 0);
    await renderer.render(frame({ frameNo: 2, bytes: seed, hash: HASH_A, views, bindings: [k6(0, VIEW1)], readback: rb(0, 32) }));
    assert.deepEqual(h.heapBytes(0, 32), [...pat(0xb0)], 'batch 2: a cache-hit repeat bind still registers and serves');
    // Batch 3 (finding 1, failure A): the guest re-uploads the ORIGINAL
    // bytes. The dispatch must observe the re-uploaded bytes, not the
    // texture the earlier dispatches wrote into. The mock shader reads
    // the texture: seed content -> 0xc1 pattern, stale GPU content -> 0xc2.
    h.onDispatch = (tex) => tex.store.set(pat(tex.store[0] === seed[0] ? 0xc1 : 0xc2), 0);
    await renderer.render(frame({ frameNo: 3, bytes: seed, hash: HASH_A, views, bindings: [k6(0, VIEW1)], readback: rb(0, 32) }));
    assert.deepEqual(h.heapBytes(0, 32), [...pat(0xc1)], 'batch 3: a re-upload in a later batch overwrites GPU writes');
    // Batch 4 (finding 1, failure B): a CPU-side write changes the
    // bytes; the new content is the seed, the dispatch writes over it.
    h.onDispatch = (tex) => tex.store.set(pat(0xd0), 0);
    await renderer.render(frame({ frameNo: 4, bytes: pat(0x30), hash: HASH_B, views, bindings: [k6(0, VIEW1)], readback: rb(0, 32) }));
    assert.deepEqual(h.heapBytes(0, 32), [...pat(0xd0)], 'batch 4: changed guest bytes reseed, then the dispatch write reads back');
    // Batch 5: no bind at all -- a copy deferred into a later
    // submission than the dispatch. The GPU truth must persist.
    await renderer.render(frame({ frameNo: 5, readback: rb(0, 32) }));
    assert.deepEqual(h.heapBytes(0, 32), [...pat(0xd0)], 'batch 5: a cross-batch deferred readback still resolves');
  } finally { h.restore(); }
});

test('finding 2: the freshest covering view wins; a same-batch tie declines loudly', async () => {
  const h = makeHarness();
  try {
    const renderer = await h.renderer();
    const seed = pat(0x10);
    const views12 = new Map([view(VIEW1, 101, 32), view(VIEW2, 98, 32)]);
    h.onDispatch = (tex) => tex.store.set(pat(0xa0), 0);
    await renderer.render(frame({ frameNo: 1, bytes: seed, hash: HASH_A, views: views12, bindings: [k6(0, VIEW1)], readback: rb(0, 32) }));
    assert.deepEqual(h.heapBytes(0, 32), [...pat(0xa0)], 'view 1 writes and reads back');
    // A second view over the SAME buffer+range (another format) is
    // bound and written in a later batch: its texture is the truth,
    // not the first-inserted registry entry.
    h.wgsl = WGSL_TEX('r32uint');
    h.onDispatch = (tex) => tex.store.set(pat(0xb0), 0);
    await renderer.render(frame({ frameNo: 2, bytes: seed, hash: HASH_A, views: views12, bindings: [k6(0, VIEW2)], readback: rb(0, 32), csHash: hex(0xc6) }));
    assert.deepEqual(h.heapBytes(0, 32), [...pat(0xb0)], 'the freshest covering view serves the readback');
    // A later batch binds NEITHER view (a copy deferred across
    // submissions): the stale pick must still serve the FRESHEST
    // binding's texture, not the first one ever registered.
    await renderer.render(frame({ frameNo: 3, readback: rb(0, 32) }));
    assert.deepEqual(h.heapBytes(0, 32), [...pat(0xb0)], 'a cross-batch readback serves the freshest covering view');
    // Both views bound in ONE batch: genuinely ambiguous -- decline
    // loudly (heap untouched) instead of serving either texture.
    h.wgsl = WGSL_TEX2;
    h.fillHeap(0xee);
    h.onDispatch = (tex) => tex.store.set(pat(0xe0), 0);
    await renderer.render(frame({ frameNo: 4, bytes: seed, hash: HASH_A, views: views12, bindings: [k6(0, VIEW1), k6(1, VIEW2)], readback: rb(0, 32), csHash: hex(0xc7) }));
    assert.deepEqual(h.heapBytes(0, 32), new Array(32).fill(0xee), 'an ambiguous same-batch tie serves nothing');
    assert.ok((renderer.snapshot().dropReasons || {})['buffer-readback-texel-ambiguous'] >= 1,
      'the ambiguity is counted by name: ' + JSON.stringify(renderer.snapshot().dropReasons));
  } finally { h.restore(); }
});

test('finding 3: a this-batch texel write outranks a raw storage binding of the same buffer', async () => {
  const h = makeHarness();
  try {
    const renderer = await h.renderer();
    h.wgsl = WGSL_TEX_SSBO;
    const seed = pat(0x10);
    const views = new Map([view(VIEW1, 101, 32)]);
    const ssbo = { binding: 1, type: 7, dstArrayElement: 0, count: 1, kind: 1, obj: BUF, range: '32', bufOff: '0' };
    h.onDispatch = (tex) => tex.store.set(pat(0xf0), 0);
    await renderer.render(frame({ frameNo: 1, bytes: seed, hash: HASH_A, views, bindings: [k6(0, VIEW1), ssbo], readback: rb(0, 32) }));
    assert.deepEqual(h.heapBytes(0, 32), [...pat(0xf0)],
      'the texel texture (seeded with the same bytes the SSBO buffer holds) is the readback source after a texel dispatch');
  } finally { h.restore(); }
});

test('finding 6: oversize and unmaterialisable views decline by name before any texture exists', async () => {
  const h = makeHarness({ maxDim: 4 });
  try {
    const renderer = await h.renderer();
    // 16385 rg32uint texels need height 5 at the 8192 wrap; the mock
    // device caps dimensions at 4. Decline by name, create nothing.
    const bigSeed = pat(0x10, 131080);
    const bigViews = new Map([view(VIEW1, 101, 131080)]);
    await renderer.render(frame({ frameNo: 1, bytes: bigSeed, hash: HASH_A, views: bigViews, bindings: [k6(0, VIEW1)], readback: rb(0, 32) }));
    const reasons = renderer.snapshot().dropReasons || {};
    assert.ok(reasons['storage-texel-too-large'] >= 1, 'oversize declines by name: ' + JSON.stringify(reasons));
    assert.equal(h.createdTextures.filter(t => t.format === 'rg32uint').length, 0, 'no oversize texture was created');
    // r16float cannot be created with STORAGE_BINDING in Dawn: decline
    // by name instead of an async validation error killing the frame.
    const f16Views = new Map([view(VIEW1, 76, 32)]);
    await renderer.render(frame({ frameNo: 2, bytes: pat(0x10), hash: HASH_A, views: f16Views, bindings: [k6(0, VIEW1)], readback: rb(0, 32), csHash: hex(0xc6) }));
    const reasons2 = renderer.snapshot().dropReasons || {};
    assert.ok(reasons2['storage-texel-format-unmaterialisable'] >= 1, 'r16float declines by name: ' + JSON.stringify(reasons2));
    assert.equal(h.createdTextures.filter(t => t.format === 'r16float').length, 0, 'no r16float storage texture was created');
  } finally { h.restore(); }
});

test('finding 6: partial tail texels are materialised (ceil-sized), not floored away', async () => {
  const h = makeHarness();
  try {
    const renderer = await h.renderer();
    // Sub-row tail: 4.5 rg32uint texels. Floor-sizing the texture
    // dropped the tail bytes from the seed upload entirely.
    const seed = pat(0x50, 36);
    const views = new Map([view(VIEW1, 101, 36)]);
    const bindings = [{ ...k6(0, VIEW1), range: '36' }];
    h.onDispatch = (tex) => tex.store.set(pat(0xa0), 0); // writes texels 0..3 only
    await renderer.render(frame({ frameNo: 1, bytes: seed, hash: HASH_A, views, bindings, readback: rb(32, 4) }));
    assert.deepEqual(h.heapBytes(0, 4), [...seed.subarray(32, 36)],
      'the tail texel bytes come from the seed, not from zero padding');
    // Wrap-crossing tail: the view ends 4 bytes into the second
    // 8192-texel row. Floor-sizing gives the texture height 1, so the
    // readback rows math (2) exceeds the texture and the copy cannot
    // be served; ceil-sizing gives height 2 and serves the tail.
    const bigSeed = pat(0x60, 65540);
    const bigViews = new Map([view(VIEW2, 101, 65540)]);
    const bigBindings = [{ ...k6(0, VIEW2), range: '65540' }];
    h.onDispatch = () => {};
    await renderer.render(frame({ frameNo: 2, bytes: bigSeed, hash: HASH_B, views: bigViews, bindings: bigBindings, readback: rb(65536, 4) }));
    assert.deepEqual(h.heapBytes(0, 4), [...bigSeed.subarray(65536, 65540)],
      'the wrap-crossing tail texel reads back from the second texture row');
  } finally { h.restore(); }
});
