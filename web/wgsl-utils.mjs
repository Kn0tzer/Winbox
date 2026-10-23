// WGSL resource layout and the shader transforms used by Vulkan replay.
import { V2_PUSH_BINDING, V2_PUSH_GROUP } from './vk-protocol.mjs';
// ---------------------------------------------------------------------------
// Push constants as a small UBO (tasks/schema-v2.md §5). naga's WGSL backend
// emits `var<immediate>` in 30.2.0 (older translations used
// `var<push_constant>`); this rewrites either declaration to a plain
// uniform at the reserved @group(3) @binding(0), and the renderer keeps one
// 256-byte uniform buffer it writeBuffers before each draw group. At most one
// push block per stage is supported — DXVK uses one.
// ---------------------------------------------------------------------------
export function rewritePushConstantsWGSL(wgsl) {
  let pushBlocks = 0;
  let code = String(wgsl).replace(
    /(?:@group\(\d+\)\s*@binding\(\d+\)\s*)?var<\s*(?:push_constant|immediate)\s*>/g,
    () => { pushBlocks++; return '@group(' + V2_PUSH_GROUP + ') @binding(' + V2_PUSH_BINDING + ') var<uniform>'; }
  );
  return { code, pushBlocks };
}

// ---------------------------------------------------------------------------
// Naga Y-negation strip.
// ---------------------------------------------------------------------------
// Naga's SPIR-V frontend appends `pos.y = -(pos.y)` (via a temp) to every
// translated vertex shader. That negation is the correct Vulkan->WebGPU
// adapter only for positive Vulkan viewport heights: on this stack NDC y=+1
// maps to texture row 0 (top), so the negation reproduces Vulkan's mapping
// for h>0. DXVK's D3D9 path uses negative viewport heights (Y-flip
// convention), which WebGPU cannot express; emulating such a viewport needs
// the UN-negated shader variant, selected per draw (see the viewport case).
export function stripNagaYNegation(wgsl) {
  return String(wgsl)
    .replace(/let\s+(_e\d+)\s*=\s*(\w+)\.y\s*;\s*\2\.y\s*=\s*-\(\1\)\s*;/g, '')
    .replace(/(\w+)\.y\s*=\s*-\(\1\.y\)\s*;/g, '');
}

// Scan translated WGSL for resource declarations. The bind-group layouts are
// built from what the shader actually declares — not from the manifest's
// DESC_SET table — so whatever binding assignment naga chose (including a
// texture+sampler pair sharing one binding, which WGSL allows) is honoured.
const WGSL_RES_DECL = /@group\((\d+)\)\s*@binding\((\d+)\)\s*var(?:<([^>]+)>)?\s*(\w+)\s*:\s*([^;]+);/g;
// DXVK's MSAA present shader averages its source samples. The page tier has
// already resolved that attachment, so loading the resolved texel performs
// the same operation. Gamma-enabled presentation retains the original shader.
export function rewriteResolvedDxvkPresent(hash, wgsl) {
  if (hash !== '8b1e0f33622e4d9f' || typeof wgsl !== 'string' ||
      !/const s_gamma_bound: bool = false;/.test(wgsl) ||
      !/var s_image: texture_multisampled_2d<f32>;/.test(wgsl)) return wgsl;
  return `struct present_info_t {src_offset: vec2<i32>, dst_offset: vec2<i32>,}
var<immediate> unnamed: present_info_t;
@group(0) @binding(0) var s_image: texture_2d<f32>;
@fragment fn main(@builtin(position) p: vec4<f32>) -> @location(0) vec4<f32> {
 let coord = vec2<i32>(p.xy) + unnamed.src_offset - unnamed.dst_offset;
 return textureLoad(s_image, coord, 0);
}`;
}

export function stripNagaFragmentDepth(wgsl) {
  const structs = [...String(wgsl).matchAll(/struct\s+(\w+)\s*\{([^}]+)\}/g)];
  for (const [whole, name, body] of structs) {
    const members = body.split('\n').filter(line => /\w+\s*:/.test(line));
    const index = members.findIndex(line => /@builtin\(frag_depth\)/.test(line));
    if (index < 0) continue;
    const constructors = [...String(wgsl).matchAll(new RegExp('\\b' + name + '\\(([^()]*)\\)', 'g'))];
    if (!constructors.length || constructors.some(m => m[1].split(',').length !== members.length ||
        m[1].split(',').some(arg => !/^\s*\w+\s*$/.test(arg)))) return wgsl;
    let code = String(wgsl).replace(whole, whole.replace(members[index], ''));
    for (const m of constructors) {
      const args = m[1].split(','); args.splice(index, 1);
      code = code.replace(m[0], name + '(' + args.join(',') + ')');
    }
    return code;
  }
  return wgsl;
}

// WebGPU requires a fragment input and the vertex output at the same location
// to have the same component count. D3D/DXVK pairs routinely declare a wider
// output (vec4) than the pixel shader consumes (vec3/vec2), which Vulkan
// permits. Widen the fragment input to the vertex output's type and swizzle it
// back where naga copies it into the shader's private global.
const WGSL_VARYING = /@location\((\d+)\)((?:\s*@\w+(?:\([^)]*\))?)*)\s*(\w+)\s*:\s*(?:vec([234])<f32>|(f32))/g;
const varyings = text => [...String(text).matchAll(WGSL_VARYING)].map(m => ({
  whole: m[0], location: +m[1], attrs: m[2], name: m[3], n: m[4] ? +m[4] : 1,
}));
const structBody = (wgsl, name) => new RegExp('struct\\s+' + name + '\\s*\\{([^}]*)\\}').exec(wgsl)?.[1];
export function matchFragmentInputsToVertexOutputs(vsWgsl, fsWgsl) {
  const vs = String(vsWgsl), fs = String(fsWgsl);
  const vsEntry = /@vertex\s*fn\s+main\s*\((?:[^()]|\([^()]*\))*\)\s*->\s*(\w+)/.exec(vs);
  const fsEntry = /@fragment\s*fn\s+main\s*\(((?:[^()]|\([^()]*\))*)\)/.exec(fs);
  const vsBody = vsEntry && structBody(vs, vsEntry[1]);
  if (!vsBody || !fsEntry) return fsWgsl;
  const outputs = new Map(varyings(vsBody).map(v => [v.location, v.n]));
  const sites = varyings(fsEntry[1]).map(v => ({ ...v, expr: v.name, where: fsEntry[1] }));
  const param = /(\w+)\s*:\s*([A-Z]\w*)\s*(?:,|$)/.exec(fsEntry[1].replace(/@location[^,]*/g, ''));
  const inBody = param && structBody(fs, param[2]);
  if (inBody) sites.push(...varyings(inBody).map(v => ({ ...v, expr: param[1] + '.' + v.name, where: inBody })));
  let code = fs;
  for (const site of sites) {
    const outN = outputs.get(site.location);
    if (!outN || outN <= site.n || outN > 4) continue;
    const use = new RegExp('=\\s*' + site.expr.replace('.', '\\.') + '\\s*;', 'g');
    if (!use.test(code)) continue;
    const swizzle = '.' + 'xyzw'.slice(0, site.n);
    code = code.replace(use, m => m.replace(/\s*;$/, '') + swizzle + ';')
      .replace(site.whole, site.whole.replace(/:\s*(?:vec\d<f32>|f32)$/, ': vec' + outN + '<f32>'));
  }
  return code;
}

// WebGPU has no multisampled 2d-array texture, and the page tier already
// resolves every multisampled attachment into the single-sample texture it
// binds for sampling. A shader that loads individual samples therefore reads
// the resolved texel: retype the binding, make every per-sample load read
// level 0, and report one sample. (DXVK's resolve/present shaders average
// c_samples loads, which the single resolved texel reproduces.)
export function rewriteMultisampledTextures(wgsl) {
  let code = String(wgsl);
  const names = [...code.matchAll(/var\s+(\w+)\s*:\s*texture_multisampled_2d(_array)?<(\w+)>\s*;/g)].map(m => m[1]);
  if (!names.length) return wgsl;
  code = code.replace(/texture_multisampled_2d(_array)?</g, 'texture_2d$1<');
  for (const name of names) {
    code = code.replace(new RegExp('\\btextureNumSamples\\(\\s*' + name + '\\s*\\)', 'g'), '1u');
    const call = new RegExp('\\btextureLoad\\(\\s*' + name + '\\s*,', 'g');
    let m, out = '', last = 0;
    while ((m = call.exec(code))) {
      let depth = 1, i = call.lastIndex, lastComma = -1;
      for (; i < code.length && depth; i++) {
        const c = code[i];
        if (c === '(') depth++; else if (c === ')') depth--; else if (c === ',' && depth === 1) lastComma = i;
      }
      if (depth || lastComma < 0) return wgsl;
      out += code.slice(last, lastComma + 1) + ' 0i)';
      last = i; call.lastIndex = i;
    }
    code = out + code.slice(last);
  }
  return code;
}

/**
 * The GPUTextureViewDimension a WGSL sampled-texture type requires: the single
 * source for both the bind group layout entry and the view bound to it, since
 * Dawn rejects any pipeline whose layout dimension differs from the shader's.
 * Covers texture_{1d,2d,2d_array,3d,cube,cube_array}, their texture_depth_*
 * and multisampled forms (multisampled textures are rewritten to 2d before
 * the layout is built). Returns null for anything that is not a sampled
 * texture, including texture_storage_*.
 * @param {string} type
 * @returns {'1d'|'2d'|'2d-array'|'3d'|'cube'|'cube-array'|null}
 */
export function wgslTextureViewDimension(type) {
  const m = /^texture_(?:depth_)?(?:multisampled_)?(1d|2d|3d|cube)(_array)?\b/.exec(type);
  if (!m) return null;
  const base = m[1];
  if (m[2]) return base === 'cube' ? 'cube-array' : base === '2d' ? '2d-array' : null;
  return /** @type {any} */ (base);
}

export function scanWgslResources(wgsl) {
  /** @type {Array<{group:number, binding:number, addr:string, name:string, type:string, arrayCount:number, sourceBinding?:number}>} */
  const out = [];
  WGSL_RES_DECL.lastIndex = 0;
  let m;
  while ((m = WGSL_RES_DECL.exec(wgsl))) {
    const type = m[5].trim();
    const addr = (m[3] || '').trim();
    if (addr === 'private' || addr === 'workgroup' || addr === 'function') continue;
    let arrayCount = 1;
    const ba = /binding_array<\s*([^,]+?)\s*,\s*(\d+)\s*>/.exec(type);
    if (ba) arrayCount = parseInt(ba[2], 10);
    out.push({ group: +m[1], binding: +m[2], addr, name: m[4], type, arrayCount });
  }
  for (const decl of out) {
    // spirvfix splits a combined image/sampler into two WebGPU bindings.
    // Its generated sampler name carries the original Vulkan descriptor slot,
    // including for anonymous SPIR-V resources and aliased shadow samplers.
    const source = /^bw_combined_g(\d+)_b(\d+)_v\d+(?:_\d*)?$/.exec(decl.name);
    if (source && +source[1] === decl.group && /^sampler(?:_comparison)?$/.test(decl.type))
      decl.sourceBinding = +source[2];
  }
  return out;
}
// WGSL uniform-address-space size computation, for padding short buffers.
// wgslUniformSizes returns a Map from "group:binding" to the byte size of the
// uniform struct declared there. Pure and testable.
// SPIR-V float registers do not need to be aligned to a 16-byte boundary, but
// WGSL does: an array<f32, 240> block is 17*256 = 4352 bytes in WGSL while the
// guest's UBO is only 960*4 = 3840. Dawn derives the implicit minBindingSize
// from the WGSL block, so the page must pad short buffers to the padded size
// or every draw encoding that frame fails with "buffer binding ... too small".
export function wgslUniformSizes(wgsl) {
  const structs = new Map();
  // Struct bodies nest braces only through mat/array/vec type parameters, all
  // of which are on one field line, so a bracket-depth walk beats a flat scan.
  const structRe = /struct\s+(\w+)\s*\{/g;
  let m;
  while ((m = structRe.exec(wgsl))) {
    let depth = 1, i = m.index + m[0].length;
    while (i < wgsl.length && depth > 0) {
      if (wgsl[i] === '{') depth++;
      else if (wgsl[i] === '}') depth--;
      i++;
    }
    const body = wgsl.slice(m.index + m[0].length, depth === 0 ? i - 1 : wgsl.length);
    const fields = [];
    const parts = []; let start = 0, pdepth = 0;
    for (let j = 0; j < body.length; ++j) {
      if (body[j] === '<' || body[j] === '(') pdepth++;
      else if (body[j] === '>' || body[j] === ')') pdepth--;
      else if (body[j] === ',' && pdepth === 0) { parts.push(body.slice(start, j)); start = j + 1; }
    }
    parts.push(body.slice(start));
    for (const part of parts) {
      const fm = /(\w+)\s*:\s*([^;@]+)/.exec(part.trim());
      if (fm) fields.push({ name: fm[1], type: fm[2].trim() });
    }
    structs.set(m[1], fields);
  }
  const out = new Map();
  const declRe = /@group\((\d+)\)\s*@binding\((\d+)\)\s*var<uniform>\s*\w+\s*:\s*([^;]+);/g;
  while ((m = declRe.exec(wgsl))) {
    const t = wgslTypeLayout(m[3].trim(), structs);
    if (t) out.set(m[1] + ":" + m[2], t.size);
  }
  return out;
}

// WebGPU's rejection message for an undersized binding:
//   ... [Buffer (unlabeled)] bound with size 3840 at group 2, binding 0 is too
//   small. The pipeline ... requires a buffer binding which is at least 4352
//   bytes. ...
// Learned sizes are cached per group:binding so later frames pad from the
// start instead of failing once first. Pure and testable.
export function parseMinBindingSize(message) {
  const m = /at group (\d+), binding (\d+) is too small[\s\S]*?at least (\d+) bytes/.exec(String(message || ''));
  if (!m) return null;
  return { group: +m[1], binding: +m[2], bytes: Math.max(+m[3], 4) };
}

// Returns {size, align} for a WGSL type in the uniform address space.
function wgslTypeLayout(type, structs) {
  const scalar = { f32: 4, i32: 4, u32: 4, bool: 4 };
  if (scalar[type]) return { size: 4, align: 4 };
  let vm = /^vec(\d)<\s*(\w+)\s*>$/.exec(type);
  if (vm) {
    const n = +vm[1];
    if (n === 2) return { size: 8, align: 8 };
    return { size: 16, align: 16 }; // vec3/vec4
  }
  let mm = /^mat(\d)x(\d)<\s*\w+\s*>$/.exec(type);
  if (mm) {
    const cols = +mm[1]; // each column is a vec aligned to 16
    return { size: cols * 16, align: 16 };
  }
  // Array counts may carry a WGSL u suffix (array<vec4<f32>, 240u>).
  let am = /^array<\s*(.+?)\s*,\s*(\d+)u?\s*>$/.exec(type);
  if (am) {
    const el = wgslTypeLayout(am[1].trim(), structs);
    if (!el) return null;
    // Uniform address space: array element stride is rounded up to 16 (WGSL
    // layout constraints), so array<f32, 240> strides 16 bytes per element.
    const stride = Math.ceil(Math.max(el.size, el.align) / 16) * 16;
    return { size: stride * (+am[2]), align: 16 };
  }
  const fields = structs.get(type);
  if (fields) {
    let offset = 0, maxAlign = 16;
    for (const f of fields) {
      const t = wgslTypeLayout(f.type, structs);
      if (!t) return null;
      maxAlign = Math.max(maxAlign, t.align);
      offset = Math.ceil(offset / t.align) * t.align + t.size;
    }
    const size = Math.ceil(offset / maxAlign) * maxAlign;
    return { size, align: maxAlign };
  }
  return null;
}
