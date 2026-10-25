import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XLookupString and XmbLookupString honor guest buffers and return key text/status', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const lookup = src.match(/static void x11_LookupString\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  const mbLookup = src.match(/static void x11_MbLookupString\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  const compose = src.match(/static bool x11IsDeadKey\(U32 keysym\) \{[\s\S]*?(?=\/\/ int XLookupString)/)?.[0];
  assert.ok(lookup); assert.ok(mbLookup);
  assert.doesNotMatch(lookup + mbLookup, /kpanic/);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-keyboard-text-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
using U32=uint32_t;using S32=int32_t;
constexpr U32 NumMask=1u<<4,ShiftMask=1u;
struct KMemory{unsigned char b[256]{};void memcpy(U32 a,const void*p,U32 n){std::memcpy(b+a,p,n);}void memcpy(void*p,U32 a,U32 n){std::memcpy(p,b+a,n);}void writed(U32 a,U32 v){std::memcpy(b+a,&v,4);}U32 readd(U32 a){U32 v;std::memcpy(&v,b+a,4);return v;}void writeb(U32 a,U32 v){b[a]=v;}};
struct CPU{KMemory* memory;U32 args[8]{};U32 eax=0;};
#define ARG1 cpu->args[1]
#define ARG2 cpu->args[2]
#define ARG3 cpu->args[3]
#define ARG4 cpu->args[4]
#define ARG5 cpu->args[5]
#define ARG6 cpu->args[6]
#define EAX cpu->eax
struct XKeyEvent{U32 type=2,keycode=8,state=0;void read(KMemory*m,U32 a){m->memcpy(this,a,sizeof(*this));}};
struct XKeyboard{static U32 keycodeToKeysym(U32 key,U32 level){return key==8?(level==1?'A':'a'):key==9?0xfe51:key==10?'e':0;}static U32 translate(U32& sym,U32 modifiers,char*out,U32 n){if(sym=='a'&&(modifiers&ShiftMask))sym='A';if(!sym)return 0;if(n)out[0]=(char)sym;return 1;}};
${compose}
${lookup}
${mbLookup}
int main(){KMemory m;CPU c{&m};XKeyEvent e{};m.memcpy(4,&e,sizeof(e));
 c.args[1]=4;c.args[2]=32;c.args[3]=1;c.args[4]=64;c.args[5]=80;x11_LookupString(&c);assert(c.eax==1&&m.b[32]=='a');U32 sym=0;m.memcpy(&sym,64,4);assert(sym=='a');
 c.args[3]=0;c.args[5]=88;x11_LookupString(&c);assert(c.eax==0);m.memcpy(&sym,64,4);assert(sym=='a');
 c.args[1]=1;c.args[2]=4;c.args[3]=32;c.args[4]=0;c.args[5]=64;c.args[6]=72;x11_MbLookupString(&c);assert(c.eax==0xffffffff);U32 status=0;m.memcpy(&status,72,4);assert(status==4);
 c.args[4]=4;x11_MbLookupString(&c);assert(c.eax==1&&m.b[32]=='a');
 // Compose acute followed by e using the caller's XComposeStatus storage.
 e.keycode=9;m.memcpy(4,&e,sizeof(e));c.args[1]=4;c.args[2]=32;c.args[3]=8;c.args[4]=64;c.args[5]=96;
 x11_LookupString(&c);assert(c.eax==0);m.memcpy(&sym,96,4);assert(sym==0xfe51);
 U32 matched=0;m.memcpy(&matched,100,4);assert(matched==1);
 e.keycode=10;m.memcpy(4,&e,sizeof(e));x11_LookupString(&c);assert(c.eax==2&&m.b[32]==0xc3&&m.b[33]==0xa9);
 m.memcpy(&matched,100,4);assert(matched==0);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('XIM unavailable path and mapping refresh are non-aborting', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const open = src.match(/static void x11_OpenIM\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  const refresh = src.match(/static void x11_RefreshKeyboardMapping\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  const create = src.match(/static void x11_CreateIC\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  assert.match(open, /EAX = 0/); assert.match(create, /EAX = 0/); assert.match(refresh, /EAX = 1/);
  assert.doesNotMatch(src.slice(src.indexOf('static void x11_VaCreateNestedList'), src.indexOf('// XImage* XCreateImage')), /kpanic\("x11_(?:VaCreateNestedList|UnsetICFocus|SetICFocus|DestroyIC|SetICValues|MbResetIC|CloseIM|CreateIC)/);
});
