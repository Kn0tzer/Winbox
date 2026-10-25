import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XMatchVisualInfo returns the matching screen visual and False when no visual matches', () => {
  const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const body = source.match(/static void x11_MatchVisualInfo\(CPU\* cpu\) \{[\s\S]*?\n}/)?.[0];
  assert.ok(body);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-match-visual-info-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <functional>
using U32=uint32_t;using S32=int32_t;constexpr U32 True=1,False=0;
struct KMemory{U32 words[16]{};void writed(U32 a,U32 v){words[a/4]=v;}};
struct Depth{U32 depth;};struct Visual{S32 c_class;};struct KThread{};
struct XVisualInfo{U32 values[10]{};void set(S32 screen,U32 address,U32 depth,Visual* visual){values[0]=address;values[1]=(U32)screen;values[2]=depth;values[3]=(U32)visual->c_class;}void write(KMemory*m,U32 a){for(U32 i=0;i<4;i++)m->writed(a+4*i,values[i]);}};
struct CPU{KThread* thread;KMemory* memory;U32 args[8]{};U32 eax=0;U32 peek32(int i){return args[i];}};
#define ARG1 cpu->peek32(1)
#define ARG2 cpu->peek32(2)
#define ARG3 cpu->peek32(3)
#define ARG4 cpu->peek32(4)
#define ARG5 cpu->peek32(5)
#define EAX cpu->eax
struct Display{template<class F>static void iterateVisuals(KThread*,U32,F f){Depth a{24},b{16};Visual rgba{4},rgb{True};f(0,0x100,&a,&rgba);f(1,0x200,&b,&rgb);}};
${body}
int main(){KThread t;KMemory m;CPU c{&t,&m};c.args[1]=0;c.args[2]=1;c.args[3]=16;c.args[4]=1;c.args[5]=8;x11_MatchVisualInfo(&c);assert(c.eax==True);assert(m.words[2]==0x200&&m.words[3]==1&&m.words[4]==16&&m.words[5]==1);c.args[4]=9;x11_MatchVisualInfo(&c);assert(c.eax==False);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
