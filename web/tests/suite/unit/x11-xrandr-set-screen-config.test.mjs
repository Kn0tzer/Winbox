import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const source = readFileSync(new URL('../../../../source/x11/x11common.cpp', import.meta.url), 'utf8');
const handler = extractSourceSlice(source,
  'static void x11_XRRSetScreenConfig(CPU* cpu) {',
  '// Status XRRSetScreenConfigAndRate(',
  { sourceName: 'source/x11/x11common.cpp' });

test('XRRSetScreenConfig switches to a valid advertised size and rejects an unknown size', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-xrandr-config-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
using U32 = uint32_t;
enum { RRSetConfigSuccess = 0, RRSetConfigFailed = 3, RR_Rotate_0 = 1 };
struct KMemory {};
struct KThread { KMemory* memory; struct Proc* process; };
struct CPU { KMemory* memory; KThread* thread; U32 args[12]{}; U32 eax=0; U32 peek32(int n) { return args[n]; } };
#define ARG1 cpu->peek32(1)
#define ARG4 cpu->peek32(4)
#define ARG5 cpu->peek32(5)
#define EAX cpu->eax
struct DisplayData {};
using DisplayDataPtr = DisplayData*;
struct XServer { U32 width=0, height=0; static XServer* getServer(){static XServer s;return &s;} DisplayDataPtr getDisplayDataByAddressOfDisplay(KMemory*,U32 p){return p==1?reinterpret_cast<DisplayDataPtr>(1):nullptr;} void changeScreen(U32 w,U32 h){width=w;height=h;} };
struct Proc {};
bool XrrGetSize(KThread*, const DisplayDataPtr&, U32 index, U32& w, U32& h) { if(index!=0) return false; w=1280; h=720; return true; }
${handler}
int main(){KMemory m;Proc p;KThread t{&m,&p};CPU c{};c.memory=&m;c.thread=&t;c.args[1]=1;c.args[4]=0;c.args[5]=RR_Rotate_0;
 x11_XRRSetScreenConfig(&c);assert(c.eax==RRSetConfigSuccess);assert(XServer::getServer()->width==1280 && XServer::getServer()->height==720);
 c.args[4]=1;x11_XRRSetScreenConfig(&c);assert(c.eax==RRSetConfigFailed);assert(XServer::getServer()->width==1280 && XServer::getServer()->height==720);
 c.args[1]=99;x11_XRRSetScreenConfig(&c);assert(c.eax==RRSetConfigFailed);c.args[1]=1;c.args[5]=2;x11_XRRSetScreenConfig(&c);assert(c.eax==RRSetConfigFailed);
}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
