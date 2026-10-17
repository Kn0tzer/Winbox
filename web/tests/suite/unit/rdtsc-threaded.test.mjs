import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';

test('32-bit RDTSC advances across threaded CPUs without instruction counting', () => {
  const root = new URL('../../../../', import.meta.url);
  const common = readFileSync(new URL('source/emulation/cpu/common/cpu.cpp', root), 'utf8');
  const normal = readFileSync(new URL('source/emulation/cpu/normal/normal_other.h', root), 'utf8');
  const extract = (s, name) => s.slice(s.indexOf(`void ${name}(`), s.indexOf('\n}', s.indexOf(`void ${name}(`)) + 2);
  const commonFn = extract(common, 'common_rdtsc');
  // Choose the non-MSVC implementation, which contains the portable clock.
  const normalFn = extract(normal.slice(normal.indexOf('#else', normal.indexOf('#include <intrin.h>'))), 'OPCALL normal_rdtsc');
  assert.ok(commonFn.includes('EDX') && normalFn.includes('EDX'));
  const dir = mkdtempSync(join(tmpdir(), 'rdtsc-threaded-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
using U32=uint32_t; using U64=uint64_t;
struct CPU { U32 eax=0,edx=0; U64 instructionCount=0,blockInstructionCount=0; };
struct DecodedOp { U32 imm=7; };
struct KSystem { static U64 ticks; static U64 getMicroCounter() { return ticks; } }; U64 KSystem::ticks=4294968;
#define EAX cpu->eax
#define EDX cpu->edx
#define OPCALL
#define START_OP(cpu,op)
#define NEXT() return
${commonFn}
${normalFn}
U64 value(CPU& c) {return ((U64)c.edx<<32)|c.eax;}
int main() {
 CPU first,second; DecodedOp op;
 common_rdtsc(&first,op.imm);
 normal_rdtsc(&second,&op);
#ifdef BOXEDWINE_MULTI_THREADED
 assert(value(first)==4294968000ULL && value(second)==value(first));
 KSystem::ticks++;
 normal_rdtsc(&second,&op);
 assert(value(second)==value(first)+1000 && second.edx==1);
 assert(first.instructionCount==0 && second.blockInstructionCount==0);
#else
 assert(value(first)==7 && value(second)==7);
#endif
}
`);
    for (const flags of [[], ['-DBOXEDWINE_MULTI_THREADED']]) {
      execFileSync('c++', ['-std=c++17', ...flags, join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
      execFileSync(join(dir, 'check'));
    }
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
