import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

const root = new URL('../../../../', import.meta.url).pathname;

for (const [arch, compiler, emulator] of [
  ['32', 'i686-linux-gnu-gcc', 'qemu-i386'],
  ['64', 'x86_64-linux-gnu-gcc', 'qemu-x86_64'],
]) test(`Vulkan ${arch}-bit resolve2 core and KHR preserve every region across the real shim`, () => {
  const source = readFileSync(join(root, `tools/rootfs64/libvk${arch}/libvk${arch}.c`), 'utf8');
  const rewritten = source.replace(/static inline uint64_t vk64_trap\(uint64_t fnId, VK64Args\* args\) \{[\s\S]*?\n\}/,
    'extern uint64_t test_trap(uint64_t, VK64Args*);\nstatic inline uint64_t vk64_trap(uint64_t fnId, VK64Args* args) { return test_trap(fnId, args); }');
  assert.notEqual(rewritten, source, 'replace only the private syscall boundary');
  const dir = mkdtempSync(join(tmpdir(), 'vk-resolve2-'));
  try {
    writeFileSync(join(dir, 'shim.c'), rewritten);
    writeFileSync(join(dir, 'check.c'), `
#define VK_NO_PROTOTYPES
#include "vulkan_core.h"
#include <assert.h>
#include <stdint.h>
#include <string.h>
extern PFN_vkVoidFunction vkGetDeviceProcAddr(VkDevice, const char*);
typedef struct { uint64_t a[16]; } Args;
static unsigned count;
static Args calls[2];
static VkImageResolve regions[2];
uint64_t test_trap(uint64_t fn, Args* args) {
  if (fn == 2 || fn == 0) return 0; // lookup tracing and shim witness
  assert(fn == 826 || fn == 827); // the existing, append-only resolve ABI
  assert(count < 2 && args->a[5] == 1);
  calls[count] = *args;
  memcpy(&regions[count++], (void*)(uintptr_t)args->a[6], sizeof(VkImageResolve));
  return 0;
}
int main(void) {
  VkImageResolve2 input[2] = {
    { .sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2,
      .srcSubresource = {1, 2, 3, 2}, .srcOffset = {4, 5, 0},
      .dstSubresource = {1, 6, 7, 2}, .dstOffset = {8, 9, 0}, .extent = {10, 11, 1} },
    { .sType = VK_STRUCTURE_TYPE_IMAGE_RESOLVE_2,
      .srcSubresource = {1, 0, 0, 1}, .srcOffset = {12, 13, 0},
      .dstSubresource = {1, 0, 0, 1}, .dstOffset = {14, 15, 0}, .extent = {16, 17, 1} }
  };
  VkResolveImageInfo2 info = { .sType = VK_STRUCTURE_TYPE_RESOLVE_IMAGE_INFO_2,
    .srcImage = (VkImage)0x123456781234ULL, .srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
    .dstImage = (VkImage)0x876543215678ULL, .dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
    .regionCount = 2, .pRegions = input };
  const char* names[] = {"vkCmdResolveImage2", "vkCmdResolveImage2KHR"};
  for (unsigned alias = 0; alias < 2; ++alias) {
    count = 0;
    PFN_vkCmdResolveImage2 resolve = (PFN_vkCmdResolveImage2)vkGetDeviceProcAddr((VkDevice)(uintptr_t)1, names[alias]);
    assert(resolve);
    resolve((VkCommandBuffer)(uintptr_t)0x4321, &info);
    assert(count == 2);
    for (unsigned i = 0; i < count; ++i) {
      assert(calls[i].a[0] == 0x4321 && calls[i].a[1] == 0x123456781234ULL && calls[i].a[3] == 0x876543215678ULL);
      assert(calls[i].a[2] == info.srcImageLayout && calls[i].a[4] == info.dstImageLayout);
      assert(!memcmp(&regions[i].srcSubresource, &input[i].srcSubresource, sizeof(VkImageSubresourceLayers)));
      assert(!memcmp(&regions[i].srcOffset, &input[i].srcOffset, sizeof(VkOffset3D)));
      assert(!memcmp(&regions[i].dstSubresource, &input[i].dstSubresource, sizeof(VkImageSubresourceLayers)));
      assert(!memcmp(&regions[i].dstOffset, &input[i].dstOffset, sizeof(VkOffset3D)));
      assert(!memcmp(&regions[i].extent, &input[i].extent, sizeof(VkExtent3D)));
    }
    count = 0; info.regionCount = 0; resolve((VkCommandBuffer)(uintptr_t)0x4321, &info);
    assert(count == 0); info.regionCount = 2;
  }
}
`);
    execFileSync(compiler, ['-std=c11', '-O0', '-fno-builtin', '-c', join(dir, 'shim.c'), '-o', join(dir, 'shim.o')]);
    execFileSync(compiler, ['-std=c11', '-static', '-I', join(root, 'source/vulkan/vk'), join(dir, 'check.c'), join(dir, 'shim.o'), '-o', join(dir, 'check')]);
    execFileSync(emulator, [join(dir, 'check')], { timeout: 10000 });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
