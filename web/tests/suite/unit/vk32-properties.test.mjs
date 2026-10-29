import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';
import { execFileSync } from 'node:child_process';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));

test('32-bit Vulkan properties use the Linux driver ABI without overwriting adjacent memory', () => {
  const source = readFileSync(join(root, 'source/vulkan/vk64bridge.cpp'), 'utf8');
  const writer = source.match(/inline void vk32_writeLimits\(U32 lim\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(writer, 'limits writer exists');
  const shared = source.match(/template <class Limits>\nvoid fillLimits\(Limits& L\) \{[\s\S]*?\n\}/)?.[0] || '';
  const indirectLimit = source.match(/const U32 VK64_MAX_INDIRECT_DRAWS = \d+;/)?.[0];
  assert.ok(indirectLimit, 'indirect draw limit exists');
  const maxSets = source.match(/const U32 VK64_MAX_SETS = [^;]+;/)?.[0];
  assert.ok(maxSets, 'max descriptor sets constant exists (vkd3d disposition: maxBoundDescriptorSets = VK64_MAX_SETS)');
  const requirements = source.match(/inline void vk32_writeRequirements\(U32 out, U64 size\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(requirements, 'requirements writer exists');
  const dir = mkdtempSync(join(tmpdir(), 'vk32-properties-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstring>
#include <cstdint>
namespace vk32abi {
#include "${join(root, 'source/vulkan/vk32_guest.h')}"
}
using U32 = uint32_t;
using U64 = uint64_t;
using namespace vk32abi;
unsigned char memory[1024];
void wr32_32(U32 a, U32 v) { memcpy(memory + a, &v, 4); }
void wr64_32(U32 a, U64 v) { memcpy(memory + a, &v, 8); }
U32 rd32_32(U32 a) { U32 v; memcpy(&v, memory + a, 4); return v; }
template<class T> void writeStruct32(U32 a, const T& v) { memcpy(memory + a, &v, sizeof(v)); }
${indirectLimit}
${maxSets}
${shared}
${writer}
${requirements}
int main() {
  static_assert(offsetof(VkPhysicalDeviceProperties, limits) == 292);
  static_assert(sizeof(VkPhysicalDeviceLimits) == 488);
  static_assert(sizeof(VkSubmitInfo2) == 36);
  static_assert(offsetof(VkSubmitInfo2, pCommandBufferInfos) == 24);
  static_assert(sizeof(VkCommandBufferSubmitInfo) == 16);
  static_assert(offsetof(VkCommandBufferSubmitInfo, commandBuffer) == 8);
  static_assert(sizeof(VkSemaphoreSubmitInfo) == 36);
  static_assert(offsetof(VkSemaphoreSubmitInfo, stageMask) == 24);
  memset(memory, 0xa5, sizeof(memory));
  vk32_writeLimits(32);
  VkPhysicalDeviceLimits l;
  memcpy(&l, memory + 32, sizeof(l));
  assert(l.bufferImageGranularity == 1);
  assert(l.minMemoryMapAlignment == 4096);
  assert(l.minUniformBufferOffsetAlignment == 256);
  assert(l.minStorageBufferOffsetAlignment == 256);
  assert(l.framebufferColorSampleCounts & 1);
  assert(l.framebufferDepthSampleCounts & 1);
  assert(l.nonCoherentAtomSize == 64);
  for (unsigned i = 0; i < 32; i++) assert(memory[i] == 0xa5);
  for (unsigned i = 32 + sizeof(l); i < sizeof(memory); i++) assert(memory[i] == 0xa5);
  VkMemoryRequirements2 r = {};
  r.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2;
  r.pNext = 700;
  writeStruct32(600, r);
  wr32_32(700, VK_STRUCTURE_TYPE_MEMORY_DEDICATED_REQUIREMENTS);
  wr32_32(704, 0);
  vk32_writeRequirements(600, 65535);
  memcpy(&r, memory + 600, sizeof(r));
  assert(r.pNext == 700);
  assert(r.memoryRequirements.size == 65536);
  assert(r.memoryRequirements.alignment == 256);
  assert(r.memoryRequirements.memoryTypeBits == 1);
  assert(rd32_32(708) == 0);
  assert(rd32_32(712) == 0);
}
`);
    execFileSync('c++', ['-std=c++17', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
    assert.match(source, /vk32_writeLimits\(\w+ \+ offsetof\(vk32abi::VkPhysicalDeviceProperties, limits\)\)/);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
