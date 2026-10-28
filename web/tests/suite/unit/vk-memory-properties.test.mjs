// web/tests/suite/unit/vk-memory-properties.test.mjs — bridge memory
// properties regression tests, campaign section 4.23 (committed-resource
// wall).
//
// vkd3d-proton requires HOST_VISIBLE | HOST_CACHED for READBACK heaps
// and for CUSTOM heaps with WRITE_BACK page property (memory.c
// vkd3d_select_memory_flags). Before this fix the bridge advertised no
// HOST_CACHED type at all, so the suballocation path's required mask was
// empty and every small READBACK / WRITE_BACK allocation failed
// E_OUTOFMEMORY (guest proof: tools/vkd3d/probe/res12.c — readback-1k
// and custom-writeback FAIL 0x8007000E while readback-4m passes through
// the direct path, which relaxes the flags). The host-only types now
// carry HOST_CACHED. These tests pin the advertised answer in source so
// a future regression fails the suite without a guest run; the guest
// proof itself is the res12 probe.

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';

function fillMemoryPropertiesRegion() {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const start = bridge.indexOf('void fillMemoryProperties(');
  assert.ok(start >= 0, 'fillMemoryProperties must exist in vk64bridge.cpp');
  const region = bridge.slice(start, start + 2000);
  assert.ok(region.includes('memoryTypeCount'), 'region must cover the type table');
  return region.replace(/\s+/g, ' ');
}

test('host-only memory types advertise HOST_CACHED (vkd3d READBACK / WRITE_BACK requirement)', () => {
  const flat = fillMemoryPropertiesRegion();
  assert.ok(
    flat.includes('VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT'),
    'the host-only arm must carry HOST_VISIBLE | HOST_COHERENT | HOST_CACHED',
  );
});

test('DEVICE_LOCAL-only type stays uncached and the advertised shape is unchanged', () => {
  const flat = fillMemoryPropertiesRegion();
  assert.ok(flat.includes('memoryHeapCount = 2'), 'two heaps');
  assert.ok(flat.includes('memoryTypeCount = 4'), 'four types');
  assert.ok(
    flat.includes('(i == 1 ? VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT :'),
    'the DEVICE_LOCAL-only arm must not gain HOST_CACHED',
  );
});
