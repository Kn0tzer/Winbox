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
// The 1.3 arm must not use the vk32_w*f hostOff-8 rule: the struct's
// tail interleaves VkBool32 and VkDeviceSize and the i386 layout the
// bridge walks packs uniformTexelBufferOffsetAlignmentBytes at 180
// where the host rule would write 184. As merged in section 4.40 the
// arm wrote the raw guest offsets directly (compile-verified with
// i686-linux-gnu-gcc against the canonical field list, sizeof 200);
// the prop32 merge (section 4.55) reconciled the arm to the
// packed-guest-struct discipline: it now writes through
// vk32_g32f/vk32_g64f taking offsetof in the pack(4)
// Vk32Vulkan13Properties mirror, whose static_asserts pin the same
// verified offsets (8/12/176/188/192, sizeof 200). The tests below pin
// the composed form: same values, same offsets, no hostOff-8 writers.

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

function mirrorRegion() {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const start = bridge.indexOf('// vk32_guest.h has no VkPhysicalDeviceVulkan13Properties');
  assert.ok(start >= 0, 'the Vk32Vulkan13Properties mirror comment must exist in vk64bridge.cpp');
  const end = bridge.indexOf('32-bit 1.3 properties mirror offsets', start);
  assert.ok(end > start, 'mirror region must terminate at the offsets static_assert');
  return bridge.slice(start, end).replace(/\s+/g, ' ');
}

test('32-bit walker serves the Vulkan 1.3 properties arm at the verified guest offsets (packed-mirror writers)', () => {
  const flat = walkerRegion();
  assert.ok(
    flat.includes('case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES:'),
    'the Vulkan 1.3 properties case must exist in vk32_walkProperties2',
  );
  // The arm serves the same five fields with the same values as the
  // 64-bit walker, through the guest-offset writers (the prop32 form;
  // section 4.55 reconciliation of the section 4.40 raw-offset arm).
  assert.ok(
    flat.includes('vk32_g32f(cur, offsetof(Vk32Vulkan13Properties, minSubgroupSize), 32);') &&
    flat.includes('vk32_g32f(cur, offsetof(Vk32Vulkan13Properties, maxSubgroupSize), 32);'),
    'subgroup size range must be 32..32, written via the guest-offset writer',
  );
  assert.ok(
    flat.includes('vk32_g32f(cur, offsetof(Vk32Vulkan13Properties, storageTexelBufferOffsetSingleTexelAlignment), (U32)VK_TRUE);'),
    'storage single-texel alignment must be VK_TRUE via the guest-offset writer',
  );
  assert.ok(
    flat.includes('vk32_g32f(cur, offsetof(Vk32Vulkan13Properties, uniformTexelBufferOffsetSingleTexelAlignment), (U32)VK_TRUE);'),
    'uniform single-texel alignment must be VK_TRUE via the guest-offset writer',
  );
  assert.ok(
    flat.includes('vk32_g64f(cur, offsetof(Vk32Vulkan13Properties, maxBufferSize), 1ull << 30);'),
    'maxBufferSize must be 1<<30 via the guest-offset writer',
  );
  // The verified guest offsets themselves (compile-verified against the
  // canonical field list) are pinned by the mirror's static_asserts.
  const mirror = mirrorRegion();
  assert.ok(
    mirror.includes('offsetof(Vk32Vulkan13Properties, minSubgroupSize) == 8') &&
    mirror.includes('offsetof(Vk32Vulkan13Properties, storageTexelBufferOffsetSingleTexelAlignment) == 176') &&
    mirror.includes('offsetof(Vk32Vulkan13Properties, uniformTexelBufferOffsetSingleTexelAlignment) == 188') &&
    mirror.includes('offsetof(Vk32Vulkan13Properties, maxBufferSize) == 192'),
    'the mirror static_asserts must pin the verified guest offsets 8/176/188/192',
  );
  assert.ok(
    mirror.includes('sizeof(Vk32Vulkan13Properties) == 200'),
    'the mirror static_assert must pin sizeof 200',
  );
  // The hostOff-8 writers must not serve this struct: their rule
  // diverges past the first uint64_t in it.
  const armStart = flat.indexOf('case VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_PROPERTIES:');
  const armEnd = flat.indexOf('case VK_STRUCTURE_TYPE', armStart + 1);
  const arm = flat.slice(armStart, armEnd);
  assert.ok(
    !arm.includes('vk32_w32f') && !arm.includes('vk32_w64f'),
    'the 1.3 arm must not use the vk32_w*f hostOff-8 field writers',
  );
});

test('the 1.3 arm documents why it must not use the hostOff-8 field writers', () => {
  const flat = walkerRegion();
  assert.ok(
    flat.includes('offsets come from the packed Vk32 mirror -- host') &&
    flat.includes('offsetof - 8 diverges past the first uint64_t here'),
    'the arm comment must document why host offsetof - 8 diverges for this struct',
  );
  const mirror = mirrorRegion();
  assert.ok(
    mirror.includes('host offsetof(maxBufferSize) - 8 = 200 would write') &&
    mirror.includes('8 bytes past the end of the 200-byte guest struct') &&
    mirror.includes('never host offsetof - 8, for every field of the 1.3 arm'),
    'the mirror comment must document the hostOff-8 divergence and the guest-offset discipline',
  );
});

// ---------------------------------------------------------------------------
// 32-bit vkCreateComputePipelines (same lane): the handler validated and
// wrote trap arg a[4] -- pAllocator, NULL for vkd3d -- instead of a[5]
// (pPipelines), so every creation returned VK_ERROR_INITIALIZATION_FAILED
// and 32-bit D3D12CreateDevice died in vkd3d_clear_uav_ops_init (vr -3)
// even though device caps had passed. The libvk32 shim's arg order is
// pinned by vk-abi-layout for the enum; this pins the handler's use of it.
// Section 4.59 replaced the stub handler with the full recording mirror
// (stage module, layout, specialization, R15 census); the a[5] discipline
// is unchanged and the pins below follow the handler's current text.

function computePipelineRegion() {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const fn = bridge.indexOf('U64 vk32Bridge(');
  assert.ok(fn >= 0, 'vk32Bridge must exist in vk64bridge.cpp');
  const start = bridge.indexOf('case VK64_fn_vkCreateComputePipelines:', fn);
  assert.ok(start >= 0, 'the 32-bit vkCreateComputePipelines case must exist');
  const end = bridge.indexOf('case VK64_fn_', start + 10);
  assert.ok(end > start, 'the next case must bound the region');
  return bridge.slice(start, end).replace(/\s+/g, ' ');
}

test('32-bit vkCreateComputePipelines writes pipeline handles to a[5] (pPipelines), not a[4] (pAllocator)', () => {
  const flat = computePipelineRegion();
  assert.ok(
    flat.includes('!args.a[5]) return VK_ERROR_INITIALIZATION_FAILED;'),
    'creation must validate the pPipelines pointer a[5]',
  );
  assert.ok(
    flat.includes('wr64_32((U32)args.a[5] + i * 8, id);'),
    'pipeline handles must be written through a[5]',
  );
  assert.ok(
    !flat.includes('U32 outAddr = (U32)args.a[4];'),
    'the allocator pointer a[4] must never receive pipeline handles',
  );
  assert.ok(
    !flat.includes('wr64_32((U32)args.a[4]'),
    'no write of any form may go through the allocator pointer a[4]',
  );
});
