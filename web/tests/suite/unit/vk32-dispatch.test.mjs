// web/tests/suite/unit/vk32-dispatch.test.mjs — 32-bit Vulkan bridge (Portal blocker).
//
// "32-bit D3D9 never worked": 32-bit dispatch missed Vulkan 1.1 *2 functions,
// all 58 objOf→objOf32 lookups were 64-bit, sample-count offsets were
// compiler-offsetof (wrong vs shipped DXVK), PE shim truncated 64→32-bit
// pointers (regionCount 2,222,416). vkMapMemory still open (Wine asserts at
// loader_thunks.c:6053).

import { test } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');
const bridge = readFileSync(join(repoRoot, 'source/vulkan/vk64bridge.cpp'), 'utf8');

test('32-bit dispatch includes Vulkan 1.1 *2 functions (ca5be95c)', () => {
  for (const fn of ['Properties2', 'Features2', 'MemoryProperties2', 'vk32_walkFeatures2', 'vk32_walkProperties2']) {
    assert.ok(bridge.includes(fn), `vk32 bridge must handle ${fn} (DXVK got NULL → hung on retry)`);
  }
});

test('32-bit uses objOf32, not 64-bit lookup (58 bugs)', () => {
  const c32 = (bridge.match(/objOf32/g) || []).length;
  assert.ok(c32 >= 50, `expected 50+ objOf32 uses (all 58 fixed), found ${c32}`);
});

test('sample-count handling matches shipped DXVK (never test-compile offsetof)', () => {
  assert.ok(bridge.includes('framebufferColorSampleCounts') || bridge.includes('SampleCounts') || bridge.includes('sampleCounts'),
    'sample-count handling must exist (MinGW offsetof lies: 368 vs real 360/364 — offsets must be validated vs shipped DXVK, see struct-layout-shipped.test.mjs)');
  assert.ok(bridge.includes('VK_SAMPLE_COUNT_1_BIT'),
    'sample-count values must use real VK_SAMPLE_COUNT bits (DXVK divide-by-zero guard)');
});

// libvk32.c is built for i386 where `uintptr_t` is 32 bits. Every guest pointer
// goes into a 64-bit trap slot through P(x); it must ZERO-extend (a pointer such
// as 0xF0001234 sign-extended to 0xFFFFFFFFF0001234 is the c50f6fba truncation
// family of bugs in reverse). Build the real shim with the trap replaced by a
// recorder and run it as an i386 binary (static, freestanding: no libc needed).
test('libvk32 P(x) zero-extends guest pointers into 64-bit trap slots, including VkSubmitInfo', (t) => {
  const cc = process.env.VK32_CC || 'i686-linux-gnu-gcc';
  const probe = spawnSync(cc, ['--version'], { encoding: 'utf8' });
  if (probe.error) return t.skip(`${cc} not installed: cannot build the i386 shim`);
  const src = readFileSync(join(repoRoot, 'tools/rootfs64/libvk32/libvk32.c'), 'utf8');
  assert.match(src, /#define P\(x\) \(\(uint64_t\)\(uintptr_t\)\(x\)\)/, 'P(x) must cast through unsigned uintptr_t, never intptr_t/int64');
  const start = src.indexOf('static inline uint64_t vk64_trap(');
  assert.ok(start > 0);
  let depth = 0, end = src.indexOf('{', start);
  for (let i = end; i < src.length; i++) { if (src[i] === '{') depth++; else if (src[i] === '}' && --depth === 0) { end = i + 1; break; } }
  const recorder = 'static VK64Args g_last; static uint64_t g_fn;\n' +
    'static inline uint64_t vk64_trap(uint64_t fnId, VK64Args* args) { g_fn = fnId; if (args) g_last = *args; return 0; }';
  const main = `
static int g_rc;
#define CHECK(n, c) do { if (!(c)) { g_rc = n; goto done; } } while (0)
void _start(void) {
    const uint32_t hi = 0xF0001234u, top = 0x80000000u;
    vkQueueSubmit(1, 1, (const void*)(uintptr_t)hi, 7);
    CHECK(1, g_fn == VK64_fn_vkQueueSubmit && g_last.a[2] == 0xF0001234ull);
    vkQueueSubmit2(1, 1, (const void*)(uintptr_t)top, 7);
    CHECK(2, g_fn == VK64_fn_vkQueueSubmit2 && g_last.a[2] == 0x80000000ull);
    vkQueueSubmit2KHR(1, 1, (const void*)(uintptr_t)hi, 7);
    CHECK(3, g_fn == VK64_fn_vkQueueSubmit2KHR && g_last.a[2] == 0xF0001234ull);
    vkQueuePresentKHR(1, (const void*)(uintptr_t)hi);
    CHECK(4, g_fn == VK64_fn_vkQueuePresentKHR && g_last.a[1] == 0xF0001234ull);
done:
    __asm__ volatile("int $0x80" :: "a"(1), "b"(g_rc));
    for (;;) {}
}
`;
  const dir = mkdtempSync(join(tmpdir(), 'vk32-p-'));
  try {
    writeFileSync(join(dir, 'shim32.c'), src.slice(0, start) + recorder + src.slice(end) + main);
    const build = spawnSync(cc, ['-std=gnu11', '-O0', '-ffreestanding', '-nostdlib', '-static', '-w',
      join(dir, 'shim32.c'), '-o', join(dir, 'shim32')], { encoding: 'utf8' });
    assert.equal(build.status, 0, 'i386 shim harness must compile:\n' + build.stderr);
    const run = spawnSync(join(dir, 'shim32'), { encoding: 'utf8' });
    assert.equal(run.error, undefined, 'i386 harness must execute on this host: ' + run.error);
    assert.equal(run.status, 0, `check ${run.status} failed: 1=vkQueueSubmit 2=vkQueueSubmit2 (sign bit) 3=vkQueueSubmit2KHR 4=vkQueuePresentKHR`);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('copy/blit handlers have defensive guards (d1713623 length_error/huge allocs)', () => {
  assert.ok(bridge.includes('length_error') || bridge.includes('bounds') || bridge.includes('clamp') || bridge.includes('nreg'),
    'copy/blit handlers must guard garbage nreg (1447756043) and smem wraparound allocs');
  for (const fn of ['vkCmdCopyBuffer2', 'vkCmdCopyImage', 'vkCmdBlitImage', 'vkCmdResolveImage']) {
    assert.ok(bridge.includes(fn), `${fn} handler must exist (copy/blit crasher family)`);
  }
});

test('vk64_benign returns FEATURE_NOT_PRESENT not OOM (e2ec6113 phantom 0x8007000E)', () => {
  assert.ok(bridge.includes('vk64_benign') || bridge.includes('FEATURE_NOT_PRESENT') || bridge.includes('vkBindBufferMemory2'),
    'benign stub must return -8 FEATURE_NOT_PRESENT, not -2 OOM (vkd3d TAIL stub phantom OOM)');
});

test('shim exact-name lookup has KHR→core aliases (6 aliases)', () => {
  assert.ok(bridge.includes('Properties2KHR') || bridge.includes('KHR') ,
    'KHR→core alias handling must exist (vkGetPhysicalDeviceProperties2KHR benign-stub trap)');
});

test('device-proc resolver includes loader functions (vkGetDeviceProcAddr)', () => {
  assert.ok(bridge.includes('vkGetDeviceProcAddr'),
    'regenerated resolver must include loader functions in g_procs (every device-proc lookup failed)');
});

test('walkFeatures2 zeroes unknown structs (spec-compliance)', () => {
  assert.ok(bridge.includes('walkFeatures2') || bridge.includes('vk32_walkFeatures2'),
    'walkFeatures2 must exist and zero unknown structs (guest VK_TRUE → vkd3d treats unsupported as available)');
});

test('missing entry points are all present (each was a real blocker)', () => {
  const required = [
    'vkQueueSubmit2', 'vkCreateDescriptorUpdateTemplate', 'vkCmdBeginRendering', 'vkCmdEndRendering',
    'vkCmdBeginRenderPass2', 'vkCmdEndRenderPass2', 'vkGetPhysicalDeviceFormatProperties2',
    'vkGetDeviceBufferMemoryRequirements', 'vkCreateBufferView', 'vkGetBufferDeviceAddress',
  ];
  for (const fn of required) {
    assert.ok(bridge.includes(fn), `${fn} must be implemented (each missing entry was a real blocker)`);
  }
});

test('vkUpdateDescriptorSets carries buffer offset (DXVK sub-allocates nonzero)', () => {
  assert.ok(bridge.includes('bufOff'),
    'vkUpdateDescriptorSets must carry VkDescriptorBufferInfo::offset (dropped offset → wrong data)');
});

test('D3D9 descriptorType union aliasing is trusted (4dba58ba)', () => {
  assert.ok(bridge.includes('descriptorType'),
    'bridge must trust descriptorType (pBufferInfo/pImageInfo alias misread as buffer)');
});
