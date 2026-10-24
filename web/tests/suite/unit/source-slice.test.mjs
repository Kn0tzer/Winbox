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
