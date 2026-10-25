import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('XIconifyWindow unmaps a live window and stores Iconic WM_STATE', () => {
  const src = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
  const handler = src.match(/static void x11_IconifyWindow\(CPU\* cpu\) \{[\s\S]*?\n\}/)?.[0];
  assert.ok(handler);
  const dir = mkdtempSync(join(tmpdir(), 'bw64-x11-iconify-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
using U32=uint32_t;using S32=int32_t;using U8=uint8_t;constexpr U32 BadWindow=3,WM_STATE=105,WM_STATE_TYPE=105,IconicState=3,Success=0,True=1,False=0;
struct XWMState{U32 state,icon;};struct CPU{U32 args[3]{};U32 eax=0;};
#define ARG1 cpu->args[1]
#define ARG2 cpu->args[2]
#define EAX cpu->eax
struct XWindow{bool unmapped=false;XWMState state{};int unmapWindow(){unmapped=true;return Success;}void setProperty(U32 a,U32 t,U32 f,U32 l,const U8*v,bool){assert(a==WM_STATE&&t==WM_STATE_TYPE&&f==32&&l==8);state=*(const XWMState*)v;}};
using XWindowPtr=XWindow*;using U8=uint8_t;struct XServer{XWindow window;static XServer*getServer(){static XServer s;return &s;}XWindow*getWindow(U32 id){return id==7?&window:nullptr;}};
${handler}
int main(){CPU c{};c.args[2]=7;x11_IconifyWindow(&c);auto&w=XServer::getServer()->window;assert(c.eax==1&&w.unmapped&&w.state.state==IconicState);c.args[2]=8;c.eax=0;x11_IconifyWindow(&c);assert(c.eax==BadWindow);}
`);
    execFileSync('c++', ['-std=c++17', '-Wall', '-Werror', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
