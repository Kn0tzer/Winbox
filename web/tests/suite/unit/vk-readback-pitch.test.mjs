// web/tests/suite/unit/vk-readback-pitch.test.mjs — bridge readback
// row-pitch regression tests, campaign section 4.24 (readback-zeros
// wall, second stage).
//
// After the Info2 copy fix, D3D12 image data reached the readback
// buffer but packed tightly (16 bytes/row for a 4x4 RGBA8 image),
// while the guest reads rows RowPitch apart (256 bytes for small
// RGBA8 images, from the copy region's bufferRowLength). Guest proof:
// tools/vkd3d/probe/rb12.c run 2 — CLEAR and CONSTPS HEX dumps showed
// the full 4x4 image correct contiguously, yet the strided checks
// failed past row 0 (CLEAR bad=36 first pixel 4, CONSTPS bad=48
// first pixel 4, exactly the 12 pixels of rows 1-3). The bridge
// dropped the region's bufferRowLength in setupReadbacks and copied
// one tight block in completeReadbacks. These tests pin the pitch
// carried through the pending record and the per-row strided
// copy-out in source so a future regression fails the suite without
// a guest run; the guest proof itself is the rb12 probe.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

function bridgeSource() {
  return readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
}

function fnRegion(bridge, label) {
  const start = bridge.indexOf(label);
  assert.ok(start >= 0, label + ' must exist in vk64bridge.cpp');
  const end = bridge.indexOf('\nstatic ', start + 10);
  assert.ok(end > start, label + ' must end at the next function');
  return bridge.slice(start, end);
}

test('readback pending record carries the guest row pitch', () => {
  const bridge = bridgeSource();
  const structStart = bridge.indexOf('struct ReadbackPending {');
  assert.ok(structStart >= 0, 'ReadbackPending must exist');
  const structEnd = bridge.indexOf('};', structStart);
  const struct = bridge.slice(structStart, structEnd);
  assert.ok(struct.includes('pitch'), 'the pending record must carry a pitch field');
});

test('setupReadbacks derives the pitch from the copy region bufferRowLength', () => {
  const region = fnRegion(bridgeSource(), 'static bool setupReadbacks(');
  assert.ok(region.includes('pend.pitch = rb.rowLen ? rb.rowLen * 4 : rb.w * 4'),
    'pitch must be bufferRowLength texels x 4 bytes, falling back to the tight row');
});

test('completeReadbacks expands tight heap rows to the guest pitch', () => {
  const region = fnRegion(bridgeSource(), 'static void completeReadbacks()');
  assert.ok(region.includes('size_t tightRow = (size_t)pend.w * 4'),
    'the tight row width must be computed from the image width');
  assert.ok(region.includes('size_t pitch = pend.pitch ? pend.pitch : tightRow'),
    'a missing pitch must fall back to the tight row');
  assert.ok(region.includes('U64 dstVa = dstBase + (U64)y * pitch'),
    'each row must land at dstBufOff + y*pitch in the guest buffer');
  assert.ok(region.includes('writeGuestBytes(dstVa, (U8*)(uintptr_t)pend.heapPtr + (size_t)y * tightRow, n)'),
    'each guest row must come from the matching tight heap row');
  assert.ok(!region.includes('writeGuestBytes(dstVa, (U8*)(uintptr_t)pend.heapPtr, pxBytes)'),
    'the old single tight-block copy must be gone');
});

test('setupReadbacks seeds the heap from guest memory for copy-only textures', () => {
  const region = fnRegion(bridgeSource(), 'static bool setupReadbacks(');
  assert.ok(region.includes('readback-prefill'),
    'the pre-fill must look the source image up by its bridge handle');
  assert.ok(region.includes('imageSubresourceOffset(*pim, 0, 0)'),
    'the pre-fill must start at the mip-0 layer-0 subresource');
  assert.ok(region.includes('readGuestBytes(heap + (size_t)py * rb.w * 4, sOff, (size_t)rb.w * 4)'),
    'each heap row must come from the matching guest-memory row');
});

test('pre-fill is best-effort and bounded (page path unchanged on failure)', () => {
  const region = fnRegion(bridgeSource(), 'static bool setupReadbacks(');
  assert.ok(region.includes('rb.ox >= 0 && rb.oy >= 0'),
    'negative copy origins must not seed');
  assert.ok(region.includes('if (sOff + (U64)rb.w * 4 > pmm->va + pmm->size) break;'),
    'a row past the allocation must stop the seed, not overrun');
});
