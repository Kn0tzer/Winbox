import { test } from 'node:test';
import assert from 'node:assert/strict';
import { createPersistentShaderCache, persistentShaderCache } from '../../../shader-cache.mjs';
import { warmPersistentShaders } from '../../../shader.mjs';

function fakeIndexedDB() {
  const databases = new Map();
  function request(result) {
    const req = { result, error: null };
    queueMicrotask(() => req.onsuccess?.());
    return req;
  }
  function makeDb(name) {
    const stores = new Map();
    const db = {
      objectStoreNames: { contains: key => stores.has(key) },
      createObjectStore(key, { keyPath }) { stores.set(key, { keyPath, records: new Map() }); },
      transaction(names, mode) {
        const tx = { error: null };
        const list = Array.isArray(names) ? names : [names];
        tx.objectStore = key => {
          const store = stores.get(key);
          return {
            get(id) { const req = { result: undefined }; queueMicrotask(() => { req.result = store.records.get(id); req.onsuccess?.(); }); return req; },
            getAll() { const req = { result: [] }; queueMicrotask(() => { req.result = [...store.records.values()]; req.onsuccess?.(); }); return req; },
            put(value) { queueMicrotask(() => { store.records.set(value[store.keyPath], value); }); return {}; },
            delete(id) { queueMicrotask(() => store.records.delete(id)); return {}; },
          };
        };
        queueMicrotask(() => tx.oncomplete?.());
        return tx;
      },
    };
    return db;
  }
  return {
    open(name) {
      const req = { result: null };
      queueMicrotask(() => {
        const first = !databases.has(name);
        req.result = databases.get(name) || makeDb(name);
        databases.set(name, req.result);
        if (first) req.onupgradeneeded?.();
        req.onsuccess?.();
      });
      return req;
    },
    deleteDatabase(name) { return request(databases.delete(name)); },
  };
}

test('persistent shader cache stores versioned WGSL and an app manifest', async () => {
  const cache = createPersistentShaderCache({ indexedDB: fakeIndexedDB(), now: () => 10 });
  assert.equal(await cache.put('glsl:vertex:abc', '@vertex fn main() {}', { stage: 'vertex' }), true);
  const record = await cache.get('glsl:vertex:abc');
  assert.equal(record.wgsl, '@vertex fn main() {}');
  assert.match(record.key, /naga-30\.2\.0:schema-1:/);
  assert.equal(await cache.record('portal', 'glsl:vertex:abc'), true);
  assert.deepEqual((await cache.getManifest('portal')).shaders, ['glsl:vertex:abc']);
  assert.equal(cache.stats.hits, 1);
  assert.equal(cache.stats.writes, 1);
});

test('missing or failing IndexedDB degrades to no cache', async () => {
  const cache = createPersistentShaderCache({ indexedDB: null });
  assert.equal(await cache.get('shader'), null);
  assert.equal(await cache.put('shader', 'wgsl'), false);
  assert.equal(cache.stats.failures, 2);
});

test('idle warm rebuilds shader modules through the supplied device', async () => {
  globalThis.indexedDB = fakeIndexedDB();
  await persistentShaderCache.put('warm:shader', '@vertex fn main() {}');
  await persistentShaderCache.record('warm-app', 'warm:shader');
  let modules = 0, compilations = 0;
  const device = { createShaderModule({ code }) { modules++; assert.match(code, /@vertex/); return { async getCompilationInfo() { compilations++; return { messages: [] }; } }; } };
  await warmPersistentShaders('warm-app', device);
  assert.equal(modules, 1);
  assert.equal(compilations, 1);
});
