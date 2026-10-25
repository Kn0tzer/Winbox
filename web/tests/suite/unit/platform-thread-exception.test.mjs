// web/tests/suite/unit/platform-thread-exception.test.mjs — the Linux x64
// host-exception register sync no longer aborts the emulator on an
// unexpected cached-register value. The only regCache/xmmCache tables
// compiled under BOXEDWINE_64 (source/emulation/cpu/x32/jitX86CodeGen.cpp)
// hold exactly the handled values {0,1,2,3,10,5,6,7} / {0..7}, and 0xFF
// slots are skipped; anything else now logs and leaves that one guest
// register stale while the caller delivers the fault to the guest through
// the signal path (work item 15).
//
// syncFromException is compiled out of the real source and run against a
// stub ucontext; the CONTEXT_* macros are defined to the stub's fields the
// same way the real file defines them onto uc_mcontext.

import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const sourceName = 'platform/linux/platformThreads-x64.cpp';
const source = readFileSync(new URL('../../../../platform/linux/platformThreads-x64.cpp', import.meta.url), 'utf8');
const slice = extractSourceSlice(source, 'void syncFromException(CPU* cpu, ucontext_t* context) {', 'class InException {', { sourceName })
  .split('\n').filter(line => !line.startsWith('#')).join('\n');

test('syncFromException contains no abort', () => {
  assert.doesNotMatch(slice, /kpanic/);
  assert.match(slice, /unhandled cached host register/);
  assert.match(slice, /unhandled cached xmm register/);
});

test('register sync copies the compiled cache tables and degrades on unexpected values', () => {
  const dir = mkdtempSync(join(tmpdir(), 'platform-thread-exception-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
typedef uint8_t U8;
typedef uint32_t U32;
typedef uint64_t U64;
typedef int32_t S32;
static int warnings = 0;
static int panics = 0;
#define kwarn_fmt(...) (++warnings, 0)
#define kpanic(...) (++panics, 0)
struct Reg { U32 u32 = 0; };
struct CPU { Reg reg[8]; U8 xmm[8][16] = {}; };
struct ucontext_t {
    U64 gregs[16];
    U8 xmm_bytes[8][16];
};
#define CONTEXT_RAX gregs[0]
#define CONTEXT_RCX gregs[1]
#define CONTEXT_RDX gregs[2]
#define CONTEXT_RBX gregs[3]
#define CONTEXT_RBP gregs[5]
#define CONTEXT_RSI gregs[6]
#define CONTEXT_RDI gregs[7]
#define CONTEXT_R10 gregs[10]
#define CONTEXT_XMM0 xmm_bytes[0]
#define CONTEXT_XMM1 xmm_bytes[1]
#define CONTEXT_XMM2 xmm_bytes[2]
#define CONTEXT_XMM3 xmm_bytes[3]
#define CONTEXT_XMM4 xmm_bytes[4]
#define CONTEXT_XMM5 xmm_bytes[5]
#define CONTEXT_XMM6 xmm_bytes[6]
#define CONTEXT_XMM7 xmm_bytes[7]
U8 regCache[8];
U8 xmmCache[8];
${slice}
static void fillContext(ucontext_t& c) {
    for (int i = 0; i < 16; i++) c.gregs[i] = 0x1000 + i;
    for (int i = 0; i < 8; i++) for (int j = 0; j < 16; j++) c.xmm_bytes[i][j] = (U8)(0x40 + i);
}
int main() {
    // the tables as compiled under BOXEDWINE_64 sync completely, silently
    {
        memcpy(regCache, (const U8[]){0, 1, 2, 3, 10, 5, 6, 7}, 8);
        memcpy(xmmCache, (const U8[]){0, 1, 2, 3, 4, 5, 6, 7}, 8);
        ucontext_t c{}; fillContext(c);
        CPU cpu;
        syncFromException(&cpu, &c);
        for (int i = 0; i < 8; i++) assert(cpu.reg[i].u32 == (U32)(0x1000 + regCache[i]));
        for (int i = 0; i < 8; i++) assert(cpu.xmm[i][0] == (U8)(0x40 + xmmCache[i]));
        assert(warnings == 0 && panics == 0);
    }
    // an unexpected cached host register (8 = R8) leaves just that guest
    // register stale, logs once, and the rest still sync
    {
        memcpy(regCache, (const U8[]){0, 1, 8, 3, 10, 5, 6, 7}, 8);
        memcpy(xmmCache, (const U8[]){0, 1, 2, 9, 4, 5, 6, 7}, 8);
        ucontext_t c{}; fillContext(c);
        CPU cpu;
        cpu.reg[2].u32 = 0xdead;
        memset(cpu.xmm[3], 0xee, 16);
        syncFromException(&cpu, &c);
        assert(cpu.reg[2].u32 == 0xdead);
        assert(cpu.reg[0].u32 == 0x1000 && cpu.reg[7].u32 == 0x1007);
        // xmm registers are also copied unconditionally before the cache
        // loop, so the stale slot holds that direct copy (0x40 + 3), not a
        // value routed through the unexpected cache entry 9
        assert(cpu.xmm[3][0] == 0x43);
        assert(cpu.xmm[0][0] == 0x40);
        assert(warnings == 2 && panics == 0);
    }
    // 0xFF slots are the not-cached marker and are skipped silently
    {
        memcpy(regCache, (const U8[]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 8);
        memcpy(xmmCache, (const U8[]){0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff}, 8);
        ucontext_t c{}; fillContext(c);
        CPU cpu;
        const int before = warnings;
        syncFromException(&cpu, &c);
        assert(warnings == before && panics == 0);
    }
    return 0;
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
