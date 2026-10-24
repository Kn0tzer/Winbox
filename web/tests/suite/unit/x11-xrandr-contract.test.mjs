import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const version = extractSourceSlice(source, 'static void x11_XRRQueryVersion(CPU* cpu) {', '// short* XRRRates', { sourceName: 'source/x11/x11common.cpp' });
const outputProperty = extractSourceSlice(source, 'static void x11_XRRGetOutputProperty(CPU* cpu) {', '// XRRScreenResources* XRRGetScreenResources', { sourceName: 'source/x11/x11common.cpp' });

test('XRandR advertises version 1.2 and absent output properties return an empty successful result', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-xrandr-contract-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <initializer_list>
using U32=uint32_t;enum{Success=0};
struct KMemory{U32 words[64]{};void writed(U32 a,U32 v){words[a/4]=v;}U32 readd(U32 a){return words[a/4];}};
struct CPU{KMemory*memory;U32 args[14]{};U32 eax=0;U32 peek32(int n){return args[n];}};
#define ARG2 cpu->peek32(2)
#define ARG3 cpu->peek32(3)
#define ARG9 cpu->peek32(9)
#define ARG10 cpu->peek32(10)
#define ARG11 cpu->peek32(11)
#define ARG12 cpu->peek32(12)
#define EAX cpu->eax
${version}
${outputProperty}
int main(){KMemory m;CPU c{};c.memory=&m;c.args[2]=4;c.args[3]=8;x11_XRRQueryVersion(&c);assert(c.eax==1&&m.readd(4)==1&&m.readd(8)==2);
 c.args[1]=99;c.args[9]=12;c.args[10]=16;c.args[11]=20;c.args[12]=24;c.args[13]=28;for(U32 a: {12u,16u,20u,24u,28u})m.writed(a,0xFFFFFFFF);x11_XRRGetOutputProperty(&c);assert(c.eax==Success);assert(m.readd(12)==0&&m.readd(16)==0&&m.readd(20)==0&&m.readd(24)==0&&m.readd(28)==0);}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
