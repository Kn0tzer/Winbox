import test from 'node:test';
import assert from 'node:assert/strict';
import { extractSourceSlice } from '../lib/source-slice.mjs';

test('source slices fail with the missing marker and source path', () => {
  assert.throws(
    () => extractSourceSlice('before\nafter', 'missing start', 'after', { sourceName: 'sample.cpp' }),
    /source slice marker not found in sample\.cpp: missing start/,
  );
  assert.throws(
    () => extractSourceSlice('before\nafter', 'before', 'missing end', { sourceName: 'sample.cpp' }),
    /source slice marker not found in sample\.cpp: missing end/,
  );
});

test('computed C++ slice offsets reject missing, reversed, and empty regions', async () => {
  const { extractSourceRange } = await import('../lib/source-slice.mjs');
  for (const [start, end] of [[-1, 4], [1, -1], [3, 2], [2, 2], [0, 99]]) {
    assert.throws(() => extractSourceRange('before after', start, end), /source slice marker not found/);
  }
  assert.equal(extractSourceRange('before after', 7), 'after');
  assert.equal(extractSourceRange('before after', 0, 6), 'before');
});
