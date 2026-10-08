// web/tests/suite/unit/buffer-cache-keys.test.mjs — Buffer cache key consistency.
//
// REGRESSION TESTS for the D3D11 buffer cache bugs (commits 074b6fe5, 5395bed3).
//
// Bug A (074b6fe5): bufferKey() included effLen (bytes.length) in the cache key.
//   For deduped (byte-less) records, effLen=0. For full records, effLen=actual.
//   Deduped lookups never hit the cache → gpuBufferFor returned null → draw
//   used zero-filled dummyVb → degenerate triangle → no pixels.
//
// Bug B (5395bed3): Bridge computes 64-bit FNV-1a. Page recomputed 32-bit
//   FNV-1a for cache key when bytes were present. Keys never matched across
//   frames.
//
// These tests verify the INVARIANTS that prevent both bugs:
//   1. bufferKey must NOT include effLen (deduped and full records for the
//      same buffer+content must produce the SAME key)
//   2. When preHash (bridge 64-bit hash) is provided, it must be used
//      verbatim — never recomputed with a different hash function

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const vkwebgpuPath = join(repoRoot, 'web/vkwebgpu.mjs');
const src = readFileSync(vkwebgpuPath, 'utf8');

// Extract the bufferKey function source for static analysis
function getBufferKeySource() {
  const m = src.match(/function bufferKey\(bufHex, bytes, usage, effLen, preHash\) \{([\s\S]*?)\n  \}/);
  if (!m) throw new Error('Could not find bufferKey function in vkwebgpu.mjs');
  return m[1];
}

test('bufferKey does NOT include effLen in the cache key (Bug A regression)', () => {
  const body = getBufferKeySource();
  // The key must be: bufHex + ':' + usage + ':' + hash
  // It must NOT contain effLen
  assert.ok(
    !body.includes('effLen'),
    'bufferKey body must not reference effLen — deduped (effLen=0) and full ' +
    '(effLen=N) records for the same buffer must produce identical keys'
  );
});

test('bufferKey uses preHash verbatim when provided (Bug B regression)', () => {
  const body = getBufferKeySource();
  // Must use preHash directly, not recompute
  assert.ok(
    body.includes('preHash'),
    'bufferKey must accept and use preHash (bridge 64-bit hash)'
  );
  // The pattern should be: preHash || (bytes ? fnv1a(bytes) : 'empty')
  // NOT: always recompute fnv1a
  assert.ok(
    /preHash\s*\|\|/.test(body),
    'bufferKey must prefer preHash over recomputing (preHash || ...)'
  );
});

test('bufferKey output format is bufHex:usage:hash', () => {
  const body = getBufferKeySource();
  // Verify the key format hasn't drifted
  assert.ok(
    body.includes("bufHex + ':' + usage + ':' + hash") || body.includes('bufHex+":"+usage+":"+hash'),
    'bufferKey format must be "bufHex:usage:hash"'
  );
});

test('gpuBufferFor passes preHash through to bufferKey', () => {
  // gpuBufferFor must forward the bridge hash to bufferKey, not drop it
  const m = src.match(/function gpuBufferFor\(bufHex, bytes, usage, preHash\) \{([\s\S]*?)\n  \}/);
  assert.ok(m, 'Could not find gpuBufferFor function');
  const body = m[1];
  assert.ok(
    body.includes('preHash'),
    'gpuBufferFor must use preHash (bridge hash) when calling bufferKey'
  );
});
