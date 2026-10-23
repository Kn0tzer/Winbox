import { init, translate } from './vendor/naga/index.js';
import { persistentShaderCache } from './shader-cache.mjs';
const persistentMemory = new Map();
let nagaReady;
let slangReady;
/** @param {string} from @param {string | Uint8Array} source @param {'vertex'|'fragment'|'compute'} stage @param {string} entryPoint @param {{rasterTopology?: number}} [options] */
async function translateWGSL(from, source, stage='vertex', entryPoint='main', options={}) {
  if(typeof source==='string' && source.length>1024*1024) throw new Error('Shader exceeds 1 MiB');
  if(source instanceof Uint8Array && source.byteLength>1024*1024) throw new Error('Shader exceeds 1 MiB');
  if(!['vertex','fragment','compute'].includes(stage)) throw new Error('Invalid shader stage');
  nagaReady ||= init(); await nagaReady;
  let result;
  if(from==='hlsl') {
    slangReady ||= import('./vendor/slang/slang-wasm.js').then(m=>m.default());
    const compiler=await slangReady;
    const handles=[];
    const keep=handle=>{if(!handle)throw new Error(String(compiler.getLastError().message||'Slang compilation failed'));handles.push(handle);return handle;};
    try {
      const global=keep(compiler.createGlobalSession());
      const target=compiler.getCompileTargets().find(t=>t.name==='WGSL').value;
      const session=keep(global.createSession(target));
      // Official WASM binding uses (source, moduleName, path), not the C++ order.
      const module=keep(session.loadModuleFromSource(String(source),'uploaded','uploaded.hlsl'));
      const entry=keep(module.findAndCheckEntryPoint(entryPoint,{vertex:1,fragment:5,compute:6}[stage]));
      const composite=keep(session.createCompositeComponentType([module,entry]));
      const linked=keep(composite.link());
      result=linked.getEntryPointCode(0,0);
      if(!result)throw new Error(String(compiler.getLastError().message||'Slang returned no WGSL'));
    } finally { handles.reverse().forEach(h=>h.delete()); }
    return translate({from:'wgsl',to:'wgsl',source:result});
  }
  if(from==='spirv') {
    if(!(source instanceof Uint8Array)||source.byteLength<20||source.byteLength%4) throw new Error('Invalid SPIR-V byte length');
    if(new DataView(source.buffer,source.byteOffset,source.byteLength).getUint32(0,true)!==0x07230203) throw new Error('Invalid SPIR-V magic');
    // Pre-pass (tools/spirvfix/spirvfix.mjs): rewrite the module patterns
    // naga's spv-in rejects — combined-sampler loads (InvalidId) and spec
    // constants (UnsupportedInstruction) — before handing bytes to naga.
    // Byte-identical passthrough when nothing applies. The export includes a
    // sibling implementation so this also works below a /winbox/ URL prefix.
    try {
      const { applySpirvFix } = await import('./spirvfix.mjs');
      source = applySpirvFix(source, options).bytes;
    } catch(e) {
      console.warn('spirvfix pre-pass unavailable, passing SPIR-V through unmodified:', e?.message || e);
    }
    return translate({from:'spirv',to:'wgsl',source});
  }
  if(from!=='glsl') throw new Error('Unknown shader language');
  return translate({from:'glsl',to:'wgsl',source:String(source),parse:{stage}});
}

// Persistent cache is opt-in through the launcher query string and stores only
// WGSL text. Hash source bytes and conversion options so GL and Vulkan entries
// cannot alias across stages or specialization settings.
async function cacheHash(value) {
  try {
    const bytes = new TextEncoder().encode(value);
    const digest = await crypto.subtle.digest('SHA-256', bytes);
    return [...new Uint8Array(digest)].map(byte => byte.toString(16).padStart(2, '0')).join('');
  } catch {
    let hash = 2166136261;
    for (let i = 0; i < value.length; i++) { hash ^= value.charCodeAt(i); hash = Math.imul(hash, 16777619); }
    return (hash >>> 0).toString(16).padStart(8, '0');
  }
}
export async function toWGSL(from, source, stage='vertex', entryPoint='main', options={}) {
  if (from === 'spirv') return translateWGSL(from, source, stage, entryPoint, options);
  const enabled = new URLSearchParams(globalThis.location?.search || '').get('shadercache') === '1';
  if (!enabled) return translateWGSL(from, source, stage, entryPoint, options);
  const text = typeof source === 'string' ? source : Array.from(source || []).join(',');
  const key = `${from}:${stage}:${entryPoint}:${JSON.stringify(options)}:${text.length}:${await cacheHash(text)}`;
  if (persistentMemory.has(key)) return persistentMemory.get(key);
  const cached = await persistentShaderCache.get(key);
  if (cached?.wgsl) { persistentMemory.set(key, cached.wgsl); return cached.wgsl; }
  const wgsl = await translateWGSL(from, source, stage, entryPoint, options);
  persistentMemory.set(key, wgsl);
  await persistentShaderCache.put(key, wgsl, { from, stage, entryPoint });
  const appId = new URLSearchParams(globalThis.location?.search || '').get('installation') || 'unknown';
  await persistentShaderCache.record(appId, key);
  return wgsl;
}

export async function warmPersistentShaders(appId, device, onShader = () => {}) {
  try {
    const manifest = await persistentShaderCache.getManifest(appId);
    for (const key of manifest?.shaders || []) {
      const record = await persistentShaderCache.get(key);
      if (!record?.wgsl) continue;
      onShader(key, record.wgsl);
      if (!key.startsWith('spirv:')) persistentMemory.set(key, record.wgsl);
      const module = device.createShaderModule({ code: record.wgsl });
      await module.getCompilationInfo?.();
    }
  } catch {}
}
