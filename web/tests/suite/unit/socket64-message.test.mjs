import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, writeFileSync, mkdtempSync, rmSync } from 'node:fs';
import { spawnSync } from 'node:child_process';
import { join } from 'node:path';
import { tmpdir } from 'node:os';

test('x64 socket messages retain addresses and flags and bound truncated packet copies', () => {
  const source = readFileSync('source/kernel/syscall64.cpp', 'utf8');
  const start = source.indexOf('static U64 sys_sendmsg64(');
  const end = source.indexOf('// Map an x86-64 Linux syscall number', start);
  const dir = mkdtempSync(join(tmpdir(), 'bw-socket64-msg-'));
  try {
    writeFileSync(join(dir, 'check.cpp'), `
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include <string>
#include <memory>
#include <atomic>
#include <algorithm>
using U64=uint64_t;using S64=int64_t;using U32=uint32_t;using S32=int32_t;using U8=uint8_t;
constexpr U32 K_ENOSYS=38,K_EFAULT=14,K_EINVAL=22,K_SOL_SOCKET=1,K_SCM_RIGHTS=1;
#define klog_fmt(...)
#define MSG_SCRATCH_BYTES (64*1024)
#define MSG_SCRATCH_HDR 0
#define MSG_SCRATCH_IOV 32
#define MSG_SCRATCH_CMSG 64
#define MSG_SCRATCH_DATA 512
struct Memory {
 U8 bytes[131072]={};U32 dataReads=0;
 U32 readd(U64 a){assert(a+4<=sizeof(bytes));U32 v;std::memcpy(&v,bytes+a,4);return v;}
 U64 readq(U64 a){assert(a+8<=sizeof(bytes));U64 v;std::memcpy(&v,bytes+a,8);return v;}
 void writed(U64 a,U32 v){assert(a+4<=sizeof(bytes));std::memcpy(bytes+a,&v,4);}
 void writeq(U64 a,U64 v){assert(a+8<=sizeof(bytes));std::memcpy(bytes+a,&v,8);}
 void memcpy(void* out,U32 a,U32 n){assert(a+n<=sizeof(bytes));if(a>=4096+512&&a<4096+512+64)dataReads+=n;std::memcpy(out,bytes+a,n);}
 void memcpy(U32 a,const void* in,U32 n){assert(a+n<=sizeof(bytes));std::memcpy(bytes+a,in,n);}
 void memcpyFromGuest(void* out,U64 a,U64 n){assert(a+n<=sizeof(bytes));std::memcpy(out,bytes+a,n);}
 void memcpyToGuest(U64 a,const void* in,U64 n){assert(a+n<=sizeof(bytes));std::memcpy(bytes+a,in,n);}
};
using KMemory=Memory;
struct Exe{bool contains(const char*){return false;}};
struct Process{U32 id=10;Exe exe;};
struct KThread{Memory* memory;std::shared_ptr<Process> process=std::make_shared<Process>();U32 id=11;};
struct CPU64{KThread* thread;Memory* memory;};
static std::atomic<int> g_wsConcInflight{0};
bool wsConcEnabled(){return false;}bool wsReadEnabled(){return false;}
template<class... A>void crashRingRecordRead(A...){}
U32 msgScratch(KThread*){return 4096;}
U32 bounceSockaddrTo32(CPU64* c,U64 src,U32 len,U32*){assert(len<=256);if(src)c->thread->memory->memcpyToGuest(1024,c->memory->bytes+src,len);return 1024;}
U32 sentName=0,sentNameLength=0;
U32 ksendmsg(KThread* t,U32,U32 h,U32){sentName=t->memory->readd(h);sentNameLength=t->memory->readd(h+4);return 4;}
U32 krecvmsg(KThread* t,U32,U32 h,U32){auto m=t->memory;auto name=m->readd(h);if(name)m->writed(name,0x12340002);m->writed(h+4,16);m->writed(h+20,0);m->writed(h+24,0x20);m->writed(4096+512,0x64636261);return 65507;}
${source.slice(start, end)}
int main(){auto guest=std::make_unique<Memory>();auto scratch=std::make_unique<Memory>();KThread t{scratch.get()};CPU64 c{&t,guest.get()};
 // A maximum UDP packet can exceed the adapter's scratch capacity when
 // MSG_TRUNC requests its full length. Copy only bytes actually received.
 auto init=[&](){guest->writeq(0,256);guest->writed(8,16);guest->writeq(16,128);guest->writeq(24,1);guest->writeq(32,0);guest->writeq(40,0);guest->writed(48,0);guest->writeq(128,512);guest->writeq(136,4);guest->writed(256,0x12340002);};
 init();assert(sys_sendmsg64(&c,3,0,0)==4);assert(sentName&&sentNameLength==16&&scratch->readd(sentName)==0x12340002);
 init();guest->writeq(136,70000);guest->bytes[512+65024]=0xa5;assert(sys_recvmsg64(&c,3,0,0x20)==65507);assert(guest->readd(256)==0x12340002);assert(guest->readd(8)==16&&guest->readd(48)==0x20);assert(guest->readd(512)==0x64636261);assert(scratch->dataReads==65024&&guest->bytes[512+65024]==0xa5);
}
`);
    const build = spawnSync('c++', ['-std=c++20', '-pthread', join(dir, 'check.cpp'), '-o', join(dir, 'check')], { encoding: 'utf8' });
    assert.equal(build.status, 0, build.stderr);
    const run = spawnSync(join(dir, 'check'), [], { encoding: 'utf8' });
    assert.equal(run.status, 0, run.stderr);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
