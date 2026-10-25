import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XSetLocaleModifiers returns a valid empty string and rejects unsupported modifiers loudly', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const handler = src.match(/static void x11_SetLocaleModifiers\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(handler);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-x11-locale-modifiers-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <string>
using U32=uint32_t;using BString=std::string;static U32 warnings=0;
void kwarn_fmt(const char*,...){warnings++;}
#define ARG1 cpu->args[1]
#define EAX cpu->eax
#define B(v) BString(v)
struct KMemory{BString value;BString readString(U32){return value;}};struct KThread;struct KProcess{BString allocated;U32 createString(KThread*,const BString&s){allocated=s;return 0x3344;}};struct KThread{KMemory*memory;KProcess*process;};struct CPU{U32 args[2]{};U32 eax=0;KThread*thread;KMemory*memory;};
${handler}
int main(){KMemory m;KProcess p;KThread t{&m,&p};CPU c{};c.thread=&t;c.memory=&m;c.args[1]=0;x11_SetLocaleModifiers(&c);assert(c.eax==0x3344&&p.allocated.empty());c.args[1]=88;m.value="";x11_SetLocaleModifiers(&c);assert(c.eax==88&&warnings==0);c.args[1]=99;m.value="@im=ibus";x11_SetLocaleModifiers(&c);assert(c.eax==0&&warnings==1);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
