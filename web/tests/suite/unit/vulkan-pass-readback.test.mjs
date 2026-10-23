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
  assert.deepEqual(v2SummarizeColorPixels(rows, 2, 2, stride, false),
    { avg: [10, 20, 30, 255], constant: true, constantColor: [10, 20, 30, 255], nonBlackPixels: 4, pixels: 4 });
  rows.set([0, 0, 0, 255], stride + 4);
  const mixed = v2SummarizeColorPixels(rows, 2, 2, stride, true); // bgra: channels swapped in output
  assert.equal(mixed.constant, false);
  assert.equal(mixed.constantColor, null);
  assert.equal(mixed.nonBlackPixels, 3);
  assert.deepEqual(mixed.avg, [22.5, 15, 7.5, 255]);
});
