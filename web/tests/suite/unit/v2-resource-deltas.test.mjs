import test from 'node:test';
import assert from 'node:assert/strict';
import { V2FrameQueue } from '../../../vkwebgpu.mjs';

test('discarded render frames retain texture and buffer deltas for hash-only references', () => {
  const q = new V2FrameQueue(1);
  const pixels = new Uint8Array([1, 2, 3, 4]), bytes = new Uint8Array([5, 6, 7, 8]);
  const frame = (data, hash = 'a') => ({ images: new Map([['image', { hash, pixels: data ? pixels : null }]]),
    buffers: new Map([['buffer', data ? bytes : null]]), bufHashes: new Map([['buffer', hash]]) });
  q.push(frame(true)); q.push(frame(false));
  const latest = q.takeLatest();
  assert.equal(q.dropped, 1);
  assert.deepEqual(latest.images.get('image').pixels, pixels);
  assert.deepEqual(latest.buffers.get('buffer'), bytes);
  q.push(frame(false, 'different'));
  const changed = q.takeLatest();
  assert.equal(changed.images.get('image').pixels, null);
  assert.equal(changed.buffers.get('buffer'), null);
});
