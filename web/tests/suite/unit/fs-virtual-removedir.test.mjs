// web/tests/suite/unit/fs-virtual-removedir.test.mjs — a guest rmdir on a virtual
// directory must fail with EPERM and log, never abort the emulator.
//
// source/io/fsvirtualnode.cpp:FsVirtualNode::removeDir was a kpanic, and
// KProcess::rmdir (kprocess.cpp) calls it directly, so any guest rmdir of a
// virtual directory stopped the emulator. The slice below is compiled against
// stub kpanic/klog and run, so the test checks the real function body.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/io/fsvirtualnode.cpp';
const source = readFileSync(new URL('../../../../source/io/fsvirtualnode.cpp', import.meta.url), 'utf8');
const removeDir = extractSourceSlice(source, 'U32 FsVirtualNode::removeDir() {', 'U32 FsVirtualNode::setTimes', { sourceName });

const kerror = readFileSync(new URL('../../../../include/kerror.h', import.meta.url), 'utf8');
const kEperm = Number(kerror.match(/#define\s+K_EPERM\s+(\d+)/)[1]);

test('virtual removeDir does not call kpanic', () => {
  assert.doesNotMatch(removeDir, /kpanic/);
});

test('virtual removeDir returns -EPERM and logs once', () => {
  const dir = mkdtempSync(join(tmpdir(), 'fs-virtual-removedir-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cstdint>
#include <cstdio>
using U32 = uint32_t;
using U64 = uint64_t;
#define K_EPERM ${kEperm}
static int panics = 0;
static int logs = 0;
[[maybe_unused]] static void kpanic(const char*) { ++panics; }
static void klog(const char*) { ++logs; }
struct FsVirtualNode {
  U32 removeDir();
  U32 setTimes(U64, U32, U64, U32);
};
U32 FsVirtualNode::setTimes(U64, U32, U64, U32) { return 0; }
${removeDir}
int main() {
  FsVirtualNode node;
  U32 result = node.removeDir();
  if (panics != 0) { std::fprintf(stderr, "panicked %d\\n", panics); return 1; }
  if (result != (U32)-K_EPERM) { std::fprintf(stderr, "result %u\\n", result); return 2; }
  if (logs != 1) { std::fprintf(stderr, "logs %d\\n", logs); return 3; }
  return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
