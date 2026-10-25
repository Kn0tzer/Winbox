import test from 'node:test';
import assert from 'node:assert/strict';
import { rgbaPng, unpackCaptureImage } from '../../../../tools/bench/vk-capture-artifact.mjs';
import { screenshotPixels } from '../lib/pixels.mjs';

const input = (format, bytes) => ({ format, w: 1, h: 1, bytesPerRow: bytes.length, data: { $bytes: Buffer.from(bytes).toString('base64') } });
test('capture artifacts preserve raw bytes, decode BGRA channels, and remove only row padding', () => {
  const a = unpackCaptureImage(input('bgra8unorm', [17, 34, 255, 255, 7, 8, 9, 10]));
  assert.deepEqual([...a.raw], [17, 34, 255, 255]);
  assert.deepEqual([...a.rgba], [255, 34, 17, 255]);
  const b = unpackCaptureImage(input('bgra8unorm', [17, 34, 255, 255, 0, 0, 0, 0]));
  assert.equal(a.sha256, b.sha256, 'undefined GPU row padding cannot cause a golden mismatch');
  const png = rgbaPng(1, 1, a.rgba);
  assert.deepEqual(screenshotPixels(png), { width: 1, height: 1, lit: 1, varied: 1, redDominant: 1 });
});
test('floating target preview clamps values but keeps raw HDR bytes intact', () => {
  const bytes = Buffer.alloc(8); [0x3800, 0x4000, 0xbc00, 0x3c00].forEach((v, i) => bytes.writeUInt16LE(v, i * 2));
  const out = unpackCaptureImage(input('rgba16float', bytes));
  assert.deepEqual(out.raw, bytes); assert.deepEqual([...out.rgba], [128, 255, 0, 255]);
  assert.throws(() => unpackCaptureImage({ ...input('rgba8unorm', []), bytesPerRow: 0 }), /extent/);
});
