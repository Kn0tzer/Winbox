// Texel buffer views (register row: Portal device creation). DXVK creates an
// R8_UINT texel buffer view during device creation (its HUD renderer's data
// buffer); the bridge used to reject every vkCreateBufferView, which failed
// D3D device creation. UNIFORM texel buffers are now served end to end:
// creation succeeds for page-servable formats, the descriptor travels as wire
// kind 5 with a V2_BUFFER_VIEW record, and spirvfix R12 rewrites the shader
// fetch onto the materialised 2D texture. STORAGE texel buffers stay
// REJECTED-LOUDLY (shader writes to a texture copy could never reach guest
// memory) and are never advertised.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { decodeV2Frame, V2_REC, V2_TEXEL_FORMAT, V2_TEXEL_WRAP, v2TexelTextureFormat } from '../../../vk-protocol.mjs';

const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
const spirvfix = readFileSync(new URL('../../../../tools/spirvfix/spirvfix.mjs', import.meta.url), 'utf8');

function caseBlock(after) {
  const at = bridge.indexOf(after);
  assert.ok(at >= 0, after);
  const end = bridge.indexOf('\n            case ', at + after.length);
  return bridge.slice(at, end < 0 ? undefined : end);
}

test('vkCreateBufferView creates views for servable formats (both ABIs)', () => {
  const blocks = [];
  let at = 0;
  while (true) {
    const i = bridge.indexOf('case VK64_fn_vkCreateBufferView', at);
    if (i < 0) break;
    blocks.push(bridge.slice(i, bridge.indexOf('case VK64_fn_', i + 10)));
    at = i + 10;
  }
  assert.equal(blocks.length, 2);
  assert.match(blocks[0], /createObj<BufferView>\(K_BUFFERVIEW/);
  assert.match(blocks[1], /createObj32<BufferView>\(K_BUFFERVIEW/);
  for (const b of blocks) {
    assert.match(b, /vk64BufferViewRange\(buf->size/);
    assert.ok(!b.includes('are not implemented'), 'blanket rejection must be gone');
  }
});

test('unservable formats keep the loud FORMAT_NOT_SUPPORTED rejection', () => {
  assert.match(bridge, /vk64Unsupported\("texel-buffer-view-format"/);
  assert.match(bridge, /!v2TexelBufferServable\(format\)\) \{\s*vk64Unsupported\("texel-buffer-view-format"[\s\S]{0,240}?return VK_ERROR_FORMAT_NOT_SUPPORTED;/);
});

test('format properties advertise UNIFORM_TEXEL_BUFFER only via the servable predicate', () => {
  const at = bridge.indexOf('static VkFormatFeatureFlags v2FormatBufferFeatureFlags');
  const body = bridge.slice(at, bridge.indexOf('\n}\n', at));
  assert.match(body, /if \(v2TexelBufferServable\(format\)\) flags \|= VK_FORMAT_FEATURE_UNIFORM_TEXEL_BUFFER_BIT;/);
  assert.ok(!bridge.includes('VK_FORMAT_FEATURE_STORAGE_TEXEL_BUFFER_BIT'),
    'storage texel must never be advertised');
});

test('descriptor writes: uniform texel records the view, storage stays loud', () => {
  const uni = caseBlock('case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER:');
  assert.ok(!uni.includes('vk64Unsupported'), 'uniform texel writes must not log unsupported');
  assert.match(uni, /b\.obj = rdq\(w\.pTexel \+ at\)/);
  const sto = caseBlock('case VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER:');
  assert.match(sto, /vk64Unsupported\("texel-buffer-storage"/);
});

test('submit path emits the view record, wire kind 5 and the view bytes', () => {
  assert.match(bridge, /case VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER: kind = 5; break;/);
  assert.match(bridge, /emit\(V2_BUFFER_VIEW, \[&\]\(ChunkWriter& p\)/);
  assert.match(bridge, /emitBufferData\(bv->buffer, vbuf->memOff \+ bv->offset, bv->range\)/);
  assert.match(bridge, /V2_BUFFER_VIEW = 0x1C/);
});

test('the R8_UINT view DXVK creates for its HUD is servable', () => {
  assert.equal(V2_TEXEL_FORMAT[13], 'r8uint'); // VK_FORMAT_R8_UINT
  assert.equal(v2TexelTextureFormat(13, undefined), 'r8uint');
});

test('unservable classes resolve to no texel texture', () => {
  assert.equal(v2TexelTextureFormat(131, undefined), null); // BC1
  assert.equal(v2TexelTextureFormat(124, undefined), null); // D16_UNORM
  assert.equal(v2TexelTextureFormat(43, undefined), null);  // R8G8B8A8_SRGB
  assert.equal(v2TexelTextureFormat(23, undefined), null);  // R8G8B8_UNORM (3-component)
});

test('norm16 texel formats fall back to half float without the tier1 feature', () => {
  assert.equal(v2TexelTextureFormat(70, undefined), 'r16float');
  assert.equal(v2TexelTextureFormat(77, undefined), 'rg16float');
});

test('page wrap width matches the spirvfix R12 wrap width', () => {
  assert.equal(V2_TEXEL_WRAP, 8192);
  assert.match(spirvfix, /const TEXEL_WRAP_WIDTH = 8192;/);
});

// --- wire decode ------------------------------------------------------------
function u32s(...v) { const b = new Uint8Array(v.length * 4); const d = new DataView(b.buffer); v.forEach((x, i) => d.setUint32(i * 4, x, true)); return b; }
function bufferView(viewId, bufferId, format, absOff, len) {
  const out = new Uint8Array(40); const d = new DataView(out.buffer);
  d.setBigUint64(0, BigInt(viewId), true); d.setBigUint64(8, BigInt(bufferId), true);
  d.setUint32(16, format, true); d.setBigUint64(24, BigInt(absOff), true); d.setBigUint64(32, BigInt(len), true);
  return out;
}
function descSet(entries) {
  const out = new Uint8Array(12 + entries.length * 44); const d = new DataView(out.buffer);
  d.setUint8(0, 0); d.setUint16(2, entries.length, true); d.setBigUint64(4, 0x77n, true);
  entries.forEach((e, i) => {
    const o = 12 + i * 44;
    d.setUint32(o, e.binding, true); d.setUint32(o + 4, e.type, true);
    d.setUint32(o + 8, 0, true); d.setUint32(o + 12, 1, true);
    d.setUint8(o + 16, e.kind);
    d.setBigUint64(o + 20, BigInt(e.obj), true); d.setBigUint64(o + 28, BigInt(e.range || 0), true);
    d.setBigUint64(o + 36, BigInt(e.off || 0), true);
  });
  return out;
}
const frame = (...mid) => [
  { type: V2_REC.FRAME_BEGIN, payload: u32s(1, 64, 64, 0) }, ...mid,
  { type: V2_REC.FRAME_END, payload: u32s(1) },
];

test('BUFFER_VIEW record and kind-5 binding decode with view identity', () => {
  const f = decodeV2Frame(frame(
    { type: V2_REC.BUFFER_VIEW, payload: bufferView(0x55, 0x99, 13, 4096, 16384) },
    { type: V2_REC.DESC_SET, payload: descSet([{ binding: 1, type: 4, kind: 5, obj: 0x55, range: 16384, off: 4096 }]) },
  ));
  const view = f.bufferViews.get('0000000000000055');
  assert.deepEqual(view, { bufferId: '0000000000000099', format: 13, absOff: '4096', len: '16384' });
  const op = f.passes[0].ops.find(o => o.op === 'descSet');
  const b = op.bindings.find(x => x.binding === 1);
  assert.equal(b.kind, 5);
  assert.equal(b.obj, '0000000000000055');
  assert.equal(b.range, '0000000000004000'); // padded hex, per the DESC_SET decode contract
  assert.equal(b.bufOff, '4096');
});
