import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('virtual node stores timestamps in milliseconds and preserves UTIME_OMIT fields', () => {
  const src = readFileSync(new URL('../../../../source/io/fsvirtualnode.cpp', import.meta.url), 'utf8');
  const modified = src.match(/U64 FsVirtualNode::lastModified\(\) \{[\s\S]*?\n}/)?.[0];
  const setTimes = src.match(/U32 FsVirtualNode::setTimes\([\s\S]*?\n}/)?.[0];
  assert.ok(modified); assert.ok(setTimes);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-virtual-settimes-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
using U32=uint32_t;using U64=uint64_t;
struct FsVirtualNode{U64 accessTimeMs=0,modifiedTimeMs=0;U64 lastModified();U32 setTimes(U64,U32,U64,U32);};
${modified}
${setTimes}
int main(){FsVirtualNode n;assert(n.setTimes(10,123456789,20,987654321)==0);assert(n.accessTimeMs==10123&&n.lastModified()==20987);
 n.setTimes(0,0x3ffffffe,30,500000000);assert(n.accessTimeMs==10123&&n.lastModified()==30500);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
