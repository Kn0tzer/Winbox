import test from 'node:test';
import assert from 'node:assert/strict';
import { v2PassReadbackConfig, v2SummarizeColorPixels } from '../../../vkwebgpu.mjs';

test('pass readback diagnostic is opt-in via ?bwPassReadback=N', () => {
  assert.equal(v2PassReadbackConfig(''), null);
  assert.equal(v2PassReadbackConfig('?bwPassReadback=0'), null);
  assert.equal(v2PassReadbackConfig('?bwPassReadback=abc'), null);
  assert.deepEqual(v2PassReadbackConfig('?bwPassReadback=30&bwPassReadbackMax=5'), { every: 30, maxPasses: 5, maxPixels: 4 * 1024 * 1024 });
  assert.equal(v2PassReadbackConfig('?bwPassReadback=1').maxPasses, 64);
});

test('colour summary reports constant colour, average and non-black count', () => {
  const stride = 256;
  const rows = new Uint8Array(stride * 2);
  for (let y = 0; y < 2; y++) for (let x = 0; x < 2; x++) rows.set([10, 20, 30, 255], y * stride + x * 4);
  assert.deepEqual(v2SummarizeColorPixels(rows, 2, 2, stride, "rgba8unorm"),
    { avg: [10, 20, 30, 255], constant: true, constantColor: [10, 20, 30, 255], nonBlackPixels: 4, nonFinite: 0, pixels: 4 });
  rows.set([0, 0, 0, 255], stride + 4);
  const mixed = v2SummarizeColorPixels(rows, 2, 2, stride, "bgra8unorm-srgb"); // bgra: channels swapped in output
  assert.equal(mixed.constant, false);
  assert.equal(mixed.constantColor, null);
  assert.equal(mixed.nonBlackPixels, 3);
  assert.deepEqual(mixed.avg, [22.5, 15, 7.5, 255]);
});

test('colour summary decodes rgba16float texels', () => {
  const bytes = new Uint8Array(256);
  const dv = new DataView(bytes.buffer);
  for (let x = 0; x < 2; x++) [0x3c00, 0x3800, 0, 0x3c00].forEach((h, c) => dv.setUint16(x * 8 + c * 2, h, true)); // 1, 0.5, 0, 1
  assert.deepEqual(v2SummarizeColorPixels(bytes, 2, 1, 256, 'rgba16float').avg, [1, 0.5, 0, 1]);
});
