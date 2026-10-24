import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('x64 nanosleep validates timespecs and passes relative duration to the guest thread', () => {
  const source = readFileSync('source/kernel/syscall64.cpp', 'utf8');
  const start = source.indexOf('static U64 sys_nanosleep64(');
  const end = source.indexOf('// clock_gettime(clk, struct timespec*)', start);
  assert.ok(start >= 0 && end > start, 'nanosleep64 helper should be present');
  const dir = mkdtempSync(join(tmpdir(), 'bw-nanosleep64-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <unordered_map>
using U64=uint64_t;using S64=int64_t;using U32=uint32_t;using S32=int32_t;using U8=uint8_t;
constexpr U32 K_EFAULT=14,K_EINVAL=22,K_EINTR=4,K_CONTINUE=0x7ffffffe;
constexpr U32 X64_CLOCK_REALTIME=0,X64_CLOCK_MONOTONIC=1,X64_CLOCK_BOOTTIME=7,X64_CLOCK_TAI=11,X64_TIMER_ABSTIME=1;
struct Memory {std::unordered_map<U64,U64> q;U64 readq(U64 a){return q[a];}void writed(U64,U32){}void writeq(U64,U64){}};
struct KThread {U64 slept=0;U32 nanoSleep(U64 n){slept=n;return 0;}};
struct CPU64 {Memory* memory;KThread* thread;};
struct KSystem {static U64 getSystemTimeAsMicroSeconds(){return 10'000'000;}static U32 getMilliesSinceStart(){return 20000;}};
${extractSourceRange(source, start,end)}
int main(){Memory m;KThread t;CPU64 c{&m,&t};
 m.q[16]=2;m.q[24]=500000000;assert(sys_nanosleep64(&c,16,0,0,0)==0&&t.slept==2500000000ULL);
 m.q[16]=1;m.q[24]=1000000000;assert((S64)sys_nanosleep64(&c,16,0,0,0)==-K_EINVAL);
 assert((S64)sys_nanosleep64(&c,0,0,0,0)==-K_EFAULT);
 m.q[16]=9;m.q[24]=0;assert(sys_nanosleep64(&c,16,1,X64_CLOCK_MONOTONIC,X64_TIMER_ABSTIME)==0&&t.slept==0);
 m.q[16]=10;m.q[24]=250000000;assert(sys_nanosleep64(&c,16,1,X64_CLOCK_REALTIME,X64_TIMER_ABSTIME)==0&&t.slept==250000000ULL);
 assert((S64)sys_nanosleep64(&c,16,1,99,0)==-K_EINVAL);
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

test('x64 getcpu reports the single emulated CPU and NUMA node', () => {
  const source = readFileSync('source/kernel/syscall64.cpp', 'utf8');
  const start = source.indexOf('static U64 sys_getcpu64(');
  const end = source.indexOf('// clock_gettime(clk, struct timespec*)', start);
  assert.ok(start >= 0 && end > start, 'getcpu64 helper should be present');
  assert.match(source, /#define X64_SYS_getcpu\s+309/);
  assert.match(source, /case X64_SYS_getcpu:\s*ret = sys_getcpu64/);
  const dir = mkdtempSync(join(tmpdir(), 'bw-getcpu64-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
using U64=uint64_t;using S64=int64_t;using U32=uint32_t;using U8=uint8_t;constexpr U32 K_EFAULT=14;
struct Memory {U32 cpu=99,node=99;void writed(U64 a,U32 v){if(a==4)cpu=v;else if(a==8)node=v;}};
struct CPU64 {Memory* memory;};
${extractSourceRange(source, start,end)}
int main(){Memory m;CPU64 c{&m};assert(sys_getcpu64(&c,4,8,1234)==0);assert(m.cpu==0&&m.node==0);assert(sys_getcpu64(&c,4,0,0)==0&&m.cpu==0);assert(sys_getcpu64(&c,0,0,0)==0);}
`);
    const build = spawnSync('c++', ['-std=c++20', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8' });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8' });
    assert.equal(run.status, 0, run.stderr);
  } finally {
    rmSync(dir, { recursive: true, force: true });
  }
});

test('x64 eventfd2 routes supported flags to the shared eventfd implementation', () => {
  const source = readFileSync('source/kernel/syscall64.cpp', 'utf8');
  assert.match(source, /#define X64_SYS_eventfd2\s+290/);
  assert.match(source, /case X64_SYS_eventfd2:[\s\S]{0,300}syscall_eventfd2\(cpu->thread, \(U32\)a1, \(U32\)a2\)/);
  assert.match(source, /case X64_SYS_sync_file_range:\s*\/\/ Our VFS writes are effectively synchronous/);
});
