import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XI2 mask decoder accepts 33 protocol bits and rejects out-of-range selections', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const helper = src.match(/static bool x11DecodeXIEventMask\([\s\S]*?\n}/)?.[0];
  assert.ok(helper);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-xi-mask-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
using U8=uint8_t;using U32=uint32_t;using U64=uint64_t;
#define XI_LASTEVENT 32
${helper}
int main(){U64 value=0;U8 raw[]={0,0,0,0,1};assert(x11DecodeXIEventMask(raw,5,value));assert(value==(U64(1)<<32));
 U8 supported[]={0,0,0,0,2};assert(!x11DecodeXIEventMask(supported,5,value));
 U8 padded[]={1,0,0,0,0,0,0,0,0,0};assert(x11DecodeXIEventMask(padded,10,value)&&value==1);
 U8 invalid[]={1,0,0,0,0,0,0,0,1};assert(!x11DecodeXIEventMask(invalid,9,value));}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('Xcursor image loading builds a host cursor and XI masks no longer abort on the fifth byte', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const load = src.match(/void x11_CursorImageLoadCursor\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  const select = src.match(/static void x11_XISelectEvents\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  assert.ok(load); assert.ok(select);
  assert.doesNotMatch(load + select, /kpanic/);
  assert.match(load, /buildCursor\(thread, cursor, image\.pixelsAddress/);
  assert.match(select, /x11DecodeXIEventMask\(bytes\.data\(\), bytes\.size\(\), value\)/);
});
