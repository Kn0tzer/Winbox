import test from 'node:test';
import assert from 'node:assert/strict';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';

class W {
  constructor() { this.bytes = []; }
  u32(n) { for (let i = 0; i < 4; i++) this.bytes.push((n >>> (8 * i)) & 255); return this; }
  u64(n) { this.u32(n); this.u32(0); return this; }
  f32(n) { this.bytes.push(...new Uint8Array(new Float32Array([n]).buffer)); return this; }
  buf() { return new Uint8Array(this.bytes); }
}
function pipeline(dynamicCull, dynamicFront) {
  const w = new W().u64(1).u64(2).u64(3);
  [3, 0, 0, 1, 1, 3, 0, 0, 0].forEach(n => w.u32(n));
  w.u32(0).u32(0);
  w.u32(0x31444e42).u32(0); [0, 0, 0, 0].forEach(n => w.f32(n));
  w.u32(0x31545352).u32(dynamicCull).u32(dynamicFront); // RST1
  return w.buf();
}
function records(extra, dc = 1, df = 1) {
  const begin = new W().u32(1).u32(4).u32(4).u32(0).buf();
  const rp = new W().u64(1).u64(2).u32(4).u32(4).u32(0).f32(0).f32(0).f32(0).f32(1).f32(1).u32(0).buf();
  return [
    { type: V2_REC.FRAME_BEGIN, payload: begin }, { type: V2_REC.RP_BEGIN, payload: rp },
    { type: V2_REC.PIPELINE, payload: pipeline(dc, df) }, ...extra,
    { type: V2_REC.RP_END, payload: new Uint8Array(0) }, { type: V2_REC.FRAME_END, payload: new W().u32(1).buf() },
  ];
}

test('RST1 pipeline tail and RASTER_STATE records decode', () => {
  assert.equal(V2_REC.RASTER_STATE, 0x1A);
  const frame = decodeV2Frame(records([
    { type: V2_REC.RASTER_STATE, payload: new W().u32(0).u32(2).buf() },
    { type: V2_REC.RASTER_STATE, payload: new W().u32(1).u32(1).buf() },
  ]));
  const pipe = frame.pipelines.get('0000000000000001');
  assert.equal(pipe.cullDynamic, true);
  assert.equal(pipe.frontDynamic, true);
  assert.deepEqual(frame.passes[0].ops.filter(o => o.op === 'rasterState'), [
    { op: 'rasterState', which: 'cull', value: 2 }, { op: 'rasterState', which: 'front', value: 1 },
  ]);
});

test('static pipelines carry no dynamic raster flags and bad raster state is rejected', () => {
  const frame = decodeV2Frame(records([], 0, 0));
  assert.equal(frame.pipelines.get('0000000000000001').cullDynamic, undefined);
  assert.throws(() => decodeV2Frame(records([{ type: V2_REC.RASTER_STATE, payload: new W().u32(2).u32(0).buf() }])), /invalid raster state/);
});

test('IMAGE_CLEAR_DS decodes depth, stencil and ranges', () => {
  assert.equal(V2_REC.IMAGE_CLEAR_DS, 0x1B);
  const payload = new W().u64(7).f32(0.5).u32(9).u32(1).u32(6).u32(0).u32(1).u32(0).u32(1).buf();
  const frame = decodeV2Frame(records([{ type: V2_REC.IMAGE_CLEAR_DS, payload }]));
  const op = frame.passes[0].ops.find(o => o.op === 'imageClearDS');
  assert.equal(op.depth, 0.5);
  assert.equal(op.stencil, 9);
  assert.deepEqual(op.ranges, [{ aspectMask: 6, baseMip: 0, levelCount: 1, baseLayer: 0, layerCount: 1 }]);
});
