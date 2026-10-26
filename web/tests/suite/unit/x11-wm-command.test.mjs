import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XSetWMProperties stores argv as a NUL-separated WM_COMMAND property', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const helper = src.match(/static void x11_SetWmCommand\([\s\S]*?\n}/)?.[0];
  assert.ok(helper);
  const setter = src.match(/static void x11_SetWMProperties\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  assert.match(setter, /x11_SetWmCommand\(thread, w, ARG5, \(S32\)ARG6\)/);
  assert.doesNotMatch(setter, /kpanic/);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-x11-wm-command-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
using U32=uint32_t; using S32=int32_t; using U8=uint8_t; using BString=std::string;
constexpr U32 XA_WM_COMMAND=34, XA_STRING=31;
struct KMemory { std::vector<std::string> strings; std::map<U32,U32> pointers; U32 readd(U32 a){return pointers[a];} BString readString(U32 a){return strings.at(a);} };
struct KThread { KMemory* memory; };
struct XWindow { U32 atom=0,type=0,format=0,length=0;std::string value;bool deleted=false;void setProperty(U32 a,U32 t,U32 f,U32 l,const U8*v,bool){atom=a;type=t;format=f;length=l;value.assign((const char*)v,l);}void deleteProperty(U32 a,bool){atom=a;deleted=true;} };
using XWindowPtr=XWindow*; struct XServer { static XServer* getServer(){static XServer s;return &s;} };
${helper}
int main(){KMemory m;m.strings={"game.exe","--fullscreen","map01"};m.pointers[100]=0;m.pointers[104]=1;m.pointers[108]=2;KThread t{&m};XWindow w;x11_SetWmCommand(&t,&w,100,3);assert(w.atom==XA_WM_COMMAND&&w.type==XA_STRING&&w.format==8);assert(w.value==std::string("game.exe\\0--fullscreen\\0map01\\0",28));assert(w.length==28);x11_SetWmCommand(&t,&w,100,0);assert(w.deleted&&w.atom==XA_WM_COMMAND);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
