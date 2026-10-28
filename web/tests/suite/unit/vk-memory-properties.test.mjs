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

// ---------------------------------------------------------------------------
// 32-bit path (campaign section 4.27, mem32 lane): the 32-bit handler in
// vk32Bridge hand-writes its own answer and never calls
// fillMemoryProperties. Its single advertised type is
// DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT (0x0007) -- no HOST_CACHED
// anywhere. That is NOT the 64-bit wall, because the 32-bit consumers are
// DXVK (D3D9/D3D11, vendored at tools/dxvk/src), and DXVK's allocator
// treats HOST_CACHED as an OPTIONAL flag: DxvkMemoryAllocator::alloc
// retries with DEVICE_LOCAL | HOST_CACHED stripped when no type satisfies
// the full request, so staging / CPU-read allocations (which do request
// HOST_CACHED) fall back to HOST_VISIBLE | HOST_COHERENT and succeed.
// vkd3d-proton, by contrast, has no such strip -- its required mask is
// hard, which is why the identical advertisement gap was a wall on the
// 64-bit path and is a NOT-A-GAP here. These tests pin both halves of
// that verdict: the 32-bit advertised answer, and the allocator premise
// it rests on. If a future DXVK vendoring removes the optional strip,
// the second test fails and this verdict must be re-reviewed.
// ---------------------------------------------------------------------------

function vk32MemPropsBlocks() {
  const bridge = readFileSync(new URL('../../../../source/vulkan/vk64bridge.cpp', import.meta.url), 'utf8');
  const vk32 = bridge.slice(bridge.indexOf('U64 vk32Bridge('));
  assert.ok(vk32.length > 0, 'vk32Bridge must exist in vk64bridge.cpp');
  const block = (label) => {
    const start = vk32.indexOf(label);
    assert.ok(start >= 0, `${label} must exist in vk32Bridge`);
    const rest = vk32.slice(start);
    // The block ends at the next case label that follows a statement --
    // stacked labels (Properties2 / Properties2KHR share one body) have no
    // ';' between them and must not truncate the block.
    let end = rest.length;
    let idx = rest.indexOf('case VK64_fn_', 10);
    while (idx >= 0) {
      if (rest.slice(0, idx).includes(';')) { end = idx; break; }
      idx = rest.indexOf('case VK64_fn_', idx + 10);
    }
    return rest.slice(0, end).replace(/\s+/g, ' ');
  };
  return {
    v1: block('case VK64_fn_vkGetPhysicalDeviceMemoryProperties:'),
    v2: block('case VK64_fn_vkGetPhysicalDeviceMemoryProperties2:'),
  };
}

test('32-bit memory properties: single type is DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT, one 4 GiB device-local heap', () => {
  const { v1, v2 } = vk32MemPropsBlocks();
  for (const [name, b] of [['v1', v1], ['v2', v2]]) {
    assert.ok(b.includes('wr32_32(dst + 0, 1);'), `${name}: memoryTypeCount must be 1`);
    assert.ok(b.includes('wr32_32(dst + 4 + 0, 0x0007);'), `${name}: type 0 propertyFlags must be 0x0007 (DEVICE_LOCAL | HOST_VISIBLE | HOST_COHERENT)`);
    assert.ok(b.includes('wr32_32(dst + 4 + 4, 0);'), `${name}: type 0 heapIndex must be 0`);
    assert.ok(b.includes('wr32_32(dst + heapOff, 1);'), `${name}: memoryHeapCount must be 1`);
    assert.ok(b.includes('wr64_32(dst + heapOff + 4, 0x100000000ULL);'), `${name}: heap 0 size must be 4 GiB`);
    assert.ok(b.includes('wr32_32(dst + heapOff + 4 + 8, 0x0001);'), `${name}: heap 0 flags must be DEVICE_LOCAL`);
  }
});

test('32-bit verdict premise: vendored DXVK allocator treats HOST_CACHED as optional (strip-and-retry)', () => {
  const dxvk = readFileSync(new URL('../../../../tools/dxvk/src/src/dxvk/dxvk_memory.cpp', import.meta.url), 'utf8').replace(/\s+/g, ' ');
  assert.ok(
    dxvk.includes('const VkMemoryPropertyFlags optionalFlags = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;'),
    'DxvkMemoryAllocator::alloc must classify HOST_CACHED as an optional flag',
  );
  assert.ok(
    dxvk.includes('info.flags &= ~optionalFlags;'),
    'DxvkMemoryAllocator::alloc must retry with the optional flags stripped',
  );
});
