// web/tests/suite/unit/fs-opennode-async.test.mjs — fs open nodes must track
// O_ASYNC state faithfully, wake pollers whose requested readiness already
// holds, and refuse zip writes/truncates loudly instead of aborting.
//
// Round 3 of the UNOWNED burn-down (docs/known-gaps/coverage.md): the fs
// open-node classes (dir/file/mem/zip/virtual) implemented setAsync and
// waitForEvents as debug-log-only no-ops, and FsZipOpenNode::setLength /
// writeNative were kpanics. The compiled slices below run the real bodies
// against a stub condition and log counters, so the test checks behaviour,
// not text. The remaining classes share the same bodies by construction;
// source assertions pin that they delegate identically.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const read = (p) => readFileSync(new URL(`../../../../${p}`, import.meta.url), 'utf8');

const openNodeCpp = read('source/io/fsopennode.cpp');
const applyAsync = extractSourceSlice(openNodeCpp, 'void FsOpenNode::applyAsyncFlag(bool isAsync) {', 'U32 FsOpenNode::getDirectoryEntryCount()', { sourceName: 'source/io/fsopennode.cpp' });

const fileCpp = read('source/io/fsfileopennode.cpp');
const fileHooks = extractSourceSlice(fileCpp, 'void FsFileOpenNode::setAsync(bool isAsync) {', 'bool FsFileOpenNode::isWriteReady()', { sourceName: 'source/io/fsfileopennode.cpp' });

const zipCpp = read('source/io/fszipopennode.cpp');
const zipHooks = extractSourceSlice(zipCpp, 'void FsZipOpenNode::setAsync(bool isAsync) {', 'bool FsZipOpenNode::isWriteReady()', { sourceName: 'source/io/fszipopennode.cpp' });
const zipSetLength = extractSourceSlice(zipCpp, 'bool FsZipOpenNode::setLength(S64 len) {', 'S64 FsZipOpenNode::getFilePointer()', { sourceName: 'source/io/fszipopennode.cpp' });
const zipWrite = extractSourceSlice(zipCpp, 'U32 FsZipOpenNode::writeNative(U8* buffer, U32 len) {', 'void FsZipOpenNode::reopen()', { sourceName: 'source/io/fszipopennode.cpp' });

test('fs open nodes no longer carry debug-only async no-ops or zip kpanics', () => {
  for (const p of ['source/io/fsdiropennode.cpp', 'source/io/fsfileopennode.cpp', 'source/io/fsmemopennode.cpp', 'source/io/fszipopennode.cpp', 'source/io/fsvirtualopennode.h']) {
    assert.doesNotMatch(read(p), /not implemented/, p);
  }
  assert.doesNotMatch(zipCpp, /kpanic\("FsZipOpenNode/);
});

test('dir/mem/virtual async hooks delegate to the shared implementations', () => {
  for (const p of ['source/io/fsdiropennode.cpp', 'source/io/fsmemopennode.cpp']) {
    const src = read(p);
    assert.match(src, /this->applyAsyncFlag\(isAsync\);/, p);
    assert.match(src, /return this->asyncEnabled;/, p);
    assert.match(src, /BOXEDWINE_CONDITION_SIGNAL_ALL\(parentCondition\);/, p);
  }
  const virt = read('source/io/fsvirtualopennode.h');
  assert.match(virt, /this->applyAsyncFlag\(isAsync\);/);
  assert.match(virt, /return this->asyncEnabled;/);
  assert.match(virt, /BOXEDWINE_CONDITION_SIGNAL_ALL\(parentCondition\);/);
});

test('compiled fs hooks track O_ASYNC, wake ready pollers, and refuse zip writes loudly', () => {
  const dir = mkdtempSync(join(tmpdir(), 'fs-opennode-async-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cstdint>
#include <cstdio>
#include <atomic>
using U8 = uint8_t;
using U32 = uint32_t;
using S64 = int64_t;
#define K_POLLIN 0x001
#define K_POLLOUT 0x004
static int warns = 0;
static void kwarn(const char*) { ++warns; }
static int panics = 0;
[[maybe_unused]] static void kpanic(const char*) { ++panics; }
struct CondImpl { int signals = 0; void signalAll() { ++signals; } };
struct Cond { CondImpl* impl; CondImpl* operator->() const { return impl; } };
using BOXEDWINE_CONDITION = Cond;
#define BOXEDWINE_CONDITION_SIGNAL_ALL(cond) (cond)->signalAll()
struct FsOpenNode {
  bool asyncEnabled = false;
  void applyAsyncFlag(bool isAsync);
  virtual bool isReadReady() { return false; }
  virtual bool isWriteReady() { return false; }
  virtual ~FsOpenNode() = default;
};
${applyAsync}
struct FsFileOpenNode : FsOpenNode {
  bool readReady = false, writeReady = false;
  bool isReadReady() override { return readReady; }
  bool isWriteReady() override { return writeReady; }
  void setAsync(bool); bool isAsync(); void waitForEvents(BOXEDWINE_CONDITION&, U32);
};
${fileHooks}
struct FsZipOpenNode : FsOpenNode {
  bool readReady = true, writeReady = false;
  bool isReadReady() override { return readReady; }
  bool isWriteReady() override { return writeReady; }
  void setAsync(bool); bool isAsync(); void waitForEvents(BOXEDWINE_CONDITION&, U32);
  bool setLength(S64); U32 writeNative(U8*, U32);
};
${zipHooks}
${zipSetLength}
${zipWrite}
int main() {
  FsFileOpenNode f;
  if (f.isAsync()) { std::fprintf(stderr, "async on at construct\\n"); return 10; }
  f.setAsync(true);
  if (!f.isAsync()) { std::fprintf(stderr, "async not tracked\\n"); return 11; }
  if (warns != 1) { std::fprintf(stderr, "warns %d, want 1\\n", warns); return 12; }
  f.setAsync(true);
  if (warns != 1) { std::fprintf(stderr, "warning not rate-limited: %d\\n", warns); return 13; }
  f.setAsync(false);
  if (f.isAsync()) { std::fprintf(stderr, "async not cleared\\n"); return 14; }
  CondImpl i1; Cond c1{&i1}; f.readReady = true; f.writeReady = false;
  f.waitForEvents(c1, K_POLLIN);
  if (i1.signals != 1) { std::fprintf(stderr, "ready poller not woken\\n"); return 15; }
  CondImpl i2; Cond c2{&i2}; f.waitForEvents(c2, K_POLLOUT);
  if (i2.signals != 0) { std::fprintf(stderr, "unready poller woken\\n"); return 16; }
  f.writeReady = true;
  CondImpl i3; Cond c3{&i3}; f.waitForEvents(c3, K_POLLOUT);
  if (i3.signals != 1) { std::fprintf(stderr, "write-ready poller not woken\\n"); return 17; }
  CondImpl i4; Cond c4{&i4}; f.waitForEvents(c4, 0);
  if (i4.signals != 0) { std::fprintf(stderr, "deregistration woke a poller\\n"); return 18; }
  FsZipOpenNode z;
  int before = warns;
  if (z.setLength(10)) { std::fprintf(stderr, "zip setLength accepted\\n"); return 19; }
  if (warns != before + 1) { std::fprintf(stderr, "zip setLength silent\\n"); return 20; }
  U8 buf[4] = {};
  if (z.writeNative(buf, 4) != 0) { std::fprintf(stderr, "zip write accepted\\n"); return 21; }
  if (warns != before + 2) { std::fprintf(stderr, "zip write silent\\n"); return 22; }
  if (panics != 0) { std::fprintf(stderr, "panicked %d\\n", panics); return 23; }
  return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
