// The bare-id buffer key is the page tier's base-slice view of a VkBuffer:
// the VB/IB draw paths (and last-resort binding fallbacks) read
// frame.buffers.get(bufId) and treat the bytes as starting at the buffer's
// base. Range records for one buffer arrive in command order, and any
// range -- a descriptor range, an update, or a buffer->image copy source
// (section 4.41) -- may precede the base range's record. The decoder must
// therefore arbitrate the bare key to the LOWEST-offset range record, not
// the first record in the stream. Regression shape (uav12, section 4.41
// merge): vkd3d sub-allocates the upload heap and the vertex buffer from
// one VkBuffer; the upload copy's source record (offset 65536, the pattern
// bytes) preceded the vertex record (offset 0), first-record-wins built
// the draw's vertex buffer out of the pattern, the fullscreen triangle
// degenerated, and the render target kept its clear colour.
import test from 'node:test';
import assert from 'node:assert/strict';
import { decodeV2Frame, V2_REC } from '../../../vk-protocol.mjs';

const BUF = 0x1b9n;
const bufData = (memOff, hash, bytes) => {
  const payload = new Uint8Array(28 + (bytes ? bytes.length : 0));
  const v = new DataView(payload.buffer);
  v.setBigUint64(0, BUF, true);
  v.setBigUint64(8, BigInt(memOff), true);
  v.setBigUint64(16, BigInt(hash), true);
  v.setUint32(24, bytes ? bytes.length : 0, true);
  if (bytes) payload.set(bytes, 28);
  return { type: V2_REC.BUFFER_DATA, payload };
};
const frameOf = (...records) => {
  const header = new Uint8Array(16), end = new Uint8Array(4);
  const hv = new DataView(header.buffer);
  hv.setUint32(4, 4, true); hv.setUint32(8, 4, true);
  return decodeV2Frame([{ type: V2_REC.FRAME_BEGIN, payload: header },
    ...records, { type: V2_REC.FRAME_END, payload: end }]);
};

const PATTERN = new Uint8Array([0x10, 0xfa, 0x1e, 0xff, 0x1d, 0xef, 0x25, 0xff]);
const VERTS = new Uint8Array([0x00, 0x00, 0x80, 0xbf, 0x00, 0x00, 0x80, 0xbf]); // -1.0f, -1.0f
const HASH_PATTERN = 0xaaaa, HASH_VERTS = 0xbbbb;

test('bare key holds the base slice when a copy-source range precedes it', () => {
  const f = frameOf(bufData(65536, HASH_PATTERN, PATTERN), bufData(0, HASH_VERTS, VERTS));
  assert.deepEqual(f.buffers.get('00000000000001b9'), VERTS,
    'the vertex path must see the base slice, not the upload pattern');
  assert.equal(f.bufHashes.get('00000000000001b9'), '000000000000bbbb');
  // The qualified keys are untouched: each range keeps its own record.
  assert.deepEqual(f.buffers.get('00000000000001b9:65536'), PATTERN);
  assert.deepEqual(f.buffers.get('00000000000001b9:0'), VERTS);
});

test('bare key follows the base slice hash when its record is byte-less', () => {
  // Later frame of the same shape: the base range is unchanged (byte-less
  // dedup record), the copy source carries fresh bytes. The bare key must
  // stay with the base slice (null bytes, base hash) so the tier resolves
  // the vertex buffer from its persistent cache by the base hash.
  const f = frameOf(bufData(65536, HASH_PATTERN, PATTERN), bufData(0, HASH_VERTS, null));
  assert.equal(f.buffers.get('00000000000001b9'), null);
  assert.equal(f.bufHashes.get('00000000000001b9'), '000000000000bbbb');
});

test('bare key is unchanged when the base record already comes first', () => {
  const f = frameOf(bufData(0, HASH_VERTS, VERTS), bufData(65536, HASH_PATTERN, PATTERN));
  assert.deepEqual(f.buffers.get('00000000000001b9'), VERTS);
  assert.equal(f.bufHashes.get('00000000000001b9'), '000000000000bbbb');
});

test('a short record at the base offset cannot starve the base slice', () => {
  const short = new Uint8Array(16).fill(7);
  const long = new Uint8Array(112).fill(9);
  const f1 = frameOf(bufData(0, 0xcccc, short), bufData(0, HASH_VERTS, long));
  assert.deepEqual(f1.buffers.get('00000000000001b9'), long);
  const f2 = frameOf(bufData(0, HASH_VERTS, long), bufData(0, 0xcccc, short));
  assert.deepEqual(f2.buffers.get('00000000000001b9'), long);
});

test('a buffer with no base record in frame keeps its only range as the bare key', () => {
  const f = frameOf(bufData(65536, HASH_PATTERN, PATTERN));
  assert.deepEqual(f.buffers.get('00000000000001b9'), PATTERN);
  assert.equal(f.bufHashes.get('00000000000001b9'), '000000000000aaaa');
});
