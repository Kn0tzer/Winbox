// DESC_SET wire header: `u8 setIndex, u8 pad, u16 nBind`. The old decoder read
// `u8 nBind` and rejected anything over 8, which broke every DXVK/vkd3d layout
// with more than 8 written descriptors (and every descriptor array).
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';

function u32s(...v) { const b = new Uint8Array(v.length * 4); const d = new DataView(b.buffer); v.forEach((x, i) => d.setUint32(i * 4, x, true)); return b; }
function descSet(setIndex, entries) {
  const out = new Uint8Array(12 + entries.length * 44);
  const d = new DataView(out.buffer);
  d.setUint8(0, setIndex); d.setUint16(2, entries.length, true); d.setBigUint64(4, 0x77n, true);
  entries.forEach((e, i) => {
    const o = 12 + i * 44;
    d.setUint32(o, e.binding, true); d.setUint32(o + 4, e.type, true);
    d.setUint32(o + 8, e.elem, true); d.setUint32(o + 12, 1, true);
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

test('DESC_SET decodes more than 8 elements, arrays and per-element offsets', () => {
  const entries = [];
  for (let i = 0; i < 20; i++) entries.push({ binding: i % 3 === 0 ? 20 : i, elem: i % 3 === 0 ? i : 0, type: 7, kind: 1, obj: 0x100 + i, range: 16, off: 64 * i });
  const f = decodeV2Frame(frame({ type: V2_REC.DESC_SET, payload: descSet(2, entries) }));
  const op = f.passes[0].ops.find(o => o.op === 'descSet');
  assert.equal(op.setIndex, 2);
  assert.equal(op.layoutId.endsWith('77'), true);
  assert.equal(op.bindings.length, 20);
  assert.equal(op.bindings[19].dstArrayElement, 0);
  assert.equal(op.bindings[18].dstArrayElement, 18);
  assert.equal(op.bindings[18].bufOff, String(64 * 18));
});

test('DESC_SET with an out-of-range set index is rejected', () => {
  assert.throws(() => decodeV2Frame(frame({ type: V2_REC.DESC_SET, payload: descSet(200, []) })), /DESC_SET out of range/);
});
