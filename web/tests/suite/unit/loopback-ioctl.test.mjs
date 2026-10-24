import { extractSourceRange } from '../lib/source-slice.mjs';
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('Wine loopback ioctls use guest queues and nonblocking state without a host fd', () => {
  const source = readFileSync('source/kernel/knativesocket.cpp', 'utf8');
  const start = source.indexOf('U32 KNativeSocketObject::ioctl(');
  const readEnd = source.indexOf('} else if (request == K_SIOCGIFCONF)', start);
  const nonblockStart = source.indexOf('} else if (request == 0x5421)', readEnd);
  const nonblockEnd = source.indexOf('} else if (request == K_SIOCGIFHWADDR)', nonblockStart);
  const method = extractSourceRange(source, start, readEnd) + extractSourceRange(source, nonblockStart, nonblockEnd) + '} return -25; }';
  const dir = mkdtempSync(join(tmpdir(), 'bw-loopback-ioctl-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <deque>
#include <vector>
#include <mutex>
#include <memory>
#include <sys/ioctl.h>
using U32=uint32_t;using U8=uint8_t;
#define LOG_SOCK(...)
#define IOCTL_ARG1 0
struct CPU {};
struct KMemory {U32 value=0;U32 readd(U32){return value;}void writed(U32,U32 v){value=v;}};
struct KThread {CPU* cpu=nullptr;KMemory* memory;};
struct KNativeSocketObject:std::enable_shared_from_this<KNativeSocketObject>{
 int nativeSocket=-1;bool loopbackStream=false,loopbackDatagram=false,loopbackStreamConnected=false,blocking=true;U32 loopbackPort=0,error=0;
 struct Packet{std::vector<U8> bytes;};std::deque<Packet> loopbackPackets;std::deque<U8> loopbackStreamBytes;
 std::mutex loopbackStreamMutex,loopbackPacketsMutex;U32 ioctl(KThread*,U32);
};
static U32 handleNativeSocketError(const std::shared_ptr<KNativeSocketObject>&,bool){return U32(-5);}
${method}
int main(){KMemory memory;KThread thread{nullptr,&memory};auto s=std::make_shared<KNativeSocketObject>();s->loopbackStream=true;s->loopbackStreamConnected=true;
 assert(s->ioctl(&thread,0x541b)==0 && memory.value==0);
 s->loopbackStreamBytes={1,2,3};assert(s->ioctl(&thread,0x541b)==0 && memory.value==3);assert(s->loopbackStreamBytes.size()==3);
 memory.value=1;assert(s->ioctl(&thread,0x5421)==0 && !s->blocking);
 memory.value=0;assert(s->ioctl(&thread,0x5421)==0 && s->blocking);
 s->loopbackStream=false;s->loopbackDatagram=true;s->loopbackPackets={{{1,2}},{{3,4,5}}};
 assert(s->ioctl(&thread,0x541b)==0 && memory.value==2);assert(s->loopbackPackets.size()==2);
 s->loopbackDatagram=false;assert(s->ioctl(&thread,0x541b)==U32(-5));
}
`);
    const build = spawnSync('c++', ['-std=c++20', '-pthread', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8' });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8' });
    assert.equal(run.status, 0, run.stderr);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
