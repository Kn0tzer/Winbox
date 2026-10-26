import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { installVkPageTier, V2_MAGIC, V2_VERSION, V2_REC } from '../../../vkwebgpu.mjs';
import { createValidationDevice } from '../fixtures/webgpu-validation-device.mjs';

const tierGlobals = ['bwVkTier', 'bwVkTierReady', 'bwVkStats', 'bwVkChunk', 'bwVkRenderer', 'bwVkRenderer2', 'bwVkTextureCompressionBC'];
function preserveGlobals(names) {
  const saved = new Map(names.map(name => [name, Object.getOwnPropertyDescriptor(globalThis, name)]));
  return () => { for (const [name, descriptor] of saved) {
    if (descriptor) Object.defineProperty(globalThis, name, descriptor);
    else delete globalThis[name];
  } };
}
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));
async function until(predicate) {
  for (let attempt = 0; attempt < 100; attempt++) {
    if (predicate()) return;
    await delay(2);
  }
  throw new Error('Vulkan lifetime operation did not settle');
}

// One clear-only frame, encoded independently of the decoder under test.
function clearFrame(sequence = 0) {
  const records = [];
  function record(type, size, write) {
    const bytes = new Uint8Array(size + 8), view = new DataView(bytes.buffer);
    view.setUint16(0, type, true); view.setUint32(4, size, true);
    write?.(new DataView(bytes.buffer, 8)); records.push(bytes);
  }
  record(V2_REC.FRAME_BEGIN, 16, view => {
    view.setUint32(0, sequence + 1, true); view.setUint32(4, 4, true); view.setUint32(8, 4, true);
  });
  record(V2_REC.RP_BEGIN, 88, view => {
    view.setBigUint64(0, 1n, true); view.setBigUint64(8, 2n, true);
    view.setUint32(16, 4, true); view.setUint32(20, 4, true); view.setUint32(24, 1, true);
    view.setUint32(28, 37, true); view.setUint32(32, 1, true); view.setUint32(40, 1, true);
    view.setFloat32(60, 1, true); view.setFloat32(64, 1, true);
    view.setBigUint64(72, 3n, true); view.setBigUint64(80, 4n, true);
  });
  record(V2_REC.RP_END, 0);
  record(V2_REC.FRAME_END, 4, view => view.setUint32(0, sequence + 1, true));
  const bytes = new Uint8Array(16 + records.reduce((sum, record) => sum + record.length, 0));
  const view = new DataView(bytes.buffer);
  view.setUint32(0, V2_MAGIC, true); view.setUint16(4, V2_VERSION, true); view.setUint32(8, sequence, true);
  let at = 16; for (const record of records) { bytes.set(record, at); at += record.length; }
  return bytes;
}

async function appEnvironment(gpu) {
  const source = await readFile(new URL('../../../app.mjs', import.meta.url), 'utf8');
  const elements = new Map(), listeners = new Map();
  let hold = false; const waiting = [];
  function element(tag = 'div') {
    const el = {
      tag, children: [], dataset: {}, value: '', textContent: '', disabled: false,
      classList: { add() {}, remove() {}, toggle() {} }, style: { setProperty() {}, removeProperty() {} }, contentWindow: { document: { getElementById: () => null } },
      setAttribute() {}, removeAttribute() {}, focus() {}, close() {}, showModal() {},
      append(...children) { for (const child of children) { this.children.push(child); if (child.id) elements.set(child.id, child); } },
      add(option) { this.children.push(option); }, replaceChildren(...children) { this.children = children; },
      remove() { this.removed = true; if (elements.get(this.id) === this) elements.delete(this.id); },
    };
    if (tag === 'canvas') Object.assign(el, { width: 4, height: 4, getContext: gpu.canvas.getContext });
    return el;
  }
  for (const [, id] of source.matchAll(/\$\('([^']+)'\)/g)) elements.set(id, element());
  const markup = await readFile(new URL('../../../index.html', import.meta.url), 'utf8');
  for (const [, id] of markup.matchAll(/\bid="([^"]+)"/g)) if (!elements.has(id)) elements.set(id, element());
  const demo = { onclick: () => globalThis.wineGames.install('doom') };
  const document = {
    createElement: element, createElementNS: (namespaceURI, tag) => Object.assign(element(tag), { namespaceURI }), createDocumentFragment: () => element('fragment'), documentElement: element('html'), querySelector: () => element('meta'), getElementById: id => elements.get(id) || null,
    querySelectorAll: () => [], body: element('body'),
  };
  // Startup only reads an empty installation library. No guest filesystem is
  // mounted, so stop's real snapshot() has no writable files to save.
  const indexedDB = { open() {
    const request = {};
    request.result = { createObjectStore() {}, close() {}, transaction() {
      const tx = { objectStore: () => ({ getAll: () => ({ result: [] }), clear() {}, put() {} }) };
      queueMicrotask(() => tx.oncomplete?.()); return tx;
    } };
    if (hold) waiting.push(() => request.onsuccess?.()); else queueMicrotask(() => request.onsuccess?.()); return request;
  } };
  Object.assign(globalThis, {
    document, indexedDB, window: globalThis, location: { origin: 'http://localhost', search: '' },
    crossOriginIsolated: true, matchMedia: () => ({ matches: false }), localStorage: { getItem: () => null, setItem() {} }, Option: class { constructor(text, value) { this.text = text; this.value = value; } },
    addEventListener: (type, callback) => listeners.set(type, callback),
  });
  const app = source.replace(/(['"])\.\/([^'"]+)\1/g,
    (_match, quote, file) => quote + new URL('../../../' + file, import.meta.url).href + quote);
  await import('data:text/javascript;base64,' + Buffer.from(app).toString('base64'));
  return { elements, demo, listeners, holdSaves() { hold = true; }, releaseSaves() { hold = false; waiting.splice(0).forEach(done => done()); } };
}

test('Stop releases Vulkan resources and a new guest starts with a fresh tier', async () => {
  const restoreGlobals = preserveGlobals([...tierGlobals, 'document', 'indexedDB', 'window', 'location',
    'crossOriginIsolated', 'matchMedia', 'localStorage', 'Option', 'addEventListener', 'wineLibrary', 'wineGames']);
  const gpu = createValidationDevice(); let destroyed = 0;
  gpu.device.destroy = () => { destroyed++; };
  try {
    const { elements, demo, listeners, holdSaves, releaseSaves } = await appEnvironment(gpu);
    await demo.onclick();
    const iframe = elements.get('display').children.find(el => el.tag === 'iframe');
    listeners.get('message')({ origin: location.origin, source: iframe.contentWindow,
      data: { wine: true, type: 'vk2', data: { bytes: clearFrame(), flags: 0 } } });
    await until(() => globalThis.bwVkStats?.().v2.rendered === 1);
    const oldTier = globalThis.bwVkTier, oldCanvas = elements.get('vkCanvas');
    assert.ok(gpu.textures.length, 'the real renderer must have allocated its cached render target');
    await elements.get('stop').onclick();
    assert.equal(destroyed, 1, 'Stop must destroy the old guest WebGPU device');
    assert.ok(oldCanvas.removed, 'Stop must remove the canvas owned by the old tier');
    assert.equal(globalThis.bwVkTier, undefined, 'old resource and shader caches must be unreachable through the global tier');
    assert.equal(globalThis.bwVkTierReady, undefined, 'restart must not reuse the old resolved initialization promise');
    assert.ok(gpu.textures.every(texture => texture.destroyed), 'cached render targets must be released');
    await demo.onclick();
    const next = installVkPageTier({ canvas: gpu.canvas });
    assert.notEqual(next, oldTier);
    assert.equal(next.stats().v2.rendered, 0);
    await next.destroy();
    // Stop is deliberately held at its database write. A restart during that
    // window must not create a guest that old teardown can remove or hide.
    const live = elements.get('display').children.filter(el => el.tag === 'iframe' && !el.removed).at(-1);
    live.contentWindow.bwFilesystemReady = true;
    live.contentWindow.bwRuntime = { fs: () => ({ readdir: () => [] }) };
    holdSaves();
    const stopping = elements.get('stop').onclick();
    await Promise.resolve();
    await demo.onclick();
    assert.equal(elements.get('display').children.filter(el => el.tag === 'iframe' && !el.removed).length, 0, 'restart must wait for pending save/teardown');
    releaseSaves(); await stopping;
    await demo.onclick();
    assert.equal(elements.get('display').children.filter(el => el.tag === 'iframe' && !el.removed).length, 1, 'a fresh run starts after teardown completes');
    assert.equal(elements.get('player').hidden, false, 'old Stop must not hide the new program');
    await elements.get('stop').onclick();
  } finally { gpu.restore(); restoreGlobals(); }
});

test('destroy cancels a queued frame before the debounce can allocate a device', async () => {
  const restoreGlobals = preserveGlobals(tierGlobals), gpu = createValidationDevice();
  let adapters = 0;
  const requestAdapter = navigator.gpu.requestAdapter;
  navigator.gpu.requestAdapter = async () => { adapters++; return requestAdapter(); };
  try {
    const tier = installVkPageTier({ canvas: gpu.canvas });
    await tier.chunk(clearFrame(), 0);
    await tier.destroy();
    await delay(20);
    assert.equal(adapters, 0, 'canceled frames must not create a new renderer after Stop');
    assert.equal(tier.stats().v2.queue, 0);
    assert.equal((await tier.chunk(clearFrame(1), 0)).reason, 'destroyed');
  } finally { gpu.restore(); restoreGlobals(); }
});

test('destroy waits for an initializing renderer and prevents its frame from rendering', async () => {
  const restoreGlobals = preserveGlobals(tierGlobals), gpu = createValidationDevice();
  let releaseAdapter, started = false, destroyed = 0;
  const requestAdapter = navigator.gpu.requestAdapter;
  navigator.gpu.requestAdapter = () => { started = true; return new Promise(resolve => {
    releaseAdapter = async () => resolve(await requestAdapter());
  }); };
  gpu.device.destroy = () => { destroyed++; };
  try {
    const tier = installVkPageTier({ canvas: gpu.canvas });
    const frame = tier.frame('{}'); await until(() => started);
    let complete = false;
    const destroy = tier.destroy().then(() => { complete = true; });
    await Promise.resolve(); assert.equal(complete, false, 'teardown must wait for pending device initialization');
    await releaseAdapter(); await destroy;
    assert.equal((await frame).reason, 'destroyed');
    assert.equal(destroyed, 1);
    assert.equal(tier.stats().rendered, 0);
    assert.equal(globalThis.bwVkRenderer, undefined, 'late initialization must not restore a stale renderer global');
    await tier.destroy(); assert.equal(destroyed, 1, 'teardown must be idempotent');
  } finally { gpu.restore(); restoreGlobals(); }
});

test('destroy waits for an in-flight GPU readback before releasing its device', async () => {
  const restoreGlobals = preserveGlobals(tierGlobals), gpu = createValidationDevice();
  let releaseReadback, mapping = false, destroyed = 0;
  const createBuffer = gpu.device.createBuffer;
  gpu.device.createBuffer = descriptor => {
    const buffer = createBuffer(descriptor);
    buffer.mapAsync = () => { mapping = true; return new Promise(resolve => { releaseReadback = resolve; }); };
    return buffer;
  };
  gpu.device.destroy = () => { destroyed++; };
  try {
    const tier = installVkPageTier({ canvas: gpu.canvas });
    await tier.chunk(clearFrame(), 0); await until(() => mapping);
    const destroy = tier.destroy();
    await Promise.resolve(); assert.equal(destroyed, 0, 'the active frame still owns its device');
    releaseReadback(); await destroy;
    assert.equal(destroyed, 1);
    assert.equal(tier.stats().v2.rendered, 1, 'the already submitted frame finishes before teardown');
  } finally { releaseReadback?.(); gpu.restore(); restoreGlobals(); }
});
