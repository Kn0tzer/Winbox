// web/tests/suite/unit/vk-compute-records.test.mjs — V2 compute records,
// campaign section 4.27.
//
// Compute-only SPIR-V modules were accepted at pipeline creation since
// the D3D12 stack merge, but dispatches were loud-dropped at submit
// ("compute-dispatch") because the V2 protocol had no compute records.
// The protocol now carries PIPELINE_COMPUTE (0x1E), DISPATCH (0x1F) and
// BUFFER_READBACK (0x20); SHADER records may carry stage 2 (compute).
// These tests pin the wire decode: a frame built from the new records
// decodes into a compute pipeline, a dispatch op and a buffer readback,
// in order, and malformed input still throws.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { decodeV2Frame, parseV2Chunk, V2_REC } from '../../../vk-protocol.mjs';

function u32s(...v) { const b = new Uint8Array(v.length * 4); const d = new DataView(b.buffer); v.forEach((x, i) => d.setUint32(i * 4, x, true)); return b; }
function shaderPayload(stage, hash, code) {
  const out = new Uint8Array(4 + 8 + 4 + code.length);
  const d = new DataView(out.buffer);
  d.setUint8(0, stage);
  d.setBigUint64(4, BigInt(hash), true);
  d.setUint32(12, code.length, true);
  out.set(code, 16);
  return out;
}
function pipelineComputePayload(pipeId, csHash, spec) {
  const tail = spec ? 4 + spec.length * 12 : 0;
  const out = new Uint8Array(16 + tail);
  const d = new DataView(out.buffer);
  d.setBigUint64(0, BigInt(pipeId), true);
  d.setBigUint64(8, BigInt(csHash), true);
  if (spec) {
    d.setUint32(16, spec.length, true);
    spec.forEach((e, i) => {
      const o = 20 + i * 12;
      d.setUint32(o, e.constantID, true);
      d.setUint32(o + 4, e.value.length, true);
      out.set(e.value, o + 8);
    });
  }
  return out;
}
function bufferReadbackPayload(srcBuffer, srcAbsOff, size, heapPtr, flagPtr) {
  const out = new Uint8Array(32);
  const d = new DataView(out.buffer);
  d.setBigUint64(0, BigInt(srcBuffer), true);
  d.setBigUint64(8, BigInt(srcAbsOff), true);
  d.setBigUint64(16, BigInt(size), true);
  d.setUint32(24, heapPtr, true);
  d.setUint32(28, flagPtr, true);
  return out;
}
const frame = (...mid) => [
  { type: V2_REC.FRAME_BEGIN, payload: u32s(1, 64, 64, 0) }, ...mid,
  { type: V2_REC.FRAME_END, payload: u32s(1) },
];

test('compute records decode into a pipeline, a dispatch and a buffer readback', () => {
  const f = decodeV2Frame(frame(
    { type: V2_REC.SHADER, payload: shaderPayload(2, 0xC5, new Uint8Array([1, 2, 3, 4])) },
    { type: V2_REC.PIPELINE_COMPUTE, payload: pipelineComputePayload(0xAA, 0xC5, [{ constantID: 7, value: new Uint8Array([9, 0, 0, 0]) }]) },
    { type: V2_REC.DISPATCH, payload: u32s(2, 3, 4) },
    { type: V2_REC.BUFFER_READBACK, payload: bufferReadbackPayload(0xBB, 0x1000, 256, 0x5000, 0x5100) },
  ));
  assert.equal(f.shaders.get('00000000000000c5').stage, 2, 'stage 2 is a compute shader');
  const cp = f.computePipelines.get('00000000000000aa');
  assert.ok(cp, 'compute pipeline decoded');
  assert.equal(cp.csHash, '00000000000000c5');
  assert.equal(cp.csSpec.length, 1);
  assert.equal(cp.csSpec[0].constantID, 7);
  assert.deepEqual([...cp.csSpec[0].value], [9, 0, 0, 0]);
  const ops = f.passes[0].ops;
  const bind = ops.find(o => o.op === 'bindComputePipe');
  const disp = ops.find(o => o.op === 'dispatch');
  assert.ok(bind && disp, 'bind + dispatch ops present');
  assert.ok(ops.indexOf(bind) < ops.indexOf(disp), 'bind precedes dispatch');
  assert.deepEqual([disp.x, disp.y, disp.z], [2, 3, 4]);
  assert.equal(f.bufReadbacks.length, 1);
  assert.equal(f.bufReadbacks[0].srcBuffer, '00000000000000bb');
  assert.equal(f.bufReadbacks[0].srcAbsOff, 0x1000n);
  assert.equal(f.bufReadbacks[0].size, 256n);
  assert.equal(f.bufReadbacks[0].heapPtr, 0x5000);
  assert.equal(f.bufReadbacks[0].flagPtr, 0x5100);
});

test('a compute pipeline without a specialization tail decodes with empty spec', () => {
  const f = decodeV2Frame(frame(
    { type: V2_REC.PIPELINE_COMPUTE, payload: pipelineComputePayload(0xAA, 0xC5, null) },
  ));
  assert.deepEqual(f.computePipelines.get('00000000000000aa').csSpec, []);
});

test('SHADER stage above compute is still rejected', () => {
  assert.throws(
    () => decodeV2Frame(frame({ type: V2_REC.SHADER, payload: shaderPayload(3, 0xC5, new Uint8Array(4)) })),
    /SHADER bad stage/);
});

test('BUFFER_READBACK with an out-of-range size is rejected', () => {
  assert.throws(
    () => decodeV2Frame(frame({ type: V2_REC.BUFFER_READBACK, payload: bufferReadbackPayload(0xBB, 0, 1 << 30, 0, 0) })),
    /BUFFER_READBACK size out of range/);
});

test('parseV2Chunk accepts the compute record types and rejects the next one', () => {
  const chunk = (type) => {
    const out = new Uint8Array(16 + 8 + 4);
    const d = new DataView(out.buffer);
    d.setUint32(0, 0x46324B56, true); // 'VK2F'
    d.setUint16(4, 2, true);
    d.setUint16(16, type, true);
    d.setUint32(20, 4, true);
    return out;
  };
  for (const t of [V2_REC.PIPELINE_COMPUTE, V2_REC.DISPATCH, V2_REC.BUFFER_READBACK, V2_REC.B2I_UPLOAD]) {
    const parsed = parseV2Chunk(chunk(t));
    assert.equal(parsed.records.length, 1);
    assert.equal(parsed.records[0].type, t);
  }
  assert.throws(() => parseV2Chunk(chunk(0x22)), /unknown record type/);
});
