// web/tests/suite/unit/bstring-remove.test.mjs — BString::remove(int, int)
// actually removes the span. It was a no-op (the body only called
// makeWritable), which silently broke FsNode::loadChildren: host files
// named *.mixed kept their suffix when registered for the guest, because
// localPath.remove(length - 6) did nothing.
//
// The allocator block, length(), makeWritable() and remove() are compiled
// out of the real source and run against a stub BString shell that shares
// its data header layout with the real class.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/util/bstring.cpp';
const source = readFileSync(new URL('../../../../source/util/bstring.cpp', import.meta.url), 'utf8');

test('BString::remove deletes the requested span and keeps the string valid', () => {
  const dir = mkdtempSync(join(tmpdir(), 'bstring-remove-'));
  try {
    const alloc = extractSourceSlice(source, 'class BStringData {', 'template <typename T>' + String.fromCharCode(10) + 'void appendData', { sourceName });
    const length = extractSourceSlice(source, 'int BString::length() const {', 'void BString::remove(int index, int len) {', { sourceName });
    const remove = extractSourceSlice(source, 'void BString::remove(int index, int len) {', 'void BString::removeAll() {', { sourceName });
    const makeWritable = extractSourceSlice(source, 'void BString::makeWritable(int len) {', 'BString BString::stringByApppendingPath', { sourceName });
    writeFileSync(join(dir, 'check.cpp'), `
#include <atomic>
#include <cassert>
#include <cstring>
#include <string>
typedef uint32_t U32;
${alloc}
class BString {
public:
    BStringData* data;
    BString(const char* s) {
        data = allocNewData();
        data->len = (int)strlen(s);
        data->level = powerOf2(data->len + 1);
        data->str = getNewString(data->level);
        memcpy(data->str, s, data->len + 1);
        data->incRefCount();
    }
    BString(const BString& o) : data(o.data) { data->incRefCount(); }
    ~BString() { data->decRefCount(); }
    int length() const;
    void remove(int index, int len = -1);
    void makeWritable(int len);
    std::string str() const { return std::string(data->str, (size_t)length()); }
};
${length}
${makeWritable}
${remove}
int main() {
    // the FsNode::loadChildren case: strip a 6-char suffix
    { BString s("game.mixed"); s.remove(s.length() - 6); assert(s.str() == "game"); assert(s.length() == 4); }
    // a middle span
    { BString s("abcdef"); s.remove(1, 2); assert(s.str() == "adef"); }
    // an overrunning length clamps to the end
    { BString s("abcdef"); s.remove(4, 100); assert(s.str() == "abcd"); }
    // an index at or past the end, and a zero-length span, change nothing
    { BString s("abc"); s.remove(3); assert(s.str() == "abc"); s.remove(9, 2); assert(s.str() == "abc"); s.remove(1, 0); assert(s.str() == "abc"); }
    // a negative index starts at 0
    { BString s("abcdef"); s.remove(-2, 2); assert(s.str() == "cdef"); }
    // removing from a shared string must not disturb the other owner
    { BString a("shared.mixed"); BString b = a; b.remove(b.length() - 6); assert(b.str() == "shared"); assert(a.str() == "shared.mixed"); }
    return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
