// web/tests/suite/unit/bstring-ctor.test.mjs — BString(const char*, bool)
// with literal=false must copy the string, not abort the emulator.
//
// source/util/bstring.cpp:BString::BString(const char*, bool) treated the
// non-literal case as a programming error and kpanic'd ("BString::BString
// oops"). Any caller that legitimately needs an owned copy through this
// constructor would stop the emulator. The constructor now allocates and
// copies, like the other owning constructors. The slices below compile the
// real allocator helpers and the real constructor and run them.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/util/bstring.cpp';
const source = readFileSync(new URL('../../../../source/util/bstring.cpp', import.meta.url), 'utf8');
const getNewString = extractSourceSlice(source, 'char* getNewString(int level) {', 'void releaseString(int level, char* str) {', { sourceName });
const powerOf2 = extractSourceSlice(source, 'int powerOf2(int requestedSize) {', 'void BStringData::decRefCount() {', { sourceName });
const ctor = extractSourceSlice(source, 'BString::BString(const char* str, bool literal) {', 'BString::BString() {', { sourceName });

test('BString non-literal constructor does not call kpanic', () => {
  assert.doesNotMatch(ctor, /kpanic/);
});

test('BString(str, false) owns a copy; BString(str, true) still borrows', () => {
  const dir = mkdtempSync(join(tmpdir(), 'bstring-ctor-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
using U32 = uint32_t;
#define SMALLEST_LEVEL 4
struct BStringData {
  char* str = nullptr;
  int len = 0;
  int level = 0;
  std::atomic_int refCount{0};
  void incRefCount() { refCount.fetch_add(1, std::memory_order_relaxed); }
};
static BStringData* allocNewData() { return new BStringData(); }
struct BString {
  BStringData* data;
  BString(const char* str, bool literal);
};
${getNewString}
${powerOf2}
${ctor}
int main() {
  char src[] = "hello world";
  BString s(src, false);
  if (s.data->len != 11) { std::fprintf(stderr, "len %d\\n", s.data->len); return 1; }
  if (s.data->level == 0) { std::fprintf(stderr, "non-literal owns no storage\\n"); return 2; }
  src[0] = 'J';
  if (std::strcmp(s.data->str, "hello world") != 0) { std::fprintf(stderr, "aliases source: %s\\n", s.data->str); return 3; }
  BString lit("static", true);
  if (lit.data->len != -1 || lit.data->level != 0) { std::fprintf(stderr, "literal path changed\\n"); return 4; }
  if (std::strcmp(lit.data->str, "static") != 0) { return 5; }
  BString emptyStr("", false);
  if (emptyStr.data->len != 0 || emptyStr.data->str[0] != 0) { std::fprintf(stderr, "empty copy wrong\\n"); return 6; }
  return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
