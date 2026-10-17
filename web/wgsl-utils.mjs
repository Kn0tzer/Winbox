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

export function scanWgslResources(wgsl) {
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
