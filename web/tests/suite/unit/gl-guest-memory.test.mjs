import test from 'node:test';
import assert from 'node:assert/strict';
import { existsSync, mkdtempSync, readFileSync, writeFileSync, rmSync } from 'node:fs';
import { execFileSync } from 'node:child_process';
import { tmpdir } from 'node:os';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const repoRoot = join(dirname(fileURLToPath(import.meta.url)), '..', '..', '..', '..');

const header = resolve(repoRoot, 'source/opengl/glguestmemory.h');
test('GL bridge preserves 32-bit pointer arrays and writes to the calling guest memory', () => {
  assert.ok(existsSync(header), 'shared GL guest-memory adapter is present');
  const dir = mkdtempSync(join(tmpdir(), 'gl-guest-memory-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
using U8=uint8_t; using U32=uint32_t; using U64=uint64_t;
struct KMemory {
 U8 bytes[128]{};
 void memcpyFromGuest(void* out,U64 p,U64 n){assert(p+n<=128);memcpy(out,bytes+p,n);}
 void memcpyToGuest(U64 p,const void* in,U64 n){assert(p+n<=128);memcpy(bytes+p,in,n);}
 void writed(U64 p,U32 v){memcpyToGuest(p,&v,4);}
 void writeq(U64 p,U64 v){memcpyToGuest(p,&v,8);}
};
struct KMemory64:KMemory {};
#include "${header}"
int main(){
 KMemory m32; KMemory64 m64;
 GLGuestMemory a(&m32),b(&m64);
 U32 p32[]={0x12345678,0xfedcba98,0xdeadbeef};
 U64 p64[]={0x123456789aULL,0xfedcba9876ULL};
 m32.memcpyToGuest(4,p32,sizeof(p32));m64.memcpyToGuest(8,p64,sizeof(p64));
 auto x=a.readPointers(4,2),y=b.readPointers(8,2);
 assert(x[0]==p32[0] && x[1]==p32[1]);assert(y[0]==p64[0] && y[1]==p64[1]);
 a.writed(32,0x11223344);b.writeq(32,0x5566778899ULL);
 U32 u=0;U64 v=0;a.memcpyFromGuest(&u,32,4);b.memcpyFromGuest(&v,32,8);
 assert(u==0x11223344 && v==0x5566778899ULL);
 assert(m32.bytes[36]==0 && m64.bytes[36]==0x55);
}
`);
    execFileSync('c++', ['-std=c++17', join(dir, 'check.cpp'), '-o', join(dir, 'check')]);
    execFileSync(join(dir, 'check'));
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('32-bit GL calls use the shared renderer and decode guest pointer arrays', () => {
  const source = readFileSync(join(repoRoot, 'source/opengl/gl64bridge.cpp'), 'utf8');
  const body = source.slice(source.indexOf('U64 gl32Bridge('));
  assert.match(body, /glBridge\(/, '32-bit entry point dispatches instead of returning a no-op');
  assert.match(source, /readPointers\(args\.a\[2\], count\)/, 'shader source pointers follow guest width');
  assert.match(source, /readPointers\(args\.a\[3\], n\)/, 'multi-draw index pointers follow guest width');
});
