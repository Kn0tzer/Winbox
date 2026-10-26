import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('CPU frequency reader converts sysfs kHz to MHz and rejects invalid values', () => {
  const src = readFileSync(new URL('../../../../platform/linux/platform.cpp', import.meta.url), 'utf8');
  const helper = src.match(/static U32 readCpuFrequencyMHz\(const char\* path\) \{[\s\S]*?\n}/)?.[0];
  assert.ok(helper);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-cpufreq-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <climits>
using U32=uint32_t;
${helper}
int main(int argc,char**argv){assert(readCpuFrequencyMHz(argv[1])==2400);assert(readCpuFrequencyMHz(argv[2])==0);assert(readCpuFrequencyMHz(argv[3])==0);}
`);
    writeFileSync(join(dir, 'freq'), '2400000\n');
    writeFileSync(join(dir, 'bad'), 'not-a-frequency\n');
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'), [join(dir, 'freq'), join(dir, 'bad'), join(dir, 'missing')]);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('CPU MHz and cpufreq APIs use host readings with a browser nominal fallback', () => {
  const src = readFileSync(new URL('../../../../platform/linux/platform.cpp', import.meta.url), 'utf8');
  assert.match(src, /Platform::getCpuFreqMHz\(\)[\s\S]*?cpuFrequencyFromSysfs\(0, "cpuinfo_max_freq"\)[\s\S]*?readProcCpuFrequencyMHz\(\)[\s\S]*?#ifdef __EMSCRIPTEN__[\s\S]*?mhz = 1000/);
  assert.match(src, /Platform::getCpuCurScalingFreqMHz\(U32 cpuIndex\)[\s\S]*?"scaling_cur_freq"/);
  assert.match(src, /Platform::getCpuMaxScalingFreqMHz\(U32 cpuIndex\)[\s\S]*?"scaling_max_freq"/);
});
