// web/tests/suite/unit/vk-properties2-32.test.mjs — 32-bit Properties2
// walker arms regression tests (lane-d3d12-32: first 32-bit D3D12 guest
// run).
//
// vk32_walkProperties2 originally had no TRANSFORM_FEEDBACK or VULKAN_1_3
// properties arms, so both structs crossed the bridge untouched (zeroed),
// and vkd3d-proton's device-creation gates rejected the adapter:
// "Lacking support for single texel alignment" (the 1.3 arm's
// SingleTexelAlignment bools) -> D3D12CreateDevice E_INVALIDARG, with the
// transformFeedbackQueries gate (TF arm) stacked directly behind it.
// The 64-bit walker has served both arms all along; these tests pin the
// 32-bit arms in source so a regression fails the suite without a guest
// run. The guest proof itself is web/tests/d3d12-32-system.mjs.
//
// The 1.3 arm intentionally writes RAW guest offsets, not the vk32_w*f
// hostOff-8 rule: the struct's tail interleaves VkBool32 and VkDeviceSize
// and the i386 layout the bridge walks packs
// uniformTexelBufferOffsetAlignmentBytes at 180 where the host rule would
// write 184. The offsets below were compile-verified with
// i686-linux-gnu-gcc against the canonical field list (sizeof 200).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

function walkerRegion() {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const start = bridge.indexOf('void vk32_walkProperties2(');
  assert.ok(start >= 0, 'vk32_walkProperties2 must exist in vk64bridge.cpp');
  const end = bridge.indexOf('vk32_writeRequirements', start);
  assert.ok(end > start, 'walker region must terminate at vk32_writeRequirements');
  return bridge.slice(start, end).replace(/\s+/g, ' ');
}

test('32-bit walker serves the transform feedback properties arm with transformFeedbackQueries true', () => {
  const flat = walkerRegion();
  assert.ok(
    flat.includes('case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TRANSFORM_FEEDBACK_PROPERTIES_EXT:'),
    'the TF properties case must exist in vk32_walkProperties2',
  );
  assert.ok(
    flat.includes('transformFeedbackQueries), (U32)VK_TRUE'),
    'transformFeedbackQueries must be VK_TRUE (vkd3d-proton device gate)',
  );
  assert.ok(
    flat.includes('transformFeedbackDraw), (U32)VK_FALSE'),
    'transformFeedbackDraw must stay VK_FALSE (stream-out is not served)',
  );
});

test('32-bit walker serves the Vulkan 1.3 properties arm at the verified raw guest offsets', () => {
  const flat = walkerRegion();
  assert.ok(
    flat.includes('case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES:'),
    'the Vulkan 1.3 properties case must exist in vk32_walkProperties2',
  );
  assert.ok(
    flat.includes('wr32_32(cur + 176, (U32)VK_TRUE); // storageTexelBufferOffsetSingleTexelAlignment'),
    'storage single-texel alignment must be VK_TRUE at guest offset 176',
  );
  assert.ok(
    flat.includes('wr32_32(cur + 188, (U32)VK_TRUE); // uniformTexelBufferOffsetSingleTexelAlignment'),
    'uniform single-texel alignment must be VK_TRUE at guest offset 188',
  );
  assert.ok(
    flat.includes('wr64_32(cur + 192, 1ull << 30); // maxBufferSize (as the 64-bit arm)'),
    'maxBufferSize must be 1<<30 at guest offset 192',
  );
  assert.ok(
    flat.includes('wr32_32(cur + 8, 32); // minSubgroupSize') &&
    flat.includes('wr32_32(cur + 12, 32); // maxSubgroupSize'),
    'subgroup size range must be 32..32 at guest offsets 8/12',
  );
});

test('the 1.3 arm documents why it must not use the hostOff-8 field writers', () => {
  const flat = walkerRegion();
  assert.ok(
    flat.includes('NOT the vk32_w*f hostOff-8 rule'),
    'the raw-offset rationale comment must stay with the arm',
  );
});

// ---------------------------------------------------------------------------
// 32-bit vkCreateComputePipelines (same lane): the handler validated and
// wrote trap arg a[4] -- pAllocator, NULL for vkd3d -- instead of a[5]
// (pPipelines), so every creation returned VK_ERROR_INITIALIZATION_FAILED
// and 32-bit D3D12CreateDevice died in vkd3d_clear_uav_ops_init (vr -3)
// even though device caps had passed. The libvk32 shim's arg order is
// pinned by vk-abi-layout for the enum; this pins the handler's use of it.

function computePipelineRegion() {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const fn = bridge.indexOf('U64 vk32Bridge(');
  assert.ok(fn >= 0, 'vk32Bridge must exist in vk64bridge.cpp');
  const start = bridge.indexOf('case VK64_fn_vkCreateComputePipelines:', fn);
  assert.ok(start >= 0, 'the 32-bit vkCreateComputePipelines case must exist');
  return bridge.slice(start, start + 1600).replace(/\s+/g, ' ');
}

test('32-bit vkCreateComputePipelines writes pipeline handles to a[5] (pPipelines), not a[4] (pAllocator)', () => {
  const flat = computePipelineRegion();
  assert.ok(
    flat.includes('if (!args.a[5]) return VK_ERROR_INITIALIZATION_FAILED;'),
    'creation must validate the pPipelines pointer a[5]',
  );
  assert.ok(
    flat.includes('U32 outAddr = (U32)args.a[5];'),
    'pipeline handles must be written through a[5]',
  );
  assert.ok(
    !flat.includes('U32 outAddr = (U32)args.a[4];'),
    'the allocator pointer a[4] must never receive pipeline handles',
  );
});
