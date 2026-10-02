import { init, translate } from './vendor/naga/index.js';
let nagaReady;
let slangReady;
/** @param {string} from @param {string | Uint8Array} source @param {'vertex'|'fragment'|'compute'} stage @param {string} entryPoint */
export async function toWGSL(from, source, stage='vertex', entryPoint='main') {
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
    return translate({from:'spirv',to:'wgsl',source});
  }
  if(from!=='glsl') throw new Error('Unknown shader language');
  return translate({from:'glsl',to:'wgsl',source:String(source),parse:{stage}});
}
