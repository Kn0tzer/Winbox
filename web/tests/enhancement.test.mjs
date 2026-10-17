import test from 'node:test';
import assert from 'node:assert/strict';
import { enhancementSize } from '../enhancement.mjs';

test('enhancement renders at display resolution and respects GPU texture limits', () => {
  assert.deepEqual(enhancementSize(1200, 750, 2), { width: 2400, height: 1500 });
  assert.deepEqual(enhancementSize(7680, 4320, 2), { width: 4096, height: 2304 });
  assert.deepEqual(enhancementSize(0, 0, 1), { width: 1, height: 1 });
});
