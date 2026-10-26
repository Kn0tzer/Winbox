import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XIM absence is reported once and LocaleOfIM returns safely', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const logger = src.match(/static void x11LogImeUnavailable\(\) \{[\s\S]*?\n\}/)?.[0];
  const open = src.match(/static void x11_OpenIM\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  const locale = src.match(/static void x11_LocaleOfIM\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(logger); assert.ok(open); assert.ok(locale);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-x11-ime-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
using U32=uint32_t;static U32 warnings=0;
void kwarn_fmt(const char*,...){warnings++;}void kpanic(const char*){assert(false && "missing XIM must not abort");}
struct CPU{U32 args[2]{};U32 eax=0;};
#define ARG1 cpu->args[1]
#define EAX cpu->eax
${logger}
${open}
${locale}
int main(){CPU c{};x11_OpenIM(&c);assert(c.eax==0);x11_LocaleOfIM(&c);assert(c.eax==0);x11_LocaleOfIM(&c);assert(c.eax==0&&warnings==1);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
