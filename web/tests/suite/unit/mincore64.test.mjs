import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { dirname, join } from 'node:path';
import { tmpdir } from 'node:os';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

test('x64 mincore reports committed pages and validates both ranges before writing', () => {
  const source = readFileSync(join(repoRoot, 'source/kernel/syscall64.cpp'), 'utf8');
  const start = source.indexOf('static U64 sys_mincore64(');
  const end = source.indexOf('// Map an x86-64 Linux syscall number', start);
  assert.ok(start >= 0 && end > start, 'mincore64 helper should be present');
  assert.match(source, /#define X64_SYS_mincore\s+27/);
  assert.match(source, /case X64_SYS_mincore:\s*ret = sys_mincore64/);

  const dir = mkdtempSync(join(tmpdir(), 'bw-mincore64-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <unordered_map>
using U64=uint64_t;using S64=int64_t;using U32=uint32_t;using U8=uint8_t;
constexpr U64 K64_PAGE_MASK=0xfff;constexpr U64 K64_PAGE_SIZE=4096;
constexpr U32 K_EINVAL=22,K_EFAULT=14,K_ENOMEM=12;
struct Memory {
 std::unordered_map<U64,bool> mapped, committed;
 U8 out[64]{};
 bool isPageMapped(U64 p){return mapped[p];}
 U8* getCommittedPagePtr(U64 p){return committed[p]?reinterpret_cast<U8*>(uintptr_t(1)):nullptr;}
 void writeb(U64 a,U8 v){assert(a<sizeof(out));out[a]=v;}
};
struct CPU64{Memory* memory;};
${extractSourceRange(source, start, end)}
int main(){Memory m;CPU64 c{&m};
 m.mapped[4]=m.mapped[5]=m.mapped[0]=true;m.committed[4]=true;
 assert(sys_mincore64(&c,0x4000,4097,0)==0);assert(m.out[0]==1&&m.out[1]==0);
 m.out[0]=0x7f;assert((S64)sys_mincore64(&c,0x4001,1,0)==-K_EINVAL&&m.out[0]==0x7f);
 assert((S64)sys_mincore64(&c,0x4000,8193,0)==-K_ENOMEM&&m.out[0]==0x7f);
 m.mapped.erase(0);assert((S64)sys_mincore64(&c,0x4000,4096,0)==-K_EFAULT&&m.out[0]==0x7f);
}
`);
    const build = spawnSync('c++', ['-std=c++20', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8' });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8' });
    assert.equal(run.status, 0, run.stderr);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});
