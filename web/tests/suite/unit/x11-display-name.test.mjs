import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XDisplayName returns an explicit name or the emulator display default', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const handler = src.match(/static void x11_DisplayName\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(handler);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-x11-display-name-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <string>
using U32=uint32_t;using BString=std::string;
#define ARG1 cpu->args[1]
#define EAX cpu->eax
#define B(v) BString(v)
struct KThread; struct KProcess{BString allocated;U32 createString(KThread*,const BString&s){allocated=s;return 0x4455;}};
struct KThread{KProcess* process;};struct CPU{U32 args[2]{};U32 eax=0;KThread* thread;};
void kpanic(const char*){assert(false && "XDisplayName must not abort");}
${handler}
int main(){KProcess p;KThread t{&p};CPU c{};c.thread=&t;c.args[1]=0;x11_DisplayName(&c);assert(c.eax==0x4455&&p.allocated==":0");c.args[1]=0x7788;x11_DisplayName(&c);assert(c.eax==0x7788);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
