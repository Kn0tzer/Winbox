import {
  ADDR, FILTER, MAX_DIMENSION, MAX_DRAWS,
  V2FrameAssembler, V2FrameQueue, V2PresentAssembler,
  V2_PUSH_BINDING, V2_PUSH_GROUP, V2_PUSH_SIZE, V2_QUEUE_CAP,
  V2_SCALED_VERTEX, V2_TOPOLOGY, v2HasColourPass, v2PresentIndex, V2_UNKNOWN_IMAGE, V2_VERT_FORMAT_SIZE,
  applyDynamicState, decodeV2Frame, parseV2Chunk, repackScaledVb,
  swizzleBgraVb, v2DepthStencilUploads, v2ImageBlitRegion, v2TextureFormat, v2TextureMipUploadPlan, v2TexturePixels, v2VertexFormat,
  v2IndexedTriangleFanIndices, v2TriangleFanIndices,
} from './vk-protocol.mjs';
import { bwLog, bwLogRl } from './vk-log.mjs';
import { bridgeRecoveryStats, reportBridgeFailure } from './bridge-recovery.mjs';
import {
  parseMinBindingSize, rewritePushConstantsWGSL, rewriteResolvedDxvkPresent,
  matchFragmentInputsToVertexOutputs, rewriteMultisampledTextures, scanWgslResources, stripNagaFragmentDepth, stripNagaYNegation, wgslTextureViewDimension, wgslUniformSizes,
} from './wgsl-utils.mjs';
// web/vkwebgpu.mjs — the BROWSER PAGE TIER for the vk64 trap boundary: the
// consumer for the FRAME-JSON manifest that source/vulkan/vk64bridge.cpp hops
// into the page at vkQueuePresentKHR (one MAIN_THREAD_EM_ASM per frame, JS body
// `window.bwVkFrame(UTF8ToString(ptr))`).
//
// WHAT IT DOES. parseManifest() decodes the manifest, web/shader.mjs translates
// the inline SPIR-V blobs to WGSL, and one WebGPU render pass replays the frame:
// uniform block @binding(0), combined image sampler split into texture @binding(1)
// + sampler @binding(2), a bgra8unorm offscreen colour target with depth24plus,
// the manifest's viewport / scissor / cull / front / depth state, and each
// vkCmdDraw as draw(vertexCount, 1, firstVertex, 0). The offscreen target is both
// copied to the page canvas and read back for the page-tier counters.
//
// This is host/p1render.mjs — the P1 reference consumer that rendered P1's
// vkcube frames — minus the node/playwright/file-IO half, wired to the live
// bridge's in-memory manifest instead of P1's sidecar .json/.bin/.spv tree.
//
// SPIR-V -> WGSL: the vendored in-page naga (web/vendor/naga = naga-wasm 30.2.0,
// the same wasm P1 used offline through parseSpirv/validate/writeWgsl) reached
// through the existing web/shader.mjs toWGSL('spirv', …) entry point, cached per
// SPIR-V hash so each guest module is translated once per page. web/naga-rt.mjs
// only lifts GLSL *source* (its whole contract is the guest shaderSource wrap),
// so it could not be reused for SPIR-V blobs; what is mirrored from it is the
// mechanism — a lazily imported page-tier module, a hash-keyed translate cache,
// and a translate that records a failure instead of throwing across the bridge's
// MAIN_THREAD_EM_ASM boundary.
//
// LIMITS (each one is counted, never thrown at the guest — see counters below):
//   * naga's SPIR-V front end rejects vkcube's cube.frag (glslang SPIR-V 1.0,
//     OpTypeSampledImage) with `error: InvalidId(40)`. p1render.mjs hit exactly
//     the same wall and shipped a hand-written equivalent instead; that WGSL is
//     vendored below under FS_OVERRIDES, keyed on the host-computed SPIR-V hash
//     (88f8a5cebeefd8c1 at 1280 bytes = cube.frag), so it can only ever be used
//     for that one blob. Any other fragment module must translate or the frame
//     is counted in `rejects` and nothing is drawn.
//   * One descriptor-set layout: binding 0 uniform (vertex), binding 1 sampled 2D
//     texture + binding 2 sampler (fragment). Multiple sets, push constants,
//     storage images, input attachments and compute are not modelled.
//   * The guest may bind a uniform range smaller than the shader's declared block
//     (vkfixture binds 128 bytes against cube.vert's 1216-byte block). WebGPU
//     reports that through an error scope rather than by throwing, so the range is
//     grown in powers of two — zero-padded, since WebGPU buffers start zeroed —
//     until the bind group validates; the working size is cached per vertex hash.
//   * A manifest with `overflow` (100000+ recorded commands) is rejected rather
//     than rendered, the same discipline web/webgpu.mjs applies.
//   * Y orientation: Vulkan and WebGPU viewports share the top-left framebuffer
//     origin, so the manifest's viewport/scissor go to setViewport/setScissorRect
//     verbatim with no flip. That is P1's proven behaviour; do not "fix" it.
//
// Exposed for the page tier (window.bwVkFrame is wired in web/runtime.html, the
// 'vk' message branch in web/app.mjs): installVkPageTier() and createVkRenderer().
import { toWGSL, warmPersistentShaders } from './shader.mjs';
import { persistentShaderCache } from './shader-cache.mjs';

export const MAX_MANIFEST_BYTES = 8 * 1024 * 1024; // a frame's JSON + inline payloads
export const MAX_UNIFORM_RANGE = 65536; // guaranteed maxUniformBufferBindingSize
const DEPTH_OP = { 0: 'never', 1: 'less', 2: 'equal', 3: 'less-equal', 4: 'greater', 5: 'not-equal', 6: 'greater-equal', 7: 'always' };
const CULL = { 0: 'none', 1: 'front', 2: 'back' };
const FRONT = { 0: 'ccw', 1: 'cw' };
export const REQUIRED_LIMIT_KEYS = Object.freeze([
  'maxTextureDimension1D', 'maxTextureDimension2D', 'maxTextureDimension3D', 'maxTextureArrayLayers',
  'maxBindGroups', 'maxSampledTexturesPerShaderStage', 'maxSamplersPerShaderStage',
  'maxUniformBuffersPerShaderStage', 'maxStorageBuffersPerShaderStage',
  'maxUniformBufferBindingSize', 'maxStorageBufferBindingSize', 'maxBufferSize',
  'maxVertexBuffers', 'maxVertexAttributes', 'maxColorAttachments', 'maxInterStageShaderVariables',
]);
/** Count a skipped-draw reason (first 400 characters) in a per-pass diagnostic map. */
function v2DiagNoteSkip(map, reason) {
  const key = String(reason).replace(/\s+/g, ' ').slice(0, 400);
  map[key] = (map[key] || 0) + 1;
}

// Opt-in per-pass colour readback diagnostic (URL `?bwPassReadback=N`): every Nth
// frame, copy each render pass's single-sample colour targets back and report
// average / constant colour plus the vs/fs hashes drawn, to localise which pass
// produces the black frame. Never enabled by default (adds a copy + map per pass).
export function v2PassReadbackConfig(search) {
  const q = new URLSearchParams(search || '');
  const every = Number.parseInt(q.get('bwPassReadback') ?? '', 10);
  if (!Number.isFinite(every) || every < 1) return null;
  const maxPasses = Number.parseInt(q.get('bwPassReadbackMax') ?? '', 10);
  return { every, maxPasses: Number.isFinite(maxPasses) && maxPasses > 0 ? maxPasses : 64, maxPixels: 4 * 1024 * 1024 };
}
function halfToFloat(h) {
  const e = (h >> 10) & 31, m = h & 1023, sgn = h & 0x8000 ? -1 : 1;
  return e === 0 ? sgn * m * 2 ** -24 : e === 31 ? (m ? NaN : sgn * Infinity) : sgn * (1 + m / 1024) * 2 ** (e - 15);
}
// Texel decoders for the diagnostic: bytes per texel and a reader returning
// [r, g, b, a] in native units (0..255 for unorm, raw value for float).
const DIAG_FORMATS = {
  rgba8: { bpt: 4, read: (v, o) => [v.getUint8(o), v.getUint8(o + 1), v.getUint8(o + 2), v.getUint8(o + 3)] },
  bgra8: { bpt: 4, read: (v, o) => [v.getUint8(o + 2), v.getUint8(o + 1), v.getUint8(o), v.getUint8(o + 3)] },
  r8: { bpt: 1, read: (v, o) => [v.getUint8(o), 0, 0, 255] },
  rg8: { bpt: 2, read: (v, o) => [v.getUint8(o), v.getUint8(o + 1), 0, 255] },
  r16f: { bpt: 2, read: (v, o) => [halfToFloat(v.getUint16(o, true)), 0, 0, 1] },
  rg16f: { bpt: 4, read: (v, o) => [halfToFloat(v.getUint16(o, true)), halfToFloat(v.getUint16(o + 2, true)), 0, 1] },
  rgba16f: { bpt: 8, read: (v, o) => [0, 2, 4, 6].map(k => halfToFloat(v.getUint16(o + k, true))) },
  r32f: { bpt: 4, read: (v, o) => [v.getFloat32(o, true), 0, 0, 1] },
  rgba32f: { bpt: 16, read: (v, o) => [0, 4, 8, 12].map(k => v.getFloat32(o + k, true)) },
};
export function v2DiagFormatKind(format) {
  const f = String(format).replace(/-srgb$/, '');
  return { rgba8unorm: 'rgba8', bgra8unorm: 'bgra8', r8unorm: 'r8', rg8unorm: 'rg8', r16float: 'r16f', rg16float: 'rg16f',
    rgba16float: 'rgba16f', r32float: 'r32f', rgba32float: 'rgba32f' }[f] || null;
}
export function v2DiagBytesPerTexel(format) { return DIAG_FORMATS[v2DiagFormatKind(format)]?.bpt || 0; }
// `format` is a WebGPU format name (or, for backward compatibility, a boolean: bgra8 vs rgba8).
export function v2SummarizeColorPixels(rows, w, h, bytesPerRow, format) {
  const kind = typeof format === 'boolean' ? (format ? 'bgra8' : 'rgba8') : v2DiagFormatKind(format);
  const fmt = DIAG_FORMATS[kind];
  if (!fmt) throw new Error('unsupported diagnostic format ' + format);
  const view = new DataView(rows.buffer, rows.byteOffset, rows.byteLength);
  const sum = [0, 0, 0, 0], round = v => Math.round(v * 1000) / 1000;
  let first = null, constant = true, nonZero = 0, nonFinite = 0;
  for (let y = 0; y < h; y++) for (let x = 0; x < w; x++) {
    const px = fmt.read(view, y * bytesPerRow + x * fmt.bpt);
    if (!first) first = px;
    else if (constant && (px[0] !== first[0] || px[1] !== first[1] || px[2] !== first[2] || px[3] !== first[3])) constant = false;
    if (px[0] || px[1] || px[2]) nonZero++;
    for (let c = 0; c < 4; c++) { if (Number.isFinite(px[c])) sum[c] += px[c]; else nonFinite++; }
  }
  const n = Math.max(1, w * h);
  return { avg: sum.map(v => round(v / n)), constant, constantColor: constant ? first.map(round) : null, nonBlackPixels: nonZero, nonFinite, pixels: w * h };
}
// Diagnostic: one-line description of what each bound descriptor resolves to
// (image size/format/non-zero byte share, rendered target, buffer size/non-zero share).
export function v2DiagDescribeBindings(sets, F, targets) {
  const out = [];
  const share = bytes => { if (!bytes?.length) return 0; const n = Math.min(bytes.length, 65536); let nz = 0; for (let i = 0; i < n; i++) if (bytes[i]) nz++; return Math.round(nz / n * 1000) / 1000; };
  for (const [setIndex, set] of sets) for (const b of set.bindings || []) {
    if (out.length >= 16) return out;
    const d = { s: setIndex, b: b.binding, t: b.type, k: b.kind };
    const imgHex = b.kind === 2 ? F.views?.get(b.obj) : b.obj;
    const img = F.images?.get(imgHex);
    if (img) Object.assign(d, { img: imgHex, hash: img.hash, w: img.w, h: img.h, fmt: img.format, px: img.pixels?.length || 0, nz: share(img.pixels),
      rt: !!(targets.get('img:' + imgHex) || targets.get('imgd:' + imgHex)) });
    else {
      const buf = F.buffers?.get(b.obj + ':' + b.bufOff) ?? F.buffers?.get(b.obj);
      if (buf !== undefined) Object.assign(d, { buf: b.obj, len: buf?.length ?? 0, nz: share(buf) });
      else if (b.obj && b.obj !== '0000000000000000') d.missing = b.obj;
    }
    out.push(d);
  }
  return out;
}
// Diagnostic: how many of this frame's images carry bytes, and how many of those are non-zero
// (sampled prefix). All-zero everywhere means uploads never reached guest image memory.
export function v2DiagImageCensus(F) {
  const out = { images: 0, withPixels: 0, nonZero: 0, byFormat: {} };
  for (const img of F.images?.values?.() || []) {
    out.images++;
    const px = img?.pixels;
    if (!px?.length) continue;
    out.withPixels++;
    const n = Math.min(px.length, 65536);
    let nz = false;
    for (let i = 0; i < n && !nz; i++) if (px[i]) nz = true;
    if (nz) out.nonZero++;
    const f = out.byFormat[img.format] ||= [0, 0];
    f[0]++; if (nz) f[1]++;
  }
  return out;
}
export function v2DepthBiasState(pipe) {
  return pipe.depthBiasEnable ? {
    depthBias: Math.round(pipe.depthBiasConstantFactor || 0),
    depthBiasClamp: pipe.depthBiasClamp || 0,
    depthBiasSlopeScale: pipe.depthBiasSlopeFactor || 0,
  } : { depthBias: 0, depthBiasClamp: 0, depthBiasSlopeScale: 0 };
}
export function v2PrimitiveState(pipe, hasDepthClipControl, indexType = 0) {
  const stripIndexType = pipe.topology === 2 || pipe.topology === 4 ? indexType : undefined;
  return {
    topology: V2_TOPOLOGY[pipe.topology] || 'triangle-list',
    stripIndexFormat: /** @type {GPUIndexFormat|undefined} */ (stripIndexType === undefined ? undefined : stripIndexType ? 'uint32' : 'uint16'),
    cullMode: CULL[pipe.cull] || 'none', frontFace: FRONT[pipe.front] || 'ccw',
    ...(pipe.depthClampEnable && hasDepthClipControl ? {unclippedDepth:true} : {}),
  };
}
export function v2SamplerLayoutType(shaderType) {
  return shaderType === 'sampler_comparison' ? 'comparison' : shaderType === 'sampler' ? 'filtering' : null;
}
export function v2TextureSampleType(textureType, elementType) {
  if (textureType.startsWith('texture_depth')) return 'depth';
  return elementType === 'i32' ? 'sint' : elementType === 'u32' ? 'uint' : 'float';
}
export function v2SamplerDescriptor(s) {
  const filtersLinear = s.mag === 'linear' && s.min === 'linear' && s.mipmap === 'linear';
  const lodMinClamp = Math.max(0, Number.isFinite(s.minLod) ? s.minLod : 0);
  const lodMaxClamp = Math.max(lodMinClamp, Number.isFinite(s.maxLod) ? s.maxLod : 32);
  const descriptor = {
    magFilter: s.mag, minFilter: s.min, mipmapFilter: s.mipmap,
    addressModeU: s.addrU, addressModeV: s.addrV, addressModeW: s.addrW,
    lodMinClamp, lodMaxClamp,
    maxAnisotropy: s.anisotropyEnable && filtersLinear ? Math.max(1, Math.min(16, Math.round(s.maxAnisotropy) || 1)) : 1,
  };
  if (s.compareEnable) descriptor.compare = DEPTH_OP[s.compareOp] || 'always';
  // WebGPU has no mipLodBias field, so this Vulkan value is retained in the record and ignored here.
  // WebGPU has no borderColor field; CLAMP_TO_BORDER was mapped to clamp-to-edge by the decoder.
  return descriptor;
}
const TEX_FORMAT = {
  R8G8B8A8_UNORM: 'rgba8unorm', R8G8B8A8_SRGB: 'rgba8unorm-srgb',
  B8G8R8A8_UNORM: 'bgra8unorm', B8G8R8A8_SRGB: 'bgra8unorm-srgb',
};

// P1's hand-written fragment stage for cube.frag (host/p1render.mjs), vendored so
// the one fragment module naga cannot parse still renders. Keyed on the SPIR-V
// hash the bridge computes over the module bytes PLUS the module size, so a
// different module can never pick this up by hash collision alone.
const FS_OVERRIDES = new Map([['88f8a5cebeefd8c1:1280', `
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
struct FSIn { @location(0) texcoord: vec4f, @location(1) frag_pos: vec3f };
const lightDir = vec3f(0.424, 0.566, 0.707);
fn linearToSrgb(x: f32) -> f32 {
  if (x <= 0.0031308) { return x * 12.92; }
  return 1.055 * pow(x, 1.0 / 2.4) - 0.055;
}
@fragment fn main(in: FSIn) -> @location(0) vec4f {
  let dX = dpdx(in.frag_pos);
  let dY = dpdy(in.frag_pos);
  let normal = normalize(cross(dX, dY));
  let light = max(0.0, dot(lightDir, normal));
  let t = textureSample(tex, samp, in.texcoord.xy);
  let lt = light * t;
  return vec4f(linearToSrgb(lt.r), linearToSrgb(lt.g), linearToSrgb(lt.b), lt.a);
}`]]);

// ---------------------------------------------------------------------------
// Manifest decoding. The host's serializer leaves the `vs`/`fs` wrapper objects
// unterminated after their inline `spv` blob (`jsonBlob(...); j += ","`), so the
// manifest arrives one or two braces short of valid JSON today. parseManifest
// repairs exactly those two sites — and only when the brace count says they are
// genuinely open, which makes the repair a no-op the moment the host closes them.
// ---------------------------------------------------------------------------
function openDepthOutsideStrings(text, end) {
  let depth = 0, inString = false, escaped = false;
  for (let i = 0; i < end; i++) {
    const ch = text[i];
    if (inString) {
      if (escaped) escaped = false;
      else if (ch === '\\') escaped = true;
      else if (ch === '"') inString = false;
    } else if (ch === '"') inString = true;
    else if (ch === '{') depth++;
    else if (ch === '}') depth--;
  }
  return depth;
}

function closeUnterminatedShaderObjects(text) {
  let out = text;
  for (const key of ['"fs":', '"ubo":']) {
    const at = out.indexOf(key);
    if (at < 0 || out[at - 1] !== ',') continue;   // not a sibling member, nothing to close
    if (openDepthOutsideStrings(out, at - 1) > 1) out = out.slice(0, at - 1) + '}' + out.slice(at - 1);
  }
  return out;
}

export function parseManifest(text) {
  if (typeof text !== 'string') throw new Error('vk64: frame manifest is not a string');
  if (!text.length || text.length > MAX_MANIFEST_BYTES) throw new Error('vk64: frame manifest size out of range');
  let json = text;
  try {
    json = JSON.parse(text);
  } catch (first) {
    json = null;
    const repaired = closeUnterminatedShaderObjects(text);
    if (repaired !== text) { try { json = JSON.parse(repaired); } catch { /* fall through */ } }
    if (!json) throw new Error('vk64: frame manifest is not valid JSON (' + (first.message || first) + ')');
  }
  return decodeManifest(json);
}

const fail = why => { throw new Error('vk64: malformed frame manifest (' + why + ')'); };
const num = (v, why) => { if (typeof v !== 'number' || !Number.isFinite(v)) fail(why); return v; };
const int = (v, why) => { num(v, why); if (!Number.isInteger(v)) fail(why); return v; };
const unit = (v, why) => Math.min(1, Math.max(0, num(v, why)));

function decodeManifest(m) {
  if (!m || typeof m !== 'object' || Array.isArray(m)) fail('not an object');
  if (m.overflow) throw new Error('vk64: frame manifest overflowed the guest command limit');
  const width = int(m.width, 'width'), height = int(m.height, 'height');
  if (width < 1 || height < 1 || width > MAX_DIMENSION || height > MAX_DIMENSION) fail('frame size');
  const clear = Array.isArray(m.clearColor) && m.clearColor.length === 4 ? m.clearColor : null;
  if (!clear) fail('clearColor');
  clear.forEach((c, i) => num(c, 'clearColor[' + i + ']'));
  const viewport = Array.isArray(m.viewport) && m.viewport.length === 6 ? m.viewport.map(Number) : [0, 0, width, height, 0, 1];
  viewport.forEach((v, i) => num(v, 'viewport[' + i + ']'));
  const scissor = Array.isArray(m.scissor) && m.scissor.length === 4
    ? m.scissor.map(v => Math.round(num(v, 'scissor'))) : [0, 0, width, height];
  const [sx, sy, sw, sh] = scissor;
  if (sw < 0 || sh < 0 || sx < 0 || sy < 0 || sx + sw > width || sy + sh > height) fail('scissor out of bounds');
  const draws = Array.isArray(m.draws) ? m.draws : fail('draws');
  if (draws.length > MAX_DRAWS) throw new Error('vk64: frame manifest exceeds the page-tier draw limit');
  const sampler = m.sampler && typeof m.sampler === 'object' ? m.sampler : {};
  const tex = m.texture && typeof m.texture === 'object' ? m.texture : null;
  const tw = tex ? int(tex.w, 'texture.w') : 0, th = tex ? int(tex.h, 'texture.h') : 0;
  if (tw < 0 || th < 0 || tw > MAX_DIMENSION || th > MAX_DIMENSION) fail('texture size');
  return {
    frame: int(m.frame ?? 0, 'frame'),
    width, height,
    colorFormat: typeof m.colorFormat === 'string' ? m.colorFormat : 'UNKNOWN',
    clear: clear.map(unit),
    clearDepth: unit(m.clearDepth ?? 1, 'clearDepth'),
    viewport, scissor,
    cull: CULL[m.cull] || 'none',
    front: FRONT[m.front] || 'ccw',
    depthTest: m.depthTest !== 0,
    depthWrite: !!m.depthWrite && m.depthTest !== 0,   // Vulkan: no depth test => no depth writes
    depthOp: DEPTH_OP[m.depthOp] || 'less-equal',
    vs: decodeShader(m.vs, 'vs'),
    fs: decodeShader(m.fs, 'fs'),
    ubo: decodeBlob(m.ubo, 'ubo'),
    texture: tex ? { w: tw, h: th, format: TEX_FORMAT[tex.format] || 'rgba8unorm', pixels: decodeBlob(tex.pixels, 'texture.pixels') } : null,
    sampler: {
      mag: FILTER[sampler.mag] || 'nearest', min: FILTER[sampler.min] || 'nearest',
      mipmap: FILTER[sampler.mipmap] || 'nearest',
      addrU: ADDR[sampler.addrU] || 'clamp-to-edge', addrV: ADDR[sampler.addrV] || 'clamp-to-edge',
      maxAnisotropy: Math.max(1, Math.min(16, Math.round(Number(sampler.maxAniso) || 1))),
    },
    draws: draws.map(d => {
      const vertexCount = int(d?.vertexCount, 'draw.vertexCount'), firstVertex = int(d?.firstVertex ?? 0, 'draw.firstVertex');
      if (vertexCount < 0 || vertexCount > 1 << 20 || firstVertex < 0) fail('draw range');
      return { vertexCount, firstVertex };
    }),
  };
}

function decodeShader(s, which) {
  if (s === null || s === undefined) return null;             // a null stage is legal in the schema
  const code = decodeBlob(s.spv, which + '.spv');
  if (!code) return null;
  return { hash: typeof s.hash === 'string' ? s.hash : '', size: code.length, code };
}

function decodeBlob(blob, which) {
  if (!blob || typeof blob !== 'object' || typeof blob.b64 !== 'string') return null;
  if (blob.b64.length > MAX_MANIFEST_BYTES) throw new Error('vk64: frame manifest blob out of range (' + which + ')');
  const bytes = b64ToBytes(blob.b64);
  if (blob.size !== undefined && int(blob.size, which + '.size') !== bytes.length) fail(which + ' size does not match its payload');
  return bytes;
}

function b64ToBytes(b64) {
  const bin = atob(b64);
  const out = new Uint8Array(bin.length);
  for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
  return out;
}

// ---------------------------------------------------------------------------
// SPIR-V -> WGSL, cached per module (hash:size). Never throws: a module naga
// rejects without an override is reported and the frame is not drawn, because
// this runs under the bridge's MAIN_THREAD_EM_ASM call.
// ---------------------------------------------------------------------------
/**
 * @param {{translate?: (from: string, source: Uint8Array, stage: string, entryPoint?: string, options?: {rasterTopology?: number}) => Promise<string>, log?: (msg: string) => void}} [options]
 */
export function createShaderCache({ translate = toWGSL, log = defaultLog, device = null } = {}) {
  const wgsl = new Map();      // key -> WGSL
  // translateMs measures the SPIR-V -> WGSL translation itself. Modules are
  // cached per hash/topology/spec key, so this is milliseconds per translation
  // EVENT, not milliseconds per frame; `translated` is the event count.
  const stats = { translated: 0, overridden: 0, rejected: 0, hits: 0, persistentHits: 0, translateMs: 0 };
  const persistentEnabled = new URLSearchParams(globalThis.location?.search || '').get('shadercache') === '1';
  const appId = new URLSearchParams(globalThis.location?.search || '').get('installation') || 'unknown';
  if (persistentEnabled && device) {
    const warm = () => warmPersistentShaders(appId, device, (key, code) => { if (key.startsWith('spirv:')) wgsl.set(key.slice(6), code); });
    if (typeof requestIdleCallback === 'function') requestIdleCallback(() => { warm().catch(() => {}); }, { timeout: 1500 });
    else setTimeout(() => warm().catch(() => {}), 0);
  }
  return {
    stats,
    async get(stage, mod, options = {}) {
      if (!mod) return null;
      // Specialization values are part of the cache key: the same SPIR-V
      // module with different spec constants produces different WGSL.
      const specKey = (options.specOverrides || []).map(e => e.constantID + ':' + Array.from(e.value).join(',')).join(';');
      const key = stage + ':' + mod.hash + ':' + mod.size + ':topology:' + (options.rasterTopology ?? 'unknown') + ':spec:' + specKey;
      if (wgsl.has(key)) { stats.hits++; return wgsl.get(key); }
      if (persistentEnabled) {
        const cached = await persistentShaderCache.get('spirv:' + key);
        if (cached?.wgsl) { wgsl.set(key, cached.wgsl); stats.persistentHits++; await persistentShaderCache.record(appId, 'spirv:' + key); return cached.wgsl; }
      }
      const override = FS_OVERRIDES.get(mod.hash + ':' + mod.size);
      if (override) { wgsl.set(key, override); stats.overridden++; return override; }
      const bwTranslateT0 = metricsNow();
      try {
        let code = String(await translate('spirv', mod.code, stage, 'main', options) ?? '');
        // Naga's shortest decimal spelling of FLT_MAX can lie just outside
        // WGSL's accepted f32 range. Preserve its exact binary value when
        // that decimal still rounds to the finite maximum in IEEE binary32.
        code = code.replace(/(?<![\w.])(-?(?:\d+(?:\.\d*)?|\.\d+)(?:e[+-]?\d+)?)f\b/gi, (literal, value) => {
          const n = Number(value), rounded = Math.fround(n), maximum = 3.4028234663852886e38;
          return Math.abs(n) > maximum && Math.abs(rounded) === maximum
            ? `bitcast<f32>(0x${rounded < 0 ? 'ff7fffff' : '7f7fffff'}u)` : literal;
        });
        if (!code) throw new Error('empty translation');
        stats.translateMs += metricsNow() - bwTranslateT0;
        wgsl.set(key, code); stats.translated++;
        if (persistentEnabled) { await persistentShaderCache.put('spirv:' + key, code, { stage, hash: mod.hash, size: mod.size }); await persistentShaderCache.record(appId, 'spirv:' + key); }
        return code;
      } catch (e) {
        stats.translateMs += metricsNow() - bwTranslateT0;
        stats.rejected++;
        log('vk64: page tier cannot translate the ' + stage + ' SPIR-V module (' + (e.formatted || e.message || e) + ') — frame skipped');
        return null;
      }
    },
  };
}

function defaultLog(msg) {
  try { (globalThis.console && console.warn || (() => {}))(msg); } catch { /* logging must never throw */ }
  try { if (typeof globalThis.bwRuntimeLog === 'function') globalThis.bwRuntimeLog(msg); } catch { /* ignore */ }
}

function rememberRecoveryError(errors, value) {
  errors.push(String(value));
  if (errors.length > 32) errors.shift();
}

const metricsNow = () => (typeof performance !== 'undefined' ? performance.now() : Date.now());

// Present pacing for the live panel: real presented-frame timestamps (the page
// tier only completes a frame when it actually reached the canvas). FPS is
// derived from the last N presents, so it is a measured rate, not a guess.
function createFramePacer(window = 60) {
  const times = [];
  return {
    present() {
      times.push(metricsNow());
      while (times.length > window) times.shift();
    },
    snapshot() {
      if (times.length < 2) return { presents: times.length, windowMs: null, fps: null, frameMs: null };
      const span = times[times.length - 1] - times[0];
      return {
        presents: times.length,
        windowMs: span,
        fps: span > 0 ? (times.length - 1) * 1000 / span : null,
        frameMs: span > 0 ? span / (times.length - 1) : null,
      };
    },
  };
}

// ---------------------------------------------------------------------------
// WebGPU timestamp queries: the only source of GENUINE GPU execution time here.
// JS queue.submit time is host-side call cost and is never reported as GPU time
// (web/frame-metrics.mjs labels it "submit (JS)").
//
// Without the adapter's timestamp-query feature the timer reports
// status 'unavailable' with that exact reason; with the feature but zero-valued
// resolutions (some backends quantise to 0) it reports 'inactive', never a
// fabricated number.
// ---------------------------------------------------------------------------
export function createGpuTimer(device, adapter, { log = defaultLog, slots = 4, window = 16 } = {}) {
  const supported = !!(adapter && adapter.features && adapter.features.has && adapter.features.has('timestamp-query'));
  const samples = [];
  const state = {
    status: supported ? 'inactive' : 'unavailable',
    reason: supported ? 'no completed GPU timestamp sample yet' : 'WebGPU adapter does not expose the timestamp-query feature',
    ms: null, lastMs: null, samples: 0, zeroResolutions: 0,
  };
  if (!supported) {
    return {
      supported: false,
      begin: () => null,
      end: () => {},
      resolve: () => {},
      snapshot: () => ({ ...state }),
      destroy: () => {},
    };
  }
  const rings = [];
  for (let i = 0; i < slots; i++) {
    try {
      const querySet = device.createQuerySet({ type: 'timestamp', count: 2 });
      const resolveBuffer = device.createBuffer({ size: 16, usage: GPUBufferUsage.QUERY_RESOLVE | GPUBufferUsage.COPY_SRC });
      const readBuffer = device.createBuffer({ size: 16, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
      rings.push({ querySet, resolveBuffer, readBuffer, busy: false });
    } catch (e) {
      log('vk64: GPU timestamp query setup failed \u2014 ' + (e.message || e));
      break;
    }
  }
  if (!rings.length) {
    state.status = 'unavailable';
    state.reason = 'query set allocation failed on this device';
    return { supported: false, begin: () => null, end: () => {}, resolve: () => {}, snapshot: () => ({ ...state }), destroy: () => {} };
  }
  let destroyed = false;

  function begin(encoder) {
    const ring = rings.find(r => !r.busy);
    if (!ring) return null;
    ring.busy = true;
    try { encoder.writeTimestamp(ring.querySet, 0); } catch (e) {
      ring.busy = false;
      log('vk64: writeTimestamp failed \u2014 ' + (e.message || e));
      return null;
    }
    return ring;
  }
  function end(encoder, ring) {
    if (!ring) return;
    try { encoder.writeTimestamp(ring.querySet, 1); } catch (e) {
      log('vk64: writeTimestamp failed \u2014 ' + (e.message || e));
      ring.busy = false;
    }
  }
  function resolve(ring) {
    if (!ring) return;
    try {
      const enc = device.createCommandEncoder();
      enc.resolveQuerySet(ring.querySet, 0, 2, ring.resolveBuffer, 0);
      enc.copyBufferToBuffer(ring.resolveBuffer, 0, ring.readBuffer, 0, 16);
      device.queue.submit([enc.finish()]);
    } catch (e) {
      log('vk64: GPU timestamp resolve failed \u2014 ' + (e.message || e));
      ring.busy = false;
      return;
    }
    ring.readBuffer.mapAsync(GPUMapMode.READ).then(() => {
      if (destroyed) return;
      // WebGPU timestamps are nanoseconds.
      const view = new BigUint64Array(ring.readBuffer.getMappedRange().slice(0));
      ring.readBuffer.unmap();
      const dt = Number(view[1] - view[0]) / 1e6;
      ring.busy = false;
      if (!(dt > 0)) {
        state.zeroResolutions++;
        state.status = 'inactive';
        state.reason = 'this backend resolved both timestamps to 0 (no GPU clock available)';
        return;
      }
      samples.push(dt);
      while (samples.length > window) samples.shift();
      state.lastMs = dt;
      state.samples = samples.length;
      state.ms = samples.reduce((sum, v) => sum + v, 0) / samples.length;
      state.status = 'measured';
      state.reason = null;
    }).catch(e => {
      ring.busy = false;
      if (destroyed) return;
      state.status = 'inactive';
      state.reason = 'GPU timestamp readback failed: ' + (e.message || e);
    });
  }

  return {
    supported: true,
    begin, end, resolve,
    snapshot: () => ({ ...state }),
    destroy: () => {
      destroyed = true;
      for (const r of rings) {
        try { r.querySet.destroy(); } catch { /* ignore */ }
        try { r.resolveBuffer.destroy(); } catch { /* ignore */ }
        try { r.readBuffer.destroy(); } catch { /* ignore */ }
      }
    },
  };
}

// ---------------------------------------------------------------------------
// createVkRenderer — web/webgpu.mjs:80 createRenderer's shape (adapter -> device
// -> persistent offscreen colour+depth -> per-key pipeline cache ->
// queue.onSubmittedWorkDone() for buffer release) generalised from the
// fixed-function GL subset to the Vulkan frame schema.
// ---------------------------------------------------------------------------
/**
 * @param {HTMLCanvasElement} canvas
 * @param {{translate?: (from: string, source: Uint8Array, stage: string, entryPoint?: string, options?: {rasterTopology?: number}) => Promise<string>, log?: (msg: string) => void}} [options]
 */
export async function createVkRenderer(canvas, { translate, log = defaultLog } = {}) {
  if (!navigator.gpu) throw new Error('WebGPU is unavailable');
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) throw new Error('No WebGPU adapter');
  // The adapter's own feature set (not the v2 renderer's preference list) decides
  // whether genuine GPU timing is possible on this legacy path.
  const requiredFeatures = /** @type {GPUFeatureName[]} */ (['texture-compression-bc', 'depth32float-stencil8', 'float32-filterable', 'clip-distances', 'timestamp-query', 'depth-clip-control'])
    .filter(feature => adapter.features.has(feature));
  const device = await adapter.requestDevice({ requiredFeatures });
  globalThis.bwVkTextureCompressionBC = device.features?.has('texture-compression-bc') === true;
  globalThis.bwVkDepth32FloatStencil8 = device.features?.has('depth32float-stencil8') === true;
  globalThis.bwVkMaxVertexBuffers = Math.min(16, device.limits?.maxVertexBuffers || 8);
  const errors = [];
  device.addEventListener('uncapturederror', e => {
    const message = 'WebGPU validation error: ' + (e.error?.message || e.error);
    rememberRecoveryError(errors, message);
    reportBridgeFailure('vkwebgpu.uncaptured', e.error || new Error(message), log);
  });
  device.lost.then(info => rememberRecoveryError(errors, 'Device lost: ' + info.reason + ' ' + info.message));

  const context = canvas.getContext('webgpu');
  if (!context) throw new Error('No WebGPU canvas context');
  const format = navigator.gpu.getPreferredCanvasFormat();
  context.configure({ device, format, alphaMode: 'opaque', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST });

  const shaders = createShaderCache({ translate, log, device });
  const pipelines = new Map();     // pipeline key -> GPURenderPipeline
  const uniformSize = new Map();   // vs hash -> working uniform range
  const live = [];                 // buffers released after the frame's work is done
  const stats = {
    frames: 0, rendered: 0, skips: 0,
    litPixels: 0, paintedPixels: 0, totalPixels: 0, width: 0, height: 0,
  };
  const snapshot = () => ({ ...stats, ...shaders.stats, recover: bridgeRecoveryStats() });
  // Live metrics for the legacy (v1 manifest) path — same shape as renderer2.
  const pacer = createFramePacer();
  const gpuTimer = createGpuTimer(device, adapter, { log });
  const timing = { renderMs: 0, renderCount: 0, submitMs: 0, submitCount: 0, pipelineMs: 0, pipelineCount: 0, shaderModuleMs: 0, shaderModuleCount: 0 };
  const metrics = () => {
    const pace = pacer.snapshot();
    return {
      presents: pace.presents, windowMs: pace.windowMs, fps: pace.fps, frameMs: pace.frameMs,
      gpu: gpuTimer.snapshot(),
      translate: { ms: shaders.stats.translateMs || 0, count: shaders.stats.translated + shaders.stats.overridden + shaders.stats.rejected, persistentHits: shaders.stats.persistentHits },
      pipeline: { ms: timing.pipelineMs, count: timing.pipelineCount },
      shaderModule: { ms: timing.shaderModuleMs, count: timing.shaderModuleCount },
      submit: { ms: timing.submitMs, count: timing.submitCount },
      render: { ms: timing.renderMs, count: timing.renderCount },
      skipped: stats.skips, unsupported: 0,
    };
  };
  let color = null, depth = null, w = 0, h = 0, destroyed = false;

  function ensureTargets(width, height) {
    if (width === w && height === h && color) return;
    w = width; h = height;
    canvas.width = w; canvas.height = h;
    color?.destroy(); depth?.destroy();
    color = device.createTexture({ size: [w, h], format, usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC });
    depth = device.createTexture({ size: [w, h], format: 'depth24plus', usage: GPUTextureUsage.RENDER_ATTACHMENT });
  }

  // The guest may bind a uniform range smaller than the block its vertex module
  // declares (vkfixture binds 128 bytes against cube.vert's 1216-byte block).
  // WebGPU reports that mismatch only at Draw-encoding time, through an error
  // scope, and never states the size it wants, so the range is grown in powers of
  // two — zero-padded, since WebGPU buffers start zeroed — until the frame
  // validates. The working size is cached per vertex module, so this costs one
  // retry, once, for a guest whose descriptor stays undersized.
  const UNIFORM_TOO_SMALL = /too small|requires a buffer binding which is at least/i;

  async function encodeFrame(man, pipeline, bgl, uniform, texView, sampler) {
    const buffer = device.createBuffer({ size: uniform, usage: GPUBufferUsage.UNIFORM | GPUBufferUsage.COPY_DST });
    if (man.ubo && man.ubo.length) device.queue.writeBuffer(buffer, 0, man.ubo);
    const group = device.createBindGroup({ layout: bgl, entries: [
      { binding: 0, resource: { buffer } },
      { binding: 1, resource: texView },
      { binding: 2, resource: sampler } ] });
    device.pushErrorScope('validation');
    const encoder = device.createCommandEncoder();
    const bwGpuSlot = gpuTimer.begin(encoder);
    /** @type {GPURenderPassDescriptor} */
    const descriptor = man.depthTest
      ? { colorAttachments: [{ view: color.createView(), clearValue: { r: man.clear[0], g: man.clear[1], b: man.clear[2], a: man.clear[3] }, loadOp: 'clear', storeOp: 'store' }],
          depthStencilAttachment: { view: depth.createView(), depthClearValue: man.clearDepth, depthLoadOp: 'clear', depthStoreOp: 'store' } }
      : { colorAttachments: [{ view: color.createView(), clearValue: { r: man.clear[0], g: man.clear[1], b: man.clear[2], a: man.clear[3] }, loadOp: 'clear', storeOp: 'store' }] };
    const pass = encoder.beginRenderPass(descriptor);
    pass.setPipeline(pipeline);
    pass.setBindGroup(0, group);
    const [vx, vy, vw, vh, vmin, vmax] = man.viewport;
    const [sx, sy, sw, sh] = man.scissor;
    pass.setViewport(vx, vy, vw, vh, vmin, vmax);
    pass.setScissorRect(sx, sy, sw, sh);
    for (const d of man.draws) if (d.vertexCount > 0) pass.draw(d.vertexCount, 1, d.firstVertex, 0);
    pass.end();
    // Readback of the offscreen target (COPY_SRC), padded to the 256-byte copy row
    // alignment like P1's consumer, then the present copy to the page canvas.
    const bytesPerRow = Math.ceil(w * 4 / 256) * 256;
    const readback = device.createBuffer({ size: bytesPerRow * h, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
    encoder.copyTextureToBuffer({ texture: color }, { buffer: readback, bytesPerRow }, [w, h]);
    encoder.copyTextureToTexture({ texture: color }, { texture: context.getCurrentTexture() }, [w, h]);
    gpuTimer.end(encoder, bwGpuSlot);
    const bwSubmitT0 = metricsNow();
    device.queue.submit([encoder.finish()]);
    timing.submitMs += metricsNow() - bwSubmitT0;
    timing.submitCount++;
    gpuTimer.resolve(bwGpuSlot);
    return { invalid: await device.popErrorScope(), buffer, readback, bytesPerRow };
  }

  async function render(text) {
    if (destroyed) throw new Error('page tier destroyed');
    const bwRenderT0 = metricsNow();
    const man = parseManifest(text);
    ensureTargets(man.width, man.height);

    const vsWgsl = await shaders.get('vertex', man.vs, { rasterTopology: 3 });
    const fsWgsl = await shaders.get('fragment', man.fs, { rasterTopology: 3 });
    if (!vsWgsl || !fsWgsl) { stats.skips++; return { rendered: false, reason: 'shader-translation' }; }
    const [vx, vy, vw, vh] = man.viewport;
    if (vw <= 0 || vh <= 0) { stats.skips++; return { rendered: false, reason: 'empty viewport' }; }

    const key = [vsWgsl.length, fsWgsl.length, man.cull, man.front, man.depthTest, man.depthWrite, man.depthOp].join(':');
    let pipeline = null, bgl = null;
    if (!pipelines.has(key)) {
      const bwModuleT0 = metricsNow();
      const vsModule = device.createShaderModule({ code: vsWgsl });
      const fsModule = device.createShaderModule({ code: fsWgsl });
      timing.shaderModuleMs += metricsNow() - bwModuleT0; timing.shaderModuleCount += 2;
      device.pushErrorScope('validation');
      const bwPipeT0 = metricsNow();
      bgl = device.createBindGroupLayout({ entries: [
        { binding: 0, visibility: GPUShaderStage.VERTEX, buffer: { type: 'uniform' } },
        { binding: 1, visibility: GPUShaderStage.FRAGMENT, texture: { sampleType: 'float', viewDimension: '2d' } },
        { binding: 2, visibility: GPUShaderStage.FRAGMENT, sampler: { type: 'filtering' } } ] });
      pipeline = device.createRenderPipeline({
        layout: device.createPipelineLayout({ bindGroupLayouts: [bgl] }),
        vertex: { module: vsModule, entryPoint: 'main' },
        fragment: { module: fsModule, entryPoint: 'main', targets: [{ format }] },
        primitive: { topology: 'triangle-list', cullMode: man.cull, frontFace: man.front },
        depthStencil: man.depthTest ? { format: 'depth24plus', depthWriteEnabled: man.depthWrite, depthCompare: man.depthOp } : undefined,
      });
      const err = await device.popErrorScope();
      timing.pipelineMs += metricsNow() - bwPipeT0;
      timing.pipelineCount++;
      if (err) throw new Error('render pipeline rejected: ' + err.message);
      pipelines.set(key, pipeline);
    } else {
      pipeline = pipelines.get(key);
      bgl = pipeline.getBindGroupLayout(0);
    }

    const sampler = device.createSampler({ magFilter: man.sampler.mag, minFilter: man.sampler.min,
      mipmapFilter: man.sampler.mipmap, addressModeU: man.sampler.addrU, addressModeV: man.sampler.addrV,
      maxAnisotropy: man.sampler.maxAnisotropy });
    const guestTexture = man.texture && man.texture.w > 0 && man.texture.h > 0;
    const tex = device.createTexture({ size: guestTexture ? [man.texture.w, man.texture.h] : [1, 1],
      format: man.texture?.format || 'rgba8unorm',
      usage: GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST });
    if (guestTexture && man.texture.pixels) {
      device.queue.writeTexture({ texture: tex }, man.texture.pixels,
        { bytesPerRow: man.texture.w * 4, rowsPerImage: man.texture.h }, [man.texture.w, man.texture.h]);
    } else if (!guestTexture) {
      // No image for the sampler this frame: a 1x1 transparent texture keeps the
      // bind group layout intact, so the draw still goes through.
      device.queue.writeTexture({ texture: tex }, new Uint8Array([0, 0, 0, 0]), { bytesPerRow: 4 }, [1, 1]);
    }
    const texView = tex.createView();

    const uniformKey = man.vs?.hash || '';
    let uniform = Math.max(uniformSize.get(uniformKey) || 0, man.ubo ? man.ubo.length : 0, 256);
    let encoded = null;
    for (let tries = 0; tries <= 8; tries++) {
      encoded = await encodeFrame(man, pipeline, bgl, uniform, texView, sampler);
      if (!encoded.invalid) break;
      const grow = UNIFORM_TOO_SMALL.test(encoded.invalid.message) && uniform < MAX_UNIFORM_RANGE;
      encoded.buffer.destroy(); encoded.readback.destroy();
      if (!grow) break;
      uniform *= 2;
      uniformSize.set(uniformKey, uniform);
    }
    if (encoded.invalid) throw new Error('frame encoding rejected: ' + encoded.invalid.message);
    uniformSize.set(uniformKey, uniform);
    live.push(encoded.buffer, tex);

    // Pass encoding, the submit and the copies are validated asynchronously, so
    // the readback can only be trusted once the error scope came back clean.
    const { readback, bytesPerRow } = encoded;
    await readback.mapAsync(GPUMapMode.READ);
    const rows = new Uint8Array(readback.getMappedRange());
    const [r, g, b, a] = man.clear.map(c => Math.round(c * 255));
    const bgra = format.startsWith('bgra');
    // Two counters, because they answer different questions. painted = pixels the
    // pass actually wrote (a cleared frame region is non-empty but proves nothing
    // about rasterization); lit = pixels that differ from the manifest's clear
    // value, i.e. geometry that survived the pipeline.
    let lit = 0, painted = 0;
    for (let y = 0; y < h; y++) {
      const row = y * bytesPerRow;
      for (let x = 0; x < w; x++) {
        const i = row + x * 4;
        const pr = rows[i], pg = rows[i + 1], pb = rows[i + 2], pa = rows[i + 3];
        if (pr || pg || pb || pa) painted++;
        if (pr !== (bgra ? b : r) || pg !== g || pb !== (bgra ? r : b) || pa !== a) lit++;
      }
    }
    readback.unmap();
    readback.destroy();

    const release = () => { for (const b of live.splice(0)) b.destroy?.(); };
    device.queue.onSubmittedWorkDone().then(release, release);

    stats.frames++; stats.rendered++;
    stats.litPixels = lit; stats.paintedPixels = painted;
    stats.totalPixels = w * h; stats.width = w; stats.height = h;
    pacer.present();
    timing.renderMs += metricsNow() - bwRenderT0;
    timing.renderCount++;
    return { rendered: true, frame: man.frame, litPixels: lit, paintedPixels: painted,
      totalPixels: w * h, uniform, uboBytes: man.ubo ? man.ubo.length : 0, clear: man.clear };
  }

  return {
    render, errors, device, shaders, snapshot, metrics,
    stats,
    get frames() { return stats.rendered; },
    destroy() { destroyed = true; try { gpuTimer.destroy(); } catch { /* ignore */ } context.unconfigure(); color?.destroy(); depth?.destroy(); device.destroy(); },
  };
}

// ---------------------------------------------------------------------------
// installVkPageTier — the page-tier sink the bridge hops into. Serialises frames
// (the SPIR-V translate and the bind-group fit are async, and a guest can present
// again before the previous frame has finished), never throws back at the guest,
// and publishes counters for the gate probe via window.bwVkStats().
// ---------------------------------------------------------------------------
/**
 * @param {{canvas?: HTMLCanvasElement, log?: (msg: string) => void}} [options]
 */
export function installVkPageTier({ canvas, log = defaultLog } = {}) {
  if (globalThis.bwVkTier) return globalThis.bwVkTier;
  let destroyed = false, destroying = null;
  const ownedCanvases = new Set();
  const stopped = () => ({ rendered: false, reason: 'destroyed' });
  const state = { received: 0, rendered: 0, rejected: 0, errors: [], last: null,
    litPixels: 0, paintedPixels: 0, totalPixels: 0 };
  const stats = () => ({ ...state, errors: state.errors.slice(-8), recover: bridgeRecoveryStats(), pipeline: renderer?.snapshot() || null,
    v2: { ...v2state, queue: v2queue.size, pipeline2: renderer2?.snapshot() || null } });
  // Page-tier metrics for the live panel: present pacing plus the host-side
  // stage spans of whichever renderer owns this presentation path. `null`
  // means that renderer has not started yet (the panel shows it inactive).
  const metrics = () => ({
    v1: renderer ? renderer.metrics() : null,
    v2: renderer2 ? renderer2.metrics() : null,
    queue: { size: v2queue.size, dropped: v2queue.dropped },
    received: { chunks: v2state.chunksReceived, frames: v2state.framesReceived },
    rendered: { v1: state.rendered, v2: v2state.rendered },
    rejected: state.rejected + v2state.rejected,
  });
  let renderer = null, pending = Promise.resolve(), starting = null;

  // --- schema v2 (binary framed records): streaming queue + render pump ---
  const v2queue = new V2FrameQueue(V2_QUEUE_CAP);
  const v2present = new V2PresentAssembler();
  const v2asm = new V2FrameAssembler();
  const v2state = { chunksReceived: 0, framesReceived: 0, rendered: 0,
    droppedConsumer: 0, chunkGaps: 0, rejected: 0 };
  let renderer2 = null, starting2 = null, pump2 = Promise.resolve();
  // Pump debouncing: the bridge hops every queued frame's chunks on present
  // (intermediate renders + the presented frame) via separate proxied calls
  // that may be interleaved with event-loop turns. A setTimeout(0) is not
  // enough: the timer can fire before the present batch's later chunks arrive
  // (worker postMessage ordering), rendering a stale intermediate frame and
  // thrashing the canvas size every present. Debounce 8ms so takeLatest()
  // sees the full present batch and keeps only the presented frame.
  let pumpTimer = null;
  function queuePump() {
    if (destroyed) return;
    if (pumpTimer) clearTimeout(pumpTimer);
    pumpTimer = setTimeout(() => {
      pumpTimer = null;
      pump2 = pump2.then(() => pumpChunks());
    }, 8);
  }

  // Canvas-resize hook: frames recorded at the old size would misconfigure
  // the targets, so the queue drains and the targets are invalidated; the
  // next frame re-creates them at the new size.
  function v2Resize() {
    v2queue.clear();
    try { renderer2?.invalidateTargets(); } catch { /* never throw at the observer */ }
  }

  async function frame(json) {
    if (destroyed) return stopped();
    state.received++;
    try {
      if (!renderer) {
        // WebGPU setup is deferred to the first frame so importing this module (and
        // its naga wasm, which shader.mjs initialises on the first translate) costs
        // nothing for a guest that never presents a frame.
        starting ||= createVkRenderer(canvas || defaultCanvas(), { log }).then(r => {
          renderer = r; globalThis.bwVkRenderer = r;
          return r;
        });
        await starting;
      }
      if (destroyed) return stopped();
      const result = await renderer.render(typeof json === 'string' ? json : JSON.stringify(json));
      state.last = result?.rendered ? result : { rendered: false, reason: result?.reason || 'skipped' };
      if (result?.rendered) {
        state.rendered++;   // a frame that was skipped (e.g. untranslatable SPIR-V) is not rendered
        state.litPixels = result.litPixels; state.paintedPixels = result.paintedPixels;
        state.totalPixels = result.totalPixels;
      }
      return result;
    } catch (e) {
      state.rejected++;
      rememberRecoveryError(state.errors, e.message || e);
      reportBridgeFailure('vkwebgpu.frame', e, log);
      return { rendered: false, reason: String(e.message || e) };
    }
  }

  async function pumpChunks() {
    if (destroyed) return stopped();
    if (!renderer2) {
      starting2 ||= createVkRenderer2(canvas || defaultCanvas(), { log, onResize: v2Resize }).then(r => {
        renderer2 = r; globalThis.bwVkRenderer2 = r;
        return r;
      });
      await starting2;
    }
    if (destroyed) return stopped();
    const f = v2queue.takeLatest(); // latest-frame-wins: the guest presenting
    if (!f) return { rendered: false, reason: 'empty' }; // faster than the page renders never builds a backlog
    try {
      // An undersized uniform binding is a size the page did not pad: Dawn
      // names the group, binding, and minimum bytes, so learn it and re-render
      // the same frame. One frame can carry several undersized bindings (DXVK
      // D3D9 has per-stage constant blocks), so retry until nothing new is
      // learned. Without this the frame is dropped and every later frame
      // repeats the failure until the WGSL scan happens to agree with Dawn.
      for (let attempt = 0; ; attempt++) {
        try {
          const result = await renderer2.render(f);
          if (result?.rendered) {
            v2state.rendered++;
            state.last = result;
            state.litPixels = result.litPixels; state.paintedPixels = result.paintedPixels;
            state.totalPixels = result.totalPixels;
          } else {
            v2state.rejected++;
          }
          return result;
        } catch (e) {
          const message = String(e.message || e);
          const learned = attempt < 4 && renderer2.learnMinBindingSize(message);
          if (!learned) {
            v2state.rejected++;
            rememberRecoveryError(state.errors, 'v2: ' + message);
            reportBridgeFailure('vkwebgpu.v2-frame', e, log);
            return { rendered: false, reason: message };
          }
          log('vk64: v2 frame ' + (f?.frameNo ?? '?') + ' retry ' + (attempt + 1) +
            ' after learning min binding size \u2014 ' + message.slice(0, 160));
        }
      }
    } catch (e) {
      v2state.rejected++;
      const message = String(e.message || e);
      rememberRecoveryError(state.errors, 'v2: ' + message);
      reportBridgeFailure('vkwebgpu.v2-frame', e, log);
      return { rendered: false, reason: message };
    }
  }

  // The wasm hop's window.bwVkChunk entry point (one call per chunk). Parsing
  // and queueing are synchronous; the render pump is serialised behind pump2.
  // Never throws back at the guest.
  function chunk(bytes, flags) {
    if (destroyed) return Promise.resolve(stopped());
    v2state.chunksReceived++;
    let parsed;
    try { parsed = parseV2Chunk(bytes); }
    catch (e) {
      v2state.rejected++;
      rememberRecoveryError(state.errors, 'v2 chunk: ' + String(e.message || e));
      reportBridgeFailure('vkwebgpu.v2-chunk', e, log);
      return Promise.resolve({ rendered: false, reason: 'bad-chunk' });
    }
    const recs = v2asm.ingest(parsed);
    v2state.chunkGaps = v2asm.gaps;
    if (!recs) return Promise.resolve({ rendered: false, reason: 'assembling' });
    let decoded;
    try { decoded = decodeV2Frame(recs); }
    catch (e) {
      v2state.rejected++;
      rememberRecoveryError(state.errors, 'v2 frame: ' + String(e.message || e));
      reportBridgeFailure('vkwebgpu.v2-frame-decode', e, log);
      return Promise.resolve({ rendered: false, reason: String(e.message || e) });
    }
    const presented = v2present.push(decoded, flags);
    if (!presented) return Promise.resolve({ rendered: false, reason: 'assembling-present' });
    v2state.framesReceived++;
    v2queue.push(presented); // drop complete presentations, never individual submissions
    v2state.droppedConsumer = v2queue.dropped;
    queuePump();
    return pump2;
  }

  function defaultCanvas() {
    const el = document.createElement('canvas');
    el.id = 'vkCanvas';
    (document.getElementById('display') || document.body).append(el);
    ownedCanvases.add(el);
    return el;
  }

  const tier = {
    frame(json) { pending = pending.then(() => frame(json)); return pending; },
    chunk,
    stats,
    metrics,
    get ready() { return !!renderer; },
    get litPixels() { return state.litPixels; },
    destroy() {
      if (destroying) return destroying;
      destroyed = true;
      if (pumpTimer) clearTimeout(pumpTimer);
      pumpTimer = null;
      v2queue.clear(); v2queue.imageData.clear(); v2queue.bufferData.clear();
      v2present.pending.length = 0; v2asm.records.length = 0;
      if (globalThis.bwVkTier === tier) delete globalThis.bwVkTier;
      if (globalThis.bwVkStats === stats) delete globalThis.bwVkStats;
      if (globalThis.bwVkChunk === acceptChunk) delete globalThis.bwVkChunk;
      // Let a renderer that is initializing or encoding finish before freeing
      // its device. Queued work sees destroyed and never starts another frame.
      destroying = Promise.allSettled([pending, pump2, starting, starting2]).then(() => {
        for (const r of [renderer, renderer2]) {
          if (!r) continue;
          r.destroy();
          if (globalThis.bwVkRenderer === r) delete globalThis.bwVkRenderer;
          if (globalThis.bwVkRenderer2 === r) delete globalThis.bwVkRenderer2;
        }
        renderer = renderer2 = null;
        for (const el of ownedCanvases) el.remove();
        ownedCanvases.clear();
      });
      return destroying;
    },
  };
  globalThis.bwVkTier = tier;
  globalThis.bwVkStats = stats;
  // The wasm bridge hops v2 chunks via window.bwVkChunk(bytes, flags) (see
  // hopV2LastToPage in vk64bridge.cpp). Wire it here so direct EM_ASM delivery
  // works even when the host page bypasses the app.mjs vk2 message path.
  const acceptChunk = (bytes, flags) => {
    bwLogRl(4, 'vulkan', 100, 'v2 chunk received: ' + bytes.length + ' bytes flags=' + flags);
    tier.chunk(bytes, flags);
  };
  globalThis.bwVkChunk = acceptChunk;
  return tier;
}

// MSAA: WebGPU guarantees sampleCount 1 and 4; a DXVK multisampled target
// resolves through a 4x texture into the single-sample target the readback and
// present path already use. Requested counts above 1 clamp to 4.
export function v2PickSampleCount(requested) { return requested > 1 ? 4 : 1; }
export function v2ImageKnown(imageId) {
  return typeof imageId === 'string' && imageId !== V2_UNKNOWN_IMAGE;
}
// Render-target cache key for one attachment. A known VkImage identity keys
// by image so a later pass sampling the same image aliases the texture that
// was rendered into (sampled-image alias replay); otherwise the geometry key
// is used, byte-identical to the pre-identity scheme. Guest image ids are
// never recycled (monotonic allocator in vk64bridge.cpp), so identity keys
// stay valid across frames like the geometry keys.
export function v2ImageTargetKey(imageId, isDepth) {
  return (isDepth ? 'imgd:' : 'img:') + imageId;
}
export function v2TargetKey(imageId, w, h, samples, isDepth) {
  if (v2ImageKnown(imageId)) return v2ImageTargetKey(imageId, isDepth);
  return (isDepth ? 'd' : 'c') + w + 'x' + h + 'x' + samples;
}

// ---------------------------------------------------------------------------
// createVkRenderer2 — the schema-v2 page-tier renderer.
//
// Per frame: for each render pass in the graph, MSAA-aware targets are built
// (multisampled colour + resolveTarget, multisampled depth), the pipeline is
// created from the translated WGSL with bind-group layouts scanned from the
// shader declarations, descriptor sets become bind groups (arrays as resource
// arrays), push constants ride the reserved group-3 UBO, and vertex/index
// binds feed draw/drawIndexed. The last pass's first colour target is copied
// to the canvas and read back for the lit/painted counters.
//
// Colour attachments retain their guest formats. Linear and sRGB views share
// storage so switching views preserves both image contents and colour encoding.
// Depth/stencil attachments retain their formats, masks and independent load/store operations.
//
// Anything the page cannot model (an untranslatable module, an unknown vertex
// format, a second push block) is counted and the frame skipped — the v1
// discipline.
// ---------------------------------------------------------------------------

const V2_STENCIL_OPS = /** @type {Record<number,GPUStencilOperation>} */ ({0:'keep',1:'zero',2:'replace',3:'increment-clamp',4:'decrement-clamp',5:'invert',6:'increment-wrap',7:'decrement-wrap'});
// VkFormats the bridge advertises, including formats represented through CPU
// conversion (packed 16-bit colours, R16 UNORM and D32/S8 fallbacks). BC is
// included because the bridge enables textureCompressionBC; v2TextureFormat
// still rejects it when the WebGPU adapter lacks texture-compression-bc.
export const V2_SUPPORTED_TEXTURE_FORMATS = Object.freeze([
  2, 3, 5, 7, 8, 9, 16, 37, 38, 41, 43, 44, 50, 64, 70, 76, 77, 83,
  91, 97, 98, 99, 100, 103, 109, 122, 124, 125, 126, 129, 130,
  131, 132, 133, 134, 135, 136, 137, 138, 139, 140, 141, 142, 143, 144, 145, 146,
]);
export const V2_SUPPORTED_VERTEX_FORMATS = Object.freeze([
  16, 17, 18, 19, 20, 21, 23, 24, 25, 26, 27, 28, 37, 38, 39, 40, 41, 42,
  44, 64, 77, 78, 79, 80, 81, 82, 83, 84, 85, 86, 87, 88, 89, 90, 91, 92,
  93, 94, 95, 96, 97, 98, 99, 100, 101, 102, 103, 104, 105, 106, 107, 108, 109,
]);
export const V2_COLOR_ATTACHMENT_FORMATS = Object.freeze([
  2, 3, 5, 7, 8, 9, 16, 37, 41, 43, 44, 50, 64, 70, 76, 77, 83, 91, 97,
  98, 99, 100, 103, 109,
]);
export const V2_COLOR_ATTACHMENT_BLEND_FORMATS = Object.freeze([
  2, 3, 5, 7, 8, 9, 16, 37, 43, 44, 50, 64, 70, 76, 77, 83, 91, 97,
]);
export const V2_DEPTH_STENCIL_FORMATS = Object.freeze([124, 125, 126, 129, 130]);
function v2StencilPipeline(pipe, dynamic) {
  if (!pipe.stencil?.enabled) return pipe;
  const stencil = {...pipe.stencil, front:{...pipe.stencil.front}, back:{...pipe.stencil.back}};
  for (const [kind,name] of ['compareMask','writeMask','reference'].entries()) {
    if (!(stencil.dynamicMask & (1 << kind))) continue;
    for (const [index,face] of [stencil.front,stencil.back].entries()) {
      if ((index === 0 && pipe.cull === 1) || (index === 1 && pipe.cull === 2)) continue;
      if (dynamic[kind][index] === undefined) throw new Error('missing dynamic stencil '+name);
      face[name] = dynamic[kind][index];
    }
  }
  return {...pipe,stencil};
}
// Intersect a Vulkan scissor with the pass extent (WebGPU requires containment).
// Returns null when nothing remains.
function clampScissor(sc, width, height) {
  const x0 = Math.max(0, sc[0] | 0), y0 = Math.max(0, sc[1] | 0);
  const x1 = Math.min(width, (sc[0] | 0) + (sc[2] | 0)), y1 = Math.min(height, (sc[1] | 0) + (sc[3] | 0));
  return x1 > x0 && y1 > y0 ? [x0, y0, x1 - x0, y1 - y0] : null;
}

function v2StencilState(pipe, depthFormat) {
  if (!pipe.stencil?.enabled) return {state:{},reference:null};
  if (!depthFormat.includes('stencil')) throw new Error('stencil pipeline requires a stencil attachment');
  const {front,back}=pipe.stencil;
  const active=pipe.cull===1?[back]:pipe.cull===2?[front]:[front,back];
  for (const field of ['compareMask','writeMask','reference'])
    if (active.some(f=>(f[field]&255)!==(active[0][field]&255))) throw new Error('incompatible active stencil face '+field);
  const face=f=>{
    if (!V2_STENCIL_OPS[f.failOp] || !V2_STENCIL_OPS[f.passOp] || !V2_STENCIL_OPS[f.depthFailOp] || !DEPTH_OP[f.compareOp]) throw new Error('unsupported stencil operation');
    return {compare:DEPTH_OP[f.compareOp],failOp:V2_STENCIL_OPS[f.failOp],passOp:V2_STENCIL_OPS[f.passOp],depthFailOp:V2_STENCIL_OPS[f.depthFailOp]};
  };
  return {state:{stencilFront:face(front),stencilBack:face(back),stencilReadMask:active[0].compareMask&255,stencilWriteMask:active[0].writeMask&255},reference:active[0].reference&255};
}

// Vulkan and WebGPU use the same component write-mask bits. Keep blend
// equations per attachment: projected shadows multiply destination colour.
const V2_BLEND_FACTORS = /** @type {Record<number, GPUBlendFactor>} */ ({
  0: 'zero', 1: 'one', 2: 'src', 3: 'one-minus-src', 4: 'dst', 5: 'one-minus-dst',
  6: 'src-alpha', 7: 'one-minus-src-alpha', 8: 'dst-alpha', 9: 'one-minus-dst-alpha',
  10: 'constant', 11: 'one-minus-constant', 12: 'constant', 13: 'one-minus-constant',
  14: 'src-alpha-saturated',
});
const V2_BLEND_OPS = /** @type {Record<number, GPUBlendOperation>} */ ({0:'add',1:'subtract',2:'reverse-subtract',3:'min',4:'max'});
/** @returns {GPUBlendComponent} */
function v2BlendComponent(src, dst, op, alpha) {
  if (!V2_BLEND_OPS[op]) throw Error('unsupported-blend-op:' + op);
  const factor = value => {
    if (alpha && value === 14) return 'one';
    if (!V2_BLEND_FACTORS[value]) throw Error('unsupported-blend-factor:' + value);
    return V2_BLEND_FACTORS[value];
  };
  return { operation: V2_BLEND_OPS[op], srcFactor: op >= 3 ? 'one' : factor(src), dstFactor: op >= 3 ? 'one' : factor(dst) };
}
/** @returns {GPUColorTargetState} */
function v2BlendTarget(pipe, index, format) {
  const b = pipe.colorBlend?.[index];
  if (!b) return { format, blend: pipe.blend ? {
    color: { srcFactor: 'src-alpha', dstFactor: 'one-minus-src-alpha' },
    alpha: { srcFactor: 'one', dstFactor: 'one-minus-src-alpha' },
  } : undefined };
  return { format, writeMask: b.writeMask & 15, blend: b.enable ? {
    color: v2BlendComponent(b.srcColor, b.dstColor, b.colorOp, false),
    alpha: v2BlendComponent(b.srcAlpha, b.dstAlpha, b.alphaOp, true),
  } : undefined };
}
function v2BlendConstantMode(pipe) {
  let color = false, alpha = false, used = false;
  for (const b of pipe.colorBlend || []) if (b.enable) {
    const colorFactors = b.colorOp < 3 ? [b.srcColor,b.dstColor] : [];
    const alphaFactors = b.alphaOp < 3 ? [b.srcAlpha,b.dstAlpha] : [];
    for (const f of colorFactors) {
      color ||= f === 10 || f === 11; alpha ||= f === 12 || f === 13;
    }
    used ||= [...colorFactors,...alphaFactors].some(f => f >= 10 && f <= 13);
  }
  if (color && alpha) throw Error('unsupported-mixed-blend-constants');
  return used ? (alpha ? 'alpha' : 'color') : null;
}

/**
 * @param {HTMLCanvasElement} canvas
 * @param {{translate?: (from: string, source: Uint8Array, stage: string, entryPoint?: string, options?: {rasterTopology?: number}) => Promise<string>, log?: (msg: string) => void, onResize?: () => void}} [options]
 */
export async function createVkRenderer2(canvas, { translate, log = defaultLog, onResize } = {}) {
  if (!navigator.gpu) throw new Error('WebGPU is unavailable');
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) throw new Error('No WebGPU adapter');
  const requiredFeatures = /** @type {GPUFeatureName[]} */ (['texture-compression-bc', 'depth32float-stencil8', 'float32-filterable', 'clip-distances', 'timestamp-query', 'depth-clip-control', 'texture-formats-tier1', 'norm16'])
    .filter(feature => adapter.features.has(feature));
  // The bridge advertises Vulkan limits independently of the adapter (see
  // fillLimits in vk64bridge.cpp), so ask for everything this adapter has
  // instead of WebGPU's conservative defaults (2048 px textures, 4 buffers).
  /** @type {Record<string, number>} */
  const requiredLimits = {};
  if (adapter.limits) for (const key of REQUIRED_LIMIT_KEYS) requiredLimits[key] = adapter.limits[key];
  const device = await adapter.requestDevice({ requiredFeatures, requiredLimits });
  globalThis.bwVkTextureCompressionBC = device.features?.has('texture-compression-bc') === true;
  globalThis.bwVkDepth32FloatStencil8 = device.features?.has('depth32float-stencil8') === true;
  globalThis.bwVkMaxVertexBuffers = Math.min(16, device.limits?.maxVertexBuffers || 8);
  const errors = [];
  device.addEventListener('uncapturederror', e => {
    const message = 'WebGPU validation error: ' + (e.error?.message || e.error);
    rememberRecoveryError(errors, message);
    reportBridgeFailure('vkwebgpu.uncaptured', e.error || new Error(message), log);
  });
  device.lost.then(info => rememberRecoveryError(errors, 'Device lost: ' + info.reason + ' ' + info.message));

  const context = canvas.getContext('webgpu');
  if (!context) throw new Error('No WebGPU canvas context');
  const format = navigator.gpu.getPreferredCanvasFormat();
  context.configure({ device, format, alphaMode: 'opaque', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST });

  const shaders = createShaderCache({ translate, log, device });
  const pipelines = new Map();   // pipeId:msN -> { pipeline, layouts, decls, hasPush, slotOf, maxGroup, key }
  const blitPipelines = new Map();
  let presentPipeline = null;
  const targets = new Map();     // targetKey -> GPUTexture (invalidated on resize; img:/imgd: keys alias by VkImage)
  const stats = {
    frames: 0, rendered: 0, skips: 0, unknownFormats: 0, unsupported: 0,
    litPixels: 0, paintedPixels: 0, totalPixels: 0, width: 0, height: 0, aliased: 0,
  };
  // Live metrics (web/frame-metrics.mjs): page-tier spans. These are JS-side
  // measurements and are labelled as such in the panel; GPU execution time comes
  // only from the timestamp timer below.
  const pacer = createFramePacer();
  const gpuTimer = createGpuTimer(device, adapter, { log });
  const timing = { renderMs: 0, renderCount: 0, submitMs: 0, submitCount: 0, pipelineMs: 0, pipelineCount: 0, shaderModuleMs: 0, shaderModuleCount: 0 };
  // "No silent drops": every command the replay ignores, substitutes or degrades is counted per
  // reason and logged on a power-of-two schedule (first 4 occurrences, then the 8th, 16th, ...).
  const dropReasons = new Map();
  function noteDrop(reason, detail = '') {
    const n = (dropReasons.get(reason) || 0) + 1;
    dropReasons.set(reason, n);
    if (n <= 4 || (n & (n - 1)) === 0) bwLog(1, 'vulkan', 'v2 dropped ' + reason + ' x' + n + (detail ? ' (' + String(detail).slice(0, 300) + ')' : ''));
    return n;
  }
  const noteUnsupported = (reason, detail) => { stats.unsupported++; return noteDrop(reason, detail); };
  const noteSkippedDraw = (reason, detail) => { stats.skippedDraws = (stats.skippedDraws || 0) + 1; return noteDrop(reason, detail); };
  const snapshot = () => ({ ...stats, ...shaders.stats, persist: { ...persistStats }, recover: bridgeRecoveryStats(), dropReasons: Object.fromEntries(dropReasons) });
  // Page-tier metrics for the live panel: every field is measured on the page.
  const metrics = () => {
    const pace = pacer.snapshot();
    return {
      presents: pace.presents, windowMs: pace.windowMs, fps: pace.fps, frameMs: pace.frameMs,
      gpu: gpuTimer.snapshot(),
      translate: { ms: shaders.stats.translateMs || 0, count: shaders.stats.translated + shaders.stats.overridden + shaders.stats.rejected, persistentHits: shaders.stats.persistentHits },
      pipeline: { ms: timing.pipelineMs, count: timing.pipelineCount },
      shaderModule: { ms: timing.shaderModuleMs, count: timing.shaderModuleCount },
      submit: { ms: timing.submitMs, count: timing.submitCount },
      render: { ms: timing.renderMs, count: timing.renderCount },
      skipped: stats.skips, unsupported: stats.unsupported,
    };
  };
  let destroyed = false;
  let resizeObs = null;
  // Track the canvas size the observer has seen. render() updates this
  // synchronously when IT resizes the canvas, so the observer's async fire
  // (initial observation or render()'s own resize) is ignored; only a real
  // external resize triggers onResize(). Without this, the observer fires
  // during render()'s awaits and invalidateTargets() destroys in-flight
  // textures ("destroyed texture used in a submit").
  let obsW = canvas.width, obsH = canvas.height;
  if (typeof ResizeObserver !== 'undefined' && onResize) {
    resizeObs = new ResizeObserver(() => { try {
      if (canvas.width !== obsW || canvas.height !== obsH) {
        obsW = canvas.width; obsH = canvas.height;
        onResize();
      }
    } catch { /* never throw at the observer */ } });
    try { resizeObs.observe(canvas); } catch { /* ignore */ }
  }

  function invalidateTargets() {
    for (const t of targets.values()) { try { t.destroy(); } catch { /* ignore */ } }
    targets.clear();
    persistGroupCache.clear();
  }

  // Minimum uniform binding sizes Dawn enforces, learned from WebGPU
  // rejections and merged with the WGSL-derived sizes. "g:b" -> bytes. The
  // WGSL scan misses some layouts (nested arrays, multi-stage blocks), so the
  // error message is the authoritative source; a frame rejected with "binding
  // is too small" is retried once the size is learned.
  const uniformMinSize = new Map();
  function learnMinBindingSize(message) {
    const learned = parseMinBindingSize(message);
    if (!learned) return false;
    const key = learned.group + ':' + learned.binding;
    const prior = uniformMinSize.get(key) || 0;
    if (learned.bytes > prior) { uniformMinSize.set(key, learned.bytes); return true; }
    return false;
  }
  function minUniformSize(group, binding, dMinSize) {
    return Math.max(dMinSize || 0, uniformMinSize.get(group + ':' + binding) || 0);
  }

  let frameTargetSizes = new Map();
  function colorStorage(viewFormat) {
    const base = viewFormat.replace(/-srgb$/, '');
    return { format: base, viewFormats: ['rgba8unorm', 'bgra8unorm'].includes(base) ? [base + '-srgb'] : [] };
  }
  function colorMsKey(imageId, w, h, samples, slot) {
    return v2TargetKey(imageId, w, h, samples, false) + ':ms' + samples + (v2ImageKnown(imageId) ? '' : ':slot' + slot);
  }
  function target(key, desc) {
    const size = frameTargetSizes.get(key);
    const imageId = /^imgd?:([0-9a-f]{16})(?:$|:)/.exec(key)?.[1];
    const image = imageId && F.images.get(imageId);
    if (size) {
      const dims = desc.size || size;
      desc = { ...desc, size: [Math.max(size[0], dims[0]), Math.max(size[1], dims[1]), ...(dims[2] ? [dims[2]] : [])] };
    }
    if (image && !desc.sampleCount) {
      const dims = desc.size || [image.w, image.h];
      desc = { ...desc, mipLevelCount: Math.max(1, image.mips || 1),
        size: [Math.max(dims[0], image.w), Math.max(dims[1], image.h), Math.max(dims[2] || 0, image.layers || 1)] };
    }
    let t = targets.get(key);
    if (!t) { t = device.createTexture({ ...desc, label: key }); targets.set(key, t); }
    return t;
  }
  function imageBlitPipeline(format, filter) {
    const key = format + ':' + filter;
    let rec = blitPipelines.get(key);
    if (rec) return rec;
    const linear = filter === 1;
    const sample = linear ? `
 let dim = vec2<i32>(textureDimensions(source, 0));
 let p = clamp(input.uv * vec2<f32>(dim) - vec2<f32>(0.5), vec2<f32>(0.0), vec2<f32>(dim - vec2<i32>(1)));
 let i = vec2<i32>(floor(p)); let f = fract(p);
 let a = textureLoad(source, i, 0); let b = textureLoad(source, min(i + vec2<i32>(1, 0), dim - vec2<i32>(1)), 0);
 let c = textureLoad(source, min(i + vec2<i32>(0, 1), dim - vec2<i32>(1)), 0);
 let d = textureLoad(source, min(i + vec2<i32>(1, 1), dim - vec2<i32>(1)), 0);
 return mix(mix(a, b, f.x), mix(c, d, f.x), f.y);` : `
 let dim = vec2<i32>(textureDimensions(source, 0));
 let p = clamp(vec2<i32>(floor(input.uv * vec2<f32>(dim))), vec2<i32>(0), dim - vec2<i32>(1));
 return textureLoad(source, p, 0);`;
    const module = device.createShaderModule({ code: `
struct Params { sourceRect: vec4<f32> };
struct Out { @builtin(position) position: vec4<f32>, @location(0) uv: vec2<f32> };
@group(0) @binding(0) var source: texture_2d<f32>;
@group(0) @binding(1) var<uniform> params: Params;
@vertex fn vs(@builtin(vertex_index) i: u32) -> Out {
 let p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
 let uv = array<vec2<f32>, 3>(vec2<f32>(0.0, 0.0), vec2<f32>(2.0, 0.0), vec2<f32>(0.0, 2.0));
 var out: Out; out.position = vec4<f32>(p[i], 0.0, 1.0); out.uv = params.sourceRect.xy + uv[i] * params.sourceRect.zw; return out;
}
@fragment fn fs(input: Out) -> @location(0) vec4<f32> {${sample}
}` });
    rec = device.createRenderPipeline({ layout: 'auto', vertex: { module, entryPoint: 'vs' },
      fragment: { module, entryPoint: 'fs', targets: [{ format }] }, primitive: { topology: 'triangle-list' } });
    blitPipelines.set(key, rec);
    return rec;
  }

  // Frame-local resource caches, reset by render().
  const F = {
    shaders: new Map(), images: new Map(), views: new Map(), viewFormats: new Map(), viewRanges: new Map(), samplers: new Map(), buffers: new Map(),
    texCache: new Map(), bufCache: new Map(), sampCache: new Map(), live: [],
  };

  // Persistent cross-frame GPU resource caches (optimization: avoid per-frame
  // re-creation of textures/buffers/samplers when content is unchanged).
  // Keyed by content hash; entries are never destroyed (bounded by unique
  // resources, which is small for real apps).
  const persistTexCache = new Map();  // imgHex:hash -> {tex, w, h, format}
  const persistBufCache = new Map();  // bufHex:usage:hash -> GPUBuffer
  // Content hash -> GPUBuffer, independent of the VkBuffer id. A byte-less
  // record names content the page may have uploaded under another buffer id
  // (or in an earlier presentation); this lets it recover the real bytes.
  const persistBufByHash = new Map(); // hash:usage -> GPUBuffer
  const persistSampCache = new Map(); // sampHex -> GPUSampler
  const persistStats = { texHits: 0, texMiss: 0, bufHits: 0, bufMiss: 0, bufHashHits: 0, bufUnresolved: 0, sampHits: 0, groupHits: 0, groupMiss: 0 };
  let persistDummyVb = null;
  // Persistent bind-group cache: pipeKey|group|fingerprint -> GPUBindGroup.
  // The fingerprint captures descriptor content (resource ids + content
  // hashes), so identical sets reuse the bind group across frames instead of
  // re-creating it per draw. Cleared with the render targets (a bind group
  // may reference a destroyed target view).
  const persistGroupCache = new Map();
  const PERSIST_GROUP_CACHE_MAX = 4096;
  // Readback sampling: the full-frame copyTextureToBuffer + mapAsync + CPU
  // pixel loop exists only for lit/painted stats. Sampling every Nth frame
  // avoids a GPU->CPU sync stall on every frame. Probes needing per-frame
  // stats set window.__bwStatsEvery = 1.
  const STATS_EVERY = (typeof window !== 'undefined' && window.__bwStatsEvery > 0)
    ? Math.max(1, Math.floor(window.__bwStatsEvery)) : 30;
  // Shared zero buffer for missing uniform bindings (was allocated per draw).
  const ZERO4096 = new Uint8Array(4096);
  const ZERO64K = new Uint8Array(65536);

  // Fast FNV-1a hash for Uint8Array content comparison.
  function fnv1a(bytes) {
    let h = 0x811c9dc5;
    for (let i = 0; i < bytes.length; i++) {
      h ^= bytes[i];
      h = Math.imul(h, 0x01000193);
    }
    return (h >>> 0).toString(16);
  }

  // Texture cache key. The content hash arrives in the IMAGE_DATA record
  // (FNV-1a 64-bit, computed wasm-side), so pixel-less dedup records resolve
  // without re-hashing pixel bytes on the page. Pass counters=null to skip
  // unknown-format accounting (used by the bind-group fingerprint, which must
  // not double-count).
  function textureKey(imgHex, img, counters, cube = false, dimension = '2d') {
    const w = Math.max(1, img.w), h = Math.max(1, img.h);
    const fmt = v2TextureFormat(img.format, counters === undefined ? stats : counters, device.features);
    const hash = img.hash || (img.pixels && img.pixels.length ? fnv1a(img.pixels) : 'empty');
    const d = dimension === '3d' ? Math.max(1, img.d || 1) : 1;
    // A cube view needs 6 layers; a cube-array view a multiple of 6.
    const layers = dimension === '3d' ? 1 : cube ? Math.max(6, Math.ceil((img.layers || 1) / 6) * 6) : Math.max(1, img.layers || 1);
    const mips = Math.max(1, img.mips || 1);
    return { ck: imgHex + ':' + w + 'x' + h + ':' + fmt + ':' + hash + ':' + dimension + ':' + d + ':' + layers + ':' + mips, w, h, d, layers, mips, fmt };
  }

  /** @param {string} imgHex @param {any} img @param {boolean} [cube] @param {string} [dimension] */
  function gpuTextureFor(imgHex, img, cube = false, dimension = '2d') {
    // Persistent cache: reuse GPUTexture when content is unchanged.
    const { ck, w, h, d, layers, mips, fmt } = textureKey(imgHex, img, undefined, cube, dimension);
    if (!fmt) return null;
    let entry = persistTexCache.get(ck);
    if (entry) { persistStats.texHits++; return entry.tex; }
    persistStats.texMiss++;
    if (w >= 1024 || h >= 1024) {
      persistStats.largeTexFormats ||= {};
      const key = `${w}x${h}:${img.format}->${fmt}`;
      persistStats.largeTexFormats[key] = (persistStats.largeTexFormats[key] || 0) + 1;
    }
    const textureDimension = dimension === '3d' ? '3d' : dimension === '1d' ? '1d' : '2d';
    const slices = dimension === '3d' ? d : layers;
    let usage = GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_DST | GPUTextureUsage.COPY_SRC;
    if (V2_COLOR_ATTACHMENT_FORMATS.includes(img.format)) usage |= GPUTextureUsage.RENDER_ATTACHMENT;
    const t = device.createTexture({
      size: [w, h, slices], format: fmt, dimension: textureDimension, mipLevelCount: mips, usage,
    });
    if (img.pixels && img.pixels.length) {
      const pixels = v2TexturePixels(img.format, img.pixels, device.features);
      const plan = v2TextureMipUploadPlan(img, fmt, dimension);
      for (const subresource of plan) {
        const src = pixels.subarray(subresource.offset, subresource.offset + subresource.byteLength);
        const origin = { x: 0, y: 0, z: dimension === '3d' ? 0 : subresource.layer };
        const extent = subresource.extent;
        if (fmt.startsWith('depth')) {
          // Vulkan packs guest depth/stencil data by format. WebGPU requires a
          // separate upload for each copyable aspect; opaque depth24plus values
          // stay available for render passes but cannot be initialized by upload.
          const { uploads, skippedDepth } = v2DepthStencilUploads(img.format, src, device.features);
          if (skippedDepth) noteUnsupported('depth-upload-opaque-format', img.format);
          for (const upload of uploads) device.queue.writeTexture({ texture: t, mipLevel: subresource.mip, origin, aspect: upload.aspect }, upload.pixels,
            { bytesPerRow: subresource.bytesPerRow / (subresource.extent[0] || 1) * upload.bytesPerTexel, rowsPerImage: subresource.rowsPerImage }, extent);
        } else {
          device.queue.writeTexture({ texture: t, mipLevel: subresource.mip, origin }, src,
            { bytesPerRow: subresource.bytesPerRow, rowsPerImage: subresource.rowsPerImage }, extent);
        }
      }
    }
    persistTexCache.set(ck, { tex: t, w, h, format: fmt });
    // Also populate frame-local cache for within-frame dedup
    F.texCache.set(imgHex, t);
    return t;
  }

  // Buffer cache key (shared with the bind-group fingerprint).
  function bufferKey(bufHex, bytes, usage, effLen, preHash) {
    const hash = preHash || (bytes ? fnv1a(bytes) : 'empty');
    return bufHex + ':' + usage + ':' + hash;
  }

  function gpuBufferFor(bufHex, bytes, usage, preHash) {
    // Persistent cache: hash content; reuse GPUBuffer if unchanged.
    // preHash (from wasm dedup) avoids re-hashing; bytes may be null.
    const ck = bufferKey(bufHex, bytes, usage, bytes ? bytes.length : 0, preHash);
    let b = persistBufCache.get(ck);
    if (b) { persistStats.bufHits++; return b; }
    if (!bytes) {
      // Deduped record whose full payload the page never received (the
      // producer used to drop complete frames when its chunk ring filled).
      // The bridge hash names the content, so recover any earlier upload of
      // the same bytes even under a different VkBuffer id.
      const hk = preHash ? preHash + ':' + usage : null;
      b = hk ? persistBufByHash.get(hk) : null;
      if (b) { persistStats.bufHashHits++; persistBufCache.set(ck, b); return b; }
      return null;
    }
    persistStats.bufMiss++;
    // A valid uint16 index stream can end on a two-byte boundary. WebGPU
    // requires mapped allocations to be a multiple of four bytes.
    b = device.createBuffer({ size: Math.max(4, Math.ceil(bytes.length / 4) * 4), usage, mappedAtCreation: true });
    new Uint8Array(b.getMappedRange()).set(bytes.subarray(0, Math.min(bytes.length, b.size)));
    b.unmap();
    persistBufCache.set(ck, b);
    if (preHash) persistBufByHash.set(preHash + ':' + usage, b);
    F.bufCache.set(bufHex + ':' + usage, b);
    return b;
  }

  function gpuSamplerFor(sampHex, s) {
    // Persistent cache: samplers are defined by parameters, never change.
    let sm = persistSampCache.get(sampHex);
    if (sm) { persistStats.sampHits++; return sm; }
    sm = device.createSampler(v2SamplerDescriptor(s));
    persistSampCache.set(sampHex, sm);
    F.sampCache.set(sampHex, sm);
    return sm;
  }

  /** @param {boolean} [cube] @param {string} [dimension] */
  let blankUses = 0; // diagnostic: placeholder textures handed out instead of a real image
  const blankTex = (cube = false, dimension = '2d', depth = false) => {
    blankUses++;
    const key = (cube ? '\0blank-cube' : dimension !== '2d' ? '\0blank-' + dimension : '\0blank') + (depth ? '-depth' : '');
    // Persistent bind groups retain this view between frames. Its texture
    // must live as long as the renderer, like ordinary cached image textures.
    let entry = persistTexCache.get(key);
    if (!entry) {
      const layers = cube ? 6 : 1;
      const textureDimension = dimension === '3d' ? '3d' : dimension === '1d' ? '1d' : '2d';
      // A shader sampling texture_depth_* only accepts a depth-format view
      // (WebGPU sample type 'depth'); a colour placeholder invalidates the
      // whole bind group. Depth textures start zeroed and cannot be written
      // with writeTexture, so the zero fill is the placeholder value.
      const t = device.createTexture({ size: [1, 1, layers], dimension: textureDimension, format: depth ? 'depth32float' : 'rgba8unorm', usage: GPUTextureUsage.TEXTURE_BINDING | (depth ? 0 : GPUTextureUsage.COPY_DST) });
      if (!depth) device.queue.writeTexture({ texture: t }, new Uint8Array(4 * layers), { bytesPerRow: 4, rowsPerImage: 1 }, [1, 1, layers]);
      entry = { tex: t }; persistTexCache.set(key, entry);
    }
    return entry.tex;
  };

  /** @returns {GPUBindGroupLayoutEntry | null} */
  function layoutEntryFor(decl) {
    const vis = GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT;
    const t = decl.type;
    if (t.startsWith('texture_') && !t.startsWith('texture_storage')) {
      const elem = /texture_\w+<(\w+)>/.exec(t)?.[1] || 'f32';
      const sampleType = v2TextureSampleType(t, elem);
      const viewDimension = wgslTextureViewDimension(t) || '2d';
      return { binding: decl.binding, visibility: vis, texture: { sampleType, viewDimension, multisampled: false } };
    }
    const samplerType = v2SamplerLayoutType(t);
    if (samplerType) return { binding: decl.binding, visibility: vis, sampler: { type: samplerType } };
    if (decl.addr === 'uniform')
      return { binding: decl.binding, visibility: vis, buffer: { type: 'uniform' } };
    if (decl.addr.startsWith('storage')) {
      const ro = !decl.addr.includes('read_write');
      return { binding: decl.binding, visibility: vis, buffer: { type: ro ? 'read-only-storage' : 'storage' } };
    }
    const sm = /texture_storage_\w+<\s*([\w-]+)\s*,\s*(\w+)/.exec(t);
    if (sm) return { binding: decl.binding, visibility: vis, storageTexture: { access: sm[2].includes('write') ? 'write-only' : 'read-only', format: /** @type {GPUTextureFormat} */ (sm[1]), viewDimension: '2d' } };
    return null;
  }

  async function getPipelineFor(pipe, sampleCount, unnegated = false, passHasDepth = false, indexType = 0, colorFormats = [format], depthFormat = 'depth24plus', depthAspects = 6) {
    // WebGPU requires the pipeline's depthStencil state to match the render
    // pass attachments: a pipeline without depthStencil cannot be set in a
    // pass with a depth attachment (Vulkan allows it). Key the cache on the
    // pass depth state and always emit a (possibly disabled) depthStencil
    // when the pass has depth.
    const stripIndexType = pipe.topology === 2 || pipe.topology === 4 ? indexType : undefined;
    const key = pipe.pipeId + ':ms' + sampleCount + ':topology:' + pipe.topology + ':vb:' + JSON.stringify(pipe.vb) + ':yny' + (unnegated ? '1' : '0') + ':pd' + (passHasDepth ? '1' : '0') + ':dt' + (pipe.depthTest ? '1' : '0') + ':dw' + (pipe.depthWrite ? '1' : '0') + ':do' + (pipe.depthOp | 0) + ':si' + (stripIndexType ?? 'none') + ':colors' + colorFormats.join(',') + ':blend:' + JSON.stringify(pipe.colorBlend ?? pipe.blend) + ':cf:' + pipe.cull + '/' + pipe.front + ':depth:' + depthFormat + ':stencil:' + JSON.stringify(pipe.stencil) + ':db:' + JSON.stringify([pipe.depthBiasEnable,pipe.depthBiasConstantFactor,pipe.depthBiasClamp,pipe.depthBiasSlopeFactor,pipe.depthClampEnable]) + ':aspects:' + depthAspects;
    if (pipelines.has(key)) return pipelines.get(key);
    const vsm = F.shaders.get(pipe.vsHash), fsm = F.shaders.get(pipe.fsHash);
    if (!vsm || !fsm) { noteUnsupported('missing-shader', 'pipeline shader module was not captured'); return { skip: 'missing-shader' }; }
    let vsWgsl = await shaders.get('vertex', { hash: pipe.vsHash, size: vsm.code.length, code: vsm.code }, { rasterTopology: pipe.topology, specOverrides: pipe.vsSpec });
    if (unnegated) vsWgsl = stripNagaYNegation(vsWgsl);
    let fsWgsl = await shaders.get('fragment', { hash: pipe.fsHash, size: fsm.code.length, code: fsm.code }, { rasterTopology: pipe.topology, specOverrides: pipe.fsSpec });
    fsWgsl = rewriteResolvedDxvkPresent(pipe.fsHash, fsWgsl);
    fsWgsl = rewriteMultisampledTextures(fsWgsl);
    if (!passHasDepth) fsWgsl = stripNagaFragmentDepth(fsWgsl);
    if (!vsWgsl || !fsWgsl) { noteUnsupported('shader-translation', 'Naga returned no WGSL for a pipeline stage'); return { skip: 'shader-translation' }; }
    fsWgsl = matchFragmentInputsToVertexOutputs(vsWgsl, fsWgsl);
    const vsR = rewritePushConstantsWGSL(vsWgsl), fsR = rewritePushConstantsWGSL(fsWgsl);
    const pushBlocks = vsR.pushBlocks + fsR.pushBlocks;
    if (vsR.pushBlocks > 1 || fsR.pushBlocks > 1) { noteUnsupported('multi-push-block'); return { skip: 'multi-push-block' }; }
    const hasPush = pushBlocks > 0;

    const declarations = scanWgslResources(vsR.code).concat(scanWgslResources(fsR.code));
    const uniqueDeclarations = new Map();
    for (const decl of declarations) {
      const key = decl.group + ':' + decl.binding;
      const prior = uniqueDeclarations.get(key);
      if (prior && JSON.stringify(layoutEntryFor(prior)) !== JSON.stringify(layoutEntryFor(decl))) {
        noteUnsupported('incompatible-shared-binding', key);
        return { skip: 'incompatible-shared-binding:' + key };
      }
      if (!prior) uniqueDeclarations.set(key, decl);
    }
    const decls = [...uniqueDeclarations.values()];
    // Uniform block sizes from the WGSL, so short buffers can be padded.
    const uniformSizes = wgslUniformSizes(vsR.code);
    for (const [binding, size] of wgslUniformSizes(fsR.code))
      uniformSizes.set(binding, Math.max(size, uniformSizes.get(binding) || 0));
    for (const d of decls) if (d.addr === "uniform") {
      const s = uniformSizes.get(d.group + ":" + d.binding);
      if (s) d.minSize = s;
    }
    const byGroup = new Map();
    for (const d of decls) {
      if (d.group > 2 && !(d.group === V2_PUSH_GROUP && d.binding === V2_PUSH_BINDING && d.addr === 'uniform')) {
        noteUnsupported('group-out-of-range', d.group); return { skip: 'group-out-of-range' };
      }
      if (!byGroup.has(d.group)) byGroup.set(d.group, []);
      byGroup.get(d.group).push(d);
    }
    const maxGroup = Math.max(0, ...byGroup.keys(), hasPush ? V2_PUSH_GROUP : 0);
    const layouts = [];
    for (let g = 0; g <= maxGroup; g++) {
      /** @type {GPUBindGroupLayoutEntry[]} */
      const entries = [];
      for (const d of byGroup.get(g) || []) {
        const e = layoutEntryFor(d);
        if (!e) { noteUnsupported('decl-kind', d.type); return { skip: 'decl-kind:' + d.type }; }
        entries.push(e);
      }
      if (g === V2_PUSH_GROUP && hasPush && !entries.some(e => e.binding === V2_PUSH_BINDING))
        entries.push({ binding: V2_PUSH_BINDING, visibility: GPUShaderStage.VERTEX | GPUShaderStage.FRAGMENT, buffer: { type: 'uniform' } });
      layouts.push(device.createBindGroupLayout({ entries }));
    }

    // Vertex input state from the PIPELINE record's layout tables.
    /** @type {GPUVertexBufferLayout[]} */
    const buffers = [];
    const vbWalk = [];
    const slotOf = new Map(); // Vulkan binding -> GPU slots and rebased offsets
    for (const vb of pipe.vb) {
      const attributes = [];
      let minStride = 0;
      const scaled = [];
      for (const va of pipe.va) {
        if (va.binding !== vb.binding) continue;
        const vf = v2VertexFormat(va.format);
        if (!vf) { noteUnsupported('vertex-format', va.format); return { skip: 'vertex-format:' + va.format }; }
        attributes.push({ shaderLocation: va.location, offset: va.offset, format: vf });
        const scaling = V2_SCALED_VERTEX[va.format];
        if (scaling) scaled.push({ ...scaling, srcOffset: va.offset, attribute: attributes[attributes.length - 1] });
        minStride = Math.max(minStride, va.offset + (scaling ? scaling.components * scaling.bits / 8 : (V2_VERT_FORMAT_SIZE[vf] || 4)));
      }
      // Attribute offsets may extend beyond a vertex's stride. They do not
      // change the distance between vertices (zero also means constant data).
      const stride = vb.stride;
      if (scaled.length && !stride) {
        noteUnsupported('vertex-stride', stride); return {skip:'vertex-stride:' + stride};
      }
      const paddedStride = stride === 0 || stride % 4 !== 0 || minStride > stride;
      let arrayStride = scaled.length || paddedStride ? Math.ceil(Math.max(stride, minStride, 4) / 4) * 4 : stride;
      const rawAttributes = pipe.va.filter(va => va.binding === vb.binding && !V2_SCALED_VERTEX[va.format] &&
        va.offset + (V2_VERT_FORMAT_SIZE[v2VertexFormat(va.format)] || 4) > stride)
        .map(va => ({ srcOffset: va.offset, dstOffset: va.offset, size: V2_VERT_FORMAT_SIZE[v2VertexFormat(va.format)] || 4 }));
      for (const a of scaled) { a.dstOffset = arrayStride; a.attribute.offset = arrayStride; arrayStride += a.components * 4; }
      const slots = [];
      const addStream = (streamAttributes, offset = 0) => {
        slots.push({slot:buffers.length,offset});
        buffers.push({arrayStride,stepMode:vb.inputRate || stride === 0 ? 'instance':'vertex',attributes:streamAttributes});
      };
      addStream(attributes);
      if (buffers.length > (device.limits?.maxVertexBuffers || 8)) {noteUnsupported('vertex-buffer-slot-limit', buffers.length);return {skip:'vertex-buffer-slot-limit'};}
      slotOf.set(vb.binding,slots);
      const attrs44 = [];
      for (const va of pipe.va) if (va.binding === vb.binding && va.format === 44) attrs44.push(va.offset);
      vbWalk.push({ binding: vb.binding, stride, attrs44, scaled, rawAttributes, paddedStride: arrayStride !== stride, outputStride: arrayStride });
    }

    let colorTargets, blendConstantMode, stencil;
    try {
      colorTargets = colorFormats.map((format, index) => v2BlendTarget(pipe, index, format));
      blendConstantMode = v2BlendConstantMode(pipe);
      stencil = v2StencilState(pipe, passHasDepth ? depthFormat : '');
      if (!(depthAspects & 4)) {
        if (pipe.stencil?.enabled) throw new Error('stencil testing without a stencil aspect');
        stencil.state.stencilWriteMask = 0;
      }
    } catch (error) { noteUnsupported('pipeline-build-threw', error.message); return { skip: error.message }; }
    device.pushErrorScope('validation');
    const bwModuleT0 = metricsNow();
    const vertexModule = device.createShaderModule({ code: vsR.code });
    // DXVK/Vulkan allow implicit-derivative sampling in non-uniform control
    // flow; WGSL's uniformity analysis rejects it. Dawn honours the
    // derivative_uniformity diagnostic switch (the hardware result is the same).
    const fragmentModule = device.createShaderModule({ code: 'diagnostic(off, derivative_uniformity);\n' + fsR.code });
    timing.shaderModuleMs += metricsNow() - bwModuleT0; timing.shaderModuleCount += 2;
    const bwPipeT0 = metricsNow();
    const pipeline = device.createRenderPipeline({
      layout: device.createPipelineLayout({ bindGroupLayouts: layouts }),
      vertex: {
        module: vertexModule, entryPoint: 'main',
        buffers: buffers.length ? buffers : undefined,
      },
      fragment: {
        module: fragmentModule, entryPoint: 'main',
        targets: colorTargets,
      },
      primitive: v2PrimitiveState(pipe, device.features?.has('depth-clip-control') === true, indexType),
      depthStencil: passHasDepth ? {
        format: /** @type {GPUTextureFormat} */ (depthFormat),
        ...stencil.state,
        ...v2DepthBiasState(pipe),
        depthWriteEnabled: (depthAspects & 2) && pipe.depthTest ? !!pipe.depthWrite : false,
        depthCompare: pipe.depthTest ? (DEPTH_OP[pipe.depthOp] || 'less-equal') : 'always',
      } : undefined,
      multisample: { count: sampleCount },
    });
    const err = await device.popErrorScope();
    timing.pipelineMs += metricsNow() - bwPipeT0;
    timing.pipelineCount++;
    if (err) {
      noteUnsupported('pipeline-rejected', err.message.split(' - ')[0]);
      // Surface the offending declarations: Dawn's message names only group/binding.
      // naga puts the attributes and the `var` on separate lines, so match the whole declaration.
      const where = /@group\((\d+)\) @binding\((\d+)\)/.exec(err.message);
      const decl = where ? [['vs', vsR.code], ['fs', fsR.code]].flatMap(([stage, code]) =>
        [...code.matchAll(new RegExp(`@group\\(${where[1]}\\)\\s*@binding\\(${where[2]}\\)\\s*var[^;]*;`, 'g'))].map(m => stage + ': ' + m[0].replace(/\s+/g, ' '))).join(' | ') : '';
      return { skip: 'pipeline-rejected:' + err.message.split(' - ')[0] + (decl ? ' [' + decl + ']' : '') };
    }
    const rec = { key, pipeline, layouts, decls, hasPush, slotOf, maxGroup, vbWalk, stripIndexType, blendConstantMode, stencilReference:stencil.reference };
    pipelines.set(key, rec);
    return rec;
  }

  const clearPipelines = new Map();
  function attachmentClearPipeline(colorFormats, depthFormat, samples, attachment) {
    const aspect=attachment.aspectMask;
    const slot=attachment.colorAttachment;
    const key=JSON.stringify([colorFormats,depthFormat,samples,aspect,slot]);
    if(clearPipelines.has(key)) return clearPipelines.get(key);
    const fields=colorFormats.map((format,i)=>`@location(${i}) c${i}:vec4<${format.endsWith('uint')?'u32':format.endsWith('sint')?'i32':'f32'}>,`);
    if(aspect&2) fields.push('@builtin(frag_depth) depth:f32,');
    const outputs=colorFormats.map((format,i)=>`o.c${i}=${format.endsWith('uint')?'data.color':format.endsWith('sint')?'bitcast<vec4<i32>>(data.color)':'bitcast<vec4<f32>>(data.color)'};`).join('');
    const fs=fields.length?`struct Out{${fields.join('')}}; @fragment fn fs()->Out{var o:Out;${outputs}${aspect&2?'o.depth=data.depth.x;':''}return o;}`:'@fragment fn fs(){}';
    const code=`struct Data{color:vec4<u32>,depth:vec4<f32>}; @group(0) @binding(0) var<uniform> data:Data;
      @vertex fn vs(@builtin(vertex_index) i:u32)->@builtin(position) vec4<f32>{let p=array<vec2<f32>,3>(vec2<f32>(-1,-1),vec2<f32>(3,-1),vec2<f32>(-1,3));return vec4<f32>(p[i],0,1);}
      ${fs}`;
    const module=device.createShaderModule({code});
    const layout=device.createBindGroupLayout({entries:[{binding:0,visibility:GPUShaderStage.FRAGMENT,buffer:{type:'uniform'}}]});
    const pipeline=device.createRenderPipeline({
      layout:device.createPipelineLayout({bindGroupLayouts:[layout]}),
      vertex:{module,entryPoint:'vs'},fragment:{module,entryPoint:'fs',targets:colorFormats.map((format,i)=>({format,writeMask:(aspect&1)&&i===slot?15:0}))},
      primitive:{topology:'triangle-list',cullMode:'none'},multisample:{count:samples},
      depthStencil:depthFormat?{format:depthFormat,depthWriteEnabled:!!(aspect&2),depthCompare:'always',
        stencilFront:{compare:'always',passOp:aspect&4?'replace':'keep'},stencilBack:{compare:'always',passOp:aspect&4?'replace':'keep'},stencilReadMask:255,stencilWriteMask:aspect&4?255:0}:undefined,
    });
    const rec={pipeline,layout};clearPipelines.set(key,rec);return rec;
  }

  // Manifest bindings covering one array element (partial descriptor-array
  // writes each carry their own dstArrayElement/count).
  function bindingForElement(bindings, bindingNum, el) {
    let fallback = null;
    for (const b of bindings) {
      if (b.binding !== bindingNum) continue;
      if (!fallback) fallback = b;
      if (el >= b.dstArrayElement && el < b.dstArrayElement + b.count) return b;
    }
    return fallback;
  }

  function imageForView(viewId, imageId) {
    const image = F.images.get(imageId), viewFormat = F.viewFormats.get(viewId);
    return image && viewFormat && viewFormat !== image.format ? { ...image, format: viewFormat } : image;
  }

  // The subresource range a sampled VkImageView was created with, as GPUTextureViewDescriptor
  // fields (undefined when the record predates the range tail or the view covers the whole image).
  function sampledViewRange(viewHex) {
    return viewHex ? F.viewRanges?.get(viewHex) : undefined;
  }
  function viewRangeKey(range) {
    return range ? '@' + range.baseMip + '+' + range.levelCount + ',' + range.baseLayer + '+' + range.layerCount : '';
  }
  function viewRangeDescriptor(range, texture, dimension) {
    if (!range) return {};
    const out = {};
    const mips = texture.mipLevelCount || 1;
    if (dimension !== '3d') {
      const baseMip = Math.min(range.baseMip, mips - 1);
      const levels = Math.max(1, Math.min(range.levelCount, mips - baseMip));
      if (baseMip || levels !== mips) { out.baseMipLevel = baseMip; out.mipLevelCount = levels; }
    }
    if (dimension === '3d' || dimension === '1d') return out;
    const layers = texture.depthOrArrayLayers || texture.size?.[2] || 1;
    const base = Math.min(range.baseLayer, layers - 1);
    let count = Math.max(1, Math.min(range.layerCount, layers - base));
    if (dimension === '2d') count = 1;
    else if (dimension === 'cube') count = 6;
    else if (dimension === 'cube-array') count = Math.floor(count / 6) * 6;
    if (count < 1 || base + count > layers || (dimension.startsWith('cube') && count % 6)) return out;   // not representable: whole-image view
    if (base || count !== layers) { out.baseArrayLayer = base; out.arrayLayerCount = count; }
    return out;
  }

  // Content fingerprint for one descriptor set: deterministic function of
  // the descriptor contents (resource ids + content hashes). Used as the key
  // for the persistent bind-group cache. Must change whenever the resolved
  // GPU resources would change; may (conservatively) differ for identical
  // content, which only costs a cache miss. Mirrors bindGroupEntries'
  // resolution logic without creating any GPU objects.
  function bindGroupFingerprint(prec, sets, inlineMap, group) {
    const parts = [];
    const set = sets.get(group);
    const bindings = set ? set.bindings : [];
    for (const d of prec.decls) {
      if (d.group !== group) continue;
      const t = d.type;
      if (group === V2_PUSH_GROUP) continue;
      if (t.startsWith('texture_') && !t.startsWith('texture_storage')) {
        const n = Math.max(1, d.arrayCount || 1);
        const dimension = wgslTextureViewDimension(t) || '2d';
        const cube = dimension === 'cube' || dimension === 'cube-array';
        for (let k = 0; k < n; k++) {
          const b = bindingForElement(bindings, d.binding, k);
          const imgHex = b && b.kind === 2 ? F.views.get(b.obj) : null;
          const img = imgHex && imageForView(b?.obj, imgHex);
          const ak = imgHex && v2ImageKnown(imgHex)
            ? (targets.has(v2ImageTargetKey(imgHex, false)) ? v2ImageTargetKey(imgHex, false)
              : targets.has(v2ImageTargetKey(imgHex, true)) ? v2ImageTargetKey(imgHex, true) : null)
            : null;
          if (ak) parts.push('a' + d.binding + ':' + k + ':' + ak + ':format' + (img?.format || 0) + viewRangeKey(sampledViewRange(b?.obj)));
          else {
            parts.push('t' + d.binding + ':' + k + ':' + (img ? textureKey(imgHex, img, null, cube, dimension).ck + viewRangeKey(sampledViewRange(b?.obj)) : 'blank'));
          }
        }
      } else if (t === 'sampler' || t === 'sampler_comparison') {
        const b = bindingForElement(bindings, d.sourceBinding ?? d.binding, 0);
        let sHex = null;
        if (b && b.kind === 3) sHex = b.obj;
        else if (b && b.kind === 2) sHex = b.range;
        const has = sHex && F.samplers.get(sHex);
        parts.push('s' + d.binding + ':' + (has ? sHex : 'default'));
      } else if (d.addr === 'uniform' || d.addr.startsWith('storage')) {
        const isStorage = d.addr.startsWith('storage');
        const usage = (isStorage ? GPUBufferUsage.STORAGE : GPUBufferUsage.UNIFORM) | GPUBufferUsage.COPY_DST;
        const n = Math.max(1, d.arrayCount || 1);
        for (let k = 0; k < n; k++) {
          const b = bindingForElement(bindings, d.binding, k);
          let bytes = null, bufHex = null, preHash = null;
          if (b && b.kind === 1) {
            bufHex = b.obj;
            const qkey = bufHex + ':' + (b.bufOff || '0');
            bytes = F.buffers.get(qkey);
            // Always key on the bridge 64-bit content hash, for full records
            // and byte-less (deduped) records alike. Rehashing present bytes
            // page-side (32-bit) produced a different key than the one used
            // for deduped records, so deduped frames missed the persistent
            // GPU buffer cache.
            preHash = F.bufHashes.get(qkey) || F.bufHashes.get(bufHex);
            if (bytes) bufHex = qkey;
            else {
              bytes = F.buffers.get(bufHex);
              if (!bytes && preHash) bufHex = qkey;
            }
          }
          else if (b && b.kind === 4) { bufHex = 'inline:' + group + ':' + d.binding + ':' + k; bytes = inlineMap.get(group + ':' + d.binding); }
          if (!bytes && !preHash) { bytes = ZERO4096; bufHex = 'zero:' + group + ':' + d.binding + ':' + k; }
          // The size suffix must not depend on whether this frame carried the
          // bytes: a byte-less (deduped) record has no length to compare, but
          // the persistent cache entry was created under the suffixed key.
          const minSize = minUniformSize(group, d.binding, d.minSize);
          if (minSize) {
            if (bytes && bytes.length < minSize) {
              const padded = new Uint8Array(minSize);
              padded.set(bytes);
              bytes = padded;
            }
            bufHex += ':min' + minSize;
          }
          parts.push('b' + d.binding + ':' + k + ':' + bufferKey(bufHex, bytes, usage, 0, preHash));
        }
      } else if (t.startsWith('texture_storage')) {
        parts.push('x' + d.binding);
      }
    }
    return parts.join('|');
  }

  function bindGroupEntries(prec, sets, inlineMap, group) {
    const entries = [];
    const set = sets.get(group);
    const bindings = set ? set.bindings : [];
    for (const d of prec.decls) {
      if (d.group !== group) continue;
      const t = d.type;
      if (group === V2_PUSH_GROUP) continue; // the push UBO is bound separately
      if (t.startsWith('texture_') && !t.startsWith('texture_storage')) {
        const views = [];
        for (let k = 0; k < d.arrayCount; k++) {
          const b = bindingForElement(bindings, d.binding, k);
          const imgHex = b && b.kind === 2 ? F.views.get(b.obj) : null;
          // Sampled-image alias replay: when this VkImage was a render target
          // earlier in the frame, sample the texture actually rendered into —
          // its IMAGE_DATA record carries the view->image link but no pixels.
          const dimension = wgslTextureViewDimension(t) || '2d';
          const cube = dimension === 'cube' || dimension === 'cube-array';
          let aliasTex = !cube && dimension === '2d' && v2ImageKnown(imgHex)
            ? (targets.get(v2ImageTargetKey(imgHex, false)) || targets.get(v2ImageTargetKey(imgHex, true)))
            : null;
          if (aliasTex) stats.aliased++;
          const img = imgHex && imageForView(b?.obj, imgHex);
          const imgFormat = img ? v2TextureFormat(img.format, stats, device.features) : null;
          if (img && !imgFormat) { noteDrop('bind-group-image-format-unsupported', img.format); return null; }
          let texture = aliasTex || (img ? gpuTextureFor(imgHex, img, cube, dimension) : blankTex(cube, dimension, t.startsWith('texture_depth')));
          if (!texture) { noteDrop('bind-group-texture-unavailable', imgHex); return null; }
          // WebGPU ties the shader's sample type to the texture format: a
          // depth texture cannot satisfy texture_2d<f32> (D3D11 shaders sample
          // depth targets as plain floats) and a colour texture cannot satisfy
          // texture_depth_2d. One mismatch invalidates the whole bind group
          // and with it the frame, so bind a blank placeholder of the type the
          // shader asks for and count it.
          const wantsDepth = t.startsWith('texture_depth'), isDepthFormat = /^depth/.test(String(texture.format));
          if (isDepthFormat !== wantsDepth && !(isDepthFormat && /<[ui]32>/.test(t) && String(texture.format).includes('stencil'))) {
            texture = blankTex(cube, dimension, wantsDepth);
            aliasTex = null;
            stats.sampleTypePlaceholders = (stats.sampleTypePlaceholders || 0) + 1;
          }
          // A combined depth/stencil texture can only be bound one aspect at a
          // time (and then takes no format override): integer element types read
          // the stencil, the rest the depth.
          const aspect = String(texture.format || '').includes('stencil') ? (/<[ui]32>/.test(t) ? 'stencil-only' : 'depth-only') : undefined;
          const viewRange = sampledViewRange(imgHex && b?.obj);
          if (aliasTex && viewRange && (viewRange.baseMip || viewRange.baseLayer)) {
            noteUnsupported('sampled-render-target-subresource', 'mip ' + viewRange.baseMip + ' layer ' + viewRange.baseLayer + ' of a rendered image; whole level 0 is sampled');
          }
          views.push(aliasTex ? aliasTex.createView(aspect ? { aspect } : imgFormat ? { format: imgFormat } : {})
            : texture.createView({ dimension, ...(aspect ? { aspect } : {}), ...viewRangeDescriptor(viewRange, texture, dimension) }));
        }
        entries.push({ binding: d.binding, resource: d.arrayCount > 1 ? views : views[0] });
      } else if (t === 'sampler' || t === 'sampler_comparison') {
        const b = bindingForElement(bindings, d.sourceBinding ?? d.binding, 0);
        let sHex = null;
        if (b && b.kind === 3) sHex = b.obj;
        else if (b && b.kind === 2) sHex = b.range; // combined image sampler's sampler
        const s = sHex && F.samplers.get(sHex);
        entries.push({ binding: d.binding, resource: s ? gpuSamplerFor(sHex, s) : device.createSampler(t === 'sampler_comparison' ? { compare: 'always' } : {}) });
      } else if (d.addr === 'uniform' || d.addr.startsWith('storage')) {
        const isStorage = d.addr.startsWith('storage');
        const mk = k => {
          const b = bindingForElement(bindings, d.binding, k);
          let bytes = null, bufHex = null, preHash = null;
          if (b && b.kind === 1) {
            bufHex = b.obj;
            // Offset-qualified: one VkBuffer may back several uniform ranges.
            // The GPU buffer cache (gpuBufferFor) is keyed by bufHex, so the
            // qualified key must become the cache key - otherwise a different
            // range of the same VkBuffer poisons the cache for this binding.
            const qkey = bufHex + ':' + (b.bufOff || '0');
            bytes = F.buffers.get(qkey);
            // Always key on the bridge 64-bit content hash, for full records
            // and byte-less (deduped) records alike. The old code only used
            // the bridge hash when bytes were absent, so full records were
            // cached under a page-side 32-bit rehash while deduped records
            // looked up the bridge hash: every deduped frame missed the
            // persistent cache, gpuBufferFor returned null, and
            // createBindGroup rejected the frame (framesAdvanced stuck at 0).
            preHash = F.bufHashes ? (F.bufHashes.get(qkey) || F.bufHashes.get(bufHex)) : null;
            if (bytes) bufHex = qkey;
            else {
              bytes = F.buffers.get(bufHex);
              if (!bytes && preHash) bufHex = qkey;
            }
          }
          else if (b && b.kind === 4) { bufHex = 'inline:' + group + ':' + d.binding + ':' + k; bytes = inlineMap.get(group + ':' + d.binding); }
          // Missing uniform data: substitute a zero buffer large enough for any
          // practical uniform block (WebGPU validates against the shader's struct).
          if (!bytes && !preHash) { bytes = ZERO4096; bufHex = 'zero:' + group + ':' + d.binding + ':' + k; }
          // The size suffix must not depend on whether this frame carried the
          // bytes: a byte-less (deduped) record has no length to compare, but
          // the persistent cache entry was created under the suffixed key, so
          // a deduped frame must compute the same key to hit it.
          const minSize = minUniformSize(group, d.binding, d.minSize);
          if (minSize) {
            if (bytes && bytes.length < minSize) {
              const padded = new Uint8Array(minSize);
              padded.set(bytes);
              bytes = padded;
            }
            bufHex += ':min' + minSize;
          }
          const usage = (isStorage ? GPUBufferUsage.STORAGE : GPUBufferUsage.UNIFORM) | GPUBufferUsage.COPY_DST;
          const gpu = gpuBufferFor(bufHex, bytes, usage, preHash);
          if (gpu) return { buffer: gpu };
          // Last resort: a null buffer makes createBindGroup throw and the
          // whole frame (including menu text) disappears. Keep the frame and
          // this binding's draws alive with zeros until the bridge resends.
          persistStats.bufUnresolved++;
          const fallback = minSize > ZERO64K.length ? new Uint8Array(minSize) : ZERO64K;
          return { buffer: gpuBufferFor(bufHex + ':unresolved', fallback, usage, null) };
        };
        if (d.arrayCount > 1) {
          const arr = [];
          for (let k = 0; k < d.arrayCount; k++) arr.push(mk(k));
          entries.push({ binding: d.binding, resource: arr });
        } else {
          entries.push({ binding: d.binding, resource: mk(0) });
        }
      } else if (t.startsWith('texture_storage')) {
        noteUnsupported('storage-texture-binding-blank', d.type); // DXVK's D3D9 path rarely writes storage images
        entries.push({ binding: d.binding, resource: blankTex().createView() });
      }
    }
    return entries;
  }

  async function render(frame, { present = true } = {}) {
    const bwRenderT0 = metricsNow();
    if (destroyed) throw new Error('page tier destroyed');
    // A submission with no colour attachment (copy/blit/clear-only command
    // buffers) must still execute, but it cannot be the one that presents.
    if (frame.submissions) {
      let result;
      const presentAt = v2PresentIndex(frame.submissions);
      for (let i = 0; i < frame.submissions.length; i++) {
        result = await render(frame.submissions[i], { present: present && i === presentAt });
        if (!result?.rendered) return result;
      }
      return result;
    }
    if (present && !v2HasColourPass(frame)) present = false;
    F.shaders = frame.shaders; F.images = frame.images;
    F.views = frame.views; F.samplers = frame.samplers; F.buffers = frame.buffers;
    F.viewFormats = frame.viewFormats || new Map();
    F.viewRanges = frame.viewRanges || new Map();
    F.bufHashes = frame.bufHashes;
    F.texCache.clear(); F.bufCache.clear(); F.sampCache.clear(); F.live.length = 0;
    // NOTE: persistTexCache/persistBufCache/persistSampCache are NOT cleared —
    // they provide cross-frame reuse of GPU resources when content is unchanged.
    if (typeof window !== 'undefined' && window.__bwDbgRender)
      console.log('bwDbg render f' + frame.frameNo + ' ' + frame.width + 'x' + frame.height +
        ' canvas ' + canvas.width + 'x' + canvas.height);
    const displayPass = frame.passes.findLast(p => p.attachments.some(a => !a.isDepth));
    const displayWidth = displayPass?.w || frame.width, displayHeight = displayPass?.h || frame.height;
    if (present && (canvas.width !== displayWidth || canvas.height !== displayHeight)) {
      canvas.width = displayWidth; canvas.height = displayHeight;
      obsW = displayWidth; obsH = displayHeight;
      // Reconfigure the WebGPU context for the new canvas size; without
      // this getCurrentTexture() may return a stale-sized texture.
      context.configure({ device, format, alphaMode: 'opaque', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST });
    }

    // An attachment can be used by several dynamic rendering subrectangles.
    // Allocate its largest required extent before recording any pass.
    frameTargetSizes = new Map();
    const reserveTarget = (key, w, h) => {
      const previous = frameTargetSizes.get(key) || [0, 0];
      frameTargetSizes.set(key, [Math.max(previous[0], w), Math.max(previous[1], h)]);
    };
    for (const p of frame.passes) {
      let colorSlot = 0;
      for (const a of p.attachments) {
        const samples = v2PickSampleCount(a.samples || 1);
        reserveTarget(v2TargetKey(a.imageId, p.w, p.h, a.isDepth ? samples : 1, !!a.isDepth), p.w, p.h);
        if (!a.isDepth) {
          if (samples > 1) reserveTarget(colorMsKey(a.imageId, p.w, p.h, samples, colorSlot), p.w, p.h);
          colorSlot++;
        }
      }
    }
    for (const [key, size] of frameTargetSizes) {
      const old = targets.get(key);
      if (old && (old.width < size[0] || old.height < size[1])) {
        old.destroy(); targets.delete(key); persistGroupCache.clear();
      }
    }
    let presentTex = null, presentW = frame.width, presentH = frame.height, lastClear = [0, 0, 0, 1];
    const encoder = device.createCommandEncoder();
    // GPU timestamp 0: the beginning of this frame's submitted GPU work.
    const bwGpuSlot = gpuTimer.begin(encoder);
    // Persistent across frames: same size every frame, no need to re-create.
    if (!persistDummyVb) {
      persistDummyVb = device.createBuffer({ size: 4096, usage: GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST });
    }
    const dummyVb = persistDummyVb;

    // --- replay state persists across passes within a frame ---
    // Vulkan allows vkCmdBindPipeline / vkCmdBindVertexBuffers outside a render
    // pass (vkd3d does this); the bound pipeline and vertex state must carry
    // over to the next pass with attachments instead of being dropped with
    // the per-pass state.
    const st = {
      pipeRec: null, pipeDesc: null, pipeNeg: false, sets: new Map(), setVer: new Map(), inlineMap: new Map(),
      pushBlock: new Uint8Array(V2_PUSH_SIZE), pushDirty: false,
      vbinds: [], ibind: null, viewport: null, scissor: null,
      pipeBad: false, badPipes: new Map(), dynamicStencil:[[undefined,undefined],[undefined,undefined],[undefined,undefined]], blendConstants:[0,0,0,0], depthBias:null, cull:undefined, front:undefined, dyn:{},
    };
    const bump = si => st.setVer.set(si, (st.setVer.get(si) || 0) + 1);
    const diagCfg = v2PassReadbackConfig(globalThis.location?.search);
    const diagOn = !!diagCfg && (stats.frames % diagCfg.every) === 0;
    const diagPasses = [];

    for (const p of frame.passes) {
      // The selected pipeline is specific to one pass's attachment state (formats,
      // sample count, depth). The bound pipeline *description* carries over; the
      // WebGPU pipeline is re-resolved at the next draw. A stale rec here made
      // clearAttachments re-set an incompatible pipeline in a later pass.
      st.pipeRec = null;
      // --- targets for this pass (MSAA-aware) ---
      let passSamples = 1;
      for (const a of p.attachments) passSamples = Math.max(passSamples, v2PickSampleCount(a.samples || 1));
      /** @type {GPURenderPassColorAttachment[]} */
      const colorAttachments = [];
      const passColorFormats = [];
      const passDiagTargets = [], passDiagDraws = { draws: 0, raw: 0, skips: {}, vs: new Set(), fs: new Set(), list: [] }, passDiagStart = { unsupported: stats.unsupported, blank: blankUses };
      let depthView = null, depthAttachment = null, passDepthFormat = 'depth24plus', passDepthAspects = 6;
      let unsupportedAttachment = false;
      for (const a of p.attachments) {
        const samples = v2PickSampleCount(a.samples || 1);
        // Known VkImage identity: the single-sample (resolve) target is keyed
        // by image so a later pass sampling it aliases the rendered texture.
        // The multisampled texture itself stays geometry-keyed.
        const knownImage = v2ImageKnown(a.imageId);
        const texBinding = knownImage ? GPUTextureUsage.TEXTURE_BINDING : 0;
        if (a.isDepth) {
          passDepthFormat = a.format ? v2TextureFormat(a.format, stats, device.features) : 'depth24plus';
          if (!passDepthFormat) { noteUnsupported('pass-depth-format-unsupported', a.format); unsupportedAttachment = true; continue; }
          const tex = target(v2TargetKey(a.imageId, p.w, p.h, samples, true), {
            size: [p.w, p.h], sampleCount: samples, format: passDepthFormat,
            usage: GPUTextureUsage.RENDER_ATTACHMENT | texBinding,
          });
          passDepthAspects = a.aspectMask || (passDepthFormat.includes('stencil') ? 6 : 2);
          depthView = tex.createView({ dimension: '2d', baseMipLevel: 0, mipLevelCount: 1, baseArrayLayer: 0, arrayLayerCount: 1 });
          depthAttachment = a;
        } else {
          const viewFormat = a.format ? v2TextureFormat(a.format, stats, device.features) : format;
          if (!viewFormat) { noteUnsupported('pass-color-format-unsupported', a.format); unsupportedAttachment = true; continue; }
          const storage = colorStorage(viewFormat);
          const slot = colorAttachments.length;
          passColorFormats.push(viewFormat);
          const single = target(v2TargetKey(a.imageId, p.w, p.h, 1, false), {
            size: [p.w, p.h], ...storage,
            usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST | GPUTextureUsage.TEXTURE_BINDING,
          });
          let view = single.createView({ format: viewFormat, dimension: '2d', baseMipLevel: 0, mipLevelCount: 1, baseArrayLayer: 0, arrayLayerCount: 1 }), resolveTarget;
          if (samples > 1) {
            const ms = target(colorMsKey(a.imageId, p.w, p.h, samples, slot), {
              size: [p.w, p.h], sampleCount: samples, ...storage, usage: GPUTextureUsage.RENDER_ATTACHMENT,
            });
            view = ms.createView({ format: viewFormat, dimension: '2d', baseMipLevel: 0, mipLevelCount: 1, baseArrayLayer: 0, arrayLayerCount: 1 });
            resolveTarget = single.createView({ format: viewFormat, dimension: '2d', baseMipLevel: 0, mipLevelCount: 1, baseArrayLayer: 0, arrayLayerCount: 1 });
          }
          colorAttachments.push({
            view, resolveTarget,
            clearValue: ((c) => ({ r: c[0], g: c[1], b: c[2], a: c[3] }))(a.clearColor || p.clearColor),
            loadOp: a.loadOp === 1 ? 'clear' : 'load',
            storeOp: a.storeOp === 1 ? 'discard' : 'store',
          });
          if (diagOn) passDiagTargets.push({ tex: single, format: viewFormat, slot, discard: a.storeOp === 1 });
          presentTex = single; presentW = p.w; presentH = p.h; lastClear = p.clearColor;
        }
      }
      if (unsupportedAttachment) {
        colorAttachments.length = 0; passColorFormats.length = 0;
        depthView = null; depthAttachment = null;
      }
      // Passes without attachments (e.g. ops recorded outside a render pass,
      // which Vulkan allows) still update the replay state; they just have no
      // encoder to issue commands against.
      let passEnc = null;
      if (colorAttachments.length || depthView) {
        const rpDesc = { colorAttachments };
        if (depthView) rpDesc.depthStencilAttachment = {
          view: depthView, depthClearValue: p.clearDepth,
          depthLoadOp: depthAttachment.loadOp === 0 ? 'load' : 'clear',
          depthStoreOp: depthAttachment.storeOp === 1 ? 'discard' : 'store',
        };
        if (depthView && passDepthFormat.includes('stencil')) Object.assign(rpDesc.depthStencilAttachment, {
          stencilClearValue:p.clearStencil || 0,
          stencilLoadOp:(depthAttachment.stencilLoadOp ?? depthAttachment.loadOp) === 1 ? 'clear':'load',
          stencilStoreOp:(depthAttachment.stencilStoreOp ?? depthAttachment.storeOp) === 1 ? 'discard':'store',
        });
        if (depthView && !(passDepthAspects & 2)) {
          delete rpDesc.depthStencilAttachment.depthLoadOp; delete rpDesc.depthStencilAttachment.depthStoreOp;
          rpDesc.depthStencilAttachment.depthReadOnly=true;
        }
        if (depthView && passDepthFormat.includes('stencil') && !(passDepthAspects & 4)) {
          delete rpDesc.depthStencilAttachment.stencilLoadOp; delete rpDesc.depthStencilAttachment.stencilStoreOp;
          rpDesc.depthStencilAttachment.stencilReadOnly=true;
        }
        passEnc = encoder.beginRenderPass(rpDesc);
      } else {
        stats.skips++;
      }

      // --- replay ops ---
      let skipped = null;

      for (const op of p.ops) {
        switch (op.op) {
          case 'imageCopy': {
            if (passEnc) throw new Error('image copy inside a render pass');
            const srcImage = F.images.get(op.srcImage), dstImage = F.images.get(op.dstImage);
            const imageDimension = image => image?.d > 1 ? '3d' : image?.layers > 1 ? '2d-array' : '2d';
            const source = targets.get(v2ImageTargetKey(op.srcImage, false)) ||
              (srcImage && gpuTextureFor(op.srcImage, srcImage, false, imageDimension(srcImage)));
            if (!source) throw new Error('image copy source missing: ' + op.srcImage);
            let destination = targets.get(v2ImageTargetKey(op.dstImage, false)) || targets.get(v2ImageTargetKey(op.dstImage, true));
            if (!destination && dstImage) destination = gpuTextureFor(op.dstImage, dstImage, false, imageDimension(dstImage));
            if (!destination) {
              destination = target(v2ImageTargetKey(op.dstImage, false), {
                size: [op.dstOrigin[0] + op.extent[0], op.dstOrigin[1] + op.extent[1]],
                ...colorStorage(source.format),
                usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST,
              });
            }
            if (!destination) throw new Error('image copy destination missing: ' + op.dstImage);
            if (srcImage?.d > 1 || dstImage?.d > 1) {
              encoder.copyTextureToTexture({ texture: source, mipLevel: op.srcMip, origin: op.srcOrigin },
                { texture: destination, mipLevel: op.dstMip, origin: op.dstOrigin }, op.extent);
            } else {
              for (let layer = 0; layer < op.extent[2]; layer++) {
                encoder.copyTextureToTexture({ texture: source, mipLevel: op.srcMip,
                  origin: [op.srcOrigin[0], op.srcOrigin[1], op.srcLayer + op.srcOrigin[2] + layer] },
                { texture: destination, mipLevel: op.dstMip,
                  origin: [op.dstOrigin[0], op.dstOrigin[1], op.dstLayer + op.dstOrigin[2] + layer] }, [op.extent[0], op.extent[1], 1]);
              }
            }
            break;
          }
          case 'imageBlit': {
            if (passEnc) throw new Error('image blit inside a render pass');
            const srcImage = F.images.get(op.srcImage), dstImage = F.images.get(op.dstImage);
            if (!srcImage || !dstImage || srcImage.d > 1 || dstImage.d > 1 ||
                !V2_COLOR_ATTACHMENT_FORMATS.includes(dstImage.format)) { noteUnsupported('blit-unsupported-images', op.srcImage + '->' + op.dstImage); break; }
            const region = v2ImageBlitRegion(op, srcImage, dstImage);
            const srcFormat = v2TextureFormat(srcImage.format, stats, device.features);
            const dstFormat = v2TextureFormat(dstImage.format, stats, device.features);
            if (!region || !srcFormat || !dstFormat || /(?:uint|sint)/.test(srcFormat) || /(?:uint|sint)/.test(dstFormat)) {
              noteUnsupported('blit-region-or-format', `${srcImage.format}->${dstImage.format}`); break;
            }
            const imageDimension = image => image.layers > 1 ? '2d-array' : '2d';
            const source = targets.get(v2ImageTargetKey(op.srcImage, false)) ||
              (F.images.has(op.srcImage) && gpuTextureFor(op.srcImage, srcImage, false, imageDimension(srcImage)));
            const destination = targets.get(v2ImageTargetKey(op.dstImage, false)) ||
              (F.images.has(op.dstImage) && gpuTextureFor(op.dstImage, dstImage, false, imageDimension(dstImage)));
            if (!source || !destination || source === destination && op.srcMip === op.dstMip && op.srcLayer === op.dstLayer) {
              noteUnsupported('blit-missing-or-self-copy', op.srcImage + '->' + op.dstImage); break;
            }
            try {
              const pipeline = imageBlitPipeline(dstFormat, op.filter);
              const srcView = source.createView({ format: srcFormat, dimension: '2d', baseMipLevel: op.srcMip,
                mipLevelCount: 1, baseArrayLayer: op.srcLayer, arrayLayerCount: 1 });
              const dstView = destination.createView({ format: dstFormat, dimension: '2d', baseMipLevel: op.dstMip,
                mipLevelCount: 1, baseArrayLayer: op.dstLayer, arrayLayerCount: 1 });
              const params = device.createBuffer({ size: 16, usage: GPUBufferUsage.UNIFORM, mappedAtCreation: true });
              new Float32Array(params.getMappedRange()).set(region.sourceRect); params.unmap(); F.live.push(params);
              const group = device.createBindGroup({ layout: pipeline.getBindGroupLayout(0), entries: [
                { binding: 0, resource: srcView }, { binding: 1, resource: { buffer: params } },
              ] });
              const pass = encoder.beginRenderPass({ colorAttachments: [{ view: dstView, loadOp: 'load', storeOp: 'store' }] });
              pass.setPipeline(pipeline); pass.setBindGroup(0, group);
              pass.setViewport(region.viewport[0], region.viewport[1], region.viewport[2], region.viewport[3], 0, 1); pass.draw(3); pass.end();
            } catch (error) {
              noteUnsupported('blit-threw', error.message);
            }
            break;
          }
          case 'clearAttachments': {
            if(!passEnc) {noteUnsupported('clear-attachments-outside-pass');break;}
            for(const attachment of op.attachments) {
              const aspect=attachment.aspectMask;
              if(((aspect&1)&&attachment.colorAttachment>=passColorFormats.length) || ((aspect&6)&&!depthView) || ((aspect&4)&&!passDepthFormat.includes('stencil')) || ((aspect&6)&~passDepthAspects)) {noteUnsupported('clear-attachment-not-in-pass', 'aspect=' + aspect + ' slot=' + attachment.colorAttachment);continue;}
              const rec=attachmentClearPipeline(passColorFormats,depthView?passDepthFormat:null,passSamples,attachment);
              const bytes=new Uint8Array(32),data=new DataView(bytes.buffer);
              attachment.colorBits.forEach((v,i)=>data.setUint32(i*4,v,true));data.setFloat32(16,attachment.depth,true);
              const buffer=gpuBufferFor('attachment-clear',bytes,GPUBufferUsage.UNIFORM|GPUBufferUsage.COPY_DST,null);
              passEnc.setPipeline(rec.pipeline);
              passEnc.setBindGroup(0,device.createBindGroup({layout:rec.layout,entries:[{binding:0,resource:{buffer}}]}));
              if(aspect&4) passEnc.setStencilReference(attachment.stencil&255);
              passEnc.setViewport(0,0,p.w,p.h,0,1);
              for(const rect of op.rects) {
                if(rect.baseLayer!==0||rect.layers!==1) {noteUnsupported('clear-rect-layers', rect.baseLayer + '+' + rect.layers);continue;}
                const x=Math.max(0,rect.x),y=Math.max(0,rect.y),w=Math.min(p.w,rect.x+rect.w)-x,h=Math.min(p.h,rect.y+rect.h)-y;
                if(w<=0||h<=0) continue;
                passEnc.setScissorRect(x,y,w,h);passEnc.draw(3,1,0,0);
              }
            }
            if(st.pipeRec) passEnc.setPipeline(st.pipeRec.pipeline);
            const v=st.viewport||[0,0,p.w,p.h,0,1];
            passEnc.setViewport(v[0],v[3]<0?v[1]+v[3]:v[1],v[2],Math.abs(v[3]),v[4],v[5]);
            const sc=clampScissor(st.scissor||[0,0,p.w,p.h],p.w,p.h);
            if(sc) passEnc.setScissorRect(sc[0],sc[1],sc[2],sc[3]);
            break;
          }
          case 'bindPipe': {
            st.pipeDesc = op.pipe; st.pipeNeg = false; st.pipeRec = null; st.pipeBad = false;
            break;
          }
          case 'stencil': {
            for (let face=0;face<2;face++) if(op.faces & (1<<face)) st.dynamicStencil[op.kind][face]=op.value;
            break;
          }
          case 'blendConstants': st.blendConstants=op.values; break;
          case 'depthBias': st.depthBias=op; break;
          case 'rasterState': if (op.which === 'cull' || op.which === 'front') st[op.which]=op.value; else st.dyn[op.which]=op.value; break;
          case 'imageClearDS': {
            // vkCmdClearDepthStencilImage: one clearing render pass per cleared mip/layer.
            if (passEnc) { noteUnsupported('clear-ds-inside-pass'); break; }
            const img = F.images.get(op.image);
            let tex = targets.get(v2ImageTargetKey(op.image, true));
            if (!tex && img) {
              const fmt = v2TextureFormat(img.format, stats, device.features);
              if (fmt && fmt.startsWith('depth')) tex = target(v2ImageTargetKey(op.image, true), {
                size: [img.w, img.h], format: fmt, usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.TEXTURE_BINDING | GPUTextureUsage.COPY_SRC | GPUTextureUsage.COPY_DST,
              });
            }
            if (!tex || !tex.format.startsWith('depth')) { noteUnsupported('clear-ds-no-depth-texture', op.image); break; }
            const hasStencil = tex.format.includes('stencil');
            for (const rg of op.ranges) {
              const levels = rg.levelCount === 0xffffffff ? tex.mipLevelCount - rg.baseMip : rg.levelCount;
              const layers = rg.layerCount === 0xffffffff ? tex.depthOrArrayLayers - rg.baseLayer : rg.layerCount;
              for (let m = rg.baseMip; m < rg.baseMip + levels && m < tex.mipLevelCount; m++) for (let l = rg.baseLayer; l < rg.baseLayer + layers && l < tex.depthOrArrayLayers; l++) {
                const attachment = { view: tex.createView({ dimension: '2d', baseMipLevel: m, mipLevelCount: 1, baseArrayLayer: l, arrayLayerCount: 1 }) };
                // Aspects outside aspectMask are preserved ('load'); depth-only formats take no stencil ops.
                if (rg.aspectMask & 2) { attachment.depthLoadOp = 'clear'; attachment.depthClearValue = Math.min(1, Math.max(0, op.depth)); attachment.depthStoreOp = 'store'; }
                else { attachment.depthLoadOp = 'load'; attachment.depthStoreOp = 'store'; }
                if (hasStencil) {
                  if (rg.aspectMask & 4) { attachment.stencilLoadOp = 'clear'; attachment.stencilClearValue = op.stencil & 255; attachment.stencilStoreOp = 'store'; }
                  else { attachment.stencilLoadOp = 'load'; attachment.stencilStoreOp = 'store'; }
                }
                encoder.beginRenderPass({ colorAttachments: [], depthStencilAttachment: attachment }).end();
              }
            }
            break;
          }
          case 'bindSets': break; // sets resolve from DESC_SET records; ids kept for debugging
          case 'descSet': st.sets.set(op.setIndex, { bindings: op.bindings }); bump(op.setIndex); break;
          case 'inline': st.inlineMap.set(op.setIndex + ':' + op.binding, op.bytes); bump(op.setIndex); break;
          case 'push': st.pushBlock.set(op.bytes, op.offset); st.pushDirty = true; break;
          case 'viewport': st.viewport = op.v; break;
          case 'scissor': st.scissor = op.s; break;
          case 'vbind': st.vbinds = op.binds; break;
          case 'ibind': st.ibind = op; break;
          case 'draw': {
            if (diagOn) passDiagDraws.raw++;
            if (skipped) { noteSkippedDraw('draw-pass-skipped'); if (diagOn) v2DiagNoteSkip(passDiagDraws.skips, 'pass-skipped'); break; }
            if (!passEnc || !st.pipeDesc) { noteSkippedDraw(!passEnc ? 'draw-no-encoder' : 'draw-no-pipeline-bound'); if (diagOn) v2DiagNoteSkip(passDiagDraws.skips, !passEnc ? 'no-encoder' : 'no-pipeline-bound'); break; }
            if (diagOn) { passDiagDraws.draws++; passDiagDraws.vs.add(st.pipeDesc.vsHash); passDiagDraws.fs.add(st.pipeDesc.fsHash); if (passDiagDraws.list.length < 24) passDiagDraws.list.push([st.pipeDesc.vsHash, st.pipeDesc.fsHash, op.indexed ? op.indexCount : op.vertexCount, op.instanceCount, v2DiagDescribeBindings(st.sets, F, targets)]); }
            let drawPipe;
            try {drawPipe=v2StencilPipeline(applyDynamicState(st.pipeDesc,st.dyn),st.dynamicStencil);}
            catch(error) {noteUnsupported('draw-stencil-pipeline', error.message);noteSkippedDraw('draw-stencil-pipeline');if (diagOn) v2DiagNoteSkip(passDiagDraws.skips, 'stencil:' + error.message);break;}
            if ((drawPipe.cullDynamic && st.cull !== undefined) || (drawPipe.frontDynamic && st.front !== undefined))
              drawPipe = {...drawPipe, ...(drawPipe.cullDynamic && st.cull !== undefined ? {cull:st.cull} : {}), ...(drawPipe.frontDynamic && st.front !== undefined ? {front:st.front} : {})};
            if (drawPipe.rasterizerDiscard) { stats.discardedDraws = (stats.discardedDraws || 0) + 1; break; }   // RASTERIZER_DISCARD: no fragments, VS has no other visible effect here
            if (drawPipe.depthBiasDynamic && st.depthBias)
              drawPipe = {...drawPipe,depthBiasConstantFactor:st.depthBias.constantFactor,depthBiasClamp:st.depthBias.clamp,depthBiasSlopeFactor:st.depthBias.slopeFactor};
            const selected=await getPipelineFor(drawPipe,passSamples,st.pipeNeg,!!depthView,st.ibind?.indexType||0,passColorFormats,passDepthFormat,passDepthAspects);
            if(selected.skip) {noteSkippedDraw('draw-pipeline-skipped', 'vs=' + st.pipeDesc.vsHash + ' fs=' + st.pipeDesc.fsHash + ' ' + String(selected.skip).slice(0, 160));if (diagOn) v2DiagNoteSkip(passDiagDraws.skips, 'vs=' + st.pipeDesc.vsHash + ' fs=' + st.pipeDesc.fsHash + ' ' + selected.skip);break;}
            st.pipeRec=selected;
            passEnc.setPipeline(selected.pipeline);
            if (st.viewport) {
              const [x, y, w, h, md, xmd] = st.viewport;
              if (w > 0 && h !== 0) {
                // WebGPU rejects negative viewport heights; Vulkan uses h<0
                // as the Y-flip convention (DXVK D3D9 always does). Emulate
                // exactly: rect (x, y+h, w, -h) plus the un-negated VS variant
                // (see stripNagaYNegation), which together reproduce Vulkan's
                // framebuffer mapping bit-for-bit.
                const negH = h < 0;
                if (negH !== st.pipeNeg && st.pipeDesc) {
                  const rec = await getPipelineFor(drawPipe, passSamples, negH, !!depthView, st.ibind?.indexType || 0, passColorFormats, passDepthFormat, passDepthAspects);
                  if (!rec.skip) { st.pipeRec = rec; st.pipeNeg = negH; if (passEnc) passEnc.setPipeline(rec.pipeline); }
                }
                passEnc.setViewport(x, negH ? y + h : y, w, Math.abs(h), md, xmd);
              }
            }
            if (st.scissor) {
              // The scissor persists across passes and may come from a larger
              // framebuffer; Vulkan clips it, WebGPU rejects the whole frame.
              const sc = clampScissor(st.scissor, p.w, p.h);
              if (sc) passEnc.setScissorRect(sc[0], sc[1], sc[2], sc[3]);
            }
            if (st.pipeRec.stripIndexType !== undefined && st.pipeRec.stripIndexType !== (st.ibind?.indexType || 0)) {
              const rec = await getPipelineFor(drawPipe, passSamples, st.pipeNeg, !!depthView, st.ibind?.indexType || 0, passColorFormats, passDepthFormat, passDepthAspects);
              if (rec.skip) { noteSkippedDraw('draw-strip-index-pipeline-skipped', rec.skip); break; }
              st.pipeRec = rec;
              passEnc.setPipeline(rec.pipeline);
            }
            if (st.pipeRec.stencilReference !== null) passEnc.setStencilReference(st.pipeRec.stencilReference);
            if (st.pipeRec.blendConstantMode) {
              const c = st.pipeDesc.blendConstantsDynamic ? st.blendConstants : (st.pipeDesc.blendConstants || [0,0,0,0]);
              passEnc.setBlendConstant(st.pipeRec.blendConstantMode === 'alpha' ? [c[3],c[3],c[3],c[3]] : c);
            }
            let invalidBindGroup = false;
            for (let g = 0; g <= st.pipeRec.maxGroup; g++) {
              if (g === V2_PUSH_GROUP) continue;
              if (!st.pipeRec.decls.some(d => d.group === g)) continue;
              // Persistent bind-group cache: the fingerprint captures
              // descriptor content, so identical sets reuse the bind group
              // across frames instead of re-creating it per draw.
              const fp = bindGroupFingerprint(st.pipeRec, st.sets, st.inlineMap, g);
              const ck = 'g|' + st.pipeRec.key + '|' + g + '|' + fp;
              let bg = persistGroupCache.get(ck);
              if (!bg) {
                persistStats.groupMiss++;
                const entries = bindGroupEntries(st.pipeRec, st.sets, st.inlineMap, g);
                if (!entries) { invalidBindGroup = true; break; }
                bg = device.createBindGroup({ layout: st.pipeRec.layouts[g], entries });
                persistGroupCache.set(ck, bg);
                if (persistGroupCache.size > PERSIST_GROUP_CACHE_MAX)
                  persistGroupCache.delete(persistGroupCache.keys().next().value);
              } else persistStats.groupHits++;
              passEnc.setBindGroup(g, bg);
            }
            if (invalidBindGroup) { noteSkippedDraw('draw-bind-group-invalid', 'vs=' + st.pipeDesc.vsHash + ' fs=' + st.pipeDesc.fsHash); break; }
            if (st.pipeRec.hasPush) {
              // Queue writes all precede the encoded draws at submit time.
              // Give each distinct push state immutable storage so a later
              // draw cannot overwrite the constants of an earlier one.
              const pushHash = fnv1a(st.pushBlock);
              const pushBuf = gpuBufferFor('push', st.pushBlock, GPUBufferUsage.UNIFORM, pushHash);
              const ck = 'g|' + st.pipeRec.key + '|push|' + pushHash;
              let pg = persistGroupCache.get(ck);
              if (!pg) {
                pg = device.createBindGroup({
                  layout: st.pipeRec.layouts[V2_PUSH_GROUP],
                  entries: [{ binding: V2_PUSH_BINDING, resource: { buffer: pushBuf } }],
                });
                persistGroupCache.set(ck, pg);
              }
              passEnc.setBindGroup(V2_PUSH_GROUP, pg);
            }
            const vbSet = new Set();
            for (const vb of st.vbinds) {
              const slots = st.pipeRec.slotOf.get(vb.binding);
              if (!slots) continue;
              const bytes = F.buffers.get(vb.buf);
              const preHash = F.bufHashes ? F.bufHashes.get(vb.buf) : null; // always use bridge hash
              if (!bytes && !preHash) { noteDrop('draw-vertex-buffer-missing', 'buf=' + vb.buf + ' binding=' + vb.binding); continue; }
              let vkey = vb.buf, vbytes = bytes;
              const walk = (st.pipeRec.vbWalk || []).find(w => w.binding === vb.binding);
              const swizzled = walk && walk.attrs44.length;
              if (swizzled) vkey += '|swz44:' + st.pipeRec.key;
              const scaled = walk?.scaled.length > 0;
              const repacked = scaled || walk?.paddedStride;
              if (scaled) vkey += '|scaled:' + st.pipeRec.key + ':' + vb.offset;
              else if (walk?.paddedStride) vkey += '|padded-stride:' + walk.outputStride + ':' + vb.offset;
              const repeatCount = walk?.stride === 0 ? (op.firstInstance || 0) + Math.max(1, op.instanceCount || 1) : 1;
              if (walk?.stride === 0) vkey += '|constant-instance:' + repeatCount;
              const usage = GPUBufferUsage.VERTEX | GPUBufferUsage.COPY_DST;
              // Converted buffers already carry the source content hash and
              // vertex layout in their cache key. Check before converting:
              // the same static mesh is bound by many draws and frames.
              let gpu = preHash ? persistBufCache.get(bufferKey(vkey, null, usage, 0, preHash)) : null;
              if (gpu) persistStats.bufHits++;
              else {
                if (swizzled) vbytes = swizzleBgraVb(bytes, walk.stride, walk.attrs44);
                if (repacked && vbytes) vbytes = repackScaledVb(vbytes.subarray(vb.offset), walk.stride, walk.outputStride, walk.scaled, walk.rawAttributes, repeatCount);
                gpu = gpuBufferFor(vkey, vbytes, usage, preHash);
              }
              if (!gpu) { noteDrop('draw-vertex-buffer-gpu-failed', 'buf=' + vb.buf + ' binding=' + vb.binding); continue; }
              for (const {slot,offset} of slots) {
                passEnc.setVertexBuffer(slot, gpu, repacked ? offset : Math.min(vb.offset + offset, gpu.size));
                vbSet.add(slot);
              }
            }
            // Slots the guest left unbound (null buffer) still need a buffer.
            for (const {slot} of [...st.pipeRec.slotOf.values()].flat()) {
              if (!vbSet.has(slot)) passEnc.setVertexBuffer(slot, dummyVb, 0);
            }
            const triangleFan = drawPipe.topology === 5;
            if (triangleFan) {
              let indices, cacheKey;
              if (op.indexed && st.ibind) {
                const bytes = F.buffers.get(st.ibind.buf);
                if (!bytes) { noteUnsupported('draw-fan-index-bytes-missing', st.ibind.buf); noteSkippedDraw('draw-fan-index-bytes-missing'); break; }
                const hash = F.bufHashes?.get(st.ibind.buf) || fnv1a(bytes);
                const indexByteOffset = Math.min(Math.max(0, st.ibind.offset), bytes.byteLength);
                indices = v2IndexedTriangleFanIndices(bytes.subarray(indexByteOffset), st.ibind.indexType, op.firstIndex, op.indexCount,
                  !!drawPipe.primitiveRestartEnable);
                cacheKey = `fan-indexed:${st.ibind.buf}:${hash}:${indexByteOffset}:${op.firstIndex}:${op.indexCount}:${st.ibind.indexType}:${drawPipe.primitiveRestartEnable ? 1 : 0}`;
              } else if (!op.indexed) {
                indices = v2TriangleFanIndices(op.vertexCount, op.firstVertex);
                cacheKey = `fan-vertices:${op.firstVertex}:${op.vertexCount}`;
              } else { noteUnsupported('draw-fan-indexed-without-index-buffer'); noteSkippedDraw('draw-fan-indexed-without-index-buffer'); break; }
              if (indices.length) {
                const gpu = gpuBufferFor(cacheKey, new Uint8Array(indices.buffer), GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST);
                if (gpu) {
                  passEnc.setIndexBuffer(gpu, 'uint32', 0);
                  passEnc.drawIndexed(indices.length, op.instanceCount, 0, op.indexed ? op.vertexOffset : 0, op.firstInstance);
                }
              }
            } else {
              let indexed = op.indexed;
              if (indexed && st.ibind) {
                const bytes = F.buffers.get(st.ibind.buf);
                const preHash = F.bufHashes ? F.bufHashes.get(st.ibind.buf) : null; // always use bridge hash
                if (!bytes && !preHash) { noteDrop('draw-index-buffer-missing', st.ibind.buf); noteSkippedDraw('draw-index-buffer-missing'); break; }
                else {
                  const gpu = gpuBufferFor(st.ibind.buf, bytes, GPUBufferUsage.INDEX | GPUBufferUsage.COPY_DST, preHash);
                  if (!gpu) { noteDrop('draw-index-buffer-gpu-failed', st.ibind.buf); noteSkippedDraw('draw-index-buffer-gpu-failed'); break; }
                  else passEnc.setIndexBuffer(gpu, st.ibind.indexType ? 'uint32' : 'uint16', Math.min(st.ibind.offset, gpu.size));
                }
              } else if (indexed) { noteDrop('draw-indexed-without-index-buffer'); noteSkippedDraw('draw-indexed-without-index-buffer'); break; }
              if (indexed) passEnc.drawIndexed(op.indexCount, op.instanceCount, op.firstIndex, op.vertexOffset, op.firstInstance);
              else if (op.vertexCount > 0) passEnc.draw(op.vertexCount, op.instanceCount, op.firstVertex, op.firstInstance);
            }
            break;
          }
          default: noteUnsupported('unknown-replay-op', op.op); break;
        }
        if (skipped) break;
      }
      if (passEnc) passEnc.end();
      if (diagOn && passEnc && diagPasses.length < diagCfg.maxPasses) {
        const entry = { pass: diagPasses.length, w: p.w, h: p.h, loadClear: p.attachments.map(a => a.loadOp === 1), draws: passDiagDraws.draws, rawDraws: passDiagDraws.raw, skips: passDiagDraws.skips,
          vs: [...passDiagDraws.vs].slice(0, 8), fs: [...passDiagDraws.fs].slice(0, 8), list: passDiagDraws.list,
          unsupported: stats.unsupported - passDiagStart.unsupported, blank: blankUses - passDiagStart.blank, census: v2DiagImageCensus(F), targets: [] };
        for (const t of passDiagTargets) {
          const bpt = v2DiagBytesPerTexel(t.format);
          if (!bpt || t.discard || p.w * p.h > diagCfg.maxPixels) {
            entry.targets.push({ slot: t.slot, format: t.format, skipped: !bpt ? 'format' : t.discard ? 'discarded' : 'size' });
            continue;
          }
          const bytesPerRow = Math.ceil(p.w * bpt / 256) * 256;
          const buffer = device.createBuffer({ size: bytesPerRow * p.h, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
          encoder.copyTextureToBuffer({ texture: t.tex }, { buffer, bytesPerRow }, [p.w, p.h]);
          entry.targets.push({ slot: t.slot, format: t.format, buffer, bytesPerRow });
        }
        diagPasses.push(entry);
      }
      if (skipped) { stats.skips++; return { rendered: false, reason: skipped }; }
    }

    if (!presentTex && present) { stats.skips++; return { rendered: false, reason: 'no-colour-target' }; }

    // V2_READBACK records (guest vkCmdCopyImageToBuffer): copy the source
    // image to a MAP_READ buffer now, resolve to the wasm heap after submit.
    // The bridge is blocked in Atomics.wait on flagPtr; we must ALWAYS notify
    // (even on failure) so it never hangs the full 10s timeout.
    const v2readbacks = [];
    if (frame.readbacks && frame.readbacks.length) {
      for (const rb of frame.readbacks) {
        const tex = targets.get('img:' + rb.srcImage) || targets.get('imgd:' + rb.srcImage);
        const rbBytesPerRow = Math.ceil(rb.w * 4 / 256) * 256;
        if (!tex) {
          v2readbacks.push({ rb, buffer: null, bytesPerRow: rbBytesPerRow, ok: false });
          continue;
        }
        try {
          const buf = device.createBuffer({ size: rbBytesPerRow * rb.h, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
          encoder.copyTextureToBuffer({ texture: tex }, { buffer: buf, bytesPerRow: rbBytesPerRow }, [rb.w, rb.h]);
          v2readbacks.push({ rb, buffer: buf, bytesPerRow: rbBytesPerRow, ok: true });
        } catch (e) {
          v2readbacks.push({ rb, buffer: null, bytesPerRow: rbBytesPerRow, ok: false });
        }
      }
    }

    // Readback of the present target (COPY_SRC) plus the present copy to the
    // page canvas, like the v1 tier. The readback exists only for the
    // lit/painted stats; it is sampled every STATS_EVERY frames to avoid a
    // GPU->CPU sync stall on every frame. The present copy always runs.
    const doStats = present && (stats.frames % STATS_EVERY) === 0;
    device.pushErrorScope('validation');
    const bytesPerRow = Math.ceil(presentW * 4 / 256) * 256;
    let readback = null;
    if (doStats) {
      readback = device.createBuffer({ size: bytesPerRow * presentH, usage: GPUBufferUsage.COPY_DST | GPUBufferUsage.MAP_READ });
      encoder.copyTextureToBuffer({ texture: presentTex }, { buffer: readback, bytesPerRow }, [presentW, presentH]);
    }
    if (present && (presentTex.width < presentW || presentTex.height < presentH))
      throw new Error(`present target ${presentTex.label} is ${presentTex.width}x${presentTex.height}, requested ${presentW}x${presentH}`);
    // The canvas (and therefore getCurrentTexture()) follows the DISPLAY pass
    // size, which can lag a frame whose last colour pass is larger: the guest
    // often renders a small offscreen pass first and presents a full-size one
    // later. Dawn then rejects the whole frame ("copy range touches outside"),
    // so grow the canvas to the present size before the copy. Never shrink:
    // a larger canvas still receives the copy at (0,0).
    if (present && (canvas.width < presentW || canvas.height < presentH)) {
      canvas.width = presentW; canvas.height = presentH;
      obsW = canvas.width; obsH = canvas.height;
      context.configure({ device, format, alphaMode: 'opaque', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST });
    }
    if (present) {
      const presentDest = context.getCurrentTexture();
      if (presentDest.width < presentW || presentDest.height < presentH)
        throw new Error(`canvas texture is ${presentDest.width}x${presentDest.height}, present copy is ${presentW}x${presentH}`);
      if ((presentTex.format || format).replace(/-srgb$/, '') === format.replace(/-srgb$/, '')) {
        encoder.copyTextureToTexture({ texture: presentTex }, { texture: presentDest }, [presentW, presentH]);
      } else {
        // The guest swapchain can be BGRA while the browser canvas is RGBA.
        // Copying those formats is invalid; a texel load preserves colour
        // channels and the already encoded sRGB bytes without another gamma step.
        if (!presentPipeline) {
          const module = device.createShaderModule({ code: `
@group(0) @binding(0) var image: texture_2d<f32>;
@vertex fn vs(@builtin(vertex_index) i: u32) -> @builtin(position) vec4<f32> {
 let p = array<vec2<f32>, 3>(vec2<f32>(-1.0, -1.0), vec2<f32>(3.0, -1.0), vec2<f32>(-1.0, 3.0));
 return vec4<f32>(p[i], 0.0, 1.0);
}
@fragment fn fs(@builtin(position) p: vec4<f32>) -> @location(0) vec4<f32> {
 return textureLoad(image, vec2<i32>(p.xy), 0);
}` });
          presentPipeline = device.createRenderPipeline({ layout: 'auto',
            vertex: { module, entryPoint: 'vs' }, fragment: { module, entryPoint: 'fs', targets: [{ format }] },
            primitive: { topology: 'triangle-list' } });
        }
        const pass = encoder.beginRenderPass({ colorAttachments: [{ view: presentDest.createView(),
          loadOp: 'clear', storeOp: 'store', clearValue: { r: 0, g: 0, b: 0, a: 1 } }] });
        pass.setPipeline(presentPipeline);
        pass.setBindGroup(0, device.createBindGroup({ layout: presentPipeline.getBindGroupLayout(0),
          entries: [{ binding: 0, resource: presentTex.createView() }] }));
        pass.setViewport(0, 0, presentW, presentH, 0, 1);
        pass.draw(3); pass.end();
      }
    }
    gpuTimer.end(encoder, bwGpuSlot);
    const bwSubmitT0 = metricsNow();
    device.queue.submit([encoder.finish()]);
    timing.submitMs += metricsNow() - bwSubmitT0;
    timing.submitCount++;
    gpuTimer.resolve(bwGpuSlot);
    const invalid = await device.popErrorScope();
    if (invalid) { if (readback) readback.destroy(); for (const e of diagPasses) for (const t of e.targets) t.buffer?.destroy(); throw new Error('v2 frame encoding rejected: ' + invalid.message); }
    if (diagPasses.length) {
      for (const e of diagPasses) for (const t of e.targets) {
        if (!t.buffer) continue;
        try {
          await t.buffer.mapAsync(GPUMapMode.READ);
          Object.assign(t, v2SummarizeColorPixels(new Uint8Array(t.buffer.getMappedRange()), e.w, e.h, t.bytesPerRow, t.format));
          t.buffer.unmap();
        } catch (err) { t.error = String(err?.message || err); }
        t.buffer.destroy(); delete t.buffer;
      }
      const report = { frame: stats.frames, passes: diagPasses };
      (globalThis.bwPassReadbackLog ||= []).push(report);
      if (globalThis.bwPassReadbackLog.length > 8) globalThis.bwPassReadbackLog.shift();
      console.info('[bwPassReadback]', JSON.stringify(report));
    }

    if (doStats) {
      await readback.mapAsync(GPUMapMode.READ);
      const rows = new Uint8Array(readback.getMappedRange());
      const [r, g, b, a] = lastClear.map(c => Math.round(c * 255));
      const bgra = (presentTex.format || format).startsWith('bgra');
      let lit = 0, painted = 0;
      for (let y = 0; y < presentH; y++) {
        const row = y * bytesPerRow;
        for (let x = 0; x < presentW; x++) {
          const i = row + x * 4;
          const pr = rows[i], pg = rows[i + 1], pb = rows[i + 2], pa = rows[i + 3];
          if (pr || pg || pb || pa) painted++;
          if (pr !== (bgra ? b : r) || pg !== g || pb !== (bgra ? r : b) || pa !== a) lit++;
        }
      }
      readback.unmap();
      readback.destroy();
      stats.litPixels = lit; stats.paintedPixels = painted;
      stats.totalPixels = presentW * presentH; stats.width = presentW; stats.height = presentH;
    }

    // Resolve V2_READBACKs: copy GPU pixels into the wasm heap and wake the
    // bridge's Atomics.wait. Always notify, even on failure.
    for (const vrb of v2readbacks) {
      const { rb, buffer, bytesPerRow: rbBpr, ok } = vrb;
      try {
        const heapBuf = globalThis.bwVkHeapBuffer;
        if (!(ok && buffer && heapBuf)) {
          noteUnsupported('readback-unavailable', 'ok=' + !!ok + ' buffer=' + !!buffer + ' heap=' + !!heapBuf + '; guest receives unfilled pixels');
        }
        if (ok && buffer && heapBuf) {
          await buffer.mapAsync(GPUMapMode.READ);
          const src = new Uint8Array(buffer.getMappedRange());
          const pxBytes = rb.w * rb.h * 4;
          const dst = new Uint8Array(heapBuf, rb.heapPtr, pxBytes);
          for (let y = 0; y < rb.h; y++) {
            dst.set(src.subarray(y * rbBpr, y * rbBpr + rb.w * 4), y * rb.w * 4);
          }
          buffer.unmap();
        }
      } catch (e) { noteUnsupported('readback-failed', e && e.message); /* still notify so the guest does not hang */ }
      try {
        if (buffer) buffer.destroy();
      } catch (e) {}
      try {
        const heapBuf = globalThis.bwVkHeapBuffer;
        if (heapBuf) Atomics.notify(new Int32Array(heapBuf, rb.flagPtr, 1), 0, 1);
      } catch (e) { noteUnsupported('readback-notify-failed', e && e.message); }
    }

    const ownedResources = F.live.splice(0);
    const release = () => { for (const x of ownedResources) { try { x.destroy(); } catch { /* ignore */ } } };
    device.queue.onSubmittedWorkDone().then(release, release);

    stats.frames++; stats.rendered++;
    if (present) pacer.present();   // FPS counts DISPLAYED frames only
    timing.renderMs += metricsNow() - bwRenderT0;
    timing.renderCount++;
    return {
      rendered: true, frame: frame.frameNo, litPixels: stats.litPixels, paintedPixels: stats.paintedPixels,
      totalPixels: presentW * presentH, clear: lastClear,
    };
  }

  return {
    render, errors, device, shaders, snapshot, metrics, stats, invalidateTargets,
    learnMinBindingSize,
    get minBindingSizes() { return Object.fromEntries(uniformMinSize); },
    get frames() { return stats.rendered; },
    destroy() {
      destroyed = true;
      try { resizeObs?.disconnect(); } catch { /* ignore */ }
      try { gpuTimer.destroy(); } catch { /* ignore */ }
      context.unconfigure(); invalidateTargets(); device.destroy();
    },
  };
}
export {
  MAX_DIMENSION, MAX_DRAWS, V2_MAGIC, V2_VERSION, V2_FLAG_MORE,
  V2_CHUNK_HEADER_LEN, V2_REC_HEADER_LEN, V2_MAX_CHUNK_BYTES,
  V2_MAX_RECORD_BYTES, V2_MAX_FRAME_RECORDS, V2_PUSH_GROUP,
  V2_PUSH_BINDING, V2_PUSH_SIZE, V2_QUEUE_CAP, V2_MAX_SETS, V2_REC,
  v2Hex64, parseV2Chunk, V2FrameAssembler, V2PresentAssembler, V2FrameQueue,
  rgba16UnormToHalf, v2TextureFormat, v2TextureMipUploadPlan, v2ImageBlitRegion,
  v2TriangleFanIndices, v2IndexedTriangleFanIndices, repackScaledVb, v2VertexFormat,
  decodeV2Frame, swizzleBgraVb, V2_UNKNOWN_IMAGE,
} from './vk-protocol.mjs';
export {
  rewritePushConstantsWGSL, stripNagaYNegation, rewriteResolvedDxvkPresent,
  stripNagaFragmentDepth, scanWgslResources, wgslUniformSizes, parseMinBindingSize,
} from './wgsl-utils.mjs';
