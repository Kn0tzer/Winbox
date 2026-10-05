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
import { toWGSL } from './shader.mjs';

export const MAX_MANIFEST_BYTES = 8 * 1024 * 1024; // a frame's JSON + inline payloads
export const MAX_DIMENSION = 8192;
export const MAX_DRAWS = 4096;
export const MAX_UNIFORM_RANGE = 65536;            // guaranteed maxUniformBufferBindingSize

// Vulkan enums, mapped once (the numeric tables are P1's, host/p1render.mjs).
const FILTER = { 0: 'nearest', 1: 'linear' };
const ADDR = { 0: 'repeat', 1: 'mirror-repeat', 2: 'clamp-to-edge', 3: 'clamp-to-edge' };
const DEPTH_OP = { 0: 'never', 1: 'less', 2: 'equal', 3: 'less-equal', 4: 'greater', 5: 'not-equal', 6: 'greater-equal', 7: 'always' };
const CULL = { 0: 'none', 1: 'front', 2: 'back' };
const FRONT = { 0: 'ccw', 1: 'cw' };
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
 * @param {{translate?: (from: string, source: Uint8Array, stage: string) => Promise<string>, log?: (msg: string) => void}} [options]
 */
export function createShaderCache({ translate = toWGSL, log = defaultLog } = {}) {
  const wgsl = new Map();      // key -> WGSL
  const stats = { translated: 0, overridden: 0, rejected: 0, hits: 0 };
  return {
    stats,
    async get(stage, mod) {
      if (!mod) return null;
      const key = stage + ':' + mod.hash + ':' + mod.size;
      if (wgsl.has(key)) { stats.hits++; return wgsl.get(key); }
      const override = FS_OVERRIDES.get(mod.hash + ':' + mod.size);
      if (override) { wgsl.set(key, override); stats.overridden++; return override; }
      try {
        const code = String(await translate('spirv', mod.code, stage) ?? '');
        if (!code) throw new Error('empty translation');
        wgsl.set(key, code); stats.translated++;
        return code;
      } catch (e) {
        stats.rejected++;
        log('vk64: page tier cannot translate the ' + stage + ' SPIR-V module (' + (e.message || e) + ') — frame skipped');
        return null;
      }
    },
  };
}

function defaultLog(msg) {
  try { (globalThis.console && console.warn || (() => {}))(msg); } catch { /* logging must never throw */ }
  try { if (typeof globalThis.bwRuntimeLog === 'function') globalThis.bwRuntimeLog(msg); } catch { /* ignore */ }
}

// ---------------------------------------------------------------------------
// createVkRenderer — web/webgpu.mjs:80 createRenderer's shape (adapter -> device
// -> persistent offscreen colour+depth -> per-key pipeline cache ->
// queue.onSubmittedWorkDone() for buffer release) generalised from the
// fixed-function GL subset to the Vulkan frame schema.
// ---------------------------------------------------------------------------
/**
 * @param {HTMLCanvasElement} canvas
 * @param {{translate?: (from: string, source: Uint8Array, stage: string) => Promise<string>, log?: (msg: string) => void}} [options]
 */
export async function createVkRenderer(canvas, { translate, log = defaultLog } = {}) {
  if (!navigator.gpu) throw new Error('WebGPU is unavailable');
  const adapter = await navigator.gpu.requestAdapter();
  if (!adapter) throw new Error('No WebGPU adapter');
  const device = await adapter.requestDevice();
  const errors = [];
  device.addEventListener('uncapturederror', e => log('vk64: page tier WebGPU error: ' + (e.error?.message || e.error)));
  device.lost.then(info => errors.push('Device lost: ' + info.reason + ' ' + info.message));

  const context = canvas.getContext('webgpu');
  if (!context) throw new Error('No WebGPU canvas context');
  const format = navigator.gpu.getPreferredCanvasFormat();
  context.configure({ device, format, alphaMode: 'opaque', usage: GPUTextureUsage.RENDER_ATTACHMENT | GPUTextureUsage.COPY_DST });

  const shaders = createShaderCache({ translate, log });
  const pipelines = new Map();     // pipeline key -> GPURenderPipeline
  const uniformSize = new Map();   // vs hash -> working uniform range
  const live = [];                 // buffers released after the frame's work is done
  const stats = {
    frames: 0, rendered: 0, skips: 0,
    litPixels: 0, paintedPixels: 0, totalPixels: 0, width: 0, height: 0,
  };
  const snapshot = () => ({ ...stats, ...shaders.stats });
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
    device.queue.submit([encoder.finish()]);
    return { invalid: await device.popErrorScope(), buffer, readback, bytesPerRow };
  }

  async function render(text) {
    if (destroyed) throw new Error('page tier destroyed');
    const man = parseManifest(text);
    ensureTargets(man.width, man.height);

    const vsWgsl = await shaders.get('vertex', man.vs);
    const fsWgsl = await shaders.get('fragment', man.fs);
    if (!vsWgsl || !fsWgsl) { stats.skips++; return { rendered: false, reason: 'shader-translation' }; }
    const [vx, vy, vw, vh] = man.viewport;
    if (vw <= 0 || vh <= 0) { stats.skips++; return { rendered: false, reason: 'empty viewport' }; }

    const key = [vsWgsl.length, fsWgsl.length, man.cull, man.front, man.depthTest, man.depthWrite, man.depthOp].join(':');
    let pipeline = null, bgl = null;
    if (!pipelines.has(key)) {
      const vsModule = device.createShaderModule({ code: vsWgsl });
      const fsModule = device.createShaderModule({ code: fsWgsl });
      device.pushErrorScope('validation');
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
    return { rendered: true, frame: man.frame, litPixels: lit, paintedPixels: painted,
      totalPixels: w * h, uniform, uboBytes: man.ubo ? man.ubo.length : 0, clear: man.clear };
  }

  return {
    render, errors, device, shaders, snapshot,
    stats,
    get frames() { return stats.rendered; },
    destroy() { destroyed = true; context.unconfigure(); color?.destroy(); depth?.destroy(); device.destroy(); },
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
  const state = { received: 0, rendered: 0, rejected: 0, errors: [], last: null,
    litPixels: 0, paintedPixels: 0, totalPixels: 0 };
  const stats = () => ({ ...state, errors: state.errors.slice(-8), pipeline: renderer?.snapshot() || null });
  let renderer = null, pending = Promise.resolve(), starting = null;

  async function frame(json) {
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
      state.errors.push(String(e.message || e));
      log('vk64: page tier frame rejected — ' + (e.message || e));
      return { rendered: false, reason: String(e.message || e) };
    }
  }

  function defaultCanvas() {
    const el = document.createElement('canvas');
    el.id = 'vkCanvas';
    (document.getElementById('display') || document.body).append(el);
    return el;
  }

  const tier = {
    frame(json) { pending = pending.then(() => frame(json)); return pending; },
    stats,
    get ready() { return !!renderer; },
    get litPixels() { return state.litPixels; },
  };
  globalThis.bwVkTier = tier;
  globalThis.bwVkStats = stats;
  return tier;
}
