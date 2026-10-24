// web/tests/suite/unit/x11-freefont.test.mjs — XFreeFont succeeds instead of
// aborting the emulator.
//
// source/x11/x11common.cpp:x11_FreeFont was a kpanic. The legacy path never loads
// a font, so there is nothing to close and the call must report Success without
// touching guest memory. The handler is compiled against a stub CPU and run.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'source/x11/x11common.cpp';
const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const freeFont = extractSourceSlice(source, 'static void x11_FreeFont(CPU* cpu) {', '// int XMoveResizeWindow(', { sourceName });

test('x11_FreeFont does not abort the emulator', () => {
  assert.doesNotMatch(freeFont, /kpanic\(/);
});

test('x11_FreeFont reports Success and writes nothing', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-freefont-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
typedef int32_t S32;
typedef uint32_t U32;
enum { Success = 0 };
// the emulator stops on kpanic; the stub stops the test the same way
[[noreturn, maybe_unused]] static void kpanic(const char* msg) { std::fprintf(stderr, "kpanic: %s\\n", msg); std::exit(3); }
struct CPU { U32 eax; };
#define EAX (cpu->eax)
${freeFont}
int main() {
    CPU cpu{};
    cpu.eax = 99;
    x11_FreeFont(&cpu);
    assert(cpu.eax == Success);
    return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
