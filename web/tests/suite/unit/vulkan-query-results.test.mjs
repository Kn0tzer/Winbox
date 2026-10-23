import test from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import { join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(fileURLToPath(new URL('../../../../', import.meta.url)));
const tempRoot = '/tmp';

test('native Vulkan query result encoding handles widths, availability, flags and timestamps', () => {
  const dir = mkdtempSync(join(tempRoot, 'query-results-'));
  try {
    const source = join(dir, 'query-results.cpp');
    const binary = join(dir, 'query-results');
    writeFileSync(source, `
#include <cassert>
#include <cstdint>
#include "vk_query_results.h"

int main() {
  using namespace vkquery;
  for (uint32_t flags = 0; flags < 16; ++flags) {
    const size_t width = (flags & VKQUERY_64_BIT) ? 8 : 4;
    const uint64_t stride = width * ((flags & VKQUERY_WITH_AVAILABILITY) ? 2 : 1);
    const auto result = vkquery::encodeResults(VKQUERY_OCCLUSION, 0, 2, 2,
        stride * 2, stride, flags, 1.0, 0);
    assert(result.complete && result.bytes.size() == stride * 2);
    for (size_t i = 0; i < 2; ++i) {
      assert(vkquery::readUnsigned(result.bytes, i * stride, width) == 1);
      if (flags & VKQUERY_WITH_AVAILABILITY)
        assert(vkquery::readUnsigned(result.bytes, i * stride + width, width) == 1);
    }
  }

  const EncodedResults strided = encodeResults(VKQUERY_OCCLUSION, 0, 2, 2, 16, 8, 0, 1.0, 0);
  assert(strided.complete && strided.countWritten == 2 && strided.bytes.size() == 12);
  assert(readUnsigned(strided.bytes, 0, 4) == 1);
  assert(readUnsigned(strided.bytes, 4, 4) == 0);
  assert(readUnsigned(strided.bytes, 8, 4) == 1);

  const auto first = vkquery::timestampTicks(100, 2.0);
  const auto second = vkquery::timestampTicks(100, 2.0);
  assert(first == 50 && second > first);
  const auto timestamps = vkquery::encodeResults(VKQUERY_TIMESTAMP, 0, 2, 2, 32, 16,
      VKQUERY_64_BIT | VKQUERY_WITH_AVAILABILITY, 1.0, first);
  assert(timestamps.complete);
  assert(vkquery::readUnsigned(timestamps.bytes, 0, 8) > second);
  assert(vkquery::readUnsigned(timestamps.bytes, 8, 8) == 1);
  assert(vkquery::readUnsigned(timestamps.bytes, 16, 8) > vkquery::readUnsigned(timestamps.bytes, 0, 8));
  assert(vkquery::readUnsigned(timestamps.bytes, 24, 8) == 1);
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Wextra', '-Werror', '-I', join(root, 'source/vulkan'), source, '-o', binary]);
    execFileSync(binary);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('32-bit and 64-bit guest shims forward query-copy arguments in the shared ABI slots', () => {
  for (const arch of ['32', '64']) {
    const dir = mkdtempSync(join(tempRoot, `query-copy-${arch}-`));
    try {
      const shim = join(root, `tools/rootfs64/libvk${arch}/libvk${arch}.c`);
      const rewritten = readFileSync(shim, 'utf8').replace(
        /static inline uint64_t vk64_trap\(uint64_t fnId, VK64Args\* args\) \{[\s\S]*?\n\}/,
        `static uint64_t capturedFn; static VK64Args capturedArgs;
static inline uint64_t vk64_trap(uint64_t fnId, VK64Args* args) { capturedFn=fnId; if(args) capturedArgs=*args; return 0; }`
      );
      assert.notEqual(rewritten, readFileSync(shim, 'utf8'));
      const source = join(dir, 'query-copy.c');
      const binary = join(dir, 'query-copy');
      const check = `
int check_query_copy(void) {
  vkCmdCopyQueryPoolResults(1, 2, 3, 4, 5, 0x12345678ULL, 0x23456789ULL, 15);
  return !(capturedFn == 904 && capturedArgs.a[0] == 1 && capturedArgs.a[1] == 2 &&
    capturedArgs.a[2] == 3 && capturedArgs.a[3] == 4 && capturedArgs.a[4] == 5 &&
    capturedArgs.a[5] == 0x12345678ULL && capturedArgs.a[6] == 0x23456789ULL &&
    capturedArgs.a[7] == 15);
}
`;
      writeFileSync(source, rewritten + check + (arch === '32' ? `
void _start(void) { int result=check_query_copy();
  __asm__ volatile("int $0x80"::"a"(1),"b"(result):"memory"); __builtin_unreachable(); }
` : `
int main(void) { return check_query_copy(); }
`));
      const compiler = arch === '32' ? 'i686-linux-gnu-gcc' : 'cc';
      execFileSync(compiler, arch === '32'
        ? ['-O0', '-fno-builtin', '-nostdlib', '-static', '-fno-pie', '-fno-stack-protector', '-Wl,-e,_start', source, '-o', binary]
        : ['-O0', '-fno-builtin', source, '-o', binary]);
      if (arch === '32') execFileSync('qemu-i386', [binary]);
      else execFileSync(binary);
    } finally {
      rmSync(dir, { recursive: true, force: true });
    }
  }
});
