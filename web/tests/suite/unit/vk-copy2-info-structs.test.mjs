// web/tests/suite/unit/vk-copy2-info-structs.test.mjs — bridge Vk*Info2
// copy-handler regression tests, campaign section 4.24 (readback-zeros
// wall).
//
// vkd3d-proton issues every copy through the Vulkan 1.3 "2" entry points
// (vkCmdCopyBuffer2, vkCmdCopyImageToBuffer2, vkCmdCopyBufferToImage2,
// vkCmdCopyImage2, vkCmdBlitImage2), each taking (commandBuffer,
// pInfo2*). The PE shim passes trap args verbatim, so the bridge must
// parse the Info2 struct from args.a[1]. Before this fix the bridge
// handlers read src/dst/count/regions from args.a[] as if the shim
// unpacked the struct — it never did — and the shim stubs themselves
// declared the wrong arity for four of the five functions (CopyBuffer2,
// CopyImage2, CopyBufferToImage2 declared 1, dropping the struct
// pointer entirely; CopyImageToBuffer2 declared 5, passing caller
// garbage as count/regions). The result: buffer copies were dropped in
// vkCmdCopyBuffer2 (guest proof: tools/vkd3d/probe/rb12.c BUF variant,
// "DROPPED ... guard !src || !dst || !src->mem || !dst->mem"), image
// copies were recorded with regionCount 0, and every D3D12 readback
// returned zeros with no error. These tests pin the parsed struct
// layout in source and the shim arity in exports.txt so a future
// regression fails the suite without a guest run; the guest proof
// itself is the rb12 probe (all five variants).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

function bridgeSource() {
  return readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
}

function handlerRegion(bridge, label) {
  const start = bridge.indexOf(label);
  assert.ok(start >= 0, label + ' must exist in vk64bridge.cpp');
  // The handler body starts at the opening brace after the label (this
  // skips the KHR alias case line for the "2" handlers).
  const brace = bridge.indexOf('{', start);
  const end = bridge.indexOf('\n    case ', brace);
  assert.ok(end > brace, label + ' handler must end at the next case');
  return bridge.slice(brace, end);
}

function exportsLines() {
  return readFileSync(new URL('../../../../tools/vkd3d/probe/exports.txt', import.meta.url), 'utf8')
    .split('\n').map((l) => l.trim()).filter(Boolean);
}

test('vkCmdCopyBuffer2 parses VkCopyBufferInfo2 from the trap struct pointer', () => {
  const region = handlerRegion(bridgeSource(), 'case VK64_fn_vkCmdCopyBuffer2:');
  assert.ok(region.includes('U64 pInfo2 = args.a[1]'), 'struct pointer comes from args.a[1]');
  assert.ok(region.includes('rd64(pInfo2 + 16)'), 'srcBuffer at struct offset 16');
  assert.ok(region.includes('rd64(pInfo2 + 24)'), 'dstBuffer at struct offset 24');
  assert.ok(region.includes('rd32(pInfo2 + 32)'), 'regionCount at struct offset 32');
  assert.ok(region.includes('rd64(pInfo2 + 40)'), 'pRegions at struct offset 40');
  assert.ok(!region.includes('objOf<Buffer>(args.a[1]'), 'must not look the struct pointer up as a buffer');
  assert.ok(!region.includes('objOf<Buffer>(args.a[2]'), 'must not read dstBuffer from trap args');
});

test('vkCmdCopyImageToBuffer2 parses VkCopyImageToBufferInfo2 from the trap struct pointer', () => {
  const region = handlerRegion(bridgeSource(), 'case VK64_fn_vkCmdCopyImageToBuffer2:');
  assert.ok(region.includes('U64 pInfo2 = args.a[1]'), 'struct pointer comes from args.a[1]');
  assert.ok(region.includes('rd64(pInfo2 + 16)'), 'srcImage at struct offset 16');
  assert.ok(region.includes('rd64(pInfo2 + 32)'), 'dstBuffer at struct offset 32');
  assert.ok(region.includes('rd32(pInfo2 + 40)'), 'regionCount at struct offset 40');
  assert.ok(region.includes('rd64(pInfo2 + 48)'), 'pRegions at struct offset 48');
  assert.ok(!region.includes('c.copyI2B.srcImage = args.a[1]'), 'must not record the struct pointer as the image');
});

test('vkCmdCopyBufferToImage2 parses VkCopyBufferToImageInfo2 from the trap struct pointer', () => {
  const region = handlerRegion(bridgeSource(), 'case VK64_fn_vkCmdCopyBufferToImage2:');
  assert.ok(region.includes('U64 pInfo2 = args.a[1]'), 'struct pointer comes from args.a[1]');
  assert.ok(region.includes('rd64(pInfo2 + 16)'), 'srcBuffer at struct offset 16');
  assert.ok(region.includes('rd64(pInfo2 + 24)'), 'dstImage at struct offset 24');
  assert.ok(region.includes('rd32(pInfo2 + 36)'), 'regionCount at struct offset 36');
  assert.ok(region.includes('rd64(pInfo2 + 40)'), 'pRegions at struct offset 40');
  assert.ok(!region.includes('c.copy.srcBuffer = args.a[1]'), 'must not record the struct pointer as the buffer');
});

test('vkCmdCopyImage2 parses VkCopyImageInfo2 from the trap struct pointer', () => {
  const region = handlerRegion(bridgeSource(), 'case VK64_fn_vkCmdCopyImage2:');
  assert.ok(region.includes('U64 pInfo2 = args.a[1]'), 'struct pointer comes from args.a[1]');
  assert.ok(region.includes('rd64(pInfo2 + 16)'), 'srcImage at struct offset 16');
  assert.ok(region.includes('rd64(pInfo2 + 32)'), 'dstImage at struct offset 32');
  assert.ok(region.includes('rd32(pInfo2 + 44)'), 'regionCount at struct offset 44');
  assert.ok(region.includes('rd64(pInfo2 + 48)'), 'pRegions at struct offset 48');
  assert.ok(!region.includes('objOf<Image>(args.a[1], K_IMAGE, "vkCmdCopyImage2")'), 'must not look the struct pointer up as an image');
});

test('vkCmdBlitImage2 parses VkBlitImageInfo2 from the trap struct pointer', () => {
  const region = handlerRegion(bridgeSource(), 'case VK64_fn_vkCmdBlitImage2:');
  assert.ok(region.includes('U64 pInfo2 = args.a[1]'), 'struct pointer comes from args.a[1]');
  assert.ok(region.includes('rd64(pInfo2 + 16)'), 'srcImage at struct offset 16');
  assert.ok(region.includes('rd64(pInfo2 + 32)'), 'dstImage at struct offset 32');
  assert.ok(region.includes('rd32(pInfo2 + 44)'), 'regionCount at struct offset 44');
  assert.ok(region.includes('rd64(pInfo2 + 48)'), 'pRegions at struct offset 48');
  assert.ok(region.includes('rd32(pInfo2 + 56)'), 'filter at struct offset 56');
  assert.ok(!region.includes('objOf<Image>(args.a[1], K_IMAGE, "vkCmdBlitImage2")'), 'must not look the struct pointer up as an image');
});

test('PE shim declares arity 2 for every copy-family Info2 entry point', () => {
  const lines = exportsLines();
  const arity = new Map(lines.map((l) => {
    const parts = l.split(/\s+/);
    return [parts[0], parts[2]];
  }));
  for (const name of [
    'vkCmdCopyBuffer2', 'vkCmdCopyBuffer2KHR',
    'vkCmdCopyImageToBuffer2', 'vkCmdCopyImageToBuffer2KHR',
    'vkCmdCopyBufferToImage2', 'vkCmdCopyBufferToImage2KHR',
    'vkCmdCopyImage2', 'vkCmdCopyImage2KHR',
    'vkCmdBlitImage2', 'vkCmdBlitImage2KHR',
  ]) {
    assert.equal(arity.get(name), '2', name + ' must take (commandBuffer, pInfo2*)');
  }
});

test('v1 copy handlers are untouched (still read unpacked trap args)', () => {
  const bridge = bridgeSource();
  const v1 = handlerRegion(bridge, 'case VK64_fn_vkCmdCopyBuffer:');
  assert.ok(v1.includes('objOf<Buffer>(args.a[1], K_BUFFER, "vkCmdCopyBuffer")'),
    'the v1 handler must keep reading srcBuffer from trap args');
  const v1i2b = handlerRegion(bridge, 'case VK64_fn_vkCmdCopyImageToBuffer:');
  assert.ok(v1i2b.includes('c.copyI2B.srcImage = args.a[1]'),
    'the v1 image-to-buffer handler must keep reading srcImage from trap args');
});
