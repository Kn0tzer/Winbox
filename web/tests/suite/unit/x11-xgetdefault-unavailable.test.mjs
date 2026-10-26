import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XGetDefault warns once when no X resource database is loaded and returns NULL', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const logger = src.match(/static void x11LogXrmUnavailable\(\) \{[\s\S]*?\n\}/)?.[0];
  const handler = src.match(/static void x11_GetDefault\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(logger); assert.ok(handler);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-x11-default-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
using U32=uint32_t;static U32 warnings=0;
void kwarn_fmt(const char*,...){warnings++;}void kpanic(const char*){assert(false && "XGetDefault must not abort");}
struct CPU{U32 eax=0;};
#define EAX cpu->eax
${logger}
${handler}
int main(){CPU c{};x11_GetDefault(&c);assert(c.eax==0&&warnings==1);c.eax=55;x11_GetDefault(&c);assert(c.eax==0&&warnings==1);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
