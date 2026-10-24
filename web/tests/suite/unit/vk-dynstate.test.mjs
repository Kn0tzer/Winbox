import test from 'node:test';
import assert from 'node:assert/strict';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';
import { DYN_ID, applyDynamicState, decodeDynState } from '../../../vk-dynstate.mjs';

class W {
  constructor() { this.bytes = []; }
  u32(n) { for (let i = 0; i < 4; i++) this.bytes.push((n >>> (8 * i)) & 255); return this; }
  u64(n) { this.u32(n); this.u32(0); return this; }
  f32(n) { this.bytes.push(...new Uint8Array(new Float32Array([n]).buffer)); return this; }
  buf() { return new Uint8Array(this.bytes); }
}
function pipeline({ topology = 3, dynMask = 0, discard = 0, tail = true } = {}) {
  const w = new W().u64(1).u64(2).u64(3);
  [topology, 0, 0, 1, 1, 3, 0, 0, 0].forEach(n => w.u32(n));
  w.u32(0).u32(0);
  w.u32(0x31444e42).u32(0); [0, 0, 0, 0].forEach(n => w.f32(n));
  if (tail) w.u32(0x314e5944).u32(dynMask).u32(discard); // DYN1
  return w.buf();
}
function records(pipe, extra = []) {
  const begin = new W().u32(1).u32(4).u32(4).u32(0).buf();
  const rp = new W().u64(1).u64(2).u32(4).u32(4).u32(0).f32(0).f32(0).f32(0).f32(1).f32(1).u32(0).buf();
  return [
    { type: V2_REC.FRAME_BEGIN, payload: begin }, { type: V2_REC.RP_BEGIN, payload: rp },
    { type: V2_REC.PIPELINE, payload: pipe }, ...extra,
    { type: V2_REC.RP_END, payload: new Uint8Array(0) }, { type: V2_REC.FRAME_END, payload: new W().u32(1).buf() },
  ];
}
const packOp = (face, fail, pass, depthFail, cmp) => face | (fail << 4) | (pass << 8) | (depthFail << 12) | (cmp << 16);

test('DYN1 tail and extended-dynamic-state RASTER_STATE records decode', () => {
  const mask = (1 << DYN_ID.topology) | (1 << DYN_ID.depthTest) | (1 << DYN_ID.stencilOp);
  const frame = decodeV2Frame(records(pipeline({ dynMask: mask, discard: 1 }), [
    { type: V2_REC.RASTER_STATE, payload: new W().u32(DYN_ID.topology).u32(1).buf() },
    { type: V2_REC.RASTER_STATE, payload: new W().u32(DYN_ID.stencilOp).u32(packOp(3, 1, 2, 3, 5)).buf() },
  ]));
  const pipe = frame.pipelines.get('0000000000000001');
  assert.equal(pipe.dynMask, mask);
  assert.equal(pipe.rasterizerDiscard, 1);
  assert.deepEqual(frame.passes[0].ops.filter(o => o.op === 'rasterState'), [
    { op: 'rasterState', which: 'topology', value: 1 },
    { op: 'rasterState', which: 'stencilOp', value: { faceMask: 3, failOp: 1, passOp: 2, depthFailOp: 3, compareOp: 5 } },
  ]);
});

test('pipelines without a DYN1 tail are fully static; hostile values are rejected', () => {
  const pipe = decodeV2Frame(records(pipeline({ tail: false }))).pipelines.get('0000000000000001');
  assert.equal(pipe.dynMask, undefined);
  assert.equal(pipe.rasterizerDiscard, undefined);
  assert.throws(() => decodeDynState(DYN_ID.topology, 6), /dynamic topology/);
  assert.throws(() => decodeDynState(DYN_ID.depthTest, 2), /dynamic depthTest/);
  assert.throws(() => decodeDynState(DYN_ID.depthOp, 8), /depth op/);
  assert.throws(() => decodeDynState(DYN_ID.stencilOp, packOp(0, 0, 0, 0, 0)), /stencil op/);
  assert.throws(() => decodeDynState(DYN_ID.stencilOp, packOp(1, 0, 0, 0, 0) | (1 << 20)), /stencil op/);
  assert.throws(() => decodeV2Frame(records(pipeline({ dynMask: 1 << 12 }))), /extended dynamic state/);
});

test('an unsupported static topology is an error, never a silent triangle-list', () => {
  assert.throws(() => decodeV2Frame(records(pipeline({ topology: 6 }))), /unsupported topology 6/);
  const pipe = decodeV2Frame(records(pipeline({ topology: 10, dynMask: 1 << DYN_ID.topology }))).pipelines.get('0000000000000001');
  assert.equal(pipe.topology, 3);   // static value ignored when the topology is dynamic
});

test('applyDynamicState overlays only the states the pipeline declared dynamic', () => {
  const base = { topology: 3, depthTest: 1, depthWrite: 1, depthOp: 1, depthBiasEnable: 0, primitiveRestartEnable: false,
    stencil: { enabled: 0, dynamicMask: 0, front: { failOp: 0, passOp: 0, depthFailOp: 0, compareOp: 7, compareMask: 255, writeMask: 255, reference: 0 },
      back: { failOp: 0, passOp: 0, depthFailOp: 0, compareOp: 7, compareMask: 255, writeMask: 255, reference: 0 } } };
  const dyn = { topology: 4, depthTest: 0, depthWrite: 0, depthOp: 7, depthBiasEnable: 1, primitiveRestart: 1, rasterizerDiscard: 1,
    stencilTest: 1, stencilOp: { faceMask: 1, failOp: 2, passOp: 3, depthFailOp: 4, compareOp: 5 } };
  assert.equal(applyDynamicState({ ...base }, dyn).topology, 3, 'static pipeline ignores the command');
  const same = { ...base };
  assert.equal(applyDynamicState(same, dyn), same, 'no copy when nothing is dynamic');
  const all = (1 << 12) - 1;
  const eff = applyDynamicState({ ...base, dynMask: all }, dyn);
  assert.equal(eff.topology, 4); assert.equal(eff.depthTest, 0); assert.equal(eff.depthWrite, 0); assert.equal(eff.depthOp, 7);
  assert.equal(eff.depthBiasEnable, 1); assert.equal(eff.primitiveRestartEnable, true); assert.equal(eff.rasterizerDiscard, 1);
  assert.equal(eff.stencil.enabled, 1);
  assert.deepEqual([eff.stencil.front.failOp, eff.stencil.front.passOp, eff.stencil.front.depthFailOp, eff.stencil.front.compareOp], [2, 3, 4, 5]);
  assert.equal(eff.stencil.back.compareOp, 7, 'back face not in faceMask');
  assert.equal(base.stencil.front.failOp, 0, 'input stencil not mutated');
  // a dynamic state never set before the draw keeps its static value
  assert.equal(applyDynamicState({ ...base, dynMask: all }, {}).depthOp, 1);
  // dynamic stencil enable on a pipeline that recorded no stencil state uses Vulkan defaults
  const noStencil = applyDynamicState({ topology: 3, dynMask: 1 << DYN_ID.stencilTest }, { stencilTest: 1 });
  assert.equal(noStencil.stencil.enabled, 1);
  assert.equal(noStencil.stencil.front.compareOp, 7);
});
