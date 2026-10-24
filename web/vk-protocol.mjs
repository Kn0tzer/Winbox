// Vulkan v2 wire parsing, frame assembly, and format conversion.
import { bwLog } from './vk-log.mjs';

// Vulkan 1.3 / EXT_extended_dynamic_state{,2} state is part of the V2
// protocol contract. Keep it here so the browser export has one copied module
// for wire decoding and replay-state interpretation.
//
// The bridge records every vkCmdSet{PrimitiveTopology,DepthTestEnable,DepthWriteEnable,
// DepthCompareOp,StencilTestEnable,StencilOp,DepthBiasEnable,RasterizerDiscardEnable,
// PrimitiveRestartEnable,DepthBoundsTestEnable} as a RASTER_STATE record (which, value);
// the pipeline's DYN1 tail says which of those states the pipeline takes from commands.
// applyDynamicState() overlays the recorded values on the static pipeline at draw time.
// A state the pipeline did not declare dynamic keeps its static value (Vulkan: the
// command only applies when the pipeline lists the state in pDynamicStates).

export const DYN_ID = Object.freeze({
  cull: 0, front: 1, topology: 2, depthTest: 3, depthWrite: 4, depthOp: 5,
  stencilTest: 6, depthBiasEnable: 7, rasterizerDiscard: 8, primitiveRestart: 9,
  stencilOp: 10, depthBoundsTest: 11,
});
const DYN_NAME = Object.freeze(Object.fromEntries(Object.entries(DYN_ID).map(([k, v]) => [v, k])));
export const DYN_MASK_LIMIT = 1 << 12;

const isBool = v => v === 0 || v === 1;
// Decodes a RASTER_STATE record. Returns {which, value}; throws on an id/value the
// bridge could never have produced (a corrupt or hostile frame).
export function decodeDynState(which, value) {
  const name = DYN_NAME[which];
  if (name === undefined) throw new Error('vk64: v2 invalid raster state');
  switch (name) {
    case 'cull': case 'front':
      if (value > 3) throw new Error('vk64: v2 invalid raster state');
      return { which: name, value };
    case 'topology':
      if (value > 5) throw new Error('vk64: v2 invalid dynamic topology');
      return { which: name, value };
    case 'depthOp':
      if (value > 7) throw new Error('vk64: v2 invalid dynamic depth op');
      return { which: name, value };
    case 'stencilOp': {
      const op = {
        faceMask: value & 3, failOp: (value >>> 4) & 7, passOp: (value >>> 8) & 7,
        depthFailOp: (value >>> 12) & 7, compareOp: (value >>> 16) & 7,
      };
      if (value >>> 19 || !op.faceMask) throw new Error('vk64: v2 invalid dynamic stencil op');
      return { which: name, value: op };
    }
    default:
      if (!isBool(value)) throw new Error('vk64: v2 invalid dynamic ' + name);
      return { which: name, value };
  }
}

const DEFAULT_FACE = Object.freeze({ failOp: 0, passOp: 0, depthFailOp: 0, compareOp: 7, compareMask: 255, writeMask: 255, reference: 0 });

// dyn: Map/object name -> last recorded value. Returns the pipeline to draw with
// (the input object when nothing is overridden).
export function applyDynamicState(pipe, dyn) {
  const mask = pipe.dynMask | 0;
  const has = id => (mask & (1 << id)) !== 0 && dyn[DYN_NAME[id]] !== undefined;
  let out = pipe, copied = false;
  const set = patch => { out = copied ? Object.assign(out, patch) : { ...out, ...patch }; copied = true; };
  if (has(DYN_ID.topology)) set({ topology: dyn.topology });
  if (has(DYN_ID.depthTest)) set({ depthTest: dyn.depthTest });
  if (has(DYN_ID.depthWrite)) set({ depthWrite: dyn.depthWrite });
  if (has(DYN_ID.depthOp)) set({ depthOp: dyn.depthOp });
  if (has(DYN_ID.depthBiasEnable)) set({ depthBiasEnable: dyn.depthBiasEnable });
  if (has(DYN_ID.primitiveRestart)) set({ primitiveRestartEnable: !!dyn.primitiveRestart });
  if (has(DYN_ID.rasterizerDiscard)) set({ rasterizerDiscard: dyn.rasterizerDiscard });
  if (has(DYN_ID.stencilTest) || has(DYN_ID.stencilOp)) {
    const base = out.stencil || { enabled: 0, dynamicMask: 0, front: { ...DEFAULT_FACE }, back: { ...DEFAULT_FACE } };
    const stencil = { ...base, front: { ...base.front }, back: { ...base.back } };
    if (has(DYN_ID.stencilTest)) stencil.enabled = dyn.stencilTest;
    if (has(DYN_ID.stencilOp)) {
      const o = dyn.stencilOp;
      for (const [bit, face] of [[1, stencil.front], [2, stencil.back]])
        if (o.faceMask & bit) { face.failOp = o.failOp; face.passOp = o.passOp; face.depthFailOp = o.depthFailOp; face.compareOp = o.compareOp; }
    }
    set({ stencil });
  }
  return out;
}

export const MAX_DIMENSION = 8192;
export const MAX_DRAWS = 4096;

// Vulkan enums, mapped once (the numeric tables are P1's, host/p1render.mjs).
export const FILTER = { 0: 'nearest', 1: 'linear' };
// WebGPU has no border addressing or mirror-clamp mode; both use clamp-to-edge.
export const ADDR = { 0: 'repeat', 1: 'mirror-repeat', 2: 'clamp-to-edge', 3: 'clamp-to-edge', 4: 'clamp-to-edge' };
// ===========================================================================
// Schema v2 — binary framed records (docs/vulkan-protocol.md §2-3). The v1 JSON
// manifest (parseManifest/createVkRenderer in vkwebgpu.mjs) is vkcube-shaped: one frame
// per submit, vs+fs only, a single descriptor set, no push constants, no
// MSAA. Real D3D9 apps via DXVK submit continuously with N descriptor sets,
// push constants per draw, indexed draws and multisampled targets; one giant
// JSON per submit cannot keep up, so v2 streams binary records.
//
// WIRE. The wasm side hops one MAIN_THREAD_EM_ASM per CHUNK (12 MiB cap,
// under this module's 16MB ingest limit; records never straddle chunks); the
// JS body receives (ptr, len, flags), copies the bytes once, and calls
// window.bwVkChunk(bytes, flags). No UTF-8 decode, no JSON.parse, no base64:
// chunk header (16 B: u32 magic 'VK2F', u16 version=2, u16 flags bit0=MORE,
// u32 seq) followed by records (8 B header: u16 type, u16 rflags, u32 len +
// payload). Records never straddle chunks. BW64_VKSCHEMA=1 keeps the v1 path;
// the page needs no sniffing because v1 arrives at window.bwVkFrame and v2 at
// window.bwVkChunk.
//
// BACKPRESSURE. Neither queue ever blocks the guest. Producer (wasm): a deque
// of complete frames capped at 8 chunks, drop-oldest-complete-frame. Consumer
// (page): V2FrameQueue, cap 3 complete frames, drop-oldest on push; the render
// pump takes the newest via takeLatest() and drops everything older —
// latest-frame-wins. Producer drops are host-side (klog); the page detects
// loss through chunkGaps on the chunk sequence.
//
// DISCIPLINE. Like the v1 tier: malformed input is counted and the frame is
// skipped, never thrown back at the guest (the hop runs on the guest's
// present path).
// ===========================================================================

export const V2_MAGIC = 0x46324B56; // 'VK2F' little-endian (bytes 56 4B 32 46)
export const V2_VERSION = 2;
export const V2_FLAG_MORE = 0x1;
export const V2_CHUNK_HEADER_LEN = 16;
export const V2_REC_HEADER_LEN = 8;
// Ingest sanity cap. Records never span chunks, and IMAGE_DATA carries a
// whole mip chain: a 4096x4096 RGBA image with mips is ~89MiB. 128MiB keeps
// one such image in a single chunk while still bounding the allocation.
export const V2_MAX_CHUNK_BYTES = 128 * 1024 * 1024;
export const V2_MAX_RECORD_BYTES = 256 * 1024 * 1024;
export const V2_MAX_FRAME_RECORDS = 1 << 20;
export const V2_PUSH_GROUP = 3;      // reserved group for the push-constant UBO
export const V2_PUSH_BINDING = 0;
export const V2_PUSH_SIZE = 256;     // == VK64_MAX_PUSH_BYTES on the host
export const V2_QUEUE_CAP = 3;       // consumer frames: latest-frame-wins
export const V2_MAX_SETS = 8;

export const V2_REC = Object.freeze({
  FRAME_BEGIN: 0x01, FRAME_END: 0x02, RP_BEGIN: 0x03, RP_END: 0x04,
  SHADER: 0x05, PIPELINE: 0x06, BIND_SETS: 0x07, DESC_SET: 0x08,
  BUFFER_DATA: 0x09, IMAGE_DATA: 0x0A, SAMPLER: 0x0B, PUSH: 0x0C,
  VIEWPORT: 0x0D, SCISSOR: 0x0E, VERTEX_BIND: 0x0F, INDEX_BIND: 0x10,
  DRAW: 0x11, INLINE_BYTES: 0x12, READBACK: 0x13, IMAGE_COPY: 0x14,
  STENCIL_STATE: 0x15, BLEND_CONSTANTS: 0x16, CLEAR_ATTACHMENTS: 0x17, DEPTH_BIAS: 0x18,
  IMAGE_BLIT: 0x19, RASTER_STATE: 0x1A, IMAGE_CLEAR_DS: 0x1B,
});

// Vulkan descriptor types (numeric, as the bridge records them).
const DT_COMBINED_IMAGE_SAMPLER = 1, DT_SAMPLER = 0;

class V2Reader {
  constructor(payload) {
    this.dv = new DataView(payload.buffer, payload.byteOffset, payload.byteLength);
    this.p = 0;
  }
  get left() { return this.dv.byteLength - this.p; }
  need(n, what) { if (this.left < n) throw new Error('vk64: v2 ' + what + ' truncated'); }
  u8() { this.need(1, 'u8'); return this.dv.getUint8(this.p++); }
  u16() { this.need(2, 'u16'); const v = this.dv.getUint16(this.p, true); this.p += 2; return v; }
  u32() { this.need(4, 'u32'); const v = this.dv.getUint32(this.p, true); this.p += 4; return v; }
  i32() { this.need(4, 'i32'); const v = this.dv.getInt32(this.p, true); this.p += 4; return v; }
  u64() { this.need(8, 'u64'); const v = this.dv.getBigUint64(this.p, true); this.p += 8; return v; }
  f32() { this.need(4, 'f32'); const v = this.dv.getFloat32(this.p, true); this.p += 4; return v; }
  bytes(n) {
    this.need(n, 'bytes(' + n + ')');
    const v = new Uint8Array(this.dv.buffer, this.dv.byteOffset + this.p, n);
    this.p += n; return v;
  }
  skip(n) { this.need(n, 'skip'); this.p += n; }
  done(what) {
    if (this.p !== this.dv.byteLength)
      throw new Error('vk64: v2 ' + what + ' has ' + (this.dv.byteLength - this.p) + ' trailing bytes');
  }
}

// The host prints hashes with %016llx; reproduce that exactly so the v1
// FS_OVERRIDES keys and the shader cache keep working for v2 modules.
export const v2Hex64 = h => h.toString(16).padStart(16, '0');

// ---------------------------------------------------------------------------
// Chunk framing: parseV2Chunk validates the header and splits the records.
// Throws (counted by the caller) on bad magic/version, truncation, or an
// unknown record type. Never touches the payloads.
// ---------------------------------------------------------------------------
export function parseV2Chunk(bytes) {
  if (!(bytes instanceof Uint8Array)) throw new Error('vk64: v2 chunk is not bytes');
  if (bytes.length < V2_CHUNK_HEADER_LEN || bytes.length > V2_MAX_CHUNK_BYTES)
    throw new Error('vk64: v2 chunk size out of range (' + bytes.length + ')');
  const dv = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (dv.getUint32(0, true) !== V2_MAGIC) throw new Error('vk64: v2 chunk bad magic');
  const version = dv.getUint16(4, true);
  if (version !== V2_VERSION) throw new Error('vk64: v2 chunk version ' + version + ' (want 2)');
  const flags = dv.getUint16(6, true);
  const seq = dv.getUint32(8, true);
  const records = [];
  let p = V2_CHUNK_HEADER_LEN;
  while (p < bytes.length) {
    if (p + V2_REC_HEADER_LEN > bytes.length) throw new Error('vk64: v2 chunk truncated record header');
    const type = dv.getUint16(p, true);
    const len = dv.getUint32(p + 4, true);
    if (type < V2_REC.FRAME_BEGIN || type > V2_REC.IMAGE_CLEAR_DS)
      throw new Error('vk64: v2 unknown record type 0x' + type.toString(16));
    if (len > V2_MAX_RECORD_BYTES || p + V2_REC_HEADER_LEN + len > bytes.length)
      throw new Error('vk64: v2 record 0x' + type.toString(16) + ' truncated (len ' + len + ')');
    records.push({ type, payload: new Uint8Array(bytes.buffer, bytes.byteOffset + p + V2_REC_HEADER_LEN, len) });
    p += V2_REC_HEADER_LEN + len;
  }
  return { flags, seq, more: (flags & V2_FLAG_MORE) !== 0, records };
}

// ---------------------------------------------------------------------------
// V2FrameAssembler — feeds parsed chunks, emits a complete record list when a
// FRAME_END seals a frame. A sequence gap (or a second FRAME_BEGIN) drops the
// partial frame: a half frame is unrenderable, and the guest keeps presenting
// behind us. gaps/droppedPartials feed window.bwVkStats().
// ---------------------------------------------------------------------------
export class V2FrameAssembler {
  constructor() { this.records = []; this.inFrame = false; this.lastSeq = -1; this.gaps = 0; this.droppedPartials = 0; }
  ingest(chunk) {
    if (this.lastSeq >= 0 && chunk.seq !== ((this.lastSeq + 1) >>> 0)) {
      this.gaps++;
      if (this.inFrame) this.droppedPartials++;
      this.records = []; this.inFrame = false;
    }
    this.lastSeq = chunk.seq;
    let frame = null;
    for (const r of chunk.records) {
      if (r.type === V2_REC.FRAME_BEGIN) {
        if (this.inFrame) this.droppedPartials++;
        this.records = [r]; this.inFrame = true;
      } else if (!this.inFrame) {
        continue; // stray record outside a frame (e.g. after a gap) — ignore
      } else {
        this.records.push(r);
        if (this.records.length > V2_MAX_FRAME_RECORDS) {
          this.records = []; this.inFrame = false; this.droppedPartials++;
          break;
        }
        if (r.type === V2_REC.FRAME_END) { frame = this.records; this.records = []; this.inFrame = false; }
      }
    }
    return frame;
  }
}

// ---------------------------------------------------------------------------
// V2FrameQueue — the consumer-side backpressure. Cap 3 complete frames;
// push() drops the oldest past the cap; takeLatest() returns the newest and
// drops everything older (latest-frame-wins); clear() drains (resize hook).
// ---------------------------------------------------------------------------
/** True when any pass of a decoded frame renders to a colour attachment. */
export function v2HasColourPass(frame) {
  return frame.passes.some(p => p.attachments.some(a => !a.isDepth));
}
/**
 * Which submission of a present batch presents: the last one that renders to a
 * colour attachment. Trailing copy/blit/clear-only submissions still execute
 * but cannot present (they have no colour target and would resize the canvas
 * to their 1x1 placeholder frame).
 */
export function v2PresentIndex(submissions) {
  let at = submissions.length - 1;
  while (at > 0 && !v2HasColourPass(submissions[at])) at--;
  return at;
}

export class V2PresentAssembler {
  constructor() { this.pending = []; }
  push(frame, flags = 0) {
    this.pending.push(frame);
    if (flags & 1) return null; // more queue submissions before this presentation
    const submissions = this.pending;
    this.pending = [];
    if (submissions.length === 1) return frame;
    // Keep submission-local resource snapshots: a buffer slice can legally
    // be updated between submissions after its guest fence has signalled.
    return { ...frame, submissions, passes: submissions.flatMap(f => f.passes) };
  }
}

export class V2FrameQueue {
  constructor(cap = V2_QUEUE_CAP) {
    this.cap = cap; this.q = []; this.dropped = 0;
    this.imageData = new Map(); this.bufferData = new Map();
  }
  get size() { return this.q.length; }
  push(frame) {
    // Payloads are deltas even when a frame's draw commands are discarded.
    // Retain the latest data so a subsequent hash-only reference can upload it.
    for (const submission of frame.submissions || [frame]) {
      for (const [id, image] of submission.images || []) {
        if (image.pixels) this.imageData.set(id, image);
        else {
          const previous = this.imageData.get(id);
          if (previous?.hash === image.hash) image.pixels = previous.pixels;
        }
      }
      for (const [id, bytes] of submission.buffers || []) {
        const hash = submission.bufHashes?.get(id);
        if (bytes) this.bufferData.set(id, { bytes, hash });
        else {
          const previous = this.bufferData.get(id);
          if (previous?.hash === hash) submission.buffers.set(id, previous.bytes);
        }
      }
    }
    this.q.push(frame);
    while (this.q.length > this.cap) { this.q.shift(); this.dropped++; }
  }
  takeLatest() {
    if (!this.q.length) return null;
    const f = this.q[this.q.length - 1];
    this.q.length = 0;
    return f;
  }
  clear() { this.q.length = 0; }
}

// ---------------------------------------------------------------------------
// Format maps. VkFormat numbers are the real ones (source/vulkan/vk64_guest.h).
// An unknown format is unsupported; never reinterpret its bytes as RGBA8.
// ---------------------------------------------------------------------------
const V2_TEX_FORMAT = {
  2: 'rgba8unorm', 3: 'rgba8unorm', 5: 'rgba8unorm', 7: 'rgba8unorm', 8: 'rgba8unorm',
  37: 'rgba8unorm', 38: 'rgba8snorm', 43: 'rgba8unorm-srgb', 44: 'bgra8unorm', 50: 'bgra8unorm-srgb',
  70: 'r16float', 76: 'r16float', 77: 'rg16float', 83: 'rg16float',
  91: 'rgba16float', 97: 'rgba16float',
  9: 'r8unorm', 16: 'rg8unorm', 41: 'rgba8uint', 64: 'rgb10a2unorm', 122: 'rg11b10ufloat',
  98: 'r32uint', 99: 'r32sint', 100: 'r32float', 103: 'rg32float', 109: 'rgba32float',
  131: 'bc1-rgba-unorm', 132: 'bc1-rgba-unorm-srgb',
  133: 'bc1-rgba-unorm', 134: 'bc1-rgba-unorm-srgb',
  135: 'bc2-rgba-unorm', 136: 'bc2-rgba-unorm-srgb',
  137: 'bc3-rgba-unorm', 138: 'bc3-rgba-unorm-srgb',
  139: 'bc4-r-unorm', 140: 'bc4-r-snorm', 141: 'bc5-rg-unorm', 142: 'bc5-rg-snorm',
  143: 'bc6h-rgb-ufloat', 144: 'bc6h-rgb-float', 145: 'bc7-rgba-unorm', 146: 'bc7-rgba-unorm-srgb',
  124: 'depth16unorm', 125: 'depth24plus', 126: 'depth32float', 129: 'depth24plus-stencil8', 130: 'depth24plus-stencil8',
};
const V2_NORM16_FORMATS = new Set([70, 77]);
const V2_PACKED_RGBA8_FORMATS = new Set([2, 3, 5, 7, 8]);
const V2_UNKNOWN_FORMATS_REPORTED = new Set();
// WebGPU has no core normalized RGBA16 texture format. Preserve its range
// in a filterable RGBA16 float texture instead of interpreting bytes as RGBA8.
export function rgba16UnormToHalf(bytes) {
  return unorm16ToHalf(bytes);
}
export function unorm16ToHalf(bytes) {
  const out = new Uint8Array(bytes.length);
  const src = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const dst = new DataView(out.buffer);
  for (let offset = 0; offset + 1 < bytes.length; offset += 2) {
    const value = src.getUint16(offset, true) / 65535;
    let bits = 0;
    if (value > 0 && value < 2 ** -14) bits = Math.round(value * 2 ** 24);
    else if (value > 0) {
      let exponent = Math.floor(Math.log2(value));
      let fraction = Math.round(value / 2 ** exponent * 1024);
      if (fraction === 2048) { exponent++; fraction = 1024; }
      bits = ((exponent + 15) << 10) | (fraction - 1024);
    }
    dst.setUint16(offset, bits, true);
  }
  return out;
}
function hasTextureFeature(features, name) { return !!features?.has?.(name); }
export function v2TextureFormat(vkFormat, counters, features) {
  let f = V2_TEX_FORMAT[vkFormat];
  if (vkFormat >= 131 && vkFormat <= 146 && !hasTextureFeature(features, 'texture-compression-bc')) f = null;
  if (V2_NORM16_FORMATS.has(vkFormat)) {
    const norm16 = hasTextureFeature(features, 'texture-formats-tier1') || hasTextureFeature(features, 'norm16');
    f = vkFormat === 70 ? (norm16 ? 'r16unorm' : 'r16float') : (norm16 ? 'rg16unorm' : 'rg16float');
  } else if (vkFormat === 130 && hasTextureFeature(features, 'depth32float-stencil8')) {
    f = 'depth32float-stencil8';
  }
  if (!f) {
    if (counters) counters.unknownFormats = (counters.unknownFormats || 0) + 1;
    if (!V2_UNKNOWN_FORMATS_REPORTED.has(vkFormat)) {
      V2_UNKNOWN_FORMATS_REPORTED.add(vkFormat);
      console.warn(`[vkwebgpu] unsupported VkFormat ${vkFormat}; skipping image`);
    }
    return null;
  }
  return f;
}
function scalePacked(value, bits) { return Math.round(value * 255 / (2 ** bits - 1)); }
export function packedUnormToRgba8(vkFormat, bytes) {
  if (!V2_PACKED_RGBA8_FORMATS.has(vkFormat)) return bytes;
  const src = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const out = new Uint8Array(Math.floor(bytes.length / 2) * 4);
  for (let pixel = 0; pixel < Math.floor(bytes.length / 2); pixel++) {
    const value = src.getUint16(pixel * 2, true);
    let r, g, b, a = 255;
    switch (vkFormat) {
      case 2: // R4G4B4A4: R is the high nibble, A the low nibble.
        r = scalePacked(value >> 12, 4); g = scalePacked((value >> 8) & 15, 4);
        b = scalePacked((value >> 4) & 15, 4); a = scalePacked(value & 15, 4); break;
      case 3: // B4G4R4A4: B is the high nibble, A the low nibble.
        b = scalePacked(value >> 12, 4); g = scalePacked((value >> 8) & 15, 4);
        r = scalePacked((value >> 4) & 15, 4); a = scalePacked(value & 15, 4); break;
      case 5: // B5G6R5: B high, R low.
        b = scalePacked(value >> 11, 5); g = scalePacked((value >> 5) & 63, 6);
        r = scalePacked(value & 31, 5); break;
      case 7: // B5G5R5A1: B high, A low.
        b = scalePacked(value >> 11, 5); g = scalePacked((value >> 6) & 31, 5);
        r = scalePacked((value >> 1) & 31, 5); a = (value & 1) ? 255 : 0; break;
      case 8: // A1R5G5B5: A high, B low.
        a = (value >> 15) ? 255 : 0; r = scalePacked((value >> 10) & 31, 5);
        g = scalePacked((value >> 5) & 31, 5); b = scalePacked(value & 31, 5); break;
    }
    const offset = pixel * 4;
    out[offset] = r; out[offset + 1] = g; out[offset + 2] = b; out[offset + 3] = a;
  }
  return out;
}
export function v2TexturePixels(vkFormat, bytes, features) {
  const directNorm16 = hasTextureFeature(features, 'texture-formats-tier1') || hasTextureFeature(features, 'norm16');
  if (vkFormat === 91) return rgba16UnormToHalf(bytes);
  if (V2_NORM16_FORMATS.has(vkFormat) && !directNorm16) return unorm16ToHalf(bytes);
  return packedUnormToRgba8(vkFormat, bytes);
}
const V2_TEXTURE_UPLOAD_BPP = Object.freeze({
  r8unorm:1, r8snorm:1, r8uint:1, r8sint:1,
  rg8unorm:2, rg8snorm:2, rg8uint:2, rg8sint:2,
  rgba8unorm:4, 'rgba8unorm-srgb':4, rgba8snorm:4, rgba8uint:4, rgba8sint:4,
  bgra8unorm:4, 'bgra8unorm-srgb':4, 'rgb10a2unorm':4,
  r16unorm:2, r16float:2, rg16unorm:4, rg16float:4, rgba16float:8,
  rg32float:8, rgba32float:16, r32float:4, r32uint:4, r32sint:4,
  depth16unorm:2, depth24plus:4, depth32float:4, 'depth24plus-stencil8':4, 'depth32float-stencil8':8,
});
/** Return byte offsets and writeTexture extents for the bridge's layer-major mip-chain image layout. */
export function v2TextureMipUploadPlan(img, format, dimension = '2d') {
  const w = Math.max(1, img.w || 1), h = dimension === '1d' ? 1 : Math.max(1, img.h || 1);
  const d = dimension === '3d' ? Math.max(1, img.d || 1) : 1;
  const layers = dimension === '3d' ? 1 : Math.max(dimension === 'cube' ? 6 : 1, img.layers || 1);
  const mips = Math.max(1, img.mips || 1), compressed = format.startsWith('bc');
  const blockBytes = /^bc[14]-/.test(format) ? 8 : 16;
  const bpp = V2_TEXTURE_UPLOAD_BPP[format] || 4;
  const plan = [];
  let offset = 0;
  for (let layer = 0; layer < layers; layer++) {
    for (let mip = 0; mip < mips; mip++) {
      const mw = Math.max(1, w >> mip), mh = Math.max(1, h >> mip);
      const md = dimension === '3d' ? Math.max(1, d >> mip) : 1;
      const bytesPerRow = compressed ? Math.ceil(mw / 4) * blockBytes : mw * bpp;
      const rowsPerImage = compressed ? Math.ceil(mh / 4) : mh;
      const byteLength = bytesPerRow * rowsPerImage * md;
      // Compressed copies operate on whole blocks. The physical mip extent is
      // rounded up to a complete block, even when the logical mip is 1–3 texels.
      const copyWidth = compressed ? Math.ceil(mw / 4) * 4 : mw;
      const copyHeight = compressed ? Math.ceil(mh / 4) * 4 : mh;
      plan.push({ layer, mip, offset, byteLength, bytesPerRow, rowsPerImage, extent: [copyWidth, copyHeight, md] });
      offset += byteLength;
    }
  }
  return plan;
}
/** Convert a Vulkan 2D color blit region to a render viewport and normalized source rectangle. */
export function v2ImageBlitRegion(op, srcImage, dstImage) {
  const srcW = Math.max(1, srcImage.w >> op.srcMip), srcH = Math.max(1, srcImage.h >> op.srcMip);
  const dstW = Math.max(1, dstImage.w >> op.dstMip), dstH = Math.max(1, dstImage.h >> op.dstMip);
  const [s0, s1] = op.srcOffsets, [d0, d1] = op.dstOffsets;
  if (Math.abs(s1[2] - s0[2]) !== 1 || Math.abs(d1[2] - d0[2]) !== 1 ||
      [...s0, ...s1].some((v, axis) => v < 0 || v > (axis % 3 === 0 ? srcW : axis % 3 === 1 ? srcH : Math.max(1, srcImage.d || 1))) ||
      [...d0, ...d1].some((v, axis) => v < 0 || v > (axis % 3 === 0 ? dstW : axis % 3 === 1 ? dstH : Math.max(1, dstImage.d || 1))))
    return null;
  const x0 = Math.min(d0[0], d1[0]), y0 = Math.min(d0[1], d1[1]);
  const width = Math.abs(d1[0] - d0[0]), height = Math.abs(d1[1] - d0[1]);
  const sx0 = d1[0] < d0[0] ? s1[0] : s0[0], sy0 = d1[1] < d0[1] ? s1[1] : s0[1];
  const sx1 = d1[0] < d0[0] ? s0[0] : s1[0], sy1 = d1[1] < d0[1] ? s0[1] : s1[1];
  return { viewport: [x0, y0, width, height], sourceRect: [sx0 / srcW, sy0 / srcH, (sx1 - sx0) / srcW, (sy1 - sy0) / srcH], srcW, srcH, dstW, dstH };
}
/**
 * @param {number} vkFormat
 * @param {Uint8Array} bytes
 * @param {any} [features]
 * @returns {{uploads:Array<{aspect:GPUTextureAspect,bytesPerTexel:number,pixels:Uint8Array<ArrayBuffer>}>,skippedDepth:boolean}}
 */
export function v2DepthStencilUploads(vkFormat, bytes, features) {
  const texels = vkFormat === 130 ? Math.floor(bytes.length / 8)
    : vkFormat === 129 || vkFormat === 125 ? Math.floor(bytes.length / 4)
      : vkFormat === 124 ? Math.floor(bytes.length / 2)
        : vkFormat === 126 ? Math.floor(bytes.length / 4) : 0;
  if (vkFormat === 124 || vkFormat === 126) {
    const bytesPerTexel = vkFormat === 124 ? 2 : 4;
    return { uploads: [{ aspect: /** @type {GPUTextureAspect} */ ('depth-only'), bytesPerTexel, pixels: new Uint8Array(bytes.subarray(0, texels * bytesPerTexel)) }], skippedDepth: false };
  }
  if (vkFormat === 125) return { uploads: [], skippedDepth: texels > 0 };
  if (vkFormat === 129 || vkFormat === 130) {
    const sourceStride = vkFormat === 129 ? 4 : 8;
    const stencil = new Uint8Array(texels);
    const source = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    for (let texel = 0; texel < texels; texel++) stencil[texel] = source.getUint8(texel * sourceStride + (vkFormat === 129 ? 3 : 4));
    const uploads = [{ aspect: /** @type {GPUTextureAspect} */ ('stencil-only'), bytesPerTexel: 1, pixels: stencil }];
    const supportsDepth32Stencil8 = hasTextureFeature(features, 'depth32float-stencil8');
    if (vkFormat === 130 && supportsDepth32Stencil8) {
      const depth = new Uint8Array(texels * 4);
      for (let texel = 0; texel < texels; texel++)
        depth.set(bytes.subarray(texel * sourceStride, texel * sourceStride + 4), texel * 4);
      uploads.unshift({ aspect: /** @type {GPUTextureAspect} */ ('depth-only'), bytesPerTexel: 4, pixels: depth });
      return { uploads, skippedDepth: false };
    }
    return { uploads, skippedDepth: texels > 0 };
  }
  return { uploads: [], skippedDepth: false };
}
const V2_VERT_FORMAT = {
  100: 'float32', 103: 'float32x2', 106: 'float32x3', 109: 'float32x4',
  98: 'uint32', 99: 'sint32',
  101: 'uint32x2', 102: 'sint32x2', 104: 'uint32x3', 105: 'sint32x3',
  107: 'uint32x4', 108: 'sint32x4',
  37: 'unorm8x4', 38: 'snorm8x4', 41: 'uint8x4', 42: 'sint8x4',
  16: 'unorm8x2', 17: 'snorm8x2', 20: 'uint8x2', 21: 'sint8x2',
  64: 'unorm10-10-10-2',
  39: 'float32x4', 40: 'float32x4', 18: 'float32x2', 19: 'float32x2',
  23: 'float32x3', 24: 'float32x3', 25: 'float32x3', 26: 'float32x3',
  27: 'uint32x3', 28: 'sint32x3',
  83: 'float16x2', 97: 'float16x4', 77: 'unorm16x2', 78: 'snorm16x2',
  79: 'float32x2', 80: 'float32x2',
  81: 'uint16x2', 82: 'sint16x2', 91: 'unorm16x4', 92: 'snorm16x4',
  93: 'float32x4', 94: 'float32x4', 95: 'uint16x4', 96: 'sint16x4',
  84: 'float32x3', 85: 'float32x3', 86: 'float32x3', 87: 'float32x3',
  88: 'uint32x3', 89: 'sint32x3', 90: 'float32x3',
  // 44 = VK_FORMAT_B8G8R8A8_UNORM. WebGPU has no BGRA vertex format; map to
  // unorm8x4 and swizzle R/B in the uploaded bytes (swizzleBgraVb), so the
  // shader sees Vulkan's component order.
  44: 'unorm8x4',
};
// Byte size of each V2_VERT_FORMAT entry, for minimum-stride computation.
export const V2_VERT_FORMAT_SIZE = {
  'float32': 4, 'float32x2': 8, 'float32x3': 12, 'float32x4': 16,
  'unorm8x2': 2, 'unorm8x4': 4, 'unorm10-10-10-2': 4,
  'snorm8x2': 2, 'snorm8x4': 4, 'uint8x2': 2, 'uint8x4': 4, 'sint8x2': 2, 'sint8x4': 4,
  'float16x2': 4, 'float16x4': 8,
  'unorm16x2': 4, 'unorm16x4': 8, 'snorm16x2': 4, 'snorm16x4': 8,
  'uint16x2': 4, 'uint16x4': 8, 'sint16x2': 4, 'sint16x4': 8,
  'uint32': 4, 'sint32': 4, 'uint32x2': 8, 'sint32x2': 8,
  'uint32x3': 12, 'sint32x3': 12, 'uint32x4': 16, 'sint32x4': 16,
};
export const V2_SCALED_VERTEX = {
  18: { components: 2, bits: 8, signed: false }, // R8G8_USCALED
  19: { components: 2, bits: 8, signed: true }, // R8G8_SSCALED
  23: { components: 3, bits: 8, signed: false, normalized: true }, // R8G8B8_UNORM
  24: { components: 3, bits: 8, signed: true, normalized: true }, // R8G8B8_SNORM
  25: { components: 3, bits: 8, signed: false }, // R8G8B8_USCALED
  26: { components: 3, bits: 8, signed: true }, // R8G8B8_SSCALED
  27: { components: 3, bits: 8, signed: false, output: 'uint32' }, // R8G8B8_UINT
  28: { components: 3, bits: 8, signed: true, output: 'sint32' }, // R8G8B8_SINT
  39: { components: 4, bits: 8, signed: false }, // R8G8B8A8_USCALED
  40: { components: 4, bits: 8, signed: true }, // R8G8B8A8_SSCALED
  80: { components: 2, bits: 16, signed: true }, // R16G16_SSCALED
  79: { components: 2, bits: 16, signed: false }, // R16G16_USCALED
  93: { components: 4, bits: 16, signed: false }, // R16G16B16A16_USCALED
  94: { components: 4, bits: 16, signed: true }, // R16G16B16A16_SSCALED
  84: { components: 3, bits: 16, signed: false, normalized: true }, // R16G16B16_UNORM
  85: { components: 3, bits: 16, signed: true, normalized: true }, // R16G16B16_SNORM
  86: { components: 3, bits: 16, signed: false }, // R16G16B16_USCALED
  87: { components: 3, bits: 16, signed: true }, // R16G16B16_SSCALED
  88: { components: 3, bits: 16, signed: false, output: 'uint32' }, // R16G16B16_UINT
  89: { components: 3, bits: 16, signed: true, output: 'sint32' }, // R16G16B16_SINT
  90: { components: 3, bits: 16, signed: true, half: true }, // R16G16B16_SFLOAT
};
export function repackScaledVb(bytes, inputStride, outputStride, attributes) {
  const count = Math.floor(bytes.length / inputStride);
  const out = new Uint8Array(count * outputStride);
  const src = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const dst = new DataView(out.buffer);
  for (let vertex = 0; vertex < count; vertex++) {
    const from = vertex * inputStride, to = vertex * outputStride;
    out.set(bytes.subarray(from, from + inputStride), to);
    for (const a of attributes) for (let component = 0; component < a.components; component++) {
      const offset = from + a.srcOffset + component * a.bits / 8;
      const value = a.half
        ? halfToFloat(src.getUint16(offset, true))
        : a.bits === 8
          ? (a.signed ? src.getInt8(offset) : src.getUint8(offset))
          : (a.signed ? src.getInt16(offset, true) : src.getUint16(offset, true));
      const converted = a.normalized
        ? (a.signed ? Math.max(-1, value / (2 ** (a.bits - 1) - 1)) : value / (2 ** a.bits - 1))
        : value;
      const dstOffset = to + a.dstOffset + component * 4;
      if (a.output === 'uint32') dst.setUint32(dstOffset, converted, true);
      else if (a.output === 'sint32') dst.setInt32(dstOffset, converted, true);
      else dst.setFloat32(dstOffset, converted, true);
    }
  }
  return out;
}
function halfToFloat(bits) {
  const sign = bits & 0x8000 ? -1 : 1;
  const exponent = (bits >> 10) & 0x1f;
  const fraction = bits & 0x03ff;
  if (exponent === 0) return sign * fraction * 2 ** -24;
  if (exponent === 0x1f) return fraction ? NaN : sign * Infinity;
  return sign * (1 + fraction / 1024) * 2 ** (exponent - 15);
}
export function v2VertexFormat(vkFormat) { return V2_VERT_FORMAT[vkFormat] || null; }
export const V2_TOPOLOGY = { 0: 'point-list', 1: 'line-list', 2: 'line-strip', 3: 'triangle-list', 4: 'triangle-strip', 5: 'triangle-list' };
export function v2TriangleFanIndices(vertexCount, firstVertex = 0) {
  const out = [];
  for (let i = 1; i + 1 < vertexCount; i++) out.push(firstVertex, firstVertex + i, firstVertex + i + 1);
  return new Uint32Array(out);
}
export function v2IndexedTriangleFanIndices(bytes, indexType, firstIndex, indexCount, primitiveRestartEnable = false) {
  const width = indexType ? 4 : 2, view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const restart = indexType ? 0xffffffff : 0xffff, out = [], segment = [];
  const append = () => {
    for (let i = 1; i + 1 < segment.length; i++) out.push(segment[0], segment[i], segment[i + 1]);
    segment.length = 0;
  };
  const end = Math.min(firstIndex + indexCount, Math.floor(bytes.byteLength / width));
  for (let i = firstIndex; i < end; i++) {
    const value = indexType ? view.getUint32(i * width, true) : view.getUint16(i * width, true);
    if (primitiveRestartEnable && value === restart) append(); else segment.push(value);
  }
  append();
  return new Uint32Array(out);
}

// ---------------------------------------------------------------------------
// decodeV2Frame — validates a sealed record list into the renderable frame
// model. Resources (shaders, pipelines, buffers, images, samplers) are
// frame-level maps; state ops nest inside the render-pass graph
// (RP_BEGIN..RP_END). Throws with a reason on any structural problem.
// ---------------------------------------------------------------------------
export function decodeV2Frame(records, counters = {}) {
  void counters;
  if (!Array.isArray(records) || !records.length || records[0].type !== V2_REC.FRAME_BEGIN)
    throw new Error('vk64: v2 frame does not start with FRAME_BEGIN');
  const last = records[records.length - 1];
  if (last.type !== V2_REC.FRAME_END) throw new Error('vk64: v2 frame does not end with FRAME_END');

  const h0 = new V2Reader(records[0].payload);
  const frameNo = h0.u32(), width = h0.u32(), height = h0.u32(), flags = h0.u32();
  h0.done('FRAME_BEGIN');
  if (flags & 1) throw new Error('vk64: v2 frame overflowed the guest command limit');
  if (width < 1 || height < 1 || width > MAX_DIMENSION || height > MAX_DIMENSION)
    throw new Error('vk64: v2 frame size out of range');
  const h1 = new V2Reader(last.payload);
  const endNo = h1.u32(); h1.done('FRAME_END');
  if (endNo !== frameNo) throw new Error('vk64: v2 FRAME_BEGIN/FRAME_END number mismatch');

  const frame = {
    frameNo, width, height, passes: [],
    shaders: new Map(), pipelines: new Map(), buffers: new Map(),
    images: new Map(), views: new Map(), viewRanges: new Map(), viewFormats: new Map(), samplers: new Map(),
    bufHashes: new Map(), readbacks: [],
  };
  const implicitPass = () => ({
    rpId: '0', fbId: '0', w: width, h: height, attachments: [],
    clearColor: [0, 0, 0, 1], clearDepth: 1, clearStencil: 0, ops: [],
  });
  let pass = null;
  const needPass = () => pass || (pass = frame.passes[frame.passes.push(implicitPass()) - 1]);
  let draws = 0;

  for (let i = 1; i < records.length - 1; i++) {
    const { type, payload } = records[i];
    const r = new V2Reader(payload);
    switch (type) {
      case V2_REC.RP_BEGIN: {
        const rpId = v2Hex64(r.u64()), fbId = v2Hex64(r.u64());
        const w = r.u32(), h = r.u32(), natt = r.u32();
        if (natt > 8) throw new Error('vk64: v2 RP_BEGIN attachment count out of range');
        /** @type {{format:number, loadOp:number, storeOp:number, samples:number, isDepth:number, viewId?:string, imageId?:string, stencilLoadOp?:number, stencilStoreOp?:number, aspectMask?:number}[]} */
        const attachments = [];
        for (let a = 0; a < natt; a++) {
          attachments.push({ format: r.u32(), loadOp: r.u32(), storeOp: r.u32(), samples: r.u32(), isDepth: r.u32() });
        }
        const clearColor = [r.f32(), r.f32(), r.f32(), r.f32()];
        const clearDepth = r.f32(), clearStencil = r.u32();
        // Attachment identity tail: one {viewId, imageId} u64 pair per
        // attachment, in attachment order. The image identity links render
        // attachments to the sampled-image aliases in frame.views.
        for (let a = 0; a < natt; a++) {
          attachments[a].viewId = v2Hex64(r.u64());
          attachments[a].imageId = v2Hex64(r.u64());
        }
        if (r.left) {
          if (r.u32() !== 0x31505453 || r.u32() !== natt)
            throw new Error('vk64: v2 RP_BEGIN invalid stencil tail');
          for (const attachment of attachments) {
            attachment.stencilLoadOp = r.u32(); attachment.stencilStoreOp = r.u32(); attachment.aspectMask = r.u32();
          }
        }
        // CLR1 tail: per-attachment colour clear values (multiple render targets).
        if (r.left) {
          if (r.u32() !== 0x31524c43 || r.u32() !== natt) throw new Error('vk64: v2 RP_BEGIN invalid clear tail');
          for (const attachment of attachments) attachment.clearColor = [r.f32(), r.f32(), r.f32(), r.f32()];
        }
        r.done('RP_BEGIN');
        pass = { rpId, fbId, w, h, attachments, clearColor, clearDepth, clearStencil, ops: [] };
        frame.passes.push(pass);
        break;
      }
      case V2_REC.RP_END: r.done('RP_END'); pass = null; break;
      case V2_REC.SHADER: {
        const stage = r.u8(); r.skip(3);
        const hash = v2Hex64(r.u64());
        const codeLen = r.u32();
        const code = r.bytes(codeLen); r.done('SHADER');
        if (stage > 1) throw new Error('vk64: v2 SHADER bad stage');
        if (!frame.shaders.has(hash)) frame.shaders.set(hash, { stage, code: code.slice() });
        break;
      }
      case V2_REC.PIPELINE: {
        const pipeId = v2Hex64(r.u64());
        const vsHash = v2Hex64(r.u64()), fsHash = v2Hex64(r.u64());
        let topology = r.u32(); const cull = r.u32(), front = r.u32();
        const depthTest = r.u32(), depthWrite = r.u32(), depthOp = r.u32(), blend = r.u32();
        const nVb = r.u32(), nVa = r.u32();
        if (nVb > 4 || nVa > 16) throw new Error('vk64: v2 PIPELINE vertex layout out of range');
        const vb = [], va = [];
        for (let k = 0; k < nVb; k++) vb.push({ binding: r.u32(), stride: r.u32(), inputRate: r.u32() });
        for (let k = 0; k < nVa; k++) va.push({ location: r.u32(), binding: r.u32(), format: r.u32(), offset: r.u32() });
        // Append-only specialization tail (2026-10-06): per-stage list of
        // {constantID, size, value bytes}. Absent in frames from older bridges.
        const readSpec = () => {
          const n = r.u32();
          if (n > 16) throw new Error('vk64: v2 PIPELINE spec count out of range');
          const entries = [];
          for (let i = 0; i < n; i++) {
            const constantID = r.u32(), size = r.u32();
            if (size !== 1 && size !== 2 && size !== 4 && size !== 8)
              throw new Error('vk64: v2 PIPELINE spec size out of range');
            const value = r.bytes(size);
            entries.push({ constantID, size, value });
          }
          return entries;
        };
        let vsSpec = [], fsSpec = [];
        if (r.p < r.dv.byteLength) {
          vsSpec = readSpec();
          fsSpec = readSpec();
        }
        let colorBlend, blendConstants, stencil, blendConstantsDynamic, depthBias;
        let primitiveRestartEnable = false, cullDynamic = false, frontDynamic = false, dynMask = 0, rasterizerDiscard = 0;
        while (r.left) {
          const tag = r.u32();
          if (tag === 0x314e5453) {
            const enabled = r.u32(), dynamicMask = r.u32(), dynamicBlend = r.u32();
            if (enabled > 1 || dynamicMask > 7 || dynamicBlend > 1) throw new Error('vk64: v2 PIPELINE invalid stencil flags');
            const face = () => ({failOp:r.u32(), passOp:r.u32(), depthFailOp:r.u32(), compareOp:r.u32(), compareMask:r.u32(), writeMask:r.u32(), reference:r.u32()});
            stencil = {enabled, dynamicMask, front:face(), back:face()};
            blendConstantsDynamic = !!dynamicBlend;
            continue;
          }
          if (tag === 0x31534244) {
            const depthBiasEnable = r.u32(), depthClampEnable = r.u32(), depthBiasDynamic = r.u32();
            const depthBiasConstantFactor = r.f32(), depthBiasClamp = r.f32(), depthBiasSlopeFactor = r.f32();
            if (depthBiasEnable > 1 || depthClampEnable > 1 || depthBiasDynamic > 1 ||
                !Number.isFinite(depthBiasConstantFactor) || !Number.isFinite(depthBiasClamp) || !Number.isFinite(depthBiasSlopeFactor))
              throw new Error('vk64: v2 PIPELINE invalid depth-bias state');
            depthBias = {depthBiasEnable, depthClampEnable, depthBiasDynamic: !!depthBiasDynamic,
              depthBiasConstantFactor, depthBiasClamp, depthBiasSlopeFactor};
            continue;
          }
          if (tag === 0x314e4146) {
            const restart = r.u32();
            if (restart > 1) throw new Error('vk64: v2 PIPELINE invalid primitive restart flag');
            primitiveRestartEnable = !!restart;
            continue;
          }
          if (tag === 0x31545352) {
            const cd = r.u32(), fd = r.u32();
            if (cd > 1 || fd > 1) throw new Error('vk64: v2 PIPELINE invalid raster dynamic flags');
            cullDynamic = !!cd; frontDynamic = !!fd;
            continue;
          }
          if (tag === 0x314e5944) {
            const mask = r.u32(), discard = r.u32();
            if (mask >= DYN_MASK_LIMIT || discard > 1) throw new Error('vk64: v2 PIPELINE invalid extended dynamic state');
            dynMask = mask; rasterizerDiscard = discard;
            continue;
          }
          if (tag !== 0x31444e42) throw new Error('vk64: v2 PIPELINE unknown extension tail');
          const count = r.u32();
          if (count > 4) throw new Error('vk64: v2 PIPELINE blend count out of range');
          colorBlend = [];
          for (let i = 0; i < count; ++i) colorBlend.push({
            enable: r.u32(), srcColor: r.u32(), dstColor: r.u32(), colorOp: r.u32(),
            srcAlpha: r.u32(), dstAlpha: r.u32(), alphaOp: r.u32(), writeMask: r.u32(),
          });
          blendConstants = [r.f32(), r.f32(), r.f32(), r.f32()];
        }
        r.done('PIPELINE');
        // A topology WebGPU cannot draw is never replayed as triangle-list: the bridge rejects
        // such pipelines. A dynamic-topology pipeline may carry any static value (ignored).
        if (!(topology in V2_TOPOLOGY)) {
          if (!(dynMask & (1 << DYN_ID.topology))) throw new Error('vk64: v2 PIPELINE unsupported topology ' + topology);
          topology = 3;
        }
        const pipe = { pipeId, vsHash, fsHash, topology, cull, front, depthTest, depthWrite, depthOp, blend, vb, va, vsSpec, fsSpec, primitiveRestartEnable, ...(cullDynamic || frontDynamic ? { cullDynamic, frontDynamic } : {}), ...(dynMask ? { dynMask } : {}), ...(rasterizerDiscard ? { rasterizerDiscard: 1 } : {}), ...(colorBlend ? { colorBlend, blendConstants } : {}), ...(stencil ? {stencil, blendConstantsDynamic} : {}), ...(depthBias || {}) };
        frame.pipelines.set(pipeId, pipe);
        needPass().ops.push({ op: 'bindPipe', pipe });
        break;
      }
      case V2_REC.BIND_SETS: {
        const firstSet = r.u8(), setCount = r.u8(); r.skip(2);
        if (firstSet + setCount > V2_MAX_SETS) throw new Error('vk64: v2 BIND_SETS out of range');
        const setIds = [];
        for (let k = 0; k < setCount; k++) setIds.push(v2Hex64(r.u64()));
        r.done('BIND_SETS');
        needPass().ops.push({ op: 'bindSets', firstSet, setIds });
        break;
      }
      case V2_REC.DESC_SET: {
        // setIndex u8, reserved u8, nBind u16: one wire entry per written
        // (binding, arrayElement); descriptor arrays and >8-binding layouts are
        // legal (DXVK/vkd3d use both).
        const setIndex = r.u8(); r.skip(1); const nBind = r.u16();
        const layoutId = v2Hex64(r.u64());
        if (setIndex >= V2_MAX_SETS) throw new Error('vk64: v2 DESC_SET out of range');
        const bindings = [];
        for (let k = 0; k < nBind; k++) {
          bindings.push({
            binding: r.u32(), type: r.u32(), dstArrayElement: r.u32(), count: r.u32(),
            kind: r.u8(), _pad1: r.u8(), _pad2: r.u16(),
            obj: v2Hex64(r.u64()), range: v2Hex64(r.u64()),
            bufOff: r.u64().toString(),
          });
        }
        r.done('DESC_SET');
        needPass().ops.push({ op: 'descSet', setIndex, layoutId, bindings });
        break;
      }
      case V2_REC.BUFFER_DATA: {
        const bufId = v2Hex64(r.u64());
        const memOff = r.u64().toString();
        const hash = v2Hex64(r.u64());
        const len = r.u32();
        if (len > V2_MAX_RECORD_BYTES) throw new Error('vk64: v2 BUFFER_DATA len out of range');
        const bytes = len ? r.bytes(len) : null; r.done('BUFFER_DATA');
        // One VkBuffer may back several bindings at different offsets; key by
        // (id, offset). Keep the bare-id key (first record) for VB/IB lookups.
        // Byte-less record (len 0): wasm-side dedup, content unchanged since
        // the last full record. Store the hash; the GPU resource resolves
        // from the persistent cache.
        const key = bufId + ':' + memOff;
        if (!frame.buffers.has(key)) {
          frame.buffers.set(key, bytes ? bytes.slice() : null);
          frame.bufHashes.set(key, hash); // always store bridge hash
        }
        if (!frame.buffers.has(bufId)) {
          frame.buffers.set(bufId, bytes ? bytes.slice() : null);
          frame.bufHashes.set(bufId, hash); // always store bridge hash
        }
        break;
      }
      case V2_REC.IMAGE_DATA: {
        const viewId = v2Hex64(r.u64()), imageId = v2Hex64(r.u64());
        const w = r.u32(), h = r.u32(), format = r.u32(), hash = v2Hex64(r.u64()), pixLen = r.u32();
        if (w > MAX_DIMENSION || h > MAX_DIMENSION || pixLen > V2_MAX_RECORD_BYTES)
          throw new Error('vk64: v2 IMAGE_DATA out of range');
        const pixels = pixLen ? r.bytes(pixLen) : null;
        const d = r.left ? r.u32() : 1, layers = r.left ? r.u32() : 1, mips = r.left ? r.u32() : 1;
        if (d < 1 || d > MAX_DIMENSION || layers < 1 || layers > MAX_DIMENSION || mips < 1 || mips > 32)
          throw new Error('vk64: v2 IMAGE_DATA depth/layers/mips out of range');
        // Optional view-range tail: the subresource range and type the view was created with.
        const viewRange = r.left >= 20 ? { baseMip: r.u32(), levelCount: r.u32(), baseLayer: r.u32(), layerCount: r.u32(), viewType: r.left ? r.u32() : 1 } : null;
        r.done('IMAGE_DATA');
        frame.views.set(viewId, imageId);
        if (viewRange) frame.viewRanges.set(viewId, viewRange);
        frame.viewFormats.set(viewId, format);
        // Pixel-less record (pixLen 0): wasm-side dedup, content unchanged
        // since the last full record. The view->image link is still recorded;
        // the texture resolves from the persistent cache via the content hash.
        if (!frame.images.has(imageId))
          frame.images.set(imageId, { w, h, d, layers, mips, format, hash, pixels: pixLen ? pixels.slice() : null });
        break;
      }
      case V2_REC.SAMPLER: {
        const id = v2Hex64(r.u64());
        const mag = r.u32(), min = r.u32(), mipmap = r.u32(), addrU = r.u32(), addrV = r.u32();
        const maxAniso = r.f32();
        // The original record ended at maxAniso. Defaults preserve old captures;
        // old records used maxAniso alone to request anisotropic filtering.
        let addrW = 2, mipLodBias = 0, anisotropyEnable = maxAniso > 1;
        let compareEnable = false, compareOp = 7, minLod = 0, maxLod = 32, borderColor = 0;
        if (r.left) {
          if (r.u32() !== 0x314d4153) throw new Error('vk64: v2 SAMPLER unknown extension tail');
          addrW = r.u32(); mipLodBias = r.f32(); anisotropyEnable = !!r.u32();
          compareEnable = !!r.u32(); compareOp = r.u32(); minLod = r.f32(); maxLod = r.f32(); borderColor = r.u32();
        }
        r.done('SAMPLER');
        frame.samplers.set(id, {
          mag: FILTER[mag] || 'nearest', min: FILTER[min] || 'nearest', mipmap: FILTER[mipmap] || 'nearest',
          addrU: ADDR[addrU] || 'clamp-to-edge', addrV: ADDR[addrV] || 'clamp-to-edge', addrW: ADDR[addrW] || 'clamp-to-edge',
          maxAnisotropy: Math.max(1, Math.min(16, Math.round(maxAniso) || 1)),
          anisotropyEnable, compareEnable, compareOp, mipLodBias, minLod, maxLod, borderColor,
        });
        break;
      }
      case V2_REC.PUSH: {
        const offset = r.u32(), size = r.u32();
        r.skip(4); // stageFlags (kept for debugging)
        if (offset + size > V2_PUSH_SIZE) throw new Error('vk64: v2 PUSH out of range');
        const bytes = r.bytes(size); r.done('PUSH');
        needPass().ops.push({ op: 'push', offset, bytes: bytes.slice() });
        break;
      }
      case V2_REC.IMAGE_COPY: {
        const srcImage = v2Hex64(r.u64()), dstImage = v2Hex64(r.u64());
        const srcMip = r.u32(), srcLayer = r.u32(), dstMip = r.u32(), dstLayer = r.u32();
        const srcOrigin = [r.u32(), r.u32(), r.u32()], dstOrigin = [r.u32(), r.u32(), r.u32()];
        const extent = [r.u32(), r.u32(), r.u32()]; r.done('image copy');
        if (srcImage === V2_UNKNOWN_IMAGE || dstImage === V2_UNKNOWN_IMAGE ||
            extent.some(n => !n || n > MAX_DIMENSION) ||
            [...srcOrigin, ...dstOrigin].some(n => n > MAX_DIMENSION) || pass?.attachments.length)
          throw new Error('vk64: v2 invalid image copy');
        needPass().ops.push({ op: 'imageCopy', srcImage, dstImage, srcMip, srcLayer, dstMip, dstLayer, srcOrigin, dstOrigin, extent });
        break;
      }
      case V2_REC.IMAGE_BLIT: {
        const srcImage = v2Hex64(r.u64()), dstImage = v2Hex64(r.u64());
        const srcMip = r.u32(), srcLayer = r.u32(), dstMip = r.u32(), dstLayer = r.u32(), filter = r.u32();
        const srcOffsets = [[r.i32(), r.i32(), r.i32()], [r.i32(), r.i32(), r.i32()]];
        const dstOffsets = [[r.i32(), r.i32(), r.i32()], [r.i32(), r.i32(), r.i32()]];
        r.done('IMAGE_BLIT');
        if (srcImage === V2_UNKNOWN_IMAGE || dstImage === V2_UNKNOWN_IMAGE || filter > 1 ||
            [...srcOffsets.flat(), ...dstOffsets.flat()].some(v => Math.abs(v) > MAX_DIMENSION) ||
            srcOffsets[0][0] === srcOffsets[1][0] || srcOffsets[0][1] === srcOffsets[1][1] ||
            dstOffsets[0][0] === dstOffsets[1][0] || dstOffsets[0][1] === dstOffsets[1][1] || pass?.attachments.length)
          throw new Error('vk64: v2 invalid image blit');
        needPass().ops.push({ op: 'imageBlit', srcImage, dstImage, srcMip, srcLayer, dstMip, dstLayer, filter, srcOffsets, dstOffsets });
        break;
      }
      case V2_REC.STENCIL_STATE: {
        const kind = r.u32(), faces = r.u32(), value = r.u32(); r.done('STENCIL_STATE');
        if (kind > 2 || (faces & ~3)) throw new Error('vk64: v2 invalid stencil state');
        needPass().ops.push({op:'stencil', kind, faces, value}); break;
      }
      case V2_REC.BLEND_CONSTANTS: {
        const values = [r.f32(),r.f32(),r.f32(),r.f32()]; r.done('BLEND_CONSTANTS');
        needPass().ops.push({op:'blendConstants', values}); break;
      }
      case V2_REC.CLEAR_ATTACHMENTS: {
        const count = r.u32();
        if (count > 8) throw new Error('vk64: v2 clear attachment count out of range');
        const attachments = [];
        for (let a = 0; a < count; a++) {
          const aspectMask=r.u32(), colorAttachment=r.u32(), colorBits=[r.u32(),r.u32(),r.u32(),r.u32()], depth=r.f32(), stencil=r.u32();
          if (!aspectMask || (aspectMask & ~7) || colorAttachment > 7 || ((aspectMask & 2) && (!Number.isFinite(depth) || depth < 0 || depth > 1)))
            throw new Error('vk64: v2 invalid clear attachment');
          attachments.push({aspectMask,colorAttachment,colorBits,depth,stencil});
        }
        const rectCount=r.u32();
        if (rectCount > 4096) throw new Error('vk64: v2 clear rectangle count out of range');
        const rects=[];
        for(let a=0;a<rectCount;a++) rects.push({x:r.u32()|0,y:r.u32()|0,w:r.u32(),h:r.u32(),baseLayer:r.u32(),layers:r.u32()});
        r.done('CLEAR_ATTACHMENTS'); needPass().ops.push({op:'clearAttachments',attachments,rects}); break;
      }
      case V2_REC.DEPTH_BIAS: {
        const constantFactor = r.f32(), clamp = r.f32(), slopeFactor = r.f32(); r.done('DEPTH_BIAS');
        if (![constantFactor, clamp, slopeFactor].every(Number.isFinite)) throw new Error('vk64: v2 invalid depth bias');
        needPass().ops.push({op:'depthBias',constantFactor,clamp,slopeFactor}); break;
      }
      case V2_REC.RASTER_STATE: {
        const which = r.u32(), value = r.u32(); r.done('RASTER_STATE');
        needPass().ops.push({ op: 'rasterState', ...decodeDynState(which, value) }); break;
      }
      case V2_REC.IMAGE_CLEAR_DS: {
        const image = v2Hex64(r.u64()), depth = r.f32(), stencil = r.u32(), count = r.u32();
        if (count > 64 || !Number.isFinite(depth)) throw new Error('vk64: v2 invalid depth/stencil clear');
        const ranges = [];
        for (let i = 0; i < count; ++i) ranges.push({ aspectMask: r.u32(), baseMip: r.u32(), levelCount: r.u32(), baseLayer: r.u32(), layerCount: r.u32() });
        r.done('IMAGE_CLEAR_DS');
        if (image === V2_UNKNOWN_IMAGE || pass?.attachments.length) throw new Error('vk64: v2 invalid depth/stencil clear');
        needPass().ops.push({ op: 'imageClearDS', image, depth, stencil, ranges }); break;
      }
      case V2_REC.VIEWPORT: {
        const v = [r.f32(), r.f32(), r.f32(), r.f32(), r.f32(), r.f32()]; r.done('VIEWPORT');
        needPass().ops.push({ op: 'viewport', v });
        break;
      }
      case V2_REC.SCISSOR: {
        const s = [r.u32(), r.u32(), r.u32(), r.u32()]; r.done('SCISSOR');
        needPass().ops.push({ op: 'scissor', s });
        break;
      }
      case V2_REC.VERTEX_BIND: {
        const nVb = r.u8(); r.skip(3);
        if (nVb > 4) throw new Error('vk64: v2 VERTEX_BIND out of range');
        const binds = [];
        for (let k = 0; k < nVb; k++) binds.push({ binding: r.u32(), buf: v2Hex64(r.u64()), offset: Number(r.u64()) });
        r.done('VERTEX_BIND');
        needPass().ops.push({ op: 'vbind', binds });
        break;
      }
      case V2_REC.INDEX_BIND: {
        const buf = v2Hex64(r.u64()), offset = Number(r.u64()), indexType = r.u32();
        r.done('INDEX_BIND');
        if (indexType > 1) throw new Error('vk64: v2 INDEX_BIND bad index type');
        needPass().ops.push({ op: 'ibind', buf, offset, indexType });
        break;
      }
      case V2_REC.DRAW: {
        const vertexCount = r.u32(), instanceCount = r.u32(), firstVertex = r.u32(), firstInstance = r.u32();
        const indexed = r.u8(); r.skip(3);
        const indexCount = r.u32(), firstIndex = r.u32(), vertexOffset = r.u32() | 0;
        r.done('DRAW');
        if (++draws > MAX_DRAWS) throw new Error('vk64: v2 frame exceeds the page-tier draw limit');
        if (vertexCount > 1 << 24 || indexCount > 1 << 24) throw new Error('vk64: v2 DRAW range out of range');
        needPass().ops.push({ op: 'draw', indexed: indexed !== 0, vertexCount, instanceCount, firstVertex, firstInstance, indexCount, firstIndex, vertexOffset });
        break;
      }
      case V2_REC.INLINE_BYTES: {
        const setIndex = r.u8(); r.skip(3);
        const binding = r.u32(), dstArrayElement = r.u32(), len = r.u32();
        if (len > V2_MAX_RECORD_BYTES) throw new Error('vk64: v2 INLINE_BYTES len out of range');
        const bytes = r.bytes(len); r.done('INLINE_BYTES');
        needPass().ops.push({ op: 'inline', setIndex, binding, dstArrayElement, bytes: bytes.slice() });
        break;
      }
      case V2_REC.READBACK: {
        const srcImage = v2Hex64(r.u64());
        const w = r.u32(), h = r.u32(), heapPtr = r.u32(), flagPtr = r.u32();
        r.done('READBACK');
        if (w < 1 || h < 1 || w > 4096 || h > 4096) throw new Error('vk64: v2 READBACK size out of range');
        bwLog(3, 'vulkan', 'READBACK queued: src=' + srcImage + ' ' + w + 'x' + h + ' heap=0x' + heapPtr.toString(16));
        frame.readbacks.push({ srcImage, w, h, heapPtr, flagPtr });
        break;
      }
      default:
        throw new Error('vk64: v2 decode hit unhandled record 0x' + type.toString(16));
    }
  }
  return frame;
}

// B8G8R8A8 vertex attributes arrive as B,G,R,A bytes. WebGPU has no BGRA
// vertex format (mapped to unorm8x4 above), so without a swizzle the shader
// would see R/B swapped vs Vulkan. Swizzle each 4-byte attribute element
// once per (buffer, layout); the GPU cache key is layout-qualified.
export function swizzleBgraVb(bytes, stride, offsets) {
  const out = new Uint8Array(bytes);
  if (!(stride > 0)) return out;
  const count = Math.floor(out.length / stride);
  for (const a of offsets) {
    for (let i = 0; i < count; i++) {
      const o = i * stride + a;
      if (o + 3 >= out.length) break;
      const t = out[o]; out[o] = out[o + 2]; out[o + 2] = t;
    }
  }
  return out;
}

// The RP_BEGIN attachment-identity tail (schema v2) carries the VkImage id
// as a 16-char hex string; all-zero means the native side could not resolve it.
export const V2_UNKNOWN_IMAGE = '0000000000000000';
