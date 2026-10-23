// Optional, best-effort persistent WGSL cache. WebGPU objects are never stored.
export const SHADER_CACHE_SCHEMA = 1;
export const SHADER_CACHE_LIMIT = 32 * 1024 * 1024;
const DB_NAME = 'wine.launcher.shader-cache.v1';
const STORE = 'shaders';
const MANIFESTS = 'manifests';
const NAGA_VERSION = '30.2.0';
const PREFIX = `naga-${NAGA_VERSION}:schema-${SHADER_CACHE_SCHEMA}:`;

function openDb(indexedDB = globalThis.indexedDB) {
  if (!indexedDB) return Promise.reject(new Error('IndexedDB unavailable'));
  return new Promise((resolve, reject) => {
    const request = indexedDB.open(DB_NAME, SHADER_CACHE_SCHEMA);
    request.onupgradeneeded = () => {
      const db = request.result;
      if (!db.objectStoreNames.contains(STORE)) db.createObjectStore(STORE, { keyPath: 'key' });
      if (!db.objectStoreNames.contains(MANIFESTS)) db.createObjectStore(MANIFESTS, { keyPath: 'appId' });
    };
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error('Unable to open shader cache'));
    request.onblocked = () => reject(new Error('Shader cache upgrade blocked'));
  });
}
function requestValue(request) {
  return new Promise((resolve, reject) => {
    request.onsuccess = () => resolve(request.result);
    request.onerror = () => reject(request.error || new Error('Shader cache request failed'));
  });
}
function transact(db, store, mode, action) {
  return new Promise((resolve, reject) => {
    const tx = db.transaction(store, mode);
    let result;
    try { result = action(tx.objectStore(store)); } catch (error) { reject(error); return; }
    tx.oncomplete = () => resolve(result?.result);
    tx.onerror = tx.onabort = () => reject(tx.error || new Error('Shader cache transaction failed'));
  });
}

export function createPersistentShaderCache({ indexedDB = globalThis.indexedDB, now = () => Date.now(), maxBytes = SHADER_CACHE_LIMIT } = {}) {
  let dbPromise;
  let openedDb = null;
  const stats = { hits: 0, misses: 0, writes: 0, failures: 0 };
  const db = () => dbPromise ||= openDb(indexedDB).then(database => (openedDb = database));
  const safe = async (fallback, operation) => {
    try { return await operation(); } catch { stats.failures++; return fallback; }
  };
  return {
    stats,
    key(key) { return PREFIX + key; },
    async get(key) {
      return safe(null, async () => {
        const database = await db(), fullKey = PREFIX + key;
        const record = await requestValue(database.transaction(STORE, 'readonly').objectStore(STORE).get(fullKey));
        if (!record) { stats.misses++; return null; }
        stats.hits++;
        record.lastUsed = now();
        transact(database, STORE, 'readwrite', store => store.put(record)).catch(() => {});
        return record;
      });
    },
    async put(key, value, metadata = {}) {
      return safe(false, async () => {
        const database = await db(), fullKey = PREFIX + key;
        const records = await requestValue(database.transaction(STORE, 'readonly').objectStore(STORE).getAll());
        const text = String(value), size = text.length * 2;
        if (size > maxBytes) return false;
        const others = records.filter(record => record.key !== fullKey);
        let bytes = others.reduce((sum, record) => sum + (record.size || String(record.wgsl || '').length * 2), 0);
        others.sort((a, b) => (a.lastUsed || 0) - (b.lastUsed || 0));
        const remove = [];
        while (bytes + size > maxBytes && others.length) {
          const oldest = others.shift(); bytes -= oldest.size || String(oldest.wgsl || '').length * 2; remove.push(oldest.key);
        }
        await new Promise((resolve, reject) => {
          const tx = database.transaction(STORE, 'readwrite'), store = tx.objectStore(STORE);
          for (const old of remove) store.delete(old);
          store.put({ key: fullKey, wgsl: text, metadata, size, lastUsed: now() });
          tx.oncomplete = resolve; tx.onerror = tx.onabort = () => reject(tx.error || new Error('Shader cache write failed'));
        });
        stats.writes++;
        return true;
      });
    },
    async getManifest(appId) {
      if (!appId) return null;
      return safe(null, async () => requestValue((await db()).transaction(MANIFESTS, 'readonly').objectStore(MANIFESTS).get(String(appId))));
    },
    async record(appId, shaderKey, pipeline = null) {
      if (!appId) return false;
      return safe(false, async () => {
        const database = await db();
        const old = await requestValue(database.transaction(MANIFESTS, 'readonly').objectStore(MANIFESTS).get(String(appId))) || { appId: String(appId), shaders: [], pipelines: [] };
        if (!old.shaders.includes(shaderKey)) old.shaders.push(shaderKey);
        // Pipeline descriptors contain live WebGPU objects in most renderers. Only
        // accept explicitly JSON-safe descriptors provided by callers.
        if (pipeline && !old.pipelines.some(item => item.key === pipeline.key)) old.pipelines.push(pipeline);
        await transact(database, MANIFESTS, 'readwrite', store => store.put(old));
        return true;
      });
    },
    async clear() {
      return safe(false, async () => {
        try { openedDb?.close?.(); } catch {}
        openedDb = null;
        await new Promise((resolve, reject) => {
          const request = indexedDB.deleteDatabase(DB_NAME);
          request.onsuccess = resolve; request.onerror = () => reject(request.error || new Error('Unable to clear shader cache'));
          request.onblocked = () => reject(new Error('Shader cache clear blocked'));
        });
        dbPromise = null;
        return true;
      });
    },
  };
}

export const persistentShaderCache = createPersistentShaderCache();
