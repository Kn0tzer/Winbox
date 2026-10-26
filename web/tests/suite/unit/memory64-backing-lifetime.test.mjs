import { extractSourceRange } from '../lib/source-slice.mjs';
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
#include <unordered_map>
#include <cassert>
using U8 = uint8_t; using U32 = uint32_t; using U64 = uint64_t;
#define K64_PAGE_SIZE 4096
void bssBufDiagLog(const char*, const void*, const void*) {}
${extractSourceRange(header, start, end)}
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
  const header = readFileSync(new URL('../../../../include/kmemory64.h', import.meta.url), 'utf8');
  const source = readFileSync(new URL('../../../../source/kernel/kmemory64.cpp', import.meta.url), 'utf8');
  const classStart = header.indexOf('class KMemory64 {');
  const entryStart = header.indexOf('namespace K64DTlb {', classStart);
  const lookupStart = header.indexOf('inline __attribute__((always_inline)) Entry* lookup', entryStart);
  const entryDecl = extractSourceRange(header, entryStart, lookupStart);
  const lookupEnd = header.indexOf('\n}', lookupStart) + 2;
  const lookup = extractSourceRange(header, lookupStart, lookupEnd);
  const insertStart = source.indexOf('inline void k64DTlbInsert(');
  const insertEnd = source.indexOf('\ninline void k64DTlbInvalidateAll()', insertStart);
  const insert = extractSourceRange(source, insertStart, insertEnd);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-cache-generation-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cstdint>
#include <atomic>
#include <cassert>
using U8 = uint8_t; using U32 = uint32_t; using U64 = uint64_t; using U64 = uint64_t;
class KMemory64 {};
${entryDecl}
std::atomic<U64> generation{1};
${lookup}
}
${insert}
int main() {
    U8 oldBacking[4096] = {}, newBacking[4096] = {};
    KMemory64 memory;
    U64 observed = K64DTlb::generation.load();
    // The caller captured oldBacking, then a sibling remapped the page.
    K64DTlb::generation.fetch_add(1);
    k64DTlbInsert(&memory, 7, oldBacking, 0, 0, observed);
    assert(K64DTlb::lookup(&memory, 7) == nullptr);
    k64DTlbInsert(&memory, 7, newBacking, 0, 0, K64DTlb::generation.load());
    assert(K64DTlb::lookup(&memory, 7)->data == newBacking);
}
`);
    execFileSync('c++', ['-std=c++17', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { stdio: 'pipe', timeout: 30000 });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe', timeout: 10000 });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
