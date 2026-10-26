import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { execFileSync } from 'node:child_process';
import { extractSourceSlice } from '../lib/source-slice.mjs';

const source = readFileSync(new URL('../../../../source/x11/xrandr.cpp', import.meta.url), 'utf8');
const getSizes = extractSourceSlice(source, 'U32 XrrGetSizes(', '\n}', { sourceName: 'source/x11/xrandr.cpp' });

test('XRandR mode list contains the active nonstandard host size without truncating the list', () => {
  const dir = mkdtempSync(join(tmpdir(), 'x11-xrandr-sizes-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <utility>
#include <vector>
using U32=uint32_t;
struct XRRScreenSize{int32_t width,height,mwidth,mheight;};
struct KMemory{uint8_t bytes[8192]{};void writed(U32 a,U32 v){std::memcpy(bytes+a,&v,4);}U32 readd(U32 a){U32 v;std::memcpy(&v,bytes+a,4);return v;}};
struct KThread;struct KProcess{U32 next=512;U32 alloc(KThread*,U32 n){U32 a=next;next+=n;return a;}};
struct KThread{KMemory*memory;KProcess*process;};
struct XrrData{U32 sizesAddress=0,sizesCount=0,ratesAddress=0,eventWindow=0,eventMask=0;};
struct DisplayData{XrrData* xrrData=nullptr;};using DisplayDataPtr=DisplayData*;
struct KNativeSystem{static void getScreenDimensions(U32*w,U32*h){*w=1366;*h=768;}};
${getSizes}
}
int main(){KMemory m;KProcess p;KThread t{&m,&p};XrrData state;DisplayData d{&state};U32 countAddress=64;U32 a=XrrGetSizes(&t,&d,0,countAddress);assert(m.readd(countAddress)==4&&state.sizesCount==4);assert(m.readd(a)==1366&&m.readd(a+4)==768);assert(m.readd(a+16)==1024&&m.readd(a+20)==768);assert(m.readd(a+32)==800&&m.readd(a+36)==600);assert(m.readd(a+48)==640&&m.readd(a+52)==480);}
`);
    execFileSync('g++', ['-std=c++17', '-Wall', '-Werror', '-o', join(dir, 'check'), join(dir, 'check.cpp')], { stdio: 'pipe' });
    execFileSync(join(dir, 'check'), [], { stdio: 'pipe' });
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
