import test from 'node:test';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('shared mapping replacement retains in-flight private backing pointers', () => {
  const header = readFileSync(new URL('../../../../include/kmemory64.h', import.meta.url), 'utf8');
  const start = header.indexOf('static inline bool bssBufDiagOn()');
  const end = header.indexOf('\nclass KMemory64', start);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-backing-lifetime-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <memory>
#include <cassert>
using U8 = uint8_t; using U32 = uint32_t;
#define K64_PAGE_SIZE 4096
void bssBufDiagLog(const char*, const void*, const void*) {}
${header.slice(start, end)}
int main() {
    U8 shared[K64_PAGE_SIZE] = {};
    K64Page page;
    U8* cached = page.commit();
    cached[0] = 7;
    // A TLB lookup or kernel futex reference can retain this pointer across
    // a sibling thread's mmap. Replacing the logical mapping must not free
    // the host buffer while an in-flight access can still reach it.
    page.adoptShared(shared);
    cached[0] = 9;
    assert(page.data == shared && page.data[0] == 0);
    assert(cached[0] == 9);
    return 0;
}
`);
    execFileSync('c++', ['-std=c++17', '-fsanitize=address', '-fno-omit-frame-pointer',
      join(dir, 'check.cpp'), '-o', join(dir, 'check')], { stdio: 'pipe', timeout: 30000 });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe', timeout: 10000,
      env: { ...process.env, ASAN_OPTIONS: 'detect_leaks=1' } });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('a pointer captured before remap cannot be cached with the newer generation', () => {
  const source = readFileSync(new URL('../../../../source/kernel/kmemory64.cpp', import.meta.url), 'utf8');
  const cache = source.slice(source.indexOf('struct K64DTlbEntry'), source.indexOf('inline void k64DTlbInvalidateAll()'));
  const dir = mkdtempSync(join(tmpdir(), 'bw64-cache-generation-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cstdint>
#include <atomic>
#include <cassert>
#include <type_traits>
using U8 = uint8_t; using U32 = uint32_t; using U64 = uint64_t;
${cache}
// Exercise the old signature too, so this regression fails before the fix.
template<class Function> void insert(Function f, const void* mem, U64 page, U8* data, U32 observed) {
    if constexpr (std::is_invocable_v<Function, const void*, U64, U8*, U32>) f(mem, page, data, observed);
    else f(mem, page, data);
}
int main() {
    U8 oldBacking[4096] = {}, newBacking[4096] = {};
    const void* memory = oldBacking;
    U32 observed = g_k64DTlbGen.load();
    // The caller captured oldBacking, then a sibling remapped the page.
    g_k64DTlbGen.fetch_add(1);
    insert(k64DTlbInsert, memory, 7, oldBacking, observed);
    assert(k64DTlbLookup(memory, 7) == nullptr);
    insert(k64DTlbInsert, memory, 7, newBacking, g_k64DTlbGen.load());
    assert(k64DTlbLookup(memory, 7) == newBacking);
}
`);
    execFileSync('c++', ['-std=c++17', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { stdio: 'pipe', timeout: 30000 });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe', timeout: 10000 });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
