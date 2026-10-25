import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XInternAtom creates atoms and honors only_if_exists', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const handler = src.match(/static void x11_InternAtom\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(handler);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-x11-intern-atom-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <map>
#include <string>
using U32=uint32_t;using BString=std::string;
struct KMemory { char name[64]{}; std::string readString(U32) { return name; } };
struct CPU { KMemory* memory; U32 args[4]{}; U32 eax=0; };
#define ARG1 cpu->args[1]
#define ARG2 cpu->args[2]
#define ARG3 cpu->args[3]
#define EAX cpu->eax
struct XServer { std::map<std::string,U32> atoms; U32 internAtom(const std::string& name, bool only) { auto it=atoms.find(name); if(it!=atoms.end())return it->second; if(only)return 0; U32 id=(U32)atoms.size()+1; atoms[name]=id; return id; } static XServer* getServer(){static XServer server;return &server;} };
void kpanic(const char*) { assert(false && "XInternAtom must not abort"); }
${handler}
int main(){ KMemory m; CPU c{&m}; std::strcpy(m.name,"WM_DELETE_WINDOW"); c.args[2]=4;c.args[3]=1; x11_InternAtom(&c); assert(c.eax==0); c.args[3]=0; x11_InternAtom(&c); assert(c.eax==1); c.args[3]=1; x11_InternAtom(&c); assert(c.eax==1); }
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
