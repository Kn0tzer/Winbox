// web/tests/suite/unit/x11-putpixel-logged.test.mjs — XPutPixel is still not
// implemented, so it must say so. It used to return without a trace.
//
// source/x11/x11common.cpp:x11_PutPixel was a commented-out kpanic and nothing
// else, so every XPutPixel dropped its pixel silently. It now logs once. It logs
// once and not per call, because a pixel loop would flood the log. The slice is
// compiled against stub klog and kpanic and run, so the test checks the real body.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/x11common.cpp';
const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const putPixel = extractSourceSlice(source, 'static void x11_PutPixel(CPU* cpu) {', 'static void x11_SubImage(CPU* cpu) {', { sourceName });

test('x11_PutPixel does not abort the emulator', () => {
  assert.doesNotMatch(putPixel, /kpanic\(/);
});

test('x11_PutPixel logs once, however many pixels are put', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-putpixel-logged-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cstdio>
struct CPU {};
static int logs = 0;
static int panics = 0;
static void klog(const char*) { ++logs; }
[[maybe_unused]] static void kpanic(const char*) { ++panics; }
${putPixel}
int main() {
  CPU cpu;
  for (int i = 0; i < 1000; i++) x11_PutPixel(&cpu);
  if (panics != 0) { std::fprintf(stderr, "panicked %d\\n", panics); return 1; }
  if (logs != 1) { std::fprintf(stderr, "logs %d\\n", logs); return 2; }
  return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
