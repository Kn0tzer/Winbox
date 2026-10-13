import test from 'node:test';
import assert from 'node:assert/strict';
import { encodeFrame, decodeFrame } from '../../game-frame-codec.mjs';

test('captured frames retain resource maps, array slices, floats and 64-bit values', () => {
  const frame = { images: new Map([['a', { pixels: new Uint8Array([9, 2, 3, 8]).subarray(1, 3) }]]),
    push: new Float32Array([0.5, -7.25]), indices: new Uint16Array([0, 65535]), id: 0xffffffffffffffffn };
  assert.deepEqual(decodeFrame(encodeFrame(frame)), frame);
});

test('a frame cannot instantiate an arbitrary global constructor', () => {
  assert.throws(() => decodeFrame('{"$type":"Function","$bytes":""}'), /Unsupported frame array/);
});
